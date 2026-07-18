// Scene/SceneManager_Render.cpp -- SceneManager::Render (cSceneMgr_Render 0x517CB0): per-frame
// scene dispatch draw. Split out of SceneManager.cpp (mechanical relocation, same class); see
// SceneManager.cpp for lifecycle/scene-change and SceneManager_Update.cpp for the per-frame tick.
#include "UI/LoginScene.h"      // pulls in Net/NetSystem.h (winsock2) first
#include "UI/GameHud.h"
#include "UI/GameWindows.h"
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"
#include "Net/NetSystem.h"
#include "Scene/SceneManager.h"
#include "Scene/WorldRenderer.h"
#include "Gfx/WorldGeometryRenderer.h" // static .WO geometry (distinct from WorldRenderer=entities)
#include "Gfx/GxdRenderer.h"           // GXD_RenderPostBlur 0x4053E0 (bloom, wired at end of InGame render) + genuine default Light()
#include "Gfx/EnvLightingFog.h"        // B5: per-frame sun/fog applicator (Env_UpdateSunLight 0x412210 / Env_UpdateFogState 0x412370)
#include "Game/PlayerAnimCursorTick.h" // Player_AdvanceAnimCursor (advances player anim cursor, switch 0x5727BF)
#include "Gfx/FxSetters.h"             // FxPool_* + FxSlot (combat FX pool dword_17D06F4, Wave D)
#include "Gfx/FxBillboard.h"           // FxBillboard_PoolTick/SetDevice (Object A leaf .PARTICLE, Wave D)
#include "Gfx/ModelObjectRenderer.h"   // model-object mesh renderer (ModelObj_Draw 0x4D71B0, combat FX mesh, Wave F)
#include <cstring>                     // std::memcpy (reads race/gender from PlayerEntity::body)
#include "World/WorldIntegration.h"    // world::WorldAssets (actually loads Z%03d.WO)
#include "World/WorldMap.h"            // world::WorldMap::LoadZoneResource / ZoneIdToFileId
#include "Audio/AudioSystem.h"        // audio::BgmChannel (scene BGM slot, cSceneMgr +612)
#include "Config/GameOptions.h"       // config::g_Options.BgmEnabled (g_BgmEnabled 0x84DEF0)
#include "Gfx/SpriteBatch.h"    // gfx::g_GameTimeSec
#include "Game/GameState.h"     // game::g_World (zoneId)
#include "Game/ClientRuntime.h" // game::Str (EnterWorld error messages)
#include "Game/MiscManagers.h"  // game::Cursors() / kCursorDefault (mPOINTER 0x8E714C, cursor reset)
#include "Game/MapWarp.h"       // game::kSelfActionStateOffset (InGame step-12 gating gate)
#include "Net/SendPackets.h"    // Net_SendPacket_Op13 (keepalive) / Net_SendOp64 (clan/faction poll request)
#include "Net/CharSelectPackets.h" // net::BuildEnterWorldTail72 (confirmed tail72 block)
// 4 auxiliary InGame-tick systems (2026-07-14 wiring mission, cf. dedicated agent reports):
// anim/collision, entity lifecycle, camera/warp/potion/guild, combo/pickup/quest. Wired below
// in case Scene::InGame (RunMainTick).
#include "Game/AnimationTick.h"
#include "Game/EntityLifecycleTick.h"
#include "Game/CameraWarpTick.h"
#include "Game/ComboPickupTick.h"
#include "Game/MotionPools.h"    // game::LoadedCoordTable / kCoordTableRow* -- GINFO-003 table
                                  // (mZONEMOVEINFO 350x805 FLOAT, original base flt_1555D08)
#include "Game/NpcInteraction.h" // NpcInteractionSystem::AutoInteractForPet (already ported, reused)
// 3 further systems wired in this same block (2026-07-14 wiring mission, continuation of the 4
// above): ground effects/auras/zone objects, auto-target/combat gate, 3rd-person camera bridge.
#include "Game/GroundAuraWorldObjectTick.h"
#include "Game/AutoTargetCombatGate.h"
#include "Gfx/CameraThirdPersonBridge.h"
#include "Net/NetClient.h"       // net::GlobalNetClient / NetCloseSocket (SCN-01: notice OK action)
#include "Core/Log.h"
#include <windows.h>
#include <cstring>
#include <cstdio>   // std::snprintf (BGM path "Z%03d.BGM")
#include <cstdint>
#include <vector>   // std::vector (GINFO-003 combo candidates, cf. ComboCandidateLookup)

namespace ts2 {

void SceneManager::Render(IDirect3DDevice9* /*device*/, const gfx::Camera& camera) {
    switch (scene_) {
    case Scene::Intro:
        // Scene_IntroRender 0x518880 (UI/IntroRender.h): logos scrolled from the REAL
        // 001_00799..831.IMG files (UI atlas), centered on their real size. Delegated to
        // LoginScene, which reuses its already-created GPU resources (cf.
        // LoginScene::RenderIntro) -- introState_ stays fully driven here (Update, case
        // Scene::Intro).
        if (login_) login_->RenderIntro(introState_);
        break;
    case Scene::ServerSelect:
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) login_->Render(scene_);
        break;
    case Scene::EnterWorld:
        // Scene_EnterWorldRender 0x52C260 (UI/EnterWorldRender.h): CharSelect->InGame
        // transition screen (zone background 008_%05d.IMG + progress bar). Delegated to
        // LoginScene (same GPU resources as Scene::Intro above). Without this case, the
        // EnterWorld scene fell through to `default:` -> black screen for the whole load.
        // enterWorldState_ stays driven by SceneManager::Update() (case Scene::EnterWorld);
        // zoneId = game::g_World.zoneId (same value as EnterWorldFlow_Update).
        if (login_) login_->RenderEnterWorld(enterWorldState_, game::g_World.zoneId);
        break;
    case Scene::InGame: {
        // --- GATE g_SceneSubState (Scene_InGameRender 0x52D0B0, v78 = *(a1+4) @0x52D0E4) ---
        // The binary picks its render path from g_SceneSubState (= inGameTickState_, cf.
        // SceneManager_Update.cpp: Setup=0/WaitFirstSpawn=1/Failed=2/InitCamera=3/MainTick=4):
        //   <=1 (loading): EMPTY SCREEN (BeginFrame + Present, no world; disasm branches lines
        //        137-148). The caller's BeginFrame has already cleared -> nothing to draw.
        //   ==2 (Failed / timeout notice): 2D DIALOGS ONLY (Gfx_Begin2D + UI_RenderAllDialogs
        //        0x5AE2D0 + Gfx_End2D; lines 149-163). Neither world nor HUD.
        //   >=3 (InitCamera/MainTick): FULL WORLD FRAME (below).
        // Without this gate, the C++ code drew the world unconditionally during zone spooling
        // and the timeout notice (unfaithful). The normal harness/flow reaches sub>=3 as soon
        // as players[0] (self) is active -> world rendered.
        const int inGameSub = g_SceneSubState;
        if (inGameSub <= 1) break;                        // empty loading screen (@0x52D0E4)
        if (inGameSub == 2) {                             // dialogs only (@lines 149-163)
            if (windowsReady_ && windows_) windows_->Render(); // UI_RenderAllDialogs 0x5AE2D0
            break;
        }
        // inGameSub >= 3: full world frame.
        // FIXED ORDER: the minimal SilverLining layer is called at two points, like the
        // original binary:
        //   1) before the terrain/decor (Env_UpdateFrame -> cAtmosphere_RenderFrame),
        //   2) after the entities (Env_StepTimeOfDay -> Atmosphere_DrawFrame).
        // Here we apply it around the world render: sky -> .WO decor -> entities -> closing
        // sky -> HUD -> windows.
        if (worldGeomReady_ && worldGeom_) worldGeom_->RenderSky(screenW_, screenH_);
        // B5 -- EnvLightingFog: directional sun light + per-frame fog in-game.
        // LIVE ANCHOR (verified in IDA this mission): Scene_InGameRender 0x52D0B0 calls
        // Env_UpdateFrame 0x412550 @0x52D30D (`mov ecx, offset g_WorldEnv ; call Env_UpdateFrame`),
        // ONCE PER FRAME in-game (xrefs_to 0x412550 = 1 single caller, this site) -> the chain
        // really is LIVE. Env_UpdateFrame chains: Env_UpdateSkyMatrix 0x412190 ->
        // cAtmosphere_RenderFrame 0x793B80 (SilverLining sky render) -> Env_UpdateSunLight
        // 0x412210 (SetLight(0) vtbl+204 @0x412367) -> Env_UpdateFogState 0x412370 (RS 28/34/35/38
        // via g_GxdRenderer_pDevice 0x18C5104).
        //
        // WHY NO "ORIGINAL" SUN/FOG HERE (documented neutral fallback, not reanimated dead
        // code): in the binary the COLORS/DIRECTION come EXCLUSIVELY from the SilverLining SDK,
        // absent from this repo:
        //   Env_UpdateSunLight -> cAtmosphere_GetSunDirectionA 0x7904D0 (direction),
        //                         cAtmosphere_GetSunColorFaded 0x7938A0 (diffuse),
        //                         cAtmosphere_GetHorizonColorFaded 0x793AB0 (ambient), on *(g_WorldEnv+8);
        //   Env_UpdateFogState -> cAtmosphere_GetSunColorFaded/IsSunUp 0x790AF0/GetSunColor 0x790B00/
        //                         GetColorAtDirection 0x790A20/GetColorBasePtr 0x7042B0 (density = colorBase.z/8435).
        // Outside TS2_SILVERLINING_ENGINE_AVAILABLE, *(g_WorldEnv+8) is null -> none of these
        // values exist. So we apply the bit-exact proven device MECHANICS, driven by the
        // GENUINE source already available: GxdRenderer::Light() = light_
        // (BuildDefaultMaterialAndLight @0x402711: Diffuse 0.7 / Ambient 0.3 /
        // Dir normalize(-1,-1,1)), and FOG OFF (no fog color/density can be faithfully
        // reproduced without SilverLining -- cf. EnvLightingFog.h §"INVENT NOTHING" and
        // Docs/TS2_DEEP_MATERIALS_FX.md §13 TODO T-16). NO sun azimuth/color is fabricated
        // here.
        //
        // ORDER (faithful to the binary): AFTER the sky/GxdRenderer setup (RenderSky above =
        // cAtmosphere_RenderFrame + default light 0) and BEFORE the normal-mapped decor
        // (worldGeom_->Render below, the FF lighting's target). Called UNCONDITIONALLY like
        // Env_UpdateFrame in Scene_InGameRender; ApplyPerFrame self-guards on a null device.
        // Device = GxdRenderer::Instance().Device() = g_GxdRenderer_pDevice 0x18C5104, EXACTLY
        // the binary's fog-pass device (physical device shared by the 2 renderer singletons).
        //
        // shader-bind-cache TRAP: NOT APPLICABLE. EnvLightingFog::ApplyPerFrame only sets
        // SetLight(0)/LightEnable(0)/SetRenderState (LIGHTING 137, FOGENABLE 28) -- it touches
        // NEITHER SetVertexShader NOR SetPixelShader (verified in Gfx/EnvLightingFog.cpp): the
        // MeshRenderer's currentPass_ is therefore not invalidated and does not need to be.
        // Setting LIGHTING=TRUE is inert for the shaded skinned draws that follow (the .WO
        // decor) and faithful to the binary.
        //
        // DELIBERATELY NOT WIRED (fidelity: dead consumer, we do not reanimate dead code):
        //   - B4 EmitterMeshRenderer (Gfx/EmitterMeshRenderer) -- the Object B Effect_* are
        //     dead, with no live xref: no producer feeds them -> no wiring here.
        //   - B6 SkyboxCube (Gfx/SkyboxCube) -- a2 (the cube) is NULL at all 8
        //     Gfx_BeginFrame 0x6A2280 call sites (xrefs_to 0x6A2280 = 8: Scene_{Intro/
        //     ServerSelect/Login/CharSelect/EnterWorld}Render + 3x Scene_InGameRender): the
        //     skybox cube is dead in the binary -> no wiring. The live sky remains
        //     worldGeom_->RenderSky above.
        gfx::EnvLightingFog::ApplyPerFrame(gfx::GxdRenderer::Instance().Device(),  // 0x18C5104 (fog device @0x4124fe)
                                           gfx::GxdRenderer::Instance().Light());  // light_ GENUINE @0x402711
        if (worldGeomReady_ && worldGeom_) worldGeom_->Render(camera, screenW_, screenH_);
        if (worldReady_ && world_) world_->Render(camera);
        if (worldGeomReady_ && worldGeom_) worldGeom_->RenderSky(screenW_, screenH_);
        // (GXD_RenderPostBlur 0x4053E0) Bloom/post-blur: the binary's SINGLE UNCONDITIONAL call
        // @0x52FB53 (Scene_InGameRender 0x52D0B0), AFTER all 3D rendering (terrain + entities +
        // sky) and BEFORE Gfx_Begin2D @0x52FB89. The bloomEnabled flag (g_GxdRenderer+24,
        // @0x4053ED) defaults to 1 (GXD_InitGlobalState 0x401320) -> bloom active.
        // RenderPostBlur self-guards (null device/PS12/PS14/handles -> no-op) and manages its
        // own EndScene/BeginScene, leaving the scene OPEN for the 2D HUD below. The npk
        // GXDEffect PS12/PS14 come from the WorldRenderer's ShaderSet (loaded when
        // worldReady_). The GxdRenderer singleton's device is attached at App_Init
        // (App.cpp:374, GXD_DeviceReinit 0x4023F0).
        // (Wave D -- combat FX) Renders FX slots: frame anchor (camera right/up from the view
        // matrix) then 3 Fx_EmitterDraw passes (mirrors Scene_InGameRender 0x52D0B0: passes 1/2
        // = block/parry/deflect meshes via ModelObjectRenderer (Wave F, s_meshDraw wired), pass
        // 3 = particles -> Object A leaf Particle_RenderBillboards
        // 0x6A70B0). BEFORE the bloom so muzzle/sparks participate in the post-blur, like in the
        // binary (sites 0x52DD14/0x52ECEB/0x52FAD8 precede GXD_RenderPostBlur @0x52FB53).
        if (worldReady_ && world_) {
            IDirect3DDevice9* fxDev = renderer_->Device();
            D3DXMATRIX fxView; camera.BuildViewMatrix(fxView);
            D3DXMATRIX fxProj; camera.BuildProjMatrix(fxProj,
                screenH_ ? static_cast<float>(screenW_) / static_cast<float>(screenH_) : 1.0f);
            const float fxRight[3] = { fxView._11, fxView._21, fxView._31 }; // camera right in world space
            const float fxUp[3]    = { fxView._12, fxView._22, fxView._32 }; // camera up in world space
            // (Gfx_BeginUnlitPass 0x69E470) MANDATORY billboard pipeline state before
            // Fx_EmitterDraw: the binary sets it @0x52FA77 right before pass 3
            // (Fx_EmitterDraw 0x585E30 sets NO render state, it only does
            // SetTexture+DrawPrimitiveUP). Byte-exact decompile of 0x69E470:
            // LIGHTING(137)=0, ZWRITEENABLE(14)=0, ALPHABLENDENABLE(27)=1, TSS0 ALPHAOP(4)=MODULATE,
            // ALPHAARG2(6)=DIFFUSE, SetFVF(0x142 XYZ|DIFFUSE|TEX1), SetTransform(WORLD, identity).
            // WITHOUT this bracket, the FVF inherited from RenderSky (screen pre-transformed
            // XYZRHW) reinterprets the 24-byte billboard vertices (WORLD-space coords) ->
            // garbage positions, muzzle/hit invisible.
            // Defensive C++ additions (the binary inherits these from a permanent state we
            // don't guarantee here): null VS/PS (MeshRenderer leaves its skinned shaders
            // bound), camera VIEW/PROJ (MeshRenderer goes through shaders, not SetTransform),
            // standard alpha SRCBLEND/DESTBLEND.
            D3DXMATRIX fxIdent; D3DXMatrixIdentity(&fxIdent);
            fxDev->SetVertexShader(nullptr);
            fxDev->SetPixelShader(nullptr);
            fxDev->SetRenderState(D3DRS_LIGHTING, FALSE);              // 137,0
            fxDev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);         // 14,0
            fxDev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);      // 27,1
            fxDev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);      // defensive (binary inherits)
            fxDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);  // defensive (binary inherits)
            fxDev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE); // 4,4
            fxDev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);   // 6,0
            fxDev->SetFVF(D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1); // 322 = 0x142
            fxDev->SetTransform(D3DTS_WORLD, &fxIdent);              // 256, identity
            fxDev->SetTransform(D3DTS_VIEW, &fxView);
            fxDev->SetTransform(D3DTS_PROJECTION, &fxProj);
            gfx::Fx_SetParticleFrame(fxDev, fxRight, fxUp, 0 /*maxQuads: no cap*/, nullptr);
            // (Wave F) Frustum planes for per-part culling of FX meshes. MESH slot POSITION and
            // RECYCLING (one-shot flipbook) happen in the UPDATE phase (tick above); the render
            // path only consumes the already-resolved slot.position/drawParam.
            if (modelObjRenderer_) modelObjRenderer_->SetFrame(fxView, fxProj);
            for (int pass = 1; pass <= 3; ++pass)
                for (int i = 0; i < gfx::FxPool_Count(); ++i)
                    gfx::Fx_EmitterDraw(&gfx::FxPool_Slots()[i], pass);
        }
        if (worldReady_ && world_)
            gfx::GxdRenderer::Instance().RenderPostBlur(world_->BloomShaderSet());
        if (hudReady_ && hud_) hud_->Render();
        if (windowsReady_ && windows_) windows_->Render();
        break;
    }
    default:
        // None: screen cleared by Renderer::BeginFrame.
        break;
    }
}

} // namespace ts2

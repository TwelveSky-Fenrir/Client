// Scene/SceneManager.cpp -- scene state machine: Intro -> ServerSelect/Login/CharSelect
// (LoginScene) -> EnterWorld -> InGame (GameHud). Faithful dispatch to cSceneMgr_Update/_Render.
// Split family: lifecycle/scene-change lives here; the per-frame tick is in
// SceneManager_Update.cpp and drawing is in SceneManager_Render.cpp.
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

#include "Scene/SceneManager_Internal.h" // g_noticeReturnToServerSelectPending / g_noticeAbnormalEndPending

// hWndParent 0x815184 -- destination window of the WSAAsyncSelect(WM_USER+1) posted by
// Net_ConnectGameServer 0x462A70. Defined by Net/GameHandlers_Misc.cpp:158, which leaves the
// explicit TODO "App must set ts2::net::g_GameSocketNotifyWnd at init, via an extern
// declaration" (GameHandlers_Misc.cpp:153-157) -- WITHOUT the assignment, the packet 0x18
// handler (Pkt_GameServerConnectResult 0x469CF0, reconnect/relay WHILE IN GAME) degrades to a
// WSAAsyncSelect failure (Str107) and the reconnect ALWAYS fails.
// No header declares it to date -> local extern declaration (the linker resolves against
// ts2::net's definition). Assigned in SceneManager::Init: notifyHwnd_ IS hWndParent
// (App_Init @0x461C51 `mov ds:hWndParent, ecx` with ecx = the HWND created by WinMain
// 0x4609C0; on the C++ side, App::Init passes this same hwnd_ to SceneManager::Init), and
// SceneManager::Init is called FROM App::Init -- so at the same moment as the original
// assignment. // 0x815184
namespace ts2::net { extern HWND g_GameSocketNotifyWnd; }

namespace ts2 {

// Definition of the g_SceneSubState 0x1676184 mirror (field +4 of cSceneMgr 0x1676180),
// declared in SceneManager.h. Kept up to date at the 4 sync points marked "0x1676184"
// below (now split across SceneManager.cpp/_Update.cpp/_Render.cpp); consumed by
// App/PlayerInputController.cpp (Camera_UpdateFromInput guard @0x50B7EC). Initial value 0 =
// entry sub-state of any scene. // 0x1676184
int g_SceneSubState = 0;

// SCN-01 -- action of the notice dialog's OK button (UI_NoticeDlg_OnLButtonUp 0x5C03F0,
// switch (*(this+4)) @0x5C04C9).
//
// WARNING: THE GAP AS WORDED IN THE ORIGINAL DOSSIER IS STALE -- DO NOT APPLY ITS FIX AS
// WRITTEN. The dossier prescribes writing a `NoticeDialog : public ui::Dialog` (render
// 0x5C0630 + hit-test 0x5C03F0) on the grounds that game::g_Client.prompt would have "12
// writers, 0 readers". That was true at extraction time; it is NO LONGER true:
// UI/GameWindows.cpp:186 `SyncPrompt()` (UI front, SAME wave) now reflects
// game::g_Client.prompt into the shared MsgBoxDialog, and it IS REALLY reached
// (GameWindows::Render:237 -> called by SceneManager::Render `windows_->Render()`).
// Registering a second dialog HERE on the SAME state would produce:
//   (1) DOUBLE rendering (MsgBox + NoticeDialog both drawing the prompt);
//   (2) DEAD code on input -- UIManager::Init (UIManager.cpp:243) registers msgBox_ at
//       index 0, and Register (UIManager.cpp:258) only does a push_back: MsgBox would always
//       route the click first and consume it, our OnClick would never be reached.
// UI/GameWindows.cpp:211-216 explicitly leaves THIS exact hole to this front:
//   "TODO [anchor 0x5C04DF]: for type 2 (UI_NoticeDlg), the original OK runs
//     Net_CloseSocket(&g_NetClient) + g_SceneMgr=2 + g_SceneSubState=0 [...] This is the
//     scope of gaps SCN-03/04: Scene/SceneManager.* is NOT owned by this front."
// So we fill EXACTLY this hole (the ACTION, not the render), with no collision.
//
// DOSSIER CORRECTION (re-proven in IDA this mission): PromptState MERGES two DISTINCT
// registries of the binary. UI_NoticeDlg_OnLButtonUp only handles type in [1,9]; the
// Open(8/9/10/14/19/20) calls from the party/guild/social handlers belong to ANOTHER dialog
// (Yes/No), already handled by GameWindows::SyncPrompt (IsNetConfirmType -> Net_SendOp45/49/
// 67/74/55/61). The switch below therefore covers ONLY types 1..9 excluded from that list --
// otherwise type 8 (party invite) would fire BOTH Op45 (correct) and Net_SendOp73 (wrong,
// case 8 of the NOTICE table). The two tables OVERLAP in numeric value but not in semantics:
// that is the central trap of this gap.

// SCN-01 -- cf. the banner above. Reproduces `switch (*(this+4))` @0x5C04C9 of
// UI_NoticeDlg_OnLButtonUp 0x5C03F0, called AFTER UI_NoticeDlg_Close (@0x5C04A5).
// The network client is resolved via net::GlobalNetClient() (= &g_NetClient 0x8156A0, the
// binary also hits the global directly), SAME idiom as UI/GameWindows.cpp:152 SendPromptReply.
void Notice_DispatchOkAction(int type) {
    // Yes/No network types (8/9/10/14/19/20): do NOT handle here -- they belong to the MsgBox
    // registry and are already sent by GameWindows::SyncPrompt (Op45/49/67/74/55/61). Cf. the
    // "the two tables overlap" warning in the banner.
    if (type == 8 || type == 9 || type == 10 || type == 14 || type == 19 || type == 20) return;

    net::NetClient* nc = net::GlobalNetClient(); // &g_NetClient 0x8156A0
    switch (type) {                              // @0x5C04C9
    case 1:
        break;                                   // `result = 1;` @0x5C04D0 -- no action
    case 2:
        // Net_CloseSocket(&g_NetClient) @0x5C04DF THEN g_SceneMgr=2 @0x5C04E4 /
        // g_SceneSubState=0 @0x5C04EE / dword_1676188=0 @0x5C04F8. BINARY ORDER respected: the
        // socket is closed BEFORE the scene change.
        if (nc) net::NetCloseSocket(*nc);                    // 0x463000 @0x5C04DF
        g_noticeReturnToServerSelectPending = true;          // g_SceneMgr = 2 @0x5C04E4
        break;
    case 3:
        // Log_WriteLine("[ABNORMAL_END] ( 3 )") @0x5C0516 then g_QuitFlag = 1 @0x5C051B.
        TS2_LOG("[ABNORMAL_END] ( 3 )");                     // 0x53F2D0 @0x5C0516
        g_noticeAbnormalEndPending = true;                   // g_QuitFlag 0x815590 @0x5C051B
        break;
    // The 6 sends of the NOTICE table. The original `this` is &g_AutoPlayMgr 0x846C08: these
    // builders read no field of this object in the C++ port (signature
    // `void Net_SendOpNN(NetClient&)`, Net/SendPackets.h) -> the argument disappears, as with
    // every other call in this file.
    case 4: if (nc) net::Net_SendOp44(*nc); break;           // 0x4B7DC0 @0x5C0531
    case 5: if (nc) net::Net_SendOp48(*nc); break;           // 0x4B83A0 @0x5C0542
    case 6: if (nc) net::Net_SendOp54(*nc); break;           // 0x4B8C60 @0x5C0553
    case 7: if (nc) net::Net_SendOp66(*nc); break;           // 0x4B9E10 @0x5C0564
    case 8: if (nc) net::Net_SendOp73(*nc); break;           // 0x4BA860 @0x5C0575 (unreachable: filtered above)
    case 9: if (nc) net::Net_SendOp60(*nc); break;           // 0x4B9550 @0x5C0586
    default: break;                                          // `result = 1;` @0x5C0592
    }
}

SceneManager::SceneManager() = default;
SceneManager::~SceneManager() { Shutdown(); }

static const char* SceneName(Scene s) {
    switch (s) {
    case Scene::Intro:        return "Intro";
    case Scene::ServerSelect: return "ServerSelect";
    case Scene::Login:        return "Login";
    case Scene::CharSelect:   return "CharSelect";
    case Scene::EnterWorld:   return "EnterWorld";
    case Scene::InGame:       return "InGame";
    default:                  return "None";
    }
}

void SceneManager::Init(gfx::Renderer& renderer, net::NetSystem& net, void* notifyHwnd,
                        int screenW, int screenH, const std::string& gameDataDir,
                        int serverModeFlag) {
    renderer_    = &renderer;
    net_         = &net;
    notifyHwnd_  = notifyHwnd;
    screenW_     = screenW;
    screenH_     = screenH;
    gameDataDir_ = gameDataDir;
    // hWndParent 0x815184 (App_Init @0x461C51) -- cf. the declaration banner at the top of
    // this file. Sole writer of ts2::net::g_GameSocketNotifyWnd, which the packet 0x18
    // handler (Net/GameHandlers_Misc.cpp:216, in-game server reconnect/relay) reads for its
    // WSAAsyncSelect: when null, it degraded to a failure (Str107) and the reconnect ALWAYS
    // failed. `notifyHwnd` IS the HWND created by WinMain 0x4609C0, relayed by App::Init.
    ts2::net::g_GameSocketNotifyWnd = static_cast<HWND>(notifyHwnd); // 0x815184
    scene_    = Scene::None;
    subState_ = 0;
    frameCount_ = 0;
    g_SceneSubState = 0;   // mirror of cSceneMgr's field +4 (fresh state) // 0x1676184

    // Connection shell scenes (ServerSelect/Login/CharSelect).
    //
    // 7th argument `&renderer` -- 3D CHARACTER PREVIEW ON CHARSELECT (scene 4).
    // Char_RenderModel 0x527020 is called EXACTLY 4 times in the whole binary (xrefs
    // re-checked this session: 4/4), all from Scene_CharSelectRender 0x51CED0, in TWO pairs
    // (pass 1 then pass 2):
    //   LIST screen    (this[15714]==1): @0x51D361 (pass=1) . @0x51D3CC (pass=2)
    //   CREATE screen  (this[15714]==2): @0x51D429 (pass=1) . @0x51D480 (pass=2)
    // gfx::MeshRenderer::Init() requires a `gfx::Renderer&` (it only reads Device() from it,
    // but a Renderer cannot be manufactured from an IDirect3DDevice9* -- Renderer::Init
    // creates its OWN device). WITHOUT this 7th argument, LoginScene::gfxRenderer_ stays null
    // (LoginScene.cpp:98) -> the guard `if (gfxRenderer_ && charMesh_.Init(*gfxRenderer_))`
    // (LoginScene.cpp:312) fails -> charModels_/charMotions_ are never created and
    // charPreviewReady_ stays false -> CharSelectRenderPreview3D() exits on its 1st line
    // (LoginScene.cpp:1458): the 4 Char_RenderModel calls draw NOTHING and the CharSelect
    // screen shows NO character, whereas the binary draws one.
    // `renderer` is already in scope here (renderer.Device() is passed as the 1st argument).
    login_ = std::make_unique<ui::LoginScene>();
    if (!login_->Init(renderer.Device(), &net, static_cast<HWND>(notifyHwnd), screenW, screenH,
                      serverModeFlag, &renderer))
        TS2_WARN("LoginScene::Init failed (login rendering unavailable).");

    // In-game HUD: built on the fly when entering the InGame scene.
    hud_ = std::make_unique<ui::GameHud>();
    hudReady_ = false;

    // Game windows (Warehouse/Guild/Quest/Skills/Options/Social/AutoPlay/Vendor/Party/
    // Trade/Character): same lifecycle as the HUD.
    windows_ = std::make_unique<ui::GameWindows>();
    windowsReady_ = false;

    // 3D entity render (players/monsters): same lifecycle as the HUD.
    world_ = std::make_unique<WorldRenderer>();
    worldReady_ = false;

    // Static world geometry (.WO, cf. Gfx/WorldGeometryRenderer.h): the GPU render
    // (worldGeom_->Init/Build) is still built lazily on entering InGame (cf. Change()),
    // because it needs a "hot" D3D device and zoneId. worldAssets_/worldMap_ (CPU zone data,
    // NOT the render), however, are now built HERE, NOT on entering InGame as before this
    // wiring: Scene::EnterWorld needs them BEFORE InGame for its LoadZoneResources sub-state
    // (cf. Update(), case EnterWorld, host.LoadZoneResource) -- see
    // Docs/TS2_ENTERWORLD_WIRING_TODO.md for the full audit.
    worldGeom_ = std::make_unique<gfx::WorldGeometryRenderer>();
    worldGeomReady_ = false;

    // Scene BGM slot (cSceneMgr +612): default ctor = zero-init, equivalent of
    // cSceneMgr_ReinitBgm 0x517A80 -> SndMgr_InitBgmSlot 0x6A80A0 (SoundObj reset to 0).
    // The audio device (DirectSoundCreate8) is created elsewhere (AudioSystem::Init, cf.
    // App); here we only allocate the slot. LoadZoneBgm loads the .BGM on entering the game.
    bgm_ = std::make_unique<audio::BgmChannel>();

    if (!gameDataDir_.empty()) {
        worldAssets_ = std::make_unique<world::WorldAssets>(gameDataDir_);
        worldMap_    = std::make_unique<world::WorldMap>(worldAssets_->MakeHooks());
        worldMap_->SetDevice(renderer.Device());
    } else {
        TS2_WARN("SceneManager: gameDataDir empty - WorldMap unavailable "
                 "(EnterWorld/InGame degraded: zone loading impossible).");
    }

    TS2_LOG("SceneManager initialized (%dx%d).", screenW, screenH);
}

void SceneManager::Shutdown() {
    if (login_)   { login_->Shutdown();   login_.reset(); }
    if (windows_) { windows_->Shutdown(); windows_.reset(); }
    if (hud_)     { hud_->Shutdown();     hud_.reset(); }
    if (world_)   { world_->Shutdown();   world_.reset(); }
    if (worldGeom_) { worldGeom_->Shutdown(); worldGeom_.reset(); }
    if (modelObjRenderer_) { modelObjRenderer_->Shutdown(); modelObjRenderer_.reset(); } // (Wave F)
    // SceneMgr_ReleaseSoundBuffers 0x517B60 -> Snd_ReleaseBuffers(cSceneMgr+153) 0x6A80D0:
    //   releases the BGM slot in cSceneMgr's destructor (App_Shutdown 0x462480).
    if (bgm_) { bgm_->Release(); bgm_.reset(); }
    worldMap_.reset();
    worldAssets_.reset();
    renderer_ = nullptr;
    net_ = nullptr;
}

void SceneManager::StartIntro() { Change(Scene::Intro); }

void SceneManager::Change(Scene s) {
    const Scene prev = scene_;   // used to release the BGM slot when LEAVING InGame (see below)
    TS2_LOG("Scene: %s -> %s", SceneName(scene_), SceneName(s));
    scene_ = s;
    subState_ = 0;
    frameCount_ = 0;
    // Every scene change restarts from sub-state 0 in the binary: cSceneMgr's field +4 is
    // reset to 0 by g_SceneMgr's writers (e.g. op 0x18 Pkt_GameServerConnectResult 0x469CF0:
    // g_SceneMgr=5 @0x469d95 THEN g_SceneSubState=0 @0x469d9f), and Scene_EnterWorldUpdate
    // itself sets it back to 1 after its case 0 (@0x52C0B0). Without this reset, the
    // @0x50B7EC guard would see a stale sub-state from the previous scene. // 0x1676184
    g_SceneSubState = 0;

    // M6 -- every ENTRY into the EnterWorld scene restarts from subState 0 in the binary: op
    // 0x18 Pkt_GameServerConnectResult 0x469CF0 (@0x469d9f, g_SceneSubState=0) and the normal
    // flow both set g_SceneSubState 0x1676184 = 0, and Scene_EnterWorldUpdate 0x52BFF0 reads
    // its current state from *(this+1)==g_SceneSubState. enterWorldState_ models this field
    // for this scene -> a symmetric reset to the one already present in ReloadZone. Without
    // it, a 2nd EnterWorld entry resumes from a stale state (WaitServerAck/Failed from the
    // previous cycle -> frozen machine). // 0x1676184
    if (s == Scene::EnterWorld)
        enterWorldState_ = game::EnterWorldFlowState{};

    // W5b -- SYMMETRIC reset for the InGame scene, same reason as the EnterWorld block above.
    // Pkt_EnterWorld 0x464160 is the ONLY writer of g_SceneMgr 0x1676180 = 6 -- proven by a
    // byte scan over the whole image: find_bytes "C7 05 80 61 67 01 06 00 00 00" -> 1 SINGLE
    // occurrence (@0x464304), and no register store (A3/89 05/89 0D/... on 0x1676180 = 0
    // matches). And that same writer ALSO sets, right after, g_SceneSubState 0x1676184 = 0
    // @0x46430E and dword_1676188 = 0 @0x464318: Scene_InGameUpdate 0x52C600's automaton
    // therefore always restarts from case 0 (Setup) on EVERY entry into scene 6, never from
    // an inherited state.
    // Scene_InGameUpdate's `this` really IS cSceneMgr 0x1676180: xrefs_to 0x52C600 -> 1 single
    // caller (cSceneMgr_Update 0x517BF0 @0x517c79), itself called with
    // `mov ecx, offset g_SceneMgr` @0x462636 -> *(this+4) = g_SceneSubState (the switch
    // @0x52C61F) and *(this+8) = dword_1676188 (the counter). game::InGameTickFlowState{} =
    // {Setup=0, frameCounter=0} is its 1:1 mirror.
    // Without this reset, an InGame->EnterWorld->InGame warp (op 0x18 -> op 0x0c) leaves
    // inGameTickState_ at MainTick: the `g_SceneSubState = 0` set above is overwritten the
    // very next frame by `g_SceneSubState = (int)inGameTickState_.state` = 4 (in
    // SceneManager_Update.cpp), Setup never replays, and above all InitCamera
    // (Cam_SetLookAt @0x52C759 / Camera_SetEyeTarget @0x52C7CF) NEVER reframes the camera on
    // the new zone. // 0x464304 / 0x46430E / 0x464318 / 0x52C600
    if (s == Scene::InGame)
        inGameTickState_ = game::InGameTickFlowState{};

    // (Wave D -- combat FX) Resets the FX slot pool (dword_17D06F4) on every game entry:
    // mirrors Pkt_EnterWorld 0x464160 @0x4642A4 (for i<g_FxAuraCount Fx_AttachSlotClear(&slot[i])).
    if (s == Scene::InGame)
        gfx::FxPool_Reset();

    // Entering the game: initializes the HUD and game windows once (stable device).
    if (s == Scene::InGame && hud_ && !hudReady_ && renderer_) {
        hudReady_ = hud_->Init(*renderer_, screenW_, screenH_);
        if (!hudReady_) TS2_WARN("GameHud::Init failed (HUD unavailable).");
    }
    // GAP-APPLIFE-02 -- wires the network client onto the chat window. WITHOUT this Bind,
    // ChatWindow::SendOnChannel returns immediately at `if (!net_) return;`
    // (UI/ChatWindow.cpp:267): input would echo locally but NO message would ever reach the
    // server. Bind had no caller (grep) -- UI/GameHud.h:159-161 explicitly requested it
    // ("requires that SceneManager own/expose a net::NetClient&"). The 7 builders involved
    // are those of UI_Chat_SubmitInput 0x68B330 (Net_SendOp39/38/68/77/81/40/80 depending on
    // channel). Reapplied on every InGame entry: the pointer stays valid (net_ is an App
    // member), and a reconnect reuses the same NetClient.
    if (s == Scene::InGame && hud_ && hudReady_ && net_)
        hud_->Chat().Bind(&net_->Client());                              // 0x68B330
    if (s == Scene::InGame && windows_ && !windowsReady_ && renderer_) {
        windowsReady_ = windows_->Init(*renderer_, notifyHwnd_, screenW_, screenH_);
        if (!windowsReady_) TS2_WARN("GameWindows::Init failed (windows unavailable).");
    }
    if (s == Scene::InGame && world_ && !worldReady_ && renderer_) {
        worldReady_ = world_->Init(*renderer_, screenW_, screenH_);
        if (!worldReady_) TS2_WARN("WorldRenderer::Init failed (world rendering unavailable).");
        // (F_ENTITY3D B8) Source of the planar shadow's ground plane
        // (Model_RenderPlanarShadow 0x40F720, plane retrieved via Collision_SegPickA
        // 0x420D60): WorldRenderer queries worldAssets_->GetGroundPlaneForShadow per entity.
        // Same member/lifetime as the GetGroundHeight host (worldAssets_ built once in
        // Init(), destroyed at Shutdown WITH world_) -> no dangling. Without this wiring, the
        // shadow pass is a clean no-op.
        if (worldReady_ && worldAssets_) world_->SetCollisionSource(worldAssets_.get());
        // (Wave D -- combat FX) ONE-TIME bootstrap of the Object A leaf + dispatch: physical
        // device (g_GfxRenderer 0x7FFE18), asset root (contains G03_GDATA\D05_GPARTICLE), and
        // wiring of the particle render hook s_particleRender -> FxBillboard_PoolRender
        // (= lazy-load .PARTICLE + Particle_RenderBillboards 0x6A70B0). s_meshDraw stays null
        // (ModelObj_Draw 0x4D71B0, model system not ported -> the block/parry/deflect mesh FX
        // stay invisible at this milestone).
        gfx::FxBillboard_SetDevice(renderer_->Device());
        gfx::FxBillboard_SetDataRoot(gameDataDir_.c_str());
        gfx::Fx_WireLeafHooks();
        // (Wave F) Model-object mesh renderer: registers the instance that receives the
        // s_meshDraw hook (wired by Fx_WireLeafHooks above; the shim is a no-op until Init()
        // has registered the instance). MiscC bank (unk_B60AB8) for combat mesh FX
        // (block/parry/deflect). ModelObj_Draw 0x4D71B0; reuses the asset::MObject parser +
        // the world's GPU upload path.
        if (!modelObjRenderer_) modelObjRenderer_ = std::make_unique<gfx::ModelObjectRenderer>();
        if (!modelObjRenderer_->Init(*renderer_, gameDataDir_))
            TS2_WARN("ModelObjectRenderer::Init failed (combat mesh FX unavailable).");
    }
    // Static world geometry (.WO): loaded ONCE on entering InGame, for the current zoneId
    // (game::g_World.zoneId). No reload on zone change mid-session (future TODO: hook
    // World_LoadZoneResource(ObjectsWO) into the warp/MapWarp flow, cf. Game/MapWarp.h --
    // outside this initial wiring's scope, same as host.LoadZoneResource already TODO in
    // case EnterWorld below).
    if (s == Scene::InGame && worldGeom_ && !worldGeomReady_ && renderer_) {
        worldGeomReady_ = worldGeom_->Init(*renderer_);
        if (!worldGeomReady_) {
            TS2_WARN("WorldGeometryRenderer::Init failed (static geometry unavailable).");
        } else if (!worldMap_ || !worldAssets_) {
            // worldMap_/worldAssets_ are now built ONCE in Init() (cf.
            // Docs/TS2_ENTERWORLD_WIRING_TODO.md §2), not here, so they are already available
            // during Scene::EnterWorld. nullptr here => gameDataDir_ was empty at Init()
            // (already warned about at that time).
            TS2_WARN("SceneManager: WorldMap unavailable (gameDataDir empty at Init) - "
                     ".WO loading impossible.");
        } else {
            const int zoneId = game::g_World.zoneId;
            // Sets the current zone key BEFORE loading any layer: SetCurrentZoneId was called
            // nowhere else (grep) -> World_LoadCurrentZoneModel 0x4DD6E0 (which reads
            // g_SelfMorphNpcId 0x1675A98) was operating on zone 0. // 0x4DD6E0
            if (worldMap_) worldMap_->SetCurrentZoneId(zoneId); // g_SelfMorphNpcId 0x1675A98
            // Redundant with the idx=3 (ResourceKind::ObjectsWO) load already done during
            // Scene::EnterWorld (LoadZoneResources, cf. host.LoadZoneResource in Update()) --
            // WorldMap::LoadZoneResource is idempotent (reloads the same file). Kept here as
            // a safety net for paths that force Change(Scene::InGame) directly WITHOUT going
            // through EnterWorld (Scene/SceneAudit.cpp, Tools/UiWindowSelfTest.cpp).
            const unsigned char ok = worldMap_->LoadZoneResource(zoneId, world::ResourceKind::ObjectsWO);
            if (ok) {
                worldGeom_->Build(*worldAssets_);
                TS2_LOG("SceneManager: .WO geometry for zone %d loaded (%zu GPU parts, %zu skipped A>1).",
                        zoneId, worldGeom_->UploadedPartCount(), worldGeom_->SkippedMultiAnchorCount());
            } else {
                TS2_WARN("SceneManager: World_LoadZoneResource(ObjectsWO, zone=%d) failed "
                         "(unknown zoneId->fileId or Z%%03d.WO file missing).", zoneId);
            }
        }
    }

    // --- Zone BGM: WORLD slot g_GameWorld+2236 (worldBgm_) -- FIDELITY FIX ---
    // The REAL play of the zone BGM is Player_ResetCombatState @0x50F76E (gated by
    // g_BgmEnabled 0x84DEF0 @0x50F75A), on the WORLD slot g_GameWorld+2236 (worldBgm_),
    // DISTINCT from the scene slot cSceneMgr+612 (SceneManager::bgm_/LoadZoneBgm, reserved for
    // the MENU Z000 BGM). The zone .BGM LOAD happens during Loading (World_LoadZoneResource
    // 0x4DCB60 case 12 -> WorldAssets::LoadWorldBgm, via EnterWorld host.LoadZoneResource
    // idx=12): here we ONLY PLAY, without re-decoding.
    // WARNING: the former LoadZoneBgm(zoneId) call was re-decoding the same .BGM into the
    //   WRONG slot (cSceneMgr+612) -- double decoding + wrong slot. Replaced by PlayWorldBgm
    //   (world slot).
    if (s == Scene::InGame) {
        if (worldAssets_) worldAssets_->PlayWorldBgm(config::g_Options.BgmEnabled == 1); // 0x50F76E
    } else if (prev == Scene::InGame) {
        // Leaving the game (back to menu / disconnect): cuts the zone ambiance (world slot) --
        //   Snd_ReleaseBuffers 0x6A80D0; + the vestigial scene slot (harmless if empty).
        if (worldAssets_) worldAssets_->ReleaseWorldBgm();
        ReleaseBgm();
    }
}

// --- Scene BGM slot: load (enter-world/zone-change) + release (exit) ---
// See Audio/AudioSystem.h (BgmChannel) for the full IDA anchor breakdown.
void SceneManager::LoadZoneBgm(int zoneId) {
    if (!bgm_) return;
    // World_LoadZoneResource 0x4DCB60 case 12: Z = World_ZoneIdToFileId(zoneId) 0x4db0f0.
    //   fileId == -1 -> the zone has no BGM (the binary skips the load, `if (v3 != -1)`
    //   @0x4dd406): cut any previous BGM and exit.
    const int fileId = world::WorldMap::ZoneIdToFileId(zoneId);
    if (fileId < 0) {
        TS2_LOG("SceneManager: zone %d has no fileId -> no BGM.", zoneId);
        ReleaseBgm();
        return;
    }
    // 0x4dd41d: .rdata string "G03_GDATA\\D10_WORLDBGM\\Z%03d.BGM" (aG03GdataD10Wor_0 @0x7a7cc8).
    //   The decoder (OggVorbisLoadCallback via asset::ReadOggFile) expects a resolvable path
    //   -> prefix the GameData root, like World/WorldIntegration::LoadWorldBgm.
    char rel[64];
    std::snprintf(rel, sizeof(rel), "G03_GDATA\\D10_WORLDBGM\\Z%03d.BGM", fileId); // 0x4dd41d
    const std::string full = gameDataDir_.empty() ? std::string(rel)
                                                   : (gameDataDir_ + "\\" + rel);
    // g_BgmEnabled 0x84DEF0 (option f12) -- gates the play (0x518c03 / 0x50f761). vol=100 is
    //   hardcoded at both play sites (0x518c14 / 0x50f76e); MusicVolume (option idx10) applies
    //   elsewhere (positional/UI sounds), not to the BGM slot's play.
    const bool enabled = (config::g_Options.BgmEnabled == 1);
    if (bgm_->LoadAndPlay(full, enabled, 100)) {
        TS2_LOG("SceneManager: zone %d BGM (Z%03d.BGM) loaded%s.", zoneId, fileId,
                enabled ? " and playing (looped)" : " (BGM option off: silent)");
    } else {
        // Required guard: .BGM missing / audio device unavailable / Ogg decoder absent -> mute,
        //   NO crash (silent client for this zone, like an unavailable DirectSound).
        TS2_WARN("SceneManager: zone %d BGM (Z%03d.BGM) unavailable "
                 "(file missing, audio not initialized, or Ogg decoder absent).", zoneId, fileId);
    }
}

void SceneManager::ReleaseBgm() {
    // SceneMgr_ReleaseSoundBuffers 0x517B60 -> Snd_ReleaseBuffers(cSceneMgr+153) 0x6A80D0.
    if (bgm_) bgm_->Release();
}

// RE-ENTRANT zone reload (warp) -- cf. the declaration in SceneManager.h. Replays case 1 of
// Scene_EnterWorldUpdate 0x52BFF0 (World_LoadZoneResource 0x4DCB60 idx 1..12) + rebuilds GPU
// .WO + BGM, LIFTING the one-shot guards that Change() never replays on re-entry.
void SceneManager::ReloadZone(int zoneId) {
    // 1. Current zone key: g_SelfMorphNpcId = g_TargetZoneId (Scene_EnterWorldUpdate
    //    0x52C173). Read by World_LoadCurrentZoneModel 0x4DD6E0 (SetCurrentZoneId) AND
    //    cGameData_LoadZoneNpcInfo 0x5578E0 (via g_World.zoneId -> LoadZoneNpcs, self spawn).
    game::g_World.zoneId = zoneId;                        // g_SelfMorphNpcId 0x1675A98
    if (worldMap_) worldMap_->SetCurrentZoneId(zoneId);   // World_LoadCurrentZoneModel 0x4DD6E0

    // 2. Re-runs World_LoadZoneResource(zoneId, kind) for kinds 1..12, faithful to the case 1
    //    loop (0x52C0F8: idx 0..19, only 1..12 load, the rest are no-ops).
    //    Idempotent (WorldMap::LoadZoneResource reloads the same file). // 0x4DCB60
    if (worldMap_) {
        for (int idx = 1; idx <= 12; ++idx)
            worldMap_->LoadZoneResource(zoneId, static_cast<world::ResourceKind>(idx)); // 0x4DCB60 case idx
    }

    // 3. Rebuilds the .WO GPU geometry for the new zone: LIFTS the worldGeomReady_ one-shot
    //    guard (in Change() the rebuild block is gated by !worldGeomReady_ -> never replayed
    //    on re-entry). ObjectsWO (kind 3) was just reloaded in the loop above -> worldAssets_
    //    is up to date. // 0x4DCB60 case 3 (ObjectsWO) + worldGeom_->Build
    if (worldGeomReady_ && worldGeom_ && worldAssets_) {
        worldGeom_->Build(*worldAssets_);
        TS2_LOG("SceneManager: zone %d reload -> .WO geometry rebuilt "
                "(%zu GPU parts).", zoneId, worldGeom_->UploadedPartCount());
    }

    // 4. Replays the new zone's BGM ambiance. worldBgm_ (world slot g_GameWorld+2236) was just
    //    RELOADED by the idx 1..12 loop above (case 12 -> WorldAssets::LoadWorldBgm, released
    //    BEFORE reload via make_unique): here we ONLY PLAY (Play @0x50F76E, gated by
    //    g_BgmEnabled). No more re-decoding into the scene slot (ex-LoadZoneBgm). // 0x4DCB60 case 12
    if (worldAssets_) worldAssets_->PlayWorldBgm(config::g_Options.BgmEnabled == 1);
}

void SceneManager::ConsumePending() {
    if (!login_) return;
    const Scene p = login_->PendingScene();
    if (p != Scene::None) {
        login_->ClearPending();
        Change(p);
    }
}

// UIFW-08 -- mouse-input action-state gate, cf. SceneManager.h.
bool SceneManager::InputSwallowedByActionState() const {
    if (scene_ != Scene::InGame || g_SceneSubState != 4) return false;   // `g_SceneMgr != 6 ||
                                                                         //  g_SceneSubState != 4` @0x50ACF7
    // g_SelfActionState[0] 0x1687328 == g_World.players[0].body @+220 (== entity+244, cf.
    // game::kSelfActionStateOffset, Game/MapWarp.h:83). SAME read as host.GetSelfActionState
    // (InGame tick step 12 in SceneManager_Update.cpp) -- factored here rather than duplicated.
    if (game::g_World.players.empty()) return false;
    const game::PlayerEntity& self0 = game::g_World.players[0];
    int32_t raw = 0;
    if (self0.body.size() >= game::kSelfActionStateOffset + sizeof(raw))
        std::memcpy(&raw, self0.body.data() + game::kSelfActionStateOffset, sizeof(raw));
    switch (raw) {   // `!= 11 && != 12 && != 33 && != 34 && != 35 && != 36 && != 37` @0x50ACF7
    case 11: case 12: case 33: case 34: case 35: case 36: case 37:
        return true;   // click TOTALLY swallowed: neither UI_RouteLButton* nor cSceneMgr_OnLButton*
    default:
        return false;
    }
}

void SceneManager::OnLButtonDown(int x, int y) {
    switch (scene_) {
    case Scene::ServerSelect:
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) { login_->OnMouseDown(scene_, x, y); ConsumePending(); }
        break;
    case Scene::InGame:
        // UIFW-08 -- Input_OnLButtonDown 0x50AC90 @0x50ACF7: in certain self action states
        // (11/12/33..37), the click reaches NEITHER the UI NOR the world. Tested BEFORE UI
        // routing, like the binary (the guard wraps the call to UI_RouteLButtonDown @0x50AD0F).
        if (InputSwallowedByActionState()) break;                        // 0x50ACF7
        // Windows (dialogs) intercept the click first (UIManager's "first consumer wins"
        // rule); otherwise it falls through to the HUD.
        if (windowsReady_ && ui::UIManager::Instance().RouteMouseDown(x, y)) break;
        if (hudReady_ && hud_) hud_->OnMouseDown(x, y);
        break;
    default: break;
    }
}

void SceneManager::OnLButtonUp(int x, int y) {
    switch (scene_) {
    case Scene::ServerSelect:
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) { login_->OnMouseUp(scene_, x, y); ConsumePending(); }
        break;
    case Scene::InGame:
        // UIFW-08 -- SAME guard, entry Input_OnLButtonUp 0x50AD20 @0x50AD87 (identical pattern
        // to @0x50ACF7, verified by disassembly: all 4 mouse entry points carry it).
        if (InputSwallowedByActionState()) break;                        // 0x50AD87
        if (windowsReady_) ui::UIManager::Instance().RouteMouseUp(x, y);
        break;
    default: break;
    }
}

// GAP-APPLIFE-02 -- UI_Chat_FocusInput 0x68B200 (UNIQUE xref: App_WndProc @0x461B5E).
bool SceneManager::FocusChatInput() {
    // Entry guard `if (g_SceneMgr == 6 && g_SceneSubState == 4)` @0x68B217: outside this
    // state, the original takes NO focus (chat does not open during zone loading nor in the
    // login shell).
    if (scene_ != Scene::InGame || g_SceneSubState != 4) return false;   // @0x68B217
    if (!hudReady_ || !hud_) return false;
    // The binary here picks between two EDITs: `if (dword_18225C0 || dword_1822724)`
    // @0x68B239 -> UI_FocusEditBox(id 16 = index 15, "say" box); otherwise id 5 = index 4 =
    // main chat (@0x68B22B / @0x68B250; the passed id equals index+1 -- g_hEditChatMain
    // 0x1668FD4 sits at (0x1668FD4-0x1668FC4)/4 = index 4 of the g_hEditLoginId 0x1668FC4
    // array).
    // ClientSource has only ONE in-game input box (ui::ChatWindow): both branches therefore
    // converge on the same widget. Neither discriminant is tracked anywhere.
    // TODO [anchor 0x68B239 / dword_18225C0 / dword_1822724]: distinguish the "say" box from
    // the main chat once it is modeled.
    hud_->Chat().Focus();                                                // 0x50F4A0 (id 5)
    return true;
}

// GAP-APPLIFE-02 -- text-input keyboard arbitration, cf. SceneManager.h.
bool SceneManager::RouteTextInputKey(int vk) {
    if (scene_ != Scene::InGame || !hudReady_ || !hud_) return false;
    // (1) A FOCUSED input field eats the key: it is UI_EditBoxWndProc 0x50E070 that receives
    // it in the binary (the native EDIT has Win32 focus), not the main window.
    // ChatWindow::OnKey covers the same key set as the original subclass: Enter -> submit
    // (UI_Chat_SubmitInput 0x68B330 @0x50E1D6), Escape -> cancel, Backspace/arrows/Tab
    // (@0x50E070 case 4).
    if (hud_->Chat().Focused()) return hud_->Chat().OnKey(vk);           // 0x50E070
    // (2) No EDIT focused -> the main window receives WM_KEYDOWN, whose ONLY keyboard
    // handling is `if (a3 == 13) UI_Chat_FocusInput();` (App_WndProc @0x461B55/@0x461B5E).
    // VK_RETURN == 13 == kVK_RETURN (UI/ChatWindow).
    if (vk == VK_RETURN) return FocusChatInput();                        // 0x461B5E -> 0x68B200
    return false;
}

void SceneManager::OnChar(char c) {
    if (scene_ == Scene::Login && login_) { login_->OnChar(c); ConsumePending(); }
    // GAP-APPLIFE-02 -- in-game chat input. ChatWindow::OnChar filters itself
    // (`if (!focused_) return false;` UI/ChatWindow.cpp:429), so the call is harmless outside
    // input. Source: App/App.cpp WM_CHAR -> scene_.OnChar (unconditional, all scenes). The
    // binary has NEITHER WM_CHAR NOR a custom EDIT: it delegates input to the 21 native Win32
    // EDITs of mEDITBOX (UI_CreateEditBoxes 0x50E460) which consume WM_CHAR themselves -- a
    // compensating deviation already assumed and documented by App/App.cpp:1067.
    else if (scene_ == Scene::InGame && hudReady_ && hud_) hud_->Chat().OnChar(c);
}

void SceneManager::OnKeyDown(int vk) {
    if ((scene_ == Scene::Login || scene_ == Scene::CharSelect) && login_) {
        login_->OnKeyDown(vk);
        ConsumePending();
    } else if (scene_ == Scene::InGame && windowsReady_ && windows_) {
        // NB: in the InGame scene, App/App.cpp:1094 DELIBERATELY restricts this entry point
        // to the DirectInput path (`if (scene_.Current() != Scene::InGame)` on WM_KEYDOWN):
        // `vk` therefore carries a DIK SCANCODE here, not a virtual-key. This is faithful --
        // the binary's in-game keyboard is 100% DIK (Game_OnHotkey 0x537330 re-reads the
        // DirectInput buffer). Chat input, on the other hand, goes through RouteTextInputKey
        // (VK, Win32 path) -- cf. cSceneMgr_OnKeyDown 0x517F80, which is __thiscall WITHOUT a
        // key parameter and just does `switch(*this)` -> case 6 -> Game_OnHotkey.
        // An OPEN dialog (Escape/Enter...) intercepts before the global open shortcuts
        // (I/C/K/G/O/...), like the original UI_RouteKeyInput.
        if (ui::UIManager::Instance().RouteKey(vk)) return;
        windows_->HandleHotkey(vk);
    }
}

void SceneManager::OnDeviceLost() {
    if (login_)    login_->OnDeviceLost();
    if (hud_)      hud_->OnDeviceLost();
    if (windows_)  windows_->OnDeviceLost();
    if (world_)    world_->OnDeviceLost();
    if (worldGeom_) worldGeom_->OnDeviceLost();
    if (modelObjRenderer_) modelObjRenderer_->OnDeviceLost(); // (Wave F) no-op: MANAGED resources survive the Reset
}

void SceneManager::OnDeviceReset() {
    if (login_)    login_->OnDeviceReset();
    if (hud_)      hud_->OnDeviceReset();
    if (windows_)  windows_->OnDeviceReset();
    if (world_)    world_->OnDeviceReset();
    if (worldGeom_) worldGeom_->OnDeviceReset();
    if (modelObjRenderer_) modelObjRenderer_->OnDeviceReset(); // (Wave F)
}

} // namespace ts2

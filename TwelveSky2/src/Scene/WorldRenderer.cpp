// Scene/WorldRenderer.cpp — see WorldRenderer.h for the exact scope of the wiring. Split into
// WorldRenderer.cpp (init/shutdown/model resolution/placeholder cube/top-level Render dispatch),
// WorldRenderer_Shadows.cpp (planar shadow pass), WorldRenderer_Entities.cpp (per-entity draw +
// weapon trail + screen projection) and WorldRenderer_Nameplates.cpp (hovered-entity label pass).
#include "Scene/WorldRenderer.h"
#include "Gfx/Renderer.h"
#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/NameplateLogic.h"
#include "Game/NpcInteraction.h"  // game::Npc_GetNameplateColor (0x540790) — MONSTER nameplate color (W9)
#include "Game/ClientRuntime.h"   // game::Str (StrTable005_Get 0x4C1D10) — NameplateHost::ResolveString (W9)
#include "Game/ExtraDatabases.h"  // game::NpcDefRecord (NPC label: name@+4, fieldF[1]@+1332) — W9
// world::World_PickEntityAtCursor (0x538AB0) + world::BuildScreenPickCamera — W9.
// LINK DEPENDENCY FOR ORCHESTRATOR: src\World\TerrainPicker.cpp/.h exist but aren't listed in
// TwelveSky2.vcxproj/.vcxproj.filters (0 "TerrainPicker" hits among 138 explicit <ClCompile>,
// unlike World\WorldMap.cpp/World\WorldIntegration.cpp) -> until added, drawNameplatePass() won't
// link (unresolved external: world::World_PickEntityAtCursor, world::BuildScreenPickCamera).
// Out of .vcxproj scope here; reimplementing hit-test would duplicate the 5 ported Scene_RayHit*
// (0x5415E0/0x541680/0x541780/0x5418B0/0x541920) or invent picking (forbidden).
#include "World/TerrainPicker.h"
#include "World/WorldIntegration.h" // F_ENTITY3D (B8): world::WorldAssets + collision::GroundPlane (shadow ground plane)
#include "Config/GameOptions.h"   // config::g_Options (g_Opt_ShowHitMarkers/ShowNameplates 0x84DED0/D4) — W9
#include "Game/StaticNpcLoader.h" // decor NPCs (mission "PNJ DECOR VISIBLES A L'ECRAN", cf. Render())
#include "Game/AnimationTick.h"       // ZoneNpc_AnimTickIsWired() / Monster_MotionTickIsWired() / IMotionFrameCountOracle — W7
#include "Game/PlayerAnimCursorTick.h" // Player_AnimCursorTickIsWired() (player cursor) — front F_PLAYERANIM
#include "Game/EntityLifecycleTick.h" // g_MonsterTickExt (per-monster motionState/animFrame) — W7
#include "Gfx/MotionCache.h"      // animated bone palette (mirror of g_ModelMotionArray 0x8E8B30) — W3-F1
#include "Gfx/PlayerPaperdoll.h"  // player paperdoll (Char_RenderModel 0x527020 layer) — W3-F1
#include "Game/WeaponTrailResolver.h" // weapon trail: id->v6 switch + stems (front F_WEAPONTRAIL)
#include "Core/Log.h"
#include <cstring>

#include "Scene/WorldRenderer_Internal.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2 {

namespace {

template <class T>
void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Implementation of game::IMotionFrameCountOracle (Pass 4 / W7, front motion-anim) backed by the
// SAME MotionCache as the draw — this is the fidelity-CRITICAL point: in the binary, the tick
// (Model_GetMotionFrameCount 0x4E5A70 @0x582d6a and friends) and the draw (Char_Draw @0x580770)
// resolve the slot via the SAME accessor on the SAME g_ModelMotionArray, so the tick's frameCount
// == the drawn palette's frameCount. Letting these two sources diverge (e.g. a separate duration
// table) would desynchronize wrap and sampling.
class MotionFrameCountOracle final : public game::IMotionFrameCountOracle {
public:
    // Model_GetMotionFrameCount 0x4E5A70 (monster).
    int GetMonsterMotionFrameCount(uint32_t monsterDefId, int animType) const override {
        return Motions().GetMonsterMotionFrameCount(monsterDefId, animType);
    }
    // Model_GetWeaponEffectFrameCount 0x4E5A40 (decor NPC), indexed like game::ZoneNpcs().
    int GetZoneNpcMotionFrameCount(int zoneNpcIndex, int animType) const override {
        const std::vector<game::StaticNpcSlot>& slots = game::ZoneNpcs();
        if (zoneNpcIndex < 0 || static_cast<size_t>(zoneNpcIndex) >= slots.size()) return 0;
        const game::NpcDefRecord* def = slots[static_cast<size_t>(zoneNpcIndex)].def;
        if (!def) return 0; // never null in practice (loader guard), defensive
        return Motions().GetNpcMotionFrameCount(*def, animType);
    }
};

// Npc_DrawMesh 0x57FF00 (cf. Docs/TS2_NPC_MESH_DRAW.md): culling guard SPECIFIC to NPCs,
// absent from Char_Draw 0x5805C0 (verified by full decompilation of both functions on
// 2026-07-14: Char_Draw contains NO Math_Dist3D call, only the camera near-cull
// IsBeyondCameraNearCull applies to players/monsters). Npc_DrawMesh, on the other hand, blocks
// drawing right at entry if Math_Dist3D(pos_npc, flt_1687330 /* = LOCAL PLAYER position, this+5
// of g_EntityArray[0] */) > 1000.0 -- BEFORE even the camera near-cull test. game::
// ComputeEntityDrawFlags (used uniformly for players/monsters/npcs here) only models the
// Char_Draw pipeline (no far-cull): without this extra guard, an NPC >1000 units from the local
// player would be drawn (placeholder cube) even though the original client never would.
constexpr float kNpcFarCullDistanceSq = 1000.0f * 1000.0f;

// Offset (bytes) of the "equipped weapon item id" field (LE u32) inside PlayerEntity::body
// (600 bytes, raw Pkt_SpawnCharacter 0x0f / 0x4646c0 payload). Valid for ANY player in the
// array (self included), wired here for REMOTE players only (the local player uses
// SelfState::equip[7].itemId, already kept up to date continuously — cf. WorldRenderer.h
// banner for the full decompilation evidence, twin function pair Weapon_ClassFromEquip
// 0x4cc9f0 (self, dword_1673248) / Weapon_ClassFromField56 0x4cc930 (generic, *(entity+172)):
// entity+172 = body+148 since the body starts at entity+0x18).
constexpr size_t kPlayerBodyWeaponItemIdOffset = 148;

// Race/gender/costume offsets (bytes) in PlayerEntity::body (mission "base player body wiring",
// 2026-07-14, cf. Docs/TS2_PLAYER_BODY_MODEL.md §3ter/§5): PROVEN by direct decompilation (3
// call sites that re-read entity+92/+96/+100/+104 on the g_EntityArray runtime array, self AND
// remote -- entity+92 = body+68 since the body starts at entity+0x18). Valid without distinction
// for p.body at any index (unlike the weapon, no separate self global is known -- doc §4 shows
// on the contrary that gender/costumeSlot0/costumeSlot1 are mutated IN PLACE in entity[0], hence
// in this same body, by Pkt_ItemActionDispatch).
constexpr size_t kPlayerBodyRaceOffset         = 68; // [0,3)
constexpr size_t kPlayerBodyGenderOffset       = 72; // [0,2)
constexpr size_t kPlayerBodyCostumeSlot0Offset = 76; // [0,7) -- catalog flt_F59A7C
constexpr size_t kPlayerBodyCostumeSlot1Offset = 80; // [0,3) -- catalog flt_F5B21C

} // namespace

// Public accessor for the oracle (cf. Scene/WorldRenderer.h for the placement rationale).
// Process-lifetime singleton, backed by the drawing MotionCache.
const game::IMotionFrameCountOracle& WorldMotionFrameCountOracle() {
    static const MotionFrameCountOracle s_oracle;
    return s_oracle;
}

// FrameCount of a PLAYER's current clip (front F_PLAYERANIM) — cf. Scene/WorldRenderer.h. Backed
// by the SAME MotionCache (Motions()) as the draw: Motion_GetFrameCount 0x4D7830 returned by
// PcModel_ResolveSlotAndApply 0x4E5A00, which bounds the cursor wrap (Char_TickMoveState @0x574922).
int WorldPlayerMotionFrameCount(int race, int gender, int weaponType, int animState) {
    if (const gfx::MotionPalette* mp = Motions().GetForPlayer(race, gender, weaponType, animState))
        return mp->frameCount; // MotionPalette+4 = Motion_GetData 0x4D78C0 (slot+140), cf. MotionCache.h
    return 0;                  // slot not resolved -> Player_AdvanceAnimCursor advances without wrap
}

// ===========================================================================
//  Init / Shutdown
// ===========================================================================

bool WorldRenderer::Init(gfx::Renderer& renderer, int screenW, int screenH) {
    device_  = renderer.Device();
    screenW_ = screenW;
    screenH_ = screenH;
    if (!device_) { TS2_ERR("WorldRenderer::Init : device nul"); return false; }

    // hWndParent 0x815184 for drawNameplatePass's ScreenToClient (@0x52FB6C) — W9. Retrieved
    // from the device rather than through a new Init() parameter (whose only caller,
    // Scene/SceneManager.cpp, is not a file of this front): hFocusWindow IS the HWND passed to
    // CreateDevice by gfx::Renderer::Init(HWND, ...). Falls back to the swap chain's
    // hDeviceWindow if the device was created without an explicit focus window.
    D3DDEVICE_CREATION_PARAMETERS cp{};
    if (SUCCEEDED(device_->GetCreationParameters(&cp)) && cp.hFocusWindow) {
        hwnd_ = cp.hFocusWindow;
    } else {
        IDirect3DSwapChain9* sc = nullptr;
        if (SUCCEEDED(device_->GetSwapChain(0, &sc)) && sc) {
            D3DPRESENT_PARAMETERS pp{};
            if (SUCCEEDED(sc->GetPresentParameters(&pp))) hwnd_ = pp.hDeviceWindow;
            sc->Release();
        }
    }
    if (!hwnd_)
        TS2_WARN("WorldRenderer::Init : HWND introuvable -> survol nameplate en coordonnees ecran brutes.");

    if (!meshRenderer_.Init(renderer)) {
        TS2_ERR("WorldRenderer::Init : MeshRenderer::Init a echoue");
        return false;
    }

    // ModelCache (mission "wire ResolveModel() to Gfx/ModelCache", 2026-07-14). gameDataDir="."
    // rather than an explicit path: WorldRenderer::Init() does NOT receive the gameDataDir
    // resolved by App (SceneManager::Init does have this parameter, but SceneManager::Change()
    // passes NONE to world_->Init() -- Scene/SceneManager.cpp is explicitly OUT OF SCOPE for
    // this mission, not to be modified). This remains correct regardless: App::ResolveGameDataDir()
    // (App/App.cpp) switches the process CWD TO gameDataDir since App::Init(), well BEFORE
    // SceneManager::Change (InGame) constructs/Inits this WorldRenderer -- by the time ModelCache
    // needs it, "." == gameDataDir (same assumption as the original hardcoded paths
    // "G01_GFONT\..." already consumed elsewhere in ClientSource without a prefix).
    modelCache_ = std::make_unique<gfx::ModelCache>(meshRenderer_, std::string("."));

    // FRONT FX-F4 (M1): loads the REAL npk shaders and wires them onto meshRenderer_ BEFORE the
    // 1st frame. Best-effort like the cube/font: a failure (npk absent) leaves meshRenderer_ on
    // its reconstructed HLSL fallback, without blocking init. IDA anchor: GXD_DeviceCreate
    // 0x401610 loads the 12 Shader_LoadVSxx/PSxx in sequence; ShaderSet::LoadFromFile reproduces
    // this path (default "./GXDEFFECT/GXDEffect.npk", key {1,4,4,1} -- cf. Shader_LoadVS03
    // 0x409AB0: Npk_OpenFile + Npk_FindEntryByName("Shader03.fx")). AttachShaderSet with a valid
    // ShaderSet (VS03/PS04) is enough to switch DrawSkinnedSubset to the real shaders (cf.
    // MeshRenderer.cpp:510).
    if (shaderSet_.LoadFromFile(device_)) {
        meshRenderer_.AttachShaderSet(&shaderSet_); // real VS03/PS04 slots (0x409AB0/0x409CC0)
        TS2_LOG("WorldRenderer : ShaderSet npk cable (Shader03/04 reels).");
    } else {
        TS2_WARN("WorldRenderer : GXDEffect.npk indisponible -> shaders HLSL reconstruits (fallback).");
    }

    if (!buildPlaceholderCube(device_))
        TS2_WARN("WorldRenderer::Init : placeholder cube indisponible (D3DXCreateBox).");
    if (!font_.Init(device_, screenW, screenH))
        TS2_WARN("WorldRenderer::Init : Font::Init a echoue (nameplates muettes).");

    ready_ = true;
    TS2_LOG("WorldRenderer pret (%dx%d).", screenW, screenH);
    return true;
}

void WorldRenderer::Shutdown() {
    font_.Shutdown();
    modelCache_.reset(); // ~ModelCache -> Clear() -> releases resident GPU VB/IB/textures
    SafeRelease(cubeMesh_);
    meshRenderer_.Shutdown();
    // FRONT FX-F4 (M1): MANDATORY ORDER -- meshRenderer_.Shutdown() has already released its
    // reference (internal shaderSet_ = nullptr, cf. MeshRenderer.cpp:195), so the npk's
    // VS/PS/CT/decl can be freed without any draw referencing already-released shaders.
    shaderSet_.Release();
    device_ = nullptr;
    hwnd_   = nullptr; // W9: symmetry with Init() (re-resolved from the device on the next Init)
    ready_  = false;
}

void WorldRenderer::OnDeviceLost() {
    font_.OnDeviceLost();
}

void WorldRenderer::OnDeviceReset() {
    font_.OnDeviceReset();
}

// ===========================================================================
//  Model resolution — wired to Gfx/ModelCache (cf. WorldRenderer.h banner)
// ===========================================================================

const gfx::SkinnedModel* WorldRenderer::ResolveMonsterModel(uint32_t monsterDefId) {
    if (!modelCache_ || monsterDefId == 0) return nullptr;
    // ModelCache::GetForMonster does all the work (g_World.db.monster read, field244 ->
    // kindIndex, M*.SOBJECT stem formula) — cf. Gfx/ModelCache.cpp.
    return modelCache_->GetForMonster(monsterDefId);
}

const gfx::SkinnedModel* WorldRenderer::ResolveWeaponModel(uint32_t weaponItemId) {
    if (!modelCache_ || weaponItemId == 0) return nullptr;
    const game::ItemInfo* item = game::GetItemInfo(weaponItemId);
    if (!item) return nullptr; // out-of-bounds id or empty slot (cf. GetItemInfo)
    return modelCache_->GetForItem(*item, /*slot=*/0); // slot 0 = main model
}

const gfx::SkinnedModel* WorldRenderer::ResolveNpcModel(const game::NpcDefRecord* npcDef) {
    if (!modelCache_ || !npcDef) return nullptr;
    // ModelCache::GetForNpc reads npcDef->fieldE (+1324, kindIndex+1) -> "N%03d%03d001.SOBJECT"
    // formula; nullptr if out of bounds [1,66] (cf. Gfx/ModelCache.cpp).
    return modelCache_->GetForNpc(*npcDef);
}

gfx::PlayerBodyModel WorldRenderer::ResolvePlayerBodyModel(int race, int gender,
                                                            int costumeSlot0, int costumeSlot1) {
    if (!modelCache_) return {};
    // ModelCache::GetForPlayerBody does all the work (kindIndex=race+3*gender formula,
    // SLOT0/SLOT1 stems, bounds) -- cf. Gfx/ModelCache.cpp.
    return modelCache_->GetForPlayerBody(race, gender, costumeSlot0, costumeSlot1);
}

// ===========================================================================
//  Placeholder cube (D3DXCreateBox + fixed pipeline, flat color)
// ===========================================================================

bool WorldRenderer::buildPlaceholderCube(IDirect3DDevice9* dev) {
    HRESULT hr = D3DXCreateBox(dev, 1.0f, 1.0f, 1.0f, &cubeMesh_, nullptr);
    if (FAILED(hr)) {
        TS2_ERR("WorldRenderer: D3DXCreateBox a echoue (0x%08lX)", hr);
        return false;
    }
    return true;
}

void WorldRenderer::drawPlaceholderCube(const D3DXVECTOR3& pos, float scale, D3DCOLOR color,
                                        float rotYDeg, const D3DXMATRIX& view, const D3DXMATRIX& proj) {
    if (!cubeMesh_ || !device_) return;

    const float sz = (scale > 0.05f) ? scale : 1.0f;
    D3DXMATRIX s, r, t, world, tmp;
    D3DXMatrixScaling(&s, sz, sz, sz);
    // Y rotation (mission ROTATION/ORIENTATION, 2026-07-14): same degree convention as
    // MeshRenderer::DrawModel / Model_Render 0x40EBB0 (S*Rz*Ry*Rx*T; here only Ry is
    // non-identity, as in the binary for this channel).
    D3DXMatrixRotationY(&r, D3DXToRadian(rotYDeg));
    // Places the cube on the ground (base at pos.y), not centered on pos.y, to stay legible
    // next to a future real model whose origin is also on the ground.
    D3DXMatrixTranslation(&t, pos.x, pos.y + sz * 0.5f, pos.z);
    D3DXMatrixMultiply(&tmp, &s, &r);
    D3DXMatrixMultiply(&world, &tmp, &t);

    // Switch back to the fixed pipeline (MeshRenderer leaves its skinned shaders bound).
    device_->SetVertexShader(nullptr);
    device_->SetPixelShader(nullptr);
    device_->SetVertexDeclaration(nullptr);
    device_->SetFVF(cubeMesh_->GetFVF());

    device_->SetTransform(D3DTS_WORLD, &world);
    device_->SetTransform(D3DTS_VIEW, &view);
    device_->SetTransform(D3DTS_PROJECTION, &proj);

    // Flat color via TFACTOR (no light/material dependency): robust and legible regardless of
    // the renderer's light state.
    device_->SetRenderState(D3DRS_LIGHTING, FALSE);
    device_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    device_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    device_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    device_->SetRenderState(D3DRS_TEXTUREFACTOR, color);
    device_->SetTexture(0, nullptr);
    device_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TFACTOR);
    device_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1);
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TFACTOR);

    // Debug net (bullet 4, task W3-F1): the cube is now only a traceability FALLBACK (never
    // drawn once a model/palette resolves) -> rendered wireframe to visually flag "model not
    // resolved" without hiding the scene. NO IDA anchor: the cube doesn't exist in the original
    // binary — a debug fallback, not fidelity.
    device_->SetRenderState(D3DRS_FILLMODE, D3DFILL_WIREFRAME);
    cubeMesh_->DrawSubset(0);
    device_->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);

    // Restores the standard texturing state (texture*diffuse modulation) expected by the
    // frame's next sprite/mesh blits.
    device_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    device_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    device_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    device_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);

    // AUDIT FIX (same class as the CharSelect crash, cf. MeshRenderer.h:369-380): this cube set
    // SetVertexShader/PixelShader(nullptr) on the SHARED device (above, in this same function)
    // without MeshRenderer knowing -> its currentPass_ cache is now stale. Without this
    // invalidation, the NEXT skinned model of the SAME pass (currentPass_ unchanged) skips the
    // VS/PS re-bind in DrawSkinnedSubset (MeshRenderer.cpp `if (currentPass_ != usePass)`) and
    // draws with the cube's NULL VS (fixed pipeline, misplaced/invisible) — as soon as a missing
    // asset sits next to a resolved model of the same pass in the body loop.
    meshRenderer_.InvalidateShaderBindingCache();
}

// ===========================================================================
//  Render — walks game::g_World.players/monsters/npcs
// ===========================================================================

void WorldRenderer::Render(const gfx::Camera& camera) {
    if (!ready_ || !device_) return;
    // The minimal SilverLining sky pass may be drawn before this render call; it breaks the
    // shared device's shader state cache. We therefore invalidate our own cache before drawing
    // entities.
    meshRenderer_.InvalidateShaderBindingCache();

    D3DXMATRIX view, proj, viewProj;
    camera.BuildViewMatrix(view);
    const float aspect = (screenH_ > 0)
        ? static_cast<float>(screenW_) / static_cast<float>(screenH_)
        : 1.0f;
    camera.BuildProjMatrix(proj, aspect);
    D3DXMatrixMultiply(&viewProj, &view, &proj);

    meshRenderer_.SetCamera(view, proj);
    // In-world character visibility fix (interim, ahead of the full G1 atmosphere pass). Before,
    // the character was rendered with MeshRenderer's DEFAULT light (ambient 0.3 / diffuse 0.7):
    // seen from behind in 3rd person, its camera-facing side stayed dark. We apply the SAME
    // PROVEN light as the CharSelect preview (D3DLIGHT9 0x18C5358, Scene_CharSelectRender
    // @0x51D034: dir=normalize(-1,-1,-1), ambient=diffuse=(0.8,0.8,0.8)) — the high 0.8 ambient
    // keeps the character clearly visible from every angle, consistent with the bright terrain.
    // TODO G1 (DEEP IDA render/sky): derive dir+color from the zone atmosphere's SUN
    // (Env_UpdateSunLight 0x412210: Diffuse=sun, Ambient=horizon, Dir=-normalize(sunDir), day/night
    // .ATM) and apply it to the terrain too for consistency.
    {
        D3DXVECTOR3 lightDir(-1.0f, -1.0f, -1.0f);
        D3DXVec3Normalize(&lightDir, &lightDir);
        meshRenderer_.SetLight(lightDir, D3DXVECTOR3(0.8f, 0.8f, 0.8f), D3DXVECTOR3(0.8f, 0.8f, 0.8f));
    }

    game::DrawCullContext cull;
    if (!game::g_World.players.empty()) {
        const game::PlayerEntity& self0 = game::g_World.players[0];
        cull.localPlayerPos = { self0.x, self0.y, self0.z };
    }
    const D3DXVECTOR3 eye = camera.Eye();
    cull.cameraPos = { eye.x, eye.y, eye.z };

    font_.BeginBatch();

    // F_ENTITY3D (B8): all entities to draw (players/monsters/npcs) are first COLLECTED into a
    // single buffer, so the PLANAR SHADOWS (renderPlanarShadows) can be drawn BEFORE the opaque
    // bodies (renderOne) — like the 0x52D9DC..0x52DB15 shadow bracket precedes the opaque pass in
    // Scene_InGameRender 0x52D0B0. The 4 loops below no longer draw directly: they fill
    // `drawables` (same active/far-cull guards as before -> same entities).
    std::vector<DrawableEntity> drawables;

    // Players (index 0 = self, cf. Game/GameState.h). Char_Draw applies to every active entity,
    // including the local player -> no index skip here; only `notSelf` (original a3)
    // distinguishes the local player for the nameplate.
    for (size_t i = 0; i < game::g_World.players.size(); ++i) {
        const game::PlayerEntity& p = game::g_World.players[i];
        if (!p.active) continue;

        DrawableEntity ent{};
        ent.renderState.active = true;
        ent.renderState.pos    = { p.x, p.y, p.z };
        ent.renderState.hp     = p.hp;
        // Y rotation (mission ROTATION/ORIENTATION, 2026-07-14): PlayerEntity::heading
        // (body+252 = move-state+36, degrees), cf. Game/GameState.h and
        // Game/EntityManager.cpp::ReadPlayerPos for the full decompilation evidence
        // (Char_Draw 0x5805C0 + CharAnimState::facingCurrentDeg, same offset). Consumed by
        // ComputeBodyMeshPlacement -> rotDeg.y below (renderOne).
        ent.renderState.facingOrAnimTimer = p.heading;
        // TODO(fidelity): scaleY/info (drawSize/modelCategoryId) aren't ported yet by
        // PlayerEntity (opaque +0x18 payload, cf. Game/GameState.h) -> neutral fallback (scale 1).
        ent.notSelf = (i != 0);
        // REAL name (mission NAMEPLATES, 2026-07-14): PlayerEntity::name, extracted by
        // EntityManager::ReadPlayerName from the network body (body+48, cf. GameState.h and
        // Game/EntityManager.cpp for the Char_DrawNameplate 0x56EF40 decompilation evidence).
        // "Player#i"/"Self" fallback ONLY if the field is still empty (e.g. the frame before the
        // first Pkt_SpawnCharacter for this slot is received) -- should no longer happen in
        // practice once the spawn has been processed.
        ent.name = !p.name.empty() ? p.name : ((i == 0) ? "Self" : ("Player#" + std::to_string(i)));
        ent.placeholderColor = (i == 0) ? 0xFF3070FFu : 0xFF60A0FFu; // blue (self brighter)
        // Real weapon: local player (i==0) via SelfState::equip[7] (slot 7 = weapon, already kept
        // continuously up to date by the equipment systems, cf. header banner); remote players
        // (i!=0) via PlayerEntity::body+148 (LE u32), offset RESOLVED by decompilation (twin
        // function pair Weapon_ClassFromEquip/Weapon_ClassFromField56, cf. WorldRenderer.h banner
        // and kPlayerBodyWeaponItemIdOffset above): this field is populated by the raw network
        // Pkt_SpawnCharacter payload memcpy, so it's available as soon as the remote entity spawns.
        ent.weaponItemId = (i == 0)
            ? game::g_World.self.equip[7].itemId
            : ReadBodyU32LE(p.body, kPlayerBodyWeaponItemIdOffset);

        // Real base body (mission "base player body wiring", 2026-07-14, cf. "PLAYERS" header
        // banner + Gfx/ModelCache.h::GetForPlayerBody): race/gender/costume read directly from
        // p.body, WITHOUT self/remote distinction (no separate self global known unlike the
        // weapon -- cf. banner: these offsets are mutated IN PLACE in entity[0], hence in this
        // same body, by Pkt_ItemActionDispatch). Populated as soon as spawned
        // (Pkt_SpawnCharacter copies the entire body).
        ent.hasBody          = true;
        ent.bodyRace         = static_cast<int>(ReadBodyU32LE(p.body, kPlayerBodyRaceOffset));
        ent.bodyGender       = static_cast<int>(ReadBodyU32LE(p.body, kPlayerBodyGenderOffset));
        ent.bodyCostumeSlot0 = static_cast<int>(ReadBodyU32LE(p.body, kPlayerBodyCostumeSlot0Offset));
        ent.bodyCostumeSlot1 = static_cast<int>(ReadBodyU32LE(p.body, kPlayerBodyCostumeSlot1Offset));
        // G3 (DEEP IDA #5) — equipped body armor: equip[2]=body+92+8*2=body+108 (torso token 003),
        // equip[5]=body+92+8*5=body+132 (legs token 004). 0 = base body. Char_DrawWeaponTrailEffect
        // 0x55E9D0 def_55FF4D 0x5603AA (13x MobDb_GetEntry on body+92+8*k, stride 8).
        ent.torsoItemId      = ReadBodyU32LE(p.body, 108);
        ent.legsItemId       = ReadBodyU32LE(p.body, 132);

        // PLAYER ANIMATION (front F_PLAYERANIM, 2026-07-17) — SAME pattern as monsters/NPCs (cf.
        // the monster loop below and renderOne): animType = entity+244 FSM state
        // (CharAnimState::state, populated from the network by Game/EntityManager.cpp:390 =
        // body+220), which SELECTS the clip (PcModel_ResolveEquipSlot 0x4E46A0, base + 156*state).
        // animCursor = entity+248 cursor (CharAnimState::animFrame), advanced by
        // game::Player_AdvanceAnimCursor (Game/PlayerAnimCursorTick.h) in the UPDATE phase
        // (frame += dt*30, wrap by subtraction — Char_TickMoveState 0x574830 @0x574922). Holds for
        // self (i==0) AND remotes without distinction (the state comes from the same entity+244
        // field for everyone, index 0 being the g_SelfActionState 0x1687328 alias).
        //
        // NON-REGRESSION GUARD (recon blocker, cf. game::Player_AnimCursorTickIsWired): until MAIN
        // calls Player_AdvanceAnimCursor per frame (Scene/SceneManager.cpp, host.UpdateEntityAnimFrame
        // — cf. front report / integrationForMain), the cursor stays 0 and hasAnimCursor=false ->
        // SampleByGameTime fallback (CORRECT clip via animType, but a global clock). SampleByCursor
        // (animFrame) is only consumed if the cursor is actually advancing, otherwise ALL players
        // would be frozen at frame 0 (strictly worse than the in-phase idle).
        ent.animType      = p.anim.state;                       // entity+244, clip selector (network)
        ent.animSlot      = p.anim.animSlot;                    // entity+240 = 2*weaponClass (weapon pose, G5 DEEP IDA)
        ent.animCursor    = p.anim.animFrame;                   // entity+248, real cursor (advanced in UPDATE)
        ent.hasAnimCursor = game::Player_AnimCursorTickIsWired();

        // WEAPON TRAIL (front F_WEAPONTRAIL, 2026-07-17) — master gate of
        // Char_DrawWeaponTrailEffect 0x55E9D0 (opaque) / Char_DrawWeaponEffectVariantB 0x56BF90
        // (shadow): weaponAnimSlot (entity+220 = this+55, active skill/weapon anim id) -> switch
        // game::ResolveWeaponTrailIndex; altWeaponSet (entity+576 = this+144) must be 0. Read from
        // CharAnimState, valid for self (i==0) AND remotes without distinction (same
        // entity+220/+576 field for everyone). Warning: NOT fed from the network yet on the
        // ClientSource side (CharAnimState::weaponAnimSlot/altWeaponSet aren't written anywhere —
        // cf. front report / integrationForMain) -> weaponAnimSlot is 0 -> the gate fails -> NO
        // trail is emitted (clean degradation). Resolves itself once MAIN populates these fields
        // (EntityManager, body+196 / body+552 = entity+220 / entity+576).
        ent.weaponAnimSlot = p.anim.weaponAnimSlot;             // entity+220 = this+55
        ent.altWeaponSet   = p.anim.altWeaponSet;               // entity+576 = this+144
        // reflectionEligible stays false (default): Char_DrawReflection 0x581090 is NEVER called
        // on g_EntityArray in the original binary (single caller @0x52DB09, in the monster loop)
        // -> wiring drawReflectionOverlay() HERE would be an invention.
        // BUT NOTE (fixed Pass 4 / W5, front shadow-wiring): this does NOT mean players have no
        // shadow in the original client. They do, drawn in the same shadow-pass bracket
        // (0x52D9DC GXD_SetupStencilShadowState .. 0x52DB15 GXD_EndStencilShadowState) by
        // Char_DrawWeaponEffectVariantB 0x56BF90 @0x52DA41 (`&g_EntityArray[908*i]`), which
        // reaches Model_RenderPlanarShadow 0x40F720 via SObject_DrawAnimated2 0x4D91C0. This
        // player shadow is NOT wired here: it requires Collision_SegPickA 0x420D60's ground plane
        // (absent from ClientSource) and its extent depends on the this+0xDC gate [NOT VERIFIED,
        // undecompiled 11 KB mega-switch].
        // TODO [anchors 0x56BF90 + 0x40F720 + 0x420D60]: cf. WorldRenderer.h §Shadow [B] banner.
        // (Player shadow: NOW wired via renderPlanarShadows, cf. bottom of Render().)
        drawables.push_back(std::move(ent));
    }

    // Monsters.
    for (size_t i = 0; i < game::g_World.monsters.size(); ++i) {
        const game::MonsterEntity& m = game::g_World.monsters[i];
        if (!m.active) continue;

        DrawableEntity ent{};
        ent.renderState.active = true;
        ent.renderState.pos    = { m.x, m.y, m.z };
        ent.renderState.hp     = m.hp;
        // Y rotation (mission ROTATION/ORIENTATION, 2026-07-14): MonsterEntity::heading
        // (body+40 = move-state+36, degrees) -- CONFIRMED DIRECTLY by decompiling Char_Draw
        // 0x5805C0 (this = &dword_1766F74[i], cf. Game/GameState.h and
        // Game/EntityManager.cpp::ReadMonsterPos for the full evidence).
        ent.renderState.facingOrAnimTimer = m.heading;
        ent.notSelf = true;
        ent.name    = "Monster#" + std::to_string(i);
        ent.placeholderColor = 0xFFE04040u; // red
        // monsterDefId = body[0] (mob id -> MONSTER_INFO), SAME convention as
        // Game/EntityManager.cpp::ResolveMobDef (raw id received from the network, no -1) --
        // ModelCache::GetForMonster() applies exactly the same read internally.
        uint32_t defId = 0;
        std::memcpy(&defId, m.body.data(), sizeof(defId));
        ent.monsterDefId = defId;

        // PER-MONSTER ANIMATION (Pass 4 / W7, gaps as-motion-01 + as-motion-02) — Char_Draw
        // 0x5805C0: animType = slot+24 (@0x580770), cursor = slot+28 (@0x580828). Authoritative
        // C++ source = game::g_MonsterTickExt[i] (Game/EntityLifecycleTick.h), whose
        // `.motionState`/`.animFrame` carry EXACTLY these two offsets and are now fed by
        // game::Monster_DispatchMotionTick (Game/AnimationTick.h §5, the 9 Char_MotionTick_* +
        // their @0x5822D3 dispatch).
        // NOT `m.anim` (GameState.h:225): that CharAnimState is modeled on PLAYER offsets
        // (entity+244/+248) and is DEAD for monsters — cf. Game/AutoTargetCombatGate.h:106-112.
        //
        // NON-REGRESSION GUARD (cf. game::Monster_MotionTickIsWired): until the
        // EntityLifecycleTickHost::DispatchMotionTick hook is assigned (Scene/SceneManager.cpp),
        // the tick doesn't run and animFrame would stay stuck at 0 -> monsters TOTALLY FROZEN,
        // strictly worse than the old global clock. The per-entity cursor is therefore only
        // consumed if it's actually fed; otherwise the old animated fallback is kept. This guard
        // is NOT binary behavior: to be removed once the wiring is locked in.
        if (i < game::g_MonsterTickExt.size()) {
            const game::MonsterTickExt& mext = game::g_MonsterTickExt[i];
            ent.animType      = mext.motionState; // slot+24 @0x580770
            ent.animCursor    = mext.animFrame;   // slot+28 @0x580828
            ent.hasAnimCursor = game::Monster_MotionTickIsWired();
        }
        // Char_DrawReflection 0x581090 (= MONSTER planar shadow, cf. WorldRenderer.h §Shadow/
        // reflection [B] banner -- the IDB's "reflection" name is misleading): single caller in
        // the whole binary, inside THIS loop (`&dword_1766F74[280*i]` @0x52DB09 in
        // Scene_InGameRender). So it's right here, and only here, that reflectionEligible must be
        // set to true -- the player/NPC shadows go through other functions of the same bracket
        // (@0x52DA41 / @0x52DAA2), not wired.
        ent.reflectionEligible = true;
        drawables.push_back(std::move(ent));
    }

    // GAMEPLAY NPC (game::g_World.npcs, fed by Pkt_SpawnNpc opcode 0x13) — NpcEntity::x/y/z
    // (body+16/20/24) confirmed by Hex-Rays decompilation of Char_SelectAuraEffect 0x5835B0 (cf.
    // Game/EntityManager.cpp and Game/GameState.h::NpcEntity) and fed by EntityManager::OnSpawnNpc.
    // NPC = never "self" for the nameplate.
    //
    // NO 3D BODY (RE idaTs2 2026-07-15, mission "PNJ GAMEPLAY SANS CORPS"): ent.bodyMeshEligible=
    // false -> renderOne emits neither model nor cube. PROVEN that the original NEVER draws a
    // body for dword_17AB534: (1) data_refs 0x17AB598 (the network NPC's `def` field) is read
    // ONLY by interaction/autoplay (Npc_Interact/AutoPlay_*), by NO rendering function; (2) the 3
    // network NPC loops of Scene_InGameRender 0x52D0B0 (0x52dc84/0x52ec5b/0x52fcae) only call
    // Char_DrawAura / Fx_DrawZoneAura / ModelObj_Draw(quest marker) / Char_DrawNameTag -- never
    // SObject_DrawEx or Char_Draw. The 3D body of visible NPCs is EXCLUSIVELY rendered by
    // Npc_DrawMesh 0x57FF00 on the SEPARATE g_NpcRenderArray 0x1764D14 array (populated by
    // Pkt_EnterWorld 0x464160), modeled by the DECOR NPC loop (ZoneNpcs) right below. The yellow
    // cube was therefore an INFIDELITY -> removed. reflectionEligible stays false (same: no NPC
    // reflection).
    //
    // ///// W9 STATE (front nameplate-entity): THIS LOOP NO LONGER EMITS ANYTHING /////
    // Before W9 it emitted a nameplate (via renderOne) — one more INFIDELITY: the binary draws NO
    // label for dword_17AB534 — its only label function (Char_DrawNameTag 0x583470) has a single
    // xref @0x52FCD9, inside the dead `dword_1668F64 == 1` block, and click category 6 (the one
    // World_PickEntityAtCursor assigns to this array) has no associated drawing (case 6
    // @0x5311A0: cursor only). The loop is KEPT (not removed) as the anchor point for the only
    // drawing the binary actually does on this array, currently NOT portable:
    //   TODO [anchor Char_DrawAura 0x583400 @0x52DCB1 (pass 1) / @0x52EC88 (pass 2)] — gap erp-08:
    //   `if (this && pass in [1,2] && this[27] != -1) ModelObj_Draw(unk_B60AB8 +
    //   148*this[27], pass, 0.0, this+32, this+35)`. BLOCKED: ModelObj_Draw 0x4D71B0 and the
    //   unk_B60AB8 template table aren't modeled in ClientSource, and game::NpcEntity doesn't
    //   expose the +27 field (aura template id). Conditional effect (only if this id is set) ->
    //   drawing nothing remains the correct behavior as long as the source doesn't exist.
    //
    // ///// FLEET C / FRONT C3 — GOAL2: MORPH ZONE AURA (Fx_DrawZoneAura 0x583F90) — TODO-anchor.
    // The binary draws a morph aura on THIS array (dword_17AB534) in the Scene_InGameRender
    // 0x52D0B0 bracket, TWICE (Fx_DrawZoneAura @0x52dd70 pass 1 / @0x52ed47 pass 2 — xrefs
    // verified in IDA). Body of 0x583F90 (decompiled, re-checked this session):
    //     if (*(a1) && a3 in [1,2]) {
    //       switch (g_SelfMorphNpcId) { 1:v7=0  6:v7=1  11:v7=2  140:v7=3 }   // else v7 stays 0
    //       slot = &unk_B60AB8 + 148*v7 + 34040;  // 34040/148 = 230 -> idxC = 230 + v7 (MiscC bank)
    //       if (ModelObj_GetSubObjectCount(slot,0) > 0) {
    //         frame = ftol(g_GameTimeSec*30) % subCount;
    //         ModelObj_Draw(slot, a3 /*pass*/, frame, a1+24 /*pos*/, &orient0 /*=0,0,0*/, 0); } }
    // => idxC in {230,231,232,233} (E{idxC+1}001.MOBJECT), selected by g_SelfMorphNpcId (== zone id
    // == game::g_World.zoneId, AVAILABLE) but ONLY for zones {1,6,11,140}.
    // NOT WIRED HERE, intentionally (the consumer isn't reachable from WorldRenderer):
    //   1. Rendering goes through ModelObjectRenderer's MiscC bank (Gfx/ModelObjectRenderer.h,
    //      front F_MOBJ: idxC 230..233 are documented there as "not routed"). WorldRenderer OWNS
    //      no ModelObjectRenderer and can't receive one without changing its public signature
    //      called by Scene/SceneManager.cpp (forbidden by the C3 disjunction constraint).
    //      Moreover the ModelObjectRenderer_MeshDrawShim only admits ONE active renderer
    //      (registered by FxRenderer): instancing a 2nd one here would break the existing FX mesh
    //      hook.
    //   2. The per-entity gate (`*(a1)` active + a1+24 position) is on dword_17AB534 (this loop),
    //      but no entity has an aura without runtime morph state -> depends on an unported state.
    // -> GOAL2 = documented + TODO-anchor [0x583F90 / ModelObjectRenderer MiscC idxC 230..233 /
    //    g_SelfMorphNpcId]. To be wired by MAIN: route Fx_DrawZoneAura to
    //    ModelObjectRenderer::MeshDraw (bank MiscC, idxC=230+v7) from the scene pass that owns the
    //    object renderer, not from here.
    for (size_t i = 0; i < game::g_World.npcs.size(); ++i) {
        const game::NpcEntity& n = game::g_World.npcs[i];
        if (!n.active) continue;

        // NPC far-cull faithful to Npc_DrawMesh 0x57FF00 (cf. constant above): absent from
        // ComputeEntityDrawFlags (which only models Char_Draw, no far-cull). Computed BEFORE
        // renderOne, as in the binary (immediate return if > 1000 units from the local player,
        // before the camera near-cull).
        const float dx = n.x - cull.localPlayerPos.x;
        const float dy = n.y - cull.localPlayerPos.y;
        const float dz = n.z - cull.localPlayerPos.z;
        if ((dx * dx + dy * dy + dz * dz) > kNpcFarCullDistanceSq) continue;

        DrawableEntity ent{};
        ent.renderState.active   = true;
        ent.renderState.pos      = { n.x, n.y, n.z };
        ent.renderState.hp       = 0; // TODO(fidelity): NpcEntity doesn't carry HP/a bar.
        ent.notSelf              = true;
        ent.bodyMeshEligible     = false; // no body (cf. banner) -> nameplate only
        // REAL NPC name = ITEM_INFO.name (+4, 25-byte cstring) via NpcEntity::def, now resolved
        // against the ITEM_INFO table (EntityManager::ResolveNpcDef = MobDb_GetEntry (mITEM), cf.
        // Pkt_SpawnNpc 0x467EC0). No fabrication: if def is null or the name is empty, ent.name is
        // left empty -> no nameplate (instead of the old "Npc#i").
        if (n.def) {
            const char* nm = reinterpret_cast<const char*>(n.def) + 4; // ITEM_INFO.name
            ent.name.assign(nm, ::strnlen(nm, 25));
        }
        // Gameplay NPC: bodyMeshEligible=false -> neither body nor shadow (renderPlanarShadows skips it).
        drawables.push_back(std::move(ent));
    }

    // DECOR NPC (game::ZoneNpcs(), Game/StaticNpcLoader.h) — DISTINCT SOURCE from the gameplay
    // NPC loop above (cf. WorldRenderer.h §"NPC" header banner for the full evidence):
    //   - g_World.npcs (previous loop) = GAMEPLAY array, fed by the network packet Pkt_SpawnNpc
    //     (opcode 0x13) -- interaction/targeting, EMPTY in practice for decor NPCs (merchants,
    //     guards, quest givers...) which have NO dedicated network packet.
    //   - game::ZoneNpcs() (this loop) = client-source EQUIVALENT of g_NpcRenderArray 0x1764D14,
    //     repopulated locally from the static mZONENPCINFO table by StaticNpcLoader::LoadZoneNpcs()
    //     (triggered by EntityManager::OnSpawnCharacter on the local player's spawn, cf.
    //     StaticNpcLoader.h header banner) -- this is THE REAL SOURCE of the decor NPCs visible
    //     in-game in the original binary (Npc_DrawMesh 0x57FF00 reads ONLY g_NpcRenderArray, never
    //     dword_17AB534).
    // SAME rendering pipeline as every other entity (renderOne()): real model via
    // ModelCache::GetForNpc(*def) if the NPC's kindIndex (NpcDefRecord::fieldE) resolves an
    // N*.SOBJECT file on disk (cf. ResolveNpcModel above), otherwise a YELLOW cube fallback (same
    // color as the gameplay loop, to stay visually consistent) -- never a blank screen. `def` is
    // never null here: StaticNpcLoader::LoadZoneNpcs() only adds a slot to ZoneNpcs() if
    // GetNpcDefRecord(kindId) succeeded (cf. StaticNpcLoader.cpp).
    for (size_t i = 0; i < game::ZoneNpcs().size(); ++i) {
        const game::StaticNpcSlot& n = game::ZoneNpcs()[i]; // = NpcRenderEntry (unified W7 pool)

        // W7 "npc-array-unify": ZoneNpcs() returns 100 FIXED slots, including inactive HOLES
        // (def==nullptr, pos 0,0,0). The contract requires testing `active` before any use (cf.
        // Game/StaticNpcLoader.h / GameState.h) -- faithful to Npc_DrawMesh 0x57FF00 /
        // Npc_RenderSlotTick 0x5803A0's `*(this+1)` guard. Before W7, ZoneNpcs() was compacted
        // (only occupied slots): without this guard, phantom cubes would now be drawn at the origin.
        if (!n.active) continue;

        // Same NPC far-cull as the gameplay loop above (Npc_DrawMesh 0x57FF00, cf.
        // kNpcFarCullDistanceSq at the top of this file): this guard applies to EVERY NPC drawn
        // through this pipeline, regardless of its data source (gameplay or decor) -- Npc_DrawMesh
        // is THE drawing function for BOTH (g_NpcRenderArray carries both NPC categories on the
        // original binary side, cf. Docs/TS2_NPC_MESH_DRAW.md).
        const float dx = n.x - cull.localPlayerPos.x;
        const float dy = n.y - cull.localPlayerPos.y;
        const float dz = n.z - cull.localPlayerPos.z;
        if ((dx * dx + dy * dy + dz * dz) > kNpcFarCullDistanceSq) continue;

        DrawableEntity ent{};
        ent.renderState.active = true;
        ent.renderState.pos    = { n.x, n.y, n.z };
        // PER-NPC ANIMATION (gaps as-motion-01 + as-motion-02) — Npc_DrawMesh 0x57FF00: animType =
        // slot+12 (@0x57ffa0, arg 3 of Model_GetNpcMeshSlot), cursor = slot+16 (@0x57fff1,
        // SObject_DrawEx animTime), displayed angle = slot+44. W7 ADAPTATION: these three values
        // are the NATIVE fields of the unified NpcRenderEntry pool (mode/frameAcc/angle), fed by
        // game::ZoneNpc_TickAnim (Game/AnimationTick.h §6, port of Npc_RenderSlotTick 0x5803A0 /
        // _Loop 0x580400 / _Once 0x5804A0) which ticks the pool DIRECTLY -- no more parallel
        // vector. The angle (+44) is the ONLY mutable one: turns toward the player when a dialogue
        // opens (@0x5dc0a2), resets to baseline beyond 400 units (@0x58048e). Consumed by
        // ComputeBodyMeshPlacement -> rotDeg.y (renderOne), same channel as
        // PlayerEntity::heading/MonsterEntity::heading.
        ent.renderState.facingOrAnimTimer = n.angle;    // slot+44
        ent.animType   = n.mode;                        // slot+12
        ent.animCursor = n.frameAcc;                    // slot+16
        // SAME non-regression guard as the monster loop (cf. game::Monster_MotionTickIsWired):
        // game::ZoneNpc_TickAnim is to date called NOWHERE (wiring to be set up by the
        // orchestrator in Scene/SceneManager.cpp after InGameTickFlow_Update, cf.
        // Game/AnimationTick.h §6). As long as it doesn't run, frameAcc stays 0 -> consuming the
        // cursor would freeze NPCs on frame 1, whereas the old SampleByGameTime fallback animated
        // them (in phase). The per-entity cursor is therefore only consumed if it's actually fed.
        // Resolves itself once the wiring is in place.
        ent.hasAnimCursor = game::ZoneNpc_AnimTickIsWired();
        ent.renderState.hp = 0; // TODO(fidelity): no HP bar for a decor NPC.
        ent.notSelf = true;
        // Real name (NpcDefRecord::name, 25-byte cstring) if available; "ZoneNpc#i" fallback in
        // the (normally impossible here, cf. LoadZoneNpcs guard) case where def would be null.
        ent.name = (n.def && n.def->name[0] != '\0')
            ? std::string(n.def->name, ::strnlen(n.def->name, sizeof(n.def->name)))
            : ("ZoneNpc#" + std::to_string(i));
        ent.placeholderColor = 0xFFF0E020u; // yellow -- same color as the gameplay NPC loop
        // Real model: non-null npcDef -> ResolveNpcModel()/ModelCache::GetForNpc() in renderOne
        // (cf. WorldRenderer.h §"NPC" header banner); cube fallback if fieldE is out of bounds
        // [1,66] or the file isn't found on disk (never an exception).
        ent.npcDef = n.def;
        // reflectionEligible stays false (default): Char_DrawReflection 0x581090 is never called
        // on an NPC array -- same rule as the gameplay NPC loop.
        // DECOR NPC planar shadow (Npc_DrawMeshGlow 0x5801D0 @0x52DAA2 in the 0x52D9DC..0x52DB15
        // bracket -> SObject_DrawAnimated2 0x4D91C0 -> Model_RenderPlanarShadow 0x40F720): NOW
        // wired (front F_ENTITY3D / B8), via renderPlanarShadows (bottom of Render()).
        // game::ZoneNpcs() IS the client-source equivalent of the g_NpcRenderArray (stride 88) that
        // Npc_DrawMeshGlow reads -> the right source. reflectionEligible stays false (not read by
        // the pass).
        drawables.push_back(std::move(ent));
    }

    // ///// PLANAR SHADOW PASS — Wave B / branch B8 (front F_ENTITY3D) /////
    // BEFORE the opaque bodies (the 0x52D9DC..0x52DB15 bracket draws shadows before the opaque
    // pass in Scene_InGameRender 0x52D0B0). Clean no-op if collisionSource_ isn't set (no ground
    // plane).
    renderPlanarShadows(drawables, cull);

    // ///// OPAQUE BODIES — renderOne per entity (Char_Draw 0x5805C0), AFTER the shadows /////
    for (const DrawableEntity& d : drawables)
        renderOne(d, cull, view, proj, viewProj);

    // ///// LABEL PASS — Pass 4 / wave W9, front nameplate-entity /////
    // AFTER the 4 body loops, as in Scene_InGameRender 0x52D0B0 where labels are emitted in the
    // 2D block opened by Gfx_Begin2D @0x52FB89, once all 3D passes are done — and NEVER from
    // Char_Draw. Emits AT MOST ONE label per frame (the hovered entity's), faithful to the
    // @0x530FC7 switch.
    drawNameplatePass(camera, cull, viewProj);

    font_.EndBatch();
}

} // namespace ts2

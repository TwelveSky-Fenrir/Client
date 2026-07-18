// Scene/SceneManager_Update.cpp -- SceneManager::Update (cSceneMgr_Update 0x517BF0): per-frame
// scene dispatch tick. Split out of SceneManager.cpp (mechanical relocation, same class); see
// SceneManager.cpp for lifecycle/scene-change and SceneManager_Render.cpp for drawing.
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

// TODO logged ONCE per integration point (avoids log flood for hooks called at 30 Hz from
// InGameTickFlow_Update -- cf. case Scene::InGame below).
#define TS2_INGAME_TODO_ONCE(...) do { \
        static bool s_ts2IngameTodoWarned = false; \
        if (!s_ts2IngameTodoWarned) { s_ts2IngameTodoWarned = true; TS2_LOG(__VA_ARGS__); } \
    } while (0)

namespace ts2 {

// GINFO-003 -- mZONEMOVEINFO sub-tables A/B ("G02_GINFO\003.BIN", 350x805 FLOAT, original
// base flt_1555D08, loaded by Motion_LoadGInfo003Bin 0x4FD420 and exposed here via
// game::LoadedCoordTable(), cf. Game/MotionPools.h).
//
// Row layout RE-DERIVED from the THREE only readers of this table in the binary:
//   GInfo2_GetVec3           0x4FD4C0: row[0..2]    = position (x,y,z)      [already ported]
//   Combo_FindNearbyFollowup 0x501270: row[3]       = countA        (DWORD) @0x501290
//                                       row[4+3i]    = candidate A vec3 (i < countA)
//                                       row[304+i]   = idA           (DWORD) @0x501337
//   GInfo2_FindVec3ByKey     0x4FD540: row[404]     = countB        (DWORD) @0x4FD583
//                                       row[705+i]   = key B         (DWORD) (i < countB)
//                                       row[405+3i]  = vec3 B                @0x4FD5FF/620/642
// Sum 3+1+300+100+1+300+100 = 805 -> the row is FULLY explained (the
// GINFO-003-SUBTABLES-UNWIRED gap was about these 802 never-read floats).
//
// `row` 0-based = 805*(motionId-1): the binary indexes 1-based via `805*a2 - 805`.
// WARNING: count/key/id fields are read as DWORD ON the FLOAT table (binary pattern
// `*((_DWORD *)this + ...)`) -> a binary bit REINTERPRETATION, definitely NOT a
// float->int conversion.
namespace {

// Reinterprets the FLOAT table's word at index `idx` as int32 (binary pattern
// `*((_DWORD *)this + idx)` of Combo_FindNearbyFollowup 0x501270 / GInfo2_FindVec3ByKey 0x4FD540).
int32_t GInfo2AsI32(const float* table, std::size_t idx) {
    int32_t v = 0;
    std::memcpy(&v, table + idx, sizeof(v));
    return v;
}

std::size_t GInfo2TableSize() {
    return static_cast<std::size_t>(game::kCoordTableRowCount) *
           static_cast<std::size_t>(game::kCoordTableRowStride);
}

// Sub-table A -- combo follow-up candidates for `motionId` (Combo_FindNearbyFollowup 0x501270:
// loop `i < row[3]`, position `row[4+3i]`, id `row[304+i]`). Final selection (distance < 30.0
// then Combo_CheckTransition == 1) is still done by game::Combo_FindNearbyFollowup.
std::vector<game::ComboMotionCandidate> GInfo2ComboCandidates(int motionId) {
    std::vector<game::ComboMotionCandidate> out;
    const float* table = game::LoadedCoordTable(); // flt_1555D08
    if (!table) return out;                        // table not loaded -> no candidates
    if (motionId < 1 || motionId > game::kCoordTableRowCount) return out; // guard @0x501286

    const std::size_t row = static_cast<std::size_t>(game::kCoordTableRowStride) *
                            static_cast<std::size_t>(motionId - 1);       // `805*a2 - 805`
    const std::size_t tableSize = GInfo2TableSize();

    const int32_t countA = GInfo2AsI32(table, row + 3);                   // row[3] @0x501290
    if (countA > 0 && countA <= 100) out.reserve(static_cast<std::size_t>(countA));

    for (int32_t i = 0; i < countA; ++i) {                                // @0x501290
        const std::size_t posIdx = row + 4 + 3 * static_cast<std::size_t>(i);  // row[4+3i]
        const std::size_t idIdx  = row + 304 + static_cast<std::size_t>(i);    // row[304+i]
        // The binary does NOT bound the loop to 100 (it reads `countA` as-is into a flat
        // global array): this guard is a MEMORY safety net for the std::vector, not a
        // behavior change -- for any well-formed row (countA <= 100) both reads stay inside
        // the row and the result matches the binary exactly.
        if (posIdx + 2 >= tableSize || idIdx >= tableSize) break;
        game::ComboMotionCandidate c;
        c.id = GInfo2AsI32(table, idIdx);   // @0x501337
        c.x  = table[posIdx + 0];
        c.y  = table[posIdx + 1];
        c.z  = table[posIdx + 2];
        out.push_back(c);
    }
    return out;
}

// Sub-table B -- GInfo2_FindVec3ByKey 0x4FD540: looks up `originKey` in the key list of
// `followupMotionId` (row[705+i], i < row[404]) and returns the associated vec3 (row[405+3i]).
// Not found / out of bounds -> {0,0,0}: the binary zeroes the output vec3 BEFORE the search
// (@0x4FD54E/555/55D) and leaves it as-is if the loop ends without a match.
void GInfo2ComboOrigin(int followupMotionId, int originKey, float outPos[3]) {
    outPos[0] = 0.0f; // @0x4FD54E
    outPos[1] = 0.0f; // @0x4FD555
    outPos[2] = 0.0f; // @0x4FD55D

    const float* table = game::LoadedCoordTable(); // flt_1555D08
    if (!table) return;
    if (followupMotionId < 1 || followupMotionId > game::kCoordTableRowCount) return; // @0x4FD56D

    const std::size_t row = static_cast<std::size_t>(game::kCoordTableRowStride) *
                            static_cast<std::size_t>(followupMotionId - 1);
    const std::size_t tableSize = GInfo2TableSize();

    const int32_t countB = GInfo2AsI32(table, row + 404);                 // row[404] @0x4FD583
    for (int32_t i = 0; i < countB; ++i) {
        const std::size_t keyIdx = row + 705 + static_cast<std::size_t>(i);   // row[705+i]
        if (keyIdx >= tableSize) return;                                       // memory safety
        if (GInfo2AsI32(table, keyIdx) != originKey) continue;
        const std::size_t posIdx = row + 405 + 3 * static_cast<std::size_t>(i); // row[405+3i]
        if (posIdx + 2 >= tableSize) return;                                    // memory safety
        outPos[0] = table[posIdx + 0]; // @0x4FD5FF
        outPos[1] = table[posIdx + 1]; // @0x4FD620
        outPos[2] = table[posIdx + 2]; // @0x4FD642
        return;
    }
    // Loop ended without a match (`i == countB` @0x4FD5DC) -> vec3 left at {0,0,0}.
}

// gx-fx-01 -- reconstruction of the monster attack-projectile spawn parameters.
// Fx_SpawnAttackProjectile 0x582530 (and its Alt variant 0x582A10) read their `this` as a
// MONSTER record (dword_1766F74, stride 280). This wiring (the pool's documented producer, cf.
// Game/GroundAuraWorldObjectTick.h:204 "Char_Update -> spawn, separate mission") fills
// game::FxProjectileSpawnParams from game::g_World.monsters[idx], its resolved MONSTER_INFO
// (m.def, +0x60), and g_MonsterTickExt[idx] (attack target). Every field carries the binary
// offset RE-PROVEN by direct decompilation of 0x582530 this mission (EA in comment); this+96 ==
// m.def (dereferenced without a guard @0x5825DF -> an active monster always has a resolved def,
// Pkt_SpawnMonster 0x467B00 rejects it otherwise, cf. EntityManager.cpp:410).
// Returns false (no spawn) if the index is invalid or m.def is null: a plain memory safety net,
// never hit for a well-formed active monster.
bool BuildMonsterProjectileParams(int idx, game::FxProjectileSpawnParams& p) {
    if (idx < 0 || static_cast<std::size_t>(idx) >= game::g_World.monsters.size()) return false;
    const game::MonsterEntity& m = game::g_World.monsters[static_cast<std::size_t>(idx)];
    if (!m.def) return false;                                   // *(this+96) @0x5825DF (active => def!=null)
    const uint8_t* def = reinterpret_cast<const uint8_t*>(m.def);

    p.owner     = m.id;                                          // *(this+4)/*(this+8)  @0x5825B5/0x5825C7
    p.startX    = m.x;                                           // *(float*)(this+32)   @0x58281F
    p.startYRaw = m.y;                                           // *(float*)(this+36)   @0x58283D (before + heightOffset)
    p.startZ    = m.z;                                           // *(float*)(this+40)   @0x58284F
    p.heading   = m.heading;                                     // *(float*)(this+56)   @0x58286F
    // targetX/Y/Z = *(float*)(this+44/48/52) = m.body[28/32/36] (body starts at record+16).
    std::memcpy(&p.targetX, m.body.data() + 28, sizeof(p.targetX)); // @0x5828D7
    std::memcpy(&p.targetY, m.body.data() + 32, sizeof(p.targetY)); // @0x5828E9
    std::memcpy(&p.targetZ, m.body.data() + 36, sizeof(p.targetZ)); // @0x5828FB
    // MONSTER_INFO fields (int32 at BYTE offsets of the def record; 944-byte record, safe bounds).
    std::memcpy(&p.weaponId,      def + 244, sizeof(p.weaponId));      // *(*(this+96)+244) @0x5825DF
    std::memcpy(&p.weaponSubtype, def + 236, sizeof(p.weaponSubtype)); // *(*(this+96)+236) @0x58268F
    std::memcpy(&p.heightOffset,  def + 328, sizeof(p.heightOffset));  // *(*(this+96)+328) @0x58283D
    std::memcpy(&p.speed,         def + 332, sizeof(p.speed));         // *(*(this+96)+332) @0x582913
    // target = *(this+68)/*(this+72) = MonsterTickExt::attackTargetId (populated by the network
    // combat AI, OUTSIDE this front -- hence the latency documented at the wiring site).
    // g_MonsterTickExt is sized by UpdateMonster (EnsureCapacity) before the hook is called;
    // defensive guard.
    if (static_cast<std::size_t>(idx) < game::g_MonsterTickExt.size())
        p.target = game::g_MonsterTickExt[static_cast<std::size_t>(idx)].attackTargetId; // @0x582601/0x582613
    return true;
}

} // namespace

void SceneManager::Update(double dt, gfx::Camera& camera) {
    ++frameCount_;
    // H2 -- op 0x18 Pkt_GameServerConnectResult 0x469CF0 sets g_SceneMgr=5 (@0x469d95, case 0 /
    // sub-result 0; confirmed in IDA: g_SceneSubState=0 @0x469d9f, dword_1676188=0 @0x469da9)
    // WHILE InGame (server reconnect/relay). The network handler (Net/GameHandlers_Misc.cpp
    // op 0x18) arms game::g_World.sceneReloadPending, but this flag is read ONLY by
    // case Scene::EnterWorld below -> never reached from InGame (dead reload). We reproduce
    // HERE the scene 6->5 switch: the switch then dispatches to case EnterWorld on the SAME
    // frame (like the binary where g_SceneMgr==5 before cSceneMgr_Update), which consumes the
    // flag via its existing sceneReloadPending branch (ReloadZone). Do NOT clear the flag here
    // (case EnterWorld will). // 0x469d95 / g_SceneMgr 0x1676180=5
    if (scene_ == Scene::InGame && game::g_World.sceneReloadPending) {
        Change(Scene::EnterWorld);
    }
    // SCN-01 -- consumes the two deferred actions of the notice OK button
    // (Notice_DispatchOkAction, SceneManager.cpp). The binary writes g_SceneMgr/g_QuitFlag
    // DIRECTLY from UI_NoticeDlg_OnLButtonUp; here we defer by one frame to avoid changing
    // scene mid click-routing (UIManager is iterating its dialog registry at that moment).
    // SAME pattern as sceneReloadPending above / sceneEnterWorldPending. The socket has
    // ALREADY been closed by Notice_DispatchOkAction, in the binary's order
    // (Net_CloseSocket @0x5C04DF BEFORE g_SceneMgr=2 @0x5C04E4).
    if (g_noticeReturnToServerSelectPending) {
        g_noticeReturnToServerSelectPending = false;
        Change(Scene::ServerSelect);   // g_SceneMgr = 2 @0x5C04E4 (+ g_SceneSubState=0 @0x5C04EE,
                                       // set by Change) -- dword_1676188=0 @0x5C04F8 = frameCount_
    }
    if (g_noticeAbnormalEndPending) {
        g_noticeAbnormalEndPending = false;
        // g_QuitFlag 0x815590 = 1 @0x5C051B. ClientSource does not reify this global: the exit
        // path goes through the message loop (App/App.cpp WM_CLOSE/WM_DESTROY ->
        // PostQuitMessage @0x461BC3), the binary's OTHER exit path. Deliberate, documented
        // deviation: same observable effect (client stops), different path.
        // TODO [anchor 0x815590]: reify g_QuitFlag (App::quit_ mirror) and set it HERE.
        TS2_WARN("SceneManager: abnormal end requested by the notice (type 3) -> PostQuitMessage.");
        ::PostQuitMessage(0);
    }
    switch (scene_) {
    case Scene::Intro:
        // Faithful automaton Scene_IntroUpdate 0x517FE0 (Game/IntroFlow.h): 90 + 33x3 + 90
        // = 279 frames (9.3s @ 30 FPS), NOT 90 frames like the old placeholder.
        if (game::UpdateIntro(introState_, 0.0f)) Change(Scene::ServerSelect);
        break;
    case Scene::ServerSelect:
        // UIFW-09 -- Scene_ServerSelectUpdate 0x518B30: the 2nd of only TWO
        // UI_ResetAllDialogs 0x5AC3F0 call sites (xrefs_to -> EXACTLY 2: @0x518B79 here and
        // @0x52C038 on the EnterWorld side, already wired below). It was not wired: dialogs
        // left open when quitting the game (or after a failed login) survived into the
        // server-select screen.
        // Original structure, re-proven by disassembly this mission:
        //   518B3C `mov ecx,[eax+4]`          -> sub-state (field +4 of cSceneMgr)
        //   518B42 sub-state==0 -> Init block; ==1 -> wait loop; else fall through
        //   518B5D `[this+8] += 1`            -> frame counter (field +8)
        //   518B69 `cmp [edx+8], 1Eh` / jnb   -> 30 frames before running the block
        //   518B79 call UI_ResetAllDialogs    <- THE SITE
        //   5191F0 `mov [ecx+4], 1` / 5191FA `mov [edx+8], 0` -> sub-state=1, counter=0
        // subState_/frameCount_ ARE the mirrors of these +4/+8 fields (SceneManager.h); until
        // now they were GHOSTS (written, never read -- grep): this block finally gives them
        // their original role for scene 2. frameCount_ is ALREADY incremented at the top of
        // Update() (`++frameCount_`), mirroring @0x518B5D -> do not re-increment here.
        // Safe even with an empty registry (ResetAll iterates dialogs_, cf. the EnterWorld
        // site comment).
        // The REST of the original block is already covered elsewhere and is NOT duplicated
        // here: Z000.BGM (@0x518BF7) + server list + status thread -> UI/LoginScene.cpp:597-604
        // and LoginScene::ServerSelectUpdate; UI_FocusEditBox(0) (@0x518BA5) -> LoginScene.
        // Still unwired, for lack of a reified equivalent (same tradeoffs as the EnterWorld
        // block below): WSndMgr_Free (@0x518B83), Gfx_ApplyOverlayBlendMode (@0x518B8D),
        // Util_SetClampedU8Field(mPOINTER,0) (@0x518B99), 150-dword scratch (@0x518BAA).
        // TODO [anchors 0x518B83 / 0x518B8D / 0x518B99 / 0x518BAA].
        if (subState_ == 0 && frameCount_ >= 30) {                       // @0x518B42 / @0x518B69 (1Eh)
            ui::UIManager::Instance().ResetAll();                        // 0x5AC3F0 @0x518B79
            subState_       = 1;                                         // `mov [ecx+4], 1` @0x5191F0
            frameCount_     = 0;                                         // `mov [edx+8], 0` @0x5191FA
            g_SceneSubState = 1;   // mirror of field +4 // 0x1676184
        }
        if (login_) { login_->Update(scene_); ConsumePending(); }
        break;
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) { login_->Update(scene_); ConsumePending(); }
        break;
    case Scene::EnterWorld: {
        // (W2-F1) RE-ENTRANT RELOAD (warp / op 0x18 Pkt_GameServerConnectResult 0x469CF0, the
        // sole writer of g_SceneMgr=5 @0x469d95; it has already done Change(Scene::EnterWorld)
        // and armed these 2 fields). Tested FIRST. g_TargetZoneId 0x1675A9C = pendingWarpZoneId.
        if (game::g_World.sceneReloadPending) {
            game::g_World.sceneReloadPending = false;
            const int warpZone = (game::g_World.pendingWarpZoneId >= 0)
                                     ? game::g_World.pendingWarpZoneId
                                     : game::g_World.zoneId;
            game::g_World.pendingWarpZoneId = -1;
            // Re-arms the visual state machine (case 0..3) for the NEW zone: without this
            // reset, enterWorldState_ would stay stuck on WaitServerAck/Failed from the
            // previous cycle. Scene_EnterWorldUpdate restarts from subState 0 (0x52C00F). // 0x52BFF0
            enterWorldState_ = game::EnterWorldFlowState{};
            subState_ = 0; frameCount_ = 0;
            g_SceneSubState = 0;   // mirror of field +4: reload = sub-state 0 // 0x1676184
            // Writes g_World.zoneId + SetCurrentZoneId (equivalent to g_SelfMorphNpcId=
            // g_TargetZoneId @0x52C173) and replays LoadZoneResource(1..12) + rebuild geo/BGM.
            // // 0x52BFF0 / 0x4DCB60
            ReloadZone(warpZone);
            break; // let the visual flow play out starting next frame
        }
        // PRIORITY, REAL InGame switch: armed by EntityManager::OnEnterWorld
        // (Game/EntityManager.cpp, op 0x0c reception) via game::g_World.
        // sceneEnterWorldPending, faithful to dword_1676180=6 written DIRECTLY by
        // Pkt_EnterWorld in the binary (cf. GameState.h and
        // Docs/TS2_ENTERWORLD_WIRING_TODO.md). Tested FIRST, before EnterWorldFlow_Update
        // below (which now only handles a fallback timeout).
        if (game::g_World.sceneEnterWorldPending) {
            game::g_World.sceneEnterWorldPending = false;
            Change(Scene::InGame);
            break;
        }
        // Faithful automaton Scene_EnterWorldUpdate 0x52BFF0 (Game/EnterWorldFlow.h): wait(30)
        // -> 20 zone resources spaced 10 frames apart (~200 frames, ~6.7s) -> wait(30) -> send
        // request -> wait for server ACK (up to 5000 frames). The real InGame switch is
        // triggered by RECEIVING the server EnterWorld packet (op 0x0c, cf. above) -- this flow
        // now only serves as VISUAL PROGRESSION (zone resource loading) + fallback timeout if
        // the server never responds.
        game::EnterWorldFlowHost host;
        host.ResetUiAndAudio = [this] {
            // Scene_EnterWorldUpdate 0x52BFF0, case 0 (WaitBeforeUnload), block guarded by
            // `if ((unsigned)*(this+2) >= 0x1E)` @0x52C02C (30 frames): purges leftover
            // CharSelect UI and audio. THE ORDER BELOW MATCHES THE BINARY (dialogs -> cursor
            // -> focus -> scratch -> sound), re-checked by disassembly (0x52C033..0x52C089).

            // (1) UI_ResetAllDialogs(&dword_1821D4C) @0x52C038 -- real, wired on the same
            // UIManager as the rest of the shell. Safe even if windows_ is not yet constructed
            // (InGame not reached yet): the registered dialog list is then empty
            // (UIManager::ResetAll() iterates dialogs_, a default-constructed empty vector).
            ui::UIManager::Instance().ResetAll();                          // 0x52C038

            // (2) Util_SetClampedU8Field(&dword_8E714C, 0) @0x52C044 -- WIRED (C-cursor).
            // dword_8E714C IS mPOINTER, the set of 9 mouse cursors (active index at +0):
            //   Util_SetClampedU8Field 0x4C1110 = `if (a2 <= 8) *this = a2;` = game::CursorSet::
            //   SetActiveSlot(0) (Game/MiscManagers.cpp:88); Cursor_AnimateTick 0x4C1140
            //   reapplies SetCursor(slot) every frame. game::Cursors() is now the SINGLE
            //   singleton ticked by App (App.cpp, unified ownership -- cf. C-cursor): this reset
            //   therefore takes effect. "Reset the cursor shape to slot 0 (arrow)" on entering
            //   Loading. Faithful order HERE: full triplet ResetAllDialogs (0x52C038) -> cursor
            //   (0x52C044) -> UI_FocusEditBox (0x52C050). INVARIANT shared by the 5 scene-entry
            //   sites: the cursor reset is always right BEFORE UI_FocusEditBox; the preceding
            //   ResetAllDialogs, however, only exists here and at ServerSelect (not InGame/Login).
            game::Cursors().SetActiveSlot(game::kCursorDefault); // 0x52C044 / 0x4C1110

            // (3) UI_FocusEditBox(&g_UIEditBoxMgr, 0) @0x52C050. With a2 = 0, the original
            // (0x50F4A0), under `if (a2 < 0x16)` (22 slots), does: `*this = 0` (focus index;
            // 0 = game, 1..21 = active input field -- cf. IDA comment on g_UIEditBoxMgr
            // 0x1668FC0) THEN, since branch `if (*this)` is false, `SetFocus(hWndParent)`
            // @0x50F4CB: strips keyboard focus from any native EDIT and returns it to the game
            // window.
            // ClientSource currently has NO live native EDIT (ui::Win32EditBox exists but no
            // file includes it): in-game text input is the custom-drawn ChatWindow widget
            // (focused_ flag). Both halves are therefore rendered as:
            //   - focus index = 0   -> Chat().Unfocus() (UI/ChatWindow.h:189);
            //   - SetFocus(hWndParent) -> notifyHwnd_ IS hWndParent 0x815184, PROVEN this
            //     mission: App_Init @0x461C51 does `mov ds:hWndParent, ecx` with ecx = arg_4
            //     = the HWND created by WinMain 0x4609C0; on the C++ side, App::Init passes
            //     this same hwnd_ to SceneManager::Init (App/App.cpp:489) -> notifyHwnd_.
            //     No observable effect as long as no native EDIT takes focus, but faithful and
            //     immediately correct once Win32EditBox is wired.
            if (hud_) hud_->Chat().Unfocus();                              // 0x52C050 / 0x50F4BB
            if (notifyHwnd_) ::SetFocus(static_cast<HWND>(notifyHwnd_));   // 0x50F4CB

            // (4) 150-dword scratch @0x52C055 (`for (i=0;i<150;++i) *(this+i+3)=0;`, i.e.
            // +0xC..+0x260) -- NOT MODELED, so NOTHING to zero. SceneManager.h:42 does document
            // "+12 150-dword buffer" but no member reifies it, and these 150 dwords remain
            // unidentified in the binary (UI/LoginScene.cpp:811-814 only names 3 of them --
            // a1[3..5] = its ok/exit/opt buttons -- which belong to LoginScene, NOT the
            // EnterWorld scene). Do not invent a phantom field.
            // TODO [anchor 0x52C055]: identify the 150-dword buffer of cSceneMgr +12.

            // (5) Snd_ReleaseBuffers(this + 153) @0x52C089: `add ecx, 264h` -> slot
            // +0x264 = +612 = THE SCENE BGM SLOT, exactly the one from
            // SceneMgr_ReleaseSoundBuffers 0x517B60 (`return Snd_ReleaseBuffers(this + 153);`),
            // already reified here by bgm_ / ReleaseBgm(). The binary cuts the sound BEFORE
            // spooling the zone (case 1).
            // CORRECTION to the comment that used to occupy this block: it flagged as "still
            // TODO" the release of the WORLD SOUND BANK (WSndMgr_Free 0x4DB060); that was a
            // slot mix-up -- 0x52C089 targets +612 (bgm_), and WSndMgr_Free operates on a
            // DISTINCT slot held by g_GameWorld, not called at this EA.
            ReleaseBgm();                                                  // 0x52C089 / 0x6A80D0

            // ALIGNED SILENCE WINDOW (C-BGM fix): case 12 of host.LoadZoneResource (idx==12)
            // now LOADS the real zone .BGM into the WORLD slot worldBgm_
            // (WorldAssets::LoadWorldBgm, g_GameWorld+2236) during Loading, exactly like
            // World_LoadZoneResource 0x4DCB60 case 12; the PLAY happens on entering InGame via
            // WorldAssets::PlayWorldBgm (0x50F76E, gated by g_BgmEnabled). No more re-decoding
            // into the scene slot cSceneMgr+612 (ex-LoadZoneBgm) -- cf. the BGM block of
            // Change() above.
        };
        host.LoadZoneResource = [this](int zoneId, int idx) {
            // World_LoadZoneResource 0x4DCB60: idx IS directly world::ResourceKind (cf.
            // EnterWorldFlow.h, audited idx in [1,12] real, 0/[13,19] faithful no-ops -- the
            // original `default` switch case does nothing for these values).
            // worldMap_ is now built ONCE in Init() (cf. above), NOT lazily on entering InGame
            // as before this wiring, precisely so it is already available here (EnterWorld
            // precedes InGame in the flow).
            if (!worldMap_) return; // gameDataDir_ was empty at Init() -- already logged
            if (idx < 1 || idx > 12) return; // faithful no-op (original `default` switch case)
            const auto kind = static_cast<world::ResourceKind>(idx);
            const unsigned char ok = worldMap_->LoadZoneResource(zoneId, kind);
            if (!ok) {
                TS2_WARN("EnterWorld: World_LoadZoneResource(kind=%d, zone=%d) failed.", idx, zoneId);
            }
        };
        host.SendEnterWorldRequest = [this] {
            // Net_SendPacket_Op12 0x4B43C0 (opcode 12, 222 bytes): block1 128 bytes (count),
            // block2 13 bytes (character name), block3 72 bytes = confirmed spawn/teleport
            // record (Net/CharSelectPackets.h::BuildEnterWorldTail72).
            uint8_t name13[13] = {};
            {
                const std::string& nm = game::g_World.self.localPlayerName;
                const size_t n = nm.size() < sizeof(name13) ? nm.size() : sizeof(name13);
                std::memcpy(name13, nm.data(), n);
            }
            uint8_t tail72[72] = {};
            net::BuildEnterWorldTail72(game::g_World.self.spawnX,
                                       game::g_World.self.spawnY,
                                       game::g_World.self.spawnZ,
                                       game::g_World.self.spawnRotationDeg,
                                       tail72);
            net::Net_SendPacket_Op12(net_->Client(), net::g_AccountName, name13, tail72);
            // Internal NetSend() is fire-and-forget (void, cf. every other Net_Send* builder in
            // this file): "true" here is an assumed partial-fidelity stand-in, same as
            // host.SendKeepAlive further down in case Scene::InGame.
            return true;
        };
        host.ShowErrorNotice = [](int strId) {
            // UI_NoticeDlg_Open(byte_18225C8, 2, StrTable005_Get(g_LangId, strId), &String):
            // strId 67 = Op12 send failure (0x52C1A2), 68 = server ACK timeout (0x52C213).
            // Same model as host.ShowSpawnTimeoutNotice (InGame). // 0x5C0280
            game::g_Client.prompt.Open(2, game::Str(strId));
        };
        const int zoneId = game::g_World.zoneId;
        if (!game::EnterWorldFlow_Update(enterWorldState_, host, zoneId)) {
            // Failed state (5000-frame ACK timeout @0x52C203 or send failure @0x52C194): the
            // Str 67/68 notice was already sent by host.ShowErrorNotice. Do NOT force InGame:
            // the binary stays in scene 5 / state 4 (default 0x52C232 = no-op). The only
            // legitimate path to InGame remains sceneEnterWorldPending (op 0x0c) tested at the
            // top of this case.
        }
        // Mirror of cSceneMgr's field +4 during the EnterWorld scene: in the binary,
        // g_SceneSubState is the SAME field (*(this+1)) for Scene_EnterWorldUpdate 0x52BFF0 and
        // Scene_InGameUpdate 0x52C600 -- so here it carries the EnterWorld state (0..4). Not
        // required by the @0x50B7EC guard (it short-circuits on g_SceneMgr != 6), but kept so
        // the mirror stays exact regardless of scene. // 0x1676184
        g_SceneSubState = static_cast<int>(enterWorldState_.state);
        break;
    }
    case Scene::InGame: {
        // cSceneMgr_Update 0x517BF0 (case 6): Scene_InGameUpdate() THEN
        // AutoPlay_Update(g_AutoPlayBot) -- in that ORDER, every InGame frame (confirmed by
        // direct decompilation). Scene_InGameUpdate = InGameTickFlow_Update
        // (Game/InGameTickFlow.h, wired below); AutoPlay stays right after, unchanged.
        //
        // Hooks wired onto EXISTING code (real, not TODOs):
        //   SendKeepAlive/SendPendingTargetPoll -> Net_SendPacket_Op13/Net_SendOp64 (Net/SendPackets.h)
        //   AppendKeepAliveFailedMessage    -> game::g_Client.msg.System(Str(70))
        //   ShowSpawnTimeoutNotice          -> game::g_Client.prompt.Open(2, Str(71)) (UI_NoticeDlg_Open)
        //   GetSelfActionState              -> g_World.players[0].body @kSelfActionStateOffset (Game/MapWarp.h)
        //   IsGm                            -> net::g_GmAuthLevel != 0 (Net/NetClient.h)
        //   IsExchangeWindowOpen            -> windows_->PlayerTrade().IsOpen() (Dialog::IsOpen)
        //   CanAutoInteractNpc/IsInventoryDirty/IsMorphInProgress
        //                                    -> windows_->AutoPlaySys().externalState.*
        // Hooks wired onto the 4 systems Game/AnimationTick.h, Game/EntityLifecycleTick.h,
        // Game/CameraWarpTick.h, Game/ComboPickupTick.h (2026-07-14 wiring mission, cf. local
        // comments at each hook below for details and documented gaps/TODOs):
        // TickWarpSuppressionTimeout, AutoUsePotion, UpdateLocalPlayerAnim,
        // UpdateEntityAnimFrame, DespawnStalePlayer, UpdateMonster/RespawnMonsterAfterKnockback,
        // TickNpcEffect/CleanupStaleNpcEffect, AutoInteractNpcForPet, UpdateQuestMarkerTimer,
        // FindComboFollowupTarget/BeginComboMorph, TickNearbyPickupSlots, RotateTipText.
        // Hooks wired onto the 3 further systems Game/GroundAuraWorldObjectTick.h,
        // Game/AutoTargetCombatGate.h, Gfx/CameraThirdPersonBridge.h (2nd wiring wave, same
        // date): TickGroundItemEffect, GetFxAuraCount/IsFxAuraActive/UpdateHomingProjectile,
        // GetWorldObjectCount/IsWorldObjectActive/TickWorldObject, ValidateAutoTarget,
        // IsCombatAllowedOnMap; InitCamera/UpdateCameraCollision now go through
        // gfx::TickThirdPersonCamera (called right after InGameTickFlow_Update below, outside
        // the host -- cf. its local comment) thanks to Update(dt, camera) finally receiving a
        // MUTABLE gfx::Camera (SceneManager.h/App.cpp extended by this same mission).
        // Still TODO (no data/instance available in ClientSource to feed it):
        // UpdateMapObjectAnim (no animated map collision object modeled).
        game::InGameTickFlowHost host;

        // --- Setup (case 0), one-shot ---------------------------------------------------
        // Scene_InGameUpdate 0x52C600 case 0 (jumptable @0x52C61F -> loc_52C626): quadruplet
        // proven instruction by instruction, in this exact order.
        // PROVEN DEVIATION vs Scene_EnterWorldUpdate 0x52BFF0 case 0: InGame's case 0 contains
        // NEITHER UI_ResetAllDialogs (@0x52C038, EnterWorld-specific) NOR
        // Snd_ReleaseBuffers(this+153) (@0x52C089, same). Do NOT add them here: entering the
        // game does not cut the BGM and does not reset the dialogs.
        host.ResetUiAndScratch = [this] {
            // (1) Gfx_ApplyOverlayBlendMode_SetState() @0x52C62B -- NOT WIRED.
            // The original (0x53F630) does exactly two things:
            //   Gfx_SetTextureBlendMode(g_GfxRenderer, 3, dword_7FFF78, 2) @0x53F646
            //   dword_8002C8 = 3 @0x53F64B  (global overlay blend-mode state)
            // and 0x69DCA0 writes renderer fields +331/+332/+333 (+332 clamped to +88) then
            // chains 4x SetTextureStageState (vtbl+276, stage 0, states 5/6/10/7).
            // NOTE (spotted this mission): the `mov ecx, offset unk_1685740` @0x52C626 that
            // precedes the call is DEAD -- 0x53F630 takes no parameter and does not use ecx
            // (it operates on g_GfxRenderer 0x7FFE18). The comment at
            // Game/InGameTickFlow.h:128 ("sub_53F630(&unk_1685740)") therefore attributes an
            // argument to this function that does not exist -- file outside this front, flagged
            // only.
            // Nothing to call here: no reified equivalent exists in ClientSource (neither
            // dword_8002C8 nor the renderer +331..+333 fields), and Gfx/Renderer.h is not owned
            // by this front. Do not confuse with MeshRenderer::applyBlendMode (0x69DCA0 is a
            // distinct overlay state path, not per-mesh blend).
            // TODO [anchor 0x52C62B / 0x53F630 / 0x69DCA0]: reify the overlay blend state
            // (dword_8002C8 + renderer +331..+333) then call it HERE.

            // (2) Util_SetClampedU8Field(&dword_8E714C, 0) @0x52C637 -- WIRED (C-cursor).
            // Strictly the same call as @0x52C044 (EnterWorld case 0): resets the cursor shape
            // to slot 0 (arrow) on entering InGame, on the SINGLE game::Cursors() singleton now
            // ticked by App (unified ownership). Cf. the @0x52C044 block above.
            game::Cursors().SetActiveSlot(game::kCursorDefault); // 0x52C637 / 0x4C1110

            // (3) UI_FocusEditBox(&g_UIEditBoxMgr, 0) @0x52C643 -- WIRED.
            // Call STRICTLY identical to @0x52C050 (EnterWorld case 0): same function, same
            // a2 = 0 (`push 0` @0x52C63E). Re-checked this mission on 0x50F4A0: under
            // `if (a2 < 0x16)`, `*this = 0` @0x50F4BB then, since branch `if (*this)` is false,
            // `SetFocus(hWndParent)` @0x50F4CB. So we reuse the already-proven pattern wired
            // above (focus index -> Chat().Unfocus(); SetFocus(hWndParent) -> notifyHwnd_,
            // which IS hWndParent 0x815184).
            if (hud_) hud_->Chat().Unfocus();                              // 0x52C643 / 0x50F4BB
            if (notifyHwnd_) ::SetFocus(static_cast<HWND>(notifyHwnd_));   // 0x50F4CB

            // (4) 150-dword scratch @0x52C648 -- NOT MODELED, so NOTHING to zero.
            // `for (i=0; i<150; ++i) *(this+i+3) = 0;` (`cmp [ebp+var_8], 96h` @0x52C65A;
            // `mov dword ptr [edx+ecx*4+0Ch], 0` @0x52C669), i.e. +0xC..+0x260 of cSceneMgr --
            // exactly the same buffer as @0x52C055 on the EnterWorld side. SceneManager.h:56
            // documents it but no member reifies it, and these 150 dwords remain unidentified
            // in the binary. Do not invent a phantom field.
            // TODO [anchor 0x52C648]: identify the 150-dword buffer of cSceneMgr +12.
        };

        // --- WaitFirstSpawn (case 1), 5000-frame timeout --------------------------------
        host.ShowSpawnTimeoutNotice = [] {
            // UI_NoticeDlg_Open(2, StrTable005_Get(g_LangId,71), "") 0x5C0280 -- real, via
            // ClientRuntime::PromptState (same model as the other modal prompts).
            game::g_Client.prompt.Open(2, game::Str(71));
        };

        // --- InitCamera (case 3), one-shot -------------------------------------------------
        // Cam_SetLookAt/Camera_SetEyeTarget are now REALLY wired via gfx::TickThirdPersonCamera
        // (Gfx/CameraThirdPersonBridge.h), called ONCE per frame right after
        // InGameTickFlow_Update below (outside the host: it handles both the one-shot framing
        // AND the follow/collision every frame from the same `justEnteredInGame` flag, cf.
        // below) -- this hook stays a no-op to avoid a double call.
        host.InitCamera = [](float, float, float) {};

        // --- MainTick step 1: keepalive /300 frames + clan/faction poll request ---------
        host.SendKeepAlive = [this]() -> bool {
            // Net_SendPacket_Op13(client, g_LocalElement) 0x4B4570. Internal NetSend() is
            // best-effort (fire-and-forget): Net_Send* builders do NOT report send success
            // (void), so "true" here is an assumed partial-fidelity stand-in (TODO: propagate
            // the NetSend bool up to here if ever needed).
            net::Net_SendPacket_Op13(net_->Client(), static_cast<int8_t>(net::g_LocalElement));
            return true;
        };
        host.AppendKeepAliveFailedMessage = [] {
            game::g_Client.msg.System(game::Str(70)); // StrTable005 id 70
        };
        host.HasPendingTargetRequest = [] {
            const auto readReqName = [](uint32_t addr) {
                const auto& blob = game::g_Client.Blob(addr, 13);
                size_t len = 0;
                while (len < blob.size() && blob[len] != 0) ++len;
                return std::string(reinterpret_cast<const char*>(blob.data()), len);
            };
            return game::HasPendingTargetRequest(readReqName(0x167468A), readReqName(0x1674697));
        };
        host.SendPendingTargetPoll = [this] {
            net::Net_SendOp64(net_->Client()); // 0x4B9B20 -- clan/faction poll request.
        };

        // --- MainTick step 2: 10s timeout of the "warp suppressed" flag ----------------------
        // Warp_TickSuppressionTimeout (Game/CameraWarpTick.h). The hook receives g_GameTimeSec
        // directly from RunMainTick, then synchronizes this latch with
        // AutoPlayExternalState::warpSuppressed (SAME global dword_1675B00, cf. top of
        // Game/CameraWarpTick.h): the real ARM site (dword_1675B00=1) lives elsewhere in
        // AutoPlaySystem (outside this wiring's scope) -- captured here on the fly as soon as
        // it's detected, so the 10s auto-clear (0x52C91F) stays faithful.
        static game::WarpSuppressionState s_warpSuppression;
        host.TickWarpSuppressionTimeout = [this](float /*dt*/) {
            const float gameTimeSec = game::g_World.gameTimeSec;
            if (windowsReady_ && windows_) {
                auto& ext = windows_->AutoPlaySys().externalState;
                if (ext.warpSuppressed && !s_warpSuppression.suppressed) {
                    game::Warp_SetSuppressed(s_warpSuppression, gameTimeSec);
                }
                game::Warp_TickSuppressionTimeout(s_warpSuppression, gameTimeSec);
                ext.warpSuppressed = s_warpSuppression.suppressed; // propagate the auto-clear back
            } else {
                game::Warp_TickSuppressionTimeout(s_warpSuppression, gameTimeSec);
            }
        };

        // --- MainTick steps 3-5: unconditional anim/collision every frame ----------
        // Game_AutoUsePotion (Game/CameraWarpTick.h). REAL wiring of the gauges/thresholds/
        // action state (already modeled in GameState.h); the auto-play belt (3x14) and UI
        // threshold settings do not yet exist anywhere in ClientSource -> hooks left null
        // (clean degradation: IsAutoPotionSystemEnabled==false by default, so the function
        // never consumes a potion until a future InventorySystem/AutoPlay setting wires them --
        // cf. AutoPotionHost in the header for each missing original EA).
        host.AutoUsePotion = [this](float /*dt*/) {
            game::AutoPotionHost potionHost;
            potionHost.GetHpGauge = [] { return static_cast<float>(game::g_World.self.hp); };
            potionHost.GetMpGauge = [] { return static_cast<float>(game::g_World.self.mp); };
            // DOCUMENTED DEVIATION (cf. Game/CameraWarpTick.h::AutoPotionHost): the binary
            // really does compare HP/MP against the Char_CalcAttackRatingMin/Max aggregates,
            // NOT against a max HP/MP capacity -- reproduced as-is for fidelity.
            potionHost.GetHpThresholdMetric = [] { return static_cast<float>(game::g_World.self.atkRatingMin); };
            potionHost.GetMpThresholdMetric = [] { return static_cast<float>(game::g_World.self.atkRatingMax); };
            potionHost.GetSelfActionState = [] {
                if (game::g_World.players.empty()) return 0;
                const game::PlayerEntity& self0 = game::g_World.players[0];
                int32_t raw = 0;
                if (self0.body.size() >= game::kSelfActionStateOffset + sizeof(raw)) {
                    std::memcpy(&raw, self0.body.data() + game::kSelfActionStateOffset, sizeof(raw));
                }
                return static_cast<int>(raw);
            };
            if (windowsReady_ && windows_) {
                potionHost.IsMorphInProgress = [this] { return windows_->AutoPlaySys().externalState.morphInProgress; };
            }
            game::Game_AutoUsePotion(potionHost);
        };
        // MapColl_UpdateObjectAnim (Game/AnimationTick.h): the original "this" (an animated map
        // collision object, MapCollisionObjectAnimState) has NO instance anywhere in
        // ClientSource to date -- neither World/WorldMap.h nor Gfx/WorldGeometryRenderer.h
        // expose a per-map-object sub-object/particle array. Real wiring is impossible without
        // extending this system (outside this wiring's scope): TODO kept.
        host.UpdateMapObjectAnim = [](float) {
            TS2_INGAME_TODO_ONCE("InGame: MapColl_UpdateObjectAnim not wired - no MapCollisionObjectAnimState "
                                   "instance anywhere in ClientSource (World/WorldMap.h does not expose "
                                   "animated map collision objects) (TODO EA 0x694A00).");
        };
        // Player_UpdateLocalAnim (Game/AnimationTick.h): operates on game::g_World (self),
        // rebuilds the ~80 morph timers at their original addresses via g_Client.Var/VarF.
        // LoadCurrentZoneModel wired to world::WorldMap::LoadCurrentZoneModel (already written,
        // instance owned by SceneManager); IsPointOnGround/ambient music left null
        // (terrain/audio outside this wiring's scope).
        host.UpdateLocalPlayerAnim = [this](float dt) {
            game::LocalAnimTickHost localHost;
            localHost.LoadCurrentZoneModel = [this](int reason) {
                if (worldMap_) worldMap_->LoadCurrentZoneModel(reason);
            };
            game::Player_UpdateLocalAnim(game::g_World, dt, nullptr, localHost);
        };
        // Char_UpdateAnimationFrame (Game/AnimationTick.h): called for entity 0 (self, step 5)
        // AND for every remote player (step 6, cf. Game/InGameTickFlow.cpp) via the SAME hook.
        // isSelf/isLocalSimulation = (idx==0). GetPendingStopRequest/ClearPendingStopRequest
        // wired to g_PendingStopRequest (0xE0000072, SAME variable as
        // Net/GameHandlers_Misc.cpp::kPendingStopReq); SendAutoPlayStopAck wired to
        // Net_SendOp95(pos_self, 2) (already declared, Net/SendPackets.h). Contact/cast
        // interruption delegated to ActionFsm (already written); the terminal 55-handler
        // switch (asset-driven, 0x5727BF) stays out of scope (stateHandler null -> FSM frozen
        // on its current state beyond contact/interrupt/FX/rotation). If an instant hit/skill
        // is validated for SELF (contactFiredThisTick), the result is serialized and sent via
        // Net_SendPacket_Op18 (76 bytes), faithfully completing Combat_QueueMeleeAttack/
        // Combat_QueueSkillAction (Game/CombatSystem.h, already written -- reused, not
        // duplicated).
        host.UpdateEntityAnimFrame = [this](int idx, float dt) {
            if (idx < 0 || static_cast<size_t>(idx) >= game::g_World.players.size()) return;
            game::PlayerEntity& p = game::g_World.players[static_cast<size_t>(idx)];
            if (!p.active) return;
            const bool isSelf = (idx == 0);

            game::CombatActorState actor;
            actor.selfId = p.id;
            actor.x = p.x; actor.y = p.y; actor.z = p.z;
            actor.facing = p.anim.state; // entity+244, same offset as CharAnimState::state

            game::CharAnimTickHost animHost;
            animHost.GetPendingStopRequest = [] { return game::g_Client.Var(0xE0000072u) != 0; };
            animHost.ClearPendingStopRequest = [] { game::g_Client.Var(0xE0000072u) = 0; };
            animHost.SendAutoPlayStopAck = [this] {
                const game::PlayerEntity& self = game::g_World.Self();
                float pos[3] = { self.x, self.y, self.z };
                net::Net_SendOp95(net_->Client(), pos, 2);
            };

            game::CharAnimTickResult result;
            game::Char_UpdateAnimationFrame(p.anim, actor, game::g_World, nullptr,
                                             isSelf, isSelf,
                                             false /* pendingCastInterrupt: TODO g_AutoHuntFuelA/B not traced */,
                                             dt, nullptr, nullptr, animHost, result);

            // F_PLAYERANIM front: advances the player anim cursor (entity+248, field 62) = the
            // UNIVERSAL idiom of the terminal switch 0x5727BF (frame += dt*30 then wrap by
            // SUBTRACTION, Char_TickMoveState 0x574922), which the C++ Char_UpdateAnimationFrame
            // does not do (stateHandler null). Current clip frameCount via the oracle backed by
            // the SAME MotionCache as the draw path; race/gender = body+68/+72 (SAME source as
            // WorldRenderer). G5 FIX (DEEP IDA render): the 3rd param (weaponType) = a4 of
            // PcModel_ResolveEquipSlot 0x4E46A0 @0x4e578e = animSlot (entity+240 = body+216 =
            // 2*weaponClass), NOT a8 (special item, ~500-case switch). Without it (frozen at 0),
            // an ARMED player played the unarmed clip. p.anim.animSlot read at spawn
            // (EntityManager). Player_AdvanceAnimCursor arms the IsWired latch itself -> no
            // freeze (clean degradation).
            int animRace = 0, animGender = 0;
            std::memcpy(&animRace,   p.body.data() + 68, sizeof(animRace));
            std::memcpy(&animGender, p.body.data() + 72, sizeof(animGender));
            const int animFrameCount = ts2::WorldPlayerMotionFrameCount(animRace, animGender, p.anim.animSlot, p.anim.state);
            game::Player_AdvanceAnimCursor(p.anim, dt, animFrameCount);

            if (isSelf && result.contactFiredThisTick) {
                uint8_t payload[76] = {};
                result.lastAction.Serialize(payload);
                net::Net_SendPacket_Op18(net_->Client(), payload);
            }
        };
        // Camera_UpdateCollision (Game/AnimationTick.h): REALLY wired via
        // gfx::TickThirdPersonCamera, the SAME single call as host.InitCamera above (cf. its
        // comment) -- this hook stays a no-op to avoid a double call to
        // Camera_UpdateCollision on the same frame.
        host.UpdateCameraCollision = [] {};

        // --- MainTick step 6: remote players, 7.5s expiry ------------------------
        // sub_55D720 (Game/EntityLifecycleTick.h): deactivates the stale slot.
        host.DespawnStalePlayer = [](int idx, float) {
            game::DespawnStalePlayer(game::g_World, idx);
        };

        // --- MainTick step 7: 88-byte array (GroundItem in GameState.h's sense) -------------
        // Fx_MeleeSwingTick 0x5803A0 (Game/GroundAuraWorldObjectTick.h). GetWeaponEffectFrameCount
        // (Model_GetWeaponEffectFrameCount 0x4E5A40) left null: no weapon-effect model/asset
        // table wired on the ClientSource side to date -> documented clean degradation (the
        // frame timer advances but never loops/completes, cf. the header's comment).
        host.TickGroundItemEffect = [](int index, float dt) {
            static const game::GroundAuraWorldObjectTickHost s_groundFxHost{}; // GetWeaponEffectFrameCount null
            game::TickGroundItemEffect(game::g_World, index, dt, s_groundFxHost);
        };

        // --- MainTick step 8: monsters, 7.5s expiry --------------------------------
        // Char_Update / sub_580550 (Game/EntityLifecycleTick.h). EntityLifecycleTickHost shared
        // with step 9 below. DispatchMotionTick + SpawnAttackProjectile(Alt) are now wired
        // below (gx-fx-01, this mission); the sub-hooks STILL null (hit-window tables
        // Anim_IsFrameInHitListA/B 0x559F80/0x55A000, melee network send
        // Combat_SendMeleeHit1/2 0x5823E0/0x582480, ground height
        // MapColl_GetGroundHeight 0x697130, impact sound Snd3D_PlayPositional 0x4DA450) stay
        // outside this front's scope: documented clean degradation at the top of
        // Game/EntityLifecycleTick.h. NB: with IsFrameInHitListA/B null, the hit window never
        // arms (inWindow=false) -- a 2nd latency lock on the projectile spawn, in addition to
        // the missing motionState=5/7 producer (cf. SpawnAttackProjectile wiring).
        static game::EntityLifecycleTickHost s_lifecycleHost;

        // DispatchMotionTick -- Char_Update 0x581E10, terminal switch @0x5822D3 (the 9
        // Char_MotionTick_* 0x582D40..0x5832E0 handlers). THIS HOOK WAS THE ROOT CAUSE of the
        // "s_lifecycleHost has no hook assigned" gap: game::Monster_DispatchMotionTick
        // (Game/AnimationTick.h §5) ALREADY carries the 9 handlers faithfully and is called by
        // UpdateMonster (EntityLifecycleTick.cpp:153) via this hook... which was assigned
        // NOWHERE -> the whole FSM was dead code and MonsterTickExt::motionState/animFrame
        // never moved. This wiring makes it actually reached (30 Hz, one call per active
        // monster), which Scene/WorldRenderer.cpp detects via game::Monster_MotionTickIsWired()
        // to only consume the per-entity cursor (`ent.hasAnimCursor`) once it's fed.
        // oracle = ts2::WorldMotionFrameCountOracle() (Scene/WorldRenderer.h, sole owner of the
        // MotionCache) -> Model_GetMotionFrameCount 0x4E5A70, SAME slot as the draw path.
        // StepTowardTarget (MapColl_StepTowardTarget 0x6974C0, Move states 3/4) left null:
        // EXPLICITLY prescribed degradation per AnimationTick.h -- we skip movement and the
        // "arrived" transition, and we do NOT under any circumstance treat "hook absent" as
        // "failed -> state=1" (the monster would never walk); the frame wrap still applies.
        // TODO [anchor MapColl_StepTowardTarget 0x6974C0] -- requires map collision geometry +
        // MONSTER_INFO+384/388, outside this front's scope.
        s_lifecycleHost.DispatchMotionTick = [](int idx, float dt) {
            static const game::MonsterMotionTickHost s_motionHost{}; // StepTowardTarget null (cf. above)
            game::Monster_DispatchMotionTick(game::g_World, idx, dt,
                                              &WorldMotionFrameCountOracle(), s_motionHost);
        };

        // gx-fx-01 -- SpawnAttackProjectile / SpawnAttackProjectileAlt: the LAST TWO unassigned
        // slots of s_lifecycleHost. Char_Update 0x581E10 calls them (@0x5820A9 state 5 /
        // @0x58213D state 7) to POPULATE the FX projectile pool dword_17D06F4 via
        // Fx_SpawnAttackProjectile(Alt) 0x582530/0x582A10 -- this IS the functional core of the
        // gx-fx-01 gap ("pool never populated"). The real port of the pool + spawn already
        // exists (game::Fx_SpawnAttackProjectile, Game/GroundAuraWorldObjectTick.cpp) but had
        // NO caller = dead code; this wiring gives it its documented producer
        // (GroundAuraWorldObjectTick.h:204). Params rebuilt by BuildMonsterProjectileParams
        // (anonymous namespace above, binary offsets re-proven in IDA this mission).
        //
        // HONEST CAVEAT (re-proven in IDA + exhaustive grep this mission): the call site is
        // guarded by `ext.motionState == 5/7 && ext.attackWindupMode == 1` then
        // `hitActionKind == 2` (EntityLifecycleTick.cpp:80/103/108/117). But NO ClientSource
        // path produces motionState=5/7, attackWindupMode=1, or hitActionKind=2:
        // Monster_DispatchMotionTick (wired just above) writes ONLY motionState=Loop(1) and
        // attackWindupMode=0 (AnimationTick.cpp:703/744), and the real producer is the network
        // monster-attack-order handler (OUTSIDE this front). The spawn therefore stays LATENT
        // (never reached today) -- but faithful and immediately functional once a
        // motionState=5/7 producer is wired. Same policy as host.GetFxAuraCount/
        // UpdateHomingProjectile below ("real wiring regardless", clean and safe degradation).
        s_lifecycleHost.SpawnAttackProjectile = [](int idx) {
            game::FxProjectileSpawnParams p;
            if (BuildMonsterProjectileParams(idx, p)) game::Fx_SpawnAttackProjectile(p);    // 0x582530 (state 5)
        };
        s_lifecycleHost.SpawnAttackProjectileAlt = [](int idx) {
            game::FxProjectileSpawnParams p;
            if (BuildMonsterProjectileParams(idx, p)) game::Fx_SpawnAttackProjectileAlt(p); // 0x582A10 (state 7)
        };

        // W9 -- MapColl_GetGroundHeight 0x697130: ground height under (x,z). Consumed by
        // UpdateMonster (@0x58223E) and TickNpcEffect (@0x582263) via
        // Game/EntityLifecycleTick.cpp:141-145 to rest the entity on the ground after a fall /
        // knockback. Hook left NULL until now -> NO monster ever found the ground again after
        // a knockback (infinite fall / frozen altitude).
        // The provider EXISTS and is ready: world::WorldAssets::GetGroundHeight
        // (World/WorldIntegration.h:133, byte-faithful port of 0x697130 over the .WM layer),
        // whose header NAMES this wiring explicitly (WorldIntegration.h:126-131: "ready to wire
        // to the consumer hooks outside this front's scope (host.GetGroundHeight
        // Game/EntityLifecycleTick.h:199)"). Identical signatures:
        //   hook     : bool(float x, float z, float probeY,        float& outGroundY)
        //   provider : bool(float x, float z, float probeCeilingY, float& outGroundY) const
        // worldAssets_ is a SceneManager member (already passed to gfx::TickThirdPersonCamera).
        // Build-safe: GetGroundHeight returns false if the .WM layer is not loaded -- exactly
        // the degradation the caller expects (`if (!GetGroundHeight(...))`).
        // `this` capture: s_lifecycleHost is STATIC but SceneManager lives for the whole
        // session (an App member) and this block re-runs every InGame frame -> the lambda is
        // reassigned with an always-valid `this`.
        if (worldAssets_) {
            s_lifecycleHost.GetGroundHeight = [this](float x, float z, float probeY,
                                                     float& outGroundY) {
                return worldAssets_->GetGroundHeight(x, z, probeY, outGroundY); // 0x697130
            };
        }
        // s_lifecycleHost sub-hooks STILL null, for lack of callable code in ClientSource
        // (checked by exhaustive grep this mission -- only comment MENTIONS exist):
        // Anim_IsFrameInHitListA/B 0x559F80/0x55A000 (hit window),
        // Combat_SendMeleeHit1/2 0x5823E0/0x582480 (melee send), GetAuraSwapDuration,
        // IsAttackTargetBypassActive. Cannot be wired without porting them first (Game/
        // SkillCombat domain, outside this front). TODO [anchors 0x559F80 / 0x55A000 /
        // 0x5823E0 / 0x582480].
        // Snd3D_PlayPositional 0x4DA450 (@0x5822B1, 1st-landing sound): Audio/Sound3D.h:95
        // ::PlayPositional exists, but the sound object / .ISN slot targeted by this EA is not
        // identified ANYWHERE -> no guessing (rule: no sound rather than a wrong sound).
        // TODO [anchor 0x4DA450 / 0x5822B1]: identify the landing .ISN slot.

        host.UpdateMonster = [](int idx, float dt) {
            game::UpdateMonster(game::g_World, idx, dt, s_lifecycleHost);
        };
        host.RespawnMonsterAfterKnockback = [](int idx) {
            game::RespawnMonsterAfterKnockback(game::g_World, idx);
        };

        // --- MainTick step 9: 152-byte array (NpcEntity in GameState.h's sense) ------------
        // Fx_GibUpdate / sub_583390 (Game/EntityLifecycleTick.h), same host as above.
        host.TickNpcEffect = [](int idx, float dt) {
            game::TickNpcEffect(game::g_World, idx, dt, s_lifecycleHost);
        };
        host.CleanupStaleNpcEffect = [](int idx) {
            game::CleanupStaleNpcEntity(game::g_World, idx);
        };

        // --- MainTick step 10: auras/homing (NOT in GameState.h) ----------------------
        // g_FxAuraCount/dword_17D06F4 (Game/GroundAuraWorldObjectTick.h): attack-projectile SoA
        // pool not modeled on the ClientSource side (cf. the header's comment);
        // GetFxAuraCount reads the real slot via g_Client.Var (0 today, nobody feeds it yet)
        // -> IsFxAuraActive/UpdateHomingProjectile never reached in practice, clean and safe
        // degradation (not a frozen stub, real wiring regardless).
        host.GetFxAuraCount = [] { return game::GetFxAuraCount(); };
        host.IsFxAuraActive = [](int index) { return game::IsFxAuraActive(index); };
        host.UpdateHomingProjectile = [](int index, float dt) { game::UpdateHomingProjectile(index, dt); };

        // --- MainTick step 11: world objects (NOT in GameState.h) -------------------
        // dword_1687230/dword_180EEF4 (Game/GroundAuraWorldObjectTick.h): wired to
        // game::g_World.zoneObjects (already sized to 500 by GameData_InitPools). TickWorldObject
        // (sub_584170) is an EMPTY __stdcall stub confirmed by decompilation -> does nothing,
        // faithfully reproduced (not a guess).
        host.GetWorldObjectCount = [] { return game::GetWorldObjectCount(game::g_World); };
        host.IsWorldObjectActive = [](int index) { return game::IsWorldObjectActive(game::g_World, index); };
        host.TickWorldObject = [](float dt) { game::TickWorldObject(dt); };

        // --- MainTick step 12, gating gate -------------------------------------------
        host.GetSelfActionState = [] {
            // g_SelfActionState[0] == g_World.players[0].body @+220 (== entity+244, cf.
            // Game/MapWarp.h::kSelfActionStateOffset, derivation verified from g_LocalPlayerSheet).
            if (game::g_World.players.empty()) return 0;
            const game::PlayerEntity& self0 = game::g_World.players[0];
            int32_t raw = 0;
            if (self0.body.size() >= game::kSelfActionStateOffset + sizeof(raw)) {
                std::memcpy(&raw, self0.body.data() + game::kSelfActionStateOffset, sizeof(raw));
            }
            return static_cast<int>(raw);
        };
        if (windowsReady_ && windows_) {
            host.IsExchangeWindowOpen = [this] { return windows_->PlayerTrade().IsOpen(); };
            // sub_53B9E0: AutoPlayExternalState::sceneTransitionBlocking already stores the RAW
            // value of sub_53B9E0 (cf. Game/AutoPlaySystem.h) -- this hook wants "true when
            // sub_53B9E0 returns true", so NO inversion here (cf. the explicit warning in
            // Game/InGameTickFlow.h).
            host.CanAutoInteractNpc = [this] { return windows_->AutoPlaySys().externalState.sceneTransitionBlocking; };
            host.IsInventoryDirty   = [this] { return windows_->AutoPlaySys().externalState.invDirtyEnable; };
            host.IsMorphInProgress  = [this] { return windows_->AutoPlaySys().externalState.morphInProgress; };
        } else {
            host.IsExchangeWindowOpen = [] { return false; };
            host.CanAutoInteractNpc   = [] { return false; };
            host.IsInventoryDirty     = [] { return false; };
            host.IsMorphInProgress    = [] { return false; };
        }
        // Npc_AutoInteractForPet 0x53B5F0: ALREADY ported by
        // NpcInteractionSystem::AutoInteractForPet() (Game/NpcInteraction.h, reused as-is, cf.
        // the combo_pickup_quest mission report). `selectedItemId` (g_SelectedInvItemId
        // 0x1673258) is not tracked anywhere in ClientSource to date -> fixed at 0 (internal
        // guard `if (selectedItemId < 1) return;` -> faithful, safe no-op; TODO the real wiring
        // once inventory item selection exists on the UI side).
        static game::NpcInteractionSystem s_npcInteract;
        host.AutoInteractNpcForPet = [this] {
            if (windowsReady_ && windows_) {
                s_npcInteract.morphInProgress = windows_->AutoPlaySys().externalState.morphInProgress;
            }
            s_npcInteract.AutoInteractForPet(0 /* TODO g_SelectedInvItemId not traced */,
                                              game::g_World.gameTimeSec);
        };

        // --- MainTick step 12a ------------------------------------------------------------
        // ValidateAutoTarget (Game/AutoTargetCombatGate.h): wired on game::g_World, default
        // range oracle (rangedLookup null -> AutoTarget_DefaultRangeLookup): mode 7 =
        // g_World.zoneObjects; mode 4 = g_World.groundItems (same array as
        // g_NpcRenderArray/dword_1764D14, cf. the AutoTargetCombatGate.h header comment) -- both
        // modes now resolve a real position, no more systematic fallback.
        host.ValidateAutoTarget = [] { game::ValidateAutoTarget(game::g_World); };

        // --- MainTick step 12b ------------------------------------------------------------
        // Quest_UpdateMarkerTimer (Game/ComboPickupTick.h), reuses game::g_QuestProgress
        // (already ported, Game/QuestSystem.h). isArenaZone (Map_IsArenaZone 0x54B690) not
        // modeled -> fixed false (TODO); warehouse window/marker sound left null (UI/audio
        // outside this wiring's scope).
        static game::QuestMarkerState s_questMarker;
        host.UpdateQuestMarkerTimer = [] {
            game::Quest_UpdateMarkerTimer(s_questMarker, game::g_QuestProgress,
                                           game::g_World.gameTimeSec,
                                           false /* isArenaZone: TODO Map_IsArenaZone not modeled */,
                                           nullptr, nullptr);
        };

        // --- MainTick step 12c -------------------------------------------------------------
        // Combo_FindNearbyFollowup (Game/ComboPickupTick.h), call site @0x52CEA9:
        //   `mov ecx, offset flt_1555D08`   -> this = GINFO-003 table (game::LoadedCoordTable())
        //   `mov edx, ds:g_SelfMorphNpcId`  -> a2   = current motionId          @0x52CE9D
        //   `push offset flt_1687330`       -> a3   = local player position     @0x52CE98
        // GINFO-003-SUBTABLES: `candidates` now REALLY reads the row's sub-table A
        // (row[3]/row[4+3i]/row[304+i]) -- this lambda IS reached: InGameTickFlow step 12c
        // calls it every 30 frames (`% 30` guard @0x52CE8E), and motionId is now the real
        // value of g_SelfMorphNpcId (the previous "untraced TODO" was STALE: the address is
        // tracked by game::WarpAddr::SelfMorphNpcId and already read by about a dozen
        // consumers via g_Client.VarGet).
        // `transitionCheck` stays null: Combo_CheckTransition 0x4FD650 (giant validator, ~250
        // pairs + ~25 untraced globals + g_MotionFrameRangeTable/SkillLevelTable_GetMin/Max)
        // belongs to the SkillCombat domain and is not ported anywhere in ClientSource -> a
        // constant 0, so no candidate ever passes the `== 1` guard @0x501337 and the result
        // stays -1. Sub-table A IS being read (gap closed) but the final SELECTION stays
        // latent. TODO [anchor Combo_CheckTransition 0x4FD650] -- outside this front's scope.
        host.FindComboFollowupTarget = [] {
            const game::PlayerEntity& self = game::g_World.Self();
            const int motionId = game::g_Client.VarGet(game::WarpAddr::SelfMorphNpcId); // @0x52CE9D
            return game::Combo_FindNearbyFollowup(motionId, self.x, self.y, self.z,
                                                   GInfo2ComboCandidates, nullptr);
        };
        // host.IsMorphInProgress is ALREADY really wired above (step-12 gating gate, the same
        // field reused here by the binary for the step-12c combo gate) -- no reassignment here.
        // BeginComboMorph (Game/ComboPickupTick.h): complete faithful port (phase=4, 72-byte
        // reset, random rotation via net::DefaultRng(), Net_SendPacket_Op20).
        // GINFO-003-SUBTABLES: `originLookup` now REALLY reads sub-table B
        // (row[404]/row[705+i]/row[405+3i]) via GInfo2_FindVec3ByKey 0x4FD540, called at site
        // @0x52CF30 with a2 = followupMotionId (var_4) and a3 = g_SelfMorphNpcId (@0x52CF20) --
        // hence `currentMotionId` = g_SelfMorphNpcId (the old "untraced TODO" was STALE, cf.
        // step 12c above).
        // HONEST CAVEAT: this lambda is only reached if FindComboFollowupTarget returns != -1,
        // which requires Combo_CheckTransition 0x4FD650 (not ported, cf. step 12c) -- sub-table
        // B is therefore faithfully wired but stays LATENT until 0x4FD650 is ported.
        // TODO [anchor Combo_CheckTransition 0x4FD650].
        static game::ComboMorphState s_comboMorph;
        host.BeginComboMorph = [this](int followupTargetId) {
            const int currentMotionId = game::g_Client.VarGet(game::WarpAddr::SelfMorphNpcId); // @0x52CF20
            game::BeginComboMorph(s_comboMorph, followupTargetId, currentMotionId,
                                   net_->Client(), GInfo2ComboOrigin);
            if (windowsReady_ && windows_) {
                windows_->AutoPlaySys().externalState.morphInProgress = s_comboMorph.inProgress;
            }
        };

        // --- MainTick step 12d -------------------------------------------------------------
        // Combat_IsElementAllowedOnMap (Game/AutoTargetCombatGate.h::IsCombatAllowedOnMapForSelf,
        // direct wrapper, already ported by Game/ComboPickupTick.h): wired on world.self.element
        // (g_LocalElement) + g_SelfMorphNpcId (g_Client.VarGet). ElementPairTable
        // (g_LocalPlayerSheet+455..458) remains unmodeled in ClientSource -> falls back to {}
        // (4x -1, "no pair registered", NOT a shortcut to "always false": the result still
        // genuinely depends on selfMorphNpcId, cf. the header's comment).
        host.IsCombatAllowedOnMap = [] { return game::IsCombatAllowedOnMapForSelf(game::g_World); };
        host.IsGm = [] { return net::g_GmAuthLevel != 0; };
        // TickNearbyPickupSlots (Game/ComboPickupTick.h): the 5 flt_1676130 slots are already
        // fed by the packet 0x82 handler (Net/GameHandlers_Misc.cpp) via g_Client.VarF -- this
        // wiring reuses the SAME storage (no duplication). Only runs if IsCombatAllowedOnMap
        // above ever returns true (faithful).
        host.TickNearbyPickupSlots = [this] {
            const game::PlayerEntity& self = game::g_World.Self();
            game::TickNearbyPickupSlots(self.x, self.y, self.z, net_->Client());
        };

        // --- MainTick step 12e -------------------------------------------------------------
        // Tips_RotateUpdate (Game/ComboPickupTick.h): reuses game::g_Strings.notices (TipsTable
        // already ported, Game/StringTables.h) -- the timer/index (600s) is already kept
        // faithfully there; this wiring only adds the missing call to append the chat.
        host.RotateTipText = [] {
            game::Tips_RotateUpdate(game::g_Strings.notices, game::g_World.gameTimeSec);
        };

        // "Just entered InGame" detection for gfx::TickThirdPersonCamera below: captured
        // BEFORE InGameTickFlow_Update, because the state machine (inGameTickState_)
        // transitions DURING this call (InitCamera -> MainTick, one-shot, cf.
        // Game/InGameTickFlow.cpp) -- this is exactly the same frame where the original binary
        // runs its case 3 (Cam_SetLookAt/Camera_SetEyeTarget, EA 0x52C6EF).
        const bool justEnteredInGame = (inGameTickState_.state == game::InGameTickState::InitCamera);

        game::InGameTickFlow_Update(inGameTickState_, host, static_cast<float>(dt));

        // Mirror of cSceneMgr's field +4 (g_SceneSubState 0x1676184) for the InGame scene: in
        // the binary, Scene_InGameUpdate 0x52C600 writes this field ITSELF along its
        // transitions (e.g. sub=4 set @0x52C7F1 at the end of case 3 InitCamera, sub=3 set
        // directly by Pkt_SpawnCharacter @0x464901). inGameTickState_.state IS its 1:1 model
        // (game::InGameTickState, values aligned with the original cases:
        // Setup=0/WaitFirstSpawn=1/Failed=2/InitCamera=3/MainTick=4) -> we copy it right AFTER
        // the call, at the point where the binary has finished writing the field for this
        // frame. Consumed by the Camera_UpdateFromInput guard @0x50B7EC
        // (App/PlayerInputController). Ordering: App::FrameTick calls g_playerInput.Update
        // (App.cpp:643) BEFORE the scene_.Update loop (App.cpp:656) -- exactly like the binary
        // calls Camera_UpdateFromInput @0x462619 before cSceneMgr_Update @0x46263B. The
        // controller therefore reads the sub-state produced by the PREVIOUS frame: same
        // one-frame lag as the original. // 0x1676184
        g_SceneSubState = static_cast<int>(inGameTickState_.state);

        // M2 -- MapColl_UpdateObjectAnim 0x694A00, called 1x/frame from Scene_InGameUpdate
        // 0x52C600 @0x52c94b (UNIQUE xref confirmed in IDA). On the ClientSource side,
        // WorldGeometryRenderer::TickWorldAnim is the documented equivalent
        // (WorldGeometryRenderer.h:271-277, kAnimFps=15.0): sole writer of wavePhase_ (water
        // bump-env matrix) + per-.WO-instance sway flipbook phase. Never called before this
        // wiring -> frozen water/sway. worldGeomReady_ one-shot guard (hot device required,
        // same guard as the .WO render). // 0x694A00
        if (worldGeomReady_ && worldGeom_)
            worldGeom_->TickWorldAnim(static_cast<float>(dt));

        // (Wave D -- combat FX) Ticks particle slots (types 5/6/7 = muzzle/hitspark/hitburst):
        // mirrors the update loop of Scene_InGameUpdate 0x52C600
        // (Particle_EnsureLoadedThenUpdateEmit 0x4D9F40 -> Particle_UpdateEmit 0x6A7530).
        // EMISSION POSITION: the binary follows the bone via slot[30]=flt_FABB5C (bone system
        // not ported); FAITHFUL approximation = source entity position (it follows the entity
        // frame by frame like the bone would), resolved via the network id stored in the slot
        // (idHi@+0xC / idLo@+0x10, written by the Fx_Attach* setters, cf. Gfx/FxSetters.cpp).
        // If the entity has despawned -> no emission (no particle at the world origin): honest
        // degradation.
        for (int i = 0; i < gfx::FxPool_Count(); ++i) {
            gfx::FxSlot& fx = gfx::FxPool_Slots()[i];
            if (!fx.state || (fx.type != 5 && fx.type != 6 && fx.type != 7)) continue;
            const uint32_t* rawId = reinterpret_cast<const uint32_t*>(&fx);
            const uint32_t idHi = rawId[3], idLo = rawId[4]; // slot[3]=+0xC, slot[4]=+0x10
            float epos[3] = { 0.0f, 0.0f, 0.0f };
            bool found = false;
            for (size_t k = 0; !found && k < game::g_World.players.size(); ++k) {
                const game::PlayerEntity& p = game::g_World.players[k];
                if (p.active && p.id.hi == idHi && p.id.lo == idLo) { epos[0]=p.x; epos[1]=p.y; epos[2]=p.z; found=true; }
            }
            for (size_t k = 0; !found && k < game::g_World.monsters.size(); ++k) {
                const game::MonsterEntity& m = game::g_World.monsters[k];
                if (m.active && m.id.hi == idHi && m.id.lo == idLo) { epos[0]=m.x; epos[1]=m.y; epos[2]=m.z; found=true; }
            }
            if (!found) continue;
            const float erot[3] = { 0.0f, 0.0f, 0.0f };
            gfx::FxBillboard_PoolTick(reinterpret_cast<gfx::FxParticlePool*>(fx.ptclPool),
                                      fx.ptclDefIndex, static_cast<float>(dt), epos, erot, nullptr);
        }

        // (Wave F -- combat mesh FX) Ticks MESH slots (types 8/9/10 = block/parry/deflect; 0xC/
        // 0xD = 12/13 ALSO routed to the MiscC bank by Fx_EmitterDraw 0x585E30). Three roles:
        //  (a) POSITION: the binary places the mesh on the weapon bone (Model_GetAttachTransform
        //      0x40FDC0, not ported); FAITHFUL approximation = source entity center (resolved by
        //      idHi/idLo, SAME pattern as the particle tick); slot.orient (+0x50) left at 0 (no
        //      invented bone transform).
        //  (b) FLIPBOOK: drawParam (+0x40) = frame index (proven Wave F, 30 fps) -> += dt*30.
        //  (c) RECYCLING (Wave F audit fix): once the flipbook is done (frame >= .MOBJECT
        //      frameCount), free the slot (Fx_AttachSlotClear 0x584220) -> WITHOUT this, wiring
        //      s_meshDraw would leave the mesh displayed FOREVER (regression) + pool leak.
        //      Faithful reconstruction of a one-shot effect; TODO(anchor) confirm the exact
        //      condition (hold/loop/clear) via a dynamic dump.
        if (modelObjRenderer_) {
            for (int i = 0; i < gfx::FxPool_Count(); ++i) {
                gfx::FxSlot& mfx = gfx::FxPool_Slots()[i];
                if (!mfx.state) continue;
                if (mfx.type != 8 && mfx.type != 9 && mfx.type != 10 && mfx.type != 12 && mfx.type != 13) continue;
                const uint32_t* mid = reinterpret_cast<const uint32_t*>(&mfx);
                const uint32_t idHi = mid[3], idLo = mid[4]; // slot[3]=+0xC, slot[4]=+0x10
                bool mfound = false;
                for (size_t k = 0; !mfound && k < game::g_World.players.size(); ++k) {
                    const game::PlayerEntity& p = game::g_World.players[k];
                    if (p.active && p.id.hi==idHi && p.id.lo==idLo) { mfx.position[0]=p.x; mfx.position[1]=p.y; mfx.position[2]=p.z; mfound=true; }
                }
                for (size_t k = 0; !mfound && k < game::g_World.monsters.size(); ++k) {
                    const game::MonsterEntity& m = game::g_World.monsters[k];
                    if (m.active && m.id.hi==idHi && m.id.lo==idLo) { mfx.position[0]=m.x; mfx.position[1]=m.y; mfx.position[2]=m.z; mfound=true; }
                }
                mfx.drawParam += static_cast<float>(dt) * 30.0f;                 // advance flipbook
                const uint32_t fc = modelObjRenderer_->FrameCount(mfx.meshIdxC); // idxC MiscC bank
                if (fc > 0 && mfx.drawParam >= static_cast<float>(fc))
                    gfx::Fx_AttachSlotClear(&mfx);                               // one-shot done -> recycle
            }
        }

        // W9 -- Npc_RenderSlotTick 0x5803A0: DECOR NPC anim (mZONENPCINFO). Called 1x/frame and
        // per active slot from Scene_InGameUpdate 0x52C600 @0x52CA4C (UNIQUE xref, confirmed in
        // IDA this mission). Original loop @0x52CA19: `i < g_NpcCount (0x1687220)`, stride 88
        // (`imul edx, 58h` @0x52CA27), guard `slot+4 != 0` (active, @0x52CA2A) on
        // g_NpcRenderArray 0x1764D14 -- game::ZoneNpc_TickAnim (Game/AnimationTick.cpp:859)
        // faithfully carries this WHOLE loop (guard + mode 0/1 dispatch), hence the single call
        // here rather than a copied loop.
        // This hook had NO caller -> frameAcc stayed 0 and ALL decor NPCs were frozen on the
        // global-clock fallback: Scene/WorldRenderer.cpp:809
        // (`ent.hasAnimCursor = game::ZoneNpc_AnimTickIsWired()`) detects this and only consumes
        // the per-entity cursor once this tick is actually reached. Site explicitly prescribed
        // by Game/AnimationTick.h:456-460.
        // ORDER: the binary runs MapColl_UpdateObjectAnim @0x52C94B BEFORE Npc_RenderSlotTick
        // @0x52CA4C -> this call stays AFTER TickWorldAnim above.
        // oracle = ts2::WorldMotionFrameCountOracle() (Scene/WorldRenderer.h, sole owner of the
        // MotionCache -> Model_GetMotionFrameCount 0x4E5A70), SAME instance as the monster
        // motion dispatch above. // 0x52CA4C / 0x5803A0
        game::ZoneNpc_TickAnim(static_cast<float>(dt), &WorldMotionFrameCountOracle());

        // InGame_InitCamera (one-shot, if justEnteredInGame) + Camera_UpdateCollision (every
        // frame): REAL wiring via gfx::TickThirdPersonCamera (Gfx/CameraThirdPersonBridge.h),
        // AFTER updating the local player's position for this frame (host.
        // UpdateEntityAnimFrame(0,...) already executed by InGameTickFlow_Update above) --
        // replaces host.InitCamera/host.UpdateCameraCollision (left no-op above).
        gfx::TickThirdPersonCamera(camera, game::g_World, static_cast<float>(dt), justEnteredInGame,
                                   worldAssets_.get()); // WG-02: real terrain collision oracle (0x69a1f0/0x540da0)

        // AutoPlay_Update(g_AutoPlayBot) -- ALWAYS after Scene_InGameUpdate, cf. the block's
        // top comment and Game/InGameTickFlow.h (integration note at the bottom of the file).
        if (windowsReady_ && windows_) windows_->UpdateAutoPlay(static_cast<float>(dt));
        break;
    }
    default: break;
    }
}

} // namespace ts2

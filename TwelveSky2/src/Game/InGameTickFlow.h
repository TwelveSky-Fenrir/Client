// Game/InGameTickFlow.h — State machine + tick order of the IN-GAME scene (InGame scene = 6).
//
// Faithful C++ rewrite of Scene_InGameUpdate 0x52C600 (dispatched by cSceneMgr_Update
// 0x517BF0 when g_SceneMgr.sceneId == Scene::InGame; cSceneMgr_Update THEN calls
// AutoPlay_Update(g_AutoPlayBot) — CONFIRMED in the disassembly, NOT reimplemented here,
// cf. integration note at the bottom of this file). Single source of truth: Hex-Rays
// decompilation via idaTs2.
//
// ACTUAL DISCOVERED FLOW (5 sub-states, one of which loops indefinitely):
//
//   0 Setup          : 1 frame. Resets leftover scratch/UI/audio -> WaitFirstSpawn.
//   1 WaitFirstSpawn : waits with a 5000-frame timeout (0x1388, ~166 s). The client does NOT
//                      enter the game on its own: it is the server's spawn packet for the
//                      LOCAL character (Pkt_SpawnCharacter, inbound opcode 15, EA 0x4646C0,
//                      NETWORK — out of scope) that, when it creates the entity at INDEX 0
//                      (= self, cf. ts2-entity-model memory doc), directly writes subState=3
//                      (InitCamera) and resets the counter to 0, short-circuiting this timeout.
//                      If nothing arrives before 5000 frames: notice (StrTable005 id 71) ->
//                      state 2 (Failed). FIDELITY FIX (audit 2026-07-14): this module now
//                      detects this short-circuit itself by reading directly
//                      g_World.players[0].active (already an existing coupling to
//                      Game/GameState.h, cf. MainTick) — BEFORE this fix, NOTHING in
//                      ClientSource ever set InGameTickState::InitCamera
//                      (EntityManager::OnSpawnCharacter explicitly returned at this
//                      "out of entity scope" point): the state machine stayed stuck here
//                      until the timeout, then moved to Failed for good, with MainTick then
//                      NEVER executed. Cf. Game/InGameTickFlow.cpp for details.
//   2 Failed         : terminal, does nothing (faithful behavior of the original case 2,
//                      which is a plain `return`).
//   3 InitCamera     : 1 frame, one-shot. Frames the 3rd-person camera on the local player
//                      (eye = self + (50,60,50), look = self + (0,10,0), cf. .cpp) then
//                      -> MainTick with the counter reset to 0.
//   4+ MainTick      : main loop, NEVER exited (the original switch has no case beyond 3;
//                      any sub-state >=4 falls into the `default`, which never changes
//                      subState again). Executed EVERY frame as long as the scene stays
//                      InGame. Exact order detailed below.
//
// EXACT ORDER OF THE MAIN TICK (MainTick, a single pass of Scene_InGameUpdate):
//   1. Every 300 frames (0x12C): Net_SendPacket_Op13 keepalive (system message on failure)
//      + optional poll of a pending clan/faction request (Net_SendOp64) if both request
//      names are set.
//   2. 10 s timeout on the "warp suppressed" flag (dword_1675B00 / AutoPlayExternalState
//      ::warpSuppressed): auto-clears if g_GameTimeSec exceeds the threshold.
//   3. Auto potion use (Game_AutoUsePotion, EVERY frame).
//   4. Map collision object anim (MapColl_UpdateObjectAnim, EVERY frame).
//   5. Local player anim (Player_UpdateLocalAnim) then anim of entity 0=self
//      (Char_UpdateAnimationFrame) then camera collision (Camera_UpdateCollision).
//   6. Remote PLAYERS loop (indices 1..N-1, g_World.players): anim if seen <=7.5 s ago,
//      otherwise despawn (entity considered stale).
//   7. "Ground item" loop / 88-byte array (Fx_MeleeSwingTick — surprising original name
//      for a pickup render tick, cf. the discrepancy below): unconditional tick if active.
//   8. MONSTER loop (g_World.monsters): update if seen <=7.5 s ago, otherwise respawn
//      after knockback (entity considered stale -> revive, NOT despawn).
//   9. "NPC" loop / 152-byte array (Fx_GibUpdate — surprising original name, cf. the
//      discrepancy below): update if seen <=7.5 s ago, otherwise cleanup.
//  10. "Aura"/homing projectile loop (64-byte array, NOT modeled in GameState.h):
//      unconditional tick if active, NO 7.5 s staleness check.
//  11. "World object" loop (76-byte array, NOT modeled in GameState.h): unconditional
//      tick if active. FAITHFUL QUIRK: the original call (sub_584170) does NOT receive
//      the loop index, only dt — reproduced as-is (host without an index).
//  12. Automatic targeting/pickup/combo block — cf. the "GATING GATE" section below.
//
// KNOWN DISCREPANCY (to flag, NOT to fix here): GameState.h models the 88-byte array
// (dword_1764D14, ex-"g_NpcRenderArray") as GroundItem and the 152-byte array
// (dword_17AB534) as NpcEntity, while the original tick functions on these arrays are
// named Fx_MeleeSwingTick (88 o) and Fx_GibUpdate (152 o) — names more evocative of visual
// effects than of "ground item"/"NPC" logic. The disassembly confirms the sizes/counters
// (g_NpcCount for the first, dword_1687228 for the second) but NOT the definitive semantics
// of these two arrays. This module follows GameState.h's classification for consistency
// with the shared foundation; the orchestrator will need to settle this.
//
// GATING GATE for the targeting/pickup/combo block (faithful, including its quirk):
//   Let A = (frameCounter % 30 == 0) AND action state != {11,12,33,34,35,36,37} AND
//           exchange window closed.
//   - if !A: the block runs WITHOUT calling NPC auto-interaction (for pets/animals).
//   - if A AND (host can auto-interact with NPC AND inventory is not "dirty"): calls
//     NPC auto-interaction THEN runs the block.
//   - if A AND (host cannot / inventory dirty): the block is ENTIRELY SKIPPED this
//     frame (no target validation, no combo, no pickup, no tip rotation). This is
//     exactly the binary's behavior (nested if/goto with no else) — reproduced as-is,
//     not a simplification on our part.
//   Block content when it runs:
//     a. Auto-target validation (mode + target still existing/in range) -> host.
//     b. Quest marker timer (Quest_UpdateMarkerTimer) -> host.
//     c. Every 30 frames (re-tested INDEPENDENTLY of the gate above, on the same
//        counter value): looks for a nearby follow-up combo; if found and no morph
//        is already in progress -> triggers the combo morph -> host.
//     d. If the local element is allowed on the map and the player is not GM:
//        ticks the 5 nearby pickup slots (<100 units) -> host.
//     e. Tip text rotation (Tips002_RotateUpdate) -> host.
//
// GAMEGUARD/ANTICHEAT — IGNORED PER PROJECT POLICY: the original polls
// Ac_GameGuard_Heartbeat every 300 frames (exits the app if != 1877) and relays a pending
// auth token (g_GuardAuthTokenPending -> Net_SendOp85) at the TOP of the `default`
// MainTick. DELIBERATELY NOT reproduced here (CLAUDE.md: "ignore the anticheat entirely").
//
// Detailed 3D rendering (skinning, particles, shaders of the effects above): precise TODO,
// out of scope — each Host hook below is the exact integration point where future
// rendering/anim/gameplay code must be wired (original EA documented per hook).
//
// Self-containment: only includes the STL + Game/GameState.h (entity counters/arrays
// already modeled by the shared foundation, cf. mission) to avoid duplicating a 2nd
// definition of the player/monster/NPC arrays. No coupling to Scene/SceneManager.h.
#pragma once
#include <functional>
#include "Game/GameState.h"

namespace ts2::game {

enum class InGameTickState : int {
    Setup          = 0, // case 0 @0x52C61F
    WaitFirstSpawn = 1, // case 1 @0x52C69B
    Failed         = 2, // case 2 @0x52C6DE (terminal)
    InitCamera     = 3, // case 3 @0x52C6EF (one-shot)
    MainTick       = 4, // default @0x52C81C (main loop, never exited)
};

struct InGameTickFlowState {
    InGameTickState state = InGameTickState::Setup;
    int frameCounter = 0; // g_SceneMgr.frameCounter (original dword_1676188)
};

// Integration points (side effects out of scope: network, UI, rendering, fine-grained
// gameplay). A null hook = no-op (returns false/0/-1 depending on type). Original EA in
// comments.
struct InGameTickFlowHost {
    // --- Setup (case 0) ---------------------------------------------------------------
    // sub_53F630(&unk_1685740) + sub_4C1110(0) 0x4C1110 (tooltip reset) +
    // UI_FocusEditBox(&g_UIEditBoxMgr,0) 0x50F4A0 + reset of the 150-dword scene scratch.
    std::function<void()> ResetUiAndScratch;

    // --- WaitFirstSpawn (case 1) --------------------------------------------------------
    // UI_NoticeDlg_Open(2, StrTable005_Get(g_LangId,71), "") 0x5C0280: local character
    // spawn wait timeout.
    std::function<void()> ShowSpawnTimeoutNotice;

    // --- InitCamera (case 3) ------------------------------------------------------------
    // Cam_SetLookAt 0x69CCD0 + Camera_SetEyeTarget 0x403420 (g_GxdRenderer) +
    // g_CamFollowDist = Math_Dist3D(g_CameraPos, ...) 0x53FAA0, THEN dword_1837E64=1 /
    // dword_1837E68=0 (camera transition flags, exact semantics beyond {1,0}
    // undetermined). Local player position provided by g_World.Self() (x,y,z) —
    // the caller could also read it itself: passed here for API convenience.
    std::function<void(float selfX, float selfY, float selfZ)> InitCamera;

    // --- MainTick: network/keepalive (step 1) --------------------------------------------
    // Net_SendPacket_Op13(client, g_LocalElement) 0x4B4570: keepalive every 300 frames.
    // Return value = send success (triggers the StrTable005 id 70 system message if false).
    std::function<bool()> SendKeepAlive;
    std::function<void()> AppendKeepAliveFailedMessage;
    // Crt_Strcmp on 2 pending request fields (g_PendingReqTargetName_Sub2/_Sub1)
    // both non-empty -> true.
    std::function<bool()> HasPendingTargetRequest;
    // Net_SendOp64 0x4B9B20: poll for a pending clan/faction request.
    std::function<void()> SendPendingTargetPoll;

    // --- MainTick: step 2 (warp suppressed) ---------------------------------------------
    // dword_1675B00 (AutoPlayExternalState::warpSuppressed): auto-clear if set for
    // >10 s (g_GameTimeSec - flt_1675B04). The host owns the timestamp when the latch was
    // set; this hook receives gameTimeSec and does the test+clear internally.
    std::function<void(float gameTimeSec)> TickWarpSuppressionTimeout;

    // --- MainTick: steps 3-5 (every frame, unconditional) -----------------------------
    std::function<void(float dt)> AutoUsePotion;              // Game_AutoUsePotion 0x5C4800
    std::function<void(float dt)> UpdateMapObjectAnim;         // MapColl_UpdateObjectAnim(15.0,dt) 0x694A00
    std::function<void(float dt)> UpdateLocalPlayerAnim;       // Player_UpdateLocalAnim 0x5321D0
    std::function<void(int entityIndex, float dt)> UpdateEntityAnimFrame; // Char_UpdateAnimationFrame 0x571880 (idx=0 here)
    std::function<void()> UpdateCameraCollision;                // Camera_UpdateCollision 0x538580

    // --- MainTick: step 6 (remote players, 7.5 s staleness) --------------------------
    std::function<void(int playerIndex, float dt)> DespawnStalePlayer; // sub_55D720 0x55D720

    // --- MainTick: step 7 (88-byte array, cf. naming discrepancy documented above) -------
    std::function<void(int index, float dt)> TickGroundItemEffect; // Fx_MeleeSwingTick 0x5803A0

    // --- MainTick: step 8 (monsters, 7.5 s staleness) ----------------------------------
    std::function<void(int monsterIndex, float dt)> UpdateMonster;             // Char_Update 0x581E10
    std::function<void(int monsterIndex)> RespawnMonsterAfterKnockback;        // 0x580550

    // --- MainTick: step 9 (152-byte array, cf. naming discrepancy documented above) ------
    std::function<void(int npcIndex, float dt)> TickNpcEffect;   // Fx_GibUpdate 0x583CD0
    std::function<void(int npcIndex)> CleanupStaleNpcEffect;     // sub_583390 0x583390

    // --- MainTick: step 10 (attack projectile pool, NOT in GameState.h) --------
    // Identification resolved (mission "aura/world-objects", 2026-07-14): despite its name,
    // this is NOT a buff/debuff aura pool — g_FxAuraCount (0x168722C) is the counter
    // for the ATTACK PROJECTILE SoA pool dword_17D06F4 (stride 64 dw), allocated by
    // Fx_SpawnAttackProjectile(Alt) 0x582530/0x582A10, already catalogued in detail in
    // Docs/TS2_FX_CATALOG.md. Deliberately not modeled in GameState.h (already documented
    // elsewhere); actual wiring of these hooks is a separate mission.
    std::function<int()> GetFxAuraCount;                          // g_FxAuraCount 0x168722C
    std::function<bool(int index)> IsFxAuraActive;
    std::function<void(int index, float dt)> UpdateHomingProjectile; // Fx_HomingProjectileUpdate 0x5862D0

    // --- MainTick: step 11 (zone objects/resource nodes) ---------------------------
    // Identification resolved (mission "aura/world-objects", 2026-07-14): dword_1687230
    // is the counter for a pool DISTINCT from the previous one (zone objects: mines, portals,
    // ...), populated by Pkt_SpawnZoneObject (opcode 0x86). Now modeled in GameState.h via
    // ZoneObjectEntity / g_World.zoneObjects (N=500); these hooks nonetheless remain
    // unwired here (runtime wiring = separate mission, cf. Game/GameState.h and
    // Game/MiscManagers.cpp for details).
    std::function<int()> GetWorldObjectCount;                     // dword_1687230
    std::function<bool(int index)> IsWorldObjectActive;
    // Faithful: NO index passed (sub_584170 0x584170 only receives dt in the original).
    std::function<void(float dt)> TickWorldObject;

    // --- MainTick: step 12, gating gate -----------------------------------------
    // Returns the local player's action state (g_SelfActionState[0]) to test
    // membership in {11,12,33,34,35,36,37} ("busy" states: dialogue/chest/etc.).
    std::function<int()> GetSelfActionState;
    std::function<bool()> IsExchangeWindowOpen;                  // UI_IsExchangeWindowOpen 0x5AC6E0
    // sub_53B9E0(this) 0x53B9E0: overall eligibility (cf. AutoPlayExternalState::
    // sceneTransitionBlocking — SENSE INVERTED here: this hook must return true when
    // sub_53B9E0 returns TRUE, NOT the "blocking" sense of AutoPlaySystem).
    std::function<bool()> CanAutoInteractNpc;
    std::function<bool()> IsInventoryDirty;                      // g_InvDirtyEnable == 1
    std::function<void()> AutoInteractNpcForPet;                 // Npc_AutoInteractForPet 0x53B5F0

    // --- MainTick: step 12a (auto-target validation) ----------------------------------
    // Validates/clears internally dword_1675B24 and the locked target (dword_1675B28/2C)
    // depending on its mode (1/2/3=player, 4=NPC in range, 5=monster, 7=object in range).
    // This module does NOT know the mode or the target arrays (owned by another system,
    // e.g. ItemPickupSystem/SkillCombat): a single opaque hook, called only when the
    // gating gate allows it.
    std::function<void()> ValidateAutoTarget;

    // --- MainTick: step 12b -------------------------------------------------------------
    std::function<void()> UpdateQuestMarkerTimer;                 // Quest_UpdateMarkerTimer 0x510D90

    // --- MainTick: step 12c (every 30 frames, independent of the gate) -----------------
    // Combo_FindNearbyFollowup 0x501270: returns the follow-up target id or -1.
    std::function<int()> FindComboFollowupTarget;
    std::function<bool()> IsMorphInProgress;                      // g_MorphInProgress 0x1675A88
    // Triggers the combo morph (dword_1675A8C=4, dword_1675A9C=followupId, resets the
    // morph fields, random rotation, Net_SendPacket_Op20). A single opaque hook.
    std::function<void(int followupTargetId)> BeginComboMorph;

    // --- MainTick: step 12d --------------------------------------------------------------
    std::function<bool()> IsCombatAllowedOnMap;                   // Combat_IsElementAllowedOnMap 0x55CBF0
    std::function<bool()> IsGm;                                   // g_GmAuthLevel != 0
    // Ticks the 5 nearby pickup slots (<100 u): clears + Net_SendOp106 for those in range.
    // A single opaque hook (owned by ItemPickupSystem).
    std::function<void()> TickNearbyPickupSlots;

    // --- MainTick: step 12e ---------------------------------------------------------------
    std::function<void()> RotateTipText;                          // Tips002_RotateUpdate 0x4C1840
};

// Scene_InGameUpdate 0x52C600. Call 1x/frame (30 FPS, dt = 1/30 s on the original side) as
// long as the active scene is InGame. IMPORTANT (confirmed in the disassembly):
// cSceneMgr_Update calls AutoPlay_Update(g_AutoPlayBot) RIGHT AFTER Scene_InGameUpdate,
// EVERY InGame frame, regardless of the sub-state above (even during Setup/
// WaitFirstSpawn/Failed/InitCamera). This file does NOT reimplement AutoPlay_Update
// (Game/AutoPlaySystem.h already does): the caller must keep the order
//   InGameTickFlow_Update(...); AutoPlaySystem::Update(dt);
// to stay faithful.
void InGameTickFlow_Update(InGameTickFlowState& state, const InGameTickFlowHost& host, float dt);

} // namespace ts2::game

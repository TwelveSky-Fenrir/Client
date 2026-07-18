// Game/CameraWarpTick.h — 4 independent pieces of the InGame tick grouped by mission:
// third-person camera, "warp suppressed" timeout, auto-use potion, active clan/party
// detection. Faithful C++ rewrite from Scene_InGameUpdate 0x52C600 (Hex-Rays via idaTs2)
// and Game_AutoUsePotion 0x5C4800.
//
// Functions covered (original EA):
//   - Cam_SetLookAt          0x69CCD0: third-person camera framing (elevation guard
//     |asin(dy/dist)| > 89.99deg -> rejected, camera unchanged). Called once on entering
//     InGame (Scene_InGameUpdate case 3, EA 0x52C6EF..0x52C816 for the full framing,
//     cf. InGameTickFlowHost::InitCamera in Game/InGameTickFlow.h).
//   - "Warp suppressed" flag timeout (dword_1675B00/flt_1675B04), exact EA 0x52C91F:
//     `if (dword_1675B00 && g_GameTimeSec - flt_1675B04 > 10.0) dword_1675B00 = 0;`
//   - Game_AutoUsePotion 0x5C4800: auto-use HP potion then (if no HP potion triggered)
//     MP potion, every InGame frame.
//   - HasPendingTargetRequest (g_PendingReqTargetName_Sub2/Sub1, EA of the test 0x52C8E9):
//     condition that triggers Net_SendOp64 every 300 frames, ALONGSIDE the keepalive
//     Net_SendPacket_Op13 (already wired elsewhere, cf. SceneManager.cpp). RENAMED AND
//     RE-DOCUMENTED (audit 2026-07-14, fresh re-decompile of Scene_InGameUpdate): the old
//     name "HasActiveGroupName" and its "active guild/party name" description were WRONG —
//     this is NOT a persistent guild/party name. IDA has since renamed the two globals to
//     g_PendingReqTargetName_Sub2 (0x167468A) / _Sub1 (0x1674697): decompiling their writers
//     confirms this is the TARGET NAME OF AN IN-FLIGHT NETWORK REQUEST, written by
//     Net_OnRequestTargetNameSet (Pkt SC opcode 0x44, sub-op 1/2 -> Sub1/Sub2) and cleared by
//     Net_OnRequestCancelClear (Pkt SC opcode 0x45, also posts StrTable005 id 534, "request
//     canceled"). Read by UI_ClanWin_OnLUp, UI_NpcMenu_RequestJoinFaction, UI_ClanCreate_
//     Validate, UI_GameHud_On{MouseDown,Click,Render} (0x5D92A0/0x5E5680/0x608780/0x6753E0/
//     0x677160/0x67A3C0): all point to a clan/faction request UI flow (join/create/clan
//     window), NOT a persistent guild/party state. Net_SendOp64 (0x4B9B20 — nonce+seq+opcode
//     ONLY, 0 bytes of useful payload) every 300 frames while a request is pending looks like
//     an in-flight request "keepalive/poll", not a "guild/party refresh".
//     CROSS-CONFIRMED (mission "CABLAGE ROSTER ALLIANCE/GUILDE", 2026-07-14,
//     Docs/TS2_ALLIANCE_PARTY_ROSTER.md §1): `game::GroupIdentity` (the former
//     `GameState.h::GroupIdentity{guildName,groupName}` that mapped these same two globals to
//     "active guild/party name") was REMOVED from GameState.h — it modeled a concept that
//     does NOT exist in the binary at this address. The real "MY active guild name" is
//     `game::g_World.allianceRoster.guildName` (== g_LocalGuildName 0x168740C, a TOTALLY
//     DIFFERENT address, fed by Net_OnGuildRosterReset/Update 0x4a/0x4f — cf.
//     Game/GameState.h::AllianceRoster, Net/GameHandlers_PartyGuild.cpp).
//     IMPACT IF NOT FIXED: future wiring that followed the old doc (hooking this to any
//     "active guild/party name") would send Net_SendOp64 under WRONG conditions (firing
//     constantly while in a guild, never when a real clan request is actually pending) —
//     a FUNCTIONAL regression once this hook is wired for real (today
//     host.HasPendingTargetRequest is wired in SceneManager.cpp, so runtime behavior now
//     matches IDA ground truth — cf. CHANGEMENT SCENEMANAGER.CPP APPLIED section below).
//
// CHANGEMENT SCENEMANAGER.CPP APPLIED (consolidation):
//   - Game/InGameTickFlow.h/.cpp now use the names `HasPendingTargetRequest` and
//     `SendPendingTargetPoll`.
//   - Scene/SceneManager.cpp reads both blobs 0x167468A/0x1674697 from g_Client.Blob()
//     and calls `game::HasPendingTargetRequest(...)`; Net_SendOp64 remains the poll for a
//     pending clan/faction request.
//
// LEAF module on the gameplay side: depends only on Gfx/Camera.h + STL (this hook takes
// std::string directly to stay self-contained, rather than including Game/GameState.h for
// a single one-line function — the caller itself reads the two relevant blobs, already
// wired on the ClientSource side, cf. CHANGEMENT SCENEMANAGER.CPP APPLIED paragraph above).
// DOES NOT INCLUDE Scene/SceneManager.h or Net/*: all external side effects (network,
// inventory/items, stats, shared global timers) are exposed via opaque hooks
// (std::function), same model as Game/InGameTickFlow.h and Game/AutoPlaySystem.h. Actual
// wiring onto the existing code (Net/SendPackets.h, GameState.h, AutoPlaySystem) is left to
// the consolidation agent (mission coordination rule: this module does NOT edit
// SceneManager.*).
#pragma once
#include <cstdint>
#include <string>
#include <functional>
#include "Gfx/Camera.h"

namespace ts2::game {

// =====================================================================================
// 1. Third-person camera — Cam_SetLookAt 0x69CCD0 + InGame entry framing
// =====================================================================================

// Faithful port of Cam_SetLookAt (originally a thiscall on a small separate camera
// object — this+198.. = eye, this+201.. = target — not modeled separately here: applied
// directly to `camera`, which exposes the same eye/target pair in an isomorphic
// spherical form (cf. Gfx/Camera.h, file header)).
//
// Rejects (returns false, `camera` UNCHANGED) if:
//   - eye == target identically (null direction vector, 0x69cd07);
//   - the elevation angle |asin(dy/dist)| in degrees exceeds 89.989998 (0x69cd8f/91 —
//     confirmed on the raw disassembly: fdivp dy/dist -> call Math_AsinFpu -> fmul
//     flt_7BB28C(=57.2957763671875) -> fabs -> fcomp flt_7EDB70(=89.989998)).
// Otherwise sets target/distance/yaw/pitch on `camera` (Camera::SetTarget/SetDistance/
// SetYaw/SetPitch) and returns true. FIDELITY NOTE: Camera::SetDistance clamps to the
// zoom bounds [25,150] (Camera_Init) — the original binary, at this exact spot, does NOT
// apply this clamp (it writes the raw eye/target into the small camera object; the
// distance clamp is a behavior of ANOTHER function, Cam_ClampDistance 0x69CE00, invoked
// elsewhere by the input controller). For the InGame entry framing (eye-target distance
// ~86.6, within default bounds) this discrepancy has no observable effect; documented for
// fidelity in case `camera` has tighter custom bounds.
bool Cam_SetLookAt(gfx::Camera& camera,
                    float eyeX, float eyeY, float eyeZ,
                    float targetX, float targetY, float targetZ);

// State of the third-person camera framing set by InGame_InitCamera (dword_1837E64/
// dword_1837E68 / g_CamFollowDist originally — exact semantics of the two flags BEYOND
// {1,0} NOT determined from the disassembly: they are never read back again in
// Scene_InGameUpdate, kept as-is for fidelity for a future camera rendering/transition
// system).
struct CameraFollowState {
    bool  initialized    = false; // dword_1837E64: 1 after InitCamera (never reset to 0 here)
    int   transitionFlag = 0;     // dword_1837E68: reset to 0 by InitCamera
    float followDist     = 0.0f;  // g_CamFollowDist — cf. fidelity note below
};

// Scene_InGameUpdate case 3 (EA 0x52C6EF..0x52C816), one-shot on entering InGame:
//   eye    = self + (50, 60, 50)
//   target = self + (0, 10, 0)
//   Cam_SetLookAt(eye, target) [+ Camera_SetEyeTarget, redundant on the binary side — only
//     one eye/target pair pushed here, onto `camera`]
//   dword_1837E64 = 1 ; dword_1837E68 = 0
// FIDELITY NOTE (g_CamFollowDist): the binary computes this value via
// Math_Dist3D(g_CameraPos, flt_80013C) — TWO FIXED renderer globals (not derived from the
// self position visible in this function, probably the PREVIOUS camera state captured
// before the overwrite above), inaccessible from this leaf module (out of scope:
// g_GxdRenderer/g_GfxRenderer). Instead, `camera.Distance()` is used AFTER applying the
// look-at (the closest available equivalent quantity on the ClientSource side: the
// third-person camera's orbital zoom, which equals ~86.6 for this exact framing) —
// approximation assumed, to correct if g_CamFollowDist turns out to be read elsewhere in
// a future system.
void InGame_InitCamera(gfx::Camera& camera, CameraFollowState& follow,
                        float selfX, float selfY, float selfZ);

// =====================================================================================
// 2. "Warp suppressed" flag timeout — dword_1675B00/flt_1675B04, EA 0x52C91F
// =====================================================================================

// Minimal mirror of dword_1675B00 ("warp suppressed" bool active) + flt_1675B04 (arming
// timestamp, g_GameTimeSec at the moment it was armed). INTEGRATION: dword_1675B00 is the
// SAME global as AutoPlayExternalState::warpSuppressed (Game/AutoPlaySystem.h), which
// currently has NO timestamp field — this module therefore stays self-contained with its
// own state; the consolidation agent will need to either sync the two bools every frame
// (WarpSuppressionState::suppressed -> externalState.warpSuppressed), or have
// AutoPlayExternalState carry the timestamp directly (out of scope for this mission: this
// file does NOT edit Game/AutoPlaySystem.h).
struct WarpSuppressionState {
    bool  suppressed = false; // dword_1675B00
    float setAtSec   = 0.0f;  // flt_1675B04
};

// EXACT EA 0x52C91F: faithful auto-clear if `suppressed` and (gameTimeSec - setAtSec) >
// 10.0. Does nothing if `suppressed` is already false (faithful: the binary tests
// `dword_1675B00 &&` before even reading flt_1675B04).
void Warp_TickSuppressionTimeout(WarpSuppressionState& state, float gameTimeSec);

// Arming the latch — NOT disassembled at EA 0x52C91F itself (this site ONLY does the
// read/auto-clear): the arming site (dword_1675B00=1; flt_1675B04=g_GameTimeSec) is
// elsewhere in the binary (out of scope for this mission, probably the same flow that
// blocks a warp attempt during a cooldown). Provided as a reasonable symmetric complement
// so `state` is usable end-to-end; to be replaced by the real arming site if/when it is
// reverse-engineered.
void Warp_SetSuppressed(WarpSuppressionState& state, float gameTimeSec);

// =====================================================================================
// 3. Auto-use potion — Game_AutoUsePotion 0x5C4800
// =====================================================================================

enum class PotionKind : uint8_t { Hp, Mp };

// One slot of the auto-play belt (3 pages x 14 slots, dword_1674404 + g_Container5_ItemId
// in the binary — container "5", distinct from the main inventory).
struct BeltSlot {
    int page = 0; // 0..2 (original i)
    int slot = 0; // 0..13 (original j)
};

// External integration points for this module — gauges/thresholds/belt/locks shared with
// other systems, OWNED by other systems (StatEngine, InventorySystem, network). Null hook
// = documented default value (see Game_AutoUsePotion below): never silently blocks the
// game, just disables the feature until the hook is wired.
struct AutoPotionHost {
    // Local player's current gauges (dword_1687370[0]=HP, dword_1687378[0]=MP on the
    // binary side). Corresponds to SelfState::hp/mp (Game/GameState.h) on the
    // ClientSource side — not read directly here to keep this module independent of
    // GameState.h (the caller does `host.GetHpGauge = [] { return (float)game::g_World.self.hp; };`).
    std::function<float()> GetHpGauge;
    std::function<float()> GetMpGauge;

    // DOCUMENTED DISCREPANCY (to verify): the binary compares the HP/MP gauges above to a
    // FRACTION of Char_CalcAttackRatingMin(g_EquipSnapshotScratch) [0x4CD970] for the HP
    // threshold, and Char_CalcAttackRatingMax(...) [0x4CE3F0] for the MP threshold — two
    // massive aggregates (equip+gems+buffs+set bonus+weapon...) that, fully decompiled,
    // do compute an "attack rating" (used elsewhere as such in the binary), NOT a max
    // HP/MP capacity. An HP-vs-attack-rating comparison is surprising but that's EXACTLY
    // what Game_AutoUsePotion does — reproduced as-is for fidelity (IDA naming already in
    // place), in the same vein as the Fx_MeleeSwingTick/Fx_GibUpdate discrepancies already
    // flagged in Game/InGameTickFlow.h. Corresponds to SelfState::atkRatingMin/atkRatingMax
    // (already modeled in GameState.h).
    std::function<float()> GetHpThresholdMetric; // Char_CalcAttackRatingMin
    std::function<float()> GetMpThresholdMetric; // Char_CalcAttackRatingMax

    // User setting for the auto-use threshold, 0=disabled, 1..5 = n/5 of the metric above;
    // 5 has a DIFFERENT meaning (switches to a "metric*0.99" test instead of
    // "threshold*metric/5", cf. Game_AutoUsePotion). dword_1674728 (HP) / dword_167472C (MP).
    std::function<int()> GetHpThresholdSetting;
    std::function<int()> GetMpThresholdSetting;

    // Searches the belt (3x14) for the first configured slot (slot type==3,
    // dword_1674404) whose resolved item (g_Container5_ItemId -> MobDb_GetEntry(&mITEM,...))
    // has an ITEM_INFO+340 subtype in the set {1,2,5} (kind==Hp) or {3,4,5} (kind==Mp).
    // Owned by the future InventorySystem/AutoPlaySystem: a single opaque hook. Returns
    // false if no slot matches (faithful: the 3x14 loop ends without triggering LABEL_70
    // in the binary, the caller continues to the next test).
    std::function<bool(PotionKind kind, BeltSlot& out)> FindBeltPotionSlot;

    // Net_SendPacket_Op22(&g_AutoPlayMgr, slot.page, slot.slot) 0x5C4C8E, THEN arms the
    // shared cooldown (g_GmCmdCooldownLatch=1 at 0x1675B08, flt_1675B0C=g_GameTimeSec at
    // 0x1675B0C — cf. IsGmCmdCooldownActive below, SAME global pair as the "one scroll in
    // flight" latch of AutoPlaySystem::CheckReturnScroll/CheckTownScroll,
    // Game/AutoPlaySystem.h). The caller is responsible for arming this shared cooldown
    // itself (Game_AutoUsePotion no longer does it separately, cf. IsGmCmdCooldownActive).
    std::function<void(PotionKind kind, BeltSlot slot)> UsePotion;

    // Locks shared with other systems (NOT owned by this module):
    std::function<bool()> IsMorphInProgress;          // g_MorphInProgress == 1 (0x1675A88)
    std::function<bool()> IsAutoPotionSystemEnabled;   // dword_1675D84 ("enabled" setting)
    std::function<bool()> IsGmCmdCooldownActive;       // g_GmCmdCooldownLatch (0x1675B08)
    std::function<void()> SetGmCmdCooldownActive;      // set by UsePotion, exposed separately
                                                        // for callers who want to do it
                                                        // themselves (host.UsePotion may
                                                        // also call it internally).
    std::function<int()>  GetSelfActionState;          // g_SelfActionState[0] (0x1687328)
};

// Game_AutoUsePotion 0x5C4800. To be called EVERY InGame frame (step 3 of MainTick,
// cf. Game/InGameTickFlow.h). Faithful guards (return with no effect if):
//   - GetHpGauge() < 1                        (0x5c485a)
//   - IsMorphInProgress()                     (same)
//   - !IsAutoPotionSystemEnabled()             (same)
//   - IsGmCmdCooldownActive()                  (same)
//   - GetSelfActionState() in {11, 12, 38}     (same)
// Then: tests the HP threshold (n/5 formula or 99%-and-below-5 formula), scans the HP
// belt ({1,2,5}); if a slot is found -> UsePotion(Hp, slot) and RETURNS (only one potion
// per frame). Otherwise, SAME logic for MP ({3,4,5}).
// Null hook (not wired) => treated as a "neutral value" (gauge/metric 0 for floats,
// false/0/-1 for the rest): the function never crashes, it just disables the
// corresponding check.
void Game_AutoUsePotion(const AutoPotionHost& host);

// =====================================================================================
// 4. Pending target request (clan/faction) — g_PendingReqTargetName_Sub2/Sub1
//    (formerly unk_167468A/unk_1674697), EA of the test 0x52C8E9
// =====================================================================================

// RENAMED (ex HasActiveGroupName) — cf. fidelity note at the top of this file (audit
// 2026-07-14): Crt_Strcmp(&g_PendingReqTargetName_Sub2,"") ||
// Crt_Strcmp(&g_PendingReqTargetName_Sub1,"") — true if EITHER of the two "pending
// request target name" slots (0x167468A / 0x1674697) is non-empty. Used every 300 frames
// (together with the Net_SendPacket_Op13 keepalive) to decide whether to send
// Net_SendOp64 — cf. InGameTickFlowHost (Game/InGameTickFlow.h, fields to rename, cf.
// header note). THIS IS NOT a persistent guild/party name — GameState.h NO LONGER has a
// "GroupIdentity" field that confused these two globals (removed by the "CABLAGE ROSTER
// ALLIANCE/GUILDE" mission, 2026-07-14); the real active guild name is
// `game::g_World.allianceRoster.guildName` (DIFFERENT address, g_LocalGuildName
// 0x168740C, cf. file header note). The two globals above are two slots written by
// Net_OnRequestTargetNameSet (Pkt SC 0x44) and cleared by Net_OnRequestCancelClear
// (Pkt SC 0x45), read by the clan/faction UI (UI_ClanWin_OnLUp,
// UI_NpcMenu_RequestJoinFaction, UI_ClanCreate_Validate, UI_GameHud_*). Parameters named
// accordingly (reqTargetSub2/reqTargetSub1, NOT guildName/groupName) — this module stays
// self-contained (no cross-include with GameState.h for a single one-line function).
bool HasPendingTargetRequest(const std::string& reqTargetSub2, const std::string& reqTargetSub1);

} // namespace ts2::game

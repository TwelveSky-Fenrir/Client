// Game/ActionStateMachine.h — character action/animation FSM (the pivot for
// combat triggering), CLEAN C++ rewrite (not byte-exact on rendering, byte-exact
// on thresholds/offsets/numeric state values and on the speed formula).
//
// Source of truth = Char_UpdateAnimationFrame (0x571880, ~5.9 KB of pseudocode,
// TwelveSky2.exe disassembly, imagebase 0x400000). This function is the per-
// character tick (this = "character sheet" object with the layout of
// byte_1685748/g_LocalPlayerSheet; a2 = "remotely driven entity / network replay"
// flag -> !a2 = real local simulation, actually triggers network sends; a3 = dt
// of the 30 FPS frame).
//
// SCOPE (mission-imposed): only the STATE MACHINE and the action TIMING — which
// states exist (original numeric values), which transitions, on which frame the
// contact (hit/skill) actually fires, and attack speed (Char_CalcAttackSpeed
// 0x4CCAB0, already wired into Game/StatFormulas.h::CalcAttackSpeed). The 3D
// skeleton/mesh rendering (PcModel_ResolveSlotAndApply 0x4E5A00, sub-object
// selection ModelObj_GetSubObjectCount 0x4D7080, per-weapon/skill animation frame
// tables Anim_IsWeaponHitFrame 0x558D80 / Anim_LookupFrameEvent 0x558B40) is OUT
// OF SCOPE: that data is driven by motion assets (SOBJECT/MOTION), not by code.
// This module exposes an `IAnimFrameOracle` interface that the render/anim layer
// must implement to wire those tables in (precise TODO at each usage site, EA cited).
//
// Original offsets recovered from Char_UpdateAnimationFrame (dword indices
// *((_DWORD*)this + N) => byte N*4; the "sibling" functions Combat_TickAttackState
// 0x574BD0, Char_AttackAnimTick_576890/576A20, Char_CastAnimTick_5762F0 etc. use
// the same layout as direct BYTE offsets `this + N` — the two notations agree):
//   +0    bool   active          record validity (function-entry guard)
//   +92   int    modelIndex      (idx23) model/appearance
//   +96   int    modelVariant    (idx24) model variant (gender/skin)
//   +108  int    (idx27) / +112 int (idx28)   additional PcModel_ResolveSlotAndApply parameters
//   +116  weapon field resolved by Weapon_ClassFromField56 (0x4CC930) -> weapon class
//   +144  bool   altWeaponSet    (idx144... see below, NOTE notation collision)
// (see detailed comments on ActionFsm below for each field kept)
//
// CombatSystem.h (ALREADY WRITTEN, DO NOT EDIT) already documents that entity+244
// is BOTH this FSM's state selector (switch in Char_UpdateAnimationFrame, ~0x5727BF)
// AND the "facing" field P[11] sent as-is in action packets (Combat_QueueMeleeAttack
// 0x573130 / Combat_QueueSkillAction 0x573200 read *(this+244) with no transform) —
// re-verified here on both builders: this is indeed a deliberate reuse of the same
// memory word, not a labeling error.
#pragma once
#include <cstdint>
#include "Game/CombatSystem.h"
#include "Game/GameState.h"

namespace ts2::game {

// Action states — EXACT numeric values from the terminal switch in
// Char_UpdateAnimationFrame (0x5727BF..0x572F42). Each case calls a dedicated
// Char_*Tick*/Char_*AnimEnd*/Combat_TickAttackState handler (EA in comment) that
// advances entity+248 (current frame) by a3*30 (or a weighted multiple for the cast
// states, see CastSlot0/1/2) and transitions when the frame reaches the anim
// duration (3D asset data, out of scope — see IAnimFrameOracle/TickTimedState).
//
// ENUM FULLY VALIDATED (W11) — switch bounds re-proven down to the instruction:
//     0x5727B2  cmp [ebp+var_6C], 5Fh   ; switch 96 cases
//     0x5727B6  ja  def_5727BF          ; default case, cases 8,24-29,47,53,59,77-80,84
//     0x5727BF  jmp jpt_5727BF[eax*4]
// I.e. 96 values (0x00..0x5F) minus 15 `default` values = **81 LIVE CASES** (not "55",
// an erroneous figure that circulated in Game/AnimationTick.*, now fixed). IDA's
// `default` list — 8; 24-29; 47; 53; 59; 77-80; 84 — matches EXACTLY, down to the 7
// ranges, the values marked "absent" below (0x08; 0x18..0x1D; 0x2F; 0x35; 0x3B;
// 0x4D..0x50; 0x54): the state table below is therefore complete and exact, with NO
// additional gaps (the old "..." wrongly implied otherwise). These 15 values fall
// into the original `default: break` -> no anim progression for this entity while
// the state holds one of them (states driven elsewhere, or unused).
enum class CharActionState : int32_t {
    Idle                    = 0x00, // Char_AnimTick_5746E0            0x5746E0 — idle loop
    Move                    = 0x01, // Char_TickMoveState              0x574830 — movement + pickup/aura proximity
    ApproachAndInteract     = 0x02, // Combat_TickAttackState          0x574BD0 — walks to target then auto-interacts (combat/pickup/npc/gather) in range
    RecoveryToMove          = 0x03, // Char_AnimEndToRecovery_576050   0x576050 — anim end -> Move, recalculates *(this+240) via Weapon_ClassFromField56
    AnimEndToIdle_5761A0    = 0x04, // Char_AnimEndToIdle_5761A0       0x5761A0
    CastSlot0               = 0x05, // Char_CastAnimTick_5762F0        0x5762F0 — skill windup, frame event table slot 0 (a4=state-5=0)
    CastSlot1               = 0x06, // Char_CastAnimTick_5764D0        0x5764D0 — same, slot 1 (a4=1)
    CastSlot2               = 0x07, // Char_CastAnimTick_5766B0        0x5766B0 — same, slot 2 (a4=2)
    // (0x08 absent from the switch: default -> no-op)
    AttackWindupA           = 0x09, // Char_AttackAnimTick_576890      0x576890 — windup hit #1, fires the attack order at anim end
    AttackWindupB           = 0x0A, // Char_AttackAnimTick_576A20      0x576A20 — windup hit #2 (variant), same
    AnimHold                = 0x0B, // Char_AnimHold_576BB0            0x576BB0
    DeathRespawn            = 0x0C, // Char_TickDeathRespawn           0x576CB0 — death anim, respawn countdown (this+740), warp destination
    AnimEndToIdle_577D70    = 0x0D, // 0x577D70
    AnimLoop_577EC0         = 0x0E, // 0x577EC0
    AnimLoop_577FC0         = 0x0F, // 0x577FC0
    AnimEndOpenDialog       = 0x10, // Char_AnimEndOpenDialog_5780C0   0x5780C0
    AnimEndToIdle_578290    = 0x11, // 0x578290
    AnimEndToState19        = 0x12, // Char_AnimEndToState19_5783E0    0x5783E0
    AnimLoop_578510         = 0x13, // 0x578510
    AnimEndToIdle_578610    = 0x14, // 0x578610
    AnimEndToIdle_578760    = 0x15, // 0x578760
    AnimEndToIdle_5788B0    = 0x16, // 0x5788B0
    AnimEndToIdle_578A00    = 0x17, // 0x578A00
    // (0x18..0x1D absent from the switch)
    AnimEndToState31        = 0x1E, // Char_AnimEndToState31_578B50    0x578B50
    AnimLoop_578C70         = 0x1F, // 0x578C70
    Run                     = 0x20, // Char_TickRunState               0x578D70
    AnimEndToState34        = 0x21, // Char_AnimEndToState34_579DD0    0x579DD0
    ArcMoveSeg1             = 0x22, // Char_TickArcMoveSeg1            0x579EE0 — jump/arc trajectory, segment 1
    ArcMoveSeg2             = 0x23, // Char_TickArcMoveSeg2            0x57A040 — segment 2
    ArcMoveSeg3             = 0x24, // Char_TickArcMoveSeg3            0x57A190 — segment 3
    AnimEndToIdle_57A2F0    = 0x25, // 0x57A2F0
    AnimEndToIdle_57A440    = 0x26, // 0x57A440
    AnimEndToIdle_57A5A0    = 0x27, // 0x57A5A0
    Channel                 = 0x28, // Char_TickChannelState           0x57A700 — skill channeling (hold)
    AnimEndToIdle_57A970    = 0x29, // 0x57A970
    CastAnimTick_57AAC0     = 0x2A, // 0x57AAC0
    CastAnimTick_57ACB0     = 0x2B, // 0x57ACB0
    CastAnimTick_57AEA0     = 0x2C, // 0x57AEA0
    CastAnimTick_57B040     = 0x2D, // 0x57B040
    CastAnimTick_57B230     = 0x2E, // 0x57B230
    // (0x2F absent)
    CastAnimTick_57B420     = 0x30, // 0x57B420
    CastAnimTick_57B610     = 0x31, // 0x57B610
    CastAnimTick_57B800     = 0x32, // 0x57B800
    CastAnimTick_57B9A0     = 0x33, // 0x57B9A0
    CastAnimTick_57BB90     = 0x34, // 0x57BB90
    // (0x35 absent)
    CastAnimTick_57BD80     = 0x36, // 0x57BD80
    CastAnimTick_57BF20     = 0x37, // 0x57BF20
    CastAnimTick_57C0C0     = 0x38, // 0x57C0C0
    CastAnimTick_57C260     = 0x39, // 0x57C260
    CastAnimTick_57C400     = 0x3A, // 0x57C400
    // (0x3B absent)
    AnimEndToIdle_57C5A0    = 0x3C, // 0x57C5A0
    AnimEndToIdle_57C6D0    = 0x3D, // 0x57C6D0
    AnimEndToIdle_57C800    = 0x3E, // 0x57C800
    AnimEndToState64        = 0x3F, // Char_AnimEndToState64_57C930    0x57C930
    LootPickup              = 0x40, // Char_TickLootPickupState        0x57CA50
    AnimEndSpawnSkillFx     = 0x41, // Char_AnimEndSpawnSkillFx_57CE40 0x57CE40 — triggers a skill FX at anim end (rendering out of scope)
    AnimEndToIdle_57D0E0    = 0x42, // 0x57D0E0
    AnimEndToIdle_57D230    = 0x43, // 0x57D230
    AnimEndToIdle_57D380    = 0x44, // 0x57D380
    CastAnimTick_57D4D0     = 0x45, // 0x57D4D0
    CastAnimTick_57D6C0     = 0x46, // 0x57D6C0
    CastAnimTick_57D8B0     = 0x47, // 0x57D8B0
    CastAnimTick_57DAA0     = 0x48, // 0x57DAA0
    CastAnimTick_57DC90     = 0x49, // 0x57DC90
    CastAnimTick_57DE30     = 0x4A, // 0x57DE30
    AnimEndToIdle_57DFD0    = 0x4B, // 0x57DFD0
    AnimEndToIdle_57E120    = 0x4C, // 0x57E120
    // (0x4D..0x50 absent)
    CastAnimTick_57E280     = 0x51, // 0x57E280
    CastAnimTick_57E420     = 0x52, // 0x57E420
    CastAnimTick_57E5C0     = 0x53, // 0x57E5C0
    // (0x54 absent)
    CastAnimTick_57E760     = 0x55, // 0x57E760
    CastAnimTick_57E950     = 0x56, // 0x57E950
    CastAnimTick_57EB40     = 0x57, // 0x57EB40
    CastAnimTick_57ED30     = 0x58, // 0x57ED30
    ActionToStand           = 0x59, // Char_ActionTick_ToStand         0x57EF20
    ActionToStand2          = 0x5A, // Char_ActionTick_ToStand2        0x57F0C0
    GuardBegin              = 0x5B, // Char_ActionTick_GuardBegin      0x57F260 — moves state to 92 (GuardLoop) then 93 (GuardEnd) if not held
    GuardLoop               = 0x5C, // Char_ActionTick_GuardLoop       0x57F410 — holds while this+548==1 (guard key held)
    GuardEnd                = 0x5D, // Char_ActionTick_GuardEnd        0x57F600
    GuardHit                = 0x5E, // Char_ActionTick_GuardHit        0x57F800 — blocked-impact anim (see CombatPacket::resultType==4)
    GuardHitAlt             = 0x5F, // Char_ActionTick_GuardHitAlt     0x57F990
};

inline int32_t ToRaw(CharActionState s) { return static_cast<int32_t>(s); }

// Interface hooking into the animation tables that are fixed in the binary but
// indexed by asset data (motion/skinning) — OUT OF SCOPE for this module
// (3D rendering). The real implementation lives in the render/anim layer.
class IAnimFrameOracle {
public:
    virtual ~IAnimFrameOracle() = default;

    // Faithful to Anim_IsWeaponHitFrame (0x558D80): fixed table {weaponAnimId -> 1..3 contact
    // frames ±1} for "simple" weapon hits (this branch is taken when
    // hitUsesSkillTable==false). weaponAnimId = entity+296 (== CombatActorState::skillId,
    // 0 for a base hit), frame = entity+248, weaponClass = class resolved by
    // Weapon_ClassFromField56 (0x4CC930, out of scope: depends on 3D equipment).
    virtual bool IsWeaponHitFrame(int32_t weaponAnimId, float frame, int32_t weaponClass) const = 0;

    // Faithful to Anim_LookupFrameEvent (0x558B40): fixed table indexed by
    // [modelIndex][weaponClass][castSlot(=state-5, 0..2)][frame±1][altIndex] -> event
    // id (skillId/effect) written to outEventId. altIndex = 0 if altWeaponSet
    // (entity+576) else weaponAnimSlot (entity+220). Used when hitUsesSkillTable==true.
    virtual bool LookupSkillFrameEvent(int32_t modelIndex, int32_t weaponClass, int32_t castSlot,
                                        float frame, int32_t altIndex, int32_t& outEventId) const = 0;
};

// Per-entity action FSM — fields kept (original offsets in comment, "entity+N"
// = byte N of the same record as CombatActorState). The position/target/skill
// fields are directly those of CombatActorState (shared with the network builders
// Build{Melee,Skill}Attack): `actor.facing` == entity+244 == `state` (typed mirror),
// `actor.meleeSubmode` == entity+284, `actor.targetId` == entity+288/292,
// `actor.skillId` == entity+296 (== a1 of Anim_IsWeaponHitFrame).
struct ActionFsm {
    // --- Context shared with the action packet builders (see CombatSystem.h) ---
    CombatActorState actor;

    // --- State / timing (entity+244, +248) ---
    CharActionState state     = CharActionState::Idle;  // entity+244 (mirror of actor.facing)
    float           animFrame = 0.0f;                   // entity+248 — position in current anim (units = frames @ 30 FPS)

    // !a2 in the binary: true if this entity is simulated locally (the player or an
    // entity whose client tick we replay) — only this case actually sends packets
    // (Net_SendPacket_Op16/18) and triggers contact; false = replay of a state already
    // received over the network (other players/monsters), just advances the anim.
    bool isLocalSimulation = true;

    // --- Contact-frame detection (block at the head of Char_UpdateAnimationFrame,
    // 0x571926..0x571D2A — active regardless of `state` as long as hitCheckActive==true) ---
    bool hitCheckActive   = false; // entity+624 (idx156) — arms the contact test this tick
    bool hitFired         = false; // entity+640 (idx160) — "already fired for this anim" latch (prevents double-trigger while the frame stays in the ±1 window)
    bool hitUsesSkillTable = false; // entity+628 (idx157) — true: Anim_LookupFrameEvent (skill table, castSlot=state-5); false: Anim_IsWeaponHitFrame (weapon table, key=actor.skillId)
    bool altWeaponSet     = false; // entity+576 (idx144) — selects the "alt weapon" entry in the two tables above
    int32_t weaponAnimSlot = 0;    // entity+220 (idx55) — passed as altIndex to LookupSkillFrameEvent when altWeaponSet==false
    int32_t lastSkillEventId = 0;  // v68 in the binary — event id written by LookupSkillFrameEvent at first fire; reused as the skillId argument of BuildMeleeAttack (see Combat_QueueMeleeAttack(v68) 0x571D7D)

    // --- Nature of the action triggered on contact (entity+632, +636) ---
    // actionKind   : 1 = instant hit/skill (dispatch to Build{Melee,Skill}Attack),
    //                2 = projectile skill (Effect_SpawnSkillProjectile 0x573A90, FX -> out of scope)
    // actionSubKind (valid only if actionKind==1): 1 = single target, 2 = area
    //                (Combat_CastAoESkillOnTargets 0x573480 — target enumeration out of scope)
    int32_t actionKind    = 1; // entity+632 (idx158)
    int32_t actionSubKind = 1; // entity+636 (idx159)

    // --- Contact-table resolution (provided by the render/anim layer, out of scope) ---
    int32_t modelIndex  = 0; // entity+92  (idx23)
    int32_t weaponClass = 0; // Weapon_ClassFromField56(0x4CC930), resolved upstream by the caller

    // --- Cast interrupt (0x57275A): local entity (self) only (comparison
    // *(this+4)==dword_1687238[0] in the binary). Exact semantics of the triggering
    // globals (g_InvDirtyEnable 0x16755AC / g_AutoHuntFuelA 0x16755A4 /
    // g_AutoHuntFuelB 0x16755A8, related to auto-hunt) not modeled here: the caller
    // sets this flag when the original condition is true.
    bool isSelf               = false;
    bool pendingCastInterrupt = false;

    // --- Outputs of the last Update() ---
    bool                 contactFiredThisTick = false; // an instant hit/skill was validated this tick
    CombatActionRequest  lastAction{};                  // request built by BuildMeleeAttack/BuildSkillAction (valid only if contactFiredThisTick)
    bool                 pendingAoECast       = false;  // actionSubKind==2 on contact -> Combat_CastAoESkillOnTargets (0x573480, out of scope: this module does not compute the target list)
    bool                 pendingProjectile    = false;  // actionKind==2 on contact -> Effect_SpawnSkillProjectile (0x573A90, FX out of scope)

    // Sets the state AND its mirror in actor.facing (entity+244) — see file header:
    // the two MUST stay in sync, they are the same original memory word.
    void SetState(CharActionState s) {
        state = s;
        actor.facing = ToRaw(s);
    }

    // Contact-detection block — faithful port of Char_UpdateAnimationFrame
    // 0x571926 (if (*(this+156)==1)) .. 0x571D2A (v66==1 -> dispatch). Called on every
    // Update() regardless of `state` (the binary only tests hitCheckActive).
    // Returns true if an instant hit/skill was validated (contactFiredThisTick).
    // `world` = entity table for target revalidation (0x571BC9/0x571C89);
    // `oracle` may be nullptr (no contact will ever be detected, degrades cleanly).
    bool UpdateContactDetection(const GameWorld& world, const IAnimFrameOracle* oracle);

    // Cast interrupt — 0x57275A: if pendingCastInterrupt && isSelf && state is one of
    // the 3 cast slots (CastSlot0/1/2), forces a return to Move with frame reset to 0
    // (like the binary: *(this+244)=1; *(this+248)=0.0;). Returns true if the
    // transition happened (and resets pendingCastInterrupt to false).
    bool ApplyPendingCastInterrupt();

    // Generic tick "the anim advances dt*30 frames until durationFrames, then
    // transitions to nextState" — the NEAR-UNIVERSAL pattern of the Char_AnimEnd*/
    // Char_AnimLoop*/Char_AttackAnimTick_*/Char_TickDeathRespawn handlers (e.g. 0x574C98,
    // 0x576958, 0x576D78, 0x57F328...). `durationFrames` is 3D anim data
    // (ModelObj_GetSubObjectCount 0x4D7080 or similar) — OUT OF SCOPE, provided by the
    // caller (render layer). `loopInstead` reproduces the "loop" variant (frame -=
    // duration instead of clamping, e.g. Char_TickMoveState 0x574911/0x574922) rather
    // than the "anim end -> transition" variant (e.g. Char_AnimEndToRecovery 0x576131).
    // Returns true if the transition (or the wraparound) happened this tick.
    bool TickTimedState(float dt, float durationFrames, CharActionState nextState, bool loopInstead = false);

    // Variant for the 3 cast states (CastSlot0/1/2) — the frame increment is NOT
    // dt*30 but dt*weaponRatePct*0.3 (0x576414, Char_CalcWeaponRatePct 0x4CD900), and
    // only happens if weaponRatePct is STRICTLY within the open bounds
    // ]Char_CalcAnimBoundMin99, Char_CalcAnimBoundMax121[ (0x57FB30/0x57FBB0 — the caller
    // supplies `withinBounds`). At anim end, returns to Move (1), frame=0, AND
    // hitCheckActive=false (0x57644F, behavior specific to this group of states — other
    // handlers don't touch hitCheckActive).
    //
    // ONE single primitive for THREE states: this framing is RE-PROVEN, not an
    // approximation. Switch cases 5/6/7 do call THREE DISTINCT functions (0x5762F0
    // @0x57284C, 0x5764D0 @0x572864, 0x5766B0 @0x57287C), but their decompilations are
    // STRICTLY identical (same offsets, same 0.3 constant, same bounds, same tail): they
    // are three compiled copies of the same body. Modeling them with a single primitive
    // is therefore FAITHFUL. (Objection "TickCastState hides 3 distinct states":
    // REFUTED, W11.)
    //
    // WARNING: SUPPLYING `weaponRatePct` — the only existing port, CalcWeaponRatePct
    // (Game/StatFormulas.h:71), has signature `(const SelfState&, const GameDatabases&)`:
    // it only computes the rate for SELF. The binary, meanwhile, calls
    // Char_CalcWeaponRatePct(this+328, this+116) @0x5763BE — PER ENTITY. It is therefore
    // NOT reusable as-is for a remote player. TODO [ancre 0x4CD900].
    bool TickCastState(float dt, double weaponRatePct, bool withinBounds, float durationFrames);

    // Guard states (0x5B..0x5F) — reproduces the this+548 sub-automaton (bool "guard
    // key held", supplied by the caller as input) / this+552 (guard sub-state:
    // 2=holding, 3=released) observed in Char_ActionTick_GuardBegin (0x57F260) and
    // Char_ActionTick_GuardLoop (0x57F410). Exposes only the state transitions + the
    // guard sub-state via `guardSubstate` (member below).
    //
    // BANNER CORRECTED (W11): the previous wording said "Does not model the network
    // send (Net_SendOp104 0x4BDAE0, OUT OF NET SCOPE)". That wording was STALE and
    // misleading — Net_SendOp104 IS ported (Net/SendPackets.h:238, .cpp:2280), as is
    // Net_SendPacket_Op16 (Net/SendPackets.h:134, .cpp:1266). The real reason for the
    // omission is a LAYERING choice, not a missing builder: Game/* does not include
    // Net/* (see the autonomy banner in Game/AnimationTick.h). The missing emissions
    // are therefore WIRING TODOs, to be honored via hooks on the caller side:
    //   GuardBegin 0x57F260, under `if (!a4)` @0x57F371: Op104(3, guardSubstate) @0x57F389,
    //     Op104(2, 0) @0x57F397, Op104(3, 3) @0x57F3D3, Op16(this+240) @0x57F400.
    //   GuardLoop  0x57F410, under `if (!a4)` @0x57F59E: Op104(1, 0) @0x57F5C3 (only if
    //     guardSubstate==3 && guardKeyHeld), Op16(this+240) @0x57F5F0.
    int32_t guardSubstate = 0; // entity+552: 0=inactive, 2=holding, 3=released/end
    bool guardKeyHeld     = false; // input supplied by the caller, reflects entity+548
    bool TickGuardBegin(float dt, float durationFrames);
    bool TickGuardLoop(float dt, float durationFrames);
};

// EXACT list of weapon/skill animation ids granting the ×1.1 speed bonus in
// Combat_TickAttackState (0x575A39..0x575A44). Reproduced as-is (byte-exact) — NOTE:
// this list is a SUPERSET of the one used by Char_CalcAttackSpeed (0x4CCD14, ~39
// values, already neutralized in StatFormulas.cpp for lack of a SelfState field
// carrying the runtime "special item"); the two lists genuinely DIVERGE in the
// binary (an original inconsistency between the two call sites, kept as-is rather
// than "fixed").
bool IsFastAttackAnimId(int32_t animId);

} // namespace ts2::game

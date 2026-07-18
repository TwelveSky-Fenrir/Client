// Game/ActionStateMachine.cpp — implementation of the action FSM (ts2::game).
// Faithful to Char_UpdateAnimationFrame (0x571880) and the state handlers cited in
// comments in ActionStateMachine.h. See that file for the full table of original
// offsets and the scope (rendering excluded).
#include "Game/ActionStateMachine.h"

namespace ts2::game {

// Contact-frame detection — 0x571926..0x571D2A.
bool ActionFsm::UpdateContactDetection(const GameWorld& world, const IAnimFrameOracle* oracle) {
    contactFiredThisTick = false;
    pendingAoECast        = false;
    pendingProjectile     = false;

    if (!hitCheckActive)   // *(this+156) != 1 (0x571926) -> no contact test this tick
        return false;

    bool justFired = false; // v66

    if (hitFired) {
        // *(this+160) != 0 (0x571936): a contact has already fired for this anim; we
        // only check whether we're STILL within the ±1 window, and if not, close the
        // latch (0x571A7B..0x571B7B / 0x571B4D..0x571B7B). Does NOT retrigger an action.
        bool stillInWindow = false;
        if (oracle) {
            if (hitUsesSkillTable) {
                const int32_t castSlot = ToRaw(state) - 5;
                const int32_t altIndex = altWeaponSet ? 0 : weaponAnimSlot;
                int32_t dummyEvent = 0;
                stillInWindow = oracle->LookupSkillFrameEvent(modelIndex, weaponClass, castSlot,
                                                                animFrame, altIndex, dummyEvent);
            } else {
                stillInWindow = oracle->IsWeaponHitFrame(actor.skillId, animFrame, weaponClass);
            }
        }
        if (!stillInWindow)
            hitFired = false; // *(this+160) = 0
    } else if (hitUsesSkillTable) {
        // 0x57194D..0x571A18: skill table (Anim_LookupFrameEvent), castSlot = state-5
        // (valid only for CastSlot0/1/2; otherwise castSlot falls outside {0,1,2} and the
        // oracle must return false — it is the oracle implementation's job to guarantee this).
        const int32_t castSlot = ToRaw(state) - 5;
        const int32_t altIndex = altWeaponSet ? 0 : weaponAnimSlot;
        int32_t skillEventId = 0;
        if (oracle && oracle->LookupSkillFrameEvent(modelIndex, weaponClass, castSlot,
                                                      animFrame, altIndex, skillEventId)) {
            hitFired = true;          // *(this+160) = 1
            justFired = true;         // v66 = 1
            lastSkillEventId = skillEventId; // v68
        }
    } else {
        // 0x571A2D..0x571A65: simple weapon table (Anim_IsWeaponHitFrame), key = actor.skillId
        // (== entity+296, 0 for a base hit).
        if (oracle && oracle->IsWeaponHitFrame(actor.skillId, animFrame, weaponClass)) {
            hitFired  = true;
            justFired = true;
        }
    }

    if (!justFired)
        return false;

    // 0x571B8F..0x571D2A: target revalidation before allowing the effect. The binary
    // rescans the corresponding entity array, testing (active && netID==target &&
    // "targetable" target-state). The exact target-state (sub_558AE0/558B10,
    // dword_168724C) is NOT ported by PlayerEntity/MonsterEntity (Game/GameState.h, not
    // modifiable here): TODO 0x558AE0 (Char_IsTargetablePlayerState) / 0x558B10
    // (Char_IsTargetableMonsterState) — approximated here as (active && id matches).
    bool targetValid = false;
    switch (actor.meleeSubmode) { // entity+284
    case 0:
        // 0x571BB6 case 0: no specific target class -> always validated (v67=1).
        targetValid = true;
        break;
    case 2:
    case 3:
        for (const PlayerEntity& p : world.players) {
            if (p.active && p.id == actor.targetId) { targetValid = true; break; }
        }
        break;
    case 5:
        for (const MonsterEntity& m : world.monsters) {
            if (m.active && m.id == actor.targetId) { targetValid = true; break; }
        }
        break;
    default:
        // {1,4,6,7}: non-combat (player/object/npc/gather interactions) -> no combat
        // contact triggered by this block (0x571BB6 default: break, v67 stays 0).
        targetValid = false;
        break;
    }
    if (!targetValid)
        return false;

    // 0x571D2A..0x571DAF: dispatch based on actionKind (entity+632) / actionSubKind (entity+636).
    if (actionKind == 1) {
        if (actionSubKind == 1) {
            // 0x571D66: if (!a2) — only the locally-simulated entity actually sends the
            // action packet (Net_SendPacket_Op18, in Combat_QueueMeleeAttack/SkillAction).
            if (isLocalSimulation) {
                if (hitUsesSkillTable)
                    // 0x571D7D: Combat_QueueMeleeAttack(v68) — v68 = event id returned by
                    // the skill table, reused as the skillId argument of the melee builder
                    // (original behavior, seemingly inverted relative to the
                    // hitUsesSkillTable/BuildMeleeAttack names — faithfully kept).
                    lastAction = BuildMeleeAttack(actor, lastSkillEventId);
                else
                    // 0x571D87: Combat_QueueSkillAction(this).
                    lastAction = BuildSkillAction(actor);
                contactFiredThisTick = true;
            }
        } else if (actionSubKind == 2) {
            // 0x571D99: Combat_CastAoESkillOnTargets(this) — area target enumeration,
            // TODO 0x573480 (out of scope: multi-target selection logic).
            if (isLocalSimulation)
                pendingAoECast = true;
        }
    } else if (actionKind == 2) {
        // 0x571DA7: Effect_SpawnSkillProjectile(a2) — called WITHOUT the !a2 test in the
        // binary (unlike the two branches above): a projectile is signaled even for an
        // entity replayed from the network (visual effect on the spectator side).
        // TODO 0x573A90 (out of scope: FX/rendering).
        pendingProjectile = true;
    }

    return contactFiredThisTick;
}

// Cast interrupt — 0x57275A..0x5727AC.
bool ActionFsm::ApplyPendingCastInterrupt() {
    if (!pendingCastInterrupt || !isSelf)
        return false;

    if (state == CharActionState::CastSlot0 ||
        state == CharActionState::CastSlot1 ||
        state == CharActionState::CastSlot2) {
        SetState(CharActionState::Move); // *(this+244) = 1
        animFrame = 0.0f;                // *(this+248) = 0.0
        pendingCastInterrupt = false;
        return true;
    }
    return false;
}

// Generic tick "anim end -> transition" / "anim loop".
//
// NATURE (W11 clarification): this is a GENERIC PRIMITIVE parameterized by
// (nextState/loopInstead), NOT the port of one specific handler. The pattern
// `frame += dt*30 ; if (frame >= duration) { transition }` is shared word-for-word by
// the vast majority of the 81 cases in the terminal switch (0x5727BF). The
// case-specific SIDE EFFECTS therefore remain the caller's responsibility — e.g. for
// case 4 (Char_AnimEndToIdle_5761A0 0x5761A0, whose call site is proven @0x572834) the
// binary ADDITIONALLY does, once the anim finishes:
//     *(this+240) = 2 * Weapon_ClassFromField56(g_EquipSnapshotScratch, this+116); @0x57629B
//     if ( !a2 ) { *(this+296) = 0;                                                @0x5762C4
//                  Net_SendPacket_Op16(&g_AutoPlayMgr, this+240); }                @0x5762DC
// TODO [ancre 0x57629B]: +240 (= "animSlot", g_SelfMoveStateBlock 0x1687324 for entity
// 0: 0x1687324 - dword_1687234 = 0xF0 = 240) has NO carrier field in
// game::CharAnimState (Game/GameState.h, out of scope for this front); Weapon_ClassFromField56
// 0x4CC930 is not ported anywhere. Relation proven if a field is added someday:
// +240 == 2 * CharAnimState::weaponClass (weaponClass being ALREADY defined as the
// result of Weapon_ClassFromField56, see ActionStateMachine.h).
bool ActionFsm::TickTimedState(float dt, float durationFrames, CharActionState nextState, bool loopInstead) {
    animFrame += dt * 30.0f; // universal pattern: *(this+248) = a3*30.0 + *(this+248)

    if (animFrame < durationFrames)
        return false;

    if (loopInstead) {
        // e.g. Char_TickMoveState 0x574911/0x574922: *(this+248) -= duration, state unchanged.
        animFrame -= durationFrames;
        return true;
    }

    SetState(nextState);
    animFrame = 0.0f;
    return true;
}

// Tick of the cast states (CastSlot0/1/2) — 0x5763BE..0x57644F.
//
// UPPER BOUND CORRECTED (Pass 4 / W11, gap CTF-02): the previous wording stated
// "0x5763BE..0x576470". That is WRONG — 0x576470 is the `Net_SendPacket_Op16` of the
// `if (!a2)` tail, which was NEVER ported. The code below does stop at 0x57644F
// (`*(this+624) = 0`). See the tail TODO at the end of the function.
bool ActionFsm::TickCastState(float dt, double weaponRatePct, bool withinBounds, float durationFrames) {
    if (withinBounds)
        // 0x576414: *(this+248) = a3 * v6 * 0.3 + *(this+248), v6 = Char_CalcWeaponRatePct.
        animFrame += static_cast<float>(dt * weaponRatePct * 0.300000011920929);
    // If !withinBounds: the binary does NOT advance the frame this tick (0x5763FA),
    // faithfully reproduced by skipping the increment.

    if (animFrame < durationFrames)
        return false;

    // 0x576437..0x57644F: return to Move, frame to 0, AND close the contact latch
    // (behavior SPECIFIC to this group of states — no other handler touches
    // hitCheckActive).
    SetState(CharActionState::Move);
    animFrame = 0.0f;
    hitCheckActive = false;

    // TODO [ancre Char_CastAnimTick_5762F0 0x5762F0 @0x57645D..0x5764C2]: `if (!a2)` tail
    // NOT ported — this is the AUTOMATIC RE-ATTACK CHAIN at the end of a cast:
    //     if ( !a2 ) {                                          @0x57645D
    //         Net_SendPacket_Op16(&g_AutoPlayMgr, this+240);    @0x576470  (builder PORTED,
    //                                                            Net/SendPackets.cpp:1266)
    //         if ( !Game_UseFirstReadySkill() ) {               @0x57647A  (0x538190, NOT ported)
    //             v3 = *(this+284);                             @0x57648E  (== actor.meleeSubmode)
    //             if ( v3 == 2 || v3 == 3 ) Player_AttackTargetPlayer();  @0x5764AA (0x539B00, NOT ported)
    //             else if ( v3 == 5 )       Player_AttackTargetMonster(); @0x5764C2 (0x53A3C0, NOT ported)
    //         }
    //     }
    // Without it, a cast that ends does NOT re-chain onto the current target. Actual
    // blocker: the 3 functions 0x538190/0x539B00/0x53A3C0 are not ported anywhere
    // (exhaustive grep of src/: cited only in comments) — see gap AP-04.
    return true;
}

// Guard states — 0x57F260 (GuardBegin) / 0x57F410 (GuardLoop).
bool ActionFsm::TickGuardBegin(float dt, float durationFrames) {
    animFrame += dt * 30.0f; // 0x57F328

    if (animFrame < durationFrames)
        return false;

    SetState(CharActionState::GuardLoop); // *(this+244) = 92 (0x57F34B)
    animFrame = 0.0f;                     // 0x57F35A
    guardSubstate = 2;                    // *(this+552) = 2 (0x57F363)

    // FIDELITY FIX (Pass 4 / wave W11, front w11-combat-fsm — gap CTF-02).
    // The GuardLoop -> GuardEnd jump is NESTED inside `if (!a4)` @0x57F371 in the binary,
    // so it is reserved for the LOCALLY-simulated entity; the previous wording applied it
    // UNCONDITIONALLY. Structure re-proven by decompiling Char_ActionTick_GuardBegin
    // 0x57F260 this session:
    //     *(this+552) = 2;                                   @0x57F363
    //     if ( !a4 ) {                                       @0x57F371   <-- the missing guard
    //         Net_SendOp104(&g_AutoPlayMgr, 3, *(this+552)); @0x57F389
    //         Net_SendOp104(&g_AutoPlayMgr, 2, 0);           @0x57F397
    //         if ( !*(this+548) ) {                          @0x57F39F
    //             *(this+552) = 3;                           @0x57F3AB
    //             *(this+244) = 93;                          @0x57F3B8
    //             ...
    //     }
    // Consequence of the fixed bug: an entity REPLAYED from the network (a4!=0, where
    // +548 "guard key held" has NO input source and is therefore always 0) was jumping
    // GuardLoop -> GuardEnd on the 1st frame, whereas the binary leaves it in GuardLoop
    // and waits for the state pushed by the server.
    if (isLocalSimulation && !guardKeyHeld) { // !a4 (0x57F371) && !*(this+548) (0x57F39F)
        guardSubstate = 3;                  // 0x57F3AB
        SetState(CharActionState::GuardEnd); // *(this+244) = 93 (0x57F3B8)
    }
    // TODO [ancre 0x57F260 @0x57F389/@0x57F397/@0x57F3D3/@0x57F3E7/@0x57F400]: the
    // `if (!a4)` tail also emits Net_SendOp104(3, guardSubstate), Net_SendOp104(2, 0), then
    // Char_UpdateWeaponGlowState 0x55D740, *(this+296)=0 and Net_SendPacket_Op16(this+240).
    // BOTH builders ARE ported (Net/SendPackets.cpp:2280 and :1266) — the blocker is not
    // "net out of scope" but a layering choice: Game/* does not include Net/* (see the
    // banner in Game/AnimationTick.h). To be wired via hooks on the caller side.
    return true;
}

bool ActionFsm::TickGuardLoop(float dt, float durationFrames) {
    animFrame += dt * 30.0f; // 0x57F4D8

    if (guardKeyHeld) { // *(this+548) == 1 (0x57F4E8)
        if (guardSubstate == 2 && animFrame < durationFrames) // 0x57F509
            return false;
        if (guardSubstate == 2 && animFrame > durationFrames) { // 0x57F52F
            animFrame = 0.0f; // 0x57F536
            return false;
        }
        // guardSubstate != 2 (already 3): falls through to the renewal below.
    } else {
        // 0x57F546..0x57F553: key released -> moves to/holds GuardEnd.
        SetState(CharActionState::GuardEnd);
        guardSubstate = 3;
    }

    animFrame = 0.0f; // 0x57F562

    if (guardSubstate == 3 && guardKeyHeld) { // 0x57F57E (redundant with the case above, faithful to the binary)
        SetState(CharActionState::GuardEnd);
        guardSubstate = 3;
    }
    return true;
}

// List of "fast" animation ids (×1.1 bonus) — Combat_TickAttackState 0x575A39.
bool IsFastAttackAnimId(int32_t id) {
    if (id >= 1301 && id <= 1305) return true;
    if (id == 2489) return true;
    if (id >= 1306 && id <= 1309) return true;
    if (id >= 1313 && id <= 1331) return true;
    if (id == 510 || id == 511) return true;
    if (id == 559) return true;
    if (id >= 814 && id <= 821) return true;
    if (id >= 2266 && id <= 2285) return true;
    if (id == 2316 || id == 2317) return true;
    if (id >= 2422 && id <= 2441) return true;
    if (id >= 1917 && id <= 1936) return true;
    if (id >= 19002 && id <= 19021) return true;
    if (id >= 19025 && id <= 19044) return true;
    if (id >= 19051 && id <= 19070) return true;
    if (id >= 19261 && id <= 19280) return true;
    return false;
}

} // namespace ts2::game

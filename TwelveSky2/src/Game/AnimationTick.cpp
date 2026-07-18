// Game/AnimationTick.cpp — implementation. See Game/AnimationTick.h for the full doc
// (original EA, scope, hosts/oracles). Decompilation source: idaTs2 (Hex-Rays).
// Split family: shared morph-timer helpers live in Game/AnimationTick_Internal.h;
// Camera_UpdateCollision/MapColl_UpdateObjectAnim (§3/§4) live in Game/AnimationTick_World.cpp;
// the monster/zone-NPC/player-FSM dispatch (§5/§6/§7) lives in Game/AnimationTick_Entities.cpp.
#include "Game/AnimationTick.h"
#include "Game/AnimationTick_Internal.h" // MorphDuration, kMorphRows, TickMorphRow
#include "Game/ClientRuntime.h"   // g_Client.Var/VarF (long-tail globals escape hatch)
#include "Game/MapWarp.h"         // BeginWarpToFactionTown, WarpAddr::SelfMorphNpcId
#include "Game/CameraWarpTick.h"  // Cam_SetLookAt (already written, reused as-is)
#include "Game/EntityLifecycleTick.h" // g_MonsterTickExt (motionState/animFrame/attackWindupMode) — §5
#include "Game/ExtraDatabases.h"      // NpcDefRecord::id (decor NPC kind, ZoneNpc_OnDialogueOpen) — §6
                                       // (the NPC pool itself = g_World.npcRenderEntries, via GameState.h)
#include <cmath>
#include <cstring>

namespace ts2::game {

// 1. Player_UpdateLocalAnim 0x5321D0
void Player_UpdateLocalAnim(GameWorld& world, float dt,
                             const IMorphModelOracle* oracle, const LocalAnimTickHost& host) {
    PlayerEntity& self = world.Self();
    const int32_t selfElement = world.self.element;
    const int32_t morphNpcId  = g_Client.VarGet(WarpAddr::SelfMorphNpcId); // g_SelfMorphNpcId

    // --- 0x5321EC..0x53222A: positional ambiance + BGM replay every 900s ---------------
    // WSndBank_UpdatePositional(dword_14A90E0, selfPos, g_Opt_MusicVolume) — OUT OF SCOPE
    // (ambient sound bank instance, property of Audio/Sound3D.h::SoundBank, not
    // modeled in game::g_World): not reproduced here for lack of a dedicated hook in this
    // module (already 4 functions to cover in this mission); the 900s BGM replay below
    // is, however, fully covered.
    float& bgmTimestamp = g_Client.VarF(0x1675B18); // flt_1675B18
    if (world.gameTimeSec - bgmTimestamp > 900.0f) {
        bgmTimestamp = world.gameTimeSec;
        if (host.IsBgmEnabled && host.IsBgmEnabled() && host.PlayAmbientBgm) host.PlayAmbientBgm();
    }

    // --- 3 "pulse" blocks (cadence this+8 %6, threshold >14): dword_1675BAC/BB0, BCC/BD0,
    // BD4/BD8, then, at the very end of the original function, E90/E98.
    // "this" IDENTITY RESOLVED (audit 2026-07-14, fresh re-decompilation via idaTs2):
    // Player_UpdateLocalAnim has only ONE call site, Scene_InGameUpdate 0x52C600
    // (`Player_UpdateLocalAnim(this, *(float*)&a2)` @0x52c95a), itself called by
    // cSceneMgr_Update 0x517BF0 (`case 6: Scene_InGameUpdate(this, a4)`), itself called
    // by App_FrameTick 0x4625D0 with ECX = `&g_SceneMgr` (0x1676180). So "this" HERE IS
    // `&g_SceneMgr`, and `*(this + 2)` (dword offset +8) = the address 0x1676188 — EXACTLY
    // the same counter as `InGameFlowState::frameCounter` documented in
    // Game/InGameTickFlow.h ("g_SceneMgr.frameCounter (originally dword_1676188)"), NOT a
    // player-specific quantity ("character sheet" — an earlier report's hypothesis was
    // wrong). This is therefore no longer an unknown: the ported equivalent ALREADY EXISTS
    // (InGameTickFlow.h), but the exact wiring (`s.frameCounter % 6 == 0`) cannot be plugged in
    // HERE without adding a parameter to Player_UpdateLocalAnim, which would break its
    // sole caller Scene/SceneManager.cpp:446 (`Player_UpdateLocalAnim(g_World, dt,
    // nullptr, localHost)`) — a file outside this mission's write scope (read-only,
    // see file-header banner/CLAUDE.md). Pending this wiring (future consolidation
    // mission: add `bool pulseTick` or `int frameCounter` as a parameter, fed
    // by `s.frameCounter` on the SceneManager.cpp side), we fall back on a
    // FUNCTIONALLY equivalent approximation: `g_SceneMgr.frameCounter` advances
    // exactly ONCE PER CALL of Player_UpdateLocalAnim (so 1:1 with the number of
    // 1/30s ticks elapsed since entering the InGame scene); `world.gameTimeSec / dt`
    // (real time elapsed since App_Init / fixed step) advances at the SAME cadence — only
    // the phase (the %6 alignment offset) differs, which has no observable effect here since
    // the only use of `pulseTick` is to ADVANCE a sub-counter every ~6 frames
    // (same aggregate cadence, no dependency on the exact phase value).
    const bool pulseTick = (dt > 0.0f) && (static_cast<int>(world.gameTimeSec / dt) % 6 == 0);
    auto TickPulse = [&](uint32_t flagAddr, uint32_t counterAddr) {
        int32_t& flag = g_Client.Var(flagAddr);
        if (!flag) return;
        int32_t& counter = g_Client.Var(counterAddr);
        if (pulseTick) ++counter;
        if (counter > 14) flag = 0;
    };
    TickPulse(0x1675BAC, 0x1675BB0);
    TickPulse(0x1675BCC, 0x1675BD0);
    TickPulse(0x1675BD4, 0x1675BD8);

    // --- Special block 0x1675BA4/BA8: table parameterized by g_SelfMorphNpcId (0x5322F0) ---
    {
        int32_t& flag = g_Client.Var(0x1675BA4);
        if (flag) {
            int32_t row = (morphNpcId >= 154) ? 128 : 127;
            if (morphNpcId >= 319 && morphNpcId <= 323) row = 241;
            const uint32_t table = 0xB60AB8u + 148u * static_cast<uint32_t>(row);
            float& frame = g_Client.VarF(0x1675BA8);
            frame += dt * 30.0f;
            const float duration = MorphDuration(oracle, table);
            if (frame >= duration) {
                flag = 0;
                frame = duration - 1.0f;
                if (host.LoadCurrentZoneModel) host.LoadCurrentZoneModel(2);
            }
        }
    }
    // --- Special blocks 0x1675BDC/BE0 and 0x1675BE4/BE8: same parameterized tables, NO
    // clamp/load at the end of the run (0x532411/0x532480) ---
    {
        int32_t& flag = g_Client.Var(0x1675BDC);
        if (flag) {
            const int32_t row = (morphNpcId >= 154) ? 142 : 140;
            const uint32_t table = 0xB60AB8u + 148u * static_cast<uint32_t>(row);
            float& frame = g_Client.VarF(0x1675BE0);
            frame += dt * 30.0f;
            if (frame >= MorphDuration(oracle, table)) flag = 0;
        }
    }
    {
        int32_t& flag = g_Client.Var(0x1675BE4);
        if (flag) {
            const int32_t row = (morphNpcId >= 154) ? 143 : 141;
            const uint32_t table = 0xB60AB8u + 148u * static_cast<uint32_t>(row);
            float& frame = g_Client.VarF(0x1675BE8);
            frame += dt * 30.0f;
            if (frame >= MorphDuration(oracle, table)) flag = 0;
        }
    }

    // --- 75 generic rows (kMorphRows table) ---------------------------------------------
    for (const MorphTimerRow& row : kMorphRows)
        TickMorphRow(row, dt, oracle, host, selfElement, self.x, self.y, self.z);

    // --- Block indexed by g_LocalElement: dword_1675D98[elt]/flt_1675DA8[elt] (0x533332) ----
    {
        constexpr uint32_t kBase1675D98 = 0x1675D98, kBase1675DA8 = 0x1675DA8;
        const uint32_t elt = static_cast<uint32_t>(selfElement < 0 ? 0 : selfElement);
        int32_t& flag = g_Client.Var(kBase1675D98 + 4u * elt);
        if (flag) {
            float& frame = g_Client.VarF(kBase1675DA8 + 4u * elt);
            frame += dt * 30.0f;
            const float duration = MorphDuration(oracle, 0xB68CCCu);
            if (frame >= duration) {
                flag = 0;
                frame = duration - 1.0f;
                if (host.LoadCurrentZoneModel) host.LoadCurrentZoneModel(selfElement + 1);
            }
        }
    }

    // --- Final pulse block (0x534494) + final check (0x5344DE) -------------------------
    TickPulse(0x1675E90, 0x1675E98);
    if (morphNpcId == 196 && g_Client.VarGet(0x1685E10) == 1)
        BeginWarpToFactionTown(selfElement);
}

// 2. Char_UpdateAnimationFrame 0x571880
namespace {

// Descriptor for the 8 "secondary" FX timers (entity+820..877). Index 0..4 = "double"
// tables parameterized by modelIndex/modelVariant (choice based on weaponAnimSlot!=0 &&
// !altWeaponSet); index 5..7 = fixed "simple" tables (no branch, same two tables on
// both sides of the binary for index 5 -> a single value is enough).
struct CharFxRow {
    bool     doubleTable;   // true = parameterized (tableWeaponSlot/tableAlt), false = simple
    uint32_t tableWeaponSlot; // used if (weaponAnimSlot!=0 && !altWeaponSet)
    uint32_t tableAlt;        // used otherwise (or the single table if !doubleTable)
};

constexpr CharFxRow kCharFxRows[8] = {
    {true,  0xADF758, 0xA71538}, // idx205/206
    {false, 0xB68264, 0xB68264}, // idx207/208 (same table on both sides in the binary)
    {true,  0xAEFDD0, 0xA81BB0}, // idx209/210
    {true,  0xAF00B4, 0xA81E94}, // idx211/212
    {true,  0xAF01DC, 0xA81FBC}, // idx213/214
    {true,  0xAF0304, 0xA820E4}, // idx215/216
    {false, 0xB67B74, 0xB67B74}, // idx217/218
    {false, 0xB66610, 0xB66610}, // idx219/220
};

// Resolves the table for a "double" row: base + 150368*modelIndex + 75184*modelVariant
// (EXACT formula of the call sites 0x571EAD/0x571E60/etc.).
uint32_t ResolveCharFxTable(const CharFxRow& row, bool useWeaponSlotTable,
                             int32_t modelIndex, int32_t modelVariant) {
    const uint32_t base = useWeaponSlotTable ? row.tableWeaponSlot : row.tableAlt;
    if (!row.doubleTable) return base;
    return base + 150368u * static_cast<uint32_t>(modelIndex)
                + 75184u  * static_cast<uint32_t>(modelVariant);
}

// Ticks one of the 8 secondary FX timers (0x571DE4..0x572425): grows toward
// ModelObj_GetSubObjectCount(table), clears the flag at the end of the run (NO frame
// clamp here — the binary does not do it for ANY of these 8 blocks, unlike the timers in
// Player_UpdateLocalAnim).
void TickCharFxSlot(FxTimerSlot& slot, const CharFxRow& row, float dt,
                     const IMorphModelOracle* oracle, int32_t weaponAnimSlot,
                     bool altWeaponSet, int32_t modelIndex, int32_t modelVariant) {
    if (!slot.active) return;
    slot.frame += dt * 30.0f;
    const bool useWeaponSlotTable = (weaponAnimSlot != 0) && !altWeaponSet;
    const uint32_t table = ResolveCharFxTable(row, useWeaponSlotTable, modelIndex, modelVariant);
    if (slot.frame >= MorphDuration(oracle, table)) slot.active = false;
}

} // namespace (Char_UpdateAnimationFrame helpers)

void Char_UpdateAnimationFrame(CharAnimState& anim, const CombatActorState& actor,
                                const GameWorld& world, const IAnimFrameOracle* hitOracle,
                                bool isLocalSimulation, bool isSelf, bool pendingCastInterrupt,
                                float dt, const IMorphModelOracle* modelOracle,
                                const std::function<void(CharActionState state, float dt)>& stateHandler,
                                const CharAnimTickHost& host, CharAnimTickResult& outResult) {
    outResult = CharAnimTickResult{};

    // --- Cast countdown UI (0x5718A3..0x5718DA): tied to the shared global dword_1675704/
    // dword_1675700 — only meaningful for the locally-simulated entity (!a2), faithful. -----
    if (g_Client.VarGet(0x1675704) == 1 && isLocalSimulation) {
        anim.cooldownA -= dt;
        if (anim.cooldownA < 0.0f) anim.cooldownA = 0.0f;
        g_Client.Var(0x1675700) = static_cast<int32_t>(anim.cooldownA); // Crt_ftol (truncation)
    }
    // --- Unconditional simple countdown (0x5718F0..0x571919) ---------------------------
    if (anim.cooldownB > 0.0f) {
        anim.cooldownB -= dt;
        if (anim.cooldownB < 0.0f) anim.cooldownB = 0.0f;
    }

    // --- Contact detection: delegated to ActionFsm::UpdateContactDetection (0x571926..
    // 0x571D2A, ALREADY WRITTEN in Game/ActionStateMachine.cpp) — builds a transient
    // ActionFsm from `anim`, runs it, copies the result back. -------------------------
    {
        ActionFsm fsm;
        fsm.actor              = actor;
        fsm.state               = static_cast<CharActionState>(anim.state);
        fsm.animFrame           = anim.animFrame;
        fsm.isLocalSimulation   = isLocalSimulation;
        fsm.hitCheckActive      = anim.hitCheckActive;
        fsm.hitFired            = anim.hitFired;
        fsm.hitUsesSkillTable   = anim.hitUsesSkillTable;
        fsm.altWeaponSet        = anim.altWeaponSet;
        fsm.weaponAnimSlot      = anim.weaponAnimSlot;
        fsm.lastSkillEventId    = anim.lastSkillEventId;
        fsm.actionKind          = anim.actionKind;
        fsm.actionSubKind       = anim.actionSubKind;
        fsm.modelIndex          = anim.modelIndex;
        fsm.weaponClass         = anim.weaponClass;
        fsm.isSelf              = isSelf;
        fsm.pendingCastInterrupt = pendingCastInterrupt;
        fsm.guardSubstate       = anim.guardSubstate;
        fsm.guardKeyHeld        = anim.guardKeyHeld;

        fsm.UpdateContactDetection(world, hitOracle);
        // --- Cast interruption (0x57275A, ALREADY WRITTEN) — called right after, matching the
        // binary (block 0x57275A sits AFTER the contact block, BEFORE the switch). --------
        fsm.ApplyPendingCastInterrupt();

        anim.state             = ToRaw(fsm.state);
        anim.animFrame          = fsm.animFrame;
        anim.hitCheckActive     = fsm.hitCheckActive;
        anim.hitFired           = fsm.hitFired;
        anim.lastSkillEventId   = fsm.lastSkillEventId;
        anim.guardSubstate      = fsm.guardSubstate;

        outResult.contactFiredThisTick = fsm.contactFiredThisTick;
        outResult.lastAction           = fsm.lastAction;
        outResult.pendingAoECast       = fsm.pendingAoECast;
        outResult.pendingProjectile    = fsm.pendingProjectile;
    }

    // --- Generic 10s latch (entity+748/752, 0x571DD2) -----------------------------------
    if (anim.genericLatch10s && (world.gameTimeSec - anim.genericLatch10sStamp) > 10.0f)
        anim.genericLatch10s = false;

    // --- 8 secondary FX timers (0x571DE4..0x572425) --------------------------------------
    for (int i = 0; i < 8; ++i)
        TickCharFxSlot(anim.fxTimers[static_cast<size_t>(i)], kCharFxRows[static_cast<size_t>(i)],
                        dt, modelOracle, anim.weaponAnimSlot, anim.altWeaponSet,
                        anim.modelIndex, anim.modelVariant);

    // --- Shared "==1" pair unk_B68954 (0x572331/0x572389) --------------------------------
    auto TickSimpleFx = [&](FxTimerSlot& slot, uint32_t table) {
        if (!slot.active) return;
        slot.frame += dt * 30.0f;
        if (slot.frame >= MorphDuration(modelOracle, table)) slot.active = false;
    };
    TickSimpleFx(anim.fx222, 0xB68954u);
    TickSimpleFx(anim.fx224, 0xB68954u);

    // --- Special aura (0x57243C..0x5724B8): latch armed when fxAuraTriggerField==2160,
    // disarmed otherwise. Attach attempted ONLY ONCE per arming (remembered via the
    // latch, matching the binary's `if (!*(this+221))`). -------------------------------
    if (anim.fxAuraTriggerField == 2160) {
        if (!anim.fxAuraAttachedLatch) {
            anim.fxAuraAttachedLatch = true;
            if (host.HasFreeAuraSlot && host.HasFreeAuraSlot() && host.AttachSpecialAura)
                host.AttachSpecialAura();
        }
    } else if (anim.fxAuraAttachedLatch) {
        anim.fxAuraAttachedLatch = false;
    }

    // --- "Infinite loop" timer (0x5724CC..0x572512): frame reset to 0 WITHOUT ever
    // clearing fxLoopMode (faithful — flag set/cleared by another system). --------------
    if (anim.fxLoopMode == 1) {
        anim.fxLoopFrame += dt * 30.0f;
        if (anim.fxLoopFrame >= MorphDuration(modelOracle, 0xB64800u))
            anim.fxLoopFrame = 0.0f;
    }

    // --- Smoothed facial rotation (0x572531..0x572649), 540°/s, wraps at 360, BYTE-EXACT
    // (no asset dependency): A=facingCurrentDeg (MUTATED), B=facingTargetDeg (read only). -
    {
        float& A = anim.facingCurrentDeg;
        const float B = anim.facingTargetDeg;
        constexpr float kRate = 540.0f;
        if (A >= B) {
            if (A > B) {
                if (A + 360.0f - B <= B - A) {
                    A -= dt * kRate;
                    if (A < 0.0f) {
                        A += 360.0f;
                        if (B > A) A = B;
                    }
                } else {
                    A += dt * kRate;
                    if (B < A) A = B;
                }
            }
        } else if (B + 360.0f - A <= A - B) {
            A += dt * kRate;
            if (A >= 360.0f) {
                A -= 360.0f;
                if (B < A) A = B;
            }
        } else {
            A -= dt * kRate;
            if (B > A) A = B;
        }
    }

    // --- Terminal switch (0x5727BF): 81 Char_*/Combat_TickAttackState cases, each
    // asset-driven (anim duration = motion data, out of scope) -> a single opaque hook,
    // called with the CURRENT state (post cast-interruption above, faithful to the
    // binary's order: `mov edx, [ecx+0F4h]` @0x5727A9 re-reads *(this+244) right before the
    // switch). "55 handlers" was WRONG: 96 cases − 15 `default` values = 81 (cf. AnimationTick.h).
    // Partial router ready to plug in here: Char_DispatchStateTick (§7). -----------------------
    if (stateHandler) stateHandler(static_cast<CharActionState>(anim.state), dt);

    // --- Pending guild mark (0x572F4E) ----------------------------------------------------
    if (anim.hasPendingGuildMark && host.RegisterGuildMark) host.RegisterGuildMark();

    // --- AutoPlay stop request (0x572F6E..0x572F8D): shared globals, only for
    // the local entity AND only from state Move(1). --------------------------------------
    if (isLocalSimulation && host.GetPendingStopRequest && host.GetPendingStopRequest()) {
        if (anim.state == ToRaw(CharActionState::Move)) {
            if (host.ClearPendingStopRequest) host.ClearPendingStopRequest();
            if (host.SendAutoPlayStopAck) host.SendAutoPlayStopAck();
        }
    }
}

} // namespace ts2::game

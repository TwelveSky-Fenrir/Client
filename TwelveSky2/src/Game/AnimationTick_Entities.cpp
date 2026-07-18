// Game/AnimationTick_Entities.cpp — implementation. See Game/AnimationTick.h for the full doc
// (original EA, scope, hosts/oracles). Decompilation source: idaTs2 (Hex-Rays).
// Split family: shared morph-timer helpers live in Game/AnimationTick_Internal.h;
// Player_UpdateLocalAnim/Char_UpdateAnimationFrame (§1/§2) live in Game/AnimationTick.cpp;
// Camera_UpdateCollision/MapColl_UpdateObjectAnim (§3/§4) live in Game/AnimationTick_World.cpp.
// This file covers §5/§6/§7: monster motion FSM, zone-decor NPC animation, and the partial
// player terminal-switch router.
#include "Game/AnimationTick.h"
#include "Game/ClientRuntime.h"   // g_Client.Var/VarF (long-tail globals escape hatch)
#include "Game/MapWarp.h"         // BeginWarpToFactionTown, WarpAddr::SelfMorphNpcId
#include "Game/CameraWarpTick.h"  // Cam_SetLookAt (already written, reused as-is)
#include "Game/EntityLifecycleTick.h" // g_MonsterTickExt (motionState/animFrame/attackWindupMode) — §5
#include "Game/ExtraDatabases.h"      // NpcDefRecord::id (decor NPC kind, ZoneNpc_OnDialogueOpen) — §6
                                       // (the NPC pool itself = g_World.npcRenderEntries, via GameState.h)
#include <cmath>
#include <cstring>

namespace ts2::game {

// 5. MONSTER animation FSM — Char_Update 0x581E10, switch @0x5822D3 (9 handlers)
//    See Game/AnimationTick.h §5 for the full state -> EA -> semantics table.
namespace {

constexpr float kFrameRate30 = 30.0f; // `frame += a3 * 30.0` — shared by all 9 handlers

// Values of the switch @0x5822D3 (= valid set of Model_GetNpcMotionSlot 0x4E5960 @0x4e59a4).
enum : int32_t {
    kMonsterMotionToIdle    = 0,
    kMonsterMotionLoop      = 1,
    kMonsterMotionMoveA     = 3,
    kMonsterMotionMoveB     = 4,
    kMonsterMotionAttackA   = 5,
    kMonsterMotionAttackB   = 7,
    kMonsterMotionHit       = 8,
    kMonsterMotionKnockback = 0xC,
    kMonsterMotionDeath     = 0x13,
};

// monsterDefId = MonsterEntity::body[0] (u32 LE) — SAME read as
// Game/EntityManager.cpp::ResolveMobDef and Scene/WorldRenderer.cpp::Render (raw id, no -1).
uint32_t MonsterDefIdOf(const MonsterEntity& m) {
    uint32_t defId = 0;
    std::memcpy(&defId, m.body.data(), sizeof(defId));
    return defId;
}

// Wiring latch — cf. Monster_MotionTickIsWired() in Game/AnimationTick.h (non-regression
// guard, NOT a binary behavior).
bool g_MonsterMotionTickRan = false;

} // namespace (monster FSM helpers)

bool Monster_MotionTickIsWired() { return g_MonsterMotionTickRan; }

void Monster_DispatchMotionTick(GameWorld& world, int monsterIndex, float dt,
                                 const IMotionFrameCountOracle* oracle,
                                 const MonsterMotionTickHost& host) {
    if (monsterIndex < 0 || static_cast<size_t>(monsterIndex) >= world.monsters.size()) return;
    const MonsterEntity& mon = world.monsters[static_cast<size_t>(monsterIndex)];
    if (!mon.active) return; // guard `if (*(_DWORD*)this)` of Char_Update @0x581e1c

    // g_MonsterTickExt is sized by UpdateMonster (EnsureMonsterExtCapacity) BEFORE calling
    // host.DispatchMotionTick; defensive guard in case another caller invokes it.
    if (static_cast<size_t>(monsterIndex) >= g_MonsterTickExt.size()) return;
    MonsterTickExt& ext = g_MonsterTickExt[static_cast<size_t>(monsterIndex)];

    // The switch @0x5822d3 has a `default: return`: a state outside the 9 cases does NOT tick
    // AT ALL (not even the frame advance) — faithful.
    const int32_t state = ext.motionState;
    switch (state) {
        case kMonsterMotionToIdle:  case kMonsterMotionLoop:
        case kMonsterMotionMoveA:   case kMonsterMotionMoveB:
        case kMonsterMotionAttackA: case kMonsterMotionAttackB:
        case kMonsterMotionHit:     case kMonsterMotionKnockback:
        case kMonsterMotionDeath:
            break;
        default:
            return; // @0x5822d3 default
    }
    g_MonsterMotionTickRan = true; // the tick actually ran (cf. Monster_MotionTickIsWired)

    // frameCount = Model_GetMotionFrameCount 0x4E5A70(kindIdx, animType) — SAME slot as the
    // draw (Char_Draw @0x580770), hence identical to palette.frameCount. All 9 handlers
    // compute it UP FRONT, before the frame advance: reproduced as-is.
    // Null oracle / count<=0 -> unknown duration: the cursor advances but we emit NO
    // bound or transition (we don't fabricate a duration). Cf. Game/AnimationTick.h.
    const int frameCount = oracle ? oracle->GetMonsterMotionFrameCount(MonsterDefIdOf(mon), state) : 0;
    const bool haveCount = (frameCount > 0);
    const float countF   = static_cast<float>(frameCount);

    // Advance shared by all 9 handlers: `*(this+28) = a3 * 30.0 + *(this+28)`
    // (@0x582d7f / @0x582def / @0x582e5f / @0x582f2f / @0x582fff / @0x58307f / @0x5830ff /
    //  @0x58316f / @0x58331f) — slot+28 = MonsterTickExt::animFrame.
    ext.animFrame += dt * kFrameRate30;

    // "Back to Loop(1)" transition: state=1 + frame=0. Pattern shared by ToIdle/AttackA/
    // AttackB/Hit and by exiting Move.
    auto backToLoop = [&ext]() {
        ext.motionState = kMonsterMotionLoop; // @0x582d99 / @0x583019 / @0x583099 / @0x583119
        ext.animFrame   = 0.0f;               // @0x582da5 / @0x583025 / @0x5830a5 / @0x583125
    };

    switch (state) {
        // --- 0: Char_MotionTick_ToIdle 0x582D40 --------------------------------------------
        case kMonsterMotionToIdle:
            if (haveCount && ext.animFrame >= countF) backToLoop(); // @0x582d92
            break;

        // --- 1: Char_MotionTick_Loop 0x582DB0 — wraps by SUBTRACTION (never a modulo) --------
        case kMonsterMotionLoop:
            if (haveCount && ext.animFrame >= countF) ext.animFrame -= countF; // @0x582e02/@0x582e10
            break;

        // --- 3/4: Char_MotionTick_MoveA 0x582E20 / MoveB 0x582EF0 --------------------------
        // Same wrap as Loop (@0x582e72/@0x582e80; @0x582f42/@0x582f50) THEN
        // MapColl_StepTowardTarget(&dword_14A88E4, this+32, this+44, speed, a3, &arrived) with
        // speed = MONSTER_INFO+384 (MoveA @0x582e9b) / +388 (MoveB @0x582f6b).
        //   failure (result==0) -> state=1, frame=0   (@0x582ebd/@0x582ec9; @0x582f8d/@0x582f99)
        //   arrival (arrived!=0) -> state=1, frame=0  (@0x582ed7/@0x582ee3; @0x582fa7/@0x582fb3)
        case kMonsterMotionMoveA:
        case kMonsterMotionMoveB: {
            if (haveCount && ext.animFrame >= countF) ext.animFrame -= countF;
            // Null hook -> skip the displacement AND the transition. Do NOT treat "no
            // hook" as "result==0 -> state=1": that would exit Move on the very 1st frame and
            // the monster would never walk (cf. Game/AnimationTick.h, degradation note).
            if (!host.StepTowardTarget) break;
            bool arrived = false;
            const bool stepped = host.StepTowardTarget(monsterIndex, state == kMonsterMotionMoveB,
                                                        dt, arrived);
            if (!stepped || arrived) backToLoop();
            break;
        }

        // --- 5/7: Char_MotionTick_AttackA 0x582FC0 / AttackB 0x583040 -----------------------
        // Identical to ToIdle plus clearing the windup slot+108 (@0x58302b / @0x5830ab).
        case kMonsterMotionAttackA:
        case kMonsterMotionAttackB:
            if (haveCount && ext.animFrame >= countF) { // @0x583012 / @0x583092
                backToLoop();
                ext.attackWindupMode = 0; // slot+108 = MonsterTickExt::attackWindupMode
            }
            break;

        // --- 8: Char_MotionTick_Hit 0x5830C0 ------------------------------------------------
        case kMonsterMotionHit:
            if (haveCount && ext.animFrame >= countF) backToLoop(); // @0x583112
            break;

        // --- 0xC: Char_MotionTick_Knockback 0x583130 — PARTIAL, cf. TODO -------------------
        // Freezes on the last frame (@0x583271/@0x583284), reproduced faithfully.
        // TODO [anchor Char_MotionTick_Knockback 0x583130 @0x583189..0x5832d0]: the knockback
        // PHYSICS block is NOT ported — -100*dt deceleration of scalar slot+204
        // (@0x5831a4), position integration along direction slot+44/+52 (@0x5831e3/
        // @0x583207), clamp against MapColl_GetGroundHeight 0x697130 (@0x58323d), then at the
        // end of the anim: slot+200=0 + timestamp slot+208 (@0x583296/@0x5832a9) and, past
        // 3s, Char_RespawnAfterKnockback 0x580550 (@0x5832d0). Reason: the carrier fields
        // (slot+76, +200, +204, +208) DO NOT EXIST in MonsterTickExt, and
        // Game/EntityLifecycleTick.h is not owned by this front — adding them unilaterally
        // would conflict with its owner. Nothing is fabricated: only the frame advance/freeze
        // is emitted. Note that the "stale > 3s" branch already has a wired equivalent on the
        // caller side (Scene_InGameUpdate @0x52cab5 -> host.RespawnMonsterAfterKnockback,
        // Scene/SceneManager.cpp) — the real functional loss is limited to the knockback
        // displacement itself.
        case kMonsterMotionKnockback:
            if (haveCount && ext.animFrame >= countF) ext.animFrame = countF - 1.0f; // @0x583284
            break;

        // --- 0x13: Char_MotionTick_Death 0x5832E0 — freezes on the last frame ---------------
        case kMonsterMotionDeath:
            if (haveCount && ext.animFrame >= countF) ext.animFrame = countF - 1.0f; // @0x583332/@0x583345
            break;

        default:
            break; // unreachable (filtered above)
    }
}

// 6. Zone-decor NPC animation — Npc_RenderSlotTick 0x5803A0 / _Loop 0x580400 / _Once 0x5804A0
//    See Game/AnimationTick.h §6. W7 ADAPTATION: ticks DIRECTLY the NATIVE fields of the
//    single pool g_World.npcRenderEntries (NpcRenderEntry: mode+12 / frameAcc+16 / angle+44 /
//    angleBase+80) -- these ARE the offsets of the original g_NpcRenderArray slot, no more
//    parallel vector (rationale obsolete since the W7 merge, cf. header AnimationTick.h §6).
namespace {

// Math_Dist3D 0x53FAA0: sqrt(dx^2+dy^2+dz^2). Local copy — same convention as
// Game/AutoTargetCombatGate.cpp / ComboPickupTick.cpp / GroundAuraWorldObjectTick.cpp, each of
// which carries its own (standalone module, no shared math header to date).
float Dist3D(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Math_AngleBetween2D 0x53FB20: heading (deg 0..360) from point (a1,a2) to (a3,a4). Local copy
// (see note above) — identical transcription to Game/GroundAuraWorldObjectTick.cpp's.
float AngleBetween2D(float a1, float a2, float a3, float a4) {
    if (a3 == a1 && a4 == a2) return 0.0f;                       // @0x53fb45
    float dx = a3 - a1, dz = a4 - a2;                            // v12,v13
    const float len = std::sqrt(dz * dz + dx * dx);              // @0x53fb82
    if (len > 0.0f) { dx /= len; dz /= len; }                    // @0x53fb99
    const float chordZ = dz - 1.0f;                              // v14
    const float chord  = std::sqrt(chordZ * chordZ + dx * dx);   // @0x53fbdb
    float half = chord * 0.5f;                                   // @0x53fbf8 (v8/2)
    if (half > 1.0f) half = 1.0f;                                // @0x53fbf8 (asin(1.0) branch)
    float ang = std::asin(half) * 2.0f;                          // @0x53fc30/@0x53fc44 (rad)
    if (a3 < a1) ang = 6.283185482025146f - ang;                 // @0x53fc54
    const float deg = ang * 57.2957763671875f + 180.0f;          // @0x53fc71
    if (deg >= 360.0f) return deg - 360.0f;                      // @0x53fc82
    return deg;                                                  // @0x53fc93
}

constexpr float kZoneNpcBaselineResetDistance = 400.0f; // @0x580483

// Wiring latch — cf. ZoneNpc_AnimTickIsWired() in Game/AnimationTick.h (non-regression
// guard, NOT a binary behavior).
bool g_ZoneNpcAnimTickRan = false;

// Npc_RenderSlotTick_Loop 0x580400 — operates on the NATIVE slot fields (NpcRenderEntry).
void ZoneNpcTickLoop(NpcRenderEntry& e, int index, float dt,
                     const IMotionFrameCountOracle* oracle, const PlayerEntity& self) {
    // frameCount = Model_GetWeaponEffectFrameCount 0x4E5A40 @0x580429 — SAME slot as the draw
    // (Npc_DrawMesh @0x57ffa0), hence identical to palette.frameCount. Computed UP FRONT, as here.
    // The original 2nd argument is *(this+3) = mode (dual animId/dispatch role, cf. 0x580429).
    const int frameCount = oracle ? oracle->GetZoneNpcMotionFrameCount(index, e.mode) : 0;

    e.frameAcc += dt * kFrameRate30; // @0x58043e: *((float*)this+4) = a3*30.0 + *((float*)this+4)
    if (frameCount > 0 && e.frameAcc >= static_cast<float>(frameCount))
        e.frameAcc -= static_cast<float>(frameCount); // @0x580451/@0x58045f: wrap by SUBTRACTION

    // @0x580483: Math_Dist3D(this+5 /*NPC pos*/, flt_1687330 /*local player*/) > 400.0
    //          -> *(this+11) = *(this+20), i.e. angle(+44) = angleBase(+80). The loader writes
    // the same value to both offsets (@0x557a42 / @0x557a62): the NPC resumes its original
    // heading once you move away.
    if (Dist3D(e.x, e.y, e.z, self.x, self.y, self.z) > kZoneNpcBaselineResetDistance)
        e.angle = e.angleBase; // @0x58048e
}

// Npc_RenderSlotTick_Once 0x5804A0.
void ZoneNpcTickOnce(NpcRenderEntry& e, int index, float dt,
                     const IMotionFrameCountOracle* oracle) {
    const int frameCount = oracle ? oracle->GetZoneNpcMotionFrameCount(index, e.mode) : 0; // @0x5804c9
    e.frameAcc += dt * kFrameRate30; // @0x5804de
    if (frameCount > 0 && e.frameAcc >= static_cast<float>(frameCount)) { // @0x5804f1
        e.mode     = 0;    // @0x5804f8: *(this+3) = 0.0 -> back to Loop mode
        e.frameAcc = 0.0f; // @0x580504
    }
}

} // namespace (zone-decor NPC helpers)

bool ZoneNpc_AnimTickIsWired() { return g_ZoneNpcAnimTickRan; }

void ZoneNpc_TickAnim(float dt, const IMotionFrameCountOracle* oracle) {
    g_ZoneNpcAnimTickRan = true; // the tick actually ran (cf. ZoneNpc_AnimTickIsWired)
    const PlayerEntity&            self = g_World.Self(); // flt_1687330 = local player position
    std::vector<NpcRenderEntry>&   pool = g_World.npcRenderEntries; // single pool (W7)

    for (size_t i = 0; i < pool.size(); ++i) {
        NpcRenderEntry& e = pool[i];
        // Guard `if (*((_DWORD*)this + 1))` (slot+4 = active, @0x5803ac): under W7 the pool
        // carries 100 FIXED slots, some of which are inactive HOLES (def==nullptr, pos 0,0,0)
        // -> explicitly skipped (the binary self-guards at the top of Npc_RenderSlotTick 0x5803a0).
        if (!e.active) continue;
        // Dispatch @0x5803ba: mode 0 -> _Loop, 1 -> _Once, any other value -> no-op (faithful).
        if (e.mode == 0)
            ZoneNpcTickLoop(e, static_cast<int>(i), dt, oracle, self); // @0x5803d9
        else if (e.mode == 1)
            ZoneNpcTickOnce(e, static_cast<int>(i), dt, oracle);       // @0x5803ee
    }
}

// UI_NpcWin_Open 0x5DB530, dialogue queue @0x5dc019..0x5dc0a8 — mutates the NATIVE slot of the
// single pool.
void ZoneNpc_OnDialogueOpen(int zoneNpcIndex, float playerX, float playerZ) {
    std::vector<NpcRenderEntry>& pool = g_World.npcRenderEntries;
    if (zoneNpcIndex < 0 || static_cast<size_t>(zoneNpcIndex) >= pool.size()) return;

    NpcRenderEntry& e = pool[static_cast<size_t>(zoneNpcIndex)];
    if (!e.active) return; // empty slot: only open a dialogue on a real NPC (W7 guard)

    if (e.mode == 1) return; // @0x5dc019/@0x5dc01d: already in progress -> nothing (idempotent)

    e.mode     = 1;    // @0x5dc026: mov [eax+0Ch], 1  -> Once mode ("greeting")
    e.frameAcc = 0.0f; // @0x5dc032: fldz / fstp [ecx+10h]

    // @0x5dc038..0x5dc06b: 5 kinds do NOT turn to face the player. The binary tests
    // `*(*a2)` = 1st dword of the def record pointed to by the slot = NpcDefRecord::id (+0, cf.
    // Game/ExtraDatabases.h:45) against {63,113,213,313,7}. def is non-null for an active slot
    // (loader guard); defensive fallback of 0 (kind 0 is in none of the 5 -> turn performed).
    const uint32_t k = e.def ? e.def->id : 0u;
    const bool skipFacing = (k == 63 || k == 113 || k == 213 || k == 313 || k == 7);
    if (!skipFacing) {
        // @0x5dc09a/@0x5dc0a2: Math_AngleBetween2D(slot+20 /*x*/, slot+28 /*z*/,
        //                        flt_1687330 /*player.x*/, flt_1687338 /*player.z*/) -> slot+44 (angle).
        e.angle = AngleBetween2D(e.x, e.z, playerX, playerZ);
    }
    // TODO [anchor 0x5dc0a8 -> Fx_MeleeSwingUpdate 0x57FE90]: positional sound played here by
    // the binary (out of scope for this front's audio/FX) — not reproduced.
}

// 7. PARTIAL router for the terminal PLAYER switch — Char_UpdateAnimationFrame 0x571880,
//    switch @0x5727BF. See Game/AnimationTick.h §7 (coverage, proofs, degradation).
namespace {

// "Unknown" duration: the cursor advances but never reaches the bound -> no transition
// is emitted. SAME idiom as MorphDuration() at the top of AnimationTick.cpp (null oracle -> 1e9f).
constexpr float kUnknownMotionDuration = 1.0e9f;

// Wiring latch — cf. Char_StateTickIsWired() in Game/AnimationTick.h (non-regression
// guard, NOT a binary behavior).
bool g_CharStateTickRan = false;

} // namespace (player FSM router helpers)

bool Char_StateTickIsWired() { return g_CharStateTickRan; }

void Char_DispatchStateTick(CharAnimState& anim, CharActionState state, float dt,
                             bool isLocalSimulation, const CharStateTickHost& host) {
    // Entry filter: only the 6 cases whose primitive is PORTED are routed. Case labels =
    // IDA's own (`jumptable 005727BF case N`); each of the 6 original functions has
    // exactly 1 caller in the whole binary (xrefs_to -> xref_count == 1), and it's this
    // switch -> bijective mapping.
    switch (state) {
        case CharActionState::AnimEndToIdle_5761A0: // case 4  @0x572834 -> 0x5761A0
        case CharActionState::CastSlot0:            // case 5  @0x57284C -> 0x5762F0
        case CharActionState::CastSlot1:            // case 6  @0x572864 -> 0x5764D0
        case CharActionState::CastSlot2:            // case 7  @0x57287C -> 0x5766B0
        case CharActionState::GuardBegin:           // case 91 @0x572EEE -> 0x57F260
        case CharActionState::GuardLoop:            // case 92 @0x572F03 -> 0x57F410
            break;
        default:
            // TODO [anchor 0x5727BF, 75 cases remaining out of 81]: EXPLICIT no-op. Covers TWO
            // distinct situations, not to be confused:
            //  (a) the 15 values of the original `default` (8; 24-29; 47; 53; 59; 77-80;
            //      84, cf. @0x5727B6) -> FAITHFUL no-op, nothing to do;
            //  (b) the 75 LIVE cases whose primitive isn't ported (Char_AnimTick_5746E0
            //      0x5746E0 case 0, Char_TickMoveState 0x574830 case 1, Combat_TickAttackState
            //      0x574BD0 case 2, Char_TickRunState 0x578D70 case 32, Char_TickDeathRespawn
            //      0x576CB0 case 12, ...) -> the anim for these states stays FROZEN, exactly as
            //      before this router. No regression, but CTF-01 is NOT closed either way.
            return;
    }
    g_CharStateTickRan = true; // the router actually ran (cf. Char_StateTickIsWired)

    // Transient ActionFsm — SAME pattern as Char_UpdateAnimationFrame above (§2): the
    // 4 primitives only touch state/animFrame/guardSubstate/hitCheckActive (+ actor.facing,
    // mirroring the same memory word entity+244) and only READ guardKeyHeld/isLocalSimulation.
    ActionFsm fsm;
    fsm.state             = state;
    fsm.animFrame         = anim.animFrame;
    fsm.hitCheckActive    = anim.hitCheckActive;
    fsm.guardSubstate     = anim.guardSubstate;
    fsm.guardKeyHeld      = anim.guardKeyHeld;
    fsm.isLocalSimulation = isLocalSimulation;
    fsm.actor.facing      = ToRaw(state); // entity+244 == state (cf. ActionStateMachine.h)

    // Anim duration: PcModel_ResolveSlotAndApply 0x4E5A00, computed UP FRONT by the 4 handlers,
    // BEFORE the frame advance (@0x5761AC / @0x5762FC / @0x57F26C / @0x57F41C) — order reproduced.
    // count<=0 (slot unresolved) == hook absent == unknown duration: we fabricate nothing.
    const int rawCount = host.GetMotionFrameCount ? host.GetMotionFrameCount(anim, isLocalSimulation) : 0;
    const float duration = (rawCount > 0) ? static_cast<float>(rawCount) : kUnknownMotionDuration;

    switch (state) {
        // --- case 4: Char_AnimEndToIdle_5761A0 0x5761A0 -> back to Move(1) -----------------
        // `frame += dt*30` @0x576268; `if (frame >= v4)` @0x576281; `*(this+244) = 1`
        // @0x5762A4; `*(this+248) = 0.0` @0x5762B3. Side effects NOT ported (+240, +296,
        // Op16): cf. the detailed TODO on TickTimedState (Game/ActionStateMachine.cpp).
        case CharActionState::AnimEndToIdle_5761A0:
            fsm.TickTimedState(dt, duration, CharActionState::Move);
            break;

        // --- cases 5/6/7: Char_CastAnimTick_5762F0 / _5764D0 / _5766B0 ----------------------
        // The three bodies are STRICTLY identical (compiled copies) -> a single primitive,
        // RE-PROVEN mapping (cf. ActionStateMachine.h::TickCastState). Without the rate hook,
        // the cast cursor does NOT ADVANCE (the rate is a multiplier, not a bound): freeze =
        // current behavior, no regression, no value fabricated.
        case CharActionState::CastSlot0:
        case CharActionState::CastSlot1:
        case CharActionState::CastSlot2: {
            double ratePct = 0.0;
            const bool withinBounds =
                host.GetCastRateWithinBounds && host.GetCastRateWithinBounds(anim, ratePct);
            fsm.TickCastState(dt, ratePct, withinBounds, duration);
            break;
        }

        // --- case 91: Char_ActionTick_GuardBegin 0x57F260 -----------------------------------
        case CharActionState::GuardBegin:
            fsm.TickGuardBegin(dt, duration);
            break;

        // --- case 92: Char_ActionTick_GuardLoop 0x57F410 ------------------------------------
        case CharActionState::GuardLoop:
            fsm.TickGuardLoop(dt, duration);
            break;

        default:
            break; // unreachable (filtered above)
    }

    anim.state          = ToRaw(fsm.state);
    anim.animFrame      = fsm.animFrame;
    anim.hitCheckActive = fsm.hitCheckActive; // only TickCastState mutates it (@0x57644F)
    anim.guardSubstate  = fsm.guardSubstate;
}

} // namespace ts2::game

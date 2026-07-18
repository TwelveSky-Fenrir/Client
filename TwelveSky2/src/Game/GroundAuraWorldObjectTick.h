// Game/GroundAuraWorldObjectTick.h — GROUND EFFECTS / AURAS (PROJECTILES) / ZONE OBJECTS
// SYSTEM: C++ rewrite of the 3 hook groups left as TODO by Game/InGameTickFlow.h
// (steps 7, 10, 11 of RunMainTick, cf. Game/InGameTickFlow.cpp). Source of truth =
// Hex-Rays decompilation via idaTs2 (imagebase 0x400000) + Docs/TS2_FX_CATALOG.md
// (catalog of 44 Fx_* functions). Self-contained Game/*.h/.cpp module (dedicated
// "ground effects/auras/zone objects" mission): does NOT edit Scene/SceneManager.*
// or App/App.* — the consolidation agent wires the InGameTickFlowHost hooks below
// onto the existing code (same policy as Game/EntityLifecycleTick.h).
//
// ===========================================================================================
// 1. Fx_MeleeSwingTick 0x5803A0 (+ Fx_MeleeSwingTick_Loop 0x580400 / _Once 0x5804A0)
// ex-VeryOldClient: EFFECT_OBJECT::Update (FSM mObjType 1..14) — CONFIRMED (Docs/TS2_FX_ROSETTA.md
// §1). EU build has NO EFFECT_OBJECT[1000] mega-struct (CONFLICT 3-A, IDA wins): swing timers
// are ported here; attach slots (Fx_Attach*) and the projectile SoA pool are separate.
// ===========================================================================================
// Step 7 of Game/InGameTickFlow.h (host.TickGroundItemEffect).
//
// *** SUPERSEDED (steps 5-8 audit, 2026-07-14): this function's IDA identity was corrected ***
// Original hypothesis (now WRONG): despite the IDA name ("melee weapon swing trail", cf.
// Docs/TS2_FX_CATALOG.md §2), the function appeared to operate on dword_1764D18 as a GROUND
// ITEM array (game::GroundItem), based on an early Docs/TS2_PROTOCOL_SPEC.md note.
// CORRECTED identity (fresh re-decompilation, updated IDB naming): sub_5803A0/580400/5804A0
// are now Npc_RenderSlotTick / _Loop / _Once, and dword_1764D14 is g_NpcRenderArray — an NPC
// render/targeting pool, NOT ground items, confirmed by 3 independent cross-checks:
//   - Scene_PickNpcAtScreen 0x541280 (screen raycast -> g_NpcRenderArray index via
//     Scene_RayHitNpcBox 0x541680, explicit "pick NPC at screen" name).
//   - Item_PickupTarget 0x539EC0 (named "pickup ground item" but whose ONLY observable
//     effect is `UI_NpcWin_Open(&g_NpcRenderArray[22*idx])` — opens an NPC dialog window,
//     not a pickup UI).
//   - Pkt_EnterWorld 0x464160: the slot destructor on this array at zone reset is
//     maybe_cGameData_ListField1ItemDtor 0x57FE70 (same naming family as neighboring
//     NPC/monster destructors in the same function).
// CONCLUSION: dword_1764D14/g_NpcRenderArray is an NPC array (3D click zone + dialog
// window), NOT ground items — contradicting Game/GameState.h's GroundItem classification
// (out of scope for this mission, not modified here). The reproduced MECHANISM
// (loop/once dispatch by ext.mode, frame advance, 400 u distance culling) is unaffected by
// this semantic correction — no code change needed in GroundAuraWorldObjectTick.cpp, only
// this documentation fix.
//
// Record layout (dword indices from the original `this` pointer = &record + 4 bytes,
// so `this+N` = record byte 4+4N):
//   this+0  (record+4)  : pointer, dereferenced then +1324 -1 before Model_GetWeaponEffectFrameCount
//                          — OUT OF SCOPE (asset/model data), never resolved here.
//   this+1  (record+8)  : active flag (head guard, redundant with GroundItem::active).
//   this+3  (record+16) : mode (0=Loop, 1=Once, other=no-op) — REUSED as-is as the
//                          2nd ("variant") parameter of Model_GetWeaponEffectFrameCount.
//   this+4  (record+20) : current frame (float, advances dt*30/s).
//   this+5..7 (record+24..32) : xyz position — mapped onto GroundItem::x/y/z (same
//                          convention as the other entity arrays, cf. GameState.h).
//   this+11 (record+48) : destination field (semantics NOT determined).
//   this+20 (record+84) : source field, copied into this+11 when the distance to the
//                          local player exceeds 400 u (semantics NOT determined — likely a
//                          frozen LOD/culling "snapshot", not resolved by this mission;
//                          reproduced MECHANICALLY, not invented).
//
// GroundItem (Game/GameState.h) is a deliberately lean, CLEAN model that does NOT carry
// these per-frame tick fields — like MonsterTickExt/NpcTickExt (Game/EntityLifecycleTick.h),
// they live in a parallel EXTENSION struct (GroundItemTickExt) indexed like
// g_World.groundItems, NOT in GameState.h (shared base file, out of scope for this mission).
//
// ===========================================================================================
// 2. Attack projectile pool (g_FxAuraCount 0x168722C / dword_17D06F4)
// ===========================================================================================
// Step 10 of Game/InGameTickFlow.h (host.GetFxAuraCount / IsFxAuraActive /
// UpdateHomingProjectile). Identification CONFIRMED ("aura/world-objects" mission,
// 2026-07-14, cf. comments in Game/AnimationTick.h/InGameTickFlow.h/MiscManagers.cpp):
// g_FxAuraCount is NOT a buff/aura pool — it is the CAPACITY (1000, cf.
// cGameData_InitPools @0x5575DC) of the UNIFIED FX SoA pool dword_17D06F4 (stride 64 dw =
// 256 bytes/slot). A slot carries either an attack PROJECTILE (FSM states 1..4/12..13) or
// an attach effect (states 5..11/14, render pool #2, NOT OWNED). This module owns the
// PROJECTILE subset allocated by Fx_SpawnAttackProjectile(Alt) 0x582530/0x582A10
// (states 3→4) and the shared FSM tick Fx_HomingProjectileUpdate 0x5862D0 for THESE states.
//
// IMPLEMENTED ("FX pool #1" mission — replaces the previous empty wiring): the slot layout
// (64 dw, anchored offset-by-offset in the .cpp), allocation (spawn), and the homing tick for
// states 3 (parabolic arc weaponId==113 / direct homing Alt / homing toward a live entity) and
// 4 (impact anim) are ported FAITHFULLY (trajectory, arrival, state transition, Op18 report
// payload). Weapon→motion tables (Anim_MapWeaponToMotion1/2/3 0x5475F0/547970/547CF0) and
// math helpers (Math_MoveProjectileArc 0x588640, Math_AngleBetween2D 0x53FB20, Math_Dist3D
// 0x53FAA0) ported inline. Effects OUT OF SCOPE (impact anim via
// ModelObj_GetSubObjectCount, Op18 network report, positional sound, elemental immunity via
// unmodeled g_LocalPlayerSheet) are deferred via g_FxProjectileHost (null callbacks =
// safe degradation: the projectile flies and transitions faithfully, only side effects
// are deferred). States 1/2/12/13 (projectiles from OTHER non-owned spawns) and 5..11/14
// (render pool #2) are ignored (faithful no-op `default:` @0x5862D0).
//
// ===========================================================================================
// 3. Zone objects / resource nodes (dword_1687230 / dword_180EEF4)
// ===========================================================================================
// Step 11 of Game/InGameTickFlow.h (host.GetWorldObjectCount / IsWorldObjectActive /
// TickWorldObject). Container ALREADY modeled: Game/GameState.h::ZoneObjectEntity,
// g_World.zoneObjects, resized to 500 by GameData_InitPools() (Game/MiscManagers.cpp).
// The original tick (sub_584170 0x584170, called for each active object) decompiles to an
// EMPTY __stdcall STUB (body `;`, NO observable effect) — reproduced here faithfully as-is:
// "simple tick logic, no known complex logic" (confirmed by decompilation, not a guess).
//
// *** REAL-CONDITIONS VERIFICATION (network -> gameplay chain audit mission,
// 2026-07-14) ***: at this module's initial writing, Net/GameHandlers_BossWorld.cpp did
// register Pkt_SpawnZoneObject on opcode 0x86, BUT the handler was a TODO
// stub (`(void)p;`) that NEVER wrote to g_World.zoneObjects — CONFIRMED CHAIN
// BREAK: the pool stayed frozen at 500 `active=false` slots end-to-end, so
// GetWorldObjectCount() returned 500 (fixed capacity, correct) but IsWorldObjectActive()
// ALWAYS returned false and TickWorldObject() was never reached on a real slot.
// FIXED HERE (same mission): Net/GameHandlers_BossWorld.cpp now implements the upsert
// by (idHi,idLo)/action faithful to RE/net_handler_notes.md (## Pkt_SpawnZoneObject, op 0x86) —
// action==2 creates/refreshes a slot (search by id, else 1st free slot, NO growth beyond
// 500), action==3 frees the slot and clears the auto-locked target if it pointed at this
// slot (dword_1675B24==7 && dword_1675B28==index, cf. Game/AutoTargetCombatGate.h). This
// file (GetWorldObjectCount/IsWorldObjectActive/TickWorldObject) required NO modification:
// it already read g_World.zoneObjects correctly, only the network producer was missing.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "Game/GameState.h"

namespace ts2::game {

// ===========================================================================
// 1. Fx_MeleeSwingTick — "ground item" extension
// ===========================================================================

// Per-frame tick fields absent from GroundItem (cf. header comment). Indexed like
// g_World.groundItems (lazy growth, same policy as MonsterTickExt/NpcTickExt
// in Game/EntityLifecycleTick.h).
struct GroundItemTickExt {
    uint32_t effectDefHandle = 0; // this+0 dereferenced — opaque, OUT OF SCOPE (asset), never
                                   // populated by this module (no asset/item system feeds it
                                   // yet on the ClientSource side).
    int32_t  mode  = 0;           // this+3 (record+16) : 0=Loop, 1=Once, other=no-op.
    float    frame = 0.0f;        // this+4 (record+20).
    float    farField44 = 0.0f;   // this+11 (record+48) — semantics undetermined.
    float    farSrcField80 = 0.0f;// this+20 (record+84) — semantics undetermined, copied
                                   // into farField44 when the distance to the local player > 400 u.
};

// Extension storage, indexed like g_World.groundItems. Grows lazily.
inline std::vector<GroundItemTickExt> g_GroundItemTickExt;

// Resets a slot's extension (to be called by the consolidation agent from the future
// ground-item spawn/pickup point, when a recycled slot changes network identity —
// same policy as Game/EntityLifecycleTick.h::ResetMonsterTickExt/ResetNpcTickExt).
// No-op if the index is out of bounds (grows the vector first if needed).
void ResetGroundItemTickExt(int groundItemIndex);

// Opaque callback to Model_GetWeaponEffectFrameCount 0x4E5A40 (OUT OF SCOPE — model/asset
// table). ex-VeryOldClient: EFFECT_OBJECT.mFrame (bounded by the MOB mesh's frame count
// via Model_GetNpcMeshSlot 0x4E5910 → Motion_GetFrameCount 0x4D7830) — CONFIRMED
// (Docs/TS2_FX_ROSETTA.md §1). `effectDefHandle` = GroundItemTickExt::effectDefHandle (always 0
// today, cf. above); `variant` = GroundItemTickExt::mode (same field as the
// loop/once dispatch, dual use faithful to the original). null, or return <= 0 -> the timer
// advances but NEVER loops/completes (same degradation policy as
// Game/AnimationTick.h::IMorphModelOracle).
struct GroundAuraWorldObjectTickHost {
    std::function<int(uint32_t effectDefHandle, int32_t variant)> GetWeaponEffectFrameCount;
};

// Fx_MeleeSwingTick 0x5803A0 (dispatch) + Fx_MeleeSwingTick_Loop 0x580400 / _Once 0x5804A0
// (merged here, faithful dispatch by ext.mode). Step 7 of Game/InGameTickFlow.h
// (host.TickGroundItemEffect = this hook, signature already aligned:
// `[](int idx, float dt){ TickGroundItemEffect(g_World, idx, dt, host); }`).
// Called only when world.groundItems[index].active (guard already done by
// the caller, cf. InGameTickFlow.cpp ~line 64); re-checked here for defensive robustness
// (faithful to the head guard `*(this+1)` @0x5803AC). No-op if index out of bounds.
void TickGroundItemEffect(GameWorld& world, int groundItemIndex, float dt,
                           const GroundAuraWorldObjectTickHost& host);

// ===========================================================================
// 2. FX attack projectile pool (g_FxAuraCount / dword_17D06F4) — cf. header comment
// ===========================================================================
// IMPLEMENTED ("FX pool #1" mission, Rule #0: IDA anchors in the .cpp). Unified SoA pool
// dword_17D06F4 (base 0x17D06F4, stride 64 dw = 256 bytes/slot, capacity g_FxAuraCount
// 0x168722C = 1000). Spawn Fx_SpawnAttackProjectile(Alt) 0x582530/0x582A10 → state 3; FSM
// tick Fx_HomingProjectileUpdate 0x5862D0 (states 3→4 owned; 1/2/12/13 = other non-owned
// spawns; 5..11/14 = render attach, pool #2 not owned). ex-VeryOldClient: EFFECT_OBJECT
// types 1→2 / 3→4 / 12→13 — PLAUSIBLE (taxonomy only; SoA stride 64 EU ≠
// EFFECT_OBJECT[1000], CONFLICT 3-A). Render/net/audio/element-leaf side effects deferred
// via g_FxProjectileHost (below).
// ===========================================================================

// Fields read by Fx_SpawnAttackProjectile @0x582530 on its `this` = a MONSTER
// dword_1766F74 record (the caller Char_Update 0x581E10 passes the monster during its
// attack tick). The wiring caller (Char_Update → spawn, separate mission) fills this
// struct from the monster + its MONSTER_INFO (`this+96`). Original byte offsets in comments.
struct FxProjectileSpawnParams {
    EntityId owner;             // caller+4 / caller+8 (shooter id)             @0x5825B5/0x5825C7
    EntityId target;            // caller+68 / caller+72 (target id)            @0x582601/0x582613
    float    startX = 0.0f;     // caller+32                                   @0x58281F
    float    startYRaw = 0.0f;  // caller+36 (before adding heightOffset)      @0x58283D
    float    startZ = 0.0f;     // caller+40                                   @0x58284F
    float    targetX = 0.0f;    // caller+44                                   @0x5828D7
    float    targetY = 0.0f;    // caller+48                                   @0x5828E9
    float    targetZ = 0.0f;    // caller+52                                   @0x5828FB
    float    heading = 0.0f;    // caller+56 (initial heading, deg)            @0x58286F
    uint32_t weaponId = 0;      // (*(caller+96))+244 (weapon/skill id)        @0x5825DF
    int32_t  weaponSubtype = 0; // (*(caller+96))+236 (element switch wep113)  @0x58268F
    int32_t  heightOffset = 0;  // (*(caller+96))+328 (added to startYRaw)     @0x58283D
    int32_t  speed = 0;         // (*(caller+96))+332 (speed, int→float)       @0x582913
};

// Mirror of the Op18 payload (this+180 = slot dw[45..56]) passed to Net_SendPacket_Op18
// (&g_AutoPlayMgr, this+180) @0x4B4CF0. Filled on impact; the actual network emission is
// done by the host (OUT OF network SCOPE).
struct FxImpactReport {
    int32_t  type = 4;          // dw[45] = 4                                  @0x58291F
    EntityId owner;             // dw[46]/dw[47] (shooter)                     @0x582935/0x582947
    EntityId target;            // dw[48]/dw[49] (= local player id on impact)
    float    impactX = 0.0f, impactY = 0.0f, impactZ = 0.0f; // dw[50..52] (local player pos)
    int32_t  flag1 = 1, flag2 = 0, flag3 = 0;                // dw[53..55]
    int32_t  homing = 0;        // dw[56] (0 = normal spawn, 1 = Alt)
};

// Side effects OUT OF SCOPE for the FX tick (render/net/audio/element-leaf). Null callback =
// safe no-op (the pool works: spawn/flight/transition stay faithful). Populated by the
// consolidation agent; original EA in comments.
struct FxProjectileHost {
    // ModelObj_GetSubObjectCount(&unk_B551B8 + 148*motionIndex, 0) 0x4D7080 : frame count of
    // the projectile's flight/impact model. <=0 (default) → no anim gating: state 4 ends
    // immediately (the projectile disappears without playing the impact anim; trajectory
    // and report stay faithful). @0x58659F (state3) / @0x58717D (state4)
    std::function<int(int motionIndex)> GetProjectileFrameCount;
    // Net_SendPacket_Op18(&g_AutoPlayMgr, slot+180) 0x4B4CF0 : auto-hit report (emitted when
    // the projectile's target is the local player). Null → no report. @0x5867B6/@0x5869E2
    std::function<void(const FxImpactReport&)> NotifyProjectileImpact;
    // Snd3D_PlayPositional(&flt_1487CBC[48*soundId], .., pos, self, 1) 0x4DA450 : positional
    // impact sound (soundId = Anim_MapWeaponToMotion3(weaponId)). Null → silent. @0x586A1E
    std::function<void(int soundId, float x, float y, float z)> PlayImpactSound;
    // dw[10] (elemImmune), computed at spawn for weaponId==113: g_LocalElement (0x1673194 =
    // Game/GameState.h self.element) + Char_GetPairedElement (0x557C00) on g_LocalPlayerSheet
    // (0x1685748, sheet NOT modeled). true suppresses the Op18 auto-hit report (elemental
    // immunity). Null → false (never immune, common case). Faithful switch (weaponSubtype):
    //   0x12→false ; 0x23→ !elem||elem==paired(0) ; 0x24→ elem==1||elem==paired(1) ;
    //   0x25→ elem==2||elem==paired(2) ; 0x26→ elem==3||elem==paired(3).  @0x58268F..0x5827A6
    std::function<bool(int weaponSubtype)> IsLocalElementImmune;
};

// Shared FX host (populated by the consolidation agent — separate mission). Null by default:
// the pool works (spawn/flight/transition) without render/network/audio.
inline FxProjectileHost g_FxProjectileHost;

// cGameData_InitPools 0x5575D0 (FX pool): sets g_FxAuraCount=1000 (@0x5575DC) and clears the
// slots (active=0). Call on world entry (Pkt_EnterWorld @0x4642A4 does the same clear
// loop). Idempotent; the default capacity is already 1000 without this call.
void Fx_InitProjectilePool();

// Fx_SpawnAttackProjectile 0x582530 : allocates the 1st free slot in state 3 (homing) from `p`.
// Return = (index<<8) of the allocated slot, or g_FxAuraCount if the pool is full (faithful:
// `return i` with no alloc). If Anim_MapWeaponToMotion1(weaponId)==-1 → slot freed, returns (index<<8).
// Homing faces: weaponId==113 → parabolic arc; else homing toward the targeted live entity.
int Fx_SpawnAttackProjectile(const FxProjectileSpawnParams& p);
// Fx_SpawnAttackProjectileAlt 0x582A10 : "direct homing toward a fixed target" variant (dw[12]=1,
// dw[56]=1; NO weaponId==113 branch). Same allocation/return.
int Fx_SpawnAttackProjectileAlt(const FxProjectileSpawnParams& p);

// g_FxAuraCount (0x168722C) — pool CAPACITY (tick loop bound for step 10, cf.
// Game/InGameTickFlow.cpp). = 1000 (cGameData_InitPools @0x5575DC).
int GetFxAuraCount();

// dword_17D06F4[64*index] (slot's 1st dw = active). Index guard @0x52CB8F (tick loop).
bool IsFxAuraActive(int index);

// Fx_HomingProjectileUpdate 0x5862D0 : FSM tick of one slot. Implements states 3
// (homing flight: arc wep113 / direct homing Alt / homing toward a live entity) and 4 (impact
// anim) FAITHFULLY. States 1/2/12/13 (projectiles from OTHER NON-OWNED spawns:
// Effect_SpawnSkillProjectile 0x573A90, …) and 5..11/14 (attach/particles, render pool #2 NOT
// OWNED) are not produced by this module → ignored (safe no-op, faithful `default: return`).
// Index guard + head active guard @0x5862D6.
void UpdateHomingProjectile(int index, float dt);

// ===========================================================================
// 3. Zone objects / resource nodes (g_World.zoneObjects) — cf. header comment
// ===========================================================================

// dword_1687230 == g_World.zoneObjects.size() (fixed post-init capacity, 500 — cf.
// GameData_InitPools, Game/MiscManagers.cpp). 0 if the pool hasn't been initialized yet.
int GetWorldObjectCount(const GameWorld& world);

// dword_180EEF4[19*index] (active state, +0x00 of ZoneObjectEntity). false if index out of
// bounds.
bool IsWorldObjectActive(const GameWorld& world, int index);

// sub_584170 0x584170 : EMPTY __stdcall STUB in the original binary (body `;`, no
// observable effect, confirmed by Hex-Rays decompilation — NOT a guess). Reproduced
// faithfully as-is: this function deliberately does NOTHING. Present only to
// complete the host.TickWorldObject hook signature (faithful: NO index passed to
// the original, cf. Game/InGameTickFlow.h).
void TickWorldObject(float dt);

} // namespace ts2::game

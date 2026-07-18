// Game/AnimationTick.h — ANIMATION/COLLISION SYSTEM: faithful C++ rewrite of 4
// functions decompiled via idaTs2 (Hex-Rays), wired onto game::g_World (Game/GameState.h)
// and Game/ActionStateMachine.h. Standalone Game/*.h/.cpp module (dedicated mission): does NOT
// edit Scene/SceneManager.*, the consolidation agent wires the hooks below onto the existing code.
//
// Functions covered (original EA, imagebase 0x400000):
//   - Player_UpdateLocalAnim        0x5321D0 — anim/FX for the LOCAL player only (this =
//     g_LocalPlayerSheet 0x1685748). EXHAUSTIVELY recounted block-by-block against a
//     fresh re-decompilation (audit 2026-07-14): 85 conditional blocks total =
//     75 generic rows (table kMorphRows, verified 1:1 by flagAddr against the binary)
//     + 3 "pulse" blocks clocked at this+8%6 (0x1675BAC/BB0, BCC/BD0, BD4/BD8)
//     + 3 special blocks indexed by g_SelfMorphNpcId (0x1675BA4/BDC/BE4)
//     + 1 block indexed by g_LocalElement (0x1675D98/DA8)
//     + 1 final pulse block (0x1675E90/E98)
//     = 83 "timer" blocks in the strict sense (the "~83" figure from an earlier report is
//     thus CONFIRMED EXACT, not underestimated), plus
//     + 1 ambiance/BGM block (900s replay, non-timer)
//     + 1 final one-shot check (g_SelfMorphNpcId==196 && dword_1685E10==1 -> warp)
//     = 85 blocks total, ALL present and verified in Player_UpdateLocalAnim below
//     (no missing block detected). Fully DATA-DRIVEN: reproduced via a static
//     table (Player_UpdateLocalAnim in AnimationTick.cpp), each timer stored at its
//     ORIGINAL ADDRESSES via game::g_Client.Var/VarF (Game/ClientRuntime.h, same convention
//     as Game/MapWarp.cpp).
//   - Char_UpdateAnimationFrame     0x571880 — advances the anim/action FSM of ONE
//     entity in the PLAYERS array (g_EntityArray, stride 908) — either the local player
//     or a remote one.
//
//     ///// FACTUAL CORRECTION — Pass 4 / wave W7, motion-anim front (2026-07-16) /////
//     The earlier wording said "(remote player OR MONSTER)". This is WRONG: NO
//     monster ever goes through 0x571880. Proof: disassembly of the single caller,
//     Scene_InGameUpdate 0x52C600 (reread instruction-by-instruction this session) — FOUR
//     DISJOINT entity families, each with its own tick function:
//       @0x52c96d  Char_UpdateAnimationFrame(g_EntityArray, 0, dt)           self      (stride 908)
//       @0x52c9fd  Char_UpdateAnimationFrame(&g_EntityArray[908*j], j, dt)   remote    (stride 908)
//       @0x52ca4c  Npc_RenderSlotTick(&g_NpcRenderArray[88*k], k, dt)        zone NPC  (stride  88)
//       @0x52cad6  Char_Update(&dword_1766F74[280*m], m, dt)                 MONSTERS  (stride 280)
//     The MONSTER animation FSM is therefore Char_Update 0x581E10 (switch @0x5822D3, 9
//     Char_MotionTick_* handlers), ported by Monster_DispatchMotionTick below (§5);
//     the zone NPC one is Npc_RenderSlotTick 0x5803A0, ported by ZoneNpc_TickAnim
//     (§6). The `TODO [anchor 0x571880]` that used to sit in Scene/WorldRenderer.cpp for
//     monster animType pointed, for the same reason, at the wrong function.
//
//     The core (contact detection, cast
//     interruption, generic tick primitives) is ALREADY written in Game/ActionStateMachine.h/.cpp
//     (ActionFsm): this function builds a TRANSIENT ActionFsm from
//     game::CharAnimState (persisted in PlayerEntity::anim/MonsterEntity::anim, cf.
//     GameState.h — field ADDED by this mission), uses it, then copies the result back.
//     Completes the orchestration with the blocks NOT covered by ActionFsm: secondary FX
//     timers (data-driven, same table engine as Player_UpdateLocalAnim), smoothed facial
//     rotation (540°/s, byte-exact, NO asset dependency), special aura, guild mark,
//     AutoPlay stop request. The terminal SWITCH (0x5727BF, 81 Char_*/
//     Combat_TickAttackState cases, each driven by an asset anim duration, out of scope) is
//     exposed via a SINGLE opaque hook `TickStateHandler` — same policy as
//     Game/ActionStateMachine.h::IAnimFrameOracle (see top of this file for the
//     rationale: 3D rendering/motion = out of gameplay scope). A PARTIAL router for this
//     switch (6 cases PROVEN out of 81) is provided in §7 below: Char_DispatchStateTick.
//
//     ///// FACTUAL CORRECTION — Pass 4 / wave W11, front w11-combat-fsm (2026-07-16) /////
//     "55 handlers" (here and in AnimationTick.cpp) was WRONG: the switch has 81 cases.
//     Proof down to the instruction: `cmp [ebp+var_6C], 5Fh ; switch 96 cases` @0x5727B2,
//     then `ja def_5727BF ; default case, cases 8,24-29,47,53,59,77-80,84` @0x5727B6
//     => 96 values (0x00..0x5F) − 15 `default` values = 81 live cases.
//   - Camera_UpdateCollision        0x538580 — 3rd-person camera: recomputes the eye by
//     keeping the same arm (eye-target) as the previous frame around the new
//     target (local player, y+10), then corrects for terrain collision (Terrain_SweepSphere
//     Segment) / objects (MapColl_LineOfSightObjects + MapColl_GetGroundHeight, ground
//     stepping). Operates on gfx::Camera (Gfx/Camera.h) and reuses Cam_SetLookAt (already
//     written in Game/CameraWarpTick.h) for the final placement — NO duplication of that function.
//   - MapColl_UpdateObjectAnim      0x694A00 — animation of map collision objects
//     (animated sub-objects at 15 fps + attached particle emitters). The original "this"
//     (zone collision object) has NO equivalent in GameState.h (property of the future
//     World/WorldMap): modeled here as STANDALONE state (MapCollisionObjectAnimState), supplied
//     by the caller (not in g_World).
//
// OUT OF SCOPE (3D asset/motion data, network, FX rendering — same policy as
// Game/ActionStateMachine.h and Game/InGameTickFlow.h): exposed via "oracle" interfaces
// (IMorphModelOracle, ICameraCollisionQueries) and opaque `std::function` hosts, original EA
// documented at each usage site. A null hook/oracle degrades cleanly
// (timer never completes / no collision correction / no-op), NEVER blocks.
//
// Self-containment: depends on Game/GameState.h (CharAnimState, GameWorld — field `anim` ADDED by
// this mission), Game/ActionStateMachine.h (ActionFsm, reused as-is), Game/MapWarp.h
// (BeginWarpToFactionTown, reused as-is), Game/ClientRuntime.h (g_Client.Var/VarF,
// "long tail" globals escape hatch), Game/CameraWarpTick.h + Gfx/Camera.h (Cam_SetLookAt,
// reused as-is). Does NOT include Scene/SceneManager.h or Net/*.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "Game/GameState.h"
#include "Game/ActionStateMachine.h"
#include "Gfx/Camera.h"

namespace ts2::game {

// 1. Player_UpdateLocalAnim 0x5321D0

// Faithful to ModelObj_GetSubObjectCount(tableAddr, 0): number of sub-objects/frames of a
// morph model identified by its ORIGINAL ADDRESS (unk_Bxxxxxx, possibly offset
// by 150368*modelIndex + 75184*modelVariant for the parameterized tables — cf. usage in
// Char_UpdateAnimationFrame). 3D asset data, OUT OF SCOPE (see top of file):
// the real implementation lives in the render/asset layer. oracle==nullptr -> the timers
// advance but never complete (clean degradation, cf. Duration() in the .cpp).
class IMorphModelOracle {
public:
    virtual ~IMorphModelOracle() = default;
    virtual int GetSubObjectCount(uint32_t tableAddr) const = 0;
};

struct LocalAnimTickHost {
    // World_LoadCurrentZoneModel((char*)dword_14A883C, reason) 0x4dd6e0 — reloads the
    // zone model layer (town/pvp/mount/etc. mode) when a morph timer
    // completes. Real target: world::WorldMap::LoadCurrentZoneModel(int), already written
    // (World/WorldMap.h), instance owned by SceneManager — wiring left to the
    // consolidation agent (host pattern, like InGameTickFlow.h).
    std::function<void(int reason)> LoadCurrentZoneModel;
    // World_IsPointOnGround(pos) — out of scope (collision/terrain height).
    std::function<bool(float x, float y, float z)> IsPointOnGround;
    // g_BgmEnabled == 1 (user music option) + Snd_Play3D(0,100,0) if true — restarts
    // the ambiance every 900s (15 min). IsBgmEnabled null -> false (no replay).
    std::function<bool()> IsBgmEnabled;
    std::function<void()> PlayAmbientBgm;
};

// Player_UpdateLocalAnim 0x5321D0. `dt` = original a3 (frame delta-time, 1/30s
// @30 FPS). Operates ONLY on the LOCAL player (world.Self(), world.self.element for
// faction resolution) — faithful to the original, which has no entity parameter (this =
// singleton g_LocalPlayerSheet). The 83 timers themselves live in game::g_Client.Var/
// VarF, NOT in GameWorld (these are original globals, not per-entity fields):
// no modification to GameState.h needed for this function.
void Player_UpdateLocalAnim(GameWorld& world, float dt,
                             const IMorphModelOracle* oracle, const LocalAnimTickHost& host);

// 2. Char_UpdateAnimationFrame 0x571880

struct CharAnimTickHost {
    // GuildMark_RegisterName(this+40) — out of scope (floating guild mark render
    // registry). Triggered when anim.hasPendingGuildMark==true (entity+68==1).
    std::function<void()> RegisterGuildMark;

    // Special aura (entity+180==2160 -> attempts to attach, cf. g_FxAuraCount/dword_17D06F4,
    // out of scope, property of the future FxAuraSystem — same array as
    // Game/InGameTickFlow.h step 10). HasFreeAuraSlot null -> false (never attached).
    std::function<bool()> HasFreeAuraSlot;
    std::function<void()> AttachSpecialAura; // Fx_AttachSpecialAura(this) 0x?

    // g_PendingStopRequest==1 && isLocalSimulation && state==Move(1) -> clear global +
    // Net_SendOp95(&g_AutoPlayMgr, 2). GetPendingStopRequest/ClearPendingStopRequest are
    // SHARED globals (not per-entity): the caller wires them to its own AutoPlay
    // state (cf. Game/AutoPlaySystem.h). Null -> never triggered.
    std::function<bool()> GetPendingStopRequest;
    std::function<void()> ClearPendingStopRequest;
    std::function<void()> SendAutoPlayStopAck;
};

// Result exposed for the SINGLE "contact detection" block (delegated to
// ActionFsm::UpdateContactDetection, already written) — see Game/ActionStateMachine.h for the
// full semantics of each field.
struct CharAnimTickResult {
    bool                contactFiredThisTick = false;
    CombatActionRequest lastAction{};
    bool                pendingAoECast    = false;
    bool                pendingProjectile = false;
};

// Char_UpdateAnimationFrame 0x571880. `anim` = the entity's persistent state (PlayerEntity::
// anim / MonsterEntity::anim, GameState.h). `actor`/`hitOracle` = combat context + event
// frame table, passed THROUGH UNCHANGED to ActionFsm::UpdateContactDetection (cf.
// Game/ActionStateMachine.h — hitOracle can be nullptr, degrades cleanly).
// `isLocalSimulation` = original !a2 (true = locally-driven entity,
// actually triggers network sends via lastAction); `isSelf` = original *(this+4)==
// dword_1687238[0] comparison (true only for entity 0 = self);
// `pendingCastInterrupt` = external condition already evaluated by the caller (g_InvDirtyEnable
// ==1 && (g_AutoHuntFuelA>0||g_AutoHuntFuelB>0), globals out of scope for this module — cf.
// Game/ActionStateMachine.h::ActionFsm::pendingCastInterrupt). `modelOracle` = same oracle
// as Player_UpdateLocalAnim (secondary FX timer tables). `stateHandler`, if
// supplied, is called AFTER the cast interruption with the CURRENT (post-interruption)
// state to advance the terminal switch (0x5727BF, 81 asset-driven cases) — null -> no
// anim progression beyond what this module already covers (contact/interrupt/FX/
// rotation), the FSM stays "frozen" on its current state.
// Calling `stateHandler` with the state RE-READ after the cast interruption is FAITHFUL: the
// binary reloads *(this+244) right before the switch (`mov edx, [ecx+0F4h]` @0x5727A9,
// 0xF4 = 244), downstream of the interruption block 0x57275A.
// WARNING WIRING (gap CTF-01/CTF-02): this parameter is `nullptr` at the single real call site
// (Scene/SceneManager.cpp:1133, 2nd `nullptr` = 10th argument) -> the switch is NEVER
// dispatched. See §7 (Char_DispatchStateTick) for the router ready to be plugged in.
void Char_UpdateAnimationFrame(CharAnimState& anim, const CombatActorState& actor,
                                const GameWorld& world, const IAnimFrameOracle* hitOracle,
                                bool isLocalSimulation, bool isSelf, bool pendingCastInterrupt,
                                float dt, const IMorphModelOracle* modelOracle,
                                const std::function<void(CharActionState state, float dt)>& stateHandler,
                                const CharAnimTickHost& host, CharAnimTickResult& outResult);

// 3. Camera_UpdateCollision 0x538580

// Terrain/object collision interfaces — OUT OF SCOPE (world geometry, property
// of World/WorldMap.*/Gfx/WorldGeometryRenderer.h, not Game/GameState.h). A null oracle
// disables ALL collision correction (the camera then follows the previous arm without
// ever pulling in from a wall/prop) — degrades cleanly, never blocks.
class ICameraCollisionQueries {
public:
    virtual ~ICameraCollisionQueries() = default;
    // Terrain_SweepSphereSegment(&from,&to,2.5,outHit) — true if the segment intersects the
    // terrain before `to`; `outHit` = impact point (replaces `to`).
    virtual bool SweepSphereSegment(const D3DXVECTOR3& from, const D3DXVECTOR3& to,
                                     float radius, D3DXVECTOR3& outHit) const = 0;
    // World_IsPointBlocked(p) — true if p is inside a collision solid.
    virtual bool IsPointBlocked(const D3DXVECTOR3& p) const = 0;
    // MapColl_LineOfSightObjects(&from,&to) — true if the from->to line requires the
    // "ground-height stepping" fallback below (furniture/wall intercepted by a map object).
    virtual bool LineOfSightBlockedByObjects(const D3DXVECTOR3& from, const D3DXVECTOR3& to) const = 0;
    // MapColl_GetGroundHeight(x,z,&out,0,0.0,0,true) — the last 4 original arguments
    // (layer index/slope threshold/flags) are FIXED at this single call site, so
    // not parameterized here. True if (x,,z) is obstructed at that height.
    virtual bool IsGroundBlocked(float x, float z) const = 0;
};

struct CameraCollisionHost {
    // Free-look mode (g_CamFreeLook && g_CamMode==3): looks for the entity whose name ==
    // g_CamFollowName (Crt_Strcmp over unk_168727C+908*i, array parallel to and indexed by
    // g_EntityArray — OUT OF SCOPE, property of the future EntityManager). Null -> treated
    // as "no target found" (faithful behavior: the binary's early `return`).
    std::function<bool(D3DXVECTOR3& outPos)> FindFreeLookFollowTarget;
    // Net_SendCmd_251(pos) — notifies the server of the new camera follow point.
    std::function<void(const D3DXVECTOR3& pos)> SendFollowCameraUpdate;
};

// Camera_UpdateCollision 0x538580. Reads/writes `camera` (3rd-person camera eye/target, cf.
// Gfx/Camera.h): `camera.Eye()`/`camera.Target()` = derived from flt_1687330/g_CameraPos of
// the PREVIOUS frame; `camera.Distance()` = approximation of g_CamFollowDist (SAME
// approximation convention as Game/CameraWarpTick.h::InGame_InitCamera, cf. its fidelity
// note — g_CamFollowDist is not read anywhere else in ClientSource to date).
// `freeLookActive`/`camMode` = g_CamFreeLook/g_CamMode (camera UI state, NOT in GameWorld).
// Does NOT modify `camera` where the binary would have taken an early `return` (faithful
// guards: "shop" morph 194 outside free-look; free-look target not found).
void Camera_UpdateCollision(gfx::Camera& camera, const GameWorld& world,
                             bool freeLookActive, int camMode,
                             const ICameraCollisionQueries* collision,
                             const CameraCollisionHost& host);

// 4. MapColl_UpdateObjectAnim 0x694A00

// Animated sub-object (original 36-byte record, *(this+27) array, count *(this+26)).
struct MapAnimSubObject {
    int32_t modelIndex = 0; // record+0 — resolution key for the animated model (out of scope)
    float   frame       = 0.0f; // record+28 — position in the anim (frames @ 15 fps)
};

// Particle emitter (original 76-byte record, *(this+32) array, count *(this+31)).
struct MapParticleEmitter {
    int32_t particleDefIndex = 0; // record+0 — index into the def table (out of scope)
    bool    initialized      = false; // record+28 — EXTERNAL PROPERTY: never written by
                                       // this function in the binary (set elsewhere,
                                       // at emitter spawn); only read here.
};

// Standalone state for the map collision object (the original "this", NOT in
// game::GameWorld — property of the future World/WorldMap, see top of file).
struct MapCollisionObjectAnimState {
    bool active = false; // *(this+1) (idx1) — record validity
    int  mode   = 0;     // *(this+2) (idx2) — must be 1 for the tick to run
    std::vector<MapAnimSubObject>   animObjects;
    std::vector<MapParticleEmitter> particleEmitters;
};

class IMapObjectAnimOracle {
public:
    virtual ~IMapObjectAnimOracle() = default;
    // ModelObj frame count for an animated collision object — model table out of
    // scope (assets), resolved via *(this+24)[modelIndex]+8 -> ...+252 in the binary.
    virtual int GetModelFrameCount(int modelIndex) const = 0;
    // Particle_Init(defTable + 232*defIndex) — starts the emitter (record not yet
    // `initialized`). Mutates state (actually starts the emitter on the FX side).
    virtual void InitParticle(int particleDefIndex) = 0;
    // Particle_UpdateEmit(dt, paramsA, paramsB) — out of scope (FX), an opaque hook per
    // already-initialized emitter (raw entity+4/+16 params not modeled here).
    virtual void UpdateParticle(int index, float dt) = 0;
};

// MapColl_UpdateObjectAnim 0x694A00. `dt` = original a3. The original a2 parameter is
// ALWAYS 15.0 at the single known call site (InGameTickFlow.h, MainTick step 4,
// `MapColl_UpdateObjectAnim(15.0,dt)`) — fixed here as a constant (kAnimFps) rather than a
// parameter to match actual usage. `oracle` can be nullptr: sub-objects
// still advance but never wrap (frame grows unbounded) and particles are neither
// initialized nor updated — clean degradation.
// ORIGINAL "this" IDENTITY (established by the 2026-07-14 audit, raw disasm of the call
// site 0x52c946): `mov ecx, offset dword_14A883C` — "this" is the SAME fixed global
// dword_14A883C passed as the first argument to World_LoadCurrentZoneModel everywhere
// in Player_UpdateLocalAnim (0x5321D0, see above). It is therefore a single SINGLETON
// (probably the "current zone model/object"), NOT a per-map-sub-object instance — useful if
// a future World/WorldMap wants to expose a single global instance of
// MapCollisionObjectAnimState instead of an array.
void MapColl_UpdateObjectAnim(MapCollisionObjectAnimState& obj, float dt,
                               IMapObjectAnimOracle* oracle);

// 5. MONSTER animation FSM — the 9 Char_MotionTick_* + their dispatch @0x5822D3
//    (Pass 4 / wave W7, motion-anim front — gaps as-motion-01 / as-motion-02)
//
// WHY THIS BLOCK EXISTS. Monsters were drawn with `animType` frozen at 0 (idle) and
// a shared GLOBAL clock: all animated entities in phase, none ever changing
// animation. The root cause was NOT the renderer but a hook that was never assigned:
// `EntityLifecycleTickHost::DispatchMotionTick` (Game/EntityLifecycleTick.h:187) is
// declared, called by UpdateMonster (EntityLifecycleTick.cpp:153) ... and nobody
// implemented it — the 9 handlers were classified "OUT OF SCOPE". Result:
// `MonsterTickExt::motionState` / `::animFrame` NEVER moved.
// => Consequence to keep in mind before wiring: hooking the renderer onto
// g_MonsterTickExt[i].animFrame WITHOUT this port would give a cursor stuck at 0, i.e.
// TOTALLY FROZEN monsters — strictly worse than the shared clock. The two must go together.
//
// GROUND TRUTH (IDA, re-proven block by block this session):
//   Char_Draw 0x5805C0 @0x580770: animType = *((_DWORD*)this + 6)  = monster slot +24
//   Char_Draw 0x5805C0 @0x580828: cursor   = *((float*)this + 7)   = monster slot +28
//   Char_Update 0x581E10 @0x5822D3: switch on this SAME slot+24, 9 cases — and these 9 values
//     {0,1,3,4,5,7,8,0xC,0x13} are EXACTLY the valid set of Model_GetNpcMotionSlot
//     0x4E5960 @0x4e59a4 (cross-proof: slot+24 IS indeed the draw's animation index).
//   All handlers: `frame += dt*30`, frameCount = Model_GetMotionFrameCount 0x4E5A70
//     (SAME slot as the draw -> count == palette.frameCount, cf. Gfx/MotionCache.h).
//     The wrap is a SINGLE SUBTRACTION (never a modulo).
//
// State -> end semantics mapping (handler EA, transition EA):
//   0  ToIdle    0x582D40: frame>=count -> state=1, frame=0            @0x582d99/@0x582da5
//   1  Loop      0x582DB0: frame>=count -> frame -= count              @0x582e10
//   3  MoveA     0x582E20: wrap @0x582e80 + StepTowardTarget(speed+384) -> arrival/failure
//                           => state=1, frame=0                         @0x582ebd/@0x582ed7
//   4  MoveB     0x582EF0: same as MoveA, speed def+388                 @0x582f8d/@0x582fa7
//   5  AttackA   0x582FC0: frame>=count -> state=1, frame=0, +108=0    @0x583019..@0x58302b
//   7  AttackB   0x583040: same as AttackA                             @0x583099..@0x5830ab
//   8  Hit       0x5830C0: frame>=count -> state=1, frame=0            @0x583119/@0x583125
//   0xC Knockback 0x583130: frame>=count -> frame = count-1 (freeze)   @0x583284
//   0x13 Death   0x5832E0: frame>=count -> frame = count-1 (freeze)    @0x583345
//
// The carrier state is `game::g_MonsterTickExt[monsterIndex]` (Game/EntityLifecycleTick.h):
// `.motionState` = slot+24, `.animFrame` = slot+28, `.attackWindupMode` = slot+108. These three
// fields ALREADY EXIST and carry the correct offsets — nothing to add to MonsterTickExt (file
// not owned by this front). `MonsterEntity::anim` (GameState.h:225) is NOT used here:
// it's a CharAnimState modeled on PLAYER offsets (entity+244/+248), dead for
// monsters (cf. warning in Game/AutoTargetCombatGate.h:106-112).

// Number of frames of an animation, per entity family. Mirrors Model_GetMotionFrameCount
// 0x4E5A70 (monster) / Model_GetWeaponEffectFrameCount 0x4E5A40 (zone NPC): both
// resolve the SAME slot as the draw, so the count is that of the draw palette.
// Asset (motion) data => OUT OF SCOPE for Game/*, exposed as an oracle — same policy as
// IMorphModelOracle/IAnimFrameOracle. Implemented by Scene/WorldRenderer.cpp (sole owner
// of the MotionCache), exposed via ts2::WorldMotionFrameCountOracle() (Scene/WorldRenderer.h).
// Returns 0 if the slot doesn't resolve (missing file/out-of-bounds index) — treated as "unknown
// duration" by the handlers (see degradation below).
class IMotionFrameCountOracle {
public:
    virtual ~IMotionFrameCountOracle() = default;
    // Model_GetMotionFrameCount 0x4E5A70(g_ModelMotionArray, MONSTER_INFO.kindIndexP1-1, animType).
    // monsterDefId = MonsterEntity::body[0] (raw id, no -1 — same convention as
    // Game/EntityManager.cpp::ResolveMobDef; the -1 on kindIndexP1 is applied internally).
    virtual int GetMonsterMotionFrameCount(uint32_t monsterDefId, int animType) const = 0;
    // Model_GetWeaponEffectFrameCount 0x4E5A40(g_ModelMotionArray, NpcDefRecord::fieldE-1, animType),
    // indexed the same way as game::ZoneNpcs() (Game/StaticNpcLoader.h).
    virtual int GetZoneNpcMotionFrameCount(int zoneNpcIndex, int animType) const = 0;
};

// Dependencies of the Move states (3/4) falling outside the "animation" scope: actual
// entity displacement against map collision. Null hook -> see degradation note on
// Monster_DispatchMotionTick.
struct MonsterMotionTickHost {
    // MapColl_StepTowardTarget 0x6974C0(&dword_14A88E4, this+32 /*pos*/, this+44 /*target*/,
    // speed, dt, &outArrived) — moves the monster toward its target. Returns false if the step
    // FAILED (blocked); outArrived=true when the target is reached. BOTH cases revert
    // the state to Loop(1) in the binary (@0x582ebd failure, @0x582ed7 arrival).
    // speed = MONSTER_INFO+384 (MoveA) / +388 (MoveB) — read by the hook implementer, who
    // alone has access to the def record and the collision geometry.
    std::function<bool(int monsterIndex, bool moveB, float dt, bool& outArrived)> StepTowardTarget;
};

// Char_Update 0x581E10, terminal switch @0x5822D3 ONLY (the upstream blocks — aura timers,
// hit window, fall physics — are already ported by Game/EntityLifecycleTick.cpp::
// UpdateMonster, which calls this dispatch last via host.DispatchMotionTick).
//
// DEGRADATION (no hook/oracle is mandatory, nothing ever blocks):
//   - null oracle OR frameCount<=0: the cursor ADVANCES (frame += dt*30) but no bound is
//     known -> no wrap, no state transition emitted. Safe: we don't fabricate a duration.
//   - null StepTowardTarget (states 3/4): the displacement AND the "arrived" transition are
//     skipped. We deliberately do NOT treat "hook absent" as "result==0 -> state=1", which
//     would kick the entity out of Move instantly every frame (the monster would never walk).
//     The frame wrap, however, is still applied (it doesn't depend on movement).
// No-op if the index is out of bounds, if the monster is inactive, or if motionState is not
// one of the 9 switch cases (binary's `default: return` @0x5822d3 — faithful).
void Monster_DispatchMotionTick(GameWorld& world, int monsterIndex, float dt,
                                 const IMotionFrameCountOracle* oracle,
                                 const MonsterMotionTickHost& host);

// True as soon as a Monster_DispatchMotionTick has actually run at least once since
// startup. WIRING NON-REGRESSION GUARD, NOT a binary behavior: it lets
// Scene/WorldRenderer.cpp consume the per-entity cursor ONLY once it is actually
// fed, and otherwise keep the old global-clock fallback (animated but in
// phase) instead of freezing every monster at frame 0 — the regression scenario described at
// the top of this block, which would occur if wiring DispatchMotionTick were forgotten. To be
// removed once the wiring is locked in by a test.
bool Monster_MotionTickIsWired();

// 6. Zone-decor NPC animation — Npc_RenderSlotTick 0x5803A0 (+ _Loop/_Once)
//    (Pass 4 / wave W7, motion-anim front — gaps as-motion-01 / as-motion-02)
//
// GROUND TRUTH (IDA, re-proven this session). Original slot = &g_NpcRenderArray[88*i]
// (0x1764D14, stride 88), layout consistent across 3 functions (cGameData_LoadZoneNpcInfo
// 0x5578E0, Npc_DrawMesh 0x57FF00, Npc_RenderSlotTick 0x5803A0):
//     +0  def ptr (*(def+1324) = kindIndex+1)   +4  active
//     +12 animType/mode (0=Loop, 1=Once)        +16 cursor
//     +20/24/28 pos                             +44 displayed angle   +80 baseline angle
//   Npc_DrawMesh @0x57ffa0: Model_GetNpcMeshSlot(..., a3 = *((_DWORD*)this + 3)) -> +12
//   Npc_DrawMesh @0x57fff1: SObject_DrawEx(..., animTime = *(this + 4), ...)     -> +16
//   Npc_RenderSlotTick_Loop 0x580400: +16 += dt*30 @0x58043e; wrap by subtraction @0x58045f;
//     if Math_Dist3D(pos, flt_1687330 /*local player*/) > 400 -> +44 = +80 @0x58048e
//   Npc_RenderSlotTick_Once 0x5804A0: frame>=count -> +12 = 0, +16 = 0 @0x5804f8/@0x580504
//
// WHO WRITES animType=1 (found — the system is indeed ALIVE): UI_NpcWin_Open 0x5DB530, when
// a NPC's dialogue window opens, @0x5dc019..0x5dc0a8:
//     if (slot+12 != 1) { slot+12 = 1; slot+16 = 0.0;                      @0x5dc026/@0x5dc032
//         if (kind not in {63,113,213,313,7})
//             slot+44 = Math_AngleBetween2D(slot+20, slot+28, player.x, player.z);  @0x5dc0a2
//         Fx_MeleeSwingUpdate(slot); }                                     @0x5dc0a8
// I.e.: animType ∈ {0,1} — 0 = looped idle, 1 = "greeting" animation played ONCE when
// talked to (+ the NPC turns to face the player, except 5 kinds), then _Once resets it to 0. The
// baseline reset beyond 400 units closes the loop (it resumes its original heading once you move away).
//
// PITFALL AVOIDED (do not repeat): `xrefs_to(0x1764D20)` (= slot+12) returns 0 xrefs. To
// conclude "nobody writes animType" would be WRONG — the writes are register-relative
// (`mov [eax+0Ch], 1`), hence INVISIBLE to absolute xref lookup. Same pitfall already
// documented for flt_18C53C0 (Scene/WorldRenderer.h:269).
//
// WHERE THE STATE LIVES — NO MORE PARALLEL VECTOR (W7 "npc-array-unify" adaptation). The
// initial writeup of this front carried a parallel `ZoneNpcAnimExt` "because StaticNpcSlot had
// NEITHER animType NOR cursor". That premise is now OBSOLETE: wave W7 merged the TWO models of
// the original pool into `GameWorld::npcRenderEntries` (Game/GameState.h, struct NpcRenderEntry),
// whose PROVEN layout ALREADY carries, at the original g_NpcRenderArray slot offsets, every
// field this tick needs:
//     +12 mode (animType, 0=Loop / 1=Once)      +16 frameAcc (cursor)
//     +44 angle (displayed, mutated by the tick) +80 angleBase (baseline)
// The tick therefore operates DIRECTLY on these native fields (faithful to Npc_RenderSlotTick
// 0x5803A0, which operates on EXACTLY the same ones), instead of duplicating state — this is
// precisely the de-duplication W7 established (`StaticNpcSlot` is now an alias of
// `NpcRenderEntry`, `kindId` removed). The loader (cGameData_LoadZoneNpcInfo 0x5578E0) already
// initializes mode=0 @0x55797f, frameAcc=0 @0x557995, angle=angleBase @0x557a42/@0x557a62: no
// init state left to port here (the old ZoneNpc_ResetAnimExt disappears along with the vector).
//
// REMAINING TICK DUPLICATE (flagged, out of scope — unresolved). This same pool already has a
// SECOND port of 0x5803A0 wired up: Game/GroundAuraWorldObjectTick.h::TickGroundItemEffect
// (InGameTickFlow step 7, Scene/SceneManager.cpp). But it writes a DEAD PARALLEL EXTENSION
// (`g_GroundItemTickExt`, null oracle) that NOBODY reads for rendering -> it animates nothing
// on screen. `ZoneNpc_TickAnim` below writes the NATIVE fields the renderer reads: it is THE ONE
// that closes the gap. The two write DISJOINT fields (native vs. dead ext) -> no
// double-advance of the same cursor; the duplicate remains a fidelity debt nonetheless (the
// binary ticks 0x5803A0 exactly once per slot/frame). This front does not remove
// TickGroundItemEffect (GroundAuraWorldObjectTick / SceneManager not owned by it) — see report.

// Npc_RenderSlotTick 0x5803A0 for ALL ACTIVE slots of g_World.npcRenderEntries (single pool,
// W7), once per frame. Faithful dispatch @0x5803ba: inactive slot (`active`==false, guard
// @0x5803ac) -> no-op; mode==0 -> _Loop (0x580400); mode==1 -> _Once (0x5804A0); any other
// value -> no-op (the binary only tests these two). Null oracle or frameCount<=0: the cursor
// (frameAcc) advances but never wraps/completes (clean degradation, consistent with
// Monster_DispatchMotionTick).
// TO WIRE (outside my files, blocking for as-motion-01/02 on the NPC side): call ONCE PER
// FRAME in the InGame tick — Scene/SceneManager.cpp, right after game::InGameTickFlow_Update
// (~line 1151) — with the oracle ts2::WorldMotionFrameCountOracle(). Without this wiring frameAcc
// stays 0 and the renderer keeps the global-clock fallback (see ZoneNpc_AnimTickIsWired below).
void ZoneNpc_TickAnim(float dt, const IMotionFrameCountOracle* oracle);

// Exact counterpart of Monster_MotionTickIsWired() for zone-decor NPCs: true as soon as a
// ZoneNpc_TickAnim has actually run. SAME non-regression guard, SAME rationale —
// as long as Scene/SceneManager.cpp does not call ZoneNpc_TickAnim per frame, the cursor stays
// at 0 and consuming `cursor` would freeze zone NPCs on the 1st frame of their idle, whereas
// they were at least animated (in phase) by the old SampleByGameTime fallback. NOT a
// binary behavior: to be removed once the wiring is locked in by a test.
bool ZoneNpc_AnimTickIsWired();

// UI_NpcWin_Open 0x5DB530 @0x5dc019..0x5dc0a8 — to be called when a zone-decor NPC's dialogue
// window OPENS. Mutates DIRECTLY the native slot g_World.npcRenderEntries[zoneNpcIndex] (index
// into the single pool = index of game::ZoneNpcs()). Idempotent: does nothing if mode is already 1
// (guard @0x5dc01d, faithful).
// `playerX/playerZ` = flt_1687330/flt_1687338 (local player position) — the NPC turns to face
// the player (angle=+44) UNLESS its NpcDefRecord::id ∈ {63,113,213,313,7} (@0x5dc03a..0x5dc06b).
// NOT WIRED to date (the NpcDialog UI belongs to a neighboring wave) -> mode stays 0: this is
// FAITHFUL (an NPC nobody talks to loops its idle), not a bug.
// NOTE: Fx_MeleeSwingUpdate(slot) 0x57FE90 (@0x5dc0a8, positional sound) is NOT reproduced here —
// out of scope for this front's audio/FX. TODO [anchor 0x57FE90].
void ZoneNpc_OnDialogueOpen(int zoneNpcIndex, float playerX, float playerZ);

// 7. PARTIAL router for the terminal PLAYER switch — Char_UpdateAnimationFrame 0x571880,
//    switch @0x5727BF (Pass 4 / wave W11, front w11-combat-fsm — gap CTF-02)
//
// WHY THIS BLOCK EXISTS. Game/ActionStateMachine.cpp carries 4 state-tick primitives
// (TickTimedState / TickCastState / TickGuardBegin / TickGuardLoop), written, anchored EA by
// EA... and that NOBODY CALLS (exhaustive grep of ClientSource: 1 single occurrence
// each = their own definition). This is the "correct code that nobody calls" defect.
// The corresponding binary path is LIVE and reachable by the player — `reaches`
// (WinMain 0x4609C0 -> Char_CastAnimTick_5762F0 0x5762F0) = true, depth 5, not truncated:
//     WinMain 0x4609C0 -> App_FrameTick 0x4625D0 -> cSceneMgr_Update 0x517BF0
//                      -> Scene_InGameUpdate 0x52C600 -> Char_UpdateAnimationFrame 0x571880
//                      -> 0x5762F0
// => the rule "a DEAD binary function should stay dead" does NOT apply here.
//
// COVERAGE: 6 cases PROVEN out of 81. Each of the 6 original functions below has
// EXACTLY ONE caller in the whole binary (`xrefs_to` -> xref_count == 1), and that
// caller is always this switch — the case -> primitive mapping is therefore
// bijective and unambiguous. The case labels are IDA's own
// (`jumptable 005727BF case N`), reread this session, not a deduction:
//     case 4  @0x572834 -> Char_AnimEndToIdle_5761A0   0x5761A0  -> TickTimedState(-> Move)
//     case 5  @0x57284C -> Char_CastAnimTick_5762F0    0x5762F0  -> TickCastState
//     case 6  @0x572864 -> Char_CastAnimTick_5764D0    0x5764D0  -> TickCastState (identical body)
//     case 7  @0x57287C -> Char_CastAnimTick_5766B0    0x5766B0  -> TickCastState (identical body)
//     case 91 @0x572EEE -> Char_ActionTick_GuardBegin  0x57F260  -> TickGuardBegin
//     case 92 @0x572F03 -> Char_ActionTick_GuardLoop   0x57F410  -> TickGuardLoop
// The other 75 cases -> explicit `default:` no-op (cf. TODO in the .cpp). This is NOT the
// binary's `default` (which only has 15 silent values): it's an admission that these cases don't
// yet have a ported primitive. They therefore stay frozen, exactly as today.
//
// DEGRADATION (no hook is mandatory, nothing ever blocks) — SAME policy as
// Monster_DispatchMotionTick (§5) and MorphDuration: WE NEVER FABRICATE A DURATION.
//   - null GetMotionFrameCount OR count<=0: "unknown" duration -> the cursor ADVANCES but
//     no transition is emitted (the timed/guard families run without ever finishing).
//   - null GetCastRateWithinBounds: the weapon rate is UNKNOWN. Since it's a MULTIPLIER
//     of the frame step (not a bound), the cast cursor does NOT advance for lack of a value.
//     Asymmetry DELIBERATE with the case above, and for the same reason (fabricate nothing) —
//     the cast state stays frozen, i.e. the current behavior: no regression.
struct CharStateTickHost {
    // PcModel_ResolveSlotAndApply 0x4E5A00 -> the NUMBER OF FRAMES of the current anim. Called at
    // the TOP of the 4 handlers, before the frame advance (@0x5761AC/@0x5762FC/@0x57F26C/@0x57F41C).
    // Original arguments (identical across all 4):
    //     PcModel_ResolveSlotAndApply(g_ModelMotionArray, this+92 /*modelIndex*/,
    //         this+96 /*modelVariant*/, this+240 /*animSlot*/, this+244 /*state*/,
    //         this+108, this+112, (*(this+576) ? 0 : *(this+220)), a2 == 0)
    // NOTE: the 8th argument reproduces EXACTLY the `altWeaponSet ? 0 : weaponAnimSlot`
    // idiom already used for `altIndex` in ActionFsm::UpdateContactDetection — an
    // independent cross-check validating both readings.
    // WARNING TODO [anchor 0x4E5A00]: `this+240` (animSlot) and `this+108`/`this+112` have NO
    // carrier field in game::CharAnimState (Game/GameState.h — out of scope for this front).
    // The hook implementer therefore CANNOT reconstruct the original call identically;
    // relation proven to fill in +240 once the field exists: +240 == 2 * weaponClass
    // (cf. @0x57629B and Game/ActionStateMachine.cpp::TickTimedState). Return <=0 as long as
    // the slot doesn't resolve -> treated as "unknown duration" (see degradation above).
    std::function<int(const CharAnimState& anim, bool isLocalSimulation)> GetMotionFrameCount;

    // Char_CalcWeaponRatePct 0x4CD900(this+328, this+116) @0x5763BE, then open-bound test
    // @0x5763FA: `v6 > Char_CalcAnimBoundMin99(this, this+328)
    //                    && v6 < Char_CalcAnimBoundMax121(this, this+328)`.
    // Returns true if the rate is within bounds (the cast cursor then advances by
    // dt*rate*0.3), and writes the rate into outRatePct. Used ONLY by cases 5/6/7.
    // WARNING TODO [anchors 0x4CD900 / 0x57FB30 / 0x57FBB0]: none of the three is ported for an
    // ARBITRARY entity. Game/StatFormulas.h:71 does port CalcWeaponRatePct, but with the
    // signature `(const SelfState&, const GameDatabases&)` = SELF only, whereas the
    // binary parameterizes it per entity (this+328, this+116). Both bounds decompile to
    // `floor(GemStat_WeaponRateFactor(*(entity+144)) * base)` with base = 99.0 (0x57FB30) /
    // 121.0 (0x57FBB0), HALVED if `*(entity+428) > 0` (@0x57FB49 / @0x57FBC9).
    std::function<bool(const CharAnimState& anim, double& outRatePct)> GetCastRateWithinBounds;
};

// Router for the terminal switch @0x5727BF, meant to be plugged into the `stateHandler`
// parameter of Char_UpdateAnimationFrame (§2). `state` = the CURRENT state re-read after the
// cast interruption (which is what Char_UpdateAnimationFrame already supplies). `anim` is
// mutated in place (state / animFrame / guardSubstate / hitCheckActive). `isLocalSimulation` =
// original !a2, propagated all the way down to the primitives since it GUARDS transitions (cf.
// TickGuardBegin: the GuardLoop->GuardEnd jump is nested inside
// `if (!a4)` @0x57F371).
// No-op for the 75 uncovered cases (cf. TODO in the .cpp) and for the 15 `default`
// values of the binary (8; 24-29; 47; 53; 59; 77-80; 84) — faithful for the latter.
//
// TO WIRE (OUTSIDE MY FILES — neighboring front, see W11 report):
//   Scene/SceneManager.cpp:1133, 10th argument (2nd `nullptr`) of game::Char_UpdateAnimationFrame,
//   in the open lambda host.UpdateEntityAnimFrame :1109. Replace this `nullptr` with:
//       [&p, isSelf](game::CharActionState st, float sdt) {
//           game::Char_DispatchStateTick(p.anim, st, sdt, isSelf, s_charStateHost);
//       }
//   (`s_charStateHost` = a game::CharStateTickHost; leaving it DEFAULT is legitimate and without
//   regression — see degradation above.) As long as this `nullptr` stays in place, this router
//   is called by nobody: CTF-02 is NOT closed, it is merely moved one step over.
void Char_DispatchStateTick(CharAnimState& anim, CharActionState state, float dt,
                             bool isLocalSimulation, const CharStateTickHost& host);

// Exact counterpart of Monster_MotionTickIsWired()/ZoneNpc_AnimTickIsWired() for the PLAYER
// FSM: true as soon as a Char_DispatchStateTick has actually run at least once. WIRING
// NON-REGRESSION GUARD, NOT a binary behavior — it lets a consumer
// (e.g. Gfx/PlayerPaperdoll.cpp:20-23, which today samples via the global-clock
// MotionCache::SampleByGameTime) switch to CharAnimState::animFrame ONLY once it is
// actually fed, instead of freezing every player at frame 0. See the freeze
// admission documented at Gfx/MotionCache.h:90-95. To be removed once the wiring is locked in
// by a test.
bool Char_StateTickIsWired();

} // namespace ts2::game

// Game/EntityLifecycleTick.h — Entity lifecycle system: despawn of stale remote
// players, monster tick (aura/hit-window/knockback), monster respawn after
// knockback, NPC effect tick + expiry (gravity/ground collision), stale NPC cleanup.
//
// C++ rewrite of the following 5 functions (sole source of truth = Hex-Rays
// decompilation via idaTs2, imagebase 0x400000):
//   sub_55D720   (named PlayerArray_SlotDestruct in the IDB) — step 6 of
//                Game/InGameTickFlow.h (host.DespawnStalePlayer).
//   Char_Update              0x581E10 — step 8 (host.UpdateMonster).
//   sub_580550   (named Char_RespawnAfterKnockback in the IDB) — step 8, "stale"
//                branch (host.RespawnMonsterAfterKnockback).
//   Fx_GibUpdate              0x583CD0 — step 9 (host.TickNpcEffect).
//   sub_583390   — step 9, "stale" branch (host.CleanupStaleNpcEffect).
//
// All 5 are called from Scene_InGameUpdate 0x52C600 (default/MainTick), for EVERY
// active entity whose g_GameTimeSec - timestamp <= 7.5 (tick) or > 7.5 ("stale"
// function). See Game/InGameTickFlow.h for the full orchestration and
// Game/GameState.h for PlayerEntity/MonsterEntity/NpcEntity (structures consumed
// here, NOT modified).
//
// === NAMING GAP CONFIRMED BY DECOMPILATION (flagged) ===
// sub_55D720, sub_580550, and sub_583390 ALL THREE decompile to a single identical
// line: `*(_DWORD*)this = 0; return this;` — i.e. they deactivate (set active=false)
// the entity slot passed to them, NOTHING ELSE. In particular, sub_580550 (renamed
// "Char_RespawnAfterKnockback" in the IDB, head comment "[GXD] Resets the character
// after knockback expires (~3s)") has EXACTLY the same observable behavior as the two
// "despawn/cleanup" functions — despite a name suggesting revival. The comment in
// Game/InGameTickFlow.h ("entity deemed stale -> revival, NOT despawn") reflects the
// intent behind the IDA name, not the function's actual behavior: at THIS code level,
// the three functions are interchangeable (slot deactivation). Reproduced faithfully
// here: the three wrappers below all call the same internal primitive DeactivateSlot().
// A future "real" monster respawn system (restarting a spawn timer, server
// notification, etc.) is OUT OF SCOPE for this mission — not found in sub_580550's own
// body (possibly handled elsewhere, e.g. server-side or in the network spawn handler).
//
// === Char_Update / Fx_GibUpdate — extra fields outside GameState.h ===
// MonsterEntity/NpcEntity (Game/GameState.h) are deliberately lightweight CLEAN
// models (not byte-exact) that do NOT carry the per-frame tick fields used by these
// two functions (aura timers, hit-window state, physical fall velocity/offset...).
// Like ActionStateMachine.h (ActionFsm, in addition to GameState) and
// EntityManager.h (GroundPickupSlot, "absent from GameState"), this module carries
// these fields in parallel EXTENSION structures indexed like
// g_World.monsters/g_World.npcs (MonsterTickExt / NpcTickExt, see below) rather than
// extending GameState.h (shared base file, out of this mission's edit scope).
// INTEGRATION WARNING: these extension vectors must be reset (Reset*TickExt) when a
// slot changes network identity (spawn on a recycled slot) — this module does NOT
// do so itself (it has no visibility into spawn sites, see Game/EntityManager.h).
// WIRED (audit 2026-07-14): Game/EntityManager.cpp::OnSpawnMonster/OnSpawnNpc now
// call ResetMonsterTickExt/ResetNpcTickExt on the "new" branch (slot freshly claimed
// via FindOrAdd — potentially recycled), NOT on the "refresh" branch (same entity,
// the extension must survive).
//
// === Out-of-scope subsystems, exposed as opaque callbacks (EntityLifecycleTickHost) ===
// Like Game/ActionStateMachine.h (IAnimFrameOracle) and Game/InGameTickFlow.h
// (InGameTickFlowHost), the following dependencies of the original binary fall
// outside this mission's "entity lifecycle" scope and are exposed as hooks:
//   - ModelObj_GetSubObjectCount 0x4D7080 (model-swap anim duration, asset data)
//   - Anim_IsFrameInHitListA/B   0x559F80 / 0x55A000 (fixed hit-frame table, asset)
//   - Combat_SendMeleeHit1/2     0x5823E0 / 0x582480 (network emission on "monster attacks")
//   - Fx_SpawnAttackProjectile(Alt) 0x582530 / 0x582A10 (projectile FX)
//   - Char_MotionTick_* (0x582D40..0x5832E0, 9 handlers), dispatched by Char_Update's
//     terminal switch — monster animation FSM, OUT OF SCOPE (3D render/anim), grouped
//     into a SINGLE opaque hook DispatchMotionTick (same treatment as BeginComboMorph/
//     ValidateAutoTarget in InGameTickFlowHost).
//   - the bypass *(_DWORD*)(*((_DWORD*)this+24)+244)==113 || ...+236)==18 (0x571FF2, same
//     pattern in Char_Update ~0x581FF2->0x582060): the exact nature of the this+24
//     pointer is AMBIGUOUS in the disassembly (used BOTH as a direct int animId for
//     Anim_IsFrameInHitListA/B AND as a dereferenced pointer for this test) — not
//     resolved by this mission, exposed as callback IsAttackTargetBypassActive()
//     (default: false, i.e. the target scan always runs).
//   - MapColl_GetGroundHeight 0x697130 (terrain collision)
//   - Snd3D_PlayPositional 0x4DA450 (impact sound on the first fall tick)
//
// Namespace ts2::game. Free functions operating on game::g_World (no implicit
// singleton: the world and the extension are passed/accessed explicitly).
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "Game/GameState.h"

namespace ts2::game {

// "Monster" extension — per-frame tick fields absent from MonsterEntity.
// Original offsets (Char_Update 0x581E10, dword index *(this+N) => byte N*4,
// real stride dword_1766F74 = 0x118 = 280 bytes = 70 dwords) given in comments.
struct MonsterTickExt {
    // --- FSM dispatch (this+6, byte 24): read by Char_Update, written by the
    // Char_MotionTick_* handlers themselves (out of scope) via host.DispatchMotionTick.
    // This module ONLY reads it to decide what to call next (no state transition
    // handled here). Values observed in the original switch (0x5822D3):
    // 0=ToIdle,1=Loop,3=MoveA,4=MoveB,5=AttackA,7=AttackB,8=Hit,0xC=Knockback,0x13=Death.
    int32_t motionState = 0;

    // --- Model-swap timers (3 independent slots, identical pattern):
    // this+64/65 (256/260), this+66/67 (264/268), this+68/69 (272/276). active=bool,
    // frame increments by dt*30 up to the duration (asset, host.GetAuraSwapDuration)
    // then active goes back to false.
    bool  auraActive[3] = {false, false, false};
    float auraFrame[3]  = {0.0f, 0.0f, 0.0f};

    // --- Hit-window detection block (states 5=AttackA / 7=AttackB only,
    // 0x581F34..0x582145). Fields consulted:
    int32_t attackWindupMode = 0;  // this+27 (108): ==1 -> hit window potentially active
    bool    hitLatched        = false; // this+30 (120): "already in the window" latch (anti double-trigger)
    int32_t hitActionKind     = 0; // this+28 (112): 1=melee hit (dispatch this+29), 2=projectile
    int32_t hitActionSub      = 0; // this+29 (116): valid if hitActionKind==1: 1=SendMeleeHit1, 2=SendMeleeHit2
    float   animFrame         = 0.0f; // this+7  (28)  : current frame, passed to the hit-list oracles
    int32_t weaponOrSkillAnimId = 0;  // this+24 (96)  : key passed to Anim_IsFrameInHitListA/B (see ambiguity above)
    EntityId attackTargetId;          // this+17/18 (68/72): network id of the current target (populated by
                                       // the combat AI, OUT OF SCOPE — this module only reads it to
                                       // revalidate the target's existence before emitting the hit)

    // --- Fall/knockback (0x582145..0x5822B6), active while fallActive, capped at
    // fallLandCounter < fallLandThreshold. Fall velocity + offset are SEPARATE from the
    // monster's real world position: the original binary's true MonsterEntity.x/y/z lives
    // at record+32/36/40 (confirmed by Fx_SpawnAttackProjectile @0x5825b5-0x582601, which
    // reads *(this+32)/*(this+36)/*(this+40) as the projectile origin), NOT record+240.
    // this+240..251 (this+60/61/62 below) is an ENTIRELY SEPARATE field, never read or
    // written by Fx_SpawnAttackProjectile or by the renderer -- fixes a prior-wave
    // documentation error claiming "this+240 == MonsterEntity.x/y/z". fallOffX/Y/Z actually
    // behaves as an ABSOLUTE flight position (likely seeded from mon.x/y/z by
    // Char_MotionTick_Knockback at knockback start, OUT OF SCOPE, never observed here), not
    // a small relative delta: the ground probe and landing sound use fallOffX/Y/Z as-is as
    // ABSOLUTE coordinates (confirmed by raw disassembly 0x582206..0x58223e, not just the
    // Hex-Rays pseudocode) -- see bug fixed in EntityLifecycleTick.cpp::UpdateMonster (audit
    // steps 5-8, 2026-07-14): the previous version of this file mistakenly passed
    // mon.x/mon.z (never updated here) to the probe instead of fallOffX/fallOffZ.
    bool    fallActive        = false; // this+53 (212)
    int32_t fallLandCounter   = 0;     // this+58 (232): incremented on each detected landing
    int32_t fallLandThreshold = 1;     // this+57 (228): stop condition (this+58 < this+57)
    float   fallVelX = 0.0f, fallVelY = 0.0f, fallVelZ = 0.0f; // this+54/55/56 (216/220/224) — fallVelY accumulates -300*dt (gravity)
    float   fallOffX = 0.0f, fallOffY = 0.0f, fallOffZ = 0.0f; // this+60/61/62 (240/244/248) — integrated offset
    float   fallGroundClearance = 0.0f; // this+59 (236): offset subtracted from ground height at clamp
};

// "NPC/effect" extension — physics tick fields absent from NpcEntity.
// Original offsets (Fx_GibUpdate 0x583CD0, real stride dword_17AB534 = 0x98 = 152 bytes
// = 38 dwords) given in comments.
struct NpcTickExt {
    bool  gibActive = false;             // this+21 (84): arms the fall physics
    float velX = 0.0f, velY = 0.0f, velZ = 0.0f; // this+28/29/30 (112/116/120) — velY accumulates -300*dt
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f; // this+32/33/34 (128/132/136) — integrated position (NOT NpcEntity.*, which has no x/y/z)
    float extraRate  = 0.0f;             // this+31 (124): additional rate (likely rotation/scale, unidentified)
    float extraValue = 0.0f;             // this+36 (144): accumulator integrated by extraRate*dt
};

// Extension storage, indexed like g_World.monsters / g_World.npcs. Grows lazily
// (EnsureMonsterExtCapacity/EnsureNpcExtCapacity, called at the top of UpdateMonster/TickNpcEffect).
inline std::vector<MonsterTickExt> g_MonsterTickExt;
inline std::vector<NpcTickExt>     g_NpcTickExt;

// Resets a slot's extension (to be called by the consolidation agent from
// EntityManager::OnSpawnMonster/OnSpawnNpc when a new monster/NPC takes ownership
// of an array index — prevents stale fallOffset/velocity from "leaking" onto the
// new entity). No-op if the index is out of bounds (grows first if needed).
void ResetMonsterTickExt(int monsterIndex);
void ResetNpcTickExt(int npcIndex);

// Opaque callbacks to out-of-scope subsystems (see head comment). A null hook =
// no-op / conservative default value (see each call site in the .cpp). Same
// convention as Game/InGameTickFlow.h::InGameTickFlowHost.
struct EntityLifecycleTickHost {
    // --- UpdateMonster: model-swap timers -----------------------------------
    // ModelObj_GetSubObjectCount 0x4D7080. auraSlot in {0,1,2} <-> {unk_B63174,
    // unk_B63208, unk_B5A180} (3 distinct models in the original binary).
    std::function<float(int auraSlot)> GetAuraSwapDuration;

    // --- UpdateMonster: hit window ----------------------------------------------
    std::function<bool(int32_t animOrWeaponId, float frame)> IsFrameInHitListA; // 0x559F80 (state 5=AttackA)
    std::function<bool(int32_t animOrWeaponId, float frame)> IsFrameInHitListB; // 0x55A000 (state 7=AttackB)
    // See the gap documented at file top (ambiguous nature of this+24).
    std::function<bool()> IsAttackTargetBypassActive;
    std::function<void(int monsterIndex)> SendMeleeHit1;            // Combat_SendMeleeHit1 0x5823E0
    std::function<void(int monsterIndex)> SendMeleeHit2;            // Combat_SendMeleeHit2 0x582480
    std::function<void(int monsterIndex)> SpawnAttackProjectile;    // Fx_SpawnAttackProjectile 0x582530 (state 5, hitActionKind==2)
    std::function<void(int monsterIndex)> SpawnAttackProjectileAlt; // Fx_SpawnAttackProjectileAlt 0x582A10 (state 7)

    // --- UpdateMonster: final FSM dispatch (switch this+6) -------------------------
    std::function<void(int monsterIndex, float dt)> DispatchMotionTick; // Char_MotionTick_* 0x582D40..0x5832E0

    // --- UpdateMonster + TickNpcEffect: terrain/sound --------------------------------
    // MapColl_GetGroundHeight 0x697130. Returns true if a ground height was found
    // under (x,z) (outGroundY filled); probeY = probe height (pos.y+20 in the
    // original, see both function bodies). IMPORTANT (re-verified by raw
    // disassembly, not just pseudocode, audit steps 5-8 2026-07-14): x/z come
    // from the EXTENSION's tick fields (ext.fallOffX/Z for UpdateMonster, ext.posX/Z
    // for TickNpcEffect), NOT MonsterEntity.x/z / NpcEntity.x/z; probeY uses this
    // tick's PRE-update value (before gravity/velocity integration), not the
    // already-integrated value -- see fixes applied in EntityLifecycleTick.cpp.
    std::function<bool(float x, float z, float probeY, float& outGroundY)> GetGroundHeight;
    // Snd3D_PlayPositional 0x4DA450: played only on the 1st detected landing
    // (fallLandCounter going from 0 to 1) in UpdateMonster. Fx_GibUpdate calls NO
    // sound at all (verified in the pseudocode — asymmetry faithful to the original).
    std::function<void(float x, float y, float z)> PlayLandingSound;
};

// The mission's 5 functions. Signatures aligned with the matching hooks in
// Game/InGameTickFlow.h::InGameTickFlowHost (DespawnStalePlayer, UpdateMonster,
// RespawnMonsterAfterKnockback, TickNpcEffect, CleanupStaleNpcEffect) for direct
// wiring by the consolidation agent, e.g.:
//   host.DespawnStalePlayer = [](int idx, float) { DespawnStalePlayer(g_World, idx); };

// sub_55D720 (PlayerArray_SlotDestruct). Deactivates g_World.players[playerIndex]
// (active = false). Touches NO other field (id/timestamp/body kept as-is, faithful
// to `*this = 0` which clears ONLY the first dword) — a future FindOrAddPlayer will
// cleanly overwrite the slot on next reuse (see GameState.cpp::FindOrAdd). No-op if
// playerIndex is out of bounds.
void DespawnStalePlayer(GameWorld& world, int playerIndex);

// sub_580550 (Char_RespawnAfterKnockback). See the naming gap at file top: actual
// behavior identical to DespawnStalePlayer/CleanupStaleNpcEntity (deactivates the
// slot). No-op if monsterIndex is out of bounds.
void RespawnMonsterAfterKnockback(GameWorld& world, int monsterIndex);

// sub_583390. Deactivates g_World.npcs[npcIndex]. No-op if npcIndex is out of bounds.
void CleanupStaleNpcEntity(GameWorld& world, int npcIndex);

// Char_Update 0x581E10 (monster tick, called when g_GameTimeSec - timestamp <= 7.5s).
// Operates on world.monsters[monsterIndex] + g_MonsterTickExt[monsterIndex] (grown as
// needed). No-op if !world.monsters[monsterIndex].active or index out of bounds
// (faithful to the function head guard `if (*(_DWORD*)this)`, doubled by the active
// filter already done by caller InGameTickFlow_Update — re-checked here for defensive
// robustness).
void UpdateMonster(GameWorld& world, int monsterIndex, float dt, const EntityLifecycleTickHost& host);

// Fx_GibUpdate 0x583CD0 (NPC/effect tick, called when g_GameTimeSec - timestamp <= 7.5s).
// Operates on world.npcs[npcIndex] + g_NpcTickExt[npcIndex] (grown as needed). Fall
// physics (gravity -300 u/s², semi-implicit integration, ground snap) — strictly
// no-op while ext.gibActive==false (faithful to the original double guard `this[0]`
// + `this+21`).
void TickNpcEffect(GameWorld& world, int npcIndex, float dt, const EntityLifecycleTickHost& host);

} // namespace ts2::game

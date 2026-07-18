// Game/PlayerCmdController.cpp — implementation of the intent layer (op 0x0F).
//
// THE COMMON MOLD of the builders (established via Hex-Rays MCP idaTs2 decompilation of the
// 8 ported functions — Net_QueueRespawnMove 0x5117A0, Net_QueueMoveResume 0x511870,
// Net_QueueMoveTo 0x5119B0, Net_QueueRunTo 0x511B00, Net_QueueAttack5/6/7 0x511EF0/
// 0x5120A0/0x512250, Player_QueueSkill_op32 0x5134D0):
//
//   if (+0xC990 != 0) abort                          // "command in flight" latch
//   if (!Char_IsAttackAction(g_LocalPlayerSheet)) abort
//   old = memcpy(g_SelfMoveStateBlock 0x1687324, 72)
//   new.animSlot    = old.animSlot ? old.animSlot : 2*Weapon_ClassFromField112(...)
//   new.actionState = <builder-specific constant>
//   new.animFrame   = 0.0 (except RunTo)
//   new.pos/dest/facing/targetFacing = <builder-specific>
//   new.{targetKind..levelBonus} = <builder-specific> ; new.combatFlag = 0
//   Net_SendPacket_Op15(&g_AutoPlayMgr, new)         // 0x4B4870, 81-byte frame
//   +0xC994 = g_GameTimeSec
//   [+0xC990 = 1]                                    // EXCEPT RunTo / MoveTo / MoveResume
//   [memcpy(g_SelfMoveStateBlock, new, 72)]          // RunTo ONLY
//   [if self actionState in {2,32} -> actionState=1, animFrame=0]  // EXCEPT RunTo / Respawn
//
// PROVEN ASYMMETRIES — DO NOT SMOOTH OVER (each verified against the decompile, not deduced):
//  - Net_QueueRunTo is the ONLY one to test the connection (MEMORY[0x8156A8] = g_NetClient+8 =
//    loginReady) @0x511B31, the ONLY one to rewrite the self block @0x511C5C, the ONLY one to
//    carry animFrame over when old.actionState==2 @0x511B88, and the ONLY one to pass a3..a8
//    through unchanged.
//  - RunTo sets facing = old.facing (+0x24) and targetFacing = angle ; ATTACKS and SKILL set
//    the angle in BOTH fields (@0x511FFE/@0x512004).
//  - MoveTo/MoveResume set facing = targetFacing = old.targetFacing (+0x28, not +0x24).
//  - MoveTo : pos = dest = a2. MoveResume : pos = old.pos, dest = {0,0,0}.
//    RespawnMove : pos = a2, dest = {0,0,0}.
//  - RunTo / MoveTo / MoveResume do NOT set the latch ; Attack5/6/7, Skill and
//    RespawnMove do set it.
//  - RespawnMove tests NEITHER the latch NOR Char_IsAttackAction : its only guard is
//    self actionState == 12 (dead) @0x5117B0 ; it does not read the self block (built from
//    scratch) and does NOT do the 2/32 fixup.
//  - Attack5/6/7 force animSlot = 2*class+1 (odd) @0x511F8C — never old.animSlot — and add an
//    anti-repeat guard @0x511F5F.
//
// "SELF" GLOBAL CORRESPONDENCE (all ALREADY modeled, no new global created):
//   g_EntityArray 0x1687234 -> g_World.players[0] ; body = entity+0x18 = 0x168724C
//   g_SelfMoveStateBlock      0x1687324 = body+216 (kPMoveState)
//   g_SelfActionState         0x1687328 = body+220 = move-state+4
//   g_SelfAnimFrame           0x168732C = body+224 = move-state+8
//   flt_1687330 (world pos)   0x1687330 = body+228 = move-state+12
//   g_SelfAttackOrder_Action  0x1687350 = body+260 = move-state+44
//   g_SelfAttackOrder_GridX   0x1687354 = body+264 ; _GridY 0x1687358 = body+268
//   g_EquipMain               0x16731D8 = g_World.self.equip ; +112 = equip[7].itemId
//                             (independently cross-checked by Game/SkillCombat.cpp:681-682)
#include "Game/PlayerCmdController.h"

#include "Game/GameState.h"       // g_World (Self()/self.equip/gameTimeSec)
#include "Game/EntityManager.h"   // kPMoveState / kPMoveStateLen (move-state block of the body)
#include "Game/GameDatabase.h"    // GetItemInfo / ItemInfo::typeCode (+188)
#include "Game/ClientRuntime.h"   // g_Client : single storage for dword_1675B00 / flt_1675B04
#include "Net/NetClient.h"        // GlobalNetClient / loginReady (g_NetClient 0x8156A0)
#include "Net/SendPackets.h"      // Net_SendPacket_Op15 (0x4B4870)
#include "Net/Rng.h"              // DefaultRng (Rng_Next 0x7603FD)

#include <cstring>
#include <cmath>

namespace ts2::game {

// The move-state block must fit within the modeled body (600 bytes) — guarantees the memcpy
// calls below stay in bounds without a runtime check (the binary addresses 0x1687324
// unbounded).
static_assert(kPMoveState + kPMoveStateLen <= 600,
              "the move-state block must fit within PlayerEntity::body (600 bytes)");
static_assert(kPMoveStateLen == sizeof(MoveCmdBlock),
              "MoveCmdBlock must exactly cover the move-state block (72 bytes)");

namespace {

// --- self "attack order" offsets, relative to the body (cf. header banner).
// Written by the server (raw body copy at spawn), read by the anti-repeat guard of
// Net_QueueAttack5/6/7 @0x511F5F. Not modeled elsewhere -> anchored local constants.
constexpr size_t kPAttackOrderAction = 260; // g_SelfAttackOrder_Action 0x1687350
constexpr size_t kPAttackOrderGridX  = 264; // g_SelfAttackOrder_GridX  0x1687354
constexpr size_t kPAttackOrderGridY  = 268; // g_SelfAttackOrder_GridY  0x1687358

inline int32_t RdI32(const uint8_t* b, size_t o) { int32_t v; std::memcpy(&v, b + o, 4); return v; }

// g_SelfActionState 0x1687328 = move-state+4 (LIVE, not the local copy).
int32_t SelfActionState() {
    return RdI32(g_World.Self().body.data(), kPMoveState + 4);
}

// Char_IsAttackAction 0x558A50 : `switch (*(this + 1784))` with this = g_LocalPlayerSheet
// 0x1685748 -> 0x1685748 + 1784*4 = 0x1687328 = g_SelfActionState (= move-state+4).
// It is therefore NOT a sheet read: it's the self action code.
bool IsAttackAction(int32_t actionState) {
    switch (actionState) {                              /*0x558a7e*/
    case 1: case 2: case 5: case 6: case 7:
    case 0xE: case 0xF: case 0x1F: case 0x20: case 0x28: case 0x40:
        return true;                                    /*0x558a85*/
    default:
        return false;                                   /*0x558a8c*/
    }
}

// Weapon_ClassFromField112 0x4CC870 : weapon class 1..3 (0 = none) from the equipped
// weapon's record type (+188) (`MobDb_GetEntry(&mITEM, *(a2 + 112))`
// @0x4CC88D, a2 = g_EquipMain -> +112 = 7th slot of 16 bytes = equip[7].itemId).
// ASSUMED GAP: the binary also caches the record in g_EquipSnapshotScratch+28
// (`*(this + 7) = record` @0x4CC88D). This scratch (0x8E719C) is not modeled and no
// ClientSource consumer reads it — the side effect is therefore omitted.
// TODO [ancre 0x4CC870]: port the cache if a reader of 0x8E719C appears.
int32_t WeaponClass() {
    const ItemInfo* rec = GetItemInfo(g_World.self.equip[7].itemId);
    if (!rec) return 0;                                 /*0x4cc893*/
    return WeaponClassFromTypeCode(rec->typeCode);      /*0x4cc8be — record+188 (shared helper)*/
}

// Reads the self block (Crt_Memcpy(local, g_SelfMoveStateBlock, 0x48u)).
MoveCmdBlock ReadSelfBlock() {
    MoveCmdBlock b{};
    std::memcpy(&b, g_World.Self().body.data() + kPMoveState, kPMoveStateLen);
    return b;
}

// Writes the self block (Crt_Memcpy(g_SelfMoveStateBlock, new, 0x48u)) — RunTo ONLY.
void WriteSelfBlock(const MoveCmdBlock& b) {
    std::memcpy(g_World.Self().body.data() + kPMoveState, &b, kPMoveStateLen);
}

// Common end-of-function fixup @0x512083 (Attack5/6/7), @0x511ADC (MoveTo), @0x51198E
// (MoveResume), @0x513630 (Skill): if the self action is "run" (2) or "skill" (32), it
// is brought back to "walk" (1) and the frame reset to 0. Writes to the LIVE block
// (g_SelfActionState / g_SelfAnimFrame), not the local copy.
void FixupSelfActionState() {
    uint8_t* b = g_World.Self().body.data();
    const int32_t s = RdI32(b, kPMoveState + 4);
    if (s == 2 || s == 32) {
        const int32_t one = 1;   std::memcpy(b + kPMoveState + 4, &one, 4);
        const float   zero = 0.0f; std::memcpy(b + kPMoveState + 8, &zero, 4);
    }
}

inline void CopyVec3(float dst[3], const float src[3]) {
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
}

} // namespace

// Math_AngleBetween2D 0x53FB20 — identical transcription to the ones in
// Game/AnimationTick.cpp:802 / Game/GroundAuraWorldObjectTick.cpp:185 (verified line by
// line against the decompile: same anchors, same operation order).
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

// ---------------------------------------------------------------------------
// State — SINGLE STORAGE in the long-tail ClientRuntime (cf. header banner).
//   +51600 = 0x1669170+0xC990 = dword_1675B00 ; +51604 = 0x1669170+0xC994 = flt_1675B04.
// Var() and VarF() index the SAME container by ADDRESS: 0x1675B00 (int) and 0x1675B04
// (float) are two distinct slots, no aliasing between them.
// ---------------------------------------------------------------------------
int32_t& PlayerCmdController::Busy()        { return g_Client.Var(0x1675B00); }
float&   PlayerCmdController::LastCmdTime() { return g_Client.VarF(0x1675B04); }

// ---------------------------------------------------------------------------
// Send — Net_SendPacket_Op15 0x4B4870.
// The binary addresses g_NetClient 0x8156A0 as a global and sends unconditionally ;
// on the C++ side the global pointer may be null until a connection has been started
// (cf. Net/NetClient.h:67) -> survival guard, with no binary equivalent.
// ---------------------------------------------------------------------------
void PlayerCmdController::Send(const MoveCmdBlock& blk) {
    net::NetClient* c = net::GlobalNetClient();
    if (!c) return;
    ts2::net::Net_SendPacket_Op15(*c, &blk);            /*0x511c37 (RunTo) etc.*/
}

// ---------------------------------------------------------------------------
// Player_ResetCombatState 0x50F6A0 — resets the latch to zero on ENTERING THE WORLD
// (Pkt_SpawnCharacter @0x4648F2, self gate WITHIN the "new slot" branch). The per-packet
// ack of the current game is a DISTINCT path (@0x464BF0, mode 3 + self), cf. the .h.
// ---------------------------------------------------------------------------
void PlayerCmdController::ResetCombatState() {
    Busy() = 0;                                         /*0x50f6e7 : *(this+51600) = 0*/
    // TODO [ancre 0x50F6A0]: the binary zeroes the ENTIRE combat/action block
    // 51480->53184 (~150 fields: +51480 @0x50F6AC, +51576 @0x50F6B9, +51608 @0x50F6F4,
    // +51620 @0x50F701, quests/morph/counters, up to Crt_Memset(this+53164,0,0x14)
    // @0x50FED9). These fields are not modeled here and have no ClientSource consumer
    // -> left unported rather than invented. Side effects already covered elsewhere:
    // BGM gate g_BgmEnabled @0x50F76E (SceneManager::LoadZoneBgm) and
    // Net_SendOp64 @0x50F746 (InGameTickFlow::SendPendingTargetPoll).
}

// ---------------------------------------------------------------------------
// Net_QueueRunTo 0x511B00 — actionState 2 (running). The most atypical builder.
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueRunTo(const float dest[3], int32_t targetKind,
                                     int32_t targetId1, int32_t targetId2,
                                     int32_t activeSkillId, int32_t level,
                                     int32_t levelBonus) {
    // @0x511b31 : MEMORY[0x8156A8] && !*(this+51600) && Char_IsAttackAction(...)
    // MEMORY[0x8156A8] = g_NetClient+8 = nc.loginReady. RunTo is the ONLY one to test it.
    net::NetClient* c = net::GlobalNetClient();
    if (!c || !c->loginReady) return false;
    if (Busy()) return false;
    if (!IsAttackAction(SelfActionState())) return false;

    const MoveCmdBlock old = ReadSelfBlock();           /*0x511b4a*/
    MoveCmdBlock blk{};
    blk.animSlot    = old.animSlot ? old.animSlot       /*0x511b74*/
                                   : 2 * WeaponClass(); /*0x511b69*/
    blk.actionState = 2;                                /*0x511b7a*/
    // RunTo is the ONLY one to carry the frame over when running was already in progress.
    blk.animFrame   = (old.actionState == 2) ? old.animFrame : 0.0f;  /*0x511b88/@0x511b97*/
    CopyVec3(blk.pos, old.pos);                         /*0x511ba0..0x511baf*/
    CopyVec3(blk.dest, dest);                           /*0x511bb7..0x511bc9*/
    blk.facing       = old.facing;                      /*0x511bcf (v30 = old+0x24)*/
    blk.targetFacing = AngleBetween2D(old.pos[0], old.pos[2], dest[0], dest[2]); /*0x511bfd*/
    blk.targetKind    = targetKind;                     /*0x511c03*/
    blk.targetId1     = targetId1;                      /*0x511c09*/
    blk.targetId2     = targetId2;                      /*0x511c0f*/
    blk.activeSkillId = activeSkillId;                  /*0x511c15*/
    blk.level         = level;                          /*0x511c1b*/
    blk.levelBonus    = levelBonus;                     /*0x511c21*/
    blk.combatFlag    = 0;                              /*0x511c24*/

    Send(blk);                                          /*0x511c37*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x511c48*/
    WriteSelfBlock(blk);                                /*0x511c5c — RunTo ONLY*/
    // NB: neither the latch (+51600) nor the 2/32 fixup here — verified against the full decompile.
    return true;
}

// ---------------------------------------------------------------------------
// Net_QueueMoveTo 0x5119B0 — actionState 1. pos AND dest = a2.
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueMoveTo(const float dest[3]) {
    if (Busy()) return false;                           /*0x5119c5*/
    if (!IsAttackAction(SelfActionState())) return false; /*0x5119d3*/

    const MoveCmdBlock old = ReadSelfBlock();           /*0x5119ec*/
    MoveCmdBlock blk{};
    blk.animSlot    = old.animSlot ? old.animSlot       /*0x511a16*/
                                   : 2 * WeaponClass(); /*0x511a0b*/
    blk.actionState = 1;                                /*0x511a1c*/
    blk.animFrame   = 0.0f;                             /*0x511a28*/
    CopyVec3(blk.pos, dest);                            /*0x511a33..0x511a48 — pos = a2*/
    CopyVec3(blk.dest, blk.pos);                        /*0x511a51..0x511a5d — dest = pos*/
    blk.facing       = old.targetFacing;                /*0x511a63 (v27 = old+0x28)*/
    blk.targetFacing = old.targetFacing;                /*0x511a69*/
    blk.targetKind   = 0;                               /*0x511a6c*/
    blk.targetId1    = -1;                              /*0x511a73*/
    // targetId2/activeSkillId/level/levelBonus/combatFlag = 0 (@0x511a7a..0x511a96)

    Send(blk);                                          /*0x511aa9*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x511aba*/
    FixupSelfActionState();                             /*0x511adc*/
    return true;
}

// ---------------------------------------------------------------------------
// Net_QueueMoveResume 0x511870 — actionState 1, resume from the memorized heading.
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueMoveResume() {
    if (Busy()) return false;                           /*0x511885*/
    if (!IsAttackAction(SelfActionState())) return false; /*0x511893*/

    const MoveCmdBlock old = ReadSelfBlock();           /*0x5118ac*/
    MoveCmdBlock blk{};
    blk.animSlot    = old.animSlot ? old.animSlot       /*0x5118d6*/
                                   : 2 * WeaponClass(); /*0x5118cb*/
    blk.actionState = 1;                                /*0x5118dc*/
    blk.animFrame   = 0.0f;                             /*0x5118e8*/
    CopyVec3(blk.pos, old.pos);                         /*0x5118f1..0x511900 (v4[3..5])*/
    // dest = {0,0,0} @0x511905..0x51190f (already zero from MoveCmdBlock's init)
    blk.facing       = old.targetFacing;                /*0x511915 (v5 = old+0x28)*/
    blk.targetFacing = old.targetFacing;                /*0x51191b*/
    blk.targetKind   = 0;                               /*0x51191e*/
    blk.targetId1    = -1;                              /*0x511925*/
    // memset(&v3[13], 0, 20) @0x51192c : targetId2..combatFlag = 0 (already zero)

    Send(blk);                                          /*0x51195b*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x51196c*/
    FixupSelfActionState();                             /*0x51198e*/
    return true;
}

// ---------------------------------------------------------------------------
// Net_QueueRespawnMove 0x5117A0 — actionState 0. Guard: self dead (actionState 12).
// Builds the block FROM SCRATCH (no read of the self block).
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueRespawnMove(const float pos[3]) {
    if (SelfActionState() != 12) return false;          /*0x5117b0*/

    MoveCmdBlock blk{};
    blk.animSlot    = 0;                                /*0x5117b7*/
    blk.actionState = 0;                                /*0x5117be*/
    blk.animFrame   = 0.0f;                             /*0x5117c7*/
    CopyVec3(blk.pos, pos);                             /*0x5117cf..0x5117e1 — pos = a2*/
    // dest = {0,0,0} @0x5117e6..0x5117f0 (already zero)
    // Heading rolled at random: (float)(Rng_Next() % 360), SAME value in both fields.
    const float ang = static_cast<float>(net::DefaultRng().NextMod(360)); /*0x511806*/
    blk.facing       = ang;
    blk.targetFacing = ang;                             /*0x51180c*/
    blk.targetKind   = 0;                               /*0x51180f*/
    blk.targetId1    = -1;                              /*0x511816*/
    // targetId2..combatFlag = 0 (@0x51181d..0x511839)

    Send(blk);                                          /*0x511849*/
    Busy() = 1;                                         /*0x511851*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x511864*/
    // NB: no 2/32 fixup here — verified against the full decompile.
    return true;
}

// ---------------------------------------------------------------------------
// Net_QueueAttack5/6/7 0x511EF0 / 0x5120A0 / 0x512250.
// All three decompiles are IDENTICAL except for the actionState constant (5/6/7):
// shared, parametrized implementation, strictly faithful.
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueAttackImpl(int32_t actionState, const float dest[3],
                                          int32_t targetKind, int32_t gridX, int32_t gridY) {
    if (Busy()) return false;                           /*0x511f05*/
    if (!IsAttackAction(SelfActionState())) return false; /*0x511f13*/

    // Anti-repeat guard @0x511F5F: if already attacking (action 5..7) with EXACTLY the
    // same order (same kind + same grid cell), silently abort.
    const uint8_t* b = g_World.Self().body.data();
    const int32_t s = SelfActionState();
    if (s >= 5 && s <= 7
        && RdI32(b, kPAttackOrderAction) == targetKind
        && RdI32(b, kPAttackOrderGridX)  == gridX
        && RdI32(b, kPAttackOrderGridY)  == gridY) {
        return false;
    }

    const MoveCmdBlock old = ReadSelfBlock();           /*0x511f71*/
    MoveCmdBlock blk{};
    // FORCED to 2*class+1 (odd): unlike the other builders, the old animSlot is NEVER
    // carried over here.
    blk.animSlot    = 2 * WeaponClass() + 1;            /*0x511f8c*/
    blk.actionState = actionState;                      /*0x511f92 (5) / 6 / 7*/
    blk.animFrame   = 0.0f;                             /*0x511f9e*/
    CopyVec3(blk.pos, old.pos);                         /*0x511fa7..0x511fb6*/
    CopyVec3(blk.dest, dest);                           /*0x511fbe..0x511fd0*/
    // The angle goes into BOTH heading fields (unlike RunTo).
    const float ang = AngleBetween2D(old.pos[0], old.pos[2], dest[0], dest[2]); /*0x511ffe*/
    blk.facing       = ang;
    blk.targetFacing = ang;                             /*0x512004*/
    blk.targetKind = targetKind;                        /*0x51200a*/
    blk.targetId1  = gridX;                             /*0x512010*/
    blk.targetId2  = gridY;                             /*0x512016*/
    // activeSkillId/level/levelBonus/combatFlag = 0 hardcoded (@0x512019..0x51202e):
    // the binary IGNORES its a6..a8 arguments on this builder.

    Send(blk);                                          /*0x512041*/
    Busy() = 1;                                         /*0x51204c*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x512062*/
    FixupSelfActionState();                             /*0x512083*/
    return true;
}

bool PlayerCmdController::QueueAttack5(const float dest[3], int32_t targetKind,
                                       int32_t gridX, int32_t gridY) {
    return QueueAttackImpl(5, dest, targetKind, gridX, gridY); /*0x511EF0*/
}

bool PlayerCmdController::QueueAttack6(const float dest[3], int32_t targetKind,
                                       int32_t gridX, int32_t gridY) {
    return QueueAttackImpl(6, dest, targetKind, gridX, gridY); /*0x5120A0*/
}

bool PlayerCmdController::QueueAttack7(const float dest[3], int32_t targetKind,
                                       int32_t gridX, int32_t gridY) {
    return QueueAttackImpl(7, dest, targetKind, gridX, gridY); /*0x512250*/
}

// ---------------------------------------------------------------------------
// Player_QueueSkill_op32 0x5134D0 — actionState 32 (skill cast).
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueSkill32(const float dest[3], int32_t skillId,
                                       int32_t level, int32_t elemResist) {
    // @0x5134f3 : if (*(this+51600) || !Char_IsAttackAction(...)) return 0;
    if (Busy() || !IsAttackAction(SelfActionState())) return false;

    const MoveCmdBlock old = ReadSelfBlock();           /*0x51350e*/
    MoveCmdBlock blk{};
    blk.animSlot    = old.animSlot ? old.animSlot       /*0x513538*/
                                   : 2 * WeaponClass(); /*0x51352d*/
    blk.actionState = 32;                               /*0x51353e*/
    blk.animFrame   = 0.0f;                             /*0x51354a*/
    CopyVec3(blk.pos, old.pos);                         /*0x513553..0x513562*/
    CopyVec3(blk.dest, dest);                           /*0x51356a..0x51357c*/
    const float ang = AngleBetween2D(old.pos[0], old.pos[2], dest[0], dest[2]); /*0x5135aa*/
    blk.facing       = ang;
    blk.targetFacing = ang;                             /*0x5135b0*/
    blk.targetKind = 0;                                 /*0x5135b3*/
    blk.targetId1  = -1;                                /*0x5135ba*/
    blk.targetId2  = 0;                                 /*0x5135c1*/
    blk.activeSkillId = skillId;                        /*0x5135cb (a6)*/
    blk.level         = level;                          /*0x5135d1 (a7)*/
    blk.levelBonus    = elemResist;                     /*0x5135d7 (a8)*/
    blk.combatFlag    = 0;                              /*0x5135da*/

    Send(blk);                                          /*0x5135ed*/
    Busy() = 1;                                         /*0x5135f8*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x51360e*/
    FixupSelfActionState();                             /*0x513630*/
    return true;
}

} // namespace ts2::game

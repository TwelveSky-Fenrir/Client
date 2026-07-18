// Game/PlayerCmdController.h — player INTENT layer (network opcode 0x0F).
//
// Ports the `g_PlayerCmdController` module (dword_1669170) of the binary: the
// Net_Queue*/Player_QueueSkill_* builders that translate a player intent (click-to-move,
// attack, skill, resume walking, respawn) into a 72-byte command block sent by
// Net_SendPacket_Op15 0x4B4870 (wire opcode 15 = 0x0F, total frame 81 bytes).
//
// This is THE path by which the character acts on the server side: without it, opcode
// 0x0F is never sent and the player can neither move, attack, nor cast a spell. All 74
// callers in the binary share the same mold (cf. .cpp):
//   Net_QueueRespawnMove  0x5117A0   Net_QueueMoveResume   0x511870
//   Net_QueueMoveTo       0x5119B0   Net_QueueRunTo        0x511B00
//   Net_QueueAttack5      0x511EF0   Net_QueueAttack6      0x5120A0
//   Net_QueueAttack7      0x512250   Player_QueueSkill_op32 0x5134D0
//
// Subset ported here = the 8 builders above (the only ones re-decompiled and proven for
// this wave). The ~66 others (Net_QueueAction3/4/9/10/13/15..18, Player_Queue*_opNN) follow
// the same mold but are NOT ported: their constants have not been verified.
// TODO [ancre 0x511C70..0x517A80]: port the rest after individual re-decompilation (each
// builder has its OWN asymmetries — cf. the .cpp banner, do not extrapolate).
//
// The "self" state read/written by these builders is not one more global: the
// g_SelfMoveStateBlock 0x1687324 block is ALREADY modeled, it is g_World.Self().body[216..288)
// (proof: g_EntityArray 0x1687234 + 0x18 (body) + 216 = 0x1687324 ; and body+228 =
// 0x1687330 = flt_1687330 = the local player's world position, cf. Game/EntityManager.cpp).
#pragma once
#include <cstdint>

namespace ts2::game {

// 72-byte command block — 1:1 mirror of the IDB's `TS2_MoveStateBlock` type (size 72),
// i.e. the raw payload copied to this+9 by Net_SendPacket_Op15 0x4B4870
// (`Crt_Memcpy(this + 9, a2, 0x48u)` @0x4B490F).
//
// FIDELITY NOTE: the "every outgoing char is emitted as 4 bytes LE" rule DOES NOT APPLY to
// opcode 0x0F — the block goes out as a raw 72-byte memcpy and contains no `char` field.
// The offsets below are those of the IDB type, re-validated field by field against the
// builders' stack frames (cf. .cpp).
struct MoveCmdBlock {
    int32_t animSlot      = 0;    // +0x00 animation slot (2*weapon_class, +1 for attacks)
    int32_t actionState   = 0;    // +0x04 internal action/motion code (the "opNN" of the IDB names)
    float   animFrame     = 0.0f; // +0x08 current animation frame
    float   pos[3]        = {};   // +0x0C source position
    float   dest[3]       = {};   // +0x18 target position
    float   facing        = 0.0f; // +0x24 current heading (deg)
    float   targetFacing  = 0.0f; // +0x28 target heading (deg) — Math_AngleBetween2D 0x53FB20
    int32_t targetKind    = 0;    // +0x2C target category
    int32_t targetId1     = 0;    // +0x30 target id/grid X
    int32_t targetId2     = 0;    // +0x34 target id/grid Y
    int32_t activeSkillId = 0;    // +0x38 active skill id
    int32_t level         = 0;    // +0x3C skill level
    int32_t levelBonus    = 0;    // +0x40 elemental bonus/resistance
    int32_t combatFlag    = 0;    // +0x44 always 0 in the 8 ported builders
};
static_assert(sizeof(MoveCmdBlock) == 72, "MoveCmdBlock must be exactly 72 bytes (op 0x0F payload)");

// Math_AngleBetween2D 0x53FB20: heading (deg 0..360) from point (a1,a2) to (a3,a4).
// Exposed here for the intent layer (the 36 command builders call it).
// DEDUPLICATION TO DO (out of scope for this front): two IDENTICAL file-local copies
// already exist, Game/AnimationTick.cpp:802 and Game/GroundAuraWorldObjectTick.cpp:185 (for
// FX). They should point here — cf. wave report, non-blocking (the 3 transcriptions are
// equivalent).
float AngleBetween2D(float a1, float a2, float a3, float a4);

// PlayerCmdController — g_PlayerCmdController 0x1669170.
//
// The binary addresses a huge struct (combat/action block 51480->53184, ~150 fields).
// Only the TWO fields actually read/written by the ported builders are modeled here ; the
// rest is not invented (cf. ResetCombatState's TODO).
//
// THE INTENT -> ACKNOWLEDGMENT CYCLE (established via data_refs on dword_1675B00 = the
// latch): the attack/skill/respawn builders set Busy() when sending, and the latch is
// released via THREE distinct paths — all proven, none optional:
//   1. Pkt_SpawnCharacter @0x464BF0 — mode 3 + self: THE PER-PACKET ACKNOWLEDGMENT, the
//      nominal path. ALREADY ported: Game/EntityManager.cpp:492 (and its twin opcode 0x15
//      Net/GameHandlers_Entity.cpp:48). This is what unblocks the player in-game.
//   2. Player_ResetCombatState @0x50F6E7 — new self slot (entering the world). Ported
//      below, wired from Game/EntityManager.cpp (new-slot branch).
//   3. Scene_InGameUpdate @0x52C8FF-@0x52C921 — TIMEOUT safety net, tested EVERY FRAME:
//      `if (Busy() && g_GameTimeSec - LastCmdTime() > 10.0) Busy() = 0;`
//      (threshold dbl_7EDB88 = 0x4024000000000000 = 10.0, verified at the byte level).
//      NOT PORTED — Scene/SceneManager.cpp is out of scope for this front ; flagged to the
//      orchestrator. Without it, a lost ack packet blocks the player indefinitely instead
//      of for 10 s.
class PlayerCmdController {
public:
    // --- Intent -> acknowledgment cycle ------------------------------------------
    // Player_ResetCombatState 0x50F6A0. UNIQUE proven caller (xrefs_to = 1):
    // Pkt_SpawnCharacter 0x4646C0 @0x4648F2, gate `var_2B0 == 0` (self) INSIDE the
    // "new slot" branch — i.e. when entering the world, NOT on every packet (the
    // per-packet ack is path 1 above).
    void ResetCombatState();

    // --- Intent builders (each one = a Net_Queue* from the binary) -------------------
    // All return true if the packet was actually sent, false if a guard cut it off.

    // Net_QueueRunTo 0x511B00 (actionState=2) — run order toward `dest`.
    // ONLY builder to (a) test the connection (MEMORY[0x8156A8] @0x511B31), (b) rewrite
    // the self block (@0x511C5C), (c) NOT set `busy`, (d) carry animFrame over.
    bool QueueRunTo(const float dest[3], int32_t targetKind, int32_t targetId1,
                    int32_t targetId2, int32_t activeSkillId, int32_t level,
                    int32_t levelBonus);

    // Net_QueueMoveTo 0x5119B0 (actionState=1) — move to an explicit position.
    // The binary IGNORES its a3..a8 arguments (targetKind=0 / targetId1=-1 hardcoded
    // @0x511A6C/@0x511A73): the signature therefore does not expose them.
    bool QueueMoveTo(const float dest[3]);

    // Net_QueueMoveResume 0x511870 (actionState=1) — resume walking from the memorized
    // heading. The binary only takes `this` (no destination).
    bool QueueMoveResume();

    // Net_QueueRespawnMove 0x5117A0 (actionState=0) — respawn. Sole guard: self
    // actionState == 12 (dead). Heading rolled at random (Rng_Next()%360 @0x511806).
    // a3..a8 ignored by the binary (targetKind=0 / targetId1=-1 hardcoded).
    bool QueueRespawnMove(const float pos[3]);

    // Net_QueueAttack5/6/7 0x511EF0 / 0x5120A0 / 0x512250 — identical except for the
    // actionState constant (verified by decompiling all three). a6..a8 ignored (0 hardcoded).
    bool QueueAttack5(const float dest[3], int32_t targetKind, int32_t gridX, int32_t gridY);
    bool QueueAttack6(const float dest[3], int32_t targetKind, int32_t gridX, int32_t gridY);
    bool QueueAttack7(const float dest[3], int32_t targetKind, int32_t gridX, int32_t gridY);

    // Player_QueueSkill_op32 0x5134D0 (actionState=32) — skill cast.
    // a3..a5 ignored (targetKind=0 / targetId1=-1 / targetId2=0 hardcoded); a6..a8 =
    // skillId / level / elemental resistance.
    bool QueueSkill32(const float dest[3], int32_t skillId, int32_t level, int32_t elemResist);

    // --- State (proven fields of struct 0x1669170) ---------------------------------
    // SINGLE STORAGE — DO NOT DUPLICATE INTO MEMBERS. The binary also addresses these two
    // fields IN ABSOLUTE TERMS (the big 0x1669170 block overlaps IDB-named "globals"):
    //   g_PlayerCmdController+51600 = 0x1669170+0xC990 = dword_1675B00 (latch)
    //   g_PlayerCmdController+51604 = 0x1669170+0xC994 = flt_1675B04   (timestamp)
    // Independently cross-checked: Player_ResetCombatState's *(this+40996) = 0x1669170+0xA024
    // = 0x1673194 = g_LocalElement, itself also a "global". The binary has only ONE storage.
    // dword_1675B00 is ALREADY modeled by the ClientRuntime long tail — duplicating it into a
    // member is this project's classic mistake (cf. Net/NetClient.h:110, g_LocalElement
    // duplicated -> started at 0 at handshake) and here the latch would NEVER clear, because
    // acknowledgments write the Var, not the member.
    //
    // These accessors therefore point at the SAME slot as g_Client.Var(0x1675B00) /
    // g_Client.VarF(0x1675B04). Intended side effect: the TWO acknowledgments already
    // written but so far WITHOUT ANY READER (Game/EntityManager.cpp:492 @0x464BF0, mode 3
    // self, and Net/GameHandlers_Entity.cpp:48, opcode 0x15) finally become LIVE.
    int32_t& Busy();          // +51600 == dword_1675B00
    float&   LastCmdTime();   // +51604 == flt_1675B04

private:
    // Common core: sends `blk` via Net_SendPacket_Op15 on net::GlobalNetClient().
    void Send(const MoveCmdBlock& blk);

    // Shared impl of Net_QueueAttack5/6/7 (decompiles IDENTICAL except for the actionState
    // constant — 5 @0x511F92, 6 @0x512142, 7 @0x5122F2).
    bool QueueAttackImpl(int32_t actionState, const float dest[3], int32_t targetKind,
                         int32_t gridX, int32_t gridY);
};

// Single instance — g_PlayerCmdController 0x1669170 (the binary has only one, addressed
// globally by the 74 builders via `mov ecx, offset g_PlayerCmdController`).
inline PlayerCmdController g_PlayerCmd;

} // namespace ts2::game

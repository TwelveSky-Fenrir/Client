// Game/CombatSystem.h — Combat resolution on the TwelveSky2 client side.
// CLEAN (not byte-exact) C++ rewrite of:
//   - cGameData_ApplyCombatResult (0x55A380): PURE applicator of the incoming combat
//     result packet (opcode 21 = 0x15, 76 bytes) -> HP delta on the right entity.
//   - Combat_QueueMeleeAttack (0x573130) / Combat_QueueSkillAction (0x573200):
//     builders of the OUTGOING action packet (opcode 18 = 0x12, 76-byte payload).
// Source of truth = Docs/TS2_GAMEPLAY_LOGIC.md §5 + disassembly (IDB RE/TwelveSky2.exe.i64)
// + RE/net_handler_notes.md (Pkt_OnCombatResult 0x468340).
//
// The client is server-authoritative: this module does NOT invent any damage. It only
// APPLIES a result computed by the server (HP delta, death flag) and BUILDS action
// requests. The visual/audio/text effects (Fx_*, Snd3D_*, Msg_AppendSystemLine) of the
// original binary belong to the render/audio/UI layers and are deliberately out of scope
// for GameState — documented but not reproduced here.
#pragma once
#include <cstdint>
#include "Game/GameState.h"

namespace ts2::game {

// ---------------------------------------------------------------------------
// Combat RESULT packet — INCOMING opcode 21 (0x15), 76 bytes = 19 int32.
// EXACT layout observed in cGameData_ApplyCombatResult (indices a2[0..18]) and
// Docs/TS2_GAMEPLAY_LOGIC.md §5.3/§5.6 ("CombatResult"). The server computes everything;
// the client applies appliedDmg1 (a2[16]) and appliedDmg2 (a2[18]).
// ---------------------------------------------------------------------------
struct CombatPacket {
    // a2[0]: result type = application branch.
    //   1,2 = PLAYER victim (2 components); 3 = MONSTER victim (component 1 only);
    //   4   = player guard/block (component 1 only).
    int32_t  resultType  = 0;
    EntityId attacker;            // a2[1..2]  attacker netID hi/lo
    EntityId victim;              // a2[3..4]  victim netID hi/lo
    float    impactX = 0.0f;      // a2[5]     world impact point X
    float    impactY = 0.0f;      // a2[6]                        Y
    float    impactZ = 0.0f;      // a2[7]                        Z
    int32_t  kind        = 0;     // a2[8]     1=melee, 2=skill
    int32_t  skillId     = 0;     // a2[9]     (==78 -> no sound client-side)
    int32_t  reserved10  = 0;     // a2[10] (+40) reserved
    int32_t  reserved11  = 0;     // a2[11] (+44) reserved
    int32_t  weaponHitId = 0;     // a2[12] (+48) 0 = MISS (no HP touched)
    int32_t  critMask    = 0;     // a2[13] (+52) bit0=HP crit, bit1=crit on 2nd bar; ==1 -> guard FX
    int32_t  absorbed    = 0;     // a2[14] (+56) absorbed (shield) — display only
    int32_t  grossDmg1   = 0;     // a2[15] (+60) gross component 1 (displayed net = gross1-absorb)
    int32_t  appliedDmg1 = 0;     // a2[16] (+64) REAL HP delta, component 1
    int32_t  grossDmg2   = 0;     // a2[17] (+68) gross component 2 (2nd bar / element)
    int32_t  appliedDmg2 = 0;     // a2[18] (+72) REAL HP delta, component 2 (player only)

    // pkt[12]==0 => missed hit: no HP application (the binary only shows text).
    bool isMiss() const { return weaponHitId == 0; }

    // Decode a raw 76-byte payload (little-endian) as memcpy'd by
    // Pkt_OnCombatResult (0x468340) from unk_8156C1.
    static CombatPacket FromRaw(const void* payload76);
};

// ---------------------------------------------------------------------------
// LOCAL player death flag (dword_16760D0 = g_SelfDeadFlag). Set by
// ApplyCombatResult when self HP would drop below 1. Reset on respawn
// (out of scope for this module).
// ---------------------------------------------------------------------------
inline bool g_SelfDead = false;

// ---------------------------------------------------------------------------
// APPLY the combat result (cGameData_ApplyCombatResult 0x55A380).
// Operates on g_World: decrements the victim's HP (PlayerEntity.hp / MonsterEntity.hp)
// and sets g_SelfDead if the local player dies. This is a PURE applicator (no damage
// computation). See Docs/TS2_GAMEPLAY_LOGIC.md §5.4.
// ---------------------------------------------------------------------------
void ApplyCombatResult(const CombatPacket& pkt);

// Convenience variant: decodes a raw 76-byte payload then applies (Pkt_OnCombatResult
// 0x468340 handler path).
void ApplyCombatResultRaw(const void* payload76);

// ---------------------------------------------------------------------------
// Local entity action context — fields of the entity record (908 bytes) read by the
// action builders (original offsets in comments). Filled by the action FSM
// (Char_UpdateAnimationFrame 0x571880) at the animation's contact frame.
// ---------------------------------------------------------------------------
struct CombatActorState {
    EntityId selfId;              // entity+4 / +8   local player netID
    EntityId targetId;           // entity+288 / +292  target netID
    float    x = 0.0f;           // entity+252  self position X
    float    y = 0.0f;           // entity+256                 Y
    float    z = 0.0f;           // entity+260                 Z
    int32_t  facing       = 0;   // entity+244  heading/facing (P[11]) — also action-state selector
    int32_t  meleeSubmode = 0;   // entity+284  melee submode: {2,3,5} -> attackSubtype {1,2,3}
    int32_t  skillId      = 0;   // entity+296  current skill (0 = pure melee)
    int32_t  skillLevelA  = 0;   // entity+300  (skillLevel = A + B)
    int32_t  skillLevelB  = 0;   // entity+304
};

// ---------------------------------------------------------------------------
// Combat action request — OUTGOING opcode 18 (0x12), 76-byte payload = 19 int32.
// Layout: Docs/TS2_GAMEPLAY_LOGIC.md §5.2/§5.6 ("CombatActionRequest"). Fields
// P[12..18] are left at 0 (the server fills them in the op21 response).
// ---------------------------------------------------------------------------
struct CombatActionRequest {
    int32_t  attackSubtype = 0;  // P[0]  1=vs player A, 2=vs player B, 3=vs monster, 5=skill-dash, 6=skill-AoE
    EntityId self;               // P[1..2]  self netID
    EntityId target;             // P[3..4]  target netID
    float    x = 0.0f;           // P[5]  self position X
    float    y = 0.0f;           // P[6]                 Y
    float    z = 0.0f;           // P[7]                 Z
    int32_t  kind       = 0;     // P[8]  1=melee, 2=skill
    int32_t  skillId    = 0;     // P[9]  (0 = pure melee)
    int32_t  skillLevel = 0;     // P[10]
    int32_t  facing     = 0;     // P[11]

    // Serialize the op18 payload (76 bytes = 19 int32 LE). P[12..18] = 0.
    void Serialize(int32_t out[19]) const;
    void Serialize(uint8_t out[76]) const;
};

// Melee attack builder (Combat_QueueMeleeAttack 0x573130).
// skillId = associated skill (0 for a basic hit). kind forced to 1 (melee),
// skillLevel forced to 0.
CombatActionRequest BuildMeleeAttack(const CombatActorState& actor, int32_t skillId);

// Skill action builder (Combat_QueueSkillAction 0x573200).
// attackSubtype derived from the skill: {4,23,42} -> 5 (dash), {5,24,43} -> 6 (AoE),
// otherwise from the melee submode. kind forced to 2 (skill), skillLevel = A+B.
CombatActionRequest BuildSkillAction(const CombatActorState& actor);

} // namespace ts2::game

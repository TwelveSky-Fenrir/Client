// Game/CombatSystem.cpp — combat resolution implementation (ts2::game).
// Faithful to cGameData_ApplyCombatResult (0x55A380), Combat_QueueMeleeAttack (0x573130)
// and Combat_QueueSkillAction (0x573200). See CombatSystem.h + Docs/TS2_GAMEPLAY_LOGIC.md §5.
#include "Game/CombatSystem.h"

#include <cstring>

namespace ts2::game {

// ===========================================================================
// Decode the result packet (76 bytes LE) — indices a2[0..18].
// ===========================================================================
CombatPacket CombatPacket::FromRaw(const void* payload76) {
    int32_t w[19];
    std::memcpy(w, payload76, sizeof(w));   // exactly 76 bytes

    CombatPacket p;
    p.resultType  = w[0];
    p.attacker    = { static_cast<uint32_t>(w[1]), static_cast<uint32_t>(w[2]) };
    p.victim      = { static_cast<uint32_t>(w[3]), static_cast<uint32_t>(w[4]) };
    std::memcpy(&p.impactX, &w[5], 4);      // a2[5..7] reinterpreted as float
    std::memcpy(&p.impactY, &w[6], 4);
    std::memcpy(&p.impactZ, &w[7], 4);
    p.kind        = w[8];
    p.skillId     = w[9];
    p.reserved10  = w[10];
    p.reserved11  = w[11];
    p.weaponHitId = w[12];
    p.critMask    = w[13];
    p.absorbed    = w[14];
    p.grossDmg1   = w[15];
    p.appliedDmg1 = w[16];
    p.grossDmg2   = w[17];
    p.appliedDmg2 = w[18];
    return p;
}

// ===========================================================================
// Apply the combat result.
//
// Original offset mapping (this = g_LocalPlayerSheet byte_1685748):
//   this+6896/6900 = self netID (= players[0].id)   this+7208 = self HP (= players[0].hp)
//   this + 908*i + 7208 = player i HP                this + 280*i + 923784 = monster i HP
// The binary also memcpy's the packet into the entity record (+644 player / +124
// monster) and fires Fx_*/Snd3D_*/Msg_AppendSystemLine. Those effects belong to the
// render/audio/UI layers and are not modeled in GameState: only the HP delta and the
// local death flag are reproduced here (a pure "applicator" core).
//
// NB: the binary only applies to entity slots that are ALREADY active (linear scan +
// "i < count" test). Per the mission spec, this goes through g_World.FindOrAdd*, which
// reuses the victim's existing slot (always already spawned for a legitimate combat
// result) or creates one if needed.
// ===========================================================================
void ApplyCombatResult(const CombatPacket& pkt) {
    const EntityId self = g_World.Self().id;   // players[0] = self (cGameData +6892)

    switch (pkt.resultType) {
    case 1:
    case 2: {
        // Victim = PLAYER. No HP touched if the hit missed.
        if (pkt.isMiss())
            return;   // pkt[12]==0: "miss" text (strtable 197/202) -> UI, no application

        // Detect LOCAL player death, BEFORE decrement (reads current self HP).
        // if (self.HP - appliedDmg1 - appliedDmg2 < 1) dword_16760D0 = 1;
        if (self.valid() && self == pkt.victim) {
            const int hpAfter = g_World.Self().hp - pkt.appliedDmg1 - pkt.appliedDmg2;
            if (hpAfter < 1)
                g_SelfDead = true;
        }

        // Apply BOTH damage components to the victim's HP, clamp >= 0.
        if (PlayerEntity* v = g_World.FindOrAddPlayer(pkt.victim)) {
            v->hp -= pkt.appliedDmg1;
            v->hp -= pkt.appliedDmg2;
            if (v->hp < 0)
                v->hp = 0;                     // clamp player to 0 (no "negative" death)
            if (self.valid() && self == pkt.victim)
                g_World.self.hp = v->hp;       // sync the self block (HUD/derived stats)
        }
        break;
    }
    case 3: {
        // Victim = MONSTER: only ONE component applied, no clamp (HP<=0 = death).
        if (pkt.isMiss())
            return;
        if (MonsterEntity* v = g_World.FindOrAddMonster(pkt.victim)) {
            v->hp -= pkt.appliedDmg1;
        }
        break;
    }
    case 4: {
        // Player GUARD/BLOCK: component 1 only, clamp >= 0. No death detection.
        if (pkt.isMiss())
            return;   // block without damage (guard text) -> UI
        if (PlayerEntity* v = g_World.FindOrAddPlayer(pkt.victim)) {
            v->hp -= pkt.appliedDmg1;
            if (v->hp < 0)
                v->hp = 0;
            if (self.valid() && self == pkt.victim)
                g_World.self.hp = v->hp;
        }
        break;
    }
    default:
        break;   // unknown resultType: no-op (default case of the original switch)
    }
}

void ApplyCombatResultRaw(const void* payload76) {
    ApplyCombatResult(CombatPacket::FromRaw(payload76));
}

// ===========================================================================
// Serialize the op18 action payload (76 bytes = 19 int32 LE).
// ===========================================================================
void CombatActionRequest::Serialize(int32_t out[19]) const {
    out[0]  = attackSubtype;
    out[1]  = static_cast<int32_t>(self.hi);
    out[2]  = static_cast<int32_t>(self.lo);
    out[3]  = static_cast<int32_t>(target.hi);
    out[4]  = static_cast<int32_t>(target.lo);
    std::memcpy(&out[5], &x, 4);   // P[5..7] = self position (float)
    std::memcpy(&out[6], &y, 4);
    std::memcpy(&out[7], &z, 4);
    out[8]  = kind;
    out[9]  = skillId;
    out[10] = skillLevel;
    out[11] = facing;
    for (int i = 12; i < 19; ++i)
        out[i] = 0;                // filled in by the server in the op21 response
}

void CombatActionRequest::Serialize(uint8_t out[76]) const {
    int32_t w[19];
    Serialize(w);
    std::memcpy(out, w, sizeof(w));
}

// ===========================================================================
// Action builders.
// ===========================================================================

// Combat_QueueMeleeAttack (0x573130): subtype derived from entity+284 {2->1,3->2,5->3}.
CombatActionRequest BuildMeleeAttack(const CombatActorState& actor, int32_t skillId) {
    CombatActionRequest r;
    switch (actor.meleeSubmode) {          // entity+284
    case 2:  r.attackSubtype = 1; break;   // vs player A
    case 3:  r.attackSubtype = 2; break;   // vs player B
    case 5:  r.attackSubtype = 3; break;   // vs monster
    default: r.attackSubtype = 0; break;   // outside {2,3,5}: undefined in the original (stack garbage), 0 here
    }
    r.self       = actor.selfId;
    r.target     = actor.targetId;
    r.x          = actor.x;
    r.y          = actor.y;
    r.z          = actor.z;
    r.kind       = 1;                      // melee
    r.skillId    = skillId;                // argument a2 (0 = basic hit)
    r.skillLevel = 0;
    r.facing     = actor.facing;           // entity+244
    return r;
}

// Combat_QueueSkillAction (0x573200): subtype based on the skill (entity+296),
// otherwise based on the melee submode (entity+284); skillLevel = entity+300 + entity+304.
CombatActionRequest BuildSkillAction(const CombatActorState& actor) {
    CombatActionRequest r;
    switch (actor.skillId) {               // entity+296
    case 4:  case 0x17: case 0x2A:
        r.attackSubtype = 5;               // skill-dash
        break;
    case 5:  case 0x18: case 0x2B:
        r.attackSubtype = 6;               // skill-AoE
        break;
    default:
        switch (actor.meleeSubmode) {      // entity+284
        case 2:  r.attackSubtype = 1; break;
        case 3:  r.attackSubtype = 2; break;
        case 5:  r.attackSubtype = 3; break;
        default: r.attackSubtype = 0; break;
        }
        break;
    }
    r.self       = actor.selfId;
    r.target     = actor.targetId;
    r.x          = actor.x;
    r.y          = actor.y;
    r.z          = actor.z;
    r.kind       = 2;                      // skill
    r.skillId    = actor.skillId;          // entity+296
    r.skillLevel = actor.skillLevelA + actor.skillLevelB;  // entity+300 + entity+304
    r.facing     = actor.facing;           // entity+244
    return r;
}

} // namespace ts2::game

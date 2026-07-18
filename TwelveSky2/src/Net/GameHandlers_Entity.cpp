// Net/GameHandlers_Entity.cpp — routes ENTITY packets to EntityManager.
//
// "entity" domain (RE/handler_domains.json): spawn / state / movement for
// the entity arrays of game::g_World. All the faithful logic already lives
// in game::EntityManager (Game/EntityManager.cpp); this file only parses
// the payload and calls the right method.
//   0x0c EnterWorld  0x0f SpawnCharacter  0x10 CharStateUpdate  0x11 CharStatDelta
//   0x12 SpawnMonster 0x13 SpawnNpc  0x15 OnCombatResult  0x19 GroundItemRemove
//   0x91 PartyMemberPosition
#include "Net/GameHandlers.h"
#include "Game/EntityManager.h"
#include "Game/ClientRuntime.h"

namespace ts2::net {

void RegisterEntityHandlers(NetSystem& sys) {
    auto& em = game::g_EntityManager;

    // 0x0c — zone load: resets the arrays + self/zone blocks.
    OnPacket<EnterWorld>(sys, 0x0c, [&em](const EnterWorld& p) { em.OnEnterWorld(p); });

    // 0x0f — character creation/update (index 0 = self).
    OnPacket<SpawnCharacter>(sys, 0x0f, [&em](const SpawnCharacter& p) { em.OnSpawnCharacter(p); });

    // 0x10 — 36 character state bitfields.
    OnPacket<CharStateUpdate>(sys, 0x10, [&em](const CharStateUpdate& p) { em.OnCharStateUpdate(p); });

    // 0x11 — HP/MP/level deltas.
    OnPacket<CharStatDelta>(sys, 0x11, [&em](const CharStatDelta& p) { em.OnCharStatDelta(p); });

    // 0x12 — monster creation/update.
    OnPacket<SpawnMonster>(sys, 0x12, [&em](const SpawnMonster& p) { em.OnSpawnMonster(p); });

    // 0x13 — NPC creation/refresh/despawn.
    OnPacket<SpawnNpc>(sys, 0x13, [&em](const SpawnNpc& p) { em.OnSpawnNpc(p); });

    // 0x19 — removes a stack from the pickup grid.
    OnPacket<GroundItemRemove>(sys, 0x19, [&em](const GroundItemRemove& p) { em.OnGroundItemRemove(p); });

    // 0x91 — world position of a party member.
    OnPacket<PartyMemberPosition>(sys, 0x91, [&em](const PartyMemberPosition& p) { em.OnPartyMemberPosition(p); });

    // 0x15 — combat result (opaque 76-byte block, undecoded). The original
    // (Pkt_OnCombatResult 0x15) applies damage numbers/effects to entity combat
    // bars; without decoding the block, we only timestamp receipt (HP follows via
    // CharStatDelta 0x11). TODO: decode block[76].
    OnPacket<OnCombatResult>(sys, 0x15, [](const OnCombatResult&) {
        game::g_Client.Var(0x1675B00) = 0; // reset local latch (dword_1675B00), faithful.
    });
}

} // namespace ts2::net

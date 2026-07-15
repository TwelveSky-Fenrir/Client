// Net/GameHandlers_Entity.cpp — routage des paquets d'ENTITÉS vers EntityManager.
//
// Domaine « entity » (RE/handler_domains.json) : spawn / état / mouvement des
// tableaux d'entités de game::g_World. Toute la logique fidèle vit déjà dans
// game::EntityManager (Game/EntityManager.cpp) ; ici on ne fait que parser le
// payload et appeler la bonne méthode.
//   0x0c EnterWorld  0x0f SpawnCharacter  0x10 CharStateUpdate  0x11 CharStatDelta
//   0x12 SpawnMonster 0x13 SpawnNpc  0x15 OnCombatResult  0x19 GroundItemRemove
//   0x91 PartyMemberPosition
#include "Net/GameHandlers.h"
#include "Game/EntityManager.h"
#include "Game/ClientRuntime.h"

namespace ts2::net {

void RegisterEntityHandlers(NetSystem& sys) {
    auto& em = game::g_EntityManager;

    // 0x0c — chargement de zone : reset des tableaux + blocs self/zone.
    OnPacket<EnterWorld>(sys, 0x0c, [&em](const EnterWorld& p) { em.OnEnterWorld(p); });

    // 0x0f — création/màj personnage (index 0 = self).
    OnPacket<SpawnCharacter>(sys, 0x0f, [&em](const SpawnCharacter& p) { em.OnSpawnCharacter(p); });

    // 0x10 — 36 bitfields d'état d'un personnage.
    OnPacket<CharStateUpdate>(sys, 0x10, [&em](const CharStateUpdate& p) { em.OnCharStateUpdate(p); });

    // 0x11 — deltas PV/PM/niveau.
    OnPacket<CharStatDelta>(sys, 0x11, [&em](const CharStatDelta& p) { em.OnCharStatDelta(p); });

    // 0x12 — création/màj monstre.
    OnPacket<SpawnMonster>(sys, 0x12, [&em](const SpawnMonster& p) { em.OnSpawnMonster(p); });

    // 0x13 — création/refresh/despawn NPC.
    OnPacket<SpawnNpc>(sys, 0x13, [&em](const SpawnNpc& p) { em.OnSpawnNpc(p); });

    // 0x19 — retrait d'une pile de la grille de ramassage.
    OnPacket<GroundItemRemove>(sys, 0x19, [&em](const GroundItemRemove& p) { em.OnGroundItemRemove(p); });

    // 0x91 — position monde d'un membre de groupe.
    OnPacket<PartyMemberPosition>(sys, 0x91, [&em](const PartyMemberPosition& p) { em.OnPartyMemberPosition(p); });

    // 0x15 — résultat de combat (bloc opaque de 76 o, non décodé). L'original
    // (Pkt_OnCombatResult 0x15) applique les nombres de dégâts / effets aux barres
    // de combat des entités ; faute de décodage du bloc, on horodate seulement la
    // réception (les PV suivent via CharStatDelta 0x11). TODO : décoder block[76].
    OnPacket<OnCombatResult>(sys, 0x15, [](const OnCombatResult&) {
        game::g_Client.Var(0x1675B00) = 0; // reset latch local (dword_1675B00), fidèle.
    });
}

} // namespace ts2::net

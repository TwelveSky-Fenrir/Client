// Net/GameHandlers.h — installation des VRAIS handlers de paquets entrants.
//
// Remplace le handler de trace par défaut de NetSystem par le routage réel :
//   opcode -> RecvPackets::Xxx::Parse(payload,len) -> mise à jour de l'état
//   (game::g_World via EntityManager, game::g_Client via ClientRuntime, UI…).
//
// Le routage est découpé par DOMAINE (un .cpp par famille) pour permettre la
// génération parallèle sans collision de fichier. Chaque module implémente sa
// fonction RegisterXxxHandlers ; InstallGameHandlers les appelle toutes.
// Fidélité : voir RE/net_handler_notes.md (sémantique d'origine par opcode).
#pragma once
#include "Net/NetSystem.h"
#include "Net/RecvPackets.h"
#include "Net/ClientState.h"   // net::g_GmCmdCooldownLatch (remis à 0 par beaucoup de handlers)
#include <utility>

namespace ts2::net {

// Helper : enregistre un handler typé. Parse le payload en T puis appelle fn(const T&).
//   OnPacket<SpawnNpc>(sys, 0x13, [](const SpawnNpc& p){ ... });
template <class T, class F>
inline void OnPacket(NetSystem& sys, std::uint8_t op, F&& fn) {
    sys.On(op, [fn = std::forward<F>(fn)](std::uint8_t /*op*/,
                                          const std::uint8_t* payload,
                                          std::uint32_t len) mutable {
        fn(T::Parse(payload, static_cast<std::size_t>(len)));
    });
}

// Helper : handler sans payload (opcode-déclencheur). Appelle fn().
template <class F>
inline void OnTrigger(NetSystem& sys, std::uint8_t op, F&& fn) {
    sys.On(op, [fn = std::forward<F>(fn)](std::uint8_t, const std::uint8_t*,
                                          std::uint32_t) mutable { fn(); });
}

// --- Modules de domaine (un .cpp chacun ; découpage par RE/handler_domains.json) ---
void RegisterEntityHandlers     (NetSystem& sys); // 0x0c/0f/10/11/12/13/15/19/91 : entités (EntityManager)
void RegisterInvCellHandlers    (NetSystem& sys); // résultats/cellules d'inventaire (achat/vente/combine/déplacement)
void RegisterInvDispatchHandlers(NetSystem& sys); // méga-dispatchers objet (enchant/refine/socket/fuse/upgrade/batch)
void RegisterPartyGuildHandlers (NetSystem& sys); // groupe/guilde/alliance/équipe
void RegisterChatSocialHandlers (NetSystem& sys); // chat/whisper/amis/notices/prompts/dialogues
void RegisterVendorTradeHandlers(NetSystem& sys); // marchand/échange/entrepôt/boutique-joueur/réparation
void RegisterBossWorldHandlers  (NetSystem& sys); // boss/zone/carte/instance/champ de bataille/classements
void RegisterMiscHandlers       (NetSystem& sys); // game-vars/connexion/script/timers/quickslot/pet/skill/divers

// Overrides fidèles issus de la décompilation IDA directe (workflow ts2-ida-gameplay-core) :
// 0x11/0x15/0x16/0x1a. Enregistré EN DERNIER dans InstallGameHandlers (remplace les
// versions simplifiées posées par les modules de domaine ci-dessus).
void RegisterCoreOverrideHandlers(NetSystem& sys);

// Installe TOUS les handlers réels sur le NetSystem (appelé par NetSystem::Init).
void InstallGameHandlers(NetSystem& sys);

} // namespace ts2::net

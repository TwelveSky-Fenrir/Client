// Net/ServerStatusQuery.h — interrogation « live » de la population/statut d'un serveur.
//
// Réécriture fidèle de Net_QueryServerStatus (0x519CC0) : le client ouvre une
// socket TCP vers (host, port), N'ENVOIE RIEN, et attend que le serveur pousse un
// enregistrement de statut de 17 octets. Les 5 premiers octets [0..4] sont un
// en-tête ignoré ; la charge utile est :
//   maxPopulation      = octets [5..8]   (uint32 LE)
//   loadStep           = octets [9..12]  (uint32 LE)   « palier de charge » (jauge)
//   currentPopulation  = octets [13..16] (uint32 LE)   = valeur de retour du binaire
// Sur tout échec (socket / gethostbyname / connect / recv<=0 / déconnexion avant
// 17 octets) le binaire renvoie -1 et N'ÉCRIT PAS outMaxPop/outLoadStep.
#pragma once
#include <cstdint>
#include <string>

namespace ts2::net {

// Résultat d'une interrogation de statut serveur (cf. Net_QueryServerStatus 0x519CC0).
struct LiveServerStatus {
    int32_t maxPopulation     = 0;   // octets [5..8]  du record de statut
    int32_t loadStep          = 0;   // octets [9..12]
    int32_t currentPopulation = -1;  // octets [13..16] ; -1 = échec/en cours
    bool    ok                = false; // false si connect/recv a échoué (curPop reste -1)
};

// Fidèle à Net_QueryServerStatus 0x519CC0 : connect TCP(host, port), recv EXACTEMENT
// 17 octets, parse maxPop@5 / loadStep@9 / curPop@13. ok=false + currentPopulation=-1
// sur tout échec. Aucun octet n'est envoyé au serveur (le serveur pousse le record).
//
// ÉCART DE FIDÉLITÉ (documenté) : le binaire est BLOQUANT (connect + recv sans
// timeout explicite). Pour ne jamais figer l'appelant (boucle UI 30 FPS), on borne
// l'attente via select() sur connect et recv (`timeoutMs`). La sémantique de parsing
// et le protocole (pas d'envoi, 17 octets exacts, offsets) restent identiques.
LiveServerStatus QueryServerStatusLive(const std::string& host, uint16_t port,
                                       uint32_t timeoutMs = 3000);

} // namespace ts2::net

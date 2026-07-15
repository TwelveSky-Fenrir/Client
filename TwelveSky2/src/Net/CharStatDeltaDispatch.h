// Net/CharStatDeltaDispatch.h — dispatcher COMPLET du paquet Pkt_CharStatDelta.
//
// Opcode 0x11 (17), EA d'origine 0x465d90, taille size_table 0x1c (24 o de payload).
// Applique des deltas de PV/PM/niveau/attributs/argent/buffs/compteurs a UNE entite
// resolue par identite reseau (idHi/idLo), et — si c'est le joueur local (index 0) —
// aux globals « self » (g_World.self + longue traine via g_Client.Var). Contrairement
// a EntityManager::OnCharStatDelta qui n'en couvre que 7, ce module reproduit
// FIDELEMENT les 32 sous-cas (subOp 1..14, 16..18, 22..36 ; 15/19/20/21 = no-op comme
// l'original) du mega-switch d'origine, dont le cas 22 (reset multi-champ imbrique).
//
// Payload (net::CharStatDelta, 24 o) :
//   +0 idHi | +4 idLo | +8 subOp | +12 valA(v36) | +16 valB(v39) | +20 valC(v43)
#pragma once
#include <cstdint>

namespace ts2::game {

// Applique le paquet op 0x11 (Pkt_CharStatDelta). `payload` pointe sur les 24 o de
// donnees (apres l'octet d'opcode) ; `len` doit valoir >= 24 sinon le paquet est
// ignore (malforme). Sans effet si aucune entite ne correspond a (idHi,idLo).
void ApplyCharStatDelta(const uint8_t* payload, uint32_t len);

} // namespace ts2::game

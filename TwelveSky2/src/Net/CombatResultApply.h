// Net/CombatResultApply.h — décodeur du paquet « résultat de combat » (op 0x15).
//
// Réécriture FIDÈLE de cGameData_ApplyCombatResult (EA 0x55A380), appelée par le
// trampoline Pkt_OnCombatResult (EA 0x468340) qui recopie 76 octets depuis le
// tampon de réception (unk_8156C1) puis délègue ici.
//
// Le bloc de 76 octets = 19 DWORD. Cette fonction met à jour les PV des entités
// concernées dans ts2::game::g_World, empile les lignes de journal de combat dans
// g_Client.msg et positionne le drapeau « self mort ». Les effets purement
// visuels/sonores (Fx_*, Snd3D_*) et les renvois réseau (Net_QueueAction9/10) sont
// laissés en TODO précis, hors périmètre gameplay.
#pragma once
#include <cstdint>

namespace ts2::game {

// [game] Applique un bloc « résultat de combat » (op 0x15) — EA 0x55A380.
//   block : 76 octets bruts (19 DWORD) tels que reçus après le trampoline 0x468340.
//   len   : longueur du bloc (attendue = 76).
void ApplyCombatResult(const uint8_t* block, uint32_t len);

} // namespace ts2::game

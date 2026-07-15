// Net/GameVarDispatch.h — méga-dispatcher Pkt_SetGameVar (opcode 0x16).
//
// Traduction fidèle de Pkt_SetGameVar (EA 0x468370, taille 0x165A, ~130 sélecteurs
// distincts sur la plage 1..158). C'est le dispatcher central des VARIABLES DE JEU
// et stats du JOUEUR LOCAL (self) : le serveur pousse une paire [selecteur:u32]
// [valeur:i32] et le client route chaque sélecteur vers le global correspondant
// (monnaie, poids d'inventaire, points d'attributs non dépensés, jauges d'auto-hunt,
// maîtrise d'élément, longue traîne de flags…), avec parfois une ligne système, un
// son, un recalcul de rating d'attaque ou un déclenchement de warp.
//
// Modèle de données : g_World.self / g_Client (inv, msg) de Game/GameState.h et
// Game/ClientRuntime.h ; les globals non modélisés passent par g_Client.Var(adresse
// d'origine) — fidèle à la « longue traîne » du binaire.
//
// RÈGLE : ce module N'ÉDITE PAS l'état partagé — il l'inclut et l'utilise.
#pragma once
#include <cstdint>

namespace ts2::game {

// Applique un paquet SetGameVar (opcode 0x16). `payload` pointe sur les octets
// APRÈS l'opcode (= unk_8156C1 du binaire) : payload[0..3] = sélecteur (u32 LE),
// payload[4..7] = valeur (i32 LE). `len` = taille du payload (>= 8 attendu).
void ApplySetGameVar(const uint8_t* payload, uint32_t len);

} // namespace ts2::game

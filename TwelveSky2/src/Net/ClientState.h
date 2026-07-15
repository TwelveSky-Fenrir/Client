// Net/ClientState.h — globals d'état client référencés par les builders sortants.
// STUBS provisoires : certains builders Net_Send* consultent/écrivent l'état local
// (verrous anti-spam, horloge de jeu, flag de transformation) AVANT d'envoyer.
// Ces variables seront câblées au vrai état du joueur au jalon Game/ ; pour l'instant
// elles existent pour que la codegen des builders compile.
#pragma once
#include <cstdint>

namespace ts2::net {

// Horloge de jeu en secondes (g_GameTimeSec / flt_815180). À relier à App::gameClock.
inline float    g_GameTimeSec        = 0.0f;

// Verrou anti-renvoi des requêtes GM/coffre (dword_1675B08/latch) : 1 = requête en vol.
inline int      g_GmCmdCooldownLatch = 0;

// Flag « transformation en cours » (mode morph) — bloque certains envois.
inline int      g_MorphInProgress    = 0;

// Estampille de dernier envoi (flt_1675B0C) : g_GameTimeSec au moment de l'émission.
inline float    flt_1675B0C          = 0.0f;

// Compteur/état auxiliaire de requête (dword_1675B10).
inline uint32_t dword_1675B10        = 0;

// Octet d'état divers (byte_1860E1C).
inline uint8_t  byte_1860E1C         = 0;

} // namespace ts2::net

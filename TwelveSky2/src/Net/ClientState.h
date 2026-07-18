// Net/ClientState.h — client-state globals referenced by outbound builders.
// Provisional stubs: some Net_Send* builders read/write local state
// (anti-spam locks, game clock, transform flag) BEFORE sending.
// These variables will be wired to real player state at the Game/ milestone; for now
// they exist so the builder codegen compiles.
#pragma once
#include <cstdint>

namespace ts2::net {

// Game clock in seconds (g_GameTimeSec / flt_815180). To be linked to App::gameClock.
inline float    g_GameTimeSec        = 0.0f;

// Anti-resend lock for GM/vault requests (dword_1675B08/latch): 1 = request in flight.
inline int      g_GmCmdCooldownLatch = 0;

// "Transform in progress" flag (morph mode) — blocks certain sends.
inline int      g_MorphInProgress    = 0;

// Timestamp of last send (flt_1675B0C): g_GameTimeSec at emission time.
inline float    flt_1675B0C          = 0.0f;

// Auxiliary request counter/state (dword_1675B10).
inline uint32_t dword_1675B10        = 0;

// Misc state byte (byte_1860E1C).
inline uint8_t  byte_1860E1C         = 0;

} // namespace ts2::net

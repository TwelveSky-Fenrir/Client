// Net/GameVarDispatch.h — Pkt_SetGameVar mega-dispatcher (opcode 0x16).
//
// Faithful translation of Pkt_SetGameVar (EA 0x468370, size 0x165A, ~130 distinct
// selectors over range 1..158). This is the central dispatcher for LOCAL PLAYER
// (self) GAME VARIABLES and stats: the server pushes a [selector:u32]
// [value:i32] pair and the client routes each selector to the matching global
// (currency, inventory weight, unspent attribute points, auto-hunt fuel gauges,
// element mastery, a long tail of flags...), sometimes with a system line, a
// sound, an attack-rating recalc, or a warp trigger.
//
// Data model: g_World.self / g_Client (inv, msg) from Game/GameState.h and
// Game/ClientRuntime.h; unmodeled globals go through g_Client.Var(original
// address) — faithful to the binary's "long tail".
//
// RULE: this module does NOT own the shared state — it includes and uses it.
#pragma once
#include <cstdint>

namespace ts2::game {

// Applies a SetGameVar packet (opcode 0x16). `payload` points to the bytes
// AFTER the opcode (= binary's unk_8156C1): payload[0..3] = selector (u32 LE),
// payload[4..7] = value (i32 LE). `len` = payload size (>= 8 expected).
void ApplySetGameVar(const uint8_t* payload, uint32_t len);

} // namespace ts2::game

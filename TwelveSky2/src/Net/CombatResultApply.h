// Net/CombatResultApply.h — decoder for the "combat result" packet (op 0x15).
//
// FAITHFUL rewrite of cGameData_ApplyCombatResult (EA 0x55A380), called by the
// Pkt_OnCombatResult trampoline (EA 0x468340), which copies 76 bytes from the
// receive buffer (unk_8156C1) then delegates here.
//
// The 76-byte block = 19 DWORD. This function updates the HP of the entities
// involved in ts2::game::g_World, pushes combat log lines into
// g_Client.msg, and sets the "self dead" flag. Purely visual/audio effects
// (Fx_*, Snd3D_*) and network resends (Net_QueueAction9/10) are
// left as precise TODOs, out of gameplay scope.
#pragma once
#include <cstdint>

namespace ts2::game {

// [game] Applies a "combat result" block (op 0x15) — EA 0x55A380.
//   block: raw 76 bytes (19 DWORD) as received after the 0x468340 trampoline.
//   len  : block length (expected = 76).
void ApplyCombatResult(const uint8_t* block, uint32_t len);

} // namespace ts2::game

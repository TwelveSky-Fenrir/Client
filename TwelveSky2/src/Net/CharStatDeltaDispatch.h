// Net/CharStatDeltaDispatch.h — COMPLETE dispatcher for the Pkt_CharStatDelta packet.
//
// Opcode 0x11 (17), original EA 0x465d90, size_table payload size 0x1c (24 B).
// Applies HP/MP/level/attribute/gold/buff/counter deltas to AN entity resolved by
// network identity (idHi/idLo), and — if it's the local player (index 0) — to the
// "self" globals (g_World.self + a long tail via g_Client.Var). Unlike
// EntityManager::OnCharStatDelta, which only covers 7 of them, this module FAITHFULLY
// reproduces all 32 sub-cases (subOp 1..14, 16..18, 22..36; 15/19/20/21 = no-op like
// the original) of the original mega-switch, including case 22 (nested multi-field reset).
//
// Payload (net::CharStatDelta, 24 B):
//   +0 idHi | +4 idLo | +8 subOp | +12 valA(v36) | +16 valB(v39) | +20 valC(v43)
#pragma once
#include <cstdint>

namespace ts2::game {

// Applies packet op 0x11 (Pkt_CharStatDelta). `payload` points to the 24 B of
// data (after the opcode byte); `len` must be >= 24 or the packet is ignored
// (malformed). No effect if no entity matches (idHi,idLo).
void ApplyCharStatDelta(const uint8_t* payload, uint32_t len);

} // namespace ts2::game

// Net/ItemActionDispatch.h — mega-dispatcher for network opcode 0x1a.
//
// Faithful rewrite of Pkt_ItemActionDispatch (EA 0x46A320, ~121 KB, one of the
// binary's largest handlers). This packet is sent by the server in response to a
// "use/equip/store an inventory object" action: the server echoes the
// position (row/column) of the affected inventory cell, the client re-reads
// the object from its own grid, resolves its ITEM_INFO record, and routes based
// on the object's TYPE (ITEM_INFO.typeCode, offset +188 = field 0xBC).
//
// Opcode 0x1a has no struct in RecvPackets.h: the payload is a simple
// block of 4 dwords (see .cpp for the map). This closes a coverage gap.
//
// SOURCE OF TRUTH: idaTs2 disassembly. Original EAs are cited in comment.
#pragma once
#include <cstdint>

namespace ts2::game {

// Applies a 0x1a packet. `payload` points to the received block (equivalent to
// the binary's unk_8156C1); `len` its size (the binary reads 16 fixed bytes).
void ApplyItemActionDispatch(const uint8_t* payload, uint32_t len);

} // namespace ts2::game

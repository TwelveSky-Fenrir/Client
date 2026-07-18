// Net/Framing.h — TS2 network frame construction (C->S) and parsing (S->C).
// Faithful rewrite of the framing observed in the disassembly:
//   - outbound builders : Net_SendPacket_Op12 0x4B43C0, Net_SendOp110 0x4B6F80
//   - inbound loop       : Net_RecvDispatch 0x463040
//   - size table         : dword_846808 (filled by Net_InitPacketHandlers 0x463270)
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "Net/Rng.h"
#include "Asset/ByteReader.h"

namespace ts2::net {

// --- Outbound header (9 bytes) --------------------------------------------
// EXACT layout observed in the builders:
//   [0..3]  nonce1        u32 LE = (rng%10000) * (rng%10000)
//   [4..6]  nonce2_lo     3 bytes LE of nonce2 = (rng%10000) * (rng%10000)
//   [7]     seq           u8  (byte_8156A5) — overwrites nonce2's 4th byte
//   [8]     opcode        u8
//   [9..]   payload
// Then single-byte XOR (byte_8156A4) over the ENTIRE packet, header included.
inline constexpr size_t kOutHeaderSize   = 9;
inline constexpr size_t kOffNonce1       = 0;
inline constexpr size_t kOffNonce2       = 4;
inline constexpr size_t kOffSeq          = 7;
inline constexpr size_t kOffOpcode       = 8;

// Bound of the two draws composing each nonce (Rng_Next() % 10000).
inline constexpr int kNonceMod = 10000;

// Client-side receive buffer size (this+32, 200000 bytes): see
// Net/PacketDispatch.h::kRecvBufferSize (sole definition — this one was an
// unused duplicate, never referenced in this file, source of a redefinition
// conflict as soon as a single .cpp includes both headers).

// ---------------------------------------------------------------------------
// PacketWriter — assembles an outbound packet: reserves the 9-byte header,
// writes the payload via the helpers, then Finalize() sets the header and encrypts.
//
// "thiscall" promotion pitfall: in the binary, every 'char' argument of a
// builder is copied as 4 bytes (Crt_Memcpy(this+off, &arg, 4)) — the server
// therefore reads an int32. Use WriteChar4LE() to reproduce this behavior.
// ---------------------------------------------------------------------------
class PacketWriter {
public:
    PacketWriter();

    // --- Payload helpers (little-endian, like the binary's Crt_Memcpy) ---
    void WriteU8(uint8_t v);
    void WriteU16(uint16_t v);
    void WriteU32(uint32_t v);
    void WriteI32(int32_t v);
    void WriteFloat(float v);

    // Outbound 'char' emitted as 4 bytes LE: char->int32 sign extension
    // (char signed under MSVC) then 4 bytes. See Net_SendOp110 0x4B6F80.
    void WriteChar4LE(int8_t v);

    // Raw copy of 'n' bytes (memcpy of payload structs/arrays).
    void WriteBytes(const void* src, size_t n);

    // Sets the header [nonce1][nonce2_lo:3][seq][opcode], XOR-encrypts the whole
    // buffer with 'xorKey' (byte_8156A4), then increments 'seq' (byte_8156A5).
    // RNG draw order (4 calls): d1,d2 -> nonce1 ; d3,d4 -> nonce2.
    //
    // NB: in the binary ++byte_8156A5 happens after a successful send(); it is
    // coupled here to Finalize() (the NetClient sends right after and closes the
    // socket on failure, making any sequence desync moot). The NetClient must
    // therefore NOT re-increment the sequence on its own side.
    void Finalize(uint8_t opcode, Rng& rng, uint8_t xorKey, uint8_t& seq);

    // Access to the finalized packet (to pass to send()).
    const uint8_t*              Data()   const { return buf_.data(); }
    size_t                      Size()   const { return buf_.size(); }
    const std::vector<uint8_t>& Buffer() const { return buf_; }

    // Payload size alone (excluding the 9-byte header).
    size_t PayloadSize() const { return buf_.size() - kOutHeaderSize; }

private:
    void Append(const void* p, size_t n);
    std::vector<uint8_t> buf_;
};

// ---------------------------------------------------------------------------
// Decoded inbound frame — points INTO the receive buffer (no copy).
// ---------------------------------------------------------------------------
struct InboundFrame {
    uint8_t        opcode     = 0;        // buf[0]
    const uint8_t* payload    = nullptr;  // payload start
    uint32_t       payloadLen = 0;        // payload size
    uint32_t       frameLen   = 0;        // total bytes consumed from the buffer
};

// ---------------------------------------------------------------------------
// PacketReader — decodes S->C frames. The inbound stream is NOT encrypted:
// Net_RecvDispatch 0x463040 reads the raw opcode. Framing: [opcode:u8][payload].
// Size is fixed per opcode via the dword_846808 table (256 u32, filled at
// runtime by Net_InitPacketHandlers 0x463270; sizeTable[op] includes the
// opcode byte, so payload = sizeTable[op] - 1). Exception: opcode 0x63 (99)
// is variable = [opcode:u8][len:u32][payload:len], consuming len+5 bytes.
// ---------------------------------------------------------------------------
class PacketReader {
public:
    static constexpr uint8_t kVariableOpcode = 0x63; // 99

    explicit PacketReader(const uint32_t* sizeTable) : sizeTable_(sizeTable) {}

    // Attempts to decode a frame from 'data' (avail bytes available).
    // Returns true and fills 'out' if a complete frame is present; false
    // if the buffer is incomplete (wait for more recv()) or if the opcode is
    // not mapped (size 0 — null handler in the binary, invalid stream).
    bool TryParse(const uint8_t* data, size_t avail, InboundFrame& out) const;

    // Bounded read cursor over a decoded frame's payload (LE).
    static ts2::asset::ByteReader Payload(const InboundFrame& f) {
        return ts2::asset::ByteReader(f.payload, f.payloadLen);
    }

private:
    const uint32_t* sizeTable_;
};

} // namespace ts2::net

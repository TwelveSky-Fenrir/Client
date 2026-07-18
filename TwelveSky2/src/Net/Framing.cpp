// Net/Framing.cpp — TS2 network framing implementation (see Framing.h).
#include "Net/Framing.h"

#include <cstring> // std::memcpy

namespace ts2::net {

// ===========================================================================
// PacketWriter
// ===========================================================================

PacketWriter::PacketWriter() {
    // Reserves the 9-byte header (filled in by Finalize); the payload
    // is appended starting at offset 9, as in the binary's builders.
    buf_.reserve(64);
    buf_.resize(kOutHeaderSize, 0);
}

void PacketWriter::Append(const void* p, size_t n) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    buf_.insert(buf_.end(), b, b + n);
}

void PacketWriter::WriteU8(uint8_t v) {
    buf_.push_back(v);
}

void PacketWriter::WriteU16(uint16_t v) {
    Append(&v, sizeof(v)); // x86 = little-endian, like the binary's Crt_Memcpy
}

void PacketWriter::WriteU32(uint32_t v) {
    Append(&v, sizeof(v));
}

void PacketWriter::WriteI32(int32_t v) {
    Append(&v, sizeof(v));
}

void PacketWriter::WriteFloat(float v) {
    Append(&v, sizeof(v)); // 4 bytes LE
}

void PacketWriter::WriteChar4LE(int8_t v) {
    // char -> int32 promotion (sign extension: 'char' is signed under MSVC),
    // then emitted as 4 bytes LE — mirrors Crt_Memcpy(this+off, &charArg, 4).
    const int32_t x = v;
    Append(&x, sizeof(x));
}

void PacketWriter::WriteBytes(const void* src, size_t n) {
    Append(src, n);
}

void PacketWriter::Finalize(uint8_t opcode, Rng& rng, uint8_t xorKey, uint8_t& seq) {
    // --- Two nonces = product of two draws bounded to 10000 --------------
    // EXACT order of the 4 RNG calls (see Net_SendPacket_Op12 0x4B43C0):
    //   d1 = Rng_Next()%10000 ;  nonce1 = (Rng_Next()%10000) * d1
    //   d3 = Rng_Next()%10000 ;  nonce2 = (Rng_Next()%10000) * d3
    const int d1 = rng.NextMod(kNonceMod);
    const uint32_t nonce1 = static_cast<uint32_t>(rng.NextMod(kNonceMod) * d1);
    const int d3 = rng.NextMod(kNonceMod);
    const uint32_t nonce2 = static_cast<uint32_t>(rng.NextMod(kNonceMod) * d3);

    // --- Header (the 9 bytes reserved at the front of the buffer) -----------------
    std::memcpy(&buf_[kOffNonce1], &nonce1, 4); // nonce1 @0..3
    std::memcpy(&buf_[kOffNonce2], &nonce2, 4); // nonce2 @4..7 (byte 7 overwritten next)
    buf_[kOffSeq]    = seq;                      // seq    @7 (leaves only 3 bytes of nonce2)
    buf_[kOffOpcode] = opcode;                   // opcode @8

    // --- Encryption: single-byte XOR over the ENTIRE packet, header included ---
    const size_t len = buf_.size(); // 9 + payload
    for (size_t i = 0; i < len; ++i)
        buf_[i] ^= xorKey;

    // --- Sequence: ++byte_8156A5 (post-increment) -------------------------
    ++seq;
}

// ===========================================================================
// PacketReader
// ===========================================================================

bool PacketReader::TryParse(const uint8_t* data, size_t avail, InboundFrame& out) const {
    if (avail < 1)                              // need at least the opcode
        return false;

    const uint8_t  op = data[0];
    const uint32_t sz = sizeTable_[op];         // total size (min = 5 for 0x63)

    if (sz == 0)                                // unmapped opcode (null handler): invalid stream
        return false;
    if (avail < sz)                             // incomplete frame (Net_RecvDispatch's 1st check)
        return false;

    if (op == kVariableOpcode) {                // 0x63: explicit length
        uint32_t len = 0;
        std::memcpy(&len, data + 1, 4);         // len:u32 @ offset 1 (sz>=5 guarantees the read)
        const uint64_t total = static_cast<uint64_t>(len) + 5u;
        if (avail < total)                      // payload not yet complete
            return false;
        out.opcode     = op;
        out.payload    = data + 5;
        out.payloadLen = len;
        out.frameLen   = static_cast<uint32_t>(total);
        return true;
    }

    // Fixed-size opcode: payload = sizeTable[op] - 1 (opcode byte included).
    out.opcode     = op;
    out.payload    = data + 1;
    out.payloadLen = sz - 1;
    out.frameLen   = sz;
    return true;
}

} // namespace ts2::net

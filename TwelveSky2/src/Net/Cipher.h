// Net/Cipher.h — TwelveSky2 network encryption: single-byte XOR (key + sequence).
//
// Source of truth: disassembly of Net_ConnectLoginServer 0x462870, Net_ConnectGameServer
// 0x462A70 and the 234 Net_Send* builders (loop "for j: buf[j] ^= v18").
//
// Two bytes are negotiated at handshake and live in the network object
// g_NetClient (0x8156A0):
//   - key : byte_8156A4 (g_NetClient+4) — XOR key applied to the WHOLE buffer.
//   - seq : byte_8156A5 (g_NetClient+5) — sequence counter, stamped at
//           offset 7 of each outgoing packet then post-incremented (mod 256).
//
// IMPORTANT (fidelity): encryption is UNIDIRECTIONAL (outgoing C->S).
// The incoming S->C stream is consumed IN THE CLEAR by Net_RecvDispatch 0x463040
// (no XOR is applied on receive, nor on the handshake banners).
#pragma once
#include <cstddef>
#include <cstdint>

namespace ts2::net {

class Cipher {
public:
    Cipher() = default;
    Cipher(uint8_t key, uint8_t seq) : key_(key), seq_(seq) {}

    // Direct access to the two negotiated bytes (byte_8156A4 / byte_8156A5).
    uint8_t Key() const { return key_; }
    uint8_t Seq() const { return seq_; }
    void    SetKey(uint8_t k) { key_ = k; }
    void    SetSeq(uint8_t s) { seq_ = s; }

    // Stateless single-byte XOR (direct helper on a key). Used when the key
    // lives elsewhere (e.g. NetClient::xorKey during Login.cpp's handshake).
    static void Xor(void* data, size_t len, uint8_t key) {
        uint8_t* p = static_cast<uint8_t*>(data);
        for (size_t i = 0; i < len; ++i)
            p[i] = static_cast<uint8_t>(p[i] ^ key);
    }

    // Handshake: the key is the 2nd byte of the server banner (buffer[1], i.e.
    // g_NetClient+33), the initial sequence = key + 127 (cf. *(this+5) += 127).
    void NegotiateFromBanner(uint8_t bannerKeyByte) {
        key_ = bannerKeyByte;
        seq_ = static_cast<uint8_t>(bannerKeyByte + 127);
    }

    // Outgoing sequence stamp: returns the current value then increments
    // ("buf[7] = seq++"; wraps mod 256). Used by the Net_Send* builders after
    // a successful send.
    uint8_t NextSeq() { return seq_++; }

    // Single-byte XOR over 'len' bytes. Symmetric (encrypt == decrypt);
    // only ever used on OUTGOING buffers.
    void Apply(void* data, size_t len) const {
        uint8_t* p = static_cast<uint8_t*>(data);
        for (size_t i = 0; i < len; ++i)
            p[i] = static_cast<uint8_t>(p[i] ^ key_);
    }
    void Encrypt(void* data, size_t len) const { Apply(data, len); }
    void Decrypt(void* data, size_t len) const { Apply(data, len); }

private:
    uint8_t key_ = 0;  // byte_8156A4
    uint8_t seq_ = 0;  // byte_8156A5
};

} // namespace ts2::net

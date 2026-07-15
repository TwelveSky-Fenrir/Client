// Net/Framing.cpp — implémentation du framing réseau TS2 (voir Framing.h).
#include "Net/Framing.h"

#include <cstring> // std::memcpy

namespace ts2::net {

// ===========================================================================
// PacketWriter
// ===========================================================================

PacketWriter::PacketWriter() {
    // Réserve les 9 octets d'en-tête (remplis par Finalize) ; le payload
    // s'ajoute à partir de l'offset 9, comme dans les builders du binaire.
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
    Append(&v, sizeof(v)); // x86 = little-endian, comme le Crt_Memcpy du binaire
}

void PacketWriter::WriteU32(uint32_t v) {
    Append(&v, sizeof(v));
}

void PacketWriter::WriteI32(int32_t v) {
    Append(&v, sizeof(v));
}

void PacketWriter::WriteFloat(float v) {
    Append(&v, sizeof(v)); // 4 octets LE
}

void PacketWriter::WriteChar4LE(int8_t v) {
    // Promotion char -> int32 (extension de signe : 'char' est signé sous MSVC),
    // puis émission sur 4 octets LE — reproduit Crt_Memcpy(this+off, &charArg, 4).
    const int32_t x = v;
    Append(&x, sizeof(x));
}

void PacketWriter::WriteBytes(const void* src, size_t n) {
    Append(src, n);
}

void PacketWriter::Finalize(uint8_t opcode, Rng& rng, uint8_t xorKey, uint8_t& seq) {
    // --- Deux nonces = produit de deux tirages bornés à 10000 --------------
    // Ordre EXACT des 4 appels RNG (cf. Net_SendPacket_Op12 0x4B43C0) :
    //   d1 = Rng_Next()%10000 ;  nonce1 = (Rng_Next()%10000) * d1
    //   d3 = Rng_Next()%10000 ;  nonce2 = (Rng_Next()%10000) * d3
    const int d1 = rng.NextMod(kNonceMod);
    const uint32_t nonce1 = static_cast<uint32_t>(rng.NextMod(kNonceMod) * d1);
    const int d3 = rng.NextMod(kNonceMod);
    const uint32_t nonce2 = static_cast<uint32_t>(rng.NextMod(kNonceMod) * d3);

    // --- En-tête (les 9 octets réservés en tête de buffer) -----------------
    std::memcpy(&buf_[kOffNonce1], &nonce1, 4); // nonce1 @0..3
    std::memcpy(&buf_[kOffNonce2], &nonce2, 4); // nonce2 @4..7 (octet 7 écrasé ensuite)
    buf_[kOffSeq]    = seq;                      // seq    @7 (ne laisse que 3 o de nonce2)
    buf_[kOffOpcode] = opcode;                   // opcode @8

    // --- Chiffrement : XOR mono-octet sur TOUT le paquet, en-tête compris ---
    const size_t len = buf_.size(); // 9 + payload
    for (size_t i = 0; i < len; ++i)
        buf_[i] ^= xorKey;

    // --- Séquence : ++byte_8156A5 (post-incrément) -------------------------
    ++seq;
}

// ===========================================================================
// PacketReader
// ===========================================================================

bool PacketReader::TryParse(const uint8_t* data, size_t avail, InboundFrame& out) const {
    if (avail < 1)                              // besoin au moins de l'opcode
        return false;

    const uint8_t  op = data[0];
    const uint32_t sz = sizeTable_[op];         // taille totale (mini = 5 pour 0x63)

    if (sz == 0)                                // opcode non mappé (handler nul) : flux invalide
        return false;
    if (avail < sz)                             // trame incomplète (1er test de Net_RecvDispatch)
        return false;

    if (op == kVariableOpcode) {                // 0x63 : longueur explicite
        uint32_t len = 0;
        std::memcpy(&len, data + 1, 4);         // len:u32 @ offset 1 (sz>=5 garantit la lecture)
        const uint64_t total = static_cast<uint64_t>(len) + 5u;
        if (avail < total)                      // payload pas encore complet
            return false;
        out.opcode     = op;
        out.payload    = data + 5;
        out.payloadLen = len;
        out.frameLen   = static_cast<uint32_t>(total);
        return true;
    }

    // Opcode à taille fixe : payload = sizeTable[op] - 1 (octet d'opcode inclus).
    out.opcode     = op;
    out.payload    = data + 1;
    out.payloadLen = sz - 1;
    out.frameLen   = sz;
    return true;
}

} // namespace ts2::net

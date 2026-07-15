// Asset/Xtea.cpp — traduction fidèle du parseur validé (RE/asset_parsers/npk.py).
#include "Asset/Xtea.h"
#include <cstring>

namespace ts2::asset {

// Somme initiale du déchiffrement = delta*32 ; DELTA_INV = -0x9E3779B9 mod 2^32.
static constexpr uint32_t kSum0     = 0xC6EF3720u;
static constexpr uint32_t kDeltaInv = 0x61C88647u;

// Clés texte des octets de queue (Xtea_XorTailByte 0x708E4B / 0x708F86).
static const char kTailStd[8] = "XtEaNpK";
static const char kTailVar[8] = "NpK!TeA";

// Bloc standard (0x708DC0). L'arithmétique uint32_t enveloppe mod 2^32 (== & 0xFFFFFFFF).
static void BlockStd(uint32_t& v0, uint32_t& v1, const uint32_t k[4]) {
    uint32_t s = kSum0;
    for (int i = 0; i < 32; ++i) {
        v1 -= (((s ^ v0) + (v0 ^ k[((s >> 2) & 3) ^ 1])) ^
               (((v0 << 2) ^ (v0 >> 5)) + ((v0 << 4) ^ (v0 >> 3))));
        v0 -= (((s ^ v1) + (v1 ^ k[(s >> 2) & 3])) ^
               (((v1 << 2) ^ (v1 >> 5)) + ((v1 << 4) ^ (v1 >> 3))));
        s += kDeltaInv;
    }
}

// Bloc variante (0x708EFF) — formulation à triple-XOR.
static void BlockVar(uint32_t& v0, uint32_t& v1, const uint32_t k[4]) {
    uint32_t s = kSum0;
    for (int i = 0; i < 32; ++i) {
        v1 -= ((v0 + s) ^ (k[3] + (v0 >> 5)) ^ (k[2] + (16u * v0)));
        s += kDeltaInv;
        v0 -= ((v1 + s) ^ (k[1] + (v1 >> 5)) ^ (k[0] + (16u * v1)));
    }
}

void XteaDecryptBuffer(uint8_t* buf, size_t n, const XteaKey& key,
                       bool tailFlag, bool variant) {
    const uint32_t* k = key.k;
    const size_t rem = n & 7;
    const size_t aligned = n - rem;

    for (size_t off = 0; off < aligned; off += 8) {
        uint32_t v0, v1;
        std::memcpy(&v0, buf + off, 4);
        std::memcpy(&v1, buf + off + 4, 4);
        if (variant) BlockVar(v0, v1, k); else BlockStd(v0, v1, k);
        std::memcpy(buf + off, &v0, 4);
        std::memcpy(buf + off + 4, &v1, 4);
    }

    if (rem && tailFlag) {
        const char* tail = variant ? kTailVar : kTailStd;
        size_t idx = rem; // idx = --v4 : part de rem-1 et décroît
        for (size_t p = aligned; p < n; ++p) {
            --idx;
            const uint8_t ks = static_cast<uint8_t>(k[idx % 4] % 255u);
            buf[p] ^= static_cast<uint8_t>(static_cast<uint8_t>(tail[idx]) ^ ks);
        }
    }
}

} // namespace ts2::asset

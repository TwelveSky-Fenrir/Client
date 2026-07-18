// Net/Rng.h — network client PRNG (Rng_Next 0x7603FD).
// Faithful rewrite: only used to generate the 2 header nonces of each
// outgoing packet (Net_SendPacket_Op12 0x4B43C0, Net_SendOp110 0x4B6F80).
#pragma once
#include <cstdint>

namespace ts2::net {

// PRNG identical to the MSVC CRT rand() used by the client.
//
// Disassembly origin — Rng_Next 0x7603FD :
//     v0   = sub_76D464();                       // _getptd_noexit (per-thread CRT)
//     seed = 214013 * *(v0 + 20) + 2531011;      // classic Microsoft LCG
//     *(v0 + 20) = seed;                          // *(v0+20) = _holdrand
//     return HIWORD(seed) & 0x7FFF;               // (seed >> 16) & 0x7FFF, RAND_MAX = 32767
//
// The "seed" state is the CRT's per-thread _holdrand. Here it is
// rematerialized as explicit state: the server does not validate the nonces,
// so only the formula AND the exact draw order need to be reproduced byte-exact.
class Rng {
public:
    // Default CRT seed (_holdrand is 1 before any srand()).
    Rng() = default;
    explicit Rng(uint32_t seed) : state_(seed) {}

    // Equivalent to srand(): sets the seed.
    void Seed(uint32_t seed) { state_ = seed; }

    // Equivalent to Rng_Next() / rand(): returns 0..0x7FFF (RAND_MAX = 32767).
    int Next() {
        state_ = 214013u * state_ + 2531011u;
        return static_cast<int>((state_ >> 16) & 0x7FFFu);
    }

    // Bounded draw used for each nonce half: "Rng_Next() % m".
    // (The binary uses m = 10000; see Net_SendPacket_Op12 0x4B43C0.)
    int NextMod(int m) { return Next() % m; }

private:
    uint32_t state_ = 1u;
};

// Shared global instance: reproduces the client's single rand() state — all
// Net_Send* builders draw from the same sequence, on the network thread.
Rng& DefaultRng();

} // namespace ts2::net

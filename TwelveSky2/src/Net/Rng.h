// Net/Rng.h — PRNG du client réseau (Rng_Next 0x7603FD).
// Réécriture fidèle : sert uniquement à fabriquer les 2 nonces d'en-tête de
// chaque paquet sortant (Net_SendPacket_Op12 0x4B43C0, Net_SendOp110 0x4B6F80).
#pragma once
#include <cstdint>

namespace ts2::net {

// PRNG identique au rand() de la CRT MSVC employé par le client.
//
// Origine désassemblée — Rng_Next 0x7603FD :
//     v0   = sub_76D464();                       // _getptd_noexit (per-thread CRT)
//     seed = 214013 * *(v0 + 20) + 2531011;      // LCG classique de Microsoft
//     *(v0 + 20) = seed;                          // *(v0+20) = _holdrand
//     return HIWORD(seed) & 0x7FFF;               // (seed >> 16) & 0x7FFF, RAND_MAX = 32767
//
// L'état « seed » est le _holdrand par-thread de la CRT. On le rématérialise
// ici en état explicite : le serveur ne valide pas les nonces, donc seuls la
// formule ET l'ordre exact des tirages doivent être reproduits à l'octet près.
class Rng {
public:
    // Graine par défaut de la CRT (_holdrand vaut 1 avant tout srand()).
    Rng() = default;
    explicit Rng(uint32_t seed) : state_(seed) {}

    // Équivaut à srand() : fixe la graine.
    void Seed(uint32_t seed) { state_ = seed; }

    // Équivaut à Rng_Next() / rand() : renvoie 0..0x7FFF (RAND_MAX = 32767).
    int Next() {
        state_ = 214013u * state_ + 2531011u;
        return static_cast<int>((state_ >> 16) & 0x7FFFu);
    }

    // Tirage borné utilisé pour chaque moitié de nonce : « Rng_Next() % m ».
    // (Le binaire emploie m = 10000 ; cf. Net_SendPacket_Op12 0x4B43C0.)
    int NextMod(int m) { return Next() % m; }

private:
    uint32_t state_ = 1u;
};

// Instance globale partagée : reproduit l'état rand() unique du client — tous
// les builders Net_Send* puisent dans la même séquence, sur le thread réseau.
Rng& DefaultRng();

} // namespace ts2::net

// Asset/Xtea.h — déchiffrement XTEA des archives NPK (fidèle au désassemblage).
// Blocs : Xtea_DecryptBlock 0x708DC0 (standard) / Xtea_DecryptBlock2 0x708EFF (variante).
// Buffer : Xtea_DecryptBuffer 0x708EA3 / Xtea_DecryptBuffer2 0x708FDE.
// Clé du client GXD = {1, 4, 4, 1} (passée par les loaders de shaders).
#pragma once
#include <cstddef>
#include <cstdint>

namespace ts2::asset {

// Clé XTEA 128 bits (4 mots). Le client GXD utilise {1,4,4,1}.
struct XteaKey {
    uint32_t k[4];
};
inline constexpr XteaKey kNpkKey = {{1u, 4u, 4u, 1u}};

// Déchiffre `buf` (n octets) en place. `variant` choisit la formulation
// (standard si version NPK >= 26). `tailFlag` traite les octets résiduels
// (n & 7) si version NPK >= 25.
void XteaDecryptBuffer(uint8_t* buf, size_t n, const XteaKey& key,
                       bool tailFlag, bool variant);

} // namespace ts2::asset

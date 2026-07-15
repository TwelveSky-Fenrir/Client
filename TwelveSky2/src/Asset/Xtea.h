// Asset/Xtea.h — déchiffrement XTEA des archives NPK (fidèle au désassemblage).
// Blocs : Xtea_DecryptBlock 0x708DC0 (standard) / Xtea_DecryptBlock2 0x708EFF (variante).
// Buffer : Xtea_DecryptBuffer 0x708EA3 / Xtea_DecryptBuffer2 0x708FDE.
// Clé du client GXD = {1, 4, 4, 1} (passée par les loaders de shaders).
//
// SEULE crypto d'assets réelle de la cible (CONFIRMED, validée sur GXDEffect.npk).
// NE PAS confondre avec la XXTEA du build VeryOldClient (SOBJECT_TEA1/XXTEA111/MOBJECT_TEA1,
// clé {0x173456A8,0xB34A67FC,0x3C67A642,0x1E432526}, 6 rounds sur 2 dwords d'en-tête) :
// algorithme DIFFÉRENT et ABSENT de la cible (CONFLICT, IDA gagne — cf. Rosetta §4.B).
// Idem GXCW : NON présent dans la cible, ne PAS l'implémenter (régression).
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

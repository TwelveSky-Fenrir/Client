// Asset/Xtea.h — XTEA decryption for NPK archives (faithful to the disassembly).
// Blocks: Xtea_DecryptBlock 0x708DC0 (standard) / Xtea_DecryptBlock2 0x708EFF (variant).
// Buffer: Xtea_DecryptBuffer 0x708EA3 / Xtea_DecryptBuffer2 0x708FDE.
// GXD client key = {1, 4, 4, 1} (passed in by the shader loaders).
//
// The ONLY real asset crypto in the target (CONFIRMED, validated on GXDEffect.npk).
// DO NOT confuse with the VeryOldClient build's XXTEA (SOBJECT_TEA1/XXTEA111/MOBJECT_TEA1,
// key {0x173456A8,0xB34A67FC,0x3C67A642,0x1E432526}, 6 rounds on 2 header dwords):
// DIFFERENT algorithm, ABSENT from the target (CONFLICT, IDA wins — cf. Rosetta §4.B).
// Same for GXCW: NOT present in the target, do NOT implement it (regression).
#pragma once
#include <cstddef>
#include <cstdint>

namespace ts2::asset {

// 128-bit XTEA key (4 words). The GXD client uses {1,4,4,1}.
struct XteaKey {
    uint32_t k[4];
};
inline constexpr XteaKey kNpkKey = {{1u, 4u, 4u, 1u}};

// Decrypts `buf` (n bytes) in place. `variant` selects the formulation
// (standard if NPK version >= 26). `tailFlag` handles the leftover bytes
// (n & 7) if NPK version >= 25.
void XteaDecryptBuffer(uint8_t* buf, size_t n, const XteaKey& key,
                       bool tailFlag, bool variant);

} // namespace ts2::asset

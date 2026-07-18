// Game/BitPacking.h — byte-by-byte manipulation utilities for a "packed" 32-bit
// word (durability/item attributes, combined talisman stats, etc.).
//
// PURE UTILITY module: no global state, free functions, byte-exact to the
// TwelveSky2.exe disassembly. All original functions share the same idiom:
// the 32-bit word is passed by value on the stack (pushed as 4 bytes even
// when IDA types the argument as `char`, because __stdcall aligns each
// argument on a dword), copied via memcpy(4) into a local buffer, modified
// byte by byte, then copied back into the return dword. This idiom is
// reproduced here with simple mask/shift operations, numerically identical
// (the memcpy round-trip only emulates an unaligned access, with no effect
// on the result). `constexpr` functions (trivial, stateless): see
// Game/BitPacking.cpp for the non-regression static_asserts.
//
// Function <-> original address mapping:
//   Bits_AddByte0     0x5456D0   Bits_AddByte1    0x545720   Bits_AddByte2  0x545870
//   Bits_ClearByte0   0x545770   Bits_ClearByte1  0x5457B0   Bits_ClearByte2 0x5457F0
//   Bits_ClearByte3   0x545940
//   Bits_SetByte2     0x545830   Bits_SetByte3    0x545900
//   Bits_PackByte012  0x5458C0
//   Bits_SetByteN     0x54BF30
//   Bits_Unpack8Bytes 0x54C5D0
//   Stat_PackCombined 0x54CEB0   Stat_UnpackCombined 0x54CE40
//
// Relationship with Game/ItemSystem.h (Item_GetAttribByte0..3, 0x545610/40/70/A0):
// THESE ARE SIBLING PRIMITIVES, NOT DUPLICATES. Item_GetAttribByte0..3 are
// pure READERS (extract one byte of the word, no modification, immediately
// preceding in the binary: 0x545610..0x5456A0). The functions in this file
// (0x5456D0 and following) are the WRITERS of the same family (add/clear/
// set/pack one byte of the word) — same encoding convention, disjoint role.
// No duplication: the "byte N of the 32-bit word" idea is reused here
// without reimplementing the readers (cf. Item_GetAttribByteN).
//
// NB: several call sites (Net/GameHandlers_InvDispatch.cpp,
// Net/GameHandlers_Misc.cpp, Game/AutoPlaySystem.cpp) already contain ad hoc
// local reimplementations of some of these primitives (forced by the "do
// not edit any existing file" rule). They were verified bit-exact against
// the functions below while writing this module; this file is the
// canonical/reusable version for any new code.
#pragma once
#include <cstdint>

namespace ts2::game {

// =====================================================================
// Bits_AddByteN — adds (modulo 256) a signed delta to byte N of the word.
// The original binary sign-extends the read byte AND the delta before the
// 32-bit addition, then truncates the result to 8 bits (mov [..], al): the
// truncated result is strictly identical to an unsigned mod-256 addition,
// regardless of the sign extension used upstream.
//   Bits_AddByte0 0x5456D0 (byte 0) · Bits_AddByte1 0x545720 (byte 1)
//   Bits_AddByte2 0x545870 (byte 2)
// =====================================================================
constexpr uint32_t Bits_AddByte0(uint32_t packed, int8_t delta) {
    const uint8_t b0 = static_cast<uint8_t>((packed & 0xFFu) + static_cast<uint8_t>(delta));
    return (packed & 0xFFFFFF00u) | b0;
}
constexpr uint32_t Bits_AddByte1(uint32_t packed, int8_t delta) {
    const uint8_t b1 = static_cast<uint8_t>(((packed >> 8) & 0xFFu) + static_cast<uint8_t>(delta));
    return (packed & 0xFFFF00FFu) | (static_cast<uint32_t>(b1) << 8);
}
constexpr uint32_t Bits_AddByte2(uint32_t packed, int8_t delta) {
    const uint8_t b2 = static_cast<uint8_t>(((packed >> 16) & 0xFFu) + static_cast<uint8_t>(delta));
    return (packed & 0xFF00FFFFu) | (static_cast<uint32_t>(b2) << 16);
}

// =====================================================================
// Bits_ClearByteN — zeroes byte N of the word, the other 3 unchanged.
//   Bits_ClearByte0 0x545770 · Bits_ClearByte1 0x5457B0
//   Bits_ClearByte2 0x5457F0 · Bits_ClearByte3 0x545940
// =====================================================================
constexpr uint32_t Bits_ClearByte0(uint32_t packed) { return packed & 0xFFFFFF00u; }
constexpr uint32_t Bits_ClearByte1(uint32_t packed) { return packed & 0xFFFF00FFu; }
constexpr uint32_t Bits_ClearByte2(uint32_t packed) { return packed & 0xFF00FFFFu; }
constexpr uint32_t Bits_ClearByte3(uint32_t packed) { return packed & 0x00FFFFFFu; }

// =====================================================================
// Bits_SetByteN — overwrites byte N of the word with `value` (8 bits), the
// other 3 bytes unchanged.
//   Bits_SetByte2 0x545830 (byte 2) · Bits_SetByte3 0x545900 (byte 3)
// =====================================================================
constexpr uint32_t Bits_SetByte2(uint32_t packed, int8_t value) {
    return (packed & 0xFF00FFFFu) | (static_cast<uint32_t>(static_cast<uint8_t>(value)) << 16);
}
constexpr uint32_t Bits_SetByte3(uint32_t packed, int8_t value) {
    return (packed & 0x00FFFFFFu) | (static_cast<uint32_t>(static_cast<uint8_t>(value)) << 24);
}

// =====================================================================
// Bits_PackByte012 0x5458C0 — builds a 32-bit word from 3 bytes
// (byte0=a1, byte1=a2, byte2=a3), byte3 explicitly set to 0.
// =====================================================================
constexpr uint32_t Bits_PackByte012(int8_t b0, int8_t b1, int8_t b2) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(b0)))
         | (static_cast<uint32_t>(static_cast<uint8_t>(b1)) << 8)
         | (static_cast<uint32_t>(static_cast<uint8_t>(b2)) << 16);
    // byte3 = 0 implicitly (bits 24..31 left unset).
}

// =====================================================================
// Bits_SetByteN 0x54BF30 — overwrites the byte at VARIABLE INDEX `byteIndex`
// (0..3) of the word with `value`.
// FIDELITY WARNING: the original binary performs NO validation of
// `byteIndex` (direct write `[ebp+edx+var_4], al` into a 4-byte stack
// buffer) — an index outside [0..3] would corrupt the stack there.
// Reproducing this UB in C++ is impossible/dangerous; this implementation
// assumes byteIndex in [0..3] (contract honored by every caller identified
// in the disassembly) and only writes in that case, with no effect out of
// bounds (minimal defense, does not change behavior for valid calls — the
// only difference from the original: no silent out-of-bounds corruption).
// =====================================================================
constexpr uint32_t Bits_SetByteN(int32_t byteIndex, int8_t value, uint32_t packed) {
    if (byteIndex < 0 || byteIndex > 3)
        return packed; // outside original contract (stack UB) — defensive no-op.
    const uint32_t shift = static_cast<uint32_t>(byteIndex) * 8u;
    const uint32_t mask = ~(0xFFu << shift);
    return (packed & mask) | (static_cast<uint32_t>(static_cast<uint8_t>(value)) << shift);
}

// =====================================================================
// Bits_Unpack8Bytes 0x54C5D0 — extracts 8 bytes (sign-extended to int32)
// from 3 packed words:
//   packed1 -> byte2 (out.p1b2), byte3 (out.p1b3)
//   packed2 -> byte0 (out.p2b0), byte1 (out.p2b1), byte2 (out.p2b2), byte3 (out.p2b3)
//   packed3 -> byte0 (out.p3b0), byte1 (out.p3b1)
// (2 + 4 + 2 = 8 bytes, hence the original name). Each output reproduces the
// binary's `movsx`: the byte is interpreted as int8_t then sign-extended to
// int32_t (not simply zero-extended).
// =====================================================================
struct Bits_Unpack8BytesResult {
    int32_t p1b2 = 0, p1b3 = 0;
    int32_t p2b0 = 0, p2b1 = 0, p2b2 = 0, p2b3 = 0;
    int32_t p3b0 = 0, p3b1 = 0;
};

constexpr Bits_Unpack8BytesResult Bits_Unpack8Bytes(uint32_t packed1, uint32_t packed2, uint32_t packed3) {
    Bits_Unpack8BytesResult r;
    r.p1b2 = static_cast<int8_t>((packed1 >> 16) & 0xFFu);
    r.p1b3 = static_cast<int8_t>((packed1 >> 24) & 0xFFu);
    r.p2b0 = static_cast<int8_t>(packed2 & 0xFFu);
    r.p2b1 = static_cast<int8_t>((packed2 >> 8) & 0xFFu);
    r.p2b2 = static_cast<int8_t>((packed2 >> 16) & 0xFFu);
    r.p2b3 = static_cast<int8_t>((packed2 >> 24) & 0xFFu);
    r.p3b0 = static_cast<int8_t>(packed3 & 0xFFu);
    r.p3b1 = static_cast<int8_t>((packed3 >> 8) & 0xFFu);
    return r;
}

// =====================================================================
// Stat_PackCombined 0x54CEB0 — encodes (hi, lo) into a combined integer
// hi*1000000 + lo, used for "combined value" stats/talismans.
//   hi (a1) must be in [0, 100]      (otherwise fails)
//   lo (a2) must be in [0, 100000]   (otherwise fails)
// Returns true and writes *out = lo + 1000000*hi on success; otherwise
// writes *out = 0 and returns false (the original binary also writes 0 in
// this case — cf. disasm 0x54ced2/0x54ced5).
// =====================================================================
constexpr bool Stat_PackCombined(int32_t hi, int32_t lo, int32_t& out) {
    if (hi < 0 || hi > 100 || lo < 0 || lo > 100000) {
        out = 0;
        return false;
    }
    out = lo + 1000000 * hi;
    return true;
}

// =====================================================================
// Stat_UnpackCombined 0x54CE40 — inverse of Stat_PackCombined: splits a
// combined value `v` into (hi, lo) = (v/1000000, v%1000000) [SIGNED
// division/remainder, idiv], then clamps: hi>100 -> 0, lo>100000 -> 0.
// If v<0, hi=lo=0 directly (no division).
// =====================================================================
constexpr void Stat_UnpackCombined(int32_t v, int32_t& hi, int32_t& lo) {
    if (v < 0) {
        hi = 0;
        lo = 0;
        return;
    }
    hi = v / 1000000;
    lo = v % 1000000;
    if (hi > 100)
        hi = 0;
    if (lo > 100000)
        lo = 0;
}

} // namespace ts2::game

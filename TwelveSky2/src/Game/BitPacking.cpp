// Game/BitPacking.cpp — translation unit anchor for Game/BitPacking.h.
//
// All functions in this module are `constexpr` (trivial, stateless) and
// defined in the header; this file only anchors compilation into the
// solution and verifies, at compile time, byte-exact fidelity to the
// disassembly via `static_assert`s reproducing cases observed at call
// sites (Net/GameHandlers_InvDispatch.cpp, Net/GameHandlers_Misc.cpp,
// Game/AutoPlaySystem.cpp) and edge cases from the original
// disassembly (0x5456D0..0x54CEB0, see header).
#include "Game/BitPacking.h"

namespace ts2::game {
namespace {

// --- Bits_AddByteN: mod-256 addition, including wraparound / negative delta ---
static_assert(Bits_AddByte0(0x00000000u, 5) == 0x00000005u, "AddByte0 simple");
static_assert(Bits_AddByte0(0x000000FFu, 1) == 0x00000000u, "AddByte0 wraparound");
static_assert(Bits_AddByte0(0x00000010u, -1) == 0x0000000Fu, "AddByte0 negative delta");
static_assert(Bits_AddByte1(0x00001200u, 1) == 0x00001300u, "AddByte1 simple");
static_assert(Bits_AddByte1(0x0000FF00u, 1) == 0x00000000u, "AddByte1 wraparound");
static_assert(Bits_AddByte2(0x00120000u, 1) == 0x00130000u, "AddByte2 simple");
static_assert(Bits_AddByte2(0x00FF0000u, 1) == 0x00000000u, "AddByte2 wraparound");
// Non-targeted bytes unchanged.
static_assert(Bits_AddByte0(0x12345678u, 1) == 0x12345679u, "AddByte0 preserves bytes 1-3");
static_assert(Bits_AddByte1(0x12345678u, 1) == 0x12345778u, "AddByte1 preserves bytes 0/2/3");
static_assert(Bits_AddByte2(0x12345678u, 1) == 0x12355678u, "AddByte2 preserves bytes 0/1/3");

// --- Bits_ClearByteN ---
static_assert(Bits_ClearByte0(0x12345678u) == 0x12345600u, "ClearByte0");
static_assert(Bits_ClearByte1(0x12345678u) == 0x12340078u, "ClearByte1");
static_assert(Bits_ClearByte2(0x12345678u) == 0x12005678u, "ClearByte2");
static_assert(Bits_ClearByte3(0x12345678u) == 0x00345678u, "ClearByte3");

// --- Bits_SetByte2/3 ---
static_assert(Bits_SetByte2(0x12345678u, 0x00) == 0x12005678u, "SetByte2 zero");
static_assert(Bits_SetByte2(0x12005678u, static_cast<int8_t>(0xAB)) == 0x12AB5678u, "SetByte2 value");
static_assert(Bits_SetByte3(0x12345678u, static_cast<int8_t>(0xCD)) == 0xCD345678u, "SetByte3 value");

// --- Bits_PackByte012: byte3 always 0 ---
static_assert(Bits_PackByte012(0x11, 0x22, 0x33) == 0x00332211u, "PackByte012");
static_assert(Bits_PackByte012(0, 0, 0) == 0u, "PackByte012 zero");

// --- Bits_SetByteN: equivalent to Bits_SetByte2/3 for index 2/3, no-op out of bounds ---
static_assert(Bits_SetByteN(2, static_cast<int8_t>(0xAB), 0x12005678u)
              == Bits_SetByte2(0x12005678u, static_cast<int8_t>(0xAB)), "SetByteN == SetByte2");
static_assert(Bits_SetByteN(3, static_cast<int8_t>(0xCD), 0x12345678u)
              == Bits_SetByte3(0x12345678u, static_cast<int8_t>(0xCD)), "SetByteN == SetByte3");
static_assert(Bits_SetByteN(0, 0x11, 0x12345678u) == 0x12345611u, "SetByteN byte0");
static_assert(Bits_SetByteN(1, 0x11, 0x12345678u) == 0x12341178u, "SetByteN byte1");
static_assert(Bits_SetByteN(-1, 0x11, 0x12345678u) == 0x12345678u, "SetByteN out of bounds (defensive no-op)");
static_assert(Bits_SetByteN(4, 0x11, 0x12345678u) == 0x12345678u, "SetByteN out of bounds (defensive no-op)");

// --- Bits_Unpack8Bytes: movsx sign-extension + 2+4+2 byte split ---
static_assert(Bits_Unpack8Bytes(0xFF000000u, 0x00000000u, 0x00000000u).p1b2 == 0, "Unpack8 p1b2");
static_assert(Bits_Unpack8Bytes(0xFF000000u, 0x00000000u, 0x00000000u).p1b3 == -1, "Unpack8 p1b3 sign-extend 0xFF");
static_assert(Bits_Unpack8Bytes(0u, 0x12345678u, 0u).p2b0 == 0x78, "Unpack8 p2b0");
static_assert(Bits_Unpack8Bytes(0u, 0x12345678u, 0u).p2b1 == 0x56, "Unpack8 p2b1");
static_assert(Bits_Unpack8Bytes(0u, 0x12345678u, 0u).p2b2 == 0x34, "Unpack8 p2b2");
static_assert(Bits_Unpack8Bytes(0u, 0x12345678u, 0u).p2b3 == 0x12, "Unpack8 p2b3");
static_assert(Bits_Unpack8Bytes(0u, 0u, 0x0000AB01u).p3b0 == 1, "Unpack8 p3b0");
static_assert(Bits_Unpack8Bytes(0u, 0u, 0x0000AB01u).p3b1 == static_cast<int8_t>(0xAB), "Unpack8 p3b1 sign-extend");

// --- Stat_PackCombined / Stat_UnpackCombined: bounds [0,100] / [0,100000] ---
constexpr bool PackOk(int32_t hi, int32_t lo, int32_t expected) {
    int32_t out = -1;
    return Stat_PackCombined(hi, lo, out) && out == expected;
}
constexpr bool PackFails(int32_t hi, int32_t lo) {
    int32_t out = 123;
    return !Stat_PackCombined(hi, lo, out) && out == 0;
}
constexpr bool UnpackEq(int32_t v, int32_t expHi, int32_t expLo) {
    int32_t hi = -1, lo = -1;
    Stat_UnpackCombined(v, hi, lo);
    return hi == expHi && lo == expLo;
}

static_assert(PackOk(3, 45000, 3045000), "PackCombined nominal");
static_assert(PackOk(100, 100000, 100100000), "PackCombined upper bounds inclusive");
static_assert(PackOk(0, 0, 0), "PackCombined zero");
static_assert(PackFails(101, 0), "PackCombined hi out of bounds");
static_assert(PackFails(0, 100001), "PackCombined lo out of bounds");
static_assert(PackFails(-1, 0), "PackCombined hi negative");

static_assert(UnpackEq(3045000, 3, 45000), "UnpackCombined nominal (round-trip of PackOk above)");
static_assert(UnpackEq(-5, 0, 0), "UnpackCombined negative -> (0,0)");
static_assert(UnpackEq(0, 0, 0), "UnpackCombined zero");
// hi/lo computed before clamping can exceed PackCombined's bounds (the
// original binary clamps *after* division/modulo, without re-checking
// hi*1000000+lo == v consistency): cf. 0x54ce87/0x54ce9b.
static_assert(UnpackEq(101000000, 0, 0), "UnpackCombined hi>100 -> clamps to 0");
static_assert(UnpackEq(100001, 0, 0), "UnpackCombined lo>100000 -> clamps to 0");

} // namespace
} // namespace ts2::game

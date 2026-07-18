// Gfx/PlayerMotionSlotResolver.cpp — implementation of the player meta-switch.
//
// Single source of truth: PcModel_ResolveEquipSlot 0x4E46A0 (control structure reproduced
// identically) + Motion_IsValidWeaponPose 0x4E3A30 (guard) + AssetMgr_InitAllSlots 0x4DEB50
// (population arithmetic = proof of the offset->stem mapping). See the header for the full
// map of anchors and the fixed-offset -> state table.
#include "Gfx/PlayerMotionSlotResolver.h"

#include <cstdio>

namespace ts2::gfx {

namespace {

// --- Pool arithmetic constants (identical in both 0x4E46A0 AND 0x4DEB50) ---
constexpr std::uint32_t kStrideRace   = 479232; // 3 * 159744
constexpr std::uint32_t kStrideGender = 159744; // 8 * 19968
constexpr std::uint32_t kStrideWeapon = 19968;  // 128 * 156
constexpr std::uint32_t kStrideState  = 156;    // size of a MotionSlot

// Region bases (0x4E46A0) == population loop bases (0x4DEB50).
constexpr std::uint32_t kRegionBodyC   = 2624960; // cat 1 loop @0x4df00c ("C", folder 001)
constexpr std::uint32_t kRegionWeaponX = 4062656; // cat 6 loop @0x4df32e ("X", folder 006)

// ABSOLUTE offset of the fallback idle slot (guard 0x4e46dd: `return this + 2644772`).
constexpr std::uint32_t kFallbackOffset = 2644772; // = kRegionBodyC + 127*156 (C, race0/gen0/wp0/state127)

// PARAMETERIZED slot: this + 479232*race + 159744*gender + 19968*wp + 156*state + regionBase.
// (the "default" paths of the a5 sub-switch, and LABEL_152's default.)
PlayerMotionSlot MakeParam(int race, int gender, int wp, int state, std::uint32_t regionBase) {
    PlayerMotionSlot s;
    s.category      = (regionBase == kRegionBodyC) ? PlayerMotionCategory::BodyC
                                                   : PlayerMotionCategory::WeaponSkillX;
    s.race          = race;
    s.gender        = gender;
    s.weaponIndex   = wp;
    s.stateIndex    = state;
    s.guardFallback = false;
    s.slotByteOffset = kStrideRace   * static_cast<std::uint32_t>(race)
                     + kStrideGender * static_cast<std::uint32_t>(gender)
                     + kStrideWeapon * static_cast<std::uint32_t>(wp)
                     + kStrideState  * static_cast<std::uint32_t>(state)
                     + regionBase;
    return s;
}

// FIXED (dedicated) slot: this + 479232*race + 159744*gender + fixedOffset (implicit wp = 0).
// Decomposes fixedOffset via the populator's arithmetic (region -> wp=0, state = rel/156).
// Proven exact for every fixedOffset emitted by 0x4E46A0 (see header: rel < 19968, rel%156==0).
PlayerMotionSlot MakeFixed(int race, int gender, std::uint32_t fixedOffset) {
    const bool inX = (fixedOffset >= kRegionWeaponX);
    const std::uint32_t regionBase = inX ? kRegionWeaponX : kRegionBodyC;
    const std::uint32_t rel        = fixedOffset - regionBase;

    PlayerMotionSlot s;
    s.category      = inX ? PlayerMotionCategory::WeaponSkillX : PlayerMotionCategory::BodyC;
    s.race          = race;
    s.gender        = gender;
    s.weaponIndex   = static_cast<int>(rel / kStrideWeapon);        // == 0 (rel < 19968)
    s.stateIndex    = static_cast<int>((rel % kStrideWeapon) / kStrideState);
    s.guardFallback = false;
    s.slotByteOffset = kStrideRace   * static_cast<std::uint32_t>(race)
                     + kStrideGender * static_cast<std::uint32_t>(gender)
                     + fixedOffset;
    return s;
}

// Fallback idle slot (guard failed): ABSOLUTE `this + 2644772`, no race/gender term.
PlayerMotionSlot MakeFallback() {
    PlayerMotionSlot s;
    s.category      = PlayerMotionCategory::BodyC;
    s.race          = 0;
    s.gender        = 0;
    s.weaponIndex   = 0;
    s.stateIndex    = static_cast<int>((kFallbackOffset - kRegionBodyC) / kStrideState); // 127
    s.guardFallback = true;
    s.slotByteOffset = kFallbackOffset;
    return s;
}

// Common sub-switch on a5 (LABEL_26/33/40/47/54/61/68/75/124 and the inline switches of 0x4E46A0):
//   a5 == 1        -> fixed X slot `fixedForState1`
//   a5 == 2 | 32   -> fixed X slot `fixedForState2or32`
//   default        -> PARAMETERIZED X slot (base 4062656, wp=a4, state=a5)
// (All a8-matched families default to region X; only LABEL_152 defaults to C.)
PlayerMotionSlot ResolveDedicatedX(int race, int gender, int wp, int state,
                                   std::uint32_t fixedForState1, std::uint32_t fixedForState2or32) {
    switch (state) {
        case 1:  return MakeFixed(race, gender, fixedForState1);
        case 2:  return MakeFixed(race, gender, fixedForState2or32);
        case 32: return MakeFixed(race, gender, fixedForState2or32);
        default: return MakeParam(race, gender, wp, state, kRegionWeaponX);
    }
}

// LABEL_152 0x4e5708: default path (a8 unmatched / non-dedicated families).
//   (a4 odd || a5 != 1 || a6 <= 112) -> PARAMETERIZED C clip (base 2624960)          0x4e578e
//   else a7 >= 1                     -> fixed C clip state 84 (offset 2638064)        0x4e5753
//   else                              -> fixed C clip state 77 (offset 2636972)        0x4e5733
PlayerMotionSlot ResolveDefaultL152(int race, int gender, int wp, int state, int a6, int a7) {
    if ((wp % 2) != 0 || state != 1 || a6 <= 112)
        return MakeParam(race, gender, wp, state, kRegionBodyC);
    if (a7 >= 1)
        return MakeFixed(race, gender, 2638064);
    return MakeFixed(race, gender, 2636972);
}

} // namespace

// Motion_IsValidWeaponPose 0x4E3A30 — switch(animState), bounds weaponPose (unsigned).
bool IsValidPlayerWeaponPose(int weaponPose, int animState) {
    const unsigned wp = static_cast<unsigned>(weaponPose); // a1 is unsigned int (0x4E3A30)
    switch (animState) { // switch(a2 = animState)
        case 0:  return wp == 0;                                     // 0x4e3a60
        case 1:  return wp < 8;                                      // 0x4e3a7d
        case 2:  return wp < 8;                                      // 0x4e3aa0
        case 3:  return wp == 0 || wp == 2 || wp == 4 || wp == 6;    // 0x4e3acf
        case 4: case 5: case 6: case 7: case 9:
                 return wp == 1 || wp == 3 || wp == 5 || wp == 7;    // 0x4e3b04..0x4e3bd8
        case 10: return wp < 8;                                      // 0x4e3bf8
        case 11: return wp < 8;                                      // 0x4e3c1b
        case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 19:
        case 20: case 21: case 22: case 23:
        case 30: case 31:
                 return wp == 0;                                     // 0x4e3c3e..0x4e3db7
        case 32: return wp < 8;                                      // 0x4e3dd4
        case 33: case 34: case 35: case 36: case 37: case 38: case 39: case 40: case 41:
                 return wp == 0;                                     // 0x4e3df7..0x4e3efd
        case 42: case 43: case 44: case 45: case 46:
                 return wp == 3;                                     // 0x4e3f20..0x4e3fac
        case 48: case 49: case 50: case 51: case 52:
                 return wp == 5;                                     // 0x4e3fcf..0x4e405b
        case 54: case 55: case 56: case 57: case 58:
                 return wp == 7;                                     // 0x4e407e..0x4e410a
        case 60: return wp == 3;                                     // 0x4e412d
        case 61: return wp == 5;                                     // 0x4e4150
        case 62: return wp == 7;                                     // 0x4e4173
        case 63: case 64: case 65: case 66: case 67: case 68:
                 return wp == 0;                                     // 0x4e4196..0x4e4245
        case 69: case 70: return wp == 3;                            // 0x4e4268..0x4e428b
        case 71: case 72: return wp == 5;                            // 0x4e42ae..0x4e42d1
        case 73: case 74: return wp == 7;                            // 0x4e42f4..0x4e4317
        case 75: case 76: return wp == 0;                            // 0x4e433a..0x4e435d
        case 81: return wp == 3;                                     // 0x4e4380
        case 82: return wp == 5;                                     // 0x4e43a3
        case 83: return wp == 7;                                     // 0x4e43c6
        case 85: case 86: return wp == 3;                            // 0x4e43e9..0x4e440c
        case 87: case 88: return wp == 5;                            // 0x4e442f..0x4e444c
        case 89: case 90: return wp == 7;                            // 0x4e4469..0x4e4486
        case 91: case 92: case 93: case 94: case 95:
                 return wp == 0;                                     // 0x4e44a3
        default: return false;                                       // LABEL_226 0x4e44ae
        // NB: states NOT listed (8, 24..29, 47, 53, 59, 77..80, 84, >95) -> invalid.
    }
}

// PcModel_ResolveEquipSlot 0x4E46A0 — control structure reproduced identically.
PlayerMotionSlot ResolvePlayerMotionSlot(int race, int gender, int weaponPose, int animState,
                                         int ctxA6, int ctxA7, int itemOrSkillId) {
    const int a2 = race, a3 = gender, a4 = weaponPose, a5 = animState;
    const int a6 = ctxA6, a7 = ctxA7, a8 = itemOrSkillId;

    // Guard 0x4e46cc (a2/a3 unsigned): race>2 || gender>1 || invalid pose -> fallback 0x4e46dd.
    if (static_cast<unsigned>(a2) > 2u || static_cast<unsigned>(a3) > 1u
        || !IsValidPlayerWeaponPose(a4, a5)) {
        return MakeFallback();
    }

    if (a8 <= 1301) { // 0x4e46ef
        if (a8 == 1301)                                              // 0x4e46f8 -> LABEL_26
            return ResolveDedicatedX(a2, a3, a4, a5, 4078256, 4078412);
        if (a8 > 814) { // 0x4e4705
            switch (a8) { // 0x4e4758
                case 815: return ResolveDedicatedX(a2, a3, a4, a5, 4080128, 4080284); // LABEL_68
                case 816: return ResolveDedicatedX(a2, a3, a4, a5, 4080440, 4080596); // LABEL_75
                case 817: return ResolveDedicatedX(a2, a3, a4, a5, 4078880, 4079036); // LABEL_40
                case 818: return ResolveDedicatedX(a2, a3, a4, a5, 4079504, 4079660); // LABEL_54
                case 819: return ResolveDedicatedX(a2, a3, a4, a5, 4078568, 4078724); // LABEL_33
                case 820: return ResolveDedicatedX(a2, a3, a4, a5, 4079192, 4079348); // LABEL_47
                case 821: return ResolveDedicatedX(a2, a3, a4, a5, 4079816, 4079972); // LABEL_61
                default:  return ResolveDefaultL152(a2, a3, a4, a5, a6, a7);          // LABEL_152
            }
        }
        if (a8 == 814)                                              // 0x4e470e -> LABEL_26
            return ResolveDedicatedX(a2, a3, a4, a5, 4078256, 4078412);
        if (a8 >= 510) { // 0x4e471b
            if (a8 <= 511)                                         // 0x4e4728 -> LABEL_40 (510,511)
                return ResolveDedicatedX(a2, a3, a4, a5, 4078880, 4079036);
            if (a8 == 559)                                         // 0x4e4735 -> LABEL_26
                return ResolveDedicatedX(a2, a3, a4, a5, 4078256, 4078412);
        }
        return ResolveDefaultL152(a2, a3, a4, a5, a6, a7);          // LABEL_152
    }

    if (a8 > 1936) { // 0x4e4766
        if (a8 > 2489) { // 0x4e47a3
            if (a8 > 19070) { // 0x4e47e3
                if (a8 >= 19261 && a8 <= 19280)                    // 0x4e482d
                    return ResolveDedicatedX(a2, a3, a4, a5, 4078256, 4078412);
                return ResolveDefaultL152(a2, a3, a4, a5, a6, a7); // LABEL_152
            }
            if (a8 >= 19051) { // 0x4e47ec (19051..19070)
                return ResolveDedicatedX(a2, a3, a4, a5, 4082000, 4082156);
            } else { // 2490..19050
                switch (a8) { // 0x4e4812
                    case 19002: case 19003: case 19004: case 19005: case 19006:
                    case 19007: case 19008: case 19009: case 19010: case 19011:
                    case 19012: case 19013: case 19014: case 19015: case 19016:
                    case 19017: case 19018: case 19019: case 19020: case 19021:
                        return ResolveDedicatedX(a2, a3, a4, a5, 4078880, 4079036); // LABEL_124
                    case 19025: case 19026: case 19027: case 19028: case 19029:
                    case 19030: case 19031: case 19032: case 19033: case 19034:
                    case 19035: case 19036: case 19037: case 19038: case 19039:
                    case 19040: case 19041: case 19042: case 19043: case 19044:
                        return ResolveDedicatedX(a2, a3, a4, a5, 4082312, 4082468);
                    default:
                        return ResolveDefaultL152(a2, a3, a4, a5, a6, a7);          // LABEL_152
                }
            }
        } else if (a8 == 2489) { // 0x4e47ac -> LABEL_33
            return ResolveDedicatedX(a2, a3, a4, a5, 4078568, 4078724);
        } else { // 1937..2488
            switch (a8) { // 0x4e47d5
                case 2266: case 2267: case 2268: case 2269: case 2270:
                case 2271: case 2272: case 2273: case 2274: case 2275:
                    return ResolveDedicatedX(a2, a3, a4, a5, 4081064, 4081220);
                case 2276: case 2277: case 2278: case 2279: case 2280:
                case 2281: case 2282: case 2283: case 2284: case 2285:
                    return ResolveDedicatedX(a2, a3, a4, a5, 4081376, 4081532);
                case 2316:
                    return ResolveDedicatedX(a2, a3, a4, a5, 4081688, 4081844);
                case 2317:
                    return ResolveDedicatedX(a2, a3, a4, a5, 4082000, 4082156);
                case 2422: case 2423: case 2424: case 2425: case 2426: case 2427:
                case 2428: case 2429: case 2430: case 2431: case 2432: case 2433:
                case 2434: case 2435: case 2436: case 2437: case 2438: case 2439:
                case 2440: case 2441:
                    return ResolveDedicatedX(a2, a3, a4, a5, 4080752, 4080908);
                default:
                    return ResolveDefaultL152(a2, a3, a4, a5, a6, a7);              // LABEL_152
            }
        }
    } else if (a8 >= 1917) { // 0x4e476f (1917..1936) -> LABEL_124
        return ResolveDedicatedX(a2, a3, a4, a5, 4078880, 4079036);
    } else { // 1302..1916
        switch (a8) { // 0x4e4795
            case 1302: case 1305: case 1308:
                return ResolveDedicatedX(a2, a3, a4, a5, 4078568, 4078724); // LABEL_33
            case 1303: case 1306: case 1309: case 1316:
                return ResolveDedicatedX(a2, a3, a4, a5, 4078880, 4079036); // LABEL_40
            case 1304: case 1307:
                return ResolveDedicatedX(a2, a3, a4, a5, 4078256, 4078412); // LABEL_26
            case 1313: case 1314: case 1315:
                return ResolveDedicatedX(a2, a3, a4, a5, 4079192, 4079348); // LABEL_47
            case 1317: case 1318: case 1319:
                return ResolveDedicatedX(a2, a3, a4, a5, 4079504, 4079660); // LABEL_54
            case 1320: case 1321: case 1322:
                return ResolveDedicatedX(a2, a3, a4, a5, 4079816, 4079972); // LABEL_61
            case 1323: case 1324: case 1325:
                return ResolveDedicatedX(a2, a3, a4, a5, 4080128, 4080284); // LABEL_68
            case 1326: case 1327: case 1328:
                return ResolveDedicatedX(a2, a3, a4, a5, 4080440, 4080596); // LABEL_75
            case 1329: case 1330: case 1331:
                return ResolveDedicatedX(a2, a3, a4, a5, 4080752, 4080908);
            default:
                return ResolveDefaultL152(a2, a3, a4, a5, a6, a7);         // LABEL_152
        }
    }

    // Unreachable: in 0x4E46A0 every branch above returns. Defensive fallback = default C.
    return ResolveDefaultL152(a2, a3, a4, a5, a6, a7);
}

std::string PlayerMotionSlot::BuildStem() const {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%c%03d%03d%03d",
                  stemLetter(), stemKind(), stemVariant(), stemState());
    return buf;
}

} // namespace ts2::gfx

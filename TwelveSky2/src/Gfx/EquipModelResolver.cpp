// Gfx/EquipModelResolver.cpp — implementation. See EquipModelResolver.h for the complete
// evidence and the CORRECTION (0x4E46A0 resolves a MOTION, not an SObject model).
//
// Anchors ported here:
//   - SObject_BuildPath        0x4D89C0  (model stems, cases 1..14)
//   - PcModel_ResolveEquipSlot 0x4E46A0  (weapon-id mega-switch -> MOTION slot)
//   - Motion_IsValidWeaponPose 0x4E3A30  (pose/state entry guard)
//   - Motion_BuildPathAndLoad  0x4D7390  (MOTION paths cat 1 "C…" / cat 6 "X…")
//   - AssetMgr_InitAllSlots    0x4DEB50  (offset decomposition -> MOTION category 1/6)
//   - Char_RenderModel         0x527020  (real binding item -> modelIndex = ITEM_INFO+196)
#include "Gfx/EquipModelResolver.h"
#include <cstdio>

namespace ts2::gfx {

namespace {

// Common race/gender bounds (guard 0x4e46cc: `a2 > 2 || a3 > 1`, UNSIGNED comparison).
inline bool RaceGenderOk(int race, int gender) {
    return static_cast<unsigned>(race) <= 2u && static_cast<unsigned>(gender) <= 1u;
}

//  Faithful port of Motion_IsValidWeaponPose 0x4E3A30: validates the (pose, state) pair.
//  a1 = pose (`a4`, 0..7) ; a2 = state (`a5`, selector 0..95). Any `a2` value outside the
//  table -> invalid (default branch LABEL_226 -> 0). `pose` is compared UNSIGNED
//  (same `a1 >= 8` / `if (a1)` as the binary).
bool MotionIsValidWeaponPose(int pose, int state) {
    const unsigned p = static_cast<unsigned>(pose);
    switch (state) {
        case 0:  return p == 0;                                   // 0x4e3a60
        case 1:  return p < 8;                                    // 0x4e3a7d
        case 2:  return p < 8;                                    // 0x4e3aa0
        case 3:  return p == 0 || p == 2 || p == 4 || p == 6;     // 0x4e3acf
        case 4:                                                   // 0x4e3b04
        case 5:                                                   // 0x4e3b39
        case 6:                                                   // 0x4e3b6e
        case 7:                                                   // 0x4e3ba3
        case 9:  return p == 1 || p == 3 || p == 5 || p == 7;     // 0x4e3bd8
        case 10: return p < 8;                                    // 0x4e3bf8
        case 11: return p < 8;                                    // 0x4e3c1b
        case 12: case 13: case 14: case 15: case 16: case 17:     // 0x4e3c3e..
        case 18: case 19: case 20: case 21: case 22: case 23:
        case 30: case 31:
            return p == 0;
        case 32: return p < 8;                                    // 0x4e3dd4
        case 33: case 34: case 35: case 36: case 37: case 38:     // 0x4e3df7..
        case 39: case 40: case 41:
            return p == 0;
        case 42: case 43: case 44: case 45: case 46: return p == 3; // 0x4e3f20..
        case 48: case 49: case 50: case 51: case 52: return p == 5; // 0x4e3fcf..
        case 54: case 55: case 56: case 57: case 58: return p == 7; // 0x4e407e..
        case 60: return p == 3;                                   // 0x4e412d
        case 61: return p == 5;                                   // 0x4e4150
        case 62: return p == 7;                                   // 0x4e4173
        case 63: case 64: case 65: case 66: case 67: case 68:     // 0x4e4196..
            return p == 0;
        case 69: case 70: return p == 3;                         // 0x4e4268..
        case 71: case 72: return p == 5;                         // 0x4e42ae..
        case 73: case 74: return p == 7;                         // 0x4e42f4..
        case 75: case 76: return p == 0;                         // 0x4e433a..
        case 81: return p == 3;                                   // 0x4e4380
        case 82: return p == 5;                                   // 0x4e43a3
        case 83: return p == 7;                                   // 0x4e43c6
        case 85: case 86: return p == 3;                         // 0x4e43e9..
        case 87: case 88: return p == 5;                         // 0x4e442f..
        case 89: case 90: return p == 7;                         // 0x4e4469..
        case 91: case 92: case 93: case 94: case 95:             // 0x4e44a3
            return p == 0;
        default: return false;                                    // LABEL_226 0x4e44ae
    }
}

//  Decoding of one family of the PcModel_ResolveEquipSlot 0x4E46A0 switch.
//  All non-default families target the MOTION cat 6 catalog (base 4062656) and
//  share the SAME sub-resolution on state `a5`:
//     a5 == 1  -> fixed slot field3 = f3a (field2 = 0)
//     a5 == 2  -> fixed slot field3 = f3b (field2 = 0)
//     a5 == 32 -> fixed slot field3 = f3b (field2 = 0)   [same value as a5==2, cf. 0x4e483c…]
//     default  -> parametric slot field2 = a4(pose), field3 = a5(state)
//  (f3a/f3b = (returnOffset - 4062656) / 156, cf. EquipModelResolver.h banner.)
EquipMotionSlot ResolveFamily(int f3a, int f3b, int race, int gender, int pose, int state) {
    EquipMotionSlot s;
    s.valid     = true;
    s.motionCat = 6;
    s.race      = race;
    s.gender    = gender;
    switch (state) {
        case 1:  s.field2 = 0;    s.field3 = f3a;   break;
        case 2:
        case 32: s.field2 = 0;    s.field3 = f3b;   break;
        default: s.field2 = pose; s.field3 = state; break;
    }
    return s;
}

// LABEL_152 0x4e5708: default branch = MOTION cat 1 catalog (base 2624960, player body).
EquipMotionSlot ResolveDefaultCat1(int race, int gender, int pose, int state, int a6, int a7) {
    EquipMotionSlot s;
    s.valid     = true;
    s.motionCat = 1;
    s.race      = race;
    s.gender    = gender;
    if ((pose % 2) != 0 || state != 1 || a6 <= 112) {   // 0x4e5708
        s.field2 = pose;                                 // 0x4e578e : +19968*a4 + 156*a5 + 2624960
        s.field3 = state;
    } else if (a7 >= 1) {                                 // 0x4e570e
        s.field2 = 0;                                    // 0x4e5753 : +2638064 == cat1 anim 84
        s.field3 = 84;
    } else {
        s.field2 = 0;                                    // 0x4e5733 : +2636972 == cat1 anim 77
        s.field3 = 77;
    }
    return s;
}

// MOTION family resolved from item id `itemId` (a8). isDefault -> ResolveDefaultCat1 (cat 1) ;
// otherwise (f3a, f3b) -> ResolveFamily (cat 6). Reproduces exactly the switch's decision tree
// (0x4e46ef -> 0x4e5708), id ranges included.
struct WeaponMotionFamily { bool isDefault; int f3a; int f3b; };

constexpr WeaponMotionFamily kDefaultFamily{true, 0, 0};

WeaponMotionFamily ClassifyWeaponItem(int itemId) {
    // FAM_26=(100,101) FAM_33=(102,103) FAM_40/124=(104,105) FAM_47=(106,107)
    // FAM_54=(108,109) FAM_61=(110,111) FAM_68=(112,113) FAM_75=(114,115)
    if (itemId <= 1301) {                                 // 0x4e46ef
        if (itemId == 1301) return {false, 100, 101};     // 0x4e46f8 -> LABEL_26
        if (itemId > 814) {                               // 0x4e4705
            switch (itemId) {                             // 0x4e4758
                case 815: return {false, 112, 113};       // LABEL_68
                case 816: return {false, 114, 115};       // LABEL_75
                case 817: return {false, 104, 105};       // LABEL_40
                case 818: return {false, 108, 109};       // LABEL_54
                case 819: return {false, 102, 103};       // LABEL_33
                case 820: return {false, 106, 107};       // LABEL_47
                case 821: return {false, 110, 111};       // LABEL_61
                default:  return kDefaultFamily;          // LABEL_152
            }
        }
        if (itemId == 814) return {false, 100, 101};      // 0x4e470e -> LABEL_26
        if (itemId >= 510) {                              // 0x4e471b
            if (itemId <= 511) return {false, 104, 105};  // 510,511 -> LABEL_40
            if (itemId == 559) return {false, 100, 101};  // 0x4e4735 -> LABEL_26
        }
        return kDefaultFamily;                            // LABEL_152
    }
    if (itemId > 1936) {                                  // 0x4e4766
        if (itemId > 2489) {                              // 0x4e47a3
            if (itemId > 19070) {                         // 0x4e47e3
                if (itemId >= 19261 && itemId <= 19280)   // 0x4e482d
                    return {false, 100, 101};             // 0x4e561d (== LABEL_26)
                return kDefaultFamily;                    // LABEL_152
            }
            if (itemId >= 19051)                          // 0x4e47ec : 19051..19070
                return {false, 124, 125};                 // 0x4e554c
            switch (itemId) {                             // 0x4e4812
                case 19002: case 19003: case 19004: case 19005: case 19006:
                case 19007: case 19008: case 19009: case 19010: case 19011:
                case 19012: case 19013: case 19014: case 19015: case 19016:
                case 19017: case 19018: case 19019: case 19020: case 19021:
                    return {false, 104, 105};             // LABEL_124
                case 19025: case 19026: case 19027: case 19028: case 19029:
                case 19030: case 19031: case 19032: case 19033: case 19034:
                case 19035: case 19036: case 19037: case 19038: case 19039:
                case 19040: case 19041: case 19042: case 19043: case 19044:
                    return {false, 126, 127};             // 0x4e547b
                default: return kDefaultFamily;           // LABEL_152
            }
        }
        if (itemId == 2489) return {false, 102, 103};     // 0x4e47ac -> LABEL_33
        switch (itemId) {                                 // 0x4e47d5 : 1937..2488
            case 2266: case 2267: case 2268: case 2269: case 2270:
            case 2271: case 2272: case 2273: case 2274: case 2275:
                return {false, 118, 119};                 // 0x4e4f95
            case 2276: case 2277: case 2278: case 2279: case 2280:
            case 2281: case 2282: case 2283: case 2284: case 2285:
                return {false, 120, 121};                 // 0x4e5066
            case 2316: return {false, 122, 123};          // 0x4e5137
            case 2317: return {false, 124, 125};          // 0x4e5208
            case 2422: case 2423: case 2424: case 2425: case 2426:
            case 2427: case 2428: case 2429: case 2430: case 2431:
            case 2432: case 2433: case 2434: case 2435: case 2436:
            case 2437: case 2438: case 2439: case 2440: case 2441:
                return {false, 116, 117};                 // 0x4e52d9
            default: return kDefaultFamily;               // LABEL_152
        }
    }
    if (itemId >= 1917) return {false, 104, 105};         // 0x4e476f : 1917..1936 -> LABEL_124
    switch (itemId) {                                     // 0x4e4795 : 1302..1916
        case 1302: case 1305: case 1308:               return {false, 102, 103}; // LABEL_33
        case 1303: case 1306: case 1309: case 1316:    return {false, 104, 105}; // LABEL_40
        case 1304: case 1307:                          return {false, 100, 101}; // LABEL_26
        case 1313: case 1314: case 1315:               return {false, 106, 107}; // LABEL_47
        case 1317: case 1318: case 1319:               return {false, 108, 109}; // LABEL_54
        case 1320: case 1321: case 1322:               return {false, 110, 111}; // LABEL_61
        case 1323: case 1324: case 1325:               return {false, 112, 113}; // LABEL_68
        case 1326: case 1327: case 1328:               return {false, 114, 115}; // LABEL_75
        case 1329: case 1330: case 1331:               return {false, 116, 117}; // 0x4e4ec4
        default:                                       return kDefaultFamily;    // LABEL_152
    }
}

} // namespace

//  (A) SObject MODEL STEMS — SObject_BuildPath 0x4D89C0.

std::string BuildSObjectStem(SObjectEquipCategory cat, int a3, int a4, int a5, int a6, int a7) {
    char buf[24];
    const int kind = a3 + 3 * a4; // 0-based, kind-based categories (C/L/W/H/A)

    switch (cat) {
        case SObjectEquipCategory::PlayerBody: // 0x4d8ba7 : C%03d%03d%03d (default, no special slot)
            if (!RaceGenderOk(a3, a4) || a5 < 0 || a6 < 0) return {};
            std::snprintf(buf, sizeof(buf), "C%03d%03d%03d", kind + 1, a5 + 1, a6 + 1);
            return buf;
        case SObjectEquipCategory::P:          // 0x4d8c2f : P%03d%03d%03d (generic indices a3/a4/a5)
            if (a3 < 0 || a4 < 0 || a5 < 0) return {};
            std::snprintf(buf, sizeof(buf), "P%03d%03d%03d", a3 + 1, a4 + 1, a5 + 1);
            return buf;
        case SObjectEquipCategory::L:          // 0x4d8c64 : L%03d%03d%03d, kind+1
            if (!RaceGenderOk(a3, a4) || a5 < 0 || a6 < 0) return {};
            std::snprintf(buf, sizeof(buf), "L%03d%03d%03d", kind + 1, a5 + 1, a6 + 1);
            return buf;
        case SObjectEquipCategory::Weapon:     // 0x4d8ca0 : W%03d%03d%03d%03d (4 indices), kind+1
            if (!RaceGenderOk(a3, a4) || a5 < 0 || a6 < 0 || a7 < 0) return {};
            std::snprintf(buf, sizeof(buf), "W%03d%03d%03d%03d", kind + 1, a5 + 1, a6 + 1, a7 + 1);
            return buf;
        case SObjectEquipCategory::H:          // 0x4d8cd5 : H%03d%03d%03d, kind+1
            if (!RaceGenderOk(a3, a4) || a5 < 0 || a6 < 0) return {};
            std::snprintf(buf, sizeof(buf), "H%03d%03d%03d", kind + 1, a5 + 1, a6 + 1);
            return buf;
        case SObjectEquipCategory::Y:          // 0x4d8cfc : Y%03d%03d (generic indices a3/a4)
            if (a3 < 0 || a4 < 0) return {};
            std::snprintf(buf, sizeof(buf), "Y%03d%03d", a3 + 1, a4 + 1);
            return buf;
        case SObjectEquipCategory::LSet2:      // 0x4d8d31 : L%03d%03d%03d, kind+7
            if (!RaceGenderOk(a3, a4) || a5 < 0 || a6 < 0) return {};
            std::snprintf(buf, sizeof(buf), "L%03d%03d%03d", kind + 7, a5 + 1, a6 + 1);
            return buf;
        case SObjectEquipCategory::A_001:      // 0x4d8d5f : A%03d001%03d, kind+1
            if (!RaceGenderOk(a3, a4) || a5 < 0) return {};
            std::snprintf(buf, sizeof(buf), "A%03d001%03d", kind + 1, a5 + 1);
            return buf;
        case SObjectEquipCategory::A_004:      // 0x4d8d8d : A%03d004%03d, kind+1
            if (!RaceGenderOk(a3, a4) || a5 < 0) return {};
            std::snprintf(buf, sizeof(buf), "A%03d004%03d", kind + 1, a5 + 1);
            return buf;
        case SObjectEquipCategory::A_001Set2:  // 0x4d8db8 : A%03d001%03d, kind+7
            if (!RaceGenderOk(a3, a4) || a5 < 0) return {};
            std::snprintf(buf, sizeof(buf), "A%03d001%03d", kind + 7, a5 + 1);
            return buf;
        case SObjectEquipCategory::A_004Set2:  // 0x4d8de3 : A%03d004%03d, kind+7
            if (!RaceGenderOk(a3, a4) || a5 < 0) return {};
            std::snprintf(buf, sizeof(buf), "A%03d004%03d", kind + 7, a5 + 1);
            return buf;
    }
    return {}; // category outside the table -> default branch 0x4d8df9 (empty string)
}

std::string BuildArmorBodyStem(int race, int gender, EquipBodySlot slot, int variant) {
    if (!RaceGenderOk(race, gender) || variant < 0) return {};

    // (middle token, kind offset) per slot — SObject_BuildPath case 1 (switch a5 @0x4d8a08).
    const char* token;
    int kindOffset;
    switch (slot) {
        case EquipBodySlot::Base0:  token = "001"; kindOffset = 1; break; // 0x4d8ba7 (a5=0 -> "001")
        case EquipBodySlot::Base1:  token = "002"; kindOffset = 1; break; // 0x4d8ba7 (a5=1 -> "002")
        case EquipBodySlot::Base2:  token = "003"; kindOffset = 1; break; // 0x4d8ba7 (a5=2 -> "003")
        case EquipBodySlot::Slot14: token = "003"; kindOffset = 7; break; // 0x4d8a30
        case EquipBodySlot::Slot15: token = "004"; kindOffset = 7; break; // 0x4d8a5e
        case EquipBodySlot::Slot16: token = "014"; kindOffset = 7; break; // 0x4d8a8c
        case EquipBodySlot::Slot17: token = "012"; kindOffset = 7; break; // 0x4d8aba
        case EquipBodySlot::Slot18: token = "013"; kindOffset = 7; break; // 0x4d8ae8
        case EquipBodySlot::Slot19: token = "008"; kindOffset = 7; break; // 0x4d8b16
        case EquipBodySlot::Slot20: token = "015"; kindOffset = 1; break; // 0x4d8b44
        case EquipBodySlot::Slot21: token = "016"; kindOffset = 1; break; // 0x4d8b72
        default: return {};
    }

    const int kind = race + 3 * gender;
    char buf[24];
    // TODO-anchor (Audit-B, off-by-one candidate, NOT verified — code currently NOT wired):
    //   AssetMgr_InitAllSlots 0x4DEB50 would populate the Base2/Slot14/Slot15 slots with a6 = i-1
    //   (@0x4df483/@0x4df8c1/@0x4df91e), and SObject_BuildPath case 1 @0x4d8a30 emits a6+1 -> the
    //   catalog entry at index M would then carry final field = M for THESE slots (so `variant`, not
    //   `variant+1`). Base0/Base1/Slot16..21 would use a6 = i (=> `variant+1`, correct below).
    //   The only slots WIRED today (Base0/Base1 via PlayerPaperdoll) are NOT affected.
    //   TO BE DECIDED dynamically (x32dbg) before wiring slots 2/14..21 -> guess nothing here.
    std::snprintf(buf, sizeof(buf), "C%03d%s%03d", kind + kindOffset, token, variant + 1);
    return buf;
}

std::string BuildWeaponStem(int race, int gender, int type, int subType, int level) {
    return BuildSObjectStem(SObjectEquipCategory::Weapon, race, gender, type, subType, level);
}

//  (B) WEAPON MOTION SLOT/STEM — PcModel_ResolveEquipSlot 0x4E46A0.

EquipMotionSlot ResolveWeaponMotionSlot(int itemId, int race, int gender,
                                        int a4_pose, int a5_state, int a6, int a7) {
    // Entry guard 0x4e46cc: race>2 || gender>1 || invalid pose/state -> sentinel
    // (returns `this + 2644772` == cat 1, race0/gender0, anim 127). Marked invalid.
    if (!RaceGenderOk(race, gender) || !MotionIsValidWeaponPose(a4_pose, a5_state)) {
        EquipMotionSlot s;
        s.valid = false; s.motionCat = 1; s.field3 = 127; // 2644772 = 2624960 + 127*156
        return s;
    }

    const WeaponMotionFamily fam = ClassifyWeaponItem(itemId);
    if (fam.isDefault)
        return ResolveDefaultCat1(race, gender, a4_pose, a5_state, a6, a7);
    return ResolveFamily(fam.f3a, fam.f3b, race, gender, a4_pose, a5_state);
}

std::string BuildWeaponMotionStem(const EquipMotionSlot& slot) {
    if (!slot.valid) return {};
    if (!RaceGenderOk(slot.race, slot.gender)) return {};
    if (slot.field2 < 0 || slot.field3 < 0) return {};

    // Motion_BuildPathAndLoad 0x4D7390 : cat 1 -> 'C' (0x4d741e), cat 6 -> 'X' (0x4d7582).
    // Same format "%c%03d%03d%03d" % (kind+1, field2+1, field3+1). The special anim==120 case of
    // cat 1 (0x4d73c9 -> "C%03d%03d011") is never reached: the 0x4E46A0 switch never
    // returns field3==120 for cat 1 (see ResolveDefaultCat1: field3 ∈ {state, 77, 84}).
    const char prefix = (slot.motionCat == 6) ? 'X' : 'C';
    const int  kind   = slot.race + 3 * slot.gender;
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%c%03d%03d%03d", prefix, kind + 1, slot.field2 + 1, slot.field3 + 1);
    return buf;
}

} // namespace ts2::gfx

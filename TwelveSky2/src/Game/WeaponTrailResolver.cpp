// Game/WeaponTrailResolver.cpp — see WeaponTrailResolver.h for the full IDA anchors.
// Byte-exact transcription of the Hex-Rays decompile of Char_DrawWeaponEffectVariantB 0x56BF90
// (switch @0x56c001..0x56c3eb, gate @0x56c411), verified identical in the disasm of
// Char_DrawWeaponTrailEffect 0x55E9D0 (labels loc_55ED14..loc_55EFxx). NO invented values.
#include "Game/WeaponTrailResolver.h"
#include <cstdio>

namespace ts2::game {

// Switch weaponAnimSlot -> v6 in [0,41] | -1. Branch structure PRESERVED as-is from the
// decompile (comparison tree + nested switches) to stay auditable line-by-line against
// 0x56BF90. Each `return` carries the EA of the corresponding site in 0x56BF90.
int ResolveWeaponTrailIndex(int weaponAnimSlot) {
    const int v4 = weaponAnimSlot;

    if (v4 > 1301) {                                   // 0x56c042
        if (v4 > 1926) {                               // 0x56c0ba
            if (v4 > 19011) {                          // 0x56c0f0
                if (v4 > 19060) {                      // 0x56c1a8
                    switch (v4) {                      // 0x56c1ff
                        case 19061: case 19062: case 19063: case 19064: case 19065:
                        case 19066: case 19067: case 19068: case 19069: case 19070:
                            return 39;                 // 0x56c3ce
                        case 19261: case 19262: case 19263: case 19264: case 19265:
                        case 19266: case 19267: case 19268: case 19269: case 19270:
                            return 40;                 // 0x56c3d7
                        case 19271: case 19272: case 19273: case 19274: case 19275:
                        case 19276: case 19277: case 19278: case 19279: case 19280:
                            return 41;                 // 0x56c3e0
                        default:
                            return -1;
                    }
                } else if (v4 >= 19051) {              // 0x56c1b1  (19051-19060)
                    return 38;                         // 0x56c3c5
                } else {
                    switch (v4) {                      // 0x56c1d6
                        case 19012: case 19013: case 19014: case 19015: case 19016:
                        case 19017: case 19018: case 19019: case 19020: case 19021:
                            return 35;                 // 0x56c3aa
                        case 19025: case 19026: case 19027: case 19028: case 19029:
                        case 19030: case 19031: case 19032: case 19033: case 19034:
                            return 36;                 // 0x56c3b3
                        case 19035: case 19036: case 19037: case 19038: case 19039:
                        case 19040: case 19041: case 19042: case 19043: case 19044:
                            return 37;                 // 0x56c3bc
                        default:
                            return -1;
                    }
                }
            } else if (v4 >= 19002) {                  // 0x56c0fd  (19002-19011)
                return 34;                             // 0x56c3a1
            } else if (v4 > 2317) {                    // 0x56c10a  (2318-19001)
                switch (v4) {                          // 0x56c19a
                    case 2422: case 2423: case 2424: case 2425: case 2426:
                    case 2427: case 2428: case 2429: case 2430: case 2431:
                        return 25;                     // 0x56c37d
                    case 2432: case 2433: case 2434: case 2435: case 2436:
                    case 2437: case 2438: case 2439: case 2440: case 2441:
                        return 27;                     // 0x56c386
                    case 2489:
                        return 4;                      // 0x56c236 (LABEL_39)
                    default:
                        return -1;
                }
            } else if (v4 == 2317) {                   // 0x56c113
                return 31;                             // 0x56c374
            } else if (v4 > 2285) {                    // 0x56c120  (2286-2316; 2317 already handled)
                return (v4 == 2316) ? 30 : -1;         // 0x56c170 -> 0x56c36b
            } else if (v4 >= 2276) {                   // 0x56c129  (2276-2285)
                return 29;                             // 0x56c362
            } else if (v4 >= 2266) {                   // 0x56c13f  (2266-2275)
                return 28;                             // 0x56c356
            } else if (v4 <= 1936) {                   // 0x56c159  (1927-1936, within the >1926 branch)
                return 33;                             // 0x56c398
            }
            return -1;                                 // 1937-2265: default
        } else if (v4 >= 1917) {                       // 0x56c0c3  (1917-1926)
            return 32;                                 // 0x56c38f
        } else {                                       // 1302-1916
            switch (v4) {                              // 0x56c0e2
                case 1302: return 1;                   // 0x56c212
                case 1303: return 2;                   // 0x56c21e
                case 1304: return 3;                   // LABEL_38 0x56c22a
                case 1305: return 4;                   // LABEL_39 0x56c236
                case 1306: return 5;                   // 0x56c242
                case 1307: return 6;                   // LABEL_41 0x56c24e
                case 1308: return 7;                   // LABEL_42 0x56c25a
                case 1309: return 8;                   // LABEL_43 0x56c266
                case 1313: return 9;                   // 0x56c272
                case 1314: return 10;                  // 0x56c27e
                case 1315: return 11;                  // LABEL_46 0x56c28a
                case 1316: return 12;                  // LABEL_47 0x56c296
                case 1317: return 13;                  // 0x56c2a2
                case 1318: return 14;                  // 0x56c2ae
                case 1319: return 15;                  // LABEL_50 0x56c2ba
                case 1320: return 16;                  // 0x56c2c6
                case 1321: return 17;                  // 0x56c2d2
                case 1322: return 18;                  // LABEL_53 0x56c2de
                case 1323: return 19;                  // 0x56c2ea
                case 1324: return 20;                  // 0x56c2f6
                case 1325: return 21;                  // LABEL_56 0x56c302
                case 1326: return 22;                  // 0x56c30e
                case 1327: return 23;                  // 0x56c31a
                case 1328: return 24;                  // LABEL_59 0x56c326
                case 1329: return 25;                  // 0x56c332
                case 1330: return 26;                  // 0x56c33e
                case 1331: return 27;                  // 0x56c34a
                default:   return -1;
            }
        }
    } else if (v4 == 1301) {                           // 0x56c04b
        return 0;                                      // 0x56c206
    } else if (v4 > 814) {                             // 0x56c058  (815-1300)
        switch (v4) {                                  // 0x56c0ac
            case 815: return 21;                       // LABEL_56
            case 816: return 24;                       // LABEL_59
            case 817: return 8;                        // LABEL_43
            case 818: return 15;                       // LABEL_50
            case 819: return 7;                        // LABEL_42
            case 820: return 11;                       // LABEL_46
            case 821: return 18;                       // LABEL_53
            default:  return -1;
        }
    } else if (v4 == 814) {                            // 0x56c061
        return 6;                                      // LABEL_41 0x56c24e
    } else if (v4 >= 510) {                            // 0x56c06e
        if (v4 <= 511) {                               // 0x56c07b  (510-511)
            return 12;                                 // LABEL_47 0x56c296
        } else if (v4 == 559) {                        // 0x56c088
            return 3;                                  // LABEL_38 0x56c22a
        }
    }
    return -1;                                         // no trail
}

// Action-state gate (switch @0x56c411 on this+61). Structure: 3 draw destinations
// (motionSub 0/1/2) + default. Transcribed case by case (each case draws the indicated sub-block;
// sub-block 2 goes through LABEL_116, gated by frameCount>=1).
int ResolveWeaponTrailMotionSub(int actionState) {
    switch (actionState) {
        // --- sub-block 0 (unk_F54DB4): unconditional draw (case 1, @0x56c527) ---
        case 1:
            return 0;

        // --- sub-block 1 (unk_F54E50): unconditional draw (case 2 / 0x20, @0x56c58e) ---
        case 2:
        case 0x20:
            return 1;

        // --- sub-block 2 (unk_F54EEC): draw gated by frameCount>=1 via LABEL_116 (@0x56d12d) ---
        // EXACT set of states that reach LABEL_116 in 0x56BF90 (case 0 included).
        case 0:
        case 5: case 6: case 7:
        case 9: case 0xA: case 0xB: case 0xC: case 0xD:
        case 0x1E: case 0x1F:
        case 0x26: case 0x27: case 0x28: case 0x29:
        case 0x2A: case 0x2B: case 0x2C: case 0x2D: case 0x2E:
        case 0x30: case 0x31: case 0x32: case 0x33: case 0x34:
        case 0x36: case 0x37: case 0x38: case 0x39: case 0x3A:
        case 0x3C: case 0x3D: case 0x3E: case 0x3F: case 0x40: case 0x41:
        case 0x42: case 0x43: case 0x44:
        case 0x45: case 0x46: case 0x47: case 0x48: case 0x49: case 0x4A: case 0x4B:
        case 0x51: case 0x52: case 0x53:
        case 0x55: case 0x56: case 0x57: case 0x58: case 0x59: case 0x5A:
            return 2;

        default:
            return -1;   // switch default: no draw
    }
}

// SObject stem cat. 9: "Y%03d001" % (trailIndex+1). See SObject_BuildPath 0x4D89C0 case 9.
std::string BuildWeaponTrailStem(int trailIndex) {
    if (trailIndex < 0 || trailIndex >= 42) return {};   // 42 entries (AssetMgr's i80<42 loop)
    char buf[16];
    std::snprintf(buf, sizeof(buf), "Y%03d001", trailIndex + 1);
    return buf;
}

} // namespace ts2::game

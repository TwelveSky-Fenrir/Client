// Game/ItemSystem.cpp — implementation faithful to the disassembly (see header).
// All floating-point constants reproduce the original float32 literals
// (f suffix promoted to double) for an identical result after truncation.
#include "Game/ItemSystem.h"
#include "Game/ClientRuntime.h"   // g_Client.VarGet — rebirth tier dword_16747BC

namespace ts2::game {

namespace {

// Crt_ftol 0x760810: double→int conversion by truncation toward zero (== C cast).
inline int Ftol(double x) { return static_cast<int>(x); }

// Intermediate float cast used by Item_ScaleStatByType*: (float)(...) then widened.
inline double Fcast(double x) { return static_cast<double>(static_cast<float>(x)); }

// Item_ClassifyRecord 0x5509A0 — category 0..9 of an ITEM_INFO record.
// Decompilation re-reviewed this mission: a1[46]=category(+184), a1[54]=subtype(+216),
// a1[47]=typeCode(+188), *a1=itemId(+0).
// The `if (!a1) return -1` guard @0x5509ab is not ported: the parameter is a
// reference (never null), so this branch is structurally unreachable here.
// Item_MeetsEquipRequirement 0x64ECD0 — sole caller of this copy — never compares
// the result to -1: neutralizing it has no observable effect.
//
// NOTE: an internal-linkage copy already exists in Game/StatFormulas.cpp (file NOT
// owned by this front) — no ODR issue (both are in an anonymous namespace).
// Deduplication to be arbitrated by the orchestrator (cf. W7 item-level-db report).
int ClassifyRecord(const ItemInfo& it) {
    if (it.category == 5) {                          // a1[46] == 5 @0x5509bf
        const uint32_t st = it.subtype;              // a1[54]
        if (st == 2 || st == 4 || st == 5 || st == 6 || st == 7 || st == 9)
            return 1;                                // @0x550a0b
        if (st == 11 || st == 12 || st == 13 || st == 14)
            return 2;                                // @0x550a4a
        switch (it.typeCode) {                       // a1[47] @0x550a62
            case 31: return 5;                       // @0x550a69
            case 32: return 6;                       // @0x550a81
            case 8:  return 8;                       // @0x550a99
            case 29: return 9;                       // @0x550aae
            default: break;
        }
    } else {
        if (it.category == 6) return 4;              // @0x550abc
        if ((it.itemId >= 201 && it.itemId <= 218) ||
            (it.itemId >= 2303 && it.itemId <= 2305))
            return 3;                                // @0x550af1
        if (it.typeCode == 28) return 7;             // @0x550b06
    }
    return 0;                                        // @0x550b11
}

} // namespace

// ItemLookup — MobDb_GetEntry 0x4C3C00 (1-based id, rejects empty slots).
ItemInfoView ItemLookup(const DataTable& itemTbl, uint32_t itemId) {
    if (itemId < 1 || itemId > itemTbl.count)
        return ItemInfoView(nullptr);
    const uint8_t* p = itemTbl.record(itemId - 1);
    if (!p)
        return ItemInfoView(nullptr);
    uint32_t id0 = 0;
    std::memcpy(&id0, p, 4);            // *(record) — the id must be non-zero
    if (id0 == 0)
        return ItemInfoView(nullptr);
    return ItemInfoView(p);
}

// Item_GetScaledStat 0x545980 — curve (coef, offset) based on (statIdx, type),
// polynomial base by level tier (45/100/113/146).
int Item_GetScaledStat(const ItemInfoView& item, int statIdx) {
    if (!item.valid())
        return 0;
    const int type = static_cast<int>(item.typeCode());   // +188
    const int lvl  = static_cast<int>(item.itemLevel());  // +204

    float coef, off;
    switch (statIdx) {
        case 1:
            if (type != 6 && (type <= 12 || type > 21)) return 0;
            coef = 0.72f;  off = 14.34f; break;
        case 2:
            switch (type) {
                case 8:  coef = 0.10f; off = 2.00f; break;
                case 9:  coef = 0.32f; off = 6.36f; break;
                case 10: coef = 0.09f; off = 1.82f; break;
                case 12: coef = 0.05f; off = 0.91f; break;
                default: return 0;
            }
            break;
        case 3:
            if (type == 10)                     { coef = 0.67f; off = 13.36f; }
            else if (type > 12 && type <= 21)   { coef = 0.29f; off = 5.73f;  }
            else                                return 0;
            break;
        case 4:
            if (type == 9)        { coef = 0.05f; off = 0.95f; }
            else if (type == 12)  { coef = 0.11f; off = 2.23f; }
            else                  return 0;
            break;
        case 5:
            if (type != 11) return 0;
            coef = 0.26f;  off = 2.00f; break;
        case 6:
            if (type != 7) return 0;
            coef = 0.13f;  off = 1.00f; break;
        default:
            return 0;
    }

    // Base by item level tier (identical across all curves).
    double base;
    if (lvl < 100)       base = static_cast<double>(lvl - 45)  * 0.1f + 0.0f;
    else if (lvl < 113)  base = static_cast<double>(lvl - 100) * 0.2f + 6.0f;
    else if (lvl < 146)  base = static_cast<double>(lvl - 113) * 0.5f + 8.0f;
    else                 return 0;   // level >= 146: outside the curve

    return Ftol(base * coef + off);
}

// Item_SocketBonusInt 0x4CA620 — type-28 weapon, byte0 of the socket word.
int Item_SocketBonusInt(const DataTable& itemTbl, uint32_t weaponId, uint32_t socketWord, int category) {
    if (weaponId == 0xFFFFFFFFu || weaponId == 0) return 0;
    if (socketWord == 0) return 0;
    ItemInfoView w = ItemLookup(itemTbl, weaponId);
    if (!w.valid()) return 0;
    if (w.typeCode() != 28) return 0;
    const uint32_t id = w.itemId();
    if (id == 2253 || id == 2254) return 0;
    if (id == 2261 || id == 2262 || id == 2300 || id == 2301) return 0;

    const int v5 = Item_GetAttribByte0(socketWord);
    if (v5 < 10) return 0;
    const int digit = v5 % 10;
    if (v5 / 10 != category) return 0;

    switch (category) {
        case 1: { static const int t[10] = {30,60,90,120,150,180,210,240,270,300};              return t[digit]; }
        case 2: { static const int t[10] = {200,400,600,800,1000,1200,1400,1600,1800,2000};      return t[digit]; }
        case 3: { static const int t[10] = {20,40,60,80,100,120,140,160,180,200};                return t[digit]; }
        case 4: { static const int t[10] = {250,500,750,1000,1250,1500,1750,2000,2250,2500};     return t[digit]; }
        case 5: { static const int t[10] = {100,200,300,400,500,600,700,800,900,1000};           return t[digit]; }
        case 6: { static const int t[10] = {100,200,300,400,500,600,700,800,900,1000};           return t[digit]; }
        default: return 0;
    }
}

// Item_SocketBonusFloat 0x4CAC30 — type-28 weapon, byte2 of the socket word.
double Item_SocketBonusFloat(const DataTable& itemTbl, uint32_t weaponId, uint32_t socketWord, int category) {
    if (weaponId == 0xFFFFFFFFu || weaponId == 0) return 0.0;
    if (socketWord == 0) return 0.0;
    ItemInfoView w = ItemLookup(itemTbl, weaponId);
    if (!w.valid()) return 0.0;
    if (w.typeCode() != 28) return 0.0;
    const uint32_t id = w.itemId();
    if (id == 2253 || id == 2254) return 0.0;
    if (id == 2261 || id == 2262 || id == 2300 || id == 2301) return 0.0;

    const int v5 = Item_GetAttribByte2(socketWord);
    if (v5 < 10) return 0.0;
    const int digit = v5 % 10;
    if (v5 / 10 != category) return 0.0;

    switch (category) {
        case 1: { static const double t[10] = {30,60,90,120,150,180,210,240,270,300};          return t[digit]; }
        case 2: { static const double t[10] = {200,400,600,800,1000,1200,1400,1600,1800,2000};  return t[digit]; }
        case 3: { static const double t[10] = {100,200,300,400,500,600,700,800,900,1000};       return t[digit]; }
        case 4: { static const double t[10] = {100,200,300,400,500,600,700,800,900,1000};       return t[digit]; }
        case 5: {
            // cat5: float multipliers (original float32 literals).
            static const double t[10] = {
                static_cast<double>(0.3f), static_cast<double>(0.3f), static_cast<double>(0.9f),
                static_cast<double>(1.2f), static_cast<double>(1.5f), static_cast<double>(1.8f),
                static_cast<double>(2.1f), static_cast<double>(2.4f), static_cast<double>(2.7f),
                static_cast<double>(3.0f)
            };
            return t[digit];
        }
        default: return 0.0;
    }
}

// Item_GetElementalBonus 0x54F590 — type-29 weapon, byte1 of the socket word.
double Item_GetElementalBonus(const DataTable& itemTbl, uint32_t weaponId, uint32_t socketWord, int key) {
    if (weaponId == 0xFFFFFFFFu || weaponId == 0) return 0.0;
    if (socketWord == 0) return 0.0;
    ItemInfoView w = ItemLookup(itemTbl, weaponId);
    if (!w.valid()) return 0.0;
    if (w.typeCode() != 29) return 0.0;

    const int v13 = Item_GetAttribByte1(socketWord);
    if (v13 < 10) return 0.0;
    const int n = v13 % 10 + 1;
    if (v13 / 10 != key) return 0.0;

    switch (key) {
        case 1: case 2: return Fcast(static_cast<double>(n) * 50.0);
        case 3:         return Fcast(static_cast<double>(n) * 200.0);
        case 4:         return Fcast(static_cast<double>(n) * 250.0);
        case 5: case 6: return Fcast(static_cast<double>(n) * 100.0);
        case 7: case 8: return Fcast(static_cast<double>(n) * 0.5);
        default:        return 0.0;
    }
}

// Item_DecodeGemBonus 0x54D390.
double Item_DecodeGemBonus(int group, int key, int gemWord) {
    switch (group) {
        case 1: {
            const int digit = gemWord / 1000000 % 10;
            const int pref  = gemWord / 1000000 - digit;
            switch (key) {
                case 10: return (pref == 10) ? static_cast<double>(digit + 11) : 0.0;
                case 20: return (pref == 20) ? static_cast<double>(digit + 11) : 0.0;
                case 30: return (pref == 30) ? static_cast<double>(digit + 11) : 0.0;
                case 40: return (pref == 40) ? static_cast<double>(digit + 11) : 0.0;
                case 70: return (pref == 70) ? static_cast<double>(digit + 11) : 0.0;
                case 80: return (pref == 80) ? static_cast<double>(digit + 11) : 0.0;
                default: return 0.0;
            }
        }
        case 2: {
            const int digit = gemWord % 1000000 / 1000 % 10;
            const int pref  = gemWord % 1000000 / 1000 - digit;
            if (key == 30)  return (pref == 30)  ? static_cast<double>(digit + 11) : 0.0;
            if (key == 130) return (pref == 130) ? static_cast<double>(digit + 11) : 0.0;
            return 0.0;
        }
        case 3: {
            const int digit = gemWord % 1000 % 10;
            const int pref  = gemWord % 1000 - digit;
            switch (key) {
                case 10: return (pref == 10) ? static_cast<double>(30  * (digit + 1)) : 0.0;
                case 20: return (pref == 20) ? static_cast<double>(20  * (digit + 1)) : 0.0;
                case 30: return (pref == 30) ? static_cast<double>(100 * (digit + 1)) : 0.0;
                case 40: return (pref == 40) ? static_cast<double>(200 * (digit + 1)) : 0.0;
                case 50: return (pref == 50) ? static_cast<double>(200 * (digit + 1)) : 0.0;
                case 60: return (pref == 60) ? static_cast<double>(100 * (digit + 1)) : 0.0;
                default: return 0.0;
            }
        }
        default:
            return 0.0;
    }
}

// Item_GetGradeValue 0x54D750.
int Item_GetGradeValue(int a1) {
    if (a1 <= 1303) {
        if (a1 >= 1301) return 30;
        if (a1 == 559)  return 20;
        if (a1 <= 813 || a1 > 821) return 0;
        return 10;                                  // 814..821
    }
    if (a1 > 1936) {
        if (a1 <= 19021) {
            if (a1 < 19002) {                       // 1937..19001: explicit list
                if ((a1 >= 2266 && a1 <= 2285) || a1 == 2316 || a1 == 2317 ||
                    (a1 >= 2422 && a1 <= 2441))
                    return 10;
                if (a1 == 2489)
                    return 20;
                return 0;
            }
            return 10;                              // 19002..19021
        }
        if (a1 > 19070) {
            if (a1 >= 19261 && a1 <= 19280) return 10;
        } else if (a1 >= 19051 || (a1 >= 19025 && a1 <= 19044)) {
            return 10;                              // 19025..19044 or 19051..19070
        }
        return 0;
    }
    if (a1 >= 1917) return 10;                       // 1917..1936
    // 1304..1916: special item grades.
    if (a1 == 1304 || a1 == 1305 || a1 == 1306 || a1 == 1314 ||
        a1 == 1318 || a1 == 1321 || a1 == 1324 || a1 == 1327) return 20;
    if (a1 == 1307 || a1 == 1308 || a1 == 1309 || a1 == 1315 ||
        a1 == 1319 || a1 == 1322 || a1 == 1325 || a1 == 1328) return 10;
    if (a1 == 1313 || a1 == 1317 || a1 == 1320 || a1 == 1323 || a1 == 1326) return 30;
    if (a1 == 1329 || a1 == 1330 || a1 == 1331) return 15;
    return 0;
}

// Item_GetGradeMultiplier 0x54D9A0 (original float32 literals).
double Item_GetGradeMultiplier(int grade) {
    switch (grade) {
        case 1:  return static_cast<double>(1.05f);
        case 2:  return static_cast<double>(1.07f);
        case 3:  return static_cast<double>(1.09f);
        case 4:  return static_cast<double>(1.11f);
        case 5:  return static_cast<double>(1.14f);
        case 6:  return static_cast<double>(1.17f);
        case 7:  return static_cast<double>(1.20f);
        case 8:  return static_cast<double>(1.25f);
        case 9:  return static_cast<double>(1.30f);
        case 10: return static_cast<double>(1.40f);
        default: return 1.0;
    }
}

// Item_GetEnchantStatDelta 0x553D50 — large table (class, slot index,
// key, enchant level byte3 ∈ 1..59). Exact transcription.
int Item_GetEnchantStatDelta(int itemClass, int slot, uint32_t socketWord, int key) {
    const int lvl = Item_GetAttribByte3(socketWord);   // byte3 = enchant level
    if (lvl < 1 || lvl > 59) return 0;

    // ---- Class 8 (special case, slot 1 only) ----
    if (itemClass == 8) {
        if (slot != 1) return 0;
        switch (key) {
            case 10:
                switch (lvl) {
                    case 2:  return -3600; case 5:  return 1200; case 8:  return 2400;
                    case 17: return -1200; case 24: return 2400; case 31: return 1200;
                    case 46: return 1200;  case 49: return -1200; case 53: return 1200;
                    case 57: return 2400;  default: return 0;
                }
            case 20:
                switch (lvl) {
                    case 1:  return -3000; case 3:  return 2000; case 6:  return 1000;
                    case 15: return 1000;  case 18: return -1000; case 20: return 3000;
                    case 21: return 1000;  case 22: return 1000; case 24: return -1000;
                    case 29: return -2000; case 30: return 1000; case 34: return 3000;
                    case 35: return 1000;  case 42: return 1000; case 51: return 1000;
                    case 52: return 2000;  case 54: return 1000; case 58: return 1000;
                    default: return 0;
                }
            case 30:
                if (lvl == 10) return 1500;
                if (lvl == 41) return 1500;
                return 0;
            case 40:
                return (lvl == 9) ? 300 : 0;
            case 50:
                switch (lvl) {
                    case 7:  return -600; case 25: return 200; case 35: return 400;
                    case 36: return 200;  case 38: return -200; case 39: return 200;
                    case 44: return 200;  case 47: return 200; case 51: return 100;
                    case 55: return 200;  default: return 0;
                }
            case 60:
                switch (lvl) {
                    case 21: return 200; case 22: return 200; case 25: return 200;
                    case 37: return 200; case 38: return 200; case 39: return -200;
                    case 45: return 200; case 47: return 200; case 55: return 200;
                    case 58: return 100; default: return 0;
                }
            case 70:
                switch (lvl) {
                    case 11: return -3; case 28: return -3; case 40: return -3;
                    default: return 0;
                }
            case 80:
                switch (lvl) {
                    case 1:  return 2;  case 2:  return 2;  case 3:  return -3; case 4:  return 2;
                    case 5:  return -3; case 6:  return 1;  case 7:  return 2;  case 8:  return -2;
                    case 9:  return 1;  case 10: return 1;  case 11: return 3;  case 12: return 1;
                    case 14: return -3; case 15: return -3; case 16: return 2;  case 17: return 2;
                    case 18: return 2;  case 19: return -2; case 20: return -2; case 21: return 1;
                    case 23: return 2;  case 27: return 1;  case 28: return 3;  case 29: return 4;
                    case 30: return 2;  case 31: return 2;  case 32: return 2;  case 33: return 3;
                    case 36: return 2;  case 37: return 2;  case 38: return 3;  case 39: return 3;
                    case 40: return 4;  case 41: return 4;  case 42: return 4;  case 43: return 4;
                    case 44: return 4;  case 45: return 4;  case 46: return 4;  case 47: return 4;
                    case 48: return 6;  case 49: return 5;  case 50: return 5;  case 51: return 5;
                    case 52: return 5;  case 53: return 5;  case 54: return 5;  case 55: return 5;
                    case 56: return 5;  case 57: return 5;  case 58: return 5;  case 59: return 6;
                    default: return 0;
                }
            case 90:
                switch (lvl) {
                    case 4:  return -3; case 5:  return 1;  case 9:  return -1; case 10: return -1;
                    case 13: return 1;  case 14: return 2;  case 15: return 1;  case 16: return -1;
                    case 19: return 3;  case 26: return 2;  case 27: return 1;  case 32: return 1;
                    case 41: return 1;  case 43: return 1;  case 48: return -2; case 49: return 1;
                    case 53: return 1;  case 54: return 1;  case 56: return 2;  default: return 0;
                }
            default:
                return 0;
        }
    }

    // ---- Classes 1 and 4 only ----
    if (itemClass != 1 && itemClass != 4) return 0;

    // Armor slots {0,2,3,4,5}
    if (slot == 0 || slot == 2 || slot == 3 || slot == 4 || slot == 5) {
        switch (key) {
            case 10:
                switch (lvl) {
                    case 2:  return -3600; case 8:  return -3600; case 17: return -1200;
                    case 24: return -2400; case 49: return -2400; default: return 0;
                }
            case 20:
                switch (lvl) {
                    case 1:  return -3000; case 3:  return 2000; case 5:  return 1000;
                    case 6:  return 1000;  case 8:  return 2000; case 9:  return -1000;
                    case 10: return -1000; case 13: return 1000; case 15: return 1000;
                    case 16: return -2000; case 18: return -1000; case 19: return 3000;
                    case 20: return 3000;  case 21: return 1000; case 22: return 1000;
                    case 24: return 2000;  case 26: return 3000; case 27: return 1000;
                    case 29: return -2000; case 30: return 1000; case 31: return 3000;
                    case 34: return 3000;  case 42: return 1000; case 48: return -2000;
                    case 49: return 1000;  case 51: return 1000; case 52: return 1000;
                    case 53: return 500;   case 54: return 1000; case 56: return 1000;
                    default: return 0;
                }
            case 30:
                switch (lvl) {
                    case 10: return 1500;  case 11: return -4500; case 13: return 1500;
                    case 31: return 1500;  case 36: return 1500;  case 41: return -1500;
                    case 46: return 1500;  case 53: return 1500;  default: return 0;
                }
            case 40:
                switch (lvl) {
                    case 9:  return 300; case 12: return 300; case 15: return 300;
                    case 16: return -300; case 34: return 300; case 37: return 300;
                    case 41: return 600; case 43: return 300; case 54: return 300;
                    default: return 0;
                }
            case 50:
                switch (lvl) {
                    case 4:  return -600; case 5:  return 200; case 25: return 200;
                    case 35: return 200;  case 38: return -200; case 39: return 200;
                    case 44: return 200;  case 47: return -200; case 51: return 100;
                    case 55: return 100;  case 58: return 300;  default: return 0;
                }
            case 60:
                switch (lvl) {
                    case 7:  return -600; case 14: return 400; case 21: return 200;
                    case 22: return 200;  case 25: return 200; case 35: return 200;
                    case 38: return 200;  case 39: return -200; case 45: return 200;
                    case 47: return -200; case 55: return 100; case 56: return 100;
                    case 57: return 300;  default: return 0;
                }
            case 70:
                if (lvl == 28) return -2;
                if (lvl == 40) return -2;
                return 0;
            case 80:
                if (lvl == 19) return -2;
                if (lvl == 32) return 2;
                return 0;
            case 100:
                switch (lvl) {
                    case 1:  return 2000; case 2:  return 2000; case 3:  return -3000; case 4:  return 2000;
                    case 5:  return -3000; case 6:  return 1000; case 7:  return 2000; case 9:  return 1000;
                    case 10: return 1000; case 11: return 3000; case 12: return 1000; case 14: return -2000;
                    case 15: return -3000; case 16: return 3000; case 17: return 3000; case 18: return 3000;
                    case 20: return -3000; case 21: return 2000; case 23: return 3000; case 27: return 1000;
                    case 28: return 3000; case 29: return 4000; case 30: return 2000; case 32: return 5000;
                    case 33: return 4000; case 35: return 2000; case 36: return 3000; case 37: return 3000;
                    case 38: return 3000; case 39: return 3000; case 40: return 5000; case 41: return 5000;
                    case 42: return 5000; case 43: return 5000; case 44: return 5000; case 45: return 5000;
                    case 46: return 5000; case 47: return 7000; case 48: return 7000; case 49: return 6000;
                    case 50: return 6000; case 51: return 7000; case 52: return 7000; case 53: return 7000;
                    case 54: return 7000; case 55: return 7000; case 56: return 7000; case 57: return 7000;
                    case 58: return 7000; case 59: return 8000; default: return 0;
                }
            default:
                return 0;
        }
    }

    // Slot 7 (weapon)
    if (slot != 7) return 0;
    switch (key) {
        case 10:
            switch (lvl) {
                case 1:  return 2400; case 2:  return 1200; case 3:  return -3600; case 4:  return 2400;
                case 5:  return 1200; case 6:  return -3600; case 8:  return -4800; case 13: return -2400;
                case 16: return 2400; case 17: return 1200; case 18: return -2400; case 19: return 2400;
                case 20: return 1200; case 21: return -3600; case 23: return -3600; case 28: return -3600;
                case 29: return -2400; case 30: return 2400; case 32: return 1200; case 33: return -1200;
                case 34: return 1200; case 35: return 2400; case 36: return 2400; case 37: return -1200;
                case 38: return 1200; case 39: return 2400; case 42: return 1200; case 45: return -2400;
                case 46: return -2400; case 47: return -2400; case 48: return -2400; case 49: return 1200;
                case 52: return 1200; case 55: return 1200; case 57: return 2400; case 58: return 2400;
                default: return 0;
            }
        case 30:
            switch (lvl) {
                case 1:  return -4500; case 6:  return 3000; case 7:  return 3000; case 10: return -4500;
                case 11: return -3000; case 12: return -1500; case 14: return -4500; case 15: return 1500;
                case 16: return -3000; case 21: return 4500; case 22: return 4500; case 25: return -3000;
                case 26: return -1500; case 27: return -4500; case 30: return -1500; case 32: return -1500;
                case 36: return 1500; case 38: return 4500; case 40: return 4500; case 41: return 3000;
                case 43: return 1500; case 46: return 1500; case 49: return -3000; case 51: return 3000;
                case 52: return 1500; case 53: return 3000; case 56: return 1500; default: return 0;
            }
        case 50:
            switch (lvl) {
                case 2:  return -600; case 3:  return 600; case 5:  return 200; case 7:  return -600;
                case 9:  return -600; case 11: return -200; case 12: return -400; case 13: return -600;
                case 14: return 600; case 15: return 200; case 17: return -400; case 18: return 600;
                case 20: return 200; case 22: return -600; case 24: return -400; case 26: return -200;
                case 27: return 200; case 28: return 400; case 32: return 200; case 33: return 200;
                case 34: return -200; case 35: return 200; case 36: return -400; case 37: return -200;
                case 39: return -800; case 41: return -200; case 44: return 200; case 45: return 200;
                case 47: return -400; case 51: return 400; case 54: return 400; case 55: return 200;
                case 56: return 200; case 58: return 400; default: return 0;
            }
        case 70:
            switch (lvl) {
                case 4:  return -6; case 5:  return -6; case 8:  return 2; case 9:  return 1;
                case 10: return 2; case 11: return 1; case 12: return 1; case 13: return 2;
                case 15: return -4; case 19: return -4; case 20: return -4; case 23: return 2;
                case 24: return 1; case 25: return 2; case 26: return 1; case 27: return 1;
                case 28: return 1; case 29: return 2; case 31: return 1; case 33: return 1;
                case 34: return 1; case 35: return -2; case 37: return 2; case 38: return -2;
                case 39: return 1; case 40: return -2; case 41: return 1; case 42: return 1;
                case 43: return 1; case 44: return 1; case 45: return 2; case 46: return 2;
                case 47: return 3; case 48: return 3; case 49: return 2; case 50: return 2;
                case 51: return 1; case 52: return 2; case 53: return 2; case 54: return 2;
                case 55: return 2; case 56: return 2; case 57: return 2; case 58: return 1;
                case 59: return 3; default: return 0;
            }
        default:
            return 0;
    }
}

// Item_ScaleStatByType* — selects the caps[idx] cap by weapon id,
// then scales (value/cap × plateau) or returns a constant plateau.
// The scaleIndex* helpers reproduce EXACTLY the original branching.
namespace {

int scaleIndexA(int a2) {
    if (a2 <= 1005) {
        if (a2 < 1002) {
            switch (a2) {
                case 541: case 560:                     return 0;
                case 543: case 544: case 548: case 561: return 1;
                case 545: case 549: case 562:           return 2;
                case 546: case 550:                     return 3;
            }
        }
        return 0;                                        // 1002..1005
    }
    if (a2 <= 2144) {
        if (a2 == 2144) return 3;
        if (a2 <= 1312) {
            if (a2 < 1310) {
                switch (a2) {
                    case 1006: case 1007: case 1008: return 1;
                    case 1012: case 1013: case 1014: return 2;
                    case 1016:                       return 3;
                }
            }
            return 3;                                    // 1310..1312
        }
        if (a2 != 1452) return 3;
        return 1;                                        // 1452
    }
    if (a2 > 12089) {
        if (a2 > 86819) return 2;                        // 86820
        if (a2 != 86819) return 3;
        return 1;                                        // 86819
    }
    if (a2 != 12001) return 3;
    return 0;                                            // 12001
}

int scaleIndexB(int a2) {
    if (a2 <= 1005) {
        if (a2 < 1002) {
            switch (a2) {
                case 542: case 547:           return 0;
                case 543: case 548: case 561: return 1;
                case 545: case 549: case 562: return 2;
                case 546: case 550:           return 3;
            }
        }
        return 0;                                        // 1002..1005
    }
    if (a2 > 2144) {
        if (a2 > 12089) {
            if (a2 > 86819) return 2;                    // 86820
            if (a2 == 86819) return 1;                   // 86819
        }
        return 3;                                        // fallback LABEL_58
    }
    if (a2 != 2144) {
        if (a2 <= 1312) {
            if (a2 < 1310) {
                switch (a2) {
                    case 1006: case 1009: case 1010: return 1;
                    case 1012: case 1013: case 1015: return 2;
                    case 1016:                       return 3;
                }
            }
            return 3;                                    // 1310..1312
        }
        if (a2 != 1452) {
            if (a2 != 2133) return 0;
            return 3;                                    // 2133
        }
        return 1;                                        // 1452
    }
    return 3;                                            // 2144
}

int scaleIndexC(int a2) {
    if (a2 <= 1005) {
        if (a2 < 1002) {
            switch (a2) {
                case 544:                     return 1;
                case 545: case 549: case 562: return 2;
                case 546: case 550:           return 3;
            }
        }
        return 0;                                        // 1002..1005
    }
    if (a2 > 2144) {
        if (a2 >= 18026) return 2;
        return 3;                                        // fallback LABEL_42
    }
    if (a2 <= 1309) {
        switch (a2) {
            case 1007: case 1009: case 1011: return 1;
            case 1012: case 1014: case 1015: return 2;
            case 1016:                       return 3;
        }
        return 3;                                        // (unreachable for valid ids)
    }
    return 3;                                            // 1310..2144
}

int scaleIndexD(int a2) {
    if (a2 > 1005) {
        if (a2 <= 1309) {
            switch (a2) {
                case 1008: case 1010: case 1011: return 1;
                case 1013: case 1014: case 1015: return 2;
                case 1016:                       return 3;
            }
            return 3;                                    // (unreachable for valid ids)
        }
        return 3;                                        // 1310..2144
    }
    if (a2 >= 1002) return 0;                             // 1002..1005
    return 3;                                            // < 1002 (546, 550)
}

// Eligibility gate (valid id lists for the 4 variants).
bool eligibleA(int a2) {
    switch (a2) {
        case 1002: case 1006: case 1007: case 1008: case 1012: case 1013: case 1014:
        case 1016: case 1310: case 1311: case 1312: case 548: case 549: case 550:
        case 86819: case 86820: case 541: case 1452: case 544: case 545: case 546:
        case 543: case 560: case 561: case 562: case 2133: case 12088: case 12089:
        case 2144: case 12058: case 2160: case 12001: case 18023: case 18024: case 18025:
            return true;
        default: return false;
    }
}
bool eligibleB(int a2) {
    switch (a2) {
        case 1003: case 1006: case 1009: case 1010: case 1012: case 1013: case 1015:
        case 1016: case 1310: case 1311: case 1312: case 547: case 548: case 549:
        case 550: case 543: case 86819: case 86820: case 542: case 1452: case 545:
        case 546: case 561: case 562: case 2133: case 12088: case 12089: case 2140:
        case 2144: case 12058: case 2160: case 18023: case 18024: case 18025:
            return true;
        default: return false;
    }
}
bool eligibleC(int a2) {
    switch (a2) {
        case 1004: case 1007: case 1009: case 1011: case 1012: case 1014: case 1015:
        case 1016: case 1310: case 1311: case 1312: case 549: case 550: case 544:
        case 545: case 546: case 562: case 86820: case 2133: case 12088: case 12089:
        case 2144: case 12058: case 2160: case 18023: case 18024: case 18025:
            return true;
        default: return false;
    }
}
bool eligibleD(int a2) {
    switch (a2) {
        case 1005: case 1008: case 1010: case 1011: case 1013: case 1014: case 1015:
        case 1016: case 1310: case 1311: case 1312: case 550: case 546: case 2133:
        case 12088: case 12089: case 2144: case 12058: case 2160: case 18023:
        case 18024: case 18025:
            return true;
        default: return false;
    }
}

} // namespace

double Item_ScaleStatByTypeA(const int32_t caps[4], int itemId, int value, int flag) {
    if (!eligibleA(itemId)) return 0.0;
    if (value < 1 || flag < 1) return 0.0;
    const int idx = scaleIndexA(itemId);
    const int cap = caps[idx];
    // "×2" ids: plateau 2000/2200; otherwise 1000/1100.
    if (itemId == 1312 || itemId == 550 || itemId == 2133 || itemId == 12088 ||
        itemId == 12089 || itemId == 2144 || itemId == 12058 || itemId == 2160 ||
        itemId == 18023 || itemId == 18024 || itemId == 18025) {
        if (value < cap) return Fcast(static_cast<double>(value) * 2000.0 / static_cast<double>(cap));
        return 2200.0;
    }
    if (value < cap) return Fcast(static_cast<double>(value) * 1000.0 / static_cast<double>(cap));
    return 1100.0;
}

double Item_ScaleStatByTypeB(const int32_t caps[4], int itemId, int value, int flag) {
    if (!eligibleB(itemId)) return 0.0;
    if (value < 1 || flag < 1) return 0.0;
    const int idx = scaleIndexB(itemId);
    const int cap = caps[idx];
    if (itemId == 1311) {
        if (value < cap) return Fcast(static_cast<double>(value) * 4000.0 / static_cast<double>(cap));
        return 4400.0;
    }
    if (value < cap) return Fcast(static_cast<double>(value) * 2000.0 / static_cast<double>(cap));
    return 2200.0;
}

double Item_ScaleStatByTypeC(const int32_t caps[4], int itemId, int value) {
    if (!eligibleC(itemId)) return 0.0;
    if (value < 1) return 0.0;
    const int idx = scaleIndexC(itemId);
    const int cap = caps[idx];
    if (itemId == 1310) {
        if (value < cap) return Fcast(static_cast<double>(value) * 4000.0 / static_cast<double>(cap));
        return 4400.0;
    }
    if (value < cap) return Fcast(static_cast<double>(value) * 2000.0 / static_cast<double>(cap));
    return 2200.0;
}

double Item_ScaleStatByTypeD(const int32_t caps[4], int itemId, int value) {
    if (!eligibleD(itemId)) return 0.0;
    if (value < 1) return 0.0;
    const int idx = scaleIndexD(itemId);
    const int cap = caps[idx];
    if (itemId == 1310) {
        if (value < cap) return Fcast(static_cast<double>(value) * 3600.0 / static_cast<double>(cap));
        return 4000.0;
    }
    if (value < cap) return Fcast(static_cast<double>(value) * 1800.0 / static_cast<double>(cap));
    return 2000.0;
}

// Item_MeetsEquipRequirement 0x64ECD0 — equipment/usage eligibility gate.
// Decompilation + disasm re-reviewed this mission. The 15 guards are evaluated in
// the EXACT ORDER of the binary: each `return false` corresponds to a distinct `xor eax,eax / jmp
// loc_64EFB4`. The final success is `return classify != 9 || rebirth >= 12`
// @0x64EFA9 (the binary does not return a constant 1).
bool Item_MeetsEquipRequirement(const ItemInfo& it, int equipSlot) {
    const SelfState& self = g_World.self;
    // dword_16747BC: rebirth tier (read by 12 of the 15 guards below).
    const int rebirth = g_Client.VarGet(0x16747BC);
    // *a2 (+0): the binary's comparisons are signed (a2 is an int*).
    const int32_t id = static_cast<int32_t>(it.itemId);

    // (1) FACTION — a2[53] = +212 @0x64ECDA-0x64ECF5:
    //     `if (a2[53] != 1 && a2[53] - 2 != g_LocalElementSecondary) return 0`.
    //     1 = master key (any faction); otherwise (faction - 2) must equal the local secondary element.
    const int32_t faction = static_cast<int32_t>(it.field212);
    if (faction != 1 && faction - 2 != self.elementSecondary)
        return false;                                            // @0x64ECF7

    // (2) SLOT — a2[54] = +216 vs this[54 + slot] @0x64ECFE-0x64ED20.
    //     SIGNED guards (`jl` @0x64ed02, `jg` @0x64ed08): only applies to slot ∈ [0..12];
    //     equipSlot == -1 (the "no slot" sentinel, e.g. push 0FFFFFFFFh @0x64af0b) skips it
    //     entirely — this is the case for 8 of the binary's 9 callers.
    //
    //     TODO [anchor 0x64ED20]: guard NOT PORTED (missing proof, rule #8).
    //     The right-hand term is `this[54 + slot]` with this = dword_1839568 (the singleton
    //     UI dialog manager — `mov ecx, offset dword_1839568` @0x5b23b9, and
    //     this of UI_InitAllDialogs 0x5ABF50 / UI_UpdateAllDialogs 0x5AC270), i.e. the
    //     13-dword array 0x1839640[0..12] (= 0x1839568 + 0xD8 + 4*slot). This array is in .bss
    //     (get_bytes(0x1839640, 56) = 56 zero bytes) and its PRODUCER remains unidentified:
    //     insn_query(op_any=0x1839640) over the 3 executable segments (1,053,354 instructions)
    //     returns NO write — all accesses go through computed base+index. As long as the
    //     semantics of these 13 dwords is unproven, we do not guess.
    //     Impact: none for equipSlot < 0; for equipSlot ∈ [0..12] (only UI_MainInventory_
    //     OnLButtonUp @0x5b23be passes a real slot), the guard is currently permissive.
    (void)equipSlot;

    // (3) SUMMED LEVEL — a2[59] + a2[58] = +236 + +232 @0x64ED29-0x64ED49:
    //     `if (a2[59] + a2[58] > g_SelfLevelBonus + g_SelfLevel) return 0`.
    //     This is the ONLY proven level requirement (itemLevel +204 plays no role here).
    if (static_cast<int32_t>(it.field236) + static_cast<int32_t>(it.field232) >
        self.levelBonus + self.level)
        return false;                                            // @0x64ED4B

    // (4) ids 13553/33553/53553 & rebirth < 6 @0x64ED55-0x64ED7A.
    if ((id == 13553 || id == 33553 || id == 53553) && rebirth < 6)
        return false;                                            // @0x64ED7C

    // (5) ids 13554/33554/53554 & rebirth < 12 @0x64ED86-0x64EDAB.
    if ((id == 13554 || id == 33554 || id == 53554) && rebirth < 12)
        return false;                                            // @0x64EDAD

    // (6) ranges 87206..87213 / 87228..87235 / 87250..87257 & rebirth < 12 @0x64EDFD.
    if (((id >= 87206 && id <= 87213) || (id >= 87228 && id <= 87235) ||
         (id >= 87250 && id <= 87257)) && rebirth < 12)
        return false;                                            // @0x64EDFF

    // (7) ids 216/217/218 & rebirth < 7 @0x64EE2E.
    if ((id == 216 || id == 217 || id == 218) && rebirth < 7)
        return false;                                            // @0x64EE30

    // (8) ids 86754/86756/86758 & rebirth < 6 @0x64EE5F.
    if ((id == 86754 || id == 86756 || id == 86758) && rebirth < 6)
        return false;                                            // @0x64EE61

    // (9) ids 86755/86757/86759 & rebirth < 12 @0x64EE90.
    if ((id == 86755 || id == 86757 || id == 86759) && rebirth < 12)
        return false;                                            // @0x64EE92

    // (10) skillFlag — a2[71] = +284 == 3 (upgrade) & rebirth < 12 @0x64EEAC.
    if (static_cast<int32_t>(it.skillFlag) == 3 && rebirth < 12)
        return false;                                            // @0x64EEAE

    // (11) ids 2303..2305 & rebirth < 7 @0x64EEDD.
    if ((id == 2303 || id == 2304 || id == 2305) && rebirth < 7)
        return false;                                            // @0x64EEDF

    // (12..15) gates by CLASS (Item_ClassifyRecord 0x5509A0). The binary re-calls the
    // function at each guard (no caching); it is pure, so a single C++ call is
    // numerically equivalent.
    const int cls = ClassifyRecord(it);
    if ((cls == 1 || cls == 4) && rebirth < 12) return false;    // @0x64EF13 / @0x64EF15
    if (cls == 2 && rebirth < 12)               return false;    // @0x64EF36 / @0x64EF38
    if ((cls == 5 || cls == 6) && rebirth < 12) return false;    // @0x64EF69 / @0x64EF6B
    if (cls == 8 && rebirth < 12)               return false;    // @0x64EF89 / @0x64EF8B

    // Exit @0x64EFA9: `return Item_ClassifyRecord(a2) != 9 || dword_16747BC >= 12;`
    return cls != 9 || rebirth >= 12;
}

} // namespace ts2::game

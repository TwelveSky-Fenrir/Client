// Game/ItemSystem.cpp — implémentation fidèle au désassemblage (voir en-tête).
// Toutes les constantes flottantes reproduisent les littéraux float32 d'origine
// (suffixe f promu en double) pour un résultat identique après troncature.
#include "Game/ItemSystem.h"

namespace ts2::game {

namespace {

// Crt_ftol 0x760810 : conversion double→int par troncature vers zéro (== cast C).
inline int Ftol(double x) { return static_cast<int>(x); }

// Cast float intermédiaire des Item_ScaleStatByType* : (float)(...) puis élargi.
inline double Fcast(double x) { return static_cast<double>(static_cast<float>(x)); }

} // namespace

// ---------------------------------------------------------------------
// ItemLookup — MobDb_GetEntry 0x4C3C00 (id 1-based, rejette les slots vides).
// ---------------------------------------------------------------------
ItemInfoView ItemLookup(const DataTable& itemTbl, uint32_t itemId) {
    if (itemId < 1 || itemId > itemTbl.count)
        return ItemInfoView(nullptr);
    const uint8_t* p = itemTbl.record(itemId - 1);
    if (!p)
        return ItemInfoView(nullptr);
    uint32_t id0 = 0;
    std::memcpy(&id0, p, 4);            // *(record) — l'id doit être non nul
    if (id0 == 0)
        return ItemInfoView(nullptr);
    return ItemInfoView(p);
}

// ---------------------------------------------------------------------
// Item_GetScaledStat 0x545980 — courbe (coef, offset) selon (statIdx, type),
// base polynomiale par paliers de niveau (45/100/113/146).
// ---------------------------------------------------------------------
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

    // Base par palier de niveau d'item (identique à toutes les courbes).
    double base;
    if (lvl < 100)       base = static_cast<double>(lvl - 45)  * 0.1f + 0.0f;
    else if (lvl < 113)  base = static_cast<double>(lvl - 100) * 0.2f + 6.0f;
    else if (lvl < 146)  base = static_cast<double>(lvl - 113) * 0.5f + 8.0f;
    else                 return 0;   // niveau >= 146 : hors courbe

    return Ftol(base * coef + off);
}

// ---------------------------------------------------------------------
// Item_SocketBonusInt 0x4CA620 — arme type 28, octet0 du mot socket.
// ---------------------------------------------------------------------
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

// ---------------------------------------------------------------------
// Item_SocketBonusFloat 0x4CAC30 — arme type 28, octet2 du mot socket.
// ---------------------------------------------------------------------
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
            // cat5 : multiplicateurs flottants (littéraux float32 d'origine).
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

// ---------------------------------------------------------------------
// Item_GetElementalBonus 0x54F590 — arme type 29, octet1 du mot socket.
// ---------------------------------------------------------------------
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

// ---------------------------------------------------------------------
// Item_DecodeGemBonus 0x54D390.
// ---------------------------------------------------------------------
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

// ---------------------------------------------------------------------
// Item_GetGradeValue 0x54D750.
// ---------------------------------------------------------------------
int Item_GetGradeValue(int a1) {
    if (a1 <= 1303) {
        if (a1 >= 1301) return 30;
        if (a1 == 559)  return 20;
        if (a1 <= 813 || a1 > 821) return 0;
        return 10;                                  // 814..821
    }
    if (a1 > 1936) {
        if (a1 <= 19021) {
            if (a1 < 19002) {                       // 1937..19001 : liste explicite
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
            return 10;                              // 19025..19044 ou 19051..19070
        }
        return 0;
    }
    if (a1 >= 1917) return 10;                       // 1917..1936
    // 1304..1916 : grades des items spéciaux.
    if (a1 == 1304 || a1 == 1305 || a1 == 1306 || a1 == 1314 ||
        a1 == 1318 || a1 == 1321 || a1 == 1324 || a1 == 1327) return 20;
    if (a1 == 1307 || a1 == 1308 || a1 == 1309 || a1 == 1315 ||
        a1 == 1319 || a1 == 1322 || a1 == 1325 || a1 == 1328) return 10;
    if (a1 == 1313 || a1 == 1317 || a1 == 1320 || a1 == 1323 || a1 == 1326) return 30;
    if (a1 == 1329 || a1 == 1330 || a1 == 1331) return 15;
    return 0;
}

// ---------------------------------------------------------------------
// Item_GetGradeMultiplier 0x54D9A0 (littéraux float32 d'origine).
// ---------------------------------------------------------------------
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

// ---------------------------------------------------------------------
// Item_GetEnchantStatDelta 0x553D50 — grande table (classe, indice de slot,
// clé, niveau d'enchant octet3 ∈ 1..59). Transcription exacte.
// ---------------------------------------------------------------------
int Item_GetEnchantStatDelta(int itemClass, int slot, uint32_t socketWord, int key) {
    const int lvl = Item_GetAttribByte3(socketWord);   // octet3 = niveau enchant
    if (lvl < 1 || lvl > 59) return 0;

    // ---- Classe 8 (cas spécial, uniquement slot 1) ----
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

    // ---- Classes 1 et 4 uniquement ----
    if (itemClass != 1 && itemClass != 4) return 0;

    // Slots d'armure {0,2,3,4,5}
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

    // Slot 7 (arme)
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

// ---------------------------------------------------------------------
// Item_ScaleStatByType* — sélection du plafond caps[idx] par id d'arme,
// puis mise à l'échelle (value/cap × plateau) ou plateau constant.
// Les helpers scaleIndex* reproduisent EXACTEMENT le branchement d'origine.
// ---------------------------------------------------------------------
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
        return 3;                                        // repli LABEL_58
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
        return 3;                                        // repli LABEL_42
    }
    if (a2 <= 1309) {
        switch (a2) {
            case 1007: case 1009: case 1011: return 1;
            case 1012: case 1014: case 1015: return 2;
            case 1016:                       return 3;
        }
        return 3;                                        // (inatteignable pour ids valides)
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
            return 3;                                    // (inatteignable pour ids valides)
        }
        return 3;                                        // 1310..2144
    }
    if (a2 >= 1002) return 0;                             // 1002..1005
    return 3;                                            // < 1002 (546, 550)
}

// Gate d'éligibilité (listes d'ids valides des 4 variantes).
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
    // Ids "×2" : plateau 2000/2200 ; sinon 1000/1100.
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

} // namespace ts2::game

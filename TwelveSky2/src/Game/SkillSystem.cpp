// Game/SkillSystem.cpp — implementation du systeme de competences (ts2::game).
// Transcription fidele du desassemblage de TwelveSky2.exe. Cf. SkillSystem.h.
#include "Game/SkillSystem.h"
#include <cstring>

namespace ts2::game {
namespace {

// Resolution 1-based d'un record ITEM_INFO (mirroir MobDb_GetEntry 0x4C3C00) :
// nul si id<1, id>count, ou record.dword0 == 0 (slot vide).
const uint8_t* ItemRecord(const DataTable& t, uint32_t itemId) {
    if (itemId < 1 || itemId > t.count) return nullptr;
    const uint8_t* rec = t.record(itemId - 1);
    if (!rec) return nullptr;
    if (Skill_ReadI32(rec, 0) == 0) return nullptr;
    return rec;
}

// Extraction des 12 octets des 3 dwords (little-endian), commune aux fonctions
// d'arbre Skill_UnpackTreeNodes / Skill_CountTreeNodes.
void UnpackBytes(uint32_t w0, uint32_t w1, uint32_t w2, uint8_t b[12]) {
    b[0] = uint8_t(w0);       b[1] = uint8_t(w0 >> 8);  b[2] = uint8_t(w0 >> 16);  b[3]  = uint8_t(w0 >> 24);
    b[4] = uint8_t(w1);       b[5] = uint8_t(w1 >> 8);  b[6] = uint8_t(w1 >> 16);  b[7]  = uint8_t(w1 >> 24);
    b[8] = uint8_t(w2);       b[9] = uint8_t(w2 >> 8);  b[10] = uint8_t(w2 >> 16); b[11] = uint8_t(w2 >> 24);
}

} // namespace

// ===================== SkillLevelTable =====================================

int SkillLevelTable::Min(int skillId) const {
    if (skillId < 1 || skillId > 350) return 0;
    const uint8_t* rec = table.record(static_cast<uint32_t>(skillId - 1));
    return rec ? Skill_ReadI32(rec, 0) : 0;
}
int SkillLevelTable::Max(int skillId) const {
    if (skillId < 1 || skillId > 350) return 0;
    const uint8_t* rec = table.record(static_cast<uint32_t>(skillId - 1));
    return rec ? Skill_ReadI32(rec, 4) : 0;
}

// ===================== SkillBar ============================================

int SkillBar::FindFree(int begin, int end) const {
    if (begin < 0) begin = 0;
    if (end > static_cast<int>(slots.size())) end = static_cast<int>(slots.size());
    for (int k = begin; k < end; ++k)
        if (slots[k].skillId == 0)   // fidele : g_LearnedSkills[k*8] < 1
            return k;
    return -1;
}

// ===================== SkillLearnFlags =====================================

bool SkillLearnFlags::IsTrackable(int skillId) {
    switch (skillId) {
        case 49: case 51: case 53:
        case 120: case 121: case 122:
        case 146: case 147: case 148: case 149: case 150: case 151: case 152:
        case 153: case 154: case 155: case 156: case 157: case 158: case 159:
        case 160: case 161: case 162: case 163: case 164:
        case 295: case 296:
        case 319: case 320: case 321: case 322: case 323:
            return true;
        default:
            return false;
    }
}
bool SkillLearnFlags::IsLearned(int skillId) const {
    if (!IsTrackable(skillId)) return false;   // fidele : default -> 0
    auto it = flags.find(skillId);
    return it != flags.end() && it->second == 1;
}

// ===================== Record & interpolation ==============================

const uint8_t* Skill_GetRecord(const DataTable& skillTbl, int skillId) {
    if (skillId < 1 || static_cast<uint32_t>(skillId) > skillTbl.count) return nullptr;
    const uint8_t* rec = skillTbl.record(static_cast<uint32_t>(skillId - 1));
    if (!rec) return nullptr;
    if (Skill_ReadI32(rec, skillinfo::kOffSkillId) == 0) return nullptr; // record vide
    return rec;
}

const char* Skill_GetName(const uint8_t* rec) {
    if (!rec) return "";
    // skillinfo::kOffName = record+4, C-string embarque (cf. commentaire d'offset
    // et UI_SkillLearn_OnLDown 0x5E20A5 : v14+4 passe directement en "%s").
    return reinterpret_cast<const char*>(rec + skillinfo::kOffName);
}

int Skill_GetIconIndex(const uint8_t* rec) {
    if (!rec) return 0;
    return Skill_ReadI32(rec, skillinfo::kOffIconIndex);
}

double Skill_InterpStat(const DataTable& skillTbl, int skillId, int level, int statIndex) {
    const uint8_t* rec = Skill_GetRecord(skillTbl, skillId);
    if (!rec) return 0.0;
    if (level < 1) return 0.0;
    if (statIndex < 1 || statIndex > 25) return 0.0;

    int32_t vMin = Skill_ReadI32(rec, skillinfo::kOffStatMin + 4u * (statIndex - 1));
    int32_t vMax = Skill_ReadI32(rec, skillinfo::kOffStatMax + 4u * (statIndex - 1));

    // Cas special stat#7 (portee distance) des competences 112..120 : penalite
    // ×0.7 sauf si le contexte range vaut 3 (mirroir dword_16851B8).
    if (statIndex == 7 && skillId >= 112 && skillId <= 120 && g_Skill112RangeMode != 3) {
        vMin = Skill_Ftol(static_cast<double>(vMin) * 0.699999988079071);
        vMax = Skill_Ftol(static_cast<double>(vMax) * 0.699999988079071);
    }

    int32_t maxLevel = Skill_ReadI32(rec, skillinfo::kOffLevelNorm);
    // Numerateur en arithmetique entiere, division en double (fidele au binaire).
    double num = static_cast<double>(level * (vMax - vMin));
    double v = static_cast<double>(vMin) + num / static_cast<double>(maxLevel);
    if (v > 0.0) return (v >= 1.0) ? v : 1.0;
    return 0.0;
}

// ===================== Cout MP =============================================

int Skill_CostById(int skillId, const SelfState& self, const DataTable& itemTbl) {
    // Table nominale codee en dur, skillId 1..138 (0 = passif).
    static const int kCost[139] = {
        0,   0,   0,   0,  15,  15,   0,   0,  20,  20,  45, //  0..10
        0,  25,  25,  50,   0, 120, 120,  45,   0,   0,       // 11..20
        0,   0,  15,  15,   0,   0,  25,  25,  50,   0,       // 21..30
       15,  15,  45,   0, 120, 120,  45,   0,   0,   0,       // 31..40
        0,  15,  15,   0,   0,  20,  20,  45,   0,  25,       // 41..50
       25,  50,   0, 120, 120,  45,   0,  20,  20,  25,       // 51..60
       25, 120, 120,  25,  25,  15,  15, 120, 120,  20,       // 61..70
       20,  25,  25, 120, 120,  30,  30,  30,  30, 150,       // 71..80
       30,   0,   0,   0,  20,  20,  25,  25, 120, 120,       // 81..90
       25,  25,  15,  15, 120, 120,  20,  20,  25,  25,       // 91..100
      120, 120,   0,   0,   0, 300, 300, 300, 300, 300,       //101..110
      300,  70,  70,  70,  70,  70,  70,  70,  70,  70,       //111..120
       20,  20,  25,  25, 120, 120,  25,  25,  15,  15,       //121..130
      120, 120,  20,  20,  25,  25, 120, 120                  //131..138
    };
    if (skillId >= 1 && skillId <= 138)
        return kCost[skillId];

    // Defaut : classe de l'arme equipee (self.equip[7] = dword_1673248).
    const uint8_t* rec = ItemRecord(itemTbl, self.equip[7].itemId);
    if (rec) {
        switch (Skill_ReadI32(rec, iteminfo::kOffTypeCode)) {
            case 0xD:  return 20;
            case 0xE:  return 25;
            case 0xF:  return 15;
            case 0x10: return 25;
            case 0x11: return 15;
            case 0x12: return 20;
            case 0x13: return 20;
            case 0x14: return 25;
            case 0x15: return 15;
            default:   return 0;
        }
    }
    return 15;
}

int Skill_CalcRegenPct(const SelfState& self, const DataTable& itemTbl) {
    int sum = 0;
    for (int i = 0; i < 13; ++i) {
        const uint8_t* rec = ItemRecord(itemTbl, self.equip[i].itemId);
        if (rec) sum += Skill_ReadI32(rec, iteminfo::kOffRegen);
    }
    return sum;
}

int Skill_CalcRealMpCost(const DataTable& skillTbl, int skillId, int level, int regenPct) {
    int cost = Skill_Ftol(Skill_InterpStat(skillTbl, skillId, level, 1));
    if (regenPct > 0)
        cost -= regenPct * cost / 100;   // arithmetique entiere, fidele
    return cost;
}

SkillCastResult Skill_TryConsumeMp(SelfState& self, const DataTable& skillTbl,
                                   const DataTable& itemTbl, int skillId, int level) {
    SkillCastResult r;
    int regen = Skill_CalcRegenPct(self, itemTbl);
    r.cost = Skill_CalcRealMpCost(skillTbl, skillId, level, regen);
    if (self.mp >= r.cost) {           // fidele : dword_1687378 >= cost
        self.mp -= r.cost;
        r.ok = true;
    }
    return r;
}

// ===================== Disponibilite ======================================

bool Skill_IsAvailableByLevel(const SkillLevelTable& t, int skillId,
                              int level, int levelBonus, int rebirth) {
    const int lvlEff = level + levelBonus;
    if (skillId > 164) {
        switch (skillId) {
            case 295: case 296: case 322: case 323:
                if (lvlEff < t.Min(skillId)) return false;
                if (lvlEff > t.Max(skillId)) return false;
                if (rebirth >= 7) {
                    if (skillId != 296 && skillId != 323) return false;
                } else {
                    if (skillId != 295 && skillId != 322) return false;
                }
                return true;
            case 319: case 320: case 321:
                return lvlEff >= t.Min(skillId) && lvlEff <= t.Max(skillId);
            default:
                return false;
        }
    }
    // skillId <= 164
    if (skillId < 146) {
        switch (skillId) {
            case 49: case 51: case 53:
            case 120: case 121: case 122:
                break;               // reconnu -> controle de niveau
            default:
                return false;
        }
    }
    // skillId dans [146..164] ou {49,51,53,120,121,122}
    return lvlEff >= t.Min(skillId) && lvlEff <= t.Max(skillId);
}

bool Skill_IsAvailableByBranch(const SkillLevelTable& t, int skillId,
                               int level, int levelBonus, int element) {
    const int lvlEff = level + levelBonus;
    const bool inRange = (lvlEff >= t.Min(skillId) && lvlEff <= t.Max(skillId));
    switch (skillId) {
        case 19: case 25: case 31: case 175: case 178: case 182: case 186: case 190:
            return inRange && element == 0;
        case 20: case 26: case 32: case 176: case 179: case 183: case 187: case 191:
            return inRange && element == 1;
        case 21: case 27: case 33: case 177: case 180: case 184: case 188: case 192:
            return inRange && element == 2;
        case 34: case 35: case 36: case 181: case 185: case 189: case 193:
            return inRange && element == 3;
        default:
            return false;
    }
}

// ===================== Resolution de slot par niveau =======================
// Transcription 1:1 de Skill_ResolveLevelSlot (0x4FB370) : chaine de tranches
// de niveau strictement imbriquee (if/else mutuellement exclusifs). La 1re
// tranche [Min(id),Max(id)] contenant lvlEff fixe (row,col).

void Skill_ResolveLevelSlot(const SkillLevelTable& t, int level, int levelBonus,
                            int rebirth, int& outRow, int& outCol) {
    outRow = -1;
    outCol = -1;
    const int lvlEff = level + levelBonus;
    auto notIn = [&](int id) { return lvlEff < t.Min(id) || lvlEff > t.Max(id); };

    if (notIn(49)) {
      if (notIn(51)) {
        if (notIn(53)) {
          if (notIn(146)) {
            if (notIn(147)) {
              if (notIn(148)) {
                if (notIn(149)) {
                  if (notIn(150)) {
                    if (notIn(151)) {
                      if (notIn(152)) {
                        if (notIn(153)) {
                          if (notIn(154)) {
                            if (notIn(155)) {
                              if (notIn(156)) {
                                if (notIn(157)) {
                                  if (notIn(158)) {
                                    if (notIn(159)) {
                                      if (notIn(160)) {
                                        if (notIn(161)) {
                                          if (notIn(162)) {
                                            if (notIn(163)) {
                                              if (notIn(164)) {
                                                if (notIn(120)) {
                                                  if (notIn(121)) {
                                                    if (notIn(122)) {
                                                      if (!notIn(295)) {
                                                        // tranche 295 : gate renaissance
                                                        if (rebirth >= 4) {
                                                          outRow = 0;
                                                          outCol = (rebirth >= 7) ? 10 : 9;
                                                        } else {
                                                          outRow = 0; outCol = 9;
                                                        }
                                                      }
                                                    } else { outRow = 0; outCol = 8; }
                                                  } else { outRow = 0; outCol = 7; }
                                                } else { outRow = 0; outCol = 6; }
                                              } else { outRow = 2; outCol = 9; }
                                            } else { outRow = 2; outCol = 8; }
                                          } else { outRow = 2; outCol = 7; }
                                        } else { outRow = 1; outCol = 5; }
                                      } else { outRow = 0; outCol = 5; }
                                    } else { outRow = 2; outCol = 6; }
                                  } else { outRow = 1; outCol = 4; }
                                } else { outRow = 0; outCol = 4; }
                              } else { outRow = 2; outCol = 5; }
                            } else { outRow = 1; outCol = 3; }
                          } else { outRow = 0; outCol = 3; }
                        } else { outRow = 2; outCol = 4; }
                      } else { outRow = 2; outCol = 3; }
                    } else { outRow = 2; outCol = 2; }
                  } else { outRow = 1; outCol = 2; }
                } else { outRow = 0; outCol = 2; }
              } else { outRow = 2; outCol = 1; }
            } else { outRow = 1; outCol = 1; }
          } else { outRow = 0; outCol = 1; }
        } else { outRow = 2; outCol = 0; }
      } else { outRow = 1; outCol = 0; }
    } else { outRow = 0; outCol = 0; }
}

// ===================== Valeurs par classe / palier =========================

int Skill_GetValueByClassA(int classId, int tier) {
    if (tier < 1 || tier > 12) return 0;
    static const int c1[12] = { 40, 80,120,160,200,240,280,320,360,400,440,520 };
    static const int c2[12] = {300,600,900,1200,1500,1800,2100,2400,2700,3000,3300,3600};
    static const int c3[12] = { 50,100,150,200,250,300,350,400,450,500,550,600 };
    static const int c5[12] = { 30, 60, 90,120,150,180,210,240,270,300,330,360 };
    switch (classId) {
        case 1: return c1[tier - 1];
        case 2: return c2[tier - 1];
        case 3: return c3[tier - 1];
        case 5: return c5[tier - 1];
        default: return 0;
    }
}

int Skill_GetValueByClassB(int classId, int tier) {
    if (tier < 1 || tier > 12) return 0;
    static const int c3[12] = {200,400,600,800,1000,1200,1400,1600,1800,2000,2200,2400};
    static const int c7[12] = { 60,120,180,240,300,360,420,480,540,600,660,720};
    switch (classId) {
        case 3: return c3[tier - 1];
        case 7: return c7[tier - 1];
        default: return 0;
    }
}

int Skill_GetUpgradeCostTier(int level) {
    static const int kTier[13] = {0,3500,4500,5500,6500,7500,8000,8500,9000,9500,10000,10500,11000};
    if (level < 0 || level > 12) return 0;
    return kTier[level];
}

// ===================== Arbre de talents ====================================

int Skill_UnpackTreeNodes(uint32_t w0, uint32_t w1, uint32_t w2, int out[5]) {
    uint8_t b[12];
    UnpackBytes(w0, w1, w2, b);
    const int sentinel = b[1];
    if (sentinel == 0) {
        out[0] = out[1] = out[2] = out[3] = out[4] = 0;
        return 0;
    }
    out[0] = b[3]  + 1000 * b[2];
    out[1] = b[5]  + 1000 * b[4];
    out[2] = b[7]  + 1000 * b[6];
    out[3] = b[9]  + 1000 * b[8];
    out[4] = b[11] + 1000 * b[10];
    return sentinel;
}

int Skill_CountTreeNodes(uint32_t w0, uint32_t w1, uint32_t w2) {
    uint8_t b[12];
    UnpackBytes(w0, w1, w2, b);
    if (b[1] == 0) return 0;                     // sentinelle nulle
    for (int i = 0; i < 5; ++i)
        if (b[2 + 2 * i] == 0 && b[3 + 2 * i] == 0)
            return i;                            // 1re paire (lo,hi) nulle
    return 5;
}

// ===================== Apprentissage =======================================

uint32_t Skill_TaughtSkillIdFromItem(const uint8_t* itemRec) {
    if (!itemRec) return 0;
    return static_cast<uint32_t>(Skill_ReadI32(itemRec, iteminfo::kOffTaughtSkill));
}

int Skill_Learn(SkillBar& bar, SelfState& self, const DataTable& skillTbl, uint32_t taughtSkillId) {
    const uint8_t* rec = Skill_GetRecord(skillTbl, static_cast<int>(taughtSkillId));
    if (!rec) return -1;

    const int section = Skill_ReadI32(rec, skillinfo::kOffSection); // idx135
    int slot = -1;
    switch (section) {
        case 1: // posture/base -> [0,10)
            slot = bar.FindFree(0, 10);
            break;
        case 2: // -> [20,30)
            slot = bar.FindFree(20, 30);
            break;
        case 3: // -> [0,20) puis [30,40)
        case 4:
            slot = bar.FindFree(0, 20);
            if (slot < 0) slot = bar.FindFree(30, 40);
            break;
        default:
            return -1;
    }
    if (slot < 0) return -1;

    const int32_t spCost = Skill_ReadI32(rec, skillinfo::kOffSpCost);
    self.skillPoints -= spCost;                                  // g_SkillPointPool -= cout
    bar.slots[slot].skillId = static_cast<uint32_t>(Skill_ReadI32(rec, skillinfo::kOffSkillId));
    bar.slots[slot].spCost  = spCost;
    return slot;
}

} // namespace ts2::game

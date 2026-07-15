// Game/SkillCombat.cpp — implémentation de l'intégration combat des compétences.
// Transcription fidèle du désassemblage de TwelveSky2.exe. Cf. SkillCombat.h.
#include "Game/SkillCombat.h"
#include <cmath>

namespace ts2::game {
namespace {

// Comparaison d'étiquette 13 o (mirroir Crt_Strcmp 0x75CF20 appliqué à des buffers
// bornés, terminés par un octet nul comme le reste des chaînes du binaire).
bool TagEquals(const char a[13], const char b[13]) {
    return std::strncmp(a, b, 13) == 0;
}

// ---------------------------------------------------------------------------
// Transcription FIDÈLE de Motion_InitFrameTable 0x4F1380 (EA 0x4F1380..0x4F69E7),
// switch(i) sur 350 cas codés en dur -- SEUL le bloc {min@+0,max@+4} est repris ici
// (le bloc {comboGroup,flag} à +700 dwords appartient à SkillBranchTable, déjà porté
// ailleurs dans ce fichier via sub_4FAB60 -- hors périmètre de cette table). `i` reste
// le même index 0-based que le binaire (i = skillId-1, cohérent avec
// SkillLevelTable::Min/Max qui indexent déjà record(skillId-1), cf. SkillSystem.cpp) :
// aucune renumérotation, transcription case-par-case pour éliminer tout risque de
// décalage d'index.
SkillLevelTable BuildSkillLevelTableHardcoded() {
    SkillLevelTable t;
    t.table.count  = 350;
    t.table.stride = 8;
    t.table.data.assign(350u * 8u, 0);
    auto set = [&](int i, int32_t mn, int32_t mx) {
        int32_t buf[2] = {mn, mx};
        std::memcpy(t.table.data.data() + static_cast<size_t>(i) * 8, buf, sizeof(buf));
    };
    for (int i = 0; i < 350; ++i) {
        switch (i) {
        case 0: case 3:                         set(i, 1, 157);   break;
        case 1:                                 set(i, 1, 157);   break;
        case 2:                                 set(i, 1, 157);   break;
        case 4: case 278:                       set(i, 157, 157); break;
        case 5: case 8:                         set(i, 1, 157);   break;
        case 6:                                 set(i, 1, 157);   break;
        case 7:                                 set(i, 1, 157);   break;
        case 9: case 122: case 274: case 286:   set(i, 157, 157); break;
        case 10: case 13:                       set(i, 1, 157);   break;
        case 11:                                set(i, 1, 157);   break;
        case 12:                                set(i, 1, 157);   break;
        case 14: case 282:                      set(i, 157, 157); break;
        case 15: case 45:                       set(i, 1, 157);   break;
        case 16:                                set(i, 1, 157);   break;
        case 17: case 70:                       set(i, 1, 157);   break;
        case 18: case 94: case 205: case 258:                     set(i, 146, 157); break;
        case 19: case 95: case 112: case 206: case 253:           set(i, 146, 157); break;
        case 20: case 96: case 113: case 207: case 254: case 263: set(i, 146, 157); break;
        case 21: case 71:                       set(i, 1, 157);   break;
        case 22: case 46:                       set(i, 1, 157);   break;
        case 23:                                set(i, 1, 157);   break;
        case 24:                                set(i, 150, 153); break;
        case 25:                                set(i, 150, 153); break;
        case 26:                                set(i, 150, 153); break;
        case 27:                                set(i, 1, 157);   break;
        case 28: case 72:                       set(i, 1, 157);   break;
        case 29: case 47:                       set(i, 1, 157);   break;
        case 30:                                set(i, 154, 157); break;
        case 31:                                set(i, 154, 157); break;
        case 32:                                set(i, 154, 157); break;
        case 33: case 97: case 208: case 264:   set(i, 146, 157); break;
        case 34:                                set(i, 150, 153); break;
        case 35:                                set(i, 154, 157); break;
        case 36: case 51: case 123:             set(i, 1, 157);   break;
        case 37:                                set(i, 90, 157);  break;
        case 38:                                set(i, 90, 112);  break;
        case 39: case 42: case 62:              set(i, 90, 157);  break;
        case 40: case 43: case 66:              set(i, 90, 157);  break;
        case 41: case 44: case 67:              set(i, 90, 157);  break;
        case 48:                                set(i, 10, 112);  break;
        case 49:                                set(i, 1, 157);   break;
        case 50:                                set(i, 20, 29);   break;
        case 52:                                set(i, 30, 39);   break;
        case 53: case 83: case 199: case 271: case 302: case 341: set(i, 157, 157); break;
        case 54:                                set(i, 113, 157); break;
        case 55: case 58: case 61:              set(i, 90, 157);  break;
        case 56: case 59: case 65:              set(i, 90, 157);  break;
        case 57: case 60: case 69:              set(i, 90, 157);  break;
        case 63:                                set(i, 90, 157);  break;
        case 64:                                set(i, 90, 157);  break;
        case 68:                                set(i, 90, 157);  break;
        case 73:                                set(i, 113, 157); break;
        case 74:                                set(i, 146, 157); break;
        case 75: case 109: case 250: case 259:  set(i, 146, 157); break;
        case 76: case 260:                      set(i, 146, 157); break;
        case 77: case 114: case 255:            set(i, 146, 157); break;
        case 78: case 115: case 256: case 265:  set(i, 146, 157); break;
        case 79: case 90: case 110: case 201: case 251:           set(i, 146, 157); break;
        case 80: case 91: case 111: case 202: case 252: case 261: set(i, 146, 157); break;
        case 81: case 92: case 203: case 262:   set(i, 146, 157); break;
        case 82: case 93: case 116: case 204: case 257:           set(i, 146, 157); break;
        case 84:                                set(i, 146, 156); break;
        case 85:                                set(i, 150, 153); break;
        case 86:                                set(i, 154, 156); break;
        case 87:                                set(i, 157, 157); break;
        case 88:                                set(i, 113, 157); break;
        case 89: case 200:                      set(i, 146, 157); break;
        case 98:                                set(i, 154, 157); break;
        case 99:                                set(i, 154, 157); break;
        case 100: case 103:                     set(i, 113, 157); break;
        case 101:                               set(i, 113, 157); break;
        case 102: case 108:                     set(i, 113, 157); break;
        case 104:                               set(i, 113, 157); break;
        case 105:                               set(i, 113, 157); break;
        case 106:                               set(i, 113, 157); break;
        case 107:                               set(i, 113, 157); break;
        case 117: case 338:                     set(i, 157, 157); break;
        case 119:                               set(i, 146, 156); break;
        case 120:                               set(i, 150, 153); break;
        case 121:                               set(i, 154, 156); break;
        case 124:                               set(i, 145, 157); break;
        case 125: case 128: case 213: case 225: set(i, 100, 157); break;
        case 126: case 217: case 229:           set(i, 100, 157); break;
        case 127: case 209: case 221:           set(i, 100, 157); break;
        case 129: case 132: case 214: case 226: set(i, 100, 157); break;
        case 130: case 218: case 230:           set(i, 100, 157); break;
        case 131: case 210: case 222:           set(i, 100, 157); break;
        case 133: case 136: case 215: case 227: set(i, 100, 157); break;
        case 134: case 219: case 231:           set(i, 100, 157); break;
        case 135: case 211: case 223:           set(i, 100, 157); break;
        case 137: case 164:                     set(i, 1, 157);   break;
        case 138: case 165: case 296: case 297: case 298: set(i, 1, 157); break;
        case 139: case 142:                     set(i, 113, 157); break;
        case 140:                               set(i, 113, 157); break;
        case 141:                               set(i, 113, 157); break;
        case 143:                               set(i, 113, 145); break;
        case 144:                               set(i, 146, 157); break;
        case 145:                               set(i, 40, 49);   break;
        case 146:                               set(i, 50, 59);   break;
        case 147:                               set(i, 60, 69);   break;
        case 148:                               set(i, 70, 79);   break;
        case 149:                               set(i, 80, 89);   break;
        case 150:                               set(i, 90, 99);   break;
        case 151:                               set(i, 100, 105); break;
        case 152:                               set(i, 106, 112); break;
        case 153:                               set(i, 113, 145); break;
        case 154:                               set(i, 116, 118); break;
        case 155:                               set(i, 119, 121); break;
        case 156:                               set(i, 122, 124); break;
        case 157:                               set(i, 125, 127); break;
        case 158:                               set(i, 128, 130); break;
        case 159:                               set(i, 131, 133); break;
        case 160:                               set(i, 134, 136); break;
        case 161:                               set(i, 137, 139); break;
        case 162:                               set(i, 140, 142); break;
        case 163:                               set(i, 143, 145); break;
        case 166:                               set(i, 113, 157); break;
        case 167:                               set(i, 113, 157); break;
        case 168:                               set(i, 113, 157); break;
        case 169:                               set(i, 1, 157);   break;
        case 170: case 173: case 212: case 224: set(i, 100, 157); break;
        case 171: case 216: case 228:           set(i, 100, 157); break;
        case 172: case 220: case 232:           set(i, 100, 157); break;
        case 174:                               set(i, 100, 112); break;
        case 175:                               set(i, 100, 112); break;
        case 176:                               set(i, 100, 112); break;
        case 177:                               set(i, 113, 122); break;
        case 178:                               set(i, 113, 122); break;
        case 179:                               set(i, 113, 122); break;
        case 180:                               set(i, 113, 122); break;
        case 181:                               set(i, 123, 132); break;
        case 182:                               set(i, 123, 132); break;
        case 183:                               set(i, 123, 132); break;
        case 184:                               set(i, 123, 132); break;
        case 185:                               set(i, 133, 142); break;
        case 186:                               set(i, 133, 142); break;
        case 187:                               set(i, 133, 142); break;
        case 188:                               set(i, 133, 142); break;
        case 189:                               set(i, 113, 145); break;
        case 190:                               set(i, 113, 145); break;
        case 191:                               set(i, 113, 145); break;
        case 192:                               set(i, 113, 145); break;
        case 193:                               set(i, 157, 157); break;
        case 194:                               set(i, 100, 112); break;
        case 195:                               set(i, 113, 145); break;
        case 196:                               set(i, 123, 132); break;
        case 197:                               set(i, 133, 142); break;
        case 198:                               set(i, 143, 145); break;
        case 233: case 236: case 239: case 242: case 245: case 248:
        case 305: case 309: case 324: case 327: set(i, 157, 157); break;
        case 234: case 237: case 240: case 243: case 246:
        case 303: case 306: case 310: case 325: case 328: set(i, 157, 157); break;
        case 235: case 238: case 241: case 244: case 247: case 291:
        case 292: case 293: case 304: case 311: case 326: case 329: set(i, 157, 157); break;
        case 249:                               set(i, 146, 157); break;
        case 266:                               set(i, 70, 112);  break;
        case 267:                               set(i, 113, 145); break;
        case 268:                               set(i, 146, 157); break;
        case 269: case 272:                     set(i, 157, 157); break;
        case 270: case 273: case 301: case 307: case 323: set(i, 157, 157); break;
        case 275: case 287:                     set(i, 157, 157); break;
        case 276: case 288:                     set(i, 157, 157); break;
        case 277: case 289:                     set(i, 157, 157); break;
        case 279:                               set(i, 157, 157); break;
        case 280:                               set(i, 157, 157); break;
        case 281:                               set(i, 157, 157); break;
        case 283:                               set(i, 157, 157); break;
        case 284:                               set(i, 157, 157); break;
        case 285:                               set(i, 157, 157); break;
        case 290:                               set(i, 1, 157);   break;
        case 294: case 295: case 313: case 316: case 331: case 334: set(i, 157, 157); break;
        case 299: case 322:                     set(i, 157, 157); break;
        case 300:                               set(i, 157, 157); break;
        case 312: case 315: case 330: case 333: set(i, 157, 157); break;
        case 314: case 317: case 332: case 335: case 340: set(i, 157, 157); break;
        case 318:                               set(i, 146, 149); break;
        case 319:                               set(i, 150, 153); break;
        case 320:                               set(i, 154, 156); break;
        case 321:                               set(i, 157, 157); break;
        case 339:                               set(i, 157, 157); break;
        default:
            break; // reste {0,0}, fidèle (skillId hors table, jamais assigné par le switch)
        }
    }
    return t;
}

} // namespace

// ===================== Exposition globale (Docs/TS2_COMBAT_ELEMENT_GATING.md) ======

ElementPairTable Combat_ReadLocalElementPairs() {
    ElementPairTable t;
    t.a = g_Client.VarGet(kElementPairAAddr);
    t.b = g_Client.VarGet(kElementPairBAddr);
    t.c = g_Client.VarGet(kElementPairCAddr);
    t.d = g_Client.VarGet(kElementPairDAddr);
    return t;
}

const SkillLevelTable& GetSkillLevelTable() {
    static const SkillLevelTable table = BuildSkillLevelTableHardcoded();
    return table;
}

// ===================== Stances actives ======================================

int Skill_GetActiveStance(const SelfState& self, const CombatMorphState& morph,
                           const SkillLevelTable& lvlTbl) {
    const int lvlEff = self.level + self.levelBonus;
    if (lvlEff >= lvlTbl.Min(49) && lvlEff <= lvlTbl.Max(49))   return 49;
    if (lvlEff >= lvlTbl.Min(120) && lvlEff <= lvlTbl.Max(120)) return 120;
    if (lvlEff >= lvlTbl.Min(154) && lvlEff <= lvlTbl.Max(154)) return 154;
    if (lvlEff < lvlTbl.Min(295) || lvlEff > lvlTbl.Max(295)) return 0;
    if (morph.rebirthTier < 4) return 295;
    if (morph.rebirthTier >= 7) return 296;
    return 295;
}

int Skill_GetActiveStance2(const SelfState& self, const CombatMorphState& morph,
                            const SkillLevelTable& lvlTbl) {
    const int lvlEff = self.level + self.levelBonus;
    if (lvlEff >= lvlTbl.Min(319) && lvlEff <= lvlTbl.Max(319)) return 319;
    if (lvlEff >= lvlTbl.Min(320) && lvlEff <= lvlTbl.Max(320)) return 320;
    if (lvlEff >= lvlTbl.Min(321) && lvlEff <= lvlTbl.Max(321)) return 321;
    if (lvlEff < lvlTbl.Min(322) || lvlEff > lvlTbl.Max(322)) return 0;
    if (morph.rebirthTier >= 7) return 323;
    return 322;
}

bool Skill_IsCurrentStanceSet(int currentActionId) {
    if (currentActionId > 122) {
        switch (currentActionId) {
            case 146: case 147: case 148: case 149: case 150: case 151: case 152:
            case 153: case 154: case 155: case 156: case 157: case 158: case 159:
            case 160: case 161: case 162: case 163: case 164:
            case 295: case 296: case 319: case 320: case 321: case 322: case 323:
                return true;
            default:
                return false;
        }
    }
    return currentActionId >= 120 || currentActionId == 49 || currentActionId == 51 ||
           currentActionId == 53;
}

// ===================== Tables de motion =====================================

int Skill_GetComboMotionId(int weaponType, int index) {
    switch (weaponType) {
        case 1:
            switch (index) {
                case 0:  return 49;
                case 1:  return 146;
                case 2:  return 149;
                case 3:  return 154;
                case 4:  return 157;
                case 5:  return 160;
                case 6:  return 120;
                case 7:  return 121;
                case 8:  return 122;
                case 9:  return 295;
                case 10: return 296;
                default: return -1;
            }
        case 2:
            switch (index) {
                case 0: return 51;
                case 1: return 147;
                case 2: return 150;
                case 3: return 155;
                case 4: return 158;
                case 5: return 161;
                default: return -1;
            }
        case 3:
            switch (index) {
                case 0: return 53;
                case 1: return 148;
                case 2: return 151;
                case 3: return 152;
                case 4: return 153;
                case 5: return 156;
                case 6: return 159;
                case 7: return 162;
                case 8: return 163;
                case 9: return 164;
                default: return -1;
            }
        case 4:
            switch (index) {
                case 0: return 319;
                case 1: return 320;
                case 2: return 321;
                case 3: return 322;
                case 4: return 323;
                default: return -1;
            }
        default:
            return -1;
    }
}

int Skill_GetMotionId2(int branch, int index) {
    switch (branch) {
        case 0:
            switch (index) {
                case 0: return 175; case 1: return 178; case 2: return 182;
                case 3: return 186; case 4: return 190; case 5: return 19;
                case 6: return 25;  case 7: return 31;  default: return -1;
            }
        case 1:
            switch (index) {
                case 0: return 176; case 1: return 179; case 2: return 183;
                case 3: return 187; case 4: return 191; case 5: return 20;
                case 6: return 26;  case 7: return 32;  default: return -1;
            }
        case 2:
            switch (index) {
                case 0: return 177; case 1: return 180; case 2: return 184;
                case 3: return 188; case 4: return 192; case 5: return 21;
                case 6: return 27;  case 7: return 33;  default: return -1;
            }
        case 3:
            switch (index) {
                case 0: return 0;   case 1: return 181; case 2: return 185;
                case 3: return 189; case 4: return 193; case 5: return 34;
                case 6: return 35;  case 7: return 36;  default: return -1;
            }
        default:
            return -1;
    }
}

int Skill_GetSpecialMotionId(int index) {
    switch (index) {
        case 0: return 267;
        case 1: return 268;
        case 2: return 269;
        case 3: return 250;
        default: return -1;
    }
}

bool Skill_IsSpecialUsable(int specialId, const SelfState& self, const CombatMorphState& morph,
                            const SkillLevelTable& lvlTbl) {
    if (!(specialId == 250 || (specialId > 266 && specialId <= 269))) return false;
    const int lvlEff = self.level + self.levelBonus;
    if (lvlEff < lvlTbl.Min(specialId)) return false;
    if (lvlEff > lvlTbl.Max(specialId)) return false;
    if (specialId == 250)
        return morph.rebirthTier > 0 || self.levelBonus == 12;
    if (specialId != 269) return true;
    return morph.rebirthTier == 0 && self.levelBonus < 12;
}

bool Skill_IsCurrentSpecial(int currentActionId) {
    return currentActionId == 250 || (currentActionId > 266 && currentActionId <= 269);
}

int Skill_GetBuffMotionId(int index) {
    switch (index) {
        case 0:  return 241; case 1:  return 242; case 2:  return 243;
        case 3:  return 244; case 4:  return 245; case 5:  return 246;
        case 6:  return 247; case 7:  return 248; case 8:  return 249;
        case 9:  return 292; case 10: return 293; case 11: return 294;
        case 12: return 311; case 13: return 312; case 14: return 325;
        case 15: return 326; case 16: return 327; case 17: return 328;
        case 18: return 329; case 19: return 330;
        default: return -1;
    }
}

int Skill_CheckBuffState(int buffSkillId, const BuffLearnedGrids& grids,
                          const char currentTag[13], int localElement) {
    const BuffLearnedGrid* grid = nullptr;
    switch (buffSkillId) {
        case 297: grid = &grids.g297; break;
        case 298: grid = &grids.g298; break;
        case 299: grid = &grids.g299; break;
        default:  return 2;
    }
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 5; ++j)
            if (TagEquals(grid->cells[static_cast<size_t>(i)][static_cast<size_t>(j)].data(), currentTag))
                return (i != localElement) ? 1 : 0;
    return 2;
}

int Skill_GetBuffLevel(int buffSkillId, const BuffLevels& levels) {
    switch (buffSkillId) {
        case 297: return levels.v297;
        case 298: return levels.v298;
        case 299: return levels.v299;
        default:  return 0;
    }
}

bool Skill_IsCurrentBuff(int currentActionId) {
    return currentActionId >= 297 && currentActionId <= 299;
}

// ===================== Remappage par arme ===================================

int Skill_RemapByWeapon(int weaponType, int skillId, const SkillBranchTable& branch,
                         const ElementPairTable& pairs) {
    const int b0 = branch.Get(skillId);
    if (b0 == -1) return skillId;
    if (b0 == weaponType) return skillId;
    if (b0 == pairs.Paired(weaponType)) return skillId;

    switch (weaponType) {
        case 0: {
            const int v = branch.Get(skillId);
            return (v <= 0 || v > 3) ? 1 : 38;
        }
        case 1: {
            const int v = branch.Get(skillId);
            return (v != 0 && (v <= 1 || v > 3)) ? 6 : 38;
        }
        case 2: {
            const int v = branch.Get(skillId);
            return (v >= 0 && (v <= 1 || v == 3)) ? 38 : 11;
        }
        case 3: {
            // Comparaison NON SIGNÉE fidèle au binaire (`> 2u`) : -1 (0xFFFFFFFF) échoue.
            const uint32_t v = static_cast<uint32_t>(branch.Get(skillId));
            return (v > 2u) ? 140 : 38;
        }
        default:
            return 0;
    }
}

// ===================== Hotkeys ==============================================

bool Skill_IsHotkeyPressed(const HotkeyBindTable& binds, int page, int slot,
                            const HotkeySnapshot& keys, int morphUiMode, int opcode) {
    if (slot == -1) return false;
    if (page < 0 || static_cast<size_t>(page) >= binds.pages.size()) return false;
    if (slot < 0 || slot >= 14) return false;
    const auto& b = binds.pages[static_cast<size_t>(page)][static_cast<size_t>(slot)];
    if (b.bound != 1) return false;
    if (b.opcode != opcode) return false;
    return keys.Pressed(slot, morphUiMode);
}

// ===================== Cast au curseur ======================================

bool Skill_CanCastAtCursor(const float selfPos[3], const SelfState& self, const GameDatabases& db,
                            const CombatMorphState& morph, const SelectedCastSlot& slot,
                            int screenX, int screenY, ITerrainPicker& picker) {
    const bool targeted = slot.selected && slot.bound == 1 &&
                           (slot.typeCode == 3 || slot.typeCode == 22 || slot.typeCode == 41);

    if (!targeted) {
        if (picker.IsPointBlocked(selfPos)) return false;
        float hit[3] = {0, 0, 0};
        return picker.PickRayScreen(screenX, screenY, /*wantEntityHit=*/false, hit);
    }

    if (!Skill_IsCurrentAttackSet(morph.currentActionId)) return false;

    // Picking « verrouillé sur cible » (mirroir Terrain_PickRayScreen(...,1) 0x699A80).
    // Le binaire retente UNE fois si le 1er appel échoue avant d'abandonner (0x54105f).
    float hit[3] = {0, 0, 0};
    if (!picker.PickRayScreen(screenX, screenY, /*wantEntityHit=*/true, hit)) {
        if (!picker.PickRayScreen(screenX, screenY, /*wantEntityHit=*/true, hit)) return false;
    }

    const float dx = selfPos[0] - hit[0];
    const float dy = selfPos[1] - hit[1];
    const float dz = selfPos[2] - hit[2];
    const double dist = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy +
                                   static_cast<double>(dz) * dz);
    if (dist < 10.0) return false;

    const int resist = CalcElementResist(self, db, slot.typeCode);
    const double range = Skill_InterpStat(db.skill, slot.typeCode, resist + slot.level, 6);
    return range >= dist;
}

// ===================== Cast d'une compétence en attente =====================

int Skill_ResolveCastOpGroup(int skillId) {
    switch (skillId) {
        case 4: case 23: case 42:    return 38;
        case 8: case 27: case 46:    return 42;
        case 9: case 28: case 47:    return 43;
        case 12: case 31: case 50:   return 48;
        case 13: case 32: case 51:   return 49;
        case 16: case 35: case 54:   return 54;
        case 17: case 36: case 55:   return 55;
        case 58: case 64: case 70:   return 45;
        case 59: case 65: case 71:   return 46;
        case 60: case 66: case 72:   return 51;
        case 61: case 67: case 73:   return 52;
        case 62: case 68: case 74:   return 57;
        case 63: case 69: case 75:   return 58;
        case 85: case 91: case 97:   return 69;
        case 86: case 92: case 98:   return 70;
        case 87: case 93: case 99:   return 71;
        case 88: case 94: case 100:  return 72;
        case 89: case 95: case 101:  return 73;
        case 90: case 96: case 102:  return 74;
        case 106: case 107: case 108: case 109: case 110: case 111: return 76;
        case 121: case 127: case 133: return 85;
        case 122: case 128: case 134: return 86;
        case 123: case 129: case 135: return 87;
        case 124: case 130: case 136: return 88;
        case 125: case 131: case 137: return 89;
        case 126: case 132: case 138: return 90;
        default: return -1;
    }
}

SkillCastAttemptResult Skill_CastStoredAtTarget(SelfState& self, const GameDatabases& db,
                                                 const CombatMorphState& morph,
                                                 const PendingSkillCast& pending,
                                                 const float pos[3], int32_t targetHi,
                                                 int32_t targetLo, int32_t targetKind,
                                                 ISkillCastSink& sink) {
    SkillCastAttemptResult out;

    // Coût MP réel (ftol(InterpStat#1) - régén%) : réutilise EXACTEMENT la formule déjà
    // câblée dans SkillSystem (Skill_CalcRegenPct + Skill_CalcRealMpCost), mirroir des
    // lignes 0x53E772..0x53E7A6 du binaire.
    const int regenPct = Skill_CalcRegenPct(self, db.item);
    out.mpCost = Skill_CalcRealMpCost(db.skill, pending.skillId, pending.level, regenPct);

    if (self.mp < out.mpCost) {
        g_Client.msg.System(Str(147));
        out.reason = SkillCastFailReason::NotEnoughMp;
        return out;
    }

    const uint8_t* rec = Skill_GetRecord(db.skill, pending.skillId);
    if (!rec) {
        out.reason = SkillCastFailReason::UnknownSkill;
        return out;
    }

    const int32_t category = Skill_ReadI32(rec, skillinfo::kOffCategory);      // idx136
    const int32_t recSkillId = Skill_ReadI32(rec, skillinfo::kOffSkillId);     // idx0

    // Garde « forme incompatible » : morph transformé 88/54 + compétence de posture
    // (catégorie 4/5) ou compétence sentinelle 78.
    if ((morph.currentActionId == 88 || morph.currentActionId == 54) &&
        (category == 4 || category == 5 || recSkillId == 78)) {
        g_Client.msg.System(Str(1920));
        out.reason = SkillCastFailReason::IncompatibleForm;
        return out;
    }

    const int opGroup = Skill_ResolveCastOpGroup(pending.skillId);
    if (opGroup == -1) {
        out.reason = SkillCastFailReason::UnknownSkill;
        return out;
    }

    // Groupe 38 (skillId 4/23/42) : garde de posture supplémentaire + blocage morph.
    if (opGroup == 38) {
        const bool stanceOk = Skill_IsCurrentStanceSet(morph.currentActionId) ||
                               Skill_IsCurrentSpecial(morph.currentActionId) ||
                               self.level >= 70;
        if (!stanceOk) {
            g_Client.msg.System(Str(1146));
            out.reason = SkillCastFailReason::StanceRequired;
            return out;
        }
        const bool morphBlocked = morph.currentActionId >= 234 && morph.currentActionId <= 240;
        if (morphBlocked) {
            g_Client.msg.System(Str(1212));
            out.reason = SkillCastFailReason::MorphBlocked;
            return out;
        }
    }

    if (!sink.QueueSkillCast(opGroup, pending.skillId, pending.level, pending.param,
                              pos, targetHi, targetLo, targetKind)) {
        out.reason = SkillCastFailReason::SinkRejected;
        return out;
    }

    // Débit MP réel : DÉLÈGUE à Skill_TryConsumeMp (SkillSystem.h, non édité) — la
    // suffisance a déjà été vérifiée ci-dessus et rien n'a pu modifier self.mp entre
    // temps, donc cet appel débite exactement `out.mpCost` (mirroir de
    // `dword_1687378[0] -= v15` exécuté uniquement APRÈS succès du builder réseau
    // d'origine, cf. 0x53E98A et suivants).
    const SkillCastResult mp = Skill_TryConsumeMp(self, db.skill, db.item, pending.skillId, pending.level);
    out.ok = mp.ok;
    out.mpCost = mp.cost;
    return out;
}

// ===================== Disponibilité par carte ==============================

int SkillLoadoutTable::Compare(const char currentTag[13], int branch) const {
    if (branch < 0 || branch > 3) return 0;
    if (TagEquals(primary[static_cast<size_t>(branch)].data(), currentTag)) return 1;
    for (int i = 0; i < 12; ++i)
        if (TagEquals(alt[static_cast<size_t>(branch)][static_cast<size_t>(i)].data(), currentTag))
            return 2;
    return 0;
}

bool Skill_IsUsableOnCurrentMap(int mapZoneIndex, int currentActionId,
                                 const SkillLoadoutTable& loadout, const char currentTag[13]) {
    bool actionOk = false;
    switch (mapZoneIndex) {
        case 0:
            actionOk = (currentActionId == 1 || currentActionId == 2 || currentActionId == 3 ||
                        currentActionId == 4);
            break;
        case 1:
            actionOk = (currentActionId == 6 || currentActionId == 7 || currentActionId == 8 ||
                        currentActionId == 9);
            break;
        case 2:
            actionOk = (currentActionId == 11 || currentActionId == 12 || currentActionId == 13 ||
                        currentActionId == 14);
            break;
        case 3:
            actionOk = (currentActionId == 140 || currentActionId == 141 ||
                        currentActionId == 142 || currentActionId == 143);
            break;
        default:
            return false;
    }
    return actionOk && loadout.Compare(currentTag, mapZoneIndex) != 0;
}

// ===================== Hit-test grille de barre ==============================

int Skill_HitTestSlot(bool panelVisible, int anchorX, int anchorY, int cursorX, int cursorY,
                       int classOffset, const SkillBar& bar) {
    if (!panelVisible) return -1;

    int i = 0;
    for (; i < 10; ++i) {
        const int row = i / 5;
        const int col = i % 5;
        const int xMin = anchorX + 195 * row + 54;
        const int xMax = anchorX + 195 * row + 94;
        const int yMin = anchorY + 69 * col + 60;
        const int yMax = anchorY + 69 * col + 110;
        if (cursorX >= xMin && cursorX <= xMax && cursorY >= yMin && cursorY <= yMax) break;
    }
    if (i == 10) return -1;

    switch (classOffset) {
        case 0: case 1: case 2: {
            const size_t idx = static_cast<size_t>(10 * classOffset + i);
            if (idx < bar.slots.size() && bar.slots[idx].skillId >= 1) return -1;
            return i + 10 * classOffset;
        }
        case 3:
            return (i != 9) ? -1 : 9;
        case 4: {
            const size_t idx = static_cast<size_t>(10 * (classOffset - 1) + i);
            if (idx < bar.slots.size() && bar.slots[idx].skillId >= 1) return -1;
            return i + 10 * (classOffset - 1);
        }
        default:
            return -1;
    }
}

// ===================== Ensembles d'action courante ===========================

bool Skill_IsCurrentAttackSet(int currentActionId) {
    if (currentActionId <= 299) {
        if (currentActionId < 297) {
            switch (currentActionId) {
                case 1: case 2: case 3: case 4: case 6: case 7: case 8: case 9:
                case 11: case 12: case 13: case 14: case 37: case 38: case 43: case 44:
                case 45: case 55: case 56: case 57: case 58: case 75: case 89: case 90:
                case 101: case 102: case 103: case 125: case 140: case 141: case 142:
                case 143: case 167: case 200: case 201:
                    return true;
                default:
                    return false;
            }
        }
        return true; // 297..299
    }
    return currentActionId == 324;
}

bool Skill_IsCurrentComboSet(int currentActionId) {
    switch (currentActionId) {
        case 19: case 20: case 21: case 25: case 26: case 27: case 31: case 32:
        case 33: case 34: case 35: case 36: case 175: case 176: case 177: case 178:
        case 179: case 180: case 181: case 182: case 183: case 184: case 185: case 186:
        case 187: case 188: case 189: case 190: case 191: case 192: case 193:
            return true;
        default:
            return false;
    }
}

bool Skill_IsCurrentSet138(int currentActionId) {
    switch (currentActionId) {
        case 138: case 139: case 165: case 166:
            return true;
        default:
            return false;
    }
}

bool Skill_IsCurrentSet5(int currentActionId) {
    switch (currentActionId) {
        case 5: case 10: case 15: case 123:
            return true;
        default:
            return false;
    }
}

} // namespace ts2::game

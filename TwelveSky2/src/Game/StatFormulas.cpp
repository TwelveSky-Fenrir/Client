// Game/StatFormulas.cpp — byte-exact implementation of the Char_Calc* cluster.
// See StatFormulas.h for the EA map and the neutralization policy. Shared helpers
// (ftol, itemRec, levelStat, classifyRecord/ById, growth tables, Snapshot/buildSnapshot,
// enchantLoopA, skillTreeEquipBonus, ...) live in StatFormulas_Internal.h and are shared
// with StatFormulas_Attack.cpp / StatFormulas_Defense.cpp.
#include "Game/StatFormulas.h"
#include "Game/StatFormulas_Internal.h"
#include "Game/StatBonusContributors.h"   // Item_SumGemStatBonus, Char_SumGemStat*, SkillTree_SumBonuses
                                          // (already includes ItemSystem.h: Item_GetElementalBonus, etc.)
#include "Game/ClientRuntime.h"          // g_Client.VarGet — see the g_EquipAux block in the internal header
#include "Game/SkillSystem.h"            // Skill_GetRecord / Skill_ReadI32 / skillinfo::kOffSection
                                         // (SkillGrowthTbl_GetRecord 0x4C4E90, used in CalcElementResist,
                                         // now in StatFormulas_Defense.cpp). No cycle: SkillSystem.h
                                         // only includes GameState.h.

namespace ts2::game {

// Equip_GetSetBonusId 0x548CE0 (reduced to the deterministic branches).
// All EquipSet_Match*/EquipSet_IsPiece87206 families (id tables not extracted) are [hook]
// -> no match. What remains is the final counting branch (0x5492D2): v11 = number of slots
// {0,2,3,4,5,7} of class 1||4; v12 = number of IsPiece [hook]=0. Returns 50 if v11==6, 20 if
// v12==6 (never), 30 if v12+v11==6 (==v11==6, already covered), else 0.
// -> 50 iff all 6 armor slots are class 1||4, else 0.
int EquipSetBonusId(const SelfState& s, const GameDatabases& db) {
    int v11 = 0;
    const int armor[6] = {0, 2, 3, 4, 5, 7};
    for (int j : armor) {
        int c = classifyById(db, s.equip[j].itemId);
        if (c == 1 || c == 4) ++v11;
    }
    if (v11 == 6) return 50;
    return 0;
}

// Char_CalcMaxHP 0x4D4ED0 — channel 7 (field328 = ItemInfo.maxHp).
int CalcMaxHP(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level;                         // g_SelfLevel 0x16731A8
    const int lb  = s.levelBonus;                    // g_SelfLevelBonus 0x16731AC
    const int setId = EquipSetBonusId(s, db);        // v17 (0x4D4F32)
    Snapshot sn = buildSnapshot(s, db);
    int v25 = 0;

    // set-factor loop + slot4/slot10 special case (0x4D4F3D)
    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->maxHp;                        // +328
        v25 += base;
        if (i == 8) continue;                        // 0x4D4F80
        bool doTail = true;                          // reach LABEL_25 (slot4/slot10)
        switch (setId) {                             // v17
            case 1: case 6:   v25 += ftol((double)base * 0.4000000059604645); break;
            case 5: case 10:  v25 += ftol((double)base * 1.0); break;
            case 11: case 16: v25 += ftol((double)base * 0.6000000238418579); break;
            case 15: case 20: v25 += ftol((double)base * 1.100000023841858); break; // LABEL_10
            case 21:          v25 += ftol((double)base * 0.2000000029802322); break;
            case 22:          v25 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) { v25 += ftol((double)base * 0.550000011920929); }
                else if (i != 9 && i != 11 && i != 12) {
                    int c = classifyRecord(it);
                    if (c == 1 || c == 4) v25 += ftol((double)base * 0.550000011920929);
                    else                  v25 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v25 += ftol((double)base * 0.550000011920929); break; // LABEL_24
            default: break;                          // no set factor
        }
        (void)doTail;
        // LABEL_25 (0x4D51BB): slot4 / slot10 handling (runs regardless of setId).
        if (i == 4) {
            int c = classifyRecord(it);
            if (c == 1 || c == 4) {
                int v31 = attrByte0(sn.sock[i]);     // sub_545610(slot4 socket)
                if (v31 > 0) { if (v31 > 100) v31 -= 100; v25 += 200 * v31; }
            } else if (it->skillFlag == 2) {         // +284==2
                // [hook] Skill_GetValueClass4 0x54ECC0 -> 0
            } else {
                // [hook] Item_GetScaledStat 0x545980 -> 0; v25 += 0 * level
            }
        } else if (i == 10) {
            float v14 = (it->itemId == 210 || it->itemId == 211 || it->itemId == 212 ||
                         it->itemId == 216 || it->itemId == 217 || it->itemId == 218 ||
                         it->itemId == 2303 || it->itemId == 2304 || it->itemId == 2305)
                        ? 7.8000002f : 3.9000001f;
            if (classifyRecord(it) != 2) {
                int v31 = attrByte0(sn.sock[i]);     // sub_545610(slot10 socket)
                if (v31 > 0) v25 += ftol((double)v31 * v14);
            }
        }
    }

    // enchant loop (0x4D53EE), key 50 — pattern A, WIRED (see enchantLoopA in
    // StatFormulas_Internal.h). Accumulator var_1C = v25, the SAME one as the LEVEL_INFO
    // base added right after @0x4D5523.
    enchantLoopA(v25, sn, 50);

    // LEVEL_INFO Stat7 base (0x4D54FF)
    // Morph branch (g_SpecialFormActive) [runtime absent]: otherwise level = lb+lvl.
    v25 += levelStat(db, lb + lvl, 8);               // LevelTable_GetStat7 (field8 = baseMaxHp)

    // skill/stance bonus (0x4D55B2)
    // Predicate = (!stanceSet && !special) || morph∈[319..323], then a morph-id condition.
    // Skill_IsCurrentStanceSet/Special 0x4FB0F0/0x4FC800 [hook]=false, g_SelfMorphNpcId
    // 0x1675A98 [runtime absent]=0, dword_1675630 [runtime absent]=0.
    // -> !false&&!false=true; (0!=84 && 0<235)=true; term = 10*(0/1000)=0. No effect.

    // g_SpecialItem/grade 0x1687310 [runtime absent]=0 (0x4D55E7..0x4D5753)
    // slotGroup(0)=0 != 30; switch(0) has no matching case -> no multiplier/special gem.

    // % buffs (0x4D575D/0x4D5787): dword_16758F8 [runtime absent]=0; growthIndex %HP
    if (s.growthIndex > 0)                            // 0x4D5787 (dword_1674774 = growthIndex)
        v25 = ftol((double)v25 * (double)(s.growthIndex % 100 + 100) * 0.009999999776482582);
    if (setId == 19) v25 += 500;                     // 0x4D57BA (never with the reduced setId ∈{0,50})

    // SkillTree_SumBonuses 0x54B700 (0x4D57C9, cat=7) — WIRED (2026-07-14 audit): g_EquipAux
    // is now available via g_Client.Var(), see the skillTreeEquipBonus() banner in
    // StatFormulas_Internal.h.
    v25 += skillTreeEquipBonus(7, sn, db);
    // g_ElementMastery 0x1675680 [runtime absent]=0 (0x4D582F): no +1000.
    // talisman (0x4D584D) — WIRED. Same pattern as CalcAttackRatingMin/Max (see
    //   StatFormulas_Attack.cpp): guard `10 <= g_TalismanSlot < 20`, Stat_UnpackCombined
    //   0x54CE40 (out-param var_14=a2, `if (var_14>0)` @0x4D586F), then Num_ToDigits8 0x54CF00
    //   on dword_167568C[slot] (@0x4D589B), `movzx var_2A / imul 32h / add var_1C`
    //   @0x4D58AD..0x4D58B4. DIGIT: var_2A is Num_ToDigits8's 8th param (a8) -> the TENS digit
    //   (10^1), hence `(digits / 10) % 10` (consistent with a4=10^5 in RatingMin, a5=10^4 in
    //   RatingMax). Multiplier 50 is specific to the HP channel. "[runtime absent]" premise
    //   REFUTED: g_TalismanSlot 0x1674760 is written by Net/GameHandlers_Misc.cpp:668 (via
    //   g_Client.Var(kTalismanSlot)), 0x1675664[slot] by :692, 0x167568C[slot] by
    //   :693/697/712/723 — and this file already reads them via VarGet in
    //   CalcAttackRatingMin/Max.
    {
        const int tslot = g_Client.VarGet(0x1674760);          // g_TalismanSlot (0x4D584D)
        if (tslot >= 10 && tslot < 20) {
            const int combined = g_Client.VarGet(0x1675664 + 4u * static_cast<uint32_t>(tslot));
            int a2 = 0;                                        // Stat_UnpackCombined 0x54CE40
            if (combined >= 0) { a2 = combined / 1000000; if (a2 > 100) a2 = 0; }
            if (a2 > 0) {                                      // 0x4D586F (`cmp var_14,0 / jle`)
                const int digits = g_Client.VarGet(0x167568C + 4u * static_cast<uint32_t>(tslot));
                v25 += 50 * ((digits / 10) % 10);              // 0x4D58AD/0x4D58B1/0x4D58B4
            }
        }
    }
    // Item_SocketBonusInt/Float 0x4CA620/0x4CAC30 [hook]=0 (0x4D58CE/0x4D58EE).
    //   TODO(verif): decompilation confirmed — these two calls use (g_SelectedInvItemId
    //   0x1673258, dword_1673260), NOT (g_LocalPlayerWeaponId, g_Slot1Socket): a "selected"
    //   item distinct from the equipped one (likely a socket-upgrade UI), with no matching
    //   SelfState field. Same pattern in the 8 other Char_Calc* functions that call them
    //   (never the weaponId/g_Slot1Socket pair) -> genuinely absent everywhere.
    // Escort_IsCurrentTarget 0x54F440 [hook]=false (0x4D5908).
    // Item_GetElementalBonus 0x54F590 (0x4D5939): WIRED — args confirmed by decompilation
    //   (g_LocalPlayerWeaponId 0x16731E8, g_Slot1Socket 0x16731F0) = (s.weaponId,
    //   s.equip[1].socket), both present in SelfState. key=5 for this channel (HP).
    v25 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 5));
    return v25;
}

// Char_CalcMaxMP 0x4D59B0 — channel 8 (field332 = ItemInfo.maxMp).
int CalcMaxMP(const SelfState& s, const GameDatabases& db) {
    // NB: the LEVEL_INFO table provides NO MP base (no LevelTable_GetStat* for it).
    const int setId = EquipSetBonusId(s, db);        // v16
    Snapshot sn = buildSnapshot(s, db);
    int v24 = 0;

    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->maxMp;                        // +332
        v24 += base;
        if (i == 8) continue;
        bool reachTail = true;
        switch (setId) {
            case 1: case 6:   v24 += ftol((double)base * 0.4000000059604645); break;
            case 5: case 10:  v24 += ftol((double)base * 1.0); break;
            case 11: case 16: v24 += ftol((double)base * 0.6000000238418579); break;
            case 15: case 20: v24 += ftol((double)base * 1.100000023841858); break;
            case 21:          v24 += ftol((double)base * 0.2000000029802322); break;
            case 22:          v24 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) v24 += ftol((double)base * 0.550000011920929);
                else if (i != 9 && i != 11 && i != 12) {
                    int c = classifyRecord(it);
                    if (c == 1 || c == 4) v24 += ftol((double)base * 0.550000011920929);
                    else                  v24 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v24 += ftol((double)base * 0.550000011920929); break;
            default: break;
        }
        (void)reachTail;
        // LABEL_25 switch(i) (0x4D5CAB):
        switch (i) {
            case 0: {
                int c = classifyRecord(it);
                if (c != 1 && c != 4) {
                    if (it->skillFlag == 2) {        // +284==2
                        // [hook] Skill_GetValueClass0 0x54ED90 -> 0
                    } else {
                        // [hook] Item_GetScaledStat 0x545980 -> 0
                    }
                }
                break;
            }
            case 4: {
                int c = classifyRecord(it);
                if (c == 1 || c == 4) {
                    int v29 = attrByte0(sn.sock[i]);
                    if (v29 > 0) { if (v29 > 100) v29 -= 100; v24 += 200 * v29; }
                }
                break;
            }
            case 9: case 11: case 12:
                // Item_SumGemStatBonus 0x4C3CC0 — WIRED (key=8, MP channel; 0x4D5F49/0x4D5F67).
                if (classifyRecord(it) == 2) v24 += Item_SumGemStatBonus(8, sn.sock[i]);
                break;
            case 10: {
                float v13 = (it->itemId == 207 || it->itemId == 208 || it->itemId == 209 ||
                             it->itemId == 216 || it->itemId == 217 || it->itemId == 218 ||
                             it->itemId == 2303 || it->itemId == 2304 || it->itemId == 2305)
                            ? 7.8000002f : 3.9000001f;
                if (classifyRecord(it) == 2) {
                    // Item_SumGemStatBonus 0x4C3CC0 — WIRED (key=8; 0x4D5EC8..0x4D5EE8).
                    v24 += Item_SumGemStatBonus(8, sn.sock[i]);
                } else {
                    int v29 = attrByte0(sn.sock[i]);
                    if (v29 > 0) v24 += ftol((double)v29 * v13);
                }
                break;
            }
            default: break;
        }
    }

    // enchant loop (0x4D5F6F), key 60 (push 3Ch @0x4D5FC4) — pattern A, WIRED.
    enchantLoopA(v24, sn, 60);

    // stance/morph bonus (0x4D60CE): dword_1675630 [runtime absent]=0 -> 10*(0%1000)=0.

    // g_SpecialItem 0x1687310 [runtime absent]=0: switch/gems -> no effect (0x4D6103..).

    // % buffs (0x4D6246): dword_1675900 [runtime absent]=0; growthIndex %MP (0x4D6270)
    if (s.growthIndex > 0)
        v24 = ftol((double)v24 * (double)(s.growthIndex % 100 + 100) * 0.009999999776482582);

    // SkillTree_SumBonuses(8,..) (0x4D6299) — WIRED (2026-07-14 audit): see CalcMaxHP.
    v24 += skillTreeEquipBonus(8, sn, db);
    // g_ElementMastery==2 [absent]; talisman [absent];
    // Item_SocketBonusInt/Float [hook]=0 — TODO(verif): g_SelectedInvItemId/dword_1673260 pair
    //   (0x4D639D/0x4D63BC), not the weaponId/socket1 pair -> genuinely absent (cf. CalcMaxHP).
    // Escort [hook]=false.
    // Item_GetElementalBonus (0x4D6409): WIRED — (s.weaponId, s.equip[1].socket), key=6.
    v24 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 6));
    return v24;
}

// Char_CalcAttackSpeed 0x4CCAB0 — attack speed (base 60).
double CalcAttackSpeed(const SelfState& s, const GameDatabases& db) {
    (void)db;
    float v1 = 60.0f;
    // g_SpecialItem 0x1687310 [runtime absent]=0: neither the id list nor slotGroup(0)==30
    //   -> no *1.1 (0x4CCD14/0x4CCD37).
    // dword_1675910 buff [absent]=0; dword_16759A0 [absent]; dword_16759B8 [absent]: no effect.
    if (s.weaponId == 1407 && s.level <= 100) {      // 0x4CCDA3 (deterministic)
        // [hook] Skill_CalcAttackSpeed(2,8) 0x4CCDD0 -> original value undeterminable without
        //   the skill tables; the base is kept. TODO 0x4CCDB1.
    }
    // dword_16759F0 (g_GlobalStunActive) [absent]=0 -> returns v1 (else 0.0) (0x4CCDBB).
    return v1;
}

// Char_CalcWeaponRatePct 0x4CD900 — weapon rate (base 100).
// a1 = runtime buff context (a1+24 buff, a1+100 debuff) [absent]; a2 = weapon attribute word
// -> GemStat_WeaponRateFactor(byte2)*0.005. We read byte2 of the weapon socket (slot 1). The
// buffs (a1+24/a1+100) are [runtime absent].
double CalcWeaponRatePct(const SelfState& s, const GameDatabases& db) {
    (void)db;
    float v4 = 100.0f;
    // if (*(a1+24) > 0)  v4 += *(a1+24);   [runtime absent] -> 0
    // if (*(a1+100) > 0) v4 *= 0.5;        [runtime absent] -> not taken
    // GemStat_WeaponRateFactor 0x54CA70 = Item_GetAttribByte2(word)*0.004999999888241291.
    double v2 = (double)attrByte2(s.equip[1].socket) * 0.004999999888241291;
    return (double)(float)((double)ftol(v2 * v4) + v4);
}

// Char_CalcRegen 0x4D67F0 — regen sum (field360).
int CalcRegen(const SelfState& s, const GameDatabases& db) {
    Snapshot sn = buildSnapshot(s, db);
    int v4 = 0;
    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (it) v4 += it->regen;                     // +360
    }
    return v4;
}

} // namespace ts2::game

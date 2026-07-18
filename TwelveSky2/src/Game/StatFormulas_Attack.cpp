// Game/StatFormulas_Attack.cpp — byte-exact implementation of the attack channels of the
// Char_Calc* cluster (external/internal attack, min/max attack rating).
// See StatFormulas.h for the EA map and the neutralization policy, and
// StatFormulas_Internal.h for the helpers shared with StatFormulas.cpp / _Defense.cpp.
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
namespace {

// Weapon-damage tables id -> constant (Weapon_BaseMinDamage 0x4C99F0, Weapon_BaseMaxDamage
// 0x4C9E10, Weapon_SpecialDamageA 0x4CA230, Weapon_SpecialDamageB 0x4CA350). PURE
// __stdcall(int itemId) functions, no global read besides the ITEM_INFO table (mITEM).
// Common guard: record not found -> 0.0; then `*(Entry+188) != <typeCode>` -> 0.0 (offset 188
// = 188/4 = idx47 = ItemInfo::typeCode, the same field classifyRecord consumes).
//
// SCOPE NOTE: the gap tracker requested these 4 tables in Game/ItemSystem.cpp/.h — files NOT
// owned by this front. They are placed here instead, local to their sole consumers
// (Char_CalcAttackRatingMin/Max, both in this file). If another front needs them, move them
// up into ItemSystem.

// Weapon_BaseMinDamage 0x4C99F0 — typeCode 28 required (0x4C9A1E). Switch 0x4C9A98..0x4C9C97.
inline double weaponBaseMinDamage(const GameDatabases& db, uint32_t itemId) {
    const ItemInfo* it = itemRec(db, itemId);            // MobDb_GetEntry(mITEM, a1)
    if (!it) return 0.0;                                 // 0x4C9A11
    if (it->typeCode != 28) return 0.0;                  // 0x4C9A1E (*(Entry+188) != 28)
    switch (itemId) {
        case 2151: return 4000.0; case 2152: return 3500.0; case 2153: return 3000.0;
        case 2154: return 3500.0;
        case 2174: return 3800.0; case 2175: return 3600.0; case 2176: return 3400.0;
        case 2177: return 3200.0; case 2178: return 3400.0; case 2179: return 3300.0;
        case 2180: return 3200.0; case 2181: return 3100.0; case 2182: return 3000.0;
        case 2183: return 3000.0; case 2184: return 3000.0; case 2185: return 3000.0;
        case 2186: return 3400.0; case 2187: return 3300.0; case 2188: return 3200.0;
        case 2189: return 3100.0;
        case 2195: return 4000.0; case 2196: return 3800.0; case 2197: return 3600.0;
        case 2198: return 3000.0; case 2199: return 3000.0; case 2200: return 3000.0;
        case 2201: return 3500.0; case 2202: return 3400.0; case 2203: return 3300.0;
        case 2204: return 3500.0; case 2205: return 3400.0; case 2206: return 3300.0;
        case 2253: return 2000.0; case 2254: return 4000.0;
        case 2261: return 2000.0; case 2262: return 4000.0;
        case 2300: return 2000.0; case 2301: return 4000.0; case 2302: return 3500.0;
        case 2410: return 4000.0;                        // isolated `a1 == 2410` branch
        case 2411: return 3800.0; case 2412: return 3600.0; case 2413: return 3000.0;
        case 2414: return 3000.0; case 2415: return 3000.0; case 2416: return 3500.0;
        case 2417: return 3400.0; case 2418: return 3300.0; case 2419: return 3500.0;
        case 2420: return 3400.0; case 2421: return 3300.0;
        default:   return 0.0;                           // LABEL_60
    }
}

// Weapon_BaseMaxDamage 0x4C9E10 — typeCode 28 required. Same ids as Min, DIFFERENT values.
inline double weaponBaseMaxDamage(const GameDatabases& db, uint32_t itemId) {
    const ItemInfo* it = itemRec(db, itemId);
    if (!it) return 0.0;
    if (it->typeCode != 28) return 0.0;
    switch (itemId) {
        case 2151: return 3500.0; case 2152: return 4000.0; case 2153: return 3500.0;
        case 2154: return 3000.0;
        case 2174: return 3400.0; case 2175: return 3300.0; case 2176: return 3200.0;
        case 2177: return 3100.0; case 2178: return 3800.0; case 2179: return 3600.0;
        case 2180: return 3400.0; case 2181: return 3200.0; case 2182: return 3400.0;
        case 2183: return 3300.0; case 2184: return 3200.0; case 2185: return 3100.0;
        case 2186: return 3000.0; case 2187: return 3000.0; case 2188: return 3000.0;
        case 2189: return 3000.0;
        case 2195: return 3500.0; case 2196: return 3400.0; case 2197: return 3300.0;
        case 2198: return 3500.0; case 2199: return 3400.0; case 2200: return 3300.0;
        case 2201: return 4000.0; case 2202: return 3800.0; case 2203: return 3600.0;
        case 2204: return 3000.0; case 2205: return 3000.0; case 2206: return 3000.0;
        case 2253: return 2000.0; case 2254: return 4000.0;
        case 2261: return 2000.0; case 2262: return 4000.0;
        case 2300: return 2000.0; case 2301: return 4000.0; case 2302: return 3500.0;
        case 2410: return 3500.0;
        case 2411: return 3400.0; case 2412: return 3300.0; case 2413: return 3500.0;
        case 2414: return 3400.0; case 2415: return 3300.0; case 2416: return 4000.0;
        case 2417: return 3800.0; case 2418: return 3600.0; case 2419: return 3000.0;
        case 2420: return 3000.0; case 2421: return 3000.0;
        default:   return 0.0;
    }
}

// Weapon_SpecialDamageA 0x4CA230 — typeCode 31 OR 32 required (0x4CA25E). Ids 0x4CA2E9..0x4CA32F.
inline double weaponSpecialDamageA(const GameDatabases& db, uint32_t itemId) {
    const ItemInfo* it = itemRec(db, itemId);
    if (!it) return 0.0;
    if (it->typeCode != 31 && it->typeCode != 32) return 0.0; // 0x4CA25E
    switch (itemId) {
        case 1838: case 1840: case 1841: case 1842:
        case 1887: case 1889:
        case 17202: case 17203: case 17204:
            return 8000.0;
        default:
            return 0.0;
    }
}

// Weapon_SpecialDamageB 0x4CA350 — typeCode 31 OR 32 required. ID SET DIFFERS from A:
// 1839 and 1890 added, 1838/1887/17202/17203 removed (decompilation of 0x4CA350: branches
// a1>1889 -> {1890, 17204}; a1==1889; switch {1839,1840,1841,1842}).
inline double weaponSpecialDamageB(const GameDatabases& db, uint32_t itemId) {
    const ItemInfo* it = itemRec(db, itemId);
    if (!it) return 0.0;
    if (it->typeCode != 31 && it->typeCode != 32) return 0.0;
    switch (itemId) {
        case 1839: case 1840: case 1841: case 1842:
        case 1889: case 1890:
        case 17204:
            return 8000.0;
        default:
            return 0.0;
    }
}

// PATTERN B — Char_CalcAttackRatingMin 0x4CDC63 (key 100). FOUR proven differences from
// pattern A (enchantLoopA, StatFormulas_Internal.h) — do NOT unify:
//   (1) NO class-8 branch: only a single call to 0x553D50 in the whole function (unique xref
//       0x4CDCF0), vs. two for the other 8 channels;
//   (2) floor 0 (not 1): `cmp var_3C,0 / jge` @0x4CDCFB;
//   (3) floor applied OUTSIDE the class test but INSIDE the slot-set test:
//       0x4CDCD2 (cls∉{1,4}) jumps to 0x4CDCFB = the floor still runs, while
//       0x4CDCC6 (slot outside the set) jumps to 0x4CDD08 = neither add nor floor;
//   (4) no explicit i!=8 guard — unnecessary, since 8 ∉ {0,2,3,4,5,7}.
inline void enchantLoopRatingMin(int& acc, const Snapshot& sn) {
    for (int i = 0; i < 13; ++i) {                       // 0x4CDC75
        if (!sn.r[i]) continue;                          // 0x4CDC85
        const int cls = classifyRecord(sn.r[i]);         // 0x4CDC9C
        if (i == 0 || i == 2 || i == 3 || i == 4 || i == 5 || i == 7) { // 0x4CDCA4..0x4CDCC6
            if (cls == 1 || cls == 4)                    // 0x4CDCC8/0x4CDCCE
                acc += Item_GetEnchantStatDelta(cls, i, sn.sock[i], 100); // 0x4CDCF0 (push 64h)
            if (acc < 0) acc = 0;                        // 0x4CDCFB (outside the class test)
        }
    }
}

} // namespace

// Char_CalcExternalAttack 0x4D0530 — channel 1 (external attack, weapon-scaled).
int CalcExternalAttack(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    // base attributes (non-morph branch 0x4D060E)
    // g_CoreAttr 0x167477C — WIRED ("[runtime absent]" premise REFUTED: written by
    // Net/GameHandlers_Misc.cpp:459/467, anchors 0x4936AD/0x493790; already read via VarGet in
    // CalcAttackRatingMin/Max below). Stat_AddCoreAttr 0x546380 is an identity stub
    // `return a2` (a2 = g_CoreAttr) -> the caller does `add esi, eax`, i.e. a plain
    // "+ g_CoreAttr". Disassembly 0x4D060E..0x4D0666: v41 (var_48) and v45 (var_38) EACH
    // receive the term (calls @0x4D062D and @0x4D065F).
    const int coreAttr = g_Client.VarGet(0x167477C);
    int v41 = s.attrExtForce  + growth292(gi) + coreAttr; // 0x16731BC + growth292 + g_CoreAttr (0x4D062D/0x4D0632)
    int v45 = s.attrOffensive + growth304(gi) + coreAttr; // 0x16731C0 + growth304 + g_CoreAttr (0x4D065F/0x4D0664)
    // TODO [runtime absent] 0x4D0572: morph branch (Level_ToTierValueB 0x54EFF0,
    //   maybe_StubReturn1A 0x54F030) — g_SpecialFormActive absent.
    // g_AttrBuffActive 0x16758A8 [runtime absent]=0: no attribute buff (0x4D0670).

    const int setId = EquipSetBonusId(s, db);        // v39
    Snapshot sn = buildSnapshot(s, db);

    for (int i = 0; i < 13; ++i) {                   // equip attribute sum (0x4D0709)
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        v41 += it->attrPrimaryA;                     // +292
        v45 += it->attrRatingMax;                    // +304
    }
    // pet (dword_1687448) [runtime absent].
    // Char_SumGemStatA/D 0x54CB00/0x54CC90 — WIRED (0x4D07BA/0x4D07CA): A feeds v45
    // (attrRatingMax/304), D feeds v41 (attrPrimaryA/292), matching the original order.
    v45 += Char_SumGemStatA(s);
    v41 += Char_SumGemStatD(s);
    // grade dword_1687474 [runtime absent] (0x4D07D4..0x4D0800).

    int v50 = 0;
    // weapon-class conversion (slot 7 = r[7], 0x4D082A)
    if (sn.r[7]) {
        switch (sn.r[7]->typeCode) {                 // +188
            case 0x0D: case 0x11: case 0x13:
                v50 += ftol((double)v41 * 2.650000095367432);
                v50 += ftol((double)v45 * 1.429999947547913);
                break;
            case 0x0E: case 0x10: case 0x14:
                v50 += ftol((double)v41 * 2.799999952316284);
                v50 += ftol((double)v45 * 1.509999990463257);
                break;
            case 0x0F: case 0x12: case 0x15:
                v50 += ftol((double)v41 * 2.509999990463257);
                v50 += ftol((double)v45 * 1.350000023841858);
                break;
            default: break;
        }
    } else {
        v50 += ftol((double)v41 * 1.25);
        v50 += ftol((double)v45 * 0.6700000166893005);
    }
    // pet-mastery flat (dword_1674798/16747C8/167479C) [runtime absent]: +413/+275 ignored (0x4D0941).

    // LEVEL_INFO Stat3 base (0x4D09AF)
    v50 += levelStat(db, lb + lvl, 4);               // baseExtAttack (field4)

    // extAttack equip sum + set factors + per-slot specials (0x4D09FE)
    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->extAttack;                    // +312
        v50 += base;
        if (i == 8) continue;
        int c48 = classifyRecord(it);                // v48
        switch (setId) {
            case 2: case 7:                       v50 += ftol((double)base * 0.4000000059604645); break;
            case 4: case 9: case 12: case 17:     v50 += ftol((double)base * 0.6000000238418579); break;
            case 5: case 10:                      v50 += ftol((double)base * 1.0); break;
            case 14: case 19:                     v50 += ftol((double)base * 0.699999988079071); break;
            case 15: case 20:                     v50 += ftol((double)base * 1.100000023841858); break;
            case 21:                              v50 += ftol((double)base * 0.2000000029802322); break;
            case 22:                              v50 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) v50 += ftol((double)base * 0.550000011920929);
                else if (i != 9 && i != 11 && i != 12) {
                    if (classifyRecord(it) == 1 || classifyById(db, it->itemId) == 4)
                        v50 += ftol((double)base * 0.550000011920929);
                    else
                        v50 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v50 += ftol((double)base * 0.550000011920929); break;
            default: break;
        }
        // LABEL_67 (0x4D0CF3): per-slot deterministic bonuses
        if (i == 1 && c48 != 9 && it->typeCode == 29) {  // +188==29
            int v57 = attrByte0(sn.sock[i]);
            if (v57 > 0) v50 += 6 * v57;
        }
        if (i == 7) {
            if (classifyRecord(sn.r[7]) == 1 || classifyById(db, it->itemId) == 4) {
                int v57 = attrByte0(sn.sock[i]);
                if (v57 > 0) { if (v57 >= 100) v57 -= 100; v50 += 1200 * v57; }
            } else if (it->skillFlag == 2) {         // +284==2
                // [hook] Skill_GetValueTier7 0x54E550 -> 0; then the weapon-scaled term:
                int v57 = attrByte0(sn.sock[i]);
                if (v57 > 0) v50 += ftol((double)base * ((double)v57 * 0.02999999932944775));
            } else if (it->skillFlag == 3) {         // +284==3
                // [hook] Skill_GetUpgradeCostTier 0x54F4D0 -> 0
            } else {
                // [hook] Item_GetScaledStat 0x545980 -> 0; v42*v43==0
                int v57 = attrByte0(sn.sock[i]);     // sub_545610(slot7 socket)
                if (v57 > 0)                          // term (0 + base) * (v57*0.03)
                    v50 += ftol((double)(0 + base) * ((double)v57 * 0.02999999932944775));
            }
        }
        if (i == 10) {
            float v37 = (it->itemId == 213 || it->itemId == 214 || it->itemId == 215 ||
                         it->itemId == 216 || it->itemId == 217 || it->itemId == 218 ||
                         it->itemId == 2303 || it->itemId == 2304 || it->itemId == 2305)
                        ? 23.4f : 11.7f;
            if (classifyRecord(it) == 2) {
                // Item_SumGemStatBonus 0x4C3CC0 — WIRED (key=3, extAttack channel; 0x4D1008/0x4D1026).
                v50 += Item_SumGemStatBonus(3, sn.sock[i]);
            } else {
                int v57 = attrByte0(sn.sock[i]);
                if (v57 > 0) v50 += ftol((double)v57 * v37);
            }
        }
        // Item_SumGemStatBonus 0x4C3CC0 — WIRED (key=3; 0x4D1097/0x4D10B5).
        if ((i == 9 || i == 11 || i == 12) && classifyRecord(it) == 2)
            v50 += Item_SumGemStatBonus(3, sn.sock[i]);
    }

    // enchant loop (0x4D10BD), key 10 (push 0Ah @0x4D1112) — pattern A, WIRED.
    enchantLoopA(v50, sn, 10);

    // stance/skill bonus (0x4D11E7)
    // Skill_IsCurrentStanceSet/Special [hook]=false, morph [absent]=0 -> entry predicate true.
    // Char_FindSkillSlotByName 0x55D520 [hook] (v36 stays -1). dword_1675668/16760CC/
    // 16760C0/1674730 [runtime absent]=0. -> v36!=0 (=-1) so no +600; else branch:
    // dword_16760CC(0)==element? if element==0 -> combos absent=0 -> v50 += 3*0 = 0.
    // No deterministic term (dword_1674730 absent). TODO 0x4D11E7 (skill state absent).

    // buffs/grade/talisman/sockets/escort/element (0x4D12DA..end):
    //   dword_1674A50/1675728 [absent]=0; dword_1675704 [absent]; g_SpecialItem 0x1687310
    //   [absent]=0 (switch has no effect); slotGroup(0)!=30; dword_16758D8[0] [absent]=0;
    //   Item_ScaleStatByTypeA 0x4C91B0 [hook]=0 (v50>=0 -> += 0) — TODO(verif): decompilation of
    //   0x4C91B0 confirms caps=dword_8E717C (mPAT table). Docs/TS2_GAMEPLAY_LOGIC.md
    //   §"Weapon caps": the mPAT loader is STUBBED (PatTbl_LoadImg_STUB -> returns 1 without
    //   loading file 005_00008.IMG) -> the 4 caps are REALLY indeterminate at runtime, not just
    //   absent from SelfState. Its other arguments (dword_1673258/1673260/167325C =
    //   g_SelectedInvItemId + value + upgrade counter) are also absent from SelfState
    //   (a "selected" item outside the equipped set). Guessing these 4 values would break
    //   fidelity -> stays [hook].
    //   g_ElementMastery==7 [absent]; talisman [absent]; Item_StatBonusTier 0x4CB6D0 [hook]=0;
    //   dword_16760D8 [absent]; Escort [hook]=false; Char_CompareSkillLoadout 0x557B00 [hook]=0;
    //   Item_SocketBonusInt/Float [hook]=0 — TODO(verif): g_SelectedInvItemId/dword_1673260 pair
    //   (0x4D16FE/0x4D171D), cf. CalcMaxHP. Item_GemSetBonusMultiplier [hook]=0 (off-list).
    //   dword_16756E0 [absent]=0.
    // NB: Item_ScaleStatByTypeA is 0 -> the guard `v50 >= ftol(0)` is true -> v50 += 0 (never
    //   the v50*=2 branch). Same for channel 2 (TypeB).
    // SkillTree_SumBonuses(1,..) (0x4D151B) — WIRED (2026-07-14 audit): see the
    //   skillTreeEquipBonus() banner in StatFormulas_Internal.h (g_EquipAux now readable via
    //   g_Client.Var()).
    v50 += skillTreeEquipBonus(1, sn, db);
    // Item_GetElementalBonus (0x4D177D): WIRED — (s.weaponId, s.equip[1].socket), key=1.
    v50 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 1));
    return v50;
}

// Char_CalcInternalAttack 0x4D1830 — channel 2 (internal attack, weapon-scaled).
int CalcInternalAttack(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    // + g_CoreAttr — WIRED (cf. CalcExternalAttack): non-morph branch, call @0x4D18D6 then
    // `add esi, eax` @0x4D18DB (var_10 = v44). Stat_AddCoreAttr 0x546380 = identity.
    int v44 = s.attrIntForce + growth296(gi) + g_Client.VarGet(0x167477C); // 0x16731C4 + growth296 + g_CoreAttr
    // TODO [runtime absent] 0x4D1864: morph branch (maybe_StubReturn1B 0x54F070).
    // g_AttrBuffActive [absent]=0 (0x4D18E7).

    const int setId = EquipSetBonusId(s, db);        // v28
    Snapshot sn = buildSnapshot(s, db);
    int v37 = 0;

    for (int i = 0; i < 13; ++i) {                   // attrPrimaryB sum (+296) (0x4D1987)
        const ItemInfo* it = sn.r[i];
        if (it) v44 += it->attrPrimaryB;
    }
    // pet (dword_1687448) [absent]; grade [absent] (0x4D19D5..).
    // Char_SumGemStatB 0x54CB80 — WIRED (0x4D1A14), feeds v44 (attrPrimaryB/296).
    v44 += Char_SumGemStatB(s);

    v37 += ftol((double)v44 * 1.629999995231628);    // internal conversion (0x4D1A47)
    // pet-mastery flat +825/+550 [absent] (0x4D1A7E).

    v37 += levelStat(db, lb + lvl, 5);               // baseIntAttack Stat4 (field5) (0x4D1AEC)

    for (int i = 0; i < 13; ++i) {                   // intAttack sum (+316) + set + specials (0x4D1B35)
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->intAttack;                    // +316
        v37 += base;
        if (i == 8) continue;
        int v34 = classifyRecord(it);
        switch (setId) {
            case 2: case 7:                       v37 += ftol((double)base * 0.4000000059604645); break;
            case 3: case 9: case 12: case 17:     v37 += ftol((double)base * 0.6000000238418579); break;
            case 5: case 10:                      v37 += ftol((double)base * 1.0); break;
            case 13: case 19:                     v37 += ftol((double)base * 0.699999988079071); break;
            case 15: case 20:                     v37 += ftol((double)base * 1.100000023841858); break;
            case 21:                              v37 += ftol((double)base * 0.2000000029802322); break;
            case 22:                              v37 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) v37 += ftol((double)base * 0.550000011920929);
                else if (i != 9 && i != 11 && i != 12) {
                    if (v34 == 1 || v34 == 4) v37 += ftol((double)base * 0.550000011920929);
                    else                      v37 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v37 += ftol((double)base * 0.550000011920929); break;
            default: break;
        }
        // LABEL_61 switch(i) (0x4D1DFD):
        switch (i) {
            case 1:
                if (v34 != 8 && v34 != 9) {
                    if (it->typeCode == 29) {        // +188==29
                        int v45 = attrByte0(sn.sock[i]);
                        if (v45 > 0) v37 += 6 * v45;
                    } else if (it->skillFlag == 2) { // +284==2
                        // [hook] Skill_GetValueByClassA 0x54E620 -> 0
                        int v45 = attrByte0(sn.sock[i]);
                        if (v45 > 0) v37 += 6 * v45;
                    } else {
                        // [hook] Item_GetScaledStat 0x545980 -> 0
                        int v45 = attrByte0(sn.sock[i]);
                        if (v45 > 0) v37 += 6 * v45;
                    }
                }
                break;
            case 2:
                if (v34 == 1 || v34 == 4) {
                    int v45 = attrByte0(sn.sock[i]);
                    if (v45 > 0) { if (v45 >= 100) v45 -= 100; v37 += 1000 * v45; }
                } else if (it->skillFlag == 2) {
                    // [hook] Skill_GetValueByClassA -> 0
                    int v45 = attrByte0(sn.sock[i]);
                    if (v45 > 0) v37 += ftol((double)base * ((double)v45 * 0.02999999932944775));
                } else {
                    // [hook] Item_GetScaledStat -> 0; v30*v31==0
                    int v45 = attrByte0(sn.sock[i]);
                    if (v45 > 0) v37 += ftol((double)(0 + base) * ((double)v45 * 0.02999999932944775));
                }
                break;
            case 3:
                if (it->skillFlag == 2) { /* [hook] Skill_GetValueByClassA -> 0 */ }
                else { /* [hook] Item_GetScaledStat -> 0 */ }
                break;
            case 5:
                if (it->skillFlag == 2) { /* [hook] Skill_GetValueByClassA -> 0 */ }
                else { /* [hook] Item_GetScaledStat -> 0 */ }
                break;
            case 9: case 11: case 12:
                // Item_SumGemStatBonus 0x4C3CC0 — WIRED (key=4, intAttack channel; 0x4D235D/0x4D237B).
                if (v34 == 2) v37 += Item_SumGemStatBonus(4, sn.sock[i]);
                break;
            case 10: {
                float v26 = (it->itemId == 204 || it->itemId == 205 || it->itemId == 206 ||
                             it->itemId == 216 || it->itemId == 217 || it->itemId == 218 ||
                             it->itemId == 2303 || it->itemId == 2304 || it->itemId == 2305)
                            ? 48.75f : 24.35f;
                if (v34 == 2) {
                    // Item_SumGemStatBonus 0x4C3CC0 — WIRED (key=4; 0x4D22F2/0x4D2310).
                    v37 += Item_SumGemStatBonus(4, sn.sock[i]);
                } else {
                    int v45 = attrByte0(sn.sock[i]);
                    if (v45 > 0) v37 += ftol((double)v45 * v26);
                }
                break;
            }
            default: break;
        }
    }

    // enchant loop (0x4D23D8), key 20 (push 14h @0x4D23D8) — pattern A, WIRED.
    enchantLoopA(v37, sn, 20);
    // g_SpecialItem switch [absent]=0. slotGroup(0)!=30.
    // dword_16758E0 buff [absent]=0. Item_ScaleStatByTypeB 0x4C95C0 [hook]=0 (guard -> += 0) —
    //   TODO(verif): same causes as Item_ScaleStatByTypeA in CalcExternalAttack (caps
    //   dword_8E717C = mPAT at the stubbed loader, undumped; "selected" item absent from
    //   SelfState).
    // dword_168744C/1687450 [absent]: no +500.
    // g_ElementMastery==1 [absent]. talisman [absent]. Escort [hook]=false.
    // Char_CompareSkillLoadout [hook]=0. dword_16756E4 [absent]=0.
    // SkillTree_SumBonuses(2,..) (0x4D25F7) — WIRED (2026-07-14 audit): see CalcMaxHP.
    v37 += skillTreeEquipBonus(2, sn, db);
    // Item_GetElementalBonus (0x4D2765): WIRED — (s.weaponId, s.equip[1].socket), key=2.
    v37 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 2));
    return v37;
}

// Char_CalcAttackRatingMin 0x4CD970 — field300 (minimum damage rating).
int CalcAttackRatingMin(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    int v39 = s.attrDefensive + growth300(gi) + g_Client.VarGet(0x167477C); // 0x16731B8 + growth300 + g_CoreAttr (Stat_AddCoreAttr 0x546380 = identity; 0x4CDA2C/0x4CDA52)
    // TODO [runtime absent] 0x4CD9D9: morph branch (g_SpecialFormActive 0x16760D4 never written=0).
    if (g_Client.VarGet(0x16758A8) > 0)                       // g_AttrBuffActive (0x4CDA5C)
        v39 += g_Client.VarGet(0x16758AC) / 100 + g_Client.VarGet(0x16758AC) % 100; // g_AttrBuff300 (0x4CDA7F)
    Snapshot sn = buildSnapshot(s, db);
    int v33 = 0;

    for (int i = 0; i < 13; ++i) {                   // attrRatingMin sum (+300)
        const ItemInfo* it = sn.r[i];
        if (it) v39 += it->attrRatingMin;
    }
    // pet [absent]; grade [absent].
    // Char_SumGemStatC 0x54CC40 — WIRED (0x4CDB16), feeds v39 (attrRatingMin/300).
    v39 += Char_SumGemStatC(s);
    v33 += ftol((double)v39 * 20.0);                 // 0x4CDB6D
    // pet-mastery flat +825/+550 [absent] (0x4CDBA4).

    for (int i = 9; i < 13; ++i) {                   // gems slots 9..12 (0x4CDBF6)
        const ItemInfo* it = sn.r[i];
        if (it && classifyRecord(it) == 2) {
            // Item_SumGemStatBonus 0x4C3CC0 — WIRED (key=1, atkRatingMin channel; 0x4CDC55).
            v33 += Item_SumGemStatBonus(1, sn.sock[i]);
            if (v33 < 0) v33 = 0;
        }
    }
    // enchant loop (0x4CDC63), key 100 (push 64h @0x4CDCD4) — pattern B, WIRED.
    // Accumulator var_3C = v33, the SAME as Item_SumGemStatBonus right above (0x4CDC52).
    // LIVE channel (37 C++ call sites) -> the most visible enchant fix.
    enchantLoopRatingMin(v33, sn);

    // meridian/stance bonus (0x4CDD43). Skill_IsCurrentStanceSet/Special 0x4FB0F0/0x4FC800
    // [hook]=false -> entry predicate (!set && !special || morph∈[319,323]) = true (block always
    // runs). Char_FindSkillSlotByName 0x55D520 [hook] -> v40 stays -1.
    {
        const int v40 = -1;                                    // 0x55D520 [hook]
        // Slot test @0x4CDD6B: `cmp var_54,0 / jz ; cmp var_54,1 / jnz` = set {0,1}.
        // var_54 is typed UNSIGNED INT in the 0x4CD970 frame (verified): the `v40 <= 1` that
        // Hex-Rays shows is thus faithful FOR AN UNSIGNED value, but the C++ declared v40 as a
        // SIGNED `int` at -1 -> `-1 <= 1` was TRUE while the binary (0xFFFFFFFF) rejects it.
        // Explicit equality form: literal, and robust to signedness.
        // The dword_1675660 global test is at 0x4CDD7E.
        if ((v40 == 0 || v40 == 1) && g_Client.VarGet(0x1675660) == 1) { // 0x4CDD6B / 0x4CDD7E
            v33 += 4000;                                       // 0x4CDD89
        } else {
            const int mEl   = g_Client.VarGet(0x16760CC);      // g_MeridianElement (0x4CDDA5)
            const int mTier = g_Client.VarGet(0x16760C0);      // g_MeridianTier
            const int mPts  = g_Client.VarGet(0x16731C8);      // g_MeridianPts_RatingMin
            if (mEl == s.element || mEl == 4) {                // g_LocalElement 0x1673194
                if      (mTier == 10 && mPts < 100) v33 += 2000; // 0x4CDDCA
                else if (mTier == 20 && mPts < 200) v33 += 4000; // 0x4CDDF1
                else if (mTier == 30)               v33 += 6000; // 0x4CDE08
                else                                v33 += 20 * mPts; // 0x4CDE18
            } else {
                v33 += 20 * mPts;                              // 0x4CDE29
            }
        }
    }

    v33 += levelStat(db, lb + lvl, 9);               // baseAtkRatingMin Stat8 (field9) (0x4CDE49)

    if (g_Client.VarGet(0x1674A54) > 0 || g_Client.VarGet(0x1675728) > 0) // dword_1674A54 (case47)/dword_1675728 (case91); 0x4CDEA1
        v33 = ftol((double)v33 * 1.200000047683716);          // 0x4CDEAC
    // g_SpecialItem switch [absent]=0 (0x4CDECA). slotGroup(0)!=30 -> no gem (0x4CDF89).
    // Item_ScaleStatByTypeC 0x4CB0D0 [hook]=0: guard v33>=0 -> += 0 (never *2) (0x4CE02B) —
    //   TODO(verif): same causes as Item_ScaleStatByTypeA in CalcExternalAttack (caps
    //   dword_8E717C = mPAT at the stubbed loader, undumped; "selected" item absent from
    //   SelfState).

    int v32 = EquipSetBonusId(s, db);                // recompute (0x4CE077)
    if (v32 == 13)      v33 += 1000;
    else if (v32 == 18) v33 += 1100;

    int v45 = 0;
    for (int i = 0; i < 13; ++i) {                   // 0x4CE0B6
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        if (classifyRecord(it) == 1 || classifyById(db, it->itemId) == 4) {
            v45 = 1;
            if (i == 0) {
                int v48 = attrByte0(sn.sock[0]);
                if (v48 > 0) { if (v48 > 100) v48 -= 100; v33 += 500 * v48; }
            }
        }
        // SkillTree_SumBonuses(3, g_EquipAux[3*i], dword_16750BC[3*i], dword_16750C0[3*i]) is
        // MERGED into THIS loop on the binary side (0x4CE18F, called on every iteration i where
        // sn.r[i] exists, independent of the if above) — wired below via skillTreeEquipBonus()
        // instead (commutative integer addition, identical result).
    }
    // SkillTree_SumBonuses(3,..) (0x4CE18F) — WIRED (2026-07-14 audit): see CalcMaxHP.
    v33 += skillTreeEquipBonus(3, sn, db);
    if (v45 == 1) v33 += 30000;                      // 0x4CE1A3

    // GemStat_AtkRatingMinFlat 0x54CA50 — WIRED. Helper = `400 * Item_GetAttribByte2(
    // g_Slot2Socket 0x1673200)` (0x54CA6B); g_Slot2Socket == 0x16731E0 + 16*2 ==
    // equip[2].socket. FLAT INTEGER term: `call GemStat_AtkRatingMinFlat @0x4CE1B6 ;
    // add eax,[ebp+var_3C] @0x4CE1BB` — plain integer addition, no ftol or %.
    // Exact position: AFTER `if (v45==1) v33 += 30000` (add 7530h @0x4CE1A8) and BEFORE the
    // g_ElementMastery==6 test (0x4CE1C1). Amplitude up to 400*255 on a WIRED channel.
    v33 += 400 * attrByte2(s.equip[2].socket);                 // 0x54CA50 (@0x4CE1B6/0x4CE1BB)
    if (g_Client.VarGet(0x1675680) == 6) v33 += 1000;          // g_ElementMastery (0x4CE1C8/0x4CE1D2)
    // talisman (0x4CE1E5): Stat_UnpackCombined 0x54CE40 (guard a2) + Num_ToDigits8 0x54CF00 (digit 10^5).
    {
        const int tslot = g_Client.VarGet(0x1674760);          // g_TalismanSlot
        if (tslot >= 10 && tslot < 20) {
            const int combined = g_Client.VarGet(0x1675664 + 4u * static_cast<uint32_t>(tslot)); // dword_1675664[slot]
            int a2 = 0;                                        // Stat_UnpackCombined .a2 (0x54CE40)
            if (combined >= 0) { a2 = combined / 1000000; if (a2 > 100) a2 = 0; }
            if (a2 > 0) {                                      // 0x4CE20A
                const int digits = g_Client.VarGet(0x167568C + 4u * static_cast<uint32_t>(tslot)); // dword_167568C[slot]
                v33 += 100 * ((digits / 100000) % 10);         // Num_ToDigits8 -> a4/v41 (10^5); 0x4CE24E
            }
        }
    }
    // Equip_ComputeGemSetBonus 0x54E420 [hook]=0.
    // STRICT BINARY ORDER (ftol not commutative with the % terms):
    //   BaseMinDamage 0x4CE276 -> SocketBonusInt 0x4CE29A -> SocketBonusFloat 0x4CE2B9
    //   -> SpecialDamageA 0x4CE2D5 -> GetElementalBonus 0x4CE2F9.
    // Both weapon tables are called with g_SelectedInvItemId 0x1673258, which IS
    // equip[8].itemId (0x16731D8 + 16*8) — the C++ already admits this for the same global in
    // CalcElementResist. Each result goes through Crt_ftol then is ADDED.
    v33 += ftol(weaponBaseMinDamage(db, s.equip[8].itemId));   // 0x4C99F0 (@0x4CE276/0x4CE283)
    // Item_SocketBonusInt/Float [hook]=0 — TODO(verif): g_SelectedInvItemId/dword_1673260 pair
    //   (0x4CE29A/0x4CE2B9), cf. CalcMaxHP.
    v33 += ftol(weaponSpecialDamageA(db, s.equip[8].itemId));  // 0x4CA230 (@0x4CE2D5/0x4CE2E2)
    // Item_GetElementalBonus (0x4CE2F9): WIRED — (s.weaponId, s.equip[1].socket), key=3.
    v33 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 3));
    // grade slots loop (0x4CE31E): Item_GetGradeStatValues 0x550B20 [hook]=false -> 0.
    // Escort [hook]=false.
    return v33;
}

// Char_CalcAttackRatingMax 0x4CE3F0 — field304 (maximum damage rating).
int CalcAttackRatingMax(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    int v24 = s.attrOffensive + growth304(gi) + g_Client.VarGet(0x167477C); // 0x16731C0 + growth304 + g_CoreAttr (0x4CE4A5/0x4CE4CB)
    // TODO [runtime absent] 0x4CE452: morph branch (g_SpecialFormActive=0; maybe_StubReturn1A 0x54F030).
    if (g_Client.VarGet(0x16758A8) > 0)                        // g_AttrBuffActive (0x4CE4D5)
        v24 += g_Client.VarGet(0x16758B0) / 100 + g_Client.VarGet(0x16758B0) % 100; // g_AttrBuff304 (0x4CE4F8)
    Snapshot sn = buildSnapshot(s, db);
    int v23 = 0;

    for (int i = 0; i < 13; ++i) {                   // attrRatingMax sum (+304)
        const ItemInfo* it = sn.r[i];
        if (it) v24 += it->attrRatingMax;
    }
    // pet [absent]; grade [absent].
    // Char_SumGemStatA 0x54CB00 — WIRED (0x4CE58F), feeds v24 (attrRatingMax/304).
    v24 += Char_SumGemStatA(s);
    v23 += ftol((double)v24 * 15.3100004196167);     // 0x4CE5C3
    // pet-mastery flat +750/+500 [absent] (0x4CE5FA).

    for (int i = 9; i < 13; ++i) {                   // gems slots 9..12 (0x4CE64B)
        const ItemInfo* it = sn.r[i];
        if (it && classifyRecord(it) == 2) {
            // Item_SumGemStatBonus 0x4C3CC0 — WIRED (key=2, atkRatingMax channel; 0x4CE688/0x4CE6A6).
            // NB: no v23>=0 floor here (unlike CalcAttackRatingMin) — confirmed by decompilation
            // of 0x4CE3F0.
            v23 += Item_SumGemStatBonus(2, sn.sock[i]);
        }
    }
    // meridian/stance bonus (0x4CE6E1). Skill state [hook]=false -> block always runs;
    // v25=-1 (Char_FindSkillSlotByName 0x55D520 [hook]).
    {
        const int v25 = -1;
        // Slot test @0x4CE709: `cmp var_34,0/1/2/3` = set {0,1,2,3}; var_34 is typed UNSIGNED
        // INT in the 0x4CE3F0 frame. Same signedness defect as in RatingMin (v25 `int` signed
        // at -1 -> `-1 < 4` wrongly true). Explicit equality form.
        // The dword_1675664 global test is at 0x4CE728, and does target INDEX 0
        // (`cmp ds:dword_1675664, 1` with no index), not [slot].
        if ((v25 == 0 || v25 == 1 || v25 == 2 || v25 == 3) &&
            g_Client.VarGet(0x1675664) == 1) {                 // 0x4CE709 / 0x4CE728
            v23 += 5000;                                       // 0x4CE732
        } else {
            const int mEl   = g_Client.VarGet(0x16760CC);      // g_MeridianElement (0x4CE74F)
            const int mTier = g_Client.VarGet(0x16760C0);      // g_MeridianTier
            const int mPts  = g_Client.VarGet(0x16731CC);      // g_MeridianPts_RatingMax
            if (mEl == s.element || mEl == 4) {
                if      (mTier == 10 && mPts < 100) v23 += 2500; // 0x4CE773
                else if (mTier == 20 && mPts < 200) v23 += 5000; // 0x4CE79B
                else if (mTier == 30)               v23 += 7500; // 0x4CE7B1
                else                                v23 += 25 * mPts; // 0x4CE7C2
            } else {
                v23 += 25 * mPts;                              // 0x4CE7D3
            }
        }
    }

    v23 += levelStat(db, lb + lvl, 10);              // baseAtkRatingMax Stat9 (field10) (0x4CE7F2)

    // g_SpecialItem switch [absent]=0 (0x4CE899).
    // Item_ScaleStatByTypeD 0x4CB3F0 [hook]=0: guard v23>=0 -> += 0 (never *2) (0x4CEA36) —
    //   TODO(verif): same causes as Item_ScaleStatByTypeA in CalcExternalAttack (caps
    //   dword_8E717C = mPAT at the stubbed loader, undumped; "selected" item absent from
    //   SelfState).

    int v19 = EquipSetBonusId(s, db);                // 0x4CEA81
    if (v19 == 12)      v23 += 1000;
    else if (v19 == 17) v23 += 1100;

    // SkillTree_SumBonuses(4,..) (0x4CEAB1) — WIRED (2026-07-14 audit): see CalcMaxHP.
    v23 += skillTreeEquipBonus(4, sn, db);
    // talisman (0x4CEB20): Stat_UnpackCombined 0x54CE40 + Num_ToDigits8 0x54CF00 (digit 10^4, x200).
    {
        const int tslot = g_Client.VarGet(0x1674760);          // g_TalismanSlot
        if (tslot >= 10 && tslot < 20) {
            const int combined = g_Client.VarGet(0x1675664 + 4u * static_cast<uint32_t>(tslot));
            int a2 = 0;
            if (combined >= 0) { a2 = combined / 1000000; if (a2 > 100) a2 = 0; }
            if (a2 > 0) {                                      // 0x4CEB46
                const int digits = g_Client.VarGet(0x167568C + 4u * static_cast<uint32_t>(tslot));
                v23 += 200 * ((digits / 10000) % 10);          // Num_ToDigits8 -> a5/v38 (10^4); 0x4CEB8D
            }
        }
    }
    // STRICT BINARY ORDER (disassembly 0x4CEB90..0x4CEC08):
    //   BaseMaxDamage 0x4CEB9C -> SocketBonusInt 0x4CEBC0 -> SpecialDamageB 0x4CEBD7
    //   -> GetElementalBonus 0x4CEBFB. NB: this channel does NOT call SocketBonusFloat
    //   (unlike RatingMin) — confirmed by disassembly.
    // Both tables take g_SelectedInvItemId 0x1673258 == equip[8].itemId.
    // WARNING: SpecialDamageB does NOT have the same id set as SpecialDamageA (cf.
    // weaponSpecialDamageB).
    v23 += ftol(weaponBaseMaxDamage(db, s.equip[8].itemId));   // 0x4C9E10 (@0x4CEB9C/0x4CEBA1)
    // Item_SocketBonusInt [hook]=0 — TODO(verif): g_SelectedInvItemId/dword_1673260 pair
    //   (0x4CEBC0), cf. CalcMaxHP.
    v23 += ftol(weaponSpecialDamageB(db, s.equip[8].itemId));  // 0x4CA350 (@0x4CEBD7/0x4CEBDC)
    // Item_GetElementalBonus (0x4CEBFB): WIRED — (s.weaponId, s.equip[1].socket), key=4.
    v23 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 4));
    return v23;
}

} // namespace ts2::game

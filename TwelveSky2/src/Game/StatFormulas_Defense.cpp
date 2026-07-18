// Game/StatFormulas_Defense.cpp — byte-exact implementation of the defensive/utility
// channels of the Char_Calc* cluster (external/internal defense, accuracy, evasion, crit
// rate, elemental resist).
// See StatFormulas.h for the EA map and the neutralization policy, and
// StatFormulas_Internal.h for the helpers shared with StatFormulas.cpp / _Attack.cpp.
#include "Game/StatFormulas.h"
#include "Game/StatFormulas_Internal.h"
#include "Game/StatBonusContributors.h"   // Item_SumGemStatBonus, Char_SumGemStat*, SkillTree_SumBonuses
                                          // (already includes ItemSystem.h: Item_GetElementalBonus, etc.)
#include "Game/ClientRuntime.h"          // g_Client.VarGet — see the g_EquipAux block in the internal header
#include "Game/SkillSystem.h"            // Skill_GetRecord / Skill_ReadI32 / skillinfo::kOffSection
                                         // (SkillGrowthTbl_GetRecord 0x4C4E90 in CalcElementResist).
                                         // No cycle: SkillSystem.h only includes GameState.h.

namespace ts2::game {

// Char_CalcExternalDefense 0x4D2830 — channel 5 (field320).
int CalcExternalDefense(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    // + g_CoreAttr — WIRED (cf. CalcExternalAttack in StatFormulas_Attack.cpp): non-morph
    // branch, call @0x4D28DD then `add esi, eax` @0x4D28E2 (var_44 = v21).
    int v21 = s.attrExtForce + growth292(gi) + g_Client.VarGet(0x167477C); // 0x16731BC + growth292 + g_CoreAttr
    // TODO [runtime absent] 0x4D286B: morph branch. g_AttrBuffActive [absent]=0.

    const int setId = EquipSetBonusId(s, db);        // v20
    Snapshot sn = buildSnapshot(s, db);
    int v29 = 0;

    for (int i = 0; i < 13; ++i) {                   // attrPrimaryA sum (+292)
        const ItemInfo* it = sn.r[i];
        if (it) v21 += it->attrPrimaryA;
    }
    // pet [absent]; grade [absent].
    // Char_SumGemStatD 0x54CC90 — WIRED (0x4D2A14), feeds v21 (attrPrimaryA/292).
    v21 += Char_SumGemStatD(s);
    v29 += ftol((double)v21 * 1.710000038146973);    // conversion (0x4D2A47)

    v29 += levelStat(db, lb + lvl, 6);               // baseExtDefense Stat5 (field6)

    for (int i = 0; i < 13; ++i) {                   // extDefense sum (+320) + set + special
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->extDefense;                   // +320
        v29 += base;
        if (i == 8) continue;
        switch (setId) {
            case 3: case 9:                   v29 += ftol((double)base * 0.6000000238418579); break;
            case 5: case 10:                  v29 += ftol((double)base * 1.0); break;
            case 8:                           v29 += ftol((double)base * 0.4000000059604645); break;
            case 13: case 18: case 19:        v29 += ftol((double)base * 0.699999988079071); break;
            case 15: case 20:                 v29 += ftol((double)base * 1.100000023841858); break;
            case 21:                          v29 += ftol((double)base * 0.2000000029802322); break;
            case 22:                          v29 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) v29 += ftol((double)base * 0.550000011920929);
                else if (i != 9 && i != 11 && i != 12) {
                    if (classifyRecord(it) == 1 || classifyById(db, it->itemId) == 4)
                        v29 += ftol((double)base * 0.550000011920929);
                    else
                        v29 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v29 += ftol((double)base * 0.550000011920929); break;
            default: break;
        }
        // LABEL_47 (0x4D2D54): slots 3 and 7
        if (i == 3) {
            if (classifyRecord(it) == 1 || classifyRecord(it) == 4) {
                int v36 = attrByte0(sn.sock[i]);
                if (v36 > 0) { if (v36 >= 100) v36 -= 100; v29 += 1500 * v36; }
            } else if (it->skillFlag == 2) {
                // [hook] Skill_GetValueByClassB 0x54E980 -> 0
                int v36 = attrByte0(sn.sock[i]);
                if (v36 > 0) v29 += ftol((double)base * ((double)v36 * 0.02999999932944775));
            } else {
                // [hook] Item_GetScaledStat -> 0
                int v36 = attrByte0(sn.sock[i]);
                if (v36 > 0) v29 += ftol((double)(0 + base) * ((double)v36 * 0.02999999932944775));
            }
        } else if (i == 7) {
            if (it->skillFlag == 2) { /* [hook] Skill_GetValueByClassB -> 0 */ }
            else { /* [hook] Item_GetScaledStat -> 0 */ }
        }
    }

    // enchant loop (0x4D300E), key 30 (push 1Eh @0x4D300E) — pattern A, WIRED.
    enchantLoopA(v29, sn, 30);
    // stance bonus (0x4D30E3): skill state [absent] ->
    //   v17<=2 && dword_167566C(0)==1 false; combos [absent]=0 -> v29 += 2*0 = 0. TODO 0x4D30E3.
    // dword_168744C/1687450 [absent]. g_SpecialItem switch [absent]=0. slotGroup(0)!=30.
    // dword_16758E8/1675960 buffs [absent]=0. setId==14 [never with the reduced set].
    // SkillTree_SumBonuses(5,..) (0x4D330B) — WIRED (2026-07-14 audit): see CalcMaxHP
    //   (StatFormulas.cpp).
    v29 += skillTreeEquipBonus(5, sn, db);
    // g_ElementMastery==5 [absent]. talisman [absent]. Item_SocketBonusInt [hook]=0 —
    //   TODO(verif): g_SelectedInvItemId/dword_1673260 pair (0x4D3410), cf. CalcMaxHP.
    //   Escort [hook]=false. (No Item_GetElementalBonus/Item_SumGemStatBonus call in this
    //   channel — confirmed by decompilation of 0x4D2830, neither is referenced.)
    return v29;
}

// Char_CalcInternalDefense 0x4D34B0 — channel 6 (field324).
int CalcInternalDefense(const SelfState& s, const GameDatabases& db) {
    const int lvl = s.level, lb = s.levelBonus;
    const int gi  = s.growthIndex;
    // + g_CoreAttr — WIRED (cf. CalcExternalAttack in StatFormulas_Attack.cpp): non-morph
    // branch, TWO terms — call @0x4D35B1 / `add esi,eax` @0x4D35B6 (var_10 = v39) and
    // call @0x4D35E3 / `add esi,eax` @0x4D35E8 (var_28 = v32).
    const int coreAttr = g_Client.VarGet(0x167477C);
    int v39 = s.attrIntForce  + growth296(gi) + coreAttr; // 0x16731C4 + growth296 + g_CoreAttr
    int v32 = s.attrDefensive + growth300(gi) + coreAttr; // 0x16731B8 + growth300 + g_CoreAttr
    // TODO [runtime absent] 0x4D34F6: morph branch (maybe_StubReturn1B 0x54F070,
    //   Level_ToTierValueA 0x54EFB0). g_AttrBuffActive [absent]=0.

    const int setId = EquipSetBonusId(s, db);        // v23
    Snapshot sn = buildSnapshot(s, db);
    int v31 = 0;

    for (int i = 0; i < 13; ++i) {                   // attrPrimaryB(+296) & attrRatingMin(+300) sums
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        v39 += it->attrPrimaryB;                     // +296
        v32 += it->attrRatingMin;                    // +300
    }
    // pet [absent]; grade [absent].
    // Char_SumGemStatB/C 0x54CB80/0x54CC40 — WIRED (0x4D3762/0x4D3772): B feeds v39
    // (attrPrimaryB/296), C feeds v32 (attrRatingMin/300).
    v39 += Char_SumGemStatB(s);
    v32 += Char_SumGemStatC(s);
    v31 += ftol((double)v39 * 1.669999957084656);    // 0x4D37BC
    v31 += ftol((double)v32 * 0.8999999761581421);   // 0x4D37D0

    v31 += levelStat(db, lb + lvl, 7);               // baseIntDefense Stat6 (field7)

    for (int i = 0; i < 13; ++i) {                   // intDefense sum (+324) + set + special
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->intDefense;                   // +324
        v31 += base;
        if (i == 8) continue;
        switch (setId) {
            case 3: case 4: case 9:           v31 += ftol((double)base * 0.6000000238418579); break;
            case 5: case 10:                  v31 += ftol((double)base * 1.0); break;
            case 8:                           v31 += ftol((double)base * 0.4000000059604645); break;
            case 13: case 14: case 18: case 19: v31 += ftol((double)base * 0.699999988079071); break;
            case 15: case 20:                 v31 += ftol((double)base * 1.100000023841858); break;
            case 21:                          v31 += ftol((double)base * 0.2000000029802322); break;
            case 22:                          v31 += ftol((double)base * 0.1000000014901161); break;
            case 30:
                if (i == 1 || i == 10) v31 += ftol((double)base * 0.550000011920929);
                else if (i != 9 && i != 11 && i != 12) {
                    int c = classifyRecord(it);
                    if (c == 1 || c == 4) v31 += ftol((double)base * 0.550000011920929);
                    else                  v31 += ftol((double)base * 1.100000023841858);
                }
                break;
            case 50: v31 += ftol((double)base * 0.550000011920929); break;
            default: break;
        }
        // LABEL_47 switch(i) (0x4D3B19):
        switch (i) {
            case 2:
                if (it->skillFlag == 2) { /* [hook] Skill_GetValueByClassC 0x54EB20 -> 0 */ }
                else { /* [hook] Item_GetScaledStat -> 0 */ }
                break;
            case 5: {
                int c = classifyRecord(it);
                if (c == 1 || c == 4) {
                    int v40 = attrByte0(sn.sock[i]);
                    if (v40 > 0) { if (v40 >= 100) v40 -= 100; v31 += 300 * v40; }
                } else if (it->skillFlag == 2) {
                    // [hook] Skill_GetValueByClassC -> 0
                    int v40 = attrByte0(sn.sock[i]);
                    if (v40 > 0) v31 += ftol((double)base * ((double)v40 * 0.02999999932944775));
                } else {
                    // [hook] Item_GetScaledStat -> 0
                    int v40 = attrByte0(sn.sock[i]);
                    if (v40 > 0) v31 += ftol((double)(0 + base) * ((double)v40 * 0.02999999932944775));
                }
                break;
            }
            case 9: case 10: case 11: case 12: {
                // Item_SumGemStatBonus 0x4C3CC0 — WIRED (key=6, intDefense channel; 0x4D3D88/0x4D3DA6).
                // NB: slot10 is included in the SAME case here (unlike other channels where
                // slot10 gets separate handling) — confirmed by decompilation of 0x4D34B0.
                int c = classifyRecord(it);
                if (c == 2) v31 += Item_SumGemStatBonus(6, sn.sock[i]);
                break;
            }
            default: break;
        }
    }

    // enchant loop (0x4D3E03), key 40 (push 28h @0x4D3E03) — pattern A, WIRED.
    enchantLoopA(v31, sn, 40);
    // stance bonus (0x4D3ED8): skill state [absent] -> 0. TODO.
    // dword_168744C/1687450 [absent]. g_SpecialItem switch [absent]=0. slotGroup(0)!=30.
    // dword_16758F0/1675968 buffs [absent]=0.
    // SkillTree_SumBonuses(6,..) (0x4D40FF) — WIRED (2026-07-14 audit): see CalcMaxHP
    //   (StatFormulas.cpp).
    v31 += skillTreeEquipBonus(6, sn, db);
    // g_ElementMastery==4 [absent].
    // talisman [absent]. Item_SocketBonusInt [hook]=0 — TODO(verif): g_SelectedInvItemId/
    //   dword_1673260 pair (0x4D4204), cf. CalcMaxHP. Event_CanTrigger 0x54F120 [hook]=false.
    // (No Item_GetElementalBonus call in this channel — confirmed by decompilation of 0x4D34B0.)
    return v31;
}

// Char_CalcAccuracy 0x4D42D0 — field336.
int CalcAccuracy(const SelfState& s, const GameDatabases& db) {
    const int setId = EquipSetBonusId(s, db);        // v10
    Snapshot sn = buildSnapshot(s, db);
    int v15 = 2;                                     // base 2 (0x4D42D9)

    for (int i = 0; i < 13; ++i) {                   // accuracy sum (+336) + set + slot4
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->accuracy;                     // +336
        v15 += base;
        if (i == 8) continue;
        switch (setId) {
            case 5: case 9: case 10: case 21:
                v15 += ftol((double)base * 0.05000000074505806); break;
            case 22:
                v15 += ftol((double)base * 0.03999999910593033); break;
            default: break;
        }
        if (i == 4 && classifyRecord(it) != 1 && classifyRecord(it) != 4) {
            int v21 = attrByte0(sn.sock[i]);         // sub_545610(slot4 socket)
            if (v21 > 0) v15 += v21 / 4;
        }
    }

    // enchant loop (0x4D449E), key 70 (push 46h @0x4D44F3) — pattern A, WIRED.
    enchantLoopA(v15, sn, 70);

    // +5 if dword_1674A58 > 0 OR dword_1675728 > 0 (0x4D4592: `cmp,0/jg` then `cmp,0/jle`
    // -> SIGNED comparisons; add 5 @0x4D45A7). "[absent]" premise REFUTED: both globals are
    // really written on the C++ side — 0x1674A58 by Net/GameVarDispatch.cpp:337 (case 48,
    // 0x468A87) and 0x1675728 by Net/GameVarDispatch.cpp:484 (0x469113). The sibling channel
    // CalcAttackRatingMin already read the sister pair dword_1674A54/0x1675728 via VarGet.
    if (g_Client.VarGet(0x1674A58) > 0 || g_Client.VarGet(0x1675728) > 0) v15 += 5; // 0x4D45A2
    // g_SpecialItem 0x1687310 [absent]=0: switch/gems -> no effect (0x4D45CA..0x4D472A).
    // dword_1675928 buff [absent]=0 (0x4D4734).
    // dword_16747BC [absent]=0: 0<=6 && !=12 -> no addition (0x4D4767).
    switch (setId) {                                 // 0x4D47AB
        case 11: case 15: case 16: v15 += 2; break;
        case 19:                   v15 += 5; break;
        case 20: case 30: case 50: v15 += 7; break;
        default: break;
    }
    // +1 if dword_168744C == 1 && dword_1687450 == 2 (0x4D47D1/0x4D47DA, add 1 @0x4D47E6).
    // "[absent]" premise REFUTED: 0x168744C written by Net/GameVarDispatch.cpp:381 and
    // Net/GameHandlers_PartyGuild.cpp:78; 0x1687450 by GameHandlers_PartyGuild.cpp:77 and
    // Net/WorldEntityDispatch.cpp:3002-3003 (duel pair).
    if (g_Client.VarGet(0x168744C) == 1 && g_Client.VarGet(0x1687450) == 2) ++v15; // 0x4D47E1
    // GemStat_AccuracyPct 0x54CA20 — WIRED. Exact binary form (0x4D47EC..0x4D4807):
    //   fild var_8 / fstp var_4C / call GemStat_AccuracyPct / fmul var_4C / call Crt_ftol
    //   / add eax, var_8
    // -> the ftol applies to the PRODUCT, and the result is ADDED (not a replacement).
    // The helper is `(float)((double)Item_GetAttribByte2(g_Slot7Socket_Weapon 0x1673250)
    // * 0.009999999776482582)`: the (float) cast is REAL (fstp/fld on a dword @0x54CA43), it
    // rounds the product BEFORE multiplying by v15 — hence the double cast below.
    // g_Slot7Socket_Weapon 0x1673250 == g_Slot0Socket 0x16731E0 + 16*7 == equip[7].socket
    // (stride-16 array written by the network layer @0x46A9E2).
    v15 += ftol((double)(float)((double)attrByte2(s.equip[7].socket) * 0.009999999776482582)
                * (double)v15);          // 0x4D47F7/0x4D47FC/0x4D47FF/0x4D4804
    // Char_CompareSkillLoadout [hook]=0: no +2.
    // Item_SocketBonusFloat [hook]=0 — TODO(verif): g_SelectedInvItemId/dword_1673260 pair
    //   (0x4D4841), cf. CalcMaxHP. Escort [hook]=false.
    // Item_GetElementalBonus (0x4D488D): WIRED — (s.weaponId, s.equip[1].socket), key=7.
    v15 += ftol(Item_GetElementalBonus(db.item, s.weaponId, s.equip[1].socket, 7));
    // dword_16756E0 [absent]=0: no +1.
    return v15;
}

// Char_CalcEvasion 0x4D4920 — field364.
int CalcEvasion(const SelfState& s, const GameDatabases& db) {
    const int setId = EquipSetBonusId(s, db);        // v8
    Snapshot sn = buildSnapshot(s, db);
    int v11 = 0;

    for (int i = 0; i < 13; ++i) {                   // evasion sum (+364) + set
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->evasion;                      // +364
        v11 += base;
        if (i == 8) continue;
        switch (setId) {
            case 5: case 10: case 21:
                v11 += ftol((double)base * 0.05000000074505806); break;
            case 22:
                v11 += ftol((double)base * 0.03999999910593033); break;
            default: break;                          // (default case: original `continue`; base already added)
        }
    }

    // enchant loop (0x4D4A4D), key 80 (push 50h @0x4D4AA2) — pattern A, WIRED.
    enchantLoopA(v11, sn, 80);

    switch (setId) {                                 // 0x4D4B60
        case 15:                   v11 += 2; break;
        case 20: case 30: case 50: v11 += 7; break;
        default: break;
    }
    // dword_16747BC [absent]=0: switch(0) -> no addition (0x4D4B9D).
    // g_SpecialItem 0x1687310 [absent]=0: slotGroup(0)!=30 -> no gem (0x4D4BD8).
    // GemStat_EvasionPct 0x54CAD0 — WIRED. Binary form (0x4D4C4D..0x4D4C68) identical to
    // CalcAccuracy's: fild var_4 / fstp var_40 / call / fmul var_40 / call Crt_ftol
    // / add eax, var_4. Helper = `(float)((double)Item_GetAttribByte2(g_Slot1Socket
    // 0x16731F0) * 0.01600000075995922)` — real (float) cast (0x54CAF9), so the product is
    // rounded BEFORE multiplication. g_Slot1Socket 0x16731F0 == 0x16731E0 + 16*1 ==
    // equip[1].socket.
    v11 += ftol((double)(float)((double)attrByte2(s.equip[1].socket) * 0.01600000075995922)
                * (double)v11);          // 0x4D4C58/0x4D4C5D/0x4D4C60/0x4D4C65
    // g_CoreAttr 0x167477C (0x4D4C6B): `cmp g_CoreAttr,60h ; jnz` -> if == 96 then +10
    // (add 0Ah @0x4D4C77), ELSE += g_CoreAttr/10 (cdq/idiv 10 @0x4D4C84..0x4D4C8C — SIGNED
    // integer division, truncation toward zero, same as C++'s / since C++11).
    // "[absent]" premise REFUTED: g_CoreAttr is incremented/decremented by
    // Net/GameHandlers_Misc.cpp:459/467 (anchors 0x4936AD/0x493790), and this same file already
    // reads it via VarGet in CalcCritRate/CalcAttackRatingMax. CalcEvasion channel = WIRED
    // (Game/NameplateLogic.cpp:322) -> real player-facing impact.
    {
        const int coreAttr = g_Client.VarGet(0x167477C);
        if (coreAttr == 0x60) v11 += 10;             // 0x4D4C6B/0x4D4C77
        else                  v11 += coreAttr / 10;  // 0x4D4C7F..0x4D4C8C
    }
    // Escort [hook]=false. Item_GetElementalBonus [hook]=0. dword_16756E4 [absent]=0.
    return v11;
}

// Char_CalcCritRate 0x4D4D70 — field308.
int CalcCritRate(const SelfState& s, const GameDatabases& db) {
    const int setId = EquipSetBonusId(s, db);        // v4
    Snapshot sn = buildSnapshot(s, db);
    int v6 = 0;

    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        int base = it->critRate;                     // +308
        v6 += base;
        if (i == 8) continue;
        if (setId == 10 || setId == 30 || setId == 50)
            v6 += ftol((double)base * 0.05000000074505806);
        if (i == 0 && classifyRecord(it) != 1 && classifyRecord(it) != 4) {
            int v7 = attrByte0(sn.sock[0]);          // sub_545610(slot0 socket)
            if (v7 > 0) v6 += 12 * v7;
        }
    }
    // dword_1675930 buff [absent]=0 (0x4D4E9D): no % multiplier.
    return v6;
}

// Char_CalcElementResist 0x4D64B0 — elemental resistance for key `element`.
int CalcElementResist(const SelfState& s, const GameDatabases& db, int element) {
    Snapshot sn = buildSnapshot(s, db);
    int v7 = 0;

    for (int i = 0; i < 13; ++i) {
        const ItemInfo* it = sn.r[i];
        if (!it) continue;
        // 8 pairs (key +372+8k, value +376+8k).
        for (int k = 0; k < 8; ++k)
            if (it->resist[k].key == element) v7 += it->resist[k].val;
        v7 += it->resistAll;                         // +368 (added per slot)
    }

    // slot8 item class (dword_1673258 = equip[8].itemId) for the socket digits.
    int v9 = classifyById(db, s.equip[8].itemId);
    if (v9 == 7 || v9 == 5 || v9 == 6) {
        // [hook] Item_SocketDigit 0x4CAB40 -> 0 (keyed by 'g'/'R'/'S'/'i'/'h'/'T').
    }
    // Item_GemSetBonusMultiplier 0x4CA440 [hook]=0 (0x4D678A): v7 += ftol(0) = v7.
    // FINAL term (0x4D67A8..0x4D67D5), placed AFTER Item_GemSetBonusMultiplier — binary order:
    // `Record = SkillGrowthTbl_GetRecord(mSKILL, a2)` where a2 IS the `element` key, then
    // `if (Record && *(Record+540)==2 && dword_168744C==1 && dword_1687450==3) ++v9;`.
    // Offset 540 == 0x21C == skillinfo::kOffSection (idx135). SkillGrowthTbl_GetRecord 0x4C4E90
    // ≡ Skill_GetRecord (Game/SkillSystem.cpp:81, 1-based access, null if the record is empty).
    // "[absent]" premise REFUTED (cf. CalcAccuracy): 0x168744C and 0x1687450 are written by
    // Net/GameVarDispatch.cpp:381, Net/GameHandlers_PartyGuild.cpp:77-78 and
    // Net/WorldEntityDispatch.cpp:3002-3003. WIRED channel (Game/SkillCombat.cpp:512).
    {
        const uint8_t* rec = Skill_GetRecord(db.skill, element);      // 0x4D67A8
        if (rec && Skill_ReadI32(rec, skillinfo::kOffSection) == 2 && // *(Record+540)==2
            g_Client.VarGet(0x168744C) == 1 &&
            g_Client.VarGet(0x1687450) == 3) {
            ++v7;                                                     // 0x4D67D5
        }
    }
    return v7;
}

} // namespace ts2::game

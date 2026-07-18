// Game/StatFormulas.h — BYTE-EXACT stat engine (client's Char_Calc* cluster).
//
// Faithful rewrite of the 15 Char_Calc* functions of TwelveSky2.exe (imagebase 0x400000):
//   Char_CalcMaxHP            0x4D4ED0   -> CalcMaxHP
//   Char_CalcMaxMP            0x4D59B0   -> CalcMaxMP
//   Char_CalcExternalAttack   0x4D0530   -> CalcExternalAttack
//   Char_CalcInternalAttack   0x4D1830   -> CalcInternalAttack
//   Char_CalcExternalDefense  0x4D2830   -> CalcExternalDefense
//   Char_CalcInternalDefense  0x4D34B0   -> CalcInternalDefense
//   Char_CalcAccuracy         0x4D42D0   -> CalcAccuracy
//   Char_CalcEvasion          0x4D4920   -> CalcEvasion
//   Char_CalcCritRate         0x4D4D70   -> CalcCritRate
//   Char_CalcAttackRatingMin  0x4CD970   -> CalcAttackRatingMin
//   Char_CalcAttackRatingMax  0x4CE3F0   -> CalcAttackRatingMax
//   Char_CalcAttackSpeed      0x4CCAB0   -> CalcAttackSpeed
//   Char_CalcWeaponRatePct    0x4CD900   -> CalcWeaponRatePct
//   Char_CalcElementResist    0x4D64B0   -> CalcElementResist
//   Char_CalcRegen            0x4D67F0   -> CalcRegen
//
// SOURCE OF TRUTH = the disassembly. The formulas (exact float factors, level-scaling
// thresholds 112/145/156, ftol ordering, weapon-class conversions) are reproduced
// identically. The universal rounding Crt_ftol (0x760810) = truncation toward zero =
// a C++ (int) cast on a double.
//
// INPUT STATE: SelfState (Game/GameState.h: level/levelBonus/attr*/growthIndex/element*/
// equip[13]{itemId,socket}) + GameDatabases (GetLevelInfo bases Stat3..Stat9, GetItemInfo
// equip bonuses at offsets +292..+432).
//
// NEUTRALIZED TERMS (runtime globals ABSENT from SelfState, or tables not extracted — see
// the header comment of StatEngine.h). Each is flagged "[hook] EA=…" or "[runtime absent]
// EA=…" at the exact spot it would apply, with its default value (0 / false):
// Item_GetEnchantStatDelta 0x553D50, Item_GetScaledStat 0x545980, Skill_GetValue*,
// Item_Socket*/Item_ScaleStatByType* (the "selected" item g_SelectedInvItemId is ALWAYS
// absent — never written anywhere in ClientSource, cf. 2026-07-14 audit — and the mPAT
// table dword_8E717C is REALLY indeterminate, its loader PatTbl_LoadImg_STUB is stubbed),
// Item_GemSetBonusMultiplier/Equip_ComputeGemSetBonus/Weapon_* (gem/weapon tables not
// extracted), g_CoreAttr 0x167477C, g_SpecialFormActive 0x16760D4 (morph — never written,
// cf. 2026-07-14 audit: unlike g_SelfMorphNpcId 0x1675A98 which it gates and which is
// itself available via g_Client.VarGet() elsewhere in ClientSource but always gated to 0
// here), % buffs (0x16758xx/0x1675xxx), g_SpecialItem/grade 0x1687310/0x1687474, talisman
// 0x1674760/0x1675664, pet 0x1687448.
// WIRED SINCE (the "formula audit" mission, 2026-07-14): Item_SumGemStatBonus 0x4C3CC0,
// Char_SumGemStat* 0x54CB00.., Item_GetElementalBonus 0x54F590 (already wired in earlier
// waves) AND SkillTree_SumBonuses 0x54B700 (NEWLY wired this mission, in the 7 channels
// that call it — CalcMaxHP/MP, Calc{External,Internal}{Attack,Defense},
// CalcAttackRatingMin/Max — via the g_Client.Var(g_EquipAux+slot*0x0C) escape hatch, now fed
// by Net/ItemActionDispatch.cpp on inventory->equipment moves; see
// StatFormulas_Internal.h::skillTreeEquipBonus(), shared by the StatFormulas*.cpp split
// family).
#pragma once
#include "Game/GameState.h"
#include "Game/GameDatabase.h"

namespace ts2::game {

// Each function rebuilds the equipment snapshot (Char_BuildEquipSnapshot 0x4CC1C0) then
// aggregates the stat like the client does. `db` is explicit for testability (defaults to
// g_World.db on the caller side). The result is the int (or double for speed/rate) that
// the original function returns.
int    CalcMaxHP            (const SelfState& s, const GameDatabases& db); // 0x4D4ED0
int    CalcMaxMP            (const SelfState& s, const GameDatabases& db); // 0x4D59B0
int    CalcExternalAttack   (const SelfState& s, const GameDatabases& db); // 0x4D0530
int    CalcInternalAttack   (const SelfState& s, const GameDatabases& db); // 0x4D1830
int    CalcExternalDefense  (const SelfState& s, const GameDatabases& db); // 0x4D2830
int    CalcInternalDefense  (const SelfState& s, const GameDatabases& db); // 0x4D34B0
int    CalcAccuracy         (const SelfState& s, const GameDatabases& db); // 0x4D42D0
int    CalcEvasion          (const SelfState& s, const GameDatabases& db); // 0x4D4920
int    CalcCritRate         (const SelfState& s, const GameDatabases& db); // 0x4D4D70
int    CalcAttackRatingMin  (const SelfState& s, const GameDatabases& db); // 0x4CD970
int    CalcAttackRatingMax  (const SelfState& s, const GameDatabases& db); // 0x4CE3F0
double CalcAttackSpeed      (const SelfState& s, const GameDatabases& db); // 0x4CCAB0
double CalcWeaponRatePct    (const SelfState& s, const GameDatabases& db); // 0x4CD900
int    CalcElementResist    (const SelfState& s, const GameDatabases& db, int element); // 0x4D64B0
int    CalcRegen            (const SelfState& s, const GameDatabases& db); // 0x4D67F0

// Equipment set id (Equip_GetSetBonusId 0x548CE0). The EquipSet_Match*/IsPiece* families
// (id tables not extracted) are neutralized (no match); only the final deterministic
// counting branch remains: returns 50 if all 6 armor slots {0,2,3,4,5,7} carry a class 1 or
// 4 item, else 0.
// This factor drives the set switches in every Char_Calc* — exposed for tests.
int    EquipSetBonusId      (const SelfState& s, const GameDatabases& db); // 0x548CE0 (reduced)

} // namespace ts2::game

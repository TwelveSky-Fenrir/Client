// Game/StatEngine.h — derived-stat recalculation engine for the "self work block".
// C++ rewrite of the client's 12 Char_Calc* functions (Docs/TS2_GAMEPLAY_LOGIC.md §2,
// IDB TwelveSky2.exe imagebase 0x400000). Operates on SelfState (Game/GameState.h) + the
// loaded .IMG tables (GameDatabases: ITEM_INFO 436 bytes, LEVEL_INFO 44 bytes).
#pragma once
#include "Game/GameState.h"

namespace ts2::game {

// Each stat aggregates, IN ORDER (order matters: ftol doesn't commute with × %):
//   (1) allocated attribute base + cultivation growth + gems,
//   (2) LEVEL_INFO[level] base,
//   (3) Σ of stat fields across the 13 equipment pieces × set factor (slot 8 excluded),
//   (4) enchant / gem / socket / element,
//   (5) cascade of % buff multipliers.
// Universal rounding: Crt_ftol (0x760810) = truncation toward zero = (int) cast.
//
// SCOPE. Everything deterministic from SelfState + GameDatabases is reproduced exactly
// (attribute bases, growth, gems/sockets, weapon-class conversion, LEVEL_INFO base, Σ of
// ITEM_INFO fields × per-channel set factors, base weapon damage, cultivation %HP/%MP,
// elemental bonuses). Terms depending on runtime globals ABSENT from SelfState (% buffs,
// special item/grade, transformed form, escort, pet/familiar, combo, skill loadout) OR on
// unextracted tables (Item_GetScaledStat 0x545980, EquipSet_Match* families of
// Equip_GetSetBonusId 0x548CE0) are neutralized (0 / false) and flagged with a "[hook]"
// comment where they appear.
//
// ⚠ THIS PARAGRAPH HAS REPEATEDLY GONE STALE ACROSS AUDITS — don't treat it as the source
// of truth; the banner in StatFormulas.h/.cpp and the per-site comments are authoritative.
// Successive corrections:
//   - audit 2026-07-14: SkillTree_SumBonuses 0x54B700 and Item_SumGemStatBonus 0x4C3CC0
//     ARE ACTUALLY wired (g_EquipAux available via the g_Client.Var() escape hatch, item socket).
//   - wave W9 (2026-07-16): removed from the list above since they too are WIRED, since:
//       * Item_GetEnchantStatDelta 0x553D50 — the table was NEVER "unextracted": it's
//         implemented in Game/ItemSystem.cpp:323 and returns nonzero values. The 9 enchant
//         loops of the Char_Calc* functions are restored (enchantLoopA/enchantLoopRatingMin helpers).
//       * talisman (g_TalismanSlot 0x1674760, dword_1675664/167568C) — written by
//         Net/GameHandlers_Misc.cpp:668/692/693.
//       * element mastery (g_ElementMastery 0x1675680) and g_CoreAttr 0x167477C — the latter
//         written by Net/GameHandlers_Misc.cpp:459/467, now read in CalcEvasion.
//     The gem multipliers GemStat_AccuracyPct/EvasionPct/AtkRatingMinFlat
//     (0x54CA20/0x54CAD0/0x54CA50) and the 4 weapon damage tables (0x4C99F0/0x4C9E10/
//     0x4CA230/0x4CA350) are also wired as of this wave.
class StatEngine {
public:
    // Recomputes ALL derived stats of s in place (uses g_World.db).
    // Equivalent to the chain of Char_Calc* triggered after a stat packet.
    // Doesn't touch current hp/mp (server-authoritative); only fills derived fields.
    static void Recompute(SelfState& s);

    // Testable variant: explicit databases (doesn't use g_World).
    static void Recompute(SelfState& s, const GameDatabases& db);

    // --- Individual stats (same EA as the client; each rebuilds the snapshot) ---
    static int CalcMaxHp           (const SelfState& s, const GameDatabases& db); // 0x4D4ED0
    static int CalcMaxMp           (const SelfState& s, const GameDatabases& db); // 0x4D59B0
    static int CalcExternalAttack  (const SelfState& s, const GameDatabases& db); // 0x4D0530
    static int CalcInternalAttack  (const SelfState& s, const GameDatabases& db); // 0x4D1830
    static int CalcExternalDefense (const SelfState& s, const GameDatabases& db); // 0x4D2830
    static int CalcInternalDefense (const SelfState& s, const GameDatabases& db); // 0x4D34B0
    static int CalcAccuracy        (const SelfState& s, const GameDatabases& db); // 0x4D42D0
    static int CalcEvasion         (const SelfState& s, const GameDatabases& db); // 0x4D4920
    static int CalcCritRate        (const SelfState& s, const GameDatabases& db); // 0x4D4D70
    static int CalcAttackRatingMin (const SelfState& s, const GameDatabases& db); // 0x4CD970
    static int CalcAttackRatingMax (const SelfState& s, const GameDatabases& db); // 0x4CE3F0
    static int CalcAttackSpeed     (const SelfState& s, const GameDatabases& db); // 0x4CCAB0
};

} // namespace ts2::game

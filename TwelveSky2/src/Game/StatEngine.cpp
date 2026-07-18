// Game/StatEngine.cpp — delegates to Game/StatFormulas (direct IDA decompilation,
// ts2-ida-gameplay-core workflow), more faithful than the initial implementation
// (which explicitly flattened per-level scaling — see history). The exact scaling
// (thresholds 112/145/156), ftol ordering, and set factors are now reproduced in
// StatFormulas.cpp. StatEngine remains the stable public facade for existing
// callers (ItemSystem/CombatSystem/UI).
#include "Game/StatEngine.h"
#include "Game/StatFormulas.h"

namespace ts2::game {

int StatEngine::CalcMaxHp           (const SelfState& s, const GameDatabases& db) { return CalcMaxHP(s, db); }
int StatEngine::CalcMaxMp           (const SelfState& s, const GameDatabases& db) { return CalcMaxMP(s, db); }
int StatEngine::CalcExternalAttack  (const SelfState& s, const GameDatabases& db) { return game::CalcExternalAttack(s, db); }
int StatEngine::CalcInternalAttack  (const SelfState& s, const GameDatabases& db) { return game::CalcInternalAttack(s, db); }
int StatEngine::CalcExternalDefense (const SelfState& s, const GameDatabases& db) { return game::CalcExternalDefense(s, db); }
int StatEngine::CalcInternalDefense (const SelfState& s, const GameDatabases& db) { return game::CalcInternalDefense(s, db); }
int StatEngine::CalcAccuracy        (const SelfState& s, const GameDatabases& db) { return game::CalcAccuracy(s, db); }
int StatEngine::CalcEvasion         (const SelfState& s, const GameDatabases& db) { return game::CalcEvasion(s, db); }
int StatEngine::CalcCritRate        (const SelfState& s, const GameDatabases& db) { return game::CalcCritRate(s, db); }
int StatEngine::CalcAttackRatingMin (const SelfState& s, const GameDatabases& db) { return game::CalcAttackRatingMin(s, db); }
int StatEngine::CalcAttackRatingMax (const SelfState& s, const GameDatabases& db) { return game::CalcAttackRatingMax(s, db); }
int StatEngine::CalcAttackSpeed     (const SelfState& s, const GameDatabases& db) { return static_cast<int>(game::CalcAttackSpeed(s, db)); }

// ===========================================================================
// Recompute — fills in all derived fields of SelfState.
//
// ⚠ MISSING WIRING (found wave W9, 2026-07-16) — TO FIX OUTSIDE THIS FILE.
// Exhaustive grep over src/: `StatEngine::Recompute` has NO caller, and the 12 derived
// SelfState fields (maxHp/maxMp/extAtk/intAtk/extDef/intDef/accuracy/evasion/critRate/
// atkRatingMin/atkRatingMax/attackSpeed) are never written anywhere else (no memcpy, no
// aggregate assignment — verified). Result: 9 of the 12 formula channels are never
// evaluated. Only CalcAttackRatingMin/Max (via Net/*), CalcEvasion
// (Game/NameplateLogic.cpp:322), and CalcElementResist (Game/SkillCombat.cpp:512) are
// reached — and those call game::Calc* DIRECTLY, bypassing Recompute.
//
// FIDELITY — do NOT just call Recompute at network dispatch points: that's not how the
// binary works. Split proven by xrefs:
//   * Char_CalcMaxHP 0x4D4ED0 / Char_CalcAccuracy 0x4D42D0 -> SOLE caller cDrawWin_Draw
//     0x629960 (reachable via UI_RenderAllDialogs 0x5AE2D0): these are CHARACTER SHEET
//     calculations, recomputed on EVERY RENDER. => wire INLINE into
//     UI/CharacterStatsWindow.cpp (~l.432-445, which already reads self.maxHp/…) by calling
//     StatEngine::CalcMaxHp(self, db) etc.
//   * HUD HP/MP bars: fed by the combat channel body+288/+296 (Char_CalcAttackRatingMin/Max
//     0x4CD970/0x4CE3F0, already wired via RecomputeSelfBars) and NOT by Char_CalcMaxHP.
//     => UI/GameHud.cpp (~l.636-637) must sync on this channel, mirroring the sync already
//     done for self.hp/mp.
//   * Char_CalcAttackSpeed 0x4CCAB0 -> 16 binary callers (Game_OnWorldLeftClick,
//     Player_CastSkill, Player_AttackTargetMonster/Player, Npc_Interact, Gather_InteractNode,
//     AutoPlay_*): wire into combat/interaction for attack timing.
// These three files aren't owned by the stat-gems-ench front (rule 5) -> escalated. Until
// this wiring lands, W9 fixes on CalcMaxHP/MaxMP/ExtAtk/IntAtk/ExtDef/IntDef/Accuracy (enchant
// loops, talisman, +5/+1) stay NON-OBSERVABLE; fixes on CalcAttackRatingMin/Max, CalcEvasion,
// and CalcElementResist are immediately effective since those channels are wired.
// ===========================================================================
void StatEngine::Recompute(SelfState& s, const GameDatabases& db) {
    s.maxHp        = CalcMaxHp(s, db);
    s.maxMp        = CalcMaxMp(s, db);
    s.extAtk       = CalcExternalAttack(s, db);
    s.intAtk       = CalcInternalAttack(s, db);
    s.extDef       = CalcExternalDefense(s, db);
    s.intDef       = CalcInternalDefense(s, db);
    s.accuracy     = CalcAccuracy(s, db);
    s.evasion      = CalcEvasion(s, db);
    s.critRate     = CalcCritRate(s, db);
    s.atkRatingMin = CalcAttackRatingMin(s, db);
    s.atkRatingMax = CalcAttackRatingMax(s, db);
    s.attackSpeed  = CalcAttackSpeed(s, db);
    // current hp/mp remain server-authoritative; not recalculated here.
}

void StatEngine::Recompute(SelfState& s) { Recompute(s, g_World.db); }

} // namespace ts2::game

// Game/StatEngine.cpp — délègue à Game/StatFormulas (décompilation IDA directe,
// workflow ts2-ida-gameplay-core), plus fidèle que l'implémentation initiale
// (qui aplatissait explicitement le scaling par niveau — cf. historique). Le
// scaling exact (seuils 112/145/156), l'ordre des ftol et les facteurs de set
// sont désormais reproduits dans StatFormulas.cpp. StatEngine reste la façade
// publique stable pour les appelants existants (ItemSystem/CombatSystem/UI).
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
// Recompute — remplit tous les champs dérivés du SelfState.
//
// ⚠ CÂBLAGE MANQUANT (constaté vague W9, 2026-07-16) — À TRAITER HORS DE CE FICHIER.
// Grep exhaustif sur src/ : `StatEngine::Recompute` n'a AUCUN appelant, et les 12 champs
// dérivés de SelfState (maxHp/maxMp/extAtk/intAtk/extDef/intDef/accuracy/evasion/critRate/
// atkRatingMin/atkRatingMax/attackSpeed) ne sont écrits NULLE PART ailleurs (aucun memcpy ni
// affectation d'agrégat — vérifié). Conséquence : 9 des 12 canaux de formules ne sont jamais
// évalués. Seuls CalcAttackRatingMin/Max (via Net/*), CalcEvasion (Game/NameplateLogic.cpp:322)
// et CalcElementResist (Game/SkillCombat.cpp:512) sont atteints — et ceux-là appellent
// game::Calc* DIRECTEMENT, sans passer par Recompute.
//
// FIDÉLITÉ — ne PAS se contenter d'appeler Recompute aux points de dispatch réseau : le binaire
// ne procède pas ainsi. Découpage prouvé par les xrefs :
//   * Char_CalcMaxHP 0x4D4ED0 / Char_CalcAccuracy 0x4D42D0 -> appelant UNIQUE cDrawWin_Draw
//     0x629960 (reachable via UI_RenderAllDialogs 0x5AE2D0) : ce sont des calculs de FICHE
//     PERSO, recalculés À CHAQUE RENDU. => à câbler INLINE dans UI/CharacterStatsWindow.cpp
//     (~l.432-445, qui lit déjà self.maxHp/…) en appelant StatEngine::CalcMaxHp(self, db) etc.
//   * Barres PV/PM du HUD : alimentées par le canal de combat body+288/+296
//     (Char_CalcAttackRatingMin/Max 0x4CD970/0x4CE3F0, déjà câblé via RecomputeSelfBars) et
//     NON par Char_CalcMaxHP. => UI/GameHud.cpp (~l.636-637) doit se synchroniser sur ce canal,
//     en symétrie du sync déjà fait pour self.hp/mp.
//   * Char_CalcAttackSpeed 0x4CCAB0 -> 16 appelants binaires (Game_OnWorldLeftClick,
//     Player_CastSkill, Player_AttackTargetMonster/Player, Npc_Interact, Gather_InteractNode,
//     AutoPlay_*) : à câbler dans le combat/interaction pour le timing d'attaque.
// Ces trois fichiers ne sont pas la propriété du front stat-gems-ench (règle 5) -> escaladé.
// Tant que ce câblage n'est pas posé, les correctifs W9 portant sur CalcMaxHP/MaxMP/ExtAtk/
// IntAtk/ExtDef/IntDef/Accuracy (boucles d'enchant, talisman, +5/+1) restent NON OBSERVABLES ;
// ceux sur CalcAttackRatingMin/Max, CalcEvasion et CalcElementResist sont, eux, immédiatement
// effectifs car ces canaux sont câblés.
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
    // hp/mp courants restent pilotés par le serveur ; on ne les recalcule pas ici.
}

void StatEngine::Recompute(SelfState& s) { Recompute(s, g_World.db); }

} // namespace ts2::game

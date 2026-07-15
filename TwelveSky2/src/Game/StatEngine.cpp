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

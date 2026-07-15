// Game/StatEngine.h — moteur de recalcul des stats dérivées du « self work block ».
// Réécriture C++ des 12 fonctions Char_Calc* du client (Docs/TS2_GAMEPLAY_LOGIC.md §2,
// IDB TwelveSky2.exe imagebase 0x400000). Opère sur SelfState (Game/GameState.h) + les
// tables .IMG chargées (GameDatabases : ITEM_INFO 436 o, LEVEL_INFO 44 o).
#pragma once
#include "Game/GameState.h"

namespace ts2::game {

// Chaque stat agrège, DANS L'ORDRE (l'ordre importe : ftol ne commute pas avec les × %) :
//   (1) base d'attributs alloués + croissance par cultivation + gemmes,
//   (2) base issue de LEVEL_INFO[niveau],
//   (3) Σ des champs de stat des 13 pièces d'équipement × facteur de set (slot 8 exclu),
//   (4) enchant / gemme / socket / élément,
//   (5) cascade de multiplicateurs de buffs %.
// Arrondi universel : Crt_ftol (0x760810) = troncature vers zéro = cast (int).
//
// PÉRIMÈTRE. Tout ce qui est déterministe à partir de SelfState + GameDatabases est reproduit
// à l'identique (bases d'attributs, croissance, gemmes/sockets, conversion classe d'arme, base
// LEVEL_INFO, Σ champs ITEM_INFO × facteurs de set par canal, dégâts de base d'arme, %HP/%MP de
// cultivation, bonus élémentaires). Les termes qui dépendent de globals runtime ABSENTS de
// SelfState (buffs %, objet spécial / grade, forme transformée, escorte, talisman, maîtrise
// d'élément, familier/pet, combo, loadout de skills, g_CoreAttr) OU de tables non extraites
// (Item_GetEnchantStatDelta 0x553D50, Item_GetScaledStat 0x545980, familles d'ids
// EquipSet_Match* de Equip_GetSetBonusId 0x548CE0) sont neutralisés (0 / false) et signalés en
// commentaire « [hook] » là où ils apparaissent. NB (audit 2026-07-14) : SkillTree_SumBonuses
// 0x54B700 et Item_SumGemStatBonus 0x4C3CC0 sont, malgré ce paragraphe, RÉELLEMENT câblés dans
// Game/StatFormulas.cpp (leurs arguments runtime — g_EquipAux via l'échappatoire g_Client.Var(),
// socket d'item — se sont avérés disponibles) ; ce paragraphe générique n'a pas été retenu à
// jour, voir le bandeau de tête de StatFormulas.h/.cpp pour l'état précis site par site.
class StatEngine {
public:
    // Recalcule TOUTES les stats dérivées de s en place (utilise g_World.db).
    // Équivaut à l'enchaînement des Char_Calc* déclenché après un paquet de stats.
    // Ne touche pas hp/mp courants (autorité serveur) ; ne fait que remplir les champs dérivés.
    static void Recompute(SelfState& s);

    // Variante testable : bases de données explicites (n'utilise pas g_World).
    static void Recompute(SelfState& s, const GameDatabases& db);

    // --- Stats individuelles (mêmes EA que le client ; chacune reconstruit le snapshot) ---
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

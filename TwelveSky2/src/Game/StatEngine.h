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
// SelfState (buffs %, objet spécial / grade, forme transformée, escorte, familier/pet, combo,
// loadout de skills) OU de tables non extraites (Item_GetScaledStat 0x545980, familles d'ids
// EquipSet_Match* de Equip_GetSetBonusId 0x548CE0) sont neutralisés (0 / false) et signalés en
// commentaire « [hook] » là où ils apparaissent.
//
// ⚠ CE PARAGRAPHE A ÉTÉ RÉGULIÈREMENT PÉRIMÉ PAR LES AUDITS — ne pas s'y fier comme source de
// vérité ; le bandeau de StatFormulas.h/.cpp et les commentaires site par site font foi.
// Corrections successives :
//   - audit 2026-07-14 : SkillTree_SumBonuses 0x54B700 et Item_SumGemStatBonus 0x4C3CC0 sont
//     RÉELLEMENT câblés (g_EquipAux dispo via l'échappatoire g_Client.Var(), socket d'item).
//   - vague W9 (2026-07-16) : retirés de la liste ci-dessus car eux aussi CÂBLÉS depuis :
//       * Item_GetEnchantStatDelta 0x553D50 — la table N'A JAMAIS ÉTÉ « non extraite » : elle
//         est implémentée dans Game/ItemSystem.cpp:323 et renvoie du non-nul. Les 9 boucles
//         d'enchant des Char_Calc* sont rétablies (helpers enchantLoopA/enchantLoopRatingMin).
//       * talisman (g_TalismanSlot 0x1674760, dword_1675664/167568C) — écrits par
//         Net/GameHandlers_Misc.cpp:668/692/693.
//       * maîtrise d'élément (g_ElementMastery 0x1675680) et g_CoreAttr 0x167477C — ce dernier
//         écrit par Net/GameHandlers_Misc.cpp:459/467, désormais lu dans CalcEvasion.
//     Les multiplicateurs de gemmes GemStat_AccuracyPct/EvasionPct/AtkRatingMinFlat
//     (0x54CA20/0x54CAD0/0x54CA50) et les 4 tables de dégâts d'arme (0x4C99F0/0x4C9E10/
//     0x4CA230/0x4CA350) sont également câblés depuis cette vague.
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

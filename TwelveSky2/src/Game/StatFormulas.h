// Game/StatFormulas.h — moteur de stats BYTE-EXACT (cluster Char_Calc* du client).
//
// Réécriture fidèle des 15 fonctions Char_Calc* de TwelveSky2.exe (imagebase 0x400000) :
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
// SOURCE DE VÉRITÉ = le désassemblage. Les formules (facteurs float exacts, seuils de
// scaling niveau 112/145/156, ordre des ftol, conversions de classe d'arme) sont
// reproduites à l'identique. L'arrondi universel Crt_ftol (0x760810) = troncature vers
// zéro = cast (int) sur double.
//
// ÉTAT PRIS EN ENTRÉE : SelfState (Game/GameState.h : level/levelBonus/attr*/growthIndex/
// element*/equip[13]{itemId,socket}) + GameDatabases (GetLevelInfo bases Stat3..Stat9,
// GetItemInfo bonus d'équip aux offsets +292..+432).
//
// TERMES NEUTRALISÉS (globals runtime ABSENTS de SelfState ou tables non extraites — cf.
// commentaire de tête de StatEngine.h). Chacun est signalé « [hook] EA=… » ou
// « [runtime absent] EA=… » à l'endroit exact où il s'appliquerait, avec sa valeur par
// défaut (0 / false) : Item_GetEnchantStatDelta 0x553D50, Item_GetScaledStat 0x545980,
// Skill_GetValue* , Item_Socket*/Item_ScaleStatByType* (item "sélectionné" g_SelectedInvItemId
// TOUJOURS absent — jamais écrit nulle part dans ClientSource, cf. audit 2026-07-14 —
// et table mPAT dword_8E717C RÉELLEMENT indéterminée, loader stubbé PatTbl_LoadImg_STUB),
// Item_GemSetBonusMultiplier/Equip_ComputeGemSetBonus/Weapon_* (tables gemmes/armes non
// extraites), g_CoreAttr 0x167477C, g_SpecialFormActive 0x16760D4 (morph — jamais écrit,
// cf. audit 2026-07-14 : contrairement à g_SelfMorphNpcId 0x1675A98 qu'il gate, lui-même
// disponible via g_Client.VarGet() ailleurs dans ClientSource mais toujours gaté à 0 ici),
// buffs % (0x16758xx/0x1675xxx), g_SpecialItem/grade 0x1687310/0x1687474, talisman
// 0x1674760/0x1675664, pet 0x1687448.
// CÂBLÉS DEPUIS (mission « audit formules », 2026-07-14) : Item_SumGemStatBonus 0x4C3CC0,
// Char_SumGemStat* 0x54CB00.., Item_GetElementalBonus 0x54F590 (déjà câblés lors de vagues
// précédentes) ET SkillTree_SumBonuses 0x54B700 (câblage NOUVEAU cette mission, dans les 7
// canaux qui l'appellent — CalcMaxHP/MP, Calc{External,Internal}{Attack,Defense},
// CalcAttackRatingMin/Max — via l'échappatoire g_Client.Var(g_EquipAux+slot*0x0C), désormais
// alimentée par Net/ItemActionDispatch.cpp lors des déplacements inventaire->équipement ;
// cf. StatFormulas.cpp::skillTreeEquipBonus()).
#pragma once
#include "Game/GameState.h"
#include "Game/GameDatabase.h"

namespace ts2::game {

// Chaque fonction reconstruit le snapshot d'équipement (Char_BuildEquipSnapshot 0x4CC1C0)
// puis agrège la stat comme le client. `db` explicite pour testabilité (défaut g_World.db
// côté appelant). Le résultat est l'entier (ou double pour vitesse/taux) retourné par
// la fonction d'origine.
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

// Identifiant de set d'équipement (Equip_GetSetBonusId 0x548CE0). Les familles
// EquipSet_Match*/IsPiece* (tables d'ids non extraites) sont neutralisées (aucun match) ;
// il ne subsiste que la branche déterministe de comptage finale : renvoie 50 si les 6
// slots d'armure {0,2,3,4,5,7} portent tous un item de classe 1 ou 4, sinon 0.
// Ce facteur pilote les switch de set dans chaque Char_Calc* — exposé pour tests.
int    EquipSetBonusId      (const SelfState& s, const GameDatabases& db); // 0x548CE0 (réduit)

} // namespace ts2::game

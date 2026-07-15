// Game/StatBonusContributors.h — contributeurs de stats actuellement NEUTRALISÉS dans
// StatFormulas.cpp (bonus d'enchantement, de gemmes/raffinage et d'arbre de compétences).
//
// Réécriture C++ PROPRE, mais NUMÉRIQUEMENT FIDÈLE au désassemblage de TwelveSky2.exe
// (tables/constantes recopiées à l'octet près). Source de vérité : IDB idaTs2 (décompilation
// directe des adresses ci-dessous), recoupée avec Docs/TS2_GAMEPLAY_LOGIC.md.
//
// Correspondance fonction ↔ adresse d'origine :
//   Item_GetEnchantStatDelta 0x553D50  — PROTOTYPE SEULEMENT ici (voir note ci-dessous).
//   Item_SumGemStatBonus     0x4C3CC0
//   Item_GemStatBonusLookup  0x4C3D90  (helper interne, non exposé dans l'en-tête)
//   Char_SumGemStatA         0x54CB00
//   Char_SumGemStatB         0x54CB80
//   Char_SumGemStatC         0x54CC40
//   Char_SumGemStatD         0x54CC90
//   SkillTree_SumBonuses     0x54B700
//   SkillTree_GetNodeValue   0x54B830  (helper interne, non exposé dans l'en-tête)
//   AnchorTbl_FindByKey      0x4C7630  (helper interne, opère sur GameDatabases::socketT)
//
// ---------------------------------------------------------------------------------------
// NOTE IMPORTANTE — Item_GetEnchantStatDelta est DÉJÀ implémentée dans ItemSystem.cpp/.h
// (vérifiée byte-exacte par re-décompilation de 0x553D50 : la table de cas (classe, slot,
// clé, niveau d'enchant) déjà écrite dans ItemSystem.cpp correspond EXACTEMENT au
// désassemblage actuel). Elle N'EST PAS un stub — contrairement à ce que laisse penser le
// commentaire « TERMES NEUTRALISÉS » de StatFormulas.h : ce dernier ne dit pas que la
// fonction est absente, mais que StatFormulas.cpp ne l'APPELLE PAS ENCORE (le point d'appel
// est neutralisé côté StatFormulas.cpp, pas la fonction elle-même).
// On se contente donc ici de RE-DÉCLARER son prototype (aucune redéfinition — sinon double
// définition au link avec ItemSystem.cpp) afin que ce module reste self-describing. Le
// prototype est recopié à l'identique de ItemSystem.h.
//
// ⚠️ POINT D'ATTENTION HISTORIQUE : les anciennes notes de câblage parlaient d'un conflit
// de type `ItemInfo` entre GameDatabase.h et ItemSystem.h. Ce n'est plus le cas dans l'arbre
// courant : GameDatabase.h exporte `ts2::game::ItemInfo` (POD 436 o byte-exact) et
// ItemSystem.h n'exporte qu'une vue distincte `ItemInfoView`. Aucun conflit d'ODR n'est donc
// présent pour le câblage actuel de StatFormulas.cpp.
// ---------------------------------------------------------------------------------------
#pragma once
#include <cstdint>
#include "Game/GameState.h"
#include "Game/ItemSystem.h"   // Item_GetAttribByte0..3, DataTable — voir note conflit ci-dessus.

namespace ts2::game {

// =====================================================================
// Item_GetEnchantStatDelta 0x553D50 — PROTOTYPE UNIQUEMENT (définition dans ItemSystem.cpp).
// Signature identique à celle déjà déclarée dans ItemSystem.h : ne pas redéfinir ici.
// =====================================================================
int Item_GetEnchantStatDelta(int itemClass, int slot, uint32_t socketWord, int key);

// =====================================================================
// Item_SumGemStatBonus 0x4C3CC0 — bonus de gemmes d'un item À 4 emplacements de gemme
// (accessoires : bagues/colliers/boucles — boucle d'appel observée en slots 9..12).
//   key        : clé de stat (observée = 1 dans le seul site d'appel décompilé ; les mêmes
//                clés que Item_GemStatBonusLookup : 1,2,4,6,8 — 3/5/7 toujours 0).
//   socketWord : mot socket 32 bits de l'item ; ses 4 octets (Item_GetAttribByte0..3) sont
//                CHACUN un « niveau de gemme » 0..100 indépendant (jusqu'à 4 gemmes/item).
//                Les octets 0..2 utilisent le groupe 1 (paliers 41-60/61-80/1-20/21-40/81-100),
//                l'octet 3 utilise le groupe 2 (paliers de 5 + plage spéciale clé 1).
// Renvoie la somme des 4 lookups (0 si socketWord == 0).
// =====================================================================
int Item_SumGemStatBonus(int key, uint32_t socketWord);

// =====================================================================
// Char_SumGemStatA/B/C/D 0x54CB00/0x54CB80/0x54CC40/0x54CC90 — bonus de raffinage/gemme
// « plat » (5 points par palier, +5 bonus si palier == 25) lu sur l'octet2 (Item_GetAttribByte2
// = « catégorie socket float / nb gemmes ») du mot socket de CHAQUE emplacement d'équipement
// concerné. Dans l'original ce sont des fonctions SANS PARAMÈTRE qui lisent directement les
// globals g_SlotNSocket (0x16731E0 + 16*N, N = index de slot) ; ici on prend `s.equip[N].socket`
// (SelfState) pour rester testable, comme le reste du moteur de stats (StatFormulas.h).
//   A (slots 7 [arme] + 2)               — alimente Char_SumAttrField304 (attrRatingMax/+304)
//   B (slots 3 + 5 + 1)                  — alimente Char_SumAttrField296 (attrPrimaryB/+296)
//   C (slot 4)                            — alimente Char_SumAttrField300 (attrRatingMin/+300)
//   D (slot 0)                            — alimente Char_SumAttrField292 (attrPrimaryA/+292)
// (mapping déduit des appelants réels : Char_SumAttrField29X/30X → Char_SumGemStat{A..D}).
// =====================================================================
int Char_SumGemStatA(const SelfState& s);
int Char_SumGemStatB(const SelfState& s);
int Char_SumGemStatC(const SelfState& s);
int Char_SumGemStatD(const SelfState& s);

// =====================================================================
// SkillTree_SumBonuses 0x54B700 — somme jusqu'à 5 bonus de nœuds d'arbre de compétences.
//   category      : catégorie transmise telle quelle à SkillTree_GetNodeValue (a1 d'origine).
//                   8 sites d'appel confirmés par re-décompilation (mission « audit formules »,
//                   2026-07-14), tous dans une boucle 0..13 sur les slots d'équip occupés, avec
//                   g_EquipAux/dword_16750BC/dword_16750C0[3*i] comme blocs (STATFORMULAS.CPP
//                   les câble via g_Client.Var(), cf. StatFormulas.cpp::skillTreeEquipBonus()) :
//                     CalcMaxHP 0x4D57C9 cat=7, CalcMaxMP 0x4D6299 cat=8,
//                     CalcExternalAttack 0x4D151B cat=1, CalcInternalAttack 0x4D25F7 cat=2,
//                     CalcExternalDefense 0x4D330B cat=5, CalcInternalDefense 0x4D40FF cat=6,
//                     CalcAttackRatingMin 0x4CE18F cat=3 (fusionné dans une boucle existante),
//                     CalcAttackRatingMax 0x4CEAB1 cat=4.
//   block0/1/2    : 3 dwords bit-packés (paramètres a2/a3/a4 d'origine — l'ABI stdcall réelle
//                   passe 3 dwords pleins malgré le typage `char` trompeur du décompilateur ;
//                   Hex-Rays sur-affine les types car seuls certains octets sont relus).
//                   Layout exact (reproduit du Crt_Memcpy d'origine, little-endian) :
//                     block0 octet1 = nombre de paires actives n (1..5, sinon renvoie 0) ;
//                     block0 octet2/3   = paire #0 (id,valeur)
//                     block1 octet0/1   = paire #1 (id,valeur)   block1 octet2/3 = paire #2
//                     block2 octet0/1   = paire #3 (id,valeur)   block2 octet2/3 = paire #4
//                   (chaque octet est un `char` SIGNÉ dans l'original — sign-extend à l'identique).
//   db            : GameDatabases (accès à db.socketT — table SOCKET_INFO 20 o, requise par
//                   le helper interne SkillTree_GetNodeValue/AnchorTbl_FindByKey).
// Pour chaque paire i < n dont l'id est non nul, ajoute SkillTree_GetNodeValue(category, id, val).
// =====================================================================
int SkillTree_SumBonuses(int category, uint32_t block0, uint32_t block1, uint32_t block2,
                          const GameDatabases& db);

} // namespace ts2::game

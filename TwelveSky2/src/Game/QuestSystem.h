// Game/QuestSystem.h — Systeme de quetes du client TwelveSky2 (ts2::game).
//
// Reecriture PROPRE (pas byte-exact, sauf QuestInfo qui est un overlay memoire) de la
// logique de quetes relevee dans le desassemblage de TwelveSky2.exe (imagebase 0x400000).
// Verite = le DESASM (MCP idaTs2). Comble le TODO(state) laisse par
// Net/GameHandlers_Misc.cpp sur le paquet 0x27 (QuestInteractResult, alias
// Pkt_SmithUpgradeResult 0x48E7D0 — mal nomme dans l'IDB d'origine) en fournissant les
// briques manquantes : table de definitions de quete, record d'etape courant
// (equivalent de g_pCurQuestStepRecord / dword_18231B4) et evaluation d'objectif.
//
// Fonctions d'origine reproduites ici (verite = decompilation reelle, une par une) :
//   QuestTbl_ValidateRecord      0x4C84C0 -> QuestTbl_ValidateRecord
//   QuestTbl_LoadImg             0x4C8630 -> LoadQuestTable
//   QuestTbl_GetRecord           0x4C88B0 -> QuestTbl_GetRecord
//   QuestTbl_FindByGroupAndName  0x4C8900 -> QuestTbl_FindByGroupAndName
//   QuestTbl_FindFirstByGroup    0x4C89C0 -> QuestTbl_FindFirstByGroup
//   QuestTbl_FindByGroupAndStage 0x4C8A60 -> QuestTbl_FindByGroupAndStage
//   QuestTbl_CountByGroup        0x4C8AF0 -> QuestTbl_CountByGroup
//   QuestTbl_CountByGroupUpTo    0x4C8B60 -> QuestTbl_CountByGroupUpTo
//   QuestTbl_FindFirstOfGroup    0x4C8BD0 -> QuestTbl_FindFirstOfGroup
//   QuestTbl_FindPrevOfGroup     0x4C8C40 -> QuestTbl_FindPrevOfGroup
//   QuestTbl_FindNextOfGroup     0x4C8CC0 -> QuestTbl_FindNextOfGroup
//   Quest_CheckObjectiveState    0x50FF10 -> Quest_CheckObjectiveState
//   Quest_IsObjectiveComplete    0x5103F0 -> Quest_IsObjectiveComplete
//   Quest_GetObjectiveResult     0x510520 -> Quest_GetObjectiveResult
//   Quest_GetRewardItemId        0x510A10 -> Quest_GetRewardItemId
//   Quest_IsRewardItemActive     0x510A90 -> Quest_IsRewardItemActive
//   Quest_IsItemAllowed          0x54F0B0 -> Quest_IsItemAllowed
// Fonctions de soutien decompilees pour completer fidelement le paquet 0x27 (callees
// directes de Pkt_SmithUpgradeResult, memes offsets de struct que Quest_CheckObjectiveState) :
//   Inventory_RemoveItem         0x510C40 -> Quest_RemoveTrackedItem
//   Inventory_ReplaceItem        0x510B40 -> Quest_ReplaceTrackedItem
//   Util_SumExceeds2Billion      0x53F660 -> Quest_SumExceeds2Billion
//   Pkt_SmithUpgradeResult       0x48E7D0 -> ApplyQuestInteractResultState (partie ETAT
//                                             uniquement — la partie MESSAGE est deja
//                                             geree par Net/GameHandlers_Misc.cpp)
//
// ---------------------------------------------------------------------------------
// DEUX sources de donnees "quete" DISTINCTES existent dans le binaire :
//
//  (A) QuestTbl — 999 x 84 o, fichier G03_GDATA\D01_GIMAGE2D\005\005_00007.IMG (pas de
//      XOR sur le compteur, pas de nom de table embarque — a la difference des 5 tables
//      de GameDatabase.cpp). Table de PROGRESSION scenaristique (groupe/etape/valeur),
//      consultee par l'UI d'aide (mHELP -> UI_CharListWnd_*, xrefs verifiees). C'est la
//      QUEST_INFO deduite/chargee ici (struct QuestInfo, DataTable g_QuestTable).
//
//  (B) Table mQUEST — 1000 lignes x 8444 o (005_00006.IMG). Quest_CheckObjectiveState/
//      IsObjectiveComplete/GetObjectiveResult/GetRewardItemId/IsRewardItemActive ET
//      g_pCurQuestStepRecord (dword_18231B4) operent TOUS sur CE type d'enregistrement
//      (offsets +64/+72/+92/+96/+116/+120/+124/+136..+156 confirmes par decompilation
//      croisee des 5 fonctions + de Pkt_SmithUpgradeResult). Modelise ici via
//      QuestStepRecord — une VUE PROPRE limitee aux seuls champs consommes (pas un overlay
//      memoire des 8444 o), projetee depuis game::QuestDefRecord (Game/ExtraDatabases.h).
//
//      L'ancien bandeau « HORS PERIMETRE — aucun loader assigne » etait PERIME : mQUEST
//      0x8E71E4 EST chargee, par `call NpcTbl_LoadImg 0x4C8090` a l'EA 0x4621A0 dans
//      App_Init (echec => MessageBoxA "[Error::mQUEST.Init()]" + App_Init renvoie 0). Cote
//      C++ elle l'est aussi, dans g_ExtraDb.quest (Game/ExtraDatabases.cpp:47).
//
//      RESOLUTION — le binaire n'a AUCUNE indirection : il appelle en dur
//      `NpcTbl_FindByTypeAndId(mQUEST, element0, questId)` 0x4C8340 (EA 0x50FF65, 0x50FFCA,
//      0x510A37, 0x510AB7, 0x664A67, 0x510E40, 0x510ECC). LookupQuestStep() resout donc EN
//      DIRECT sur g_ExtraDb.quest. Le pointeur de fonction QuestStepLookup (ci-dessous) est
//      une invention de la reecriture, datant de l'epoque ou la table n'etait pas modelisee ;
//      il n'a JAMAIS eu d'implementeur -> toute l'evaluation d'objectif etait morte (le
//      lookup renvoyait nullptr a vie). Il est conserve UNIQUEMENT comme override de test.
//
// RÈGLE : ce fichier n'édite AUCUN header existant. Il inclut Game/GameState.h (DataTable,
// SelfState non utilisé), Game/GameDatabase.h (ItemInfo/GetItemInfo pour
// Quest_IsRewardItemActive) et Game/ClientRuntime.h (g_Client.Var/VarF pour la longue
// traîne de globals scalaires + InventoryState pour la grille d'inventaire).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <array>
#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/ClientRuntime.h"

namespace ts2::game {

// ===========================================================================
// (A) QUEST_INFO — table de progression scenaristique (005_00007.IMG).
// ===========================================================================
#pragma pack(push, 1)

// QuestInfo — 84 o. Layout deduit de QuestTbl_ValidateRecord 0x4C84C0 :
//   +0  id     (1-based, DOIT valoir index+1 ; 0 = slot vide -> ligne valide mais inerte)
//   +4  name[5][13]  5 candidats de nom (chaine C, terminateur NUL DANS les 13 o -> 12
//                    caracteres utiles max ; validate echoue si aucun NUL trouve)
//   +72 group  1..4   (les fonctions QuestTbl_Find* comparent "group-1 == group0")
//   +76 stage  1..145 (etape au sein du groupe)
//   +80 value  1..999 (parametre libre — jamais interprete par les QuestTbl_* eux-memes)
// (69 -> 72 : 3 o de bourrage naturel pour realigner l'int32 suivant, comme le compilateur
//  d'origine — pas de #pragma pack necessaire ici, mais on le fixe explicitement pour
//  garantir sizeof==84 sur toute cible.)
struct QuestInfo {
    uint32_t id;             // +0
    char     name[5][13];    // +4  (65 o)
    uint8_t  _pad69[3];      // +69 bourrage d'alignement (jamais lu)
    uint32_t group;          // +72
    uint32_t stage;          // +76
    uint32_t value;           // +80
};
static_assert(sizeof(QuestInfo) == 84, "QuestInfo doit faire 84 o (cf. QuestTbl_ValidateRecord 0x4C84C0)");

#pragma pack(pop)

// Table de definitions de quete (999 lignes x 84 o), separee de GameDatabases (non
// editable) — chargee par LoadQuestTable() ci-dessous.
inline DataTable g_QuestTable;

// Acces 0-based brut a une ligne (nullptr hors bornes) — utilitaire interne exposé pour
// les tests.
inline const QuestInfo* QuestRecordAt(const DataTable& table, uint32_t row0) {
    const uint8_t* r = table.record(row0);
    return reinterpret_cast<const QuestInfo*>(r);
}

// QuestTbl_ValidateRecord 0x4C84C0 — valide la ligne 0-based `row0` de `table`.
// Une ligne vide (id==0) est VALIDE (slot libre). Bornes/coherence id, NUL des 5 noms,
// group/stage/value. `row0` doit etre < table.count (comme l'appelant d'origine, qui ne
// boucle que sur [0,count)) ; hors bornes -> false.
bool QuestTbl_ValidateRecord(const DataTable& table, int row0);

// QuestTbl_LoadImg 0x4C8630 — charge G03_GDATA\D01_GIMAGE2D\005\005_00007.IMG dans
// `g_QuestTable` (enveloppe [rawSize][packedSize][zlib] -> [count:u32][999*84 o], SANS
// XOR sur le compteur ni nom de table embarque, a la difference des tables de
// GameDatabase.cpp). Exige count==999 (garde d'integrite en dur). Valide chaque ligne via
// QuestTbl_ValidateRecord ; renvoie false (table quand meme peuplee) des la premiere ligne
// invalide, comme l'original. `gameDataDir` = racine "GameData" (meme convention que
// LoadGameDatabases).
bool LoadQuestTable(const std::string& gameDataDir);

// QuestTbl_GetRecord 0x4C88B0 — acces 1-based ; nullptr si id hors [1,count] ou slot vide.
const QuestInfo* QuestTbl_GetRecord(const DataTable& table, int id1based);

// QuestTbl_FindByGroupAndName 0x4C8900 — recherche par (group0, nom) parmi les 5 noms de
// chaque ligne du groupe (comparaison insensible a la casse, Crt_Stricmp 0x76668B).
// `group0` = group-1 (convention commune a TOUTES les fonctions QuestTbl_Find* ci-dessous :
// le binaire compare toujours `field72 - 1 == a2`). Renvoie un POINTEUR sur la ligne
// (contrairement aux fonctions suivantes qui renvoient un ID — fidele au binaire, qui
// deref/ne-deref pas selon la fonction).
const QuestInfo* QuestTbl_FindByGroupAndName(const DataTable& table, int group0, const char* name);

// QuestTbl_FindFirstByGroup 0x4C89C0 — ne fait rien si !(flagA3==1 && flagA4==0) (garde
// d'origine, sens exact inconnu — parametres bruts a2/a3 du binaire). Sinon, renvoie l'ID
// (1-based, == index+1) de la premiere ligne du groupe avec stage==1, ou 0 si absente.
int QuestTbl_FindFirstByGroup(const DataTable& table, int group0, int flagA3, int flagA4);

// QuestTbl_FindByGroupAndStage 0x4C8A60 — renvoie l'ID (1-based) de la ligne
// (group0, stage) exacte, ou 0 si absente. C'est la fonction dont le resultat alimente
// classiquement g_pCurQuestStepRecord-like caches cote UI (via QuestTbl_GetRecord ensuite).
int QuestTbl_FindByGroupAndStage(const DataTable& table, int group0, int stage);

// QuestTbl_CountByGroup 0x4C8AF0 — nombre de lignes non-vides du groupe.
int QuestTbl_CountByGroup(const DataTable& table, int group0);

// QuestTbl_CountByGroupUpTo 0x4C8B60 — idem, restreint aux `rowLimit` premieres lignes
// (0-based, PAS borne a table.count — fidele au binaire).
int QuestTbl_CountByGroupUpTo(const DataTable& table, int group0, int rowLimit);

// QuestTbl_FindFirstOfGroup 0x4C8BD0 — ID (1-based) de la premiere ligne du groupe, 0 si
// absente.
int QuestTbl_FindFirstOfGroup(const DataTable& table, int group0);

// QuestTbl_FindPrevOfGroup 0x4C8C40 — scanne EN ARRIERE a partir de la ligne 0-based
// (fromId1based - 2) pour trouver la ligne precedente du groupe ; renvoie son ID, ou
// `fromId1based` INCHANGE si aucune trouvee (fidele : le binaire renvoie l'argument tel
// quel en cas d'echec, PAS 0).
int QuestTbl_FindPrevOfGroup(const DataTable& table, int group0, int fromId1based);

// QuestTbl_FindNextOfGroup 0x4C8CC0 — symetrique, scanne EN AVANT a partir de la ligne
// 0-based fromId1based ; renvoie `fromId1based` inchange si aucune trouvee.
int QuestTbl_FindNextOfGroup(const DataTable& table, int group0, int fromId1based);

// ===========================================================================
// (B) Etape de quete courante — vue sur la table NPC type mQUEST (HORS PERIMETRE,
//     cf. bandeau en tete de fichier). Champs = union de ceux consommes par
//     Quest_CheckObjectiveState/GetObjectiveResult/GetRewardItemId/IsRewardItemActive et
//     par Pkt_SmithUpgradeResult (0x48E7D0, alias QuestInteractResult op 0x27).
// ===========================================================================
struct QuestStepRecord {
    uint32_t field64  = 0; // +64  levelReq : porte de NIVEAU de la branche "implicite"
                            //      (Quest_CheckObjectiveState @0x50FF86 : `cmp g_SelfLevel,
                            //      [rec+0x40]` puis `jge` -> 1). Donnees intro : 1,1,1,1,2,4,
                            //      6,8,10,12 = une echelle de niveau.
    uint32_t category = 0; // +72  1..8 : type d'objectif — selecteur du switch a 8 cas de
                            //      Quest_CheckObjectiveState @0x50FFFC, et source de
                            //      g_QuestObjType 0x16745FC (Pkt_SmithUpgradeResult @0x48E929
                            //      lit `[rec+0x48]`). Le commentaire "1..6" etait FAUX : le
                            //      validateur borne 1..8 et les donnees couvrent 1..8
                            //      ({1:227,2:57,3:38,4:69,5:127,6:7,7:88,8:75}).
    uint32_t field92  = 0; // +92  v10[23] : id/valeur "resultat" (variante A)
    uint32_t field96  = 0; // +96  v10[24] : id/valeur "resultat" (variante B, case 4/6.1)
    uint32_t field116 = 0; // +116 v10[29] : id/valeur "resultat" si objectif rempli
    uint32_t targetId = 0; // +120 v10[30] : id cible de l'objectif courant (mob/item/pnj)
    uint32_t required = 0; // +124 v10[31] : quantite/valeur requise
    struct { uint32_t type = 0; uint32_t value = 0; } reward[3]; // +136/+140,+144/+148,
                                                                  // +152/+156 : jusqu'a 3
                                                                  // recompenses (type==6
                                                                  // => value = id d'item,
                                                                  // cf Quest_GetRewardItemId)
};

// LookupQuestStep — resolution DIRECTE sur g_ExtraDb.quest via
// FindQuestDefByElementAndId (= NpcTbl_FindByTypeAndId 0x4C8340), projetee en
// QuestStepRecord. Fidele : le binaire appelle le resolveur en dur, sans indirection.
// `zoneId` est un NOM HISTORIQUE TROMPEUR conserve pour les appelants hors perimetre
// (Game/ComboPickupTick.cpp:179/188, UI/GameHud.cpp:971, UI/QuestTrackerWindow.cpp) : ce
// parametre est en realite l'ELEMENT LOCAL 0-based (g_LocalElement 0x1673194 ==
// game::g_World.self.element), PAS une zone/map. Preuve : Quest_GetRewardItemId 0x510A10
// lit `[this+0xA024]` avec this = g_PlayerCmdController 0x1669170 (@0x5E01C5) et
// 0x1669170+0xA024 = 0x1673194 = g_LocalElement ; UI_EventNoticeWnd_Open 0x6649F0 @0x664A67
// appelle litteralement NpcTbl_FindByTypeAndId(mQUEST, g_LocalElement, g_CurQuestId).
// -> Les appelants qui l'affichent comme un nom de zone (StrTable003/zoneNames) sont dans
//    l'erreur ; signale a l'orchestrateur, hors de ce fichier.
const QuestStepRecord* LookupQuestStep(int zoneId, int npcQuestId);

// Override de test UNIQUEMENT (le binaire n'a pas cette indirection — cf. bandeau (B)).
// nullptr (defaut) => LookupQuestStep resout sur la vraie table.
using QuestStepLookup = const QuestStepRecord* (*)(int zoneId, int npcQuestId);
void SetQuestStepLookup(QuestStepLookup fn);

// Accesseur equivalent a g_pCurQuestStepRecord (dword_18231B4) = record de l'etape de quete
// COURANTE. RESOLU EN DIRECT (corps dans QuestSystem.cpp).
//
// FAIT VERIFIE DANS IDA (2026-07-16) : g_pCurQuestStepRecord 0x18231B4 n'est ECRIT NULLE PART
// dans le binaire — les 20 references a cette adresse sont TOUTES dans Pkt_SmithUpgradeResult
// 0x48E7D0 et TOUTES des LECTURES (aucun store ; xrefs re-verifiees). Mais le binaire est un jeu
// qui TOURNE : ce pointeur ne peut PAS valoir NULL a l'execution quand les case 1/2/3/4 le
// deferencent, sa valeur runtime est forcement le record de l'etape courante. Equivalence prouvee
// g_pCurQuestStepRecord == NpcTbl_FindByTypeAndId(mQUEST, g_LocalElement, g_CurQuestId) : dans le
// case 2, la boucle de recompenses lit g_pCurQuestStepRecord+8*i+136 (@0x48EB0E) et
// Quest_GetRewardItemId 0x510A10 relit Find(element, g_CurQuestId) @0x510A37 pour la MEME
// recompense -> meme record.
// => CurrentQuestStepRecord() resout donc EN DIRECT via LookupQuestStep (comme les 7 autres
//    consommateurs) au lieu de renvoyer un g_curStepRecord qu'AUCUN code n'ecrit. L'ancienne
//    version renvoyait nullptr a vie -> l'etat des case 1/2/3/4 d'ApplyQuestInteractResultState
//    ET la garde de visibilite du QuestTracker (UI/QuestTrackerWindow.cpp:48, fichier voisin)
//    etaient du CODE MORT. Ce n'est PAS fabriquer un ecrivain a g_curStepRecord (aucun store
//    fabrique) : c'est une resolution a la demande, fidele a la lecture d'un pointeur qui, en jeu,
//    vaut ce record. Les gardes `if (npc)` d'ApplyQuestInteractResultState restent (LookupQuestStep
//    peut rendre nullptr si la table n'est pas chargee ou npcQuestId==0) = seul comportement non-UB.
// => SetCurrentQuestStepRecord subsiste comme OVERRIDE DE TEST (prioritaire) ; g_curStepRecord
//    reste nullptr en production.
// NB CABLAGE (hors de ce module) : la resolution depend de g_QuestProgress.npcQuestId (== g_CurQuestId
// 0x16745F4) et g_World.self.element (== g_LocalElement 0x1673194), alimentes par le RESEAU. Tant que
// le front reseau ne les peuple pas, la resolution rend nullptr (gating correct : aucune quete active).
const QuestStepRecord* CurrentQuestStepRecord();
void SetCurrentQuestStepRecord(const QuestStepRecord* record);

// ---------------------------------------------------------------------------------
// Etat de progression du joueur — miroir des champs du gros struct joueur
// g_PlayerCmdController 0x1669170 consommes par Quest_CheckObjectiveState & consorts.
// ATTENTION : les indices Hex-Rays (+10249, +11553, ...) sont SCALES `int*`, pas des
// offsets octets. Offsets octets reels = indice*4, verifies au desassemblage.
//
// DEUX champs de l'ancienne version ont ete SUPPRIMES car ils dupliquaient un etat qui
// existe deja ailleurs (et que PERSONNE n'ecrivait -> evaluation morte) :
//   * `totalKillCount` (+10254 -> octet 0xA038 -> 0x1669170+0xA038 = 0x16731A8 =
//     g_SelfLevel) n'etait PAS un compteur de kills mais le NIVEAU du joueur. La branche
//     "implicite" est une porte de niveau : @0x50FF80 `mov ecx,[edx+0xA038]` puis @0x50FF86
//     `cmp ecx,[eax+0x40]` / `jge` -> 1. Lu desormais depuis g_World.self.level
//     (GameState.h::SelfState, le miroir etabli de g_SelfLevel, ecrit par le reseau :
//     Net/CharStatDeltaDispatch.cpp:239, Game/EntityManager.cpp:523).
//   * `killTrack` (2x64 slots de 6 dwords, "+10320") etait un doublon fantome de la GRILLE
//     D'INVENTAIRE, toujours nul donc jamais satisfait. Octet 10320*4 = 0xA140 ->
//     0x1669170+0xA140 = 0x16732B0 = g_InvMain (nom IDA), et l'indexation `384*i + 6*j` du
//     binaire est exactement `0x600*row + 0x18*col`, celle deja modelisee par
//     ClientRuntime.h::InventoryState. Les fonctions IDA `Inventory_RemoveItem 0x510C40` /
//     `Inventory_ReplaceItem 0x510B40` sont donc BIEN NOMMEES (l'ancien commentaire qui les
//     disait mal nommees etait faux) : elles operent sur g_Client.inv.
// ---------------------------------------------------------------------------------
struct QuestProgressState {
    // +10249 -> octet 0xA024 -> 0x1669170+0xA024 = 0x1673194 = g_LocalElement.
    // NOM TROMPEUR conserve (appelants hors perimetre) : c'est l'ELEMENT local 0-based,
    // PAS une zone. Cf. LookupQuestStep ci-dessus. Doit valoir g_World.self.element.
    int zoneId           = 0;
    int npcQuestId        = 0; // +11553 -> 0xB484 = g_CurQuestId 0x16745F4
    int objectiveMode     = 0; // +11554 -> 0xB488 = g_QuestObjMode 0x16745F8 (0 = porte de niveau, 1 = objectif actif)
    int objectiveType     = 0; // +11555 -> 0xB48C = g_QuestObjType 0x16745FC (1..8)
    int objectiveTarget   = 0; // +11556 -> 0xB490 = g_QuestObjParam1 0x1674600 (id cible / phase du type 6)
    int objectiveProgress = 0; // +11557 -> 0xB494 = g_QuestObjParam2 0x1674604 (compteur de progression)
};

// ===========================================================================
// Evaluation d'objectif — traduction fidele (branches/seuils/ordre inchanges).
// ===========================================================================

// Quest_CheckObjectiveState 0x50FF10 — etat de l'objectif courant :
//   branche "implicite" (mode/type/cible/progression tous a 0) -> 0 ou 1 (bool)
//   branche "active" (mode==1) -> 0 (invalide/non trouve), 2 (en cours), 3 (rempli, cases
//   1-5/8), 4 (case 6.2 non rempli) ou 5 (case 6.2 rempli)
int Quest_CheckObjectiveState(const QuestProgressState& s);

// Quest_IsObjectiveComplete 0x5103F0 — mappe Quest_CheckObjectiveState sur un bool selon
// objectiveType (==3 pour 1/2/3/4/5/8, ==5 pour 6, ==2 pour 7 ; sinon false).
bool Quest_IsObjectiveComplete(const QuestProgressState& s);

// Quest_GetObjectiveResult 0x510520 — memes branches que CheckObjectiveState mais renvoie
// un champ de QuestStepRecord (field92/96/116) au lieu d'un code d'etat.
int Quest_GetObjectiveResult(const QuestProgressState& s);

// Quest_GetRewardItemId 0x510A10 — id d'item de la premiere recompense de type==6 (0 si
// aucune ou record introuvable).
int Quest_GetRewardItemId(const QuestProgressState& s);

// Quest_IsRewardItemActive 0x510A90 — vrai si la recompense de type==6 existe ET que
// ITEM_INFO(itemId).typeCode == 2 (mirroir MobDb_GetEntry 0x4C3C00 + comparaison +188==2).
bool Quest_IsRewardItemActive(const QuestProgressState& s);

// Quest_IsItemAllowed 0x54F0B0 — item `itemId` present dans la liste blanche (14 slots)
// du conteneur `containerIndex` (globals dword_1675808[14*a1+i] / dword_1687494[0] —
// longue traine non modelisee, adressee via g_Client.Var()). containerIndex==1 exige en
// plus g_Client.VarGet(0x1687494)==1 (garde d'origine).
bool Quest_IsItemAllowed(int containerIndex, int itemId);

// ===========================================================================
// Callees directes de Pkt_SmithUpgradeResult (op 0x27) operant sur QuestProgressState.
// ===========================================================================

// Inventory_RemoveItem 0x510C40 — cherche `itemId` dans les lignes 0..1 de la GRILLE
// D'INVENTAIRE (g_InvMain 0x16732B0, `384*i + 6*j + 10320` == `0x600*row + 0x18*col`), remet
// les 6 dwords de la cellule a zero (EA 0x510CBF..0x510D63) et renvoie l'indice de ligne
// (0/1), ou -1 si absent (@0x510D7D). Le nom IDA `Inventory_*` est CORRECT : c'est bien
// l'inventaire (row 0 = sac principal, row 1 = page bonus).
int Quest_RemoveTrackedItem(InventoryState& inv, uint32_t itemId);

// Inventory_ReplaceItem 0x510B40 — cherche `oldItemId` dans les lignes 0..1 de la grille,
// remplace l'id (+0 @0x510BC2) puis remet a zero flag/color/durability (+12/+16/+20 =
// dwords 10323/10324/10325, EA 0x510BDE/0x510BFF/0x510C20). gridX/gridY (+4/+8 = 10321/
// 10322) sont INCHANGES — fidele : l'original n'ecrit que +0/+12/+16/+20. Renvoie l'indice
// de ligne, ou -1 si absent.
int Quest_ReplaceTrackedItem(InventoryState& inv, uint32_t oldItemId, uint32_t newItemId);

// Util_SumExceeds2Billion 0x53F660 — (a+b) > 2 000 000 000 en arithmetique 64 bits.
inline bool Quest_SumExceeds2Billion(int64_t a, int64_t b) { return (a + b) > 2000000000LL; }

// ===========================================================================
// Paquet 0x27 QuestInteractResult (alias Pkt_SmithUpgradeResult 0x48E7D0). Struct propre
// (pas de dependance sur Net/RecvPackets.h) : [resultCode][invRow][invSlot][gridX][gridY].
// ===========================================================================
struct QuestInteractResultPacket {
    uint32_t resultCode = 0; // 1..9
    int32_t  invRow      = -1; // -1 = pas d'ecriture inventaire (comparaison signee fidele)
    uint32_t invSlot     = 0;
    uint32_t gridX       = 0;
    uint32_t gridY       = 0;
};

// ApplyQuestInteractResultState — PARTIE ETAT de Pkt_SmithUpgradeResult 0x48E7D0
// (resultCode 1..9). La partie MESSAGE (StrTable005_Get + Msg_AppendSystemLine des codes
// 109/432..439) est DEJA geree par Net/GameHandlers_Misc.cpp (0x27) ; cette fonction NE LA
// DUPLIQUE PAS, sauf pour les messages 427..431 de la boucle de recompenses du case 2 (str
// 427=type6, 428=type2/poids, 429=type3/monnaie, 430=type4, 431=type5), qui ne sont PAS
// couverts par le TODO de GameHandlers_Misc.cpp (celui-ci ne traite que le message de
// premier niveau selon resultCode, pas la boucle interne des 3 slots de recompense).
// Utilise CurrentQuestStepRecord() pour TOUS les acces au record d'etape ; cet accesseur
// resout desormais EN DIRECT via LookupQuestStep(g_World.self.element, g_QuestProgress.npcQuestId),
// exactement la cle que le binaire relit via NpcTbl_FindByTypeAndId(mQUEST, element, g_CurQuestId)
// dans Quest_GetRewardItemId/IsRewardItemActive (@0x510A37) — d'ou l'equivalence avec le global
// cache g_pCurQuestStepRecord 0x18231B4 (cf. le bloc de doc de CurrentQuestStepRecord ci-dessus).
// TODO PRECIS (hors perimetre — audio/UI, pas de logique d'etat) :
//   Snd3D_PlayScaledVolume 0x4DA380, UI_EventNoticeWnd_Open 0x6649F0 (cases 1/2/5),
//   cGameHud_ResetUiState 0x62AFB0 (cases 2/3/4).
void ApplyQuestInteractResultState(const QuestInteractResultPacket& p,
                                    QuestProgressState& progress,
                                    InventoryState& inv);

// Instance globale unique (miroir propre de l'état de progression de quête, à
// l'image de g_Guild/g_Warehouse). Utilisée par GameHandlers_Core (écrit via
// ApplyQuestInteractResultState) et par QuestTrackerWindow (lu pour l'affichage).
inline QuestProgressState g_QuestProgress;

} // namespace ts2::game

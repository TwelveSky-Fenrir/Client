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
//  (B) Enregistrements NPC de type mQUEST — table NPC generale (8444 o/ligne,
//      NpcTbl_FindByTypeAndId 0x4C8340), HORS PERIMETRE de cette mission (aucun loader
//      assigne, aucun EA de chargeur fourni). Quest_CheckObjectiveState/IsObjectiveComplete/
//      GetObjectiveResult/GetRewardItemId/IsRewardItemActive ET g_pCurQuestStepRecord
//      (dword_18231B4) operent TOUS sur CE type d'enregistrement (offsets +64/+72/+92/
//      +96/+116/+120/+124/+136..+156 confirmes par decompilation croisee des 5 fonctions
//      + de Pkt_SmithUpgradeResult). On le modelise ici via QuestStepRecord — une VUE
//      PROPRE limitee aux seuls champs consommes (pas un overlay memoire des 8444 o) —
//      plus une interface d'injection (QuestStepLookup) a cabler sur le futur chargeur
//      NPC. TODO PRECIS : brancher QuestStepLookup sur un vrai chargeur de la table NPC
//      (NpcTbl_FindByTypeAndId 0x4C8340) quand ce sous-systeme existera.
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
    uint32_t field64  = 0; // +64  seuil (branche "objectif implicite" sans etape active)
    uint32_t category = 0; // +72  1..6 : type d'interaction (dispatch dword_18231B4+72
                            //      dans Pkt_SmithUpgradeResult case 1/4)
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

// Resolveur injectable (table NPC mQUEST reelle non modelisee ici — TODO PRECIS ci-dessus).
// Signature fidele a NpcTbl_FindByTypeAndId(mQUEST, zoneId, npcQuestId) 0x4C8340.
using QuestStepLookup = const QuestStepRecord* (*)(int zoneId, int npcQuestId);
void SetQuestStepLookup(QuestStepLookup fn);
const QuestStepRecord* LookupQuestStep(int zoneId, int npcQuestId);

// Accesseur equivalent a g_pCurQuestStepRecord (dword_18231B4) : le record d'etape mis en
// cache par le (futur) code d'ouverture de dialogue de quete. nullptr par defaut.
const QuestStepRecord* CurrentQuestStepRecord();
void SetCurrentQuestStepRecord(const QuestStepRecord* record);

// ---------------------------------------------------------------------------------
// Etat de progression du joueur (mirroir des offsets +10249/+10254/+11553..+11557/+10320
// du gros struct joueur g_PlayerCmdController 0x1669170, seuls champs consommes par
// Quest_CheckObjectiveState & consorts). QuestKillTrackSlot = une des 2x64 entrees
// (6 dwords) de la grille de suivi ; seul le premier dword (id/valeur suivie) est
// interprete par l'algorithme d'origine, les 5 autres sont des metadonnees opaques
// (compteur/aux, jamais lues par les fonctions ciblees — mises a zero par
// Quest_RemoveTrackedItem/ReplaceTrackedItem).
// ---------------------------------------------------------------------------------
struct QuestKillTrackSlot {
    uint32_t id = 0;                          // +0 (seul champ compare par l'algorithme)
    uint32_t aux1 = 0, aux2 = 0, aux3 = 0, aux4 = 0, aux5 = 0; // +4..+20 (opaques)
};
struct QuestProgressState {
    int zoneId           = 0; // +10249 (*4 = offset dword) : zone/map courante
    int totalKillCount    = 0; // +10254 : compteur global utilise par la branche "implicite"
    int npcQuestId        = 0; // +11553 : id de l'enregistrement NPC (mQUEST) courant
    int objectiveMode     = 0; // +11554 : 0 = aucun objectif custom actif, 1 = actif
    int objectiveType     = 0; // +11555 : 1..8 (type d'objectif)
    int objectiveTarget   = 0; // +11556 : parametre cible (sens depend du type)
    int objectiveProgress = 0; // +11557 : compteur de progression courant
    std::array<std::array<QuestKillTrackSlot, 64>, 2> killTrack{}; // +10320.. (2 blocs x 64)
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

// Inventory_RemoveItem 0x510C40 — cherche `itemId` dans killTrack (2x64), remet le slot a
// zero et renvoie l'indice de bloc (0/1), ou -1 si absent. NOTE : malgre son nom
// d'origine, opere sur QuestProgressState::killTrack (table de suivi de quete), PAS sur la
// grille d'inventaire principale (g_Client.inv).
int Quest_RemoveTrackedItem(QuestProgressState& s, uint32_t itemId);

// Inventory_ReplaceItem 0x510B40 — cherche `oldItemId` dans killTrack, remplace l'id par
// `newItemId` puis remet aux3/aux4/aux5 (+12/+16/+20) a zero ; aux1/aux2 (+4/+8) sont
// INCHANGES — fidele : l'original n'ecrit que +0/+12/+16/+20. Renvoie l'indice de bloc,
// ou -1 si absent.
int Quest_ReplaceTrackedItem(QuestProgressState& s, uint32_t oldItemId, uint32_t newItemId);

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
// Utilise CurrentQuestStepRecord() pour TOUS les acces au record d'etape (le binaire
// d'origine relit deux fois via NpcTbl_FindByTypeAndId(mQUEST, s.zoneId, s.npcQuestId)
// dans Quest_GetRewardItemId/IsRewardItemActive — resolution equivalente en pratique
// puisque g_pCurQuestStepRecord est cache depuis la meme cle (zoneId, npcQuestId)).
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

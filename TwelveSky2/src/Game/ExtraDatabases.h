// Game/ExtraDatabases.h — chargeur de 2 tables .IMG supplementaires (005_00005/00006),
// distinctes des 5 tables de Game/GameDatabase.h (LEVEL/ITEM/SKILL/MONSTER/SOCKET).
//
// ATTENTION NOMS IDA TROMPEURS (verifie via decompilation + ValidateRecord voisins,
// meme methode que MobDb/ItemDefTbl documentee ailleurs dans ce projet) :
//   005_00005.IMG -> chargeur IDA "SkillDefTbl_LoadImg" 0x4C6BD0 / validateur "SkillDefTbl_ValidateRecord"
//                    0x4C65F0. CE N'EST PAS une table de competences : rec[0] = "Blacksmith Wu" + un
//                    gros bloc de texte/dialogue, et l'erreur associee est [Error::mNPC.Init()].
//                    -> C'est la vraie table de definition des PNJ, chargee dans le manager "mNPC".
//                    Renomme ici NpcDefRecord / NpcDefTbl.
//   005_00006.IMG -> chargeur IDA "NpcTbl_LoadImg" 0x4C8090 / validateur "NpcTbl_ValidateRecord" 0x4C78C0.
//                    CE N'EST PAS une table de PNJ : rec[0] = "[Intro] Banker Bai & Beggar Xiao" +
//                    10 blocs de dialogue, erreur associee [Error::mQUEST.Init()].
//                    -> C'est la vraie table de definition des QUETES, chargee dans le manager "mQUEST".
//                    Renomme ici QuestDefRecord / QuestDefTbl.
//
// Layouts deduits des validateurs (mêmes bornes en dur que les gardes d'integrite des 5 tables
// de GameDatabase.h) : les offsets ou une borne coincide avec le compte d'une autre table connue
// (ITEM_INFO=99999, SKILL_INFO=300, LEVEL_INFO=145) sont de FORTES hypotheses de role (correlation
// de bornes), pas des certitudes prouvees par un accesseur observe — signale en commentaire.
//
// Enveloppe fichier : [rawSize u32][packedSize u32][zlib] -> payload (cf. Asset_DecompressImg 0x53F5E0,
// meme decodeur que les 5 tables). Contrairement aux 5 tables de GameDatabase.h, il N'Y A PAS de nom de
// table embarque dans le payload : les enregistrements commencent directement a l'offset 4 (juste apres
// le compteur), header=4 pour les 2 tables ici.
#pragma once
#include "Game/GameState.h" // pour ts2::game::DataTable
#include <cstdint>
#include <string>

namespace ts2::game {

// ---------------------------------------------------------------------------
// Enregistrements types (layout byte-exact des .IMG, deduits des boucles de
// validation NpcDefTbl_ValidateRecord 0x4C65F0 / QuestDefTbl_ValidateRecord 0x4C78C0).
// ---------------------------------------------------------------------------
#pragma pack(push, 1)

// NpcDefRecord — 11736 o. Table "mNPC" (005_00005.IMG). Slot vide si id == 0 (garde
// en tete de NpcDefTbl_ValidateRecord : `if (!*(DWORD*)rec) return 1;`).
// Cross-check VeryOldClient : classe NPC (VeryOldClient/GameSystem/CNPC.cpp, singleton mNPC).
// Detail par offset : Docs/TS2_TABLES_ROSETTA.md §6. NB : VeryOld nHeadImg[6] est un champ
// RUNTIME (overlay mNPC_nHeadImg dans CNPC::Init), ABSENT du record fichier 11736 o.
struct NpcDefRecord {
    uint32_t id;                 // +0     1..500, DOIT valoir index+1 (0 = slot vide) ; ex-VeryOldClient: nIndex (CONFIRMED)
    char     name[25];           // +4     nom du PNJ (ex. rec[0] = "Blacksmith Wu"), null dans [0..24] ; ex-VeryOldClient: nName (CONFIRMED)
    uint8_t  _pad29[3];          // +29    reserve (alignement)
    uint32_t fieldA;              // +32    (1..5) role inconnu — precede la grille de textes, peut-etre
                                   //        le nombre de sous-menus/dialogues actifs sur les 5 disponibles.
                                   //        ex-VeryOldClient: nSpeechNum (PLAUSIBLE, resout la supposition)
    char     textGrid[5][5][51]; // +36    grille 5x5 de chaines (<=51 o, null-terminees) — texte de
                                   //        dialogue/menu du PNJ (5 "pages" x 5 lignes, hypothese).
                                   //        ex-VeryOldClient: nSpeech[5][5][51] (CONFIRMED, structure 5x5x51 identique)
    uint8_t  _pad1311[1];        // +1311  reserve (alignement)
    uint32_t fieldB;              // +1312  (1..5)  role inconnu ; ex-VeryOldClient: nTribe (PLAUSIBLE, tribus 1..4 + neutre ?)
    uint32_t fieldC;              // +1316  (1..17) role inconnu (17 ~ nb. de zones/maps ?) ; ex-VeryOldClient: nType (PLAUSIBLE)
    uint32_t fieldD;              // +1320  (1..10000) role inconnu (coordonnee monde ? cf. ITEM_INFO champs 192/196/200 bornes similaires)
                                   //        ex-VeryOldClient: nDataSortNumber2D (PLAUSIBLE, corrige la supposition : index/tri image 2D portrait)
    // RESOLU (Docs/TS2_NPC_MESH_DRAW.md §2-3, decompilation Npc_DrawMesh 0x57FF00) :
    // kindIndex+1 du modele visuel PNJ. `Npc_DrawMesh` lit `*(DWORD*)(*this+1324) - 1` sur
    // l'enregistrement runtime pointe par g_NpcRenderArray[i].ptr (resolu via
    // SkillDefTbl_GetRecord(mNPC, kindId) == GetNpcDefRecord() ici) et l'utilise pour indexer
    // g_NpcMeshCatalog (66 entrees, stem "N%03d%03d001.SOBJECT") apres verif Model_GetNpcMeshSlot
    // (borne dure 0x41=65, donc fieldE doit valoir [1,66]). Cote ClientSource : voir
    // Gfx/ModelCache.h::GetForNpc, qui calcule kindIndex = fieldE - 1.
    uint32_t fieldE;              // +1324  kindIndex+1 du modele PNJ (N*.SOBJECT), [1,66] ; ex-VeryOldClient: nDataSortNumber3D (CONFIRMED, role modele 3D prouve)
    // fieldF[1] (+1332) RESOLU (meme doc, meme fonction) : portee d'interaction/clic du PNJ,
    // comparee via Target_IsBeyondClickRange((float*)this+5, fieldF[1]) -- hauteur/rayon de la
    // garde anti-clipping camera, meme role que ItemInfo.drawSize pour Char_Draw. fieldF[0]/[2]
    // restent de role inconnu.
    uint32_t fieldF[3];           // +1328  3x (1..1000) [1]=portee d'interaction (RESOLU), [0]/[2] inconnus ; ex-VeryOldClient: nSize[3] (PLAUSIBLE)
    uint32_t fieldG[100];         // +1340  100x (1..2) — probablement des drapeaux booleens (etat/dispo) ; ex-VeryOldClient: nMenu[100] (PLAUSIBLE, 100 entrees de menu actif/inactif)
    // <100000 par valeur : correlation forte avec ITEM_INFO (garde d'integrite = 99999 objets,
    // cf. GameDatabase.h). Hypothese : identifiants d'objets vendus par ce PNJ marchand (3 categories x 28 slots).
    // ex-VeryOldClient: nShopInfo[3][28] (PLAUSIBLE, structure 3x28 identique + role boutique).
    uint32_t shopItemIds[3][28];  // +1740
    // <=300 par valeur : correlation forte avec SKILL_INFO (garde d'integrite = 300 competences,
    // cf. GameDatabase.h). Hypothese : identifiants de competences enseignees par ce PNJ (3x8).
    // ex-VeryOldClient: nSkillInfo1[3][8] (PLAUSIBLE, structure 3x8 identique).
    uint32_t teachSkillIds[3][8]; // +2076
    // <=300 par valeur : meme borne que SKILL_INFO. Hypothese : matrice de pre-requis/couts de
    // competences (3 groupes x 3 x 3 x 8 slots) — structure imbriquee non elucidee en detail.
    // ex-VeryOldClient: nSkillInfo2[3][3][3][8] (PLAUSIBLE, structure 3x3x3x8 identique).
    uint32_t skillMatrix[3][3][3][8]; // +2172
    // <=100000000 (1e8) par valeur, indexe par 145 (== garde LEVEL_INFO) x 15. Hypothese : table de
    // couts (or ?) par niveau de joueur x 15 emplacements (ex. cout d'entrainement d'une competence
    // scalant avec le niveau). ex-VeryOldClient: nGambleCostInfo[145][15] (PLAUSIBLE, cout de pari par niveau).
    uint32_t levelCostTable[145][15]; // +3036
};
static_assert(sizeof(NpcDefRecord) == 11736, "NpcDefRecord doit faire 11736 o");

// QuestDefRecord — 8444 o. Table "mQUEST" (005_00006.IMG). Slot vide si name == ""
// (garde en tete de QuestDefTbl_ValidateRecord : `if (!Crt_Strcmp(rec->name, "")) return 1;` —
// CONTRAIREMENT aux autres tables, ce n'est PAS id==0 qui marque un slot vide ici).
// Cross-check VeryOldClient : classe QUEST (VeryOldClient/GameSystem/CQUEST.cpp, singleton mQUEST).
// Cross-check semantique FORT (Docs/TS2_TABLES_ROSETTA.md §7) : le champ CHAINE (name) est le
// marqueur de slot vide dans les DEUX builds (VeryOld qSubject = "cle de vacuite").
struct QuestDefRecord {
    uint32_t id;                 // +0    1..1000, DOIT valoir index+1 ; ex-VeryOldClient: qIndex (CONFIRMED)
    char     name[51];           // +4    titre de la quete (ex. rec[0] = "[Intro] Banker Bai & Beggar Xiao") ; ex-VeryOldClient: qSubject[51] (CONFIRMED, cle de vacuite)
    uint8_t  _pad55[1];          // +55   reserve (alignement)
    uint32_t fieldA;              // +56   (1..4)   role inconnu — peut-etre le type de quete
    uint32_t fieldB;              // +60   (1..1000) role inconnu
    uint32_t levelReq;            // +64   (1..145) — correlation forte avec LEVEL_INFO : niveau requis ; ex-VeryOldClient: qLevel (PLAUSIBLE, corr. de borne ; aucun accesseur observe)
    uint32_t fieldD;              // +68   (1..2)   role inconnu — drapeau (repetable ?)
    uint32_t fieldE;              // +72   (1..8)   role inconnu — categorie/chaine de quetes ?
    uint32_t fieldF;              // +76   (0..200) role inconnu
    uint8_t  _unk80[12];          // +80   12 o NON couverts par le validateur (aucune garde observee)
    uint32_t fieldG;              // +92   (1..500) role inconnu
    uint32_t fieldH[5];           // +96   5x (0..500) role inconnu
    uint32_t fieldI;              // +116  (1..500) role inconnu
    uint8_t  _unk120[16];         // +120  16 o NON couverts par le validateur
    // 3 paires (categorie 1..6, valeur <=1e8) : hypothese = table de recompenses (item/or/exp x montant).
    // ex-VeryOldClient: qReward[3][2] (type 1..6 / valeur) (PLAUSIBLE, meme forme).
    struct { uint32_t category; uint32_t value; } rewards[3]; // +136
    uint32_t fieldK;              // +160  (0..1000) role inconnu
    // 10 blocs de dialogue (correspond au commentaire desassemblage "10 blocs de dialogue") :
    // chaque bloc = 15 lignes de texte (<=51 o, null-terminees, VALIDEES) + 63 o de fin de bloc
    // NON valides par NpcTbl_ValidateRecord (metadonnees par bloc — orateur ? drapeaux ? — inconnues).
    struct {
        char    lines[15][51]; // texte de dialogue (15 lignes max par bloc)
        uint8_t _tail[63];     // NON valide par le desassemblage — role inconnu
    } dialogue[10];               // +164
};
static_assert(sizeof(QuestDefRecord) == 8444, "QuestDefRecord doit faire 8444 o");

#pragma pack(pop)

// ---------------------------------------------------------------------------
// API.
// ---------------------------------------------------------------------------

// Etat des 2 tables supplementaires (separe de game::GameDatabases pour ne pas modifier
// GameState.h — cf. regle du module autonome).
struct ExtraDatabases {
    DataTable npc;   // NpcDefRecord   (11736 o) — manager "mNPC",   005_00005.IMG
    DataTable quest; // QuestDefRecord (8444 o)  — manager "mQUEST", 005_00006.IMG
};

// Instance globale unique (miroir de g_World.db mais hors de GameState.h).
inline ExtraDatabases g_ExtraDb;

// Charge les 2 tables .IMG dans g_ExtraDb. `gameDataDir` = racine "GameData" (les fichiers
// sont sous <gameDataDir>\G03_GDATA\D01_GIMAGE2D\005\). Renvoie true si LES DEUX tables sont
// chargees et validees (garde de compteur OK pour chaque enregistrement, comme l'original qui
// echoue au premier ValidateRecord invalide).
bool LoadExtraDatabases(const std::string& gameDataDir);

// Accesseur type NpcDefRecord. `npcId` 1-based (1..500) ; nullptr hors bornes OU slot vide (id==0).
const NpcDefRecord* GetNpcDefRecord(uint32_t npcId);

// Accesseur type QuestDefRecord. `questId` 1-based (1..1000) ; nullptr hors bornes OU slot vide
// (name vide — cf. semantique differente de l'empty-check pour cette table).
const QuestDefRecord* GetQuestDefRecord(uint32_t questId);

} // namespace ts2::game

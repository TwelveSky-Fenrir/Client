// Game/MotionPools.h — pools ASSET/MOTION initialisés par App_Init : mGDATA,
// mZONEMAININFO, mZONENPCINFO, mZONEMOVEINFO (cf. CLAUDE.md, App_Init 0x461C20,
// séquence "[Error::mXXX.Init()]").
//
// Correspondance EXACTE relevée dans App_Init (0x461C20, désassemblage) :
//   if ( AssetMgr_InitAllSlots(&g_ModelMotionArray) )        // "[Error::mGDATA.Init()]"        0x4DEB50
//     if ( Motion_InitFrameTable(&g_MotionFrameRangeTable) ) // "[Error::mZONEMAININFO.Init()]"  0x4F1380
//       if ( Motion_LoadGInfo002Bin(&dword_14AA930) )        // "[Error::mZONENPCINFO.Init()]"   0x4FCFD0
//         if ( Motion_LoadGInfo003Bin(&flt_1555D08) )        // "[Error::mZONEMOVEINFO.Init()]"  0x4FD420
//
// ATTENTION nommage trompeur : malgré le nom "GInfo002/003", le manager déclenché par
// l'échec de Motion_LoadGInfo003Bin est bien mZONEMOVEINFO (PAS mZONENPCINFO — celui-ci
// correspond à Motion_LoadGInfo002Bin). Vérifié en relisant les 4 blocs
// MessageBoxA("[Error::mXXX.Init()]") consécutifs dans App_Init.
//
// -----------------------------------------------------------------------------------
// 1) AssetMgr_InitAllSlots 0x4DEB50 (mGDATA) — PAS un chargeur de fichier : c'est
//    l'initialisation d'un ENORME tableau en mémoire (g_ModelMotionArray, offsets
//    observés jusqu'à ~0xBC07B4 ≈ 12.3 Mo) qui préconstruit, pour CHAQUE emplacement
//    (sprite 2D, modèle, motion, sobject statique/eau, piste sonore 3D) de CHAQUE
//    catégorie/sous-catégorie, le chemin de fichier associé — via 6 fonctions
//    "BuildPath" séparées, chacune adossée à ses propres tables de noms de dossiers
//    en .rdata (hors périmètre de cette mission — ces fonctions appartiennent aux
//    sous-systèmes render/asset "Sprite2D/ModelObj/Motion/SObject/Snd3D") :
//      Sprite2D_BuildPath      0x4D68E0  (stride slot 148 o, catégories 1..7)
//      ModelObj_BuildPath      0x4D6E20  (stride slot 148 o, catégories 1..5)
//      Motion_BuildPathAndLoad 0x4D7390  (stride slot 156 o, catégories 1..6)
//      SObject_BuildPath       0x4D89C0  (stride slot 144 o, catégories 1..21 + 11..14)
//      SObject_BuildPathW      0x4D96A0  (stride slot 144 o, catégorie 6, variante "wide")
//      Snd3D_SetISNPath        0x4DA0C0  (stride slot 192 o, catégories 1..6)
//    Puis : WSndMgr_Reset 0x4DAFC0 et 5x cThread_Start 0x78FBF0 (démarre 5 threads de
//    chargement asynchrone qui consomment ces chemins plus tard). La fonction retourne
//    toujours 1 (aucun échec possible dans le désassemblage).
//    Ce module n'implémente PAS les 6 BuildPath (hors périmètre : formats de noms de
//    fichiers d'un autre sous-système) ; il documente et EXPOSE la géométrie exacte du
//    pool (comptes par catégorie/strides, extraits des bornes de boucles du désassemblage)
//    pour que le sous-système propriétaire puisse la câbler fidèlement plus tard.
//    InitModelMotionPool() ci-dessous reproduit fidèlement la SEULE partie sans
//    dépendance externe : le déclenchement de WSndMgr_Reset/cThread_Start est HORS
//    PÉRIMÈTRE réseau/thread de ce module (TODO, cf. .cpp) ; la fonction renvoie donc
//    simplement `true` comme l'original (pas d'échec possible).
//
// 2) Motion_InitFrameTable 0x4F1380 (mZONEMAININFO) — PUREMENT DE LA DONNÉE : un
//    `switch(i)` à 350 cas (338 explicites + 12 par défaut) qui remplit un tableau de
//    350 lignes {start,end,type,flag} SANS AUCUNE lecture de fichier. Layout mémoire
//    d'origine (g_MotionFrameRangeTable, DWORD* nommé `this`) : deux blocs parallèles
//    de 350 entrées entrelacées 2 à 2 —
//      this[2*i]      = start   (frame de début, 1-based, 0 si case par défaut)
//      this[2*i+1]    = end     (frame de fin)
//      this[700+2*i]  = type    (catégorie d'animation, -1 si case par défaut)
//      this[701+2*i]  = flag    (0/1)
//    soit this[0..699] et this[700..1399] (1400 DWORD = 5600 o). Extrait ICI comme un
//    tableau struct-of-350 (MotionFrameRange[350]) — valeurs identiques, layout mémoire
//    ré-agencé pour la lisibilité (aucun autre module du client réécrit ne référence le
//    buffer brut par pointeur, donc le ré-agencement est sans perte de fidélité
//    fonctionnelle). Toutes les valeurs ont été extraites automatiquement du pseudocode
//    Hex-Rays de Motion_InitFrameTable (350/350 lignes couvertes, y compris les 12 cas
//    par défaut {0,0,-1,0} du "default: continue;").
//
// 3) Motion_LoadGInfo002Bin 0x4FCFD0 (mZONENPCINFO) — VRAI chargeur de fichier :
//    lit "G02_GINFO\002.BIN" (sous la racine GameData) intégralement en RAW (aucune
//    compression/enveloppe, CreateFileA+ReadFile bruts) dans dword_14AA930, exactement
//    701400 o = 350 lignes x 501 DWORD. Échec si la taille lue diffère de 701400.
//
//    ⚠️ CORRECTIF 2026-07-14 (Docs/TS2_NPC_ZONE_LOADER_TRIGGER.md) : la ligne 1..350
//    N'EST PAS une "table de points d'ancrage d'arme/effet par frame de motion" comme
//    documenté précédemment ici (erreur d'interprétation) — c'est la table de PLACEMENT
//    DES PNJ STATIQUES PAR ZONE (350 zones, i = zoneId-1), consommée directement par
//    `cGameData_LoadZoneNpcInfo` 0x5578E0 pour peupler `g_NpcRenderArray` au (re)chargement
//    de la zone courante (cf. Docs/TS2_NPC_RENDER_ARRAY_WRITER.md). Layout de ligne
//    i (0-based, zoneId = i+1) :
//      row[0]        = n, nombre de PNJ statiques placés sur cette zone (0..100)
//      row[1..100]   = kindId (u32, 1-based, index dans la table `mNPC`) des n PNJ
//      row[101..400] = n triplets flottants position (x,y,z) à l'offset 101+3*j
//                       (j = index PNJ 0-based, apparié à row[1+j])
//      row[401..500] = n flottants angle d'affichage, à l'offset 401+j (apparié à row[1+j])
//    Les noms de fonctions IDA "GInfo_FindMotionByFrameId"/"GInfo_CalcLeftMargin"/
//    "GInfo_CalcRightMargin"/"Motion_GetAABB" sont TROMPEURS (héritage d'un pass de
//    nommage antérieur qui a supposé, à tort, une sémantique "motion/animation") :
//    décompilation confirmée, leurs 3 SEULS appelants (xrefs_to = 1 site chacun) sont
//    dans `Quest_DrawTracker` 0x510FC0 (HUD de suivi de quête), qui les utilise pour
//    retrouver DANS QUELLE ZONE se trouve un PNJ de quête donné (recherche linéaire de
//    `kindId` dans row[1..n] sur les 350 zones) puis calculer une marge d'écran par
//    rapport aux bornes de LA ZONE (pas d'une motion — `Motion_GetAABB` 0x4F6F60 est en
//    réalité une table statique de 350 rectangles de bornes monde par zoneId, valeurs
//    de l'ordre de ±10000 unités, incompatibles avec une bounding-box d'animation de
//    personnage). Non renommées dans l'IDB par prudence (risque de collision avec un
//    futur pass "Quest_"), mais leur rôle réel est: "trouve la zone contenant un PNJ de
//    kindId donné, calcule la position affichée relative aux bornes de cette zone".
//
//    Fonctions consommatrices d'origine (toutes 0x4FDxxx, contiguës à
//    Motion_LoadGInfo002Bin) :
//      GInfo_FindMotionByFrameId 0x4FD070 (this, kindId) -> zoneId 1-based ou 0
//        (parcourt row[1..n] à la recherche de kindId) — REIMPLÉMENTÉ ici (autonome,
//        nom conservé pour compat avec le code existant malgré la sémantique corrigée).
//      GInfo_CalcLeftMargin  0x4FD1D0 (this, kindId) -> round(pos.x - ZoneBounds(zone).minX)
//      GInfo_CalcRightMargin 0x4FD2F0 (this, kindId) -> round(ZoneBounds(zone).maxX - pos.z)
//        Ces deux dernières dépendent de Motion_GetAABB 0x4F6F60 (13.9 Ko, table de 350
//        rectangles de bornes de zone en dur — HORS PÉRIMÈTRE, sous-système quest/UI).
//        Ce module expose GetRawAttachPoint() qui renvoie le triplet brut (x,y,z) NON
//        corrigé par les bornes de zone — la couche appelante qui possède la table de
//        bornes peut faire la soustraction elle-même (cf. TODO dans MotionPools.cpp).
//    Pour le PLACEMENT DES PNJ DE ZONE (le cas d'usage principal, cGameData_LoadZoneNpcInfo),
//    utiliser plutôt les accesseurs directs ZoneNpcCount()/ZoneNpcKindId()/ZoneNpcPosition()/
//    ZoneNpcAngle() ci-dessous (indexation directe par zoneId, pas de recherche) — cf.
//    Game/StaticNpcLoader.h pour le chargeur de haut niveau équivalent à
//    cGameData_LoadZoneNpcInfo.
//
// 4) Motion_LoadGInfo003Bin 0x4FD420 (mZONEMOVEINFO) — VRAI chargeur de fichier :
//    lit "G02_GINFO\003.BIN" intégralement en RAW dans flt_1555D08, exactement
//    1127000 o = 350 lignes x 805 FLOAT (3220 o/ligne). Échec si la taille lue diffère.
//    Layout de ligne, motion = i+1 (1-based), déduit de GInfo2_GetVec3 0x4FD4C0 :
//      row[0..2] = position (x,y,z) de la motion (SEULS champs consommés dans le
//                  désassemblage relevé — les 802 flottants restants par ligne ne sont
//                  référencés par aucun appelant connu, rôle non élucidé).
//    C'est CETTE table que Game/MapWarp.h utilise via IFactionTownCoordResolver comme
//    fallback GInfo2_GetVec3(npcId) -> vec3 (cf. commentaire en tête de MapWarp.h).
//    GetVec3() ci-dessous est la réimplémentation fidèle de GInfo2_GetVec3.
//
// Formats fichiers découverts : G02_GINFO\002.BIN et G02_GINFO\003.BIN sont tous deux
// des DUMPS BRUTS DE TABLEAUX C (aucun en-tête, aucune compression, taille fixe connue
// à l'avance) — à ne pas confondre avec l'enveloppe [rawSize][packedSize][zlib] des
// .IMG (cf. Asset/ImgFile.h) : ici c'est un ReadFile() direct sur le buffer final.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::game {

// ---------------------------------------------------------------------------
// 1) mGDATA — géométrie du pool g_ModelMotionArray (AssetMgr_InitAllSlots 0x4DEB50).
//    Purement documentaire (constantes extraites des bornes de boucles du
//    désassemblage) ; le pool réel (chemins de fichiers par slot) est construit par
//    les 6 fonctions BuildPath listées ci-dessus, hors périmètre de ce module.
// ---------------------------------------------------------------------------
namespace ModelMotionPoolLayout {
    // Strides d'enregistrement par famille de slot (octets, relevés dans les pas des
    // boucles `this + STRIDE * i + ...` du désassemblage).
    inline constexpr int kSprite2DSlotStride = 148;
    inline constexpr int kModelObjSlotStride = 148;
    inline constexpr int kMotionSlotStride   = 156;
    inline constexpr int kSObjectSlotStride  = 144;
    inline constexpr int kSoundSlotStride    = 192;

    // Comptes de slots par catégorie Sprite2D_BuildPath(this, categorie, ...) — bornes
    // des boucles `for (i=0; i<N; ++i)` de AssetMgr_InitAllSlots, dans l'ordre du code.
    inline constexpr int kSprite2DCat1Count = 4500;
    inline constexpr int kSprite2DCat2Count = 4000;
    inline constexpr int kSprite2DCat3Count = 760;
    inline constexpr int kSprite2DCat4Count = 150;
    inline constexpr int kSprite2DCat5Count = 999;
    inline constexpr int kSprite2DCat6Count = 3 * 35;   // boucle imbriquée 3x35
    inline constexpr int kSprite2DCat7Count = 350;

    // ModelObj_BuildPath : 3x2x508 (cat.1), 3x2x35 (cat.2), 320 (cat.3), 246 (cat.4),
    // 3x2x508 (cat.5).
    inline constexpr int kModelObjCat1Count = 3 * 2 * 508;
    inline constexpr int kModelObjCat2Count = 3 * 2 * 35;
    inline constexpr int kModelObjCat3Count = 320;
    inline constexpr int kModelObjCat4Count = 246;
    inline constexpr int kModelObjCat5Count = 3 * 2 * 508;

    // Motion_BuildPathAndLoad : 3x3x8x128 (cat.1 et cat.6), 66x3 (cat.2), 333x21 (cat.3),
    // 59x6x2 (cat.4), 3x2x2 (cat.1 variante 2 sous-args), 42x3 (cat.5).
    inline constexpr int kMotionCat1Count = 3 * 3 * 8 * 128;
    inline constexpr int kMotionCat2Count = 66 * 3;
    inline constexpr int kMotionCat3Count = 333 * 21;
    inline constexpr int kMotionCat4Count = 59 * 6 * 2;
    inline constexpr int kMotionCat5Count = 42 * 3;
    inline constexpr int kMotionCat6Count = 3 * 3 * 8 * 128;

    // Nombre de threads de chargement asynchrone démarrés par cThread_Start en fin de
    // AssetMgr_InitAllSlots (unk_BC0604/BC0670/BC06DC/BC0748/BC07B4 + this).
    inline constexpr int kAsyncLoaderThreadCount = 5;
} // namespace ModelMotionPoolLayout

// Initialise le pool mGDATA. Réimplémentation FIDÈLE de la partie de
// AssetMgr_InitAllSlots 0x4DEB50 qui ne dépend d'aucun autre sous-système : elle ne
// construit PAS les chemins de fichiers (délégué aux 6 fonctions BuildPath, hors
// périmètre — cf. commentaire de tête) et ne démarre PAS les threads asynchrones
// (délégué à Thread/Sound, hors périmètre). Renvoie toujours true, comme l'original
// (aucun chemin d'échec dans le désassemblage).
bool InitModelMotionPool();

// ---------------------------------------------------------------------------
// 2) mZONEMAININFO — table 350 lignes {start,end,type,flag} (Motion_InitFrameTable
//    0x4F1380). Purement des constantes en dur dans le binaire, aucun fichier.
// ---------------------------------------------------------------------------
struct MotionFrameRange {
    int32_t start = 0;  // this[2*i]     — frame de début (1-based), 0 si non couvert
    int32_t end   = 0;  // this[2*i+1]   — frame de fin
    int32_t type  = -1; // this[700+2*i] — catégorie d'animation, -1 si non couvert
    int32_t flag  = 0;  // this[701+2*i] — indicateur 0/1
};
inline constexpr int kMotionFrameTableCount = 350;

// Remplit g_MotionFrameRangeTable-équivalent (350 entrées, valeurs extraites du
// pseudocode Hex-Rays de Motion_InitFrameTable, 350/350 cas couverts). Renvoie
// toujours true (fidèle : aucun chemin d'échec dans l'original).
bool InitFrameTable();

// Accesseur — motionIndex 1-based (1..350), comme les accesseurs GInfo2_GetVec3/
// GInfo_FindMotionByFrameId. Renvoie nullptr hors bornes ou si InitFrameTable() n'a
// pas encore été appelé.
const MotionFrameRange* GetFrameRange(int motionIndex1Based);

// ---------------------------------------------------------------------------
// 3) mZONENPCINFO — table d'ancrage 350x501 DWORD (Motion_LoadGInfo002Bin 0x4FCFD0,
//    fichier "G02_GINFO\002.BIN" sous la racine GameData, 701400 o exacts, RAW).
// ---------------------------------------------------------------------------
inline constexpr int kAttachTableRowCount   = 350;   // motions 1..350
inline constexpr int kAttachTableRowStride  = 501;   // DWORD par ligne
inline constexpr size_t kGInfo002BinSize    = 701400; // 350 * 501 * 4

// Charge "G02_GINFO\002.BIN" depuis <gameDataDir>\G02_GINFO\002.BIN. Échoue (false,
// buffer inchangé) si le fichier est illisible OU si sa taille diffère de
// kGInfo002BinSize (garde stricte, fidèle au `NumberOfBytesRead == 701400` d'origine).
bool LoadGInfo002Bin(const std::string& gameDataDir);

// Pointeur brut vers la table chargée (350*501 DWORD), ou nullptr si non chargée.
const uint32_t* LoadedAttachTable();

// Ligne brute (501 DWORD) pour une motion 1-based, ou nullptr hors bornes / non chargée.
const uint32_t* AttachTableRow(int motionIndex1Based);

// GInfo_FindMotionByFrameId 0x4FD070 — réimplémentation fidèle et autonome (ne dépend
// pas de Motion_GetAABB). Parcourt les 350 lignes, renvoie l'index de motion (1-based)
// de la PREMIÈRE ligne dont un frame id (row[1..row[0]]) == frameId, ou 0 si absent.
int FindMotionByFrameId(int frameId);

// Point d'ancrage BRUT (x,y,z) — combine la recherche interne de GInfo_CalcLeftMargin/
// GInfo_CalcRightMargin (mêmes index i/j) mais SANS la correction par Motion_GetAABB
// (hors périmètre, cf. commentaire de tête). L'appelant qui dispose de la bounding-box
// de la motion peut reproduire exactement :
//   leftMargin  = round(x - aabb.minX)   (GInfo_CalcLeftMargin  0x4FD1D0)
//   rightMargin = round(aabb.maxX - z)   (GInfo_CalcRightMargin 0x4FD2F0)
// Renvoie false si frameId n'est trouvé dans aucune ligne (x/y/z laissés à 0).
bool GetRawAttachPoint(int frameId, float& x, float& y, float& z, int* outMotionIndex1Based = nullptr);

// --- Accesseurs directs "PNJ de zone" (indexation par zoneId, PAS de recherche) ---
// Ajoutés le 2026-07-14 après correction sémantique (cf. commentaire de tête §3) :
// reproduisent exactement la lecture faite par `cGameData_LoadZoneNpcInfo` 0x5578E0,
// qui indexe DIRECTEMENT `row = AttachTableRow(zoneId)` (pas de recherche par valeur).
// npcIndex0Based doit être < ZoneNpcCount(zoneId1Based) (sinon retour false/valeurs à 0).
// Voir Game/StaticNpcLoader.h pour le chargeur de haut niveau qui les enchaîne.

// row[0] — nombre de PNJ statiques placés sur la zone (0 si zoneId hors bornes ou table
// non chargée). Borne dure côté fichier original : 100 max (row[1..100]).
int ZoneNpcCount(int zoneId1Based);

// row[1+npcIndex] — kindId 1-based (index dans la table mNPC, cf. SkillDefTbl_GetRecord
// dans cGameData_LoadZoneNpcInfo). Renvoie 0 hors bornes.
uint32_t ZoneNpcKindId(int zoneId1Based, int npcIndex0Based);

// row[101+3*npcIndex .. +2] — position (x,y,z) du PNJ. Renvoie false hors bornes
// (x/y/z laissés à 0).
bool ZoneNpcPosition(int zoneId1Based, int npcIndex0Based, float& x, float& y, float& z);

// row[401+npcIndex] — angle d'affichage initial du PNJ (radians, copié tel quel en
// "baseline" +80 par cGameData_LoadZoneNpcInfo). Renvoie 0.0f hors bornes.
float ZoneNpcAngle(int zoneId1Based, int npcIndex0Based);

// ---------------------------------------------------------------------------
// 4) mZONEMOVEINFO — table de coordonnées 350x805 FLOAT (Motion_LoadGInfo003Bin
//    0x4FD420, fichier "G02_GINFO\003.BIN" sous la racine GameData, 1127000 o exacts,
//    RAW). Utilisée par Game/MapWarp.h (IFactionTownCoordResolver, fallback
//    GInfo2_GetVec3) — brancher LoadedCoordTable()/GetVec3() depuis là.
// ---------------------------------------------------------------------------
inline constexpr int kCoordTableRowCount  = 350;  // motions/NPC 1..350
inline constexpr int kCoordTableRowStride = 805;  // FLOAT par ligne (3220 o)
inline constexpr size_t kGInfo003BinSize  = 1127000; // 350 * 805 * 4

// Charge "G02_GINFO\003.BIN" depuis <gameDataDir>\G02_GINFO\003.BIN. Échoue (false,
// buffer inchangé) si illisible ou si la taille diffère de kGInfo003BinSize.
bool LoadGInfo003Bin(const std::string& gameDataDir);

// Pointeur brut vers la table chargée (350*805 FLOAT), ou nullptr si non chargée.
// C'est l'accesseur demandé pour brancher Game/MapWarp.h : layout = ligne i (0-based,
// motion=i+1) de 805 float, les 3 premiers = position (x,y,z) ; le reste (802 float)
// n'est consommé par aucun appelant connu du désassemblage relevé.
const float* LoadedCoordTable();

// GInfo2_GetVec3 0x4FD4C0 — réimplémentation fidèle : motionIndex1Based hors [1,350]
// => (0,0,0) et renvoie false ; sinon lit row[0..2] et renvoie true.
bool GetVec3(int motionIndex1Based, float& x, float& y, float& z);

} // namespace ts2::game

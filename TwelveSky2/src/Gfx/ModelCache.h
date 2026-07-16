// Gfx/ModelCache.h — cache paresseux des modèles skinnés (.SOBJECT) résidents GPU.
//
// PROBLÈME résolu ici : Gfx/MeshRenderer.h (DÉJÀ ÉCRIT, jalon 4) sait uploader/dessiner
// un asset::SObject une fois PARSÉ EN MÉMOIRE (MeshRenderer::Upload), mais rien dans
// ClientSource ne va chercher le fichier .SOBJECT sur disque pour une entité donnée, ne
// le parse (asset::SObject::Load), ne l'uploade, ni ne garde le résultat en mémoire GPU
// d'une frame à l'autre. ModelCache comble ce trou : clé = nom de modèle SANS extension
// ("stem", ex. "C001001001"), valeur = ts2::gfx::SkinnedModel déjà résident GPU.
//
// PATTERN DE CHEMIN — déterminé par exploration réelle du dépôt (PAS une supposition) :
//   GameData/G03_GDATA/D04_GSOBJECT/NNN/<stem>.SOBJECT
// avec NNN = dossier de catégorie, ET stem[0] = lettre de préfixe cohérente à 100% avec
// les fichiers réellement présents sur disque (2280+66+915+6+... fichiers recensés) ET
// avec `SObject_BuildPath 0x4D89C0` (Docs/TS2_GXD_ENGINE.md §2.7 : "par catégorie 1=C
// perso, 2=N NPC, 3=M monstre, 4=P, 5/10=L, 6=W arme, 7=H, 9=Y, 10=A accessoire") :
//   001/C*.SOBJECT (2280, perso)   002/N*.SOBJECT (66, NPC)   003/M*.SOBJECT (915, monstre)
//   004/P*.SOBJECT                 005/L*.SOBJECT             006/W*.SOBJECT (816, arme)
//   007/H*.SOBJECT                 009/Y*.SOBJECT              010/A*.SOBJECT
// (008 n'existe pas — trou confirmé par le scan disque, pas une lacune de cette table).
// Voir aussi Game/MotionPools.h (§1, commentaire de tête) qui documente que
// SObject_BuildPath appartient au pool mGDATA et était jusqu'ici explicitement HORS
// PÉRIMÈTRE de tout module déjà écrit — ce fichier l'implémente enfin (BuildSObjectPath).
//
// CORRÉLATION AVEC LES BASES DE DONNÉES (mission) :
//   - ITEM_INFO (Game/GameDatabase.h, DÉJÀ ÉCRIT) : le champ `model[3][51]` EST
//     directement le "stem" attendu ici (commentaire d'origine : "3 noms de modeles (51 o
//     chacun)") — cf. GetForItem() ci-dessous, câblé et fonctionnel.
//
//   - MONSTER_INFO / NpcDefRecord (mission "résolution modèle monstre/PNJ", RE complémentaire
//     menée pour ce fichier, décompilation MCP idaTs2 en direct — PAS les EA suggérés par la
//     mission qui sont FAUX, cf. piège de nommage ci-dessous) :
//     ATTENTION PIÈGE DE NOMMAGE IDB (même famille que le swap NPC/Quest documenté en tête de
//     Game/ExtraDatabases.h) : la mission cite « MobDb_LoadImg 0x4C3930 / MobDb_ValidateEntry
//     0x4C2C50 » pour les monstres — CES DEUX EA CHARGENT/VALIDENT EN RÉALITÉ ITEM_INFO
//     (005_00002.IMG), PAS MONSTER_INFO. Les vrais EA MONSTER_INFO (005_00004.IMG, nom embarqué
//     "MONSTER_INFO", rec[0]="Goblin") sont `ItemDefTbl_LoadImg 0x4C62A0` (nom IDB trompeur,
//     charge en fait les monstres) / validateur `ItemDefTbl_ValidateRecord 0x4C5350` — décompilés
//     ici pour confirmation, cf. Docs/TS2_IMG_FORMAT.md §4.2 (déjà correct) et
//     Game/GameDatabase.cpp::kTables (stride 944, header 88, count 10000 — déjà exact).
//     Layout confirmé par décompilation du validateur 0x4C5350 : id@+0 (1..10000, ==index+1),
//     name@+4 (25 o cstring), PUIS DEUX CHAÎNES OPTIONNELLES 101 o à +29 et +130 (boucle
//     `for(j<2) for(k<101 && rec[101*j+k+29])` — TOUJOURS VIDES sur les 12 premiers records
//     réels du fichier live, cf. dump Python ad hoc : ni "Goblin"(id1) ni "Dragon Priest"(id2)
//     n'y ont de texte). ÇA N'EST DONC PAS un champ "nom de modèle" à la ITEM_INFO — rôle non
//     élucidé (peut-être sous-titre/texte de boss, jamais peuplé sur les mobs communs testés).
//     Puis champs numériques dont dims collision @+248/+252/+256 (Game/EntityManager.cpp::
//     kDefDimA/kDefDimB, formule sqrt(a²+b²)*0.5confirmée), drop table A ×5 paires @+448 (8 o
//     chacune) et drop table B ×50 paires @+544 — AUCUN champ dans les ~230 dwords du record
//     944 o n'a de borne de validation compatible avec un "index de modèle" (voir plus bas).
//
//     RÉSOLUTION RÉELLE DU MODÈLE (trouvée via `AssetMgr_InitAllSlots 0x4DEB50`, qui pré-génère
//     TOUS les chemins SObject au boot en appelant `SObject_BuildPath 0x4D89C0` pour chaque
//     catégorie) : le nom de fichier n'est PAS un champ stocké dans MONSTER_INFO / NpcDefRecord
//     — il est **calculé par un printf** à partir d'un "kindIndex" (indice de MODÈLE visuel,
//     PAS l'id de définition) :
//       Catégorie 3 (monstre, dossier 003) : "M%03d%03d%03d.SOBJECT" %
//                                              (kindIndex+1, variant+1, sub+1)
//         kindIndex 0-based ∈ [0,333) (boucle AssetMgr `for(i59<333)`) ; variant 0-based ∈ [0,3) :
//         0="pose de base" (1 fichier, sub fixe 0), 1="variante" (sub 0..3, 4 fichiers),
//         2="variante secondaire" (1 fichier, sub fixe 0).
//       Catégorie 2 (PNJ, dossier 002) : "N%03d%03d001.SOBJECT" % (kindIndex+1, variant+1)
//         kindIndex 0-based ∈ [0,66) (boucle AssetMgr `for(i57<66)`) ; variant observé TOUJOURS 0
//         dans le binaire (boucle interne `for(i58<1)`) mais le printf accepte un 2e indice —
//         exposé quand même pour fidélité totale au format (cf. BuildNpcStem ci-dessous).
//     Ces comptes (333 modèles monstre, 66 modèles PNJ) correspondent EXACTEMENT aux comptes de
//     fichiers réels cités par le bandeau ci-dessus (003/M* et 002/N*) — la table de catégories
//     kSObjectCategories existante (prefix 'M'->3, 'N'->2) est donc DÉJÀ correcte pour ces stems.
//
//     ✅ MONSTRE RÉSOLU (mission complémentaire « résolution modèle monstre/PNJ », RE menée le
//     2026-07-14, preuve complète et vérifiée dans Docs/TS2_MONSTER_NPC_MODEL.md §1-4) : le champ
//     qui donne le kindIndex EST bien dans MONSTER_INFO — c'est le dword `+244` (nommé
//     `kindIndexP1` dans le doc), 1..10000 selon le validateur mais borné en pratique à [1,333]
//     par le consommateur (Model_GetNpcMotionSlot 0x4E5960, `a2 > 0x14C` => fallback) :
//       kindIndex (0-based, [0,333)) = MONSTER_INFO.field244 - 1
//     Preuve directe : `Char_Draw 0x5805C0` lit `*(DWORD*)(*(this+24)+244) - 1` et le passe tel
//     quel à `Model_GetNpcMotionSlot`/`SObject_DrawEx` ; `this+24` == MONSTER_INFO* est prouvé par
//     `Pkt_SpawnMonster 0x467B00` (stocke `ItemDefTbl_GetRecord(id)` à l'offset dword 24 de
//     l'entité monstre). Preuve arithmétique croisée (§4 du doc) : le catalogue statique 333
//     entrées pré-généré par `AssetMgr_InitAllSlots 0x4DEB50` (`flt_FBBF3C`/`flt_FF67CC`) tombe
//     EXACTEMENT sur les mêmes adresses que celles lues par `Char_Draw`/`Char_UpdateMotionState`.
//     Les 1263 enregistrements MONSTER_INFO peuplés (id max=1511) partagent donc bien N-vers-1 ce
//     kindIndex 333 valeurs (plusieurs définitions = même modèle visuel, ex. variantes de rang).
//     `GetForMonster()` ci-dessous est câblé sur cette formule (cf. Gfx/ModelCache.cpp).
//
//     ✅ PNJ RÉSOLU (mission "rendu mesh PNJ", 2026-07-14, cf. Docs/TS2_NPC_MESH_DRAW.md §2-3 +
//     Docs/TS2_NPC_ZONE_LOADER_TRIGGER.md — ce bandeau REMPLACE l'ancienne note "PNJ NON RÉSOLU",
//     désormais caduque) : le corps PNJ EST dessiné en jeu par `Npc_DrawMesh 0x57FF00` (sur un
//     tableau de RENDU séparé `g_NpcRenderArray` 0x1764D14, pas `dword_17AB534` — d'où l'absence
//     d'appel `Char_Draw` sur les PNJ). `Npc_DrawMesh` lit le kindIndex+1 du modèle visuel à
//     l'offset `+1324` de l'enregistrement `mNPC` résolu (`SkillDefTbl_GetRecord`) = champ
//     `NpcDefRecord::fieldE` (Game/ExtraDatabases.h). Borne dure [1,66] par `Model_GetNpcMeshSlot
//     0x4E5910` (`a2 <= 0x41`). `GetForNpc()` ci-dessous appelle `GetForNpcKind(fieldE - 1, 0)`
//     dans cette borne, nullptr sinon (fallback d'origine hors catalogue).
//
//   - EntityRenderInfo::modelCategoryId (Game/EntityDrawLogic.h) est un ENTIER (id
//     costume/modèle), PAS un nom de fichier : sa résolution vers un stem passe par une
//     table dédiée (probable PcModel_ResolveEquipSlot 0x4E46A0, Docs/TS2_GXD_ENGINE.md),
//     elle aussi hors périmètre de cette mission (cache seulement).
#pragma once
#include "Gfx/MeshRenderer.h"
#include "Game/GameDatabase.h"
#include "Game/ExtraDatabases.h" // pour game::NpcDefRecord (GetForNpc)
#include <cstdint>
#include <string>
#include <unordered_map>

namespace ts2::gfx {

// ---------------------------------------------------------------------------
//  Résolution de chemin (voir bandeau de tête pour la preuve du pattern).
// ---------------------------------------------------------------------------
struct SObjectCategory { char prefix; int folder; };

// Table de catégories D04_GSOBJECT vérifiée par scan disque (cf. bandeau de tête).
inline constexpr SObjectCategory kSObjectCategories[] = {
    {'C', 1}, {'N', 2}, {'M', 3}, {'P', 4}, {'L', 5}, {'W', 6}, {'H', 7}, {'Y', 9}, {'A', 10},
};

// Numéro de dossier D04_GSOBJECT/NNN pour un préfixe de stem donné ('C','N','M',...).
// -1 si préfixe inconnu.
int ResolveSObjectFolder(char prefix);

// Chemin complet : <gameDataDir>\G03_GDATA\D04_GSOBJECT\NNN\<stem>.SOBJECT.
// Chaîne vide si `stem` est vide ou de préfixe inconnu (aucune exception levée).
std::string BuildSObjectPath(const std::string& gameDataDir, const std::string& stem);

// ---------------------------------------------------------------------------
//  Stems monstre/PNJ — format confirmé par décompilation de SObject_BuildPath 0x4D89C0
//  (cas catégorie 3 et 2), cf. bandeau de tête pour la preuve complète et les comptes réels.
// ---------------------------------------------------------------------------

// Stem "M{kindIndex+1:03d}{variant+1:03d}{sub+1:03d}" (catégorie 3, dossier 003, préfixe 'M').
// kindIndex 0-based dans [0,333) — CE N'EST PAS le champ `id` d'un enregistrement MONSTER_INFO
// (1..10000/1263 peuplés, cf. bandeau) : c'est l'indice de MODÈLE visuel (333 valeurs distinctes
// seulement). variant 0-based dans [0,3), sub borné selon variant (0/2 -> sub doit valoir 0 ;
// 1 -> sub dans [0,4)), fidèle aux 3 boucles internes d'AssetMgr_InitAllSlots 0x4DEB50. Chaîne
// vide si un paramètre est hors bornes (aucune exception levée).
std::string BuildMonsterStem(int kindIndex, int variant = 0, int sub = 0);

// Stem "N{kindIndex+1:03d}{variant+1:03d}001" (catégorie 2, dossier 002, préfixe 'N').
// kindIndex 0-based dans [0,66) — CE N'EST PAS le champ `id` d'un NpcDefRecord (1..500/131
// peuplés, cf. ExtraDatabases.h + bandeau) : c'est l'indice de MODÈLE visuel (66 valeurs
// distinctes seulement). variant 0-based, sans borne connue côté binaire (le seul appel observé
// dans AssetMgr_InitAllSlots utilise toujours variant=0) — exposé pour fidélité au printf de
// SObject_BuildPath (case 2), qui accepte n'importe quel a4. Chaîne vide si kindIndex ou variant
// est négatif ou si kindIndex est hors bornes.
std::string BuildNpcStem(int kindIndex, int variant = 0);

// Stem "C{kindIndex+1:03d}{slotToken:03d}{variant+1:03d}" (catégorie 1, dossier 001, préfixe
// 'C') — CORPS DE BASE JOUEUR, RÉSOLU par décompilation directe (mission "résolution modèle de
// corps de base joueur", 2026-07-14, cf. Docs/TS2_PLAYER_BODY_MODEL.md §3ter/§5 : preuve à 3
// sites d'appel qui relisent entity+92/+96 (=body+68/+72) sur le tableau runtime g_EntityArray
// lui-même, self ET distants — PAS un simple recoupement de cardinalités). kindIndex 0-based =
// race + 3*gender (race∈[0,3), gender∈[0,2) → 6 combinaisons). `slot` sélectionne LA PIÈCE de
// corps (0 = SLOT0, catalogue flt_F59A7C, 7 variantes, token de chemin "001" ; 1 = SLOT1,
// catalogue flt_F5B21C, 3 variantes, token "002") — ce token est une CONSTANTE de catalogue
// câblée par l'appelant d'origine, JAMAIS une donnée réseau (cf. doc §0). `variant` = valeur
// brute lue depuis PlayerEntity::body+76 (slot 0) ou body+80 (slot 1). Chaîne vide si race/
// gender/slot/variant est hors bornes (aucune exception levée) : race∈[0,3), gender∈[0,2),
// slot∈{0,1}, variant∈[0,7) pour slot 0 ou [0,3) pour slot 1.
std::string BuildPlayerBodyStem(int race, int gender, int slot, int variant);

// Les 2 pièces de corps de base RÉELLEMENT dessinées ensemble par le pipeline joueur d'origine
// (Char_DrawWeaponTrailEffect EA 0x561750-0x561786 pour SLOT0 / 0x561949-0x561993 pour SLOT1,
// PAS Char_Draw — cf. Docs/TS2_PLAYER_BODY_MODEL.md §3ter-2 : Char_Draw ne dessine jamais de
// joueur). Chaque pointeur est indépendamment nullptr si son stem est hors bornes, introuvable
// sur disque, ou de parsing/upload invalide — pas d'exception, cf. ModelCache::Get().
struct PlayerBodyModel {
    const SkinnedModel* slot0 = nullptr; // C{kindIndex+1}001{costumeSlot0+1} (7 var. possibles)
    const SkinnedModel* slot1 = nullptr; // C{kindIndex+1}002{costumeSlot1+1} (3 var. possibles)
};

// ---------------------------------------------------------------------------
//  ModelCache — clé = stem (nom de modèle SANS chemin ni extension, ex "C001001001").
// ---------------------------------------------------------------------------
class ModelCache {
public:
    // renderer doit déjà être Init() (device D3D9 prêt) ; gameDataDir = racine
    // "GameData" (même convention que Game::LoadGameDatabases). maxResident = nombre de
    // modèles GPU gardés simultanément avant éviction LRU simple ; 0 = illimité
    // (déconseillé — aucune borne mémoire GPU).
    explicit ModelCache(MeshRenderer& renderer, std::string gameDataDir, size_t maxResident = 256);

    ModelCache(const ModelCache&) = delete;
    ModelCache& operator=(const ModelCache&) = delete;

    // Lazy-load : renvoie le modèle résident pour `stem`, le chargeant/uploadant au
    // premier accès. nullptr si stem vide, préfixe inconnu, fichier introuvable, parsing
    // SOBJECT invalide, ou upload GPU en échec — un échec est MIS EN CACHE (ne retente
    // pas de charger le même stem manquant à chaque appel, cf. TS2_WARN journalisé une
    // seule fois au premier échec). Un SOBJECT valide mais 0-mesh N'EST PAS un échec : il
    // est mis en cache et renvoyé normalement (SkinnedModel::Empty() == true côté appelant).
    const SkinnedModel* Get(const std::string& stem);

    // Convenience : résout ITEM_INFO::model[slot] (Game/GameDatabase.h, DÉJÀ ÉCRIT) et
    // appelle Get() dessus. slot 0 = modèle principal, 1/2 = variantes secondaires selon
    // l'item (rôle exact des slots 1/2 non désambiguïsé par ce module — cf. bandeau de
    // tête de Game/GameDatabase.h). Renvoie nullptr si le slot est hors bornes [0,3) ou si
    // le champ correspondant est une chaîne vide (item sans variante à ce slot).
    const SkinnedModel* GetForItem(const game::ItemInfo& item, int slot = 0);

    // Lazy-load direct par indice de MODÈLE monstre (voir BuildMonsterStem ci-dessus pour la
    // sémantique complète de kindIndex/variant/sub — CE N'EST PAS un id MONSTER_INFO). C'est
    // aujourd'hui la SEULE API monstre utilisable de façon fiable : cf. bandeau de tête pour la
    // preuve qu'aucun champ connu de MONSTER_INFO ne permet de calculer kindIndex depuis un id
    // de définition. nullptr si kindIndex/variant/sub hors bornes (délégué à BuildMonsterStem).
    const SkinnedModel* GetForMonsterKind(int kindIndex, int variant = 0, int sub = 0);

    // Idem pour un indice de MODÈLE PNJ (voir BuildNpcStem ci-dessus).
    const SkinnedModel* GetForNpcKind(int kindIndex, int variant = 0);

    // RÉSOLU (mission "câblage corps de base joueur", 2026-07-14, cf. bandeau de tête +
    // Docs/TS2_PLAYER_BODY_MODEL.md §3ter/§5) : lazy-load direct des 2 pièces de corps de base
    // joueur (SLOT0+SLOT1) pour un couple race/genre/costume donné — voir BuildPlayerBodyStem
    // ci-dessus pour la formule exacte et les bornes. `race`/`gender` viennent de
    // PlayerEntity::body+68/+72, `costumeSlot0`/`costumeSlot1` de body+76/+80 (mêmes offsets
    // valables pour le joueur local ET les joueurs distants, cf. doc §1 : entity[0]+96/100/104
    // sont mutés EN PLACE par l'équipement, donc déjà à jour dans `body` sans global séparé —
    // contrairement à l'arme, qui a un global self dédié dword_1673248 câblé ailleurs).
    // Chaque pièce est résolue indépendamment (l'une peut réussir et l'autre échouer/être hors
    // bornes) — voir PlayerBodyModel ci-dessus.
    PlayerBodyModel GetForPlayerBody(int race, int gender, int costumeSlot0, int costumeSlot1);

    // RÉSOLU (mission "résolution modèle monstre/PNJ dans ModelCache", 2026-07-14, cf. bandeau
    // de tête + Docs/TS2_MONSTER_NPC_MODEL.md §2/§4/§7) : résout via `game::GetMonsterInfo(monsterDefId)`
    // (accesseur 1-based, cf. ItemDefTbl_GetRecord 0x4C6570 base+944*(id-1)), lit le champ typé
    // `MonsterInfo::kindIndexP1` (+244, 1-based tel que stocké dans le fichier) et appelle
    // `GetForMonsterKind(kindIndexP1 - 1, 0, 0)` si `1 <= kindIndexP1 <= 333`. nullptr si la
    // table monstre n'est pas chargée, si `monsterDefId` est hors bornes / slot vide, ou si
    // kindIndexP1 est hors du catalogue de 333 modèles connus (fallback d'origine,
    // cf. Model_GetNpcMotionSlot 0x4E5960).
    // CORRECTION off-by-one (ce jalon) : l'ancien code indexait `g_World.db.monster.record(id)`
    // SANS -1 -- FAUX, car Pkt_SpawnMonster 0x467B00 passe l'id réseau BRUT (1-based) au getter
    // 1-based 0x4C6570. `monsterDefId` reste l'id 1-based (même convention que ResolveMobDef,
    // désormais elle-même corrigée). La struct typée `game::MonsterInfo` EXISTE désormais
    // (Game/GameDatabase.h) ; l'ancienne note "const MonsterInfo& n'existe PAS" est caduque, mais
    // la signature reste `uint32_t monsterDefId` par cohérence avec ResolveMobDef.
    const SkinnedModel* GetForMonster(uint32_t monsterDefId);

    // Variante d'état de dégât (Char_Draw 0x5805C0). `hpCurrent` = hp runtime de l'entité
    // (record monstre offset 92 = body+76 = `*((int*)this+23)` dans Char_Draw). Renvoie nullptr
    // si `mi->field232 != 2` (le monstre n'a pas d'états de dégât), si l'id/kindIndexP1 est hors
    // catalogue, ou si la table n'est pas chargée. Famille "M{k+1}002{sub+1}" (variant 1, 4
    // sous-modèles) : sub = 3 - ftol(hpCurrent*100/hpMax)/30 (borné [0,3]). Gate graphique
    // d'origine (g_Opt_GfxDetailShadows 0x84DEF8 == 1) laissée à l'appelant de rendu (W3), qui
    // choisit d'appeler cette variante vs GetForMonster (pose de base) selon l'option.
    const SkinnedModel* GetForMonsterDamaged(uint32_t monsterDefId, int hpCurrent);

    // RÉSOLU (mission "rendu mesh PNJ", 2026-07-14, cf. Docs/TS2_NPC_MESH_DRAW.md §2-3 +
    // Docs/TS2_NPC_ZONE_LOADER_TRIGGER.md) : `npc.fieldE` (+1324) EST le kindIndex+1 du modèle
    // visuel PNJ (preuve directe par décompilation de `Npc_DrawMesh` 0x57FF00, qui lit ce même
    // offset sur l'enregistrement `mNPC` résolu par `SkillDefTbl_GetRecord`). Appelle
    // `GetForNpcKind(fieldE - 1, 0)` si `1 <= fieldE <= 66` (borne dure `Model_GetNpcMeshSlot`
    // 0x4E5910, `a2 <= 0x41`) ; nullptr sinon (fallback d'origine hors catalogue).
    const SkinnedModel* GetForNpc(const game::NpcDefRecord& npc);

    // Purge tout le cache (libère immédiatement tous les VB/IB/textures GPU).
    void Clear();

    size_t Resident()    const { return entries_.size(); }
    size_t MaxResident()  const { return maxResident_; }

private:
    struct Entry {
        SkinnedModel model;
        uint64_t     lastUseTick = 0;
        bool         loadFailed  = false;
    };

    void EvictIfNeeded();

    MeshRenderer&                          renderer_;
    std::string                            gameDataDir_;
    size_t                                 maxResident_;
    uint64_t                               tick_ = 0;
    std::unordered_map<std::string, Entry> entries_;
};

} // namespace ts2::gfx

// World/WorldMap.h — chargeur de monde / zone du client TwelveSky2 (réécriture fidèle).
//
// Reverse byte-exact des quatre fonctions du sous-système « monde » :
//   World_LoadMap             0x4116b0 — porte DRM « ALT1 » + init atmosphère + météo Atmosphere.DAT
//   World_LoadDataFile        0x4118f0 — parse d'un fichier de configuration texte (.ATM) via ifstream
//   World_LoadZoneResource    0x4dcb60 — aiguillage du chargement des ressources par zone (WG/WO/WP/WM/…)
//   World_LoadCurrentZoneModel 0x4dd6e0 — (re)chargement du modèle .WM de la couche courante
// Helpers :
//   World_ZoneIdToFileId      0x4db0f0 — table zoneId -> fileId (~340 entrées)
//   cAtmosphere_ctor          0x791b40 — porte DRM : SilverLining_ValidateLicense("ALT1 License 3", clé hex)
//
// MODULE LEAF : n'inclut AUCUN header projet lourd (ni Asset, ni Gfx). Toute I/O réelle
// (chargement de fichiers/modèles/textures, appels device D3D, validation de licence) est
// déléguée à des hooks (WorldLoadHooks). On reproduit ici la SÉQUENCE, la GARDE DRM, les
// CHEMINS de fichiers, les IDENTIFIANTS de zone et les FLAGS relevés dans le désassemblage.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// MODULE LEAF : on garde l'en-tête léger. Les vues terrain typées (Gaps G4/G5/G7) sont
// définies dans Asset/WorldChunk.h ; ici on ne fait que des DÉCLARATIONS ANTICIPÉES (aucun
// include Asset). WorldMap.cpp inclut Asset/WorldChunk.h pour définir les accesseurs.
namespace ts2::asset {
struct CollisionMesh;
struct CollisionFace;
struct CollisionQuadNode;
struct TerrainVertex;
} // namespace ts2::asset

namespace ts2::world {

// ---------------------------------------------------------------------------
// Constantes de la porte DRM (World_LoadMap 0x4116b0 -> cAtmosphere_ctor 0x791b40).
// La map protégée « ALT1 » : le constructeur d'atmosphère appelle
// SilverLining_ValidateLicense(nom, cléHex) avec ces DEUX chaînes CONSTANTES
// (elles sont codées en dur dans le binaire, indépendamment du nom de map).
// ---------------------------------------------------------------------------
inline constexpr char kAltLicenseName[] = "ALT1 License 3";              // aAlt1License3 0x7ec940
inline constexpr char kAltLicenseKey[]  = "113e355254250a02094e32165441"; // a113e355254250a 0x7ec920

// Ressource d'atmosphère par défaut passée à World_LoadMap depuis la case 7.
inline constexpr char kAtmosphereResourceDir[] = "G03_GDATA\\D11_ATMOSPHERE\\"; // 0x7a7db8

// Répertoire par défaut des ressources SilverLining (cAtmosphere_ctor 0x791c1f).
inline constexpr char kSilverLiningResourceDir[] = ".\\Resources\\"; // aResources 0x7ebbf4

// Géolocalisation par défaut si Atmosphere.DAT absent (Env_SetGeoLocation 0x411d30) : Séoul.
// PLAUSIBLE (VeryOldClient n/a) : ces kDefaultGeo* SONT l'autorité runtime fidèle (37.6/127.0/0.0,
// prouvée par Env_SetGeoLocation 0x411d30), à ne PAS confondre avec les défauts SDK-stock de
// SilverLiningConfig (WorldIntegration.h, lon -122/lat 30) qui ne sont qu'un fallback de parsing.
inline constexpr double kDefaultGeoLat = 37.6;   // latitude
inline constexpr double kDefaultGeoLon = 127.0;  // longitude
inline constexpr double kDefaultGeoAlt = 0.0;    // altitude

// Nom du fichier météo lu par World_LoadMap (aAtmosphereDat 0x7ec950).
inline constexpr char kAtmosphereDatFile[] = "Atmosphere.DAT";

// ---------------------------------------------------------------------------
// Types de ressource de World_LoadZoneResource (0x4dcb60), valeurs = le paramètre a3.
// ---------------------------------------------------------------------------
// L'aiguillage a3 == index de ressource reproduit fidèlement le dispatch VeryOldClient
// `mZONE.Load(zone, idx)` (indicateur seul — les valeurs 1..12 sont prouvées par IDA, cf.
// Docs/TS2_WORLD_ROSETTA.md §1.B/1.C/1.D, chaque cas ci-dessous porte son ancre IDA).
enum class ResourceKind : int {
    FreeSound      = 1,   // WSndMgr_Free           0x4db060
    MapFileWG      = 2,   // .WG  MapColl_LoadMapFile   0x697b30 ; CONFIRMED ex-VeryOldClient: mZONE.Load(zone,2)/LoadWG
    ObjectsWO      = 3,   // .WO  MapColl_LoadObjectsA  0x6980d0 ; CONFIRMED ex-VeryOldClient: mZONE.Load(zone,3)/LoadWO (mMObject)
    ObjectsWP      = 4,   // .WP  MapColl_LoadObjectsB  0x6983b0 ; CONFIRMED ex-VeryOldClient: mZONE.Load(zone,4)/LoadWP (mPSystem)
    ShadowTex      = 5,   // .SHADOW Tex_LoadFromFile    0x6a9910 ; CONFIRMED ex-VeryOldClient: mShadowTexture/MakeShadowTexture
    WorldModel     = 6,   // .WM + .WJ  MapColl_LoadFaces 0x694510 ; CONFIRMED ex-VeryOldClient: WORLD_FOR_GXD::LoadWM (WM1/2/3->mRANGE1/2/3)
    Atmosphere     = 7,   // .ATM  World_LoadMap + World_LoadDataFile
    Minimap01      = 8,   // _MINIMAP01.IMG  Tex_LoadCompressedDDS 0x6a2e80
    Minimap02      = 9,   // _MINIMAP02.IMG
    Minimap03      = 10,  // _MINIMAP03.IMG
    WorldSound     = 11,  // .WSOUND  WSndBank_LoadFile   0x4da790
    WorldBgm       = 12,  // .BGM  Snd_LoadOggToBuffers   0x6a8120
};

// ---------------------------------------------------------------------------
// Emplacement de collision cible (offsets relevés dans l'objet monde `this`).
// World_LoadZoneResource case 6 et World_LoadCurrentZoneModel écrivent dans
// l'un de ces trois sous-objets MapColl.
// ---------------------------------------------------------------------------
// CONFLICT C-01 (voir Docs/TS2_WORLD_ROSETTA.md §2) : VeryOldClient (WORLD_FOR_GXD) ne connaît
// QUE des couches .WM (WM1/2/3 -> mRANGE1/2/3), PAS de .WJ. IDA distingue .WM(+0xA8) + .WJ(+0x150)
// + .WM2(+0x1F8) — build-diff, IDA GAGNE (World_LoadZoneResource 0x4dcb60 case 6, 3 slots MapColl,
// stride 42 dwords). NE PAS backporter l'absence de WJ. Les 3 offsets ci-dessous SONT l'ancre IDA.
enum class CollisionSlot : int {
    Main      = 0,  // this+0xA8  (168)  — collision principale (.WM), aussi rechargée par LoadCurrentZoneModel
    WJ        = 1,  // this+0x150 (336)  — collision secondaire (.WJ) — absent de VeryOldClient (CONFLICT C-01)
    Secondary = 2,  // this+0x1F8 (504)  — .WM secondaire (zones 50/52/170 : Z170_2.WM)
};

// ---------------------------------------------------------------------------
// Hooks : toute I/O réelle est déléguée à l'hôte. Chaque callback renvoie le
// booléen (tronqué à un octet dans le binaire) du chargeur d'origine.
// `user` = contexte opaque de l'hôte (recopié tel quel).
// ---------------------------------------------------------------------------
struct WorldLoadHooks {
    void* user = nullptr;

    // --- World_LoadMap 0x4116b0 / cAtmosphere_ctor 0x791b40 (porte DRM) ---
    // Crt_OperatorNew(648) : alloue l'objet cAtmosphere (648 octets). nullptr = échec.
    void* (*allocAtmosphere)(void* user, unsigned size) = nullptr;
    // cAtmosphere_ctor : construit l'atmosphère et VALIDE la licence SilverLining
    // (SilverLining_ValidateLicense 0x795db0). Renvoie le pointeur d'objet construit.
    void* (*constructAtmosphere)(void* user, void* mem, const char* licenseName,
                                 const char* licenseKey) = nullptr;
    // (*device)[+164] / (*device)[+168] : encadrement device D3D autour de l'init atmosphère.
    void  (*deviceBeginMap)(void* user, void* device) = nullptr;
    void  (*deviceEndMap)(void* user, void* device)   = nullptr;
    // cAtmosphere_Initialize 0x793390 : init(1, mapName, 0, device). Renvoie 0 = SUCCÈS
    // (le binaire prend la branche « échec/retour 0 » quand ce retour est non nul).
    int   (*atmosphereInitialize)(void* user, void* atmosphere, const char* mapName,
                                  void* device) = nullptr;
    // Ouvre+parse Atmosphere.DAT (ifstream + boucle Istream_GetChar). true si présent/valide.
    bool  (*loadWeatherDat)(void* user, const char* path) = nullptr;
    // Env_SetGeoLocation 0x411d30 (appelé si Atmosphere.DAT absent/vide).
    void  (*setGeoLocation)(void* user, double lat, double lon, double alt) = nullptr;
    // World_FinishLoad 0x411c40 (finalisation après parse réussi).
    void  (*finishLoad)(void* user) = nullptr;

    // --- World_LoadZoneResource 0x4dcb60 ---
    bool (*freeZoneSound)(void* user) = nullptr;                                   // case 1
    bool (*loadMapFileWG)(void* user, const char* path) = nullptr;                 // case 2
    bool (*loadObjectsWO)(void* user, const char* path) = nullptr;                 // case 3
    bool (*loadObjectsWP)(void* user, const char* path) = nullptr;                 // case 4
    bool (*loadShadowTexture)(void* user, const char* path) = nullptr;             // case 5
    bool (*loadFaces)(void* user, CollisionSlot slot, const char* path) = nullptr; // case 6 / LoadCurrentZoneModel
    void (*freeFaces)(void* user, CollisionSlot slot) = nullptr;                   // MapColl_Free 0x693180
    bool (*loadMinimap)(void* user, int index /*1..3*/, const char* path) = nullptr; // case 8/9/10
    bool (*loadWorldSound)(void* user, const char* path) = nullptr;               // case 11
    bool (*loadWorldBgm)(void* user, const char* path) = nullptr;                 // case 12
    // World_LoadDataFile 0x4118f0 (fichier texte .ATM) — utilisé par la case 7.
    bool (*loadDataFile)(void* user, const char* path) = nullptr;

    // --- Câblage requête terrain (Gap G02) ---
    // Après un loadFaces réussi d'une couche, WorldMap récupère la maille de collision
    // DÉCODÉE (asset::CollisionMesh, Gap G01) pour répondre aux requêtes de sol. Optionnel
    // (guardé). Réf IDA : la MapColl runtime EST le `this` de MapColl_GetGroundHeight 0x697130
    // (faces this[22], quadtree this[35]) ; ici on relie la donnée déjà décodée par l'hôte.
    const asset::CollisionMesh* (*queryCollisionMesh)(void* user, CollisionSlot slot) = nullptr;
};

// ---------------------------------------------------------------------------
// WorldMap — état + logique des quatre chargeurs. Représente l'objet monde
// (les champs `this+N` du binaire) sans posséder les ressources : celles-ci
// vivent côté hôte, atteintes via les hooks.
// ---------------------------------------------------------------------------
class WorldMap {
public:
    explicit WorldMap(const WorldLoadHooks& hooks) : hooks_(hooks) {}

    // Device D3D courant (this+12) — g_GfxRenderer_pDevice 0x800074 dans la case 7.
    void SetDevice(void* device) { device_ = device; }

    // Id de zone courant utilisé par LoadCurrentZoneModel (g_SelfMorphNpcId 0x1675a98).
    void SetCurrentZoneId(int zoneId) { currentZoneId_ = zoneId; }
    int  CurrentZoneId() const { return currentZoneId_; }

    // World_LoadMap 0x4116b0.
    //   `mapName` -> a2 (passé à cAtmosphere_Initialize ; ex. kAtmosphereResourceDir).
    //   `drmKey`  -> clé hex de la porte DRM (constante kAltLicenseKey dans le binaire).
    //   Le device est pris sur device_ (SetDevice). Renvoie true (=1) en succès.
    bool LoadMap(const std::string& mapName, const std::string& drmKey = kAltLicenseKey);

    // World_LoadZoneResource 0x4dcb60. Renvoie l'octet de retour d'origine (LOBYTE(v3)).
    unsigned char LoadZoneResource(int zoneId, ResourceKind kind);

    // World_LoadCurrentZoneModel 0x4dd6e0. `mode` = a2 (index de couche/état).
    // Renvoie -1 si zone invalide, 0 si aucun modèle à recharger, sinon le retour de loadFaces.
    int LoadCurrentZoneModel(int mode);

    // World_ZoneIdToFileId 0x4db0f0 : zoneId -> fileId (Z%03d.*). -1 si inconnu.
    static int ZoneIdToFileId(int zoneId);

    // -----------------------------------------------------------------------
    // Données terrain TYPÉES / consommables (Gaps G4/G5/G7). La maille de collision
    // décodée (asset::CollisionMesh, produite par Asset/WorldChunk lors du parse .WM/.WG)
    // est détenue par l'hôte via le hook loadFaces ; WorldMap la référence SANS la posséder.
    // Le câblage (l'intégration appelle SetCollisionMesh après un loadFaces de la couche
    // principale) vit dans WorldIntegration (NON possédé ici) — d'où le TODO ci-dessous.
    // Tant qu'aucune maille n'est liée, les accesseurs renvoient des vecteurs vides
    // (build-safe, aucun déréférencement). Réf IDA : MapColl_LoadFaces 0x694510 /
    // MapColl_GetGroundHeight 0x697130 / Terrain_Render 0x698670.
    // TODO(integration) : brancher SetCollisionMesh(slot=Main) après loadFaces (G02/G05).
    void SetCollisionMesh(const asset::CollisionMesh* mesh) { collisionMesh_ = mesh; }
    const asset::CollisionMesh* CollisionMeshData() const { return collisionMesh_; }

    // Faces de collision / rendu terrain (156o typées, Gap G4). MapColl_LoadFaces 0x694510.
    const std::vector<asset::CollisionFace>& Faces() const;
    // Nœuds du quadtree (48o typés, Gap G5). MapColl_GetGroundHeight 0x697130 : racine = index 0,
    // feuille <=> child[0]==-1 ; trisIndex = offset dans FaceIndices().
    const std::vector<asset::CollisionQuadNode>& Quadtree() const;
    // Sommets terrain à plat (40o, FVF 530, Gap G7). Terrain_Render 0x698670 (upload VB).
    const std::vector<asset::TerrainVertex>& Vertices() const;
    // Buffer d'index de faces agrégé (feuilles du quadtree -> Faces()). Support requête sol (G02).
    const std::vector<uint32_t>& FaceIndices() const;

    // -----------------------------------------------------------------------
    // Requêtes de sol / collision terrain (Gaps G02/G03/G04). Portage byte-fidèle des
    // MapColl_* (voir `namespace collision` en bas de ce header) sur la maille principale
    // liée (collisionMesh_, câblée par le hook queryCollisionMesh après un loadFaces de la
    // couche Main .WM). Build-safe : renvoient false / no-op tant qu'aucune maille n'est liée.
    // Ce sont les FOURNISSEURS que l'agent de consolidation (G03) câble aux hooks consommateurs
    // hors périmètre : host.GetGroundHeight (Game/EntityLifecycleTick.h:199), IsPointOnGround
    // (Game/AnimationTick.h:95), IsGroundBlocked (ICameraCollisionQueries AnimationTick.h:190).
    // -----------------------------------------------------------------------
    // MapColl_GetGroundHeight 0x697130, forme consommateur (a5=1,a6=probeCeilingY,a7=0,a8=1,
    // cf. Char_Update 0x581e10 / World_IsPointOnGround 0x540d40) : hauteur de sol marchable
    // sous (x,z), plafonnée à probeCeilingY. true si trouvée (outGroundY rempli).
    bool GetGroundHeight(float x, float z, float probeCeilingY, float& outGroundY) const;
    // IsGroundBlocked-shape (AnimationTick.h:190) : MapColl_GetGroundHeight(x,z,&out,0,0.0,0,1)!=0.
    bool HasGroundAt(float x, float z) const;
    // World_IsPointOnGround 0x540d40 : (x,y,z) est-il au-dessus d'un sol marchable (plafond=y+20).
    bool IsPointOnGround(float x, float y, float z) const;
    // MapColl_RaycastNearest 0x6960c0 : 1er impact de face le long du rayon (start + t*dir, t>=0).
    // outFaceIndex/outHit = impact le plus proche ; twoSide accepte les faces des deux côtés (a7).
    bool Raycast(const float start[3], const float dir[3], uint32_t& outFaceIndex,
                 float outHit[3], bool twoSide = false) const;
    // MapColl_SlideMoveGround 0x697330 : glisse (from->to) plaqué à la maille (pas = speed*dt),
    // puis résout la hauteur de sol. outPos = position finale {x,y,z} (toujours rempli).
    bool SlideMoveGround(const float from[3], const float to[3], float speed, float dt,
                         float outPos[3]) const;

    // --- Champs relevés de l'objet monde ---
    // CONFLICT C-03 (voir Docs/TS2_WORLD_ROSETTA.md §2) : `valid_` (this+4) ET `atmosphereLoaded_`
    // (byte_18C67C8) désignent le MÊME octet g_WorldEnv+4 dans la cible. IDA : World_LoadMap
    // 0x41176E fait `*(this+4)=1` en succès (armant le court-circuit `||` de la case 7), remis à 0
    // par World_UnloadMap 0x411a80. Ici LoadMap ne pose que `valid_` -> le court-circuit case 7 ne
    // s'arme jamais (correctif fidèle = poser aussi `atmosphereLoaded_` en succès ; jalon compilé dédié).
    bool  valid_       = false;   // this+4  (mis à 1 par World_LoadMap en succès) = byte_18C67C8 (CONFLICT C-03)
    void* atmosphere_  = nullptr; // this+8  (objet cAtmosphere)
    void* device_      = nullptr; // this+12 (device D3D)

    // Flag dword_1686134 : sélection de variante de la zone 291 dans la case 6 de
    // LoadZoneResource (0 -> Z291_1.WM, sinon -> Z291_2.WM). Dans LoadCurrentZoneModel,
    // la zone 291 est au contraire pilotée par `mode` (voir CurrentZoneModelPath).
    int flagZ291Variant = 0;      // dword_1686134

    // Flag byte_18C67C8 : atmosphère déjà chargée (case 7). Si vrai, on saute World_LoadMap.
    // (byte_18C67C8 == g_WorldEnv+4 == le MÊME octet que `valid_` dans la cible — CONFLICT C-03 ci-dessus.)
    bool atmosphereLoaded_ = false; // byte_18C67C8

    // Templates de chemins (relevés tels quels dans .rdata).
    static constexpr char kFmtWG[]       = "G03_GDATA\\D07_GWORLD\\Z%03d.WG";
    static constexpr char kFmtWO[]       = "G03_GDATA\\D07_GWORLD\\Z%03d.WO";
    static constexpr char kFmtWP[]       = "G03_GDATA\\D07_GWORLD\\Z%03d.WP";
    static constexpr char kFmtShadow[]   = "G03_GDATA\\D07_GWORLD\\Z%03d.SHADOW";
    static constexpr char kFmtWM[]       = "G03_GDATA\\D07_GWORLD\\Z%03d.WM";
    static constexpr char kFmtWJ[]       = "G03_GDATA\\D07_GWORLD\\Z%03d.WJ";
    static constexpr char kFmtAtm[]      = "G03_GDATA\\D07_GWORLD\\Z%03d.ATM";
    static constexpr char kFmtMinimap1[] = "G03_GDATA\\D07_GWORLD\\Z%03d_MINIMAP01.IMG";
    static constexpr char kFmtMinimap2[] = "G03_GDATA\\D07_GWORLD\\Z%03d_MINIMAP02.IMG";
    static constexpr char kFmtMinimap3[] = "G03_GDATA\\D07_GWORLD\\Z%03d_MINIMAP03.IMG";
    static constexpr char kFmtWSound[]   = "G03_GDATA\\D09_WSOUND\\Z%03d\\Z%03d.WSOUND";
    static constexpr char kFmtBgm[]      = "G03_GDATA\\D10_WORLDBGM\\Z%03d.BGM";

    // Chemin .WM principal (case 6) pour un fileId. `z291Variant` = flagZ291Variant.
    // Pour 50/52/170, renvoie le principal Z170_1.WM (le secondaire Z170_2.WM se charge à part).
    static std::string ZoneModelPathWM(int fileId, int z291Variant);
    // Chemin .WJ secondaire (case 6) pour un fileId.
    static std::string ZoneModelPathWJ(int fileId);
    // Chemin .WM de la couche courante (LoadCurrentZoneModel) selon fileId + mode.
    // Chaîne vide => aucune ressource à charger (le binaire compare à "" et saute).
    static std::string CurrentZoneModelPath(int fileId, int mode);

private:
    WorldLoadHooks hooks_;
    int  currentZoneId_ = 0;
    // this+180 : copie de dword_18C5358 (config météo par défaut). 104 octets, tous à 0.
    std::array<uint8_t, 104> weather_{};
    // Maille de collision typée liée (Gaps G4/G5/G7). NON-possédante (détenue par l'hôte).
    const asset::CollisionMesh* collisionMesh_ = nullptr;
};

// ===========================================================================
// namespace collision — moteur de requête terrain (Gaps G02/G03/G04).
// Portage byte-fidèle des fonctions MapColl_* de TwelveSky2, opérant directement sur la
// maille DÉCODÉE (asset::CollisionMesh, Gap G01 déjà en place). La MapColl runtime EST le
// `this` de ces fonctions ; correspondance (prouvée par les EA cités) :
//   this[1]  (flag actif)   -> maille chargée (tris + nodes non vides)
//   this[22] (base faces)   -> mesh.tris[]        (stride 156 ; plan a/b/c/d @+124/+128/+132/+136)
//   this[35] (base quadtree)-> mesh.nodes[]       (48o ; racine = index 0 ; feuille <=> child[0]==-1)
//   leaf.trisIndex + i      -> mesh.triIndices[node.trisIndex + i] -> tris[faceIdx]
// Fonctions pures, sans état, build-safe (guards sur maille vide / index hors bornes).
// Réf IDA par fonction dans WorldMap.cpp.
// ===========================================================================
namespace collision {

// MapColl_RayHitTriangle 0x695ae0 — containment barycentrique 3D du point {px,py,pz} dans la face.
bool RayHitTriangle(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                    float px, float py, float pz);
// MapColl_PointInTriangleXZ 0x695c70 — containment barycentrique du point (px,pz) dans le plan XZ.
bool PointInTriangleXZ(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                       float px, float pz);
// MapColl_GetGroundHeight 0x697130 — hauteur de sol sous (x,z) (le « sol nul » comblé).
//   a5CeilingGiven/a6Ceiling : plafond explicite (sinon = racine bboxMax.y) ;
//   a7TwoSide : accepter les faces orientées vers le bas (skip filtre marchable planeB>0) ;
//   a8OnlyOne : renvoyer le 1er hit (sinon retenir le plus haut). true => outGroundY rempli.
bool GetGroundHeight(const asset::CollisionMesh& mesh, float x, float z,
                     float& outGroundY, bool a5CeilingGiven, float a6Ceiling,
                     bool a7TwoSide, bool a8OnlyOne);
// World_IsPointOnGround 0x540d40 — (x,y,z) au-dessus d'un sol marchable (plafond=y+20 ; a5=a8=1).
bool IsPointOnGround(const asset::CollisionMesh& mesh, float x, float y, float z);
// MapColl_PointInMeshXZ 0x695dc0 — (x,z) tombe-t-il dans une face de la feuille (aucun filtre) ?
bool PointInMeshXZ(const asset::CollisionMesh& mesh, float x, float z);
// MapColl_RayPlaneTriHit 0x695ee0 — intersecte le rayon (start + t*dir, t>=0) au plan de la face
// puis teste le containment 3D ; outHit rempli si touché.
bool RayPlaneTriHit(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                    const float start[3], const float dir[3], float outHit[3], bool twoSide);
// Collide_SegmentAABB 0x69fb20 — test SAT segment(point,dir) vs AABB [bmin,bmax].
bool SegmentAABB(const float p[3], const float dir[3],
                 const float bmin[3], const float bmax[3]);
// MapColl_RaycastNearest 0x6960c0 — impact de face le plus proche le long de (start + t*dir),
// descente quadtree récursive depuis nodeIndex (0 = racine). outFaceIndex/outHit = plus proche.
bool RaycastNearest(const asset::CollisionMesh& mesh, uint32_t nodeIndex,
                    const float start[3], const float dir[3],
                    uint32_t& outFaceIndex, float outHit[3], bool twoSide);
// MapColl_SlideMoveGround 0x697330 — glisse (from->to) plaqué à la maille marchable en XZ
// (pas = speed*dt) puis résout la hauteur ; outPos = {x,y,z} final. true si sol trouvé.
bool SlideMoveGround(const asset::CollisionMesh& mesh, const float from[3],
                     const float to[3], float speed, float dt, float outPos[3]);

} // namespace collision

} // namespace ts2::world

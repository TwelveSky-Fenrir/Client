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
inline constexpr double kDefaultGeoLat = 37.6;   // latitude
inline constexpr double kDefaultGeoLon = 127.0;  // longitude
inline constexpr double kDefaultGeoAlt = 0.0;    // altitude

// Nom du fichier météo lu par World_LoadMap (aAtmosphereDat 0x7ec950).
inline constexpr char kAtmosphereDatFile[] = "Atmosphere.DAT";

// ---------------------------------------------------------------------------
// Types de ressource de World_LoadZoneResource (0x4dcb60), valeurs = le paramètre a3.
// ---------------------------------------------------------------------------
enum class ResourceKind : int {
    FreeSound      = 1,   // WSndMgr_Free           0x4db060
    MapFileWG      = 2,   // .WG  MapColl_LoadMapFile   0x697b30
    ObjectsWO      = 3,   // .WO  MapColl_LoadObjectsA  0x6980d0
    ObjectsWP      = 4,   // .WP  MapColl_LoadObjectsB  0x6983b0
    ShadowTex      = 5,   // .SHADOW Tex_LoadFromFile    0x6a9910
    WorldModel     = 6,   // .WM + .WJ  MapColl_LoadFaces 0x694510
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
enum class CollisionSlot : int {
    Main      = 0,  // this+0xA8  (168)  — collision principale (.WM), aussi rechargée par LoadCurrentZoneModel
    WJ        = 1,  // this+0x150 (336)  — collision secondaire (.WJ)
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

    // --- Champs relevés de l'objet monde ---
    bool  valid_       = false;   // this+4  (mis à 1 par World_LoadMap en succès)
    void* atmosphere_  = nullptr; // this+8  (objet cAtmosphere)
    void* device_      = nullptr; // this+12 (device D3D)

    // Flag dword_1686134 : sélection de variante de la zone 291 dans la case 6 de
    // LoadZoneResource (0 -> Z291_1.WM, sinon -> Z291_2.WM). Dans LoadCurrentZoneModel,
    // la zone 291 est au contraire pilotée par `mode` (voir CurrentZoneModelPath).
    int flagZ291Variant = 0;      // dword_1686134

    // Flag byte_18C67C8 : atmosphère déjà chargée (case 7). Si vrai, on saute World_LoadMap.
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
};

} // namespace ts2::world

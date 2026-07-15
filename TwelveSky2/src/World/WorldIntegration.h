// World/WorldIntegration.h — branche WorldMap (leaf, hooks) sur les VRAIS chargeurs
// Asset (WorldChunk/Texture/Sound) et Audio (SoundBank), pour que
// World_LoadZoneResource charge réellement les fichiers G03_GDATA\D07_GWORLD\Z%03d.*.
//
// WorldMap (World/WorldMap.h, décompilation IDA directe) ne possède aucune ressource :
// il expose des hooks. Ce fichier est la COLLE d'intégration (écrite à la main, pas
// par un agent) entre ce leaf module et les couches Asset/Audio déjà validées.
//
// Périmètre NON couvert ici (documenté, pas de faux-semblant) :
//   - Atmosphère/météo SilverLining COMPLÈTE (cAtmosphere_ctor 0x791b40, nuages/précipitations/
//     étoiles/soleil/lune) : SDK externe SilverLiningDirectX9-MT.dll non lié au projet ->
//     LoadMap() échoue proprement (comme si la licence était absente) plutôt que de simuler
//     un succès. EN REVANCHE (2026-07-15, mission "WAVE_06_silverlining") : le fichier
//     SilverLining.config global est chargé une fois, et le fichier .ATM PAR ZONE (case 7 de
//     LoadZoneResource, ci-dessous LoadDataFile) EST réellement parsé — cf.
//     Asset/AtmosphereFile.h — puis exposé via Atmosphere() pour Gfx/SkyRenderer.h. C'est un
//     sous-ensemble honnête (position géo + heure réelle + flags de rendu), pas le SDK.
//   - Rendu D3D des chunks chargés (upload VB/IB/texture) : hors périmètre "monde
//     de données" ; à brancher au jalon Gfx (MeshRenderer) une fois le placement
//     caméra/scène décidé.
#pragma once
#include "World/WorldMap.h"
#include "Asset/AtmosphereFile.h"
#include <array>
#include <memory>
#include <string>

namespace ts2::asset { class WorldChunk; struct Texture; class WSound; }
namespace ts2::audio { class SoundBank; }

namespace ts2::world {

// Sous-ensemble non destructif de SilverLining.config, chargé une fois par session.
// Le client d'origine exploite un grand nombre de clés côté DLL SilverLining ; ici on
// ne retient que les réglages de base nécessaires au socle de rendu et au repli.
struct SilverLiningConfig {
    double defaultLongitude = -122.064840;
    double defaultLatitude  = 30.0;
    double defaultAltitude  = 100.0;

    int defaultYear   = 2006;
    int defaultMonth  = 8;
    int defaultDay    = 15;
    int defaultHour   = 12;
    int defaultMinute = 0;
    double defaultSecond   = 0.0;
    double defaultTimezone  = -8.0;
    bool defaultDst         = true;

    double defaultTurbidity = 2.2;
    bool disableToneMapping = false;
    bool enableAtmosphereFromSpace = false;
    double atmosphereHeight = 300000.0;
    double atmosphereScaleHeightMeters = 8435.0;

    double skyBoxGamma = 2.2;
    bool skySimpleShader = false;
    double sunWidthDegrees = 1.0;
    double moonWidthDegrees = 10.0;
    bool disableLensFlare = false;
    bool disableSunGlare = true;
    bool disableMoonGlare = true;
    bool disableStarGlare = true;

    bool enablePrecipitationVisibilityEffects = true;
    int rainMaxParticles = 100000;
    int snowMaxParticles = 200000;
    int sleetMaxParticles = 100000;
};

// Ressources chargées pour la zone COURANTE (une seule zone active à la fois,
// comme le client d'origine qui recharge/écrase ses chunks au changement de zone).
class WorldAssets {
public:
    explicit WorldAssets(std::string gameDataDir);
    ~WorldAssets();
    WorldAssets(const WorldAssets&) = delete;
    WorldAssets& operator=(const WorldAssets&) = delete;

    // Construit les hooks liés à cette instance (user = this). À passer à WorldMap.
    WorldLoadHooks MakeHooks();

    // Racine "GameData" (contient G03_GDATA\D07_GWORLD, D09_WSOUND, D10_WORLDBGM, D11_ATMOSPHERE).
    const std::string& GameDataDir() const { return gameDataDir_; }

    // Accès aux chunks chargés (nullptr si absent/non chargé).
    const asset::WorldChunk* Collision(CollisionSlot slot) const;
    const asset::WorldChunk* Faces()   const { return wg_.get(); }
    const asset::WorldChunk* Objects() const { return wo_.get(); }
    const asset::WorldChunk* FxNodes() const { return wp_.get(); }

    // État d'atmosphère RÉEL de la zone courante (fichier Z%03d.ATM, parsé byte-exact par
    // Asset/AtmosphereFile.h — case 7 de World_LoadZoneResource). Atmosphere().Valid()==false
    // tant qu'aucune zone n'a chargé son .ATM avec succès (cf. LoadDataFile ci-dessous) ;
    // consommé par Gfx/SkyRenderer.h::SetAtmosphere() pour le gradient jour/nuit dérivé de
    // l'heure réelle de la zone.
    const asset::AtmosphereFile& Atmosphere() const { return atmosphere_; }
    const SilverLiningConfig& SilverLining() const { return silverLining_; }

private:
    // --- Implémentations des hooks (signatures WorldLoadHooks, `user` = this*). ---
    static void* AllocAtmosphere(void* user, unsigned size);
    static void* ConstructAtmosphere(void* user, void* mem, const char* name, const char* key);
    static void  DeviceBeginMap(void* user, void* device);
    static void  DeviceEndMap(void* user, void* device);
    static int   AtmosphereInitialize(void* user, void* atmosphere, const char* mapName, void* device);
    static bool  LoadWeatherDat(void* user, const char* path);
    static void  SetGeoLocation(void* user, double lat, double lon, double alt);
    static void  FinishLoad(void* user);

    static bool FreeZoneSound(void* user);
    static bool LoadMapFileWG(void* user, const char* path);
    static bool LoadObjectsWO(void* user, const char* path);
    static bool LoadObjectsWP(void* user, const char* path);
    static bool LoadShadowTexture(void* user, const char* path);
    static bool LoadFaces(void* user, CollisionSlot slot, const char* path);
    static void FreeFaces(void* user, CollisionSlot slot);
    static bool LoadMinimap(void* user, int index, const char* path);
    static bool LoadWorldSound(void* user, const char* path);
    static bool LoadWorldBgm(void* user, const char* path);
    static bool LoadDataFile(void* user, const char* path);

    std::string gameDataDir_;

    std::unique_ptr<asset::WorldChunk> wm_;             // CollisionSlot::Main
    std::unique_ptr<asset::WorldChunk> wj_;             // CollisionSlot::WJ
    std::unique_ptr<asset::WorldChunk> wmSecondary_;    // CollisionSlot::Secondary
    std::unique_ptr<asset::WorldChunk> wg_;
    std::unique_ptr<asset::WorldChunk> wo_;
    std::unique_ptr<asset::WorldChunk> wp_;
    std::unique_ptr<asset::Texture>    shadow_;
    std::array<std::unique_ptr<asset::Texture>, 3> minimaps_;
    std::unique_ptr<asset::WSound>     wsound_;
    std::unique_ptr<audio::SoundBank>  soundBank_;
    asset::AtmosphereFile              atmosphere_; // Z%03d.ATM de la zone courante (case 7)
    SilverLiningConfig                 silverLining_; // SilverLining.config (global, session)
};

} // namespace ts2::world

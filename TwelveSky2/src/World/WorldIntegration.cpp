// World/WorldIntegration.cpp — implémentation des hooks WorldLoadHooks.
#include "World/WorldIntegration.h"
#include "Asset/WorldChunk.h"
#include "Asset/Texture.h"
#include "Asset/Sound.h"
#include "Asset/FileUtil.h"
#include "Audio/Sound3D.h"
#include "Audio/AudioSystem.h"
#include "Core/Log.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

namespace ts2::world {

namespace {

std::string Trim(std::string s) {
    const auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), isSpace));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), isSpace).base(), s.end());
    return s;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool ParseBool(const std::string& value, bool fallback) {
    const std::string v = Lower(Trim(value));
    if (v == "yes" || v == "true" || v == "1" || v == "on") return true;
    if (v == "no" || v == "false" || v == "0" || v == "off") return false;
    return fallback;
}

template <class T>
void ParseNumeric(const std::string& value, T& out) {
    std::istringstream ss(value);
    ss >> out;
}

void LoadSilverLiningConfig(const std::string& path, SilverLiningConfig& cfg) {
    std::ifstream file(path);
    if (!file) {
        TS2_WARN("World : SilverLining.config absent (\"%s\").", path.c_str());
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        const size_t hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = Trim(line);
        if (line.empty()) continue;

        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;

        std::string key = Lower(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));

        if (key == "default-longitude") ParseNumeric(value, cfg.defaultLongitude);
        else if (key == "default-latitude") ParseNumeric(value, cfg.defaultLatitude);
        else if (key == "default-altitude") ParseNumeric(value, cfg.defaultAltitude);
        else if (key == "default-year") ParseNumeric(value, cfg.defaultYear);
        else if (key == "default-month") ParseNumeric(value, cfg.defaultMonth);
        else if (key == "default-day") ParseNumeric(value, cfg.defaultDay);
        else if (key == "default-hour") ParseNumeric(value, cfg.defaultHour);
        else if (key == "default-minute") ParseNumeric(value, cfg.defaultMinute);
        else if (key == "default-second") ParseNumeric(value, cfg.defaultSecond);
        else if (key == "default-timezone") ParseNumeric(value, cfg.defaultTimezone);
        else if (key == "default-dst") cfg.defaultDst = ParseBool(value, cfg.defaultDst);
        else if (key == "default-turbidity") ParseNumeric(value, cfg.defaultTurbidity);
        else if (key == "disable-tone-mapping") cfg.disableToneMapping = ParseBool(value, cfg.disableToneMapping);
        else if (key == "enable-atmosphere-from-space") cfg.enableAtmosphereFromSpace = ParseBool(value, cfg.enableAtmosphereFromSpace);
        else if (key == "atmosphere-height") ParseNumeric(value, cfg.atmosphereHeight);
        else if (key == "atmosphere-scale-height-meters") ParseNumeric(value, cfg.atmosphereScaleHeightMeters);
        else if (key == "sky-box-gamma") ParseNumeric(value, cfg.skyBoxGamma);
        else if (key == "sky-simple-shader") cfg.skySimpleShader = ParseBool(value, cfg.skySimpleShader);
        else if (key == "sun-width-degrees") ParseNumeric(value, cfg.sunWidthDegrees);
        else if (key == "moon-width-degrees") ParseNumeric(value, cfg.moonWidthDegrees);
        else if (key == "disable-lens-flare") cfg.disableLensFlare = ParseBool(value, cfg.disableLensFlare);
        else if (key == "disable-sun-glare") cfg.disableSunGlare = ParseBool(value, cfg.disableSunGlare);
        else if (key == "disable-moon-glare") cfg.disableMoonGlare = ParseBool(value, cfg.disableMoonGlare);
        else if (key == "disable-star-glare") cfg.disableStarGlare = ParseBool(value, cfg.disableStarGlare);
        else if (key == "enable-precipitation-visibility-effects") cfg.enablePrecipitationVisibilityEffects = ParseBool(value, cfg.enablePrecipitationVisibilityEffects);
        else if (key == "rain-max-particles") ParseNumeric(value, cfg.rainMaxParticles);
        else if (key == "snow-max-particles") ParseNumeric(value, cfg.snowMaxParticles);
        else if (key == "sleet-max-particles") ParseNumeric(value, cfg.sleetMaxParticles);
    }

    TS2_LOG("World : SilverLining.config charge (lat=%.3f lon=%.3f alt=%.1f heure=%02d:%02d turbidity=%.1f atmo=%d)",
            cfg.defaultLatitude, cfg.defaultLongitude, cfg.defaultAltitude,
            cfg.defaultHour, cfg.defaultMinute, cfg.defaultTurbidity,
            cfg.enableAtmosphereFromSpace ? 1 : 0);
}

} // namespace

WorldAssets::WorldAssets(std::string gameDataDir) : gameDataDir_(std::move(gameDataDir)) {
    LoadSilverLiningConfig(gameDataDir_ + "\\G03_GDATA\\D11_ATMOSPHERE\\SilverLining.config", silverLining_);
}
WorldAssets::~WorldAssets() = default;

WorldLoadHooks WorldAssets::MakeHooks() {
    WorldLoadHooks h;
    h.user = this;
    h.allocAtmosphere      = &AllocAtmosphere;
    h.constructAtmosphere  = &ConstructAtmosphere;
    h.deviceBeginMap       = &DeviceBeginMap;
    h.deviceEndMap         = &DeviceEndMap;
    h.atmosphereInitialize = &AtmosphereInitialize;
    h.loadWeatherDat       = &LoadWeatherDat;
    h.setGeoLocation       = &SetGeoLocation;
    h.finishLoad            = &FinishLoad;
    h.freeZoneSound        = &FreeZoneSound;
    h.loadMapFileWG        = &LoadMapFileWG;
    h.loadObjectsWO        = &LoadObjectsWO;
    h.loadObjectsWP        = &LoadObjectsWP;
    h.loadShadowTexture    = &LoadShadowTexture;
    h.loadFaces            = &LoadFaces;
    h.freeFaces            = &FreeFaces;
    h.loadMinimap          = &LoadMinimap;
    h.loadWorldSound       = &LoadWorldSound;
    h.loadWorldBgm         = &LoadWorldBgm;
    h.loadDataFile         = &LoadDataFile;
    h.queryCollisionMesh   = &QueryCollisionMesh; // Gap G02 : relie la maille décodée à WorldMap
    return h;
}

const asset::WorldChunk* WorldAssets::Collision(CollisionSlot slot) const {
    switch (slot) {
    case CollisionSlot::Main:      return wm_.get();
    case CollisionSlot::WJ:        return wj_.get();
    case CollisionSlot::Secondary: return wmSecondary_.get();
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Requêtes de sol / collision terrain (Gaps G02/G03/G04). La maille décodée (Gap G01) d'une
// couche .WM est exposée à WorldMap via le hook queryCollisionMesh, et directement ici via
// des méthodes qui délèguent au moteur ts2::world::collision:: (portage byte-fidèle des
// MapColl_*, cf. World/WorldMap.cpp). Toutes build-safe (false si la couche .WM est absente).
// ---------------------------------------------------------------------------
const asset::CollisionMesh* WorldAssets::MainCollisionMesh() const {
    const asset::WorldChunk* c = Collision(CollisionSlot::Main); // wm_ (.WM = collision pure)
    if (!c) return nullptr;
    const asset::MapCollisionChunk* mc = c->AsCollision();       // .WM -> MapCollisionChunk
    return mc ? &mc->mesh : nullptr;
}
// Hook WorldLoadHooks::queryCollisionMesh — appelé par WorldMap après un loadFaces réussi.
const asset::CollisionMesh* WorldAssets::QueryCollisionMesh(void* user, CollisionSlot slot) {
    auto* self = static_cast<WorldAssets*>(user);
    const asset::WorldChunk* c = self->Collision(slot);
    if (!c) return nullptr;
    const asset::MapCollisionChunk* mc = c->AsCollision(); // couches .WM/.WJ/.WM2 (les .WG = AsFace)
    return mc ? &mc->mesh : nullptr;
}

bool WorldAssets::GetGroundHeight(float x, float z, float probeCeilingY, float& outGroundY) const {
    const asset::CollisionMesh* m = MainCollisionMesh();
    if (!m) return false;
    // MapColl_GetGroundHeight 0x697130 forme consommateur (a5=1, a6=probeCeilingY, a7=0, a8=1).
    return collision::GetGroundHeight(*m, x, z, outGroundY, true, probeCeilingY, false, true);
}
bool WorldAssets::HasGroundAt(float x, float z) const {
    const asset::CollisionMesh* m = MainCollisionMesh();
    if (!m) return false;
    float out = 0.0f; // IsGroundBlocked-shape : plafond défaut (racine bboxMax.y), a8=1
    return collision::GetGroundHeight(*m, x, z, out, false, 0.0f, false, true);
}
bool WorldAssets::IsPointOnGround(float x, float y, float z) const {
    const asset::CollisionMesh* m = MainCollisionMesh();
    if (!m) return false;
    return collision::IsPointOnGround(*m, x, y, z); // World_IsPointOnGround 0x540d40
}
bool WorldAssets::PointInMeshXZ(float x, float z) const {
    const asset::CollisionMesh* m = MainCollisionMesh();
    if (!m) return false;
    return collision::PointInMeshXZ(*m, x, z); // MapColl_PointInMeshXZ 0x695dc0
}
bool WorldAssets::Raycast(const float start[3], const float dir[3], uint32_t& outFaceIndex,
                          float outHit[3], bool twoSide) const {
    const asset::CollisionMesh* m = MainCollisionMesh();
    if (!m) return false;
    return collision::RaycastNearest(*m, 0, start, dir, outFaceIndex, outHit, twoSide); // 0x6960c0
}
bool WorldAssets::SlideMoveGround(const float from[3], const float to[3], float speed, float dt,
                                  float outPos[3]) const {
    const asset::CollisionMesh* m = MainCollisionMesh();
    if (!m) { outPos[0] = from[0]; outPos[1] = from[1]; outPos[2] = from[2]; return false; }
    return collision::SlideMoveGround(*m, from, to, speed, dt, outPos); // 0x697330
}

// ---------------------------------------------------------------------------
// Atmosphère / météo — SilverLining NON lié à ce projet (SilverLiningDirectX9-MT.dll
// hors périmètre de ClientSource). On échoue PROPREMENT plutôt que de simuler un
// succès : allocAtmosphere renvoie nullptr (=> atmosphere_ reste nul), et
// atmosphereInitialize renvoie 0 (= "pas d'échec bloquant") pour laisser le reste
// du chargement de zone (collision/objets/fx, la partie gameplay) se poursuivre
// sans ciel/météo rendus. Documenté ici plutôt que passé sous silence.
// ---------------------------------------------------------------------------
void* WorldAssets::AllocAtmosphere(void* /*user*/, unsigned /*size*/) {
    return nullptr; // SilverLining non lié -> pas d'objet atmosphère.
}
void* WorldAssets::ConstructAtmosphere(void* /*user*/, void* /*mem*/, const char*, const char*) {
    return nullptr; // jamais appelé tant qu'AllocAtmosphere renvoie nullptr.
}
void WorldAssets::DeviceBeginMap(void* /*user*/, void* /*device*/) {}
void WorldAssets::DeviceEndMap(void* /*user*/, void* /*device*/) {}
int  WorldAssets::AtmosphereInitialize(void* /*user*/, void* /*atmosphere*/,
                                       const char* /*mapName*/, void* /*device*/) {
    return 0; // "pas d'échec" -> WorldMap::LoadMap continue (valid_=true, sans ciel).
}
bool WorldAssets::LoadWeatherDat(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    const std::string full = self->gameDataDir_ + "\\" + kAtmosphereResourceDir + path;
    asset::AtmosphereFile weather;
    if (weather.Load(full) || weather.Load(path)) {
        self->atmosphere_ = std::move(weather);
        return true;
    }
    return false;
}
void WorldAssets::SetGeoLocation(void* /*user*/, double lat, double lon, double alt) {
    TS2_LOG("World : geoloc par defaut (%.2f, %.2f, %.2f) — Atmosphere.DAT absent.", lat, lon, alt);
}
void WorldAssets::FinishLoad(void* /*user*/) {
    TS2_LOG("World : chargement atmosphere termine (Atmosphere.DAT present).");
}

// ---------------------------------------------------------------------------
// Géométrie de zone — chargeurs RÉELS (Asset/WorldChunk, validés 455/455 fichiers).
// ---------------------------------------------------------------------------
bool WorldAssets::FreeZoneSound(void* user) {
    auto* self = static_cast<WorldAssets*>(user);
    if (self->soundBank_) self->soundBank_.reset();
    return true;
}
// CONFIRMED ex-VeryOldClient: WORLD_FOR_GXD::LoadWG. Ancre IDA : MapColl_LoadMapFile 0x697b30.
// TODO terrain WG (surface non rendue) : chargé + parsé (EOF-exact) mais consommé par AUCUN
// renderer (Faces() sans appelant de dessin) -> voir SPEC TS2_WORLD_ROSETTA.md §3 G05
// (Terrain_Render 0x698670, 2x/frame depuis Scene_InGameRender 0x52d0b0). Landing = WorldGeometryRenderer.
bool WorldAssets::LoadMapFileWG(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    self->wg_ = std::make_unique<asset::WorldChunk>();
    return self->wg_->Load(self->gameDataDir_ + "\\" + path);
}
// CONFIRMED ex-VeryOldClient: LoadWO (mMObject). Ancre IDA : MapColl_LoadObjectsA 0x6980d0.
// Réellement rendu par Gfx/WorldGeometryRenderer (gabarits + auxRecords/placement).
bool WorldAssets::LoadObjectsWO(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    self->wo_ = std::make_unique<asset::WorldChunk>();
    return self->wo_->Load(self->gameDataDir_ + "\\" + path);
}
// CONFIRMED ex-VeryOldClient: LoadWP (mPSystem). Ancre IDA : MapColl_LoadObjectsB 0x6983b0.
// TODO hors-FRONT 4 : parsé (FxNodes()) mais JAMAIS rendu (torches/feux/cascades invisibles) ->
// X01 (FRONT 8), point d'entrée de rendu WP non identifié dans l'IDB, cf. TS2_WORLD_ROSETTA.md §3.Z.
bool WorldAssets::LoadObjectsWP(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    self->wp_ = std::make_unique<asset::WorldChunk>();
    return self->wp_->Load(self->gameDataDir_ + "\\" + path);
}
// CONFIRMED ex-VeryOldClient: mShadowTexture (dispatch case 5). Ancre IDA : Tex_LoadFromFile 0x6a9910.
// PLAUSIBLE (VeryOldClient) — non prouvé IDA : décodage = DDS plain (loader distinct de
// Tex_LoadCompressedDDS 0x6a2e80 des minimaps ; interne de 0x6a9910 non décompilé cette passe).
// TODO terrain SHADOW : texture chargée dans shadow_ mais JAMAIS appliquée (lightmap stage 1 / uv1)
// -> voir SPEC TS2_WORLD_ROSETTA.md §3 G06 (dépend du rendu terrain G05).
bool WorldAssets::LoadShadowTexture(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    self->shadow_ = std::make_unique<asset::Texture>();
    return self->shadow_->LoadDDS(self->gameDataDir_ + "\\" + path);
}
// CONFIRMED ex-VeryOldClient: WORLD_FOR_GXD::LoadWM (WM1/2/3 -> mRANGE1/2/3). Ancre IDA :
// MapColl_LoadFaces 0x694510. TODO terrain WM (hauteur de sol) : le chunk est chargé (framing GXD
// byte-exact) mais son `CollisionMesh.raw` n'est JAMAIS décodé typé ni requêté -> sol nul partout.
// Voir SPEC TS2_WORLD_ROSETTA.md §3 : G01 (décoder raw -> CollisionTri 156o [matIndex@0, plan@124]
// + QuadNode 48o), puis G02 = porter MapColl_GetGroundHeight 0x697130 (descente quadtree XZ +
// plane-solve y=(d - x*a - z*c)/b + barycentrique MapColl_RayHitTriangle 0x695ae0). Landing = World/.
bool WorldAssets::LoadFaces(void* user, CollisionSlot slot, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    auto chunk = std::make_unique<asset::WorldChunk>();
    const bool ok = chunk->Load(self->gameDataDir_ + "\\" + path);
    switch (slot) {
    case CollisionSlot::Main:      self->wm_          = std::move(chunk); break;
    case CollisionSlot::WJ:        self->wj_          = std::move(chunk); break;
    case CollisionSlot::Secondary: self->wmSecondary_ = std::move(chunk); break;
    }
    return ok;
}
void WorldAssets::FreeFaces(void* user, CollisionSlot slot) {
    auto* self = static_cast<WorldAssets*>(user);
    switch (slot) {
    case CollisionSlot::Main:      self->wm_.reset();          break;
    case CollisionSlot::WJ:        self->wj_.reset();          break;
    case CollisionSlot::Secondary: self->wmSecondary_.reset(); break;
    }
}
bool WorldAssets::LoadMinimap(void* user, int index, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    if (index < 1 || index > 3) return false;
    auto& slot = self->minimaps_[static_cast<size_t>(index - 1)];
    slot = std::make_unique<asset::Texture>();
    // Enveloppe GXD .IMG détectée (pixels non décodés : cTexture_LoadFromImgFile
    // 0x457A20 hors périmètre de ce parseur, cf. Asset/Texture.h).
    return slot->LoadFile(self->gameDataDir_ + "\\" + path);
}

// ---------------------------------------------------------------------------
// Audio de zone — conteneur .WSOUND parsé (byte-exact), mais le PCM final exige
// un décodeur Ogg Vorbis (libvorbis/libogg) NON lié à ce projet : le chargement
// s'arrête proprement à "conteneur valide, PCM indisponible" plutôt que de
// simuler un son. AudioSystem::LoadPcm renvoie false sans callback enregistré.
// ---------------------------------------------------------------------------
bool WorldAssets::LoadWorldSound(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    self->wsound_ = std::make_unique<asset::WSound>();
    if (!self->wsound_->Load(self->gameDataDir_ + "\\" + path)) return false;

    std::vector<std::string> soundPaths;
    soundPaths.reserve(self->wsound_->Count());
    for (uint32_t i = 1; i <= self->wsound_->Count(); ++i)
        soundPaths.push_back(self->wsound_->OggPathFor(i));

    std::vector<audio::SoundBank::BankEmitter> emitters;
    emitters.reserve(self->wsound_->Emitters().size());
    for (const auto& e : self->wsound_->Emitters())
        emitters.push_back({ e.soundIndex, { e.x, e.y, e.z }, e.radius });

    self->soundBank_ = std::make_unique<audio::SoundBank>();
    // Ne fait PAS échouer LoadZoneResource si le PCM ne peut être décodé : le
    // conteneur (métadonnées + émetteurs) est correctement chargé quoi qu'il arrive
    // (cf. limitation Ogg documentée ci-dessus).
    self->soundBank_->Load(soundPaths, emitters);
    return true;
}
bool WorldAssets::LoadWorldBgm(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    (void)self;
    audio::SoundBuffer bgm; // décodage Ogg indisponible (cf. note ci-dessus) -> échec propre attendu.
    const bool ok = bgm.LoadFromPath(self->gameDataDir_ + "\\" + path, audio::PlayMode::Loop, 1);
    if (!ok) TS2_WARN("World : BGM \"%s\" — conteneur trouve mais decodeur Ogg absent.", path);
    return ok;
}

// .ATM par zone (World_LoadZoneResource case 7, seul appelant de ce hook — cf. World/
// WorldMap.cpp::LoadZoneResource, ResourceKind::Atmosphere, chemin "Z%03d.ATM" avec le
// zoneId BRUT). Contrairement à AVANT (2026-07-14), le contenu est RÉELLEMENT parsé (cf.
// Asset/AtmosphereFile.h, byte-exact, validé 89/89 fichiers réels) plutôt que juste vérifié
// non-vide : c'est la donnée RÉELLE consommée ensuite par Gfx/SkyRenderer.h pour dériver le
// gradient jour/nuit de l'heure/position géo réelles de la zone active.
bool WorldAssets::LoadDataFile(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    if (self->atmosphere_.Load(self->gameDataDir_ + "\\" + path)) return true;
    return self->atmosphere_.Load(path); // repli : path déjà complet (même politique qu'avant)
}

} // namespace ts2::world

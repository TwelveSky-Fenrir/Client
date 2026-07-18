// World/WorldIntegration.cpp — implementation of the WorldLoadHooks hooks.
#include "World/WorldIntegration.h"
#include "Asset/WorldChunk.h"
#include "Asset/Texture.h"
#include "Asset/Sound.h"
#include "Asset/FileUtil.h"
#include "Audio/Sound3D.h"
#include "Audio/AudioSystem.h"
#include "Gfx/GpuTexture.h" // BEW-01 : GPU upload of minimaps (full type required here : ~WorldAssets)
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
    h.queryCollisionMesh   = &QueryCollisionMesh; // Gap G02 : links the decoded mesh to WorldMap
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

// Ground / terrain collision queries (Gaps G02/G03/G04). The decoded mesh (Gap G01) of a
// .WM layer is exposed to WorldMap via the queryCollisionMesh hook, and directly here via
// methods that delegate to the ts2::world::collision:: engine (byte-faithful port of the
// MapColl_*, cf. World/WorldMap.cpp). All build-safe (false if the .WM layer is absent).
// Decoded collision mesh of a .WM/.WJ/.WM2 layer (AsCollision). The .WG are AsFace (cf.
// TerrainMesh). Single source — MainCollisionMesh/QueryCollisionMesh delegate to it.
const asset::CollisionMesh* WorldAssets::CollisionMeshOf(CollisionSlot slot) const {
    const asset::WorldChunk* c = Collision(slot);
    if (!c) return nullptr;
    const asset::MapCollisionChunk* mc = c->AsCollision(); // .WM/.WJ/.WM2 -> MapCollisionChunk
    return mc ? &mc->mesh : nullptr;
}
const asset::CollisionMesh* WorldAssets::MainCollisionMesh() const {
    return CollisionMeshOf(CollisionSlot::Main); // wm_ (.WM = pure collision)
}
// Hook WorldLoadHooks::queryCollisionMesh — called by WorldMap after a successful loadFaces.
const asset::CollisionMesh* WorldAssets::QueryCollisionMesh(void* user, CollisionSlot slot) {
    return static_cast<WorldAssets*>(user)->CollisionMeshOf(slot);
}

bool WorldAssets::GetGroundHeight(float x, float z, float probeCeilingY, float& outGroundY) const {
    const asset::CollisionMesh* m = MainCollisionMesh();
    if (!m) return false;
    // MapColl_GetGroundHeight 0x697130 consumer shape (a5=1, a6=probeCeilingY, a7=0, a8=1).
    return collision::GetGroundHeight(*m, x, z, outGroundY, true, probeCeilingY, false, true);
}
bool WorldAssets::HasGroundAt(float x, float z) const {
    const asset::CollisionMesh* m = MainCollisionMesh();
    if (!m) return false;
    float out = 0.0f; // IsGroundBlocked-shape : default ceiling (root bboxMax.y), a8=1
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

// Wave B4 — ground plane for the planar shadow (F_ENTITY3D). Model_RenderPlanarShadow 0x40f720 picks
// the walkable layer filtered by materialIndex==1 (SegNodeNearestA @0x421128) : ClientSource side =
// the decoded mesh of the Main layer (.WM). See front report for Main vs .WG.
bool WorldAssets::GetGroundPlaneForShadow(const float modelPos[3], float modelHeight,
                                          const float lightDir[3], float maxDist,
                                          collision::GroundPlane& out) const {
    const asset::CollisionMesh* m = MainCollisionMesh();
    if (!m) { out.valid = false; return false; }
    return collision::GetGroundPlaneForShadow(*m, modelPos, modelHeight, lightDir, maxDist, out); // 0x40f720
}
bool WorldAssets::GetGroundPlaneUnder(float x, float z, collision::GroundPlane& out) const {
    const asset::CollisionMesh* m = MainCollisionMesh();
    if (!m) { out.valid = false; return false; }
    return collision::GetGroundPlaneUnder(*m, x, z, out); // 0x697130 (descent + vertical ground plane)
}

// WG-02 — CAMERA collision (Camera_UpdateCollision 0x538580). Each oracle targets a
// DIFFERENT slot (proven): terrain sweep = .WG (slot 0 = g_GameWorld @0x5387b9) ; blocked point =
// Main+WJ (0x540da0) ; ground (HasGroundAt above) = Main (@0x5388f4). Consumed by the
// Gfx/CameraThirdPersonBridge::WorldCameraCollision oracle (game::ICameraCollisionQueries).
const asset::CollisionMesh* WorldAssets::TerrainMesh() const {
    // .WG = slot 0 (g_GameWorld itself). Its mesh is a MapFaceChunk (AsFace), NOT a
    // MapCollisionChunk. Anchor: Camera_UpdateCollision @0x5387b9 (mov ecx, offset g_GameWorld)
    // -> Terrain_SweepSphereSegment 0x69a1f0 on this[35]=quadtree of the .WG (dword_14A88C8).
    if (!wg_) return nullptr;
    const asset::MapFaceChunk* fc = wg_->AsFace();
    return fc ? &fc->mesh : nullptr;
}
bool WorldAssets::SweepCameraSegment(const float from[3], const float to[3], float radius,
                                     float outHit[3]) const {
    const asset::CollisionMesh* m = TerrainMesh(); // .WG (0x69a1f0 / g_GameWorld @0x5387b9)
    if (!m) return false;
    return collision::SweepSphereSegment(*m, from, to, radius, outHit);
}
bool WorldAssets::IsPointBlocked(const float p[3]) const {
    const asset::CollisionMesh* main = CollisionMeshOf(CollisionSlot::Main); // &dword_14A88E4
    if (!main) return true; // 0x540de1 : no Main mesh -> no ground -> blocked (faithful)
    const asset::CollisionMesh* wj = CollisionMeshOf(CollisionSlot::WJ);     // &dword_14A898C
    return collision::IsPointBlocked(*main, wj, p); // 0x540da0
}
bool WorldAssets::LineOfSightBlockedByObjects(const float /*from*/[3], const float /*to*/[3]) const {
    // MapColl_LineOfSightObjects 0x696fc0 — NOT ported this wave (PROVEN blocker): the real test
    // intersects the segment with the PER-FRAME OBB of each placed .WO object (Model_TransformVertsPick
    // 0x6a3e00 : OBB table @part+284, 64-byte blocks indexed by frame). That table lives in the
    // undecoded GXD geometry blob (asset::WorldMeshPart::geo, Asset/WorldChunk.h:149-157) and the
    // frame index (rec+28 = Math_RandRangeFloat @0x69835d) is runtime-only, absent from AuxRecord
    // (Asset/WorldChunk.h:193-198). Asset files not owned by this front.
    // TODO [MapColl_LineOfSightObjects 0x696fc0 / Model_TransformVertsPick 0x6a3e00].
    return false;
}

// WG-03 — Screen->terrain picking (Terrain_PickRayScreen 0x699a80). Delegates to the
// requested layer (Main @0x536715 / WJ @0x540fc4). The game::ITerrainPicker implementer
// (Game/SkillCombat.h:238, out of scope) derives the ScreenPickCamera from gfx::Camera.
bool WorldAssets::PickRayScreen(CollisionSlot slot, const collision::ScreenPickCamera& cam,
                                int sx, int sy, uint32_t& outFaceIndex, float outHit[3],
                                bool twoSide) const {
    const asset::CollisionMesh* m = CollisionMeshOf(slot);
    if (!m) return false;
    return collision::PickRayScreen(*m, cam, sx, sy, outFaceIndex, outHit, twoSide); // 0x699a80
}

// GX-ICON-01 — Zone minimap : exposure of the 3 textures + world bounds. The textures are
// loaded by LoadMinimap (case 8/9/10) but were never exposed (private member, no
// accessor) -> minimap with no background. UI-side production remains out of scope.
const asset::Texture* WorldAssets::Minimap(int index) const {
    if (index < 0 || index > 2) return nullptr; // 0=_MINIMAP01/1=_MINIMAP02/2=_MINIMAP03
    return minimaps_[static_cast<size_t>(index)].get(); // world+2092/+2132/+2172, stride 40 @0x681aab
}
// BEW-01 : D3D9 surface of minimap `index` = field +36 of the target object (Tex_LoadCompressedDDS
// 0x6A2E80 @0x6A3040 : D3DXCreateTextureFromFileInMemoryEx(..., this+9)). D3DPOOL_MANAGED on the
// gfx::GpuTexture side -> survives a device reset, no recreation to wire up.
IDirect3DTexture9* WorldAssets::MinimapTexture(int index) const {
    if (index < 0 || index > 2) return nullptr;
    const gfx::GpuTexture* t = minimapGpu_[static_cast<size_t>(index)].get();
    return (t && t->Valid()) ? t->Handle() : nullptr;
}
// BEW-01 : var_868/var_864 of UI_GameHud_Render (@0x681560/@0x68157B) = fields +4/+8 of the
// texture object of index `mode` = GXD header +0/+4 (`qmemcpy(this+1, header, 0x1C)` @0x6A2FFE) =
// LOGICAL dims. DO NOT use asset::Texture::width/height (= physical DDS surface, NextPow2).
bool WorldAssets::MinimapLogicalSize(int index, int& outW, int& outH) const {
    const asset::Texture* t = Minimap(index);
    if (!t || t->imgLogicalWidth == 0 || t->imgLogicalHeight == 0) return false;
    outW = static_cast<int>(t->imgLogicalWidth);   // texture+4  @0x681560 (dword_14A906C[0x28*mode])
    outH = static_cast<int>(t->imgLogicalHeight);  // texture+8  @0x68157B (dword_14A9070[0x28*mode])
    return true;
}
WorldAssets::MinimapBounds WorldAssets::MinimapWorldBounds() const {
    MinimapBounds b{0.0f, 0.0f, 0.0f, 0.0f, false};
    const asset::CollisionMesh* m = TerrainMesh(); // dword_14A88C8 = .WG quadtree (g_GameWorld+140)
    if (!m || m->nodes.empty()) return b;
    const asset::CollisionQuadNode& root = m->nodes[0]; // UI_GameHud_Render @0x681513..@0x68154b
    b.minX    =  root.bboxMin[0]; // @0x681519 (+0)
    b.maxX    =  root.bboxMax[0]; // @0x681527 (+12)
    b.negMaxZ = -root.bboxMax[2]; // @0x681535 (+20, fchs)
    b.negMinZ = -root.bboxMin[2]; // @0x681546 (+8,  fchs)
    b.valid   = true;
    return b;
}

// Atmosphere / weather — SilverLining is NOT linked into this project (SilverLiningDirectX9-MT.dll
// out of scope for ClientSource). We fail CLEANLY rather than simulate a
// success: allocAtmosphere returns nullptr (=> atmosphere_ stays null), and
// atmosphereInitialize returns 0 (= "no blocking failure") so the rest
// of zone loading (collision/objects/fx, the gameplay part) can proceed
// without a rendered sky/weather. Documented here rather than left silent.
void* WorldAssets::AllocAtmosphere(void* /*user*/, unsigned /*size*/) {
    return nullptr; // SilverLining not linked -> no atmosphere object.
}
void* WorldAssets::ConstructAtmosphere(void* /*user*/, void* /*mem*/, const char*, const char*) {
    return nullptr; // never called as long as AllocAtmosphere returns nullptr.
}
void WorldAssets::DeviceBeginMap(void* /*user*/, void* /*device*/) {}
void WorldAssets::DeviceEndMap(void* /*user*/, void* /*device*/) {}
int  WorldAssets::AtmosphereInitialize(void* /*user*/, void* /*atmosphere*/,
                                       const char* /*mapName*/, void* /*device*/) {
    return 0; // "no failure" -> WorldMap::LoadMap continues (valid_=true, without sky).
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

// Zone geometry — REAL loaders (Asset/WorldChunk, validated 455/455 files).
bool WorldAssets::FreeZoneSound(void* user) {
    auto* self = static_cast<WorldAssets*>(user);
    if (self->soundBank_) self->soundBank_.reset();
    return true;
}
// CONFIRMED ex-VeryOldClient: WORLD_FOR_GXD::LoadWG. IDA anchor: MapColl_LoadMapFile 0x697b30.
// ACTUALLY rendered (FRONT W3-F3): Faces() is consumed by Gfx/WorldGeometryRenderer::buildTerrain()
// (FF FVF 530 path, Terrain_Render 0x698670, 2x/frame from Scene_InGameRender 0x52d0b0). The
// terrain/water category comes from textures[m].trailer[0/1] (Tex_LoadCompressedFromHandle 0x6a9cf0).
bool WorldAssets::LoadMapFileWG(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    self->wg_ = std::make_unique<asset::WorldChunk>();
    return self->wg_->Load(self->gameDataDir_ + "\\" + path);
}
// CONFIRMED ex-VeryOldClient: LoadWO (mMObject). IDA anchor: MapColl_LoadObjectsA 0x6980d0.
// Actually rendered by Gfx/WorldGeometryRenderer (templates + auxRecords/placement).
bool WorldAssets::LoadObjectsWO(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    self->wo_ = std::make_unique<asset::WorldChunk>();
    return self->wo_->Load(self->gameDataDir_ + "\\" + path);
}
// CONFIRMED ex-VeryOldClient: LoadWP (mPSystem). IDA anchor: MapColl_LoadObjectsB 0x6983b0.
// ACTUALLY rendered (FRONT W3-F3): FxNodes() is consumed by Gfx/WorldGeometryRenderer::buildFx()
// + RenderFxBillboards(). The .WP render entry point IS Terrain_Render a5=2 @0x698c6d
// (Gfx_BeginUnlitPass 0x69e470 -> Particle_RenderBillboards 0x6a70b0) — the "unidentified X01" was WRONG.
bool WorldAssets::LoadObjectsWP(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    self->wp_ = std::make_unique<asset::WorldChunk>();
    return self->wp_->Load(self->gameDataDir_ + "\\" + path);
}
// CONFIRMED ex-VeryOldClient: mShadowTexture (dispatch case 5). IDA anchor: Tex_LoadFromFile 0x6a9910.
// PLAUSIBLE (VeryOldClient) — not IDA-proven: decoding = plain DDS (loader distinct from
// Tex_LoadCompressedDDS 0x6a2e80 of the minimaps ; internals of 0x6a9910 not decompiled this pass).
// NOW APPLIED (FRONT W3-F3): the lightmap is bound to stage 1 (uv1, MODULATE) by the FF
// path of Gfx/WorldGeometryRenderer (Terrain_Render @0x698f54/@0x698f68). Raw DDS bytes are kept
// here (shadowBytes_) so the renderer can create the GPU texture without including Asset/Texture.h.
bool WorldAssets::LoadShadowTexture(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    const std::string full = self->gameDataDir_ + "\\" + path;
    self->shadow_ = std::make_unique<asset::Texture>();
    self->shadowBytes_.clear();
    asset::ReadWholeFile(full, self->shadowBytes_); // raw DDS bytes for the renderer (best-effort)
    return self->shadow_->LoadDDS(full);
}
// CONFIRMED ex-VeryOldClient: WORLD_FOR_GXD::LoadWM (WM1/2/3 -> mRANGE1/2/3). IDA anchor:
// MapColl_LoadFaces 0x694510. RESOLVED (Gaps G01/G02/G04, collision campaign) — the old comment
// "raw, never decoded -> ground null everywhere" is OUTDATED : the .WM chunk is now DECODED, TYPED, by
// asset::WorldChunk::ParseCollisionMesh (WorldChunk.cpp, CollisionTri 156B [matIndex@0, plane@124..136]
// + QuadNode 48B) and QUERIED by the ported MapColl_* (MapColl_GetGroundHeight 0x697130 = XZ quadtree
// descent + plane-solve y=(d - x*a - z*c)/b + barycentric MapColl_RayHitTriangle 0x695ae0, in
// World/WorldMap.cpp ; SegPick for the planar shadow Collision_SegPickA 0x420D60 in World/CollisionMesh.cpp).
// MainCollisionMesh() returns this decoded mesh, consumed by GetGroundPlaneForShadow / GetGroundHeight.
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
    const size_t i = static_cast<size_t>(index - 1);
    auto& slot = self->minimaps_[i];
    slot = std::make_unique<asset::Texture>();
    self->minimapGpu_[i].reset(); // zone reload : the old surface is dropped (world+2092+40*i overwritten)
    // GXD .IMG family T wrapper: Texture::LoadFile MATERIALIZES the pixels (DXT blocks) via
    // ImgFile + LoadFromImgFile — format PROVEN identical to Tex_LoadCompressedDDS 0x6A2E80
    // (36-byte GXD header + embedded DDS, cf. @0x6A2FFE `qmemcpy(this+1, header, 0x1C)` / @0x6A3040
    // D3DXCreateTextureFromFileInMemoryEx).
    if (!slot->LoadFile(self->gameDataDir_ + "\\" + path)) return false;

    // BEW-01 : the binary does NOT stop at decoding — @0x6A3040 it creates the D3D9 texture and
    // stores it in the object (+36), an object that UI_GameHud_Render then blits @0x681AB1. We
    // reproduce that upload here: the texture belongs to the WORLD (world+2092+40*index), not the HUD.
    // Without a device (SetDevice not called), we stay CPU-only and the minimap degrades cleanly
    // to its flat color — never a crash.
    if (!self->device_) {
        TS2_WARN("World : minimap %d decodee mais device absent (SetDevice non appele) "
                 "-> mini-carte sans fond.", index);
        return true; // decoding succeeded : case 8/9/10 does return 1 (faithful @0x4DD2xx)
    }
    // CreateFromTexture (CreateTexture + LockRect on the already-decoded blocks) rather than
    // CreateFromImgFile (exact D3DX replica) : `slot` already carries the blocks, no need to reread
    // the file. Benign accepted gap: the target passes mipLevels=1 to D3DXCreateTextureFromFileInMemoryEx
    // (@0x6A3040) whereas CreateFromTexture uploads all DDS levels — ID3DXSprite only reads
    // level 0, the blit @0x681AB1 is identical.
    auto gpu = std::make_unique<gfx::GpuTexture>();
    if (!gpu->CreateFromTexture(self->device_, *slot)) {
        TS2_WARN("World : upload GPU de la minimap %d echoue (format %d non supporte).",
                 index, static_cast<int>(slot->format));
        return true; // same : an upload failure does not fail the zone load
    }
    self->minimapGpu_[i] = std::move(gpu);
    TS2_LOG("World : minimap %d prete (logique %ux%u, surface %ux%u).", index,
            slot->imgLogicalWidth, slot->imgLogicalHeight, slot->width, slot->height);
    return true;
}

// Zone audio — .WSOUND container parsed (byte-exact) AND now actually decoded.
// NOTE (AUD-05) : the previous banner claimed "Ogg decoding unavailable / Ogg decoder
// absent, clean failure expected". That is OUTDATED : Audio/OggVorbisDecoder.cpp exists and
// AudioSystem::Init wires it in (AudioSystem.cpp:300-301 `if (!HasLoadCallback())
// SetLoadCallback(&OggVorbisLoadCallback);`), libvorbis being actually linked. PCM is therefore
// genuinely produced — hence the severity of the old LoadWorldBgm bug: it decoded EVERYTHING then
// discarded it. The "silent, no crash" fallback remains if DirectSound is unavailable or the file is
// missing.
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
    // Does NOT fail LoadZoneResource if a sound can't be opened: the container
    // (metadata + emitters) is loaded correctly regardless.
    self->soundBank_->Load(soundPaths, emitters);
    // AUD-03 : this bank is the counterpart of dword_14A90E0. It stays MUTE as long as
    // WSndBank_UpdatePositional 0x4DAC30 is not called per frame (@0x5321EC, at the head of
    // Player_UpdateLocalAnim 0x5321D0) — tick ported in Game/AnimationTick.*, OUTSIDE this front.
    // Provider exposed here : WorldAssets::SoundBank() (cf. .h). Wiring : see front report.
    return true;
}
// AUD-05 — World_LoadZoneResource 0x4DCB60 case 12 @0x4DD43E :
//   Snd_LoadOggToBuffers(ecx = g_GameWorld + 0x8BC, "G03_GDATA\D10_WORLDBGM\Z%03d.BGM", 1, 1, 1)
// with g_GameWorld = 0x14A883C -> ecx = 0x14A90F8 = the world's PERSISTENT slot. Two fixes
// proven against the previous state:
//   1. the SoundBuffer was AUTOMATIC (stack) -> ~SoundBuffer() on `return` destroyed the
//      just-decoded IDirectSoundBuffer. It is now a member (`worldBgm_`), the exact counterpart
//      of g_GameWorld+2236 : the slot survives, as in the target.
//   2. the mode was PlayMode::Loop (=kind 2) whereas the pushes @0x4DD425-0x4DD429 give
//      a3 = kind = 1 = ONE-SHOT single (cf. the IDA comment on Snd_LoadOggToBuffers 0x6A8120 :
//      "1=one-shot single, 2=loop single, 3=pool"). PlayMode::OneShot (Audio/AudioSystem.h:85).
// Case 12 only LOADS: playback is a separate site (PlayWorldBgm, cf. .h).
bool WorldAssets::LoadWorldBgm(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    self->worldBgm_ = std::make_unique<audio::SoundBuffer>(); // slot g_GameWorld+2236 (0x14A90F8)
    const bool ok = self->worldBgm_->LoadFromPath(self->gameDataDir_ + "\\" + path,
                                                  audio::PlayMode::OneShot, 1); // kind=1 @0x4DD429
    if (!ok) {
        // .BGM absent / DirectSound unavailable -> silent, never a crash (the binary doesn't
        // fail the zone either: case 12 falls back to def_4DCBA4 @0x4DD443).
        TS2_WARN("World : BGM \"%s\" indisponible (fichier absent ou audio non initialise).", path);
        self->worldBgm_.reset();
    }
    return ok;
}
// Player_ResetCombatState : @0x50F75A `cmp ds:g_BgmEnabled, 1` / @0x50F761 `jnz` ->
// @0x50F769 `mov ecx, offset dword_14A90F8` (= g_GameWorld+0x8BC, the slot loaded above) ->
// @0x50F76E `call Snd_Play3D` with pushes 0 / 0x64 / 0 = (pan=0, vol=100, a2=0).
void WorldAssets::PlayWorldBgm(bool bgmEnabled) {
    if (!bgmEnabled) return;   // gate g_BgmEnabled 0x84DEF0 @0x50F75A
    if (!worldBgm_) return;    // slot not loaded -> no-op (Snd_Play3D exits on !loaded)
    worldBgm_->Play(100, 0);   // vol = 0x64 @0x50F765, pan = 0 @0x50F763
}
// Snd_ReleaseBuffers 0x6A80D0 on the world slot.
void WorldAssets::ReleaseWorldBgm() {
    if (worldBgm_) worldBgm_->Release();
    worldBgm_.reset();
}

// Per-zone .ATM (World_LoadZoneResource case 7, sole caller of this hook — cf. World/
// WorldMap.cpp::LoadZoneResource, ResourceKind::Atmosphere, "Z%03d.ATM" path with the
// RAW zoneId). Unlike BEFORE (2026-07-14), the content is ACTUALLY parsed (cf.
// Asset/AtmosphereFile.h, byte-exact, validated 89/89 real files) rather than just checked
// non-empty: it is the REAL data then consumed by Gfx/SkyRenderer.h to derive the
// day/night gradient from the real hour/geo position of the active zone.
bool WorldAssets::LoadDataFile(void* user, const char* path) {
    auto* self = static_cast<WorldAssets*>(user);
    if (self->atmosphere_.Load(self->gameDataDir_ + "\\" + path)) return true;
    return self->atmosphere_.Load(path); // fallback : path already complete (same policy as before)
}

} // namespace ts2::world

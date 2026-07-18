// World/WorldMap.h — TwelveSky2 client world/zone loader (faithful rewrite).
//
// Byte-exact reverse of the four functions of the "world" subsystem:
//   World_LoadMap             0x4116b0 — "ALT1" DRM gate + atmosphere init + Atmosphere.DAT weather
//   World_LoadDataFile        0x4118f0 — parses a text config file (.ATM) via ifstream
//   World_LoadZoneResource    0x4dcb60 — dispatches per-zone resource loading (WG/WO/WP/WM/...)
//   World_LoadCurrentZoneModel 0x4dd6e0 — (re)loads the .WM model of the current layer
// Helpers:
//   World_ZoneIdToFileId      0x4db0f0 — zoneId -> fileId table (~340 entries)
//   cAtmosphere_ctor          0x791b40 — DRM gate: SilverLining_ValidateLicense("ALT1 License 3", hex key)
//
// MODULE LEAF: does NOT include any heavy project header (neither Asset nor Gfx). All real I/O
// (loading files/models/textures, D3D device calls, license validation) is delegated to hooks
// (WorldLoadHooks). We reproduce here the SEQUENCE, the DRM GATE, the file PATHS, the zone
// IDENTIFIERS and the FLAGS found in the disassembly.
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// MODULE LEAF: keep the header light. The typed terrain views (Gaps G4/G5/G7) are
// defined in Asset/WorldChunk.h; here we only FORWARD-DECLARE (no Asset include).
// WorldMap.cpp includes Asset/WorldChunk.h to define the accessors.
namespace ts2::asset {
struct CollisionMesh;
struct CollisionFace;
struct CollisionQuadNode;
struct TerrainVertex;
} // namespace ts2::asset

namespace ts2::world {

// ---------------------------------------------------------------------------
// DRM gate constants (World_LoadMap 0x4116b0 -> cAtmosphere_ctor 0x791b40).
// The protected "ALT1" map: the atmosphere constructor calls
// SilverLining_ValidateLicense(name, hexKey) with these TWO CONSTANT strings
// (hardcoded in the binary, independent of the map name).
// ---------------------------------------------------------------------------
inline constexpr char kAltLicenseName[] = "ALT1 License 3";              // aAlt1License3 0x7ec940
inline constexpr char kAltLicenseKey[]  = "113e355254250a02094e32165441"; // a113e355254250a 0x7ec920

// Default atmosphere resource passed to World_LoadMap from case 7.
inline constexpr char kAtmosphereResourceDir[] = "G03_GDATA\\D11_ATMOSPHERE\\"; // 0x7a7db8

// Default SilverLining resource directory (cAtmosphere_ctor 0x791c1f).
inline constexpr char kSilverLiningResourceDir[] = ".\\Resources\\"; // aResources 0x7ebbf4

// Default geolocation if Atmosphere.DAT is absent (Env_SetGeoLocation 0x411d30): Seoul.
// PLAUSIBLE (VeryOldClient n/a): these kDefaultGeo* ARE the faithful runtime authority
// (37.6/127.0/0.0, proven by Env_SetGeoLocation 0x411d30) — do NOT confuse with the stock SDK
// defaults of SilverLiningConfig (WorldIntegration.h, lon -122/lat 30), which are only a parse fallback.
inline constexpr double kDefaultGeoLat = 37.6;   // latitude
inline constexpr double kDefaultGeoLon = 127.0;  // longitude
inline constexpr double kDefaultGeoAlt = 0.0;    // altitude

// Weather file name read by World_LoadMap (aAtmosphereDat 0x7ec950).
inline constexpr char kAtmosphereDatFile[] = "Atmosphere.DAT";

// ---------------------------------------------------------------------------
// Resource types of World_LoadZoneResource (0x4dcb60), values = the a3 parameter.
// ---------------------------------------------------------------------------
// The a3 dispatch == resource index faithfully reproduces the VeryOldClient dispatch
// `mZONE.Load(zone, idx)` (indicator only — values 1..12 are proven by IDA, cf.
// Docs/TS2_WORLD_ROSETTA.md §1.B/1.C/1.D, each case below carries its IDA anchor).
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
// Target collision slot (offsets found in the world object `this`).
// World_LoadZoneResource case 6 and World_LoadCurrentZoneModel write into
// one of these three MapColl sub-objects.
// ---------------------------------------------------------------------------
// CONFLICT C-01 (see Docs/TS2_WORLD_ROSETTA.md §2): VeryOldClient (WORLD_FOR_GXD) only knows
// .WM layers (WM1/2/3 -> mRANGE1/2/3), NOT .WJ. IDA distinguishes .WM(+0xA8) + .WJ(+0x150)
// + .WM2(+0x1F8) — build-diff, IDA WINS (World_LoadZoneResource 0x4dcb60 case 6, 3 MapColl slots,
// stride 42 dwords). Do NOT backport the absence of WJ. The 3 offsets below ARE the IDA anchor.
enum class CollisionSlot : int {
    Main      = 0,  // this+0xA8  (168)  — main collision (.WM), also reloaded by LoadCurrentZoneModel
    WJ        = 1,  // this+0x150 (336)  — secondary collision (.WJ) — absent from VeryOldClient (CONFLICT C-01)
    Secondary = 2,  // this+0x1F8 (504)  — secondary .WM (zones 50/52/170: Z170_2.WM)
};

// ---------------------------------------------------------------------------
// Hooks: all real I/O is delegated to the host. Each callback returns the
// boolean (truncated to a byte in the binary) of the original loader.
// `user` = opaque host context (copied through as-is).
// ---------------------------------------------------------------------------
struct WorldLoadHooks {
    void* user = nullptr;

    // --- World_LoadMap 0x4116b0 / cAtmosphere_ctor 0x791b40 (DRM gate) ---
    // Crt_OperatorNew(648): allocates the cAtmosphere object (648 bytes). nullptr = failure.
    void* (*allocAtmosphere)(void* user, unsigned size) = nullptr;
    // cAtmosphere_ctor: constructs the atmosphere and VALIDATES the SilverLining license
    // (SilverLining_ValidateLicense 0x795db0). Returns the constructed object pointer.
    void* (*constructAtmosphere)(void* user, void* mem, const char* licenseName,
                                 const char* licenseKey) = nullptr;
    // (*device)[+164] / (*device)[+168]: D3D device bracket around the atmosphere init.
    void  (*deviceBeginMap)(void* user, void* device) = nullptr;
    void  (*deviceEndMap)(void* user, void* device)   = nullptr;
    // cAtmosphere_Initialize 0x793390: init(1, mapName, 0, device). Returns 0 = SUCCESS
    // (the binary takes the "failure/return 0" branch when this return is nonzero).
    int   (*atmosphereInitialize)(void* user, void* atmosphere, const char* mapName,
                                  void* device) = nullptr;
    // Opens+parses Atmosphere.DAT (ifstream + Istream_GetChar loop). true if present/valid.
    bool  (*loadWeatherDat)(void* user, const char* path) = nullptr;
    // Env_SetGeoLocation 0x411d30 (called if Atmosphere.DAT is absent/empty).
    void  (*setGeoLocation)(void* user, double lat, double lon, double alt) = nullptr;
    // World_FinishLoad 0x411c40 (finalization after a successful parse).
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
    // World_LoadDataFile 0x4118f0 (.ATM text file) — used by case 7.
    bool (*loadDataFile)(void* user, const char* path) = nullptr;

    // --- Terrain query wiring (Gap G02) ---
    // After a successful loadFaces of a layer, WorldMap retrieves the DECODED collision
    // mesh (asset::CollisionMesh, Gap G01) to answer ground queries. Optional (guarded).
    // IDA ref: the runtime MapColl IS the `this` of MapColl_GetGroundHeight 0x697130
    // (faces this[22], quadtree this[35]); here we bind the data already decoded by the host.
    const asset::CollisionMesh* (*queryCollisionMesh)(void* user, CollisionSlot slot) = nullptr;
};

// ---------------------------------------------------------------------------
// WorldMap — state + logic of the four loaders. Represents the world object
// (the binary's `this+N` fields) without owning the resources: those live
// on the host side, reached via the hooks.
// ---------------------------------------------------------------------------
class WorldMap {
public:
    explicit WorldMap(const WorldLoadHooks& hooks) : hooks_(hooks) {}

    // Current D3D device (this+12) — g_GfxRenderer_pDevice 0x800074 in case 7.
    void SetDevice(void* device) { device_ = device; }

    // Current zone id used by LoadCurrentZoneModel (g_SelfMorphNpcId 0x1675a98).
    void SetCurrentZoneId(int zoneId) { currentZoneId_ = zoneId; }
    int  CurrentZoneId() const { return currentZoneId_; }

    // World_LoadMap 0x4116b0.
    //   `mapName` -> a2 (passed to cAtmosphere_Initialize; e.g. kAtmosphereResourceDir).
    //   `drmKey`  -> DRM gate hex key (kAltLicenseKey constant in the binary).
    //   The device is taken from device_ (SetDevice). Returns true (=1) on success.
    bool LoadMap(const std::string& mapName, const std::string& drmKey = kAltLicenseKey);

    // World_LoadZoneResource 0x4dcb60. Returns the original return byte (LOBYTE(v3)).
    unsigned char LoadZoneResource(int zoneId, ResourceKind kind);

    // World_LoadCurrentZoneModel 0x4dd6e0. `mode` = a2 (layer/state index).
    // Returns -1 if the zone is invalid, 0 if no model needs reloading, else loadFaces' return.
    int LoadCurrentZoneModel(int mode);

    // World_ZoneIdToFileId 0x4db0f0: zoneId -> fileId (Z%03d.*). -1 if unknown.
    static int ZoneIdToFileId(int zoneId);

    // -----------------------------------------------------------------------
    // TYPED / consumable terrain data (Gaps G4/G5/G7). The decoded collision mesh
    // (asset::CollisionMesh, produced by Asset/WorldChunk while parsing .WM/.WG) is owned
    // by the host via the loadFaces hook; WorldMap references it WITHOUT owning it.
    // The wiring (the integration layer calls SetCollisionMesh after a loadFaces of the
    // main layer) lives in WorldIntegration (NOT owned here) — hence the TODO below.
    // As long as no mesh is bound, the accessors return empty vectors (build-safe, no
    // dereference). IDA ref: MapColl_LoadFaces 0x694510 / MapColl_GetGroundHeight 0x697130 /
    // Terrain_Render 0x698670.
    // TODO(integration): wire SetCollisionMesh(slot=Main) after loadFaces (G02/G05).
    void SetCollisionMesh(const asset::CollisionMesh* mesh) { collisionMesh_ = mesh; }
    const asset::CollisionMesh* CollisionMeshData() const { return collisionMesh_; }

    // Collision/render faces (typed 156B, Gap G4). MapColl_LoadFaces 0x694510.
    const std::vector<asset::CollisionFace>& Faces() const;
    // Quadtree nodes (typed 48B, Gap G5). MapColl_GetGroundHeight 0x697130: root = index 0,
    // leaf <=> child[0]==-1; trisIndex = offset into FaceIndices().
    const std::vector<asset::CollisionQuadNode>& Quadtree() const;
    // Flattened terrain vertices (40B, FVF 530, Gap G7). Terrain_Render 0x698670 (VB upload).
    const std::vector<asset::TerrainVertex>& Vertices() const;
    // Aggregated face-index buffer (quadtree leaves -> Faces()). Backs ground queries (G02).
    const std::vector<uint32_t>& FaceIndices() const;

    // -----------------------------------------------------------------------
    // Ground/terrain collision queries (Gaps G02/G03/G04). Byte-faithful port of the
    // MapColl_* functions (see `namespace collision` at the bottom of this header) on the
    // bound main mesh (collisionMesh_, wired by the queryCollisionMesh hook after a loadFaces
    // of the Main .WM layer). Build-safe: return false/no-op as long as no mesh is bound.
    // These are the PROVIDERS that the consolidation agent (G03) wires to the out-of-scope
    // consumer hooks: host.GetGroundHeight (Game/EntityLifecycleTick.h:199), IsPointOnGround
    // (Game/AnimationTick.h:95), IsGroundBlocked (ICameraCollisionQueries AnimationTick.h:190).
    // -----------------------------------------------------------------------
    // MapColl_GetGroundHeight 0x697130, consumer shape (a5=1,a6=probeCeilingY,a7=0,a8=1,
    // cf. Char_Update 0x581e10 / World_IsPointOnGround 0x540d40): walkable ground height
    // under (x,z), capped at probeCeilingY. true if found (outGroundY filled).
    bool GetGroundHeight(float x, float z, float probeCeilingY, float& outGroundY) const;
    // IsGroundBlocked-shape (AnimationTick.h:190): MapColl_GetGroundHeight(x,z,&out,0,0.0,0,1)!=0.
    bool HasGroundAt(float x, float z) const;
    // World_IsPointOnGround 0x540d40: is (x,y,z) above a walkable ground (ceiling=y+20)?
    bool IsPointOnGround(float x, float y, float z) const;
    // MapColl_RaycastNearest 0x6960c0: 1st face impact along the ray (start + t*dir, t>=0).
    // outFaceIndex/outHit = nearest impact; twoSide accepts faces from both sides (a7).
    bool Raycast(const float start[3], const float dir[3], uint32_t& outFaceIndex,
                 float outHit[3], bool twoSide = false) const;
    // MapColl_SlideMoveGround 0x697330: slides (from->to) pinned to the mesh (step = speed*dt),
    // then resolves ground height. outPos = final position {x,y,z} (always filled).
    bool SlideMoveGround(const float from[3], const float to[3], float speed, float dt,
                         float outPos[3]) const;

    // --- Fields captured from the world object ---
    // EW-02 (Pass 4/W11): in the TARGET, `valid_` (this+4) and the case-7 flag (byte_18C67C8) are
    // the SAME byte g_WorldEnv+4 — World_LoadMap @0x41176E sets it, World_UnloadMap 0x411A80
    // @0x411A9F clears it every zone entry (step 1, before step 7 rereads it @0x4DD202/@0x4DD217).
    // The port keeps `valid_` as the literal @0x41176E write (unobservable) and `atmosphereLoaded_`
    // as the step-7 read value, always false here, so LoadMap replays 1x/zone, faithfully — the
    // "C-03 fix" that armed `atmosphereLoaded_` for 1x/session was a reverted REGRESSION (see
    // WorldMap.cpp success branch).
    bool  valid_       = false;   // this+4  (set to 1 by World_LoadMap on success @0x41176E) — write-only here
    void* atmosphere_  = nullptr; // this+8  (cAtmosphere object; always null in ClientSource)
    void* device_      = nullptr; // this+12 (D3D device)

    // Flag dword_1686134: zone-291 variant selection in LoadZoneResource case 6
    // (0 -> Z291_1.WM, else -> Z291_2.WM). In LoadCurrentZoneModel, zone 291 is instead
    // driven by `mode` (see CurrentZoneModelPath).
    int flagZ291Variant = 0;      // dword_1686134

    // Flag byte_18C67C8 (g_WorldEnvAtmInitFlag): atmosphere already loaded, read by case 7
    // (@0x4DD202 -> @0x4DD217 `jnz` skips World_LoadMap if armed). Always 0 at step 7 in the
    // target (World_UnloadMap 0x411A80 cleared it at step 1) -> left false here so World_LoadMap
    // replays every zone (EW-02). Do NOT arm it (C-03 trap) or port UnloadMap (harmful no-op,
    // atmosphere_ always null, cf. WorldMap.cpp).
    bool atmosphereLoaded_ = false; // byte_18C67C8 (0x18C67C8), read @0x4DD202; left at 0 (EW-02)

    // Path templates (as found verbatim in .rdata).
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

    // Main .WM path (case 6) for a fileId. `z291Variant` = flagZ291Variant.
    // For 50/52/170, returns the primary Z170_1.WM (the secondary Z170_2.WM loads separately).
    static std::string ZoneModelPathWM(int fileId, int z291Variant);
    // Secondary .WJ path (case 6) for a fileId.
    static std::string ZoneModelPathWJ(int fileId);
    // Current-layer .WM path (LoadCurrentZoneModel) by fileId + mode.
    // Empty string => no resource to load (the binary compares to "" and skips).
    static std::string CurrentZoneModelPath(int fileId, int mode);

private:
    WorldLoadHooks hooks_;
    int  currentZoneId_ = 0;
    // this+180: SAVE of the global sun D3DLIGHT9 dword_18C5358 (EW-05). 0x68 = 104 =
    // sizeof(D3DLIGHT9); this is NOT "weather config zeroed out". In the target: World_LoadMap
    // @0x4117A2 qmemcpy(this+180, &dword_18C5358, 0x68) = global->instance (save);
    // World_UnloadMap @0x411AB6 qmemcpy(&dword_18C5358, this+180, 0x68) = instance->global
    // (restore). The global is the directional D3DLIGHT9 built every frame by
    // Env_UpdateSunLight 0x412210 (memset 0x68 @0x41227B, Type=3 @0x412329, SetLight vtbl+204
    // @0x412367; cf. Gfx/SilverLiningSky.cpp:217-222). Not modeled (no reader of weather_;
    // UnloadMap not ported, EW-02) -> stays write-only, the fill(0) is unobservable.
    std::array<uint8_t, 104> weather_{};
    // Bound typed collision mesh (Gaps G4/G5/G7). NON-owning (held by the host).
    const asset::CollisionMesh* collisionMesh_ = nullptr;
};

// ===========================================================================
// namespace collision — terrain query engine (Gaps G02/G03/G04). Byte-faithful port of
// TwelveSky2's MapColl_* functions, operating directly on the DECODED mesh
// (asset::CollisionMesh, Gap G01 already in place). The runtime MapColl IS the
// `this` of these functions; correspondence (proven by the cited EAs):
//   this[1]  (active flag)  -> loaded mesh (tris + nodes non-empty)
//   this[22] (faces base)   -> mesh.tris[]        (stride 156; plane a/b/c/d @+124/+128/+132/+136)
//   this[35] (quadtree base)-> mesh.nodes[]       (48B; root = index 0; leaf <=> child[0]==-1)
//   leaf.trisIndex + i      -> mesh.triIndices[node.trisIndex + i] -> tris[faceIdx]
// Pure, stateless functions, build-safe (guards on empty mesh / out-of-bounds index).
// Per-function IDA ref in WorldMap.cpp.
// ===========================================================================
namespace collision {

// MapColl_RayHitTriangle 0x695ae0 — 3D barycentric containment of point {px,py,pz} within the face.
bool RayHitTriangle(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                    float px, float py, float pz);
// MapColl_PointInTriangleXZ 0x695c70 — barycentric containment of point (px,pz) in the XZ plane.
bool PointInTriangleXZ(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                       float px, float pz);
// MapColl_GetGroundHeight 0x697130 — ground height under (x,z) ("null ground" filled in).
//   a5CeilingGiven/a6Ceiling: explicit ceiling (else = root bboxMax.y);
//   a7TwoSide: accept downward-facing faces (skip the walkable filter planeB>0);
//   a8OnlyOne: return the 1st hit (else keep the highest). true => outGroundY filled.
bool GetGroundHeight(const asset::CollisionMesh& mesh, float x, float z,
                     float& outGroundY, bool a5CeilingGiven, float a6Ceiling,
                     bool a7TwoSide, bool a8OnlyOne);
// World_IsPointOnGround 0x540d40 — (x,y,z) above a walkable ground (ceiling=y+20; a5=a8=1).
bool IsPointOnGround(const asset::CollisionMesh& mesh, float x, float y, float z);
// MapColl_PointInMeshXZ 0x695dc0 — does (x,z) fall inside a leaf face (no filter)?
bool PointInMeshXZ(const asset::CollisionMesh& mesh, float x, float z);
// MapColl_RayPlaneTriHit 0x695ee0 — intersects the ray (start + t*dir, t>=0) with the face plane
// then tests 3D containment; outHit filled if hit.
bool RayPlaneTriHit(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                    const float start[3], const float dir[3], float outHit[3], bool twoSide);
// Collide_SegmentAABB 0x69fb20 — SAT test segment(point,dir) vs AABB [bmin,bmax].
bool SegmentAABB(const float p[3], const float dir[3],
                 const float bmin[3], const float bmax[3]);
// MapColl_RaycastNearest 0x6960c0 — nearest face impact along (start + t*dir),
// recursive quadtree descent from nodeIndex (0 = root). outFaceIndex/outHit = nearest.
bool RaycastNearest(const asset::CollisionMesh& mesh, uint32_t nodeIndex,
                    const float start[3], const float dir[3],
                    uint32_t& outFaceIndex, float outHit[3], bool twoSide);
// MapColl_SlideMoveGround 0x697330 — slides (from->to) pinned to the walkable mesh in XZ
// (step = speed*dt) then resolves height; outPos = final {x,y,z}. true if ground found.
bool SlideMoveGround(const asset::CollisionMesh& mesh, const float from[3],
                     const float to[3], float speed, float dt, float outPos[3]);

// ===========================================================================
// WG-02 — CAMERA collision (Camera_UpdateCollision 0x538580). Sphere sweep chain
// against the .WG TERRAIN quadtree (slot 0 = g_GameWorld itself, proven
// @0x5387b9 `mov ecx, offset g_GameWorld`). None of these building blocks existed on the
// C++ side before; these are pure byte-faithful functions (@EA anchor on each def in WorldMap.cpp).
// ===========================================================================
// Collide_AABBOverlap_0 0x6a0600 — AABB overlap (boxA=[amin,amax]) vs (boxB=[bmin,bmax]).
bool AABBOverlap(const float amin[3], const float amax[3],
                 const float bmin[3], const float bmax[3]);
// Collide_ProjectTriOnAxis 0x69f9c0 — projects the triangle's 3 vertices (9 floats v0,v1,v2) onto
// `axis`, outputs [outMin,outMax].
void ProjectTriOnAxis(const float axis[3], const float tri9[9], float& outMin, float& outMax);
// Collide_ProjectBoxOnAxis 0x69fa80 — projects the OBB (center, 3x3 axes matrix, half-extents)
// onto `axis`, outputs [outMin,outMax].
void ProjectBoxOnAxis(const float axis[3], const float center[3], const float axes9[9],
                      const float half[3], float& outMin, float& outMax);
// Collide_TriAABB 0x6a00e0 — SAT test triangle vs AABB (13 axes). The original a1/a2 args
// (g_GfxRenderer, tri int) are scratch/context, NOT used for the geometry.
bool TriAABB(const asset::CollisionFace& face, const float aabbMin[3], const float aabbMax[3]);
// MapColl_SweepSphereNearest 0x696ad0 — thick-ray/sphere sweep (radius) [from->to] vs the
// `nodeIndex` subtree, nearest impact. Recursive (4 children), guard `radius+1 > len`.
bool SweepSphereNearest(const asset::CollisionMesh& mesh, uint32_t nodeIndex,
                        const float from[3], const float to[3], float radius, float outHit[3]);
// Terrain_SweepSphereSegment 0x69a1f0 — descends the root's 4 children, nearest impact.
// `mesh` = the .WG mesh (TerrainMesh()). true if the segment crosses the terrain.
bool SweepSphereSegment(const asset::CollisionMesh& mesh, const float from[3], const float to[3],
                        float radius, float outHit[3]);
// World_IsPointBlocked 0x540da0 — blocked if (a) no Main ground under ceiling p.y+20, OR (b) a
// WJ ground exists above the Main ground. `wj==nullptr` => 2nd test false (documented degradation).
bool IsPointBlocked(const asset::CollisionMesh& main, const asset::CollisionMesh* wj,
                    const float p[3]);

// ===========================================================================
// WG-03 — Screen->terrain picking (Terrain_PickRayScreen 0x699a80). Screen->world ray
// unprojection + quadtree descent. Module LEAF: camera parameters are taken BY VALUE (no
// Gfx/d3dx9 include); the D3DXVec3TransformNormal unprojection is hand-written.
// g_GfxRenderer correspondence (report §0.3): eye = +792; invView (16 floats) = +892;
// proj11 = +728; proj22 = +748; screenW = +648; screenH = +652.
// ===========================================================================
struct ScreenPickCamera {
    float eye[3];        // g_CameraPos/flt_800134/flt_800138 (0x800130..)
    float invView[16];   // unk_800194 = Matrix_Inverse(view) — 4x4 row-major
    float proj11;        // flt_8000F0 = proj._11
    float proj22;        // flt_800104 = proj._22
    int   screenW;       // dword_8000A0
    int   screenH;       // dword_8000A4
};
// Builds the origin (=eye) + direction of the screen ray (W-1/H-1 denominators, z=1 BEFORE
// transform, NOT renormalized after). Anchor 0x699ae7..0x699b40. Always returns true.
bool BuildScreenRay(const ScreenPickCamera& cam, int sx, int sy, float outOrigin[3],
                    float outDir[3]);
// Terrain_PickRayScreen 0x699a80: root AABB gate (Collide_SegmentAABB) then descent of the 4
// children (MapColl_RaycastNearest), nearest impact by Euclidean distance to the eye.
bool PickRayScreen(const asset::CollisionMesh& mesh, const ScreenPickCamera& cam,
                   int sx, int sy, uint32_t& outFaceIndex, float outHit[3], bool twoSide);

} // namespace collision

} // namespace ts2::world

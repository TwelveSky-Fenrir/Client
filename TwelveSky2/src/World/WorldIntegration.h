// World/WorldIntegration.h -- wires WorldMap (leaf, hooks) to the REAL Asset
// (WorldChunk/Texture/Sound) and Audio (SoundBank) loaders, so that
// World_LoadZoneResource actually loads G03_GDATA\D07_GWORLD\Z%03d.* files.
//
// WorldMap (World/WorldMap.h, direct IDA decompilation) owns no resources:
// it exposes hooks. This file is the integration GLUE (hand-written, not
// agent-generated) between this leaf module and the already-validated Asset/Audio layers.
//
// Scope NOT covered here (documented, not pretended):
//   - COMPLETE SilverLining atmosphere/weather (cAtmosphere_ctor 0x791b40, clouds/precipitation/
//     stars/sun/moon): external SDK SilverLiningDirectX9-MT.dll not linked into the project ->
//     LoadMap() fails cleanly (as if the license were absent) rather than faking
//     success. HOWEVER (2026-07-15, mission "WAVE_06_silverlining"): the global
//     SilverLining.config file is loaded once, and the PER-ZONE .ATM file (case 7 of
//     LoadZoneResource, LoadDataFile below) IS actually parsed -- see
//     Asset/AtmosphereFile.h -- then exposed via Atmosphere() for Gfx/SkyRenderer.h. This is an
//     honest subset (geo position + real time + render flags), not the SDK.
//   - D3D rendering of loaded chunks (VB/IB/texture upload): out of scope for the "data
//     world" milestone; to be wired at the Gfx milestone (MeshRenderer) once the
//     camera/scene placement is decided.
#pragma once
#include "World/WorldMap.h"
#include "World/CollisionMesh.h" // Wave B4: collision::GroundPlane + segment-pick / ground-plane chain
#include "Asset/AtmosphereFile.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Global COM forward decl -- avoids including d3d9.h/d3dx9.h here (same idiom as Scene/SceneManager.h:15).
struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace ts2::asset { class WorldChunk; struct Texture; class WSound; }
namespace ts2::audio { class SoundBank; class SoundBuffer; }
namespace ts2::gfx   { class GpuTexture; } // Gfx/GpuTexture.h -- included by the .cpp only

namespace ts2::world {

// Non-destructive subset of SilverLining.config, loaded once per session.
// The original client uses a large number of keys on the SilverLining DLL side; here
// only the base settings needed for the rendering foundation and fallback are kept.
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

// Resources loaded for the CURRENT zone (only one active zone at a time,
// like the original client which reloads/overwrites its chunks on zone change).
class WorldAssets {
public:
    explicit WorldAssets(std::string gameDataDir);
    ~WorldAssets();
    WorldAssets(const WorldAssets&) = delete;
    WorldAssets& operator=(const WorldAssets&) = delete;

    // Builds the hooks bound to this instance (user = this). Pass to WorldMap.
    WorldLoadHooks MakeHooks();

    // D3D9 device used to UPLOAD already-decoded zone textures (today: the 3
    // minimaps, see MinimapTexture below). Must be set BEFORE the first LoadZoneResource
    // (same as WorldMap::SetDevice). Without a device, minimaps stay CPU-only and MinimapTexture()
    // returns nullptr -> the minimap falls back to its flat fill (clean "zone not loaded" degradation).
    void SetDevice(IDirect3DDevice9* dev) { device_ = dev; }
    IDirect3DDevice9* Device() const { return device_; }

    // "GameData" root (contains G03_GDATA\D07_GWORLD, D09_WSOUND, D10_WORLDBGM, D11_ATMOSPHERE).
    const std::string& GameDataDir() const { return gameDataDir_; }

    // Access to loaded chunks (nullptr if absent/not loaded).
    // Collision (.WM/.WJ/.WM2) -- CONFIRMED ex-VeryOldClient: WORLD_FOR_GXD::LoadWM (mRANGE*). IDA
    // anchor: MapColl_LoadFaces 0x694510. TODO WM terrain (ground height): buffer loaded but NEVER
    // type-decoded (CollisionTri 156B + QuadNode 48B) nor queried -> ground is null everywhere. See SPEC
    // TS2_WORLD_ROSETTA.md §3 G01 (decode) + G02 (MapColl_GetGroundHeight 0x697130); consumers
    // G03 (Char_Update 0x581e10 ...). Raycast/sweep/slide queries = G04 (MapColl_RaycastNearest 0x6960c0).
    const asset::WorldChunk* Collision(CollisionSlot slot) const;
    // Terrain .WG -- CONFIRMED ex-VeryOldClient: WORLD_FOR_GXD::LoadWG. IDA anchor: MapColl_LoadMapFile
    // 0x697b30 (actual loader for faces + materials/textures). ACTUALLY rendered by Gfx/
    // WorldGeometryRenderer (fixed-function FVF 530 path, Terrain_Render 0x698670, called BEFORE the
    // .WO by Scene_InGameRender @0x52d9be). The CATEGORY/water comes from textures[m].trailer[0]/trailer[1]
    // (proven by Tex_LoadCompressedFromHandle 0x6a9cf0: mat+40=cat, mat+44=subOrder). G01/G02 collision done.
    const asset::WorldChunk* Faces()   const { return wg_.get(); }
    // Static objects .WO -- CONFIRMED ex-VeryOldClient: LoadWO (mMObject). IDA anchor:
    // MapColl_LoadObjectsA 0x6980d0. ACTUALLY rendered by Gfx/WorldGeometryRenderer (placement OK).
    const asset::WorldChunk* Objects() const { return wo_.get(); }
    // Zone FX .WP -- CONFIRMED ex-VeryOldClient: LoadWP (mPSystem). IDA anchor:
    // MapColl_LoadObjectsB 0x6983b0. ACTUALLY rendered by Gfx/WorldGeometryRenderer::RenderFxBillboards()
    // : the .WP render entry point IS Terrain_Render a5=2 @0x698c6d (Gfx_BeginUnlitPass 0x69e470 ->
    // Particle_RenderBillboards 0x6a70b0) -- the earlier "unidentified entry point" was WRONG.
    const asset::WorldChunk* FxNodes() const { return wp_.get(); }

    // Zone .SHADOW lightmap (full raw DDS file), consumed by
    // Gfx/WorldGeometryRenderer::buildTerrain() -> stage 1 (uv1, MODULATE). Empty if absent/not loaded.
    // IDA anchor: Tex_LoadFromFile 0x6a9910 (loads); Terrain_Render @0x698f68 (binds stage 1).
    const std::vector<uint8_t>& ShadowBytes() const { return shadowBytes_; }
    // Decoded .SHADOW texture object (asset::Texture, see LoadShadowTexture case 5) -- nullptr if absent.
    const asset::Texture* Shadow() const { return shadow_.get(); }

    // REAL atmosphere state of the current zone (Z%03d.ATM file, parsed byte-exact by
    // Asset/AtmosphereFile.h -- case 7 of World_LoadZoneResource). Atmosphere().Valid()==false
    // until a zone has successfully loaded its .ATM (see LoadDataFile below);
    // consumed by Gfx/SkyRenderer.h::SetAtmosphere() for the day/night gradient derived from
    // the zone's real time.
    const asset::AtmosphereFile& Atmosphere() const { return atmosphere_; }
    const SilverLiningConfig& SilverLining() const { return silverLining_; }

    // Ground / terrain collision queries (Gaps G02/G03/G04), on the primary layer (.WM,
    // CollisionSlot::Main). Providers ready to wire to out-of-scope consumer hooks
    // (host.GetGroundHeight Game/EntityLifecycleTick.h:199; IsPointOnGround Game/AnimationTick.h:95;
    // IsGroundBlocked ICameraCollisionQueries AnimationTick.h:190). Delegate to the
    // ts2::world::collision:: engine (World/WorldMap.h), byte-faithful port of MapColl_*. Build-safe:
    // return false / no-op if the .WM layer isn't loaded/decoded. IDA anchors on each line.
    bool GetGroundHeight(float x, float z, float probeCeilingY, float& outGroundY) const; // 0x697130
    bool HasGroundAt(float x, float z) const;                                             // 0x697130 (default ceiling)
    bool IsPointOnGround(float x, float y, float z) const;                                // 0x540d40
    bool PointInMeshXZ(float x, float z) const;                                           // 0x695dc0
    bool Raycast(const float start[3], const float dir[3], uint32_t& outFaceIndex,
                 float outHit[3], bool twoSide = false) const;                            // 0x6960c0
    bool SlideMoveGround(const float from[3], const float to[3], float speed, float dt,
                         float outPos[3]) const;                                          // 0x697330

    // Wave B4 -- GROUND PLANE for the projected planar shadow (F_ENTITY3D). PROVIDER of the ground
    // plane that Model_RenderPlanarShadow 0x40f720 reads then passes to D3DXMatrixShadow. Two paths:
    //   GetGroundPlaneForShadow: binary-faithful path -- SEGMENT pick model+height -> +lightDir
    //     (Collision_SegPickA 0x420d60), filters materialIndex==1, plane extraction + bias -d-0.1.
    //   GetGroundPlaneUnder    : VERTICAL convenience -- plane directly under (x,z) via the
    //     MapColl_GetGroundHeight 0x697130 descent (walkable filter planeB>0). Useful if the light is
    //     ~vertical or for a simple fallback.
    // Operate on the .WM Main layer (walkable, materialIndex==1) -- see the front report for the
    // Main vs .WG choice (the `a8` object of Model_RenderWithShadow 0x40eee0 belongs to the Game layer).
    // Build-safe: return false / out.valid=false if the layer isn't loaded. The light vector to pass
    // to D3DXMatrixShadow ALONGSIDE out.shadowPlane is { -lightDir, 0.0 }.
    bool GetGroundPlaneForShadow(const float modelPos[3], float modelHeight, const float lightDir[3],
                                 float maxDist, collision::GroundPlane& out) const;       // 0x40f720
    bool GetGroundPlaneUnder(float x, float z, collision::GroundPlane& out) const;        // 0x697130

    // WG-02 -- CAMERA collision (Camera_UpdateCollision 0x538580). PROVIDERS for the binary's 4
    // queries (3 distinct slots, proven) that the oracle Gfx/CameraThirdPersonBridge
    // (WorldCameraCollision : game::ICameraCollisionQueries) wires per InGame frame:
    //   SweepCameraSegment -> .WG (slot 0 = g_GameWorld, @0x5387b9); IsPointBlocked -> Main+WJ;
    //   HasGroundAt (already present) -> Main; LineOfSightBlockedByObjects -> .WO objects (TODO).
    // Collision mesh of any layer (.WM/.WJ/.WM2 via AsCollision). nullptr if absent.
    const asset::CollisionMesh* CollisionMeshOf(CollisionSlot slot) const;
    // TERRAIN .WG mesh (slot 0 = g_GameWorld itself) -- a MapFaceChunk (AsFace), NOT a
    // MapCollisionChunk. NEVER queried before this front. Anchor: Terrain_SweepSphereSegment
    // 0x69a1f0 operates on this[35]=quadtree of the .WG (dword_14A88C8).
    const asset::CollisionMesh* TerrainMesh() const;
    // Terrain_SweepSphereSegment 0x69a1f0 -- camera sphere sweep (radius 2.5) against the .WG.
    // false if the zone has no terrain loaded (faithful "zone not loaded" degradation).
    bool SweepCameraSegment(const float from[3], const float to[3], float radius,
                            float outHit[3]) const;                                        // 0x69a1f0
    // World_IsPointBlocked 0x540da0 -- point blocked (no Main ground, or WJ ground above Main).
    bool IsPointBlocked(const float p[3]) const;                                          // 0x540da0
    // MapColl_LineOfSightObjects 0x696fc0 -- NOT ported (proven blocker: per-frame OBB table not
    // decoded in asset::WorldMeshPart::geo). Always returns false -> the "ground-height
    // stepping" fallback of Camera_UpdateCollision is disabled (faithful "objects ignored" degradation).
    bool LineOfSightBlockedByObjects(const float from[3], const float to[3]) const;       // 0x696fc0 (TODO)

    // WG-03 -- Screen->terrain picking (Terrain_PickRayScreen 0x699a80). PROVIDER: the concrete
    // implementer of game::ITerrainPicker (Game/SkillCombat.h:238, skill-learn-cast front / orchestrator)
    // derives a collision::ScreenPickCamera from gfx::Camera + viewport and calls this. The binary's
    // picking targets the .WM(Main @0x536715) / .WJ(@0x540fc4) slots, already decoded/queryable.
    bool PickRayScreen(CollisionSlot slot, const collision::ScreenPickCamera& cam,
                       int sx, int sy, uint32_t& outFaceIndex, float outHit[3],
                       bool twoSide) const;                                                // 0x699a80

    // GX-ICON-01 / BEW-01 -- Zone minimap (3 Tex_LoadCompressedDDS 0x6A2E80 textures). The 3
    // minimaps ARE consumed by the binary (indexed access @0x681AAB, NOT dead); the 0/1/2
    // selection comes from widget+0x268, guarded by widget+0x264==1 (@0x6818B4/@0x6818C7) -- UI state.
    // STATUS: the consumer now EXISTS (UI/MinimapWidget::DrawPanels, 145x128 crop blit) and
    // receives texture+bounds via ui::MinimapWidget::SetSourceProvider -- to be wired in
    // Scene/SceneManager.cpp (see the front report, wiring is not in this file).
    // Minimap texture by index 0..2 (0=_MINIMAP01, 1=_MINIMAP02, 2=_MINIMAP03). nullptr otherwise.
    const asset::Texture* Minimap(int index) const;                                       // 0x6a2e80

    // --- BEW-01: what the UI consumer (UI/MinimapWidget) must actually receive -------
    // GPU texture of minimap `index`, uploaded by LoadMinimap once SetDevice has been called.
    // nullptr if zone/index not loaded or device absent. D3DPOOL_MANAGED (gfx::GpuTexture) -> NO
    // recreation needed on OnDeviceLost/OnDeviceReset. Anchor: Tex_LoadCompressedDDS 0x6A2E80
    // @0x6A3040 (D3DXCreateTextureFromFileInMemoryEx -> object+36 = IDirect3DTexture9*, here Handle()).
    IDirect3DTexture9* MinimapTexture(int index) const;
    // LOGICAL dimensions (GXD header +0/+4) of minimap `index` = EXACTLY var_868/var_864 of
    // UI_GameHud_Render: @0x681560 `mov ecx, ds:dword_14A906C[eax]` and @0x68157B `mov ecx,
    // ds:dword_14A9070[eax]` with eax = 0x28*mode -- but dword_14A906C == unk_14A9068+4 and
    // dword_14A9070 == unk_14A9068+8, i.e. fields +4/+8 of the TEXTURE OBJECT at index `mode`
    // (`qmemcpy(this+1, header, 0x1C)` @0x6A2FFE). These are therefore NOT "per-mode scales":
    // they are the logical image size, distinct from the physical D3D9 NextPow2 surface
    // (Util_NextPow2_GXD @0x6A3040) -> asset::Texture::imgLogicalWidth/Height, NEVER width/height.
    // false if index invalid / not loaded.
    bool MinimapLogicalSize(int index, int& outW, int& outH) const;                        // 0x6a2e80
    // World bounds of the minimap = bbox of the .WG quadtree ROOT (dword_14A88C8 = TerrainMesh()
    // ->nodes[0]). IDA anchor: UI_GameHud_Render @0x681513/@0x681527/@0x681535/@0x681546 (note the
    // TWO negations on Z). self->pixel projection on the UI side (out of scope):
    //   px = (int)( logicalW * ( self.x  - minX)    / (maxX    - minX) )
    //   py = (int)( logicalH * (-self.z  - negMaxZ) / (negMinZ - negMaxZ) )
    struct MinimapBounds { float minX; float maxX; float negMaxZ; float negMinZ; bool valid; };
    MinimapBounds MinimapWorldBounds() const;

    // AUD-03 -- PROVIDER for the positional ambience tick. `soundBank_` (loaded by
    // LoadWorldSound, case 11) is the counterpart of the global bank dword_14A90E0; the binary
    // refreshes it EVERY frame at the TOP of Player_UpdateLocalAnim 0x5321D0:
    //   @0x5321DC  mov eax, ds:g_Opt_MusicVolume   ; 0x84DEE8, option idx10, 0..100 (NOT a boolean)
    //   @0x5321E2  push offset flt_1687330         ; = dword_1687234+0xFC = self position
    //   @0x5321E7  mov ecx, offset dword_14A90E0   ; global bank
    //   @0x5321EC  call WSndBank_UpdatePositional  ; 0x4DAC30  (UNIQUE xref 1/1)
    // Mapping to the existing C++ signature (Audio/Sound3D.h:147): a5 -> (enable = a5 != 0,
    // enableScale = a5). The TICK itself lives in Game/AnimationTick.* (outside this front): this
    // accessor is the prerequisite for wiring it. nullptr until a zone has loaded its .WSOUND.
    audio::SoundBank* SoundBank() const { return soundBank_.get(); }                       // 0x4dac30

    // AUD-05 -- Zone BGM: PERSISTENT slot, exact counterpart of g_GameWorld+2236 (0x14A90F8).
    // World_LoadZoneResource 0x4DCB60 case 12 @0x4DD43E:
    //   Snd_LoadOggToBuffers(this + 0x8BC, "G03_GDATA\D10_WORLDBGM\Z%03d.BGM", 1, 1, 1)
    //   -> ecx = g_GameWorld(0x14A883C) + 0x8BC = 0x14A90F8; a3 = kind = 1 = single ONE-SHOT
    //      (NOT 2=loop: see IDA comment on Snd_LoadOggToBuffers 0x6A8120).
    // The play call is elsewhere -- Player_ResetCombatState @0x50F769 `mov ecx, offset dword_14A90F8`
    // (= the SAME slot) then @0x50F76E `call Snd_Play3D` with vol=0x64=100 / pan=0, guarded by
    // @0x50F75A `cmp ds:g_BgmEnabled, 1` (0x84DEF0). PlayWorldBgm() reproduces this call site.
    //
    // WARNING: MODEL CONFLICT to be arbitrated by the orchestrator (see report): Scene/SceneManager.cpp
    // (LoadZoneBgm/bgm_) ALREADY plays this same Z%03d.BGM via cSceneMgr+612 -- which is, in fact, the
    // menu BGM slot (Scene_ServerSelectUpdate 0x518B30). Wiring PlayWorldBgm() WITHOUT removing
    // LoadZoneBgm would play the track TWICE. The two slots are distinct in the target.
    audio::SoundBuffer* WorldBgm() const { return worldBgm_.get(); }                       // g_GameWorld+2236
    // Player_ResetCombatState @0x50F75A/@0x50F769/@0x50F76E: if (g_BgmEnabled == 1)
    //   Snd_Play3D(&dword_14A90F8, 0, /*vol*/100, /*pan*/0). No-op if the slot isn't loaded.
    void PlayWorldBgm(bool bgmEnabled);                                                    // 0x50f76e
    // Snd_ReleaseBuffers 0x6A80D0 on the world slot (frees the SoundObj at g_GameWorld+2236).
    void ReleaseWorldBgm();                                                                // 0x6a80d0

private:
    // --- Hook implementations (WorldLoadHooks signatures, `user` = this*). ---
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
    // queryCollisionMesh hook (WorldLoadHooks): connects a layer's decoded mesh (Gap G01/G02)
    // to WorldMap for its ground queries. nullptr if the layer isn't loaded.
    static const asset::CollisionMesh* QueryCollisionMesh(void* user, CollisionSlot slot);

    // Decoded collision mesh of the primary layer (.WM Main), nullptr if absent.
    const asset::CollisionMesh* MainCollisionMesh() const;

    std::string gameDataDir_;
    IDirect3DDevice9* device_ = nullptr;  // set by SetDevice (minimap upload, BEW-01)

    std::unique_ptr<asset::WorldChunk> wm_;             // CollisionSlot::Main
    std::unique_ptr<asset::WorldChunk> wj_;             // CollisionSlot::WJ
    std::unique_ptr<asset::WorldChunk> wmSecondary_;    // CollisionSlot::Secondary
    std::unique_ptr<asset::WorldChunk> wg_;
    std::unique_ptr<asset::WorldChunk> wo_;
    std::unique_ptr<asset::WorldChunk> wp_;
    // .SHADOW loaded (case 5) and NOW bound to stage 1 (lightmap on uv1) by the Gfx/
    // WorldGeometryRenderer FF path (IDA anchor: Terrain_Render @0x698f54/@0x698f68). `shadow_` = decoded
    // object (asset::Texture); `shadowBytes_` = raw DDS bytes of the file, provided to the renderer via
    // ShadowBytes() (the renderer creates the GPU texture without depending on Asset/Texture.h).
    std::unique_ptr<asset::Texture>    shadow_;
    std::vector<uint8_t>               shadowBytes_;
    // The 3 zone minimaps = world+2092/+2132/+2172 (unk_14A9068, stride 40 @0x681AAB).
    //   `minimaps_`   = decoded CPU object (logical dims + DXT blocks) -- asset::Texture.
    //   `minimapGpu_` = corresponding D3D9 surface (field +36 of the target object, @0x6A3040),
    //                   uploaded by LoadMinimap if SetDevice has been called. BEW-01.
    std::array<std::unique_ptr<asset::Texture>, 3> minimaps_;
    std::array<std::unique_ptr<gfx::GpuTexture>, 3> minimapGpu_; // incomplete type here: ~WorldAssets is
                                                                 // defined in the .cpp (GpuTexture complete)
    std::unique_ptr<asset::WSound>     wsound_;
    std::unique_ptr<audio::SoundBank>  soundBank_;
    // AUD-05: zone's PERSISTENT BGM slot = g_GameWorld+2236 (0x14A90F8), loaded by
    // LoadWorldBgm (case 12 @0x4DD43E). Before this front, a stack AUTOMATIC SoundBuffer was
    // decoded then destroyed on `return` -- pure wasted work.
    std::unique_ptr<audio::SoundBuffer> worldBgm_;
    asset::AtmosphereFile              atmosphere_; // current zone's Z%03d.ATM (case 7)
    SilverLiningConfig                 silverLining_; // SilverLining.config (global, session)
};

} // namespace ts2::world

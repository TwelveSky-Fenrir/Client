// Gfx/SkyRenderer.h — FIRST REAL sky/atmosphere rendering step, derived from REAL .ATM
// data (Asset/AtmosphereFile.h), NOT a reimplementation of the SilverLining SDK.
//
// === What this module DOES ===
//   Draws a full-screen vertical gradient (zenith at top -> horizon at bottom), whose TWO
//   colors are computed from the REAL time of day (AtmDateTime::HourOfDay, field +0x1D of
//   the active zone's .ATM file — cf. Asset/AtmosphereFile.h) via a simple day/night
//   keyframe model (midnight/dawn/noon/dusk, cf. .cpp), NOT a fixed invented color. Also
//   honors the real file's @+642 flag (RenderFlagForceBlackSky): if set, the sky renders
//   opaque black, exactly as the original engine would for this case (override, independent
//   of the Perez computation).
//   Drawn at the right point in the frame (before the scenery, then again after the
//   entities) with depth-test active, to occupy the same time window as the two real
//   SilverLining calls described in Docs/TS2_SILVERLINING_PIPELINE.md, but in a minimal
//   and conservative form.
//
// === What this module does NOT do (honest, not simulated) ===
//   - NO clouds (stratus/cirrus/cumulus), NO precipitation (rain/snow/hail), NO stars, NO
//     sun/moon drawn (disc, halo, lens flare), NO atmosphere-from-space. The real
//     SilverLining SDK (~150 `cAtmosphere_*`/`SL_*`/`Sky_*` functions statically linked into
//     TwelveSky2.exe + backend `SilverLiningDirectX9-MT.dll`) remains out of scope — it's a
//     complete commercial engine, not reimplementable in one mission.
//   - NO real ephemeris computation: the sun/moon position is NOT computed (no
//     azimuth/elevation), only the HOUR (+ the "black sky" flag) drives the gradient. The
//     .ATM file's latitude/longitude are read (cf. AtmosphereFile) but not used by the
//     current color model — exposed here for diagnostics for a future mission that might
//     want to use them (e.g. real solar elevation computation).
//   - NO Perez atmospheric scattering, NO volumetric fog — cf. Env_UpdateFogState
//     (0x412370) on the original binary side, not reproduced.
//   - The .ATM file's color/scalar keyframe and celestial-body tracks (AtmosphereFile::
//     ColorKeyframes/Bodies/ScalarKeyframes) are IGNORED by this module (exposed raw by the
//     parser but never read here): the day/night gradient is an independent model, not a
//     reading of these tracks (whose exact semantics remain an RE TODO, cf. AtmosphereFile.h).
#pragma once
#include "Gfx/Renderer.h"
#include <cstdint>

namespace ts2::asset { class AtmosphereFile; }
namespace ts2::world { struct SilverLiningConfig; }

namespace ts2::gfx {

class SkyRenderer {
public:
    bool Init(Renderer& renderer);
    void Shutdown();

    // Loads the SilverLining.config globals relevant to the rendering baseline.
    void ApplyConfig(const world::SilverLiningConfig& cfg);

    // Recomputes the derived colors from an actually-parsed AtmosphereFile (called on
    // zone load/change, cf. World/WorldIntegration.h::WorldAssets::Atmosphere()).
    // atm.Valid()==false (missing/corrupt file) -> documented fallback to "neutral noon" (the
    // midpoint of the SAME day/night model, not a distinct arbitrary tint) and
    // HasRealAtmosphere()==false so the caller can distinguish/log it.
    void SetAtmosphere(const asset::AtmosphereFile& atm);

    // Draws the sky. See banner above for its place in the frame (very first, before world
    // geometry). No effect if Init() did not succeed.
    // Faithful Env_RenderSkyCube 0x6a8f60 fallback if a sky cube is fed (HasSkyCube()),
    // otherwise a full-screen PLACEHOLDER gradient (cf. day/night model section in the .cpp).
    void Render(int screenW, int screenH);

    // --- 6-face skybox cube (faithful Env_RenderSkyCube 0x6a8f60 fallback) --------------------
    // These setters model the runtime state read by Env_RenderSkyCube (renderer globals + zone
    // sky object), NOT owned by this front-end. PROVEN ANCHOR (Cam_SetProjection 0x69cbef):
    //   - skyRadius_ = far plane = g_GfxRenderer+136 (param a3 stored @0x69cbd5 -> flt_7FFEA0
    //     0x7FFEA0); cube side = far/sqrt(3) (@0x6a8f9b). Static image value = 0 (runtime cache).
    //   - fogActive_ = g_GfxRenderer+140 = fog-enable (read @0x69cc06 -> dword_7FFEA4 0x7FFEA4).
    //   - camPos_ = g_CameraPos 0x800130 (=g_GfxRenderer+792), world translation @0x6a936e.
    //   - faceTex_ = zone sky object's face descriptors (this+13, 52-byte stride, 1st dword =
    //     IDirect3DTexture9*). WARNING: NOT STATICALLY PROVABLE SOURCE (cf. .cpp + TS2_SKY_ROSETTA T-12):
    //     in this EU build, Env_RenderSkyCube is DEAD CODE (a2==NULL at all 8 Gfx_BeginFrame
    //     0x6a2280 call sites) -> the sky object is never constructed -> no loader feeds the 6 textures.
    //     As long as faceTex_ isn't supplied by its owner -> HasSkyCube()==false -> gradient.
    // faceTex_ NOT owned (raw ptr): the owner (zone/asset) MUST re-supply them after a
    //   device-lost/reset; SkyRenderer owns NO D3DPOOL_DEFAULT resource (cube = DrawPrimitiveUP).
    void SetSkyCubeTextures(IDirect3DTexture9* const faces[6]); // zone sky object (52-byte face desc., this+13)
    void SetSkyRadius(float r);          // r = far plane (flt_7FFEA0 = g_GfxRenderer+136, Cam_SetProjection 0x69cbef a3)
    void SetCameraPosition(float x, float y, float z); // g_CameraPos 0x800130 = g_GfxRenderer+792
    void SetFogActive(bool on);          // dword_7FFEA4 = g_GfxRenderer+140 (fog-enable, Cam_SetProjection 0x69cbef +140)

    // Layer-1 activation entry point (wiring B3): pushes in one call the runtime state that
    // Gfx_BeginFrame 0x6a2280 feeds to Env_RenderSkyCube (far plane -> radius, camera eye, fog).
    // Does NOT activate the cube alone — without SetSkyCubeTextures(), HasSkyCube() stays false -> gradient.
    // To be called by the scene just before Render(), mirroring Gfx_BeginFrame.
    void UpdateSkyRuntime(float farPlane, const float camPos[3], bool fogEnabled);

    bool HasSkyCube() const;             // true if skyRadius_>0 && >=1 face texture

    // --- Diagnostics (tests, sanity logs) --------------------------------------------
    bool     HasRealAtmosphere() const { return hasRealAtmosphere_; }
    double   HourOfDay()         const { return hourOfDay_; }
    double   Latitude()          const { return latitude_; }
    double   Longitude()         const { return longitude_; }
    bool     ForcedBlackSky()    const { return forceBlackSky_; }
    uint32_t ZenithColorArgb()   const { return zenithColor_; }
    uint32_t HorizonColorArgb()  const { return horizonColor_; }

private:
    void recomputeColors();

    // Sky cube: rebuilds the 24 vertices if the radius changed (cache this+210), then
    // draws the 6 faces. Anchors Env_RenderSkyCube 0x6a8f60 (detail in the .cpp).
    void rebuildCubeVertices();
    void renderCube();

    // Snapshot of the GLOBAL SilverLining.config settings (read once per session by
    // cAtmosphere_Initialize 0x793390); PER-ZONE data (hour/lat/lon/flags) arrives separately
    // via SetAtmosphere() (.ATM, Atmosphere_Deserialize 0x795A40). cf. Docs/TS2_SILVERLINING_CONFIG.md §2.
    // The default values below = stock SDK defaults, NOT ported from VeryOldClient (different build).
    struct ConfigSnapshot {
        double defaultLongitude = -122.064840;
        double defaultLatitude  = 30.0;
        double defaultAltitude  = 100.0;
        int defaultYear   = 2006;
        int defaultMonth  = 8;
        int defaultDay    = 15;
        int defaultHour   = 12;
        int defaultMinute = 0;
        double defaultSecond  = 0.0;
        double defaultTimezone = -8.0;
        bool defaultDst = true;
        double defaultTurbidity = 2.2;   // config "default-turbidity" @ cAtmosphere_Initialize 0x793390 (code default 2.0, shipped file 2.2)
        bool disableToneMapping = false; // config "disable-tone-mapping" (global) / cf. g_SL_ForceToneMapping 0x18C4DEC
        bool enableAtmosphereFromSpace = false; // config "enable-atmosphere-from-space" @ 0x793390 (default true, file "no")
        double atmosphereHeight = 100000.0;     // config "atmosphere-height" @ cAtmosphere_Initialize 0x793390
                                                // CONFIRMED @0x7935eb: *v20 = 100000.0 then Cfg_GetDouble(100000.0, "atmosphere-height", v20)
                                                //   -> real default if the key is absent = 100000.0 (corrected from 300000.0; ex-D-1/T-13). Field not
                                                //   consumed by the gradient (cosmetic) but aligned with the IDA truth.
        // "atmosphere-scale-height-meters" RE-READ PER FRAME @ cAtmosphere_RenderFrame 0x793B80 (Cfg_GetDouble -> dbl_18636E8).
        // PLAUSIBLE (VeryOldClient) — not IDA-proven (8435.0 = Earth's atmospheric pressure scale height, never read as a literal in the IDB).
        // ex-VeryOldClient: CAtmosphere::SetSceneFog H = 8435.0 ("Pressure scale height of Earth's atmosphere")
        double atmosphereScaleHeightMeters = 8435.0;
    };

    IDirect3DDevice9* dev_ = nullptr;
    bool   ready_ = false;

    bool   hasRealAtmosphere_ = false; // true only if SetAtmosphere() received a Valid() .ATM
    double hourOfDay_    = 12.0;       // decimal hour [0,24) — "noon" fallback until an .ATM arrives
    double latitude_     = 0.0;        // exposed, not used by the color model (cf. banner)
    double longitude_    = 0.0;
    bool   forceBlackSky_ = false;     // zone's AtmosphereFile::RenderFlagForceBlackSky()

    ConfigSnapshot config_;
    uint32_t zenithColor_  = 0xFF335C99; // recomputed by recomputeColors() on each Init()/SetAtmosphere()
    uint32_t horizonColor_ = 0xFF9CB8DE;

    // --- Sky cube runtime state (Env_RenderSkyCube 0x6a8f60) ----------------------
    // Default values => inactive cube => PLACEHOLDER gradient (no regression).
    struct SkyCubeVertex { float x, y, z, u, v; }; // 20 bytes, FVF D3DFVF_XYZ|D3DFVF_TEX1 (0x102) @0x6a934f

    IDirect3DTexture9* faceTex_[6] = { nullptr };  // zone sky object this+13 (52-byte face desc., 1st dword = texture)
    float  skyRadius_   = 0.0f;   // flt_7FFEA0 = g_GfxRenderer+136 = far plane (Cam_SetProjection 0x69cbef, a3 @0x69cbd5). Static image value = 0 (runtime cache).
    float  camPos_[3]   = { 0.0f, 0.0f, 0.0f }; // g_CameraPos 0x800130 = g_GfxRenderer+792 (world translation @0x6a936e)
    bool   fogActive_   = false;  // dword_7FFEA4 = g_GfxRenderer+140 = fog-enable (Cam_SetProjection 0x69cbef +140 @0x69cc06 ; disabled around the cube @0x6a933c)
    float  cubeCacheRadius_ = -1.0f; // this+210 (byte 0x348): rebuild cache @0x6a8f88/@0x6a92c6
    SkyCubeVertex cubeVerts_[24];    // this+211 (6 faces x 4 vertices)
};

} // namespace ts2::gfx

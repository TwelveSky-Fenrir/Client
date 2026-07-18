// Gfx/SkyRenderer.cpp — see SkyRenderer.h for the full banner (what's derived from real
// .ATM data vs. simplified/absent). Drawing = full-screen XYZRHW two-color quad (same D3D
// settings as the old Gfx/WorldGeometryRenderer.cpp::drawSkyGradientFallback fallback,
// whose draw logic this file takes over/replaces — the NOVELTY here is the color
// computation, derived from the REAL .ATM time of day instead of two hardcoded fixed tints).
#include "Gfx/SkyRenderer.h"
#include "Asset/AtmosphereFile.h"
#include "World/WorldIntegration.h"
#include "Core/Log.h"

#include <algorithm>
#include <cmath>
#include <iterator>

#pragma comment(lib, "d3d9.lib")

namespace ts2::gfx {

namespace {

struct ScreenGradientVertex {
    float x, y, z, rhw;
    D3DCOLOR color;
};
constexpr DWORD kScreenGradientFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;

// ---------------------------------------------------------------------------------------
// SIMPLE hourly-keyframe day/night model (NOT a real ephemeris, cf. .h banner). Two separate
// tables (zenith / horizon): the zenith stays in blues (darker at night), the horizon takes
// on the warm tints typical of dawn/dusk — this is the only "invention" in this module,
// clearly documented as a simplified procedural model, NOT a reading of real Perez
// scattering. Hours/colors were chosen to be plausible and readable, LINEARLY interpolated
// (RGB) between the two keyframes bracketing the current hour, with circular wraparound
// (24h == 0h).
// ---------------------------------------------------------------------------------------
struct ColorKeyframe { double hour; float r, g, b; };

constexpr ColorKeyframe kZenithKeyframes[] = {
    { 0.0,  0.02f, 0.03f, 0.10f }, // midnight — very dark night blue
    { 4.0,  0.03f, 0.05f, 0.14f }, // deep night
    { 6.0,  0.20f, 0.30f, 0.55f }, // dawn — zenith brightens
    { 8.0,  0.25f, 0.45f, 0.80f }, // morning
    { 12.0, 0.20f, 0.45f, 0.85f }, // noon — full sky blue
    { 17.0, 0.22f, 0.35f, 0.65f }, // late afternoon
    { 19.0, 0.10f, 0.12f, 0.30f }, // dusk — rapid darkening
    { 21.0, 0.04f, 0.06f, 0.16f }, // nightfall
    { 24.0, 0.02f, 0.03f, 0.10f }, // == 0h (wrap)
};

constexpr ColorKeyframe kHorizonKeyframes[] = {
    { 0.0,  0.05f, 0.06f, 0.14f }, // midnight
    { 4.0,  0.08f, 0.08f, 0.18f }, // deep night
    { 6.0,  0.90f, 0.55f, 0.35f }, // dawn — orange
    { 8.0,  0.75f, 0.75f, 0.80f }, // morning — bright
    { 12.0, 0.60f, 0.72f, 0.88f }, // noon — pale blue
    { 17.0, 0.85f, 0.60f, 0.45f }, // late afternoon — golden
    { 19.0, 0.80f, 0.35f, 0.25f }, // dusk — red/orange
    { 21.0, 0.12f, 0.10f, 0.20f }, // nightfall
    { 24.0, 0.05f, 0.06f, 0.14f }, // == 0h (wrap)
};

// Linearly interpolates within a keyframe table sorted by increasing hour [0,24].
void LerpKeyframes(const ColorKeyframe* table, size_t count, double hour,
                    float& outR, float& outG, float& outB) {
    hour = std::clamp(hour, 0.0, 24.0);
    for (size_t i = 0; i + 1 < count; ++i) {
        const ColorKeyframe& a = table[i];
        const ColorKeyframe& b = table[i + 1];
        if (hour >= a.hour && hour <= b.hour) {
            const double span = (b.hour - a.hour);
            const float t = span > 0.0 ? static_cast<float>((hour - a.hour) / span) : 0.0f;
            outR = a.r + (b.r - a.r) * t;
            outG = a.g + (b.g - a.g) * t;
            outB = a.b + (b.b - a.b) * t;
            return;
        }
    }
    // Out of bounds (should not happen with [0,24] tables): last keyframe.
    outR = table[count - 1].r; outG = table[count - 1].g; outB = table[count - 1].b;
}

D3DCOLOR ToArgb(float r, float g, float b) {
    auto clampByte = [](float v) -> DWORD {
        return static_cast<DWORD>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return D3DCOLOR_ARGB(0xFF, clampByte(r), clampByte(g), clampByte(b));
}

} // namespace

bool SkyRenderer::Init(Renderer& renderer) {
    dev_ = renderer.Device();
    if (!dev_) { TS2_ERR("SkyRenderer::Init : device nul."); return false; }
    ready_ = true;
    recomputeColors(); // fallback ("noon") colors until an .ATM has been supplied
    TS2_LOG("SkyRenderer pret (gradient procedural derive de l'heure .ATM reelle).");
    return true;
}

void SkyRenderer::Shutdown() {
    dev_ = nullptr;
    ready_ = false;
}

void SkyRenderer::ApplyConfig(const world::SilverLiningConfig& cfg) {
    config_.defaultLongitude = cfg.defaultLongitude;
    config_.defaultLatitude = cfg.defaultLatitude;
    config_.defaultAltitude = cfg.defaultAltitude;
    config_.defaultYear = cfg.defaultYear;
    config_.defaultMonth = cfg.defaultMonth;
    config_.defaultDay = cfg.defaultDay;
    config_.defaultHour = cfg.defaultHour;
    config_.defaultMinute = cfg.defaultMinute;
    config_.defaultSecond = cfg.defaultSecond;
    config_.defaultTimezone = cfg.defaultTimezone;
    config_.defaultDst = cfg.defaultDst;
    config_.defaultTurbidity = cfg.defaultTurbidity;
    config_.disableToneMapping = cfg.disableToneMapping;
    config_.enableAtmosphereFromSpace = cfg.enableAtmosphereFromSpace;
    config_.atmosphereHeight = cfg.atmosphereHeight;
    config_.atmosphereScaleHeightMeters = cfg.atmosphereScaleHeightMeters;
    TS2_LOG("SkyRenderer : config SilverLining chargee (turbidity=%.1f, altitude atmos=%.0f m).",
            config_.defaultTurbidity, config_.atmosphereHeight);
}

void SkyRenderer::SetAtmosphere(const asset::AtmosphereFile& atm) {
    hasRealAtmosphere_ = atm.Valid();
    if (hasRealAtmosphere_) {
        hourOfDay_     = atm.HourOfDay();
        latitude_      = atm.Latitude();
        longitude_     = atm.Longitude();
        forceBlackSky_ = atm.RenderFlagForceBlackSky();
        TS2_LOG("SkyRenderer::SetAtmosphere : \"%s\" reel charge (heure=%.2fh lat=%.3f lon=%.3f "
                "forceBlack=%d skipCelestial=%d).",
                atm.Path().c_str(), hourOfDay_, latitude_, longitude_,
                forceBlackSky_ ? 1 : 0, atm.RenderFlagSkipCelestial() ? 1 : 0);
    } else {
        // FIXED fallback (D-2/T-14 — IDA wins): when the zone atmosphere data is missing
        // ("Atmosphere.DAT"/.ATM absent), the original engine does NOT fall back to the
        // SilverLining.config defaults (30/-122) but to a hardcoded Seoul geo, proven by:
        //   World_LoadMap 0x4116B0 @0x41184b: Env_SetGeoLocation(this, 37.6, 127.0, 0.0)
        //   (branch taken when File_IfstreamOpen("Atmosphere.DAT") fails -> v9[2]&4 != 0 || !v9[21]).
        //   Env_SetGeoLocation 0x411D30 overrides lat/lon [-90,90]/[-180,180] then sets DateTime
        //   2010-07-26 15:00 (@0x411dcf..0x411de7: v10=2010/v11=7/v12=26/v13=15) and turbidity 2.0.
        // Note: the config_ defaults (30/-122, 2006-08-15 12:00) remain the SilverLining.config
        //   stock values (cAtmosphere_Initialize 0x793390) — DO NOT confuse with this Seoul
        //   fallback from the World_LoadMap path. lat/lon do not yet drive the gradient (T-5);
        //   the 15:00 hour, however, IS consumed by the day/night model -> observable faithful fallback.
        latitude_  = 37.6;   // World_LoadMap 0x4116B0 @0x41184b -> Env_SetGeoLocation(.,37.6,.)
        longitude_ = 127.0;  // World_LoadMap 0x4116B0 @0x41184b -> Env_SetGeoLocation(.,.,127.0)
        hourOfDay_ = 15.0;   // Env_SetGeoLocation 0x411D30 @0x411de7 : v13=15 (2010-07-26 15:00, min/sec=0)
        forceBlackSky_ = false;
        TS2_WARN("SkyRenderer::SetAtmosphere : .ATM invalide/absent -> repli Seoul 37.6/127.0 15:00 "
                 "(fidele World_LoadMap 0x4116B0 -> Env_SetGeoLocation 0x411D30).");
    }
    recomputeColors();
}

void SkyRenderer::recomputeColors() {
    if (forceBlackSky_) {
        // Faithfully reproduces the original engine's "black sky" override (@+642, cf. banner
        // in .h / Docs/TS2_SILVERLINING_CONFIG.md §3.2-3.3): both zenith AND horizon go opaque black.
        // CONFIRMED — cAtmosphere_RenderFrame 0x793B80: if @+642, the 3 samples (base sky
        //   dword_811860.., secondary sky dword_811AAC.., horizon qword_8117A0..) are forced to (0,0,0,1)
        //   instead of the Perez computation (SL_GetSkyColorRGBA/SL_GetHorizonColor). (.ATM flag — no VeryOldClient hint.)
        zenithColor_ = horizonColor_ = D3DCOLOR_ARGB(0xFF, 0, 0, 0);
        return;
    }
    float zr, zg, zb, hr, hg, hb;
    LerpKeyframes(kZenithKeyframes, std::size(kZenithKeyframes), hourOfDay_, zr, zg, zb);
    LerpKeyframes(kHorizonKeyframes, std::size(kHorizonKeyframes), hourOfDay_, hr, hg, hb);
    zenithColor_  = ToArgb(zr, zg, zb);
    horizonColor_ = ToArgb(hr, hg, hb);
}

// =======================================================================================
// 6-face skybox cube — FAITHFUL fallback of Env_RenderSkyCube 0x6a8f60 (predates SilverLining).
//
// DEFERRED: full SilverLining remains unimplemented. cAtmosphere_RenderFrame 0x793B80
//   (real per-frame sky/atmosphere rendering) and SL_Renderer_LoadBackendDLL 0x71F300
//   (LoadLibraryA("<cfg>/VC9/win32/SilverLiningDirectX9-MT.dll") on map load, then ~90
//   GetProcAddress calls filling the C table g_SL_* 0x186381C-0x186397C) depend on the
//   SilverLiningDirectX9-MT.dll / SilverLining-MT.lib library, NOT redistributable. The cube
//   below is the faithful intermediate step: the exact geometry/D3D9 state of
//   Env_RenderSkyCube, fed at runtime by the setters (zone sky textures, radius, camera position).
//
// SOURCE OF THE 6 FACE TEXTURES — NOT STATICALLY PROVABLE (honest, NOT invented):
//   In this EU build, Env_RenderSkyCube 0x6a8f60 is DEAD CODE. Its sole caller
//   Gfx_BeginFrame 0x6a2280 only invokes it if (a2 && *a2), but all 8 call sites of
//   Gfx_BeginFrame (Scene_IntroRender 0x5188c8, Scene_ServerSelectRender 0x5192a8,
//   Scene_LoginRender 0x51b078, Scene_CharSelectRender 0x51cf28, Scene_EnterWorldRender
//   0x52c2b8, Scene_InGameRender 0x52d13a/0x52d187/0x52d1fc) ALL pass a2 = NULL (push 0).
//   The zone sky object (the `this` of 0x6a8f60) is thus never constructed and NO loader
//   feeds the 6 face textures (this+13). Corroborated by Docs/TS2_SKY_ROSETTA.md T-12
//   ("6-texture loader to identify", PLAUSIBLE, not located in the IDB). => We do NOT
//   FABRICATE an asset path. The cube stays ready (faithful geometry/state below) but is
//   gated by HasSkyCube(): without SetSkyCubeTextures(), the fallback remains the full-screen
//   gradient (layer 2, minimal). TODO (anchored T-12 / 0x6a8f60): if layer 1 were ever to be
//   reactivated, identify at runtime (x32dbg) the sky object and the this+13 loader BEFORE
//   porting any asset path.
//
// RADIUS = FAR PLANE (proven, Cam_SetProjection 0x69cbef): flt_7FFEA0 (0x7FFEA0) =
//   g_GfxRenderer+136, written by Cam_SetProjection with param a3 "far" (@0x69cbd5); cube
//   side = far/sqrt(3) (@0x6a8f9b). Static image value = 0 (runtime cache) => MAIN must push
//   the renderer's far plane via UpdateSkyRuntime()/SetSkyRadius(). fog = dword_7FFEA4 =
//   g_GfxRenderer+140 (Cam_SetProjection +140, read @0x69cc06).
// =======================================================================================

// Rebuilds the 24 sky cube vertices. Env_RenderSkyCube 0x6a8f60 @0x6a8f9b..0x6a92c6.
void SkyRenderer::rebuildCubeVertices() {
    // Cache @0x6a8f88 / @0x6a92c6: only rebuild if the radius (flt_7FFEA0) has changed.
    if (cubeCacheRadius_ == skyRadius_) return;

    // @0x6a8f9b: v3 = flt_7FFEA0 / sqrt(3.0)  (dbl_7EDA38 = 3.0). s = half-edge of faces 1-5.
    const float s = skyRadius_ / std::sqrt(3.0f);
    // WARNING - PROVEN FAITHFUL ASYMMETRY (decompile 0x6a9039..0x6a9087): face 0 (+Z) uses
    //   ±0.5*v3 (half size) while faces 1-5 use ±v3. To be reproduced as-is (the original
    //   binary IS this way — FIDELITY rule, do not "fix" into a symmetric cube).
    //   flt_7A939C=+0.5, flt_7BB294=-0.5, flt_7EDA10=-1.0.
    const float h = 0.5f * s;

    // Identical UVs for all 6 faces (proven @0x6a8fa1.. and per-face: v0=(0,1) v1=(0,0)
    //   v2=(1,1) v3=(1,0)). Position table = plan §A.1 (step-by-step trace of the FPU stack).
    const SkyCubeVertex verts[24] = {
        // Face 0 (+Z, faceTex_[0]) — HALF SIZE h. @0x6a9039..0x6a9087
        { -h, -h,  h, 0.0f, 1.0f }, {  -h,  h,  h, 0.0f, 0.0f },
        {  h, -h,  h, 1.0f, 1.0f }, {   h,  h,  h, 1.0f, 0.0f },
        // Face 1 (-Z, faceTex_[1]). @0x6a908d..0x6a90dd
        {  s, -s, -s, 0.0f, 1.0f }, {   s,  s, -s, 0.0f, 0.0f },
        { -s, -s, -s, 1.0f, 1.0f }, {  -s,  s, -s, 1.0f, 0.0f },
        // Face 2 (-X, faceTex_[2]). @0x6a90e3..0x6a9135
        { -s, -s, -s, 0.0f, 1.0f }, {  -s,  s, -s, 0.0f, 0.0f },
        { -s, -s,  s, 1.0f, 1.0f }, {  -s,  s,  s, 1.0f, 0.0f },
        // Face 3 (+X, faceTex_[3]). @0x6a913b..0x6a919d
        {  s, -s,  s, 0.0f, 1.0f }, {   s,  s,  s, 0.0f, 0.0f },
        {  s, -s, -s, 1.0f, 1.0f }, {   s,  s, -s, 1.0f, 0.0f },
        // Face 4 (+Y top, faceTex_[4]). @0x6a91a3..0x6a91fd
        { -s,  s,  s, 0.0f, 1.0f }, {  -s,  s, -s, 0.0f, 0.0f },
        {  s,  s,  s, 1.0f, 1.0f }, {   s,  s, -s, 1.0f, 0.0f },
        // Face 5 (-Y bottom, faceTex_[5]). @0x6a9205..0x6a92bb
        { -s, -s, -s, 0.0f, 1.0f }, {  -s, -s,  s, 0.0f, 0.0f },
        {  s, -s, -s, 1.0f, 1.0f }, {   s, -s,  s, 1.0f, 0.0f },
    };
    for (int i = 0; i < 24; ++i) cubeVerts_[i] = verts[i];

    cubeCacheRadius_ = skyRadius_; // @0x6a92c6: this+210 = flt_7FFEA0
}

// Draws the camera-centered sky cube. EXACT sequence of Env_RenderSkyCube 0x6a8f60.
void SkyRenderer::renderCube() {
    rebuildCubeVertices();

    // @0x6a92d7: push 7 -> SetRenderState(D3DRS_ZENABLE=7, FALSE) — depth-TEST off
    //   (RS 7 = D3DRS_ZENABLE, NOT 14=ZWRITEENABLE; immediate `push 7` proven @0x6a92d4).
    dev_->SetRenderState(D3DRS_ZENABLE, FALSE);

    // @0x6a9324: Gfx_SetLight(g_GfxRenderer, 1, ...) configures light slot 0 (Object A
    //   fields not owned by this front-end). The FVF (XYZ|TEX1) has NEITHER normal NOR
    //   diffuse => lighting has no visual effect on the cube => we force lighting off (faithful render).
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);

    // @0x6a933c: if dword_7FFEA4 (fog active), SetRenderState(D3DRS_FOGENABLE=28, FALSE).
    if (fogActive_) dev_->SetRenderState(D3DRS_FOGENABLE, FALSE);

    // @0x6a934f: SetFVF(258 = 0x102 = D3DFVF_XYZ | D3DFVF_TEX1).
    dev_->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);

    // @0x6a936e: D3DXMatrixTranslation(&m, g_CameraPos, flt_800134, flt_800138) then
    //   @0x6a9385 SetTransform(D3DTS_WORLD=256, &m) => cube centered on the camera eye
    //   (infinite skybox). Identity matrix + translation set by hand (bit-exact equivalent,
    //   no dependency on legacy D3DX).
    D3DMATRIX m = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        camPos_[0], camPos_[1], camPos_[2], 1.0f,
    };
    dev_->SetTransform(D3DTS_WORLD, &m);

    // @0x6a9398 / @0x6a93ab: clamp U/V (D3DSAMP_ADDRESSU=1/ADDRESSV=2 -> D3DTADDRESS_CLAMP=3)
    //   to avoid edge bleeding between faces.
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    // Global state NOT touched by the binary (inherited from the pipeline, not owned by
    //   this front-end): set explicitly here for an interior-textured cube, then restored
    //   below. CULLMODE NONE guarantees face visibility when viewed from inside;
    //   COLOROP=SELECTARG1(TEXTURE) => pure texture (lighting off, no diffuse).
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

    // 6-face loop @0x6a93b1..0x6a93ee: SetTexture(0, faceTex[i]) (@0x6a93cc, vtbl+260) then
    //   DrawPrimitiveUP(D3DPT_TRIANGLESTRIP=5, PrimitiveCount=2, &face, Stride=20) (@0x6a93e1,
    //   vtbl+332). faceTex_ = 1st dword of the sky object's 52-byte face descriptors (v7 += 13).
    for (int i = 0; i < 6; ++i) {
        dev_->SetTexture(0, faceTex_[i]);
        dev_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, &cubeVerts_[i * 4], sizeof(SkyCubeVertex));
    }

    // @0x6a93fd / @0x6a9410: restore wrap U/V (D3DTADDRESS_WRAP=1).
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

    // @0x6a942a: if fog, SetRenderState(D3DRS_FOGENABLE=28, TRUE).
    if (fogActive_) dev_->SetRenderState(D3DRS_FOGENABLE, TRUE);

    // @0x6a9435: Gfx_SkyboxEndState 0x69d780 reapplies the material (SetMaterial, Object A
    //   fields not owned) — skipped here; instead we restore the global states compensated above.
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);

    // @0x6a9446: push 7 -> SetRenderState(D3DRS_ZENABLE=7, TRUE) — restores the depth-TEST
    //   (immediate `push 7` proven @0x6a9443; value `push 1` @0x6a9441).
    dev_->SetRenderState(D3DRS_ZENABLE, TRUE);
}

// --- Sky cube setters (modeled runtime state, cf. SkyRenderer.h) -----------------
// faceTex_ = NON-owned raw ptr (zone sky object this+13, 1st dword = texture). The owner
// (zone/asset) remains responsible for their lifetime and MUST re-supply them after a
// device-reset (SkyRenderer owns no D3DPOOL_DEFAULT resource: the cube goes through DrawPrimitiveUP).
void SkyRenderer::SetSkyCubeTextures(IDirect3DTexture9* const faces[6]) {
    for (int i = 0; i < 6; ++i) faceTex_[i] = faces ? faces[i] : nullptr;
}

void SkyRenderer::SetSkyRadius(float r) {
    // r = renderer far plane (flt_7FFEA0 = g_GfxRenderer+136, Cam_SetProjection 0x69cbef a3
    // @0x69cbd5). rebuildCubeVertices then applies side = r/sqrt(3) (@0x6a8f9b).
    if (r != skyRadius_) {
        skyRadius_ = r;
        cubeCacheRadius_ = -1.0f; // forces rebuild on next rebuildCubeVertices() call
    }
}

void SkyRenderer::SetCameraPosition(float x, float y, float z) {
    camPos_[0] = x; camPos_[1] = y; camPos_[2] = z; // g_CameraPos 0x800130 = g_GfxRenderer+792
}

void SkyRenderer::SetFogActive(bool on) {
    fogActive_ = on; // dword_7FFEA4 = g_GfxRenderer+140 (fog-enable, Cam_SetProjection 0x69cbef +140)
}

// Wiring B3 (layer 1 activation): mirrors the state that Gfx_BeginFrame 0x6a2280 feeds to
// Env_RenderSkyCube 0x6a8f60. Groups the 3 proven globals (far plane -> radius, camera eye, fog).
// Does NOT activate the cube alone: without SetSkyCubeTextures(), HasSkyCube()==false -> gradient.
void SkyRenderer::UpdateSkyRuntime(float farPlane, const float camPos[3], bool fogEnabled) {
    SetSkyRadius(farPlane);                                         // flt_7FFEA0 = g_GfxRenderer+136 (Cam_SetProjection 0x69cbef a3)
    if (camPos) SetCameraPosition(camPos[0], camPos[1], camPos[2]); // g_CameraPos 0x800130
    SetFogActive(fogEnabled);                                       // dword_7FFEA4 = g_GfxRenderer+140
}

bool SkyRenderer::HasSkyCube() const {
    // Active when Env_RenderSkyCube's gate would be crossed: radius > 0 (this+0 != 0 =>
    //   flt_7FFEA0 fed) and at least one face texture present.
    if (skyRadius_ <= 0.0f) return false;
    for (int i = 0; i < 6; ++i) if (faceTex_[i]) return true;
    return false;
}

void SkyRenderer::Render(int screenW, int screenH) {
    if (!ready_ || !dev_ || screenW <= 0 || screenH <= 0) return;

    // FAITHFUL fallback (Env_RenderSkyCube 0x6a8f60) as soon as a sky cube is fed by the zone.
    if (HasSkyCube()) { renderCube(); return; }

    // --- PLACEHOLDER (unchanged): full-screen day/night gradient (cf. .h banner). Simplified
    //     procedural model, NOT a reading of real Perez scattering — do NOT refine the invented
    //     colors (rule). Serves as fallback while the sky cube is not fed (zone sky radius/
    //     textures not yet wired).
    const float w = static_cast<float>(screenW);
    const float h = static_cast<float>(screenH);
    // -0.5f pixel-texel D3D9 offset: irrelevant here (no texture), kept for consistency with
    // the engine's other full-screen quads (cf. the old WorldGeometryRenderer::drawSkyGradientFallback).
    ScreenGradientVertex verts[4] = {
        { -0.5f,      -0.5f,      1.0f, 1.0f, zenithColor_  }, // top-left
        { w - 0.5f,   -0.5f,      1.0f, 1.0f, zenithColor_  }, // top-right
        { -0.5f,      h - 0.5f,   1.0f, 1.0f, horizonColor_ }, // bottom-left
        { w - 0.5f,   h - 0.5f,   1.0f, 1.0f, horizonColor_ }, // bottom-right
    };

    dev_->SetVertexShader(nullptr);
    dev_->SetPixelShader(nullptr);
    dev_->SetVertexDeclaration(nullptr);
    dev_->SetFVF(kScreenGradientFVF);

    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);
    dev_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev_->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev_->SetTexture(0, nullptr);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_DISABLE);

    dev_->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, verts, sizeof(ScreenGradientVertex));

    // Restores the state expected by the rest of the frame (same policy as the old
    // drawSkyGradientFallback / WorldRenderer::drawPlaceholderCube).
    dev_->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
}

} // namespace ts2::gfx

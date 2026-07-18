// Gfx/SilverLiningSky.cpp — see SilverLiningSky.h for the full banner (missing-engine finding,
// gating, semantics). Two mutually exclusive paths:
//   • TS2_SILVERLINING_ENGINE_AVAILABLE == 1: calls the REAL SilverLining::Atmosphere API
//     (needs external/SilverLining/Include on the include path + link against SilverLining-MT.lib).
//   • TS2_SILVERLINING_ENGINE_AVAILABLE == 0 (default): loads/probes the real DLL backend then
//     reports EngineUnavailable; the other methods are documented no-ops.
#include "Gfx/SilverLiningSky.h"
#include "Asset/AtmosphereFile.h"
#include "Core/Log.h"

#include <string>

#if TS2_SILVERLINING_ENGINE_AVAILABLE
// --- Engine mode ONLY: these headers are NOT required to build the module in fallback mode. ---
// Requires this .vcxproj addition for this mode: AdditionalIncludeDirectories += $(ProjectDir)external\SilverLining\Include
#include "Gfx/Camera.h"          // BuildViewMatrix / BuildProjMatrix (D3DXMATRIX)
#include "Atmosphere.h"          // SilverLining::Atmosphere
#include "AtmosphericConditions.h"
#include "Location.h"
#include "LocalTime.h"
#endif

namespace ts2::gfx {

#if !TS2_SILVERLINING_ENGINE_AVAILABLE
namespace {
// DirectX9 backend subpath, faithful to SL_Renderer_LoadBackendDLL 0x71F300 (case 1 = DIRECTX9):
// the original loader does Str_Append(resourceDir, "VC9/") then "win32/" then the DLL name.
constexpr char kBackendSubPath[] = "VC9/win32/SilverLiningDirectX9-MT.dll";
} // namespace
#endif

// =====================================================================================
// Init
// =====================================================================================
bool SilverLiningSky::Init(IDirect3DDevice9* device, const char* resourceDir) {
    dev_ = device;
    ready_ = false;
    backendDllFound_ = false;

    if (!device) {
        TS2_ERR("SilverLiningSky::Init: null D3D9 device.");
        return false;
    }
    // resourceDir = SDK "Resources" root (contains VC9/win32 + SilverLining.config). Falls back
    // to "." if not provided (matching the documented ".\\Resources" default of Atmosphere::Initialize).
    const char* baseDir = (resourceDir && resourceDir[0]) ? resourceDir : ".";

#if TS2_SILVERLINING_ENGINE_AVAILABLE
    // ---------------------------------------------------------------------------------
    // ENGINE MODE (only enabled when the out-of-repo SilverLining-MT.lib is provided).
    // ---------------------------------------------------------------------------------
    // REAL license found in cAtmosphere_ctor 0x791B40 (see TS2_SILVERLINING_PIPELINE.md §1).
    // CONFIRMED — decompilation of 0x791B40: SilverLining_ValidateLicense(a3=userName, a4=hexKey) receives
    //   EXACTLY this userName/key pair (args verified in the IDB); this module reuses the same strings.
    // ex-VeryOldClient: SILVERLINING_LICENSE (macro consumed by CAtmosphere::Create -> new Atmosphere(...))
    auto* atmo = new SilverLining::Atmosphere("ALT1 License 3", "113e355254250a02094e32165441");

    // renderer = DIRECTX9 (case 1), rightHanded = false (GXD is left-handed, see Camera.h LookAtLH),
    // environment = D3D9 device. Faithful to cAtmosphere_Initialize 0x793390.
    // CONFIRMED — 0x793390 is called as Initialize(1, "G03_GDATA\\D11_ATMOSPHERE\\", 0, device); the
    //   DIRECTX9 == 1 enum matches. // ex-VeryOldClient: CAtmosphere::Create -> Atmosphere::Initialize(1,…)
    const int err = atmo->Initialize(SilverLining::Atmosphere::DIRECTX9, baseDir,
                                     /*rightHanded*/ false, device);
    // ex-VeryOldClient: Atmosphere::InitializeErrors::E_NOERROR (return code tested by CAtmosphere::Create) — 0x793390
    if (err != SilverLining::Atmosphere::E_NOERROR) {
        TS2_ERR("SilverLiningSky::Init: Atmosphere::Initialize failed (code=%d).", err);
        delete atmo;
        return false;
    }

    // ENGINE TODO: Atmosphere::Initialize (Atmosphere.h l.370-376) REQUIRES a device created with
    // D3DPRESENTFLAG_LOCKABLE_BACKBUFFER and WITHOUT D3DCREATE_PUREDEVICE. Needs auditing on the
    // Gfx/Renderer.cpp side (Renderer::Init) — out of scope for this standalone module.

    // Lens flare SDK (T-2): the official flare comes from the SDK, NOT from custom code (D-4: VeryOld's
    // DrawForLensFlare is dead code). The SL_LensFlare object is constructed by
    // cAtmosphere_Initialize 0x793390 @0x7934ce (LensFlare_Init 0x79A1D0, member +8 of cAtmosphere);
    // enabled here via the SDK path. The "disable-lens-flare" key of SilverLining.config is read by
    // the SDK at Initialize — not re-driven here (default: enabled). // ex-VeryOldClient: CAtmosphere::EnableLensFlare(true)
    atmo->EnableLensFlare(true);                 // Atmosphere.h — SDK path (see TS2_SKY_ROSETTA.md T-2/D-4)

    atmosphere_ = atmo;
    backendDllFound_ = true;
    ready_ = true;
    TS2_LOG("SilverLiningSky::Init: SilverLining engine initialized (DIRECTX9, resourceDir=\"%s\").", baseDir);
    return true;

#else
    // ---------------------------------------------------------------------------------
    // FALLBACK MODE (static SilverLining-MT.lib engine absent — current state of the repo).
    // We still load the REAL backend DLL and resolve its real exports, EXACTLY like
    // SL_Renderer_LoadBackendDLL 0x71F300, to prove the real renderer is wired up — then
    // report EngineUnavailable (no sky can be produced without the high-level engine).
    std::string dllPath = baseDir;
    if (!dllPath.empty()) {
        const char last = dllPath.back();
        if (last != '/' && last != '\\') dllPath += '/';
    }
    dllPath += kBackendSubPath; // -> "<resourceDir>/VC9/win32/SilverLiningDirectX9-MT.dll"

    HMODULE backend = ::LoadLibraryA(dllPath.c_str());
    if (!backend) {
        TS2_WARN("SilverLiningSky::Init: backend DLL not found/readable (\"%s\", err=%lu) "
                 "-> SilverLining sky unavailable.", dllPath.c_str(), ::GetLastError());
        return false;
    }

    // Resolves the 2 exports the original loader also resolves (SetContext = dword_186381C,
    // SetEnvironment = dword_1863828). Proves the DLL does export the expected backend interface.
    const bool hasSetEnvironment = (::GetProcAddress(backend, "SetEnvironment") != nullptr);
    const bool hasSetContext     = (::GetProcAddress(backend, "SetContext") != nullptr);
    backendDllFound_ = (hasSetEnvironment && hasSetContext);

    TS2_LOG("SilverLiningSky::Init: backend \"%s\" loaded (SetEnvironment=%s, SetContext=%s).",
            dllPath.c_str(), hasSetEnvironment ? "OK" : "MISSING", hasSetContext ? "OK" : "MISSING");

    // ENGINE TODO: the REAL 0x71F300 would then call
    //   SetEnvironment(rightHanded=false, device, resourceLoader)
    // to obtain a renderer "context". But `resourceLoader` is a SilverLining::ResourceLoader*
    // SUPPLIED by the static engine (SilverLining-MT.lib), ABSENT from the repo; without it no
    // valid context can be created, and the SilverLining::Atmosphere class (Initialize/BeginFrame/
    // EndFrame) stays unlinkable. We do NOT fabricate a fake Atmosphere API ("INVENT NOTHING" rule).
    // => Unload the backend and report EngineUnavailable; the caller keeps its gradient fallback.
    ::FreeLibrary(backend);
    TS2_WARN("SilverLiningSky::Init: static SilverLining engine absent (SilverLining-MT.lib) "
             "-> EngineUnavailable (Init returns false, caller fallback stays active).");
    return false;
#endif
}

// =====================================================================================
// Atmosphere config: GLOBAL (once per session) + PER ZONE (documentation, see TS2_SILVERLINING_CONFIG.md)
// =====================================================================================
// GLOBAL — SilverLining.config read once by cAtmosphere_Initialize 0x793390 (via World_LoadMap
//   0x4116B0): default-turbidity (code default 2.0, shipped file 2.2), atmosphere-height (default
//   100000), enable-atmosphere-from-space (default true, file "no"), atmosphere-scale-height-meters
//   (RE-READ EVERY FRAME in cAtmosphere_RenderFrame 0x793B80 -> dbl_18636E8). Fallback if "Atmosphere.DAT"
//   is missing: hardcoded Seoul geo (lat 37.6 / lon 127.0), see TS2_SILVERLINING_CONFIG.md §2.1.
//   // ex-VeryOldClient: CAtmosphere::Create (reads "Atmosphere.DAT" else SetTimeAndLocation(37.6,127.0,…))
// PER ZONE — G03_GDATA\D07_GWORLD\Z%03d.ATM loaded on every map by World_LoadZoneResource 0x4dcb60
//   (case 7) -> Atmosphere_Deserialize 0x795A40: 5 flags (+0 @+352 skipCelestial, +1 @+642 forceBlackSky,
//   +2 @+643 / +3 @+644 unidentified, +4 forceToneMapping) + geo vec3 (lat/lon/altitude) + 2x DateTime
//   + 3 keyframe tracks. 89 .ATM files (sizes 208/284/360/512, content varies by zone).
//   // ex-VeryOldClient: CAtmosphere::Load(pAtmosphereDataPath) (same Serialize/Unserialize stream)
// =====================================================================================
// UpdateTime
// =====================================================================================
void SilverLiningSky::UpdateTime(const asset::AtmosphereFile& atm) {
    // Stores the zone's render flags in both modes (diagnostic + consumed by
    // RenderSkyBefore in engine mode). Faithful to cAtmosphere_RenderFrame @+352/@+642.
    skipCelestial_ = atm.RenderFlagSkipCelestial();
    forceBlackSky_ = atm.RenderFlagForceBlackSky();

#if TS2_SILVERLINING_ENGINE_AVAILABLE
    if (!ready_ || !atmosphere_) return;
    auto* atmo = static_cast<SilverLining::Atmosphere*>(atmosphere_);
    SilverLining::AtmosphericConditions* cond = atmo->GetConditions();

    // Reuses the lat/lon/altitude + time ALREADY parsed by Asset/AtmosphereFile (no reparse).
    // CONFIRMED — same Location/LocalTime -> GetConditions()->SetLocation/SetTime sequence as the original;
    //   value source = the zone's .ATM (Atmosphere_Deserialize 0x795A40, see TS2_SILVERLINING_CONFIG.md §3.2).
    // ex-VeryOldClient: CAtmosphere::SetTimeAndLocation (Location/LocalTime -> SetLocation/SetTime)
    SilverLining::Location loc;
    loc.SetLatitude(atm.Latitude());
    loc.SetLongitude(atm.Longitude());
    loc.SetAltitude(atm.Altitude());

    const asset::AtmDateTime& d = atm.CurrentDateTime();
    SilverLining::LocalTime t;
    t.SetYear(d.year);
    t.SetMonth(d.month);
    t.SetDay(d.day);
    t.SetHour(d.hour);
    t.SetMinutes(d.minute);
    t.SetSeconds(d.second);
    t.SetTimeZone(d.timezone);
    t.SetObservingDaylightSavingsTime(d.dst != 0);

    cond->SetLocation(loc);
    cond->SetTime(t);
    // ENGINE TODO: turbidity/visibility come from SilverLining.config (GLOBAL, once per
    // session — see TS2_SILVERLINING_CONFIG.md §2), NOT from the .ATM; nothing invented here.
#else
    // Fallback mode: the flags above are stored (diagnostic); there is no
    // Atmosphere object to feed time/position into. No-op beyond that.
#endif
}

// =====================================================================================
// SilverLining (C++) vtables — offsets CONFIRMED by decompilation (documentation)
// =====================================================================================
// The cAtmosphere object (648 B) aggregates C++-vtable sub-objects, invoked per frame from
// cAtmosphere_RenderFrame 0x793B80 / cAtmosphere_DrawObjects 0x792A60. PROVEN offsets (IDA = truth):
//   • Sky* @ cAtmosphere+0 (built by SL_Sky_Construct 0x72BF60):
//        vtable+8  = update sky haze/params        (0x793B80: (**this + 8)(...))
//        vtable+4  = sample sky color               (0x793B80: (**this + 4)(...))
//        vtable+32 = DrawSky (__stdcall, gated @+352/drawSky) (0x793B80: (**this + 32)(...))
//   • Ephemeris* @ +4 (Sky_UpdateCelestialPositions 0x7571C0), LensFlare* @ +8, AtmoFromSpace* @ +16.
//   • Each cloud layer of the red-black tree RbTreeB: vtable+52 = Draw()
//        (cAtmosphere_DrawObjects 0x792A60: v10 = *vtbl + 52; iterated via RbTreeB_IterNext).
//        // ex-VeryOldClient: SilverLining::CloudLayer / CloudLayerFactory (Core/SilverLining/Include;
//        //   usages CAtmosphere.cpp SetupCirrusClouds/SetupCumulusCongestusClouds).
// NB: table 0x186381C-0x186397C (g_SL_*) is the BACKEND's C interface (GetProcAddress pointers),
//   NOT a C++ vtable — see Docs/TS2_SILVERLINING_PIPELINE.md §2.
//
// =====================================================================================
// applySceneLightingAndFog — sun lighting (D3DLIGHT9) + GXD fog derived from the Atmosphere SDK
//   Faithful to Env_UpdateSunLight 0x412210 + Env_UpdateFogState 0x412370 (called per frame from
//   Env_UpdateFrame 0x412550, right after cAtmosphere_RenderFrame). Engine mode only.
// =====================================================================================
#if TS2_SILVERLINING_ENGINE_AVAILABLE
void SilverLiningSky::applySceneLightingAndFog() {
    if (!ready_ || !atmosphere_ || !dev_) return;
    auto* atmo = static_cast<SilverLining::Atmosphere*>(atmosphere_);

    // ---- Directional sun light — faithful to Env_UpdateSunLight 0x412210 ----
    // The original builds a D3DLIGHT9 (0x68 B) at dword_18C5358: memset 0 (@0x41227b), Type=3
    // DIRECTIONAL (@0x412329), Diffuse = sun color (dword_18C535C.., a=1 @0x412266), Specular =
    // white (dword_18C536C..5378 = 1.0), Ambient = horizon color (dword_18C537C.., a=1 @0x41229a),
    // Direction = -sunDir (dword_18C5398/9C, after Vec3_Normalize 0x41226d then negation @0x4122cb),
    // then SetLight(0, &light) via renderer vtable+204 (@0x412367).
    // FIDELITY NOTE: the original reads internal "faded" variants (cAtmosphere_GetSunColorFaded
    //   0x7938a0 diffuse / cAtmosphere_GetHorizonColorFaded 0x793ab0 ambient) — NOT exposed by the
    //   redistributable SDK. We use the native SDK getters prescribed by TS2_SILVERLINING_INTEGRATION.md §3.5:
    //   the channel mapping is identical; the "faded" attenuation near the horizon is not reproduced
    //   (depends on an internal game wrapper -> RE TODO). Signatures = header ref in doc §3.5.
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    atmo->GetSunOrMoonPosition(&sx, &sy, &sz);   // Atmosphere.h l.97 — (unit) direction to the sun/moon
    float dr = 1.0f, dg = 1.0f, db = 1.0f;
    atmo->GetSunOrMoonColor(&dr, &dg, &db);      // l.165 — sun color (=> Diffuse)
    float ar = 0.0f, ag = 0.0f, ab = 0.0f;
    atmo->GetAmbientColor(&ar, &ag, &ab);        // l.227 — ambient color (=> Ambient, ex-faded horizon)

    D3DLIGHT9 light;
    ::ZeroMemory(&light, sizeof(light));         // Crt_Memset(&dword_18C5358, 0, 0x68) @0x41227b
    light.Type = D3DLIGHT_DIRECTIONAL;           // = 3 (dword_18C5358 = 3 @0x412329)
    light.Diffuse.r = dr; light.Diffuse.g = dg; light.Diffuse.b = db; light.Diffuse.a = 1.0f;   // @0x412266 a=1
    light.Specular.r = 1.0f; light.Specular.g = 1.0f; light.Specular.b = 1.0f; light.Specular.a = 1.0f; // 18C536C..5378
    light.Ambient.r = ar; light.Ambient.g = ag; light.Ambient.b = ab; light.Ambient.a = 1.0f;   // @0x41229a a=1
    light.Direction.x = -sx; light.Direction.y = -sy; light.Direction.z = -sz;                  // -sunDir @0x4122cb..
    dev_->SetLight(0, &light);                   // SetLight(index 0) — renderer vtable+204 @0x412367
    dev_->LightEnable(0, TRUE);                  // the original enables light 0 on the world render path
    dev_->SetRenderState(D3DRS_LIGHTING, TRUE);

    // ---- GXD fog — faithful to Env_UpdateFogState 0x412370's render states ----
    // States written by the original (via g_GxdRenderer vtable+228 = IDirect3DDevice9::SetRenderState):
    //   28 = D3DRS_FOGENABLE  = TRUE          (@0x4124fe)
    //   34 = D3DRS_FOGCOLOR   = 0x00RRGGBB    (@0x412511; composed @0x4124f6)
    //   35 = D3DRS_FOGTABLEMODE = 1 = D3DFOG_EXP (@0x412525) -> per-pixel fog driven by density
    //   38 = D3DRS_FOGDENSITY = density (float bits) (@0x41253e)
    // FIDELITY NOTE: the exact color/density from 0x412370 (sun up: sunColor*sunColorFaded;
    //   sun down: GetColorAtDirection(0) + density = clamp(-colorBase.z/8435)*5e-6 @0x412416)
    //   rely on internal game wrappers (GetSunColorFaded/GetColorAtDirection/IsSunUp),
    //   not redistributed. We use the SDK getters GetFogEnabled/GetFogSettings (Atmosphere.h
    //   l.271/284, doc §3.5); the 8435 m density formula stays RE TODO (T-16).
    if (atmo->GetFogEnabled()) {                 // l.271 — fog enabled by the current atmospheric config
        float fogDensity = 0.0f, fr = 0.0f, fg = 0.0f, fb = 0.0f;
        atmo->GetFogSettings(&fogDensity, &fr, &fg, &fb);                 // l.284 — density + color
        const D3DCOLOR fogColor = D3DCOLOR_COLORVALUE(fr, fg, fb, 1.0f);  // alpha ignored by D3DRS_FOGCOLOR
        dev_->SetRenderState(D3DRS_FOGENABLE, TRUE);                      // 28 @0x4124fe
        dev_->SetRenderState(D3DRS_FOGCOLOR, fogColor);                   // 34 @0x412511
        dev_->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_EXP);             // 35 = D3DFOG_EXP @0x412525
        dev_->SetRenderState(D3DRS_FOGDENSITY, *reinterpret_cast<const DWORD*>(&fogDensity)); // 38 @0x41253e
    } else {
        dev_->SetRenderState(D3DRS_FOGENABLE, FALSE);
    }
}
#endif

// =====================================================================================
// RenderSkyBefore — before world geometry (SetCamera/Proj + BeginFrame)
// =====================================================================================
void SilverLiningSky::RenderSkyBefore(const Camera& camera) {
#if TS2_SILVERLINING_ENGINE_AVAILABLE
    if (!ready_ || !atmosphere_ || !dev_) return;
    auto* atmo = static_cast<SilverLining::Atmosphere*>(atmosphere_);

    // Back-buffer aspect ratio (the engine requires matrices consistent with the active viewport).
    D3DVIEWPORT9 vp{};
    dev_->GetViewport(&vp);
    const float aspect = (vp.Height > 0) ? static_cast<float>(vp.Width) / static_cast<float>(vp.Height)
                                         : 1.0f;

    // GXD view/projection matrices (float) -> double[16] expected by SetCameraMatrix/SetProjectionMatrix.
    D3DXMATRIX view, proj;
    camera.BuildViewMatrix(view);
    camera.BuildProjMatrix(proj, aspect);
    double camMtx[16], projMtx[16];
    const float* vf = reinterpret_cast<const float*>(&view);
    const float* pf = reinterpret_cast<const float*>(&proj);
    for (int i = 0; i < 16; ++i) { camMtx[i] = vf[i]; projMtx[i] = pf[i]; }
    // ex-VeryOldClient: CAtmosphere::SetViewProjectionMatrix (copies double[16] view/proj -> SetCameraMatrix/
    //   SetProjectionMatrix) — CONFIRMED, see cAtmosphere_RenderFrame 0x793B80 (SL_Backend_SetParamBlock208/80).
    atmo->SetCameraMatrix(camMtx);
    atmo->SetProjectionMatrix(projMtx);

    if (forceBlackSky_) {
        // "Black sky" override (@+642): black clear + BeginFrame without skybox — faithful to
        // cAtmosphere_RenderFrame (see TS2_SILVERLINING_CONFIG.md §3.3).
        dev_->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
        atmo->BeginFrame(/*drawSky*/ false, /*geocentric*/ false, /*skyBoxDim*/ 0.0, /*drawStars*/ false);
    } else {
        // @+352: if RenderFlagSkipCelestial, the engine skips sun/moon/stars (drawSky=false).
        const bool drawSky = !skipCelestial_;
        // ex-VeryOldClient: CAtmosphere::StartRender -> Atmosphere::BeginFrame(true) — CONFIRMED, per-frame
        //   entry point cAtmosphere_RenderFrame 0x793B80 (the actual sky draw is gated @+352 as above).
        atmo->BeginFrame(drawSky, /*geocentric*/ false, /*skyBoxDim*/ 0.0, /*drawStars*/ drawSky);
    }

    // Sun lighting + fog derived from the SDK, pushed into the device AFTER BeginFrame — faithful to
    // Env_UpdateFrame 0x412550 which, every frame, calls Env_UpdateSunLight 0x412210 then
    // Env_UpdateFogState 0x412370 right after cAtmosphere_RenderFrame, INDEPENDENTLY of the @+642 flag
    // (black sky does not cancel scene lighting/fog). See TS2_SKY_ROSETTA.md T-7.
    applySceneLightingAndFog();
#else
    (void)camera; // documented no-op: no BeginFrame possible without the engine.
#endif
}

// =====================================================================================
// RenderSkyAfter — after the scene (EndFrame: clouds + precipitation)
// =====================================================================================
void SilverLiningSky::RenderSkyAfter(const Camera& camera) {
#if TS2_SILVERLINING_ENGINE_AVAILABLE
    if (!ready_ || !atmosphere_) return;
    (void)camera; // EndFrame reuses the camera state set by RenderSkyBefore.
    auto* atmo = static_cast<SilverLining::Atmosphere*>(atmosphere_);
    // ex-VeryOldClient: CAtmosphere::EndRender -> Atmosphere::EndFrame() — CONFIRMED, see Atmosphere_DrawFrame 0x794FE0.
    atmo->EndFrame(/*drawClouds*/ true, /*drawPrecipitation*/ true);
#else
    (void)camera; // documented no-op: no EndFrame possible without the engine.
#endif
}

// =====================================================================================
// Shutdown
// =====================================================================================
void SilverLiningSky::Shutdown() {
#if TS2_SILVERLINING_ENGINE_AVAILABLE
    if (atmosphere_) {
        // ~Atmosphere cleans up Sky/Ephemeris/LensFlare/Precip and unloads the backend via
        // ShutdownShaderSystem (see cAtmosphere_dtor 0x791C40).
        // CONFIRMED — 0x791C40 destroys the sub-objects Sky/Ephemeris/LensFlare (this+0/+1/+2/+4) and frees
        //   the ResourceLoader/timer singletons (dword_18C4DE4/DE8). // ex-VeryOldClient: CAtmosphere::Destroy
        delete static_cast<SilverLining::Atmosphere*>(atmosphere_);
        atmosphere_ = nullptr;
    }
#endif
    dev_ = nullptr;
    ready_ = false;
    backendDllFound_ = false;
}

} // namespace ts2::gfx

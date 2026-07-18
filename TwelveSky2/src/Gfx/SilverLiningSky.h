// Gfx/SilverLiningSky.h — STANDALONE module integrating the REAL SilverLining SDK (DirectX9),
// gated by a compile flag. This is the "SilverLining sky" entry point of the rewritten client;
// it does NOT replace Gfx/SkyRenderer (procedural gradient) — wiring the two together
// (real-sky priority vs. gradient fallback) is left to the integrator (App/Scene).
//
// === DECISIVE FINDING (PROVEN, cf. Docs/TS2_SILVERLINING_INTEGRATION.md) ===
//   The TwelveSky2 SilverLining SDK has TWO layers:
//     1. A HIGH-LEVEL ENGINE (class SilverLining::Atmosphere: Initialize/BeginFrame/EndFrame/
//        GetSunOrMoonColor/…) compiled STATICALLY into the original TwelveSky2.exe (cf.
//        TS2_SILVERLINING_PIPELINE.md §1). Provided by the static library
//        `SilverLining-MT.lib`, NOT redistributable and ABSENT from the repo.
//     2. A low-level BACKEND RENDERER `SilverLiningDirectX9-MT.dll` (91 C exports, the
//        SilverLiningDLLCommon.h interface: SetEnvironment/SetContext/DrawStrip/…), loaded
//        DYNAMICALLY at runtime by SL_Renderer_LoadBackendDLL 0x71F300. THIS file IS present
//        (vendored under external/SilverLining/VC9/win32/) — cf. RE/silverlining_exports.txt.
//   Consequence: without layer 1, the Atmosphere class isn't linkable -> the real sky cannot
//   render. We do NOT fabricate a fake Atmosphere API ("DON'T INVENT ANYTHING" project rule).
//
// === GATING ===
//   TS2_SILVERLINING_ENGINE_AVAILABLE (default 0):
//     - == 1 (once SilverLining-MT.lib is supplied out-of-repo + external/SilverLining/Include
//       is on the include path): the .cpp #include "Atmosphere.h" and calls the REAL SDK API
//       (mapping proven from the actual headers — Atmosphere.h/AtmosphericConditions.h/
//       Location.h/LocalTime.h).
//     - == 0 (current reality): Init still loads the REAL backend DLL (LoadLibraryA +
//       GetProcAddress SetEnvironment/SetContext — faithful to 0x71F300, proves the real
//       renderer is wired) but returns false (EngineUnavailable), since no sky geometry can be
//       emitted without the engine. The other methods are documented no-ops.
//
//   Note: the module compiles ALONE, WITHOUT external/SilverLining/Include: the gated-OFF
//   block includes no SDK header. The SDK include is only required for engine mode (== 1) —
//   see the .cpp.
#pragma once

#include <windows.h>
#include <d3d9.h>

// Gating flag — defaults to 0 (static engine absent) unless the build environment overrides it.
#ifndef TS2_SILVERLINING_ENGINE_AVAILABLE
#define TS2_SILVERLINING_ENGINE_AVAILABLE 0
#endif

namespace ts2::asset { class AtmosphereFile; }

namespace ts2::gfx {

class Camera;

// Wrapper around the real SilverLining atmosphere engine (DirectX9). API requested by the task;
// semantics of each method: see comments + the .cpp (engine mode vs. fallback mode).
class SilverLiningSky {
public:
    // Prepares the SilverLining sky for the given D3D9 device.
    //   resourceDir: root of the SilverLining resources (SDK's "Resources"), i.e. the folder
    //   that CONTAINS VC9/win32/<backend>.dll and SilverLining.config. In the rewritten client
    //   this is external/SilverLining/ (vendored); in original TS2 it was G03_GDATA\D11_ATMOSPHERE\.
    //   The backend DLL path is built exactly as SL_Renderer_LoadBackendDLL 0x71F300 does:
    //   <resourceDir> + "VC9/win32/SilverLiningDirectX9-MT.dll" (case 1 = DIRECTX9).
    //
    //   Return: true ONLY if the real engine was initialized (never reachable while
    //   TS2_SILVERLINING_ENGINE_AVAILABLE == 0). In fallback mode, always returns false
    //   (EngineUnavailable) after actually loading/probing the backend DLL. The caller MUST
    //   keep its own fallback (e.g. Gfx/SkyRenderer gradient) when Init() returns false.
    // ex-VeryOldClient: CAtmosphere::Create(pResourcePath, pDevice) — CONFIRMED (license 0x791B40 +
    //   Atmosphere::Initialize(1,…) 0x793390; backend path faithful to SL_Renderer_LoadBackendDLL 0x71F300).
    bool Init(IDirect3DDevice9* device, const char* resourceDir);

    // Pushes the zone's time/geographic position (real .ATM file already parsed) into the
    // engine's AtmosphericConditions. No-op in fallback mode.
    // ex-VeryOldClient: CAtmosphere::SetTimeAndLocation — CONFIRMED (SetLocation/SetTime; values = zone
    //   .ATM, Atmosphere_Deserialize 0x795A40).
    void UpdateTime(const asset::AtmosphereFile& atm);

    // Sky render BEFORE world geometry: SetCameraMatrix/SetProjectionMatrix + BeginFrame
    // (sky/sun/moon/stars). Window = Env_UpdateFrame 0x412550 in the original. No-op without engine.
    // ex-VeryOldClient: CAtmosphere::StartRender (SetViewProjectionMatrix + BeginFrame) — CONFIRMED,
    //   cf. cAtmosphere_RenderFrame 0x793B80.
    void RenderSkyBefore(const Camera& camera);

    // Clouds/precipitation render AFTER the scene: EndFrame (back-to-front sort). No-op without engine.
    // ex-VeryOldClient: CAtmosphere::EndRender (Atmosphere::EndFrame) — CONFIRMED, cf. Atmosphere_DrawFrame 0x794FE0.
    void RenderSkyAfter(const Camera& camera);

    // Destroys the Atmosphere object (dtor unloads the backend via ShutdownShaderSystem) / releases state.
    // ex-VeryOldClient: CAtmosphere::Destroy — CONFIRMED, cf. cAtmosphere_dtor 0x791C40.
    void Shutdown();

    // --- Diagnostics (tests, sanity logs) -------------------------------------------------
    // ready_: true only if the real engine is initialized (always false in fallback mode).
    bool Ready() const { return ready_; }
    // backendDllFound_: the redistributable backend was loaded + its exports resolved successfully
    // (useful to distinguish "DLL missing" from "DLL present but static engine missing").
    bool BackendDllFound() const { return backendDllFound_; }

private:
#if TS2_SILVERLINING_ENGINE_AVAILABLE
    // Derives sun lighting (D3DLIGHT9) + GXD fog from the SDK Atmosphere object and pushes them
    // to the device — called by RenderSkyBefore AFTER BeginFrame (window = right after
    // cAtmosphere_RenderFrame in Env_UpdateFrame 0x412550). Faithful to Env_UpdateSunLight 0x412210 +
    // Env_UpdateFogState 0x412370. No-op outside engine mode (method not declared).
    void applySceneLightingAndFog();
#endif

    IDirect3DDevice9* dev_ = nullptr;
    bool ready_ = false;           // real engine initialized (never true in fallback mode)
    bool backendDllFound_ = false; // backend DLL actually loaded+resolved (diagnostic)

    // Zone render flags (recorded by UpdateTime, consumed by RenderSkyBefore in engine mode —
    // faithful to cAtmosphere_RenderFrame @+352/@+642, cf. TS2_SILVERLINING_CONFIG.md §3.3).
    bool skipCelestial_ = false;   // AtmosphereFile::RenderFlagSkipCelestial() (@+352) — CONFIRMED: @+352 gates
                                   //   the entire sun/moon/stars/AtmoFromSpace block in 0x793B80 (if(!@+352)).
    bool forceBlackSky_ = false;   // AtmosphereFile::RenderFlagForceBlackSky() (@+642) — CONFIRMED: @+642 forces
                                   //   the 3 sky/horizon samples to (0,0,0,1) in 0x793B80.
                                   //   (.ATM flags @+643/@+644 still unidentified, cf. CONFIG §3.2 — no VeryOldClient hint.)

    // Opaque SilverLining::Atmosphere*: kept as void* so this header does NOT require the SDK
    // include (the module compiles without external/SilverLining/Include). Used only in engine mode.
    void* atmosphere_ = nullptr;
};

} // namespace ts2::gfx

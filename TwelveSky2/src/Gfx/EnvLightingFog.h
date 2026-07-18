// Gfx/EnvLightingFog.h — PER-FRAME applicator for the directional sun light (D3DLIGHT9)
// + fixed-function exponential fog, reproducing BIT-EXACT the device MECHANICS of:
//   • Env_UpdateSunLight  0x412210 : builds a directional D3DLIGHT9 (sun) then SetLight(0,&l)
//   • Env_UpdateFogState  0x412370 : sets the per-pixel EXP fog (RS 28/34/35=EXP/38)
// both called every in-game frame by Env_UpdateFrame 0x412550 (right after the SilverLining sky
// render: cf. Docs/TS2_DEEP_MATERIALS_FX.md §5/§6 and the order @0x52d30d).
//
// === WHY THIS MODULE (the actionable gap) =========================================================
//   In the binary, the sun's COLORS and DIRECTION, as well as the fog color/density, are
//   derived from wrappers INTERNAL to the SilverLining SDK:
//     - cAtmosphere_GetSunColorFaded   0x7938A0  (Diffuse "faded")
//     - cAtmosphere_GetHorizonColorFaded 0x793AB0 (Ambient "faded")
//     - cAtmosphere_GetSunDirectionA   0x7904D0  (sun direction)
//     - cAtmosphere_GetSunColor/IsSunUp/GetColorAtDirection/GetColorBasePtr (fog, density 8435 m)
//   This SDK (SilverLining-MT.lib static engine layer) is NON-redistributable and ABSENT from the repo ->
//   outside `TS2_SILVERLINING_ENGINE_AVAILABLE`, Gfx/SilverLiningSky::applySceneLightingAndFog does NOT
//   run, so NO directional light or fog is set on the live path. This module fills that gap by applying
//   the device MECHANICS (proven bit-exact), driven by a REAL SOURCE already available on the C++
//   side — WITHOUT inventing any sun position/color.
//
// === "INVENT NOTHING" RULE (source choice) ===================================================
//   The source of the VALUES (never fabricated here) is, in order of fidelity:
//     1. A complete EnvLightingFogState supplied by a real atmospheric source (e.g. SilverLiningSky
//        in engine mode, or a future ephemeris solver) — full sun+fog path.
//     2. Failing that: the DEFAULT D3DLIGHT9 that GxdRenderer builds (GxdRenderer::Light(),
//        built by GXD_DeviceReinit (default light @0x402711): Diffuse 0.7 / Ambient 0.3 /
//        Direction normalize(-1,-1,1)). This is a GENUINE engine ARTIFACT, not an invented value.
//        -> documented NEUTRAL fallback: apply the light mechanics with this light, and FOG OFF
//          (no fog color/density is faithfully reproducible without SilverLining — see
//          Docs/TS2_DEEP_MATERIALS_FX.md §13, TODO RE T-16 "colorBase.z / 8435").
//   The module does NOT compute solar azimuth/elevation (neither does SkyRenderer: cf. its banner): any
//   sun direction comes from a supplied source, never guessed.
//
// === STANDALONE MODULE (no wiring) =================================================================
//   This header depends on NO other client module (just <d3d9.h>). The caller (FLEET/MAIN) chooses
//   the source and wires the call into the in-game frame — mirroring Env_UpdateFrame @0x52d30d, AFTER
//   the sky render / GxdRenderer::SetupFrame (which already sets light 0 = default) and BEFORE the
//   normal-mapped scenery. Creates NO D3DPOOL_DEFAULT resource (only SetLight/SetRenderState) -> NOTHING
//   to release/recreate on device-lost: the applicator is stateless, just call it again next frame.
#pragma once

#include <d3d9.h>

namespace ts2::gfx {

// EnvLightingFogState — the "source" consumed by ApplyPerFrame: ONE frame's lighting+fog state.
// Each field traces its anchor in Env_UpdateSunLight 0x412210 / Env_UpdateFogState 0x412370.
struct EnvLightingFogState {
    // --- Directional sun light — Env_UpdateSunLight 0x412210 mechanics ---
    bool      applySun     = true;  // false => touches NEITHER light 0 NOR RS LIGHTING (leaves current state)
    D3DLIGHT9 sun{};                // Type=DIRECTIONAL(3), Diffuse=sun (a=1), Specular=white, Ambient=horizon (a=1),
                                    //   Direction = -normalize(sunDir). See BuildSunLight() for layout §5.1.
    bool      enableLight  = true;  // LightEnable(0, TRUE) — mirrors the live frame-setup (GXD_SetDirectionalLight
                                    //   0x403980 sets vtbl+212=LightEnable(0,1) ; GxdRenderer::SetupFrame likewise).
                                    //   NB: 0x412210 itself ONLY does SetLight(0) — cf. .cpp comment.
    bool      setLightingRS = true; // SetRenderState(D3DRS_LIGHTING=137, TRUE) — same origin (frame-setup), required
                                    //   for the sun to light the normal-mapped FF geometry (.WO objects/scenery).

    // --- Per-pixel EXP fog — Env_UpdateFogState 0x412370 mechanics ---
    //   NEUTRAL BY DEFAULT: fogEnabled=false => the applicator sets RS 28 FOGENABLE=FALSE. The binary
    //   ALWAYS enables in-game fog; but its color/density come from SilverLining (absent) -> we only
    //   reproduce it with a real source. Without one, we cleanly disable it (documented fallback).
    bool      fogEnabled = false;
    D3DCOLOR  fogColor   = 0x00000000; // 0x00RRGGBB — composed by PackFogColor(), alpha ignored by D3DRS_FOGCOLOR (@0x412511)
    float     fogDensity = 0.0f;       // D3DRS_FOGDENSITY (float bits) — @0x41253e
};

// EnvLightingFog — stateless device applicator (static methods).
class EnvLightingFog {
public:
    // PER-FRAME applicator (full form). Reproduces bit-exact:
    //   Env_UpdateSunLight 0x412210 : SetLight(0,&src.sun)  [+ LightEnable(0)/RS137 per src, cf. .cpp]
    //   Env_UpdateFogState 0x412370 : if src.fogEnabled -> RS 28=TRUE / 34=color / 35=EXP / 38=density ;
    //                                 else -> RS 28 FOGENABLE=FALSE (neutral fallback).
    // dev null or src.applySun==false: each part is handled independently (see .cpp). No effect if dev==nullptr.
    static void ApplyPerFrame(IDirect3DDevice9* dev, const EnvLightingFogState& src);

    // PER-FRAME applicator (NEUTRAL FALLBACK form, source = real D3DLIGHT9). Applies the light MECHANICS
    // with `defaultSunLight` (intended to receive GxdRenderer::Light() — the engine's GENUINE default
    // light) and leaves FOG OFF. Equivalent to ApplyPerFrame(dev, MakeNeutral(defaultSunLight)).
    static void ApplyPerFrame(IDirect3DDevice9* dev, const D3DLIGHT9& defaultSunLight);

    // Builds a documented NEUTRAL state: sun = defaultSunLight (expected = GxdRenderer::Light()), fog OFF.
    static EnvLightingFogState MakeNeutral(const D3DLIGHT9& defaultSunLight);

    // Assembles the sun D3DLIGHT9 EXACTLY like Env_UpdateSunLight 0x412210 (mapping §5.1), from
    // values SUPPLIED by a real atmospheric source (no invention: these are INPUTS):
    //   sunColor[3]     -> Diffuse.rgb, Diffuse.a=1        (dword_18C535C..5368, @0x412266)
    //   horizonColor[3] -> Ambient.rgb, Ambient.a=1        (dword_18C537C..5388, @0x41229a)
    //   sunDir[3]       -> Direction = -normalize(sunDir)  (Vec3_Normalize @0x41226d then negation @0x4122cb)
    //   Specular = (1,1,1,1) white                         (dword_18C536C..5378)
    //   Type = D3DLIGHT_DIRECTIONAL (3)                    (@0x412329)
    // Position/Range/Falloff/Attenuations/Theta/Phi = 0 (memset 0x68, @0x41227b).
    static D3DLIGHT9 BuildSunLight(const float sunColor[3],
                                   const float horizonColor[3],
                                   const float sunDir[3]);

    // Composes the fog color like @0x4124f6 (EXACT reproduction of the binary's expression; the top
    // byte comes out 0xFF but D3DRS_FOGCOLOR only uses RGB). r/g/b in [0,1].
    static D3DCOLOR PackFogColor(float r, float g, float b);
};

} // namespace ts2::gfx

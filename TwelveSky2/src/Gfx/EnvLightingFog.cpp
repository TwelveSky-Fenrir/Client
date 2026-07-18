// Gfx/EnvLightingFog.cpp — cf. header banner. Reproduces the device mechanics of
//   Env_UpdateSunLight 0x412210 and Env_UpdateFogState 0x412370 (Env_UpdateFrame 0x412550 chain).
// Each device call carries its IDA anchor (0xADDR). No invented value: the colors/direction
// are INPUTS supplied by a real source; the neutral fallback = GxdRenderer default light + fog off.
#include "Gfx/EnvLightingFog.h"

#include <cmath>

namespace ts2::gfx {

// PackFogColor — EXACT reproduction of the fog color composition @0x4124f6.
//   Binary:
//     v15 = (int)(255.0 * B);
//     v5  = (u8)v15 | (((u8)(int)(G*255.0) | (((int)(R*255.0) | 0xFFFFFF00) << 8)) << 8);
//   Effective result: 0xFFRRGGBB (top byte forced to 0xFF by the OR 0xFFFFFF00). D3DRS_FOGCOLOR
//   only uses the 24 RGB bits — alpha is ignored (cf. Docs/TS2_DEEP_MATERIALS_FX.md §6.1).
D3DCOLOR EnvLightingFog::PackFogColor(float r, float g, float b) {
    const int          rI = static_cast<int>(r * 255.0f);            // (int)(R*255)
    const unsigned char gB = static_cast<unsigned char>(static_cast<int>(g * 255.0f)); // (u8)(int)(G*255)
    const unsigned char bB = static_cast<unsigned char>(static_cast<int>(b * 255.0f)); // (u8)(int)(B*255)  (= v15)
    // Expression byte-for-byte identical to the binary (guarantees a bit-exact FOGCOLOR register).
    return static_cast<D3DCOLOR>(
        bB | ((gB | ((rI | static_cast<int>(0xFFFFFF00)) << 8)) << 8));
}

// BuildSunLight — assembles the sun D3DLIGHT9 like Env_UpdateSunLight 0x412210 (mapping §5.1).
//   memset 0x68 @0x41227b -> all fields to 0 (Position/Range/Falloff/Atten/Theta/Phi stay null).
//   Type=3 DIRECTIONAL @0x412329 ; Diffuse=sunColor a=1 ; Specular white ; Ambient=horizon a=1 ;
//   Direction = -normalize(sunDir) (Vec3_Normalize @0x41226d then negation @0x4122cb/0x4122f2/0x41230b).
D3DLIGHT9 EnvLightingFog::BuildSunLight(const float sunColor[3],
                                        const float horizonColor[3],
                                        const float sunDir[3]) {
    D3DLIGHT9 l{};                     // value-init = all fields to 0 (Crt_Memset 0x68 @0x41227b)

    l.Type = D3DLIGHT_DIRECTIONAL;     // dword_18C5358 = 3  @0x412329

    // Diffuse = sun color ("faded" in the original), alpha = 1.0 (@0x412266).
    l.Diffuse.r = sunColor[0];         // dword_18C535C  @0x4122df
    l.Diffuse.g = sunColor[1];         // dword_18C5360  @0x4122c1
    l.Diffuse.b = sunColor[2];         // dword_18C5364  @0x4122e8
    l.Diffuse.a = 1.0f;                // dword_18C5368  (v16=1.0 @0x412266)

    // Specular = white (dword_18C536C..5378 = 1.0/1.0/1.0/1.0, @0x412305..0x412333).
    l.Specular.r = 1.0f;
    l.Specular.g = 1.0f;
    l.Specular.b = 1.0f;
    l.Specular.a = 1.0f;

    // Ambient = horizon color ("faded" in the original), alpha = 1.0 (@0x41229a).
    l.Ambient.r = horizonColor[0];     // dword_18C537C  @0x4122ae
    l.Ambient.g = horizonColor[1];     // dword_18C5380  @0x4122b8
    l.Ambient.b = horizonColor[2];     // dword_18C5384  @0x41229e
    l.Ambient.a = 1.0f;                // dword_18C5388  (v12=1.0 @0x412261)

    // Direction = -normalize(sunDir). Faithful to Vec3_Normalize(&v3,&v3) @0x41226d then -v3/-v4/-v5.
    float dx = sunDir[0], dy = sunDir[1], dz = sunDir[2];
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 0.0f) { dx /= len; dy /= len; dz /= len; } // Vec3_Normalize (anti-NaN guard for a null vector)
    l.Direction.x = -dx;               // dword_18C5398  @0x4122cb / @0x412339
    l.Direction.y = -dy;               // dword_18C539C  @0x4122f2
    l.Direction.z = -dz;               // dword_18C53A0  @0x41230b
    return l;
}

// MakeNeutral — documented NEUTRAL fallback: sun = GxdRenderer default light (GENUINE, @0x402711),
//   fog OFF. No fabricated value: `defaultSunLight` is an engine artifact supplied by the caller.
EnvLightingFogState EnvLightingFog::MakeNeutral(const D3DLIGHT9& defaultSunLight) {
    EnvLightingFogState s;
    s.applySun      = true;
    s.sun           = defaultSunLight; // = GxdRenderer::Light() (Diffuse 0.7 / Ambient 0.3 / Dir norm(-1,-1,1))
    s.enableLight   = true;            // mirrors GXD_SetDirectionalLight 0x403980 (LightEnable(0,1))
    s.setLightingRS = true;            // RS 137 LIGHTING for FF lighting of normal-mapped scenery
    s.fogEnabled    = false;           // neutral fallback: no faithful color/density source (without SilverLining)
    return s;
}

// ApplyPerFrame (full state) — sets the device mechanics for the frame.
void EnvLightingFog::ApplyPerFrame(IDirect3DDevice9* dev, const EnvLightingFogState& src) {
    if (!dev) return;

    // ---- Directional sun light — Env_UpdateSunLight 0x412210 ----
    // The binary ONLY does SetLight(0,&light) via renderer vtbl+204 (@0x412367). LightEnable(0,TRUE)
    // and RS 137 LIGHTING come from the GXD engine's live frame-setup (GXD_SetDirectionalLight
    // 0x403980 sets vtbl+212=LightEnable(0,1) ; D3DRS_LIGHTING=TRUE set by the live device setup
    // ~0x405B32 — NOT by GxdRenderer::SetupFrame, which is DEAD, cf. GxdRenderer.cpp). Reproduced here
    // so the module is self-sufficient. Each part is governed by a flag from the source state.
    if (src.applySun) {
        dev->SetLight(0, &src.sun);                         // SetLight(index 0)  @0x412367 (vtbl+204)
        if (src.enableLight)  dev->LightEnable(0, TRUE);    // LightEnable(0,1)    (GXD_SetDirectionalLight vtbl+212)
        if (src.setLightingRS) dev->SetRenderState(D3DRS_LIGHTING, TRUE); // RS 137 = 1
    }

    // ---- Per-pixel EXP fog — Env_UpdateFogState 0x412370 ----
    // The binary always writes these 4 RS via g_GxdRenderer_pDevice (same physical device) vtbl+228.
    // Neutral fallback: if no faithful source (fogEnabled==false), properly DISABLE it (RS 28=FALSE).
    if (src.fogEnabled) {
        dev->SetRenderState(D3DRS_FOGENABLE, TRUE);                         // RS 28 = 1        @0x4124fe
        dev->SetRenderState(D3DRS_FOGCOLOR, src.fogColor);                  // RS 34 = RRGGBB   @0x412511
        dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_EXP);                // RS 35 = 1 (EXP)  @0x412525
        dev->SetRenderState(D3DRS_FOGDENSITY,
                            *reinterpret_cast<const DWORD*>(&src.fogDensity)); // RS 38 (float bits) @0x41253e
    } else {
        dev->SetRenderState(D3DRS_FOGENABLE, FALSE);                        // documented neutral fallback (fog off)
    }
}

// ApplyPerFrame (neutral fallback, source = real D3DLIGHT9) — default path without SilverLining.
void EnvLightingFog::ApplyPerFrame(IDirect3DDevice9* dev, const D3DLIGHT9& defaultSunLight) {
    ApplyPerFrame(dev, MakeNeutral(defaultSunLight));
}

} // namespace ts2::gfx

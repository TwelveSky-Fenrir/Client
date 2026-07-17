// Gfx/EnvLightingFog.cpp — cf. bandeau du header. Reproduit les mécaniques device de
//   Env_UpdateSunLight 0x412210 et Env_UpdateFogState 0x412370 (chaîne Env_UpdateFrame 0x412550).
// Chaque appel device porte son ancre IDA (0xADDR). Aucune valeur inventée : les couleurs/direction
// sont des ENTRÉES fournies par une source réelle ; le repli neutre = light par défaut GxdRenderer + fog off.
#include "Gfx/EnvLightingFog.h"

#include <cmath>

namespace ts2::gfx {

// =====================================================================================================
// PackFogColor — reproduction EXACTE de la composition de couleur de fog @0x4124f6.
//   Binaire :
//     v15 = (int)(255.0 * B);
//     v5  = (u8)v15 | (((u8)(int)(G*255.0) | (((int)(R*255.0) | 0xFFFFFF00) << 8)) << 8);
//   Résultat effectif : 0xFFRRGGBB (haut octet forcé à 0xFF par le OR 0xFFFFFF00). D3DRS_FOGCOLOR
//   n'utilise que les 24 bits RGB — l'alpha est ignorée (cf. Docs/TS2_DEEP_MATERIALS_FX.md §6.1).
// =====================================================================================================
D3DCOLOR EnvLightingFog::PackFogColor(float r, float g, float b) {
    const int          rI = static_cast<int>(r * 255.0f);            // (int)(R*255)
    const unsigned char gB = static_cast<unsigned char>(static_cast<int>(g * 255.0f)); // (u8)(int)(G*255)
    const unsigned char bB = static_cast<unsigned char>(static_cast<int>(b * 255.0f)); // (u8)(int)(B*255)  (= v15)
    // Expression byte-pour-byte identique au binaire (garantit un registre FOGCOLOR bit-exact).
    return static_cast<D3DCOLOR>(
        bB | ((gB | ((rI | static_cast<int>(0xFFFFFF00)) << 8)) << 8));
}

// =====================================================================================================
// BuildSunLight — assemble le D3DLIGHT9 soleil comme Env_UpdateSunLight 0x412210 (mapping §5.1).
//   memset 0x68 @0x41227b -> tous les champs à 0 (Position/Range/Falloff/Atten/Theta/Phi restent nuls).
//   Type=3 DIRECTIONAL @0x412329 ; Diffuse=sunColor a=1 ; Specular blanc ; Ambient=horizon a=1 ;
//   Direction = -normalize(sunDir) (Vec3_Normalize @0x41226d puis négation @0x4122cb/0x4122f2/0x41230b).
// =====================================================================================================
D3DLIGHT9 EnvLightingFog::BuildSunLight(const float sunColor[3],
                                        const float horizonColor[3],
                                        const float sunDir[3]) {
    D3DLIGHT9 l{};                     // value-init = tous champs à 0 (Crt_Memset 0x68 @0x41227b)

    l.Type = D3DLIGHT_DIRECTIONAL;     // dword_18C5358 = 3  @0x412329

    // Diffuse = couleur soleil (« faded » dans l'original), alpha = 1.0 (@0x412266).
    l.Diffuse.r = sunColor[0];         // dword_18C535C  @0x4122df
    l.Diffuse.g = sunColor[1];         // dword_18C5360  @0x4122c1
    l.Diffuse.b = sunColor[2];         // dword_18C5364  @0x4122e8
    l.Diffuse.a = 1.0f;                // dword_18C5368  (v16=1.0 @0x412266)

    // Specular = blanc (dword_18C536C..5378 = 1.0/1.0/1.0/1.0, @0x412305..0x412333).
    l.Specular.r = 1.0f;
    l.Specular.g = 1.0f;
    l.Specular.b = 1.0f;
    l.Specular.a = 1.0f;

    // Ambient = couleur horizon (« faded » dans l'original), alpha = 1.0 (@0x41229a).
    l.Ambient.r = horizonColor[0];     // dword_18C537C  @0x4122ae
    l.Ambient.g = horizonColor[1];     // dword_18C5380  @0x4122b8
    l.Ambient.b = horizonColor[2];     // dword_18C5384  @0x41229e
    l.Ambient.a = 1.0f;                // dword_18C5388  (v12=1.0 @0x412261)

    // Direction = -normalize(sunDir). Fidèle à Vec3_Normalize(&v3,&v3) @0x41226d puis -v3/-v4/-v5.
    float dx = sunDir[0], dy = sunDir[1], dz = sunDir[2];
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (len > 0.0f) { dx /= len; dy /= len; dz /= len; } // Vec3_Normalize (garde anti-NaN si vecteur nul)
    l.Direction.x = -dx;               // dword_18C5398  @0x4122cb / @0x412339
    l.Direction.y = -dy;               // dword_18C539C  @0x4122f2
    l.Direction.z = -dz;               // dword_18C53A0  @0x41230b
    return l;
}

// =====================================================================================================
// MakeNeutral — repli NEUTRE documenté : sun = light par défaut GxdRenderer (GENUINE, @0x402711),
//   fog OFF. Aucune valeur fabriquée : `defaultSunLight` est un artefact du moteur fourni par l'appelant.
// =====================================================================================================
EnvLightingFogState EnvLightingFog::MakeNeutral(const D3DLIGHT9& defaultSunLight) {
    EnvLightingFogState s;
    s.applySun      = true;
    s.sun           = defaultSunLight; // = GxdRenderer::Light() (Diffuse 0.7 / Ambient 0.3 / Dir norm(-1,-1,1))
    s.enableLight   = true;            // miroir GXD_SetDirectionalLight 0x403980 (LightEnable(0,1))
    s.setLightingRS = true;            // RS 137 LIGHTING pour l'éclairage FF du décor à normales
    s.fogEnabled    = false;           // repli neutre : pas de source fidèle de couleur/densité (sans SilverLining)
    return s;
}

// =====================================================================================================
// ApplyPerFrame (état complet) — pose la mécanique device pour la frame.
// =====================================================================================================
void EnvLightingFog::ApplyPerFrame(IDirect3DDevice9* dev, const EnvLightingFogState& src) {
    if (!dev) return;

    // ---- Lumière solaire directionnelle — Env_UpdateSunLight 0x412210 ----
    // Le binaire ne fait QUE SetLight(0,&light) via renderer vtbl+204 (@0x412367). LightEnable(0,TRUE)
    // et RS 137 LIGHTING proviennent du frame-setup vivant (GXD_SetDirectionalLight 0x403980 pose
    // vtbl+212=LightEnable(0,1) ; GxdRenderer::SetupFrame pose les deux) — reproduits ici pour que le
    // module soit auto-suffisant. Chaque volet est gouverné par un flag de l'état source.
    if (src.applySun) {
        dev->SetLight(0, &src.sun);                         // SetLight(index 0)  @0x412367 (vtbl+204)
        if (src.enableLight)  dev->LightEnable(0, TRUE);    // LightEnable(0,1)    (GXD_SetDirectionalLight vtbl+212)
        if (src.setLightingRS) dev->SetRenderState(D3DRS_LIGHTING, TRUE); // RS 137 = 1
    }

    // ---- Fog EXP par-pixel — Env_UpdateFogState 0x412370 ----
    // Le binaire écrit toujours ces 4 RS via g_GxdRenderer_pDevice (même device physique) vtbl+228.
    // Repli neutre : si aucune source fidèle (fogEnabled==false), on DÉSACTIVE proprement (RS 28=FALSE).
    if (src.fogEnabled) {
        dev->SetRenderState(D3DRS_FOGENABLE, TRUE);                         // RS 28 = 1        @0x4124fe
        dev->SetRenderState(D3DRS_FOGCOLOR, src.fogColor);                  // RS 34 = RRGGBB   @0x412511
        dev->SetRenderState(D3DRS_FOGTABLEMODE, D3DFOG_EXP);                // RS 35 = 1 (EXP)  @0x412525
        dev->SetRenderState(D3DRS_FOGDENSITY,
                            *reinterpret_cast<const DWORD*>(&src.fogDensity)); // RS 38 (bits float) @0x41253e
    } else {
        dev->SetRenderState(D3DRS_FOGENABLE, FALSE);                        // repli neutre documenté (fog off)
    }
}

// =====================================================================================================
// ApplyPerFrame (repli neutre, source = D3DLIGHT9 réel) — chemin par défaut hors SilverLining.
// =====================================================================================================
void EnvLightingFog::ApplyPerFrame(IDirect3DDevice9* dev, const D3DLIGHT9& defaultSunLight) {
    ApplyPerFrame(dev, MakeNeutral(defaultSunLight));
}

} // namespace ts2::gfx

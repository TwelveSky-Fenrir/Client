// Gfx/EnvLightingFog.h — Applicateur PAR FRAME de la lumière solaire directionnelle (D3DLIGHT9)
// + du fog exponentiel fixed-function, reproduisant BIT-EXACT les MÉCANIQUES device de :
//   • Env_UpdateSunLight  0x412210 : construit un D3DLIGHT9 directionnel (soleil) puis SetLight(0,&l)
//   • Env_UpdateFogState  0x412370 : pose le fog EXP par-pixel (RS 28/34/35=EXP/38)
// tous deux appelés chaque frame in-game par Env_UpdateFrame 0x412550 (juste après le rendu du ciel
// SilverLining : cf. Docs/TS2_DEEP_MATERIALS_FX.md §5/§6 et l'ordre @0x52d30d).
//
// === POURQUOI CE MODULE (le gap actionnable) =========================================================
//   Dans le binaire, les COULEURS et la DIRECTION du soleil, ainsi que la couleur/densité du fog, sont
//   dérivées de wrappers INTERNES au SDK SilverLining :
//     - cAtmosphere_GetSunColorFaded   0x7938A0  (Diffuse « faded »)
//     - cAtmosphere_GetHorizonColorFaded 0x793AB0 (Ambient « faded »)
//     - cAtmosphere_GetSunDirectionA   0x7904D0  (direction soleil)
//     - cAtmosphere_GetSunColor/IsSunUp/GetColorAtDirection/GetColorBasePtr (fog, densité 8435 m)
//   Ce SDK (couche moteur statique SilverLining-MT.lib) est NON redistribuable et ABSENT du dépôt →
//   hors `TS2_SILVERLINING_ENGINE_AVAILABLE`, Gfx/SilverLiningSky::applySceneLightingAndFog ne s'exécute
//   PAS, donc AUCUNE lumière directionnelle ni fog n'est posée dans le chemin vivant. Ce module comble
//   ce trou en appliquant la MÉCANIQUE device (prouvée bit-exact), pilotée par une SOURCE RÉELLE déjà
//   disponible côté C++ — SANS inventer de position/couleur de soleil.
//
// === RÈGLE « N'INVENTE RIEN » (choix de la source) ===================================================
//   La source des VALEURS (jamais fabriquée ici) est, par ordre de fidélité :
//     1. Un EnvLightingFogState complet fourni par une source atmosphérique réelle (p.ex. SilverLiningSky
//        en mode moteur, ou un futur solveur d'éphéméride) — chemin plein soleil+fog.
//     2. À défaut : le D3DLIGHT9 PAR DÉFAUT que GxdRenderer construit (GxdRenderer::Light(),
//        = BuildDefaultMaterialAndLight, PROUVÉ bit-exact @0x402711 : Diffuse 0.7 / Ambient 0.3 /
//        Direction normalize(-1,-1,1)). C'est un ARTEFACT GENUINE du moteur, pas une valeur inventée.
//        → repli NEUTRE documenté : on applique la mécanique de lumière avec ce light, et FOG OFF
//          (aucune couleur/densité de fog n'est reproductible fidèlement sans SilverLining — voir
//          Docs/TS2_DEEP_MATERIALS_FX.md §13, TODO RE T-16 « colorBase.z / 8435 »).
//   Le module NE calcule PAS d'azimut/élévation solaire (SkyRenderer non plus : cf. son bandeau) : toute
//   direction de soleil provient d'une source fournie, jamais devinée.
//
// === MODULE AUTONOME (aucun câblage) =================================================================
//   Ce header ne dépend d'AUCUN autre module du client (juste <d3d9.h>). L'appelant (FLOTTE/MAIN) choisit
//   la source et branche l'appel dans la frame in-game — miroir d'Env_UpdateFrame @0x52d30d, APRÈS le
//   rendu du ciel / GxdRenderer::SetupFrame (qui pose déjà light 0 = défaut) et AVANT le décor à normales.
//   Ne crée AUCUNE ressource D3DPOOL_DEFAULT (que des SetLight/SetRenderState) → RIEN à libérer/recréer
//   sur device-lost : l'applicateur est stateless, il suffit de le rappeler à la frame suivante.
#pragma once

#include <d3d9.h>

namespace ts2::gfx {

// -----------------------------------------------------------------------------------------------------
// EnvLightingFogState — la « source » consommée par ApplyPerFrame : l'état d'éclairage+fog d'UNE frame.
// Chaque champ trace son ancre dans Env_UpdateSunLight 0x412210 / Env_UpdateFogState 0x412370.
// -----------------------------------------------------------------------------------------------------
struct EnvLightingFogState {
    // --- Lumière solaire directionnelle — mécanique Env_UpdateSunLight 0x412210 ---
    bool      applySun     = true;  // false => ne touche NI la lumière 0 NI RS LIGHTING (laisse l'état courant)
    D3DLIGHT9 sun{};                // Type=DIRECTIONAL(3), Diffuse=soleil (a=1), Specular=blanc, Ambient=horizon (a=1),
                                    //   Direction = -normalize(dirSoleil). Voir BuildSunLight() pour le layout §5.1.
    bool      enableLight  = true;  // LightEnable(0, TRUE) — miroir du frame-setup vivant (GXD_SetDirectionalLight
                                    //   0x403980 pose vtbl+212=LightEnable(0,1) ; GxdRenderer::SetupFrame idem).
                                    //   NB : 0x412210 lui-même ne fait QUE SetLight(0) — cf. commentaire .cpp.
    bool      setLightingRS = true; // SetRenderState(D3DRS_LIGHTING=137, TRUE) — même origine (frame-setup), requis
                                    //   pour que le soleil éclaire la géométrie FF à normales (objets .WO/décor).

    // --- Fog EXP par-pixel — mécanique Env_UpdateFogState 0x412370 ---
    //   NEUTRE PAR DÉFAUT : fogEnabled=false → l'applicateur pose RS 28 FOGENABLE=FALSE. Le binaire, lui,
    //   ACTIVE toujours le fog in-game ; mais sa couleur/densité viennent de SilverLining (absent) → on ne
    //   les reproduit qu'avec une source réelle. Sans elle, on désactive proprement (repli documenté).
    bool      fogEnabled = false;
    D3DCOLOR  fogColor   = 0x00000000; // 0x00RRGGBB — composé par PackFogColor(), alpha ignorée par D3DRS_FOGCOLOR (@0x412511)
    float     fogDensity = 0.0f;       // D3DRS_FOGDENSITY (bits float) — @0x41253e
};

// -----------------------------------------------------------------------------------------------------
// EnvLightingFog — applicateur device stateless (méthodes statiques).
// -----------------------------------------------------------------------------------------------------
class EnvLightingFog {
public:
    // Applicateur PAR FRAME (forme complète). Reproduit bit-exact :
    //   Env_UpdateSunLight 0x412210 : SetLight(0,&src.sun)  [+ LightEnable(0)/RS137 selon src, cf. .cpp]
    //   Env_UpdateFogState 0x412370 : si src.fogEnabled → RS 28=TRUE / 34=color / 35=EXP / 38=density ;
    //                                 sinon → RS 28 FOGENABLE=FALSE (repli neutre).
    // dev nul ou src.applySun==false : gère chaque volet indépendamment (voir .cpp). Aucun effet si dev==nullptr.
    static void ApplyPerFrame(IDirect3DDevice9* dev, const EnvLightingFogState& src);

    // Applicateur PAR FRAME (forme REPLI NEUTRE, source = D3DLIGHT9 réel). Passe la MÉCANIQUE de lumière
    // avec `defaultSunLight` (destiné à recevoir GxdRenderer::Light() — le light par défaut GENUINE du
    // moteur) et laisse le FOG OFF. Équivaut à ApplyPerFrame(dev, MakeNeutral(defaultSunLight)).
    static void ApplyPerFrame(IDirect3DDevice9* dev, const D3DLIGHT9& defaultSunLight);

    // Fabrique un état NEUTRE documenté : sun = defaultSunLight (attendu = GxdRenderer::Light()), fog OFF.
    static EnvLightingFogState MakeNeutral(const D3DLIGHT9& defaultSunLight);

    // Assemble le D3DLIGHT9 soleil EXACTEMENT comme Env_UpdateSunLight 0x412210 (mapping §5.1), à partir
    // de valeurs FOURNIES par une source atmosphérique réelle (aucune invention : ce sont des ENTRÉES) :
    //   sunColor[3]     -> Diffuse.rgb, Diffuse.a=1        (dword_18C535C..5368, @0x412266)
    //   horizonColor[3] -> Ambient.rgb, Ambient.a=1        (dword_18C537C..5388, @0x41229a)
    //   sunDir[3]       -> Direction = -normalize(sunDir)  (Vec3_Normalize @0x41226d puis négation @0x4122cb)
    //   Specular = (1,1,1,1) blanc                         (dword_18C536C..5378)
    //   Type = D3DLIGHT_DIRECTIONAL (3)                    (@0x412329)
    // Position/Range/Falloff/Atténuations/Theta/Phi = 0 (memset 0x68, @0x41227b).
    static D3DLIGHT9 BuildSunLight(const float sunColor[3],
                                   const float horizonColor[3],
                                   const float sunDir[3]);

    // Compose la couleur de fog comme @0x4124f6 (reproduction EXACTE de l'expression du binaire ; le haut
    // octet ressort à 0xFF mais D3DRS_FOGCOLOR n'utilise que RGB). r/g/b dans [0,1].
    static D3DCOLOR PackFogColor(float r, float g, float b);
};

} // namespace ts2::gfx

// Gfx/SilverLiningSky.cpp — voir SilverLiningSky.h pour le bandeau complet (constat moteur absent,
// gating, sémantique). Deux chemins mutuellement exclusifs :
//   • TS2_SILVERLINING_ENGINE_AVAILABLE == 1 : appelle la VRAIE API SilverLining::Atmosphere
//     (nécessite external/SilverLining/Include au chemin d'inclusion + link SilverLining-MT.lib).
//   • TS2_SILVERLINING_ENGINE_AVAILABLE == 0 (défaut) : charge/sonde le backend DLL réel puis
//     signale EngineUnavailable ; les autres méthodes sont des no-op documentées.
#include "Gfx/SilverLiningSky.h"
#include "Asset/AtmosphereFile.h"
#include "Core/Log.h"

#include <string>

#if TS2_SILVERLINING_ENGINE_AVAILABLE
// --- Mode moteur UNIQUEMENT : ces en-têtes ne sont PAS requis pour compiler le module en repli. ---
// Ajout .vcxproj requis pour ce mode : AdditionalIncludeDirectories += $(ProjectDir)external\SilverLining\Include
#include "Gfx/Camera.h"          // BuildViewMatrix / BuildProjMatrix (D3DXMATRIX)
#include "Atmosphere.h"          // SilverLining::Atmosphere
#include "AtmosphericConditions.h"
#include "Location.h"
#include "LocalTime.h"
#endif

namespace ts2::gfx {

#if !TS2_SILVERLINING_ENGINE_AVAILABLE
namespace {
// Sous-chemin du backend DirectX9, fidèle à SL_Renderer_LoadBackendDLL 0x71F300 (case 1 = DIRECTX9) :
// le loader d'origine fait Str_Append(resourceDir, "VC9/") puis "win32/" puis le nom de la DLL.
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
        TS2_ERR("SilverLiningSky::Init : device D3D9 nul.");
        return false;
    }
    // resourceDir = racine « Resources » du SDK (contient VC9/win32 + SilverLining.config). Repli
    // sur "." si non fourni (comme le défaut ".\\Resources" documenté par Atmosphere::Initialize).
    const char* baseDir = (resourceDir && resourceDir[0]) ? resourceDir : ".";

#if TS2_SILVERLINING_ENGINE_AVAILABLE
    // ---------------------------------------------------------------------------------
    // MODE MOTEUR (activable seulement avec SilverLining-MT.lib fournie hors dépôt).
    // ---------------------------------------------------------------------------------
    // Licence RÉELLE relevée dans cAtmosphere_ctor 0x791B40 (cf. TS2_SILVERLINING_PIPELINE.md §1).
    // CONFIRMED — décompilation 0x791B40 : SilverLining_ValidateLicense(a3=userName, a4=hexKey) reçoit
    //   EXACTEMENT ce couple userName/clé (args vérifiés dans l'IDB) ; le module réutilise les mêmes chaînes.
    // ex-VeryOldClient: SILVERLINING_LICENSE (macro consommée par CAtmosphere::Create → new Atmosphere(...))
    auto* atmo = new SilverLining::Atmosphere("ALT1 License 3", "113e355254250a02094e32165441");

    // renderer = DIRECTX9 (case 1), rightHanded = false (repère GXD gauche, cf. Camera.h LookAtLH),
    // environment = device D3D9. Fidèle à cAtmosphere_Initialize 0x793390.
    // CONFIRMED — 0x793390 est appelé en Initialize(1, "G03_GDATA\\D11_ATMOSPHERE\\", 0, device) ; l'enum
    //   DIRECTX9 == 1 concorde. // ex-VeryOldClient: CAtmosphere::Create → Atmosphere::Initialize(1,…)
    const int err = atmo->Initialize(SilverLining::Atmosphere::DIRECTX9, baseDir,
                                     /*rightHanded*/ false, device);
    // ex-VeryOldClient: Atmosphere::InitializeErrors::E_NOERROR (code retour testé par CAtmosphere::Create) — 0x793390
    if (err != SilverLining::Atmosphere::E_NOERROR) {
        TS2_ERR("SilverLiningSky::Init : Atmosphere::Initialize a echoue (code=%d).", err);
        delete atmo;
        return false;
    }

    // TODO MOTEUR : Atmosphere::Initialize (Atmosphere.h l.370-376) EXIGE un device créé avec
    // D3DPRESENTFLAG_LOCKABLE_BACKBUFFER et SANS D3DCREATE_PUREDEVICE. Audit à faire côté
    // Gfx/Renderer.cpp (Renderer::Init) — hors périmètre de ce module autonome.

    // Lens flare SDK (T-2) : le flare officiel vient du SDK, PAS d'un code maison (D-4 : le
    // DrawForLensFlare de VeryOld est du code mort). L'objet SL_LensFlare est construit par
    // cAtmosphere_Initialize 0x793390 @0x7934ce (LensFlare_Init 0x79A1D0, membre +8 de cAtmosphere) ;
    // on l'active ici via la voie SDK. La clé "disable-lens-flare" du SilverLining.config est lue par
    // le SDK à Initialize — non re-pilotée ici (défaut : activé). // ex-VeryOldClient: CAtmosphere::EnableLensFlare(true)
    atmo->EnableLensFlare(true);                 // Atmosphere.h — voie SDK (cf. TS2_SKY_ROSETTA.md T-2/D-4)

    atmosphere_ = atmo;
    backendDllFound_ = true;
    ready_ = true;
    TS2_LOG("SilverLiningSky::Init : moteur SilverLining initialise (DIRECTX9, resourceDir=\"%s\").", baseDir);
    return true;

#else
    // ---------------------------------------------------------------------------------
    // MODE REPLI (moteur statique SilverLining-MT.lib absent — réalité actuelle du dépôt).
    // On charge quand même le VRAI backend DLL et on résout ses exports réels, EXACTEMENT comme
    // SL_Renderer_LoadBackendDLL 0x71F300, pour prouver que le renderer réel est câblé — puis on
    // signale EngineUnavailable (aucun ciel ne peut être émis sans le moteur haut niveau).
    std::string dllPath = baseDir;
    if (!dllPath.empty()) {
        const char last = dllPath.back();
        if (last != '/' && last != '\\') dllPath += '/';
    }
    dllPath += kBackendSubPath; // -> "<resourceDir>/VC9/win32/SilverLiningDirectX9-MT.dll"

    HMODULE backend = ::LoadLibraryA(dllPath.c_str());
    if (!backend) {
        TS2_WARN("SilverLiningSky::Init : backend DLL introuvable/illisible (\"%s\", err=%lu) "
                 "-> ciel SilverLining indisponible.", dllPath.c_str(), ::GetLastError());
        return false;
    }

    // Résout les 2 exports que le loader d'origine résout aussi (SetContext = dword_186381C,
    // SetEnvironment = dword_1863828). Prouve que la DLL exporte bien l'interface backend attendue.
    const bool hasSetEnvironment = (::GetProcAddress(backend, "SetEnvironment") != nullptr);
    const bool hasSetContext     = (::GetProcAddress(backend, "SetContext") != nullptr);
    backendDllFound_ = (hasSetEnvironment && hasSetContext);

    TS2_LOG("SilverLiningSky::Init : backend \"%s\" charge (SetEnvironment=%s, SetContext=%s).",
            dllPath.c_str(), hasSetEnvironment ? "OK" : "MANQUANT", hasSetContext ? "OK" : "MANQUANT");

    // TODO MOTEUR : le VRAI 0x71F300 appellerait ensuite
    //   SetEnvironment(rightHanded=false, device, resourceLoader)
    // pour obtenir un « context » renderer. Or `resourceLoader` est un SilverLining::ResourceLoader*
    // FOURNI par le moteur statique (SilverLining-MT.lib), ABSENT du dépôt ; sans lui aucun contexte
    // valide ne peut être créé, et la classe SilverLining::Atmosphere (Initialize/BeginFrame/EndFrame)
    // reste non linkable. On NE fabrique PAS d'API Atmosphere factice (règle « N'INVENTE RIEN »).
    // => On décharge le backend et on signale EngineUnavailable ; l'appelant garde son repli gradient.
    ::FreeLibrary(backend);
    TS2_WARN("SilverLiningSky::Init : moteur SilverLining statique absent (SilverLining-MT.lib) "
             "-> EngineUnavailable (Init renvoie false, le repli appelant reste actif).");
    return false;
#endif
}

// =====================================================================================
// Config atmosphère : GLOBALE (1×/session) + PAR ZONE (documentation, cf. TS2_SILVERLINING_CONFIG.md)
// =====================================================================================
// GLOBAL — SilverLining.config lu une seule fois par cAtmosphere_Initialize 0x793390 (via World_LoadMap
//   0x4116B0) : default-turbidity (défaut code 2.0, fichier livré 2.2), atmosphere-height (défaut code
//   100000), enable-atmosphere-from-space (défaut true, fichier "no"), atmosphere-scale-height-meters
//   (RELU PAR FRAME dans cAtmosphere_RenderFrame 0x793B80 → dbl_18636E8). Repli si "Atmosphere.DAT" absent :
//   géo codée en dur Séoul (lat 37.6 / lon 127.0), cf. TS2_SILVERLINING_CONFIG.md §2.1.
//   // ex-VeryOldClient: CAtmosphere::Create (lit "Atmosphere.DAT" sinon SetTimeAndLocation(37.6,127.0,…))
// PAR ZONE — G03_GDATA\D07_GWORLD\Z%03d.ATM chargé à chaque map par World_LoadZoneResource 0x4dcb60
//   (case 7) → Atmosphere_Deserialize 0x795A40 : 5 flags (+0 @+352 skipCelestial, +1 @+642 forceBlackSky,
//   +2 @+643 / +3 @+644 non identifiés, +4 forceToneMapping) + vec3 géo (lat/lon/altitude) + 2×DateTime
//   + 3 pistes de keyframes. 89 fichiers .ATM (tailles 208/284/360/512, contenu diffère par zone).
//   // ex-VeryOldClient: CAtmosphere::Load(pAtmosphereDataPath) (Serialize/Unserialize du même flux)
// =====================================================================================
// UpdateTime
// =====================================================================================
void SilverLiningSky::UpdateTime(const asset::AtmosphereFile& atm) {
    // Mémorise les flags de rendu de la zone dans les deux modes (diagnostic + consommés par
    // RenderSkyBefore en mode moteur). Fidèle à cAtmosphere_RenderFrame @+352/@+642.
    skipCelestial_ = atm.RenderFlagSkipCelestial();
    forceBlackSky_ = atm.RenderFlagForceBlackSky();

#if TS2_SILVERLINING_ENGINE_AVAILABLE
    if (!ready_ || !atmosphere_) return;
    auto* atmo = static_cast<SilverLining::Atmosphere*>(atmosphere_);
    SilverLining::AtmosphericConditions* cond = atmo->GetConditions();

    // Réutilise la lat/lon/altitude + l'heure DÉJÀ parsées par Asset/AtmosphereFile (pas de reparse).
    // CONFIRMED — même séquence Location/LocalTime → GetConditions()->SetLocation/SetTime que l'original ;
    //   source des valeurs = .ATM de zone (Atmosphere_Deserialize 0x795A40, cf. TS2_SILVERLINING_CONFIG.md §3.2).
    // ex-VeryOldClient: CAtmosphere::SetTimeAndLocation (Location/LocalTime → SetLocation/SetTime)
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
    // TODO MOTEUR : turbidité/visibilité proviennent de SilverLining.config (GLOBALE, une fois par
    // session — cf. TS2_SILVERLINING_CONFIG.md §2), PAS du .ATM ; ne rien inventer ici.
#else
    // Mode repli : les flags ci-dessus sont mémorisés (diagnostic) ; il n'y a pas d'objet
    // Atmosphere à alimenter en heure/position. No-op au-delà.
#endif
}

// =====================================================================================
// Vtables SilverLining (C++) — offsets CONFIRMÉS par décompilation (documentation)
// =====================================================================================
// L'objet cAtmosphere (648 o) agrège des sous-objets à vtable C++, invoqués par frame depuis
// cAtmosphere_RenderFrame 0x793B80 / cAtmosphere_DrawObjects 0x792A60. Offsets PROUVÉS (IDA = vérité) :
//   • Sky* @ cAtmosphere+0 (construit par SL_Sky_Construct 0x72BF60) :
//        vtable+8  = maj haze/params ciel          (0x793B80 : (**this + 8)(...))
//        vtable+4  = échantillon couleur ciel       (0x793B80 : (**this + 4)(...))
//        vtable+32 = DrawSky (__stdcall, gaté @+352/drawSky) (0x793B80 : (**this + 32)(...))
//   • Ephemeris* @ +4 (Sky_UpdateCelestialPositions 0x7571C0), LensFlare* @ +8, AtmoFromSpace* @ +16.
//   • Chaque couche nuage de l'arbre rouge-noir RbTreeB : vtable+52 = Draw()
//        (cAtmosphere_DrawObjects 0x792A60 : v10 = *vtbl + 52 ; itère via RbTreeB_IterNext).
//        // ex-VeryOldClient: SilverLining::CloudLayer / CloudLayerFactory (Core/SilverLining/Include ;
//        //   usages CAtmosphere.cpp SetupCirrusClouds/SetupCumulusCongestusClouds).
// NB : la table 0x186381C-0x186397C (g_SL_*) est l'interface C du BACKEND (pointeurs GetProcAddress),
//   PAS une vtable C++ — cf. Docs/TS2_SILVERLINING_PIPELINE.md §2.
//
// =====================================================================================
// applySceneLightingAndFog — éclairage solaire (D3DLIGHT9) + fog GXD dérivés de l'Atmosphere SDK
//   Fidèle à Env_UpdateSunLight 0x412210 + Env_UpdateFogState 0x412370 (appelés par frame depuis
//   Env_UpdateFrame 0x412550, juste après cAtmosphere_RenderFrame). Mode moteur uniquement.
// =====================================================================================
#if TS2_SILVERLINING_ENGINE_AVAILABLE
void SilverLiningSky::applySceneLightingAndFog() {
    if (!ready_ || !atmosphere_ || !dev_) return;
    auto* atmo = static_cast<SilverLining::Atmosphere*>(atmosphere_);

    // ---- Lumière directionnelle solaire — fidèle à Env_UpdateSunLight 0x412210 ----
    // L'original construit un D3DLIGHT9 (0x68 o) à dword_18C5358 : memset 0 (@0x41227b), Type=3
    // DIRECTIONAL (@0x412329), Diffuse = couleur soleil (dword_18C535C.., a=1 @0x412266), Specular =
    // blanc (dword_18C536C..5378 = 1.0), Ambient = couleur horizon (dword_18C537C.., a=1 @0x41229a),
    // Direction = -dirSoleil (dword_18C5398/9C, après Vec3_Normalize 0x41226d puis négation @0x4122cb),
    // puis SetLight(0, &light) via renderer vtable+204 (@0x412367).
    // NOTE FIDÉLITÉ : l'original lit des variantes « faded » internes au jeu (cAtmosphere_GetSunColorFaded
    //   0x7938a0 diffus / cAtmosphere_GetHorizonColorFaded 0x793ab0 ambiant) — NON exposées par le SDK
    //   redistribuable. On passe par les getters SDK natifs prescrits par TS2_SILVERLINING_INTEGRATION.md §3.5 :
    //   le mapping des canaux est identique ; l'atténuation « faded » près de l'horizon n'est pas reproduite
    //   (dépend d'un wrapper interne au jeu -> TODO RE). Signatures = réf. header dans le doc §3.5.
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    atmo->GetSunOrMoonPosition(&sx, &sy, &sz);   // Atmosphere.h l.97 — direction (unitaire) vers le soleil/la lune
    float dr = 1.0f, dg = 1.0f, db = 1.0f;
    atmo->GetSunOrMoonColor(&dr, &dg, &db);      // l.165 — couleur du soleil (=> Diffuse)
    float ar = 0.0f, ag = 0.0f, ab = 0.0f;
    atmo->GetAmbientColor(&ar, &ag, &ab);        // l.227 — couleur ambiante (=> Ambient, ex-horizon faded)

    D3DLIGHT9 light;
    ::ZeroMemory(&light, sizeof(light));         // Crt_Memset(&dword_18C5358, 0, 0x68) @0x41227b
    light.Type = D3DLIGHT_DIRECTIONAL;           // = 3 (dword_18C5358 = 3 @0x412329)
    light.Diffuse.r = dr; light.Diffuse.g = dg; light.Diffuse.b = db; light.Diffuse.a = 1.0f;   // @0x412266 a=1
    light.Specular.r = 1.0f; light.Specular.g = 1.0f; light.Specular.b = 1.0f; light.Specular.a = 1.0f; // 18C536C..5378
    light.Ambient.r = ar; light.Ambient.g = ag; light.Ambient.b = ab; light.Ambient.a = 1.0f;   // @0x41229a a=1
    light.Direction.x = -sx; light.Direction.y = -sy; light.Direction.z = -sz;                  // -dirSoleil @0x4122cb..
    dev_->SetLight(0, &light);                   // SetLight(index 0) — renderer vtable+204 @0x412367
    dev_->LightEnable(0, TRUE);                  // l'original active la lumière 0 dans le chemin de rendu monde
    dev_->SetRenderState(D3DRS_LIGHTING, TRUE);

    // ---- Fog GXD — fidèle aux render-states d'Env_UpdateFogState 0x412370 ----
    // États écrits par l'original (via g_GxdRenderer vtable+228 = IDirect3DDevice9::SetRenderState) :
    //   28 = D3DRS_FOGENABLE  = TRUE          (@0x4124fe)
    //   34 = D3DRS_FOGCOLOR   = 0x00RRGGBB    (@0x412511 ; composée @0x4124f6)
    //   35 = D3DRS_FOGTABLEMODE = 1 = D3DFOG_EXP (@0x412525) -> fog par-pixel piloté par la densité
    //   38 = D3DRS_FOGDENSITY = densité (bits float) (@0x41253e)
    // NOTE FIDÉLITÉ : la couleur/densité exactes de 0x412370 (soleil levé : sunColor*sunColorFaded ;
    //   soleil couché : GetColorAtDirection(0) + densité = clamp(-colorBase.z/8435)·5e-6 @0x412416)
    //   s'appuient sur des wrappers internes au jeu (GetSunColorFaded/GetColorAtDirection/IsSunUp),
    //   non redistribués. On passe par les getters SDK GetFogEnabled/GetFogSettings (Atmosphere.h
    //   l.271/284, doc §3.5) ; la formule de densité en 8435 m reste TODO RE (T-16).
    if (atmo->GetFogEnabled()) {                 // l.271 — fog activé par la config atmosphérique courante
        float fogDensity = 0.0f, fr = 0.0f, fg = 0.0f, fb = 0.0f;
        atmo->GetFogSettings(&fogDensity, &fr, &fg, &fb);                 // l.284 — densité + couleur
        const D3DCOLOR fogColor = D3DCOLOR_COLORVALUE(fr, fg, fb, 1.0f);  // alpha ignorée par D3DRS_FOGCOLOR
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
// RenderSkyBefore — avant la géométrie de monde (SetCamera/Proj + BeginFrame)
// =====================================================================================
void SilverLiningSky::RenderSkyBefore(const Camera& camera) {
#if TS2_SILVERLINING_ENGINE_AVAILABLE
    if (!ready_ || !atmosphere_ || !dev_) return;
    auto* atmo = static_cast<SilverLining::Atmosphere*>(atmosphere_);

    // Aspect du back-buffer (le moteur exige des matrices cohérentes avec le viewport actif).
    D3DVIEWPORT9 vp{};
    dev_->GetViewport(&vp);
    const float aspect = (vp.Height > 0) ? static_cast<float>(vp.Width) / static_cast<float>(vp.Height)
                                         : 1.0f;

    // Matrices vue/projection GXD (float) -> double[16] attendu par SetCameraMatrix/SetProjectionMatrix.
    D3DXMATRIX view, proj;
    camera.BuildViewMatrix(view);
    camera.BuildProjMatrix(proj, aspect);
    double camMtx[16], projMtx[16];
    const float* vf = reinterpret_cast<const float*>(&view);
    const float* pf = reinterpret_cast<const float*>(&proj);
    for (int i = 0; i < 16; ++i) { camMtx[i] = vf[i]; projMtx[i] = pf[i]; }
    // ex-VeryOldClient: CAtmosphere::SetViewProjectionMatrix (copie double[16] view/proj → SetCameraMatrix/
    //   SetProjectionMatrix) — CONFIRMED, cf. cAtmosphere_RenderFrame 0x793B80 (SL_Backend_SetParamBlock208/80).
    atmo->SetCameraMatrix(camMtx);
    atmo->SetProjectionMatrix(projMtx);

    if (forceBlackSky_) {
        // Override « ciel noir » (@+642) : clear noir + BeginFrame sans skybox — fidèle à
        // cAtmosphere_RenderFrame (cf. TS2_SILVERLINING_CONFIG.md §3.3).
        dev_->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
        atmo->BeginFrame(/*drawSky*/ false, /*geocentric*/ false, /*skyBoxDim*/ 0.0, /*drawStars*/ false);
    } else {
        // @+352 : si RenderFlagSkipCelestial, le moteur saute soleil/lune/étoiles (drawSky=false).
        const bool drawSky = !skipCelestial_;
        // ex-VeryOldClient: CAtmosphere::StartRender → Atmosphere::BeginFrame(true) — CONFIRMED, entrée par
        //   frame cAtmosphere_RenderFrame 0x793B80 (le rendu ciel réel est gaté @+352 comme ci-dessus).
        atmo->BeginFrame(drawSky, /*geocentric*/ false, /*skyBoxDim*/ 0.0, /*drawStars*/ drawSky);
    }

    // Éclairage solaire + fog dérivés du SDK, poussés dans le device APRÈS BeginFrame — fidèle à
    // Env_UpdateFrame 0x412550 qui, chaque frame, appelle Env_UpdateSunLight 0x412210 puis
    // Env_UpdateFogState 0x412370 juste après cAtmosphere_RenderFrame, INDÉPENDAMMENT du flag @+642
    // (le noir de ciel n'annule pas la lumière/fog scène). Cf. TS2_SKY_ROSETTA.md T-7.
    applySceneLightingAndFog();
#else
    (void)camera; // no-op documenté : aucun BeginFrame possible sans moteur.
#endif
}

// =====================================================================================
// RenderSkyAfter — après la scène (EndFrame : nuages + précipitations)
// =====================================================================================
void SilverLiningSky::RenderSkyAfter(const Camera& camera) {
#if TS2_SILVERLINING_ENGINE_AVAILABLE
    if (!ready_ || !atmosphere_) return;
    (void)camera; // EndFrame réutilise l'état caméra posé par RenderSkyBefore.
    auto* atmo = static_cast<SilverLining::Atmosphere*>(atmosphere_);
    // ex-VeryOldClient: CAtmosphere::EndRender → Atmosphere::EndFrame() — CONFIRMED, cf. Atmosphere_DrawFrame 0x794FE0.
    atmo->EndFrame(/*drawClouds*/ true, /*drawPrecipitation*/ true);
#else
    (void)camera; // no-op documenté : aucun EndFrame possible sans moteur.
#endif
}

// =====================================================================================
// Shutdown
// =====================================================================================
void SilverLiningSky::Shutdown() {
#if TS2_SILVERLINING_ENGINE_AVAILABLE
    if (atmosphere_) {
        // ~Atmosphere nettoie Sky/Ephemeris/LensFlare/Precip et décharge le backend via
        // ShutdownShaderSystem (cf. cAtmosphere_dtor 0x791C40).
        // CONFIRMED — 0x791C40 détruit les sous-objets Sky/Ephemeris/LensFlare (this+0/+1/+2/+4) et libère
        //   les singletons ResourceLoader/timer (dword_18C4DE4/DE8). // ex-VeryOldClient: CAtmosphere::Destroy
        delete static_cast<SilverLining::Atmosphere*>(atmosphere_);
        atmosphere_ = nullptr;
    }
#endif
    dev_ = nullptr;
    ready_ = false;
    backendDllFound_ = false;
}

} // namespace ts2::gfx

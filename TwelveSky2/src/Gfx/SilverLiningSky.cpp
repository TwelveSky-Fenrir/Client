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
    auto* atmo = new SilverLining::Atmosphere("ALT1 License 3", "113e355254250a02094e32165441");

    // renderer = DIRECTX9 (case 1), rightHanded = false (repère GXD gauche, cf. Camera.h LookAtLH),
    // environment = device D3D9. Fidèle à cAtmosphere_Initialize 0x793390.
    const int err = atmo->Initialize(SilverLining::Atmosphere::DIRECTX9, baseDir,
                                     /*rightHanded*/ false, device);
    if (err != SilverLining::Atmosphere::E_NOERROR) {
        TS2_ERR("SilverLiningSky::Init : Atmosphere::Initialize a echoue (code=%d).", err);
        delete atmo;
        return false;
    }

    // TODO MOTEUR : Atmosphere::Initialize (Atmosphere.h l.370-376) EXIGE un device créé avec
    // D3DPRESENTFLAG_LOCKABLE_BACKBUFFER et SANS D3DCREATE_PUREDEVICE. Audit à faire côté
    // Gfx/Renderer.cpp (Renderer::Init) — hors périmètre de ce module autonome.
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
    atmo->SetCameraMatrix(camMtx);
    atmo->SetProjectionMatrix(projMtx);

    if (forceBlackSky_) {
        // Override « ciel noir » (@+642) : clear noir + BeginFrame sans skybox — fidèle à
        // cAtmosphere_RenderFrame (cf. TS2_SILVERLINING_CONFIG.md §3.3).
        dev_->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 0, 0, 0), 1.0f, 0);
        atmo->BeginFrame(/*drawSky*/ false, /*geocentric*/ false, /*skyBoxDim*/ 0.0, /*drawStars*/ false);
        return;
    }
    // @+352 : si RenderFlagSkipCelestial, le moteur saute soleil/lune/étoiles (drawSky=false).
    const bool drawSky = !skipCelestial_;
    atmo->BeginFrame(drawSky, /*geocentric*/ false, /*skyBoxDim*/ 0.0, /*drawStars*/ drawSky);
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
        delete static_cast<SilverLining::Atmosphere*>(atmosphere_);
        atmosphere_ = nullptr;
    }
#endif
    dev_ = nullptr;
    ready_ = false;
    backendDllFound_ = false;
}

} // namespace ts2::gfx

// Gfx/SilverLiningSky.h — module AUTONOME d'intégration du VRAI SDK SilverLining (DirectX9),
// gardé par un flag de compilation. C'est le point d'entrée « ciel SilverLining » du client
// réécrit ; il NE remplace PAS Gfx/SkyRenderer (gradient procédural) — le câblage entre les deux
// (priorité vrai-ciel vs repli gradient) est laissé à l'intégrateur (App/Scene).
//
// === CONSTAT DÉCISIF (PROUVÉ, cf. Docs/TS2_SILVERLINING_INTEGRATION.md) ===
//   Le SDK SilverLining de TwelveSky2 a DEUX couches :
//     1. Un MOTEUR HAUT NIVEAU (classe SilverLining::Atmosphere : Initialize/BeginFrame/EndFrame/
//        GetSunOrMoonColor/…) compilé STATIQUEMENT dans TwelveSky2.exe d'origine (cf.
//        TS2_SILVERLINING_PIPELINE.md §1). Il est fourni par la bibliothèque statique
//        `SilverLining-MT.lib`, NON redistribuable et ABSENTE du dépôt.
//     2. Un BACKEND RENDERER bas niveau `SilverLiningDirectX9-MT.dll` (91 exports C, l'interface
//        de SilverLiningDLLCommon.h : SetEnvironment/SetContext/DrawStrip/…), chargé DYNAMIQUEMENT
//        au runtime par SL_Renderer_LoadBackendDLL 0x71F300. CE fichier-là EST présent (vendored
//        sous external/SilverLining/VC9/win32/) — cf. RE/silverlining_exports.txt.
//   Conséquence : sans la couche 1, la classe Atmosphere n'est pas linkable → le vrai ciel ne peut
//   PAS rendre. On NE fabrique PAS une fausse API Atmosphere (règle projet « N'INVENTE RIEN »).
//
// === GATING ===
//   TS2_SILVERLINING_ENGINE_AVAILABLE (défaut 0) :
//     • == 1 (quand SilverLining-MT.lib sera fourni hors dépôt + external/SilverLining/Include au
//       chemin d'inclusion) : le .cpp #include "Atmosphere.h" et appelle la VRAIE API SDK (mapping
//       prouvé depuis les en-têtes réels — Atmosphere.h/AtmosphericConditions.h/Location.h/LocalTime.h).
//     • == 0 (réalité actuelle) : Init charge quand même le VRAI backend DLL (LoadLibraryA +
//       GetProcAddress SetEnvironment/SetContext — fidèle à 0x71F300, prouve que le renderer réel
//       est câblé) mais retourne false (EngineUnavailable), car aucune géométrie de ciel ne peut
//       être émise sans le moteur. Les autres méthodes sont des no-op documentées.
//
//   ⚠ Le module compile SEUL, SANS external/SilverLining/Include : le bloc gardé-OFF n'inclut aucun
//   en-tête SDK. L'include SDK n'est requis QUE pour le mode moteur (== 1) — cf. le .cpp.
#pragma once

#include <windows.h>
#include <d3d9.h>

// Flag de gating — défini à 0 (moteur statique absent) si l'environnement de build ne l'impose pas.
#ifndef TS2_SILVERLINING_ENGINE_AVAILABLE
#define TS2_SILVERLINING_ENGINE_AVAILABLE 0
#endif

namespace ts2::asset { class AtmosphereFile; }

namespace ts2::gfx {

class Camera;

// Enveloppe du moteur d'atmosphère SilverLining réel (DirectX9). API demandée par la tâche ;
// sémantique de chaque méthode : voir les commentaires + le .cpp (mode moteur vs mode repli).
class SilverLiningSky {
public:
    // Prépare le ciel SilverLining pour le device D3D9 donné.
    //   resourceDir : racine des ressources SilverLining (« Resources » du SDK), c.-à-d. le dossier
    //   qui CONTIENT VC9/win32/<backend>.dll et SilverLining.config. Dans le client réécrit, c'est
    //   external/SilverLining/ (vendored) ; dans TS2 d'origine c'était G03_GDATA\D11_ATMOSPHERE\.
    //   Le chemin du backend DLL est construit exactement comme SL_Renderer_LoadBackendDLL 0x71F300 :
    //   <resourceDir> + "VC9/win32/SilverLiningDirectX9-MT.dll" (case 1 = DIRECTX9).
    //
    //   Retour : true UNIQUEMENT si le moteur réel a été initialisé (jamais atteignable tant que
    //   TS2_SILVERLINING_ENGINE_AVAILABLE == 0). En mode repli, retourne toujours false
    //   (EngineUnavailable) après avoir réellement chargé/sondé le backend DLL. L'appelant DOIT
    //   conserver son propre repli (ex. Gfx/SkyRenderer gradient) quand Init() renvoie false.
    bool Init(IDirect3DDevice9* device, const char* resourceDir);

    // Pousse l'heure/position géographique de la zone (fichier .ATM réel déjà parsé) dans les
    // AtmosphericConditions du moteur. No-op en mode repli.
    void UpdateTime(const asset::AtmosphereFile& atm);

    // Rendu ciel AVANT la géométrie de monde : SetCameraMatrix/SetProjectionMatrix + BeginFrame
    // (ciel/soleil/lune/étoiles). Fenêtre = Env_UpdateFrame 0x412550 dans l'original. No-op sans moteur.
    void RenderSkyBefore(const Camera& camera);

    // Rendu nuages/précipitations APRÈS la scène : EndFrame (tri back-to-front). No-op sans moteur.
    void RenderSkyAfter(const Camera& camera);

    // Détruit l'objet Atmosphere (le dtor décharge le backend via ShutdownShaderSystem) / libère l'état.
    void Shutdown();

    // --- Diagnostics (tests, logs de sanité) --------------------------------------------------
    // ready_ : true seulement si le moteur réel est initialisé (toujours false en mode repli).
    bool Ready() const { return ready_; }
    // backendDllFound_ : le backend redistribuable a été chargé + ses exports résolus avec succès
    // (utile pour distinguer « DLL absente » de « DLL présente mais moteur statique manquant »).
    bool BackendDllFound() const { return backendDllFound_; }

private:
    IDirect3DDevice9* dev_ = nullptr;
    bool ready_ = false;           // moteur réel initialisé (jamais true en mode repli)
    bool backendDllFound_ = false; // backend DLL réellement chargé+résolu (diagnostic)

    // Flags de rendu de la zone (mémorisés par UpdateTime, consommés par RenderSkyBefore en mode
    // moteur — fidèle à cAtmosphere_RenderFrame @+352/@+642, cf. TS2_SILVERLINING_CONFIG.md §3.3).
    bool skipCelestial_ = false;   // AtmosphereFile::RenderFlagSkipCelestial() (@+352)
    bool forceBlackSky_ = false;   // AtmosphereFile::RenderFlagForceBlackSky() (@+642)

    // SilverLining::Atmosphere* opaque : gardé en void* pour que ce header n'impose PAS l'include SDK
    // (le module compile sans external/SilverLining/Include). Utilisé seulement en mode moteur.
    void* atmosphere_ = nullptr;
};

} // namespace ts2::gfx

// Gfx/SkyRenderer.h — PREMIER PAS RÉEL de rendu ciel/atmosphère, dérivé des VRAIES données
// .ATM (Asset/AtmosphereFile.h), PAS une réimplémentation du SDK SilverLining.
//
// === Ce que ce module FAIT ===
//   Dessine un gradient vertical plein écran (zénith en haut -> horizon en bas), dont les
//   DEUX couleurs sont calculées à partir de l'heure du jour RÉELLE (AtmDateTime::HourOfDay,
//   champ +0x1D du fichier .ATM de la zone active — cf. Asset/AtmosphereFile.h) via un modèle
//   jour/nuit simple à keyframes (minuit/aube/midi/crépuscule, cf. .cpp), PAS une couleur
//   fixe inventée. Respecte aussi le flag @+642 du fichier réel (RenderFlagForceBlackSky) :
//   si posé, le ciel est rendu noir opaque, exactement comme le ferait le moteur d'origine
//   pour ce cas (override, indépendant du calcul Perez).
//   Dessiné au bon moment dans la frame (avant décor, puis à nouveau après les entités)
//   avec depth-test actif, pour occuper la même fenêtre temporelle que les deux appels
//   SilverLining réels décrits dans Docs/TS2_SILVERLINING_PIPELINE.md, mais sous une
//   forme minimale et prudente.
//
// === Ce que ce module NE FAIT PAS (honnête, pas simulé) ===
//   - PAS de nuages (stratus/cirrus/cumulus), PAS de précipitations (pluie/neige/grêle),
//     PAS d'étoiles, PAS de soleil/lune dessinés (disque, halo, lens flare), PAS d'atmosphère-
//     depuis-l'espace. Le SDK SilverLining réel (~150 fonctions `cAtmosphere_*`/`SL_*`/`Sky_*`
//     statiquement liées dans TwelveSky2.exe + backend `SilverLiningDirectX9-MT.dll`) reste
//     hors périmètre — c'est un moteur commercial complet, pas réimplémentable en une mission.
//   - PAS de calcul d'éphéméride réel : la position du soleil/de la lune n'est PAS calculée
//     (pas d'azimut/élévation), seule l'HEURE (+ le flag "ciel noir") pilote le gradient. La
//     latitude/longitude du fichier .ATM sont lues (cf. AtmosphereFile) mais pas exploitées
//     par le modèle de couleur actuel — exposées ici en diagnostic pour une future mission qui
//     voudrait les utiliser (ex. calcul d'élévation solaire réel).
//   - PAS de diffusion atmosphérique de Perez, PAS de fog volumétrique — cf. Env_UpdateFogState
//     (0x412370) côté binaire d'origine, non reproduit.
//   - Les pistes de keyframes couleur/scalaires/corps célestes du fichier .ATM (AtmosphereFile::
//     ColorKeyframes/Bodies/ScalarKeyframes) sont IGNORÉES par ce module (exposées brutes par
//     le parseur mais jamais lues ici) : le gradient jour/nuit est un modèle indépendant, pas
//     une lecture de ces pistes (dont la sémantique exacte reste un TODO RE, cf. AtmosphereFile.h).
#pragma once
#include "Gfx/Renderer.h"
#include <cstdint>

namespace ts2::asset { class AtmosphereFile; }
namespace ts2::world { struct SilverLiningConfig; }

namespace ts2::gfx {

class SkyRenderer {
public:
    bool Init(Renderer& renderer);
    void Shutdown();

    // Charge les paramètres globaux SilverLining.config pertinents pour le socle de rendu.
    void ApplyConfig(const world::SilverLiningConfig& cfg);

    // Recalcule les couleurs dérivées à partir d'un AtmosphereFile réellement parsé (appelé
    // au chargement/changement de zone, cf. World/WorldIntegration.h::WorldAssets::Atmosphere()).
    // atm.Valid()==false (fichier absent/corrompu) -> repli documenté sur "midi neutre" (le
    // point médian du MÊME modèle jour/nuit, pas une teinte arbitraire distincte) et
    // HasRealAtmosphere()==false pour que l'appelant puisse le distinguer/logguer.
    void SetAtmosphere(const asset::AtmosphereFile& atm);

    // Dessine le gradient plein écran. Cf. bandeau ci-dessus pour la place dans la frame
    // (en tout premier, avant la géométrie de monde). Sans effet si Init() n'a pas réussi.
    void Render(int screenW, int screenH);

    // --- Diagnostics (tests, logs de sanité) --------------------------------------------
    bool     HasRealAtmosphere() const { return hasRealAtmosphere_; }
    double   HourOfDay()         const { return hourOfDay_; }
    double   Latitude()          const { return latitude_; }
    double   Longitude()         const { return longitude_; }
    bool     ForcedBlackSky()    const { return forceBlackSky_; }
    uint32_t ZenithColorArgb()   const { return zenithColor_; }
    uint32_t HorizonColorArgb()  const { return horizonColor_; }

private:
    void recomputeColors();

    // Instantané des réglages GLOBAUX de SilverLining.config (lus 1×/session par cAtmosphere_Initialize
    // 0x793390) ; les données PAR ZONE (heure/lat/lon/flags) arrivent séparément via SetAtmosphere() (.ATM,
    // Atmosphere_Deserialize 0x795A40). cf. Docs/TS2_SILVERLINING_CONFIG.md §2. Les valeurs par défaut ci-
    // dessous = defaults SDK stock, PAS transposées de VeryOldClient (build différent).
    struct ConfigSnapshot {
        double defaultLongitude = -122.064840;
        double defaultLatitude  = 30.0;
        double defaultAltitude  = 100.0;
        int defaultYear   = 2006;
        int defaultMonth  = 8;
        int defaultDay    = 15;
        int defaultHour   = 12;
        int defaultMinute = 0;
        double defaultSecond  = 0.0;
        double defaultTimezone = -8.0;
        bool defaultDst = true;
        double defaultTurbidity = 2.2;   // config "default-turbidity" @ cAtmosphere_Initialize 0x793390 (défaut code 2.0, fichier livré 2.2)
        bool disableToneMapping = false; // config "disable-tone-mapping" (global) / cf. g_SL_ForceToneMapping 0x18C4DEC
        bool enableAtmosphereFromSpace = false; // config "enable-atmosphere-from-space" @ 0x793390 (défaut true, fichier "no")
        double atmosphereHeight = 100000.0;     // config "atmosphere-height" @ cAtmosphere_Initialize 0x793390
                                                // CONFIRMED @0x7935eb : *v20 = 100000.0 puis Cfg_GetDouble(100000.0, "atmosphere-height", v20)
                                                //   -> défaut réel si la clé est absente = 100000.0 (corrigé depuis 300000.0 ; ex-D-1/T-13). Champ non
                                                //   consommé par le gradient (cosmétique) mais aligné sur la vérité IDA.
        // "atmosphere-scale-height-meters" RELU PAR FRAME @ cAtmosphere_RenderFrame 0x793B80 (Cfg_GetDouble → dbl_18636E8).
        // PLAUSIBLE (VeryOldClient) — non prouvé IDA (8435.0 = hauteur d'échelle de pression terrestre, jamais lue comme littéral dans l'IDB).
        // ex-VeryOldClient: CAtmosphere::SetSceneFog H = 8435.0 ("Pressure scale height of Earth's atmosphere")
        double atmosphereScaleHeightMeters = 8435.0;
    };

    IDirect3DDevice9* dev_ = nullptr;
    bool   ready_ = false;

    bool   hasRealAtmosphere_ = false; // true seulement si SetAtmosphere() a reçu un .ATM Valid()
    double hourOfDay_    = 12.0;       // heure décimale [0,24) — repli "midi" tant qu'aucun .ATM
    double latitude_     = 0.0;        // exposée, pas exploitée par le modèle de couleur (cf. bandeau)
    double longitude_    = 0.0;
    bool   forceBlackSky_ = false;     // AtmosphereFile::RenderFlagForceBlackSky() de la zone

    ConfigSnapshot config_;
    uint32_t zenithColor_  = 0xFF335C99; // recalculés par recomputeColors() dès Init()/SetAtmosphere()
    uint32_t horizonColor_ = 0xFF9CB8DE;
};

} // namespace ts2::gfx

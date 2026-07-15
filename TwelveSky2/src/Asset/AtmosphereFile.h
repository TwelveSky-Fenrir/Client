// Asset/AtmosphereFile.h — parseur du format « .ATM » (état d'atmosphère SilverLining
// sérialisé RAW, PAR ZONE) : G03_GDATA\D07_GWORLD\Z%03d.ATM. 89 fichiers réels livrés.
//
// === Sources IDA (vérité unique) ===
//   Atmosphere_Deserialize   0x795A40  — racine du format (5 flags + KfAnim)
//   KfAnim_Deserialize       0x708160  — corps du désérialiseur (3 pistes de keyframes)
//   Astro_DeserializeBody    0x6FE960  — corps céleste / couche nuage (76 o)
//   DateTime_Serialize       0x6FE720  — DateTime (37 o), symétrique lecture/écriture
//   World_LoadDataFile       0x4118F0  — ouvre Z%03d.ATM, copie en mémoire
//   World_LoadZoneResource   0x4DCB60  — case 7 = .ATM (zoneId BRUT, pas fileId)
//
// Layout binaire exact (cf. Docs/TS2_ASSET_FORMATS.md §2.7 ET Docs/TS2_SILVERLINING_CONFIG.md
// §3.2 — les deux documents concordent ; RE/asset_parsers/sky_atm.py est la traduction
// Python VALIDÉE octet-exact sur les 89 fichiers réels du dépôt, EOF exact, ce module en est
// la traduction C++ FIDÈLE, même ordre de champs, mêmes tailles) :
//
//   +0x00  5×u8    flags de rendu (cf. RenderFlagSkipCelestial/RenderFlagForceBlackSky/…)
//   +0x05  3×f64   Location (latitude, longitude, altitude)      — Serialize_WriteVec3d
//   +0x1D  DateTime courant   (37 o) — heure du jour DE LA ZONE, VARIABLE d'un fichier à l'autre
//   +0x42  DateTime éphéméride (37 o) — référence éphéméride, CONSTANTE observée (2006-08-15 12:00 tz=-8 dst=1)
//   +0x67  3×f64   globaux (observés : 2.2 gamma, 30000 visibilité, 0)
//   ...    u32 nColorKF  ; nColorKF × { i32 key ; 4×f64 }         (keyframes couleur)
//   ...    u32 nBodies   ; nBodies  × CORPS_CÉLESTE (76 o)        (voir AtmCelestialBody)
//   ...    u32 nScalarKF ; nScalarKF× { i32 key ; f64 }           (keyframes scalaires)
//   fin    u8 flag ; 4×f64 queue (observé : 0.001, 1, 1, 1)
//
//   DateTime (37 o) : i32 year,month,day,hour,minute + f64 timezone,second + u8 dst.
//   CORPS_CÉLESTE (76 o) : i32 key ; i32 type(0-5) ; 7×f64 géométrie ; 4×u8 flags ;
//                          u32 nInner + nInner×{i32;f64} ; 4 o queue.
//   Taille totale attendue = 171 + 76*nBodies + 37  →  208/284/360/512 o pour 0/1/2/4 couches
//   (formule + tailles observées confirmées sur les 89 .ATM réels).
//
// === CE QUE CE PARSEUR FAIT ===
//   Désérialise la structure ENTIÈRE, byte-exact, avec vérification EOF (miroir du validateur
//   Python `sky_atm.py::parse_atm`, 89/89 OK). Expose la latitude/longitude/altitude, les DEUX
//   DateTime (dont l'heure RÉELLE du jour de la zone), les flags de rendu, et les 3 pistes de
//   keyframes BRUTES (couleur/corps célestes/scalaires) — sans les INTERPRÉTER.
//
// === CE QUE CE PARSEUR NE FAIT PAS (honnête, pas simulé) ===
//   Aucune interprétation physique : pas de calcul d'éphéméride solaire/lunaire réel (position
//   du soleil, azimut, élévation), pas de diffusion atmosphérique de Perez, pas de nuages/
//   précipitations/étoiles. Les pistes de keyframes (couleur/corps/scalaires) sont exposées
//   BRUTES pour une future mission — ce module ne les consomme pas lui-même. La consommation
//   simplifiée (gradient jour/nuit dérivé de l'heure) vit dans Gfx/SkyRenderer.h, séparément.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

// DateTime (37 o) — cf. bandeau. Nommé AtmDateTime pour ne pas entrer en collision avec les
// types DATE/SYSTEMTIME de <windows.h> ni struct tm de <ctime>, déjà inclus ailleurs dans le projet.
struct AtmDateTime {
    int32_t year = 0, month = 0, day = 0, hour = 0, minute = 0;
    double  timezone = 0.0, second = 0.0;
    uint8_t dst = 0;

    // Heure du jour en heures décimales [0, 24) — hour + minute/60 + second/3600, repliée
    // modulo 24 (les .ATM réels observés restent dans [0,24) mais on protège les cas limites).
    double HourOfDay() const;
};

struct AtmColorKeyframe  { int32_t key = 0; double v[4] = {0, 0, 0, 0}; };
struct AtmScalarKeyframe { int32_t key = 0; double value = 0.0; };

// CORPS_CÉLESTE / couche nuage (76 o sur disque avec nInner==0, cf. bandeau).
struct AtmCelestialBody {
    int32_t key = 0;
    int32_t type = 0;                 // 0-5, factory Astro_CreateBodyByType 0x6FE7D0
    double  geom[7] = {0, 0, 0, 0, 0, 0, 0};
    uint8_t flags[4] = {0, 0, 0, 0};
    std::vector<std::pair<int32_t, double>> inner; // nInnerMap × {clé, valeur} (observé vide)
    uint8_t tail[4] = {0, 0, 0, 0};   // queue spécifique au type
};

// État d'atmosphère PAR ZONE désérialisé depuis un fichier .ATM (ou Atmosphere.DAT, même
// format — Atmosphere_Deserialize est commun aux deux, cf. World/WorldMap.h).
class AtmosphereFile {
public:
    // Charge + parse le fichier entier. Renvoie false si lecture ou structure invalide
    // (bornes dépassées, EOF non atteint exactement — miroir strict du validateur Python).
    bool Load(const std::string& path);

    // Parse un buffer déjà en mémoire (utilisé par Load(), exposé pour les tests).
    bool Parse(const uint8_t* data, size_t size);

    bool Valid() const { return valid_; }

    // --- Flags de rendu (+0x00, 5×u8) --------------------------------------------------
    // @+352 dans l'objet cAtmosphere runtime : si vrai, le moteur d'origine saute TOUT le
    // rendu soleil/lune/étoiles/anneau-espace (probable "zone sans ciel visible", intérieur).
    bool RenderFlagSkipCelestial() const { return renderFlags_[0] != 0; }
    // @+642 : si vrai, force la couleur ciel/horizon à noir opaque (0,0,0,1) au lieu du
    // calcul physique Perez du SDK — override "ciel noir".
    bool RenderFlagForceBlackSky() const { return renderFlags_[1] != 0; }
    // @+643/@+644 : sémantique non identifiée avec certitude par la RE (cf. doc). Exposés
    // bruts, non interprétés ici.
    uint8_t RawFlag2() const { return renderFlags_[2]; }
    uint8_t RawFlag3() const { return renderFlags_[3]; }
    // g_SL_ForceToneMapping (0x18C4DEC) : force le tone-mapping HDR indépendamment du réglage
    // global "disable-tone-mapping".
    uint8_t ForceToneMapping() const { return renderFlags_[4]; }

    // --- Position géographique (+0x05, 3×f64) ------------------------------------------
    double Latitude()  const { return lat_; }
    double Longitude() const { return lon_; }
    double Altitude()  const { return alt_; }

    // --- Date/heure (+0x1D / +0x42, 2×37 o) --------------------------------------------
    const AtmDateTime& CurrentDateTime()     const { return dtCurrent_; }   // heure DU JOUR de la zone (variable)
    const AtmDateTime& EphemerisReference()  const { return dtEphemeris_; } // référence éphéméride (quasi constante)

    // Heure décimale [0,24) dérivée de CurrentDateTime() — c'est la valeur RÉELLE utilisée
    // par Gfx/SkyRenderer.h pour dériver le gradient jour/nuit.
    double HourOfDay() const { return dtCurrent_.HourOfDay(); }

    // --- Globaux (+0x67, 3×f64) : observés (2.2 gamma, 30000 visibilité, 0) -------------
    auto Globals() const -> const double(&)[3] { return globals3_; }

    // --- Pistes de keyframes BRUTES (non interprétées ici, cf. bandeau) -----------------
    const std::vector<AtmColorKeyframe>&  ColorKeyframes()  const { return colorKeys_; }
    const std::vector<AtmCelestialBody>&  Bodies()          const { return bodies_; }
    const std::vector<AtmScalarKeyframe>& ScalarKeyframes() const { return scalarKeys_; }

    uint8_t TailFlag() const { return tailFlag_; }
    auto Tail4() const -> const double(&)[4] { return tail4_; }

    // --- Diagnostics (miroir du validateur Python sky_atm.py) --------------------------
    size_t ConsumedBytes() const { return consumed_; }
    size_t TotalBytes()    const { return total_; }
    const std::string& Path() const { return path_; }

private:
    std::string path_;
    bool    valid_ = false;
    uint8_t renderFlags_[5] = {0, 0, 0, 0, 0};
    double  lat_ = 0.0, lon_ = 0.0, alt_ = 0.0;
    AtmDateTime dtCurrent_;
    AtmDateTime dtEphemeris_;
    double  globals3_[3] = {0.0, 0.0, 0.0};
    std::vector<AtmColorKeyframe>  colorKeys_;
    std::vector<AtmCelestialBody>  bodies_;
    std::vector<AtmScalarKeyframe> scalarKeys_;
    uint8_t tailFlag_ = 0;
    double  tail4_[4] = {0.0, 0.0, 0.0, 0.0};
    size_t  consumed_ = 0;
    size_t  total_ = 0;
};

} // namespace ts2::asset

// Asset/AtmosphereFile.h — parser for the ".ATM" format (SilverLining atmosphere state
// serialized RAW, PER ZONE): G03_GDATA\D07_GWORLD\Z%03d.ATM. 89 real files shipped.
//
// === IDA sources (single source of truth) ===
//   Atmosphere_Deserialize   0x795A40  — format root (5 flags + KfAnim)
//   KfAnim_Deserialize       0x708160  — deserializer body (3 keyframe tracks)
//   Astro_DeserializeBody    0x6FE960  — celestial body / cloud layer (76 bytes)
//   DateTime_Serialize       0x6FE720  — DateTime (37 bytes), symmetric read/write
//   World_LoadDataFile       0x4118F0  — opens Z%03d.ATM, copies into memory
//   World_LoadZoneResource   0x4DCB60  — case 7 = .ATM (RAW zoneId, not fileId)
//
// Exact binary layout (cf. Docs/TS2_ASSET_FORMATS.md §2.7 AND Docs/TS2_SILVERLINING_CONFIG.md
// §3.2 — both documents agree; RE/asset_parsers/sky_atm.py is the VALIDATED byte-exact
// Python port against the 89 real files in the repo, EOF exact; this module is its
// FAITHFUL C++ port, same field order, same sizes):
//
//   +0x00  5×u8    render flags (cf. RenderFlagSkipCelestial/RenderFlagForceBlackSky/…)
//   +0x05  3×f64   Location (latitude, longitude, altitude)      — Serialize_WriteVec3d
//   +0x1D  Current DateTime    (37 bytes) — time of day OF THE ZONE, VARIES per file
//   +0x42  Ephemeris DateTime  (37 bytes) — ephemeris reference, observed CONSTANT (2006-08-15 12:00 tz=-8 dst=1)
//   +0x67  3×f64   globals (observed: 2.2 gamma, 30000 visibility, 0)
//   ...    u32 nColorKF  ; nColorKF × { i32 key ; 4×f64 }         (color keyframes)
//   ...    u32 nBodies   ; nBodies  × CELESTIAL BODY (76 bytes)   (see AtmCelestialBody)
//   ...    u32 nScalarKF ; nScalarKF× { i32 key ; f64 }           (scalar keyframes)
//   end    u8 flag ; 4×f64 tail (observed: 0.001, 1, 1, 1)
//
//   DateTime (37 bytes): i32 year,month,day,hour,minute + f64 timezone,second + u8 dst.
//   CELESTIAL BODY (76 bytes): i32 key ; i32 type(0-5) ; 7×f64 geometry ; 4×u8 flags ;
//                              u32 nInner + nInner×{i32;f64} ; 4-byte tail.
//   Expected total size = 171 + 76*nBodies + 37  →  208/284/360/512 bytes for 0/1/2/4 layers
//   (formula + observed sizes confirmed against the 89 real .ATM files).
//
// === WHAT THIS PARSER DOES ===
//   Deserializes the ENTIRE structure, byte-exact, with EOF verification (mirrors the
//   Python validator `sky_atm.py::parse_atm`, 89/89 OK). Exposes latitude/longitude/altitude,
//   BOTH DateTime values (including the zone's REAL time of day), render flags, and the 3
//   RAW keyframe tracks (color/celestial bodies/scalars) — without INTERPRETING them.
//
// === WHAT THIS PARSER DOES NOT DO (honest, not simulated) ===
//   No physical interpretation: no real solar/lunar ephemeris computation (sun position,
//   azimuth, elevation), no Perez atmospheric scattering, no clouds/precipitation/stars.
//   The keyframe tracks (color/bodies/scalars) are exposed RAW for a future mission — this
//   module does not consume them itself. The simplified consumption (day/night gradient
//   derived from the hour) lives separately in Gfx/SkyRenderer.h.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

// DateTime (37 bytes) — cf. banner above. Named AtmDateTime to avoid colliding with the
// DATE/SYSTEMTIME types from <windows.h> or struct tm from <ctime>, already included elsewhere in the project.
struct AtmDateTime {
    int32_t year = 0, month = 0, day = 0, hour = 0, minute = 0;
    double  timezone = 0.0, second = 0.0;
    uint8_t dst = 0;

    // Time of day in decimal hours [0, 24) — hour + minute/60 + second/3600, wrapped
    // modulo 24 (real .ATM files observed stay within [0,24) but edge cases are guarded).
    double HourOfDay() const;
};

struct AtmColorKeyframe  { int32_t key = 0; double v[4] = {0, 0, 0, 0}; };
struct AtmScalarKeyframe { int32_t key = 0; double value = 0.0; };

// CELESTIAL BODY / cloud layer (76 bytes on disk with nInner==0, cf. banner above).
struct AtmCelestialBody {
    int32_t key = 0;
    int32_t type = 0;                 // 0-5, factory Astro_CreateBodyByType 0x6FE7D0
    double  geom[7] = {0, 0, 0, 0, 0, 0, 0};
    uint8_t flags[4] = {0, 0, 0, 0};
    std::vector<std::pair<int32_t, double>> inner; // nInnerMap × {key, value} (observed empty)
    uint8_t tail[4] = {0, 0, 0, 0};   // type-specific tail
};

// Atmosphere state PER ZONE deserialized from an .ATM file (or Atmosphere.DAT, same
// format — Atmosphere_Deserialize is shared by both, cf. World/WorldMap.h).
class AtmosphereFile {
public:
    // Loads + parses the entire file. Returns false on read failure or invalid structure
    // (bounds exceeded, EOF not reached exactly — strict mirror of the Python validator).
    bool Load(const std::string& path);

    // Parses an already in-memory buffer (used by Load(), exposed for tests).
    bool Parse(const uint8_t* data, size_t size);

    bool Valid() const { return valid_; }

    // --- Render flags (+0x00, 5×u8) --------------------------------------------------
    // @+352 in the runtime cAtmosphere object: if true, the original engine skips ALL
    // sun/moon/stars/space-ring rendering (likely "zone with no visible sky", indoor).
    bool RenderFlagSkipCelestial() const { return renderFlags_[0] != 0; }
    // @+642: if true, forces the sky/horizon color to opaque black (0,0,0,1) instead of
    // the SDK's physical Perez calculation — "black sky" override.
    bool RenderFlagForceBlackSky() const { return renderFlags_[1] != 0; }
    // @+643/@+644: semantics not identified with certainty by RE (cf. doc). Exposed
    // raw, not interpreted here.
    uint8_t RawFlag2() const { return renderFlags_[2]; }
    uint8_t RawFlag3() const { return renderFlags_[3]; }
    // g_SL_ForceToneMapping (0x18C4DEC): forces HDR tone-mapping independently of the
    // global "disable-tone-mapping" setting.
    uint8_t ForceToneMapping() const { return renderFlags_[4]; }

    // --- Geographic position (+0x05, 3×f64) ------------------------------------------
    double Latitude()  const { return lat_; }
    double Longitude() const { return lon_; }
    double Altitude()  const { return alt_; }

    // --- Date/time (+0x1D / +0x42, 2×37 bytes) --------------------------------------------
    const AtmDateTime& CurrentDateTime()     const { return dtCurrent_; }   // zone's TIME OF DAY (variable)
    const AtmDateTime& EphemerisReference()  const { return dtEphemeris_; } // ephemeris reference (near-constant)

    // Decimal hour [0,24) derived from CurrentDateTime() — this is the REAL value used
    // by Gfx/SkyRenderer.h to derive the day/night gradient.
    double HourOfDay() const { return dtCurrent_.HourOfDay(); }

    // --- Globals (+0x67, 3×f64): observed (2.2 gamma, 30000 visibility, 0) -------------
    auto Globals() const -> const double(&)[3] { return globals3_; }

    // --- RAW keyframe tracks (not interpreted here, cf. banner above) -----------------
    const std::vector<AtmColorKeyframe>&  ColorKeyframes()  const { return colorKeys_; }
    const std::vector<AtmCelestialBody>&  Bodies()          const { return bodies_; }
    const std::vector<AtmScalarKeyframe>& ScalarKeyframes() const { return scalarKeys_; }

    uint8_t TailFlag() const { return tailFlag_; }
    auto Tail4() const -> const double(&)[4] { return tail4_; }

    // --- Diagnostics (mirrors the Python validator sky_atm.py) --------------------------
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

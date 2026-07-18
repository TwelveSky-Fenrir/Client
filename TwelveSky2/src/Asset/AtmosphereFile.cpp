// Asset/AtmosphereFile.cpp — see AtmosphereFile.h for the full banner (format, source EAs,
// what is done/not done). FAITHFUL C++ port of RE/asset_parsers/sky_atm.py
// (parse_atm), validated EOF-exact against the 89 real .ATM files in the repo.
#include "Asset/AtmosphereFile.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Core/Log.h"

#include <cmath>

namespace ts2::asset {

namespace {

// DateTime (37 bytes): 5×i32 (20 B) + 2×f64 (16 B) + 1×u8 (1 B) = 37 B. Exact mirror of
// sky_atm.py::_read_datetime.
AtmDateTime ReadDateTime(ByteReader& r) {
    AtmDateTime dt;
    dt.year   = r.I32();
    dt.month  = r.I32();
    dt.day    = r.I32();
    dt.hour   = r.I32();
    dt.minute = r.I32();
    dt.timezone = r.F64();
    dt.second   = r.F64();
    dt.dst      = r.U8();
    return dt;
}

// CELESTIAL BODY / cloud layer (76 bytes when nInner==0). Exact mirror of sky_atm.py::_read_body
// (Astro_DeserializeBody 0x6FE960 + CloudLayer_Deserialize 0x703E80).
AtmCelestialBody ReadBody(ByteReader& r) {
    AtmCelestialBody b;
    b.key  = r.I32();
    b.type = r.I32();
    for (double& g : b.geom) g = r.F64();
    for (uint8_t& f : b.flags) f = r.U8();
    const int32_t nInner = r.I32();
    b.inner.reserve(nInner > 0 ? static_cast<size_t>(nInner) : 0);
    for (int32_t i = 0; i < nInner; ++i) {
        const int32_t k = r.I32();
        const double  v = r.F64();
        b.inner.emplace_back(k, v);
    }
    for (uint8_t& t : b.tail) t = r.U8(); // 4-byte tail (type-specific, observed empty/nInner=0)
    return b;
}

} // namespace

double AtmDateTime::HourOfDay() const {
    double h = static_cast<double>(hour) + static_cast<double>(minute) / 60.0 + second / 3600.0;
    // Defensive modulo-24 fallback: real .ATM files observed stay within [0,24), but we
    // assume nothing for a future out-of-range file.
    h = std::fmod(h, 24.0);
    if (h < 0.0) h += 24.0;
    return h;
}

bool AtmosphereFile::Load(const std::string& path) {
    path_ = path;
    std::vector<uint8_t> bytes;
    if (!ReadWholeFile(path, bytes) || bytes.empty()) {
        TS2_WARN("AtmosphereFile::Load : lecture impossible ou fichier vide (\"%s\").", path.c_str());
        valid_ = false;
        return false;
    }
    return Parse(bytes.data(), bytes.size());
}

bool AtmosphereFile::Parse(const uint8_t* data, size_t size) {
    valid_ = false;
    total_ = size;
    consumed_ = 0;
    try {
        ByteReader r(data, size);

        for (uint8_t& f : renderFlags_) f = r.U8();          // +0x00, 5×u8

        lat_ = r.F64();                                       // +0x05
        lon_ = r.F64();
        alt_ = r.F64();

        dtCurrent_   = ReadDateTime(r);                       // +0x1D, 37 bytes
        dtEphemeris_ = ReadDateTime(r);                        // +0x42, 37 bytes

        for (double& g : globals3_) g = r.F64();               // +0x67, 3×f64

        const int32_t nColor = r.I32();
        colorKeys_.clear();
        colorKeys_.reserve(nColor > 0 ? static_cast<size_t>(nColor) : 0);
        for (int32_t i = 0; i < nColor; ++i) {
            AtmColorKeyframe kf;
            kf.key = r.I32();
            for (double& v : kf.v) v = r.F64();
            colorKeys_.push_back(kf);
        }

        const int32_t nBodies = r.I32();
        bodies_.clear();
        bodies_.reserve(nBodies > 0 ? static_cast<size_t>(nBodies) : 0);
        for (int32_t i = 0; i < nBodies; ++i) bodies_.push_back(ReadBody(r));

        const int32_t nScalar = r.I32();
        scalarKeys_.clear();
        scalarKeys_.reserve(nScalar > 0 ? static_cast<size_t>(nScalar) : 0);
        for (int32_t i = 0; i < nScalar; ++i) {
            AtmScalarKeyframe kf;
            kf.key = r.I32();
            kf.value = r.F64();
            scalarKeys_.push_back(kf);
        }

        tailFlag_ = r.U8();
        for (double& t : tail4_) t = r.F64();

        consumed_ = r.Pos();
        if (consumed_ != total_) {
            TS2_WARN("AtmosphereFile::Parse : consomme %zu != taille totale %zu (\"%s\") "
                     "-> structure suspecte, rejetee.",
                     consumed_, total_, path_.c_str());
            valid_ = false;
            return false;
        }
        valid_ = true;
        return true;
    } catch (const AssetError& e) {
        TS2_WARN("AtmosphereFile::Parse : \"%s\" (\"%s\").", e.what(), path_.c_str());
        valid_ = false;
        return false;
    }
}

} // namespace ts2::asset

// Asset/Sound.cpp — traduction fidèle de RE/asset_parsers/wsound.py (validé 75/75).
#include "Asset/Sound.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Core/Log.h"
#include <cstdio>
#include <cstring>

namespace ts2::asset {

namespace {
constexpr size_t   REC_META   = 100;      // octets par record métadonnées (this+8)
constexpr size_t   REC_EMIT   = 20;       // octets par record émetteur    (this+20)
constexpr size_t   NAME_BUF   = 64;       // taille du buffer nom au début du record 100 o
constexpr uint32_t COUNT_MAX  = 100000;   // borne de vraisemblance sur count
constexpr uint32_t COUNT2_MAX = 1000000;  // borne de vraisemblance sur count2
} // namespace

bool IsOgg(const uint8_t* data, size_t size) {
    return data && size >= 4 && std::memcmp(data, kOggMagic, 4) == 0;
}

bool ReadOggFile(const std::string& path, std::vector<uint8_t>& out) {
    if (!ReadWholeFile(path, out)) {
        TS2_ERR("OGG : ouverture impossible : %s", path.c_str());
        out.clear();
        return false;
    }
    if (!IsOgg(out)) {
        TS2_WARN("OGG : signature 'OggS' absente : %s", path.c_str());
        out.clear();
        return false;
    }
    return true;
}

bool WSound::Load(const std::string& path) {
    path_ = path;
    entries_.clear();
    emitters_.clear();
    count_ = count2_ = 0;
    sizeOk_ = false;
    expectedSize_ = actualSize_ = trailing_ = badIndex_ = 0;

    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data)) {
        TS2_ERR("WSOUND : ouverture impossible : %s", path.c_str());
        return false;
    }
    actualSize_ = data.size();

    try {
        if (data.size() < 8)
            throw AssetError("fichier trop court");

        ByteReader r(data);

        // [u32 count] : nb de sons (this+8).
        count_ = r.U32();
        if (count_ == 0 || count_ > COUNT_MAX)
            throw AssetError("count invraisemblable");

        // Vérifie qu'il reste de quoi lire les records méta + le u32 count2.
        const size_t needMeta = REC_META * static_cast<size_t>(count_);
        if (r.Pos() + needMeta + 4 > data.size())
            throw AssetError("pas assez d'octets pour les records meta");

        // count records de 100 o : nom source (64 o, null-terminé) + 36 o périmés.
        entries_.reserve(count_);
        for (uint32_t i = 0; i < count_; ++i) {
            std::string raw = r.Str(NAME_BUF);       // 64 octets bruts (latin-1)
            size_t z = raw.find('\0');
            if (z != std::string::npos) raw.resize(z);
            r.Skip(REC_META - NAME_BUF);             // saute les 36 o runtime
            WSoundEntry e;
            e.name = std::move(raw);
            entries_.push_back(std::move(e));
        }

        // [u32 count2] : nb d'émetteurs positionnels (this+16).
        count2_ = r.U32();
        if (count2_ > COUNT2_MAX)
            throw AssetError("count2 invraisemblable");

        // count2 records de 20 o : soundIndex(u32) + x,y,z,radius(float).
        const size_t needEmit = REC_EMIT * static_cast<size_t>(count2_);
        if (r.Pos() + needEmit > data.size())
            throw AssetError("pas assez d'octets pour les emetteurs");

        emitters_.reserve(count2_);
        for (uint32_t j = 0; j < count2_; ++j) {
            WSoundEmitter em;
            em.soundIndex = r.U32();
            em.x      = r.F32();
            em.y      = r.F32();
            em.z      = r.F32();
            em.radius = r.F32();
            emitters_.push_back(em);
        }

        // Validation (miroir du validateur Python).
        expectedSize_ = 4 + needMeta + 4 + needEmit;
        sizeOk_       = (expectedSize_ == actualSize_);
        trailing_     = actualSize_ - r.Pos();

        // Cohérence : soundIndex doit rester dans [0, count).
        for (const auto& em : emitters_)
            if (em.soundIndex >= count_) ++badIndex_;

        if (!sizeOk_)
            TS2_WARN("WSOUND : taille attendue=%zu reelle=%zu (trailing=%zu) : %s",
                     expectedSize_, actualSize_, trailing_, path.c_str());
        if (badIndex_)
            TS2_WARN("WSOUND : %zu emetteurs avec soundIndex hors [0,%u) : %s",
                     badIndex_, count_, path.c_str());

        return true;
    } catch (const std::exception& ex) {
        TS2_ERR("WSOUND : parse echoue (%s) : %s", ex.what(), path.c_str());
        entries_.clear();
        emitters_.clear();
        return false;
    }
}

std::string WSound::OggPathFor(size_t oneBasedIndex) const {
    // "<base>_%04d.OGG" (ex. "Z001.WSOUND_0001.OGG").
    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), "_%04zu.OGG", oneBasedIndex);
    return path_ + suffix;
}

size_t WSound::LoadExternalOggs() {
    size_t loaded = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const std::string ogg = OggPathFor(i + 1);  // index 1-base
        if (ReadOggFile(ogg, entries_[i].oggData))
            ++loaded;
    }
    return loaded;
}

} // namespace ts2::asset

// Asset/Sound.cpp — faithful translation of RE/asset_parsers/wsound.py (validated 75/75).
#include "Asset/Sound.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Core/Log.h"
#include <cstdio>
#include <cstring>

namespace ts2::asset {

namespace {
constexpr size_t   kRecMeta   = 100;      // bytes per metadata record (this+8)
constexpr size_t   kRecEmit   = 20;       // bytes per emitter record  (this+20)
constexpr size_t   kNameBuf   = 64;       // name buffer size at the start of the 100-byte record
constexpr uint32_t kCountMax  = 100000;   // plausibility bound on count
constexpr uint32_t kCount2Max = 1000000;  // plausibility bound on count2
} // namespace

bool IsOgg(const uint8_t* data, size_t size) {
    return data && size >= 4 && std::memcmp(data, kOggMagic, 4) == 0;
}

bool ReadOggFile(const std::string& path, std::vector<uint8_t>& out) {
    if (!ReadWholeFile(path, out)) {
        TS2_ERR("OGG: cannot open: %s", path.c_str());
        out.clear();
        return false;
    }
    if (!IsOgg(out)) {
        TS2_WARN("OGG: missing 'OggS' signature: %s", path.c_str());
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
        TS2_ERR("WSOUND: cannot open: %s", path.c_str());
        return false;
    }
    actualSize_ = data.size();

    try {
        if (data.size() < 8)
            throw AssetError("file too short");

        ByteReader r(data);

        // [u32 count]: number of sounds (this+8).
        count_ = r.U32();
        if (count_ == 0 || count_ > kCountMax)
            throw AssetError("implausible count");

        // Checks there are enough bytes left to read the meta records + the u32 count2.
        const size_t needMeta = kRecMeta * static_cast<size_t>(count_);
        if (r.Pos() + needMeta + 4 > data.size())
            throw AssetError("not enough bytes for meta records");

        // count 100-byte records: source name (64 bytes, null-terminated) + 36 stale bytes.
        entries_.reserve(count_);
        for (uint32_t i = 0; i < count_; ++i) {
            std::string raw = r.Str(kNameBuf);       // 64 raw bytes (latin-1)
            size_t z = raw.find('\0');
            if (z != std::string::npos) raw.resize(z);
            r.Skip(kRecMeta - kNameBuf);             // skip the 36 runtime bytes
            WSoundEntry e;
            e.name = std::move(raw);
            entries_.push_back(std::move(e));
        }

        // [u32 count2]: number of positional emitters (this+16).
        count2_ = r.U32();
        if (count2_ > kCount2Max)
            throw AssetError("implausible count2");

        // count2 20-byte records: soundIndex(u32) + x,y,z,radius(float).
        const size_t needEmit = kRecEmit * static_cast<size_t>(count2_);
        if (r.Pos() + needEmit > data.size())
            throw AssetError("not enough bytes for emitters");

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

        // Validation (mirrors the Python validator).
        expectedSize_ = 4 + needMeta + 4 + needEmit;
        sizeOk_       = (expectedSize_ == actualSize_);
        trailing_     = actualSize_ - r.Pos();

        // Consistency: soundIndex must stay within [0, count).
        for (const auto& em : emitters_)
            if (em.soundIndex >= count_) ++badIndex_;

        if (!sizeOk_)
            TS2_WARN("WSOUND: expected size=%zu actual=%zu (trailing=%zu): %s",
                     expectedSize_, actualSize_, trailing_, path.c_str());
        if (badIndex_)
            TS2_WARN("WSOUND: %zu emitters with soundIndex out of [0,%u): %s",
                     badIndex_, count_, path.c_str());

        return true;
    } catch (const std::exception& ex) {
        TS2_ERR("WSOUND: parse failed (%s): %s", ex.what(), path.c_str());
        entries_.clear();
        emitters_.clear();
        return false;
    }
}

std::string WSound::OggPathFor(size_t oneBasedIndex) const {
    // "<base>_%04d.OGG" (e.g. "Z001.WSOUND_0001.OGG").
    char suffix[32];
    std::snprintf(suffix, sizeof(suffix), "_%04zu.OGG", oneBasedIndex);
    return path_ + suffix;
}

size_t WSound::LoadExternalOggs() {
    size_t loaded = 0;
    for (size_t i = 0; i < entries_.size(); ++i) {
        const std::string ogg = OggPathFor(i + 1);  // 1-based index
        if (ReadOggFile(ogg, entries_[i].oggData))
            ++loaded;
    }
    return loaded;
}

} // namespace ts2::asset

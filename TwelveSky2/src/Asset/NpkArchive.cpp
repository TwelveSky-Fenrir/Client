// Asset/NpkArchive.cpp — traduction fidèle de RE/asset_parsers/npk.py (validé).
#include "Asset/NpkArchive.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <cctype>
#include <cstring>

namespace ts2::asset {

uint32_t NpkArchive::HashName(const std::string& name) {
    uint32_t a = 1, b = 0;
    for (unsigned char c : name) {
        uint32_t v = (c >= 'A' && c <= 'Z') ? (a + c + 32u) % 0xFFF1u
                                            : (a + c) % 0xFFF1u;
        a = v;
        b = (b + v) % 0xFFF1u;
    }
    return (a | (b << 16)) % 0x101u;
}

bool NpkArchive::Open(const std::string& path, const XteaKey& key) {
    key_ = key;
    entries_.clear();
    if (!ReadWholeFile(path, data_)) {
        TS2_ERR("NPK : ouverture impossible : %s", path.c_str());
        return false;
    }
    try {
        ByteReader r(data_);
        if (!(r.PeekMagic("NPK!", 4) || r.PeekMagic("NPAK", 4)))
            throw AssetError("magic NPK invalide");
        r.Skip(4);
        version_    = r.U32();
        entryCount_ = r.U32();
        dirOff_     = r.U32();
        dataOff_    = r.U32();
        if (version_ < 21) throw AssetError("version NPK non supportee");
        if (version_ >= 23) r.U32(); // hdrExtra (non validé)

        // Table de répertoire : [dirOff, dataOff), déchiffrée XTEA.
        const bool tail    = version_ >= 25;
        const bool variant = !(version_ >= 26); // >=26 => XTEA standard
        size_t start = dirOff_;
        size_t length = (version_ >= 24) ? (size_t(dataOff_) - dirOff_)
                                         : (data_.size() - dirOff_);
        if (start + length > data_.size()) throw AssetError("table repertoire hors fichier");

        std::vector<uint8_t> dir(data_.begin() + start, data_.begin() + start + length);
        XteaDecryptBuffer(dir.data(), dir.size(), key_, tail, variant);

        ByteReader dr(dir);
        entries_.reserve(entryCount_);
        for (uint32_t i = 0; i < entryCount_; ++i) {
            NpkEntry e;
            e.offset   = dr.U32();
            e.stored   = dr.U32();
            e.raw      = dr.U32();
            e.flags    = dr.U32();
            e.filetime = dr.U64();
            e.nameLen  = dr.U16();
            e.reserved = dr.U16();
            e.name     = dr.Str(e.nameLen);
            entries_.push_back(std::move(e));
        }
        return true;
    } catch (const std::exception& ex) {
        TS2_ERR("NPK : parse echoue (%s) : %s", ex.what(), path.c_str());
        entries_.clear();
        return false;
    }
}

std::vector<uint8_t> NpkArchive::Read(const NpkEntry& e) const {
    if (size_t(e.offset) + e.stored > data_.size())
        throw AssetError("blob NPK hors fichier : " + e.name);
    std::vector<uint8_t> blob(data_.begin() + e.offset, data_.begin() + e.offset + e.stored);
    const bool tail = version_ >= 25;

    // 1) variante XTEA AVANT décompression si (0x100 && 0x100000)
    if ((e.flags & kNpkX2) && (e.flags & kNpkX2Pre))
        XteaDecryptBuffer(blob.data(), blob.size(), key_, tail, /*variant*/ true);
    // 2) XTEA standard si 0x10
    if (e.flags & kNpkXtea)
        XteaDecryptBuffer(blob.data(), blob.size(), key_, tail, /*variant*/ false);
    // 3) décompression zlib si 0x1000 (seuil rawSize >= 0x100)
    if (e.flags & kNpkZlib) {
        if (e.raw >= 0x100) {
            blob = Zlib::Instance().InflateTo(blob.data(), blob.size(), e.raw);
        }
        // sinon : copie directe (déjà dans blob)
    }
    // 4) variante XTEA APRÈS décompression si (0x100 && !0x100000)
    if ((e.flags & kNpkX2) && !(e.flags & kNpkX2Pre))
        XteaDecryptBuffer(blob.data(), blob.size(), key_, /*tail*/ false, /*variant*/ true);

    return blob;
}

const NpkEntry* NpkArchive::Find(const std::string& name) const {
    for (const auto& e : entries_) {
        if (e.name.size() != name.size()) continue;
        bool eq = true;
        for (size_t i = 0; i < name.size(); ++i) {
            if (std::tolower((unsigned char)e.name[i]) != std::tolower((unsigned char)name[i])) {
                eq = false; break;
            }
        }
        if (eq) return &e;
    }
    return nullptr;
}

std::vector<uint8_t> NpkArchive::Read(const std::string& name) const {
    const NpkEntry* e = Find(name);
    if (!e) throw AssetError("entree NPK absente : " + name);
    return Read(*e);
}

} // namespace ts2::asset

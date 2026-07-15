// Asset/ImgFile.cpp — fidèle à RE/img_extract.py (validé sur 11839 textures + tables).
#include "Asset/ImgFile.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <cctype>
#include <cstring>

namespace ts2::asset {

bool ImgFile::Load(const std::string& path) {
    kind_ = ImgKind::Unknown;
    payload_.clear();
    fourCC_.clear();
    tableName_.clear();

    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data)) {
        TS2_ERR("IMG : ouverture impossible : %s", path.c_str());
        return false;
    }
    if (data.size() >= 4 && std::memcmp(data.data(), "PK\x03\x04", 4) == 0) {
        // Fichier ZIP (ex. GLS.IMG du launcher) — hors périmètre du client de jeu.
        TS2_ERR("IMG : fichier ZIP (launcher) ignore : %s", path.c_str());
        return false;
    }

    try {
        // Enveloppe [rawSize:u32][packedSize:u32][flux zlib].
        payload_ = Zlib::Instance().DecodeEnvelope(data.data(), data.size(), /*headerExtra*/ 0);
    } catch (const std::exception& ex) {
        TS2_ERR("IMG : decompression echouee (%s) : %s", ex.what(), path.c_str());
        return false;
    }

    // Classification du payload : texture DXT ? ou table de données ?
    const uint8_t* p = payload_.data();
    const size_t n = payload_.size();
    const size_t head = n < 64 ? n : 64;
    for (const char* fcc : {"DXT1", "DXT3", "DXT5"}) {
        for (size_t i = 0; i + 4 <= head; ++i) {
            if (std::memcmp(p + i, fcc, 4) == 0) { kind_ = ImgKind::TextureDxt; fourCC_ = fcc; return true; }
        }
    }
    // Table : chaîne ASCII imprimable à l'offset 4 (après le compteur XOR).
    if (n >= 8) {
        std::string name;
        for (size_t i = 4; i < n && i < 34; ++i) {
            char c = static_cast<char>(p[i]);
            if (c == 0) break;
            if (c < 32 || c > 126) { name.clear(); break; }
            name.push_back(c);
        }
        if (name.size() >= 3) { kind_ = ImgKind::Table; tableName_ = name; return true; }
    }
    kind_ = ImgKind::Raw;
    return true;
}

} // namespace ts2::asset

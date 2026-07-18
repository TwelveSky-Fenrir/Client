// Asset/ImgFile.cpp — faithful to RE/img_extract.py (validated on 11839 textures + tables).
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
        // ZIP file (e.g. launcher's GLS.IMG) — outside the game client's scope.
        TS2_ERR("IMG : fichier ZIP (launcher) ignore : %s", path.c_str());
        return false;
    }

    try {
        // Envelope [rawSize:u32][packedSize:u32][zlib stream]. Asset_DecompressImg 0x53F5E0.
        // ex-VeryOldClient: IMAGE_FOR_GXD (ZlibScope.h) — identical framing (CONFIRMED).
        payload_ = Zlib::Instance().DecodeEnvelope(data.data(), data.size(), /*headerExtra*/ 0);
    } catch (const std::exception& ex) {
        TS2_ERR("IMG : decompression echouee (%s) : %s", ex.what(), path.c_str());
        return false;
    }

    // Payload classification: DXT texture? or data table?
    // cTexture_LoadFromImgFile 0x457A20 (2D UI) / Tex_LoadCompressedDDS 0x6A2E80 (minimaps).
    // ex-VeryOldClient: IMAGE_FOR_GXD — the VeryOld D3DXIMAGE_INFO header has a DIFFERENT
    // layout; IDA wins (GXD header 36 bytes + FourCC@+28 + embedded DDS @+36). Detection by
    // FourCC scan alone = CONFIRMED. Pixel MATERIALIZATION (parse GXD header + embedded DDS
    // -> DXT blocks) is now handled by Texture::LoadFromImgFile (CPU layer); GPU upload by
    // gfx::GpuTexture (CreateFromTexture / CreateFromImgFile). Gap G5 closed on the Asset/ side.
    const uint8_t* p = payload_.data();
    const size_t n = payload_.size();
    const size_t head = n < 64 ? n : 64;
    for (const char* fcc : {"DXT1", "DXT3", "DXT5"}) {
        for (size_t i = 0; i + 4 <= head; ++i) {
            if (std::memcmp(p + i, fcc, 4) == 0) { kind_ = ImgKind::TextureDxt; fourCC_ = fcc; return true; }
        }
    }
    // Table: printable ASCII string at offset 4 (after the XOR counter).
    // GAP G1 (outside Asset/ scope, belongs to FRONT 9/GameData): decoding `count ^ MAGIC`
    // + fixed-stride records is NOT applied here. Cluster *_LoadImg 0x4C2680..0x4C8DA0,
    // MAGIC value per table (LevelTable 0x4C2680=0xE31, ITEM_INFO 0x4C3930=0x1CB3,
    // SkillGrowth 0x4C4BC0=0xC7E, MONSTER 0x4C62A0=0x1583, 0x4C6BD0=0x1022, SOCKET 0x4C7390=0xFDB).
    // Simple u32 integrity guard (NOT GXCW, NOT XTEA). Not covered by VeryOldClient.
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

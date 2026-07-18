// Asset/ImgFile.h — .IMG file reader (3 families, cf. Docs/TS2_IMG_FORMAT.md).
//   (Z) ZIP "PK\x03\x04"          -> GLS.IMG (launcher bundle)
//   (T) envelope [rawSize][packedSize][zlib] -> DXT1/DXT3 texture
//   (D) same envelope             -> encrypted data table [count^magic][name[30]][records]
// Common decoder = Asset_DecompressImg 0x53F5E0 -> uncompress (GXDCompress.dll).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

enum class ImgKind {
    Unknown,
    TextureDxt,// 2D texture: 36-byte GXD header + FourCC DXT1/DXT3 + embedded DDS (materialized
               // by Texture::LoadFromImgFile — cTexture_LoadFromImgFile 0x457A20)
    Table,     // data table: embedded name + records
    Raw,       // decompressed payload with no recognized marker
};

class ImgFile {
public:
    // Loads and decodes a .IMG. Returns false if read/decompression fails.
    // (ZIP "PK\x03\x04" case = launcher's GLS.IMG: Load() deliberately fails — outside
    // the game client's scope, no ZIP decoder exists in this Asset/ layer.)
    bool Load(const std::string& path);

    ImgKind Kind() const { return kind_; }
    // Decompressed payload (families T/D). Empty for Zip.
    const std::vector<uint8_t>& Payload() const { return payload_; }

    // Family T: texture FourCC ("DXT1"/"DXT3"), empty otherwise.
    const std::string& FourCC() const { return fourCC_; }

    // Family D: embedded name (e.g. "LEVEL_INFO"), empty otherwise.
    const std::string& TableName() const { return tableName_; }

private:
    ImgKind kind_ = ImgKind::Unknown;
    std::vector<uint8_t> payload_;
    std::string fourCC_;
    std::string tableName_;
};

} // namespace ts2::asset

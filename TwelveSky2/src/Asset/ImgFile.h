// Asset/ImgFile.h — lecteur des fichiers .IMG (3 familles, cf. Docs/TS2_IMG_FORMAT.md).
//   (Z) ZIP "PK\x03\x04"          -> GLS.IMG (bundle launcher)
//   (T) enveloppe [rawSize][packedSize][zlib] -> texture DXT1/DXT3
//   (D) même enveloppe            -> table de données chiffrée [count^magic][name[30]][records]
// Décodeur commun = Asset_DecompressImg 0x53F5E0 -> uncompress (GXDCompress.dll).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

enum class ImgKind {
    Unknown,
    TextureDxt,// texture 2D : en-tête GXD 36 o + FourCC DXT1/DXT3 + DDS embarqué (matérialisée
               // par Texture::LoadFromImgFile — cTexture_LoadFromImgFile 0x457A20)
    Table,     // table de données : nom embarqué + enregistrements
    Raw,       // payload décompressé sans marqueur reconnu
};

class ImgFile {
public:
    // Charge et décode un .IMG. Renvoie false si lecture/décompression impossible.
    // (Cas ZIP "PK\x03\x04" = GLS.IMG du launcher : Load() échoue volontairement — hors
    // périmètre du client de jeu, aucun décodeur ZIP n'existe dans cette couche Asset/.)
    bool Load(const std::string& path);

    ImgKind Kind() const { return kind_; }
    // Payload décompressé (familles T/D). Vide pour Zip.
    const std::vector<uint8_t>& Payload() const { return payload_; }

    // Famille T : FourCC de la texture ("DXT1"/"DXT3"), vide sinon.
    const std::string& FourCC() const { return fourCC_; }

    // Famille D : nom embarqué (ex. "LEVEL_INFO"), vide sinon.
    const std::string& TableName() const { return tableName_; }

private:
    ImgKind kind_ = ImgKind::Unknown;
    std::vector<uint8_t> payload_;
    std::string fourCC_;
    std::string tableName_;
};

} // namespace ts2::asset

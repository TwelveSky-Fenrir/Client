// Asset/Texture.cpp — fidèle à RE/asset_parsers/textures.py (validé 203/205).
#include "Asset/Texture.h"
#include "Asset/FileUtil.h"
#include "Core/Log.h"
#include <cstring>

namespace ts2::asset {

// ---------------------------------------------------------------------------
// Helpers locaux
// ---------------------------------------------------------------------------
namespace {

inline uint16_t Rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t Rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline bool Pow2(uint32_t v) { return v > 0 && (v & (v - 1)) == 0; }
inline uint8_t Scale5(uint32_t v) { return uint8_t((v << 3) | (v >> 2)); } // 5 bits -> 8 bits

// Nombre d'octets d'un bloc DXT (DXT1=8, DXT2..5=16). 0 => non-DXT.
size_t DxtBlockBytes(uint32_t fourccVal) {
    switch (fourccVal) {
        case kFourCC_DXT1: return 8;   // "DXT1"
        case 0x32545844u:             // "DXT2"
        case kFourCC_DXT3:             // "DXT3"
        case 0x34545844u:             // "DXT4"
        case kFourCC_DXT5: return 16;  // "DXT5"
        default: return 0;
    }
}

// Taille (octets) d'un niveau DXT WxH (identique à dxt_level_bytes du parseur).
size_t DxtLevelBytes(uint32_t w, uint32_t h, size_t blockBytes) {
    size_t bw = (w + 3) / 4; if (bw < 1) bw = 1;
    size_t bh = (h + 3) / 4; if (bh < 1) bh = 1;
    return bw * bh * blockBytes;
}

} // namespace

void Texture::Clear() {
    *this = Texture{};
}

// ---------------------------------------------------------------------------
// Détection de famille (cf. sniff() de textures.py — même ordre de priorité)
// ---------------------------------------------------------------------------
TextureFamily Texture::Sniff(const uint8_t* b, size_t n) {
    if (n >= 4 && std::memcmp(b, "DDS ", 4) == 0) return TextureFamily::Dds;
    if (n >= 4 && std::memcmp(b, "PK\x03\x04", 4) == 0) return TextureFamily::ImgZip;
    // Enveloppe GXD [rawSize:u32][packedSize:u32][flux zlib 78 01/9C/DA].
    if (n >= 10 && b[8] == 0x78 && (b[9] == 0x01 || b[9] == 0x9C || b[9] == 0xDA)) {
        uint32_t raw = Rd32(b), packed = Rd32(b + 4);
        if (size_t(8) + packed <= n + 4 && raw >= packed) return TextureFamily::ImgGxd;
    }
    // TGA : pas de magic ; heuristique sur l'en-tête.
    if (n >= 18) {
        uint8_t cmaptype = b[1], imgtype = b[2], bpp = b[16];
        bool typeOk = (imgtype == 0 || imgtype == 1 || imgtype == 2 || imgtype == 3 ||
                       imgtype == 9 || imgtype == 10 || imgtype == 11);
        bool bppOk = (bpp == 8 || bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32);
        if ((cmaptype == 0 || cmaptype == 1) && typeOk && bppOk) return TextureFamily::Tga;
    }
    return TextureFamily::Unknown;
}

// ---------------------------------------------------------------------------
// Chargeurs fichier
// ---------------------------------------------------------------------------
bool Texture::LoadFile(const std::string& path) {
    Clear();
    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data)) {
        TS2_ERR("Texture : ouverture impossible : %s", path.c_str());
        return false;
    }
    switch (Sniff(data.data(), data.size())) {
        case TextureFamily::Dds:
            return LoadFromDdsMemory(data.data(), data.size());
        case TextureFamily::Tga:
            return LoadFromTgaMemory(data.data(), data.size());
        case TextureFamily::ImgZip:
            // GLS.IMG (launcher, PAS le client de jeu) : archive ZIP(store) de membres
            // zlib. Détecté uniquement (family/pixels vides) : ImgFile::Load() refuse
            // volontairement ce cas (hors périmètre), aucun décodeur ZIP n'existe dans
            // cette couche Asset/ — le client de jeu ne charge jamais de .IMG au format
            // ZIP (seul le launcher, hors périmètre de ClientSource, le fait).
            family = TextureFamily::ImgZip;
            TS2_WARN("Texture : enveloppe IMG-ZIP (PK) detectee -- format launcher (GLS.IMG), "
                     "non decode (hors perimetre du client de jeu) : %s", path.c_str());
            return true;
        case TextureFamily::ImgGxd: {
            family = TextureFamily::ImgGxd;
            imgRawSize    = Rd32(data.data());
            imgPackedSize = Rd32(data.data() + 4);
            TS2_WARN("Texture : enveloppe IMG-GXD (raw=%u packed=%u) -> decoder via ImgFile : %s",
                     imgRawSize, imgPackedSize, path.c_str());
            return true;
        }
        default:
            TS2_ERR("Texture : format non reconnu : %s", path.c_str());
            return false;
    }
}

bool Texture::LoadTGA(const std::string& path) {
    Clear();
    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data)) {
        TS2_ERR("TGA : ouverture impossible : %s", path.c_str());
        return false;
    }
    return LoadFromTgaMemory(data.data(), data.size());
}

bool Texture::LoadDDS(const std::string& path) {
    Clear();
    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data)) {
        TS2_ERR("DDS : ouverture impossible : %s", path.c_str());
        return false;
    }
    return LoadFromDdsMemory(data.data(), data.size());
}

// ---------------------------------------------------------------------------
// DDS (.SHADOW et DDS bruts) — cf. parse_dds()
// ---------------------------------------------------------------------------
bool Texture::LoadFromDdsMemory(const uint8_t* b, size_t n) {
    Clear();
    if (n < 128) { TS2_ERR("DDS : fichier < 128 o (en-tete incomplet)"); return false; }
    if (std::memcmp(b, "DDS ", 4) != 0) { TS2_ERR("DDS : magic != 'DDS '"); return false; }

    uint32_t dwSize   = Rd32(b + 4);
    uint32_t dwHeight = Rd32(b + 12);
    uint32_t dwWidth  = Rd32(b + 16);
    uint32_t dwMips   = Rd32(b + 28);
    uint32_t fccVal   = Rd32(b + 84);
    if (dwSize != 124) { TS2_ERR("DDS : dwSize != 124 (%u)", dwSize); return false; }

    family      = TextureFamily::Dds;
    width       = dwWidth;
    height      = dwHeight;
    mipCount    = dwMips;
    fourCCValue = fccVal;

    // Nom FourCC lisible (octets 84..87, nuls de fin retirés).
    char fcc[5] = { char(b[84]), char(b[85]), char(b[86]), char(b[87]), 0 };
    for (int i = 3; i >= 0 && fcc[i] == 0; --i) fcc[i] = 0;
    fourCC.assign(fcc);
    acceptedByLoader = (fccVal == kFourCC_DXT1 || fccVal == kFourCC_DXT3 || fccVal == kFourCC_DXT5);

    size_t blockBytes = DxtBlockBytes(fccVal);
    if (blockBytes == 0) {
        // DDS non-DXT (pixelformat RGB/A) : on ne calcule pas la taille exacte,
        // on conserve simplement les octets bruts après le header.
        format = PixelFormat::DdsRaw;
        pixels.assign(b + 128, b + n);
        return true;
    }

    // Taille attendue = somme des niveaux de mip (comme parse_dds).
    uint32_t levels = dwMips > 0 ? dwMips : 1;
    size_t total = 0;
    uint32_t w = dwWidth, h = dwHeight;
    for (uint32_t i = 0; i < levels; ++i) {
        total += DxtLevelBytes(w, h, blockBytes);
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    size_t need = 128 + total;
    if (n < need) {
        TS2_ERR("DDS : trop court : %zu < %zu attendu (%s %u niveaux)", n, need, fourCC.c_str(), levels);
        Clear();
        return false;
    }
    // On conserve exactement `total` octets de blocs (ignore un éventuel footer).
    format = PixelFormat::DxtBlocks;
    pixels.assign(b + 128, b + 128 + total);
    return true;
}

// ---------------------------------------------------------------------------
// TGA — cf. parse_tga() + décodage effectif des pixels en RGBA8 top-down.
// ---------------------------------------------------------------------------
bool Texture::LoadFromTgaMemory(const uint8_t* b, size_t n) {
    Clear();
    if (n < 18) { TS2_ERR("TGA : fichier < 18 o (en-tete incomplet)"); return false; }

    const uint8_t idlen    = b[0];
    const uint8_t cmaptype = b[1];
    const uint8_t imgtype  = b[2];
    const uint16_t cmFirst  = Rd16(b + 3);
    const uint16_t cmLen    = Rd16(b + 5);
    const uint8_t  cmEnt    = b[7];
    const uint16_t w    = Rd16(b + 12);
    const uint16_t h    = Rd16(b + 14);
    const uint8_t  bppS = b[16];
    const uint8_t  desc = b[17];

    // Types connus (TGA_TYPE du parseur).
    const bool known = (imgtype == 0 || imgtype == 1 || imgtype == 2 || imgtype == 3 ||
                        imgtype == 9 || imgtype == 10 || imgtype == 11);
    if (!known) { TS2_ERR("TGA : image type inconnu %u", imgtype); return false; }
    if (w == 0 || h == 0) { TS2_ERR("TGA : dimensions nulles"); return false; }

    const bool topO   = (desc & 0x20) != 0;
    const bool rightO = (desc & 0x10) != 0;

    // Offsets (identiques au parseur).
    const size_t headerAndId = size_t(18) + idlen;
    size_t cmapBytes = 0;
    if (cmaptype == 1) cmapBytes = size_t(cmLen) * ((cmEnt + 7) / 8);
    const size_t pixelOff = headerAndId + cmapBytes;

    // Vérification de taille pour les formats non compressés (types 2 et 3).
    if (imgtype == 2 || imgtype == 3) {
        const size_t need = pixelOff + size_t(w) * h * (bppS / 8);
        if (n < need) {
            TS2_ERR("TGA : trop court : %zu < %zu attendu", n, need);
            return false;
        }
    }
    if (pixelOff > n) { TS2_ERR("TGA : offset pixel hors fichier"); return false; }

    const bool isColormap  = (imgtype == 1 || imgtype == 9);
    const bool isGray      = (imgtype == 3 || imgtype == 11);
    const bool isRLE       = (imgtype == 9 || imgtype == 10 || imgtype == 11);

    // Octets par unité source lue (index colormap ou pixel direct).
    const size_t srcBpp = (bppS + 7) / 8; // 8->1, 15/16->2, 24->3, 32->4

    // Décode un échantillon direct (non colormap) en RGBA.
    auto sampleRGBA = [&](const uint8_t* s, uint8_t& r, uint8_t& g, uint8_t& bl, uint8_t& a) {
        if (isGray) {
            uint8_t v = s[0]; r = g = bl = v; a = (srcBpp >= 2) ? s[1] : 255;
        } else if (srcBpp >= 4) {          // 32 bpp : B,G,R,A
            bl = s[0]; g = s[1]; r = s[2]; a = s[3];
        } else if (srcBpp == 3) {          // 24 bpp : B,G,R
            bl = s[0]; g = s[1]; r = s[2]; a = 255;
        } else {                           // 15/16 bpp : A RRRRR GGGGG BBBBB
            uint16_t v = uint16_t(s[0] | (s[1] << 8));
            r  = Scale5((v >> 10) & 31);
            g  = Scale5((v >> 5) & 31);
            bl = Scale5(v & 31);
            a  = 255; // le bit d'alpha 16 bpp est ignoré (opaque), comme le converter
        }
    };

    // Palette (colormap) décodée en RGBA, indexée par index absolu.
    std::vector<uint8_t> pal; // 4 o/entrée
    if (isColormap && cmaptype == 1) {
        const size_t entBytes = (cmEnt + 7) / 8;
        const size_t palStart = headerAndId; // la colormap suit l'ID field
        if (palStart + size_t(cmLen) * entBytes > n) {
            TS2_ERR("TGA : colormap hors fichier");
            return false;
        }
        const size_t palCount = size_t(cmFirst) + cmLen;
        pal.assign(palCount * 4, 0);
        for (uint16_t i = 0; i < cmLen; ++i) {
            const uint8_t* s = b + palStart + size_t(i) * entBytes;
            uint8_t r, g, bl, a;
            if (entBytes >= 4)      { bl = s[0]; g = s[1]; r = s[2]; a = s[3]; }
            else if (entBytes == 3) { bl = s[0]; g = s[1]; r = s[2]; a = 255; }
            else {                    uint16_t v = uint16_t(s[0] | (s[1] << 8));
                                      r = Scale5((v >> 10) & 31); g = Scale5((v >> 5) & 31);
                                      bl = Scale5(v & 31); a = 255; }
            uint8_t* d = &pal[size_t(cmFirst + i) * 4];
            d[0] = r; d[1] = g; d[2] = bl; d[3] = a;
        }
    }

    // Buffer de scan : RGBA dans l'ordre de stockage du fichier (avant normalisation).
    const size_t pixelCount = size_t(w) * h;
    std::vector<uint8_t> scan(pixelCount * 4);

    size_t cur = pixelOff; // curseur de lecture dans b

    // Écrit un pixel (index de scan) à partir d'une unité source à `cur`.
    auto emit = [&](size_t idx) -> bool {
        if (cur + srcBpp > n) return false;
        const uint8_t* s = b + cur;
        uint8_t r, g, bl, a;
        if (isColormap) {
            size_t index = (srcBpp >= 2) ? Rd16(s) : s[0];
            if (index * 4 + 3 < pal.size()) {
                const uint8_t* p = &pal[index * 4];
                r = p[0]; g = p[1]; bl = p[2]; a = p[3];
            } else { r = g = bl = 0; a = 255; }
        } else {
            sampleRGBA(s, r, g, bl, a);
        }
        uint8_t* d = &scan[idx * 4];
        d[0] = r; d[1] = g; d[2] = bl; d[3] = a;
        cur += srcBpp;
        return true;
    };

    if (!isRLE) {
        for (size_t i = 0; i < pixelCount; ++i) {
            if (!emit(i)) { TS2_ERR("TGA : donnees pixel tronquees"); return false; }
        }
    } else {
        // RLE : paquets [header:u8] ; bit7 => run (1 pixel répété), sinon raw (count pixels).
        size_t i = 0;
        while (i < pixelCount) {
            if (cur >= n) { TS2_ERR("TGA : flux RLE tronque"); return false; }
            uint8_t hdr = b[cur++];
            size_t count = size_t(hdr & 0x7F) + 1;
            if (hdr & 0x80) { // run-length : une unité source, répétée
                if (cur + srcBpp > n) { TS2_ERR("TGA : run RLE tronque"); return false; }
                const uint8_t* s = b + cur;
                uint8_t r, g, bl, a;
                if (isColormap) {
                    size_t index = (srcBpp >= 2) ? Rd16(s) : s[0];
                    if (index * 4 + 3 < pal.size()) {
                        const uint8_t* p = &pal[index * 4];
                        r = p[0]; g = p[1]; bl = p[2]; a = p[3];
                    } else { r = g = bl = 0; a = 255; }
                } else {
                    sampleRGBA(s, r, g, bl, a);
                }
                cur += srcBpp;
                for (size_t k = 0; k < count && i < pixelCount; ++k, ++i) {
                    uint8_t* d = &scan[i * 4];
                    d[0] = r; d[1] = g; d[2] = bl; d[3] = a;
                }
            } else { // raw : count unités source consécutives
                for (size_t k = 0; k < count && i < pixelCount; ++k, ++i) {
                    if (!emit(i)) { TS2_ERR("TGA : raw RLE tronque"); return false; }
                }
            }
        }
    }

    // Normalisation en top-down, gauche-à-droite (RGBA8).
    // top_origin=0 => rangées stockées de bas en haut ; right_origin=1 => miroir X.
    pixels.resize(pixelCount * 4);
    for (uint32_t y = 0; y < h; ++y) {
        uint32_t srcRow = topO ? y : (h - 1 - y);
        for (uint32_t x = 0; x < w; ++x) {
            uint32_t srcCol = rightO ? (w - 1 - x) : x;
            const uint8_t* s = &scan[(size_t(srcRow) * w + srcCol) * 4];
            uint8_t* d = &pixels[(size_t(y) * w + x) * 4];
            d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3];
        }
    }

    family = TextureFamily::Tga;
    width  = w;
    height = h;
    format = PixelFormat::RGBA8;
    bpp    = bppS;
    tgaImageType = imgtype;
    topOrigin    = topO;
    rightOrigin  = rightO;
    convertibleDxt = (imgtype == 2 && (bppS == 24 || bppS == 32) && Pow2(w) && Pow2(h));
    return true;
}

} // namespace ts2::asset

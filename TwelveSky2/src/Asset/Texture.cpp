// Asset/Texture.cpp — faithful to RE/asset_parsers/textures.py (validated 203/205).
#include "Asset/Texture.h"
#include "Asset/FileUtil.h"
#include "Asset/ImgFile.h"   // materializes .IMG family T (LoadFromImgFile) — Asset_DecompressImg 0x53F5E0
#include "Core/Log.h"
#include <cstring>

namespace ts2::asset {

// Local helpers
namespace {

inline uint16_t Rd16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
inline uint32_t Rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
inline bool Pow2(uint32_t v) { return v > 0 && (v & (v - 1)) == 0; }
inline uint8_t Scale5(uint32_t v) { return uint8_t((v << 3) | (v >> 2)); } // 5 bits -> 8 bits

// Number of bytes in a DXT block (DXT1=8, DXT2..5=16). 0 => non-DXT.
// Tex_LoadFromFile 0x6A9910 (Format ∈ {DXT1/DXT3/DXT5}). ex-VeryOldClient: TEXTURE_FOR_GXD
// (Load2/LoadGXCW "requires DXT") — mip calc + FourCC byte-exact (CONFIRMED).
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

// Size (bytes) of a DXT level WxH (identical to dxt_level_bytes in the parser).
size_t DxtLevelBytes(uint32_t w, uint32_t h, size_t blockBytes) {
    size_t bw = (w + 3) / 4; if (bw < 1) bw = 1;
    size_t bh = (h + 3) / 4; if (bh < 1) bh = 1;
    return bw * bh * blockBytes;
}

} // namespace

void Texture::Clear() {
    *this = Texture{};
}

// Family detection (cf. sniff() in textures.py — same priority order)
TextureFamily Texture::Sniff(const uint8_t* b, size_t n) {
    if (n >= 4 && std::memcmp(b, "DDS ", 4) == 0) return TextureFamily::Dds;
    if (n >= 4 && std::memcmp(b, "PK\x03\x04", 4) == 0) return TextureFamily::ImgZip;
    // GXD envelope [rawSize:u32][packedSize:u32][zlib stream 78 01/9C/DA].
    if (n >= 10 && b[8] == 0x78 && (b[9] == 0x01 || b[9] == 0x9C || b[9] == 0xDA)) {
        uint32_t raw = Rd32(b), packed = Rd32(b + 4);
        if (size_t(8) + packed <= n + 4 && raw >= packed) return TextureFamily::ImgGxd;
    }
    // TGA: no magic; heuristic on the header.
    if (n >= 18) {
        uint8_t cmaptype = b[1], imgtype = b[2], bpp = b[16];
        bool typeOk = (imgtype == 0 || imgtype == 1 || imgtype == 2 || imgtype == 3 ||
                       imgtype == 9 || imgtype == 10 || imgtype == 11);
        bool bppOk = (bpp == 8 || bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32);
        if ((cmaptype == 0 || cmaptype == 1) && typeOk && bppOk) return TextureFamily::Tga;
    }
    return TextureFamily::Unknown;
}

// File loaders
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
            // GLS.IMG (launcher, NOT the game client): ZIP(store) archive of zlib
            // members. Detected only (family/pixels empty): ImgFile::Load() deliberately
            // refuses this case (out of scope), no ZIP decoder exists in this Asset/
            // layer — the game client never loads .IMG in ZIP format (only the launcher,
            // out of scope for ClientSource, does).
            family = TextureFamily::ImgZip;
            TS2_WARN("Texture : enveloppe IMG-ZIP (PK) detectee -- format launcher (GLS.IMG), "
                     "non decode (hors perimetre du client de jeu) : %s", path.c_str());
            return true;
        case TextureFamily::ImgGxd: {
            // GXD envelope [rawSize][packedSize][zlib]. Decompression + classification
            // delegated to ImgFile (which wraps Asset_DecompressImg 0x53F5E0), then
            // family T materialization. cTexture_LoadFromImgFile 0x457A20.
            const uint32_t rawSz    = Rd32(data.data());
            const uint32_t packedSz = Rd32(data.data() + 4);
            ImgFile img;
            if (img.Load(path) && img.Kind() == ImgKind::TextureDxt && LoadFromImgFile(img)) {
                imgRawSize    = rawSz;    // LoadFromImgFile()->Clear() erased them: re-expose
                imgPackedSize = packedSz; // the envelope sizes (informative).
                return true;
            }
            // Family D (data table) or materialization not possible: envelope detected,
            // pixels empty (historical behavior preserved for non-textures).
            Clear();
            family        = TextureFamily::ImgGxd;
            imgRawSize    = rawSz;
            imgPackedSize = packedSz;
            TS2_WARN("Texture : enveloppe IMG-GXD (raw=%u packed=%u) non materialisee en texture : %s",
                     rawSz, packedSz, path.c_str());
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

// DDS (.SHADOW and raw DDS) — cf. parse_dds(); Tex_LoadFromFile 0x6A9910 (rejects non-DXT).
// ex-VeryOldClient: TEXTURE_FOR_GXD::LoadFromDDS (CONFIRMED) — VeryOld DXT1 alpha heuristic
// (alphaMode = data[65] >= data[64] ? 1 : 2) cross-checks the open IDA question (u16 @128/130).
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

    // Readable FourCC name (bytes 84..87, trailing nulls stripped).
    char fcc[5] = { char(b[84]), char(b[85]), char(b[86]), char(b[87]), 0 };
    for (int i = 3; i >= 0 && fcc[i] == 0; --i) fcc[i] = 0;
    fourCC.assign(fcc);
    acceptedByLoader = (fccVal == kFourCC_DXT1 || fccVal == kFourCC_DXT3 || fccVal == kFourCC_DXT5);

    size_t blockBytes = DxtBlockBytes(fccVal);
    if (blockBytes == 0) {
        // Non-DXT DDS (RGB/A pixelformat): exact size not computed,
        // raw bytes after the header are kept as-is.
        format = PixelFormat::DdsRaw;
        pixels.assign(b + 128, b + n);
        return true;
    }

    // Expected size = sum of mip levels (like parse_dds).
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
    // Keep exactly `total` bytes of blocks (ignore any footer).
    format = PixelFormat::DxtBlocks;
    pixels.assign(b + 128, b + 128 + total);
    return true;
}

// .IMG GXD texture (family T) — cTexture_LoadFromImgFile 0x457A20 / Tex_LoadCompressedDDS 0x6A2E80
//
// PROVEN format (BOTH loaders are byte-identical; disassembly + real extraction
// on 001_00001/002, 002_00001, Z001_MINIMAP01/02) — the decompressed payload is:
//   +0  u32 width       (LOGICAL,  e.g. 261)   -> copied raw into the sprite: qmemcpy 0x1C @0x457B67
//   +4  u32 height      (LOGICAL,  e.g. 90)
//   +8  u32[5]          (dw2=1 dw3=1 dw4=0x14/0x15 dw5=3 dw6=2; flags/type, NOT read by the loader)
//   +28 u32 D3DFORMAT   (FourCC "DXT1"/"DXT3")               -> v11[7], passed as Format to D3DX
//   +32 u32 dataSize    (size of the embedded image file)    -> v11[8], SrcDataSize
//   +36 u8[dataSize]    = STANDARD Microsoft DDS file ("DDS ", 124-byte header), already pow2
// The binary does: D3DXCreateTextureFromFileInMemoryEx(dev, payload+36, dataSize,
//   NextPow2(width), NextPow2(height), 1 mip, DXTn, D3DPOOL_MANAGED, ...) — 0x457BC5 / 0x6A3040.
//
// RESOLUTION of the open "2 subforms" question (TS2_ASSET_ROSETTA.md / gap G5): there is only
// ONE format. The embedded DDS is ALREADY rounded to a power of 2 (261x90 logical -> DDS
// 512x128), so NextPow2(logical) == DDS dimensions and D3DX does NOT resize. We therefore
// materialize the DDS as-is (width/height = physical pow2 dims) and keep the LOGICAL
// dimensions separately (imgLogicalWidth/Height), exactly like the original sprite
// (qmemcpy of the raw header BEFORE any NextPow2; getters Sprite2D_GetWidth/GetHeight 0x4D6CD0/0x4D6D20).
//
// PURE CPU LAYER — no "D3D9 upload" here: Asset/ does NOT depend on d3d9/d3dx9 (otherwise any
// headless consumer of Asset — AssetSelfTest, parsers — would inherit the Direct3D SDK, and we'd
// duplicate gfx::GpuTexture). GPU upload via the existing device therefore stays in the
// Gfx layer, already faithful and guarded (`if(!dev) return false`):
//   - gfx::GpuTexture::CreateFromTexture(dev, asset::Texture)  -> DXT blocks via CreateTexture+LockRect
//     (consumes the result of THIS parse: PixelFormat::DxtBlocks + width/height/fourCC/mipCount),
//   - gfx::GpuTexture::CreateFromImgFile(dev, asset::ImgFile)  -> exact D3DX replica of 0x457A20.
bool Texture::LoadFromImgTexturePayload(const uint8_t* payload, size_t size) {
    Clear();
    if (!payload || size < 36) {
        TS2_ERR("IMG-tex : payload < 36 o (en-tete GXD incomplet)");
        return false;
    }

    const uint32_t w0     = Rd32(payload + 0);   // logical width (GXD header +0)
    const uint32_t h0     = Rd32(payload + 4);   // logical height (GXD header +4)
    const uint32_t fccVal = Rd32(payload + 28);  // FourCC/D3DFORMAT (GXD header +28)
    const uint32_t dataSz = Rd32(payload + 32);  // size of the embedded file (GXD header +32)

    if (dataSz > size - 36) {   // size >= 36 guaranteed above -> no overflow (Win32 32-bit size_t)
        TS2_ERR("IMG-tex : bloc image hors payload (dataSize=%u, payload=%zu)", dataSz, size);
        return false;
    }
    const uint8_t* embedded = payload + 36;      // embedded image file (SrcData)

    // PROVEN subform (11839/11839 .IMG family T): the embedded file is a standard DDS.
    if (dataSz >= 4 && std::memcmp(embedded, "DDS ", 4) == 0) {
        if (!LoadFromDdsMemory(embedded, dataSz)) return false;  // width/height=pow2, fourCC, DXT blocks
        // Keep the LOGICAL dimensions from the GXD header (LoadFromDdsMemory set the pow2 ones).
        imgLogicalWidth  = w0;
        imgLogicalHeight = h0;
        return true;
    }

    // Subform NOT observed in the corpus. D3DXCreateTextureFromFileInMemoryEx would also
    // accept BMP/TGA/PNG, but no CPU decoder for these containers exists in this layer.
    // TODO(0x457A20 @0x457BC5 / D3DXCreateTextureFromFileInMemoryEx @0x6BB660): if such a
    // file appeared, materializing it would go through the gfx::GpuTexture::
    // CreateFromImgFile GPU bridge (D3DX path already faithful), NOT here — Asset/ stays free of D3D9 dependency.
    TS2_ERR("IMG-tex : fichier embarque non-DDS (FourCC en-tete 0x%08X) non decode en CPU",
            static_cast<unsigned>(fccVal));
    return false;
}

bool Texture::LoadFromImgFile(const ImgFile& img) {
    if (img.Kind() != ImgKind::TextureDxt) {
        TS2_ERR("IMG-tex : ImgFile kind != TextureDxt (payload non-texture)");
        return false;
    }
    const std::vector<uint8_t>& pl = img.Payload();
    return LoadFromImgTexturePayload(pl.data(), pl.size());
}

// TGA — cf. parse_tga() + actual pixel decoding to top-down RGBA8.
// Tex_LoadTgaConvert 0x417180 / Tex_LoadDDS 0x6A2680.
// ex-VeryOldClient: TEXTURE_FOR_GXD::LoadFromTGA (CONFIRMED for the on-disk format).
// GAP G6 (PLAUSIBLE, low confidence): the target re-encodes TGA 24/32 to DXT1/DXT3 via
// D3DXCreateTextureFromFileInMemoryEx (+ temp DDS "HONGCHANGWOO"); here we materialize
// RGBA8 (visually equivalent, DXT bit-exactness not reproduced).
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

    // Known types (TGA_TYPE from the parser).
    const bool known = (imgtype == 0 || imgtype == 1 || imgtype == 2 || imgtype == 3 ||
                        imgtype == 9 || imgtype == 10 || imgtype == 11);
    if (!known) { TS2_ERR("TGA : image type inconnu %u", imgtype); return false; }
    if (w == 0 || h == 0) { TS2_ERR("TGA : dimensions nulles"); return false; }

    const bool topO   = (desc & 0x20) != 0;
    const bool rightO = (desc & 0x10) != 0;

    // Offsets (same as the parser).
    const size_t headerAndId = size_t(18) + idlen;
    size_t cmapBytes = 0;
    if (cmaptype == 1) cmapBytes = size_t(cmLen) * ((cmEnt + 7) / 8);
    const size_t pixelOff = headerAndId + cmapBytes;

    // Size check for uncompressed formats (types 2 and 3).
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

    // Bytes per source unit read (colormap index or direct pixel).
    const size_t srcBpp = (bppS + 7) / 8; // 8->1, 15/16->2, 24->3, 32->4

    // Decode a direct (non-colormap) sample into RGBA.
    auto sampleRGBA = [&](const uint8_t* s, uint8_t& r, uint8_t& g, uint8_t& bl, uint8_t& a) {
        if (isGray) {
            uint8_t v = s[0]; r = g = bl = v; a = (srcBpp >= 2) ? s[1] : 255;
        } else if (srcBpp >= 4) {          // 32 bpp: B,G,R,A
            bl = s[0]; g = s[1]; r = s[2]; a = s[3];
        } else if (srcBpp == 3) {          // 24 bpp: B,G,R
            bl = s[0]; g = s[1]; r = s[2]; a = 255;
        } else {                           // 15/16 bpp: A RRRRR GGGGG BBBBB
            uint16_t v = uint16_t(s[0] | (s[1] << 8));
            r  = Scale5((v >> 10) & 31);
            g  = Scale5((v >> 5) & 31);
            bl = Scale5(v & 31);
            a  = 255; // the 16 bpp alpha bit is ignored (opaque), like the converter
        }
    };

    // Palette (colormap) decoded to RGBA, indexed by absolute index.
    std::vector<uint8_t> pal; // 4 bytes/entry
    if (isColormap && cmaptype == 1) {
        const size_t entBytes = (cmEnt + 7) / 8;
        const size_t palStart = headerAndId; // the colormap follows the ID field
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

    // Scan buffer: RGBA in the file's storage order (before normalization).
    const size_t pixelCount = size_t(w) * h;
    std::vector<uint8_t> scan(pixelCount * 4);

    size_t cur = pixelOff; // read cursor into b

    // Write a pixel (scan index) from a source unit at `cur`.
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
        // RLE: packets [header:u8]; bit7 => run (1 repeated pixel), otherwise raw (count pixels).
        size_t i = 0;
        while (i < pixelCount) {
            if (cur >= n) { TS2_ERR("TGA : flux RLE tronque"); return false; }
            uint8_t hdr = b[cur++];
            size_t count = size_t(hdr & 0x7F) + 1;
            if (hdr & 0x80) { // run-length: one source unit, repeated
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
            } else { // raw: count consecutive source units
                for (size_t k = 0; k < count && i < pixelCount; ++k, ++i) {
                    if (!emit(i)) { TS2_ERR("TGA : raw RLE tronque"); return false; }
                }
            }
        }
    }

    // Normalize to top-down, left-to-right (RGBA8).
    // top_origin=0 => rows stored bottom-up; right_origin=1 => X mirror.
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

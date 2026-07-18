// Asset/Texture.h — reader for TwelveSky2's various texture formats.
// Faithful translation of RE/asset_parsers/textures.py (validated 203/205).
// ex-VeryOldClient: TEXTURE_FOR_GXD (LoadFromTGA/LoadFromDDS) — read names/logic; target
// side = zlib only, no GXCW substitution (LoadGXCW/Load2 = VeryOld artifact, CONFLICT §4.B).
//
// Three families loaded by the client (IDA loaders in []):
//   (TGA)    raw .tga, standard 18-byte TGA header. The client only accepts type 2
//            (true-color 24/32 bpp, power-of-2 dimensions) into the DXT converter
//            [Tex_LoadTgaConvert 0x417180]; others (8 bpp gray, colormap) are
//            loaded directly by D3DX.
//   (SHADOW) .SHADOW = standard DDS ("DDS " + 124-byte header), FourCC DXT1/3/5.
//            [Tex_LoadFromFile 0x6A9910] only accepts DXT1/DXT3/DXT5.
//   (IMG)    GXD envelope [rawSize:u32][packedSize:u32][zlib stream] -> after inflate,
//            36-byte GXD header + embedded Microsoft DDS file (family T). Materialized
//            here via LoadFromImgFile / LoadFromImgTexturePayload (parses the GXD header
//            then delegates the embedded DDS to LoadFromDdsMemory); family D (data table)
//            stays out of scope. cTexture_LoadFromImgFile 0x457A20 (2D UI) /
//            Tex_LoadCompressedDDS 0x6A2E80 (minimaps) — same PROVEN format.
//
// This reader goes further than the Python parser: it MATERIALIZES the pixels.
//   - TGA  -> decoded to top-down RGBA8 (32 bpp), ready for upload.
//   - DDS  -> keeps compressed DXT blocks as-is (GPU/D3DX decodes them);
//             fourCC + mip count + dimensions exposed.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

class ImgFile; // Asset/ImgFile.h — source of the decompressed GXD payload (LoadFromImgFile)

// DXT FourCC values accepted by the texture loaders (Tex_LoadFromFile 0x6A9910).
constexpr uint32_t kFourCC_DXT1 = 0x31545844u; // "DXT1"
constexpr uint32_t kFourCC_DXT3 = 0x33545844u; // "DXT3"
constexpr uint32_t kFourCC_DXT5 = 0x35545844u; // "DXT5"

// Detected file family (cf. sniff() in textures.py).
enum class TextureFamily {
    Unknown,
    Tga,      // raw TGA decoded to RGBA8
    Dds,      // DDS/.SHADOW: DXT blocks (or raw RGB/A data) kept as-is
    ImgZip,   // "PK\x03\x04" envelope (GLS.IMG, launcher) — never decoded: ImgFile::Load()
              // also refuses this case (out of scope for the game client, no ZIP decoder here)
    ImgGxd,   // [rawSize][packedSize][zlib] envelope — LoadFile now MATERIALIZES it as a
              // texture (family T) via ImgFile + LoadFromImgFile; family D (table) -> empty pixels
};

// Interpretation of the `pixels` field's bytes.
enum class PixelFormat {
    Unknown,
    RGBA8,     // decoded TGA: 4 bytes/pixel, R,G,B,A order, top-down
    DxtBlocks, // DDS DXT1/3/5: compressed blocks (payload after the 128-byte header)
    DdsRaw,    // non-DXT DDS (RGB/A pixelformat): raw bytes after the header
};

struct Texture {
    TextureFamily family = TextureFamily::Unknown;
    uint32_t width  = 0;
    uint32_t height = 0;
    PixelFormat format = PixelFormat::Unknown;

    // ----- DDS ------------------------------------------------------------
    std::string fourCC;              // "DXT1"/"DXT3"/"DXT5" (nulls stripped); empty if N/A
    uint32_t    fourCCValue = 0;     // FourCC as LE u32
    uint32_t    mipCount    = 0;     // dwMipMapCount (0 => 1 implicit level)
    bool        acceptedByLoader = false; // true if DXT1/3/5 (Tex_LoadFromFile)

    // ----- TGA ------------------------------------------------------------
    uint8_t tgaImageType = 0;        // 1 colormap, 2 truecolor, 3 gray, 9/10/11 RLE
    uint8_t bpp = 0;                 // source bits/pixel (8/16/24/32)
    bool    topOrigin   = false;     // descriptor bit5: top-left origin
    bool    rightOrigin = false;     // descriptor bit4: right-side origin
    bool    convertibleDxt = false;  // eligible for the DXT converter (type2 24/32 pow2)

    // ----- IMG (envelope + GXD header, family T) -----------------------
    uint32_t imgRawSize    = 0;      // ImgGxd: envelope's declared rawSize
    uint32_t imgPackedSize = 0;      // ImgGxd: envelope's declared packedSize
    // LOGICAL sprite dimensions (GXD header +0/+4), DISTINCT from the physical pow2
    // surface of the embedded DDS (that one -> width/height above). 0 outside .IMG
    // family T. cTexture_LoadFromImgFile 0x457A20 (raw 0x1C-byte header qmemcpy @0x457B67,
    // BEFORE any NextPow2); getters Sprite2D_GetWidth 0x4D6CD0 / Sprite2D_GetHeight 0x4D6D20.
    // E.g. 001_00001.IMG: logical 261x90, embedded DDS 512x128.
    uint32_t imgLogicalWidth  = 0;
    uint32_t imgLogicalHeight = 0;

    // Pixel/block data (cf. PixelFormat). Empty for ImgZip/ImgGxd.
    std::vector<uint8_t> pixels;

    bool Empty() const { return pixels.empty(); }
    void Clear();

    // Universal entry point: dispatches by magic (then implicit extension).
    // Returns true for decoded TGA/DDS, and also for detected ImgZip/ImgGxd
    // (in that case pixels stays empty: go through ImgFile to decompress).
    bool LoadFile(const std::string& path);

    // Explicit per-family loads.
    bool LoadTGA(const std::string& path);
    bool LoadDDS(const std::string& path);

    // Decodes a standard Microsoft DDS already in memory (magic "DDS ", 124-byte header).
    // Sources: .SHADOW extracted from an NPK, OR the DDS file embedded in a family T
    // .IMG at offset +36 (cf. LoadFromImgTexturePayload below).
    bool LoadFromDdsMemory(const uint8_t* data, size_t size);

    // ---- .IMG GXD texture materialization (family T) --------------------------------
    // cTexture_LoadFromImgFile 0x457A20 (2D UI) / Tex_LoadCompressedDDS 0x6A2E80 (minimaps)
    // — same PROVEN format (real extraction: 001/002/007 + Z*_MINIMAP). `img` must
    // be an ImgFile of kind==TextureDxt (payload already decompressed & classified). Parses
    // the 36-byte GXD header (width@+0 / height@+4 / FourCC@+28 / dataSize@+32) then
    // materializes the embedded DDS file (+36) into DXT blocks via LoadFromDdsMemory. Result:
    //   width/height          = physical pow2 dimensions of the DDS (D3D9 surface),
    //   imgLogicalWidth/Height = LOGICAL sprite dimensions (GXD header).
    // Pure CPU layer: NO D3D9 dependency here — GPU upload stays in gfx::GpuTexture
    // (CreateFromTexture = DXT blocks via LockRect; CreateFromImgFile = D3DX replica of
    // 0x457A20). See the detailed note in Texture.cpp.
    bool LoadFromImgFile(const ImgFile& img);
    bool LoadFromImgTexturePayload(const uint8_t* payload, size_t size);

    // Decodes a TGA already in memory.
    bool LoadFromTgaMemory(const uint8_t* data, size_t size);

    // Detects a buffer's family without decoding it (cf. sniff() in the parser).
    static TextureFamily Sniff(const uint8_t* data, size_t size);
};

} // namespace ts2::asset

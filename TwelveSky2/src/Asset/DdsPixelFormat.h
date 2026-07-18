// Asset/DdsPixelFormat.h — parses the `ddspf` block (DDS_PIXELFORMAT) of a
// NON-DXT DDS and maps R/G/B/A masks -> D3DFORMAT.
//
// IDA anchors (RE/TwelveSky2.exe.i64, imagebase 0x400000):
//   - Tex_ReadPacked      0x417740  (model textures: decompresses, then
//                                    D3DXGetImageInfoFromFileInMemory 0x6BB666 ->
//                                    a1[7] = Format; gate DXT1/2/3/5 @0x41795D)
//   - Tex_LoadImageFile   0x4173A0  (same on file; gate DXT @0x417480)
//   - Tex_ReadFromMemory  0x417D20  (memory variant; gate DXT @0x417EFE)
// All THREE loaders delegate format detection to the import
//   D3DXGetImageInfoFromFileInMemory @0x6BB666 (which fills D3DXIMAGE_INFO,
//   field Format = a1[7] computed FROM `ddspf`), then reject any FourCC other
//   than DXT1/2/3/5. The ground truth of the mask->D3DFORMAT mapping is thus
//   the D3DX9 / Microsoft DDS convention (the binary does not re-encode this
//   mapping itself).
//
// Why this file: the census (Docs/TS2_IMG_FORMAT.md §0) counts 5 `GXD_RAW`
// entries — DDS files WITHOUT a DXT FourCC (masked RGB/A pixelformat).
// asset::Texture classifies them as PixelFormat::DdsRaw then DISCARDS the
// `ddspf` masks (Texture.cpp:177); the gfx::GpuTexture::CreateFromTexture
// bridge thus returns false (GpuTexture.cpp:87). This module recovers the
// masks and deduces the exact D3DFORMAT to make these 5 textures uploadable
// (backlog T3 of Docs/TS2_DEEP_TEX_NPK.md).
//
// PURE CPU LAYER: like all of Asset/, this file does NOT depend on d3d9/d3dx9.
// The target format is exposed via the DdsD3dFormat enum, whose numeric values
// DUPLICATE identically the D3DFORMAT constants from <d3d9types.h> — the Gfx
// layer casts directly `static_cast<D3DFORMAT>(uint32_t(fmt))`.
//
// DDS offsets PROVEN in IDA (qmemcpy(v41, base+4, 0x7C) @0x4179B0 / 0x417F63):
//   file+0   char[4] magic "DDS "
//   file+4   u32     dwSize        (== 124)                 v41[0]
//   file+8   u32     dwFlags                                v41[1]
//   file+12  u32     dwHeight                               v41[2]
//   file+16  u32     dwWidth                                v41[3]
//   file+20  u32     dwPitchOrLinearSize                    v41[4]
//   file+24  u32     dwDepth                                v41[5]
//   file+28  u32     dwMipMapCount                          v41[6]
//   file+32  u32[11] dwReserved1                            v41[7..17]
//   file+76  ---- DDS_PIXELFORMAT `ddspf` (32 bytes) ----   v41[18..25]
//     +76  u32 dwSize (== 32) | +80 dwFlags | +84 dwFourCC     (Texture.cpp:158 reads +84)
//     +88  u32 dwRGBBitCount | +92 dwRBitMask | +96 dwGBitMask
//     +100 u32 dwBBitMask     | +104 dwABitMask
//   file+108 u32 dwCaps ... (header ends at +128)
#pragma once
#include <cstddef>
#include <cstdint>

namespace ts2::asset {

// --- `ddspf.dwFlags` flags (DDPF_*, Microsoft DDS) --------------------------
constexpr uint32_t kDdpfAlphaPixels = 0x00000001u; // DDPF_ALPHAPIXELS : dwABitMask valid
constexpr uint32_t kDdpfAlpha       = 0x00000002u; // DDPF_ALPHA       : alpha only (A8)
constexpr uint32_t kDdpfFourCC      = 0x00000004u; // DDPF_FOURCC      : dwFourCC valid
constexpr uint32_t kDdpfPaletteIdx8 = 0x00000020u; // DDPF_PALETTEINDEXED8
constexpr uint32_t kDdpfRgb         = 0x00000040u; // DDPF_RGB         : uncompressed RGB(A)
constexpr uint32_t kDdpfYuv         = 0x00000200u; // DDPF_YUV
constexpr uint32_t kDdpfLuminance   = 0x00020000u; // DDPF_LUMINANCE   : L/LA
constexpr uint32_t kDdpfBumpDuDv    = 0x00080000u; // DDPF_BUMPDUDV    : signed U/V

// --- File offsets (see table above) -----------------------------------------
constexpr size_t kDdsMagicSize        = 4;    // "DDS "
constexpr size_t kDdsHeaderSize       = 124;  // DDS_HEADER (dwSize)
constexpr size_t kDdsFullHeaderSize   = 128;  // magic + header: raw pixels start here
constexpr size_t kDdsPixelFormatOff   = 76;   // `ddspf` within the file
constexpr size_t kDdsHeightOff        = 12;   // dwHeight
constexpr size_t kDdsWidthOff         = 16;   // dwWidth
constexpr size_t kDdsMipCountOff      = 28;   // dwMipMapCount

// Magic "DDS " as little-endian u32 (0x20534444).
constexpr uint32_t kDdsMagicValue = 0x20534444u;

// --- Numeric D3DFORMAT values (<d3d9types.h>), duplicated to keep Asset/ -----
// FREE of a Direct3D dependency. Gfx: static_cast<D3DFORMAT>(uint32_t(fmt)).
enum class DdsD3dFormat : uint32_t {
    Unknown      = 0,   // D3DFMT_UNKNOWN
    R8G8B8       = 20,  // D3DFMT_R8G8B8
    A8R8G8B8     = 21,  // D3DFMT_A8R8G8B8
    X8R8G8B8     = 22,  // D3DFMT_X8R8G8B8
    R5G6B5       = 23,  // D3DFMT_R5G6B5
    X1R5G5B5     = 24,  // D3DFMT_X1R5G5B5
    A1R5G5B5     = 25,  // D3DFMT_A1R5G5B5
    A4R4G4B4     = 26,  // D3DFMT_A4R4G4B4
    R3G3B2       = 27,  // D3DFMT_R3G3B2
    A8           = 28,  // D3DFMT_A8
    A8R3G3B2     = 29,  // D3DFMT_A8R3G3B2
    X4R4G4B4     = 30,  // D3DFMT_X4R4G4B4
    A2B10G10R10  = 31,  // D3DFMT_A2B10G10R10
    A8B8G8R8     = 32,  // D3DFMT_A8B8G8R8
    X8B8G8R8     = 33,  // D3DFMT_X8B8G8R8
    G16R16       = 34,  // D3DFMT_G16R16
    A2R10G10B10  = 35,  // D3DFMT_A2R10G10B10
    A16B16G16R16 = 36,  // D3DFMT_A16B16G16R16
    L8           = 50,  // D3DFMT_L8
    A8L8         = 51,  // D3DFMT_A8L8
    A4L4         = 52,  // D3DFMT_A4L4
    V8U8         = 60,  // D3DFMT_V8U8
    Q8W8V8U8     = 63,  // D3DFMT_Q8W8V8U8
    V16U16       = 64,  // D3DFMT_V16U16
    L16          = 81,  // D3DFMT_L16
};

// --- `ddspf` mirrored as-is (8 dwords at file+76) ---------------------------
struct DdsPixelFormat {
    uint32_t size        = 0;  // dwSize        (file+76, == 32)
    uint32_t flags       = 0;  // dwFlags       (file+80, DDPF_*)
    uint32_t fourCC      = 0;  // dwFourCC      (file+84)
    uint32_t rgbBitCount = 0;  // dwRGBBitCount (file+88)
    uint32_t rMask       = 0;  // dwRBitMask    (file+92)
    uint32_t gMask       = 0;  // dwGBitMask    (file+96)
    uint32_t bMask       = 0;  // dwBBitMask    (file+100)
    uint32_t aMask       = 0;  // dwABitMask    (file+104)

    bool IsFourCC()    const { return (flags & kDdpfFourCC) != 0; }
    bool HasAlpha()    const { return (flags & kDdpfAlphaPixels) != 0; }
    bool IsRgb()       const { return (flags & kDdpfRgb) != 0; }
    bool IsLuminance() const { return (flags & kDdpfLuminance) != 0; }
    bool IsAlphaOnly() const { return (flags & kDdpfAlpha) != 0; }
    bool IsBumpDuDv()  const { return (flags & kDdpfBumpDuDv) != 0; }
};

// --- Raw DDS surface ready for upload (DdsRaw path) -------------------------
// Groups everything the Gfx layer needs for a CreateTexture + LockRect of a
// non-DXT DDS: the deduced D3DFORMAT, dimensions (dwWidth/dwHeight from the
// header), mip level count, and the raw pixel data offset (128).
struct DdsRawSurface {
    DdsD3dFormat format       = DdsD3dFormat::Unknown;
    uint32_t     width        = 0;
    uint32_t     height       = 0;
    uint32_t     mipCount     = 0;   // dwMipMapCount (0 => 1 implicit level)
    uint32_t     bitsPerPixel = 0;   // ddspf.dwRGBBitCount
    size_t       dataOffset   = kDdsFullHeaderSize; // raw pixels after the 128-byte header

    // Row pitch (bytes) of level 0 for a linear packed format.
    uint32_t RowPitch() const { return width * (bitsPerPixel / 8u); }
    // Size (bytes) of level 0 alone (no mips).
    size_t   Level0Bytes() const { return size_t(RowPitch()) * height; }
    bool     Valid() const {
        return format != DdsD3dFormat::Unknown && width != 0 && height != 0;
    }
};

// Reads the `ddspf` block (file+76, 32 bytes) of a complete in-memory DDS.
// Validates "DDS " @0 and dwSize@+4 == 124. Returns false if too short / not a DDS.
bool ReadDdsPixelFormat(const uint8_t* ddsFile, size_t size, DdsPixelFormat& out);

// Maps a NON-DXT `ddspf` (RGB / luminance / alpha / masked bump) to a
// D3DFORMAT, per the D3DX9 / Microsoft DDS convention (see file header).
//   - DDPF_FOURCC with a "4-letter" FourCC (e.g. "DXT1") -> Unknown: these DDS
//     go through the block path (PixelFormat::DxtBlocks of Texture.cpp), NOT here.
//   - DDPF_FOURCC with a small value (<= 0xFF) -> direct D3DFORMAT: D3DX
//     convention for extended formats (float, A16B16G16R16, ...) stored as-is
//     in the FourCC field.
//   - Otherwise: exact match (bitCount + R/G/B/A masks) against the known table.
// Returns DdsD3dFormat::Unknown if no format is recognized.
DdsD3dFormat MapDdsPixelFormatToD3dFormat(const DdsPixelFormat& pf);

// Full parse: reads `ddspf` + dwWidth/dwHeight/dwMipMapCount and deduces the
// format, ready for a GPU upload via the DdsRaw path. Returns false if the file
// is not a non-DXT DDS with a recognized format (out.Valid() is then false).
bool ParseDdsRawSurface(const uint8_t* ddsFile, size_t size, DdsRawSurface& out);

// Bytes per pixel of a linear packed DdsD3dFormat (0 if unknown / compressed).
uint32_t DdsD3dFormatBytesPerPixel(DdsD3dFormat fmt);

} // namespace ts2::asset

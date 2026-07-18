// Asset/DdsPixelFormat.cpp — `ddspf` parsing + D3DFORMAT mapping implementation.
//
// Ground truth = D3DX9: the 3 model-texture loaders (Tex_ReadPacked 0x417740,
// Tex_LoadImageFile 0x4173A0, Tex_ReadFromMemory 0x417D20) delegate format
// detection to D3DXGetImageInfoFromFileInMemory (import @0x6BB666), which fills
// the Format field of D3DXIMAGE_INFO (= a1[7]) from `ddspf`. These loaders THEN
// only keep DXT1/2/3/5 (gate @0x41795D / 0x417480 / 0x417EFE) and discard the
// rest; this is where we recover the non-DXT path (the 5 GXD_RAW entries from
// the census, Docs/TS2_IMG_FORMAT.md §0).
//
// The mask->D3DFORMAT mapping below reproduces the D3DX9 / Microsoft DDS
// convention (DDS_PIXELFORMAT, cf. <d3d9types.h> D3DFMT_* and the DirectX SDK
// `dds.h` header). EXACT match on (flag category, dwRGBBitCount, R/G/B/A) —
// this is what distinguishes A8R8G8B8 from X8R8G8B8, A1R5G5B5 from X1R5G5B5, etc.
#include "Asset/DdsPixelFormat.h"

#include <cstring>

namespace ts2::asset {

namespace {

// Read a little-endian u32 (on-disk DDS files are native x86 LE).
inline uint32_t Rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// One entry of the recognition table: (flag category, bit count,
// R/G/B/A masks) -> format. `category` is the required discriminating DDPF_* bit.
struct MaskEntry {
    uint32_t     category;    // DDPF_RGB / DDPF_LUMINANCE / DDPF_ALPHA / DDPF_BUMPDUDV
    uint32_t     bitCount;    // expected dwRGBBitCount
    uint32_t     r, g, b, a;  // expected exact masks
    DdsD3dFormat format;
};

// NON-DXT recognition table, aligned on the D3DX9 / DDS convention.
// Order matters: A* (alpha) variants precede X* (padding) ones, discrimination
// being done on the exact alpha mask.
constexpr MaskEntry kMaskTable[] = {
    // ---- Uncompressed RGB(A) (DDPF_RGB) ----------------------------------
    // 32 bpp
    { kDdpfRgb, 32, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0xff000000u, DdsD3dFormat::A8R8G8B8   },
    { kDdpfRgb, 32, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0x00000000u, DdsD3dFormat::X8R8G8B8   },
    { kDdpfRgb, 32, 0x000000ffu, 0x0000ff00u, 0x00ff0000u, 0xff000000u, DdsD3dFormat::A8B8G8R8   },
    { kDdpfRgb, 32, 0x000000ffu, 0x0000ff00u, 0x00ff0000u, 0x00000000u, DdsD3dFormat::X8B8G8R8   },
    { kDdpfRgb, 32, 0x0000ffffu, 0xffff0000u, 0x00000000u, 0x00000000u, DdsD3dFormat::G16R16     },
    { kDdpfRgb, 32, 0x3ff00000u, 0x000ffc00u, 0x000003ffu, 0xc0000000u, DdsD3dFormat::A2R10G10B10 },
    { kDdpfRgb, 32, 0x000003ffu, 0x000ffc00u, 0x3ff00000u, 0xc0000000u, DdsD3dFormat::A2B10G10R10 },
    // 24 bpp
    { kDdpfRgb, 24, 0x00ff0000u, 0x0000ff00u, 0x000000ffu, 0x00000000u, DdsD3dFormat::R8G8B8     },
    // 16 bpp
    { kDdpfRgb, 16, 0x0000f800u, 0x000007e0u, 0x0000001fu, 0x00000000u, DdsD3dFormat::R5G6B5     },
    { kDdpfRgb, 16, 0x00007c00u, 0x000003e0u, 0x0000001fu, 0x00008000u, DdsD3dFormat::A1R5G5B5   },
    { kDdpfRgb, 16, 0x00007c00u, 0x000003e0u, 0x0000001fu, 0x00000000u, DdsD3dFormat::X1R5G5B5   },
    { kDdpfRgb, 16, 0x00000f00u, 0x000000f0u, 0x0000000fu, 0x0000f000u, DdsD3dFormat::A4R4G4B4   },
    { kDdpfRgb, 16, 0x00000f00u, 0x000000f0u, 0x0000000fu, 0x00000000u, DdsD3dFormat::X4R4G4B4   },
    { kDdpfRgb, 16, 0x000000e0u, 0x0000001cu, 0x00000003u, 0x0000ff00u, DdsD3dFormat::A8R3G3B2   },
    // 8 bpp
    { kDdpfRgb,  8, 0x000000e0u, 0x0000001cu, 0x00000003u, 0x00000000u, DdsD3dFormat::R3G3B2     },

    // ---- Luminance (DDPF_LUMINANCE) ---------------------------------------
    { kDdpfLuminance, 16, 0x000000ffu, 0x00000000u, 0x00000000u, 0x0000ff00u, DdsD3dFormat::A8L8 },
    { kDdpfLuminance, 16, 0x0000ffffu, 0x00000000u, 0x00000000u, 0x00000000u, DdsD3dFormat::L16  },
    { kDdpfLuminance,  8, 0x0000000fu, 0x00000000u, 0x00000000u, 0x000000f0u, DdsD3dFormat::A4L4 },
    { kDdpfLuminance,  8, 0x000000ffu, 0x00000000u, 0x00000000u, 0x00000000u, DdsD3dFormat::L8   },

    // ---- Alpha only (DDPF_ALPHA) -------------------------------------------
    { kDdpfAlpha, 8, 0x00000000u, 0x00000000u, 0x00000000u, 0x000000ffu, DdsD3dFormat::A8 },

    // ---- Signed U/V bump (DDPF_BUMPDUDV) -----------------------------------
    { kDdpfBumpDuDv, 16, 0x000000ffu, 0x0000ff00u, 0x00000000u, 0x00000000u, DdsD3dFormat::V8U8     },
    { kDdpfBumpDuDv, 32, 0x0000ffffu, 0xffff0000u, 0x00000000u, 0x00000000u, DdsD3dFormat::V16U16   },
    { kDdpfBumpDuDv, 32, 0x000000ffu, 0x0000ff00u, 0x00ff0000u, 0xff000000u, DdsD3dFormat::Q8W8V8U8 },
};

} // namespace

bool ReadDdsPixelFormat(const uint8_t* b, size_t n, DdsPixelFormat& out) {
    out = DdsPixelFormat{};
    // Need at least the full header (128 bytes) to read `ddspf` (+76..+107).
    if (!b || n < kDdsFullHeaderSize) return false;
    if (std::memcmp(b, "DDS ", 4) != 0) return false;      // magic @0
    if (Rd32(b + 4) != kDdsHeaderSize) return false;       // dwSize @+4 == 124

    const uint8_t* pf = b + kDdsPixelFormatOff;             // `ddspf` @+76
    out.size        = Rd32(pf + 0);
    out.flags       = Rd32(pf + 4);
    out.fourCC      = Rd32(pf + 8);
    out.rgbBitCount = Rd32(pf + 12);
    out.rMask       = Rd32(pf + 16);
    out.gMask       = Rd32(pf + 20);
    out.bMask       = Rd32(pf + 24);
    out.aMask       = Rd32(pf + 28);
    return true;
}

DdsD3dFormat MapDdsPixelFormatToD3dFormat(const DdsPixelFormat& pf) {
    // FourCC path: compressed DDS (DXT1..5) are NOT "raw" surfaces — they go
    // through the Texture.cpp block path (DxtBlocks).
    if (pf.IsFourCC()) {
        // D3DX convention: an EXTENDED format (float, A16B16G16R16, ...) is
        // stored directly in the FourCC field as a small D3DFORMAT integer
        // (<= 0xFF, so not a "4-letter" ASCII code). Return it as-is.
        if (pf.fourCC != 0 && pf.fourCC <= 0xFFu) {
            return static_cast<DdsD3dFormat>(pf.fourCC);
        }
        return DdsD3dFormat::Unknown; // "DXT1".. and other 4CC: outside the raw path
    }

    // Exact match against the table: flag category + bitCount + masks.
    for (const MaskEntry& e : kMaskTable) {
        if ((pf.flags & e.category) != 0 &&
            pf.rgbBitCount == e.bitCount &&
            pf.rMask == e.r && pf.gMask == e.g &&
            pf.bMask == e.b && pf.aMask == e.a) {
            return e.format;
        }
    }
    return DdsD3dFormat::Unknown;
}

bool ParseDdsRawSurface(const uint8_t* b, size_t n, DdsRawSurface& out) {
    out = DdsRawSurface{};
    DdsPixelFormat pf;
    if (!ReadDdsPixelFormat(b, n, pf)) return false;

    const DdsD3dFormat fmt = MapDdsPixelFormatToD3dFormat(pf);
    if (fmt == DdsD3dFormat::Unknown) return false; // DXT or unrecognized format

    out.format       = fmt;
    out.width        = Rd32(b + kDdsWidthOff);     // dwWidth  @+16
    out.height       = Rd32(b + kDdsHeightOff);    // dwHeight @+12
    out.mipCount     = Rd32(b + kDdsMipCountOff);  // dwMipMapCount @+28
    // For an extended format stored in FourCC (bitCount often 0), fall back to
    // the depth deduced from the D3DFORMAT; otherwise take dwRGBBitCount directly.
    out.bitsPerPixel = pf.rgbBitCount != 0 ? pf.rgbBitCount
                                           : (DdsD3dFormatBytesPerPixel(fmt) * 8u);
    out.dataOffset   = kDdsFullHeaderSize;         // raw pixels at +128
    return out.Valid();
}

uint32_t DdsD3dFormatBytesPerPixel(DdsD3dFormat fmt) {
    switch (fmt) {
        case DdsD3dFormat::A8:
        case DdsD3dFormat::R3G3B2:
        case DdsD3dFormat::L8:
        case DdsD3dFormat::A4L4:
            return 1;
        case DdsD3dFormat::R5G6B5:
        case DdsD3dFormat::X1R5G5B5:
        case DdsD3dFormat::A1R5G5B5:
        case DdsD3dFormat::A4R4G4B4:
        case DdsD3dFormat::A8R3G3B2:
        case DdsD3dFormat::X4R4G4B4:
        case DdsD3dFormat::A8L8:
        case DdsD3dFormat::V8U8:
        case DdsD3dFormat::L16:
            return 2;
        case DdsD3dFormat::R8G8B8:
            return 3;
        case DdsD3dFormat::A8R8G8B8:
        case DdsD3dFormat::X8R8G8B8:
        case DdsD3dFormat::A8B8G8R8:
        case DdsD3dFormat::X8B8G8R8:
        case DdsD3dFormat::G16R16:
        case DdsD3dFormat::A2B10G10R10:
        case DdsD3dFormat::A2R10G10B10:
        case DdsD3dFormat::V16U16:
        case DdsD3dFormat::Q8W8V8U8:
            return 4;
        case DdsD3dFormat::A16B16G16R16:
            return 8;
        default:
            return 0;
    }
}

} // namespace ts2::asset

// Asset/DdsPixelFormat.cpp — implémentation du parse `ddspf` + mapping D3DFORMAT.
//
// Vérité = D3DX9 : les 3 loaders de textures de modèle (Tex_ReadPacked 0x417740,
// Tex_LoadImageFile 0x4173A0, Tex_ReadFromMemory 0x417D20) délèguent la détection
// de format à D3DXGetImageInfoFromFileInMemory (import @0x6BB666), qui remplit le
// champ Format de D3DXIMAGE_INFO (= a1[7]) à partir de `ddspf`. Ces loaders ne
// retiennent ENSUITE que DXT1/2/3/5 (gate @0x41795D / 0x417480 / 0x417EFE) et
// jettent le reste ; c'est ici qu'on récupère le chemin non-DXT (les 5 GXD_RAW
// du census, Docs/TS2_IMG_FORMAT.md §0).
//
// Le mapping masque->D3DFORMAT ci-dessous reproduit la convention D3DX9 / DDS
// Microsoft (DDS_PIXELFORMAT, cf. <d3d9types.h> D3DFMT_* et l'en-tête `dds.h` du
// DirectX SDK). Match EXACT sur (catégorie de drapeau, dwRGBBitCount, R/G/B/A) —
// c'est ce qui distingue A8R8G8B8 de X8R8G8B8, A1R5G5B5 de X1R5G5B5, etc.
#include "Asset/DdsPixelFormat.h"

#include <cstring>

namespace ts2::asset {

namespace {

// Lecture u32 little-endian (les DDS sur disque sont natifs x86 LE).
inline uint32_t Rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Une entrée de la table de reconnaissance : (catégorie de drapeau, nb de bits,
// masques R/G/B/A) -> format. `category` est le bit DDPF_* discriminant exigé.
struct MaskEntry {
    uint32_t     category;    // DDPF_RGB / DDPF_LUMINANCE / DDPF_ALPHA / DDPF_BUMPDUDV
    uint32_t     bitCount;    // dwRGBBitCount attendu
    uint32_t     r, g, b, a;  // masques exacts attendus
    DdsD3dFormat format;
};

// Table de reconnaissance NON-DXT, alignée sur la convention D3DX9 / DDS.
// L'ordre importe : les variantes A* (alpha) précèdent les X* (padding), la
// discrimination se faisant sur le masque alpha exact.
constexpr MaskEntry kMaskTable[] = {
    // ---- RGB(A) non compressé (DDPF_RGB) --------------------------------
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

    // ---- Luminance (DDPF_LUMINANCE) -------------------------------------
    { kDdpfLuminance, 16, 0x000000ffu, 0x00000000u, 0x00000000u, 0x0000ff00u, DdsD3dFormat::A8L8 },
    { kDdpfLuminance, 16, 0x0000ffffu, 0x00000000u, 0x00000000u, 0x00000000u, DdsD3dFormat::L16  },
    { kDdpfLuminance,  8, 0x0000000fu, 0x00000000u, 0x00000000u, 0x000000f0u, DdsD3dFormat::A4L4 },
    { kDdpfLuminance,  8, 0x000000ffu, 0x00000000u, 0x00000000u, 0x00000000u, DdsD3dFormat::L8   },

    // ---- Alpha seul (DDPF_ALPHA) ----------------------------------------
    { kDdpfAlpha, 8, 0x00000000u, 0x00000000u, 0x00000000u, 0x000000ffu, DdsD3dFormat::A8 },

    // ---- Bump U/V signé (DDPF_BUMPDUDV) ---------------------------------
    { kDdpfBumpDuDv, 16, 0x000000ffu, 0x0000ff00u, 0x00000000u, 0x00000000u, DdsD3dFormat::V8U8     },
    { kDdpfBumpDuDv, 32, 0x0000ffffu, 0xffff0000u, 0x00000000u, 0x00000000u, DdsD3dFormat::V16U16   },
    { kDdpfBumpDuDv, 32, 0x000000ffu, 0x0000ff00u, 0x00ff0000u, 0xff000000u, DdsD3dFormat::Q8W8V8U8 },
};

} // namespace

bool ReadDdsPixelFormat(const uint8_t* b, size_t n, DdsPixelFormat& out) {
    out = DdsPixelFormat{};
    // Il faut au moins l'en-tête complet (128 o) pour que `ddspf` (+76..+107) soit lu.
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
    // Chemin FourCC : les DDS compressés (DXT1..5) ne sont PAS des surfaces
    // "brutes" — ils passent par le chemin blocs de Texture.cpp (DxtBlocks).
    if (pf.IsFourCC()) {
        // Convention D3DX : un format ÉTENDU (float, A16B16G16R16, ...) est rangé
        // en clair dans le champ FourCC sous forme d'un petit entier D3DFORMAT
        // (<= 0xFF, donc pas un code ASCII "4 lettres"). On le renvoie tel quel.
        if (pf.fourCC != 0 && pf.fourCC <= 0xFFu) {
            return static_cast<DdsD3dFormat>(pf.fourCC);
        }
        return DdsD3dFormat::Unknown; // "DXT1".. et autres 4CC : hors chemin brut
    }

    // Match exact contre la table : catégorie de drapeau + bitCount + masques.
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
    if (fmt == DdsD3dFormat::Unknown) return false; // DXT ou format non reconnu

    out.format       = fmt;
    out.width        = Rd32(b + kDdsWidthOff);     // dwWidth  @+16
    out.height       = Rd32(b + kDdsHeightOff);    // dwHeight @+12
    out.mipCount     = Rd32(b + kDdsMipCountOff);  // dwMipMapCount @+28
    // Pour un format étendu rangé en FourCC (bitCount souvent 0), on retombe sur
    // la profondeur déduite du D3DFORMAT ; sinon on prend dwRGBBitCount directement.
    out.bitsPerPixel = pf.rgbBitCount != 0 ? pf.rgbBitCount
                                           : (DdsD3dFormatBytesPerPixel(fmt) * 8u);
    out.dataOffset   = kDdsFullHeaderSize;         // pixels bruts à +128
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

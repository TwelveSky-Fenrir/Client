// Asset/DdsPixelFormat.h — parse du bloc `ddspf` (DDS_PIXELFORMAT) d'un DDS
// NON-DXT et mapping des masques R/G/B/A -> D3DFORMAT.
//
// Ancres IDA (RE/TwelveSky2.exe.i64, imagebase 0x400000) :
//   - Tex_ReadPacked      0x417740  (textures de modèle : décompresse, puis
//                                    D3DXGetImageInfoFromFileInMemory 0x6BB666 ->
//                                    a1[7] = Format ; gate DXT1/2/3/5 @0x41795D)
//   - Tex_LoadImageFile   0x4173A0  (idem sur fichier ; gate DXT @0x417480)
//   - Tex_ReadFromMemory  0x417D20  (variante mémoire ; gate DXT @0x417EFE)
// Les TROIS loaders délèguent la détection de format à l'import
//   D3DXGetImageInfoFromFileInMemory @0x6BB666 (qui remplit D3DXIMAGE_INFO,
//   champ Format = a1[7] calculé À PARTIR de `ddspf`), puis rejettent tout
//   FourCC ≠ DXT1/2/3/5. La VÉRITÉ du mapping masque->D3DFORMAT est donc la
//   convention D3DX9 / DDS Microsoft (le binaire ne re-code pas ce mapping).
//
// Pourquoi ce fichier : le census (Docs/TS2_IMG_FORMAT.md §0) compte 5 `GXD_RAW`
// — des DDS SANS FourCC DXT (pixelformat RGB/A masqué). asset::Texture les classe
// en PixelFormat::DdsRaw puis JETTE les masques `ddspf` (Texture.cpp:177) ; le pont
// gfx::GpuTexture::CreateFromTexture renvoie donc false (GpuTexture.cpp:87). Ce
// module récupère les masques et en déduit le D3DFORMAT exact pour rendre ces 5
// textures uploadables (backlog T3 de Docs/TS2_DEEP_TEX_NPK.md).
//
// COUCHE CPU PURE : comme tout Asset/, ce fichier NE dépend PAS de d3d9/d3dx9.
// Le format cible est exposé via l'enum DdsD3dFormat, dont les valeurs numériques
// DUPLIQUENT à l'identique les constantes D3DFORMAT de <d3d9types.h> — la couche
// Gfx caste directement `static_cast<D3DFORMAT>(uint32_t(fmt))`.
//
// Offsets DDS PROUVÉS en IDA (qmemcpy(v41, base+4, 0x7C) @0x4179B0 / 0x417F63) :
//   fichier+0   char[4] magic "DDS "
//   fichier+4   u32     dwSize        (== 124)                 v41[0]
//   fichier+8   u32     dwFlags                                v41[1]
//   fichier+12  u32     dwHeight                               v41[2]
//   fichier+16  u32     dwWidth                                v41[3]
//   fichier+20  u32     dwPitchOrLinearSize                    v41[4]
//   fichier+24  u32     dwDepth                                v41[5]
//   fichier+28  u32     dwMipMapCount                          v41[6]
//   fichier+32  u32[11] dwReserved1                            v41[7..17]
//   fichier+76  ---- DDS_PIXELFORMAT `ddspf` (32 o) ----       v41[18..25]
//     +76  u32 dwSize (== 32) | +80 dwFlags | +84 dwFourCC     (Texture.cpp:158 lit +84)
//     +88  u32 dwRGBBitCount | +92 dwRBitMask | +96 dwGBitMask
//     +100 u32 dwBBitMask     | +104 dwABitMask
//   fichier+108 u32 dwCaps ... (fin d'en-tête à +128)
#pragma once
#include <cstddef>
#include <cstdint>

namespace ts2::asset {

// --- Drapeaux `ddspf.dwFlags` (DDPF_*, DDS Microsoft) -----------------------
constexpr uint32_t kDdpfAlphaPixels = 0x00000001u; // DDPF_ALPHAPIXELS : dwABitMask valide
constexpr uint32_t kDdpfAlpha       = 0x00000002u; // DDPF_ALPHA       : alpha seul (A8)
constexpr uint32_t kDdpfFourCC      = 0x00000004u; // DDPF_FOURCC      : dwFourCC valide
constexpr uint32_t kDdpfPaletteIdx8 = 0x00000020u; // DDPF_PALETTEINDEXED8
constexpr uint32_t kDdpfRgb         = 0x00000040u; // DDPF_RGB         : RGB(A) non compressé
constexpr uint32_t kDdpfYuv         = 0x00000200u; // DDPF_YUV
constexpr uint32_t kDdpfLuminance   = 0x00020000u; // DDPF_LUMINANCE   : L/LA
constexpr uint32_t kDdpfBumpDuDv    = 0x00080000u; // DDPF_BUMPDUDV    : U/V signé

// --- Offsets fichier (voir tableau ci-dessus) -------------------------------
constexpr size_t kDdsMagicSize        = 4;    // "DDS "
constexpr size_t kDdsHeaderSize       = 124;  // DDS_HEADER (dwSize)
constexpr size_t kDdsFullHeaderSize   = 128;  // magic + header : début des pixels bruts
constexpr size_t kDdsPixelFormatOff   = 76;   // `ddspf` dans le fichier
constexpr size_t kDdsHeightOff        = 12;   // dwHeight
constexpr size_t kDdsWidthOff         = 16;   // dwWidth
constexpr size_t kDdsMipCountOff      = 28;   // dwMipMapCount

// Magic "DDS " en u32 little-endian (0x20534444).
constexpr uint32_t kDdsMagicValue = 0x20534444u;

// --- Valeurs numériques D3DFORMAT (<d3d9types.h>), dupliquées pour garder ----
// Asset/ SANS dépendance Direct3D. Gfx : static_cast<D3DFORMAT>(uint32_t(fmt)).
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

// --- `ddspf` reflété tel quel (8 dwords à fichier+76) -----------------------
struct DdsPixelFormat {
    uint32_t size        = 0;  // dwSize        (fichier+76, == 32)
    uint32_t flags       = 0;  // dwFlags       (fichier+80, DDPF_*)
    uint32_t fourCC      = 0;  // dwFourCC      (fichier+84)
    uint32_t rgbBitCount = 0;  // dwRGBBitCount (fichier+88)
    uint32_t rMask       = 0;  // dwRBitMask    (fichier+92)
    uint32_t gMask       = 0;  // dwGBitMask    (fichier+96)
    uint32_t bMask       = 0;  // dwBBitMask    (fichier+100)
    uint32_t aMask       = 0;  // dwABitMask    (fichier+104)

    bool IsFourCC()    const { return (flags & kDdpfFourCC) != 0; }
    bool HasAlpha()    const { return (flags & kDdpfAlphaPixels) != 0; }
    bool IsRgb()       const { return (flags & kDdpfRgb) != 0; }
    bool IsLuminance() const { return (flags & kDdpfLuminance) != 0; }
    bool IsAlphaOnly() const { return (flags & kDdpfAlpha) != 0; }
    bool IsBumpDuDv()  const { return (flags & kDdpfBumpDuDv) != 0; }
};

// --- Surface DDS brute prête à l'upload (chemin DdsRaw) ---------------------
// Regroupe tout ce dont la couche Gfx a besoin pour un CreateTexture + LockRect
// d'un DDS non-DXT : le D3DFORMAT déduit, les dimensions (dwWidth/dwHeight de
// l'en-tête), le nombre de niveaux et l'offset des pixels bruts (128).
struct DdsRawSurface {
    DdsD3dFormat format       = DdsD3dFormat::Unknown;
    uint32_t     width        = 0;
    uint32_t     height       = 0;
    uint32_t     mipCount     = 0;   // dwMipMapCount (0 => 1 niveau implicite)
    uint32_t     bitsPerPixel = 0;   // ddspf.dwRGBBitCount
    size_t       dataOffset   = kDdsFullHeaderSize; // pixels bruts après l'en-tête 128 o

    // Pas (octets) d'une rangée du niveau 0 pour un format packé linéaire.
    uint32_t RowPitch() const { return width * (bitsPerPixel / 8u); }
    // Taille (octets) du niveau 0 seul (sans mips).
    size_t   Level0Bytes() const { return size_t(RowPitch()) * height; }
    bool     Valid() const {
        return format != DdsD3dFormat::Unknown && width != 0 && height != 0;
    }
};

// Lit le bloc `ddspf` (fichier+76, 32 o) d'un DDS complet en mémoire.
// Valide "DDS " @0 et dwSize@+4 == 124. Renvoie false si trop court / non-DDS.
bool ReadDdsPixelFormat(const uint8_t* ddsFile, size_t size, DdsPixelFormat& out);

// Mappe un `ddspf` NON-DXT (RGB / luminance / alpha / bump masqué) vers un
// D3DFORMAT, selon la convention D3DX9 / DDS Microsoft (cf. en-tête de fichier).
//   - DDPF_FOURCC avec un FourCC "4 lettres" (ex. "DXT1") -> Unknown : ces DDS
//     passent par le chemin blocs (PixelFormat::DxtBlocks de Texture.cpp), PAS ici.
//   - DDPF_FOURCC avec une petite valeur (<= 0xFF) -> D3DFORMAT direct : convention
//     D3DX pour les formats étendus (float, A16B16G16R16, ...) rangés tels quels
//     dans le champ FourCC.
//   - Sinon : match exact (bitCount + masques R/G/B/A) contre la table connue.
// Renvoie DdsD3dFormat::Unknown si aucun format n'est reconnu.
DdsD3dFormat MapDdsPixelFormatToD3dFormat(const DdsPixelFormat& pf);

// Parse complet : lit `ddspf` + dwWidth/dwHeight/dwMipMapCount et déduit le
// format, prêt pour un upload GPU du chemin DdsRaw. Renvoie false si le fichier
// n'est pas un DDS non-DXT dont le format est reconnu (out.Valid() est alors false).
bool ParseDdsRawSurface(const uint8_t* ddsFile, size_t size, DdsRawSurface& out);

// Octets par pixel d'un DdsD3dFormat packé linéaire (0 si inconnu / compressé).
uint32_t DdsD3dFormatBytesPerPixel(DdsD3dFormat fmt);

} // namespace ts2::asset

// Gfx/GpuTexture.cpp — implémentation du pont asset -> IDirect3DTexture9 (moteur GXD).
// Ancres : Tex_LoadFromFile 0x6A9910, cTexture_LoadFromImgFile 0x457A20, Tex_ReadFromMemory 0x417D20.
#include "Gfx/GpuTexture.h"
#include "Gfx/Renderer.h"
#include "Core/Log.h"

#include <cstddef>
#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

// Lecture u32 little-endian (les payloads GXD sont natifs x86 LE).
inline uint32_t Rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Copie fidèle d'Util_NextPow2 0x457220 : plus petite puissance de 2 >= v (>= 1).
inline UINT NextPow2(uint32_t v) {
    UINT r = 1;
    while (r < v) r *= 2;
    return r;
}

// FourCC S3TC -> D3DFORMAT. Astuce : D3DFMT_DXTn == MAKEFOURCC('D','X','T','n') == la
// valeur u32 du FourCC ; le cast est donc exact. D3DFMT_UNKNOWN si non-DXT.
inline D3DFORMAT DxtFormat(uint32_t fcc) {
    switch (fcc) {
        case 0x31545844u: // "DXT1"
        case 0x32545844u: // "DXT2"
        case 0x33545844u: // "DXT3"
        case 0x34545844u: // "DXT4"
        case 0x35545844u: // "DXT5"
            return static_cast<D3DFORMAT>(fcc);
        default:
            return D3DFMT_UNKNOWN;
    }
}

// Octets par bloc 4x4 : DXT1 = 8, DXT2..5 = 16 (identique à Texture.cpp::DxtBlockBytes).
inline uint32_t DxtBlockBytes(uint32_t fcc) { return (fcc == 0x31545844u) ? 8u : 16u; }

} // namespace

// ---------------------------------------------------------------------------------------
// Cycle de vie
// ---------------------------------------------------------------------------------------
GpuTexture::~GpuTexture() { Release(); }

GpuTexture::GpuTexture(GpuTexture&& o) noexcept
    : tex_(o.tex_), width_(o.width_), height_(o.height_), mips_(o.mips_), format_(o.format_) {
    o.tex_    = nullptr;
    o.width_  = o.height_ = o.mips_ = 0;
    o.format_ = D3DFMT_UNKNOWN;
}

GpuTexture& GpuTexture::operator=(GpuTexture&& o) noexcept {
    if (this != &o) {
        Release();
        tex_    = o.tex_;    width_ = o.width_;  height_ = o.height_;
        mips_   = o.mips_;   format_ = o.format_;
        o.tex_    = nullptr;
        o.width_  = o.height_ = o.mips_ = 0;
        o.format_ = D3DFMT_UNKNOWN;
    }
    return *this;
}

void GpuTexture::Release() {
    if (tex_) { tex_->Release(); tex_ = nullptr; }
    width_ = height_ = mips_ = 0;
    format_ = D3DFMT_UNKNOWN;
}

// ---------------------------------------------------------------------------------------
// Pont asset::Texture -> GPU (dispatch par PixelFormat)
// ---------------------------------------------------------------------------------------
bool GpuTexture::CreateFromTexture(IDirect3DDevice9* dev, const asset::Texture& s) {
    if (!dev) { TS2_ERR("GpuTexture : device nul"); return false; }
    switch (s.format) {
        case asset::PixelFormat::DxtBlocks: return UploadDxtBlocks(dev, s);
        case asset::PixelFormat::RGBA8:     return UploadRgba8(dev, s);
        case asset::PixelFormat::DdsRaw:
            // DDS non-DXT : les masques de pixelformat ne sont pas conservés par
            // asset::Texture -> impossible de reconstruire le format GPU de façon fiable.
            TS2_ERR("GpuTexture : DDS non-DXT (DdsRaw) non supporte par le pont");
            return false;
        default:
            TS2_ERR("GpuTexture : asset non decode (format=%d)", static_cast<int>(s.format));
            return false;
    }
}

bool GpuTexture::CreateFromTexture(const Renderer& r, const asset::Texture& s) {
    return CreateFromTexture(r.Device(), s);
}

// Upload manuel des blocs S3TC (l'en-tête DDS a déjà été retiré par asset::Texture).
// La chaîne de mips et le calcul de taille par niveau reproduisent Texture.cpp exactement,
// donc srcOff parcourt précisément pixels.size().
bool GpuTexture::UploadDxtBlocks(IDirect3DDevice9* dev, const asset::Texture& s) {
    const D3DFORMAT fmt = DxtFormat(s.fourCCValue);
    if (fmt == D3DFMT_UNKNOWN) {
        TS2_ERR("GpuTexture : FourCC DXT inconnu 0x%08X", s.fourCCValue);
        return false;
    }
    if (s.width == 0 || s.height == 0) { TS2_ERR("GpuTexture : dimensions DXT nulles"); return false; }

    const uint32_t blockBytes = DxtBlockBytes(s.fourCCValue);
    const uint32_t levels     = s.mipCount > 0 ? s.mipCount : 1; // 0 => 1 niveau implicite

    IDirect3DTexture9* tex = nullptr;
    const HRESULT hr = dev->CreateTexture(s.width, s.height, levels, 0 /*Usage*/, fmt,
                                          D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr) || !tex) {
        TS2_ERR("GpuTexture : CreateTexture(DXT) echouee hr=0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    size_t   srcOff = 0;
    uint32_t w = s.width, h = s.height;
    for (uint32_t lvl = 0; lvl < levels; ++lvl) {
        uint32_t bw = (w + 3) / 4; if (bw < 1) bw = 1;
        uint32_t bh = (h + 3) / 4; if (bh < 1) bh = 1;
        const size_t rowBytes   = size_t(bw) * blockBytes; // 1 rangée de blocs
        const size_t levelBytes = rowBytes * bh;

        if (srcOff + levelBytes > s.pixels.size()) {
            TS2_ERR("GpuTexture : blocs DXT tronques au niveau %u", lvl);
            tex->Release();
            return false;
        }

        D3DLOCKED_RECT lr{};
        if (FAILED(tex->LockRect(lvl, &lr, nullptr, 0))) {
            TS2_ERR("GpuTexture : LockRect(DXT) niveau %u echoue", lvl);
            tex->Release();
            return false;
        }
        const uint8_t* src = s.pixels.data() + srcOff;
        uint8_t*       dst = static_cast<uint8_t*>(lr.pBits);
        // Recopie rangée de blocs par rangée de blocs (Pitch verrouille peut etre pade).
        for (uint32_t r = 0; r < bh; ++r)
            std::memcpy(dst + size_t(r) * lr.Pitch, src + size_t(r) * rowBytes, rowBytes);
        tex->UnlockRect(lvl);

        srcOff += levelBytes;
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }

    Release();
    tex_ = tex; width_ = s.width; height_ = s.height; mips_ = levels; format_ = fmt;
    return true;
}

// Upload d'un TGA décodé (RGBA8 top-down) en A8R8G8B8. En mémoire, A8R8G8B8 little-endian
// s'écrit B,G,R,A -> on re-permute depuis la source R,G,B,A.
bool GpuTexture::UploadRgba8(IDirect3DDevice9* dev, const asset::Texture& s) {
    if (s.width == 0 || s.height == 0) { TS2_ERR("GpuTexture : dimensions RGBA nulles"); return false; }
    const size_t need = size_t(s.width) * s.height * 4;
    if (s.pixels.size() < need) {
        TS2_ERR("GpuTexture : pixels RGBA tronques (%zu < %zu)", s.pixels.size(), need);
        return false;
    }

    IDirect3DTexture9* tex = nullptr;
    const HRESULT hr = dev->CreateTexture(s.width, s.height, 1, 0, D3DFMT_A8R8G8B8,
                                          D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr) || !tex) {
        TS2_ERR("GpuTexture : CreateTexture(A8R8G8B8) echouee hr=0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    D3DLOCKED_RECT lr{};
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) {
        TS2_ERR("GpuTexture : LockRect(A8R8G8B8) echoue");
        tex->Release();
        return false;
    }
    for (uint32_t y = 0; y < s.height; ++y) {
        const uint8_t* srow = s.pixels.data() + size_t(y) * s.width * 4;    // R,G,B,A
        uint8_t*       drow = static_cast<uint8_t*>(lr.pBits) + size_t(y) * lr.Pitch; // B,G,R,A
        for (uint32_t x = 0; x < s.width; ++x) {
            drow[x * 4 + 0] = srow[x * 4 + 2]; // B
            drow[x * 4 + 1] = srow[x * 4 + 1]; // G
            drow[x * 4 + 2] = srow[x * 4 + 0]; // R
            drow[x * 4 + 3] = srow[x * 4 + 3]; // A
        }
    }
    tex->UnlockRect(0);

    Release();
    tex_ = tex; width_ = s.width; height_ = s.height; mips_ = 1; format_ = D3DFMT_A8R8G8B8;
    return true;
}

// ---------------------------------------------------------------------------------------
// Pont .IMG (cTexture_LoadFromImgFile 0x457A20) — réplique exacte
// ex-VeryOldClient: TEXTURE_FOR_GXD::Load2 (conteneur zlib -> DXT1/3/5 -> CreateTextureFromFileInMemoryEx)
// ---------------------------------------------------------------------------------------
bool GpuTexture::CreateFromImgFile(IDirect3DDevice9* dev, const asset::ImgFile& img) {
    if (!dev) { TS2_ERR("GpuTexture : device nul"); return false; }
    if (img.Kind() != asset::ImgKind::TextureDxt) {
        TS2_ERR("GpuTexture : ImgFile kind != TextureDxt");
        return false;
    }

    const std::vector<uint8_t>& pl = img.Payload();
    // En-tête texture GXD du payload décompressé (offsets figés au désassemblage, queue
    // 0x457B5B-0x457C0A) :
    //   +0  u32 width      -> Width  = NextPow2(width)  (0 si width==0)
    //   +4  u32 height     -> Height = NextPow2(height) (0 si height==0)
    //   +8..+24            flags / nb de niveaux / type (non utilises par le loader)
    //   +28 u32 FourCC     -> Format (DXT1/DXT3/DXT5)
    //   +32 u32 imageSize  -> SrcDataSize
    //   +36 ...            fichier image (SrcData) de imageSize octets
    if (pl.size() < 36) { TS2_ERR("GpuTexture : payload IMG < 36 o"); return false; }

    const uint32_t w0     = Rd32(pl.data() + 0);
    const uint32_t h0     = Rd32(pl.data() + 4);
    const uint32_t fourcc = Rd32(pl.data() + 28);
    const uint32_t imgSz  = Rd32(pl.data() + 32);

    if (size_t(36) + imgSz > pl.size()) {
        TS2_ERR("GpuTexture : bloc image IMG hors payload (size=%u, payload=%zu)", imgSz, pl.size());
        return false;
    }

    // NextPow2 seulement si dw >= 1, sinon 0 — comportement exact du binaire (cmp/jge).
    const UINT width  = (w0 >= 1) ? NextPow2(w0) : 0;
    const UINT height = (h0 >= 1) ? NextPow2(h0) : 0;

    IDirect3DTexture9* tex = nullptr;
    const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev,
        pl.data() + 36,                    // pSrcData    (fichier image a +36)
        imgSz,                             // SrcDataSize (u32 a +32)
        width,                             // Width       (NextPow2 de dw0)
        height,                            // Height      (NextPow2 de dw1)
        1,                                 // MipLevels
        0,                                 // Usage
        static_cast<D3DFORMAT>(fourcc),    // Format      (FourCC a +28)
        D3DPOOL_MANAGED,                   // Pool = 1
        D3DX_FILTER_NONE,                  // Filter = 1
        D3DX_FILTER_NONE,                  // MipFilter = 1
        0,                                 // ColorKey
        nullptr,                           // pSrcInfo
        nullptr,                           // pPalette
        &tex);
    if (FAILED(hr) || !tex) {
        TS2_ERR("GpuTexture : D3DXCreateTextureFromFileInMemoryEx (IMG) hr=0x%08lX",
                static_cast<unsigned long>(hr));
        return false;
    }

    Release();
    tex_ = tex;
    // BUG CORRIGE (mission "GRILLE SERVERSELECT", 2026-07-14, preuve EA par EA) :
    // Width()/Height() DOIVENT rester les dimensions LOGIQUES (w0/h0, l'en-tete +0/+4 du
    // payload IMG), PAS les dimensions physiques de la surface D3D9 (qui sont arrondies a
    // la puissance de 2 par Util_NextPow2_GXD/NextPow2 -- ex. 737x755 -> 1024x1024).
    // Preuve dans le binaire (Tex_LoadCompressedDDS 0x6A2E80, EA 0x6A2FFE) :
    // `qmemcpy(this+1, v14, 0x1Cu)` copie l'en-tete IMG BRUT (dw0=w0, dw1=h0, ...) dans le
    // struct sprite AVANT tout appel a Util_NextPow2_GXD ; Sprite2D_GetWidth 0x4D6CD0 et
    // Sprite2D_GetHeight 0x4D6D20 lisent ENSUITE directement ces champs coles (offsets
    // +108/+112 du sprite = +4/+8 du struct texture = w0/h0 du payload) -- le NextPow2 ne
    // sert QUE de parametre a D3DXCreateTextureFromFileInMemoryEx (allocation de la
    // surface), jamais reecrit dans les champs consultes par les getters. Utiliser les
    // dimensions physiques ici causait un mauvais centrage (ex. panneau ServerSelect
    // 001_01786.IMG : w0=737/h0=755 reel vs 1024x1024 physique -> panneau decale de 143/134
    // px, texte et barre de charge superposes au bandeau titre -- constate a l'ecran,
    // corrige ici a la source pour TOUS les consommateurs de GpuTexture::Width()/Height()).
    D3DSURFACE_DESC d{};
    if (SUCCEEDED(tex_->GetLevelDesc(0, &d))) {
        format_ = d.Format;
    } else {
        format_ = static_cast<D3DFORMAT>(fourcc);
    }
    width_  = (w0 >= 1) ? static_cast<uint32_t>(w0) : width;
    height_ = (h0 >= 1) ? static_cast<uint32_t>(h0) : height;
    mips_ = tex_->GetLevelCount();
    return true;
}

bool GpuTexture::CreateFromImgFile(const Renderer& r, const asset::ImgFile& img) {
    return CreateFromImgFile(r.Device(), img);
}

// ---------------------------------------------------------------------------------------
// Chargeur générique D3DX depuis un fichier image complet en mémoire (Tex_LoadFromFile 0x6A9910)
// ex-VeryOldClient: TEXTURE_FOR_GXD::Load (D3DXGetImageInfo + CreateTextureFromFileInMemoryEx, DXT1/3/5)
// ---------------------------------------------------------------------------------------
bool GpuTexture::CreateFromImageFileInMemory(IDirect3DDevice9* dev, const void* data, uint32_t size) {
    if (!dev || !data || size == 0) { TS2_ERR("GpuTexture : arguments invalides (image memoire)"); return false; }

    // Comme Tex_LoadFromFile : on lit d'abord l'info du conteneur (dimensions/mips/format).
    D3DXIMAGE_INFO info{};
    if (FAILED(D3DXGetImageInfoFromFileInMemory(data, size, &info))) {
        TS2_ERR("GpuTexture : image memoire non reconnue par D3DX");
        return false;
    }
    // Note : l'original 0x6A9910 rejette tout sauf DXT1/DXT3/DXT5 ; on reste permissif ici.

    IDirect3DTexture9* tex = nullptr;
    const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev, data, size,
        info.Width, info.Height, info.MipLevels,
        0,                       // Usage
        info.Format,
        D3DPOOL_MANAGED,         // Pool = 1
        D3DX_FILTER_NONE,        // Filter = 1
        D3DX_FILTER_NONE,        // MipFilter = 1
        0,                       // ColorKey
        &info,                   // pSrcInfo
        nullptr,                 // pPalette
        &tex);
    if (FAILED(hr) || !tex) {
        TS2_ERR("GpuTexture : D3DXCreateTextureFromFileInMemoryEx (memoire) hr=0x%08lX",
                static_cast<unsigned long>(hr));
        return false;
    }

    Release();
    tex_ = tex; width_ = info.Width; height_ = info.Height; mips_ = info.MipLevels; format_ = info.Format;
    return true;
}

} // namespace ts2::gfx

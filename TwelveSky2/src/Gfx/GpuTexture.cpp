// Gfx/GpuTexture.cpp — implementation of the asset -> IDirect3DTexture9 bridge (GXD engine).
// Anchors: Tex_LoadFromFile 0x6A9910, cTexture_LoadFromImgFile 0x457A20, Tex_ReadFromMemory 0x417D20.
#include "Gfx/GpuTexture.h"
#include "Gfx/Renderer.h"
#include "Core/Log.h"

#include <cstddef>
#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

// Little-endian u32 read (GXD payloads are native x86 LE).
inline uint32_t Rd32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Faithful copy of Util_NextPow2 0x457220: smallest power of 2 >= v (>= 1).
inline UINT NextPow2(uint32_t v) {
    UINT r = 1;
    while (r < v) r *= 2;
    return r;
}

// S3TC FourCC -> D3DFORMAT. Trick: D3DFMT_DXTn == MAKEFOURCC('D','X','T','n') == the
// FourCC's u32 value; the cast is therefore exact. D3DFMT_UNKNOWN if not DXT.
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

// Bytes per 4x4 block: DXT1 = 8, DXT2..5 = 16 (same as Texture.cpp::DxtBlockBytes).
inline uint32_t DxtBlockBytes(uint32_t fcc) { return (fcc == 0x31545844u) ? 8u : 16u; }

} // namespace

// ---------------------------------------------------------------------------------------
// Lifecycle
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
// asset::Texture -> GPU bridge (dispatch by PixelFormat)
// ---------------------------------------------------------------------------------------
bool GpuTexture::CreateFromTexture(IDirect3DDevice9* dev, const asset::Texture& s) {
    if (!dev) { TS2_ERR("GpuTexture : device nul"); return false; }
    switch (s.format) {
        case asset::PixelFormat::DxtBlocks: return UploadDxtBlocks(dev, s);
        case asset::PixelFormat::RGBA8:     return UploadRgba8(dev, s);
        case asset::PixelFormat::DdsRaw:
            // Non-DXT DDS: pixelformat masks are not preserved by asset::Texture ->
            // impossible to reliably reconstruct the GPU format.
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

// Manual S3TC block upload (the DDS header has already been stripped by asset::Texture).
// The mip chain and per-level size computation replicate Texture.cpp exactly,
// so srcOff walks precisely through pixels.size().
bool GpuTexture::UploadDxtBlocks(IDirect3DDevice9* dev, const asset::Texture& s) {
    const D3DFORMAT fmt = DxtFormat(s.fourCCValue);
    if (fmt == D3DFMT_UNKNOWN) {
        TS2_ERR("GpuTexture : FourCC DXT inconnu 0x%08X", s.fourCCValue);
        return false;
    }
    if (s.width == 0 || s.height == 0) { TS2_ERR("GpuTexture : dimensions DXT nulles"); return false; }

    const uint32_t blockBytes = DxtBlockBytes(s.fourCCValue);
    const uint32_t levels     = s.mipCount > 0 ? s.mipCount : 1; // 0 => 1 implicit level

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
        const size_t rowBytes   = size_t(bw) * blockBytes; // 1 row of blocks
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
        // Copy block row by block row (the locked Pitch may be padded).
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

// Upload of a decoded TGA (RGBA8 top-down) to A8R8G8B8. In memory, little-endian
// A8R8G8B8 is stored B,G,R,A -> re-permute from the source R,G,B,A.
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
// .IMG bridge (cTexture_LoadFromImgFile 0x457A20) — exact replica
// ex-VeryOldClient: TEXTURE_FOR_GXD::Load2 (zlib container -> DXT1/3/5 -> CreateTextureFromFileInMemoryEx)
// ---------------------------------------------------------------------------------------
bool GpuTexture::CreateFromImgFile(IDirect3DDevice9* dev, const asset::ImgFile& img) {
    if (!dev) { TS2_ERR("GpuTexture : device nul"); return false; }
    if (img.Kind() != asset::ImgKind::TextureDxt) {
        TS2_ERR("GpuTexture : ImgFile kind != TextureDxt");
        return false;
    }

    const std::vector<uint8_t>& pl = img.Payload();
    // GXD texture header of the decompressed payload (offsets fixed at disassembly, tail
    // 0x457B5B-0x457C0A):
    //   +0  u32 width      -> Width  = NextPow2(width)  (0 if width==0)
    //   +4  u32 height     -> Height = NextPow2(height) (0 if height==0)
    //   +8..+24            flags / level count / type (unused by the loader)
    //   +28 u32 FourCC     -> Format (DXT1/DXT3/DXT5)
    //   +32 u32 imageSize  -> SrcDataSize
    //   +36 ...            image file (SrcData) of imageSize bytes
    if (pl.size() < 36) { TS2_ERR("GpuTexture : payload IMG < 36 o"); return false; }

    const uint32_t w0     = Rd32(pl.data() + 0);
    const uint32_t h0     = Rd32(pl.data() + 4);
    const uint32_t fourcc = Rd32(pl.data() + 28);
    const uint32_t imgSz  = Rd32(pl.data() + 32);

    if (size_t(36) + imgSz > pl.size()) {
        TS2_ERR("GpuTexture : bloc image IMG hors payload (size=%u, payload=%zu)", imgSz, pl.size());
        return false;
    }

    // NextPow2 only if dw >= 1, else 0 — exact behavior of the binary (cmp/jge).
    const UINT width  = (w0 >= 1) ? NextPow2(w0) : 0;
    const UINT height = (h0 >= 1) ? NextPow2(h0) : 0;

    IDirect3DTexture9* tex = nullptr;
    const HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev,
        pl.data() + 36,                    // pSrcData    (image file at +36)
        imgSz,                             // SrcDataSize (u32 at +32)
        width,                             // Width       (NextPow2 of dw0)
        height,                            // Height      (NextPow2 of dw1)
        1,                                 // MipLevels
        0,                                 // Usage
        static_cast<D3DFORMAT>(fourcc),    // Format      (FourCC at +28)
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
    // BUG FIXED ("GRILLE SERVERSELECT" mission, 2026-07-14, EA-by-EA proof):
    // Width()/Height() MUST stay the LOGICAL dimensions (w0/h0, the +0/+4 header of the
    // IMG payload), NOT the physical D3D9 surface dimensions (rounded up to the next
    // power of 2 by Util_NextPow2_GXD/NextPow2 -- e.g. 737x755 -> 1024x1024).
    // Proof in the binary (Tex_LoadCompressedDDS 0x6A2E80, EA 0x6A2FFE):
    // `qmemcpy(this+1, v14, 0x1Cu)` copies the RAW IMG header (dw0=w0, dw1=h0, ...) into the
    // sprite struct BEFORE any call to Util_NextPow2_GXD ; Sprite2D_GetWidth 0x4D6CD0 and
    // Sprite2D_GetHeight 0x4D6D20 subsequently read these glued fields directly (offsets
    // +108/+112 of the sprite = +4/+8 of the texture struct = w0/h0 of the payload) -- the
    // NextPow2 result is ONLY used as a parameter to D3DXCreateTextureFromFileInMemoryEx
    // (surface allocation), never written back into the fields read by the getters. Using
    // physical dimensions here caused bad centering (e.g. ServerSelect panel
    // 001_01786.IMG: w0=737/h0=755 real vs 1024x1024 physical -> panel offset by 143/134
    // px, text and loading bar overlapping the title banner -- observed on screen,
    // fixed here at the source for ALL consumers of GpuTexture::Width()/Height()).
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
// Generic D3DX loader from a whole image file in memory (Tex_LoadFromFile 0x6A9910)
// ex-VeryOldClient: TEXTURE_FOR_GXD::Load (D3DXGetImageInfo + CreateTextureFromFileInMemoryEx, DXT1/3/5)
// ---------------------------------------------------------------------------------------
bool GpuTexture::CreateFromImageFileInMemory(IDirect3DDevice9* dev, const void* data, uint32_t size) {
    if (!dev || !data || size == 0) { TS2_ERR("GpuTexture : arguments invalides (image memoire)"); return false; }

    // Like Tex_LoadFromFile: first read the container info (dimensions/mips/format).
    D3DXIMAGE_INFO info{};
    if (FAILED(D3DXGetImageInfoFromFileInMemory(data, size, &info))) {
        TS2_ERR("GpuTexture : image memoire non reconnue par D3DX");
        return false;
    }
    // Note: the original 0x6A9910 rejects everything except DXT1/DXT3/DXT5; we stay permissive here.

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

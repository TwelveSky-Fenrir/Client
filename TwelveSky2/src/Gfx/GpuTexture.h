// Gfx/GpuTexture.h — bridge "decoded assets" -> IDirect3DTexture9 of the GXD engine.
//
// Faithful rewrite of the TwelveSky2 client's texture loaders:
//   - Tex_LoadFromFile          0x6A9910  (DDS/.SHADOW: D3DXGetImageInfo + CreateTextureFromFileInMemoryEx)
//   - cTexture_LoadFromImgFile  0x457A20  (.IMG GXD texture: proprietary header + FourCC + image file)
//   - Tex_ReadFromMemory        0x417D20  (packed payload + DDS mips; LOD semantics reference)
// Original binary import: D3DXCreateTextureFromFileInMemoryEx @0x1960338.
// ex-VeryOldClient: TEXTURE_FOR_GXD (CTEXTURE_FOR_GXD.cpp) — Load(HANDLE) = zlib .IMG container;
//   Load2(BYTE*) = [fileDataSize][origSize][compSize][comp] -> Decompress -> DXT1/3/5 ->
//   D3DXCreateTextureFromFileInMemoryEx (D3DPOOL_MANAGED). Confirmed pipeline (texture_pipeline).
//   NB: homonymous class v1 (Core/GXD) and v2 (TW2AddIn) — same load semantics.
//
// Unlike Renderer (which only uses the Windows SDK's D3D9), THIS module is allowed to
// use the legacy D3DX layer (DirectX SDK June 2010): it is the exact bridge to the same
// API the original binary used.
#pragma once
#include <d3dx9.h>   // ID3DXSprite/ID3DXFont, D3DXCreateTextureFromFileInMemoryEx (SDK June 2010)
#include <cstdint>

#include "Asset/Texture.h"
#include "Asset/ImgFile.h"

namespace ts2::gfx {

class Renderer; // fwd decl: convenience overloads via Renderer::Device()

// RAII wrapper of an IDirect3DTexture9 created from an already-decoded asset.
// Non-copyable (owns the texture), movable.
class GpuTexture {
public:
    GpuTexture() = default;
    ~GpuTexture();

    GpuTexture(const GpuTexture&)            = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&& o) noexcept;
    GpuTexture& operator=(GpuTexture&& o) noexcept;

    // ---- Main bridge: ts2::asset::Texture (already decoded) -> GPU texture --------------
    // Manual CreateTexture + LockRect path, because asset::Texture has already stripped the
    // 128 B DDS header and only keeps the blocks/pixels:
    //   - PixelFormat::DxtBlocks : CreateTexture(D3DFMT_DXTn) + S3TC block upload per level.
    //   - PixelFormat::RGBA8     : CreateTexture(A8R8G8B8)    + swizzle R,G,B,A -> B,G,R,A.
    //   - PixelFormat::DdsRaw    : unsupported (RGB/A pixelformat not preserved) -> false.
    bool CreateFromTexture(IDirect3DDevice9* dev, const asset::Texture& src);
    bool CreateFromTexture(const Renderer& r,      const asset::Texture& src);

    // ---- .IMG bridge: exact replica of cTexture_LoadFromImgFile 0x457A20 ---------------
    // Expects an ImgFile of kind == TextureDxt; reads the GXD texture header from Payload()
    // (width@+0, height@+4, FourCC@+28, imageSize@+32, image file@+36) then calls
    // D3DXCreateTextureFromFileInMemoryEx with dimensions rounded up to the next power of 2
    // (Util_NextPow2_GXD, for the D3D9 surface allocation ONLY). Width()/Height() after this
    // call stay the LOGICAL dimensions (width@+0/height@+4), NOT the rounded surface --
    // see the detailed comment in the .cpp (proof via Tex_LoadCompressedDDS
    // 0x6A2E80 / Sprite2D_GetWidth 0x4D6CD0 / Sprite2D_GetHeight 0x4D6D20).
    bool CreateFromImgFile(IDirect3DDevice9* dev, const asset::ImgFile& img);
    bool CreateFromImgFile(const Renderer& r,      const asset::ImgFile& img);

    // ---- Generic D3DX loader, modeled on Tex_LoadFromFile 0x6A9910 ---------------------
    // `data`/`size` = COMPLETE image file in memory (DDS/.SHADOW/TGA/PNG...). D3DX
    // recognizes the container alone. (The original restricts to FourCC DXT1/3/5; permissive here.)
    bool CreateFromImageFileInMemory(IDirect3DDevice9* dev, const void* data, uint32_t size);

    // ---- Access --------------------------------------------------------------------------
    IDirect3DTexture9* Handle()    const { return tex_; }
    bool               Valid()     const { return tex_ != nullptr; }
    // LOGICAL dimensions (real sprite content), NOT the physical D3D9 surface rounded up to
    // the next power of 2 for CreateFromImgFile (see comment above) -- faithful to
    // Sprite2D_GetWidth/GetHeight 0x4D6CD0/0x4D6D20 of the original.
    uint32_t           Width()     const { return width_; }
    uint32_t           Height()    const { return height_; }
    uint32_t           MipLevels() const { return mips_; }
    D3DFORMAT          Format()    const { return format_; }

    void Release();

private:
    bool UploadDxtBlocks(IDirect3DDevice9* dev, const asset::Texture& src); // PixelFormat::DxtBlocks
    bool UploadRgba8(IDirect3DDevice9* dev,     const asset::Texture& src); // PixelFormat::RGBA8

    IDirect3DTexture9* tex_    = nullptr;
    uint32_t           width_  = 0;
    uint32_t           height_ = 0;
    uint32_t           mips_   = 0;
    D3DFORMAT          format_ = D3DFMT_UNKNOWN;
};

} // namespace ts2::gfx

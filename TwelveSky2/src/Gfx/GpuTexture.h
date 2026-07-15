// Gfx/GpuTexture.h — pont "assets décodés" -> IDirect3DTexture9 du moteur GXD.
//
// Réécriture fidèle des chargeurs de textures du client TwelveSky2 :
//   - Tex_LoadFromFile          0x6A9910  (DDS/.SHADOW : D3DXGetImageInfo + CreateTextureFromFileInMemoryEx)
//   - cTexture_LoadFromImgFile  0x457A20  (.IMG texture GXD : en-tête propriétaire + FourCC + fichier image)
//   - Tex_ReadFromMemory        0x417D20  (payload packé + mips DDS ; référence de sémantique LOD)
// Import binaire d'origine : D3DXCreateTextureFromFileInMemoryEx @0x1960338.
//
// Contrairement au Renderer (qui n'utilise que le D3D9 du Windows SDK), CE module a le
// droit d'utiliser la couche D3DX legacy (DirectX SDK June 2010) : c'est le pont exact
// vers les mêmes API que le binaire d'origine.
#pragma once
#include <d3dx9.h>   // ID3DXSprite/ID3DXFont, D3DXCreateTextureFromFileInMemoryEx (SDK June 2010)
#include <cstdint>

#include "Asset/Texture.h"
#include "Asset/ImgFile.h"

namespace ts2::gfx {

class Renderer; // décl. avant : surcharges pratiques via Renderer::Device()

// Enveloppe RAII d'une IDirect3DTexture9 créée à partir d'un asset déjà décodé.
// Non-copiable (possède la texture), déplaçable.
class GpuTexture {
public:
    GpuTexture() = default;
    ~GpuTexture();

    GpuTexture(const GpuTexture&)            = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&& o) noexcept;
    GpuTexture& operator=(GpuTexture&& o) noexcept;

    // ---- Pont principal : ts2::asset::Texture (déjà décodée) -> texture GPU -------------
    // Chemin manuel CreateTexture + LockRect, car asset::Texture a déjà retiré l'en-tête
    // DDS 128 o et ne conserve que les blocs/pixels :
    //   - PixelFormat::DxtBlocks : CreateTexture(D3DFMT_DXTn) + upload des blocs S3TC par niveau.
    //   - PixelFormat::RGBA8     : CreateTexture(A8R8G8B8)    + swizzle R,G,B,A -> B,G,R,A.
    //   - PixelFormat::DdsRaw    : non supporté (pixelformat RGB/A non préservé) -> false.
    bool CreateFromTexture(IDirect3DDevice9* dev, const asset::Texture& src);
    bool CreateFromTexture(const Renderer& r,      const asset::Texture& src);

    // ---- Pont .IMG : réplique exacte de cTexture_LoadFromImgFile 0x457A20 ---------------
    // Attend un ImgFile de kind == TextureDxt ; lit l'en-tête texture GXD dans Payload()
    // (width@+0, height@+4, FourCC@+28, tailleImage@+32, fichier image@+36) puis appelle
    // D3DXCreateTextureFromFileInMemoryEx avec dimensions arrondies à la puissance de 2
    // (Util_NextPow2_GXD, pour l'allocation de la surface D3D9 SEULEMENT). Width()/Height()
    // après cet appel restent les dimensions LOGIQUES (width@+0/height@+4), PAS la surface
    // arrondie -- cf. commentaire détaillé dans le .cpp (preuve Tex_LoadCompressedDDS
    // 0x6A2E80 / Sprite2D_GetWidth 0x4D6CD0 / Sprite2D_GetHeight 0x4D6D20).
    bool CreateFromImgFile(IDirect3DDevice9* dev, const asset::ImgFile& img);
    bool CreateFromImgFile(const Renderer& r,      const asset::ImgFile& img);

    // ---- Chargeur générique D3DX, calqué sur Tex_LoadFromFile 0x6A9910 -----------------
    // `data`/`size` = fichier image COMPLET en mémoire (DDS/.SHADOW/TGA/PNG...). D3DX
    // reconnaît le conteneur seul. (L'original restreint aux FourCC DXT1/3/5 ; ici permissif.)
    bool CreateFromImageFileInMemory(IDirect3DDevice9* dev, const void* data, uint32_t size);

    // ---- Accès -------------------------------------------------------------------------
    IDirect3DTexture9* Handle()    const { return tex_; }
    bool               Valid()     const { return tex_ != nullptr; }
    // Dimensions LOGIQUES (contenu réel du sprite), PAS la surface D3D9 physique arrondie à
    // la puissance de 2 pour CreateFromImgFile (cf. commentaire ci-dessus) -- fidèle à
    // Sprite2D_GetWidth/GetHeight 0x4D6CD0/0x4D6D20 de l'original.
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

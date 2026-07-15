// Gfx/IconTextureCache.cpp — voir Gfx/IconTextureCache.h pour le contexte complet.
#include "Gfx/IconTextureCache.h"
#include "Asset/ImgFile.h"

namespace ts2::gfx {

GpuTexture* IconTextureCache::GetOrLoad(IDirect3DDevice9* dev, const std::string& path) {
    if (path.empty()) return nullptr;

    auto it = entries_.find(path);
    if (it != entries_.end())
        return it->second.Valid() ? &it->second : nullptr;

    GpuTexture tex;
    if (dev) {
        asset::ImgFile img;
        if (img.Load(path))
            tex.CreateFromImgFile(dev, img);
    }
    // Met aussi en cache l'échec (texture invalide) pour ne pas ré-essayer chaque frame.
    auto res = entries_.emplace(path, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

} // namespace ts2::gfx

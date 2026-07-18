// Gfx/IconTextureCache.h — SHARED GPU cache of .IMG icons, keyed by file path.
//
// PROBLEM fixed (mission "texture/model cache memory audit", 2026-07-14):
// UI/InventoryWindow.h, UI/WarehouseWindow.h, UI/EnchantWindow.h and UI/VendorShopWindow.h each
// held their OWN `std::unordered_map<uint32_t, gfx::GpuTexture> iconCache_` (same copy-pasted
// GetIconTex pattern 4x, plus a near-identical ResolveItemIconPath in 3 of the 4 .cpp). These 4
// windows are PERSISTENT members of UI/GameWindows.h (built once at InGame scene Init(),
// Open()/Close() only show/hide). All 4 GetIconTex resolve
// "G03_GDATA\D01_GIMAGE2D\002\002_%05u.IMG" via the same ITEM_INFO::iconId field, so visiting bag
// then warehouse/shop/enchant with the same item decoded and uploaded the SAME .IMG icon as a
// separate IDirect3DTexture9 per window, never freed for the session (no eviction, no periodic
// Clear()). Over a long session touching much of the ~3100-file icon pool
// (G03_GDATA/D01_GIMAGE2D/002, cf. UI/PanelSkin.h Step 1), the x4 duplication wasted significant
// VRAM (plus redundant D3DX decode + D3DPOOL_MANAGED allocation) for zero benefit (all 4 windows
// render identical pixels).
//
// This cache replaces the 4 unordered_map<itemId,...> with ONE shared instance (owned by
// UI::GameWindows, cf. GameWindows.h/.cpp), keyed by FILE PATH rather than itemId: extra
// benefit, two different itemIds sharing the same ITEM_INFO::iconId (common for color/quality
// variants) now produce only ONE GPU texture, even within a single window (the old per-itemId
// cache didn't dedupe that case either).
//
// Scope note: Gfx/ModelCache.h (skinned .SOBJECT world models) was ALREADY a single shared
// instance (one ModelCache in Scene/WorldRenderer, keyed by file stem, used for both the player
// body and items/monsters) — nothing to fix there, cf. audit.
#pragma once
#include "Gfx/GpuTexture.h"
#include <d3d9.h>
#include <string>
#include <unordered_map>

namespace ts2::gfx {

class IconTextureCache {
public:
    IconTextureCache() = default;

    IconTextureCache(const IconTextureCache&)            = delete;
    IconTextureCache& operator=(const IconTextureCache&) = delete;

    // Lazy-load: returns the resident texture for `path` (GameData-relative path,
    // e.g. "G03_GDATA\D01_GIMAGE2D\002\002_00042.IMG"), loading/uploading it on the 1st
    // access for this EXACT path (regardless of itemId/calling window).
    // nullptr if `path` empty, `dev` null, file missing, or GPU decode/upload
    // fails — a failure is CACHED (does not retry loading the same missing path on every
    // call), same policy as the original GetIconTex functions it replaces.
    GpuTexture* GetOrLoad(IDirect3DDevice9* dev, const std::string& path);

    // Purge (immediately releases all resident GPU textures).
    void Clear() { entries_.clear(); }

    size_t Resident() const { return entries_.size(); }

private:
    std::unordered_map<std::string, GpuTexture> entries_;
};

} // namespace ts2::gfx

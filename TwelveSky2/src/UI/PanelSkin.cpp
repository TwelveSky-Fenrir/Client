// UI/PanelSkin.cpp — implementation (cf. UI/PanelSkin.h for the slot -> file
// derivation, proven by disassembly).
#include "UI/PanelSkin.h"
#include "Asset/ImgFile.h"
#include "Core/Log.h"

#include <cstdio>

namespace ts2::ui {
namespace {

// Mirror of g_UseTRVariant 0x1669190 — initialized to 0 by WinMain @0x4609FB.
bool s_useTRVariant = false;

} // namespace

void SetUseTRVariant(bool on) { s_useTRVariant = on; }

// Sprite2D_BuildPath 0x4D6900 case 1: exact replica of the two branches.
//   g_UseTRVariant == 1 -> "G03_GDATA\D01_GIMAGE2D\001\TR\001_%05d.IMG"  @0x4d6928
//   otherwise           -> "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG"     @0x4d6945
// The file number equals (slot index) + 1 (argument `a3 + 1`).
std::string Cat1SlotPath(int slot) {
    if (slot < 0) return std::string();
    char buf[80];
    if (s_useTRVariant) // `cmp g_UseTRVariant, 1 / jnz` @0x4d6913
        std::snprintf(buf, sizeof(buf),
                      "G03_GDATA\\D01_GIMAGE2D\\001\\TR\\001_%05d.IMG", slot + 1);
    else
        std::snprintf(buf, sizeof(buf),
                      "G03_GDATA\\D01_GIMAGE2D\\001\\001_%05d.IMG", slot + 1);
    return std::string(buf);
}

void PanelSkin::EnsureLoaded(const UiContext& ctx) const {
    if (tried_) return;
    tried_ = true; // a single attempt, success or failure (no retry per frame)

    // Lazy path resolution for the `Cat1Slot` form: it MUST happen here and not in
    // the constructor, because PanelSkin instances are `static const` built before
    // App::Init has set the TR variant (cf. SetUseTRVariant).
    if (path_.empty() && slot_ >= 0)
        path_ = Cat1SlotPath(slot_);

    if (path_.empty()) {
        TS2_WARN("PanelSkin : aucun chemin (slot=%d), repli appelant", slot_);
        return;
    }

    if (!ctx.renderer || !ctx.renderer->Device()) {
        TS2_WARN("PanelSkin : device D3D9 indisponible, repli (\"%s\")", path_.c_str());
        return;
    }

    asset::ImgFile img;
    if (!img.Load(path_.c_str())) {
        TS2_WARN("PanelSkin : chargement/decompression impossible, repli (\"%s\")", path_.c_str());
        return;
    }
    if (img.Kind() != asset::ImgKind::TextureDxt) {
        TS2_WARN("PanelSkin : \"%s\" n'est pas une texture DXT (kind=%d), repli",
                 path_.c_str(), static_cast<int>(img.Kind()));
        return;
    }
    if (!tex_.CreateFromImgFile(*ctx.renderer, img)) {
        TS2_WARN("PanelSkin : creation texture GPU echouee, repli (\"%s\")", path_.c_str());
        return;
    }
    TS2_LOG("PanelSkin : fond charge (\"%s\", %ux%u)", path_.c_str(), tex_.Width(), tex_.Height());
}

// NATURAL-SIZE blit — Sprite2D_Draw 0x4D6B20: UI_DrawSprite(this+104, x, y,
// 0,0,0,0,0) @0x4D6B72, with no scale factor at all.
bool PanelSkin::Draw(const UiContext& ctx, int x, int y) const {
    if (ctx.phase != UiPhase::Panels) return false; // sprite blit: Panels phase only

    EnsureLoaded(ctx);

    if (!tex_.Valid() || tex_.Width() == 0 || tex_.Height() == 0 ||
        !ctx.sprites || !ctx.sprites->Ready())
        return false; // unavailable: caller handles its own fallback

    ctx.sprites->DrawSpriteScaled(tex_.Handle(), nullptr, x, y, 1.0f, 1.0f,
                                  gfx::kSpriteWhite, /*compensatePos=*/true);
    return true;
}

uint32_t PanelSkin::TexW(const UiContext& ctx) const {
    EnsureLoaded(ctx);
    return tex_.Valid() ? tex_.Width() : 0u;
}

uint32_t PanelSkin::TexH(const UiContext& ctx) const {
    EnsureLoaded(ctx);
    return tex_.Valid() ? tex_.Height() : 0u;
}

// STRETCHED blit — NOT faithful, kept for the 11 existing callers.
// TODO [anchor 0x4D6B20]: see the block in the UI/PanelSkin.h declaration
// (gap TT-06). Fix = migrate each window to Draw(ctx,x,y) + TexW/TexH, in its own
// .cpp — outside the scope of this front.
bool PanelSkin::Draw(const UiContext& ctx, int x, int y, int w, int h,
                     D3DCOLOR fallbackColor) const {
    if (ctx.phase != UiPhase::Panels) return false; // FillRect/sprite blit: Panels phase only
    if (w <= 0 || h <= 0) return false;

    EnsureLoaded(ctx);

    if (tex_.Valid() && tex_.Width() > 0 && tex_.Height() > 0 &&
        ctx.sprites && ctx.sprites->Ready()) {
        const float sx = static_cast<float>(w) / static_cast<float>(tex_.Width());
        const float sy = static_cast<float>(h) / static_cast<float>(tex_.Height());
        // compensatePos=true: the displayed position stays (x,y) despite the scale,
        // exactly like UiContext::FillRect (UI/UIManager.cpp).
        ctx.sprites->DrawSpriteScaled(tex_.Handle(), nullptr, x, y, sx, sy,
                                      gfx::kSpriteWhite, /*compensatePos=*/true);
        return true;
    }

    // Fallback: plain colored rectangle. Never blocks rendering.
    ctx.FillRect(x, y, w, h, fallbackColor);
    return false;
}

} // namespace ts2::ui

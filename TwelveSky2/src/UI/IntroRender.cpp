// UI/IntroRender.cpp — Intro rendering implementation.
// Ground truth = Scene_IntroRender 0x518880 (see UI/IntroRender.h for EA/formula detail).
#include "UI/IntroRender.h"
#include "Asset/ImgFile.h" // asset::ImgFile (.IMG loader, real logo)
#include "Core/Log.h"
#include <cstdio>

namespace ts2::ui {

namespace {
// Re-verified 2026-07-15 (fresh decompile of Scene_IntroRender 0x518880 + Gfx_BeginFrame
// 0x6A2280): Scene_IntroRender calls Gfx_BeginFrame(g_GfxRenderer, nullptr) (EA 0x5188c3) ->
// IDirect3DDevice9::Clear(D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER, color=g_GfxRenderer+1308, Z=1.0).
// g_GfxRenderer+1308 (0x800334) == 0 -> pure black background, guaranteed by the device
// clear (Gfx/Renderer::BeginFrame). Scene_IntroRender draws no background quad or text,
// only the current logo sprite (Sprite2D_Draw); no color constant is needed here (any
// colored fallback would be an unfaithful invention).
} // namespace

gfx::GpuTexture* IntroRender::GetLogoSprite(const UiContext& ctx, int slotIndex) {
    (void)ctx; // kept for signature compat; the real device comes from SetDevice() (see .h)
    if (!device_) return nullptr;
    auto it = logoCache_.find(slotIndex);
    if (it != logoCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    char path[80];
    // Sprite2D_BuildPath 0x4D6945: category 1 -> "001_%05d.IMG" with a3+1.
    std::snprintf(path, sizeof(path), "G03_GDATA/D01_GIMAGE2D/001/001_%05d.IMG", slotIndex + 1);
    asset::ImgFile img;
    if (img.Load(path) && tex.CreateFromImgFile(device_, img)) {
        TS2_LOG("IntroRender : logo charge (\"%s\", slot=%d, %ux%u)", path, slotIndex, tex.Width(), tex.Height());
    } else {
        // Load failure: draw NOTHING (matches Sprite2D_Draw, which draws nothing if
        // EnsureLoaded fails). NO fallback fill/label invented.
        TS2_WARN("IntroRender : logo indisponible (\"%s\", slot=%d) — rien dessine (fidele).", path, slotIndex);
    }

    auto res = logoCache_.emplace(slotIndex, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

void IntroRender::Render(const UiContext& ctx, const game::IntroState& state) {
    // Scene_IntroRender 0x518880 draws NO background quad (the device's black clear is
    // enough, see banner above) and no text. In sub-state 0 (Init/wait, kIntroWaitFrames)
    // -> NOTHING drawn (EA 0x5189E1 `if (*(this+1))`).
    if (state.subState == 0)
        return;

    // EA 0x518A0B-0x518A8F: current logo, centered on ITS logical size, then drawn by
    // Sprite2D_Draw/UI_DrawSprite. At 1024×768 with the real 668×229 IMGs, this gives
    // (178,270): 1024/2-668/2 (EA 0x518A28/0x518A48) and 768/2-229/2 (EA 0x518A55/0x518A75).
    // Drawn ONLY in the Panels phase. If the real texture is unavailable -> NOTHING drawn
    // (no fallback fill/label invented, matching the binary which draws nothing when the
    // sprite fails).
    const int logoId = intro_layout::LogoSpriteIndex(state.subState);
    gfx::GpuTexture* logo = GetLogoSprite(ctx, logoId);
    const bool hasRealLogo = logo && logo->Handle() && logo->Width() > 0 && logo->Height() > 0;
    if (hasRealLogo && ctx.phase == UiPhase::Panels && ctx.sprites && ctx.sprites->Ready()) {
        // Scene_IntroRender 0x518A48/0x518A75: Sprite2D_GetWidth/Height read the logical
        // fields +108/+112, not the rounded D3D9 surface; UI_DrawSprite 0x6A3093..0x6A30C5
        // passes this source rectangle to ID3DXSprite::Draw.
        const RECT src{0, 0, static_cast<LONG>(logo->Width()), static_cast<LONG>(logo->Height())};
        const int lx = ctx.screenW / 2 - static_cast<int>(logo->Width()) / 2;
        const int ly = ctx.screenH / 2 - static_cast<int>(logo->Height()) / 2;
        ctx.sprites->DrawSprite(logo->Handle(), &src, lx, ly, gfx::kSpriteWhite);
    }
}

} // namespace ts2::ui

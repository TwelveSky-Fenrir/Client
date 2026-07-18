// UI/ServerSelectRender.cpp — implementation of the ServerSelect rendering.
// Ground truth = Scene_ServerSelectRender 0x519250 and its layout helpers (see
// UI/ServerSelectRender.h for EA/formula details).
#include "UI/ServerSelectRender.h"
#include "Core/Types.h" // ts2::kRefWidth/kRefHeight
#include "Core/Log.h"   // TS2_WARN (warns if a .IMG is unreadable; no visual fallback)
#include "Asset/ImgFile.h" // asset::ImgFile (.IMG loader, real background/buttons)
#include <algorithm>
#include <cstdio>

namespace ts2::ui {

namespace serverselect_layout {

int ButtonOffsetX(int id) {
    // ServerSelect_GetButtonX 0x519F40: switch(a1) { case 0..9: v3+291; case 60: v3+539; default: 0; }
    switch (id) {
    case 0: case 1: case 2: case 3: case 4:
    case 5: case 6: case 7: case 8: case 9:
        return 291;
    case kSpecialSlotC: // 60
        return 539;
    default:
        return 0;
    }
}

int ButtonOffsetY(int id) {
    // ServerSelect_GetButtonY 0x51A0A0: switch(a1) { ... } — EXACT values captured,
    // including the 278 (case 7) vs 287 (case 1) divergence, faithful to the disassembly.
    switch (id) {
    case 0: return 196;
    case 1: return 287;
    case 2: return 378;
    case 3: return 469;
    case 4: return 560;
    case 5: return 651;
    case 6: return 196;
    case 7: return 278; // (not 287 — real binary discrepancy, not a typo)
    case 8: return 378;
    case 9: return 469;
    case kSpecialSlotA: return 196; // 40
    case kSpecialSlotB: return 196; // 50
    case kSpecialSlotC: return 469; // 60
    default: return 0;
    }
}

int ButtonImageId(int id) {
    // ServerSelect_GetButtonImageId 0x51A220.
    switch (id) {
    case 0: return 1786;
    case 1: return 1788;
    case 2: return 1790;
    case 3: return 1792;
    case 4: return 1794;
    case 5: return 1810;
    case 6: return 1798;
    case 7: return 1800;
    case 8: return 1802;
    case 9: return 1808;
    case kSpecialSlotA: return 3452; // 40
    case kSpecialSlotB: return 3099; // 50
    case kSpecialSlotC: return 2630; // 60
    default: return 0;
    }
}

LoadBarOffsets GetLoadBarOffsets(int id) {
    // ServerSelect_DrawLoadBar 0x51A440: switch(a2) { case 0..9,60: ...; default: (nothing,
    // uninitialized local variables in the binary) }. We return valid=false for the
    // default case rather than inventing a position.
    LoadBarOffsets r;
    r.valid = true;
    switch (id) {
    case 0: r.barOffX = 291; r.barOffY = 237; r.fullOffX = 443; r.fullOffY = 234; break;
    case 1: r.barOffX = 291; r.barOffY = 328; r.fullOffX = 443; r.fullOffY = 325; break;
    case 2: r.barOffX = 291; r.barOffY = 419; r.fullOffX = 443; r.fullOffY = 416; break;
    case 3: r.barOffX = 291; r.barOffY = 510; r.fullOffX = 443; r.fullOffY = 507; break;
    case 4: r.barOffX = 291; r.barOffY = 601; r.fullOffX = 443; r.fullOffY = 598; break;
    case 5: r.barOffX = 291; r.barOffY = 692; r.fullOffX = 443; r.fullOffY = 689; break;
    case 6: r.barOffX = 291; r.barOffY = 237; r.fullOffX = 443; r.fullOffY = 234; break;
    case 7: r.barOffX = 291; r.barOffY = 328; r.fullOffX = 443; r.fullOffY = 325; break;
    case 8: r.barOffX = 291; r.barOffY = 419; r.fullOffX = 443; r.fullOffY = 416; break;
    case 9: r.barOffX = 291; r.barOffY = 510; r.fullOffX = 443; r.fullOffY = 507; break;
    case kSpecialSlotC: // 60
        r.barOffX = 542; r.barOffY = 510; r.fullOffX = 694; r.fullOffY = 507; break;
    default:
        r.valid = false; break;
    }
    return r;
}

} // namespace serverselect_layout

// Explicit import (rather than relying on using-directive transitivity through the
// anonymous namespace below): makes kPanelW/kButtonW/... directly visible throughout
// the rest of this file (including ServerSelectRender::Render, defined outside the
// anonymous namespace).
using namespace serverselect_layout;

namespace {

inline bool PtInRect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
}

inline int SpriteW(gfx::GpuTexture* tex, int fallback) {
    return (tex && tex->Handle() && tex->Width() > 0) ? static_cast<int>(tex->Width()) : fallback;
}

inline int SpriteH(gfx::GpuTexture* tex, int fallback) {
    return (tex && tex->Handle() && tex->Height() > 0) ? static_cast<int>(tex->Height()) : fallback;
}

inline void ProjectActionButton(const UiContext& ctx, int spriteW, int spriteH, int& outX, int& outY) {
    // UI_ProjectSpriteToScreen 0x50F5D0, called by ServerSelect with (imgId=4, 891,701)
    // at EA 0x5196D1/0x519A79/0x519AED: half = Sprite2D_GetWidth/Height(...)/2 with
    // INTEGER division, then Crt_ftol(scale * (ref + half)) - half.
    const float scaleX = static_cast<float>(ctx.screenW) / static_cast<float>(ts2::kRefWidth);
    const float scaleY = static_cast<float>(ctx.screenH) / static_cast<float>(ts2::kRefHeight);
    const int halfW = spriteW / 2;
    const int halfH = spriteH / 2;
    outX = static_cast<int>(scaleX * static_cast<float>(891 + halfW) + 0.5f) - halfW;
    outY = static_cast<int>(scaleY * static_cast<float>(701 + halfH) + 0.5f) - halfH;
}

// Population level 0..7, EXACTLY the comparison chain of
// ServerSelect_DrawLoadBar 0x51A440 (EA 0x51a70b-0x51a82f): level = count of k in 1..7
// such that pop >= k*loadStep (this[13371+i], fed by the server via the status
// record). Capped at 7. NOTE: if loadStep==0 (status not received yet) and pop>=0, all
// pop>=0 comparisons are true -> level 7 (full bar, sprite 001_01907.IMG), faithful to
// the binary (no invented maxPop/8 step).
int LoadLevel(int32_t pop, int32_t loadStep) {
    int level = 0;
    for (; level < 7 && pop >= static_cast<int32_t>(level + 1) * loadStep; ++level) {}
    return level;
}

// Blits an atlas sprite at its native size (Sprite2D_Draw 0x4d6b20) ONLY if the
// texture is actually loaded; otherwise draws STRICTLY NOTHING — FAITHFUL behavior
// of Sprite2D_Draw (which displays nothing if EnsureLoaded fails). NO colored
// fallback, NO frame, NO invented text: the binary only draws real `.IMG` sprites.
// Panels phase only (ID3DXSprite batch opened by the caller), no-op outside that phase.
void DrawAtlas(const UiContext& ctx, gfx::GpuTexture* tex, int x, int y) {
    if (ctx.phase != UiPhase::Panels) return;
    if (tex && tex->Handle() && tex->Width() > 0 && tex->Height() > 0
        && ctx.sprites && ctx.sprites->Ready()) {
        ctx.sprites->DrawSprite(tex->Handle(), nullptr, x, y, gfx::kSpriteWhite);
    }
}

} // namespace

gfx::GpuTexture* ServerSelectRender::GetSprite(int slotIndex) {
    if (!device_) return nullptr; // SetDevice() not called yet -> nothing drawn (no fallback fill)
    auto it = spriteCache_.find(slotIndex);
    if (it != spriteCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    char path[80];
    std::snprintf(path, sizeof(path), "G03_GDATA/D01_GIMAGE2D/001/001_%05d.IMG", slotIndex + 1);
    asset::ImgFile img;
    if (img.Load(path))
        tex.CreateFromImgFile(device_, img);
    else
        TS2_WARN("ServerSelectRender : slot %d illisible (%s) - sprite non dessine.", slotIndex, path);

    auto res = spriteCache_.emplace(slotIndex, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

void ServerSelectRender::Render(const UiContext& ctx, const game::ServerSelectState& state,
                                 int cursorX, int cursorY, bool singleServerMode) {
    // Init substate (this[1]==0): the binary draws NOTHING beyond the device clear
    // already done upstream (Gfx_BeginFrame) -> we draw nothing here either (no
    // fallback fill — faithful to the disassembly).
    if (state.subState == game::ServerSelectSubState::Init) {
        return;
    }

    // --- Full-screen scaled background (this[168] = backgroundImageId, EA 0x519461) ---
    // scaleX/scaleY = nWidth/kRefWidth, nHeight/kRefHeight (v13/v14 0x519435/0x519419).
    // Real atlas texture (2380/2381, cf. Scene_ServerSelectUpdate 0x518B30) drawn
    // SCALED full-screen; if loading fails, we draw NOTHING (faithful to
    // Sprite2D_DrawScaled -> EnsureLoaded which displays nothing on failure — NEVER a
    // colored fallback). The sprite draw only makes sense in the Panels phase (ID3DXSprite
    // batch opened by the caller).
    {
        gfx::GpuTexture* bg = GetSprite(state.backgroundImageId);
        if (ctx.phase == UiPhase::Panels && bg && bg->Handle() && bg->Width() > 0 && bg->Height() > 0
            && ctx.sprites && ctx.sprites->Ready()) {
            const float sx = static_cast<float>(ctx.screenW) / static_cast<float>(bg->Width());
            const float sy = static_cast<float>(ctx.screenH) / static_cast<float>(bg->Height());
            ctx.sprites->DrawSpriteScaled(bg->Handle(), nullptr, 0, 0, sx, sy,
                                          gfx::kSpriteWhite, /*compensatePos=*/true);
        }
    }

    // --- Central panel (unk_929344, slot 1785 -> 001_01786.IMG), centered
    // (EA 0x519470-0x5194BF). Centered on the texture's REAL dimensions when loaded
    // (= Sprite2D_GetWidth/Height(unk_929344) of the original), nominal dimensions
    // kPanelW/kPanelH used ONLY to position buttons/bars if the texture isn't loaded
    // (positioning only, NO fallback drawing).
    gfx::GpuTexture* panelTex = GetSprite(kPanelImgSlot);
    const bool panelValid = panelTex && panelTex->Handle() && panelTex->Width() > 0 && panelTex->Height() > 0;
    const int panelW = panelValid ? static_cast<int>(panelTex->Width())  : kPanelW;
    const int panelH = panelValid ? static_cast<int>(panelTex->Height()) : kPanelH;
    const int baseX = ctx.screenW / 2 - panelW / 2;
    const int baseY = ctx.screenH / 2 - panelH / 2;
    // FIDELITY: the disassembly draws the panel via a single
    // `Sprite2D_Draw(&unk_929344, v26, v19)` (EA 0x5194BF) and draws NO text title
    // (the visible label is BAKED into 001_01786.IMG). If the sprite isn't loaded, we
    // draw NOTHING — no colored fallback, no frame, no invented title.
    DrawAtlas(ctx, panelTex, baseX, baseY);

    if (singleServerMode) {
        // --- "Big number" branch (g_ServerModeFlag != 0), EA 0x519664-0x5196B0 ---
        // Here the binary draws server 0's population via UI_DrawNumberValue
        // (EA 0x53FCC0), which calls UI_DrawText (EA 0x69E750, mode 2 = outline): this is
        // TTF TEXT rendered by ID3DXFont::DrawTextA (vtable +56) — NOT `.IMG` digit
        // sprites. This text therefore goes through Gfx/Font (already present), not the
        // sprite atlas. Rendering this number's text isn't wired into this foundation yet:
        // we therefore draw NO number rather than invented text (this path isn't active
        // anyway for the documented launch command /0/0/2). Only the load bar, made of
        // real `.IMG` sprites, is drawn.
        if (!state.servers.empty()) {
            const auto lb = GetLoadBarOffsets(0);
            if (lb.valid) {
                const int32_t pop = state.servers[0].currentPopulation;
                const int32_t maxPop = state.servers[0].maxPopulation;
                const int32_t loadStep = state.servers[0].loadStep; // this[13371] (not a real threshold)
                const int barSlot = (pop >= 0) ? kLoadBarStepSlot[LoadLevel(pop, loadStep)] : kLoadBarPendingSlot;
                DrawAtlas(ctx, GetSprite(barSlot), baseX + lb.barOffX, baseY + lb.barOffY);
                if (pop >= maxPop)
                    DrawAtlas(ctx, GetSprite(kLoadBarFullSlot), baseX + lb.fullOffX, baseY + lb.fullOffY);
            }
        }
    } else {
        // --- Loop branch (g_ServerModeFlag == 0), EA 0x5194DD-0x51964C — ACTUALLY
        // ACTIVE case for the documented launch command. The caller (LoginScene.cpp) now
        // bounds [selectedGroupBtnLo, selectedGroupBtnHi] to the single SingleServer entry
        // (no more 6 channels): this loop therefore draws exactly ONE button, faithful to
        // the binary, not an invented grid.
        const int lo = std::max(0, state.selectedGroupBtnLo);
        const int hiTable = std::min(state.selectedGroupBtnHi,
                                     static_cast<int>(state.servers.size()) - 1);
        for (int i = lo; i <= hiTable; ++i) {
            const auto& sv = state.servers[static_cast<size_t>(i)];
            const int x = baseX + ButtonOffsetX(i);
            const int y = baseY + ButtonOffsetY(i);
            const bool populationKnown = sv.currentPopulation >= 0;
            // Faithful (EA 0x51958C): hover state ONLY if pop>=0 AND pop<maxPop
            // (this[12371]) — NO maxPop<=0 escape hatch. With maxPop==0 (status not
            // received yet), 0<0 is false, so the binary NEVER draws the hover state.
            const bool notFull = sv.currentPopulation < sv.maxPopulation;
            gfx::GpuTexture* btnTex = GetSprite(ButtonImageId(i));
            // Sprite2D_HitTest 0x51958C/0x5199E6: native rectangle of sprite ButtonImageId(i)
            // (001_01787.IMG and variants = 153x23 at 1024x768), not an invented template.
            const bool hover = PtInRect(cursorX, cursorY, x, y,
                                        SpriteW(btnTex, kButtonW), SpriteH(btnTex, kButtonH));

            // Sprite2D_HitTest (EA 0x51958C) + population/capacity guard -> "hovered/active"
            // variant (imageId+1), otherwise "released" variant (imageId), EA
            // 0x5195D3-0x51962C. ONLY the button's real `.IMG` sprite is drawn (slot
            // ButtonImageId(i), hover variant = the NEXT slot +1); if it isn't loaded,
            // nothing is drawn — NO colored fallback, no text label (the name/population
            // are BAKED into the button's sprite; the binary's loop draws no text). The
            // hit-test above shares the same sprite's native dimensions: rendering and
            // clicking coincide pixel-for-pixel.
            const bool useHover = hover && populationKnown && notFull;
            DrawAtlas(ctx, useHover ? GetSprite(ButtonImageId(i) + 1) : btnTex, x, y);

            // Load bars (EA 0x51964C -> ServerSelect_DrawLoadBar) — real sprites only
            // (9 files 001_01900..001_01908.IMG + 001_02600.IMG).
            const auto lb = GetLoadBarOffsets(i);
            if (lb.valid) {
                const int barSlot = populationKnown
                    ? kLoadBarStepSlot[LoadLevel(sv.currentPopulation, sv.loadStep)]
                    : kLoadBarPendingSlot;
                DrawAtlas(ctx, GetSprite(barSlot), baseX + lb.barOffX, baseY + lb.barOffY);
                if (populationKnown && sv.currentPopulation >= sv.maxPopulation)
                    DrawAtlas(ctx, GetSprite(kLoadBarFullSlot), baseX + lb.fullOffX, baseY + lb.fullOffY);
            }
        }
    }

    // --- Back/action button (UI_ProjectSpriteToScreen 0x50F5D0, design anchor (891,701),
    // 3 REAL states, slots 4/5/6 -> 001_00005/6/7.IMG, unk_8E8DA0/unk_8E8E34/unk_8E8EC8) ---
    gfx::GpuTexture* backNormal = GetSprite(kActionBtnNormalSlot);
    const int backW = SpriteW(backNormal, kBackBtnW);
    const int backH = SpriteH(backNormal, kBackBtnH);
    int backX = 0, backY = 0;
    ProjectActionButton(ctx, backW, backH, backX, backY);
    const bool backHover = PtInRect(cursorX, cursorY, backX, backY, backW, backH);
    const int backSlot = actionButtonPressed_ ? kActionBtnPressedSlot
                        : backHover            ? kActionBtnHoverSlot
                                                : kActionBtnNormalSlot;
    // Real `.IMG` sprite of the current state only; if not loaded, nothing is drawn
    // (NO colored fallback nor fallback frame).
    DrawAtlas(ctx, backSlot == kActionBtnNormalSlot ? backNormal : GetSprite(backSlot), backX, backY);

    // The binary continues here into UI_RenderAllDialogs (EA 0x51974E): it's up to the
    // caller to invoke UIManager::Instance().Render() AFTER this Render() to preserve
    // ordering (popups drawn above the ServerSelect screen).
}

void ServerSelectRender::OnActionButtonMouseDown(int cursorX, int cursorY, const UiContext& ctx) {
    gfx::GpuTexture* backNormal = GetSprite(serverselect_layout::kActionBtnNormalSlot);
    const int backW = SpriteW(backNormal, serverselect_layout::kBackBtnW);
    const int backH = SpriteH(backNormal, serverselect_layout::kBackBtnH);
    int backX = 0, backY = 0;
    ProjectActionButton(ctx, backW, backH, backX, backY);
    if (PtInRect(cursorX, cursorY, backX, backY, backW, backH))
        actionButtonPressed_ = true;
}

bool ServerSelectRender::OnActionButtonMouseUp(int cursorX, int cursorY, const UiContext& ctx) {
    // Scene_ServerSelectOnMouseUp 0x519AC0: disarms the latch (this[3]=0, EA 0x519AFE) then,
    // if it was armed, re-tests hover on the button sprite (Sprite2D_HitTest unk_8E8DA0,
    // EA 0x519B1A). Returns true only if the full click (down+up) stayed on the
    // button -> the caller opens the exit confirmation (EA 0x519B3E).
    const bool wasPressed = actionButtonPressed_;
    actionButtonPressed_ = false;
    if (!wasPressed) return false;
    gfx::GpuTexture* backNormal = GetSprite(serverselect_layout::kActionBtnNormalSlot);
    const int backW = SpriteW(backNormal, serverselect_layout::kBackBtnW);
    const int backH = SpriteH(backNormal, serverselect_layout::kBackBtnH);
    int backX = 0, backY = 0;
    ProjectActionButton(ctx, backW, backH, backX, backY);
    return PtInRect(cursorX, cursorY, backX, backY, backW, backH);
}

} // namespace ts2::ui

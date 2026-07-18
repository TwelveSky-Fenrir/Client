// UI/MinimapWidget.cpp — see UI/MinimapWidget.h for the contract + EAs.
#include "UI/MinimapWidget.h"
#include "Game/StaticNpcLoader.h"

#include <cstdio>

namespace ts2::ui {

namespace {
// --- Palette (ARGB), consistent with the HUD style guide (see UI/GameHud.cpp). ---
constexpr D3DCOLOR kPanelBg        = 0xC0101018u; // panel background (same as vitals frame)
constexpr D3DCOLOR kPanelBorder    = 0xFF3A3A48u;
constexpr D3DCOLOR kButtonBg       = 0xFF303038u;
constexpr D3DCOLOR kButtonBorder   = 0xFF505060u;
constexpr D3DCOLOR kViewportBg     = 0xD0081018u; // "map" background (translucent midnight blue)
constexpr D3DCOLOR kViewportBorder = 0xFF2A2A38u;
constexpr D3DCOLOR kSelfDotColor   = 0xFFFFFFFFu; // local player (white)
constexpr D3DCOLOR kPlayerDotColor = 0xFF40C0FFu; // remote players (cyan)
constexpr D3DCOLOR kMonsterDotColor= 0xFFE03030u; // monsters (red)
constexpr D3DCOLOR kNpcDotColor    = 0xFF30D040u; // decor NPCs (green, merchants/guards/quest)
constexpr D3DCOLOR kQuestDotColor  = 0xFFFFD030u; // quest highlight (gold, blinking)
constexpr D3DCOLOR kTextColor      = 0xFFFFFFFFu;

constexpr int kSelfDotSize  = 5;
constexpr int kOtherDotSize = 4;

// Panel dimensions (§12a: actual .IMG sprite dimensions not statically readable
// — same limitation as GameHud's vitals frame, see its header banner — estimated
// here from the documented 145x128 clamp window §12b + button/text margins).
constexpr int kSmallPanelW = 150, kSmallPanelH = 150;
constexpr int kBigPanelW   = 210, kBigPanelH   = 260;
constexpr int kToggleW = 20, kToggleH = 16;

// --- Viewport geometry: PROVEN (BEW-01), the rest estimated ---------------------------------
// UI_DrawSprite of the map background @0x681A6C-0x681AB1: push 0x80 / push 0x91 (h/w) then
// destination `[var_89C][4] + 0x2A` (y) and `[var_89C][0] + 4` (x). Same in mode 2 @0x681D33.
// The viewport is therefore FIXED at (frame.x+4, frame.y+42, 145, 128) — independent of bigMode_ —
// and NOT a rect derived from margins (the old kViewportMarginSide/kViewportTop/... was
// an invention). The panel itself remains estimated (sprites unk_8F16A4/unk_8F1AB0 not
// statically readable), but is sized to contain this proven viewport.
constexpr int kViewportX = 0x04; // @0x681A98 (`add edx, 4`)
constexpr int kViewportY = 0x2A; // @0x681A8C (`add eax, 2Ah`)  = 42
constexpr int kViewportW = 0x91; // @0x681A71 (`push 91h`)      = 145
constexpr int kViewportH = 0x80; // @0x681A6C (`push 80h`)      = 128

// Crop offset: self is targeted at the CENTER of the 145x128 window.
// @0x681995 (`sub ecx, 48h`) and @0x681A08 (`sub ecx, 40h`).
constexpr int kCropHalfW = 0x48; // 72
constexpr int kCropHalfH = 0x40; // 64

// Marker OMISSION bounds @0x681EC0-0x681F03 (asymmetric in the target: 0x96=150 and
// 0xA8=168 don't exactly match 4+145=149 / 42+128=170 — reproduced as-is, not "corrected").
constexpr int kMarkerMaxX = 0x96; // @0x681ED3 (`add edx, 96h`)
constexpr int kMarkerMaxY = 0xA8; // @0x681EFB (`add eax, 0A8h`)
} // namespace

// =============================================================================
// Layout
// =============================================================================
void MinimapWidget::Init(int screenW, int screenH) {
    screenW_ = screenW;
    screenH_ = screenH;
    RecomputeLayout();
}

void MinimapWidget::RecomputeLayout() {
    const int panelW = bigMode_ ? kBigPanelW : kSmallPanelW;
    const int panelH = bigMode_ ? kBigPanelH : kSmallPanelH;

    // Actual anchor: (nWidth - background width, 0) @0x6815BD-0x6816E6 — top-right,
    // flush against the top edge.
    layout_.frame = MmRect{ screenW_ - panelW, 0, panelW, panelH };
    layout_.toggleBtn = MmRect{ layout_.frame.x + 2, layout_.frame.y + 2, kToggleW, kToggleH };

    // BEW-01: PROVEN viewport (frame + (4,42), 145x128) — see constants above. Fixed in the
    // target regardless of this+612/this+616: all 3 modes blit to the SAME spot, only the
    // SOURCE rect changes (@0x681A6C-0x681AB1 vs @0x681D33-0x681D6B).
    layout_.viewport = MmRect{
        layout_.frame.x + kViewportX,
        layout_.frame.y + kViewportY,
        kViewportW,
        kViewportH
    };
}

void MinimapWidget::ToggleSize() { SetBigMode(!bigMode_); }

void MinimapWidget::SetBigMode(bool big) {
    bigMode_ = big;
    RecomputeLayout();
}

// =============================================================================
// Hit-test — §12a toggle button + generic click on the panel.
// =============================================================================
bool MinimapWidget::OnMouseDown(int x, int y) {
    if (layout_.toggleBtn.Contains(x, y)) {
        ToggleSize();
        return true;
    }
    // Click elsewhere on the panel: consumed (blocks passthrough to the 3D world
    // behind the HUD, like GameHud::OnMouseDown does on its own frame).
    return layout_.frame.Contains(x, y);
}

// =============================================================================
// Drawing primitives (same formulas as GameHud::DrawFilledRect/DrawBorder,
// duplicated here to avoid coupling this widget to GameHud — see .h banner).
// =============================================================================
void MinimapWidget::DrawFilledRect(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                                   const MmRect& r, D3DCOLOR color) {
    if (!whiteTex || r.w <= 0 || r.h <= 0) return;
    RECT src{ 0, 0, 1, 1 };
    sprite.DrawSpriteScaled(whiteTex, &src, r.x, r.y,
                            static_cast<float>(r.w), static_cast<float>(r.h),
                            color, /*compensatePos=*/true);
}

void MinimapWidget::DrawBorderRect(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                                   const MmRect& r, int t, D3DCOLOR color) {
    if (t <= 0) return;
    DrawFilledRect(sprite, whiteTex, MmRect{ r.x,           r.y,           r.w, t   }, color);
    DrawFilledRect(sprite, whiteTex, MmRect{ r.x,           r.y + r.h - t, r.w, t   }, color);
    DrawFilledRect(sprite, whiteTex, MmRect{ r.x,           r.y,           t,   r.h }, color);
    DrawFilledRect(sprite, whiteTex, MmRect{ r.x + r.w - t, r.y,           t,   r.h }, color);
}

void MinimapWidget::DrawDot(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                            int cx, int cy, int size, D3DCOLOR color) {
    DrawFilledRect(sprite, whiteTex,
                   MmRect{ cx - size / 2, cy - size / 2, size, size }, color);
}

// =============================================================================
// BEW-01 — World -> image -> screen projection. REAL formulas, surveyed byte by byte in
// UI_GameHud_Render 0x67A3C0; they replace the old "radar" ProjectToViewport, which
// used an invented world radius (kWorldViewRadius=4000) AND inverted clamp/omission.
// =============================================================================

// @0x681915-0x68193C (X) and @0x681942-0x681969 (Y). The exact FPU order is:
//   fild imgW ; fld wx ; fsub minX ; fmulp -> imgW*(wx-minX)
//   fld maxX  ; fsub minX ; fdivp -> / (maxX-minX) ; call Crt_ftol (truncation toward zero).
// The -wz comes from `fchs` @0x68190D (the world is in Z, the image in descending Y).
void MinimapWidget::WorldToImagePixel(const MinimapSource& s, float wx, float wz,
                                      int& px, int& py) {
    const float spanX = s.maxX - s.minX;          // var_844 - var_848
    const float spanY = s.negMinZ - s.negMaxZ;    // var_83C - var_840
    px = (spanX != 0.0f)
             ? static_cast<int>(static_cast<float>(s.imgW) * (wx - s.minX) / spanX)
             : 0;
    py = (spanY != 0.0f)
             ? static_cast<int>(static_cast<float>(s.imgH) * (-wz - s.negMaxZ) / spanY)
             : 0;
}

// @0x68198F-0x6819FC (X) / @0x681A02-0x681A54 (Y). Target structure, branch by branch:
//   srcX = selfPx - 0x48 ; if (srcX < 0) srcX = 0 ;                          (`jns` @0x68199E)
//                          else if (srcX > imgW-0x91) srcX = imgW-0x91 ;     (`jle` @0x6819CF)
// (The binary compensates the self marker position var_838 in parallel; this compensation is
// algebraically equivalent to `sx = frame.x + 4 + px - srcX` in all THREE branches — verified —
// so MarkerScreenPos reproduces it without duplicating the computation.)
// Fidelity note: if imgW < 0x91, `imgW-0x91` is negative and the target clamps srcX to a
// NEGATIVE value. Reproduced as-is (no real minimap is narrower than 145).
void MinimapWidget::ComputeCrop(const MinimapSource& s, int selfPx, int selfPy,
                                int& srcX, int& srcY) {
    srcX = selfPx - kCropHalfW;                       // @0x681995
    if (srcX < 0) srcX = 0;                           // @0x68199E -> @0x6819B2
    else if (srcX > s.imgW - kViewportW)              // @0x6819C4/@0x6819CF
        srcX = s.imgW - kViewportW;                   // @0x6819FC

    srcY = selfPy - kCropHalfH;                       // @0x681A08
    if (srcY < 0) srcY = 0;                           // @0x681A0E -> @0x681A19
    else if (srcY > s.imgH - kViewportH)              // @0x681A28/@0x681A30
        srcY = s.imgH - kViewportH;                   // @0x681A54
}

// @0x681E7E-0x681F05. Marker screen position + OMISSION test (never a clamp).
bool MinimapWidget::MarkerScreenPos(int px, int py, int srcX, int srcY,
                                    int& sx, int& sy) const {
    sx = layout_.frame.x + px - srcX + kViewportX;  // @0x681E8C/@0x681E90
    sy = layout_.frame.y + py - srcY + kViewportY;  // @0x681EAB/@0x681EAF
    // @0x681EC3 `jl` / @0x681ED9 `jg` / @0x681EED `jl` / @0x681F00 `jle` -> sinon jmp loc_68229E.
    if (sx < layout_.frame.x + kViewportX)  return false;
    if (sx > layout_.frame.x + kMarkerMaxX) return false;
    if (sy < layout_.frame.y + kViewportY)  return false;
    if (sy > layout_.frame.y + kMarkerMaxY) return false;
    return true;
}

// =============================================================================
// DrawPanels — background + viewport + markers (inside the caller's
// sprite.Begin()/End(), see GameHud::Render).
// =============================================================================
void MinimapWidget::DrawPanels(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex) {
    // Panel background (§12a — substitute for .IMG sprite unk_8F16A4/unk_8F1AB0, actual
    // dimensions not statically readable). Drawn BEFORE the size guard, like @0x6818A9
    // (Sprite2D_Draw of unk_8F1AB0) which precedes the `cmp [eax+264h], 1` @0x6818B4.
    DrawFilledRect(sprite, whiteTex, layout_.frame, kPanelBg);
    DrawBorderRect(sprite, whiteTex, layout_.frame, 1, kPanelBorder);

    // Size toggle button (§12a).
    DrawFilledRect(sprite, whiteTex, layout_.toggleBtn, kButtonBg);
    DrawBorderRect(sprite, whiteTex, layout_.toggleBtn, 1, kButtonBorder);

    // PROVEN GUARD @0x6818B4: `cmp dword ptr [eax+264h], 1 ; jnz loc_683934` — in SMALL map mode,
    // the target draws NEITHER map background NOR markers. The old code always drew them.
    if (!bigMode_) return;

    // --- Zone map background (BEW-01) --------------------------------------------------
    // this+616 (windowMode_) indexes the 3 textures: `imul ecx, 28h ; add ecx, offset unk_14A9068`
    // @0x681AA8/@0x681AAB. The provider is wired from Scene/SceneManager.cpp.
    MinimapSource src{};
    const bool haveSrc = sourceProvider_
                         && sourceProvider_(static_cast<int>(windowMode_), src)
                         && src.tex != nullptr && src.imgW > 0 && src.imgH > 0
                         && src.maxX != src.minX && src.negMinZ != src.negMaxZ;

    const game::PlayerEntity& self = game::g_World.Self(); // flt_1687330/flt_1687338 = dword_1687234+0xFC/+0x104
    int srcX = 0, srcY = 0;

    if (!haveSrc) {
        // FALLBACK (zone with no minimap loaded / provider absent). The target has no such case:
        // UI_DrawSprite 0x6A3080 exits immediately if the texture object isn't valid (`if (*this)`
        // @0x6A3080) -> the area simply stays empty. We keep a flat fill + border to keep the HUD
        // readable, and still project markers (without cropping: srcX=srcY=0).
        DrawFilledRect(sprite, whiteTex, layout_.viewport, kViewportBg);
        DrawBorderRect(sprite, whiteTex, layout_.viewport, 1, kViewportBorder);
    } else if (windowMode_ == MinimapWindowMode::Free) {
        // case 2 @0x681C7D: srcX=srcY=0 (@0x681D1F/@0x681D29) then UI_DrawSprite with a4=0
        // (@0x681D33-0x681D6B) -> full source rect (0,0,imgW,imgH), blitted at the image's
        // NATURAL size (no scaling: UI_DrawSprite just does Draw(tex, rect, pos)).
        RECT full{ 0, 0, src.imgW, src.imgH };
        sprite.DrawSprite(src.tex, &full,
                          layout_.frame.x + kViewportX, layout_.frame.y + kViewportY,
                          gfx::kSpriteWhite); // hardcoded color -1 @0x6A30FC
    } else {
        // cases 0 (@0x6818FB) and 1 (@0x681ABB): IDENTICAL in the target (doc §12b "redundant/
        // refactor leftover") -> scrolling 145x128 crop, centered on self and clamped to the edges.
        int selfPx = 0, selfPy = 0;
        WorldToImagePixel(src, self.x, self.z, selfPx, selfPy);
        ComputeCrop(src, selfPx, selfPy, srcX, srcY);
        RECT crop{ srcX, srcY, srcX + kViewportW, srcY + kViewportH }; // a5..a8 @0x681A6C-0x681A80
        sprite.DrawSprite(src.tex, &crop,
                          layout_.frame.x + kViewportX, layout_.frame.y + kViewportY,
                          gfx::kSpriteWhite);
    }

    // --- Markers --------------------------------------------------------------------------
    // All go through the SAME rule as the target: image pixel -> screen via the current crop,
    // then OMISSION out of bounds (never clamped to the edge) — see MarkerScreenPos.

    // Local player (var_838/var_20). PROVEN in all THREE modes: cases 0/1 compute it
    // @0x68196F-0x68198C (with clamp compensation) and mode 2 also computes it
    // @0x681CF1-0x681D1C (frame + px + 4 / frame + py + 0x2A, null crop) -> no mode gate here.
    // The viewport center is the right spot ONLY if the crop isn't clamped: near the map edge
    // self DRIFTS away from the center. The old code pinned it to the center permanently — unfaithful.
    if (haveSrc) {
        int px = 0, py = 0, sx = 0, sy = 0;
        WorldToImagePixel(src, self.x, self.z, px, py);
        if (MarkerScreenPos(px, py, srcX, srcY, sx, sy))
            DrawDot(sprite, whiteTex, sx, sy, kSelfDotSize, kSelfDotColor);
    } else {
        // Fallback with no map: self at the center (no bbox to project anything against).
        DrawDot(sprite, whiteTex, layout_.viewport.x + layout_.viewport.w / 2,
                layout_.viewport.y + layout_.viewport.h / 2, kSelfDotSize, kSelfDotColor);
        return; // without a real bbox, no other projection makes sense
    }

    // Mode gate for entity markers. PROVEN for the NPC loop only: the PER-ENTRY switch
    // @0x681DDD-@0x681DF3 routes case 0 -> 0x681DF8, case 1 -> 0x68204D, and DEFAULT (mode 2)
    // -> `jmp loc_68229E` = the loop-continue label -> no NPC drawn in mode 2.
    // INFERENCE (not proven) for players/monsters: they do NOT appear in this block of the
    // target (which only projects g_NpcRenderArray) — these are modeling additions, see the
    // .h banner. We align them with the nearest proven rule rather than inventing behavior.
    if (windowMode_ == MinimapWindowMode::Free) return; // @0x681DF3

    // Remote players (§12c — alliance roster not modeled on the GameState side, see the .h
    // TODO: all active players besides self are projected).
    for (size_t i = 1; i < game::g_World.players.size(); ++i) {
        const game::PlayerEntity& p = game::g_World.players[i];
        if (!p.active) continue;
        int px = 0, py = 0, sx = 0, sy = 0;
        WorldToImagePixel(src, p.x, p.z, px, py);
        if (MarkerScreenPos(px, py, srcX, srcY, sx, sy))
            DrawDot(sprite, whiteTex, sx, sy, kOtherDotSize, kPlayerDotColor);
    }

    // Monsters (§12c) — blinking highlight if designated via SetQuestHighlightMonster
    // (original formula §9: ftol(gameTime*2)%2==1, hidden every other frame).
    for (const game::MonsterEntity& m : game::g_World.monsters) {
        if (!m.active) continue;
        int px = 0, py = 0, sx = 0, sy = 0;
        WorldToImagePixel(src, m.x, m.z, px, py);
        if (!MarkerScreenPos(px, py, srcX, srcY, sx, sy)) continue;

        D3DCOLOR color = kMonsterDotColor;
        if (questHighlightMonster_.valid() && m.id == questHighlightMonster_) {
            const bool blinkOn = (static_cast<long>(gfx::g_GameTimeSec * 2.0f) % 2) == 0;
            if (!blinkOn) continue; // blinking: not drawn every other frame
            color = kQuestDotColor;
        }
        DrawDot(sprite, whiteTex, sx, sy, kOtherDotSize, color);
    }

    // Decor NPCs — the ONLY category this block of the target actually draws
    // (g_NpcRenderArray 0x1764D14, stride 0x58; loop @0x681D8B bounded by g_NpcCount 0x1687220).
    // Client-source equivalent = game::ZoneNpcs()/StaticNpcLoader (see the .h banner).
    // Per-type icons (switch on def+0x520 = fieldB @0x681F19 -> atlas 0x92D/0x930/0x933/0x936/
    // 0x939) not modeled: the atlas belongs to GameHud, out of scope -> single-color dot.
    for (const game::StaticNpcSlot& n : game::ZoneNpcs()) {
        // @0x681DA6 `cmp ds:dword_1764D18[edx], 0 ; jnz` -> inactive slot (entry+4 == 0) = SKIPPED.
        // Essential since W7: ZoneNpcs() is a pool of 100 FIXED slots WITH GAPS (see
        // Game/StaticNpcLoader.h) — the old loop, without this test, dropped a marker at
        // the world origin for every empty slot.
        if (!n.active) continue;
        // @0x681DC0 `cmp dword ptr [ecx+524h], 4 ; jnz` with ecx = entry->def -> def+1316 =
        // NpcDefRecord::fieldC (Game/ExtraDatabases.h:56). Value 4 is excluded from the minimap.
        if (n.def && n.def->fieldC == 4u) continue; // 4u: fieldC is a uint32_t (avoids C4389)
        // x @0x1764D28+i*88 (= entry+20) and z @0x1764D30+i*88 (= entry+28) @0x681E01/@0x681E16.
        int px = 0, py = 0, sx = 0, sy = 0;
        WorldToImagePixel(src, n.x, n.z, px, py);
        if (MarkerScreenPos(px, py, srcX, srcY, sx, sy))
            DrawDot(sprite, whiteTex, sx, sy, kOtherDotSize, kNpcDotColor);
    }

    // TODO party/alliance: see the MinimapWidget.h header banner (no alliance roster
    // on the GameState side) — clean fallback, the rest of the minimap stays fully functional.
}

// =============================================================================
// DrawText — toggle button + (large mode) zone name/coordinates (§12a),
// inside the caller's font.BeginBatch()/EndBatch().
// =============================================================================
void MinimapWidget::DrawText(gfx::Font& font) {
    if (!font.Ready()) return;
    char buf[64];

    // Toggle button label (real text = ServerSelect_GetButtonTextId(map
    // name), StrTable not wired here -> generic +/- symbol).
    font.DrawTextStyled(bigMode_ ? "-" : "+",
                        layout_.toggleBtn.x + kToggleW / 2 - 3, layout_.toggleBtn.y + 2,
                        kTextColor, gfx::kStyleShadow);

    if (!bigMode_) return; // small map: no zone name or coordinates (§12a)

    // Zone name (approximated by the raw zone id, for lack of StrTable003 wired
    // into this model — see Docs/TS2_UI_GAMEHUD_RENDER.md §12a).
    std::snprintf(buf, sizeof(buf), "Zone %d", game::g_World.zoneId);
    int tw = font.MeasureText(buf);
    font.DrawTextStyled(buf, layout_.frame.x + (layout_.frame.w - tw) / 2,
                        layout_.viewport.y + layout_.viewport.h + 4,
                        kTextColor, gfx::kStyleShadow);

    // Local player coordinates, format "%d , %d" faithful to the binary (§12a).
    const game::PlayerEntity& self = game::g_World.Self();
    std::snprintf(buf, sizeof(buf), "%d , %d", static_cast<int>(self.x), static_cast<int>(self.z));
    tw = font.MeasureText(buf);
    font.DrawTextStyled(buf, layout_.frame.x + (layout_.frame.w - tw) / 2,
                        layout_.viewport.y + layout_.viewport.h + 18,
                        kTextColor, gfx::kStyleShadow);
}

} // namespace ts2::ui

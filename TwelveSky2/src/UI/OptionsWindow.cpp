// UI/OptionsWindow.cpp — implementation of the Options window. See OptionsWindow.h.
#include "UI/OptionsWindow.h"
#include "UI/PanelSkin.h"
#include "Game/ClientRuntime.h" // game::Str(id) -> real StrTable005 text (005.DAT, mMESSAGE)
#include "Scene/SceneManager.h" // ::ts2::Notice_DispatchOkAction(2) : Logout (OPT-02, @0x5C04DF)
#include "Core/Log.h"           // TS2_LOG : "[ABNORMAL_END] ( 5 )" (Quit game, @0x66D253)

#include <algorithm> // std::clamp
#include <cstdio>    // snprintf

namespace ts2::ui {

namespace {

// Real panel background (best effort): (400,440) template from the UI atlas folder
// G03_GDATA/D01_GIMAGE2D/001 — candidate NOT CONFIRMED by IDA, chosen by ratio/height
// proximity to the Options panel (380x440, exact height match; cf. detailed methodology
// in UI/PanelSkin.h). Automatic fallback to kColBg if absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_03637.IMG");


// Table of the 21 fields ACTUALLY exposed by UI_OptionsWnd (0x66C180/0x66D140/0x66EAC0),
// in the ORIGINAL panel's ORDER (hence also memory order idx0..idx22, skipping
// idx3/idx8 — cf. banner in OptionsWindow.h). This is the window's single source of
// truth: Render/OnClick iterate this table instead of duplicating 21 near-identical
// blocks of code.
//
// NOTE OPT-09 (2 merged, documented, unported rows): the original panel actually has
// 23 control rows. The 2 missing from kFields are, per UI_OptionsWnd_OnClick 0x66D140:
//   - the "blend" row (this+22/23, y+210, @0x66DAEF/@0x66DB58): writes NO g_Opt_* field;
//     it only calls Gfx_ApplyOverlayBlendMode_SetState 0x53F630 (NOT ported, Gfx) then
//     Options_SaveBin. With no options field, modeling it here would be a made-up no-op.
//   - the Brightness DUPLICATE (this+24/25, y+231, @0x66DBE5/@0x66DC64): edits
//     g_Opt_BrightnessLevel a 2nd time, with the exact same [1,9] clamps as idx9 below (no
//     new semantics). kFieldCount=21 thus absorbs the 23 real rows.
//
// valueStrBase = StrTable005 id (005.DAT) for value==minV, read and verified by
// disassembling UI_OptionsWnd_Render 0x66EAC0 (each branch `if(g_Opt_X==v)
// StrTable005_Get(g_LangId,ID)`). -1 = no text in the original (the 2 volume sliders)
// -> numeric "%d" fallback AND SLIDER marker (cf. IsSlider below, OPT-04).
const OptionsFieldDef kFields[] = {
    // FR label                        field                                     chkbx  min max step  valueStrBase (005.DAT)
    { "Effets de compétence/arme",  &config::GameOptions::ShowSkillEffects,   true,  0, 1,   1,  158 }, // idx0  158=OFF 159=ON
    { "Distance d'affichage",       &config::GameOptions::DisplayRangeTier,  false, 1, 3,   1,  160 }, // idx1  160=LOW 161=MEDIUM 162=HIGH
    // idx2: real values are "Basic"/"Advanced" (StrTable005 #168/#169) — NOT a simple
    // generic on/off; the "misc"/"reserved" label from an earlier pass was a placeholder,
    // no runtime consumer identified beyond this UI toggle.
    { "Mode interface (Basic/Advanced)", &config::GameOptions::Toggle2Reserved, true, 0, 1, 1, 168 }, // idx2  168=Basic 169=Advanced
    // idx3 (Reserved3): ABSENT — no row in the original UI_OptionsWnd, cf. OptionsWindow.h.
    { "Marqueurs de coups",         &config::GameOptions::ShowHitMarkers,     true,  0, 1,   1,  170 }, // idx4  170=OFF 171=ON
    { "Plaques de nom",             &config::GameOptions::ShowNameplates,     true,  0, 1,   1,  172 }, // idx5  172=OFF 173=ON
    { "Traînée d'arme",             &config::GameOptions::WeaponTrail,        true,  0, 1,   1,  174 }, // idx6  174=OFF 175=ON
    // idx7: REAL text order confirmed non-monotone (176..179 = OFF,HIGH,MEDIUM,LOW) —
    // the numeric level 1..3 does NOT go from weakest to strongest in the original text.
    // "+" increment quirk (OPT-05): 0->1, 1->3 (2 unreachable going up), 3->3 —
    // reproduced in ActivateIfHit (RowPlus) and NOT via a generic clamp.
    { "Lueur d'arme (niveau)",      &config::GameOptions::WeaponGlowLevel,   false, 0, 3,   1,  176 }, // idx7  176=OFF 177=HIGH 178=MEDIUM 179=LOW
    // idx8 (Reserved8): ABSENT — same as idx3.
    { "Luminosité (Phase)",         &config::GameOptions::BrightnessLevel,   false, 1, 9,   1,  182 }, // idx9  182..190 = "Phase 1".."Phase 9"
    { "Volume musique",             &config::GameOptions::MusicVolume,       false, 0, 100, 5,  -1  }, // idx10 SLIDER (no text in the original)
    { "Volume effets sonores",      &config::GameOptions::SoundVolume,       false, 0, 100, 5,  -1  }, // idx11 SLIDER (no text in the original)
    { "Musique de fond activée",    &config::GameOptions::BgmEnabled,         true,  0, 1,   1,  191 }, // idx12 191=OFF 192=ON
    { "Mode raccourcis (morph UI)", &config::GameOptions::MorphUiMode,       false, 1, 2,   1,  460 }, // idx13 460="[F1]-[F10]" 461="[1]-[0]"
    { "Ombres/reflets",             &config::GameOptions::GfxDetailShadows,   true,  0, 1,   1,  193 }, // idx14 193=Disable 194=Enable
    { "Filtre chat de groupe",      &config::GameOptions::FilterPartyChat,    true,  0, 1,   1,  193 }, // idx15 193=Disable 194=Enable
    { "Filtre invit. groupe",       &config::GameOptions::FilterPartyInvite,  true,  0, 1,   1,  193 }, // idx16 193=Disable 194=Enable
    { "Filtre invit. alliance",     &config::GameOptions::FilterAllyInvite,   true,  0, 1,   1,  193 }, // idx17 193=Disable 194=Enable
    { "Filtre confirmation 19",     &config::GameOptions::FilterPrompt19,     true,  0, 1,   1,  193 }, // idx18 193=Disable 194=Enable
    { "Filtre confirmation 20",     &config::GameOptions::FilterPrompt20,     true,  0, 1,   1,  193 }, // idx19 193=Disable 194=Enable
    { "Filtre confirmation 10",     &config::GameOptions::FilterPrompt10,     true,  0, 1,   1,  193 }, // idx20 193=Disable 194=Enable
    { "Filtre confirmation 14",     &config::GameOptions::FilterPrompt14,     true,  0, 1,   1,  193 }, // idx21 193=Disable 194=Enable
    { "Filtre messages monde",      &config::GameOptions::FilterWorldEntity,  true,  0, 1,   1,  193 }, // idx22 193=Disable 194=Enable
};
constexpr int kFieldCount = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));

// A field is a volume SLIDER (idx10 MusicVolume / idx11 SoundVolume) iff it has no
// StrTable005 text (valueStrBase==-1). In the binary these 2 rows are NOT -/+ buttons
// but draggable handles (UI_OptionsWnd_OnMouseDown 0x66C180 grabs the knob;
// UI_OptionsWnd_Render 0x66EAC0 writes the value INTO the render while the knob is
// armed: @0x66FF58 music, @0x66FFEE sound). Cf. OPT-04.
bool IsSlider(const OptionsFieldDef& f) { return f.valueStrBase < 0; }

// Real value text for row `f` at current value `v`: reads StrTable005 (005.DAT,
// mMESSAGE) via game::Str(valueStrBase + (v - minV)), matching the contiguous read
// confirmed in UI_OptionsWnd_Render 0x66EAC0. "%d" fallback for the 2 volume sliders
// (valueStrBase==-1, cf. kFields comment).
std::string FieldValueText(const OptionsFieldDef& f, int32_t v) {
    if (f.valueStrBase < 0) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%d", v);
        return buf;
    }
    const int32_t clamped = std::clamp(v, f.minV, f.maxV);
    return game::Str(f.valueStrBase + (clamped - f.minV));
}

} // namespace

OptionsWindow::OptionsWindow() {
    x_ = 0; y_ = 0; bOpen_ = false;
}

void OptionsWindow::Open() {
    // UI_OptionsWnd_Open 0x66C120: only sets the "visible" flag (*(this+2)=1) then
    // zero-fills the 47 button latches (this+3..this+49). It does NOT re-read the disk —
    // g_Options is already in memory (normalized at boot via Options_LoadAndNormalize
    // 0x4C2110) and every mutation persists it immediately (OPT-01). We reproduce this
    // behavior: no Load() here, just reset scroll and armed state.
    Dialog::Open();
    scroll_ = 0;
    armedTarget_ = Target::None;
    armedRow_ = -1;
}

void OptionsWindow::Layout(int screenW, int screenH, Rect& panel, Rect& close,
                            Rect& logout, Rect& exitGame, Rect& list,
                            Rect& scrollUp, Rect& scrollDown) const {
    panel.x = screenW / 2 - kPanelW / 2;
    panel.y = screenH / 2 - kPanelH / 2;
    panel.w = kPanelW;
    panel.h = kPanelH;

    // Close button (cross) at the top-right of the panel.
    close = { panel.x + panel.w - kCloseBtn - 6, panel.y + 4, kCloseBtn, kCloseBtn };

    // List area: below the title, above the bottom button bar.
    list.x = panel.x + 10;
    list.y = panel.y + kTitleH + 6;
    list.w = panel.w - 20 - (kCloseBtn + 4); // reserve the scroll-arrow column
    list.h = panel.h - kTitleH - kFooterH - 12;

    // Scroll arrows, narrow column to the right of the list.
    scrollUp   = { list.x + list.w + 4, list.y,                     kCloseBtn, kScrollBtnH };
    scrollDown = { list.x + list.w + 4, list.y + list.h - kScrollBtnH, kCloseBtn, kScrollBtnH };

    // Footer (OPT-02/03): "Logout" on the LEFT, "Quit game" on the RIGHT — order
    // faithful to the binary (this+5 x+148 @0x66D2AC before this+4 x+202 @0x66D236).
    // These 2 buttons REPLACE the old Save/Cancel pair (an invention; persistence is
    // now implicit on every mutation, OPT-01).
    const int by = panel.y + panel.h - kFooterH + 6;
    const int bw = 120, bh = 24;
    logout   = { panel.x + panel.w / 2 - bw - 6, by, bw, bh };
    exitGame = { panel.x + panel.w / 2 + 6,      by, bw, bh };
}

int OptionsWindow::VisibleRowCount(const Rect& list) const {
    return list.h / kRowH;
}

int OptionsWindow::MaxScroll(const Rect& list) const {
    const int visible = VisibleRowCount(list);
    return (kFieldCount > visible) ? (kFieldCount - visible) : 0;
}

void OptionsWindow::ClampScroll(const Rect& list) {
    scroll_ = std::clamp(scroll_, 0, MaxScroll(list));
}

bool OptionsWindow::RowRect(const Rect& list, int row, Rect& out) const {
    const int rel = row - scroll_;
    const int visible = VisibleRowCount(list);
    if (rel < 0 || rel >= visible) return false;
    out = { list.x, list.y + rel * kRowH, list.w, kRowH };
    return true;
}

OptionsWindow::Rect OptionsWindow::RowCheckboxRect(const Rect& row) const {
    const int s = 14;
    return { row.x + row.w - s - 4, row.y + (kRowH - s) / 2, s, s };
}

OptionsWindow::Rect OptionsWindow::RowMinusRect(const Rect& row) const {
    const int s = 16;
    return { row.x + row.w - 3 * s - 44, row.y + (kRowH - s) / 2, s, s };
}

OptionsWindow::Rect OptionsWindow::RowPlusRect(const Rect& row) const {
    const int s = 16;
    return { row.x + row.w - s - 4, row.y + (kRowH - s) / 2, s, s };
}

OptionsWindow::Rect OptionsWindow::RowSliderTrackRect(const Rect& row) const {
    // 94 px track (kTrackW), mirrors dbl_7BAF20=94.0. Right-aligned, leaving ~46 px for
    // the numeric "%d" text (0..100). Since the ported panel is smaller, the geometry
    // isn't pixel-transposable — only the 94 width is kept.
    const int trackW = kTrackW;
    const int h = 6;
    return { row.x + row.w - trackW - 46, row.y + (kRowH - h) / 2, trackW, h };
}

bool OptionsWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    Rect panel, close, logout, exitGame, list, scrollUp, scrollDown;
    Layout(lastScreenW_, lastScreenH_, panel, close, logout, exitGame, list, scrollUp, scrollDown);

    armedTarget_ = Target::None;
    armedRow_ = -1;

    if (!In(panel, x, y)) return false; // click outside the window: does not consume

    if (In(close, x, y))          { armedTarget_ = Target::Close; }
    else if (In(logout, x, y))    { armedTarget_ = Target::Logout; }
    else if (In(exitGame, x, y))  { armedTarget_ = Target::ExitGame; }
    else if (In(scrollUp, x, y))   { armedTarget_ = Target::ScrollUp; }
    else if (In(scrollDown, x, y)) { armedTarget_ = Target::ScrollDown; }
    else if (In(list, x, y)) {
        // Find the row + sub-control under the cursor.
        for (int row = scroll_; row < kFieldCount; ++row) {
            Rect r;
            if (!RowRect(list, row, r)) break;
            if (!In(r, x, y)) continue;
            const OptionsFieldDef& f = kFields[row];
            if (f.checkbox) {
                if (In(RowCheckboxRect(r), x, y)) { armedTarget_ = Target::RowCheckbox; armedRow_ = row; }
            } else if (IsSlider(f)) {
                // Knob grab (OPT-04): the binary tests the knob sprite; here we arm on
                // the whole horizontal span of the track (In(r) already guarantees y)
                // for a workable click zone in the reduced panel.
                Rect tr = RowSliderTrackRect(r);
                if (x >= tr.x && x < tr.x + tr.w) { armedTarget_ = Target::Slider; armedRow_ = row; }
            } else {
                if (In(RowMinusRect(r), x, y))      { armedTarget_ = Target::RowMinus; armedRow_ = row; }
                else if (In(RowPlusRect(r), x, y))  { armedTarget_ = Target::RowPlus;  armedRow_ = row; }
            }
            break;
        }
    }
    return true; // effectively a modal panel: the whole surface consumes the click
}

void OptionsWindow::ActivateIfHit(const Rect& panel, const Rect& close, const Rect& logout,
                                   const Rect& exitGame, const Rect& list,
                                   const Rect& scrollUp, const Rect& scrollDown, int x, int y) {
    switch (armedTarget_) {
        case Target::Close:
            if (In(close, x, y)) Close();
            break;
        case Target::ExitGame:
            // this+4 @0x66D236: Log "[ABNORMAL_END] ( 5 )" @0x66D253 ; g_QuitFlag=1 @0x66D258 ;
            // Close @0x66D265. g_QuitFlag (0x815590) modeled via PostQuitMessage(0) (project
            // idiom, cf. UI/LoginScene.cpp:232-233, App/App.cpp WM_DESTROY). Does NOT save.
            if (In(exitGame, x, y)) {
                TS2_LOG("[ABNORMAL_END] ( 5 )"); // Log_WriteLine 0x53F2D0
                ::PostQuitMessage(0);
                Close();                          // UI_OptionsWnd_Close 0x66C160
            }
            break;
        case Target::Logout:
            // this+5 @0x66D2AC: Net_CloseSocket(&g_NetClient) @0x66D2C4 ; g_SceneMgr=2 @0x66D2C9 ;
            // g_SceneSubState=0 @0x66D2D3 ; dword_1676188=0 @0x66D2DD ; Close @0x66D2EA. The
            // quadruplet is already ported by ::ts2::Notice_DispatchOkAction case 2 (same
            // primitive @0x5C04DF: NetCloseSocket + DEFERRED return to ServerSelect). This
            // respects the "no live scene change" constraint (SceneManager owned elsewhere).
            // Does NOT save.
            if (In(logout, x, y)) {
                ::ts2::Notice_DispatchOkAction(2); // 0x5C04C9 case 2
                Close();                            // UI_OptionsWnd_Close 0x66C160
            }
            break;
        case Target::ScrollUp:
            if (In(scrollUp, x, y)) { scroll_ -= 1; ClampScroll(list); }
            break;
        case Target::ScrollDown:
            if (In(scrollDown, x, y)) { scroll_ += 1; ClampScroll(list); }
            break;
        case Target::RowCheckbox: {
            if (armedRow_ < 0 || armedRow_ >= kFieldCount) break;
            Rect r;
            if (!RowRect(list, armedRow_, r)) break;
            if (!In(RowCheckboxRect(r), x, y)) break;
            const OptionsFieldDef& f = kFields[armedRow_];
            int32_t& v = config::g_Options.*(f.field);
            v = (v != 0) ? 0 : 1; // toggle (UI_OptionsWnd_OnClick: flips 0/1)
            v = std::clamp<int32_t>(v, f.minV, f.maxV);
            // OPT-01: every successful mutation saves (LABEL_270 @0x66E819: Options_SaveBin).
            config::g_Options.Save();
            break;
        }
        case Target::RowMinus: {
            if (armedRow_ < 0 || armedRow_ >= kFieldCount) break;
            Rect r;
            if (!RowRect(list, armedRow_, r)) break;
            if (!In(RowMinusRect(r), x, y)) break;
            const OptionsFieldDef& f = kFields[armedRow_];
            int32_t& v = config::g_Options.*(f.field);
            v = std::clamp<int32_t>(v - f.step, f.minV, f.maxV);
            config::g_Options.Save(); // OPT-01 (LABEL_270 @0x66E819)
            break;
        }
        case Target::RowPlus: {
            if (armedRow_ < 0 || armedRow_ >= kFieldCount) break;
            Rect r;
            if (!RowRect(list, armedRow_, r)) break;
            if (!In(RowPlusRect(r), x, y)) break;
            const OptionsFieldDef& f = kFields[armedRow_];
            int32_t& v = config::g_Options.*(f.field);
            if (f.field == &config::GameOptions::WeaponGlowLevel) {
                // OPT-05 — original quirk: `if (++v > 1) v = 3;` (@0x66D998/@0x66D99A).
                // -> 0->1, 1->3 (value 2 is only reachable going DOWN), 3->3.
                // DO NOT replace with a generic clamp or GameOptions::Normalize() (their
                // 0..3 clamp would hide the 1->3 jump).
                v = (v + 1 > 1) ? 3 : v + 1;
            } else {
                v = std::clamp<int32_t>(v + f.step, f.minV, f.maxV);
            }
            config::g_Options.Save(); // OPT-01 (LABEL_270 @0x66E819)
            break;
        }
        case Target::Slider: {
            // OPT-04 — end-of-drag commit (this+26/27 @0x66DC9C/@0x66DCB9: Options_SaveBin
            // WITHOUT a hit-test). The value was already written live by Render (@0x66FF58);
            // we recompute it one last time from the release position, then save.
            if (armedRow_ < 0 || armedRow_ >= kFieldCount) break;
            Rect r;
            if (!RowRect(list, armedRow_, r)) break;
            const OptionsFieldDef& f = kFields[armedRow_];
            const int span = f.maxV - f.minV; // 100 (0..100)
            if (span > 0) {
                Rect tr = RowSliderTrackRect(r);
                const int cx = std::clamp(x, tr.x, tr.x + tr.w);
                int32_t nv = static_cast<int32_t>((cx - tr.x) * span / tr.w) + f.minV;
                nv = std::clamp<int32_t>(nv, f.minV, f.maxV);
                config::g_Options.*(f.field) = nv;
            }
            config::g_Options.Save(); // OPT-01/OPT-04
            break;
        }
        default: break;
    }
    (void)panel;
    armedTarget_ = Target::None;
    armedRow_ = -1;
}

bool OptionsWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    Rect panel, close, logout, exitGame, list, scrollUp, scrollDown;
    Layout(lastScreenW_, lastScreenH_, panel, close, logout, exitGame, list, scrollUp, scrollDown);

    const bool inside = In(panel, x, y);
    if (inside) ActivateIfHit(panel, close, logout, exitGame, list, scrollUp, scrollDown, x, y);
    else { armedTarget_ = Target::None; armedRow_ = -1; }

    return inside; // only consumes if the release lands inside the window
}

bool OptionsWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    Rect panel, close, logout, exitGame, list, scrollUp, scrollDown;
    Layout(lastScreenW_, lastScreenH_, panel, close, logout, exitGame, list, scrollUp, scrollDown);

    if (vk == VK_ESCAPE) { Close(); return true; }
    if (vk == VK_UP)     { scroll_ -= 1; ClampScroll(list); return true; }
    if (vk == VK_DOWN)   { scroll_ += 1; ClampScroll(list); return true; }
    if (vk == VK_PRIOR)  { scroll_ -= VisibleRowCount(list); ClampScroll(list); return true; } // Page Up
    if (vk == VK_NEXT)   { scroll_ += VisibleRowCount(list); ClampScroll(list); return true; } // Page Down
    return true; // effectively modal while open: swallows other keys
}

void OptionsWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Rect panel, close, logout, exitGame, list, scrollUp, scrollDown;
    Layout(ctx.screenW, ctx.screenH, panel, close, logout, exitGame, list, scrollUp, scrollDown);
    ClampScroll(list);

    if (ctx.phase == UiPhase::Panels) {
        // Panel background + frame.
        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColBorder, 2);
        // Title band.
        ctx.FillRect(panel.x, panel.y, panel.w, kTitleH, kColTitleBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, kTitleH, kColBorder, 1);

        // Close button (cross).
        const bool closeHover = In(close, cursorX, cursorY);
        ctx.FillRect(close.x, close.y, close.w, close.h,
                     (armedTarget_ == Target::Close) ? kColBtnDown : (closeHover ? kColBtnHover : kColClose));
        ctx.DrawFrame(close.x, close.y, close.w, close.h, kColBorder, 1);

        // Scroll arrows.
        const bool canUp = scroll_ > 0;
        const bool canDown = scroll_ < MaxScroll(list);
        ctx.FillRect(scrollUp.x, scrollUp.y, scrollUp.w, scrollUp.h,
                     (armedTarget_ == Target::ScrollUp) ? kColBtnDown
                     : (canUp && In(scrollUp, cursorX, cursorY) ? kColBtnHover : kColBtn));
        ctx.DrawFrame(scrollUp.x, scrollUp.y, scrollUp.w, scrollUp.h, kColBorder, 1);
        ctx.FillRect(scrollDown.x, scrollDown.y, scrollDown.w, scrollDown.h,
                     (armedTarget_ == Target::ScrollDown) ? kColBtnDown
                     : (canDown && In(scrollDown, cursorX, cursorY) ? kColBtnHover : kColBtn));
        ctx.DrawFrame(scrollDown.x, scrollDown.y, scrollDown.w, scrollDown.h, kColBorder, 1);

        // Visible rows: alternating band + control (checkbox / slider / -/+).
        for (int row = scroll_; row < kFieldCount; ++row) {
            Rect r;
            if (!RowRect(list, row, r)) break;
            if ((row & 1) == 0) ctx.FillRect(r.x, r.y, r.w, r.h, kColRowAlt);

            const OptionsFieldDef& f = kFields[row];
            if (f.checkbox) {
                Rect cb = RowCheckboxRect(r);
                const bool hover = In(cb, cursorX, cursorY);
                ctx.FillRect(cb.x, cb.y, cb.w, cb.h,
                             (armedTarget_ == Target::RowCheckbox && armedRow_ == row) ? kColBtnDown
                             : (hover ? kColBtnHover : kColBtn));
                ctx.DrawFrame(cb.x, cb.y, cb.w, cb.h, kColBorder, 1);
            } else if (IsSlider(f)) {
                Rect tr = RowSliderTrackRect(r);
                const int span = f.maxV - f.minV; // 100
                // OPT-04 — LIVE write during drag: the binary writes g_Opt_MusicVolume
                // (@0x66FF58) / g_Opt_SoundVolume (@0x66FFEE) INSIDE Render while the knob
                // is armed. Faithful formula: v = ftol((cursorX - trackX) * 100 / 94),
                // cursor clamped to the track (clamp [winX+0x80, winX+0xDE] @0x66FED0).
                if (armedTarget_ == Target::Slider && armedRow_ == row && span > 0 && tr.w > 0) {
                    const int cx = std::clamp(cursorX, tr.x, tr.x + tr.w);
                    int32_t nv = static_cast<int32_t>((cx - tr.x) * span / tr.w) + f.minV;
                    nv = std::clamp<int32_t>(nv, f.minV, f.maxV);
                    config::g_Options.*(f.field) = nv;
                }
                // Track + handle.
                ctx.FillRect(tr.x, tr.y, tr.w, tr.h, kColBtn);
                ctx.DrawFrame(tr.x, tr.y, tr.w, tr.h, kColBorder, 1);
                const int32_t vc = std::clamp<int32_t>(config::g_Options.*(f.field), f.minV, f.maxV);
                const int kx = (span > 0) ? (tr.x + (vc - f.minV) * tr.w / span) : tr.x;
                const int kw = 6;
                const bool armed = (armedTarget_ == Target::Slider && armedRow_ == row);
                ctx.FillRect(kx - kw / 2, r.y + 2, kw, kRowH - 4, armed ? kColBtnDown : kColBtnHover);
                ctx.DrawFrame(kx - kw / 2, r.y + 2, kw, kRowH - 4, kColBorder, 1);
            } else {
                Rect mr = RowMinusRect(r);
                Rect pr = RowPlusRect(r);
                const bool mHover = In(mr, cursorX, cursorY);
                const bool pHover = In(pr, cursorX, cursorY);
                ctx.FillRect(mr.x, mr.y, mr.w, mr.h,
                             (armedTarget_ == Target::RowMinus && armedRow_ == row) ? kColBtnDown
                             : (mHover ? kColBtnHover : kColBtn));
                ctx.DrawFrame(mr.x, mr.y, mr.w, mr.h, kColBorder, 1);
                ctx.FillRect(pr.x, pr.y, pr.w, pr.h,
                             (armedTarget_ == Target::RowPlus && armedRow_ == row) ? kColBtnDown
                             : (pHover ? kColBtnHover : kColBtn));
                ctx.DrawFrame(pr.x, pr.y, pr.w, pr.h, kColBorder, 1);
            }
        }

        // Footer buttons: Logout (left) / Quit game (right, "danger" tint).
        const bool logoutHover = In(logout, cursorX, cursorY);
        ctx.FillRect(logout.x, logout.y, logout.w, logout.h,
                     (armedTarget_ == Target::Logout) ? kColBtnDown : (logoutHover ? kColBtnHover : kColBtn));
        ctx.DrawFrame(logout.x, logout.y, logout.w, logout.h, kColBorder, 1);
        const bool exitHover = In(exitGame, cursorX, cursorY);
        ctx.FillRect(exitGame.x, exitGame.y, exitGame.w, exitGame.h,
                     (armedTarget_ == Target::ExitGame) ? kColBtnDown : (exitHover ? kColBtnHover : kColClose));
        ctx.DrawFrame(exitGame.x, exitGame.y, exitGame.w, exitGame.h, kColBorder, 1);
        return;
    }

    // --- Text phase ---
    ctx.Text("Options", panel.x + 10, panel.y + 6, kColTitle);
    ctx.Text("X", close.x + 5, close.y + 2, kColText);
    ctx.Text("^", scrollUp.x + 5, scrollUp.y + 2, kColText);
    ctx.Text("v", scrollDown.x + 5, scrollDown.y + 2, kColText);

    for (int row = scroll_; row < kFieldCount; ++row) {
        Rect r;
        if (!RowRect(list, row, r)) break;
        const OptionsFieldDef& f = kFields[row];
        ctx.Text(f.label, r.x + 4, r.y + 4, kColText);

        int32_t v = config::g_Options.*(f.field);
        // REAL value text (StrTable005 005.DAT, cf. FieldValueText) — replaces raw
        // numeric display: reproduces what UI_OptionsWnd_Render 0x66EAC0 actually draws
        // (ON/OFF, LOW/MEDUIM/HIGH, Basic/Advanced, Phase N, etc.).
        const std::string valueText = FieldValueText(f, v);
        if (f.checkbox) {
            Rect cb = RowCheckboxRect(r);
            // State text to the left of the checkbox (space reserved in the row), the
            // checkbox itself remains the click affordance (v66EAC0 draws the text at a
            // fixed column; here it's placed against the checkbox to stay readable at the
            // port's reduced panel widths).
            const int vw = ctx.MeasureText(valueText.c_str());
            ctx.Text(valueText.c_str(), cb.x - vw - 6, r.y + 4, kColText);
            if (v != 0) {
                ctx.Text("X", cb.x + 3, cb.y + 1, kColCheck); // "cross if true"
            }
        } else if (IsSlider(f)) {
            // Numeric volume value, right of the track ("%d" fallback, OPT-04).
            Rect tr = RowSliderTrackRect(r);
            ctx.Text(valueText.c_str(), tr.x + tr.w + 6, r.y + 4, kColText);
        } else {
            Rect mr = RowMinusRect(r);
            Rect pr = RowPlusRect(r);
            ctx.Text("-", mr.x + 5, mr.y + 1, kColText);
            ctx.Text("+", pr.x + 4, pr.y + 1, kColText);
            const int vw = ctx.MeasureText(valueText.c_str());
            const int vx = mr.x + mr.w + ((pr.x - (mr.x + mr.w)) - vw) / 2;
            ctx.Text(valueText.c_str(), vx, r.y + 4, kColText);
        }
    }

    // Footer button labels (OPT-02).
    const char* logoutLbl = "Déconnexion";
    const int logoutLblW = ctx.MeasureText(logoutLbl);
    ctx.Text(logoutLbl, logout.x + (logout.w - logoutLblW) / 2, logout.y + 5, kColText);
    const char* exitLbl = "Quitter le jeu";
    const int exitLblW = ctx.MeasureText(exitLbl);
    ctx.Text(exitLbl, exitGame.x + (exitGame.w - exitLblW) / 2, exitGame.y + 5, kColText);
}

} // namespace ts2::ui

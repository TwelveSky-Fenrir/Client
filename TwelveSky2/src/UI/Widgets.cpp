// UI/Widgets.cpp — implementation of the base 2D widgets (ts2::ui).
// See UI/Widgets.h for the mapping to the disassembly (Docs/TS2_CLIENT_SHELL.md §2.2).
#include "UI/Widgets.h"

#include <cmath>   // std::fmod

namespace ts2::ui {

// ===========================================================================
// Internal helpers
// ===========================================================================
namespace {

// Emits a sprite blit via the 2D batch (SpriteBatch) already opened by the caller.
inline void BlitSprite(gfx::SpriteBatch& sb, const WidgetSprite& s, int x, int y) {
    if (s.Valid())
        sb.DrawSprite(s.tex, s.SrcPtr(), x, y, gfx::kSpriteWhite);
}

// Draw X position based on alignment within a rect of width w.
inline int AlignedX(gfx::Font& font, const std::string& text,
                    int x, int w, Align a) {
    if (a == Align::Left || w <= 0 || text.empty())
        return x;
    const int tw = font.MeasureText(text.c_str());
    if (a == Align::Center) return x + (w - tw) / 2;
    return x + (w - tw); // Right
}

// Vertical center of a text of height th within a rect of height h.
inline int CenteredY(int y, int h, int th) {
    return (h > th) ? y + (h - th) / 2 : y;
}

} // namespace

// ===========================================================================
// Label
// ===========================================================================
void Label::Draw(gfx::SpriteBatch& sb, gfx::Font& font) {
    (void)sb; // Label only draws text
    if (!visible_ || text_.empty())
        return;
    const int tx = AlignedX(font, text_, x_, w_, align_);
    const int ty = CenteredY(y_, h_, textH_);
    font.DrawTextStyled(text_.c_str(), tx, ty, color_, style_);
}

// ===========================================================================
// Button
// ===========================================================================
void Button::OnMouseMove(int mx, int my) {
    hover_active_ = enabled_ && HitTest(mx, my);
}

bool Button::OnMouseDown(int mx, int my) {
    if (!visible_ || !enabled_ || !HitTest(mx, my))
        return false;
    // Armed latch (faithful: btnPressed[i] set on down, cf. UI_MsgBox_OnLButtonDown).
    // NOTE: the UI click sound (flt_1487E3C via Snd3D_PlayScaledVolume) would play
    // here in the original — to be wired via the audio subsystem.
    armed_        = true;
    hover_active_ = true;
    return true;
}

bool Button::OnMouseUp(int mx, int my) {
    if (!armed_)
        return false;
    armed_ = false;
    // Confirmed on release INSIDE the button (faithful: *_OnLButtonUp).
    const bool inside = visible_ && enabled_ && HitTest(mx, my);
    if (inside && onClick_)
        onClick_();
    return inside;
}

void Button::DrawSkin(gfx::SpriteBatch& sb) const {
    if (!visible_)
        return;

    // Visual state selection: pressed > hover > normal (with fallback), and the
    // associated fallback color (used only if the chosen skin has no valid texture).
    const WidgetSprite* skin = &normal_;
    D3DCOLOR fallbackCol = fallbackNormal_;
    if (hover_active_ && hover_.Valid()) {
        skin = &hover_;
        fallbackCol = fallbackHover_;
    }
    if (armed_) {
        fallbackCol = fallbackPressed_; // pressed state: pressed fallback color even if
                                         // pressed_ has no texture (skin stays hover_/normal_ below).
        if (pressed_.Valid())
            skin = &pressed_;
    }

    if (skin->Valid()) {
        BlitSprite(sb, *skin, x_, y_);
    } else if (fallbackTex_ && w_ > 0 && h_ > 0) {
        // Colored rect fallback (identical to callers' old manual FillRect, e.g.
        // LoginScene::BtnColor + FillRect before this extension).
        sb.DrawSpriteScaled(fallbackTex_, nullptr, x_, y_,
                            static_cast<float>(w_), static_cast<float>(h_),
                            fallbackCol, /*compensatePos=*/true);
    }
    // Neither texture nor fallback configured (fallbackTex_ == nullptr): old
    // behavior — draw nothing. Doesn't break any existing caller that hasn't
    // adopted textures.
}

void Button::DrawLabel(gfx::Font& font) const {
    if (!visible_ || label_.empty())
        return;
    // Centered label; slight +1,+1 offset when the button is pressed.
    const int off = armed_ ? 1 : 0;
    const int tx  = AlignedX(font, label_, x_, w_, Align::Center) + off;
    const int ty  = CenteredY(y_, h_, textH_) + off;
    const D3DCOLOR col = enabled_ ? labelColor_ : kTextDisabled;
    font.DrawTextStyled(label_.c_str(), tx, ty, col, labelStyle_);
}

void Button::Draw(gfx::SpriteBatch& sb, gfx::Font& font) {
    DrawSkin(sb);
    DrawLabel(font);
}

// ===========================================================================
// EditBox
// ===========================================================================
void EditBox::SetText(const std::string& t) {
    text_ = t;
    if (text_.size() > maxLen_)
        text_.resize(maxLen_);
    caret_ = text_.size();
}

void EditBox::Clear() {
    text_.clear();
    caret_ = 0;
}

void EditBox::SetFocused(bool f) {
    focused_ = f;
    if (focused_ && caret_ > text_.size())
        caret_ = text_.size();
}

std::string EditBox::DisplayString() const {
    // Password mask (faithful: Crt_Memset(String, 42, len), 42 == '*').
    if (password_)
        return std::string(text_.size(), kPasswordMaskChar);
    return text_;
}

bool EditBox::OnMouseDown(int mx, int my) {
    if (!visible_ || !enabled_)
        return false;
    const bool inside = HitTest(mx, my);
    SetFocused(inside); // click inside -> focus; click outside -> unfocus
    return inside;
}

bool EditBox::OnChar(unsigned int ch) {
    if (!focused_ || !enabled_)
        return false;
    // Control characters (backspace, tab, enter...) handled in OnKey.
    if (ch < 0x20u || ch == 0x7Fu)
        return false;
    if (text_.size() >= maxLen_)
        return true; // consumed but field full (faithful: EM_LIMITTEXT)
    text_.insert(caret_, 1, static_cast<char>(ch));
    ++caret_;
    return true;
}

bool EditBox::OnKey(int vk) {
    if (!focused_ || !enabled_)
        return false;

    // CARET NAVIGATION SWALLOWED — UI_EditBoxWndProc 0x50E070, default branch
    // (def_50E0C4 @0x50E342): `cmp var_5C, 100h / jz loc_50E38E` @0x50e34e then
    //   loc_50E38E: `cmp var_60, 23h / jb loc_50E3A9`  @0x50e394-0x50e398
    //               `cmp var_60, 28h / jbe loc_50E3A2` @0x50e39a-0x50e39e
    //   loc_50E3A2: `mov eax, 1 / jmp loc_50E3C6`      @0x50e3a2-0x50e3a7
    // => on WM_KEYDOWN (0x100), any wParam in [0x23..0x28] returns 1 WITHOUT ever
    // reaching CallWindowProcA(lpPrevWndFunc, …) @0x50e3c0: the native EDITProc
    // NEVER sees these keys. Covers VK_END(0x23), VK_HOME(0x24), VK_LEFT(0x25),
    // VK_UP(0x26), VK_RIGHT(0x27), VK_DOWN(0x28).
    //
    // This branch is reached both by `default:` AND by the `goto LABEL_53` of ALL
    // the named cases (0,1,3,4,5,7,8,9,10,15) — since arrows are never wParam
    // 9/13, the filter applies to ALL subclassed boxes. Proven consequence: in
    // the binary the caret is PERMANENTLY at the end of the text and input is
    // append-only. Consumed, with no effect (gap UIFW-04).
    if (vk >= 0x23 && vk <= 0x28)
        return true;

    switch (vk) {
    case VK_BACK: // 0x08: not filtered by 0x50E070 -> forwarded to the native EDITProc.
        // caret_ is always text_.size() (see above): therefore ALWAYS removes
        // the last character.
        if (caret_ > 0) {
            text_.erase(caret_ - 1, 1);
            --caret_;
        }
        return true;
    case VK_DELETE: // 0x2E: not filtered (outside [0x23..0x28]) -> reaches the
        // native EDITProc, but since the caret is always at the end of the text,
        // this is a no-op IN PRACTICE. The guard below reproduces this without a
        // special case: caret_ == text_.size() => no deletion.
        if (caret_ < text_.size())
            text_.erase(caret_, 1);
        return true;
    case VK_RETURN: // 0x0D: submission (faithful: UI_Chat_SubmitInput / login)
        if (onSubmit_) onSubmit_();
        return true;
    case VK_TAB: // 0x09: next field (faithful: UI_FocusEditBox)
        if (onTab_) onTab_();
        return true;
    default:
        return false;
    }
}

void EditBox::Draw(gfx::SpriteBatch& sb, gfx::Font& font) {
    if (!visible_)
        return;

    // Optional background via the 2D batch.
    BlitSprite(sb, bg_, x_, y_);

    const int tx = x_ + padX_;
    const int ty = CenteredY(y_, h_, textH_);

    // Text (masked if password).
    const std::string disp = DisplayString();
    if (!disp.empty())
        font.DrawTextStyled(disp.c_str(), tx, ty, textColor_, textStyle_);

    // Caret: drawn when the field has focus (faithful: caret sprite if focused).
    // Optional blinking (requested) — can be disabled to stick to the binary.
    if (focused_) {
        bool showCaret = true;
        if (caretBlink_) {
            const float phase = std::fmod(gfx::g_GameTimeSec, kCaretBlinkPeriodSec);
            showCaret = phase < (kCaretBlinkPeriodSec * 0.5f);
        }
        if (showCaret) {
            // Prefix width (masked if password) -> caret position.
            const std::string prefix =
                password_ ? std::string(caret_, kPasswordMaskChar)
                          : text_.substr(0, caret_);
            const int cx = tx + (prefix.empty() ? 0 : font.MeasureText(prefix.c_str()));
            // Caret-style vertical bar (the binary blits sprite unk_8EA42C; here we
            // render a '|' glyph via the font, sufficient and legible).
            font.DrawTextStyled("|", cx - 1, ty, caretColor_, gfx::kStyleNormal);
        }
    }
}

} // namespace ts2::ui

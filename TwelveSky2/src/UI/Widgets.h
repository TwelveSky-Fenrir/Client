// UI/Widgets.h — reusable 2D widgets for the TwelveSky2 client shell (ts2::ui).
//
// PRAGMATIC reimplementation of TwelveSky2's "UI framework" foundation, drawn
// on top of the Gfx layer (SpriteBatch + Font). See Docs/TS2_CLIENT_SHELL.md §2.2.
//
// The original binary is NOT an object system: it is a static registry of
// ~38 singleton dialogs driven by free __thiscall functions, and text entry
// goes through 21 native Win32 EDIT controls subclassed onto
// UI_EditBoxWndProc (0x50E070). Here we rebuild SELF-CONTAINED widgets that
// draw themselves (no native EDIT), while staying faithful to observed
// behaviors:
//   - password mask = '*' character (0x2A / 42), cf. Scene_LoginRender
//     0x51B020: Crt_Memset(String, 42, len) before drawing.
//   - caret drawn only when the field has focus (sprite unk_8EA42C).
//     The original does NOT blink; we add optional blinking (requested),
//     disableable via SetCaretBlink(false) to stay faithful to the binary.
//   - Tab navigation -> next field, Enter -> contextual submission
//     (UI_EditBoxWndProc: Tab=UI_FocusEditBox, Enter=UI_Chat_SubmitInput...).
//   - buttons: latch armed on mouse-down, validated on mouse-up
//     (btnPressed[] pattern of the dialogs, cf. UI_MsgBox_OnLButtonDown/Up).
//
// Draw contract: Draw(SpriteBatch&, Font&) assumes BOTH batches are already
// OPEN by the caller (SpriteBatch::Begin() and Font::BeginBatch()), because
// ID3DXSprite and ID3DXFont each carry their own batch. Widgets don't
// manage opening/closing — they only emit their primitives.
#pragma once
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"

#include <windows.h>   // RECT, VK_*
#include <d3d9.h>      // IDirect3DTexture9, D3DCOLOR
#include <cstdint>
#include <string>
#include <functional>

namespace ts2::ui {

// Default colors (ARGB) — opaque white and "disabled" gray.
inline constexpr D3DCOLOR kTextWhite    = 0xFFFFFFFFu;
inline constexpr D3DCOLOR kTextDisabled = 0xFF808080u;

// Password mask character (faithful: Crt_Memset(buf, 42, len)).
inline constexpr char kPasswordMaskChar = '*'; // 0x2A / 42

// Caret blink period (seconds): 0.5 s on / 0.5 s off.
inline constexpr float kCaretBlinkPeriodSec = 1.0f;

// Input limits of the 21 native EDIT boxes — UI_CreateEditBoxes 0x50E460.
// Recorded one by one from the operands of `SendMessageA(h, 0xC5 /*EM_LIMITTEXT*/,
// <limit>, 0)` in the switch @0x50EDDD (jumptable jpt_50EDDD), each case chaining
// into `SetWindowLongA(h, -4 /*GWL_WNDPROC*/, UI_EditBoxWndProc)`:
//   case 0  @0x50EDE4=0x7F   case 1  @0x50EE31=0x7F   case 2  @0x50EE7E=0x0C
//   case 3  @0x50EECB=0x0C   case 4  @0x50EF18=0x3C   case 5  @0x50EF65=0x0C
//   case 6  @0x50EFB2=0x0C   case 7  @0x50EFFF=0x3C   case 8  @0x50F04C=0x32
//   case 9  @0x50F099=0x32   case 10 @0x50F0E6=0x32   case 11 @0x50F133=0x32
//   case 12 @0x50F180=0x04   case 13 @0x50F1CD=0x18   case 14 @0x50F21A=0x20
//   case 15 @0x50F267=0x3C   case 16 @0x50F2B4=0x0C   case 17 @0x50F301=0x0C
//   case 18 @0x50F34E=0x30   case 19 @0x50F395=0x18
// 0-based HWND index (manager index = HWND index + 1, cf. UI_FocusEditBox 0x50F4A0).
//
// The jumptable has ONLY 20 targets: there is NO `case 20`. The 21st box falls
// through to `default: LABEL_2` (def_50EDDD @0x50F3DA -> 0x50ED00): no EM_LIMITTEXT,
// no SetWindowLongA either — it is NOT subclassed onto UI_EditBoxWndProc and keeps
// NATIVE Tab/Enter/arrows/paste (unlike the other 20, cf. the [0x23..0x28] filter
// reproduced in EditBox::OnKey). Hence the sentinel 0 at [20].
//
// TODO [anchor 0x50EDDD] — WIRING OUTSIDE THIS FILE (gap UIFW-05). The only
// callers of SetMaxLength are LoginScene.cpp:108 (0x7F = kEditLimit[0] ✓),
// LoginScene.cpp:109 (0x7F = kEditLimit[1] ✓) and LoginScene.cpp:147 (12 = 0x0C =
// kEditLimit[5] ✓) — values already correct, but hardcoded: should route
// through kEditLimit[] for a single source of truth. Only ONE DIVERGENCE in
// value remains, out of scope for this front: GuildWindow.cpp:46
// `nameEdit_.SetMaxLength(kNameStride-1)` = 12, while box 7 — the one for
// Guild_AddMemberFromInput 0x66BCD0, cf. UI_EditBoxWndProc case 7 @0x50E24B — is
// 0x3C = 60 (@0x50EFFF). Fix: `SetMaxLength(kEditLimit[7])`, keeping the
// truncation to kNameStride-1 AT NETWORK SEND TIME, not at keystroke.
inline constexpr size_t kEditLimit[21] = {
    0x7F, 0x7F, 0x0C, 0x0C, 0x3C, 0x0C, 0x0C, 0x3C, 0x32, 0x32, 0x32,
    0x32, 0x04, 0x18, 0x20, 0x3C, 0x0C, 0x0C, 0x30, 0x18,
    0 // [20]: no case in the jumptable -> neither limit nor subclassing
};

// Horizontal text alignment of a Label.
enum class Align { Left, Center, Right };

// WidgetSprite — NON-OWNING reference to a GPU texture + optional
// sub-rectangle. The texture belongs to an atlas/skin loaded elsewhere (e.g.
// gfx::GpuTexture from a .IMG G03_GDATA); only the raw handle is stored.
//   Usage: ui::WidgetSprite{ gpuTex.Handle() }               // full texture
//          ui::WidgetSprite{ gpuTex.Handle(), RECT{l,t,r,b} } // sub-region
struct WidgetSprite {
    IDirect3DTexture9* tex    = nullptr; // non-owned handle (GpuTexture::Handle())
    RECT               src    {};        // source sub-rectangle (if useSrc)
    bool               useSrc = false;   // false -> blit the full texture

    WidgetSprite() = default;
    WidgetSprite(IDirect3DTexture9* t) : tex(t) {}                 // full texture
    WidgetSprite(IDirect3DTexture9* t, const RECT& r)             // sub-region
        : tex(t), src(r), useSrc(true) {}

    bool          Valid() const { return tex != nullptr; }
    const RECT*   SrcPtr() const { return useSrc ? &src : nullptr; }
};

// Widget — common base: screen rect, visibility, enabled state, hit-test.
// (The binary keeps x/y/bOpen at the head of each dialog struct; formalized here.)
class Widget {
public:
    virtual ~Widget() = default;

    void SetBounds(int x, int y, int w, int h) { x_ = x; y_ = y; w_ = w; h_ = h; }
    void SetPos(int x, int y)                  { x_ = x; y_ = y; }
    void SetSize(int w, int h)                 { w_ = w; h_ = h; }

    int  X() const { return x_; }
    int  Y() const { return y_; }
    int  W() const { return w_; }
    int  H() const { return h_; }

    void SetVisible(bool v) { visible_ = v; }
    bool Visible() const    { return visible_; }
    void SetEnabled(bool e) { enabled_ = e; }
    bool Enabled() const    { return enabled_; }

    // Point inside the widget's rect (visible only). Faithful to
    // Sprite2D_HitTest 0x4D6C50: tests sprite position + dimensions.
    bool HitTest(int mx, int my) const {
        return visible_ && mx >= x_ && mx < x_ + w_ && my >= y_ && my < y_ + h_;
    }

protected:
    int  x_ = 0, y_ = 0, w_ = 0, h_ = 0;
    bool visible_ = true;
    bool enabled_ = true;
};

// Label — static text drawn via Font (normal/shadow/outline style).
// Aligned within the widget's rect (Left/Center/Right); vertically centered
// if a height is provided. Corresponds to the StrTable005 labels of the dialogs.
class Label : public Widget {
public:
    Label() = default;
    explicit Label(std::string text) : text_(std::move(text)) {}

    void SetText(std::string t)        { text_ = std::move(t); }
    const std::string& Text() const    { return text_; }

    void SetColor(D3DCOLOR c)          { color_ = c; }
    void SetStyle(int mode)            { style_ = mode; } // gfx::kStyle*
    void SetAlign(Align a)             { align_ = a; }
    void SetTextHeight(int h)          { textH_ = h; }

    // sb: unused (Label only draws text) — present for consistency
    // with the Draw(SpriteBatch&, Font&) contract.
    void Draw(gfx::SpriteBatch& sb, gfx::Font& font);

private:
    std::string text_;
    D3DCOLOR    color_ = kTextWhite;
    int         style_ = gfx::kStyleNormal;
    Align       align_ = Align::Left;
    int         textH_ = 12; // default font height (App_Init: Height=12)
};

// Button — 3-state sprited button (normal/hover/pressed) + optional label.
// Interaction model faithful to the dialogs: latch armed on mouse-down
// (OnMouseDown), action fired on mouse-up INSIDE the button (OnMouseUp).
class Button : public Widget {
public:
    void SetSkin(const WidgetSprite& normal,
                 const WidgetSprite& hover,
                 const WidgetSprite& pressed) {
        normal_ = normal; hover_ = hover; pressed_ = pressed;
    }
    void SetNormal(const WidgetSprite& s)  { normal_ = s; }
    void SetHover(const WidgetSprite& s)   { hover_ = s; }
    void SetPressed(const WidgetSprite& s) { pressed_ = s; }

    void SetLabel(std::string t)        { label_ = std::move(t); }
    const std::string& Label() const    { return label_; }
    void SetLabelColor(D3DCOLOR c)      { labelColor_ = c; }
    void SetLabelStyle(int mode)        { labelStyle_ = mode; }
    void SetTextHeight(int h)           { textH_ = h; }

    // Callback fired when the button is validated (full click inside).
    void SetOnClick(std::function<void()> cb) { onClick_ = std::move(cb); }

    // Mouse events. Return true if the event concerns the button.
    void OnMouseMove(int mx, int my);
    bool OnMouseDown(int mx, int my); // arms the latch if inside the button
    bool OnMouseUp(int mx, int my);   // validates (fires onClick) if released inside

    bool Hovered() const { return hover_active_; }
    bool Pressed() const { return armed_; }

    // Indicates whether THIS BUTTON was wired with AT LEAST ONE real texture (normal/hover/
    // pressed), regardless of the CURRENT visual state. Distinct from an older test "does the
    // currently displayed state have a texture" (renamed/re-fixed, Login audit
    // 2026-07-14, Docs/TS2_LOGINSCENE_AUDIT.md §4): since the fidelity fix of
    // Scene_LoginRender 0x51B020 (EA 0x51B48D/0x51B50F — the sprite unk_8E9084/unk_8E9240
    // is drawn ONLY when the mouse hovers the button; there is NO idle "normal" sprite,
    // cf. ApplyConfirmCancelSkin which now only wires Hover+Pressed), okBtn_/
    // exitBtn_ have NO texture at all as long as they are neither hovered nor pressed —
    // testing the current state would have wrongly brought back the fallback label
    // ("OK"/"Quitter") as soon as the mouse leaves the button, whereas the binary shows
    // neither sprite nor text in that case (the real label, if any, is painted on the
    // panel itself). The fallback label must appear ONLY when NONE of the 3
    // textures could be loaded (the .IMG file is genuinely absent/unreadable).
    bool HasAnySkin() const {
        return normal_.Valid() || hover_.Valid() || pressed_.Valid();
    }

    // Clears the pressed latch + hover state (faithful to the generic field reset of
    // cSceneMgr done by scenes on entering an Init sub-state, e.g. Scene_LoginUpdate
    // 0x51A8D0 case 0: `for(i=0;i<150;++i) a1[i+3]=0;` covers among others the 3 button
    // latches this[3..5]). To be called explicitly by the scene owning the button
    // at its own reset time; this widget never resets itself.
    void Reset() { armed_ = false; hover_active_ = false; }

    // ---- Color fallback (solid rect), used ONLY when the skin (normal_/hover_/
    // pressed_) selected for the current state is invalid (no texture provided via
    // SetNormal/SetHover/SetPressed, or the .IMG file is genuinely absent/unreadable at
    // runtime). Default behavior (fallbackTex_ == nullptr): STRICTLY identical
    // to the old Draw() (no rect drawn if the skin is invalid) — does NOT affect ANY
    // existing caller unless it explicitly calls these setters. Cf.
    // Docs/TS2_LOGIN_BUTTON_ASSETS.md (LoginScene::okBtn_/exitBtn_, real textures with
    // fallback to the already-in-place colored rect).
    void SetFallbackColors(D3DCOLOR normalCol, D3DCOLOR hoverCol, D3DCOLOR pressedCol) {
        fallbackNormal_ = normalCol; fallbackHover_ = hoverCol; fallbackPressed_ = pressedCol;
    }
    // 1x1 white texture used to draw the fallback (Button does not own its own
    // texture: the caller supplies one already created for its own use, e.g.
    // LoginScene::whiteTex_ / InventoryWindow::whiteTex_ — same technique as FillRect).
    void SetFallbackTexture(IDirect3DTexture9* whiteTex) { fallbackTex_ = whiteTex; }

    // Draws ONLY the sprite/rect (no text) — for callers that already separate
    // sprite rendering (Panels phase, sprites_.Begin()/End()) from text rendering (Text
    // phase, font.BeginBatch()/EndBatch()), like LoginScene. Opens/closes NO batch: call
    // it between a SpriteBatch::Begin() and its End().
    void DrawSkin(gfx::SpriteBatch& sb) const;
    // Draws ONLY the label — call it in the same text pass as the rest
    // (between Font::BeginBatch()/EndBatch()).
    void DrawLabel(gfx::Font& font) const;

    // Combined: DrawSkin() then DrawLabel(). Draw contract: assumes BOTH batches
    // are already OPEN by the caller (SpriteBatch::Begin() AND Font::BeginBatch()), since
    // ID3DXSprite and ID3DXFont each carry their own batch (cf. file header comment)
    // — callers with separate passes (e.g. LoginScene) should use
    // DrawSkin()/DrawLabel() individually instead of this combined variant.
    void Draw(gfx::SpriteBatch& sb, gfx::Font& font);

private:
    WidgetSprite normal_, hover_, pressed_;
    std::string  label_;
    D3DCOLOR     labelColor_ = kTextWhite;
    int          labelStyle_ = gfx::kStyleNormal;
    int          textH_      = 12;
    bool         hover_active_ = false; // current hover state
    bool         armed_        = false; // pressed (latch)
    std::function<void()> onClick_;

    // Colored rect fallback (cf. SetFallbackColors/SetFallbackTexture above).
    D3DCOLOR            fallbackNormal_  = 0;
    D3DCOLOR            fallbackHover_   = 0;
    D3DCOLOR            fallbackPressed_ = 0;
    IDirect3DTexture9*  fallbackTex_     = nullptr; // non-owned
};

// EditBox — self-contained input field (no native Win32 EDIT). Handles character
// insertion, backspace, password mask ('*'), blinking caret, submission
// (Enter) and next field (Tab).
// Faithful to UI_EditBoxWndProc 0x50E070 (Tab/Enter) and Scene_LoginRender 0x51B020
// (mask '*', caret after the text when focused).
//
// NO caret navigation, by design (gap UIFW-04): UI_EditBoxWndProc swallows
// WM_KEYDOWN for any wParam in [0x23..0x28] (@0x50E394-0x50E3A7) without ever
// reaching CallWindowProcA @0x50E3C0 — VK_END/HOME/LEFT/UP/RIGHT/DOWN therefore
// NEVER move the caret in the binary. Resulting invariant, relied upon by the
// implementation: `caret_ == text_.size()` at all times (SetText l.133
// and OnChar l.170-171 maintain it, nothing else moves it) — input is
// append-only, VK_DELETE is effectively a no-op, and VK_BACK deletes the last
// character. Two other filters on the same branch (not modeled here, since
// there's no native EDIT): WM_CONTEXTMENU 0x7B -> `return 1` @0x50E34C (no
// context menu) and WM_PASTE 0x302 -> `return 1` if g_GmAuthLevel (0x1669294) < 1 and
// index != 14 @0x50E369-0x50E378 (paste reserved for GM, except box 14).
class EditBox : public Widget {
public:
    void SetBackground(const WidgetSprite& bg) { bg_ = bg; }

    void SetText(const std::string& t);
    const std::string& Text() const { return text_; }
    void Clear();

    // Max length (faithful: EM_LIMITTEXT @0x50EDDD). Pass kEditLimit[<index of
    // the original box>] rather than a literal — cf. the kEditLimit block.
    void SetMaxLength(size_t n) { maxLen_ = n; }
    size_t MaxLength() const    { return maxLen_; }

    void SetPassword(bool p)    { password_ = p; }
    bool IsPassword() const     { return password_; }

    void SetFocused(bool f);
    bool Focused() const        { return focused_; }

    void SetTextColor(D3DCOLOR c)  { textColor_ = c; }
    void SetCaretColor(D3DCOLOR c) { caretColor_ = c; }
    void SetTextStyle(int mode)    { textStyle_ = mode; }
    void SetCaretBlink(bool b)     { caretBlink_ = b; }
    void SetPadding(int px)        { padX_ = px; }
    void SetTextHeight(int h)      { textH_ = h; }

    // Callbacks: Enter (submission) and Tab (next field).
    void SetOnSubmit(std::function<void()> cb) { onSubmit_ = std::move(cb); }
    void SetOnTab(std::function<void()> cb)    { onTab_ = std::move(cb); }

    // Mouse: focuses the field on click inside (defocus otherwise via SetFocused).
    bool OnMouseDown(int mx, int my);

    // Keyboard. OnChar: printable character (WM_CHAR). OnKey: virtual key
    // (WM_KEYDOWN) — editing/navigation/submission. Return true if consumed.
    bool OnChar(unsigned int ch);
    bool OnKey(int vk);

    void Draw(gfx::SpriteBatch& sb, gfx::Font& font);

private:
    // Displayed string: masked with '*' if password, raw text otherwise.
    std::string DisplayString() const;

    WidgetSprite bg_;
    std::string  text_;
    size_t       maxLen_    = 0x7F;  // 127 (login/pw)
    size_t       caret_     = 0;     // insertion index [0..text_.size()]
    bool         password_  = false;
    bool         focused_   = false;
    bool         caretBlink_= true;
    D3DCOLOR     textColor_ = kTextWhite;
    D3DCOLOR     caretColor_= kTextWhite;
    int          textStyle_ = gfx::kStyleNormal;
    int          padX_      = 4;
    int          textH_     = 12;
    std::function<void()> onSubmit_;
    std::function<void()> onTab_;
};

} // namespace ts2::ui

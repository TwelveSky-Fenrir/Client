// UI/ConfirmMsgBox.cpp — see header banner. Port of UI_MsgBox_* (dword_1822438).
#include "UI/ConfirmMsgBox.h"

namespace ts2::ui {

// Element offsets (relative to the panel's top-left corner) — UI_MsgBox_Render 0x5C3100.
namespace {
constexpr int kOkX     = 165; // OK     @0x5C35F5
constexpr int kCancelX = 241; // Cancel @0x5C368D
constexpr int kBtnY    = 90;  // row of the 2 buttons
constexpr int kTitleX  = 234; // title center @0x5C31B2
constexpr int kTitleY  = 42;  // title when body is empty @0x5C31AC
}

void ConfirmMsgBox::Open(std::string title, int32_t actionType, OkFn onOk) {
    open_  = true;
    title_ = std::move(title);
    type_  = actionType;
    onOk_  = std::move(onOk);
    btnPressed_[0] = btnPressed_[1] = false;
}

void ConfirmMsgBox::Close() {
    open_ = false;
    btnPressed_[0] = btnPressed_[1] = false;
    onOk_ = nullptr;
    title_.clear();
}

void ConfirmMsgBox::Recenter(const SpriteProvider& sprite, int screenW, int screenH) {
    gfx::GpuTexture* panel = sprite ? sprite(7) : nullptr;
    const int pw = panel ? static_cast<int>(panel->Width())  : 0; // 0x5C313B
    const int ph = panel ? static_cast<int>(panel->Height()) : 0; // 0x5C3160
    panelX_ = screenW / 2 - pw / 2;
    panelY_ = screenH / 2 - ph / 2;
}

// Sprite2D_HitTest 0x4D6C50: bounds >= left/top and < right/bottom, native .IMG dims.
bool ConfirmMsgBox::HitSprite(gfx::GpuTexture* t, int x, int y, int mx, int my) {
    if (!t || t->Width() <= 0 || t->Height() <= 0) return false;
    const int w = static_cast<int>(t->Width()), h = static_cast<int>(t->Height());
    return mx >= x && mx < x + w && my >= y && my < y + h;
}

void ConfirmMsgBox::Render(const SpriteProvider& sprite, gfx::SpriteBatch& sb, gfx::Font& font,
                           int screenW, int screenH, POINT cursor, D3DCOLOR textColor) {
    if (!open_) return;
    Recenter(sprite, screenW, screenH);

    if (sb.Ready()) {
        sb.Begin();
        if (gfx::GpuTexture* panel = sprite(7))
            sb.DrawSprite(panel->Handle(), nullptr, panelX_, panelY_, gfx::kSpriteWhite);
        // 3-state pattern (hit-test ALWAYS against the IDLE slot) — OK (base 8) and Cancel (base 11).
        auto drawBtn = [&](int idleSlot, int bx, bool pressed) {
            int slot = idleSlot;
            if (pressed)                                                          slot = idleSlot + 2;
            else if (HitSprite(sprite(idleSlot), bx, panelY_ + kBtnY, cursor.x, cursor.y)) slot = idleSlot + 1;
            if (gfx::GpuTexture* t = sprite(slot))
                sb.DrawSprite(t->Handle(), nullptr, bx, panelY_ + kBtnY, gfx::kSpriteWhite);
        };
        drawBtn(8,  panelX_ + kOkX,     btnPressed_[0]); // OK 8/9/10 @0x5C35F5
        drawBtn(11, panelX_ + kCancelX, btnPressed_[1]); // Cancel 11/12/13 @0x5C368D
        sb.End();
    }

    if (font.Ready() && !title_.empty()) {
        font.BeginBatch();
        const int tx = panelX_ + kTitleX - font.MeasureText(title_.c_str()) / 2; // centered @0x5C31B2
        font.DrawTextAt(title_.c_str(), tx, panelY_ + kTitleY, textColor);        // empty body @0x5C31AC
        font.EndBatch();
    }
}

bool ConfirmMsgBox::OnMouseDown(const SpriteProvider& sprite, int x, int y, int screenW, int screenH) {
    if (!open_) return false;
    Recenter(sprite, screenW, screenH); // the binary recenters on every click (0x5C0980)
    if (HitSprite(sprite(8), panelX_ + kOkX, panelY_ + kBtnY, x, y))           btnPressed_[0] = true;
    else if (HitSprite(sprite(11), panelX_ + kCancelX, panelY_ + kBtnY, x, y)) btnPressed_[1] = true;
    return true; // modal: consumes every click
}

bool ConfirmMsgBox::OnMouseUp(const SpriteProvider& sprite, int x, int y, int screenW, int screenH) {
    if (!open_) return false;
    Recenter(sprite, screenW, screenH);
    // LATCH GUARD (fidelity, UI_MsgBox_OnLButtonUp 0x5C0A90): the action fires ONLY if the
    // latch armed on mouse-down is set. The binary tests `cmp [this+0Ch],0 ; jz` @0x5C0B56 (OK =
    // btnPressed_[0]) and `cmp [this+10h],0 ; jz` @0x5C2D2D (Cancel = btnPressed_[1]), clearing the
    // latch BEFORE the hit-test (@0x5C0B66 / @0x5C2D3D). Without this guard, an up-on-OK WITHOUT a
    // down-on-OK (down outside the button, or dragged from Cancel) would trigger deletion/quit —
    // an accidental confirmation. The OK branch RETURNS before testing Cancel (modal stays open
    // if the OK latch was set but released outside the button).
    if (btnPressed_[0]) {                          // OK guard @0x5C0B56 (this+0xC)
        btnPressed_[0] = false;                    //          @0x5C0B66 (cleared BEFORE hit-test)
        if (HitSprite(sprite(8), panelX_ + kOkX, panelY_ + kBtnY, x, y)) {
            OkFn cb = onOk_; Close(); if (cb) cb(); // OK hit -> Close (0x5C0960) then action (case 1/2)
        }
        return true;                               // OK latched, released outside button -> stays open
    }
    if (btnPressed_[1]) {                          // Cancel guard @0x5C2D2D (this+0x10)
        btnPressed_[1] = false;                    //              @0x5C2D3D
        if (HitSprite(sprite(11), panelX_ + kCancelX, panelY_ + kBtnY, x, y))
            Close();                               // Cancel = plain close (switch default)
        return true;
    }
    return true; // neither OK nor Cancel latched (loc_5C2E93): modal stays open, click consumed
}

} // namespace ts2::ui

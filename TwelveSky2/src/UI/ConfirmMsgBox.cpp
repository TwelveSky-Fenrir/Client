// UI/ConfirmMsgBox.cpp — cf. bandeau du header. Port de UI_MsgBox_* (dword_1822438).
#include "UI/ConfirmMsgBox.h"

namespace ts2::ui {

// Offsets des éléments (relatifs au coin haut-gauche du panneau) — UI_MsgBox_Render 0x5C3100.
namespace {
constexpr int kOkX     = 165; // OK      @0x5C35F5
constexpr int kCancelX = 241; // Annuler @0x5C368D
constexpr int kBtnY    = 90;  // ligne des 2 boutons
constexpr int kTitleX  = 234; // centre du titre @0x5C31B2
constexpr int kTitleY  = 42;  // titre quand corps vide @0x5C31AC
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

// Sprite2D_HitTest 0x4D6C50 : bornes >= gauche/haut et < droite/bas, dims natives du .IMG.
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
        // Motif 3 états (hit-test TOUJOURS sur le slot IDLE) — OK (base 8) et Annuler (base 11).
        auto drawBtn = [&](int idleSlot, int bx, bool pressed) {
            int slot = idleSlot;
            if (pressed)                                                          slot = idleSlot + 2;
            else if (HitSprite(sprite(idleSlot), bx, panelY_ + kBtnY, cursor.x, cursor.y)) slot = idleSlot + 1;
            if (gfx::GpuTexture* t = sprite(slot))
                sb.DrawSprite(t->Handle(), nullptr, bx, panelY_ + kBtnY, gfx::kSpriteWhite);
        };
        drawBtn(8,  panelX_ + kOkX,     btnPressed_[0]); // OK 8/9/10 @0x5C35F5
        drawBtn(11, panelX_ + kCancelX, btnPressed_[1]); // Annuler 11/12/13 @0x5C368D
        sb.End();
    }

    if (font.Ready() && !title_.empty()) {
        font.BeginBatch();
        const int tx = panelX_ + kTitleX - font.MeasureText(title_.c_str()) / 2; // centré @0x5C31B2
        font.DrawTextAt(title_.c_str(), tx, panelY_ + kTitleY, textColor);        // corps vide @0x5C31AC
        font.EndBatch();
    }
}

bool ConfirmMsgBox::OnMouseDown(const SpriteProvider& sprite, int x, int y, int screenW, int screenH) {
    if (!open_) return false;
    Recenter(sprite, screenW, screenH); // le binaire recentre à chaque clic (0x5C0980)
    if (HitSprite(sprite(8), panelX_ + kOkX, panelY_ + kBtnY, x, y))           btnPressed_[0] = true;
    else if (HitSprite(sprite(11), panelX_ + kCancelX, panelY_ + kBtnY, x, y)) btnPressed_[1] = true;
    return true; // modal : consomme tout clic
}

bool ConfirmMsgBox::OnMouseUp(const SpriteProvider& sprite, int x, int y, int screenW, int screenH) {
    if (!open_) return false;
    Recenter(sprite, screenW, screenH);
    const bool okHit     = HitSprite(sprite(8),  panelX_ + kOkX,     panelY_ + kBtnY, x, y);
    const bool cancelHit = HitSprite(sprite(11), panelX_ + kCancelX, panelY_ + kBtnY, x, y);
    if (okHit) {
        OkFn cb = onOk_;   // capturer AVANT Close() (qui vide onOk_)
        Close();           // UI_ConfirmPrompt_Close 0x5C0960 (ferme d'abord)
        if (cb) cb();      // puis switch(type) = action OK (UI_MsgBox_OnLButtonUp 0x5C0A90)
    } else if (cancelHit) {
        Close();           // Annuler = fermeture sèche (aucun case)
    } else {
        btnPressed_[0] = btnPressed_[1] = false; // relâché hors bouton : dé-presse (reste ouvert)
    }
    return true; // modal : consomme tout clic
}

} // namespace ts2::ui

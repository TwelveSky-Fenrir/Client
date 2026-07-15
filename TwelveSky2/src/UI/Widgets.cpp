// UI/Widgets.cpp — implémentation des widgets 2D de base (ts2::ui).
// Voir UI/Widgets.h pour le mapping vers le désassemblage (Docs/TS2_CLIENT_SHELL.md §2.2).
#include "UI/Widgets.h"

#include <cmath>   // std::fmod

namespace ts2::ui {

// ===========================================================================
// Helpers internes
// ===========================================================================
namespace {

// Émet un blit sprite via le lot 2D (SpriteBatch) déjà ouvert par l'appelant.
inline void BlitSprite(gfx::SpriteBatch& sb, const WidgetSprite& s, int x, int y) {
    if (s.Valid())
        sb.DrawSprite(s.tex, s.SrcPtr(), x, y, gfx::kSpriteWhite);
}

// Position X de dessin selon l'alignement dans un rectangle de largeur w.
inline int AlignedX(gfx::Font& font, const std::string& text,
                    int x, int w, Align a) {
    if (a == Align::Left || w <= 0 || text.empty())
        return x;
    const int tw = font.MeasureText(text.c_str());
    if (a == Align::Center) return x + (w - tw) / 2;
    return x + (w - tw); // Right
}

// Centre vertical d'un texte de hauteur th dans un rectangle de hauteur h.
inline int CenteredY(int y, int h, int th) {
    return (h > th) ? y + (h - th) / 2 : y;
}

} // namespace

// ===========================================================================
// Label
// ===========================================================================
void Label::Draw(gfx::SpriteBatch& sb, gfx::Font& font) {
    (void)sb; // le Label ne dessine que du texte
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
    // Latch armé (fidèle : btnPressed[i] posé au down, cf. UI_MsgBox_OnLButtonDown).
    // NOTE : le son de clic UI (flt_1487E3C via Snd3D_PlayScaledVolume) serait joué
    // ici dans l'original — à brancher via le sous-système audio.
    armed_        = true;
    hover_active_ = true;
    return true;
}

bool Button::OnMouseUp(int mx, int my) {
    if (!armed_)
        return false;
    armed_ = false;
    // Validation au relâchement DANS le bouton (fidèle : *_OnLButtonUp).
    const bool inside = visible_ && enabled_ && HitTest(mx, my);
    if (inside && onClick_)
        onClick_();
    return inside;
}

void Button::DrawSkin(gfx::SpriteBatch& sb) const {
    if (!visible_)
        return;

    // Sélection de l'état visuel : pressé > survol > normal (avec repli), et couleur de
    // repli associée (utilisée seulement si le skin retenu est sans texture valide).
    const WidgetSprite* skin = &normal_;
    D3DCOLOR fallbackCol = fallbackNormal_;
    if (hover_active_ && hover_.Valid()) {
        skin = &hover_;
        fallbackCol = fallbackHover_;
    }
    if (armed_) {
        fallbackCol = fallbackPressed_; // état pressé : repli couleur pressé même si pressed_
                                         // est sans texture (skin reste hover_/normal_ ci-dessous).
        if (pressed_.Valid())
            skin = &pressed_;
    }

    if (skin->Valid()) {
        BlitSprite(sb, *skin, x_, y_);
    } else if (fallbackTex_ && w_ > 0 && h_ > 0) {
        // Repli rect coloré (identique à l'ancien FillRect manuel des appelants, ex.
        // LoginScene::BtnColor + FillRect avant cette extension).
        sb.DrawSpriteScaled(fallbackTex_, nullptr, x_, y_,
                            static_cast<float>(w_), static_cast<float>(h_),
                            fallbackCol, /*compensatePos=*/true);
    }
    // Ni texture ni repli configuré (fallbackTex_ == nullptr) : ancien comportement — ne
    // rien dessiner. Ne casse aucun appelant existant qui n'a pas adopté les textures.
}

void Button::DrawLabel(gfx::Font& font) const {
    if (!visible_ || label_.empty())
        return;
    // Libellé centré ; léger décalage +1,+1 quand le bouton est enfoncé.
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
    // Masque mot de passe (fidèle : Crt_Memset(String, 42, len), 42 == '*').
    if (password_)
        return std::string(text_.size(), kPasswordMaskChar);
    return text_;
}

bool EditBox::OnMouseDown(int mx, int my) {
    if (!visible_ || !enabled_)
        return false;
    const bool inside = HitTest(mx, my);
    SetFocused(inside); // clic dedans -> focus ; clic dehors -> défocus
    return inside;
}

bool EditBox::OnChar(unsigned int ch) {
    if (!focused_ || !enabled_)
        return false;
    // Caractères de contrôle (backspace, tab, entrée...) gérés dans OnKey.
    if (ch < 0x20u || ch == 0x7Fu)
        return false;
    if (text_.size() >= maxLen_)
        return true; // consommé mais champ plein (fidèle : EM_LIMITTEXT)
    text_.insert(caret_, 1, static_cast<char>(ch));
    ++caret_;
    return true;
}

bool EditBox::OnKey(int vk) {
    if (!focused_ || !enabled_)
        return false;
    switch (vk) {
    case VK_BACK: // 0x08 : suppression avant le caret
        if (caret_ > 0) {
            text_.erase(caret_ - 1, 1);
            --caret_;
        }
        return true;
    case VK_DELETE: // 0x2E : suppression sous le caret
        if (caret_ < text_.size())
            text_.erase(caret_, 1);
        return true;
    case VK_LEFT: // 0x25
        if (caret_ > 0) --caret_;
        return true;
    case VK_RIGHT: // 0x27
        if (caret_ < text_.size()) ++caret_;
        return true;
    case VK_HOME: // 0x24
        caret_ = 0;
        return true;
    case VK_END: // 0x23
        caret_ = text_.size();
        return true;
    case VK_RETURN: // 0x0D : soumission (fidèle : UI_Chat_SubmitInput / login)
        if (onSubmit_) onSubmit_();
        return true;
    case VK_TAB: // 0x09 : champ suivant (fidèle : UI_FocusEditBox)
        if (onTab_) onTab_();
        return true;
    default:
        return false;
    }
}

void EditBox::Draw(gfx::SpriteBatch& sb, gfx::Font& font) {
    if (!visible_)
        return;

    // Fond optionnel via le lot 2D.
    BlitSprite(sb, bg_, x_, y_);

    const int tx = x_ + padX_;
    const int ty = CenteredY(y_, h_, textH_);

    // Texte (masqué si mot de passe).
    const std::string disp = DisplayString();
    if (!disp.empty())
        font.DrawTextStyled(disp.c_str(), tx, ty, textColor_, textStyle_);

    // Caret : dessiné quand le champ a le focus (fidèle : sprite caret si focus).
    // Clignotement optionnel (demandé) — désactivable pour coller au binaire.
    if (focused_) {
        bool showCaret = true;
        if (caretBlink_) {
            const float phase = std::fmod(gfx::g_GameTimeSec, kCaretBlinkPeriodSec);
            showCaret = phase < (kCaretBlinkPeriodSec * 0.5f);
        }
        if (showCaret) {
            // Largeur du préfixe (masqué si mot de passe) -> position du caret.
            const std::string prefix =
                password_ ? std::string(caret_, kPasswordMaskChar)
                          : text_.substr(0, caret_);
            const int cx = tx + (prefix.empty() ? 0 : font.MeasureText(prefix.c_str()));
            // Trait vertical façon caret (le binaire blitte le sprite unk_8EA42C ;
            // ici on rend un glyphe '|' via la police, suffisant et lisible).
            font.DrawTextStyled("|", cx - 1, ty, caretColor_, gfx::kStyleNormal);
        }
    }
}

} // namespace ts2::ui

// UI/OptionsWindow.cpp — implémentation de la fenêtre Options. Voir OptionsWindow.h.
#include "UI/OptionsWindow.h"
#include "UI/PanelSkin.h"
#include "Game/ClientRuntime.h" // game::Str(id) -> texte réel StrTable005 (005.DAT, mMESSAGE)

#include <algorithm> // std::clamp
#include <cstdio>    // snprintf

namespace ts2::ui {

namespace {

// Fond de panneau réel (best effort) : gabarit (400,440) du dossier atlas UI
// G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, choisi par
// proximité de ratio/hauteur avec le panneau Options (380x440, correspondance
// de hauteur exacte ; cf. méthodologie détaillée dans UI/PanelSkin.h). Repli
// automatique sur kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_03637.IMG");


// Table des 21 champs RÉELLEMENT exposés par UI_OptionsWnd (0x66C180/0x66D140/0x66EAC0),
// dans l'ORDRE du panneau d'origine (donc aussi l'ordre mémoire idx0..idx22, en sautant
// idx3/idx8 — cf. bandeau OptionsWindow.h). C'est la seule source de vérité de la
// fenêtre : Render/OnClick itèrent cette table plutôt que de dupliquer 21 blocs de code
// quasi identiques.
//
// valueStrBase = id StrTable005 (005.DAT) pour value==minV, lu et vérifié en désassemblant
// UI_OptionsWnd_Render 0x66EAC0 (chaque branche `if(g_Opt_X==v) StrTable005_Get(g_LangId,ID)`).
// -1 = pas de texte dans l'original (les 2 sliders de volume) -> repli numérique "%d".
const OptionsFieldDef kFields[] = {
    // label FR                       champ                                     chkbx  min max step  valueStrBase (005.DAT)
    { "Effets de compétence/arme",  &config::GameOptions::ShowSkillEffects,   true,  0, 1,   1,  158 }, // idx0  158=OFF 159=ON
    { "Distance d'affichage",       &config::GameOptions::DisplayRangeTier,  false, 1, 3,   1,  160 }, // idx1  160=LOW 161=MEDIUM 162=HIGH
    // idx2 : valeurs réelles "Basic"/"Advanced" (StrTable005 #168/#169) — PAS un simple
    // marche/arrêt générique ; le libellé "divers"/"réservé" d'une passe précédente était
    // un placeholder, aucun consommateur runtime identifié au-delà de ce toggle UI.
    { "Mode interface (Basic/Advanced)", &config::GameOptions::Toggle2Reserved, true, 0, 1, 1, 168 }, // idx2  168=Basic 169=Advanced
    // idx3 (Reserved3) : ABSENT — aucune ligne dans UI_OptionsWnd d'origine, cf. OptionsWindow.h.
    { "Marqueurs de coups",         &config::GameOptions::ShowHitMarkers,     true,  0, 1,   1,  170 }, // idx4  170=OFF 171=ON
    { "Plaques de nom",             &config::GameOptions::ShowNameplates,     true,  0, 1,   1,  172 }, // idx5  172=OFF 173=ON
    { "Traînée d'arme",             &config::GameOptions::WeaponTrail,        true,  0, 1,   1,  174 }, // idx6  174=OFF 175=ON
    // idx7 : ordre textuel RÉEL confirmé non-monotone (176..179 = OFF,HIGH,MEDIUM,LOW) —
    // le niveau numérique 1..3 ne va PAS du plus faible au plus fort dans le texte d'origine.
    { "Lueur d'arme (niveau)",      &config::GameOptions::WeaponGlowLevel,   false, 0, 3,   1,  176 }, // idx7  176=OFF 177=HIGH 178=MEDIUM 179=LOW
    // idx8 (Reserved8) : ABSENT — idem idx3.
    { "Luminosité (Phase)",         &config::GameOptions::BrightnessLevel,   false, 1, 9,   1,  182 }, // idx9  182..190 = "Phase 1".."Phase 9"
    { "Volume musique",             &config::GameOptions::MusicVolume,       false, 0, 100, 5,  -1  }, // idx10 pas de texte dans l'original (slider)
    { "Volume effets sonores",      &config::GameOptions::SoundVolume,       false, 0, 100, 5,  -1  }, // idx11 pas de texte dans l'original (slider)
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

// Texte de valeur réel pour la ligne `f` avec la valeur courante `v` : lit StrTable005
// (005.DAT, mMESSAGE) via game::Str(valueStrBase + (v - minV)), fidèle à la lecture
// contiguë confirmée dans UI_OptionsWnd_Render 0x66EAC0. Repli "%d" pour les 2 sliders
// de volume (valueStrBase==-1, cf. commentaire de kFields).
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
    // Recharge l'état disque à l'ouverture (cohérent avec Options_LoadAndNormalize
    // appelé au démarrage) : la fenêtre reflète toujours ce qui est effectivement
    // persisté quand on l'ouvre, avant toute modification locale.
    Dialog::Open();
    scroll_ = 0;
    armedTarget_ = Target::None;
    armedRow_ = -1;
}

void OptionsWindow::Layout(int screenW, int screenH, Rect& panel, Rect& close,
                            Rect& save, Rect& cancel, Rect& list,
                            Rect& scrollUp, Rect& scrollDown) const {
    panel.x = screenW / 2 - kPanelW / 2;
    panel.y = screenH / 2 - kPanelH / 2;
    panel.w = kPanelW;
    panel.h = kPanelH;

    // Bouton fermeture (croix) en haut à droite du panneau.
    close = { panel.x + panel.w - kCloseBtn - 6, panel.y + 4, kCloseBtn, kCloseBtn };

    // Zone de liste : sous le titre, au-dessus de la barre de boutons bas.
    list.x = panel.x + 10;
    list.y = panel.y + kTitleH + 6;
    list.w = panel.w - 20 - (kCloseBtn + 4); // réserve la colonne des flèches de scroll
    list.h = panel.h - kTitleH - kFooterH - 12;

    // Flèches de défilement, colonne étroite à droite de la liste.
    scrollUp   = { list.x + list.w + 4, list.y,                     kCloseBtn, kScrollBtnH };
    scrollDown = { list.x + list.w + 4, list.y + list.h - kScrollBtnH, kCloseBtn, kScrollBtnH };

    // Boutons Sauvegarder / Annuler, alignés en bas du panneau.
    const int by = panel.y + panel.h - kFooterH + 6;
    const int bw = 120, bh = 24;
    save   = { panel.x + panel.w / 2 - bw - 6, by, bw, bh };
    cancel = { panel.x + panel.w / 2 + 6,      by, bw, bh };
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

bool OptionsWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    Rect panel, close, save, cancel, list, scrollUp, scrollDown;
    Layout(lastScreenW_, lastScreenH_, panel, close, save, cancel, list, scrollUp, scrollDown);

    armedTarget_ = Target::None;
    armedRow_ = -1;

    if (!In(panel, x, y)) return false; // clic hors fenêtre : ne consomme pas

    if (In(close, x, y))      { armedTarget_ = Target::Close; }
    else if (In(save, x, y))  { armedTarget_ = Target::Save; }
    else if (In(cancel, x, y)){ armedTarget_ = Target::Cancel; }
    else if (In(scrollUp, x, y))   { armedTarget_ = Target::ScrollUp; }
    else if (In(scrollDown, x, y)) { armedTarget_ = Target::ScrollDown; }
    else if (In(list, x, y)) {
        // Cherche la ligne + sous-contrôle sous le curseur.
        for (int row = scroll_; row < kFieldCount; ++row) {
            Rect r;
            if (!RowRect(list, row, r)) break;
            if (!In(r, x, y)) continue;
            const OptionsFieldDef& f = kFields[row];
            if (f.checkbox) {
                if (In(RowCheckboxRect(r), x, y)) { armedTarget_ = Target::RowCheckbox; armedRow_ = row; }
            } else {
                if (In(RowMinusRect(r), x, y))      { armedTarget_ = Target::RowMinus; armedRow_ = row; }
                else if (In(RowPlusRect(r), x, y))  { armedTarget_ = Target::RowPlus;  armedRow_ = row; }
            }
            break;
        }
    }
    return true; // panneau modal de fait : toute la surface consomme le clic
}

void OptionsWindow::ActivateIfHit(const Rect& panel, const Rect& close, const Rect& save,
                                   const Rect& cancel, const Rect& list,
                                   const Rect& scrollUp, const Rect& scrollDown, int x, int y) {
    switch (armedTarget_) {
        case Target::Close:
            if (In(close, x, y)) Close();
            break;
        case Target::Save:
            if (In(save, x, y)) config::g_Options.Save();
            break;
        case Target::Cancel:
            if (In(cancel, x, y)) config::g_Options.Load();
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
            v = (v != 0) ? 0 : 1; // toggle (UI_OptionsWnd_OnClick : bascule 0/1)
            v = std::clamp<int32_t>(v, f.minV, f.maxV);
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
            break;
        }
        case Target::RowPlus: {
            if (armedRow_ < 0 || armedRow_ >= kFieldCount) break;
            Rect r;
            if (!RowRect(list, armedRow_, r)) break;
            if (!In(RowPlusRect(r), x, y)) break;
            const OptionsFieldDef& f = kFields[armedRow_];
            int32_t& v = config::g_Options.*(f.field);
            v = std::clamp<int32_t>(v + f.step, f.minV, f.maxV);
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
    Rect panel, close, save, cancel, list, scrollUp, scrollDown;
    Layout(lastScreenW_, lastScreenH_, panel, close, save, cancel, list, scrollUp, scrollDown);

    const bool inside = In(panel, x, y);
    if (inside) ActivateIfHit(panel, close, save, cancel, list, scrollUp, scrollDown, x, y);
    else { armedTarget_ = Target::None; armedRow_ = -1; }

    return inside; // ne consomme que si le relâchement tombe dans la fenêtre
}

bool OptionsWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    Rect panel, close, save, cancel, list, scrollUp, scrollDown;
    Layout(lastScreenW_, lastScreenH_, panel, close, save, cancel, list, scrollUp, scrollDown);

    if (vk == VK_ESCAPE) { Close(); return true; }
    if (vk == VK_UP)     { scroll_ -= 1; ClampScroll(list); return true; }
    if (vk == VK_DOWN)   { scroll_ += 1; ClampScroll(list); return true; }
    if (vk == VK_PRIOR)  { scroll_ -= VisibleRowCount(list); ClampScroll(list); return true; } // Page Up
    if (vk == VK_NEXT)   { scroll_ += VisibleRowCount(list); ClampScroll(list); return true; } // Page Down
    return true; // fenêtre modale de fait tant qu'ouverte : avale les autres touches
}

void OptionsWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Rect panel, close, save, cancel, list, scrollUp, scrollDown;
    Layout(ctx.screenW, ctx.screenH, panel, close, save, cancel, list, scrollUp, scrollDown);
    ClampScroll(list);

    if (ctx.phase == UiPhase::Panels) {
        // Fond + cadre du panneau.
        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColBorder, 2);
        // Bandeau titre.
        ctx.FillRect(panel.x, panel.y, panel.w, kTitleH, kColTitleBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, kTitleH, kColBorder, 1);

        // Bouton fermeture (croix).
        const bool closeHover = In(close, cursorX, cursorY);
        ctx.FillRect(close.x, close.y, close.w, close.h,
                     (armedTarget_ == Target::Close) ? kColBtnDown : (closeHover ? kColBtnHover : kColClose));
        ctx.DrawFrame(close.x, close.y, close.w, close.h, kColBorder, 1);

        // Flèches de défilement.
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

        // Lignes visibles : bande alternée + contrôle (case ou -/+).
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

        // Boutons Sauvegarder / Annuler.
        const bool saveHover = In(save, cursorX, cursorY);
        ctx.FillRect(save.x, save.y, save.w, save.h,
                     (armedTarget_ == Target::Save) ? kColBtnDown : (saveHover ? kColBtnHover : kColBtn));
        ctx.DrawFrame(save.x, save.y, save.w, save.h, kColBorder, 1);
        const bool cancelHover = In(cancel, cursorX, cursorY);
        ctx.FillRect(cancel.x, cancel.y, cancel.w, cancel.h,
                     (armedTarget_ == Target::Cancel) ? kColBtnDown : (cancelHover ? kColBtnHover : kColBtn));
        ctx.DrawFrame(cancel.x, cancel.y, cancel.w, cancel.h, kColBorder, 1);
        return;
    }

    // --- Phase texte ---
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
        // Texte de valeur RÉEL (StrTable005 005.DAT, cf. FieldValueText) — remplace
        // l'affichage numérique brut : reproduit ce que UI_OptionsWnd_Render 0x66EAC0
        // dessine réellement (ON/OFF, LOW/MEDUIM/HIGH, Basic/Advanced, Phase N, etc.).
        const std::string valueText = FieldValueText(f, v);
        if (f.checkbox) {
            Rect cb = RowCheckboxRect(r);
            // Texte d'état à gauche de la case (place réservée dans la ligne), la case
            // elle-même reste l'affordance de clic (v66EAC0 dessine le texte à une
            // colonne fixe ; ici on le colle contre la case pour rester lisible aux
            // largeurs de panneau réduites du portage).
            const int vw = ctx.MeasureText(valueText.c_str());
            ctx.Text(valueText.c_str(), cb.x - vw - 6, r.y + 4, kColText);
            if (v != 0) {
                ctx.Text("X", cb.x + 3, cb.y + 1, kColCheck); // « croix si vrai »
            }
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

    const char* saveLbl = "Sauvegarder";
    const int saveLblW = ctx.MeasureText(saveLbl);
    ctx.Text(saveLbl, save.x + (save.w - saveLblW) / 2, save.y + 5, kColText);
    const char* cancelLbl = "Annuler";
    const int cancelLblW = ctx.MeasureText(cancelLbl);
    ctx.Text(cancelLbl, cancel.x + (cancel.w - cancelLblW) / 2, cancel.y + 5, kColText);
}

} // namespace ts2::ui

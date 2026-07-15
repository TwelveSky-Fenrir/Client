// UI/OptionsWindow.h — Fenêtre « Options / Paramètres » du client TwelveSky2.
//
// Transposition C++ propre du dialogue d'options du désassemblage :
//   UI_OptionsWnd_OnClick     0x66D140  cases à cocher + boutons +/- avec bornage
//   UI_OptionsWnd_OnMouseDown 0x66C180  latch des boutons (~47 hit-tests, volumes en fild)
//   UI_OptionsWnd_Render      0x66EAC0  dessine l'état + LIT LE VRAI TEXTE (StrTable005_Get,
//                                       0x4C1D20) affiché à droite de chaque ligne
//   Options_SetDefaults       0x4C2020  \
//   Options_LoadBin           0x4C2140   > config::GameOptions (Config/GameOptions.h)
//   Options_SaveBin           0x4C2280  /
//
// Le bloc d'options est 23 × int32 (voir Config/GameOptions.h pour le layout byte-exact
// et les bornes documentées champ par champ). Cette fenêtre expose les 21 champs
// RÉELLEMENT pilotés par un contrôle dans UI_OptionsWnd_Render/OnClick/OnMouseDown :
//   - bornes [0,1]                -> case à cocher (toggle au clic)
//   - bornes plus larges          -> boutons « - » / « + » qui clampent à la borne
// idx3 (Reserved3) et idx8 (Reserved8) sont VOLONTAIREMENT ABSENTS de kFields : lecture
// complète des 3 fonctions UI d'origine (0x66C180..0x66D833 OnMouseDown, 0x66D140..0x66E82A
// OnClick, 0x66EAC0..0x67048B Render) confirme qu'AUCUN hit-test, AUCUN Sprite2D_Draw et
// AUCUN StrTable005_Get ne référence ces deux champs — ils sont chargés/sauvés (92 octets
// intacts) mais n'ont jamais eu de ligne dans le panneau d'origine. Leur redonner une case
// à cocher inventée serait un placeholder, pas une fidélité ; GameOptions.h les documente
// déjà comme « réservés, aucune xref ».
//
// VALEURS AFFICHÉES : le texte à droite de chaque ligne (ex. "ON"/"OFF", "LOW"/"MEDUIM"/
// "HIGH", "Basic"/"Advanced", "Phase 1".."Phase 9", "[F1]-[F10]"/"[1]-[0]") est le VRAI
// texte StrTable005 (005.DAT, mMESSAGE) lu par UI_OptionsWnd_Render via StrTable005_Get
// (this+106*i-102) — reconstitué ici via game::Str(id) (Game/ClientRuntime.cpp), qui lit
// la même table 005.DAT chargée par Game/StringTables.h. valueStrBase = id StrTable005
// pour value==minV ; les valeurs suivantes sont CONTIGÜES (+1 par pas), confirmé pour
// les 19 champs concernés en désassemblant Render. Les 2 sliders de volume (idx10/idx11)
// n'ont AUCUN texte dans l'original (juste une poignée de slider Sprite2D_Draw sans
// StrTable005_Get) : valueStrBase=-1 -> repli numérique "%d" (fidèle : c'est réellement
// la seule info textuelle absente du panneau d'origine).
//
// « Sauvegarder » écrit sur disque (config::g_Options.Save()), « Annuler » recharge
// l'état disque (config::g_Options.Load()), écrasant les modifications en mémoire.
//
// Aucune de ces 23 valeurs ne transite par le réseau (ce sont des préférences locales
// client, cf. commentaire GameOptions.h) : il n'y a donc aucun TODO(send) ici.
#pragma once
#include <cstdint>

#include "UI/UIManager.h"
#include "Config/GameOptions.h"

namespace ts2::ui {

// Description d'un champ du bloc d'options — pointeur-membre vers config::GameOptions
// pour piloter les 21 lignes depuis une seule table de données (pas de code dupliqué).
struct OptionsFieldDef {
    const char* label;                          // libellé affiché (FR)
    int32_t config::GameOptions::* field;        // pointeur-membre (idx0..idx22, cf. skip idx3/idx8)
    bool    checkbox;                            // true = borne [0,1] -> case à cocher
    int32_t minV;                                // borne basse documentée
    int32_t maxV;                                // borne haute documentée
    int32_t step;                                // pas des boutons -/+ (ignoré si checkbox)
    int32_t valueStrBase;                        // id StrTable005 pour value==minV, -1 = pas de texte (slider numérique)
};

// Fenêtre Options : liste verticale scrollable des 23 champs de config::g_Options,
// plus Sauvegarder / Annuler / Fermer. Hérite de ts2::ui::Dialog (contrat UIManager).
class OptionsWindow : public Dialog {
public:
    OptionsWindow();

    void Open() override;                          // Dialog::Open() + recharge l'état affiché
    void Close() override { Dialog::Close(); }

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x, y, w, h; };

    // Cible actuellement « armée » (bouton enfoncé, en attente du relâchement dessus).
    enum class Target {
        None, Close, Save, Cancel, ScrollUp, ScrollDown,
        RowCheckbox, RowMinus, RowPlus,
    };

    // Géométrie recalculée à chaque frame à partir des dimensions écran (centrage),
    // exactement comme MsgBoxDialog::Layout — le hit-test (routé entre deux frames)
    // s'appuie sur lastScreenW_/lastScreenH_ mémorisées au dernier Render.
    void Layout(int screenW, int screenH, Rect& panel, Rect& close,
                Rect& save, Rect& cancel, Rect& list, Rect& scrollUp, Rect& scrollDown) const;
    // Rectangle de la ligne `row` (index dans kFields) dans le repère de `list`,
    // en tenant compte du défilement courant. Renvoie false si hors zone visible.
    bool RowRect(const Rect& list, int row, Rect& out) const;
    // Sous-rectangles interactifs d'une ligne (checkbox OU minus/plus+valeur).
    Rect RowCheckboxRect(const Rect& row) const;
    Rect RowMinusRect(const Rect& row) const;
    Rect RowPlusRect(const Rect& row) const;

    int  VisibleRowCount(const Rect& list) const;
    int  MaxScroll(const Rect& list) const;
    void ClampScroll(const Rect& list);

    // Applique l'action de la cible armée si le relâchement tombe dessus.
    void ActivateIfHit(const Rect& panel, const Rect& close, const Rect& save,
                        const Rect& cancel, const Rect& list,
                        const Rect& scrollUp, const Rect& scrollDown, int x, int y);

    static bool In(const Rect& r, int x, int y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }

    Target armedTarget_ = Target::None;
    int    armedRow_    = -1; // ligne concernée si RowCheckbox/RowMinus/RowPlus

    int    scroll_ = 0; // première ligne visible (défilement)

    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // --- Constantes de géométrie (panneau + liste) ---
    static constexpr int kPanelW   = 380;
    static constexpr int kPanelH   = 440;
    static constexpr int kTitleH   = 26;
    static constexpr int kFooterH  = 34;
    static constexpr int kRowH     = 20;
    static constexpr int kCloseBtn = 18;
    static constexpr int kScrollBtnH = 18;

    // --- Palette (ARGB, cf. contrat UI) ---
    static constexpr D3DCOLOR kColBg       = Argb(224, 32, 32, 40);
    static constexpr D3DCOLOR kColBorder   = Argb(255, 128, 128, 128);
    static constexpr D3DCOLOR kColTitleBg  = Argb(255, 44, 44, 60);
    static constexpr D3DCOLOR kColTitle    = Argb(255, 255, 221, 102);
    static constexpr D3DCOLOR kColText     = Argb(255, 255, 255, 255);
    static constexpr D3DCOLOR kColRowAlt   = Argb(60, 255, 255, 255);
    static constexpr D3DCOLOR kColBtn      = Argb(255, 56, 64, 88);
    static constexpr D3DCOLOR kColBtnHover = Argb(255, 64, 96, 160);
    static constexpr D3DCOLOR kColBtnDown  = Argb(255, 150, 120, 70);
    static constexpr D3DCOLOR kColCheck    = Argb(255, 96, 255, 96);
    static constexpr D3DCOLOR kColClose    = Argb(255, 255, 96, 96);
};

} // namespace ts2::ui

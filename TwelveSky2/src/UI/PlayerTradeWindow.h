// UI/PlayerTradeWindow.h — fenêtre ÉCHANGE ENTRE JOUEURS du client TS2 (distincte
// du marchand PNJ / de l'entrepôt : voir UI/WarehouseWindow.h pour ce dernier).
//
// Réécriture C++ PROPRE branchée sur l'état déjà écrit par
// Net/GameHandlers_VendorTrade.cpp (NE PAS ÉDITER, lu en entier pour cette
// mission) qui applique les paquets :
//   0x31 TradeRequestPrompt  — mémorise le partenaire (idLo/val1/val2) + un
//                               "extra", remet l'état d'échange à 0, affiche
//                               un message système "[promptId] ..."
//   0x32 TradeRequestResult  — ligne de résultat de DEMANDE d'échange (avant
//                               ouverture de la table) ; pas d'état modifié
//   0x33 TradeActionResult   — résultat d'action (accepter/annuler/terminé) ;
//                               remet TOUJOURS l'état d'échange et le partenaire
//                               à zéro en sortie
//   0x26 TradeResult         — résultat de VENTE/entrepôt (PAS l'échange joueur
//                               à proprement parler : ce paquet écrit une cellule
//                               d'inventaire g_Client.inv, pas de grille dédiée)
//
// Les globals d'origine repris ICI (mêmes adresses que dans
// GameHandlers_VendorTrade.cpp, dupliquées car ce fichier ne doit pas être
// édité) :
//   dword_168741C  g_TradePartnerIdLo  — identité du partenaire (mot 0)
//   dword_1687420                      — identité du partenaire (mot 1)
//   dword_1687424                      — « accord local » (comparé au code de
//                                         0x33 : voir GameHandlers_VendorTrade.cpp
//                                         case 1/2)
//   dword_1675B24  état d'échange      — remis à 0 par 0x31 ET 0x33 ; AUCUN des
//                                         handlers actuellement écrits ne l'arme
//                                         à une valeur non nulle (le TODO(state)
//                                         de la table d'objets échangés n'est pas
//                                         implémenté côté handlers). On l'affiche
//                                         donc tel quel, en diagnostic brut,
//                                         SANS lui prêter une sémantique non
//                                         prouvée.
//   dword_1675D84  extra               — valeur additionnelle du prompt 0x31
//
// ÉCART DOCUMENTÉ (à ne pas cacher) : aucune structure de données pour le
// CONTENU de la table d'échange (objets déposés par chaque joueur) n'a été
// identifiée dans les artefacts RE actuels. Le blob 1232 o vu ailleurs
// (Game/WarehouseSystem.h::WarehouseGrid) est RÉUTILISÉ par la fenêtre UI
// partagée d'origine (dword_1822990) pour plusieurs modes (entrepôt/marchand/
// échange), mais son layout byte-exact n'a été déduit QUE pour le chemin
// entrepôt (UI_StorageWin_CommitGrid/Open) ; rien ne prouve qu'il s'applique
// tel quel à l'échange joueur, et GameHandlers_VendorTrade.cpp ne peuple aucune
// grille pour 0x31/0x32/0x33. Cette fenêtre affiche donc deux grilles 5x5 de
// cellules VIDES (placeholder visuel,« 25 cellules » affiché littéralement)
// plutôt que de fabriquer un WarehouseGrid non prouvé pour ce contexte.
//
// CÂBLAGE NON FAIT (hors périmètre, GameHandlers_VendorTrade.cpp interdit
// d'édition) : rien n'appelle actuellement Open()/Close() sur cette fenêtre
// depuis les handlers réseau. Le contrat Dialog (Open/Close publics) est
// respecté pour qu'un futur point d'intégration (ex. dans 0x31, ou dans le
// futur TODO(state) de la table d'objets) puisse le faire sans toucher à ce
// fichier.
//
// Règle du projet : ce fichier n'édite AUCUN header existant ; il inclut
// UI/UIManager.h et Game/ClientRuntime.h en lecture seule.
#pragma once
#include "UI/UIManager.h"
#include "Game/ClientRuntime.h"

#include <cstdint>
#include <string>

namespace ts2::ui {

class PlayerTradeWindow : public Dialog {
public:
    PlayerTradeWindow() = default;

    // Ouverture/fermeture (Dialog::Open/Close). L'ouverture réarme les latches
    // de boutons ; aucune donnée n'est modifiée (la fenêtre ne fait que LIRE
    // g_Client.Var(...) à chaque Render, elle ne les initialise pas).
    void Open() override;
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x, y, w, h; };

    // Géométrie recalculée à partir des dimensions écran (comme MsgBoxDialog::Layout,
    // UI/UIManager.cpp) ; lastScreenW_/H_ mémorisent la géométrie effectivement
    // dessinée pour que le hit-test (routé entre deux frames) reste aligné.
    void Layout(int screenW, int screenH, Rect& box, Rect& closeBtn,
                Rect& acceptBtn, Rect& cancelBtn,
                Rect& selfGrid, Rect& partnerGrid) const;

    // Dessine une grille kGridRows x kGridCols de cellules VIDES (placeholder,
    // voir note d'écart en tête de fichier) + son libellé au-dessus.
    void DrawGridPlaceholder(const UiContext& ctx, const Rect& grid,
                              const char* label) const;

    void HandleAccept(); // bouton « Accepter »
    void HandleCancel(); // bouton « Annuler » / fermeture (X)

    // --- Latches boutons (armés au clic-enfoncé, validés au relâchement dedans,
    //     pattern MsgBoxDialog::btnPressed_) ---
    bool closePressed_  = false;
    bool acceptPressed_ = false;
    bool cancelPressed_ = false;

    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // --- Géométrie (référence 1024x768, cf. WarehouseWindow.h) ---
    static constexpr int kGridCols  = 5;
    static constexpr int kGridRows  = 5;
    static constexpr int kCellSize  = 26;
    static constexpr int kCellGap   = 3;
    static constexpr int kGridW     = kGridCols * kCellSize + (kGridCols - 1) * kCellGap;
    static constexpr int kGridH     = kGridRows * kCellSize + (kGridRows - 1) * kCellGap;

    static constexpr int kPanelPad  = 16;
    static constexpr int kGridGap   = 28;   // écart entre les deux grilles
    static constexpr int kHeaderH   = 26;   // bandeau titre
    static constexpr int kGridLabelH= 16;   // libellé "Vous"/"Partenaire"
    static constexpr int kInfoH     = 34;   // ligne de diagnostic partenaire/accord
    static constexpr int kNoteH     = 16;   // note "25 cellules (...)"
    static constexpr int kFooterH   = 44;   // rangée de boutons
    static constexpr int kCloseSize = 18;
    static constexpr int kBtnW      = 110;
    static constexpr int kBtnH      = 26;

    static constexpr int kPanelW = kPanelPad * 2 + kGridW * 2 + kGridGap;
    static constexpr int kPanelH = kHeaderH + kPanelPad + kGridLabelH + kGridH
                                  + kNoteH + kInfoH + kFooterH + kPanelPad;

    // --- Palette (D3DCOLOR = 0xAARRGGBB, cf. consigne de mission) ---
    static constexpr D3DCOLOR kColBg       = 0xE0202028u; // fond panneau
    static constexpr D3DCOLOR kColFrame    = 0xFF808080u; // cadre
    static constexpr D3DCOLOR kColHeaderBg = 0xFF2A2A34u; // bandeau titre
    static constexpr D3DCOLOR kColTitle    = 0xFFFFDD66u; // titre
    static constexpr D3DCOLOR kColText     = 0xFFFFFFFFu; // texte
    static constexpr D3DCOLOR kColTextDim  = 0xFFAAAAAAu; // texte atténué (diagnostic)
    static constexpr D3DCOLOR kColHover    = 0xFF4060A0u; // survol
    static constexpr D3DCOLOR kColDanger   = 0xFFE04040u; // « HP » réutilisé en accent Annuler
    static constexpr D3DCOLOR kColCellBg   = 0xFF1A1A20u; // cellule vide (placeholder)
    static constexpr D3DCOLOR kColBtnBg    = 0xFF383850u; // bouton (repos)
    static constexpr D3DCOLOR kColBtnDown  = 0xFF2A2A3Au; // bouton (enfoncé)
    static constexpr D3DCOLOR kColCloseBg  = 0xFF483030u; // bouton fermer (repos)
};

} // namespace ts2::ui

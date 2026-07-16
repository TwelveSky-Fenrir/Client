// UI/PlayerTradeWindow.h — fenêtre « échange entre joueurs ».
//
// ===========================================================================
// AVERTISSEMENT DE FIDÉLITÉ — CETTE FENÊTRE N'A AUCUNE CONTREPARTIE DANS LE BINAIRE
// ===========================================================================
// Vérifié exhaustivement (Passe 4, vague W6, décompilation fraîche) : TwelveSky2.exe
// n'implémente AUCUNE table d'échange joueur-à-joueur. Il n'existe ni grille d'objets
// échangés, ni dépôt d'or, ni verrou de moitié, ni bouton « accepter ». Les actions
// « ajouter/retirer un objet, poser de l'or, verrouiller, accepter » — que cette
// fenêtre dessine — n'existent tout simplement pas dans ce build.
//
// PREUVES (ne pas ré-ouvrir ce débat sans les contredire) :
//  1. UI_InitAllDialogs 0x5ABF50 énumère les ~38 singletons de dialogue du HUD :
//     AUCUN n'est une table d'échange. La seule fenêtre à grille d'objets est
//     dword_1822990 (UI_StorageWin_Open 0x5D27A0), qui ne connaît que 5 modes —
//     1 = marchand PNJ, 2 = BOUTIQUE d'un joueur (émet Net_SendPacket_Op33 @0x5D2C24),
//     3/4/5 = entrepôt (Net_SendPacket_Op108). Aucun accept/lock/or/add-item.
//  2. Le seul chemin joueur<->joueur est Player_InteractWithPlayer 0x5392E0 ->
//     UI_ClanWin_Open (menu contextuel joueur) -> UI_ClanWin_OnLUp 0x5D92A0 page 2,
//     dont les 2 boutons « échange » émettent Net_SendOp43(nom13, 2) @0x5D9F8A et
//     Net_SendOp43(nom13, 1) @0x5DA0F1 (op 0x2B, len 26 — les 2 SEULES xrefs du
//     builder), puis ouvrent une NOTICE passive (UI_NoticeDlg_Open) sans aucun bouton
//     émetteur.
//  3. Les 3 réponses serveur sont PUREMENT TEXTUELLES — elles n'ouvrent aucune fenêtre
//     et ne peuplent aucune grille :
//       0x31 Pkt_TradeRequestPrompt 0x48FD20 : memcpy(v7, 0x8156C1, 12) puis
//            g_TradePartnerIdLo=v7[0] @0x48fd8f, dword_1687420=v7[1] @0x48fd97,
//            dword_1687424=v7[2] @0x48fda0, dword_1675D84=v5 @0x48fdac ; son + une
//            ligne système "%s [%d]%s" (StrTable005 314/315). RIEN d'autre.
//       0x32 Pkt_TradeRequestResult 0x48FE10 : une ligne système, point.
//       0x33 Pkt_TradeActionResult  0x48FEA0 : switch(code 0..3) -> ligne système,
//            puis remise à 0 de g_PendingOrderKind/g_TradePartnerIdLo/dword_1687420/
//            dword_1687424 et dword_1675D84=1 (@0x48ff66-0x48ff8e).
// => Ne RIEN émettre depuis HandleAccept()/HandleCancel() est donc CORRECT : ce ne
//    sont pas des builders manquants, ces paquets n'existent pas. Les grilles 5x5
//    ci-dessous sont une RÉINVENTION assumée (la fenêtre est instanciée par
//    UI/GameWindows.h:190, header non possédé par ce front -> non supprimable ici).
//
// ===========================================================================
// GLOBALS D'ORIGINE — DEUX ÉTIQUETTES FAUSSES CORRIGÉES (W4-F3, re-confirmé W6)
// ===========================================================================
//   dword_168741C  g_TradePartnerIdLo — identité du partenaire, mot 0 (0x8156C1+0)
//   dword_1687420                     — identité du partenaire, mot 1 (0x8156C1+4)
//   dword_1687424                     — identité du partenaire, mot 2 (0x8156C1+8),
//        écrit par le SERVEUR via Pkt_TradeRequestPrompt 0x48FD20 @0x48fda0, puis
//        comparé au CODE D'ACTION reçu par Pkt_TradeActionResult 0x48FEA0 @0x48fefe
//        (`if (dword_1687424[0] == v7)` -> StrTable005 #318 sinon #319).
//        ⚠️ CE N'EST PAS un « accord local » — étiquette RÉFUTÉE, ne pas la réintroduire.
//   dword_1675B24  = g_PendingOrderKind — type d'ordre monde / ciblage
//        (Player_InteractWithPlayer 0x5392E0, Game_OnWorldLeftClick 0x536690,
//        Player_CastSkill 0x53BC40, AutoPlay_UpdateTargeting 0x45D080).
//        ⚠️ CE N'EST PAS un « état d'échange » — étiquette RÉFUTÉE. Les handlers
//        d'échange ne le remettent à 0 que par EFFET DE BORD (reset du ciblage à
//        l'arrivée d'un prompt). Il n'est ni lu ni affiché par cette fenêtre.
//   dword_1675D84  — écrit par 0x31 (= mot lu à 0x8156D1, @0x48fdac) et forcé à 1 par
//        0x33 (@0x48ff8e). Son ÉCRITURE est prouvée ; sa SÉMANTIQUE ne l'est pas ->
//        non affiché (ne pas lui inventer un rôle).
//
// CÂBLAGE : rien n'appelle Open()/Close() depuis les handlers réseau — et c'est
// FIDÈLE, puisqu'aucun paquet d'origine n'ouvre de table d'échange. L'ouverture ne
// vient aujourd'hui que de la touche 'T' (hotkeys::kPlayerTrade, UI/GameWindows.h:70
// -> UI/GameWindows.cpp:135), dont le commentaire « ouverture normale = paquet serveur
// 0x31 » est FAUX (0x31 n'ouvre rien, cf. preuve 3 ci-dessus) : header non possédé par
// ce front, corrigé par signalement au rapport de vague.
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

// UI/ShareBoxWindow.h — fenêtre UI_ShareBoxDlg (dword_1822560, ctor 0x5CDDD0).
//
// ###########################################################################
// # ATTENTION AU NOM : ce n'est PAS un « coffre partagé ».                  #
// #                                                                         #
// # Le symbole IDB `UI_ShareBoxDlg` est TROMPEUR (nom hérité d'une passe de #
// # nommage antérieure). La décompilation prouve que cette fenêtre est le   #
// # PANNEAU DE CONFIGURATION DE LA CEINTURE AUTO-POTION (consommables) :    #
// # UI_ShareBoxDlg_Draw 0x5CE4D0 rend les 10 emplacements de                #
// # g_AutoPotionBelt 0x16757B0 avec leur compteur de charges                #
// # dword_16757D8 (« %d / %d » sur 30). Aucun rapport avec l'entrepôt       #
// # (UI_StorageWin_* / dword_1822990 / opcodes 0x22-0x24), déjà porté par   #
// # UI/WarehouseWindow.*. Le nom de fichier/classe suit le symbole IDA      #
// # (règle « IDA = vérité unique ») ; la SÉMANTIQUE est celle décrite ici.  #
// ###########################################################################
//
// Fonctions d'origine transcrites (toutes re-prouvées par décompilation le 2026-07-16) :
//   UI_ShareBoxDlg_InitBtnMap 0x5CDFB0 -> constantes de boutons ci-dessous
//   UI_ShareBoxDlg_Open       0x5CE0C0 -> Open()
//   UI_ShareBoxDlg_Close      0x5CE100 -> Close()
//   UI_ShareBoxDlg_OnLDown    0x5CE120 -> OnMouseDown()
//   UI_ShareBoxDlg_OnLUp      0x5CE330 -> OnClick()
//   UI_ShareBoxDlg_Draw       0x5CE4D0 -> Render()
//   UI_ShareBox_MoveItem      0x5CEAB0 -> MoveItem()  [statique : __stdcall libre, 0 `this`]
//
// NON PORTÉ (délibérément) : UI_ShareBox_Withdraw 0x5CEC40 — 0 xref, fonction MORTE.
//
// ===========================================================================
// CORRECTIONS AU DOSSIER DE GAPS (USD-01), prouvées par décompilation
// ===========================================================================
//  1. « MoveItem(a1, a2) : sélecteur 1=déposer / 2=retirer » -> FAUX. La
//     signature réelle est MoveItem(a1 = drapeau VERBEUX, a2 = code d'action) :
//     `a1` ne fait que gater l'affichage des messages d'erreur (`if (a1)` aux
//     EA 0x5CEAD3 / 0x5CEB0E / 0x5CEB89 / 0x5CEBBB), il ne sélectionne RIEN.
//  2. Les DEUX appelants vivants passent littéralement `(1, 1)` :
//     `push 1 ; push 1` aux EA 0x5CE3EA-0x5CE3EC (OnLUp) et 0x679FE8-0x679FEA
//     (UI_GameHud_ProcNet case 47). La valeur 2 n'est émise par AUCUN chemin :
//     la branche `a2 != 1` (indexée par dword_1675800) est INATTEIGNABLE en
//     pratique. Elle est tout de même transcrite (fidélité au corps de la
//     fonction), et signalée comme telle dans le .cpp.
//  3. `dword_1822588` n'est PAS une « garde » distincte du HUD : c'est
//     `0x1822560 + 40` = le champ `*(this+10)` = bOpen lui-même (EA 0x5CE0CC).
//     Le HUD fait donc un simple toggle de bOpen, pas un test d'un second flag.
//  4. EA 0x5CE3F1 n'est PAS un « drag & drop » : c'est le clic du bouton 3946.
//
// ===========================================================================
// RÉSEAU — AUCUNE ÉMISSION POSSIBLE AUJOURD'HUI (builder absent, cf. .cpp)
// ===========================================================================
// Le seul exutoire réseau de la fenêtre est
// `Net_QueueAction16(&g_PlayerCmdController, a2)` (0x512B90, EA 0x5CEC28), qui
// n'existe PAS côté C++ (grep exhaustif : 0 occurrence). Il dépend de trois
// briques elles-mêmes non portées (g_SelfMoveStateBlock 0x1687324,
// Char_IsAttackAction 0x558A50, verrou g_PlayerCmdController+51600) et relève
// du backlog réseau (math-01 / W8). MoveItem() transcrit donc fidèlement TOUTES
// les gardes et TOUS les messages, et s'arrête sur un `// TODO [ancre 0x5CEC28]`
// au lieu d'inventer un appel — cf. rapport de vague.
//
// Règle du projet : ce fichier n'édite AUCUN header existant ; il inclut
// UI/UIManager.h, Game/ClientRuntime.h et Gfx/* en lecture seule.
#pragma once
#include "UI/UIManager.h"
#include "Game/ClientRuntime.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"

#include <cstdint>

namespace ts2::ui {

class ShareBoxWindow : public Dialog {
public:
    ShareBoxWindow();
    ~ShareBoxWindow() override;

    // Cache GPU d'icônes PARTAGÉ (cf. Gfx/IconTextureCache.h) : injecté par
    // UI/GameWindows.cpp, même instance qu'InventoryWindow/WarehouseWindow/
    // EnchantWindow/VendorShopWindow. nullptr (repli) -> ownIconCache_ locale.
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }

    // UI_ShareBoxDlg_Open 0x5CE0C0 : *(this+10)=1 puis boucle i<4 remettant
    // *(this+11+i)=0 (EA 0x5CE0D3). Seuls 2 des 4 latches sont réellement
    // utilisés (+11 = bouton action, +13 = bouton fermer) ; +12 et +14 sont
    // remis à zéro mais jamais lus — reproduit tel quel (cf. .cpp).
    void Open() override;
    void Close() override;                       // UI_ShareBoxDlg_Close 0x5CE100

    bool OnMouseDown(int x, int y) override;     // UI_ShareBoxDlg_OnLDown 0x5CE120
    bool OnClick(int x, int y) override;         // UI_ShareBoxDlg_OnLUp   0x5CE330

    void Render(const UiContext& ctx, int cursorX, int cursorY) override; // Draw 0x5CE4D0

    // -----------------------------------------------------------------------
    // API DE CÂBLAGE — les 3 déclencheurs prouvés vivent dans des fichiers NON
    // possédés par ce front (UI/GameHud.cpp = vague W9, Net/ItemActionDispatch.cpp
    // = backlog réseau). On expose donc des points d'entrée STATIQUES pour que
    // chacun soit un one-liner sans dépendance sur GameWindows :
    //
    //   (1) Net/ItemActionDispatch.cpp::HandleAutoPotionBelt, après la l.296 —
    //       miroir de l'EA 0x46AF6C (`call UI_ShareBoxDlg_Open` dans
    //       Pkt_ItemActionDispatch 0x46A320, typeCode 26) :
    //           ui::ShareBoxWindow::OpenActive();
    //
    //   (2) UI/GameHud.cpp (toggle HUD) — miroir 0x6799A9 -> 0x6799CF :
    //           if (auto* w = ui::ShareBoxWindow::Active()) {
    //               if (!w->IsOpen()) { UIManager::Instance().CloseAll(); w->Open(); }
    //               else               w->Close();
    //           }
    //
    //   (3) UI/GameHud.cpp::ProcNet case 47 — miroir de l'EA 0x679FF1 :
    //           ui::ShareBoxWindow::MoveItem(/*verbose=*/1, /*action=*/1);
    //
    // Active() renvoie l'instance enregistrée (une seule dans tout le process,
    // possédée par GameWindows) ou nullptr hors session.
    // -----------------------------------------------------------------------
    static ShareBoxWindow* Active();
    static void            OpenActive();

    // UI_ShareBox_MoveItem 0x5CEAB0 — __stdcall LIBRE dans le binaire (n'utilise
    // que des globals, aucun `this`) : donc statique ici aussi.
    //   verbose : `a1` — gate l'affichage des messages d'erreur (PAS un sélecteur).
    //   action  : `a2` — code d'action ; SEULE la valeur 1 est jamais émise
    //             (cf. bandeau de tête, correction n°2).
    static void MoveItem(int verbose, int action);

    // Nombre d'emplacements de ceinture (boucles `i < 10`, EA 0x5CE5E4 / 0x5CE185 /
    // 0x5CE89D). Les 3 boucles du binaire sont bornées à 10, comme les gardes
    // `>= 0xA` de MoveItem (EA 0x5CEAC4 / 0x5CEB3F).
    static constexpr int kSlots = 10;

private:
    struct Rect { int x, y, w, h; };

    // Recentrage écran, RECALCULÉ À CHAQUE ÉVÉNEMENT comme dans le binaire :
    // Draw (EA 0x5CE567/0x5CE58F), OnLDown (0x5CE15D/0x5CE182) et OnLUp
    // (0x5CE36B/0x5CE390) refont TOUS les trois
    //   x = nWidth/2  - Sprite2D_GetWidth(unk_977404)/2
    //   y = nHeight/2 - Sprite2D_GetHeight(unk_977404)/2
    // avant tout hit-test. On reproduit ce recentrage systématique.
    void RecomputeCenter(int screenW, int screenH);

    Rect PanelRect() const;
    Rect SlotRect(int i) const;        // (x + 55*(i%5) + 19, y + 55*(i/5) + 41)
    Rect ActionButtonRect() const;     // boutons 3946/3947 -> (158, 165)
    Rect CloseButtonRect() const;      // boutons 3950/3951 -> (229, 165)

    // Hit-test d'emplacement — comparaisons STRICTES `>` / `<` du binaire
    // (EA 0x5CE209), bornes exclusives des DEUX côtés : `a4 > ox+19 && a4 < ox+74`.
    // PointInRect (inclusif bas / exclusif haut) N'EST PAS équivalent : on écrit
    // donc la comparaison à la main pour rester bit-fidèle.
    bool SlotAt(int mx, int my, int& outSlot) const;

    gfx::GpuTexture* GetIconTex(IDirect3DDevice9* dev, uint32_t itemId);
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;

    // --- Latches de boutons (champs +11 et +13 du dialogue d'origine) ---
    // Armés au clic-enfoncé (OnLDown : *(this+11)=1 EA 0x5CE283 ; *(this+13)=1
    // EA 0x5CE2E4), désarmés au relâchement (OnLUp EA 0x5CE39F / 0x5CE409).
    bool actionLatch_ = false;   // +11 (bouton 3946 pressé -> sprite 3947)
    bool closeLatch_  = false;   // +13 (bouton 3950 pressé -> sprite 3951)

    // ----------------------------------------------------------------------
    // GÉOMÉTRIE — littéraux relevés dans le binaire (exacts), sauf l'ÉTENDUE du
    // panneau et la TAILLE des boutons, qui dérivent des sprites
    // (Sprite2D_GetWidth/Height de unk_977404 / unk_977498 / unk_9776E8) et ne
    // sont donc PAS connaissables statiquement : valeurs de repli dimensionnées
    // pour contenir tous les éléments prouvés (cf. .cpp).
    // ----------------------------------------------------------------------
    static constexpr int kSlotPitch = 55;  // 55*(i%5) / 55*(i/5)  (EA 0x5CE629/0x5CE64A)
    static constexpr int kSlotOx    = 19;  // +19                  (EA 0x5CE629)
    static constexpr int kSlotOy    = 41;  // +41                  (EA 0x5CE64A)
    static constexpr int kSlotCols  = 5;   // i%5 / i/5 -> 5 colonnes x 2 lignes
    static constexpr int kCountDx   = 44;  // centre du texte      (EA 0x5CE8FF)
    static constexpr int kCountDy   = 77;  // ligne du texte       (EA 0x5CE93F)
    static constexpr int kMaxCharges = 30; // littéral 30 du « %d / %d » (EA 0x5CE8E1)

    // UI_ShareBoxDlg_InitBtnMap 0x5CDFB0 — littéraux de la BtnPosMapA :
    //   3946/3947 -> (158, 165)  EA 0x5CE01D / 0x5CE034   (bouton d'action)
    //   3950/3951 -> (229, 165)  EA 0x5CE04B / 0x5CE062   (bouton fermer)
    // Les entrées 3942/3943/3944 (25,-33), 3945 (357,286) et 3952 (-26,-55) sont
    // insérées par InitBtnMap mais JAMAIS lues par Draw/OnLDown/OnLUp -> non portées.
    static constexpr int kBtnActionX = 158;
    static constexpr int kBtnActionY = 165;
    static constexpr int kBtnCloseX  = 229;
    static constexpr int kBtnCloseY  = 165;

    // Replis (dérivés de sprites, cf. ci-dessus).
    static constexpr int kBtnW   = 62;
    static constexpr int kBtnH   = 24;
    static constexpr int kPanelW = 310;  // contient la grille (19..294) et le bouton fermer (229+62)
    static constexpr int kPanelH = 200;  // contient les compteurs (y=173) et les boutons (165+24)

    // --- Palette (D3DCOLOR = 0xAARRGGBB), même convention que WarehouseWindow ---
    static constexpr D3DCOLOR kColPanelBg   = 0xE0202028u;
    static constexpr D3DCOLOR kColFrame     = 0xFF808080u;
    static constexpr D3DCOLOR kColTitle     = 0xFFFFDD66u;
    static constexpr D3DCOLOR kColText      = 0xFFFFFFFFu;
    // Pas de couleur « emplacement vide » : le binaire ne dessine RIEN pour un
    // emplacement vide (garde `>= 1`, EA 0x5CE60B) — cf. Render() dans le .cpp.
    static constexpr D3DCOLOR kColSlotBg    = 0xFF34343Eu; // repli si l'icône ne charge pas
    static constexpr D3DCOLOR kColBtnBg     = 0xFF3A3A46u;
    static constexpr D3DCOLOR kColBtnHover  = 0xFF4060A0u;
    static constexpr D3DCOLOR kColBtnDown   = 0xFF5878C0u;
    // Surbrillances : unk_94D970 (slot d'inventaire sélectionné, EA 0x5CE6CA) et
    // unk_947A0C (slot de ceinture sélectionné, EA 0x5CE6E9). Sprites d'overlay
    // dans le binaire -> rendus ici en cadres colorés (repli, cf. .cpp).
    static constexpr D3DCOLOR kColSelInv    = 0xFFFFCC33u; // unk_94D970
    static constexpr D3DCOLOR kColSelBelt   = 0xFF33FF99u; // unk_947A0C
};

} // namespace ts2::ui

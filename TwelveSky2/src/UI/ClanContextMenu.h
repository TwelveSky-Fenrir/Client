// UI/ClanContextMenu.h — menu contextuel joueur (UI_ClanWin, g_ClanWin dword_1822938).
//
// Gap SGP-1. C'est le SEUL chemin joueur-à-joueur du binaire (cf. UI/PlayerTradeWindow.h:18-23,
// qui le documentait déjà) : sans lui, les 5 builders Net_SendOp47/53/59/65/72 n'ont aucun
// appelant et le joueur ne peut JAMAIS inviter en groupe/guilde/alliance — il ne peut que
// recevoir (les handlers 0x2e/0x34/... sont tous câblés). Asymétrie prouvée, corrigée ici.
//
// ---------------------------------------------------------------------------
// CYCLE D'ORIGINE (les 5 fonctions transcrites ci-dessous)
// ---------------------------------------------------------------------------
//   UI_ClanWin_Open   0x5D8E10  garde arène -> Str(1352) + RETOUR SANS OUVRIR ; sinon
//                               UI_CloseAllDialogs(dword_1821D4C,1) @0x5D8E50, [2]=1,
//                               [3..11]=0, [12]=1, strcpy nom, [17..20]=a3..a6.
//   UI_ClanWin_Close  0x5D8ED0  [2]=0.
//   UI_ClanWin_OnLDown 0x5D8EF0 hit-test -> Snd3D_PlayScaledVolume(flt_1487E3C,..,0,100,1)
//                               + latch=1, return 1.
//   UI_ClanWin_OnLUp  0x5D92A0  Close() PUIS gardes PUIS émission (cf. .cpp, ancres par branche).
//   UI_ClanWin_Draw   0x5DA210  fond centré + 6 boutons (3 états : pressé/survol/normal).
//
// ---------------------------------------------------------------------------
// LAYOUT PROUVÉ de g_ClanWin (dwords, 0x5D8E10 / 0x5D92A0 / 0x5DA210 concordants)
// ---------------------------------------------------------------------------
//   [0] x   [1] y   [2] visible   [3..11] 9 latches de bouton   [12] mode (1|2)
//   +52     nom de la cible (13 o NUL-terminé ; `this+52` = payload des builders)
//   [17] level        (dword_16872A0[227*i] du joueur ciblé, passé par l'ouvreur)
//   [18] levelBonus   (dword_16872A4[227*i])
//   [19] dword_168731C[227*i]  — rôle non établi, PAS lu par OnLUp/Draw (conservé
//        par fidélité de signature : Open l'écrit @0x5D8EBC)
//   [20] element      (dword_168728C[227*i])
//
// Boutons — MODE 1 (fond unk_8F7608 centré ; tous à x+12, pitch 26) :
//   [3]  y+28   unk_8F9134  -> passe en mode 2 (PAS de Close)
//   [4]  y+54   unk_8FB634  -> Op47 @0x5D94B1  + NoticeDlg(5, Str357)
//   [5]  y+80   unk_92DC1C  -> Op53 @0x5D9685  + NoticeDlg(6, Str491)   « invitation groupe »
//   [6]  y+106  unk_923880  -> Op59 @0x5D98E6  + NoticeDlg(9, Str506)
//   [7]  y+132  unk_8F8A44  -> Op65 @0x5D9BDC  + NoticeDlg(7, Str359)
//   [8]  y+158  unk_8F8C00  -> Op72 @0x5D9D71  + NoticeDlg(8, Str397)
//   [9]  y+184  unk_8F7FDC  -> Close seul
// Boutons — MODE 2 (fond unk_941AA8 centré) :
//   [10] (x+165, y+90) unk_941B3C -> Op43(nom, 2) @0x5D9F8A + NoticeDlg(4, Str356)
//   [11] (x+241, y+90) unk_941C64 -> Op43(nom, 1) @0x5DA0F1 + NoticeDlg(4, Str356)
//
// ---------------------------------------------------------------------------
// CÂBLAGE (à faire suivre — fichiers NON possédés par ce front)
// ---------------------------------------------------------------------------
// Cette fenêtre est ENREGISTRÉE dans UIManager par GameWindows::Init : elle est donc
// rendue ET cliquable dès qu'un appelant invoque OpenForPlayer(). Mais les DEUX ouvreurs
// du binaire (xrefs_to(0x5D8E10) = 2, vérifiés) sont tributaires du picking d'entité,
// absent du C++ (gap G-PICK-05, autre front) :
//   (a) Player_InteractWithPlayer 0x5392E0 @0x539514 — appelée uniquement par
//       Game_OnWorldLeftClick 0x536690 (0x5371D9/0x537201/0x537224). Gardes du site
//       d'appel : Math_Dist3D(cible, self) <= 30.0 @0x5393DE, g_PendingOrderKind == 1,
//       !dword_1675B00 && Char_IsAttackAction(g_LocalPlayerSheet), et
//       dword_1687428[227*i] == 0 (sinon UI_StorageWin_Open(...,2,...) @0x539534).
//   (b) Player_AutoInteractPlayer 0x5396F0 @0x539887 — appelée uniquement par
//       Combat_TickAttackState 0x574BD0.
// => Ligne exacte à poser une fois G-PICK-05 livré (cf. rapport, wiringTodoForOrchestrator).
#pragma once
#include "UI/UIManager.h"
#include <string>

namespace ts2::ui {

// ClanContextMenu — transposition de UI_ClanWin (dword_1822938). Panneau plat
// (UiContext::FillRect/DrawFrame/Text) au lieu des sprites .IMG, même parti pris
// pragmatique que MsgBoxDialog (UI/UIManager.h:147-148) : la LOGIQUE (gardes,
// émissions, latches, modes) est fidèle, le SKIN ne l'est pas.
class ClanContextMenu : public Dialog {
public:
    // [12] mode — 1 = menu à 6 entrées, 2 = confirmation à 2 boutons (Op43).
    static constexpr int kModeMenu    = 1; // 0x5D8E8A ([12] = 1 à l'ouverture)
    static constexpr int kModeConfirm = 2; // 0x5D92E9 ([12] = 2 via le bouton [3])

    // UI_ClanWin_Open 0x5D8E10. NOMMÉE `OpenForPlayer` et NON `Open` : `Dialog::Open()`
    // (0 argument) est appelée par le framework ; une surcharge à 5 arguments la
    // masquerait (name hiding). Même précédent que PartyWindow::OpenMemberSelect
    // (UI/PartyWindow.h:88-101).
    // `field19` = [19] (dword_168731C[227*i]) : écrit @0x5D8EBC, jamais relu par le
    // binaire — conservé pour ne pas amputer la signature d'origine.
    void OpenForPlayer(const std::string& targetName, int level, int levelBonus,
                       int field19, int element);

    void Close() override;                              // UI_ClanWin_Close 0x5D8ED0
    bool OnMouseDown(int x, int y) override;            // UI_ClanWin_OnLDown 0x5D8EF0
    bool OnClick(int x, int y) override;                // UI_ClanWin_OnLUp   0x5D92A0
    void Render(const UiContext& ctx, int cursorX, int cursorY) override; // 0x5DA210

    int                Mode()       const { return mode_; }       // [12]
    const std::string& TargetName() const { return targetName_; } // this+52

private:
    struct Rect { int x, y, w, h; };

    // Indices de latch : latch_[k] <-> champ [k+3] du binaire.
    enum LatchId {
        kLatchToConfirm = 0, // [3]  -> mode 2
        kLatchOp47      = 1, // [4]
        kLatchOp53      = 2, // [5]  invitation de groupe
        kLatchOp59      = 3, // [6]
        kLatchOp65      = 4, // [7]
        kLatchOp72      = 5, // [8]
        kLatchCloseMenu = 6, // [9]
        kLatchOp43Two   = 7, // [10] mode 2, Op43(nom, 2)
        kLatchOp43One   = 8, // [11] mode 2, Op43(nom, 1)
        kLatchCount     = 9  // boucle `for (i=0; i<9; ++i) *(this+i+3) = 0` @0x5D8E5F
    };

    // Géométrie recalculée à chaque frame (le binaire recentre dans _Draw, _OnLDown ET
    // _OnLUp — les trois refont le même calcul depuis nWidth/nHeight).
    void LayoutMenu(int screenW, int screenH, Rect& panel, Rect btns[7]) const;
    void LayoutConfirm(int screenW, int screenH, Rect& panel, Rect& btnTwo, Rect& btnOne) const;

    // Émissions (chacune porte ses gardes + son ancre ; cf. .cpp).
    void FireOp47();
    void FireOp53();
    void FireOp59();
    void FireOp65();
    void FireOp72();
    void FireOp43(int8_t flag); // flag 2 = bouton [10], flag 1 = bouton [11]

    std::string targetName_;                 // this+52 (13 o côté binaire)
    int         mode_       = kModeMenu;     // [12]
    int         level_      = 0;             // [17]
    int         levelBonus_ = 0;             // [18]
    int         field19_    = 0;             // [19] (écrit, jamais relu)
    int         element_    = 0;             // [20]
    bool        latch_[kLatchCount] = {};    // [3..11]

    // Dims écran du dernier Render : le hit-test (routé entre deux frames) doit
    // s'aligner sur la géométrie dessinée — même idiome que MsgBoxDialog
    // (UI/UIManager.h:180-184).
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;
};

} // namespace ts2::ui

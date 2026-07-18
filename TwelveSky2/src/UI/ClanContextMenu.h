// UI/ClanContextMenu.h — player context menu (UI_ClanWin, g_ClanWin dword_1822938).
//
// Gap SGP-1. This is the ONLY player-to-player path in the binary (cf. UI/PlayerTradeWindow.h:18-23,
// which already documented it): without it, the 5 builders Net_SendOp47/53/59/65/72 have no
// caller and the player can NEVER invite to a group/guild/alliance — only receive (handlers
// 0x2e/0x34/... are all wired). Proven asymmetry, fixed here.
//
// ---------------------------------------------------------------------------
// ORIGINAL LIFECYCLE (the 5 functions transcribed below)
// ---------------------------------------------------------------------------
//   UI_ClanWin_Open   0x5D8E10  arena guard -> Str(1352) + RETURN WITHOUT OPENING; otherwise
//                               UI_CloseAllDialogs(dword_1821D4C,1) @0x5D8E50, [2]=1,
//                               [3..11]=0, [12]=1, strcpy name, [17..20]=a3..a6.
//   UI_ClanWin_Close  0x5D8ED0  [2]=0.
//   UI_ClanWin_OnLDown 0x5D8EF0 hit-test -> Snd3D_PlayScaledVolume(flt_1487E3C,..,0,100,1)
//                               + latch=1, return 1.
//   UI_ClanWin_OnLUp  0x5D92A0  Close() THEN guards THEN emission (cf. .cpp, per-branch anchors).
//   UI_ClanWin_Draw   0x5DA210  centered background + 6 buttons (3 states: pressed/hover/normal).
//
// ---------------------------------------------------------------------------
// PROVEN LAYOUT of g_ClanWin (dwords, 0x5D8E10 / 0x5D92A0 / 0x5DA210 concordant)
// ---------------------------------------------------------------------------
//   [0] x   [1] y   [2] visible   [3..11] 9 button latches   [12] mode (1|2)
//   +52     target name (13-byte NUL-terminated ; `this+52` = builder payload)
//   [17] level        (dword_16872A0[227*i] of the targeted player, passed by the opener)
//   [18] levelBonus   (dword_16872A4[227*i])
//   [19] dword_168731C[227*i]  — role not established, NOT read by OnLUp/Draw (kept for
//        signature fidelity: Open writes it @0x5D8EBC)
//   [20] element      (dword_168728C[227*i])
//
// Buttons — MODE 1 (unk_8F7608 background centered ; all at x+12, pitch 26):
//   [3]  y+28   unk_8F9134  -> switches to mode 2 (NO Close)
//   [4]  y+54   unk_8FB634  -> Op47 @0x5D94B1  + NoticeDlg(5, Str357)
//   [5]  y+80   unk_92DC1C  -> Op53 @0x5D9685  + NoticeDlg(6, Str491)   "group invite"
//   [6]  y+106  unk_923880  -> Op59 @0x5D98E6  + NoticeDlg(9, Str506)
//   [7]  y+132  unk_8F8A44  -> Op65 @0x5D9BDC  + NoticeDlg(7, Str359)
//   [8]  y+158  unk_8F8C00  -> Op72 @0x5D9D71  + NoticeDlg(8, Str397)
//   [9]  y+184  unk_8F7FDC  -> Close only
// Buttons — MODE 2 (unk_941AA8 background centered):
//   [10] (x+165, y+90) unk_941B3C -> Op43(name, 2) @0x5D9F8A + NoticeDlg(4, Str356)
//   [11] (x+241, y+90) unk_941C64 -> Op43(name, 1) @0x5DA0F1 + NoticeDlg(4, Str356)
//
// ---------------------------------------------------------------------------
// WIRING (still to do — files NOT owned by this front)
// ---------------------------------------------------------------------------
// This window is REGISTERED in UIManager by GameWindows::Init: it is therefore rendered AND
// clickable as soon as a caller invokes OpenForPlayer(). But the binary's TWO openers
// (xrefs_to(0x5D8E10) = 2, verified) depend on entity picking, absent from the C++
// (gap G-PICK-05, another front):
//   (a) Player_InteractWithPlayer 0x5392E0 @0x539514 — called only by
//       Game_OnWorldLeftClick 0x536690 (0x5371D9/0x537201/0x537224). Call-site guards:
//       Math_Dist3D(target, self) <= 30.0 @0x5393DE, g_PendingOrderKind == 1,
//       !dword_1675B00 && Char_IsAttackAction(g_LocalPlayerSheet), and
//       dword_1687428[227*i] == 0 (otherwise UI_StorageWin_Open(...,2,...) @0x539534).
//   (b) Player_AutoInteractPlayer 0x5396F0 @0x539887 — called only by
//       Combat_TickAttackState 0x574BD0.
// => Exact hookup line to add once G-PICK-05 ships (cf. report, wiringTodoForOrchestrator).
#pragma once
#include "UI/UIManager.h"
#include <string>

namespace ts2::ui {

// ClanContextMenu — transposition of UI_ClanWin (dword_1822938). Flat panel
// (UiContext::FillRect/DrawFrame/Text) instead of .IMG sprites, same pragmatic choice as
// MsgBoxDialog (UI/UIManager.h:147-148): the LOGIC (guards, emissions, latches, modes) is
// faithful, the SKIN is not.
class ClanContextMenu : public Dialog {
public:
    // [12] mode — 1 = 6-entry menu, 2 = 2-button confirmation (Op43).
    static constexpr int kModeMenu    = 1; // 0x5D8E8A ([12] = 1 at open)
    static constexpr int kModeConfirm = 2; // 0x5D92E9 ([12] = 2 via button [3])

    // UI_ClanWin_Open 0x5D8E10. NAMED `OpenForPlayer`, NOT `Open`: `Dialog::Open()`
    // (0 args) is called by the framework; a 5-arg overload would hide it (name hiding).
    // Same precedent as PartyWindow::OpenMemberSelect (UI/PartyWindow.h:88-101).
    // `field19` = [19] (dword_168731C[227*i]): written @0x5D8EBC, never read back by the
    // binary — kept so as not to truncate the original signature.
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

    // Latch indices: latch_[k] <-> binary field [k+3].
    enum LatchId {
        kLatchToConfirm = 0, // [3]  -> mode 2
        kLatchOp47      = 1, // [4]
        kLatchOp53      = 2, // [5]  group invite
        kLatchOp59      = 3, // [6]
        kLatchOp65      = 4, // [7]
        kLatchOp72      = 5, // [8]
        kLatchCloseMenu = 6, // [9]
        kLatchOp43Two   = 7, // [10] mode 2, Op43(name, 2)
        kLatchOp43One   = 8, // [11] mode 2, Op43(name, 1)
        kLatchCount     = 9  // loop `for (i=0; i<9; ++i) *(this+i+3) = 0` @0x5D8E5F
    };

    // Geometry recomputed every frame (the binary recenters in _Draw, _OnLDown AND
    // _OnLUp — all three redo the same computation from nWidth/nHeight).
    void LayoutMenu(int screenW, int screenH, Rect& panel, Rect btns[7]) const;
    void LayoutConfirm(int screenW, int screenH, Rect& panel, Rect& btnTwo, Rect& btnOne) const;

    // Emissions (each carries its own guards + anchor; cf. .cpp).
    void FireOp47();
    void FireOp53();
    void FireOp59();
    void FireOp65();
    void FireOp72();
    void FireOp43(int8_t flag); // flag 2 = button [10], flag 1 = button [11]

    std::string targetName_;                 // this+52 (13 bytes in the binary)
    int         mode_       = kModeMenu;     // [12]
    int         level_      = 0;             // [17]
    int         levelBonus_ = 0;             // [18]
    int         field19_    = 0;             // [19] (written, never read back)
    int         element_    = 0;             // [20]
    bool        latch_[kLatchCount] = {};    // [3..11]

    // Screen dims from the last Render: the hit-test (routed between two frames) must
    // stay aligned with the drawn geometry — same idiom as MsgBoxDialog
    // (UI/UIManager.h:180-184).
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;
};

} // namespace ts2::ui

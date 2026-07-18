// UI/PlayerTradeWindow.h — "player-to-player trade" window.
//
// FIDELITY WARNING — THIS WINDOW HAS NO COUNTERPART IN THE BINARY
//
// Exhaustively verified (Pass 4, wave W6, fresh decompilation): TwelveSky2.exe
// implements NO player-to-player trade table. There is no traded-item grid, no gold
// deposit, no half-lock, no "accept" button. The actions "add/remove an item, place
// gold, lock, accept" — which this window draws — simply do not exist in this build.
//
// EVIDENCE (do not reopen this debate without contradicting it):
//  1. UI_InitAllDialogs 0x5ABF50 enumerates the ~38 HUD dialog singletons: NONE is a
//     trade table. The only item-grid window is dword_1822990 (UI_StorageWin_Open
//     0x5D27A0), which only knows 5 modes — 1 = NPC vendor, 2 = a player's SHOP
//     (emits Net_SendPacket_Op33 @0x5D2C24), 3/4/5 = warehouse (Net_SendPacket_Op108).
//     No accept/lock/gold/add-item.
//  2. The only player<->player path is Player_InteractWithPlayer 0x5392E0 ->
//     UI_ClanWin_Open (player context menu) -> UI_ClanWin_OnLUp 0x5D92A0 page 2, whose
//     2 "trade" buttons emit Net_SendOp43(name13, 2) @0x5D9F8A and Net_SendOp43(name13,
//     1) @0x5DA0F1 (op 0x2B, len 26 — the builder's 2 ONLY xrefs), then open a passive
//     NOTICE (UI_NoticeDlg_Open) with no emitting button at all.
//  3. The 3 server replies are PURELY TEXTUAL — they open no window and populate no
//     grid:
//       0x31 Pkt_TradeRequestPrompt 0x48FD20: memcpy(v7, 0x8156C1, 12) then
//            g_TradePartnerIdLo=v7[0] @0x48fd8f, dword_1687420=v7[1] @0x48fd97,
//            dword_1687424=v7[2] @0x48fda0, dword_1675D84=v5 @0x48fdac; a sound + one
//            system line "%s [%d]%s" (StrTable005 314/315). NOTHING else.
//       0x32 Pkt_TradeRequestResult 0x48FE10: one system line, period.
//       0x33 Pkt_TradeActionResult  0x48FEA0: switch(code 0..3) -> system line, then
//            zeroes g_PendingOrderKind/g_TradePartnerIdLo/dword_1687420/dword_1687424
//            and sets dword_1675D84=1 (@0x48ff66-0x48ff8e).
// => Emitting NOTHING from HandleAccept()/HandleCancel() is therefore CORRECT: these
//    aren't missing builders, these packets simply don't exist. The 5x5 grids below
//    are a deliberate REINVENTION (the window is instantiated by UI/GameWindows.h:190,
//    a header not owned by this front -> cannot be removed here).
//
// ORIGINAL GLOBALS — TWO WRONG LABELS FIXED (W4-F3, re-confirmed W6)
//
//   dword_168741C  g_TradePartnerIdLo — partner identity, word 0 (0x8156C1+0)
//   dword_1687420                     — partner identity, word 1 (0x8156C1+4)
//   dword_1687424                     — partner identity, word 2 (0x8156C1+8),
//        written by the SERVER via Pkt_TradeRequestPrompt 0x48FD20 @0x48fda0, then
//        compared against the ACTION CODE received by Pkt_TradeActionResult 0x48FEA0
//        @0x48fefe (`if (dword_1687424[0] == v7)` -> StrTable005 #318 else #319).
//        WARNING: this is NOT a "local agreement" — label REFUTED, do not reintroduce it.
//   dword_1675B24  = g_PendingOrderKind — world order / targeting type
//        (Player_InteractWithPlayer 0x5392E0, Game_OnWorldLeftClick 0x536690,
//        Player_CastSkill 0x53BC40, AutoPlay_UpdateTargeting 0x45D080).
//        WARNING: this is NOT a "trade state" — label REFUTED. The trade handlers
//        only zero it as a SIDE EFFECT (targeting reset when a prompt arrives). It is
//        neither read nor displayed by this window.
//   dword_1675D84  — written by 0x31 (= word read at 0x8156D1, @0x48fdac) and forced to
//        1 by 0x33 (@0x48ff8e). Its WRITE side is proven; its SEMANTICS are not ->
//        not displayed (don't invent a role for it).
//
// WIRING: nothing calls Open()/Close() from the network handlers — and that is
// FAITHFUL, since no original packet opens a trade table. Today the window only opens
// via the 'T' key (hotkeys::kPlayerTrade, UI/GameWindows.h:70 -> UI/GameWindows.cpp:135),
// whose comment "normal opening = server packet 0x31" is WRONG (0x31 opens nothing, see
// evidence 3 above): header not owned by this front, corrected via a note in the wave
// report.
//
// Project rule: this file edits NO existing header; it includes UI/UIManager.h and
// Game/ClientRuntime.h read-only.
#pragma once
#include "UI/UIManager.h"
#include "Game/ClientRuntime.h"

#include <cstdint>
#include <string>

namespace ts2::ui {

class PlayerTradeWindow : public Dialog {
public:
    PlayerTradeWindow() = default;

    // Open/close (Dialog::Open/Close). Opening re-arms the button latches; no
    // data is modified (the window only READS g_Client.Var(...) on each Render,
    // it never initializes it).
    void Open() override;
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x, y, w, h; };

    // Geometry recalculated from screen dimensions (like MsgBoxDialog::Layout,
    // UI/UIManager.cpp); lastScreenW_/H_ store the geometry actually drawn so
    // hit-testing (routed across two frames) stays aligned.
    void Layout(int screenW, int screenH, Rect& box, Rect& closeBtn,
                Rect& acceptBtn, Rect& cancelBtn,
                Rect& selfGrid, Rect& partnerGrid) const;

    // Draws a kGridRows x kGridCols grid of EMPTY cells (placeholder, see the
    // fidelity note at the top of the file) + its label above.
    void DrawGridPlaceholder(const UiContext& ctx, const Rect& grid,
                              const char* label) const;

    void HandleAccept(); // "Accept" button
    void HandleCancel(); // "Cancel" button / close (X)

    // --- Button latches (armed on press, validated on release inside,
    //     MsgBoxDialog::btnPressed_ pattern) ---
    bool closePressed_  = false;
    bool acceptPressed_ = false;
    bool cancelPressed_ = false;

    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // --- Geometry (1024x768 reference, cf. WarehouseWindow.h) ---
    static constexpr int kGridCols  = 5;
    static constexpr int kGridRows  = 5;
    static constexpr int kCellSize  = 26;
    static constexpr int kCellGap   = 3;
    static constexpr int kGridW     = kGridCols * kCellSize + (kGridCols - 1) * kCellGap;
    static constexpr int kGridH     = kGridRows * kCellSize + (kGridRows - 1) * kCellGap;

    static constexpr int kPanelPad  = 16;
    static constexpr int kGridGap   = 28;   // gap between the two grids
    static constexpr int kHeaderH   = 26;   // title bar
    static constexpr int kGridLabelH= 16;   // "Vous"/"Partenaire" label
    static constexpr int kInfoH     = 34;   // partner/agreement diagnostic line
    static constexpr int kNoteH     = 16;   // "25 cells (...)" note
    static constexpr int kFooterH   = 44;   // button row
    static constexpr int kCloseSize = 18;
    static constexpr int kBtnW      = 110;
    static constexpr int kBtnH      = 26;

    static constexpr int kPanelW = kPanelPad * 2 + kGridW * 2 + kGridGap;
    static constexpr int kPanelH = kHeaderH + kPanelPad + kGridLabelH + kGridH
                                  + kNoteH + kInfoH + kFooterH + kPanelPad;

    // --- Palette (D3DCOLOR = 0xAARRGGBB, cf. mission spec) ---
    static constexpr D3DCOLOR kColBg       = 0xE0202028u; // panel background
    static constexpr D3DCOLOR kColFrame    = 0xFF808080u; // frame
    static constexpr D3DCOLOR kColHeaderBg = 0xFF2A2A34u; // title bar
    static constexpr D3DCOLOR kColTitle    = 0xFFFFDD66u; // title
    static constexpr D3DCOLOR kColText     = 0xFFFFFFFFu; // text
    static constexpr D3DCOLOR kColTextDim  = 0xFFAAAAAAu; // dimmed text (diagnostic)
    static constexpr D3DCOLOR kColHover    = 0xFF4060A0u; // hover
    static constexpr D3DCOLOR kColDanger   = 0xFFE04040u; // "HP" color reused as Cancel accent
    static constexpr D3DCOLOR kColCellBg   = 0xFF1A1A20u; // empty cell (placeholder)
    static constexpr D3DCOLOR kColBtnBg    = 0xFF383850u; // button (idle)
    static constexpr D3DCOLOR kColBtnDown  = 0xFF2A2A3Au; // button (pressed)
    static constexpr D3DCOLOR kColCloseBg  = 0xFF483030u; // close button (idle)
};

} // namespace ts2::ui

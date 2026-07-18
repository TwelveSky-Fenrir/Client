// UI/GuildWindow.h — "Guild" window: internal roster (50 members) + guild actions.
//
// ts2::ui::Dialog (UI/UIManager.h) wired onto ts2::game::g_Guild
// (Game/GuildSystem.h, already written — see that header for the detailed original
// layout). This window READS g_Guild via its public API (CountMembers/AddMember) and
// draws on top of it using the UiContext primitives.
//
// NETWORK — FAITHFUL pattern (Pass 4 / wave W6)
// The binary has only ONE network object, g_NetClient 0x8156A0, addressed GLOBALLY: the
// Net_Send* builders read it DIRECTLY without ever receiving it as a parameter
// (Guild_AddMemberFromInput 0x66BCD0 @0x66bd5b calls Net_SendOp76 without a socket).
// This window therefore uses net::GlobalNetClient() (Net/NetClient.h:67-68), populated
// by ConnectLoginServer/ConnectGameServer (Net/Login.cpp:131/313).
//
// W6 FIX (proven Pass-3 defect): the former Bind(net::NetClient*)/net_ pair is
// REMOVED. Bind() was called NOWHERE in the composition (verified: only
// skillTree_.Bind(...) exists, UI/GameWindows.cpp:72) -> net_ was always
// null -> ConfirmAdd()'s `if (net_)` was DEAD CODE and the project's only guild
// emission never went out. GlobalNetClient() restores the binary's singleton without
// dependency injection.
//
// LAYOUT DEVIATION (assumed, already documented in Pass 3 — unchanged here)
// The REAL original window (UI_GuildMgrWnd_Open/OnClick/Render 0x667E20/0x668B70/
// 0x66A2E0, state g_Guild 0x1839968) is a 5-page state machine (`*(this+426)`:
// 1=roster, 2=invite, 3=announcement, 4=rank, 5=alliance) showing 10 rows per PAGE
// (`*(this+427)` = page 0..4) across 50 slots — NOT a continuous scroll as here — and
// routes its destructive actions through a confirmation MsgBox (UI_MsgBox_Open
// 0x5C08C0, dword_1822438) whose release (UI_MsgBox_OnLButtonUp 0x5C1170)
// performs the send. This window is a pragmatic reinvention: only the screen
// CENTERING is proven bit-exact (cf. UI/GuildWindow.cpp::ComputeGeometry).
// The EMISSIONS and their GUARDS, however, are reproduced faithfully (see the .cpp).
//
// Interactions:
//   - Scrollable list of non-empty members (name + rank), 10 visible rows out of 50
//     max slots, up/down scroll buttons. Clicking a row = selection
//     (`*(this+428)` originally, -1 = none).
//   - "Add"      -> name input   -> Net_SendOp76           (op 0x4C)
//   - "Rank"     -> rank input   -> Net_SendGuarded_10     (Op75 sub-op 10)
//   - "Leave"    -> Net_SendGuarded_4                      (Op75 sub-op 4)
//   - "Dissolve" -> Net_SendGuarded_6                      (Op75 sub-op 6)
//   - "X" on a row -> kick, Net_SendGuarded_8               (Op75 sub-op 8)
//   - "X" on the title bar -> closes the window (Dialog::Close()).
//   Each action reproduces the binary's guards (master / selected row / target
//   != self) and their StrTable005 refusal messages — details and exact EA
//   anchors in UI/GuildWindow.cpp.
//
// Assumed limitation (documented): the UIManager only routes WM_KEYDOWN (OnKey(vk)),
// not WM_CHAR — input therefore only accepts digits/uppercase/space (the
// VK_0..VK_9 and VK_A..VK_Z codes coincide with their ASCII codes on Win32), no accents/
// lowercase/punctuation. See ChatWindow (a non-Dialog module of the project) for an example
// that receives a real WM_CHAR via a dedicated route — not available here.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "UI/UIManager.h"
#include "UI/Widgets.h"
#include "Game/GuildSystem.h"

namespace ts2::ui {

class GuildWindow : public Dialog {
public:
    GuildWindow();

    void Open() override;
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    // --- Geometry ---------------------------------------------------------
    struct Rect { int x = 0, y = 0, w = 0, h = 0; };
    struct Geom {
        Rect panel, header, closeBtn;
        Rect listArea, scrollUp, scrollDown;
        Rect actionRow, addBtn, rankBtn, leaveBtn, dissolveBtn;
        Rect editBox, confirmBtn, cancelBtn;
        Rect feedbackArea;
    };

    // Current input mode — degraded analog of the binary's `*(g_Guild+426)` page
    // (UI_GuildMgrWnd_OnClick 0x668B70): None ~ page 1 (roster), AddMember ~ page 2
    // (invite, 0x668E29 `*(this+426)=2`), SetRank ~ page 4 (rank, 0x6694F3
    // `*(this+426)=4`). Pages 3 (announcement) and 5 (alliance) are not ported —
    // cf. TODO in the .cpp.
    enum class InputMode { None, AddMember, SetRank };

    enum class PressedBtn { None, Close, Add, Rank, Leave, Dissolve,
                            ScrollUp, ScrollDown, Confirm, Cancel, Kick, Row };

    Geom ComputeGeometry(int screenW, int screenH) const;
    Rect RowRect(const Geom& g, int rowOnScreen) const;
    Rect KickRect(const Geom& g, int rowOnScreen) const;

    // Indices [0..49] of the non-empty slots (game::g_Guild.members[i].Empty()==false).
    std::vector<int> VisibleMemberIndices() const;
    int MaxScroll() const;

    // `*(g_Guild+28) == g_SelfName` (Crt_Strcmp, UI_GuildMgrWnd_OnClick 0x668B70
    // @0x668DEF/0x66935E/0x669706/0x66984C…) — cf. fidelity note in the .cpp.
    bool IsSelfGuildMaster() const;

    // Name of the selected member, or "" if no row selected (`*(this+428) == -1`).
    std::string SelectedMemberName() const;

    void Confirm();      // validates the current input according to mode_
    void CancelInput();  // closes the input without sending

    void DoKick();       // row "X"      -> Net_SendGuarded_8
    void DoLeave();      // "Leave"      -> Net_SendGuarded_4
    void DoDissolve();   // "Dissolve"   -> Net_SendGuarded_6

    void SetFeedback(const std::string& text, D3DCOLOR color);

    // --- State ----------------------------------------------------------------
    InputMode   mode_         = InputMode::None; // active input mode (cf. enum)
    int         scrollOffset_ = 0;     // scroll offset (rows from the top)
    int         selectedIdx_  = -1;    // `*(g_Guild+428)`: selected slot, -1 = none
    EditBox     nameEdit_;             // input field (name or rank depending on mode_)

    std::string feedback_;                  // last local message
    D3DCOLOR    feedbackColor_ = 0xFF60FF60u; // success by default
    float       feedbackUntil_ = -1.0f;       // expiration timestamp (ctx.gameTimeSec)

    PressedBtn  pressedBtn_     = PressedBtn::None; // latch armed on OnMouseDown
    int         pressedKickIdx_ = -1;               // member index targeted by the armed "X"
    int         pressedRowIdx_  = -1;               // member index targeted by the armed row

    // Screen dims + clock cached from the last Render (hit-testing, routed between
    // frames, must line up with the actually-drawn geometry). Same pattern as
    // MsgBoxDialog::lastScreenW_/lastScreenH_ in UIManager.cpp.
    mutable int   lastScreenW_     = ts2::kRefWidth;
    mutable int   lastScreenH_     = ts2::kRefHeight;
    mutable float lastGameTimeSec_ = 0.0f;

    // --- Geometry constants (panel ~300x284, 10 visible rows) ---
    static constexpr int kPanelW        = 300;
    static constexpr int kHeaderH       = 28;
    static constexpr int kVisibleRows   = 10;
    static constexpr int kRowH          = 18;
    static constexpr int kListGap       = 8;
    static constexpr int kActionH       = 26;
    static constexpr int kFeedbackH     = 16;
    static constexpr int kBottomMargin  = 10;
    static constexpr int kMargin        = 8;
    static constexpr int kCloseBtnSize  = 18;
    static constexpr int kScrollBtnSize = 16;
    static constexpr int kKickBtnSize   = 16;
    static constexpr int kPanelH        = kHeaderH + kListGap + kVisibleRows * kRowH +
                                           kListGap + kActionH + kListGap + kFeedbackH +
                                           kBottomMargin;
    static constexpr float kFeedbackDurationSec = 3.0f;

    // Max length of the "rank" input: GetWindowTextA(dword_1668FF4, this+1945, 5)
    // (UI_GuildMgrWnd_OnClick 0x668B70 @0x669e43) -> 5 NUL-terminated bytes = 4 characters.
    static constexpr int kRankMaxChars = 4;
};

} // namespace ts2::ui

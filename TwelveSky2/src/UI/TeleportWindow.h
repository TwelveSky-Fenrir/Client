// UI/TeleportWindow.h — PAGE 76 of the `cNpcWin` mega-window: paid teleportation
// to one of the faction's 4 destinations (service code 76 / 0x4C of the NPC menu).
//
// ANCHORING (Pass 4 / wave W11, front `w11-npc-vendor-warp`) — SEVEN binary functions:
//   cTeleportWin_Init            0x627BA0  init page 76 (*(this+180)=76, clears 100 latches)
//   cTeleportWin_OnMouseDown     0x627BF0  hit-test of the 4 rows + close button, arms latch
//   cTeleportWin_OnCommit        0x627D50  release: level/var guards + arms+emits Op20
//   cTeleportWin_Draw            0x628030  list rendering (3 color states per row)
//   cTeleportWin_GetSlotCount    0x628250  = return 4      (number of destinations)
//   cTeleportWin_GetDestMapId    0x6282D0  slot 0..3 -> mapId {313,316,331,334}, default 0
//   cTeleportWin_FormatEntryLabel 0x628260 "zoneName suffix" (StrTable003 + StrTable005[225])
//
// LIVENESS (call chain proven in the IDB, each with a unique xref):
//   INIT   : App_WndProc 0x461930 -> Input_OnLButtonUp 0x50AD20 -> UI_RouteLButtonUp 0x5AD0F0
//            -> UI_NpcWin_OnLUp_Dispatch 0x5DD3B0 -> UI_NpcMenu_OnLUp 0x5DF640
//            @0x5dfad4 (jumptable 005DF72C case 76) -> cTeleportWin_Init 0x627BA0.
//   DOWN   : ... UI_NpcWin_OnLDown_Dispatch 0x5DCB10 @0x5dd24a -> cTeleportWin_OnMouseDown.
//   COMMIT : ... UI_NpcWin_OnLUp_Dispatch 0x5DD3B0 @0x5ddaea (case 76) -> cTeleportWin_OnCommit.
//   DRAW   : Scene_*Render -> UI_RenderAllDialogs 0x5AE2D0 -> UI_NpcWin_Draw_Dispatch 0x5DE180
//            @0x5de765 -> cTeleportWin_Draw 0x628030.
//
// OBJECT MODEL: in the binary, page 0 (menu, UI/NpcDialogWindow) and page 76 (this class)
// are the SAME cNpcWin object; the field *(this+180) (page id) switches the
// OnLDown/OnLUp/Draw dispatchers from one handler set to another. The two pages are therefore
// MUTUALLY EXCLUSIVE (never drawn at the same time). On the C++ side they are modeled as TWO
// separate Dialog classes: NpcDialogWindow::DispatchService(76) OPENS this window and CLOSES
// the menu, which faithfully reproduces the page swap (see NpcDialogWindow::OpenTeleportPage).
//
// WARNING WIRING (outside my files, flagged to the orchestrator):
//   1. UI/GameWindows.h/.cpp must OWN a `teleport_` instance, REGISTER it with
//      UIManager (without which Render/OnMouseDown/OnClick are never routed), and call
//      `npcDialog_.SetTeleportWindow(&teleport_)` (without which service code 76 opens nothing).
//   2. PRE-EXISTING UPSTREAM defect, NOT introduced here: NpcDialogWindow::Open() is called
//      nowhere (3D-world NPC-click routing is not ported). The NPC menu — hence
//      destination 76 — remains unreachable until that front exists. TeleportWindow
//      INHERITS this block, it does not worsen it.
#pragma once
#include "UI/UIManager.h"

namespace ts2::ui {

// Page 76 "paid teleportation" of cNpcWin. Has only 4 fixed destinations (GetSlotCount
// 0x628250 = 4) mapped to constant mapIds (GetDestMapId 0x6282D0).
class TeleportWindow : public Dialog {
public:
    TeleportWindow();

    // Number of destinations (cTeleportWin_GetSlotCount 0x628250 @0x62825c: return 4).
    static constexpr int kSlotCount = 4;

    // cTeleportWin_Init 0x627BA0: *(this+180)=76 then clears the 100 latches *(this+70..169).
    // Only the first 5 (close + 4 rows) are used by this page.
    void Open() override;
    void Close() override;

    // cTeleportWin_OnMouseDown 0x627BF0 (via UI_NpcWin_OnLDown_Dispatch page 76).
    bool OnMouseDown(int x, int y) override;
    // cTeleportWin_OnCommit 0x627D50 (via UI_NpcWin_OnLUp_Dispatch page 76).
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    // cTeleportWin_Draw 0x628030 (via UI_NpcWin_Draw_Dispatch page 76).
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // Introspection (tests / wiring front). `slot` in [0, kSlotCount).
    // cTeleportWin_GetDestMapId 0x6282D0.
    static int32_t DestMapId(int slot);

private:
    struct Rect { int x, y, w, h; };

    // Recentering on EVERY call, like the binary (nWidth/2 - Sprite2D_GetWidth(&unk_8FE3E0)/2,
    // nHeight/2 - Sprite2D_GetHeight(&unk_8FE3E0)/2 — 0x627c04/0x627d64/0x628054).
    void Recenter(int screenW, int screenH);

    // Rectangle of row `i` — STRICT inequalities from the binary (mx>x+37 && mx<x+217 &&
    // my>y+18i+26 && my<y+18i+38), height 12, pitch 18. Identical EAs in OnMouseDown
    // (0x627cXX), OnCommit (0x627e4c..0x627e85) and Draw hover (0x62818f). NB: this is NOT
    // Dialog::PointInRect (which bounds the left edge with >=) — hence the explicit test.
    bool RowHit(int i, int mx, int my) const;
    // Close button: Sprite2D_HitTest(&unk_8F3798, x+235, y+4, mx, my) — 0x627c68/0x627de0.
    bool CloseButtonHit(int mx, int my) const;

    // "Arms the warp block (mode 6) then emits Op20" — LOCAL transcription of the tail of
    // cTeleportWin_OnCommit 0x627f65..0x628008 (the binary has no shared function; same
    // choice as UI/NpcDialogWindow.cpp::ArmWarpAndSendOp20, identical body except mode 6).
    void ArmWarpAndSendOp20(int32_t zoneId, const float pos[3]);

    // Msg_AppendSystemLine(g_ChatManager, StrTable005_Get(g_LangId, id), g_SysMsgColor).
    static void SysMsg(int strId);

    bool closeLatch_        = false;             // this+70    (armed 0x627c84, read/cleared 0x627da8/db4)
    bool slotLatch_[kSlotCount] = {};            // this+71..74 (armed 0x627d29, read/cleared 0x627e24/e36)

    // Screen dims from the last Render: the hit-test (OnMouseDown/OnClick) is routed between two
    // frames and must align with the drawn geometry (same pattern as MsgBoxDialog /
    // NpcDialogWindow — the binary recenters indifferently in OnLDown/OnLUp/Draw).
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // TODO [ancre unk_8FE3E0 (panel) / unk_8F3798 (close button)]: REAL dimensions =
    // Sprite2D_GetWidth/Height of the UI atlas sprites, not resolved on the ClientSource side. These
    // placeholders are used ONLY for background/frame and recentering; the hit-tests use
    // the binary's EXACT offsets (+37/+217/+18i+26/+18i+38, +235/+4), independent of them.
    static constexpr int kPanelW  = 264;   // >= 235 (close button) + margin
    static constexpr int kPanelH  = 112;   // 26 + 4*18 + bottom margin
    static constexpr int kCloseW  = 20;    // close button placeholder
    static constexpr int kCloseH  = 20;

    static constexpr D3DCOLOR kColBg      = 0xE0202028u; // panel background (PanelSkin fallback)
    static constexpr D3DCOLOR kColBorder  = 0xFF808080u; // frame
    static constexpr D3DCOLOR kColRest    = 0xFFFFFFFFu; // state 1 (resting)
    static constexpr D3DCOLOR kColHover   = 0xFFFFDD66u; // state 3 (hover)
    static constexpr D3DCOLOR kColPressed = 0xFF66AAFFu; // state 2 (latched/pressed)
    static constexpr D3DCOLOR kColClose   = 0xFFCC4040u; // close button (pressed)
};

} // namespace ts2::ui

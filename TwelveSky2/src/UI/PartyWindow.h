// UI/PartyWindow.h — "Party" window (ts2::ui).
//
// ===========================================================================
// AUDIT CORRECTION (Pass 4, wave W6, 2026-07-16) — READ FIRST
// ===========================================================================
// This file's previous banner concluded:
//   "NO UI_PartyWin_*/UI_PartyHud_* function exists in the IDB (unlike
//     UI_ClanWin_* for the Guild): there is NO dedicated 'Party' window."
// The PREMISE was correct, the CONCLUSION was WRONG: the search used the wrong
// prefix. The binary's party window DOES EXIST and is called UI_MemberSelectWnd
// (global object g_MemberSelectWnd 0x184BE38):
//   UI_MemberSelectWnd_Open        0x667260  (arena guard, reset, -> Net_SendOp57)
//   UI_MemberSelectWnd_Close       0x667350  (*(this+2) = 0)
//   UI_MemberSelectWnd_OnMouseDown 0x667370  (hit-test 10 slots + 2 buttons)
//   UI_MemberSelectWnd_OnClick     0x667580  (Close / Confirm -> UI_MsgBox 21)
//   UI_MemberSelectWnd_ProcNet     0x667730  (UI file code 33 = open/close toggle)
//   UI_MemberSelectWnd_Render      0x667860  (screen centering nWidth/2 - w/2)
// It EMITS TWO PACKETS (Op57 = 0x39 on open, Op58 = 0x3A on confirm): the "zero
// network emission" of this file was thus a REAL defect, now fixed (cf. .cpp).
//
// ---------------------------------------------------------------------------
// dword_184BE40 IS NOT AN "ACTIVE PARTY FLAG"
// ---------------------------------------------------------------------------
// The previous banner (and Net/GameHandlers_PartyGuild.cpp:186) document
// dword_184BE40 as "active party flag". This is WRONG: it is field +8 (bOpen) of
// the window object g_MemberSelectWnd 0x184BE38. Proof by layout — 0x67814E /
// 0x67815A (UI_GameHud_OnClick) load `ecx, offset g_MemberSelectWnd` = 0x184BE38
// then call _Open/_Close; the three globals thought to be "scattered" are
// actually fields of a single object:
//   0x184BE38  +0   x                  (0x6673AD : *this = nWidth/2 - w/2)
//   0x184BE3C  +4   y                  (0x6673D2 : nHeight/2 - h/2)
//   0x184BE40  +8   bOpen              (0x66729F : *(this+2)=1 ; guard 0x66737D)
//   0x184BE44  +12  "Close" latch      (*(this+3), armed 0x6674EE)
//   0x184BE48  +16  "Confirm" latch    (*(this+4), armed 0x66753D)
//   0x184BE4C  +20  selected slot      (0x6672D1 : *(this+5) = -1)
//   0x184BE50  +24  values[10]         (0x6672F6 : *(this+j+6) = -2)
// Arithmetic check: 0x184BE38+8 = 0x184BE40, +20 = 0x184BE4C, +24 = 0x184BE50.
//
// CONSEQUENCE (dead-code chain broken by this wave): NO ONE ever wrote
// Var(0x184BE40) — it was only READ, by this file (visibility guard) and by
// Net/GameHandlers_PartyGuild.cpp:186 (handler 0x3f guard). VarGet returns 0 for
// an absent key (Game/ClientRuntime.h:164): both guards were therefore ALWAYS
// false, which made dead (a) this whole panel, (b) handler 0x3f's body, hence
// (c) the roster-pagination Net_SendOp57. Open()/Close() below finally write this
// flag (anchors 0x66729F / 0x667365), which resurrects the chain.
//
// ---------------------------------------------------------------------------
// THIS FILE CARRIES TWO SEPARATE THINGS (do not conflate)
// ---------------------------------------------------------------------------
// 1. The MEMBER SELECTOR (modal, centered) = the binary's real window
//    (UI_MemberSelectWnd). Faithful: data = g_PartyRosterNames (10 slots x 13
//    bytes, 0x1674608 -> game::g_World.partyRoster.names), selection, Close/Confirm,
//    Op57/Op58 emissions. This is wave W6's contribution.
// 2. The "raid frame" HUD PANEL (top-left, teammates' HP/MP) = a pragmatic
//    INVENTION with NO 1:1 equivalent in the binary, inherited from an earlier
//    session and honestly documented as such at the time. Kept as-is (no
//    opportunistic refactor), but its visibility guard is fixed: it NO LONGER
//    depends on Var(0x184BE40) (which means "member selector open", unrelated),
//    only on the presence of at least one resolved member — which is exactly the
//    contract this panel already claimed ("ALWAYS SHOWN as long as the party has
//    at least one resolved member").
//    Reminder from the original audit, still valid: the top-left position is NOT
//    proven by the disassembly; the real UI_GameHud_Render teammate indicators
//    are markers projected in world space (WorldToScreen), not a fixed HUD panel.
//    Do not re-document this position as "real".
//
// ---------------------------------------------------------------------------
// MISSING WIRING (files NOT owned by this wave — to follow up)
// ---------------------------------------------------------------------------
// OpenMemberSelect()/Toggle() are faithful and complete, but NO caller reaches
// them today. The binary's 3 triggers live in files not owned by this wave:
//   (a) Net/GameHandlers_PartyGuild.cpp (handler 0x3e Net_OnPartyMemberNameSet
//       0x4909A0) must call Party().OpenMemberSelect() — anchor 0x4909F8. This is
//       the pagination's KICKOFF: without it, Op57 is never relaunched.
//   (b) UI/GameHud.cpp — toolbar button, anchor 0x678139 (UI_GameHud_OnClick:
//       UI_CloseAllDialogs(dword_1821D4C, 1) then _Open/_Close) -> Party().Toggle().
//   (c) UI/GameWindows.cpp — keyboard shortcut (UI file code 33, anchor 0x667730)
//       -> Party().Toggle(). No hotkeys::kParty exists to date.
// Until (a) is wired, Net/GameHandlers_PartyGuild.cpp:193 (pagination Net_SendOp57)
// remains unreachable EVEN IF Var(0x184BE40) is now written: handler 0x3f is only
// relaunched by the loop kicked off in 0x3e.
// NB registration order: UI/GameWindows.cpp:59 registers party_ at the END of the
// list (= rendered first/at the back, routed last). Correct for the raid frame, but
// the member selector is MODAL and should be routed/rendered first. To be arbitrated
// by the orchestrator (file not owned here).
//
// ---------------------------------------------------------------------------
// Data sources (the selector WRITES; the raid frame is READ-ONLY)
// ---------------------------------------------------------------------------
//   - game::g_World.partyRoster.names : mirror of g_PartyRosterNames 0x1674608
//                                       (10 slots x 13 bytes) — selector's source.
//   - game::g_World.self             : real local-player HP/MP (StatEngine).
//   - game::g_World.players[i]       : visible player entities (dword_1687234,
//                                       stride 908, index 0 = self).
//   - game::g_Client.Var(address)    : mirror of g_MemberSelectWnd fields
//                                       (cf. layout above) and member HP:
//       dword_1687458 + 908*i         : current HP of the member at player slot i
//       dword_168745C + 908*i         : max HP of the member at player slot i
//                                        (PartyMemberHpSet 0x7f / PartyMemberUpdate 0x80).
//
// FAITHFUL LIMITATIONS (no invention):
//   - The selector is indexed by ROSTER SLOT (0..9), exactly like the binary
//     (g_PartyRosterNames): no dubious join happens on the network path —
//     Op57/Op58 carry a roster index, not an entity index.
//   - The raid frame, on the other hand, still cross-references players[i] <->
//     partyRoster.names[i] on a best-effort basis: NO RE proof that the (server-
//     assigned) roster slot matches the entity index. Falls back to a generic
//     label if empty.
//   - Other members' MP bar: PartyMemberHpSet writes the SAME address pair
//     (dword_1687458/168745C) for HP and MP: no distinct address exists for
//     other members' MP. Bar greyed out rather than invented.
//   - Selector geometry: the OFFSETS are proven (slots (x+17, y+81+20*i),
//     Close (x+252, y+24), Confirm (x+214, y+298)); the panel and button
//     DIMENSIONS come from .IMG sprites resolved at runtime
//     (Sprite2D_GetWidth(unk_90265C)) and are NOT known statically -> derived
//     values, flagged TODO in the .cpp.
#pragma once
#include "UI/UIManager.h"
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"

#include <string>
#include <vector>

namespace ts2::ui {

// PartyWindow — carries (1) the modal member selector (faithful, UI_MemberSelectWnd)
// and (2) the auto-hidden "raid frame" HUD panel (kept invention).
class PartyWindow : public Dialog {
public:
    PartyWindow() = default;

    // --- Member selector (the binary's real party window) ---

    // UI_MemberSelectWnd_Open 0x667260: refuses in arena zones (message 1352),
    // otherwise bOpen=1, resets latches/selection/values, then
    // Net_SendOp57(first NON-EMPTY roster slot) — a single send (the binary
    // `return`s inside the loop, 0x667344).
    //
    // WHY NOT `Open()` (override)? UI/GameWindows.cpp:78 calls `party_.Open()` right
    // at Init, under the OLD contract "panel always visible, opened once" (the
    // invented raid frame). Mapping UI_MemberSelectWnd_Open onto `Open()` would
    // therefore make the MODAL selector appear on entering the game — a visible
    // regression this wave has no right to introduce (GameWindows.cpp is not
    // owned). The faithful opener thus has an explicit name; `Open()` stays the
    // default Dialog::Open() (inert: Render() recomputes bOpen_ every frame).
    // TO FOLLOW UP: remove GameWindows.cpp:78, now moot (raid-frame visibility no
    // longer depends on an Open()).
    void OpenMemberSelect();

    // UI_MemberSelectWnd_Close 0x667350: bOpen=0 (nothing else).
    void CloseMemberSelect();

    // Close() is wired to CloseMemberSelect(): this is FAITHFUL — the binary
    // closes all dialogs (UI_CloseAllDialogs 0x5AC590, anchor 0x6677C4) before
    // opening the selector, so UIManager::CloseAll/ResetAll must indeed close it too.
    void Close() override;

    // UI_MemberSelectWnd_ProcNet 0x667730 (UI file code 33): toggle. Only opens if
    // g_SceneMgr==6 && g_SceneSubState==4 (in-game) — cf. .cpp for the ported guard.
    // NO CALLER to date (cf. "missing wiring" banner above).
    void Toggle();

    bool MemberSelectOpen() const; // Var(0x184BE40) != 0

    // --- Routing (the modal selector takes priority over the raid frame) ---
    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override { (void)vk; return false; } // no proven internal shortcut

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    // ------------------------------------------------------------------
    // Member selector (UI_MemberSelectWnd)
    // ------------------------------------------------------------------
    struct MsLayout {
        int x = 0, y = 0, w = 0, h = 0;
    };
    MsLayout ComputeMsLayout(int screenW, int screenH) const;
    // Rectangles derived from the PROVEN offsets (cf. banner).
    void MsSlotRect(const MsLayout& m, int i, int& rx, int& ry, int& rw, int& rh) const;
    void MsCloseRect(const MsLayout& m, int& rx, int& ry, int& rw, int& rh) const;
    void MsConfirmRect(const MsLayout& m, int& rx, int& ry, int& rw, int& rh) const;

    void RenderMemberSelect(const UiContext& ctx, int cursorX, int cursorY);

    // Button latches (fields +12/+16 of g_MemberSelectWnd: 0x184BE44/0x184BE48).
    // Kept as plain members rather than Var(): purely internal to the window, no
    // other file reads them (unlike +8/+20/+24, exposed via Var()).
    bool msCloseLatch_   = false; // *(this+3) — 0x6674EE / 0x6675EF
    bool msConfirmLatch_ = false; // *(this+4) — 0x66753D / 0x66764E

    // Screen dims memorized at the last Render: OnMouseDown/OnClick are routed
    // BETWEEN two frames and only receive (x,y), yet the binary re-centers the
    // window on every event (0x667394 / 0x6675A2). Same idiom as
    // MsgBoxDialog::lastScreenW_.
    mutable int lastScreenW_ = ts2::kRefWidth;  // nWidth  0x1669184
    mutable int lastScreenH_ = ts2::kRefHeight; // nHeight 0x1669188

    // ------------------------------------------------------------------
    // "Raid frame" HUD panel (kept invention, cf. banner)
    // ------------------------------------------------------------------
    struct RowLayout {
        std::string name;
        int nameY = 0;
        int hpX = 0, hpY = 0, hpW = 0, hpH = 0;
        int hp = 0, hpMax = 0;
        int mpX = 0, mpY = 0, mpW = 0, mpH = 0;
        int mp = 0, mpMax = 0;
        bool hasMp = false; // false => greyed-out MP bar (no data, cf. .h banner)
    };

    struct Layout {
        bool visible = false;
        int  x = 0, y = 0, w = 0, h = 0;
        std::vector<RowLayout> rows;
    };

    Layout BuildLayout(int screenW, int screenH) const;

    mutable int  lastX_ = 0, lastY_ = 0, lastW_ = 0, lastH_ = 0;
    mutable bool lastVisible_ = false;

    static constexpr int kMaxRows  = 8;
    static constexpr int kPanelW   = 210;
    static constexpr int kMarginX  = 12;  // top-left anchor (NOT proven, cf. banner)
    static constexpr int kMarginY  = 12;
    static constexpr int kPadX     = 10;
    static constexpr int kPadY     = 8;
    static constexpr int kTitleH   = 18;
    static constexpr int kNameH    = 13;
    static constexpr int kBarH     = 7;
    static constexpr int kBarGapY  = 2;
    static constexpr int kRowGapY  = 6;

    // ------------------------------------------------------------------
    // Original addresses (mirrored via game::ClientRuntime::Var)
    // ------------------------------------------------------------------
    // Fields of the g_MemberSelectWnd 0x184BE38 object (proven layout, cf. banner):
    static constexpr uint32_t kVarMemberSelectOpen = 0x184BE40; // +8  bOpen
    static constexpr uint32_t kVarSelectedSlot     = 0x184BE4C; // +20 selected slot
    static constexpr uint32_t kVarSlotValuesBase   = 0x184BE50; // +24 values[10]
    // Misc:
    static constexpr uint32_t kVarWhisperPreset    = 0x184C64C; // g_WhisperPresetSlot (0x66747C)
    // (g_SelfMorphNpcId 0x1675A98 and g_SysMsgColor 0x84DFD8 are addressed directly
    //  by the anonymous-namespace helpers in the .cpp — no duplicate constant here.)
    // Raid frame (invention):
    static constexpr uint32_t kVarMemberHpBase     = 0x1687458; // dword_1687458[227*i]
    static constexpr uint32_t kVarMemberHpMaxBase  = 0x168745C; // dword_168745C[227*i]
    static constexpr uint32_t kMemberStride        = 908;       // 227 dwords * 4 bytes

    static constexpr int kRosterSlots = 10;  // 0x6672D8 / 0x667300 : loops j<10, k<10
    static constexpr int kSlotUnset   = -1;  // 0x6672D1 : *(this+5) = -1
    static constexpr int kValueUnset  = -2;  // 0x6672F6 : *(this+j+6) = -2

    // --- Selector geometry: PROVEN offsets (cf. .h banner) ---
    static constexpr int kMsSlotDX   = 17;   // 0x667438 : *this + 17
    static constexpr int kMsSlotDY0  = 81;   // 0x667438 : *(this+1) + 20*i + 81
    static constexpr int kMsSlotStep = 20;
    static constexpr int kMsCloseDX  = 252;  // 0x6674D2 : *this + 252
    static constexpr int kMsCloseDY  = 24;
    static constexpr int kMsOkDX     = 214;  // 0x667521 : *this + 214
    static constexpr int kMsOkDY     = 298;
    // --- DERIVED dimensions (.IMG sprites resolved at runtime, cf. .h banner) ---
    static constexpr int kMsW = 280, kMsH = 330;
    static constexpr int kMsSlotW = 246, kMsSlotH = 18;
    static constexpr int kMsCloseW = 20, kMsCloseH = 20;
    static constexpr int kMsOkW = 52, kMsOkH = 22;

    // --- StrTable005 identifiers (proven) ---
    static constexpr int kStrArenaRefused = 1352; // 0x667287 : arena refusal
    static constexpr int kStrNoSelection  = 529;  // 0x6676A4 : "no member selected"
    static constexpr int kStrConfirmBody  = 530;  // 0x6676CA : confirmation body (MsgBox 21)

    static constexpr D3DCOLOR kColBg      = Argb(224, 32, 32, 40);
    static constexpr D3DCOLOR kColBorder  = Argb(255, 128, 128, 128);
    static constexpr D3DCOLOR kColTitle   = Argb(255, 255, 221, 102);
    static constexpr D3DCOLOR kColText    = Argb(255, 255, 255, 255);
    static constexpr D3DCOLOR kColHpBg    = Argb(200, 60, 20, 20);
    static constexpr D3DCOLOR kColHpFill  = Argb(255, 224, 64, 64);
    static constexpr D3DCOLOR kColMpBg    = Argb(200, 20, 24, 60);
    static constexpr D3DCOLOR kColMpFill  = Argb(255, 64, 96, 224);
    static constexpr D3DCOLOR kColNoData  = Argb(160, 70, 70, 70);
    static constexpr D3DCOLOR kColSelBg   = Argb(255, 64, 96, 160); // selected slot
    static constexpr D3DCOLOR kColSlotBg  = Argb(255, 48, 48, 56);
    static constexpr D3DCOLOR kColBtnBg   = Argb(255, 64, 64, 72);
};

} // namespace ts2::ui

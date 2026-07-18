// UI/StoragePwWindow.h — UI_StoragePwWnd window (dword_1839950, ctor 0x666900).
//
// ###########################################################################
// # NAME WARNING: this is NOT a "storage password".                        #
// #                                                                         #
// # The IDB symbol `UI_StoragePwWnd` is MISLEADING. The window has NO      #
// # input field: no EditBox, no caret, no masking. It is a box with       #
// # THREE SPRITE BUTTONS comparing the alliance mini-roster                #
// # g_AllianceRosterNames 0x16749B8 to the local player name byte_1673184: #
// #   button 1 (+87,+56)  = LEAVE the alliance   (non-leaders only)        #
// #   button 2 (+87,+110) = DISBAND the alliance (leader only)             #
// #   button 3 (+87,+164) = CLOSE                                          #
// # The file/class name follows the IDA symbol (rule "IDA = single         #
// # source of truth"); the SEMANTICS are as described here.                #
// ###########################################################################
//
// IDENTITY EVIDENCE (the gap tracker left the identity as "to be clarified"):
//   - `String` 0x7EC95F = EMPTY STRING (read_cstring: length 0). The
//     `Crt_Strcmp(g_AllianceRosterNames, &String)` calls are therefore tests
//     of "is roster slot 0 empty?", not password comparisons.
//   - byte_1673184 = LOCAL player name. Independent proof:
//     AutoPlay_ValidateChatName 0x45E670 does
//     `for (i<5) if (strcmp(&g_AllianceRosterNames[13*i], "")) ++v3;
//      if (v3 != 1 || strcmp(roster[0], byte_1673184)) return 0; Net_SendOp71(...)`
//     -> roster[0] is compared to the local player to decide "am I alone?".
//     Corroborated by Docs/TS2_ALLIANCE_PARTY_ROSTER.md:105.
//   - g_AllianceRosterNames[0] = LEADER (5 slots x 13 bytes) — modeled on the C++ side by
//     game::g_World.allianceRoster.memberNames[0] (Game/GameState.h:531-545).
//
// Original functions transcribed (all re-proven via decompilation on 2026-07-16):
//   UI_StoragePwWnd_Open        0x666960 -> Open()
//   UI_StoragePwWnd_Close       0x666A10 -> Close()
//   UI_StoragePwWnd_OnMouseDown 0x666A30 -> OnMouseDown()
//   UI_StoragePwWnd_OnClick     0x666BB0 -> OnClick()
//   UI_StoragePwWnd_ProcNet     0x666EB0 -> ProcKeyCommand()  [keyboard toggle]
//   UI_StoragePwWnd_Render      0x666FE0 -> Render()
//
// ===========================================================================
// DECISIVE FACT — THIS WINDOW EMITS NOTHING, EVER (proven, see .cpp)
// ===========================================================================
// Its two actions only open a confirmation box
// `UI_MsgBox_Open(dword_1822438, 11|12, Str(364|365), "")`. But:
//   - UI_MsgBox_Open 0x5C08C0 writes the type at `this+24` = 0x1822450;
//     data_refs(0x1822450) = EXACTLY 10 readers, covering types
//     8, 9, 10, 14, 19, 20, 37, 38 — NEITHER 11 NOR 12;
//   - the acceptance jump table 0x5C2DC3 routes 11 and 12 to its default
//     def_5C2DC3 0x5C2E8C = `mov eax, 1 ; retn 8` (pure epilogue).
// => Clicking "Yes" on the confirmation triggers STRICTLY NOTHING in the
// original binary: no opcode, no effect. The window is a cosmetic dead end.
// Fidelity requires reproducing THIS — above all, do not invent an
// emission (Op59/Op65/Op72 or otherwise). This greatly reduces the
// real severity of gap USD-02 (reported as "high").
//
// Project rule: this file does not edit ANY existing header.
#pragma once
#include "UI/UIManager.h"
#include "Game/ClientRuntime.h"

#include <string>

namespace ts2::ui {

class StoragePwWindow : public Dialog {
public:
    StoragePwWindow();
    ~StoragePwWindow() override;

    // UI_StoragePwWnd_Open 0x666960 — TWO refusals BEFORE any opening:
    //   (a) Map_IsArenaZone() -> message StrTable005 #1352, NOTHING else (EA 0x66696E)
    //   (b) roster[0] empty   -> message StrTable005 #355,  NOTHING else (EA 0x6669A3)
    // Else: *(this+2)=1 then loop `for (i=0;i<3;++i) *(this+i+3)=0` (EA 0x6669DC).
    void Open() override;
    void Close() override;                    // UI_StoragePwWnd_Close 0x666A10

    bool OnMouseDown(int x, int y) override;  // UI_StoragePwWnd_OnMouseDown 0x666A30
    bool OnClick(int x, int y) override;      // UI_StoragePwWnd_OnClick     0x666BB0

    void Render(const UiContext& ctx, int cursorX, int cursorY) override; // 0x666FE0

    // -----------------------------------------------------------------------
    // WIRING API — the keyboard trigger lives in App/PlayerInputController.cpp
    // (wave W9, NOT owned by this front). Chain proven in the binary:
    //   Camera_UpdateFromInput 0x50B7D0, case P (scancode DIK 25) @0x50DB80
    //     -> if mouse-look OFF: def_50C7A8 0x50DD5B -> call UI_RouteKeyInput @0x50DEC1
    //     -> UI_RouteKeyInput 0x5ADF50 @0x5AE124 -> UI_StoragePwWnd_ProcNet 0x666EB0.
    // On the C++ side this path ALREADY EXISTS: App/PlayerInputController.cpp:151-152
    //   `case input::dik::kP:  if (blocked || st_.mouseLook == 0) { RouteSceneKey(dik); break; }`
    // and `input::dik::kP` equals 25 — exactly the id tested by ProcNet.
    // EXACT line to add, in PlayerInputController::RouteSceneKey(int dik):
    //     if (ui::StoragePwWindow::ProcKeyCommand(dik)) return;   // 0x5AE124 -> 0x666EB0
    //
    // ProcKeyCommand fully transcribes UI_StoragePwWnd_ProcNet 0x666EB0 (including
    // the InGame scene guard and the open/close asymmetry) and
    // returns true if the command was consumed (binary's return 1).
    // -----------------------------------------------------------------------
    static StoragePwWindow* Active();
    static bool             ProcKeyCommand(int cmdId);

    // Keyboard command id consumed (`g_UiCmdQueueRecords[5*i] == 25`,
    // EA 0x666F39 / 0x666FB5) = scancode DIK_P.
    static constexpr int kCmdToggle = 25;

private:
    struct Rect { int x, y, w, h; };

    // Screen recentering redone on EVERY event, like the binary:
    // Render (EA 0x667000/0x66703E), OnMouseDown (0x666A52/0x666A90),
    // OnClick (0x666BD2/0x666C10):
    //   x = nWidth/2  - Sprite2D_GetWidth(unk_8F70D4)/2
    //   y = nHeight/2 - Sprite2D_GetHeight(unk_8F70D4)/2
    void RecomputeCenter(int screenW, int screenH);

    Rect PanelRect() const;
    Rect LeaveButtonRect() const;    // btn1: (+87, +56)
    Rect DisbandButtonRect() const;  // btn2: (+87, +110)   <- DRAW / MOUSEDOWN
    Rect DisbandClickRect() const;   // btn2: (+87, +100)   <- CLICK (binary quirk!)
    Rect CloseButtonRect() const;    // btn3: (+87, +164)

    void HandleLeaveClick();         // UI_StoragePwWnd_OnClick, branch *(this+3)
    void HandleDisbandClick();       // UI_StoragePwWnd_OnClick, branch *(this+4)

    // --- Button latches (+3, +4, +5 from the original dialog) ---
    bool leaveLatch_    = false;  // +3 (EA 0x666ACF / 0x666C23)
    bool disbandLatch_  = false;  // +4 (EA 0x666B1C / 0x666D21)
    bool closeLatch_    = false;  // +5 (EA 0x666B69 / 0x666E18)

    // ----------------------------------------------------------------------
    // GEOMETRY — exact binary literals, except panel extent and
    // button size (derived from sprites unk_8F70D4 / unk_8F7168 /
    // unk_8F7290 / unk_8F73B8, not statically knowable): fallback values.
    // ----------------------------------------------------------------------
    static constexpr int kBtnX       = 87;   // +87 for all THREE buttons
    static constexpr int kBtnLeaveY  = 56;   // EA 0x666AB3 / 0x666C4A / 0x6670B0
    static constexpr int kBtnCloseY  = 164;  // EA 0x666B4D / 0x666E42 / 0x66718F

    // ######################################################################
    // # PROVEN BINARY QUIRK — REPRODUCE AS-IS (fidelity rule)              #
    // # Button 2 does NOT use the same band depending on the event:        #
    // #   Render      0x66711E -> +110                                      #
    // #   OnMouseDown 0x666B00 -> +110                                      #
    // #   OnClick     0x666D48 -> +100   <-- 10 px HIGHER                  #
    // # This is not a decompilation typo: the three EAs are                 #
    // # distinct and legible. Real consequence in the original game:        #
    // # the CLICKABLE band of the "disband" button is offset 10 px          #
    // # upward relative to the DRAWN band. Reproduced.                     #
    // ######################################################################
    static constexpr int kBtnDisbandDrawY  = 110; // draw + mousedown
    static constexpr int kBtnDisbandClickY = 100; // click (quirk 0x666D48)

    static constexpr int kBtnW   = 96;
    static constexpr int kBtnH   = 28;
    static constexpr int kPanelW = 270;  // fits the buttons (87+96 = 183)
    static constexpr int kPanelH = 210;  // fits the bottom button (164+28 = 192)

    // --- StrTable005 identifiers (all recorded via decompilation) ---
    static constexpr int kStrArenaRefused = 1352; // Open, EA 0x666987
    static constexpr int kStrNoAlliance   = 355;  // Open EA 0x6669C0 ; OnClick EA 0x666C84
    static constexpr int kStrLeaderCant   = 362;  // btn1, EA 0x666CC4
    static constexpr int kStrLeaveConfirm = 364;  // btn1 -> MsgBox type 11, EA 0x666CED
    static constexpr int kStrNotLeader    = 363;  // btn2, EA 0x666DC2
    static constexpr int kStrDisbandConf  = 365;  // btn2 -> MsgBox type 12, EA 0x666DEB

    // MsgBox types (UI_MsgBox_Open(dword_1822438, type, body, title)).
    // NO consumer accepts 11/12 — see the "DECISIVE FACT" header banner.
    static constexpr int kMsgBoxLeave   = 11; // EA 0x666CFA
    static constexpr int kMsgBoxDisband = 12; // EA 0x666DF8

    // --- Palette (fallback if the sprites are not resolved) ---
    static constexpr D3DCOLOR kColPanelBg  = 0xE0202028u;
    static constexpr D3DCOLOR kColFrame    = 0xFF808080u;
    static constexpr D3DCOLOR kColTitle    = 0xFFFFDD66u;
    static constexpr D3DCOLOR kColText     = 0xFFFFFFFFu;
    static constexpr D3DCOLOR kColBtnBg    = 0xFF3A3A46u;
    static constexpr D3DCOLOR kColBtnHover = 0xFF4060A0u;
    static constexpr D3DCOLOR kColBtnDown  = 0xFF5878C0u;
};

} // namespace ts2::ui

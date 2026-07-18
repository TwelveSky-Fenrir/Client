// UI/Win32EditBox.h — RAII wrapper around a native child Win32 EDIT control
// (ts2::ui::Win32EditBox). See Docs/TS2_WIN32_EDITBOX.md.
//
// FIDELITY (EA-proven in RE/TwelveSky2.exe.i64) — UI_CreateEditBoxes 0x50E460:
// the binary creates 21 generic system EDIT controls (index 0 = login ID,
// 1 = password) via a single looped CreateWindowExA (EA 0x50ED93):
//   CreateWindowExA(
//       0x100          /* dwExStyle = WS_EX_CLIENTEDGE                        */,
//       "edit"         /* lpClassName (system class, case-insensitive)        */,
//       nullptr        /* lpWindowName = no initial text                      */,
//       0x50800080     /* dwStyle = WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL */,
//       x, y, w, h,
//       hWndParent     /* 0x815184 = main window (App::hwnd_)                 */,
//       (HMENU)(i+1)   /* control id                                          */,
//       hInstance      /* 0x815578 = App::hInst_                              */,
//       nullptr);
// Then, for each field: ShowWindow(h, SW_HIDE) (EA 0x50EDC4) — all created
// hidden — and, for boxes 0 to 19, SendMessageA(h, EM_LIMITTEXT=0xC5,
// kEditLimit[i], 0) — the limit is SPECIFIC TO EACH BOX (switch @0x50EDDD; it
// is not 127 everywhere: 0x7F for 0/1 @0x50EDF8/0x50EE45, but 0x0C, 0x3C, 0x32,
// 0x04… elsewhere) — followed by subclassing the WndProc to UI_EditBoxWndProc
// 0x50E070 (Tab/Enter navigation + key filtering). Box 20 has NEITHER limit
// NOR subclassing (`default: LABEL_2`).
//
// STATE (wave W9, gap UIFW-07) — THIS WRAPPER IS NOT WIRED IN.
// Exhaustive grep of `Win32EditBox` across all of src/: no instance, no call to
// Create(). The class is entirely dead code, and the project already went
// THE OTHER WAY (custom widget `ChatWindow`/`focused_`, cf. Scene/SceneManager.cpp:608-618).
// This file is kept as-is (removing it would require touching the .vcxproj, out
// of scope) but its proven DIVERGENCES have been fixed below, so as not to
// trap whoever wires it in someday. Functional cost of the missing native EDIT:
// no IME (CJK input impossible even though g_LangId 0x84DFF8 drives localized
// tables) and no clipboard.
//
// ORIGINAL MODEL, to understand BEFORE any wiring (IDA comments from 0x50E460 /
// 0x50F4A0): the 21 EDIT controls are created then PERMANENTLY HIDDEN (a single ShowWindow
// in the whole binary, @0x50EDC4, with nCmdShow=0=SW_HIDE — the style's WS_VISIBLE bit
// is neutralized right after). They NEVER paint anything: they only serve as a
// TEXT BUFFER + keyboard/IME FOCUS TARGET. The engine reads GetWindowTextA every frame
// and redraws itself in bitmap font (Scene_LoginRender 0x51B020:
// GetWindowTextA(g_hEditLoginPw,…) @0x51B366, then Crt_Memset(String, 42, strlen) @0x51B3CE
// — 42 = '*' — then UI_DrawNumberValue(String, …) @0x51B40C). THIS IS WHY
// Present() never conflicts with the EDIT controls.
// => Win32EditBox::Show() (below) has NO binary counterpart and would break this
//    model: do not call it in a faithful wiring.
//
// REMAINING, ACCEPTED DEVIATION:
//   - Subclassing to UI_EditBoxWndProc 0x50E070 (SetWindowLongA(h, -4, …) @0x50EE19 &
//     co., applied to boxes 0..19) NOT reproduced here: it routes Tab/Enter to
//     SCENE targets (case 0: Tab -> UI_FocusEditBox(mgr,2); case 1: Tab -> box 1,
//     Enter -> g_SceneSubState=2; case 4: Tab -> box 4, Enter -> UI_Chat_SubmitInput;
//     case 15: Enter -> Chat_SubmitTypedMessage) and requires a global focus state
//     (g_UIEditBoxMgr 0x1668FC0) that this wrapper does not have. Generic filters of the
//     default case (LABEL_53): WM_CONTEXTMENU(0x7B) -> 1 @0x50E367; WM_KEYDOWN(0x100) with
//     wParam in [0x23..0x28] (END/HOME/LEFT/UP/RIGHT/DOWN) -> 1 @0x50E39E; WM_PASTE(0x302) blocked if
//     `g_GmAuthLevel < 1 && i != 14` @0x50E376; else CallWindowProcA(lpPrevWndFunc 0x1669018).
//     TODO [anchor UI_EditBoxWndProc 0x50E070]: port along with the focus manager
//     (UI_FocusEditBox 0x50F4A0) the day the native EDIT controls get wired in.
//
// RAII: the handle is destroyed on Destroy() (idempotent) and on the destructor.
// Non-copyable class (an HWND has only one owner).
#pragma once
#include <windows.h>
#include <string>

// kEditLimit[21] (per-box EM_LIMITTEXT limits, switch @0x50EDDD) is already defined —
// and thoroughly proven case by case — in UI/Widgets.h, in this SAME namespace
// ts2::ui. It is REUSED rather than duplicated: two homonymous `inline constexpr`
// in the same namespace would be a redefinition (and, at equal values, a
// drift trap). Table cross-checked independently against 0x50EDDD: identical.
#include "UI/Widgets.h"

namespace ts2::ui {

// Text buffer size: 127 usable characters + NUL terminator, faithful to
// the EM_LIMITTEXT=0x7F limit set by UI_CreateEditBoxes on boxes 0/1 —
// the highest of kEditLimit (UI/Widgets.h), so this buffer covers all 21 boxes.
inline constexpr int kMaxText = 128;

// Number of native EDIT controls created by UI_CreateEditBoxes 0x50E460 (`if (v16 >= 21) return 1`
// @0x50ED0D).
inline constexpr int kEditBoxCount = 21;

// CONSTANT style of the 21 boxes (CreateWindowExA @0x50ED93). Warning: NO ES_PASSWORD and no
// EM_SETPASSWORDCHAR anywhere in the binary: password masking is
// PURELY SOFTWARE at render time (Crt_Memset(String, 42, strlen) @0x51B3CE). Adding
// ES_PASSWORD here would be a divergence — it was one (gap UIFW-07), now fixed.
inline constexpr DWORD kEditStyle   = 0x50800080; // WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL
inline constexpr DWORD kEditExStyle = 0x00000100; // WS_EX_CLIENTEDGE

// Reminder (table in UI/Widgets.h:78): kEditLimit[i] = EM_LIMITTEXT limit of box
// i, 0 = NO limit set. Box 20 falls into `default: LABEL_2` of the switch
// @0x50EDDD (no EM_LIMITTEXT, no subclassing): hence the sentinel 0 at [20] — this
// is not an oversight, and Create() must test it before sending the message.
// Useful landmarks: box 0/1 = login ID / password (0x7F); box 3 = whisper
// target (0x0C); box 4 = chat input line (0x3C); box 14 = the only
// box exempted from the WM_PASTE block (`i != 14` @0x50E376).

// Win32EditBox — RAII wrapper around a native child EDIT control. A single owned
// HWND; deferred creation (Create), idempotent destruction (Destroy/dtor).
class Win32EditBox {
public:
    Win32EditBox() = default;
    ~Win32EditBox() { Destroy(); }

    // Non-copyable: the HWND is owned (a copy would cause a double
    // DestroyWindow). Move operations are not implicitly generated
    // (destructor + copy declared) -> the class is also non-movable.
    Win32EditBox(const Win32EditBox&)            = delete;
    Win32EditBox& operator=(const Win32EditBox&) = delete;

    // Creates the child EDIT control of `parent`, faithful to CreateWindowExA @0x50ED93:
    // exStyle=kEditExStyle, style=kEditStyle (constant, without ES_PASSWORD),
    // hMenu=(HMENU)(boxIndex+1) — the binary's control id, @0x50ED93 — and
    // hInstance retrieved via the parent's GWLP_HINSTANCE (= hInstance 0x815578).
    // After creation: ShowWindow(SW_HIDE) @0x50EDC4, then EM_LIMITTEXT(kEditLimit[boxIndex])
    // if non-zero (@0x50EDF8 & co.). Destroys any pre-existing handle. true if HWND valid.
    //
    // `boxIndex` is in [0, kEditBoxCount) = the binary's box index (0 = login ID,
    // 1 = password, 3 = whisper target, 4 = chat input). It carries BOTH
    // the control id and the input limit — hence replacing the old
    // `bool password` parameter (which does not exist in the binary, cf. gap UIFW-07).
    //
    // Warning: `w`/`h` are expected to be ALREADY computed by the caller. The binary derives them
    // from the box's rect WITH a +1 on each dimension (@0x50ED93):
    //   w = rect.right - rect.left + 1 ; h = rect.bottom - rect.top + 1
    // (rect[i] = g_UIEditBoxMgr+4*i+23 dwords, base 0x166901C). To reproduce on the caller side.
    bool Create(HWND parent, int x, int y, int w, int h, int boxIndex);

    // Replaces the control's text (SetWindowTextA). No-op if not created.
    void SetText(const char* text);
    void SetText(const std::string& text);

    // Reads the current text (GetWindowTextA, kMaxText buffer). "" if not created.
    std::string GetText() const;

    void Focus(); // SetFocus on the control (no-op if not created)
    void Show();  // ShowWindow(SW_SHOW)
    void Hide();  // ShowWindow(SW_HIDE) — faithful rest state (0x50EDC4)

    // Destroys the HWND (DestroyWindow) and resets the handle to nullptr. Idempotent:
    // safe on double call (e.g. explicit Destroy() then destructor).
    void Destroy();

    HWND Handle() const { return hwnd_; }

    // Repositions/resizes the control without changing Z-order or focus.
    // Extension beyond the original API: needed so a scene can place the
    // native control according to the actual UI atlas geometry.
    void SetBounds(int x, int y, int w, int h);

private:
    HWND hwnd_ = nullptr;
};

} // namespace ts2::ui

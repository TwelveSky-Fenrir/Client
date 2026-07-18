// UI/Win32EditBox.cpp — implementation of the native Win32 EDIT wrapper.
// Style/limit values proven by EA in UI_CreateEditBoxes 0x50E460
// (cf. header of UI/Win32EditBox.h and Docs/TS2_WIN32_EDITBOX.md).
#include "UI/Win32EditBox.h"

#include "Core/Log.h"

namespace ts2::ui {

bool Win32EditBox::Create(HWND parent, int x, int y, int w, int h, int boxIndex) {
    // Clean re-creation: destroys any pre-existing handle (Create is idempotent).
    Destroy();

    if (!parent) {
        TS2_WARN("Win32EditBox::Create : parent HWND nul, creation ignoree");
        return false;
    }
    if (boxIndex < 0 || boxIndex >= kEditBoxCount) {
        // The binary creates ONLY the 21 boxes of its loop (`if (v16 >= 21)` @0x50ED0D):
        // outside that range there is neither a control id nor an input limit defined.
        TS2_WARN("Win32EditBox::Create : boxIndex %d hors [0,%d), creation ignoree",
                 boxIndex, kEditBoxCount);
        return false;
    }

    // hInstance = that of the module that created the parent window, retrieved
    // transitively via GWLP_HINSTANCE (equivalent to hInstance 0x815578 =
    // App::hInst_ in the binary) without extending the wrapper's signature.
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(parent, GWLP_HINSTANCE));

    // CreateWindowExA @0x50ED93, identically: exStyle=0x100 (WS_EX_CLIENTEDGE),
    // class "edit" (system class, case-insensitive), no initial text, CONSTANT style
    // 0x50800080 for all 21 boxes (no ES_PASSWORD: gap UIFW-07 fixed —
    // masking is software-only, cf. Crt_Memset(String, 42, strlen) @0x51B3CE), and
    // hMenu = (HMENU)(v16 + 1) = the control id (@0x50ED93; the old nullptr was
    // the 2nd proven divergence of the gap).
    hwnd_ = CreateWindowExA(
        kEditExStyle,
        "edit",
        nullptr,
        kEditStyle,
        x, y, w, h,
        parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(boxIndex + 1)),
        hInst,
        nullptr);

    if (!hwnd_) {
        TS2_WARN("Win32EditBox::Create : CreateWindowExA a echoue (err=%lu)", GetLastError());
        return false;
    }

    // Faithful — and PERMANENT — rest state: ShowWindow(h, 0) = SW_HIDE @0x50EDC4. This is
    // the only ShowWindow ever posted on an EDIT in the whole binary: they are NEVER
    // shown (text buffer + focus target only, cf. header of Win32EditBox.h).
    ShowWindow(hwnd_, SW_HIDE);

    // Faithful input limit, PER BOX: EM_LIMITTEXT (0xC5) to kEditLimit[boxIndex]
    // (switch @0x50EDDD). Box 20 falls into `default: LABEL_2` = no EM_LIMITTEXT
    // sent, hence the `!= 0` guard (the old code set 127 for ALL boxes).
    if (kEditLimit[boxIndex] != 0) {
        SendMessageA(hwnd_, EM_LIMITTEXT,
                     static_cast<WPARAM>(kEditLimit[boxIndex]), 0);
    }

    // TODO [anchor 0x50EE19 / UI_EditBoxWndProc 0x50E070]: the binary then subclasses
    // boxes 0..19 — `SetWindowLongA(h, -4 /*GWL_WNDPROC*/, UI_EditBoxWndProc)`, the
    // previous proc being stored in lpPrevWndFunc 0x1669018 — and ABORTS the
    // creation (`return 0`) if SetWindowLongA returns 0. Not reproduced: the proc routes
    // Tab/Enter to scene targets and relies on the global focus state g_UIEditBoxMgr
    // 0x1668FC0, absent from this wrapper (filter details in the header of Win32EditBox.h).

    return true;
}

void Win32EditBox::SetText(const char* text) {
    if (hwnd_) {
        SetWindowTextA(hwnd_, text ? text : "");
    }
}

void Win32EditBox::SetText(const std::string& text) {
    if (hwnd_) {
        SetWindowTextA(hwnd_, text.c_str());
    }
}

std::string Win32EditBox::GetText() const {
    if (!hwnd_) {
        return std::string();
    }
    char buf[kMaxText] = {0};
    // GetWindowTextA truncates and NUL-terminates within the given limit; faithful
    // to the GetWindowTextA(g_hEditLoginId, ..., 128) reads of Scene_LoginUpdate.
    GetWindowTextA(hwnd_, buf, kMaxText);
    return std::string(buf);
}

void Win32EditBox::Focus() {
    if (hwnd_) {
        SetFocus(hwnd_);
    }
}

void Win32EditBox::Show() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOW);
    }
}

void Win32EditBox::Hide() {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_HIDE);
    }
}

void Win32EditBox::Destroy() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

void Win32EditBox::SetBounds(int x, int y, int w, int h) {
    if (hwnd_) {
        SetWindowPos(hwnd_, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

} // namespace ts2::ui

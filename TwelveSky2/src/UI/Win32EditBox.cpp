// UI/Win32EditBox.cpp — implémentation du wrapper EDIT Win32 natif.
// Valeurs de style/limite prouvées par EA dans UI_CreateEditBoxes 0x50E460
// (cf. en-tête de UI/Win32EditBox.h et Docs/TS2_WIN32_EDITBOX.md).
#include "UI/Win32EditBox.h"

#include "Core/Log.h"

namespace ts2::ui {

bool Win32EditBox::Create(HWND parent, int x, int y, int w, int h, bool password) {
    // Recréation propre : détruit tout handle préexistant (Create idempotent).
    Destroy();

    if (!parent) {
        TS2_WARN("Win32EditBox::Create : parent HWND nul, creation ignoree");
        return false;
    }

    // Style fidèle EA (CreateWindowExA @0x50ED93) :
    //   0x50800080 = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL.
    DWORD style = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
    // Déviation fidèle-UX assumée : le binaire masque le mot de passe en
    // logiciel au rendu (Scene_LoginRender 0x51B020), pas via ES_PASSWORD. Un
    // contrôle natif visible impose ES_PASSWORD pour un masquage visuel réel.
    if (password) {
        style |= ES_PASSWORD;
    }

    // hInstance = celui du module ayant créé la fenêtre parente, récupéré
    // transitivement via GWLP_HINSTANCE (équivalent à hInstance 0x815578 =
    // App::hInst_ dans le binaire) sans étendre la signature du wrapper.
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(parent, GWLP_HINSTANCE));

    // dwExStyle = WS_EX_CLIENTEDGE (0x100), classe "EDIT" (insensible à la
    // casse), pas de texte initial, id de contrôle laissé à 0 (le wrapper
    // n'utilise pas WM_COMMAND ; l'appelant identifie le champ par son HWND).
    hwnd_ = CreateWindowExA(
        WS_EX_CLIENTEDGE,
        "EDIT",
        nullptr,
        style,
        x, y, w, h,
        parent,
        nullptr,
        hInst,
        nullptr);

    if (!hwnd_) {
        TS2_WARN("Win32EditBox::Create : CreateWindowExA a echoue (err=%lu)", GetLastError());
        return false;
    }

    // État de repos fidèle : contrôle créé masqué (ShowWindow(h, SW_HIDE) @0x50EDC4).
    ShowWindow(hwnd_, SW_HIDE);
    // Limite de saisie fidèle : EM_LIMITTEXT=0xC5 à 0x7F=127 (boxes 0/1 @0x50EDF8).
    SendMessageA(hwnd_, EM_LIMITTEXT, static_cast<WPARAM>(kMaxText - 1), 0);

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
    // GetWindowTextA tronque et termine par NUL dans la limite fournie ; fidèle
    // aux lectures GetWindowTextA(g_hEditLoginId, ..., 128) de Scene_LoginUpdate.
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

// UI/Win32EditBox.cpp — implémentation du wrapper EDIT Win32 natif.
// Valeurs de style/limite prouvées par EA dans UI_CreateEditBoxes 0x50E460
// (cf. en-tête de UI/Win32EditBox.h et Docs/TS2_WIN32_EDITBOX.md).
#include "UI/Win32EditBox.h"

#include "Core/Log.h"

namespace ts2::ui {

bool Win32EditBox::Create(HWND parent, int x, int y, int w, int h, int boxIndex) {
    // Recréation propre : détruit tout handle préexistant (Create idempotent).
    Destroy();

    if (!parent) {
        TS2_WARN("Win32EditBox::Create : parent HWND nul, creation ignoree");
        return false;
    }
    if (boxIndex < 0 || boxIndex >= kEditBoxCount) {
        // Le binaire ne crée QUE les 21 boîtes de sa boucle (`if (v16 >= 21)` @0x50ED0D) :
        // hors de cette plage il n'y a ni id de contrôle ni limite de saisie définis.
        TS2_WARN("Win32EditBox::Create : boxIndex %d hors [0,%d), creation ignoree",
                 boxIndex, kEditBoxCount);
        return false;
    }

    // hInstance = celui du module ayant créé la fenêtre parente, récupéré
    // transitivement via GWLP_HINSTANCE (équivalent à hInstance 0x815578 =
    // App::hInst_ dans le binaire) sans étendre la signature du wrapper.
    HINSTANCE hInst = reinterpret_cast<HINSTANCE>(GetWindowLongPtr(parent, GWLP_HINSTANCE));

    // CreateWindowExA @0x50ED93, à l'identique : exStyle=0x100 (WS_EX_CLIENTEDGE),
    // classe "edit" (système, insensible à la casse), pas de texte initial, style
    // CONSTANT 0x50800080 pour les 21 boîtes (aucun ES_PASSWORD : gap UIFW-07 corrigé —
    // le masquage est logiciel, cf. Crt_Memset(String, 42, strlen) @0x51B3CE), et
    // hMenu = (HMENU)(v16 + 1) = l'id de contrôle (@0x50ED93 ; l'ancien nullptr était
    // la 2e divergence prouvée du gap).
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

    // État de repos fidèle — et PERMANENT : ShowWindow(h, 0) = SW_HIDE @0x50EDC4. C'est
    // l'unique ShowWindow posé sur un EDIT dans tout le binaire : ils ne sont JAMAIS
    // affichés (tampon texte + cible de focus seulement, cf. en-tête de Win32EditBox.h).
    ShowWindow(hwnd_, SW_HIDE);

    // Limite de saisie fidèle, PAR BOÎTE : EM_LIMITTEXT (0xC5) à kEditLimit[boxIndex]
    // (switch @0x50EDDD). La boîte 20 tombe dans `default: LABEL_2` = aucun EM_LIMITTEXT
    // envoyé, d'où la garde `!= 0` (l'ancien code posait 127 pour TOUTES les boîtes).
    if (kEditLimit[boxIndex] != 0) {
        SendMessageA(hwnd_, EM_LIMITTEXT,
                     static_cast<WPARAM>(kEditLimit[boxIndex]), 0);
    }

    // TODO [ancre 0x50EE19 / UI_EditBoxWndProc 0x50E070] : le binaire sous-classe ensuite
    // les boîtes 0..19 — `SetWindowLongA(h, -4 /*GWL_WNDPROC*/, UI_EditBoxWndProc)`, le
    // proc précédent étant mémorisé dans lpPrevWndFunc 0x1669018 — et ABANDONNE la
    // création (`return 0`) si SetWindowLongA renvoie 0. Non reproduit : le proc route
    // Tab/Entrée vers des cibles de scène et s'appuie sur le focus global g_UIEditBoxMgr
    // 0x1668FC0, absents de ce wrapper (détail des filtres en tête de Win32EditBox.h).

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

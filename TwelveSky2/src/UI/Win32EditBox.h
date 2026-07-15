// UI/Win32EditBox.h — wrapper RAII autour d'un contrôle EDIT Win32 natif enfant
// (ts2::ui::Win32EditBox). Voir Docs/TS2_WIN32_EDITBOX.md.
//
// FIDÉLITÉ (EA-prouvée dans RE/TwelveSky2.exe.i64) — UI_CreateEditBoxes 0x50E460 :
// le binaire crée 21 contrôles EDIT système génériques (index 0 = login ID,
// 1 = mot de passe) via un unique CreateWindowExA en boucle (EA 0x50ED93) :
//   CreateWindowExA(
//       0x100          /* dwExStyle = WS_EX_CLIENTEDGE                        */,
//       "edit"         /* lpClassName (classe système, insensible à la casse) */,
//       nullptr        /* lpWindowName = pas de texte initial                 */,
//       0x50800080     /* dwStyle = WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL */,
//       x, y, w, h,
//       hWndParent     /* 0x815184 = fenêtre principale (App::hwnd_)          */,
//       (HMENU)(i+1)   /* id de contrôle                                      */,
//       hInstance      /* 0x815578 = App::hInst_                              */,
//       nullptr);
// Puis, pour chaque champ : ShowWindow(h, SW_HIDE) (EA 0x50EDC4) — tous créés
// masqués — et, pour les boxes 0 et 1, SendMessageA(h, EM_LIMITTEXT=0xC5,
// 127=0x7F, 0) (EA 0x50EDF8/0x50EE45), suivi d'un sous-classement de la WndProc
// vers UI_EditBoxWndProc 0x50E070 (navigation Tab/Entrée + filtrage de touches).
//
// DÉVIATIONS ASSUMÉES de ce wrapper (documentées dans le doc de référence) :
//   1. ES_PASSWORD : le binaire NE masque PAS le mot de passe au niveau Win32
//      (aucun ES_PASSWORD ni EM_SETPASSWORDCHAR dans UI_CreateEditBoxes) ; le
//      masquage d'origine est purement logiciel au rendu (Scene_LoginRender
//      0x51B020 : Crt_Memset(String, '*', len) avant dessin). Comme ce wrapper
//      expose un contrôle natif VISIBLE, un masquage visuel exige ES_PASSWORD :
//      on l'ajoute donc quand password=true (déviation fidèle-UX mandatée).
//   2. Sous-classement UI_EditBoxWndProc (Tab bascule ID<->PW, Entrée=login,
//      blocage nav/paste/contextmenu) NON reproduit ici : c'est de la logique
//      de scène, laissée à l'appelant (LoginScene). Ne retire aucune
//      fonctionnalité du wrapper lui-même.
//
// RAII : le handle est détruit au Destroy() (idempotent) et au destructeur.
// Classe non copiable (un HWND n'a qu'un seul propriétaire).
#pragma once
#include <windows.h>
#include <string>

namespace ts2::ui {

// Taille du tampon de texte : 127 caractères utiles + terminateur NUL, fidèle à
// la limite EM_LIMITTEXT=0x7F posée par UI_CreateEditBoxes sur les boxes 0/1.
inline constexpr int kMaxText = 128;

// ---------------------------------------------------------------------------
// Win32EditBox — enveloppe RAII d'un contrôle EDIT natif enfant. Un seul HWND
// possédé ; création différée (Create), destruction idempotente (Destroy/dtor).
class Win32EditBox {
public:
    Win32EditBox() = default;
    ~Win32EditBox() { Destroy(); }

    // Non copiable : le HWND est possédé (une copie provoquerait un double
    // DestroyWindow). Les opérations de déplacement ne sont pas implicitement
    // générées (destructeur + copie déclarés) → la classe est aussi non déplaçable.
    Win32EditBox(const Win32EditBox&)            = delete;
    Win32EditBox& operator=(const Win32EditBox&) = delete;

    // Crée le contrôle EDIT enfant de `parent`. Style fidèle EA :
    // exStyle=WS_EX_CLIENTEDGE, style=WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL
    // (+ES_PASSWORD si password). hInstance récupéré via GWLP_HINSTANCE du parent.
    // Après création : ShowWindow(SW_HIDE) puis EM_LIMITTEXT(127). Détruit tout
    // handle préexistant avant de recréer. Renvoie true si le HWND est valide.
    bool Create(HWND parent, int x, int y, int w, int h, bool password);

    // Remplace le texte du contrôle (SetWindowTextA). No-op si non créé.
    void SetText(const char* text);
    void SetText(const std::string& text);

    // Lit le texte courant (GetWindowTextA, tampon kMaxText). "" si non créé.
    std::string GetText() const;

    void Focus(); // SetFocus sur le contrôle (no-op si non créé)
    void Show();  // ShowWindow(SW_SHOW)
    void Hide();  // ShowWindow(SW_HIDE) — état de repos fidèle (0x50EDC4)

    // Détruit le HWND (DestroyWindow) et remet le handle à nullptr. Idempotent :
    // sûr en double appel (ex. Destroy() explicite puis destructeur).
    void Destroy();

    HWND Handle() const { return hwnd_; }

    // Repositionne/redimensionne le contrôle sans changer l'ordre Z ni le focus.
    // Extension hors API d'origine : indispensable pour qu'une scène place le
    // contrôle natif d'après la géométrie réelle de l'atlas UI.
    void SetBounds(int x, int y, int w, int h);

private:
    HWND hwnd_ = nullptr;
};

} // namespace ts2::ui

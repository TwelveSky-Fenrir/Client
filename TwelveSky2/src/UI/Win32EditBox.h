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
// masqués — et, pour les boîtes 0 à 19, SendMessageA(h, EM_LIMITTEXT=0xC5,
// kEditLimit[i], 0) — la limite est PROPRE À CHAQUE BOÎTE (switch @0x50EDDD ; ce
// n'est pas 127 partout : 0x7F pour 0/1 @0x50EDF8/0x50EE45, mais 0x0C, 0x3C, 0x32,
// 0x04… ailleurs) — suivi d'un sous-classement de la WndProc vers UI_EditBoxWndProc
// 0x50E070 (navigation Tab/Entrée + filtrage de touches). La boîte 20 n'a NI limite
// NI sous-classement (`default: LABEL_2`).
//
// ---------------------------------------------------------------------------
// ÉTAT (vague W9, gap UIFW-07) — CE WRAPPER N'EST PAS CÂBLÉ.
// ---------------------------------------------------------------------------
// Grep exhaustif de `Win32EditBox` sur tout src/ : aucune instance, aucun appel à
// Create(). La classe est intégralement morte, et le projet a déjà tranché DANS
// L'AUTRE SENS (widget custom `ChatWindow`/`focused_`, cf. Scene/SceneManager.cpp:608-618).
// Ce fichier est conservé tel quel (sa suppression exigerait le .vcxproj, hors
// périmètre) mais ses DIVERGENCES prouvées ont été corrigées ci-dessous, pour ne pas
// piéger le front qui le câblerait un jour. Coût fonctionnel de l'absence d'EDIT natif :
// pas d'IME (saisie CJK impossible alors que g_LangId 0x84DFF8 pilote des tables
// localisées) et pas de presse-papier.
//
// MODÈLE D'ORIGINE, à comprendre AVANT tout câblage (commentaires IDA de 0x50E460 /
// 0x50F4A0) : les 21 EDIT sont créés puis MASQUÉS EN PERMANENCE (un seul ShowWindow
// dans tout le binaire, @0x50EDC4, avec nCmdShow=0=SW_HIDE — le bit WS_VISIBLE du style
// est neutralisé juste après coup). Ils ne peignent JAMAIS rien : ils ne servent que de
// TAMPON TEXTE + CIBLE DE FOCUS clavier/IME. Le moteur lit GetWindowTextA chaque frame
// et redessine lui-même en police bitmap (Scene_LoginRender 0x51B020 :
// GetWindowTextA(g_hEditLoginPw,…) @0x51B366, puis Crt_Memset(String, 42, strlen) @0x51B3CE
// — 42 = '*' — puis UI_DrawNumberValue(String, …) @0x51B40C). C'est POUR CELA que
// Present() n'entre jamais en conflit avec les EDIT.
// => Win32EditBox::Show() (ci-dessous) n'a AUCUNE contrepartie binaire et casserait ce
//    modèle : ne pas l'appeler dans un câblage fidèle.
//
// DÉVIATION RESTANTE, ASSUMÉE :
//   - Sous-classement UI_EditBoxWndProc 0x50E070 (SetWindowLongA(h, -4, …) @0x50EE19 &
//     co., posé sur les boîtes 0..19) NON reproduit ici : il route Tab/Entrée vers des
//     cibles de SCÈNE (case 0 : Tab -> UI_FocusEditBox(mgr,2) ; case 1 : Tab -> boîte 1,
//     Entrée -> g_SceneSubState=2 ; case 4 : Tab -> boîte 4, Entrée -> UI_Chat_SubmitInput ;
//     case 15 : Entrée -> Chat_SubmitTypedMessage) et exige un état global de focus
//     (g_UIEditBoxMgr 0x1668FC0) que ce wrapper n'a pas. Filtres génériques du default
//     (LABEL_53) : WM_CONTEXTMENU(0x7B) -> 1 @0x50E367 ; WM_KEYDOWN(0x100) avec
//     wParam ∈ [0x23..0x28] (END/HOME/←/↑/→/↓) -> 1 @0x50E39E ; WM_PASTE(0x302) bloqué si
//     `g_GmAuthLevel < 1 && i != 14` @0x50E376 ; sinon CallWindowProcA(lpPrevWndFunc 0x1669018).
//     TODO [ancre UI_EditBoxWndProc 0x50E070] : à porter avec le gestionnaire de focus
//     (UI_FocusEditBox 0x50F4A0) le jour où les EDIT natifs sont câblés.
//
// RAII : le handle est détruit au Destroy() (idempotent) et au destructeur.
// Classe non copiable (un HWND n'a qu'un seul propriétaire).
#pragma once
#include <windows.h>
#include <string>

// kEditLimit[21] (limites EM_LIMITTEXT par boîte, switch @0x50EDDD) est déjà défini —
// et abondamment prouvé cas par cas — dans UI/Widgets.h, dans CE MÊME namespace
// ts2::ui. On le RÉUTILISE au lieu d'en refaire une copie : deux `inline constexpr`
// homonymes dans le même namespace seraient une redéfinition (et, à valeurs égales,
// un piège de dérive). Table recoupée indépendamment sur 0x50EDDD : identique.
#include "UI/Widgets.h"

namespace ts2::ui {

// Taille du tampon de texte : 127 caractères utiles + terminateur NUL, fidèle à
// la limite EM_LIMITTEXT=0x7F posée par UI_CreateEditBoxes sur les boîtes 0/1 —
// la plus haute de kEditLimit (UI/Widgets.h), donc ce tampon couvre les 21 boîtes.
inline constexpr int kMaxText = 128;

// Nombre d'EDIT natifs créés par UI_CreateEditBoxes 0x50E460 (`if (v16 >= 21) return 1`
// @0x50ED0D).
inline constexpr int kEditBoxCount = 21;

// Style CONSTANT des 21 boîtes (CreateWindowExA @0x50ED93). ⚠ AUCUN ES_PASSWORD et aucun
// EM_SETPASSWORDCHAR nulle part dans le binaire : le masquage du mot de passe est
// PUREMENT LOGICIEL au rendu (Crt_Memset(String, 42, strlen) @0x51B3CE). Ajouter
// ES_PASSWORD ici serait une divergence — c'en était une (gap UIFW-07), corrigée.
inline constexpr DWORD kEditStyle   = 0x50800080; // WS_CHILD|WS_VISIBLE|WS_BORDER|ES_AUTOHSCROLL
inline constexpr DWORD kEditExStyle = 0x00000100; // WS_EX_CLIENTEDGE

// Rappel (table dans UI/Widgets.h:78) : kEditLimit[i] = limite EM_LIMITTEXT de la boîte
// i, 0 = AUCUNE limite posée. La boîte 20 tombe dans `default: LABEL_2` du switch
// @0x50EDDD (ni EM_LIMITTEXT, ni sous-classement) : d'où le 0 sentinelle en [20] — ce
// n'est pas un oubli, et Create() doit donc le tester avant d'envoyer le message.
// Repères utiles : boîte 0/1 = login ID / mot de passe (0x7F) ; boîte 3 = cible de
// chuchotement (0x0C) ; boîte 4 = ligne de saisie du chat (0x3C) ; boîte 14 = seule
// boîte exemptée du blocage WM_PASTE (`i != 14` @0x50E376).

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

    // Crée le contrôle EDIT enfant de `parent`, fidèle à CreateWindowExA @0x50ED93 :
    // exStyle=kEditExStyle, style=kEditStyle (constant, sans ES_PASSWORD),
    // hMenu=(HMENU)(boxIndex+1) — l'id de contrôle du binaire, @0x50ED93 — et
    // hInstance récupéré via GWLP_HINSTANCE du parent (= hInstance 0x815578).
    // Après création : ShowWindow(SW_HIDE) @0x50EDC4, puis EM_LIMITTEXT(kEditLimit[boxIndex])
    // si non nul (@0x50EDF8 & co.). Détruit tout handle préexistant. true si HWND valide.
    //
    // `boxIndex` ∈ [0, kEditBoxCount) = l'index de boîte du binaire (0 = login ID,
    // 1 = mot de passe, 3 = cible de chuchotement, 4 = saisie chat). Il porte À LA FOIS
    // l'id de contrôle et la limite de saisie — d'où le remplacement de l'ancien
    // paramètre `bool password` (qui n'existe pas dans le binaire, cf. gap UIFW-07).
    //
    // ⚠ `w`/`h` sont attendus DÉJÀ calculés par l'appelant. Le binaire les dérive du
    // rect de la boîte AVEC un +1 sur chaque dimension (@0x50ED93) :
    //   w = rect.right - rect.left + 1 ; h = rect.bottom - rect.top + 1
    // (rect[i] = g_UIEditBoxMgr+4*i+23 dwords, base 0x166901C). À reproduire côté appelant.
    bool Create(HWND parent, int x, int y, int w, int h, int boxIndex);

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

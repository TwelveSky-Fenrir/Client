// Input/InputSystem.h — sous-système ENTRÉE (DirectInput8 clavier + souris Win32).
//
// Réécriture FIDÈLE, vérité = désassemblage de TwelveSky2.exe (imagebase 0x400000).
// Ancres :
//   - Gfx_InitDevice        0x69B9B0  (queue : init DirectInput8, cf. 0x69C7F2..0x69C8C5)
//   - j_DirectInput8Create  0x6BC6C0  (thunk -> DirectInput8Create)
//   - Input_AcquireKeyboard 0x6A2130  (Acquire/poll ou Unacquire du clavier, par frame)
//   - App_FrameTick         0x4625D0  (appelle Input_AcquireKeyboard(g_WindowActive))
//   - App_WndProc           0x461930  (route les messages souris Win32)
//   - Input_ResetMouseState 0x50E000  (reset état drag caméra)
//   - Input_OnLButtonDown   0x50AC90 / Up 0x50AD20 ; RButtonDown 0x50ADB0 / Up 0x50AE40
//   - Camera_MouseDragRotate 0x50AFD0 (orbite par delta souris quand RMB tenu)
//
// ---------------------------------------------------------------------------
// CE QUE FAIT LE CLIENT D'ORIGINE (relevé exact)
// ---------------------------------------------------------------------------
// L'entrée matérielle du client N'UTILISE DirectInput8 QUE POUR LE CLAVIER.
// La souris passe entièrement par les messages Win32 (WndProc) — aucune donnée
// c_dfDIMouse/c_dfDIMouse2 n'existe dans le binaire (recherche vide).
//
// Les objets DirectInput sont rangés DANS le singleton renderer g_GfxRenderer
// (0x7FFE18), aux offsets suivants (this = 0x7FFE18, indices _DWORD) :
//   +5556 (0x8013CC) : IDirectInput8A*        (this+1389)  <- DirectInput8Create
//   +5560 (0x8013D0) : IDirectInputDevice8A*  (this+1390)  <- clavier
//   +5564 (0x8013D4) : BYTE state[256]        (this+1391)  <- GetDeviceState (état DIK immédiat)
//   +5820 (0x8014D4) : DWORD bufCount = 32    (this+1455)  <- pdwInOut de GetDeviceData
//   +5824 (0x8014D8) : DIDEVICEOBJECTDATA buf[32] (this+1456) <- GetDeviceData (événements tamponnés)
// (Les globales byte_8013E4..byte_8013F5 lues par Camera_UpdateFromInput 0x50B7D0
//  sont donc simplement state[0x8013D4 + DIK] : Q/W/E/R/A/S/D/F = DIK 0x10/0x11/0x12/0x13/0x1E/0x1F/0x20/0x21.)
//
// Séquence d'init (0x69C7F2..0x69C8C5), toutes les valeurs immédiates confirmées :
//   DirectInput8Create(hinst, 0x0800, IID_IDirectInput8A, &pDI, NULL)   // riidltf 0x7BBEF8
//   pDI->CreateDevice(GUID_SysKeyboard, &pKb, NULL)                     // off_7BC068 = GUID_SysKeyboard
//   pKb->SetDataFormat(&c_dfDIKeyboard)                                 // unk_7BC764 = c_dfDIKeyboard (dwDataSize=256)
//   pKb->SetCooperativeLevel(hwnd, DISCL_FOREGROUND|DISCL_NONEXCLUSIVE) // 4|2 = 6
//   pKb->SetProperty(DIPROP_BUFFERSIZE, {dwSize=20,dwHeaderSize=16,dwObj=0,dwHow=0,dwData=32})
//   pKb->Acquire()
//
// Poll par frame (Input_AcquireKeyboard, active = g_WindowActive) :
//   si actif   : Acquire() ; memset(state,0,256) ; GetDeviceState(256,state) ;
//                bufCount=32 ; GetDeviceData(sizeof(DIDEVICEOBJECTDATA)=20, buf, &bufCount, 0)
//   si inactif : Unacquire() ; bufCount=0
//
// Souris (App_WndProc, indices lParam = LOWORD=x, HIWORD=y) :
//   WM_MOUSEMOVE   0x200 -> Camera_MouseDragRotate (orbite SEULEMENT si wParam==MK_RBUTTON=2)
//   WM_LBUTTONDOWN 0x201 -> SetCapture + Input_OnLButtonDown(x,y)
//   WM_LBUTTONUP   0x202 -> ReleaseCapture + Input_OnLButtonUp(x,y)
//   WM_RBUTTONDOWN 0x204 -> SetCapture + Input_OnRButtonDown(x,y)
//   WM_RBUTTONUP   0x205 -> ReleaseCapture + Input_OnRButtonUp(x,y)
//   WM_MBUTTONDOWN  0x207 -> Camera_ResetView(x,y)
//   WM_MOUSEWHEEL  0x20A -> Camera_MouseWheelZoom(SHIWORD(wParam) = cran molette)
//
// NOTE anticheat : rien de GameGuard/nProtect n'est réimplémenté (hors périmètre).
// ---------------------------------------------------------------------------
#pragma once

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

#include <windows.h>
#include <dinput.h>
#include <cstdint>
#include <functional>

namespace ts2::input {

// ---------------------------------------------------------------------------
// Scancodes DIK utiles, relevés dans le désassemblage (valeurs = octets DIK).
// <dinput.h> définit déjà DIK_* ; on redonne ici les codes exploités par le
// client avec leur rôle vérifié, plus les quickslots 1..0.
// ---------------------------------------------------------------------------
namespace dik {
// Quickslots / touches numériques : DIK_1..DIK_9 = 0x02..0x0A, DIK_0 = 0x0B.
// (cases 2..10 de Camera_UpdateFromInput 0x50B7D0 = DIK_1..DIK_9.)
inline constexpr int k1 = 0x02;
inline constexpr int k2 = 0x03;
inline constexpr int k3 = 0x04;
inline constexpr int k4 = 0x05;
inline constexpr int k5 = 0x06;
inline constexpr int k6 = 0x07;
inline constexpr int k7 = 0x08;
inline constexpr int k8 = 0x09;
inline constexpr int k9 = 0x0A;
inline constexpr int k0 = 0x0B;

// Table des quickslots dans l'ordre d'affichage 1,2,3,4,5,6,7,8,9,0.
inline constexpr int kQuickslot[10] = { k1, k2, k3, k4, k5, k6, k7, k8, k9, k0 };

// Déplacement (état lu directement dans state[] par Camera_UpdateFromInput) :
inline constexpr int kQ = 0x10; // strafe gauche   (byte_8013E4)
inline constexpr int kW = 0x11; // avancer         (byte_8013E5)
inline constexpr int kE = 0x12; // strafe droite   (byte_8013E6)
inline constexpr int kR = 0x13; // monter          (byte_8013E7)
inline constexpr int kA = 0x1E; // rotation gauche (byte_8013F2)
inline constexpr int kS = 0x1F; // reculer         (byte_8013F3)
inline constexpr int kD = 0x20; // rotation droite (byte_8013F4)
inline constexpr int kF = 0x21; // descendre       (byte_8013F5)

// Autres touches d'action (via les événements tamponnés, cases de 0x50B7D0) :
inline constexpr int kP    = 0x19; // case 25  : bascule vue épaule
inline constexpr int kL    = 0x26; // case 38  : verrou/ciblage
inline constexpr int kZ    = 0x2C; // case 44  : zoom -
inline constexpr int kX    = 0x2D; // case 45  : cycle
inline constexpr int kC    = 0x2E; // case 46  : zoom +
inline constexpr int kF12  = 0x58; // case 88  : capture d'écran (Screenshot_SaveNext)
inline constexpr int kHome  = 0xC7; // case 199 : bascule état (this+0)
inline constexpr int kPrior = 0xC9; // case 201 : cycle (this+1)
inline constexpr int kEnd   = 0xCF; // case 207 : bascule souris + Input_ResetMouseState
inline constexpr int kReturn = 0x1C; // WndProc WM_KEYDOWN(VK_RETURN) -> focus chat
} // namespace dik

// Un événement clavier tamponné, décodé depuis un DIDEVICEOBJECTDATA.
struct KeyEvent {
    uint8_t dik;      // DIDEVICEOBJECTDATA.dwOfs (scancode DIK)
    bool    pressed;  // (DIDEVICEOBJECTDATA.dwData & 0x80) != 0
};

// État souris (dérivé des messages Win32, cf. App_WndProc).
struct MouseState {
    int  x = 0;            // dernière position X (client), LOWORD(lParam)
    int  y = 0;            // dernière position Y (client), HIWORD(lParam)
    int  dx = 0;           // delta X accumulé depuis le dernier GetMouseDelta()
    int  dy = 0;           // delta Y accumulé depuis le dernier GetMouseDelta()
    int  wheel = 0;        // cran molette accumulé (WM_MOUSEWHEEL, SHIWORD(wParam))
    bool left = false;     // bouton gauche enfoncé
    bool right = false;    // bouton droit enfoncé
};

// Constantes DirectInput réutilisées (relevées dans Gfx_InitDevice).
inline constexpr DWORD kCoopFlags     = DISCL_FOREGROUND | DISCL_NONEXCLUSIVE; // = 6
inline constexpr DWORD kKbBufferSize  = 32;   // dwData du DIPROP_BUFFERSIZE
inline constexpr int   kKeyStateBytes = 256;  // c_dfDIKeyboard : dwDataSize = 0x100

// Callbacks optionnels pour router la souris comme App_WndProc (le module reste
// autonome : il n'inclut aucun header UI/Scene, il expose des hooks à câbler).
using MouseButtonCb = std::function<void(int x, int y)>;
using MouseWheelCb  = std::function<void(int delta)>;

// ---------------------------------------------------------------------------
// InputSystem : wrapper DirectInput8 clavier + suivi souris Win32.
// ---------------------------------------------------------------------------
class InputSystem {
public:
    InputSystem() = default;
    ~InputSystem();

    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    // Init DirectInput8 clavier (Gfx_InitDevice 0x69C7F2..0x69C8C5).
    // hinst = HINSTANCE module ; hwnd = fenêtre principale (celle capturée par
    // SetCapture dans App_WndProc). Renvoie false sur échec (device relâché).
    bool Init(HINSTANCE hinst, HWND hwnd);

    // Unacquire + Release device + Release IDirectInput8. Idempotent.
    void Shutdown();

    bool IsInitialized() const { return keyboard_ != nullptr; }

    // Poll par frame — équivalent exact de Input_AcquireKeyboard(active).
    // 'windowActive' = g_WindowActive (0x81558C, mis à jour par WM_ACTIVATEAPP).
    void Poll(bool windowActive);

    // Acquire/Unacquire explicites (vtable+28 / vtable+32).
    HRESULT Acquire();
    HRESULT Unacquire();

    // ---- Clavier : état immédiat (GetDeviceState, 256 octets DIK) ----------
    // Touche enfoncée ssi bit 0x80 posé (test « byte < 0 » du binaire).
    bool IsKeyDown(int dik) const {
        return (dik >= 0 && dik < kKeyStateBytes) &&
               (keyState_[static_cast<unsigned>(dik)] & 0x80) != 0;
    }
    const uint8_t* KeyState() const { return keyState_; }

    // ---- Clavier : événements tamponnés (GetDeviceData, DIPROP_BUFFERSIZE=32) ----
    int      BufferedCount() const { return static_cast<int>(bufCount_); }
    KeyEvent BufferedEvent(int i) const;
    // Premier événement APPUI (dwData&0x80) du tampon, ou -1 (boucle de 0x50B7D0).
    int      FirstKeyDownDik() const;

    // ---- Souris (messages Win32) ------------------------------------------
    // Décode et applique un message Win32 souris exactement comme App_WndProc
    // (y compris Set/ReleaseCapture). Renvoie true si le message a été géré.
    bool ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Alimentation directe (si l'appelant ne passe pas par ProcessMessage) —
    // reflète le routage 0x461930.
    void OnMouseMove(int x, int y, WPARAM keyFlags);       // WM_MOUSEMOVE 0x200
    void OnLButtonDown(int x, int y);                       // WM_LBUTTONDOWN 0x201
    void OnLButtonUp(int x, int y);                         // WM_LBUTTONUP 0x202
    void OnRButtonDown(int x, int y);                       // WM_RBUTTONDOWN 0x204
    void OnRButtonUp(int x, int y);                         // WM_RBUTTONUP 0x205
    void OnMButtonDown(int x, int y);                       // WM_MBUTTONDOWN 0x207
    void OnMouseWheel(int delta);                           // WM_MOUSEWHEEL 0x20A

    // Accès état souris.
    const MouseState& Mouse() const { return mouse_; }
    int  MouseX() const { return mouse_.x; }
    int  MouseY() const { return mouse_.y; }
    bool LeftButtonDown() const { return mouse_.left; }
    bool RightButtonDown() const { return mouse_.right; }

    // Delta souris accumulé depuis le dernier appel (puis remis à zéro).
    // Fidèle à Camera_MouseDragRotate : delta = position courante - dernière.
    void GetMouseDelta(int& dx, int& dy);
    // Cran molette accumulé depuis le dernier appel (puis remis à zéro).
    int  ConsumeWheel();

    // Hooks optionnels (défaut : aucun) — câblés à Input_On*/Camera_* par le shell.
    //
    // ÉTAT DU CÂBLAGE (vérifié par grep exhaustif, vague W9) : 4 des 6 setters sont
    // assignés par App::Init — SetMButtonDownCallback (App.cpp:584 -> Camera_ResetView
    // 0x50AED0), SetMouseWheelCallback (App.cpp:607 -> Camera_MouseWheelZoom 0x50B460),
    // SetRButtonDownCallback (App.cpp:640 -> Input_OnRButtonDown 0x50ADB0) et
    // SetRButtonUpCallback (App.cpp:658 -> Input_OnRButtonUp 0x50AE40). Les hooks du clic
    // DROIT s'exécutent donc réellement à chaque clic ; leur chaîne terminale est
    // ui::UIManager::RouteRButtonDown/Up (UI/UIManager.h), miroir de UI_RouteRButtonDown
    // 0x5AD5D0 / UI_RouteRButtonUp 0x5ADA90.
    //
    // ⚠ SetLButtonDownCallback / SetLButtonUpCallback n'ont, EUX, AUCUN appelant :
    // onLDown_/onLUp_ restent perpétuellement nuls. Ce n'est PAS un défaut — le clic
    // GAUCHE emprunte une autre route, équivalente et déjà câblée : App::HandleMessage
    // le pousse directement à scene_.OnLButtonDown/Up (App.cpp:1043-1050), sans passer
    // par ces hooks. Les setters sont conservés (symétrie de l'API + fidélité au
    // App_WndProc d'origine, qui appelle bien Input_OnLButtonDown 0x50AC90 @0x461A22 sur
    // WM_LBUTTONDOWN, après SetCapture(hWndParent) @0x4619FB) mais restent inertes tant
    // qu'App ne bascule pas le clic gauche sur le même schéma que le clic droit. Ne pas
    // les supprimer sans re-câbler App.
    void SetLButtonDownCallback(MouseButtonCb cb) { onLDown_ = std::move(cb); }
    void SetLButtonUpCallback(MouseButtonCb cb)   { onLUp_   = std::move(cb); }
    void SetRButtonDownCallback(MouseButtonCb cb) { onRDown_ = std::move(cb); }
    void SetRButtonUpCallback(MouseButtonCb cb)   { onRUp_   = std::move(cb); }
    void SetMButtonDownCallback(MouseButtonCb cb) { onMDown_ = std::move(cb); }
    void SetMouseWheelCallback(MouseWheelCb cb)   { onWheel_ = std::move(cb); }

private:
    HWND                  hwnd_     = nullptr;
    IDirectInput8A*       di_       = nullptr;  // g_GfxRenderer+5556
    IDirectInputDevice8A* keyboard_ = nullptr;  // g_GfxRenderer+5560
    bool                  acquired_ = false;

    // État clavier immédiat : g_GfxRenderer+5564, 256 octets DIK.
    uint8_t              keyState_[kKeyStateBytes] = {};
    // Événements tamponnés : g_GfxRenderer+5824, 32 * DIDEVICEOBJECTDATA.
    DIDEVICEOBJECTDATA   buf_[kKbBufferSize] = {};
    DWORD                bufCount_ = 0;          // g_GfxRenderer+5820

    MouseState           mouse_;

    MouseButtonCb onLDown_, onLUp_, onRDown_, onRUp_, onMDown_;
    MouseWheelCb  onWheel_;
};

} // namespace ts2::input

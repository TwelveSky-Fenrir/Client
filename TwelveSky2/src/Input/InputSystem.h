// Input/InputSystem.h — INPUT subsystem (DirectInput8 keyboard + Win32 mouse).
//
// FAITHFUL rewrite, source of truth = TwelveSky2.exe disassembly (imagebase 0x400000).
// Anchors:
//   - Gfx_InitDevice        0x69B9B0  (queue: DirectInput8 init, cf. 0x69C7F2..0x69C8C5)
//   - j_DirectInput8Create  0x6BC6C0  (thunk -> DirectInput8Create)
//   - Input_AcquireKeyboard 0x6A2130  (Acquire/poll or Unacquire the keyboard, per frame)
//   - App_FrameTick         0x4625D0  (calls Input_AcquireKeyboard(g_WindowActive))
//   - App_WndProc           0x461930  (routes Win32 mouse messages)
//   - Input_ResetMouseState 0x50E000  (resets camera drag state)
//   - Input_OnLButtonDown   0x50AC90 / Up 0x50AD20 ; RButtonDown 0x50ADB0 / Up 0x50AE40
//   - Camera_MouseDragRotate 0x50AFD0 (orbit by mouse delta while RMB held)
//
// ---------------------------------------------------------------------------
// WHAT THE ORIGINAL CLIENT DOES (exact findings)
// ---------------------------------------------------------------------------
// Hardware input in the original client ONLY USES DirectInput8 FOR THE KEYBOARD.
// The mouse goes entirely through Win32 messages (WndProc) — no
// c_dfDIMouse/c_dfDIMouse2 data exists in the binary (search came up empty).
//
// The DirectInput objects live INSIDE the renderer singleton g_GfxRenderer
// (0x7FFE18), at the following offsets (this = 0x7FFE18, _DWORD indices):
//   +5556 (0x8013CC) : IDirectInput8A*        (this+1389)  <- DirectInput8Create
//   +5560 (0x8013D0) : IDirectInputDevice8A*  (this+1390)  <- keyboard
//   +5564 (0x8013D4) : BYTE state[256]        (this+1391)  <- GetDeviceState (immediate DIK state)
//   +5820 (0x8014D4) : DWORD bufCount = 32    (this+1455)  <- pdwInOut of GetDeviceData
//   +5824 (0x8014D8) : DIDEVICEOBJECTDATA buf[32] (this+1456) <- GetDeviceData (buffered events)
// (The byte_8013E4..byte_8013F5 globals read by Camera_UpdateFromInput 0x50B7D0
//  are therefore simply state[0x8013D4 + DIK]: Q/W/E/R/A/S/D/F = DIK 0x10/0x11/0x12/0x13/0x1E/0x1F/0x20/0x21.)
//
// Init sequence (0x69C7F2..0x69C8C5), all immediate values confirmed:
//   DirectInput8Create(hinst, 0x0800, IID_IDirectInput8A, &pDI, NULL)   // riidltf 0x7BBEF8
//   pDI->CreateDevice(GUID_SysKeyboard, &pKb, NULL)                     // off_7BC068 = GUID_SysKeyboard
//   pKb->SetDataFormat(&c_dfDIKeyboard)                                 // unk_7BC764 = c_dfDIKeyboard (dwDataSize=256)
//   pKb->SetCooperativeLevel(hwnd, DISCL_FOREGROUND|DISCL_NONEXCLUSIVE) // 4|2 = 6
//   pKb->SetProperty(DIPROP_BUFFERSIZE, {dwSize=20,dwHeaderSize=16,dwObj=0,dwHow=0,dwData=32})
//   pKb->Acquire()
//
// Per-frame poll (Input_AcquireKeyboard, active = g_WindowActive):
//   if active   : Acquire() ; memset(state,0,256) ; GetDeviceState(256,state) ;
//                bufCount=32 ; GetDeviceData(sizeof(DIDEVICEOBJECTDATA)=20, buf, &bufCount, 0)
//   if inactive : Unacquire() ; bufCount=0
//
// Mouse (App_WndProc, lParam indices = LOWORD=x, HIWORD=y):
//   WM_MOUSEMOVE   0x200 -> Camera_MouseDragRotate (orbit ONLY if wParam==MK_RBUTTON=2)
//   WM_LBUTTONDOWN 0x201 -> SetCapture + Input_OnLButtonDown(x,y)
//   WM_LBUTTONUP   0x202 -> ReleaseCapture + Input_OnLButtonUp(x,y)
//   WM_RBUTTONDOWN 0x204 -> SetCapture + Input_OnRButtonDown(x,y)
//   WM_RBUTTONUP   0x205 -> ReleaseCapture + Input_OnRButtonUp(x,y)
//   WM_MBUTTONDOWN  0x207 -> Camera_ResetView(x,y)
//   WM_MOUSEWHEEL  0x20A -> Camera_MouseWheelZoom(SHIWORD(wParam) = wheel notch)
//
// ANTICHEAT NOTE: nothing from GameGuard/nProtect is reimplemented (out of scope).
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
// Useful DIK scancodes, found in the disassembly (values = DIK bytes).
// <dinput.h> already defines DIK_*; here we restate the codes the client
// actually uses with their verified role, plus the quickslots 1..0.
// ---------------------------------------------------------------------------
namespace dik {
// Quickslots / number keys: DIK_1..DIK_9 = 0x02..0x0A, DIK_0 = 0x0B.
// (cases 2..10 of Camera_UpdateFromInput 0x50B7D0 = DIK_1..DIK_9.)
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

// Quickslot table in display order 1,2,3,4,5,6,7,8,9,0.
inline constexpr int kQuickslot[10] = { k1, k2, k3, k4, k5, k6, k7, k8, k9, k0 };

// Movement (state read directly from state[] by Camera_UpdateFromInput):
inline constexpr int kQ = 0x10; // strafe left     (byte_8013E4)
inline constexpr int kW = 0x11; // move forward     (byte_8013E5)
inline constexpr int kE = 0x12; // strafe right     (byte_8013E6)
inline constexpr int kR = 0x13; // move up          (byte_8013E7)
inline constexpr int kA = 0x1E; // rotate left      (byte_8013F2)
inline constexpr int kS = 0x1F; // move backward    (byte_8013F3)
inline constexpr int kD = 0x20; // rotate right     (byte_8013F4)
inline constexpr int kF = 0x21; // move down        (byte_8013F5)

// Other action keys (via buffered events, cases from 0x50B7D0):
inline constexpr int kP    = 0x19; // case 25  : toggle shoulder view
inline constexpr int kL    = 0x26; // case 38  : lock/target
inline constexpr int kZ    = 0x2C; // case 44  : zoom -
inline constexpr int kX    = 0x2D; // case 45  : cycle
inline constexpr int kC    = 0x2E; // case 46  : zoom +
inline constexpr int kF12  = 0x58; // case 88  : screenshot (Screenshot_SaveNext)
inline constexpr int kHome  = 0xC7; // case 199 : toggle state (this+0)
inline constexpr int kPrior = 0xC9; // case 201 : cycle (this+1)
inline constexpr int kEnd   = 0xCF; // case 207 : toggle mouse state + Input_ResetMouseState
inline constexpr int kReturn = 0x1C; // WndProc WM_KEYDOWN(VK_RETURN) -> focus chat
} // namespace dik

// A buffered keyboard event, decoded from a DIDEVICEOBJECTDATA.
struct KeyEvent {
    uint8_t dik;      // DIDEVICEOBJECTDATA.dwOfs (DIK scancode)
    bool    pressed;  // (DIDEVICEOBJECTDATA.dwData & 0x80) != 0
};

// Mouse state (derived from Win32 messages, cf. App_WndProc).
struct MouseState {
    int  x = 0;            // last X position (client), LOWORD(lParam)
    int  y = 0;            // last Y position (client), HIWORD(lParam)
    int  dx = 0;           // accumulated X delta since last GetMouseDelta()
    int  dy = 0;           // accumulated Y delta since last GetMouseDelta()
    int  wheel = 0;        // accumulated wheel notch (WM_MOUSEWHEEL, SHIWORD(wParam))
    bool left = false;     // left button held
    bool right = false;    // right button held
};

// Reused DirectInput constants (found in Gfx_InitDevice).
inline constexpr DWORD kCoopFlags     = DISCL_FOREGROUND | DISCL_NONEXCLUSIVE; // = 6
inline constexpr DWORD kKbBufferSize  = 32;   // dwData of DIPROP_BUFFERSIZE
inline constexpr int   kKeyStateBytes = 256;  // c_dfDIKeyboard: dwDataSize = 0x100

// Optional callbacks to route the mouse like App_WndProc (the module stays
// standalone: it doesn't include any UI/Scene header, it exposes hooks to wire up).
using MouseButtonCb = std::function<void(int x, int y)>;
using MouseWheelCb  = std::function<void(int delta)>;

// ---------------------------------------------------------------------------
// InputSystem: DirectInput8 keyboard wrapper + Win32 mouse tracking.
// ---------------------------------------------------------------------------
class InputSystem {
public:
    InputSystem() = default;
    ~InputSystem();

    InputSystem(const InputSystem&) = delete;
    InputSystem& operator=(const InputSystem&) = delete;

    // Init DirectInput8 keyboard (Gfx_InitDevice 0x69C7F2..0x69C8C5).
    // hinst = module HINSTANCE; hwnd = main window (the one captured via
    // SetCapture in App_WndProc). Returns false on failure (device released).
    bool Init(HINSTANCE hinst, HWND hwnd);

    // Unacquire + Release device + Release IDirectInput8. Idempotent.
    void Shutdown();

    bool IsInitialized() const { return keyboard_ != nullptr; }

    // Per-frame poll — exact equivalent of Input_AcquireKeyboard(active).
    // 'windowActive' = g_WindowActive (0x81558C, updated by WM_ACTIVATEAPP).
    void Poll(bool windowActive);

    // Explicit Acquire/Unacquire (vtable+28 / vtable+32).
    HRESULT Acquire();
    HRESULT Unacquire();

    // ---- Keyboard: immediate state (GetDeviceState, 256 DIK bytes) --------
    // Key down iff bit 0x80 set (the binary's "byte < 0" test).
    bool IsKeyDown(int dik) const {
        return (dik >= 0 && dik < kKeyStateBytes) &&
               (keyState_[static_cast<unsigned>(dik)] & 0x80) != 0;
    }
    const uint8_t* KeyState() const { return keyState_; }

    // ---- Keyboard: buffered events (GetDeviceData, DIPROP_BUFFERSIZE=32) ----
    int      BufferedCount() const { return static_cast<int>(bufCount_); }
    KeyEvent BufferedEvent(int i) const;
    // First DOWN event (dwData&0x80) in the buffer, or -1 (loop from 0x50B7D0).
    int      FirstKeyDownDik() const;

    // ---- Mouse (Win32 messages) --------------------------------------------
    // Decodes and applies a Win32 mouse message exactly like App_WndProc
    // (including Set/ReleaseCapture). Returns true if the message was handled.
    bool ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    // Direct feed (if the caller doesn't go through ProcessMessage) —
    // mirrors the 0x461930 routing.
    void OnMouseMove(int x, int y, WPARAM keyFlags);       // WM_MOUSEMOVE 0x200
    void OnLButtonDown(int x, int y);                       // WM_LBUTTONDOWN 0x201
    void OnLButtonUp(int x, int y);                         // WM_LBUTTONUP 0x202
    void OnRButtonDown(int x, int y);                       // WM_RBUTTONDOWN 0x204
    void OnRButtonUp(int x, int y);                         // WM_RBUTTONUP 0x205
    void OnMButtonDown(int x, int y);                       // WM_MBUTTONDOWN 0x207
    void OnMouseWheel(int delta);                           // WM_MOUSEWHEEL 0x20A

    // Mouse state access.
    const MouseState& Mouse() const { return mouse_; }
    int  MouseX() const { return mouse_.x; }
    int  MouseY() const { return mouse_.y; }
    bool LeftButtonDown() const { return mouse_.left; }
    bool RightButtonDown() const { return mouse_.right; }

    // Accumulated mouse delta since the last call (then reset to zero).
    // Faithful to Camera_MouseDragRotate: delta = current position - last.
    void GetMouseDelta(int& dx, int& dy);
    // Accumulated wheel notch since the last call (then reset to zero).
    int  ConsumeWheel();

    // Optional hooks (default: none) — wired to Input_On*/Camera_* by the shell.
    //
    // WIRING STATE (verified by exhaustive grep, wave W9): 4 of the 6 setters are
    // assigned by App::Init — SetMButtonDownCallback (App.cpp:584 -> Camera_ResetView
    // 0x50AED0), SetMouseWheelCallback (App.cpp:607 -> Camera_MouseWheelZoom 0x50B460),
    // SetRButtonDownCallback (App.cpp:640 -> Input_OnRButtonDown 0x50ADB0) and
    // SetRButtonUpCallback (App.cpp:658 -> Input_OnRButtonUp 0x50AE40). The RIGHT-click
    // hooks therefore really do run on every click; their terminal chain is
    // ui::UIManager::RouteRButtonDown/Up (UI/UIManager.h), mirroring UI_RouteRButtonDown
    // 0x5AD5D0 / UI_RouteRButtonUp 0x5ADA90.
    //
    // WARNING SetLButtonDownCallback / SetLButtonUpCallback have NO caller at all:
    // onLDown_/onLUp_ stay permanently null. This is NOT a defect — the LEFT click
    // takes a different, already-wired equivalent route: App::HandleMessage pushes it
    // directly to scene_.OnLButtonDown/Up (App.cpp:1043-1050), bypassing these hooks.
    // The setters are kept (API symmetry + fidelity to the original App_WndProc, which
    // does call Input_OnLButtonDown 0x50AC90 @0x461A22 on WM_LBUTTONDOWN, after
    // SetCapture(hWndParent) @0x4619FB) but stay inert unless App switches the left
    // click to the same scheme as the right click. Do not remove them without
    // re-wiring App.
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

    // Immediate keyboard state: g_GfxRenderer+5564, 256 DIK bytes.
    uint8_t              keyState_[kKeyStateBytes] = {};
    // Buffered events: g_GfxRenderer+5824, 32 * DIDEVICEOBJECTDATA.
    DIDEVICEOBJECTDATA   buf_[kKbBufferSize] = {};
    DWORD                bufCount_ = 0;          // g_GfxRenderer+5820

    MouseState           mouse_;

    MouseButtonCb onLDown_, onLUp_, onRDown_, onRUp_, onMDown_;
    MouseWheelCb  onWheel_;
};

} // namespace ts2::input

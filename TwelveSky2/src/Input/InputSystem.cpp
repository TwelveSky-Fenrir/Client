// Input/InputSystem.cpp — FAITHFUL implementation of the INPUT subsystem.
//
// Source of truth = TwelveSky2.exe disassembly. See InputSystem.h for the
// memory map and EA anchor table. Key points reproduced identically:
//   - Keyboard init: Gfx_InitDevice queue 0x69C7F2..0x69C8C5.
//   - Keyboard poll: Input_AcquireKeyboard 0x6A2130.
//   - Mouse routing: App_WndProc 0x461930.
#include "InputSystem.h"

#include <cstring>  // std::memset

#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")

namespace ts2::input {

// DIDEVICEOBJECTDATA is 20 bytes in 32-bit (cbObjectData passed = 20 in
// GetDeviceData at 0x6A21A0). Checked here to stay bit-faithful to the binary.
static_assert(sizeof(DIDEVICEOBJECTDATA) == 20,
              "DIDEVICEOBJECTDATA must be 20 bytes (faithful Win32 build)");

InputSystem::~InputSystem() {
    Shutdown();
}

// -----------------------------------------------------------------------------
// Init — reproduces the Gfx_InitDevice queue (0x69C7F2..0x69C8C5).
// -----------------------------------------------------------------------------
bool InputSystem::Init(HINSTANCE hinst, HWND hwnd) {
    Shutdown();
    hwnd_ = hwnd;

    // DirectInput8Create(hinst, 0x0800, IID_IDirectInput8A, &di_, NULL)  (0x69C80B)
    HRESULT hr = DirectInput8Create(hinst, DIRECTINPUT_VERSION, IID_IDirectInput8A,
                                    reinterpret_cast<void**>(&di_), nullptr);
    if (FAILED(hr)) {          // *a11 = 16
        di_ = nullptr;
        return false;
    }

    // di_->CreateDevice(GUID_SysKeyboard, &keyboard_, NULL)  (vtable+12, 0x69C82F)
    hr = di_->CreateDevice(GUID_SysKeyboard, &keyboard_, nullptr);
    if (FAILED(hr)) {          // *a11 = 17
        Shutdown();
        return false;
    }

    // keyboard_->SetDataFormat(&c_dfDIKeyboard)  (vtable+44, 0x69C84F)
    hr = keyboard_->SetDataFormat(&c_dfDIKeyboard);
    if (FAILED(hr)) {          // *a11 = 18
        Shutdown();
        return false;
    }

    // keyboard_->SetCooperativeLevel(hwnd, DISCL_FOREGROUND|DISCL_NONEXCLUSIVE=6)
    //   (vtable+52, 0x69C871)
    hr = keyboard_->SetCooperativeLevel(hwnd_, kCoopFlags);
    if (FAILED(hr)) {          // *a11 = 19
        Shutdown();
        return false;
    }

    // keyboard_->SetProperty(DIPROP_BUFFERSIZE, {20,16,0,DIPH_DEVICE,32})
    //   (vtable+24, 0x69C8AF; v65 = {20,16,0,0,32})
    DIPROPDWORD dipdw;
    dipdw.diph.dwSize       = sizeof(DIPROPDWORD);   // 20
    dipdw.diph.dwHeaderSize = sizeof(DIPROPHEADER);  // 16
    dipdw.diph.dwObj        = 0;
    dipdw.diph.dwHow        = DIPH_DEVICE;           // 0
    dipdw.dwData            = kKbBufferSize;         // 32
    hr = keyboard_->SetProperty(DIPROP_BUFFERSIZE, &dipdw.diph);
    if (FAILED(hr)) {          // *a11 = 20
        Shutdown();
        return false;
    }

    // keyboard_->Acquire()  (vtable+28, 0x69C8C5)
    Acquire();
    return true;
}

void InputSystem::Shutdown() {
    if (keyboard_) {
        keyboard_->Unacquire();
        keyboard_->Release();
        keyboard_ = nullptr;
    }
    if (di_) {
        di_->Release();
        di_ = nullptr;
    }
    acquired_ = false;
    bufCount_ = 0;
}

HRESULT InputSystem::Acquire() {
    if (!keyboard_) return E_FAIL;
    HRESULT hr = keyboard_->Acquire();          // DI_OK or S_FALSE (already acquired)
    if (SUCCEEDED(hr)) acquired_ = true;
    return hr;
}

HRESULT InputSystem::Unacquire() {
    if (!keyboard_) return E_FAIL;
    acquired_ = false;
    return keyboard_->Unacquire();
}

// -----------------------------------------------------------------------------
// Poll — exact equivalent of Input_AcquireKeyboard 0x6A2130.
//   a2 (windowActive) != 0: Acquire; memset(state,0,256); GetDeviceState;
//                           bufCount=32; GetDeviceData.
//   a2 == 0                : Unacquire; bufCount=0.
// -----------------------------------------------------------------------------
void InputSystem::Poll(bool windowActive) {
    if (!keyboard_) return;

    if (windowActive) {
        // vtable+28: Acquire (called unconditionally every frame)
        keyboard_->Acquire();
        acquired_ = true;

        // memset(state, 0, 0x100) then GetDeviceState(256, state)  (vtable+36)
        std::memset(keyState_, 0, sizeof(keyState_));
        keyboard_->GetDeviceState(kKeyStateBytes, keyState_);

        // bufCount=32; GetDeviceData(20, buf, &bufCount, 0)  (vtable+40)
        bufCount_ = kKbBufferSize;
        HRESULT hr = keyboard_->GetDeviceData(sizeof(DIDEVICEOBJECTDATA),
                                              buf_, &bufCount_, 0);
        if (FAILED(hr))
            bufCount_ = 0;   // the binary ignores the value; we neutralize the buffer
    } else {
        // vtable+32: Unacquire; the binary resets the event counter to 0.
        keyboard_->Unacquire();
        acquired_ = false;
        bufCount_ = 0;
    }
}

// -----------------------------------------------------------------------------
// Buffered keyboard.
// -----------------------------------------------------------------------------
KeyEvent InputSystem::BufferedEvent(int i) const {
    KeyEvent ev{ 0, false };
    if (i >= 0 && i < static_cast<int>(bufCount_)) {
        ev.dik     = static_cast<uint8_t>(buf_[i].dwOfs);
        ev.pressed = (buf_[i].dwData & 0x80) != 0;
    }
    return ev;
}

// "for i: if (buf[i].dwData & 0x80) -> break" loop from Camera_UpdateFromInput.
int InputSystem::FirstKeyDownDik() const {
    for (int i = 0; i < static_cast<int>(bufCount_); ++i) {
        if ((buf_[i].dwData & 0x80) != 0)
            return static_cast<int>(buf_[i].dwOfs);
    }
    return -1;
}

// -----------------------------------------------------------------------------
// Mouse — routing identical to App_WndProc 0x461930.
// -----------------------------------------------------------------------------
bool InputSystem::ProcessMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    const int x = static_cast<int>(static_cast<short>(LOWORD(lParam)));
    const int y = static_cast<int>(static_cast<short>(HIWORD(lParam)));
    switch (msg) {
    case WM_MOUSEMOVE:        // 0x200 -> Camera_MouseDragRotate(x, y, wParam)
        OnMouseMove(x, y, wParam);
        return true;
    case WM_LBUTTONDOWN:      // 0x201 -> SetCapture + Input_OnLButtonDown
        OnLButtonDown(x, y);
        return true;
    case WM_LBUTTONUP:        // 0x202 -> ReleaseCapture + Input_OnLButtonUp
        OnLButtonUp(x, y);
        return true;
    case WM_RBUTTONDOWN:      // 0x204 -> SetCapture + Input_OnRButtonDown
        OnRButtonDown(x, y);
        return true;
    case WM_RBUTTONUP:        // 0x205 -> ReleaseCapture + Input_OnRButtonUp
        OnRButtonUp(x, y);
        return true;
    case WM_MBUTTONDOWN:      // 0x207 -> Camera_ResetView
        OnMButtonDown(x, y);
        return true;
    case WM_MOUSEWHEEL:       // 0x20A -> Camera_MouseWheelZoom(SHIWORD(wParam))
        OnMouseWheel(static_cast<int>(static_cast<short>(HIWORD(wParam))));
        return true;
    default:
        return false;
    }
}

void InputSystem::OnMouseMove(int x, int y, WPARAM /*keyFlags*/) {
    // Delta = current position - last (Camera_MouseDragRotate 0x50B0BA).
    // We accumulate; the right button (MK_RBUTTON=2) is the orbit condition,
    // left to the caller via RightButtonDown()/GetMouseDelta().
    mouse_.dx += x - mouse_.x;
    mouse_.dy += y - mouse_.y;
    mouse_.x = x;
    mouse_.y = y;
}

void InputSystem::OnLButtonDown(int x, int y) {
    mouse_.left = true;
    if (hwnd_) SetCapture(hwnd_);       // App_WndProc: SetCapture(hWndParent)
    if (onLDown_) onLDown_(x, y);       // -> Input_OnLButtonDown 0x50AC90
}

void InputSystem::OnLButtonUp(int x, int y) {
    mouse_.left = false;
    ReleaseCapture();                   // App_WndProc: ReleaseCapture()
    if (onLUp_) onLUp_(x, y);           // -> Input_OnLButtonUp 0x50AD20
}

void InputSystem::OnRButtonDown(int x, int y) {
    mouse_.right = true;
    if (hwnd_) SetCapture(hwnd_);
    if (onRDown_) onRDown_(x, y);       // -> Input_OnRButtonDown 0x50ADB0
}

void InputSystem::OnRButtonUp(int x, int y) {
    mouse_.right = false;
    ReleaseCapture();
    if (onRUp_) onRUp_(x, y);           // -> Input_OnRButtonUp 0x50AE40
}

void InputSystem::OnMButtonDown(int x, int y) {
    if (onMDown_) onMDown_(x, y);       // -> Camera_ResetView 0x50AED0
}

void InputSystem::OnMouseWheel(int delta) {
    mouse_.wheel += delta;
    if (onWheel_) onWheel_(delta);      // -> Camera_MouseWheelZoom 0x50B460
}

void InputSystem::GetMouseDelta(int& dx, int& dy) {
    dx = mouse_.dx;
    dy = mouse_.dy;
    mouse_.dx = 0;
    mouse_.dy = 0;
}

int InputSystem::ConsumeWheel() {
    int w = mouse_.wheel;
    mouse_.wheel = 0;
    return w;
}

} // namespace ts2::input

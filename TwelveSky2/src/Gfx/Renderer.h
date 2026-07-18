// Gfx/Renderer.h — Direct3D9 device of the GXD engine.
// Faithful to Gfx_InitDevice 0x69B9B0 / GXD_DeviceReinit 0x4023F0 / GXD_BeginScene 0x404640
// / Gfx_Present 0x69E270 (see Docs/TS2_GXD_ENGINE.md).
// ex-VeryOldClient: Core/GXD (v1 / Object A = g_GfxRenderer 0x7FFE18) — the CREATOR of the
//   physical device (Direct3DCreate9 + CreateDevice), one of the two homonymous GXD classes;
//   the other (v2 TW2AddIn::GXD = Object B 0x18C4EF8) REUSES this device, cf. Gfx/GxdRenderer.h.
//   CONFIRMED Docs/TS2_GXD_ROSETTA.md §1.1/§3.
// Uses ONLY the Windows SDK's Direct3D9 (no legacy D3DX): math goes through
// DirectXMath, shaders through d3dcompiler, sprite/font will be reimplemented.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>

namespace ts2::gfx {

class Renderer {
public:
    ~Renderer();

    // Creates the device (D3DDEVTYPE_HAL, HW vertex processing + SOFTWARE fallback).
    bool Init(HWND hwnd, int width, int height, bool windowed);
    void Shutdown();

    bool BeginFrame(); // Clear(TARGET|ZBUFFER) + BeginScene (Gfx_BeginFrame)
    void EndFrame();   // EndScene + Present (Gfx_Present)

    // --- Device lost/reset observers (Gfx_HandleDeviceLostReset 0x69DD40) ---
    // Observer signature; `user` is the opaque pointer supplied to SetDeviceCallbacks.
    using DeviceNotifyFn = void (*)(void* user);

    // Registers the observers called AROUND IDirect3DDevice9::Reset.
    // Anchor: Gfx_HandleDeviceLostReset 0x69DD40. In the binary, the renderer (Object A =
    // g_GfxRenderer 0x7FFE18) directly OWNS the ID3DXEffect (+620), ID3DXFont (+612)
    // and ID3DXSprite (+608), and itself calls their OnLostDevice (@0x69DE3E) then, after
    // the Reset (@0x69DE55), their OnResetDevice (@0x69DE9B). On the ClientSource side these
    // D3DX objects belong to higher layers (SceneManager -> LoginScene/GameHud/GameWindows ->
    // Font/SpriteBatch): the transposition therefore goes through an observer.
    // WITHOUT registration, Reset() is called without notifying the ID3DXSprite/ID3DXFont and
    // restoring after a device loss (Alt+Tab, resolution change) is IMPOSSIBLE.
    //
    // WIRING REQUIRED (outside this front — to be placed by the orchestrator in App.cpp): this
    // hook is INERT until someone registers it (onLost_/onReset_ stay nullptr => the
    // OnLostDevice/OnResetDevice chain never runs = dead code). The attach point is
    // ts2::App::Init, RIGHT AFTER the GxdRenderer::DeviceReinit block succeeds (App.cpp:379,
    // after the closing brace on line 379), onto the ts2::SceneManager::OnDeviceLost/
    // OnDeviceReset chain (SceneManager.h:98-99, public):
    //     renderer_.SetDeviceCallbacks(
    //         [](void* u){ static_cast<SceneManager*>(u)->OnDeviceLost();  },
    //         [](void* u){ static_cast<SceneManager*>(u)->OnDeviceReset(); },
    //         &scene_);
    // (capture-less lambdas -> convert to DeviceNotifyFn; `scene_` = member App.h:42.)
    void SetDeviceCallbacks(DeviceNotifyFn onLost, DeviceNotifyFn onReset, void* user);

    IDirect3D9*      D3D() const { return d3d_; }
    IDirect3DDevice9* Device() const { return device_; }
    bool Ready() const { return device_ != nullptr; }
    void SetClearColor(uint32_t argb) { clearColor_ = argb; }

private:
    // Gfx_HandleDeviceLostReset 0x69DD40 — called UNCONDITIONALLY at the top of every frame
    // (the binary calls it at the top of EVERY Scene_*Render: 0x518894, 0x519274, 0x51B044,
    // 0x51CEF4, 0x52C284, 0x52D10D/0x52D15A/0x52D1C5 — 8 xrefs).
    // RETURN CONTRACT (counter-intuitive, faithful to the binary):
    //   true  <=> TestCooperativeLevel returned D3D_OK (@0x69DD79, the only `return 1`) -> render.
    //   false in ALL other cases, INCLUDING after a SUCCESSFUL Reset() (LABEL_42 @0x69E251:
    //         *a5=0 then `return 0`): the reset frame is SKIPPED, rendering resumes next frame.
    bool HandleDeviceLost();

    // Sets the sampler states (stages 0/1) + initial render states EXACTLY as Object A does,
    // taken directly from Gfx_InitDevice 0x69c470..0x69c543 (fixed LINEAR, NOT anisotropic;
    // the old GXD_ConfigSamplerStates 0x403B50 anchor belonged to Object B / GxdRenderer).
    // Sampler/render states don't survive Reset(): re-invoked after CreateDevice AND after
    // every successful Reset() (HandleDeviceLost).
    void ApplyInitialDeviceStates();

    IDirect3D9*           d3d_        = nullptr; // pD3D9 @+240 (0x7FFF08)   ex-VeryOldClient: mDirect3D
    IDirect3DDevice9*     device_     = nullptr; // pDevice @+604 (0x800074) ex-VeryOldClient: mGraphicDevice
    D3DPRESENT_PARAMETERS pp_         = {};      // ex-VeryOldClient: mGraphicPresentParameters (PLAUSIBLE)
    uint32_t              clearColor_ = 0x00000000; // pure black (ARGB), faithful to the original clear

    // Loss/restore observers (cf. SetDeviceCallbacks). Not owned.
    // Stand in for the direct OnLostDevice/OnResetDevice calls on Effect(+620)/
    // Font(+612)/Sprite(+608) made by Gfx_HandleDeviceLostReset 0x69DD40.
    DeviceNotifyFn        onLost_     = nullptr;
    DeviceNotifyFn        onReset_    = nullptr;
    void*                 notifyUser_ = nullptr;

    // NB: there is NO MORE `deviceLost_` flag. The binary stores NO loss state at all:
    // Gfx_Present 0x69E270 calls EndScene (vtbl+168) then Present (vtbl+68) and NEVER
    // inspects Present's HRESULT; detection goes exclusively through TestCooperativeLevel
    // (vtbl+12 @0x69DD4C) done at the top of every frame. The former flag (set by EndFrame
    // on D3DERR_DEVICELOST, read by BeginFrame) was a local invention that, on top of that,
    // prevented any recovery if the loss occurred anywhere other than at Present.
};

} // namespace ts2::gfx

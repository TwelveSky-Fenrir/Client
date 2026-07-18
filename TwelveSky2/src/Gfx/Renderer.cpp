// Gfx/Renderer.cpp — Direct3D9 device.
#include "Gfx/Renderer.h"
// GXD_ConfigSamplerStates 0x403B50 operates on Object B (g_GxdRenderer 0x18C4EF8, via its
// pDevice field @+524) but is called PER FRAME from the Scene_*Render functions, right after
// Gfx_BeginFrame 0x6A2280 (Object A) — hence this BeginFrame dependency on the GxdRenderer singleton.
// No include cycle: Gfx/GxdRenderer.h does not include Gfx/Renderer.h.
#include "Gfx/GxdRenderer.h"
#include "Core/Log.h"

#pragma comment(lib, "d3d9.lib")

namespace ts2::gfx {

Renderer::~Renderer() { Shutdown(); }

bool Renderer::Init(HWND hwnd, int width, int height, bool windowed) {
    // Gfx_InitDevice 0x69B9B0 — creator of the physical device (Object A = g_GfxRenderer 0x7FFE18).
    // The original NEVER consults GetAdapterDisplayMode: the back-buffer is fixed to X8R8G8B8
    // and the fullscreen video mode is imposed by ChangeDisplaySettingsA (device stays Windowed).

    // Fullscreen: ChangeDisplaySettingsA(CDS_FULLSCREEN) BEFORE CreateDevice.
    // The original tests `if (fullscreen requested)`; here `windowed` = the inverse of that flag (a2, this+27).
    if (!windowed) {                             // a2 = fullscreen flag
        DEVMODEA dm;                             // Gfx_InitDevice 0x69bb20
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize        = 156;                  // sizeof(DEVMODEA) fige a 156
        dm.dmBitsPerPel  = 32;
        dm.dmPelsWidth   = (DWORD)width;         // a5
        dm.dmPelsHeight  = (DWORD)height;        // a6
        dm.dmFields      = 0x1C0000;             // DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT
        // Gfx_InitDevice 0x69bb20: ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN=4).
        if (ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL) {
            TS2_ERR("ChangeDisplaySettingsA a echoue (code fidelite 3)"); // *a11=3 @0x69bb2e
            return false;
        }
    }

    // Direct3DCreate9(0x20) — Gfx_InitDevice 0x69bb3b (failure => code 4).
    d3d_ = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d_) { TS2_ERR("Direct3DCreate9 a echoue (code fidelite 4)"); return false; }

    // GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps) — Gfx_InitDevice 0x69bb6f (failure => code 5).
    // Reproduces the original ordering (caps queried before CheckDeviceFormat).
    D3DCAPS9 caps{};                             // Gfx_InitDevice 0x69bb6f
    if (FAILED(d3d_->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps))) {
        TS2_ERR("GetDeviceCaps HAL a echoue (code fidelite 5)");         // *a11=5 @0x69bb75
        Shutdown(); return false;
    }

    // CheckDeviceFormat(0, HAL, X8R8G8B8, 0, D3DRTYPE_TEXTURE, DXTn) x3 BEFORE CreateDevice.
    // FourCC proven: 0x31545844="DXT1" (0x69bb88), 0x33545844="DXT3" (0x69bba8),
    // 0x35545844="DXT5" (0x69bbc8). A failure branches to loc_69C966 => code 6.
    if (FAILED(d3d_->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                  D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT1)) ||   // 0x69bb95
        FAILED(d3d_->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                  D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT3)) ||   // 0x69bbb5
        FAILED(d3d_->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                  D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT5))) {   // 0x69bbd5
        TS2_ERR("CheckDeviceFormat DXTn non supporte (code fidelite 6)"); // *a11=6 @0x69c96a
        Shutdown(); return false;
    }

    // Present params — Gfx_InitDevice memset(this+137,0,0x38) 0x69bbef then fixed fields.
    ZeroMemory(&pp_, sizeof(pp_));                          // 0x69bbef
    pp_.BackBufferWidth        = width;                     // 0x69bc28 (*(this+137))
    pp_.BackBufferHeight       = height;                    // 0x69bc31 (*(this+138))
    pp_.BackBufferFormat       = D3DFMT_X8R8G8B8;           // 0x69bc37 (=22, INCONDITIONNEL)
    pp_.BackBufferCount        = 1;                         // 0x69bc03 (esi=1)
    pp_.MultiSampleType        = D3DMULTISAMPLE_NONE;       // 0x69bc41 (ebx=0)
    pp_.MultiSampleQuality     = 0;                         // 0x69bc47 (ebx=0)
    pp_.SwapEffect             = D3DSWAPEFFECT_DISCARD;     // 0x69bc09 (esi=1)
    pp_.hDeviceWindow          = hwnd;                      // 0x69bc4d (edi=hwnd)
    pp_.Windowed               = TRUE;                      // 0x69bc0f (esi=1, INCONDITIONNEL)
    pp_.EnableAutoDepthStencil = TRUE;                      // 0x69bc15 (esi=1)
    pp_.AutoDepthStencilFormat = D3DFMT_D24S8;              // 0x69bc53 (=0x4B=75)
    pp_.Flags                  = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL; // 0x69bc5d (=2)
    pp_.FullScreen_RefreshRateInHz = 0;                     // 0x69bc67 (ebx=0)
    pp_.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE;       // 0x69bc6d (=0x80000000)

    // CreateDevice — Gfx_InitDevice 0x69bc9d. BehaviorFlags HW=68 = HARDWARE_VERTEXPROCESSING(0x40)
    // | MULTITHREADED(0x4). The original passes hwnd as both hFocusWindow AND pp_.hDeviceWindow.
    DWORD hwFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;
    HRESULT hr = d3d_->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, hwFlags, &pp_, &device_);
    if (FAILED(hr)) {
        // Fallback to SOFTWARE_VERTEXPROCESSING(0x20) | MULTITHREADED (= 36), like the client.
        TS2_WARN("CreateDevice HW echoue (0x%08lX), fallback SOFTWARE", hr);
        DWORD swFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;
        hr = d3d_->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, swFlags, &pp_, &device_);
    }
    if (FAILED(hr)) { TS2_ERR("CreateDevice a echoue (0x%08lX, code fidelite 7)", hr); Shutdown(); return false; }

    // Initial sampler/render states EXACTLY as Object A sets them (Gfx_InitDevice 0x69c470..0x69c543).
    ApplyInitialDeviceStates();

    TS2_LOG("Device D3D9 cree : %dx%d (%s)", width, height, windowed ? "fenetre" : "plein-ecran");
    return true;
}

// Gfx_InitDevice 0x69c470..0x69c543: sampler states (stages 0/1 ONLY) + initial render states
// of Object A. FIXED LINEAR filters — NOT anisotropic, NOT dependent on caps.
// (The old anisotropic code based on GXD_ConfigSamplerStates 0x403B50 belonged to Object B /
//  GxdRenderer: wrong anchor for Object A, replaced here.)
// SetSamplerState = vtbl+0x114 (276), SetRenderState = vtbl+0xE4 (228). ebx=0, edi=1 confirmed
// in the disassembly. Sampler AND render states do NOT survive Reset(): re-invoked after every
// successful Reset() (HandleDeviceLost).
void Renderer::ApplyInitialDeviceStates() {
    if (!device_) return;
    IDirect3DDevice9* d = device_;

    // --- Stage 0: MAG/MIN=LINEAR, MIP=POINT, ADDRESS U/V=WRAP ---
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);   // 0x69c470 (type5, val 2)
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);   // 0x69c480 (type6, val 2)
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);    // 0x69c48f (type7, val edi=1)
    d->SetSamplerState(0, D3DSAMP_ADDRESSU,  D3DTADDRESS_WRAP); // 0x69c49d (type edi=1, val 1)
    d->SetSamplerState(0, D3DSAMP_ADDRESSV,  D3DTADDRESS_WRAP); // 0x69c4ac (type2, val edi=1)
    // --- Stage 1: MAG/MIN=LINEAR, MIP=NONE, ADDRESS U/V=CLAMP ---
    d->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);   // 0x69c4bc (type5, val 2)
    d->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);   // 0x69c4cc (type6, val 2)
    d->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);     // 0x69c4db (type7, val ebx=0)
    d->SetSamplerState(1, D3DSAMP_ADDRESSU,  D3DTADDRESS_CLAMP);// 0x69c4ea (type edi=1, val 3)
    d->SetSamplerState(1, D3DSAMP_ADDRESSV,  D3DTADDRESS_CLAMP);// 0x69c4fa (type2, val 3)

    // --- Initial render states (Gfx_InitDevice 0x69c508..0x69c543) ---
    d->SetRenderState(D3DRS_ALPHAREF,     0);                    // 0x69c508 (state 24, val 0)
    d->SetRenderState(D3DRS_ALPHAFUNC,    D3DCMP_GREATER);       // 0x69c517 (state 25, val 5=GREATER, NOT GREATEREQUAL)
    d->SetRenderState(D3DRS_SRCBLEND,     D3DBLEND_SRCALPHA);    // 0x69c526 (state 19, val 5)
    d->SetRenderState(D3DRS_DESTBLEND,    D3DBLEND_INVSRCALPHA); // 0x69c535 (state 20, val 6)
    d->SetRenderState(D3DRS_DITHERENABLE, TRUE);                 // 0x69c543 (state 26, val edi=1)

    // Do NOT touch stage 2 or MAXANISOTROPY: Object A does not configure them.
    // The 3 writes *(this+331..333)={2,0,1} @0x69c457/61/67 are internal renderer cache
    // fields (not device calls); not modeled here (no consumer).
    // The fog block @0x69c3e4 (FOGENABLE/FOGCOLOR/FOGTABLEMODE=3/FOGSTART/FOGEND/RANGEFOGENABLE)
    // is gated by a9 (this+35, fog param absent from the Init signature) — out of scope here.
}

void Renderer::Shutdown() {
    if (device_) { device_->Release(); device_ = nullptr; }
    if (d3d_)    { d3d_->Release();    d3d_    = nullptr; }
}

void Renderer::SetDeviceCallbacks(DeviceNotifyFn onLost, DeviceNotifyFn onReset, void* user) {
    // Registers the observers standing in for the OnLostDevice/OnResetDevice calls that
    // Gfx_HandleDeviceLostReset 0x69DD40 issues directly on its owned D3DX objects
    // (Effect +620 / Font +612 / Sprite +608 of g_GfxRenderer 0x7FFE18).
    onLost_     = onLost;
    onReset_    = onReset;
    notifyUser_ = user;
}

// OBJECT B DEVICE-LOST PAIR (gap G5, wired in Passe 4 / W9).
//
// GxdRenderer::RenderPostBlur (GXD_RenderPostBlur 0x4053E0) creates 4 half-resolution render
// targets in D3DUSAGE_RENDERTARGET + D3DPOOL_DEFAULT (+540/+544/+548/+552); without the pair
// below, Reset() would fail while these 4 objects are alive (D3D9 rule) and the device would
// never recover after Alt+Tab. The VB/IB/textures in Gfx/MeshRenderer.cpp,
// Gfx/WorldGeometryRenderer.cpp and Gfx/GpuTexture.cpp stay D3DPOOL_MANAGED and survive
// Reset() on their own.
//
// Original protocol, proven by Scene_IntroRender 0x518880 (identical in the 5 other scenes):
// `result = Gfx_HandleDeviceLostReset(g_GfxRenderer, ..., &v9)` @0x518894;
//   if result == 0 && v9 == 1 -> GXD_OnDeviceLost(&g_GxdRenderer)   @0x5188A8, return immediately
//   if result != 0            -> GXD_RestoreAfterReset(&g_GxdRenderer) @0x5188BC then BeginFrame
// Transposition: Object A (this Renderer) OWNS the Reset, so the Reset() call is directly
// bracketed by Object B's two methods, placing OnDeviceLost before Reset and RestoreAfterReset
// after a successful Reset — same relative order as the original (which calls them from the
// scenes instead, since Gfx_HandleDeviceLostReset 0x69DD40 has no reference to g_GxdRenderer;
// the observable result is the same).
//
// The 12 shaders + skinned vertex declaration that GXD_RestoreAfterReset 0x404570 recompiles
// under the same flag belong to ts2::gfx::ShaderSet, not GxdRenderer: their restoration goes
// through the onReset_ observer (cf. SetDeviceCallbacks / Renderer.h).
bool Renderer::HandleDeviceLost() {
    // --- Gfx_HandleDeviceLostReset 0x69DD40, sequence re-read instruction by instruction ---
    // TestCooperativeLevel = vtbl+12 @0x69DD4C (device @+604 = 0x800074).
    HRESULT hr = device_->TestCooperativeLevel();

    // 0x88760868 D3DERR_DEVICELOST @0x69DD54: *a5=0, `return 0` -> not restorable yet.
    if (hr == D3DERR_DEVICELOST) return false;

    // 0x88760869 D3DERR_DEVICENOTRESET @0x69DD5F: the device can be reinitialized.
    if (hr == D3DERR_DEVICENOTRESET) {
        // The binary first Release()s its 4 glow textures/surfaces, D3DPOOL_DEFAULT
        // (+632/+624/+636/+628, Release = vtbl+8 @0x69DD95..0x69DDF4, each reset to 0).
        // ClientSource's D3DPOOL_MANAGED resources (Gfx/MeshRenderer.cpp,
        // Gfx/WorldGeometryRenderer.cpp, Gfx/GpuTexture.cpp, UIManager::CreateWhiteTexture)
        // are the only ones that survive Reset() on their own. The glow post-process
        // (+1432, "FILTER" effect) is not ported.

        // GXD_OnDeviceLost 0x4042E0 (Object B) — BEFORE the Reset, counterpart of @0x5188A8.
        // Releases the 4 bloom D3DPOOL_DEFAULT render targets (+540/+544/+548/+552) created
        // by GxdRenderer::RenderPostBlur and sets flag +527548. WITHOUT THIS CALL, the
        // Reset() below would fail (D3D9 refuses a Reset while any D3DPOOL_DEFAULT resource
        // is alive). Safe no-op if the bloom has never run.
        GxdRenderer::Instance().OnDeviceLost();

        // OnLostDevice BEFORE Reset — order PROVEN @0x69DE3E, short-circuited by `&&`:
        //   Effect(+620) vtbl+276 -> Font(+612) vtbl+64 -> Sprite(+608) vtbl+48.
        // A failure (<0) skips the Reset and sets *a5=1 @0x69DE14. Transposition: the observer
        // (SceneManager::OnDeviceLost -> Font::OnDeviceLost -> ID3DXFont/ID3DXSprite::
        // OnLostDevice) returns void and cannot fail; the intra-object order is set by
        // the owner of the D3DX objects, not here.
        if (onLost_) onLost_(notifyUser_);

        // Reset(pp) = vtbl+64 @0x69DE55, D3DPRESENT_PARAMETERS @+548. Failure -> *a5=1, `return 0`.
        hr = device_->Reset(&pp_);
        if (FAILED(hr)) return false;

        // OnResetDevice AFTER Reset — REVERSE order, PROVEN @0x69DE9B:
        //   Sprite(+608) vtbl+52 -> Font(+612) vtbl+68 -> Effect(+620) vtbl+280.
        if (onReset_) onReset_(notifyUser_);

        // GXD_RestoreAfterReset 0x404570 (Object B) — AFTER a SUCCESSFUL Reset, counterpart
        // of @0x5188BC. Consumes the flag +527548 set above. The 4 bloom RTs are NOT
        // recreated here: RenderPostBlur lazily recreates them next frame
        // (`if (!*(a1+540))` @0x40547A), exactly like the original.
        GxdRenderer::Instance().RestoreAfterReset();

        // Sampler AND render states do NOT survive Reset() in D3D9: the binary resets them
        // all in one go right after (SetViewport/SetTransform/SetMaterial/SetLight/
        // SetRenderState/SetSamplerState, 0x69DF3A..0x69E1AC). We reapply here the modeled
        // subset of Object A (Gfx_InitDevice 0x69C470..0x69C543); Object B's sampler states
        // are reapplied every frame anyway by ConfigSamplerStates (cf. BeginFrame).
        ApplyInitialDeviceStates();

        // LABEL_42 @0x69E251: *a5=0 then `return 0`. After a SUCCESSFUL Reset the binary
        // returns 0 -> the caller does NOT RENDER this frame (no Gfx_BeginFrame, no Gfx_Present).
        // Rendering resumes next frame, once TestCooperativeLevel returns D3D_OK.
        return false;

        // NOT PORTED from GXD_RestoreAfterReset 0x404570 (flag +527548 and the 4 bloom RTs are
        // now handled above by GxdRenderer::RestoreAfterReset): the original also recompiles
        // the 12 shaders, recreates the 76-byte skinned vertex declaration
        // (g_GxdSkinnedVertexDecl76 0x814A58 -> +526880 @0x40461F) and, if a map is loaded
        // (+6352), calls World_ReloadMap 0x411B60 @0x40458F. These three families don't belong
        // to Renderer (a low-level D3D9 wrapper with no reference to ts2::gfx::ShaderSet or
        // World/WorldMap). IDirect3DVertexShader9 / IDirect3DPixelShader9 /
        // IDirect3DVertexDeclaration9 are technically NOT D3DPOOL_DEFAULT and survive Reset():
        // this reload is defensive rather than necessary in the original. Their owners must
        // go through the onReset_ observer above.
    }

    // Any other NON-ZERO HRESULT @0x69DD69 (e.g. D3DERR_DRIVERINTERNALERROR): *a5=1, `return 0`.
    // D3D_OK @0x69DD79: *a5=0, `return 1` — the ONLY path that allows rendering the frame.
    // Test on `!= 0` and NOT on FAILED(): the binary does `if (v6)` @0x69DD63 (test/jnz on the
    // raw value), so a non-zero success HRESULT would also be rejected.
    return hr == D3D_OK;
}

bool Renderer::BeginFrame() {
    if (!device_) return false;

    // Gfx_HandleDeviceLostReset 0x69DD40 — UNCONDITIONAL every frame, like at the top of
    // every Scene_*Render (0x518894, 0x51B044, ...). The old `deviceLost_ &&` guard was
    // a local invention: the binary has no flag and polls TestCooperativeLevel every
    // frame (cf. Renderer.h). false -> frame skipped (device lost OR just reset).
    if (!HandleDeviceLost()) return false;

    // Gfx_BeginFrame 0x6A2280: Clear(vtbl+172) then BeginScene (vtbl+164 @0x6A24CC).
    device_->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                   clearColor_, 1.0f, 0);
    if (FAILED(device_->BeginScene())) return false;

    // GXD_ConfigSamplerStates 0x403B50 (Object B) — called PER FRAME, AFTER Gfx_BeginFrame
    // (i.e. after Clear+BeginScene) and BEFORE any draw pass. Position proven by the
    // 6 call sites in the Scene_*Render functions: 0x518916, 0x5192F6, 0x51B0C6, 0x51CF76,
    // 0x52C306, 0x52D24A (the 7th site is GXD_BeginScene 0x404640 @0x4047C7, not ported).
    // It's this PER-FRAME call that makes anisotropic filtering effective: it deliberately
    // OVERWRITES the LINEAR filters set once at init by Object A
    // (Gfx_InitDevice 0x69C470, cf. ApplyInitialDeviceStates) — both coexist in the
    // binary, and the net runtime result is ANISOTROPIC. Safe no-op as long as
    // GxdRenderer::DeviceReinit hasn't run yet (guard `if (!device_) return;`), e.g. in self-tests.
    GxdRenderer::Instance().ConfigSamplerStates();
    return true;
}

void Renderer::EndFrame() {
    // Gfx_Present 0x69E270: EndScene (vtbl+168 @0x69E27C) then
    // Present(0,0,0,0) (vtbl+68 @0x69E296). Present's HRESULT is NEVER inspected by
    // the binary (it's simply returned): device-loss detection goes exclusively through
    // the TestCooperativeLevel at the top of the frame. So no flag is set here either —
    // what the old code did (`deviceLost_ = true`) was an invention.
    if (!device_) return;
    device_->EndScene();
    device_->Present(nullptr, nullptr, nullptr, nullptr);
}

} // namespace ts2::gfx

// Gfx/GxdRenderer.cpp — implementation of the GXD renderer singleton.
// Faithfully rebuilds the D3D9 states set by the original renderer (0x18C4EF8).
#include "Gfx/GxdRenderer.h"
#include "Gfx/ShaderSet.h" // npk PS12/PS14 slots for RenderPostBlur — read-only (W9)
#include <cmath>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

GxdRenderer& GxdRenderer::Instance() {
    static GxdRenderer s_instance; // equivalent of the global g_GxdRenderer @ 0x18C4EF8
                                   // ex-VeryOldClient: TW2AddIn::GXD (v2 / Object B)
    return s_instance;
}

void GxdRenderer::Shutdown() {
    // Releases the 4 bloom render targets (D3DPOOL_DEFAULT): without this, Shutdown would
    // leak 4 COM objects. Same order as GXD_OnDeviceLost (+548, +540, +552, +544).
    ReleaseBlurTargets();

    d3d_ = nullptr;
    device_ = nullptr;
    width_ = 0;
    height_ = 0;
    fovDeg_ = 45.0f;
    nearZ_ = 0.0f;
    farZ_ = 0.0f;
    currentShaderId_ = 0;
    useLinearFilter_ = false;
    // depthBiasCapable_ / twoSidedStencil_: NOT reset to false (gap G3, W9).
    // GXD_FreeGlobalState 0x401530 (fully decompiled) touches NEITHER +28 NOR +32; these
    // fields are locked to 1 by GXD_InitGlobalState @0x4013B2/@0x4013B8 and only ever
    // LOCKED (never cleared) by GXD_DeviceReinit @0x402928/@0x402939.
    // Cf. the note in GxdRenderer.h above their declaration.
}

// Matrix construction (part of GXD_DeviceReinit 0x4023F0).
void GxdRenderer::BuildMatrices() {
    // Full-screen viewport, depth 0..1. (+560)
    viewport_.X      = 0;
    viewport_.Y      = 0;
    viewport_.Width  = static_cast<DWORD>(width_);
    viewport_.Height = static_cast<DWORD>(height_);
    viewport_.MinZ   = 0.0f;
    viewport_.MaxZ   = 1.0f;

    // "Half-viewport" matrix: NDC [-1,1] -> pixels [0,w]x[0,h], Y flipped. (+584)
    //   [ w/2    0    0   0 ]
    //   [  0  -h/2    0   0 ]
    //   [  0    0    1   0 ]
    //   [ w/2  h/2    0   1 ]
    const float hw = static_cast<float>(width_)  * 0.5f;
    const float hh = static_cast<float>(height_) * 0.5f;
    D3DXMatrixIdentity(&matHalfViewport_);
    matHalfViewport_._11 = hw;
    matHalfViewport_._22 = -hh;
    matHalfViewport_._33 = 1.0f;
    matHalfViewport_._41 = hw;
    matHalfViewport_._42 = hh;

    // Left-handed perspective projection: FOV 45deg (PI/4 rad, cf. 0.78539819), aspect = w/h. (+648)
    D3DXMatrixPerspectiveFovLH(&matProj_, D3DX_PI / 4.0f,
                               static_cast<float>(width_) / static_cast<float>(height_),
                               nearZ_, farZ_);

    // LookAtLH view: eye (0,0,-10), target (0,0,0), up (0,1,0). (+712/+724 -> +748)
    eye_ = D3DXVECTOR3(0.0f, 0.0f, -10.0f);
    at_  = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    D3DXVECTOR3 up(0.0f, 1.0f, 0.0f);
    D3DXMatrixLookAtLH(&matView_, &eye_, &at_, &up);

    // World = identity. (+988)
    D3DXMatrixIdentity(&matWorld_);
}

// Default material + directional light (part of GXD_DeviceReinit).
void GxdRenderer::BuildDefaultMaterialAndLight() {
    // ex-VeryOldClient: mMaterial (+1052) + mLight (+1120). v1/v2 guard: the light is
    // Amb 0.3 / Diff 0.7 (v2 / Object B, PROVEN BIT-EXACT at 0x402711), NOT 0.4/0.5 (v1 / Object A).
    // Material: opaque white diffuse/ambient, black specular/emissive, power 0. (+1052)
    ZeroMemory(&material_, sizeof(material_));
    material_.Diffuse  = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
    material_.Ambient  = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
    material_.Specular = D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f);
    material_.Emissive = D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f);
    material_.Power    = 0.0f;

    // Default directional light. (+1120)
    ZeroMemory(&light_, sizeof(light_));
    light_.Type      = D3DLIGHT_DIRECTIONAL;                // 3
    light_.Diffuse   = D3DXCOLOR(0.7f, 0.7f, 0.7f, 1.0f);
    light_.Specular  = D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f);
    light_.Ambient   = D3DXCOLOR(0.3f, 0.3f, 0.3f, 1.0f);
    light_.Position  = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    light_.Direction = D3DXVECTOR3(-1.0f, -1.0f, 1.0f);
    // The base light is normalized (sub_6BB60C / Vec3_Normalize).
    D3DXVECTOR3 dir(light_.Direction.x, light_.Direction.y, light_.Direction.z);
    D3DXVec3Normalize(&dir, &dir);
    light_.Direction = dir;
    // Range/Falloff/Attenuation/Theta/Phi stay at 0.
}

// GXD_DeviceReinit 0x4023F0
// ex-VeryOldClient: TW2AddIn::GXD::InitForAddIn(sx,sy,near,far,d3d,dev,res) (GXDCore.cpp:523)
//   — reuses_device=true: receives an IDirect3D9*+IDirect3DDevice9* ALREADY created by Object A.
bool GxdRenderer::DeviceReinit(IDirect3D9* d3d, IDirect3DDevice9* device,
                               int width, int height, float nearZ, float farZ,
                               int* outError) {
    if (outError) *outError = 0;
    d3d_      = d3d;
    device_   = device;
    width_    = width;
    height_   = height;
    fovDeg_   = 45.0f;
    nearZ_    = nearZ;
    farZ_     = farZ;

    if (!d3d_ || !device_) { if (outError) *outError = 1; return false; }

    // GetDeviceCaps -> fills the embedded D3DCAPS9 (+164); MaxAnisotropy = g_GxdMaxAnisotropy (+272).
    if (FAILED(device_->GetDeviceCaps(&caps_))) { if (outError) *outError = 2; return false; }

    // Requires vs_2_0 (>= 0xFFFE0200) and ps_2_0 (>= 0xFFFF0200).
    if (caps_.VertexShaderVersion < 0xFFFE0200u) { if (outError) *outError = 3; return false; }
    if (caps_.PixelShaderVersion  < 0xFFFF0200u) { if (outError) *outError = 4; return false; }

    // GXD_DeviceReinit 0x4023F0 @0x402928/@0x402939 — LOCK TO 1, NOT AN ASSIGNMENT
    // (gap G3, fixed in Passe 4 / W9). Disassembly re-read, edi = 1:
    //     test dword ptr [esi+0C8h], 4000000h    ; caps+36  = RasterCaps
    //     jz   short loc_40292D                  ; @0x402928  <- NO else
    //     mov  [esi+1Ch], edi                    ; @0x40292A  this+28 = 1
    //     test dword ptr [esi+12Ch], 100h        ; caps+136 = StencilCaps
    //     jz   short loc_40293C                  ; @0x402937  <- NO else
    //     mov  [esi+20h], edi                    ; @0x402939  this+32 = 1
    // Combined with the init-to-1 of GXD_InitGlobalState (@0x4013B2/@0x4013B8), these fields
    // are ALWAYS 1 regardless of GPU caps. The old code (`= (caps & bit) != 0`)
    // produced `false` where the binary guarantees 1 on a GPU lacking the cap.
    // Cf. the note in GxdRenderer.h (dual identity +28 = g_ShadowsEnabled / +32 = g_ShadowMethod).
    if ((caps_.RasterCaps  & D3DPRASTERCAPS_DEPTHBIAS) != 0) depthBiasCapable_ = true; // (+28)
    if ((caps_.StencilCaps & D3DSTENCILCAPS_TWOSIDED)  != 0) twoSidedStencil_  = true; // (+32)
    maxAnisotropy_   = caps_.MaxAnisotropy;

    // (+527548) = 0 @0x402451: a freshly (re)attached device is no longer "lost".
    deviceLost_ = false;

    BuildMatrices();
    BuildDefaultMaterialAndLight();
    return true;
}

// GXD_BeginScene 0x404640
//
// NO CALLER — AND THIS IS FAITHFUL, NOT A GAP (settled Passe 4 / W9, gap G4 refuted).
//
// `xrefs_to(0x404640)` = 0: GXD_BeginScene is DEAD IN THE BINARY TOO. A SetupFrame()
// with no caller is therefore the EXACT transposition, not a wiring gap to fill.
// The actual runtime path is App.cpp (frame loop) -> Renderer::BeginFrame() ->
// SceneManager::Render(), which reproduces Gfx_BeginFrame 0x6A2280 (Object A: Clear +
// BeginScene) and calls GXD_ConfigSamplerStates 0x403B50 at the right position — exactly
// like the 6 call sites of the Scene_*Render functions (the 7th, 0x4047C7, is here, in this
// dead function). Wiring up SetupFrame() would additionally produce a DUPLICATE Clear/BeginScene
// with Renderer::BeginFrame (nested BeginScene => D3DERR_INVALIDCALL).
//
// Do NOT "fix" the view recompute below: gap G4 previously called for its removal on the
// grounds that "the original doesn't redo a LookAt every frame". That's FALSE — GXD_BeginScene
// does it, line for line: v7 = *(a1+724) - *(a1+712) @0x404692 ; Vec3_Normalize(a1+736)
// @0x4046C0 ; up = (0,1,0) ; j_D3DXMatrixLookAtLH(a1+748, a1+712, a1+724, &up) @0x4046E3.
// What the gap had spotted (the Object A -> Object B qmemcpy's in the Scene_*Render functions,
// @0x5188DC/ED/F5) is a DIFFERENT, PARALLEL path that writes DISJOINT fields (+748/+988/+736..744,
// not +712/+724): both coexist. It's modeled by SetViewMatrix/SetWorldMatrix/
// SetViewDir (cf. GxdRenderer.h), which add to SetCamera without replacing it.
bool GxdRenderer::SetupFrame() {
    if (!device_) return false;
    IDirect3DDevice9* dev = device_;

    // 1) Clear target + z-buffer + stencil (Flags = 7), color 0, Z = 1.0.
    dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);

    // 2) View direction then LookAtLH view matrix (recomputed every frame). (+736/+748)
    viewDir_ = at_ - eye_;
    D3DXVec3Normalize(&viewDir_, &viewDir_);
    D3DXVECTOR3 up(0.0f, 1.0f, 0.0f);
    D3DXMatrixLookAtLH(&matView_, &eye_, &at_, &up);

    // 3) World reset to identity. (+988)
    D3DXMatrixIdentity(&matWorld_);

    // 4) Viewport + transforms + material + light.
    dev->SetViewport(&viewport_);
    dev->SetTransform(D3DTS_PROJECTION, &matProj_);   // state 3
    dev->SetTransform(D3DTS_VIEW,       &matView_);   // state 2
    dev->SetTransform(D3DTS_WORLD,      &matWorld_);  // state 256
    dev->SetMaterial(&material_);
    dev->LightEnable(0, TRUE);
    dev->SetLight(0, &light_);

    // 5) Sampler states + dithering.
    ConfigSamplerStates();
    dev->SetRenderState(D3DRS_DITHERENABLE, TRUE);     // state 26

    // 6) Frustum_BuildPlanes 0x406090 (culling). The inverse view matrices computed
    //    right after in GXD_BeginScene (+876/+888/+964/+976, billboard base/up vectors)
    //    are not reproduced here: they feed billboarded sprite/particle rendering,
    //    which is out of GxdRenderer's scope (cf. SpriteBatch/PtclDef on the GXD side).
    BuildFrustumPlanes();

    // 7) Open the scene then reset the pipeline to fixed-function.
    HRESULT hr = dev->BeginScene();
    currentShaderId_ = 0;            // (+526884)
    dev->SetVertexShader(nullptr);    // vtable 92
    dev->SetPixelShader(nullptr);     // vtable 107
    return SUCCEEDED(hr);
}

// GXD_ConfigSamplerStates 0x403B50
// ex-VeryOldClient: TW2AddIn::GXD::SetDefaultTextureSamplerState
//   (ANISOTROPIC vs LINEAR depending on mSamplerOptionValue = field +8 / useLinearFilter_).
//
// WIRING (gap GX-DEV-02): called PER FRAME from ts2::gfx::Renderer::BeginFrame(),
// right after Clear+BeginScene — position faithful to the 6 call sites of the Scene_*Render
// functions (0x518916, 0x5192F6, 0x51B0C6, 0x51CF76, 0x52C306, 0x52D24A), which all call
// GXD_ConfigSamplerStates(&g_GxdRenderer) right after Gfx_BeginFrame(g_GfxRenderer).
// It is REACHED by the actual runtime path App.cpp -> Renderer::BeginFrame().
// (Previously its only C++ caller was SetupFrame(), itself uncalled: anisotropic
//  filtering was therefore NEVER active and the device stayed on the LINEAR filters
//  set at init by Object A / Gfx_InitDevice 0x69C470.)
//
// The branch actually taken is ANISOTROPIC: field +8 (dword_18C4F00) has only one
// absolute writer in the binary, GXD_InitGlobalState 0x40139C, which sets it to 0 — the
// linear branch (+8 != 0) is dead in practice but reproduced here for fidelity.
//
// ACCEPTED DEVIATION (benign, identical final state): the binary orders the SetSamplerState
// calls per stage (with a shared stage-2 ADDRESSU/V tail @0x403E1B/0x403E34), here two
// loops s=0..2 are used instead. Each (stage, type) pair is written EXACTLY ONCE with the
// SAME value in both forms: the resulting device state is strictly identical.
void GxdRenderer::ConfigSamplerStates() {
    if (!device_) return;
    IDirect3DDevice9* dev = device_;

    if (useLinearFilter_) {
        // Branch "this[2] != 0": bilinear (LINEAR) filtering on stages 0..2.
        for (DWORD s = 0; s <= 2; ++s) {
            dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        }
    } else {
        // Branch "this[2] == 0" (default): anisotropic filtering + max factor.
        for (DWORD s = 0; s <= 2; ++s) {
            dev->SetSamplerState(s, D3DSAMP_MINFILTER,     D3DTEXF_ANISOTROPIC);
            dev->SetSamplerState(s, D3DSAMP_MAGFILTER,     D3DTEXF_ANISOTROPIC);
            dev->SetSamplerState(s, D3DSAMP_MIPFILTER,     D3DTEXF_ANISOTROPIC);
            dev->SetSamplerState(s, D3DSAMP_MAXANISOTROPY, maxAnisotropy_);
        }
    }

    // WRAP addressing (U,V) on the 3 stages (applied in both branches).
    for (DWORD s = 0; s <= 2; ++s) {
        dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }
}

// GXD_SetDirectionalLight 0x403980
void GxdRenderer::SetDirectionalLight(int mode, float ar, float ag, float ab, float aa) {
    if (!device_) return;
    IDirect3DDevice9* dev = device_;

    dev->LightEnable(0, TRUE); // enabled at function entry

    D3DLIGHT9 light;
    ZeroMemory(&light, sizeof(light));
    light.Type     = D3DLIGHT_DIRECTIONAL;              // 3
    light.Diffuse  = D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f);// contributes no diffuse
    light.Specular = D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f);

    if (mode == kLightAutoAmbient) {
        // Ambient = diffuse*0.5 + ambient of the base directional light.
        light.Ambient.r = light_.Diffuse.r * 0.5f + light_.Ambient.r;
        light.Ambient.g = light_.Diffuse.g * 0.5f + light_.Ambient.g;
        light.Ambient.b = light_.Diffuse.b * 0.5f + light_.Ambient.b;
        light.Ambient.a = 1.0f;
    } else if (mode == kLightUserAmbient) {
        light.Ambient = D3DXCOLOR(ar, ag, ab, aa);
    }
    // kLightKeepAmbient: ambient stays (0,0,0,0).

    light.Position  = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    light.Direction = D3DXVECTOR3(-1.0f, -1.0f, 1.0f); // not normalized (faithful to the original)

    dev->SetLight(0, &light);
}

// GXD_WorldToScreen 0x405C00
bool GxdRenderer::WorldToScreen(const D3DXVECTOR3& world, int& sx, int& sy) const {
    // Rejects points outside the frustum (Frustum_ContainsPoint5 0x406560).
    if (!FrustumContains(world)) return false;

    // Combines view * projection * half-viewport, then projects the point.
    D3DXMATRIX vp, vph;
    D3DXMatrixMultiply(&vp,  &matView_, &matProj_);          // sub_6BB618
    D3DXMatrixMultiply(&vph, &vp,        &matHalfViewport_);  // sub_6BB618
    D3DXVECTOR3 p;
    D3DXVec3TransformCoord(&p, &world, &vph);                  // sub_6BB612

    // Round to the nearest pixel: truncation toward 0 (Crt_ftol) + half-unit.
    // NB: the disassembly is symmetric in X and Y (the decompiler's "ftol(0.5)"
    //      artifact comes from a residual 0.5 left on the x87 stack from the X computation).
    int ix = static_cast<int>(p.x);
    if (p.x - static_cast<float>(ix) >= 0.5f) ++ix;
    int iy = static_cast<int>(p.y);
    if (p.y - static_cast<float>(iy) >= 0.5f) ++iy;
    sx = ix;
    sy = iy;
    return true;
}

// Camera_SetEyeTarget 0x403420 — pushes eye (+712) / target (+724) CONDITIONALLY.
// Decompilation re-read in full; (a2,a3,a4) = eye, (a5,a6,a7) = target:
//     if ( a5 == a2 && a6 == a3 && a7 == a4 ) return 0;          /*0x403465*/
//     v17 = a2-a5 ; v18 = a3-a6 ; v19 = a4-a7 ;                  /*0x403483..0x403491*/
//     v14 = fabs(v18) / Crt_sqrtf(v17*v17 + v18*v18 + v19*v19) ; /*0x4034a7..0x4034d6*/
//     Math_AsinFpu(v14) ; v15 = v14 * 57.2957763671875 ;         /*0x4034de..0x4034f1*/
//     if ( fabs(v15) > 89.989998 ) return 0;                     /*0x40350e*/
//     *(this+178..183) = a2..a7 ; return 1;                      /*0x403522..0x403574*/
// (this+178 = byte 712 = eye_ ; this+181 = byte 724 = at_.)
// The rejection is a HARD REFUSAL: the fields keep their previous value.
bool GxdRenderer::SetCamera(const D3DXVECTOR3& eye, const D3DXVECTOR3& at) {
    // 1) Eye coincides with target -> undefined view direction. (@0x403465)
    if (at.x == eye.x && at.y == eye.y && at.z == eye.z) return false;

    const float dx = eye.x - at.x;
    const float dy = eye.y - at.y;
    const float dz = eye.z - at.z;

    // 2) Elevation too close to the pole -> LookAtLH degenerates (up fixed at (0,1,0)). (@0x40350E)
    //    EXACT literals from the binary: 57.2957763671875 (0x42652EE1) and 89.989998.
    const float len = sqrtf(dx * dx + dy * dy + dz * dz);
    const float elevDeg = asinf(fabsf(dy) / len) * 57.2957763671875f;
    if (fabsf(elevDeg) > 89.989998f) return false;

    eye_ = eye; // (+712)
    at_  = at;  // (+724)
    return true;
}

// Bloom render targets — "lazy creation" part of GXD_RenderPostBlur
// 0x4053E0 (@0x40547A..0x405563).
//
// Fidelity details that matter:
//   - SIGNED INTEGER division w/2 and h/2 (`cdq/sub/sar 1` @0x405464-0x40546A): on an
//     odd width the original loses the remainder pixel, we do the same.
//   - j_D3DXCreateTexture(dev, halfW, halfH, MipLevels=1, Usage=1 D3DUSAGE_RENDERTARGET,
//     Format=22 D3DFMT_X8R8G8B8, Pool=0 D3DPOOL_DEFAULT, &tex)  @0x40548F / @0x4054F8
//   - GetSurfaceLevel(0, &surf) = vtbl+72                        @0x4054AC / @0x405536
//     (`lea edx, [esi+228h]` @0x405526 : 0x228 = 552 = surfB ; 0x224 = 548 = surfA)
//   - each test is INDEPENDENT (`if (!texA) {...}` then `if (texB) goto ...`): the two
//     pairs are created separately, not as a block.
bool GxdRenderer::EnsureBlurTargets() {
    if (!device_) return false;

    const int halfW = width_  / 2; // (+48)/2, signed integer division
    const int halfH = height_ / 2; // (+52)/2
    if (halfW <= 0 || halfH <= 0) return false;

    // --- Pair A (texA @+540 -> surfA @+548) ---
    if (!blurTexA_) {
        if (FAILED(D3DXCreateTexture(device_, static_cast<UINT>(halfW), static_cast<UINT>(halfH),
                                     1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8,
                                     D3DPOOL_DEFAULT, &blurTexA_))) {
            blurTexA_ = nullptr;
            return false; // `return result` @0x405496 : nothing has been taken yet
        }
        if (FAILED(blurTexA_->GetSurfaceLevel(0, &blurSurfA_))) {
            // @0x4054BA: Release(texA) + texA = 0, then return.
            blurSurfA_ = nullptr;
            blurTexA_->Release();
            blurTexA_ = nullptr;
            return false;
        }
    }

    // --- Pair B (texB @+544 -> surfB @+552) ---
    if (!blurTexB_) {
        if (FAILED(D3DXCreateTexture(device_, static_cast<UINT>(halfW), static_cast<UINT>(halfH),
                                     1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8,
                                     D3DPOOL_DEFAULT, &blurTexB_))) {
            // @0x405506: Release(surfA) + surfA = 0, Release(texA) + texA = 0.
            blurTexB_ = nullptr;
            ReleaseBlurTargets();
            return false;
        }
        if (FAILED(blurTexB_->GetSurfaceLevel(0, &blurSurfB_))) {
            // @0x405544: Release(surfA), surfA=0 ; Release(texA), texA=0 ; Release(texB), texB=0.
            blurSurfB_ = nullptr;
            ReleaseBlurTargets();
            return false;
        }
    }
    return true;
}

// GXD_OnDeviceLost 0x4042E0 @0x4042E3..0x404347: EXACT release order
// +548 (surfA), +540 (texA), +552 (surfB), +544 (texB), each reset to 0 after Release().
void GxdRenderer::ReleaseBlurTargets() {
    if (blurSurfA_) { blurSurfA_->Release(); blurSurfA_ = nullptr; } // (+548) @0x4042FD
    if (blurTexA_)  { blurTexA_->Release();  blurTexA_  = nullptr; } // (+540) @0x404315
    if (blurSurfB_) { blurSurfB_->Release(); blurSurfB_ = nullptr; } // (+552) @0x40432D
    if (blurTexB_)  { blurTexB_->Release();  blurTexB_  = nullptr; } // (+544) @0x404345
}

// GXD_OnDeviceLost 0x4042E0
//
// The original releases 24 D3DPOOL_DEFAULT objects: the 4 bloom RTs (+540/+544/+548/+552),
// the skinned vertex declaration (+526880) and 10 shader slot pairs
// (+526888..+527536), then `if (*(this+6352) == 1) World_Shutdown(this+6348)` @0x40454C.
// On the ClientSource side, GxdRenderer only owns the 4 RTs (cf. the RestoreAfterReset
// note in GxdRenderer.h for the split of the other three families).
void GxdRenderer::OnDeviceLost() {
    deviceLost_ = true; // (+527548) = 1 @0x4042EC — set BEFORE any release
    ReleaseBlurTargets();
}

// GXD_RestoreAfterReset 0x404570
//   if ( *(this+527548) == 1 ) {           /*0x40457a*/
//       ... World_ReloadMap / CreateVertexDeclaration / 12 shaders ...
//       if ( failure ) return 0;           /*0x4045b8*/
//       *(this+527548) = 0;                /*0x404629*/
//   }
//   return 1;                              /*0x4045b7*/
// -> returns true DOING NOTHING if the flag wasn't set (nominal case).
// The 4 RTs don't need to be recreated here: RenderPostBlur lazily recreates them
// (`if (!*(a1+540))` @0x40547A), exactly like the original.
bool GxdRenderer::RestoreAfterReset() {
    if (!deviceLost_) return true; // `return 1` @0x4045B7
    if (!device_) return false;    // no device: nothing to restore, flag kept
    deviceLost_ = false;           // (+527548) = 0 @0x404629 (total success)
    return true;
}

// GXD_RenderPostBlur 0x4053E0 — 3-stage bloom (decompilation + disassembly re-read).
//
// WIRING — CALL SITE OUTSIDE THIS FRONT, TO BE PLACED BY THE ORCHESTRATOR.
//   Original site: UNIQUE and UNCONDITIONAL, straight line (no jump between the two
//   bounds) in Scene_InGameRender:
//       @0x52FB49  call Env_StepTimeOfDay
//       @0x52FB53  mov ecx, offset g_GxdRenderer ; call GXD_RenderPostBlur
//       @0x52FB89  call Gfx_Begin2D
//   Expected C++ mirror: Scene/WorldRenderer.cpp (or Scene/InGameScene), AFTER all 3D
//   rendering and BEFORE the switch to 2D, unconditionally:
//       ts2::gfx::GxdRenderer::Instance().RenderPostBlur(shaderSet_);
//   Until this is placed, this function stays dead code.
//
// The original guard `cmp [ecx+18h], 1` @0x4053ED is NOT reproduced as an option:
// its only writer (@0x4013AC, `= 1`) makes it always true -> no activation parameter.
// The `else` branch (Release of the 4 RTs when the guard is false) is therefore dead: it is
// nonetheless carried out, elsewhere and for a real reason, by OnDeviceLost().
//
// Quad: 4 vertices (x, y, z, rhw, u, v), stride 24, D3DFVF_XYZRHW|D3DFVF_TEX1 = 260,
// drawn as a TRIANGLESTRIP (2 primitives):
//     v0 = (0, H, 0, 1, 0, 1)   v1 = (0, 0, 0, 1, 0, 0)
//     v2 = (W, H, 0, 1, 1, 1)   v3 = (W, 0, 0, 1, 1, 0)
// with (W,H) = (halfW, halfH) for passes 6/7 (@0x4055F0..0x405675) and FULL
// resolution for the composite (@0x405912..0x405995).
namespace {

// One vertex of the full-screen quad: 24 bytes, FVF 260.
struct PostBlurVertex {
    float x, y, z, rhw;
    float u, v;
};
static_assert(sizeof(PostBlurVertex) == 24, "GXD_RenderPostBlur's quad has a stride of 24");

// Fills the TRIANGLESTRIP quad in the binary's EXACT order.
void BuildPostBlurQuad(PostBlurVertex q[4], float w, float h) {
    q[0] = { 0.0f, h,    0.0f, 1.0f, 0.0f, 1.0f };
    q[1] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    q[2] = { w,    h,    0.0f, 1.0f, 1.0f, 1.0f };
    q[3] = { w,    0.0f, 0.0f, 1.0f, 1.0f, 0.0f };
}

} // namespace

void GxdRenderer::RenderPostBlur(const ShaderSet& shaders) {
    if (!device_) return;

    const GxdShader& sh12 = shaders.Get(GxdShaderId::PS12_PostBlur); // horizontal blur
    const GxdShader& sh14 = shaders.Get(GxdShaderId::PS14_PostBlur); // vertical blur
    if (!sh12.Valid() || !sh14.Valid()) return; // shaders not loaded: nothing to do

    // Sampler registers + handles resolved at compile time (the original froze them in
    // this+527424 / this+527464 (PS12) and this+527488 / this+527528 (PS14)).
    const int  samp12 = sh12.Sampler("mTexture0");
    const int  samp14 = sh14.Sampler("mTexture0");
    const D3DXHANDLE h12 = sh12.Handle("mTexture0PostSize");
    const D3DXHANDLE h14 = sh14.Handle("mTexture0PostSize");
    if (samp12 < 0 || samp14 < 0 || !h12 || !h14) return;

    if (!EnsureBlurTargets()) return;

    IDirect3DDevice9* dev = device_;
    const int halfW = width_  / 2;
    const int halfH = height_ / 2;

    // --- Fetches the back-buffer (vtbl+72 @0x405580) ---
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;

    // --- Downsample back-buffer -> surfA (StretchRect vtbl+136, Filter=2 LINEAR @0x4055AD) ---
    if (FAILED(dev->StretchRect(bb, nullptr, blurSurfA_, nullptr, D3DTEXF_LINEAR))) {
        bb->Release(); // @0x4055B9: the original exits via the surface's Release
        return;
    }

    // --- States shared by the 3 passes (@0x4055D5 / @0x4055EC ; ebp = 0 confirmed in the disasm) ---
    dev->SetRenderState(D3DRS_ZENABLE,  FALSE); // state 7   = 0
    dev->SetRenderState(D3DRS_LIGHTING, FALSE); // state 137 = 0

    PostBlurVertex quad[4];
    BuildPostBlurQuad(quad, static_cast<float>(halfW), static_cast<float>(halfH));

    // PASS 6 — horizontal blur: texA -> surfB (PS12)
    // The original closes the scene, switches the target, then reopens it: EndScene/SetRenderTarget/
    // BeginScene (@0x405681 / @0x40569A / @0x4056AB).
    dev->EndScene();
    dev->SetRenderTarget(0, blurSurfB_);
    dev->BeginScene();

    currentShaderId_ = 6;                                        // (+526884) = 6 @0x4056B3
    dev->SetVertexShader(nullptr);                                // @0x4056C7
    dev->SetPixelShader(sh12.ps);                                 // @0x4056DF (+527408)
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);                     // 260 @0x4056F4
    dev->SetTexture(static_cast<DWORD>(samp12), blurTexA_);      // @0x40570F
    dev->SetSamplerState(static_cast<DWORD>(samp12), D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); // @0x40572B
    dev->SetSamplerState(static_cast<DWORD>(samp12), D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP); // @0x405747
    // ID3DXConstantTable::SetFloat = vtbl+68 @0x40576B — SetFloat, NOT SetFloatArray.
    // Value = (float)halfW (COERCE_FLOAT(LODWORD(v23)), v23 converted @0x40563D).
    sh12.ct->SetFloat(dev, h12, static_cast<float>(halfW));
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(PostBlurVertex)); // @0x405787
    dev->SetSamplerState(static_cast<DWORD>(samp12), D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP); // @0x4057A3
    dev->SetSamplerState(static_cast<DWORD>(samp12), D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP); // @0x4057BF

    // PASS 7 — vertical blur: texB -> surfA (PS14)  (@0x4057D0 / @0x4057E9 / @0x4057FA)
    dev->EndScene();
    dev->SetRenderTarget(0, blurSurfA_);
    dev->BeginScene();

    currentShaderId_ = 7;                                        // (+526884) = 7 @0x405802
    dev->SetVertexShader(nullptr);                                // @0x405816
    dev->SetPixelShader(sh14.ps);                                 // @0x40582E (+527472)
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);                     // 260 @0x405843
    dev->SetTexture(static_cast<DWORD>(samp14), blurTexB_);      // @0x40585E
    dev->SetSamplerState(static_cast<DWORD>(samp14), D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); // @0x40587A
    dev->SetSamplerState(static_cast<DWORD>(samp14), D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP); // @0x405896
    // Value = (float)halfH (COERCE_FLOAT(LODWORD(v22))) @0x4058BA.
    sh14.ct->SetFloat(dev, h14, static_cast<float>(halfH));
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(PostBlurVertex)); // @0x4058D6
    dev->SetSamplerState(static_cast<DWORD>(samp14), D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP); // @0x4058F2
    dev->SetSamplerState(static_cast<DWORD>(samp14), D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP); // @0x40590E

    // COMPOSITE — texA (blurred) added to the back-buffer, at FULL resolution.
    // Quad rebuilt with W = (+48) and H = (+52)  (@0x405912..0x405995).
    BuildPostBlurQuad(quad, static_cast<float>(width_), static_cast<float>(height_));

    dev->EndScene();                 // @0x4059A1
    dev->SetRenderTarget(0, bb);     // @0x4059B8
    bb->Release();                   // @0x4059C4 — released BEFORE BeginScene, like the original
    bb = nullptr;
    dev->BeginScene();               // @0x4059D5

    currentShaderId_ = 0;                                        // (+526884) = 0 @0x4059DE
    dev->SetVertexShader(nullptr);                                // @0x4059ED
    dev->SetPixelShader(nullptr);                                 // @0x4059FF — back to fixed-function
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);                     // 260 @0x405A14
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);            // state 27 = 1 @0x405A29
    dev->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCCOLOR);      // state 19 = 3 @0x405A3E
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);           // state 20 = 2 @0x405A53
    dev->SetTexture(0, blurTexA_);                               // stage 0 @0x405A68
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); // @0x405A7E
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP); // @0x405A94
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(PostBlurVertex)); // @0x405AB0
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);  // @0x405AC6
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);  // @0x405ADC

    // --- State restoration (EXACT order from the binary) ---
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);          // state 20 = 1 @0x405AF1
    dev->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_ONE);           // state 19 = 2 @0x405B06
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);           // state 27 = 0 @0x405B1A
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);                    // state 137 = 1 @0x405B32
    dev->SetRenderState(D3DRS_ZENABLE,  TRUE);                    // state 7 = 1 @0x405B47
    // NB: the restoration leaves SRCBLEND=ONE / DESTBLEND=ZERO — these are NOT Object A's
    // initial states (SRCALPHA/INVSRCALPHA, Gfx_InitDevice @0x69C526/@0x69C535).
    // This is exactly what the original does; we don't "fix" it.
}

// Frustum_BuildPlanes 0x406090: extracts 6 planes (Gribb/Hartmann) from the
// combined view*projection matrix (matView_ * matProj_, same product as WorldToScreen).
// Each plane is normalized (division by the norm of vector (a,b,c)), faithful to
// the original which calls Math_CIsqrt then 1/x rather than a direct rsqrt.
void GxdRenderer::BuildFrustumPlanes() {
    D3DXMATRIX vp;
    D3DXMatrixMultiply(&vp, &matView_, &matProj_); // sub_6BB618, Out = M1 * M2

    auto normalize = [](float a, float b, float c, float d, D3DXPLANE& out) {
        float invLen = 1.0f / sqrtf(a * a + b * b + c * c);
        out.a = a * invLen;
        out.b = b * invLen;
        out.c = c * invLen;
        out.d = d * invLen;
    };

    // Matrix rows (row-major D3DX, v' = v * M): row1=(_11,_21,_31,_41)?
    // No: here "row N" denotes the Nth ROW of the matrix, _N1.._N4.
    const float m11 = vp._11, m12 = vp._12, m13 = vp._13, m14 = vp._14;
    const float m21 = vp._21, m22 = vp._22, m23 = vp._23, m24 = vp._24;
    const float m31 = vp._31, m32 = vp._32, m33 = vp._33, m34 = vp._34;
    const float m41 = vp._41, m42 = vp._42, m43 = vp._43, m44 = vp._44;

    // [0] Left   = row4 + row1
    normalize(m14 + m11, m24 + m21, m34 + m31, m44 + m41, frustumPlanes_[0]);
    // [1] Right  = row4 - row1
    normalize(m14 - m11, m24 - m21, m34 - m31, m44 - m41, frustumPlanes_[1]);
    // [2] Bottom = row4 + row2
    normalize(m14 + m12, m24 + m22, m34 + m32, m44 + m42, frustumPlanes_[2]);
    // [3] Top    = row4 - row2
    normalize(m14 - m12, m24 - m22, m34 - m32, m44 - m42, frustumPlanes_[3]);
    // [4] Near   = row3 alone (D3D convention, depth [0,1])
    normalize(m13, m23, m33, m43, frustumPlanes_[4]);
    // [5] Far    = row4 - row3
    normalize(m14 - m13, m24 - m23, m34 - m33, m44 - m43, frustumPlanes_[5]);
}

// Frustum_ContainsPoint5 0x406560: tests the first 5 planes (left/right/bottom/
// top/near); the far plane is deliberately not tested (faithful to the original,
// the only function called by GXD_WorldToScreen 0x405C00).
bool GxdRenderer::FrustumContains(const D3DXVECTOR3& world) const {
    for (int i = 0; i < 5; ++i) {
        const D3DXPLANE& p = frustumPlanes_[i];
        if (p.a * world.x + p.b * world.y + p.c * world.z + p.d < 0.0f) return false;
    }
    return true;
}

} // namespace ts2::gfx

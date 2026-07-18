// Gfx/GxdRenderer.h — renderer singleton of the GXD engine (g_GxdRenderer @ 0x18C4EF8).
// ex-VeryOldClient: TW2AddIn::GXD (v2 / Object B, Core/TW2AddIn/GXDHeader.h) — the CORRECT
//   class of the two homonymous GXD classes (the one carrying the 12 skinned shaders), CONFIRMED
//   Docs/TS2_GXD_ROSETTA.md §1.1/§3; do NOT confuse with v1 Core/GXD = Object A
//   (g_GfxRenderer 0x7FFE18, cf. Renderer.h). Proven discriminant: light 0.3/0.7 (v2),
//   NOT 0.4/0.5 (v1).
// Stacks, on top of ts2::gfx::Renderer, the "high-level" device states:
// projection/view/world matrices, material, directional light, viewport and texture
// sampling states.
//
// Faithful to the disassembly (see Docs/TS2_GXD_ENGINE.md):
//   GXD_DeviceReinit        0x4023F0  -> DeviceReinit        // ex-VeryOldClient: TW2AddIn::GXD::InitForAddIn (GXDCore.cpp:523, reuses_device=true)
//   GXD_BeginScene          0x404640  -> SetupFrame          // ex-VeryOldClient: BeginForDrawing (v2)
//   GXD_ConfigSamplerStates 0x403B50  -> ConfigSamplerStates // ex-VeryOldClient: SetDefaultTextureSamplerState
//   GXD_SetDirectionalLight 0x403980  -> SetDirectionalLight
//   GXD_WorldToScreen       0x405C00  -> WorldToScreen
//
// The original struct starts at 0x18C4EF8; the "(+N)" comments give the offset of the
// corresponding field in that struct. COM device B fields (CONFIRMED §1.1):
//   pD3D9 @+160 (0x18C4F98) · pDevice @+524 (0x18C5104) · pSprite @+528 (0x18C5108) ·
//   pFont @+532 (0x18C510C) · pDInput8 @+5440 (0x18C6438). TRAP: dword_18C5104 = the
//   pDevice FIELD (base+524), NOT the base of the renderer.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>

namespace ts2::gfx {

// The npk's 12 shaders (Gfx/ShaderSet.h). Forward declaration ONLY: RenderPostBlur
// receives the shader set as a PARAMETER (it does not own it) to avoid creating a
// header dependency GxdRenderer.h -> ShaderSet.h. In the binary the shader slots are
// file-scope globals (block 0x1945918+) that GXD_RenderPostBlur 0x4053E0 reads via
// this+527404..+527528 — here they're passed through the reference instead.
class ShaderSet;

class GxdRenderer {
public:
    // Ambient modes of SetDirectionalLight (edi param of GXD_SetDirectionalLight).
    enum LightMode : int {
        kLightKeepAmbient = 0, // ambient left at 0
        kLightAutoAmbient = 1, // ambient = diffuse*0.5 + ambient of the base light
        kLightUserAmbient = 2, // ambient supplied by the caller
    };

    // Singleton access (the original is the global g_GxdRenderer @ 0x18C4EF8).
    static GxdRenderer& Instance();

    // (GXD_DeviceReinit) Reattaches the singleton to the D3D9 device already created by
    // g_GfxRenderer, fetches the caps, validates vs_2_0/ps_2_0 and (re)builds the
    // proj/view/world matrices, viewport, material and default light.
    // Returns false + error code (1..4) if the caps are missing.
    // NB: loading the 12 shaders + GXDCompress.dll (original codes 5..12) belongs to
    //     the shader component and is not handled here.
    bool DeviceReinit(IDirect3D9* d3d, IDirect3DDevice9* device,
                      int width, int height, float nearZ, float farZ,
                      int* outError = nullptr);

    void Shutdown();

    // (GXD_BeginScene) Prepares the frame: Clear(target+z+stencil), recomputes the view,
    // viewport, SetTransform proj/view/world, SetMaterial, LightEnable+SetLight, sampler
    // states, dithering, BeginScene, then resets the pipeline to fixed-function.
    // TODO [anchor 0x404640]: NO CALLER to date — frame start goes through
    // ts2::gfx::Renderer::BeginFrame (Gfx_BeginFrame 0x6A2280, Object A). See the detailed
    // note above the definition in GxdRenderer.cpp.
    bool SetupFrame();

    // (GXD_ConfigSamplerStates) Min/mag/mip filters (anisotropic by default, linear if
    // useLinearFilter_) + WRAP addressing (U,V) on stages 0..2.
    // Called PER FRAME by ts2::gfx::Renderer::BeginFrame(), right after Clear+BeginScene:
    // same position as the 6 call sites of the Scene_*Render functions (0x518916, 0x51B0C6, ...).
    // Deliberately overwrites, every frame, the LINEAR filters set at init by Object A.
    void ConfigSamplerStates();

    // (GXD_SetDirectionalLight) Fills a local directional D3DLIGHT9 (diffuse/specular
    // zero, ambient per mode, direction (-1,-1,1)) then LightEnable(0)+SetLight(0).
    void SetDirectionalLight(int mode, float ar = 0.0f, float ag = 0.0f,
                             float ab = 0.0f, float aa = 1.0f);

    // (GXD_WorldToScreen) Projects a world point to a rounded screen pixel (round half-up).
    // Returns false if the point is outside the frustum.
    bool WorldToScreen(const D3DXVECTOR3& world, int& sx, int& sy) const;

    // (GXD_RenderPostBlur 0x4053E0) Bloom/post-blur: downsample the back-buffer to half
    // resolution, horizontal blur (pass 6 = PS12) then vertical (pass 7 = PS14) in
    // ping-pong, finally additive SRCCOLOR/ONE composite onto the back-buffer.
    //
    // NO ACTIVATION OPTION — faithful: the original guard `cmp [ecx+18h], 1` @0x4053ED
    // tests dword_18C4F10 (= base+24), whose xrefs_to gives ONLY ONE writer,
    // GXD_InitGlobalState @0x4013AC (`= 1`): the guard is ALWAYS true and the `else`
    // branch (Release of the 4 RTs) is dead. Bloom cannot be disabled in the client.
    //
    // SCENE CONTRACT (mandatory): the original function does EndScene/BeginScene around
    // EACH render target switch — it therefore ASSUMES an OPEN scene on entry and leaves
    // one OPEN on exit. Call it between BeginFrame() and EndFrame().
    //
    // WIRING TO BE PLACED OUTSIDE THIS FILE (no file in this front mirrors
    //   Scene_InGameRender 0x52D0B0): the original call is UNIQUE and UNCONDITIONAL, straight
    //   line @0x52FB53 (`mov ecx, offset g_GxdRenderer ; call GXD_RenderPostBlur`),
    //   between Env_StepTimeOfDay @0x52FB49 and Gfx_Begin2D @0x52FB89 — i.e. AFTER all
    //   3D rendering and BEFORE the switch to 2D. Cf. the wiring note in GxdRenderer.cpp.
    void RenderPostBlur(const ShaderSet& shaders);

    // (GXD_OnDeviceLost 0x4042E0) Sets the device-lost flag (+527548 @0x4042EC) then
    // releases the owned D3DPOOL_DEFAULT resources. Original release order re-read in the
    // decompiler: +548 (surfA), +540 (texA), +552 (surfB), +544 (texB) — each reset to
    // 0 after Release(). These are EXACTLY the 4 bloom render targets above.
    // Call BEFORE IDirect3DDevice9::Reset (counterpart @0x5188A8).
    void OnDeviceLost();

    // (GXD_RestoreAfterReset 0x404570) Guarded by flag `+527548 == 1` @0x40457A;
    // returns true doing nothing if the flag isn't set (`return 1` @0x4045B7), and
    // only clears the flag to 0 (@0x404629) on total success.
    // Call AFTER a SUCCESSFUL Reset() (counterpart @0x5188BC).
    //
    // REDUCED SCOPE, INTENTIONAL (not an omission): the original also recreates, under this
    // same flag, the skinned vertex declaration (CreateVertexDeclaration vtbl+344 @0x40461F,
    // g_GxdSkinnedVertexDecl76 0x814A58 -> +526880) and RECOMPILES the 12 shaders in order
    // VS01,PS02,VS03,PS04,VS05,PS06,VS07,PS08,VS09,PS12,PS14,VS15 (`return 0` @0x4045B8 if
    // one fails), and calls World_ReloadMap @0x40458F if a map is loaded (+6352).
    // On the ClientSource side, NONE of these three objects belong to GxdRenderer: the
    // declaration + the 12 shaders belong to ts2::gfx::ShaderSet (Gfx/ShaderSet.h, not owned
    // by this front) and the map to World/. They technically survive Reset() anyway
    // (IDirect3DVertexShader9/PixelShader9/VertexDeclaration9 are not D3DPOOL_DEFAULT):
    // this reload is defensive in the original. Their owners must go through the
    // Renderer::SetDeviceCallbacks observer (cf. Gfx/Renderer.h).
    // The 4 bloom RTs, however, ARE D3DPOOL_DEFAULT and are LAZILY recreated
    // by RenderPostBlur (`if (!*(a1+540))` @0x40547A) — exactly like the original.
    bool RestoreAfterReset();

    // (Camera_SetEyeTarget 0x403420) Pushes the eye (+712..+720) / target (+724..+732) pair.
    // RETURNS A BOOLEAN and REJECTS two cases — semantics re-read in the decompiler:
    //   1. `if (a5==a2 && a6==a3 && a7==a4) return 0;` @0x403465  -> eye == target.
    //   2. elevation angle |asin(|eye.y-at.y| / dist) * 57.2957763671875| > 89.989998
    //      @0x40350E -> `return 0`: too close to the pole for LookAtLH (up fixed at (0,1,0)).
    // The fields are written ONLY if both tests pass (@0x403522..0x403574).
    // NB: this 89.99 bound is DISTINCT from the 89.9 clamp of Cam_OrbitPitch 0x69CF90
    // (cf. Camera.h::kPitchLimitDeg) and from the 30/80 mouse-drag bounds — all three
    // coexist, at three different levels.
    //
    // NO CALLER to date (grep: the only `SetCamera` hits are
    //   MeshRenderer::SetCamera(view, proj), an UNRELATED class). The original, meanwhile, has
    //   33 live callers (Camera_MouseDragRotate, Camera_MouseWheelZoom,
    //   Camera_UpdateFromInput, Scene_InGameUpdate, Camera_UpdateCollision): the wiring
    //   is to be placed outside this front (App/App.cpp or Scene/).
    bool SetCamera(const D3DXVECTOR3& eye, const D3DXVECTOR3& at);

    // COPY setters (Scene_*Render: Object A -> Object B, once per frame).
    // Boundary proven by Scene_IntroRender 0x518880 (identical in the 5 other scenes):
    //   qmemcpy(&unk_18C51E4, &unk_800154, 0x40) @0x5188DC : matView_ (+748) <- GfxRenderer+828
    //   qmemcpy(&g_WorldMatrix, &dword_800244, 0x40) @0x5188ED : matWorld_ (+988) <- +1068
    //   dword_18C51D8/DC/E0 (+736/+740/+744) <- g_CameraDir 0x800148/4C/50 (+816/+820/+824)
    //     @0x5188F5 / @0x518901 / @0x51890C
    // These fields are DISJOINT from +712/+724 (eye/target above): both paths
    // coexist in the binary, they don't replace one another.
    // NO CALLER to date — wiring to be placed outside this front (App/App.cpp, guard
    //   `if (renderer_.BeginFrame())`, or Scene/).
    void SetViewMatrix(const D3DXMATRIX& view) { matView_ = view; }   // (+748)
    void SetWorldMatrix(const D3DXMATRIX& world) { matWorld_ = world; } // (+988)
    void SetViewDir(const D3DXVECTOR3& dir) { viewDir_ = dir; }       // (+736)

    IDirect3DDevice9*   Device()   const { return device_; }
    const D3DXMATRIX&   Proj()     const { return matProj_; }
    const D3DXMATRIX&   View()     const { return matView_; }
    const D3DXMATRIX&   World()    const { return matWorld_; }
    const D3DMATERIAL9& Material() const { return material_; }
    const D3DLIGHT9&    Light()    const { return light_; }

    void SetUseLinearFilter(bool v) { useLinearFilter_ = v; } // (+8, dword_18C4F00)
    bool DepthBiasCapable() const { return depthBiasCapable_; }
    bool TwoSidedStencil()   const { return twoSidedStencil_; }

private:
    void BuildMatrices();                // proj / view / world / half-viewport
    void BuildDefaultMaterialAndLight(); // material + directional light
    // Lazily creates the 4 half-resolution bloom render targets (@0x40547A..0x405563).
    // false if a creation fails (the original then releases what it had already taken and exits).
    bool EnsureBlurTargets();
    // Release() + reset to nullptr of the 4 RTs, original order +548, +540, +552, +544.
    void ReleaseBlurTargets();
    void BuildFrustumPlanes();           // Frustum_BuildPlanes 0x406090
    bool FrustumContains(const D3DXVECTOR3& world) const; // Frustum_ContainsPoint5 0x406560

    IDirect3D9*       d3d_      = nullptr; // pD3D9 @+160  — ex-VeryOldClient: (device COM B)
    IDirect3DDevice9* device_   = nullptr; // pDevice @+524 (0x18C5104) — ex-VeryOldClient: (device COM B, == g_GfxRenderer+604)

    int   width_  = 0;      // (+48 ; viewport +568)  ex-VeryOldClient: mScreenXSize
    int   height_ = 0;      // (+52 ; viewport +572)  ex-VeryOldClient: mScreenYSize
    float fovDeg_ = 45.0f;  // (+56) informative; the projection uses PI/4 rad — ex-VeryOldClient: mFovY (45.0)
    float nearZ_  = 0.0f;   // (+60)  ex-VeryOldClient: mNearPlane
    float farZ_   = 0.0f;   // (+64)  ex-VeryOldClient: mFarPlane

    D3DVIEWPORT9 viewport_{};        // (+560) X,Y,W,H,MinZ=0,MaxZ=1  ex-VeryOldClient: mViewport
    D3DXMATRIX   matHalfViewport_;   // (+584) NDC [-1,1] -> pixels    ex-VeryOldClient: mViewportMatrix
    D3DXMATRIX   matProj_;           // (+648) D3DXMatrixPerspectiveFovLH  ex-VeryOldClient: mPerspectiveMatrix
    D3DXVECTOR3  eye_{0.0f, 0.0f, -10.0f}; // (+712)  ex-VeryOldClient: mCameraEye
    D3DXVECTOR3  at_{0.0f, 0.0f, 0.0f};     // (+724)  ex-VeryOldClient: mCameraLook
    D3DXVECTOR3  viewDir_{0.0f, 0.0f, 0.0f};// (+736) normalize(at-eye)  ex-VeryOldClient: mCameraForward
    D3DXMATRIX   matView_;           // (+748) D3DXMatrixLookAtLH  ex-VeryOldClient: mViewMatrix
    D3DXMATRIX   matWorld_;          // (+988) identity           ex-VeryOldClient: mWorldMatrix
    D3DMATERIAL9 material_{};        // (+1052)  ex-VeryOldClient: mMaterial
    D3DLIGHT9    light_{};           // (+1120)  ex-VeryOldClient: mLight (v2: Amb 0.3/Diff 0.7 — NOT v1 0.4/0.5)

    D3DCAPS9 caps_{};                // (+164)  ex-VeryOldClient: mGraphicSupportInfo
    DWORD    maxAnisotropy_ = 1;     // caps.MaxAnisotropy (g_GxdMaxAnisotropy @ +272)
    int      currentShaderId_ = 0;   // (+526884) current program, reset to 0 per frame — ex-VeryOldClient: mPresentShaderProgramNumber (0=fixed,6=Filter1,7=Filter2)

    bool useLinearFilter_ = false;  // (+8) 0 => anisotropic (default), !=0 => linear — ex-VeryOldClient: mSamplerOptionValue (ctor=0)

    // FIELDS +28 / +32 — DUAL IDENTITY (gap G3, fixed in Passe 4 / W9).
    //
    // These two dwords carry TWO names depending on which end you look from:
    //   +28 = 0x18C4F14 = "depthBiasCapable_" (producer: GPU caps) == g_ShadowsEnabled
    //         (consumer: gate for ALL shadow rendering, read @0x40EEF8)
    //   +32 = 0x18C4F18 = "twoSidedStencil_"  (producer) == g_ShadowMethod
    //         (consumer: 0 = z-fail Carmack 2-pass, 1 = two-sided stencil;
    //          read @0x40EFCC / @0x40F27B / @0x40F66A)
    //
    // THEY ARE ALWAYS 1 IN THE BINARY — two stacking proofs:
    //   1. GXD_InitGlobalState 0x401320 sets them to 1 (`mov ebx, 1` @0x401365 then
    //      `mov ds:g_ShadowsEnabled, ebx` @0x4013B2 / `mov ds:g_ShadowMethod, ebx` @0x4013B8).
    //   2. GXD_DeviceReinit only ever LOCKS them to 1, never clears them — re-read in the
    //      disassembly: `jz short loc_40292D ; mov [esi+1Ch], edi` @0x402928/@0x40292A and
    //      `test dword ptr [esi+12Ch], 100h ; jz ; mov [esi+20h], edi` @0x40292D..@0x402939
    //      (edi = 1, 0x1C = 28, 0x20 = 32). NO `else` branch: a GPU lacking the cap simply
    //      leaves the 1 from init in place.
    //   GXD_FreeGlobalState 0x401530 (fully decompiled) touches NEITHER +28 NOR +32 —
    //   hence the absence of a reset to false in Shutdown() (cf. GxdRenderer.cpp).
    // => initialized to `true` and NEVER written `false` (cf. DeviceReinit).
    //
    // AUTHORITATIVE CONSUMER MODEL: Gfx/MeshRenderer.h:391-392
    //   (shadowsEnabled_ = true / shadowMethod_ = 1, same anchors 0x4013B2/0x4013B8). The same
    //   binary field is therefore modeled TWICE in ClientSource; on future divergence,
    //   MeshRenderer.h wins — it's the one that actually drives a render branch.
    //   DepthBiasCapable()/TwoSidedStencil() themselves have no caller (grep).
    // Producer note: bit 0x4000000 = D3DPRASTERCAPS_DEPTHBIAS (legacy depth-bias), NOT
    // anisotropy (0x20000). Rosetta §2 labels this bit "SLOPESCALEDEPTHBIAS", but
    // 0x4000000 IS DEPTHBIAS (SLOPESCALEDEPTHBIAS = 0x2000000) — IDA wins. Anisotropy does
    // NOT depend on this field (cf. useLinearFilter_ / maxAnisotropy_, ConfigSamplerStates).
    // ex-VeryOldClient: mCheckDepthBias / mCheckTwoSideStencilFunction.
    bool depthBiasCapable_ = true; // (+28) == g_ShadowsEnabled 0x18C4F14, locked to 1
    bool twoSidedStencil_  = true; // (+32) == g_ShadowMethod   0x18C4F18, locked to 1

    // ----- Bloom post-process (GXD_RenderPostBlur 0x4053E0) — D3DPOOL_DEFAULT -------------
    // Lazily created at the 1st RenderPostBlur (`if (!*(a1+540))` @0x40547A) and released
    // by OnDeviceLost(). surfX = GetSurfaceLevel(0) of texX (vtbl+72 @0x4054AC / @0x405536).
    // Ping-pong: texA = downsample(backbuffer) -> texB = blurH(texA) -> texA = blurV(texB)
    //             -> backbuffer += texA (SRCCOLOR/ONE).
    IDirect3DTexture9* blurTexA_  = nullptr; // (+540)
    IDirect3DTexture9* blurTexB_  = nullptr; // (+544)
    IDirect3DSurface9* blurSurfA_ = nullptr; // (+548) surface 0 of blurTexA_
    IDirect3DSurface9* blurSurfB_ = nullptr; // (+552) surface 0 of blurTexB_

    // (+527548) "Device lost" flag: set by OnDeviceLost (@0x4042EC), consumed and
    // cleared by RestoreAfterReset (@0x40457A / @0x404629), reset to 0 by DeviceReinit
    // (@0x402451).
    bool deviceLost_ = false;

    // Frustum planes (Frustum_BuildPlanes 0x406090, this+309..332 in the original,
    // i.e. 6 planes of 4 floats extracted from the combined view*projection matrix via
    // the Gribb/Hartmann method, D3D depth convention [0,1]):
    //   [0]=left(row4+row1) [1]=right(row4-row1) [2]=bottom(row4+row2) [3]=top(row4-row2)
    //   [4]=near(row3 alone, without row4) [5]=far(row4-row3)
    // Each plane is stored as (a,b,c,d) such that a*x+b*y+c*z+d >= 0 means "inside".
    // Frustum_ContainsPoint5 (0x406560, used by WorldToScreen) only tests the first 5
    // planes (left/right/bottom/top/near); the far plane [5] is computed
    // but not tested by this original function (faithful to the disassembly).
    D3DXPLANE frustumPlanes_[6]{};
};

} // namespace ts2::gfx

// Gfx/ModelObjectRenderer.h — rendering of effect "model-objects" (.MOBJECT) from the
// MiscC bank (unk_B60AB8), MINIMAL SLICE: unblocks the `s_meshDraw` hook of FxRenderer.cpp and
// makes the combat FX mesh VISIBLE (block/parry/deflect, types 8/9/0xA of Fx_EmitterDraw).
//
// FRONT F_MOBJ (2026-07-17). IDA ground truth: TwelveSky2.exe (idaTs2, imagebase 0x400000). Each
// block of the .cpp carries its anchor. Byte-exact RECON: Docs/TS2_EXTRACT_MOBJ_DISPATCH.md,
// TS2_EXTRACT_MESHPART_FULL.md, TS2_EXTRACT_MOBJ_BANKS.md, TS2_EXTRACT_MOBJ_VERDICT.md.
//
// ===========================================================================================
//  ORIGINAL CHAIN REPRODUCED (full material via B1 — see FRONT C1 section below)
// ===========================================================================================
//   Fx_EmitterDraw 0x585E30 (already ported, FxRenderer.cpp) — s_meshDraw wired HERE
//     -> ModelObj_Draw 0x4D71B0        : lazy-load + D3D state reset + Model_RenderWithShadow_0
//        -> ModelObj_Load 0x4D6F80      : (V1: SYNCHRONOUS — see assumed divergence below)
//           -> Model_LoadFromFile 0x6A3490 -> MeshPart_Load 0x6AD160  (parser ALREADY written:
//              asset::MObject, Asset/Model.h — REUSED as-is, byte-exact)
//        -> Model_RenderWithShadow_0 0x6A4110 : Rz*Ry*Rx*T matrix, frame = Crt_Dbl2Uint(a3)
//           gate [0, parts[0].A-1], per-part frustum-cull (Cam_FrustumTestSphere 0x69EF90, ×1)
//           -> MeshPart_RenderFull 0x6B0850 : base-draw core @0x6B1327 (SetStreamSource(0, VB,
//              32*frame*B, 32) + SetIndices(IB) + SetTexture(0, tex0) + DrawIndexedPrimitive(
//              TRIANGLELIST, 0, 0, B, 0, D)) — IDENTICAL to the already-ported world path
//              (WorldGeometryRenderer::renderObjects, MeshPart_Render 0x6AED60).
//
//  BANK RESOLUTION (proven by AssetMgr_InitAllSlots 0x4DEB50 @0x4dee8c): the MiscC bank
//  (unk_B60AB8 = g_ModelMotionArray 0x8E8B30 + 2588552) is populated by the loop
//  `for(i6<246) ModelObj_BuildPath(base + 148*i6, category=4, i6, 0, 0)` -> category 4 =
//  `G03_GDATA\D02_GMOBJECT\003\E%03d001.MOBJECT` with `%03d = idxC + 1` (ModelObj_BuildPath
//  0x4D6E20 @0x4d6ed5). So slot idxC in [0,245] -> file E{idxC+1:03}001.MOBJECT.
//  Known idxC (already written by FxSetters.cpp): 131 = Deflect; 135/156 = BlockGuard/Parry;
//  230..233 = morph auras (Fx_DrawZoneAura, not routed by this hook — see MAIN INTEGRATION).
//
// ===========================================================================================
//  FRONT C1 (2026-07-17) — COMPLETE MATERIAL STATE MACHINE (B1 wiring)
// ===========================================================================================
//  The per-part indexed base-draw is now REPLACED by MeshPartMaterialRenderer::Render
//  (Gfx/MeshPartMaterial.h, full port of MeshPart_RenderFull 0x6B0850): the fixed-function
//  layers (light-anim ping-pong 0x6B08AF, specular glow 0x6B0A11, flipbook 0x6B0D33,
//  UV-scroll tex1/tex2 0x6B0F59/0x6B19BB, 2nd texture 0x6B19AD, billboard 0x6B107C — honest
//  fallback) are set by ONE state machine, exactly as Model_RenderWithShadow_0 0x6A4110
//  @0x6a4362/@0x6a45f7 calls MeshPart_RenderFull. PROVEN FX path (ModelObj_Draw 0x4D71B0
//  @0x4d72af: Model_RenderWithShadow_0(model, pass, frame, pos, orient, 0.0, 0, 1, 0)) ->
//  animTime phase=0, decal=null, glowEnable=1, alphaFade=0.
//  With null header fields (default case for most FX), Render degenerates EXACTLY into
//  the base diffuse draw (SetTexture(0,tex0) + base-draw @0x6B1327) — original behavior.
//  LIGHTING CAVEAT (documented fallback): this renderer keeps LIGHTING=FALSE (unlit draw, the
//  choice of the original slice — behavior PRESERVED). LIGHT-based layers (light-anim/glow/
//  noLight) are therefore SET but visually inert (D3DMATERIAL9/lights ignored under
//  LIGHTING=FALSE); TEXTURE/BLEND layers (flipbook, uv-scroll, 2nd texture, alpha modes) are
//  fully active. No LIGHTING=TRUE invented (state not proven on this path — TODO anchor).
//  If mat.decoded==false (degenerate part): fallback = current base-draw UNCHANGED.
//  AvatarA/NpcB bank (types 1-4) not handled in V1 (single MiscC bank) — TODO anchor.
//
//  HONEST DEGRADATION (placement): exact placement on the weapon bone
//  (SObject_Draw 0x4D8F90 -> Model_GetAttachTransform 0x40FDC0) is NOT ported. `pos`/`orient`
//  come from the slot (populated by MAIN at the source entity's center, like particles): the
//  mesh is VISIBLE but placed at the entity's center, not at the weapon tip. No bone transform
//  invented. Auras (Char_DrawAura/Fx_DrawZoneAura), on the other hand, pass REAL entity
//  positions -> fully faithful if MAIN routes them to this renderer (out of scope for V1).
//
//  ASSUMED DIVERGENCE (loading): ModelObj_Load 0x4D6F80 with a6=0 pushes an ASYNCHRONOUS
//  request (MobjLoader_Enqueue 0x4E68E0, background thread); here loading is SYNCHRONOUS on
//  the 1st draw (asset::MObject::Load). No format/protocol/gameplay altered — the mesh appears
//  the same frame instead of the next one. TODO anchor: port the async loader if threading
//  fidelity becomes a goal.
#pragma once
#include "Gfx/Renderer.h"
#include "Gfx/FxRenderer.h"        // FxMeshBank, FxModelObjDrawFn (hook s_meshDraw)
#include "Gfx/MeshPartMaterial.h"  // FRONT C1 : MeshPartMaterialRenderer::Render + MeshPartGpu/MeshPartTextures/MeshPartRuntime
#include "Asset/Model.h"           // asset::MObject / MeshPart / MGeometry / MTexture / MeshPartMaterial (ported parser)
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ts2::gfx {

class ModelObjectRenderer {
public:
    ~ModelObjectRenderer();
    ModelObjectRenderer() = default;
    ModelObjectRenderer(const ModelObjectRenderer&) = delete;
    ModelObjectRenderer& operator=(const ModelObjectRenderer&) = delete;

    // renderer must already be Init()'d (D3D9 device ready); gameDataDir = "GameData" root
    // (same convention as Gfx/ModelCache and Game/GameDatabase). Registers THIS instance as the
    // active renderer for the hook shim (ModelObjectRenderer_MeshDrawShim).
    bool Init(Renderer& renderer, std::string gameDataDir);
    void Shutdown();

    // D3DPOOL_MANAGED: survives a Reset() without re-upload (same policy as
    // WorldGeometryRenderer / MeshRenderer). No-op — see .cpp for the device recreation note.
    void OnDeviceLost();
    void OnDeviceReset();

    // Call ONCE/frame BEFORE the Fx_EmitterDraw passes: supplies the frame's view/proj matrices,
    // from which the 6 frustum planes are reconstructed (mirrors the g_GfxRenderer+334 planes
    // read by Cam_FrustumTestSphere 0x69EF90). Without this call, per-part culling is DISABLED
    // (all present parts are drawn — safe degradation, never over-culls).
    void SetFrame(const D3DXMATRIX& view, const D3DXMATRIX& proj);

    // Mirrors ModelObj_Draw 0x4D71B0 (reduced) — called by the s_meshDraw hook from
    // Fx_EmitterDraw for a MESH slot. `bank`: only MiscC is handled in V1. `idxC`: bank slot
    // (file E{idxC+1}001.MOBJECT). `pass` in {1,2}. `drawParam` = frame index (a3).
    // `pos` (3 world floats) / `orient` (3 euler-degree floats) = a4/a5 of the binary.
    void MeshDraw(FxMeshBank bank, int idxA, int idxB, int idxC,
                  int pass, float drawParam, const float pos[3], const float* orient);

    // Sanity: number of .MOBJECT entries resident in the MiscC cache.
    size_t ResidentCount() const { return cacheMiscC_.size(); }

    // Number of flipbook frames (parts[0].A) of a MiscC slot — synchronous lazy-load if needed.
    // Used for the FX mesh slot lifecycle (recycling once the flipbook is done, see SceneManager,
    // otherwise the mesh would stay displayed permanently). Returns 0 if idxC out of bounds / load failed.
    uint32_t FrameCount(int idxC);

private:
    // GPU part of a .MOBJECT (mirrors MeshPart 408 bytes, GPU fields) — same layout as
    // WorldGeometryRenderer::StaticObject, source = asset::MeshPart instead of WorldMeshPart.
    struct GpuPart {
        IDirect3DVertexBuffer9* vb      = nullptr; // 32*A*B bytes (A frames), MeshPart+288 (a1[72])
        IDirect3DIndexBuffer9*  ib      = nullptr; // 6*D bytes (shared across frames), MeshPart+292 (a1[73])
        IDirect3DTexture9*      diffuse = nullptr; // tex0/base (OWNED), MeshPart+344 (a1[86])
        IDirect3DTexture9*      second  = nullptr; // tex1/2nd texture (OWNED), MeshPart+396 (a1[99])
        int                     baseMode   = 0;    // MeshPart+340 (a1[85]) = tex0.trailer1 (alphaMode) — 1=alpha-test/2=blend
        int                     secondMode = 0;    // MeshPart+392 (a1[98]) = tex1.trailer1 (alphaMode)
        // Animated flipbook atlas (MeshPart+404 a1[101], count = a1[100] = matCount): OWNED textures.
        std::vector<IDirect3DTexture9*> flipbook;
        // Decoded 120-byte material header (asset::MeshPartMaterial, FLEET A) — copied from part.mat;
        // mat.decoded==false => base-draw fallback. Value-type (no pointer), safe to copy.
        asset::MeshPartMaterial mat;
        uint32_t                A = 1;             // flipbook frames (MGeometry.M = MeshPart+252)
        uint32_t                B = 0;             // vertices per frame (MGeometry.V = MeshPart+256)
        uint32_t                D = 0;             // triangles/primCount (MGeometry.I = MeshPart+264)
        // Per-frame bbox/node block (A*64 bytes, MeshPart+284 / a1[71]): 64 bytes per element, center vec3 @+48,
        // radius @+60 — source of the per-part frustum-cull (Model_RenderWithShadow_0 @0x6a431b/@0x6a4339)
        // AND the view-dependent fresnel glow (MeshPart_RenderFull @0x6b0a81, MeshPartRuntime.frameNodes).
        std::vector<uint8_t>    frameBbox;
    };
    // Bank entry = a 148-byte ModelObj slot (mirror: loaded Model container + GPU parts).
    struct MObjEntry {
        bool                 loaded      = false; // ModelObj+0: loaded successfully
        bool                 loadFailed  = false; // remembered failure (do not retry every frame)
        uint32_t             frameCountA = 0;     // parts[0].A (frame gate bound, MeshPart+252)
        std::vector<GpuPart> parts;               // parts actually uploaded (VB/IB/tex)
    };

    // Mirrors ModelObj_Load 0x4D6F80 (SYNCHRONOUS): resolves E{idxC+1}001.MOBJECT, parses (MObject::Load)
    // and uploads on first access. nullptr if idxC out of [0,245] or load failure (cached).
    MObjEntry* getOrLoadMiscC(int idxC);
    bool uploadPart(const asset::MeshPart& part, GpuPart& out);
    void releaseEntry(MObjEntry& e);
    void releaseAll();

    // World = Rz(orient.z°)*Ry(orient.y°)*Rx(orient.x°)*T(pos) — order IDENTICAL to
    // Model_RenderWithShadow_0 0x6a41a3-0x6a4299 (= WorldGeometryRenderer::BuildInstanceWorldMatrix).
    static D3DXMATRIX BuildWorldMatrix(const float pos[3], const float* orient);
    static IDirect3DTexture9* createTexture(IDirect3DDevice9* dev, const asset::MTexture& tex);
    // Cam_FrustumTestSphere 0x69EF90 (margin ×1: plane·c + d >= -radius) on the 6 reconstructed planes.
    bool sphereInFrustum(const D3DXVECTOR3& c, float radius) const;

    // Material animation clock (v66 = Terrain_PushRenderState() + a3; a3=0 on the FX path).
    // Mirrors Terrain_PushRenderState 0x69CB80 = QPC timer (elapsed seconds), same pattern as
    // EmitterMeshRenderer::ElapsedSeconds. LOCAL origin at the 1st call (relative phase, faithful).
    float animClockSeconds();

    IDirect3DDevice9*                  dev_ = nullptr;
    std::string                        gameDataDir_;
    bool                               ready_      = false;
    bool                               frameValid_ = false; // was SetFrame called this frame?
    float                              planes_[6][4] = {};  // 6 frustum planes (inward-facing, normalized)
    // World camera eye/target (MeshPartRuntime) DERIVED from the view matrix in SetFrame (g_GfxRenderer
    // 0x7FFE18 absent): eye = inverse(view) translation; target = eye + forward (zaxis LookAtLH).
    // Fallback (0,0,0) until SetFrame has been called (frameValid_==false).
    D3DXVECTOR3                        cameraEye_ = {0.0f, 0.0f, 0.0f};
    D3DXVECTOR3                        cameraAt_  = {0.0f, 0.0f, 0.0f};
    // QPC timer (animClockSeconds): lazy local origin.
    long long                          qpcFreq_  = 0;
    long long                          qpcStart_ = 0;
    bool                               qpcInit_  = false;
    std::unordered_map<int, MObjEntry> cacheMiscC_;         // MiscC bank (unk_B60AB8), key = idxC
};

// ---------------------------------------------------------------------------
// Free shim for the FxModelObjDrawFn hook (s_meshDraw): forwards to the active renderer registered
// by ModelObjectRenderer::Init. No-op if no active renderer (wiring order doesn't matter).
// Signature STRICTLY identical to FxModelObjDrawFn (FxRenderer.h).
void ModelObjectRenderer_MeshDrawShim(FxMeshBank bank, int idxA, int idxB, int idxC,
                                      int pass, float drawParam,
                                      const float pos[3], const float* orient);

} // namespace ts2::gfx

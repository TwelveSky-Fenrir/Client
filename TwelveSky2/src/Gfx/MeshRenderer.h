// Gfx/MeshRenderer.h - skinned mesh/model rendering for the GXD engine (Direct3D9).
//
// FAITHFUL rewrite of TwelveSky2's GPU-skinned draw path:
//   Model_DrawSkinnedSubset 0x40CA40  (core: bone palette -> VS constants, DrawIndexedPrimitive)
//   Model_Render            0x40EBB0  (composes the world matrix, loops meshes/LOD)
//   Mesh_ReadFromFile       0x40BC50  (subsets: 76 B VB, 6 B/face IB, INDEX16)
//   g_GxdVertexDecl         0x814A58  (D3DVERTEXELEMENT9 declaration of the 76 B vertex)
//   Shader_LoadVS03_SkinnedLit 0x409AB0 / Shader_LoadPS04_Tex 0x409CC0
//       (4-influence GPU skinning via mKeyMatrix, see Docs/TS2_GXD_ENGINE.md Sec 2.2)
//       ex-VeryOldClient: mAmbient2_VS (02.Ambient2.vs.fx / MakeShaderProgram03) +
//       mAmbient2_PS (02.Ambient2.ps.fx / MakeShaderProgram04) - skinned Pass 2, CONFIRMED Sec 1.4.
//
// Uses the DirectX SDK June 2010 (d3dx9) exactly like the original binary:
// D3DXCompileShader + ID3DXConstantTable (SetMatrixArray/SetMatrix/SetFloatArray),
// D3DXCreateTextureFromFileInMemoryEx, D3DXMatrix*.
//
// The physical device is ts2::gfx::Renderer's (== Object B +524 dword_18C5104).
// ex-VeryOldClient: pDevice @+524 of TW2AddIn::GXD (v2), == g_GfxRenderer+604 (shared device).
#pragma once
#include "Gfx/Renderer.h"
#include "Gfx/ShaderSet.h" // real npk shader slots (VS03/PS04/VS15) - read only (W3-F2)
#include "Asset/Model.h"
#include <d3dx9.h>
#include <cstddef>
#include <cstdint>
#include <utility> // std::move (SkinnedModel move operations)
#include <vector>

namespace ts2::gfx {

// ---------------------------------------------------------------------------
//  GPU skinned vertex - 76 bytes, EXACT layout of the g_GxdVertexDecl declaration
//  (found at 0x814A58; see Docs/TS2_GXD_ENGINE.md Sec 4.3):
//  ex-VeryOldClient: mVertexElementForSKIN2 -> mDECLForSKIN2 (stride 76, BLENDINDICES D3DCOLOR) -
//  BIT-EXACT match IDA=VeryOld, CONFIRMED Docs/TS2_GXD_ROSETTA.md Sec 1.5.
//    [0]  POSITION     FLOAT3   @ 0
//    [1]  BLENDWEIGHT  FLOAT4   @ 12  (4 weights)
//    [2]  BLENDINDICES D3DCOLOR @ 28  (4 packed bone indices)
//    [3]  TANGENT      FLOAT3   @ 32
//    [4]  BINORMAL     FLOAT3   @ 44
//    [5]  NORMAL       FLOAT3   @ 56
//    [6]  TEXCOORD0    FLOAT2   @ 68
//  The first 32 bytes = CPU SkinVertex (position + 4 weights + 4 indices).
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct GpuSkinVertex {
    float    position[3];    // +0   POSITION
    float    blendWeight[4]; // +12  BLENDWEIGHT (w0..w3)
    uint32_t blendIndices;   // +28  BLENDINDICES (D3DCOLOR: bytes b0,b1,b2,b3)
    float    tangent[3];     // +32  TANGENT
    float    binormal[3];    // +44  BINORMAL
    float    normal[3];      // +56  NORMAL
    float    texcoord[2];    // +68  TEXCOORD0
};
#pragma pack(pop)
static_assert(sizeof(GpuSkinVertex) == 76, "GpuSkinVertex doit faire 76 octets");
static_assert(offsetof(GpuSkinVertex, blendWeight) == 12, "BLENDWEIGHT @ 12");
static_assert(offsetof(GpuSkinVertex, blendIndices) == 28, "BLENDINDICES @ 28");
static_assert(offsetof(GpuSkinVertex, tangent) == 32, "TANGENT @ 32");
static_assert(offsetof(GpuSkinVertex, binormal) == 44, "BINORMAL @ 44");
static_assert(offsetof(GpuSkinVertex, normal) == 56, "NORMAL @ 56");
static_assert(offsetof(GpuSkinVertex, texcoord) == 68, "TEXCOORD @ 68");

// Bone palette already resolved for ONE frame (mKeyMatrix uploaded via SetMatrixArray).
// In Model_DrawSkinnedSubset: base + ((frame*bonesPerFrame) << 6), count=bonesPerFrame.
struct BonePalette {
    const D3DXMATRIX* matrices = nullptr; // frame slice (bonesPerFrame matrices, 64 B each)
    UINT              count    = 0;       // bone count (bonesPerFrame)
    bool Valid() const { return matrices != nullptr && count > 0; }
};

// Full animation palette (MotionPalette: valid/frameCount/bonesPerFrame/base,
// see Docs/TS2_GXD_ENGINE.md Sec 5.7). One matrix = 64 B (4x4 float).
struct MotionPalette {
    int               valid         = 0;
    int               frameCount    = 0;
    int               bonesPerFrame = 0;
    const D3DXMATRIX* base          = nullptr;

    // Cuts out one frame's bone slice (frame = ftol(animTime), bounded 0..frameCount-1).
    // Reproduces v59 = ftol(a5) then base + ((frame*bonesPerFrame) << 6).
    BonePalette FrameSlice(float animTime) const;
};

// One LOD level of a mesh, GPU-resident (Model_DrawSkinnedSubset: arrays
// a2+688 = VB per LOD, a2+696 = IB per LOD, a2+684 = vtxCount, a2+692 = triCount).
struct SkinnedLod {
    IDirect3DVertexBuffer9* vb          = nullptr; // 76 B/vertex
    IDirect3DIndexBuffer9*  ib          = nullptr; // INDEX16, 6 B/face
    UINT                    vertexCount = 0;
    UINT                    faceCount   = 0;

    // CPU data retained for the shadow volume (Model_BuildShadowVolume 0x40DC70):
    //   skinCpu  = SObjectSubset::skin        (32 B/vertex: pos12 + weights16 + boneIdx u32; mesh+700 v6[175])
    //   idxTopo  = SObjectSubset::indexCopy1  (6 B/face = 3x u16: triangle topology; mesh+704 v6[176])
    //   idxAdj   = SObjectSubset::indexCopy2  (6 B/face = 3x u16: per-edge adjacency; mesh+708 v6[177])
    // Retained as raw bytes because BuildShadowVolume skins on the CPU + walks the silhouette.
    std::vector<uint8_t> skinCpu; // 32 * vertexCount
    std::vector<uint8_t> idxTopo; // 6  * faceCount
    std::vector<uint8_t> idxAdj;  // 6  * faceCount
};

// A model mesh (SObjectMesh, 888 B). Parser "subsets" = LOD levels.
//
// THREE MATERIAL SLOTS (Model_DrawSkinnedSubset 0x40CA40, material = 56 B):
//   mat0 = mesh+712, mat1 = mesh+768, mat2 = mesh+824   (@0x40cab2 / @0x40cab6 / @0x40cabc)
//   pTexture = mat+52; blendMode = mat+44 (alphaMode trailer, see asset::SObjectTexture)
//   -> 712+52 = 764 = the "+764" tested by Model_Render 0x40EBB0 @0x40ee53.
// mat1/mat2 feed multi-texture passes 3 and 4 (mTexture1/mTexture2), see DrawSkinnedSubset.
struct SkinnedMesh {
    std::vector<SkinnedLod> lods;
    IDirect3DTexture9*      diffuse   = nullptr; // mat0.pTexture (mesh+712, +52) -> mTexture0
    IDirect3DTexture9*      tex1      = nullptr; // mat1.pTexture (mesh+768, +52) -> mTexture1 (passes 3/4)
    IDirect3DTexture9*      tex2      = nullptr; // mat2.pTexture (mesh+824, +52) -> mTexture2 (pass 4)
    // mat0 +44. NAME FIX (Pass 4 / W7): "2 = additive" was WRONG. The binary sets
    // SRCBLEND=5 (D3DBLEND_SRCALPHA) @0x40cc1c + DESTBLEND=6 (D3DBLEND_INVSRCALPHA) @0x40cc1e/0x40cc20
    // = STANDARD ALPHA BLENDING. A true additive would be ONE/ONE - that's not what 0x40CA40 does.
    uint32_t                blendMode = 0;       // 0=opaque / 1=alpha-test / 2=alpha blend
    bool                    empty     = true;    // valid==0 => empty mesh
};

// Complete GPU-resident skinned model (.SOBJECT).
//
// SOLE OWNER of COM resources (VB/IB/textures released by Release()): MOVABLE,
// NOT COPYABLE. The original binary stores each model in ONE catalog slot by POINTER
// (Mesh_ReadFromFile 0x40BC50 / SObject_DrawEx 0x4D9330 read a2+688/696 in place) - never a
// value copy of a model, so a single ownership per resource. The C++ code must reflect that.
//
// FIXED BUG (CharSelect crash, d3d9 AV reading 0x00000001 in DrawIndexedPrimitive):
// declaring `~SkinnedModel` removes the implicit MOVE CONSTRUCTOR but LEAVES the implicit
// (shallow) COPY. Without the declarations below, `ModelCache::Get` did
// `entries_.emplace(stem, std::move(entry))` (ModelCache.cpp:113): that "move" fell back to the
// shallow copy, duplicating the COM pointers WITHOUT AddRef; then the local `entry` was
// destroyed and its Release() dropped the refcounts to 0 -> VB/IB FREED, while the copy
// resident in the map kept dangling pointers. The next DrawIndexedPrimitive
// (MeshRenderer.cpp:777, CharPreview3D path) dereferenced a dead IDirect3DVertexBuffer9.
// CharSelect is the FIRST site that draws skinned geometry via ModelCache -> first to crash.
// Making the type move-only turns `std::move(entry)` into a real transfer (source emptied, a
// single owner): no more double-Release, no more dangling pointer.
class SkinnedModel {
public:
    std::vector<SkinnedMesh> meshes;

    SkinnedModel() = default;

    SkinnedModel(const SkinnedModel&)            = delete; // COM resources, single ownership
    SkinnedModel& operator=(const SkinnedModel&) = delete;

    SkinnedModel(SkinnedModel&& o) noexcept : meshes(std::move(o.meshes)) {}
    SkinnedModel& operator=(SkinnedModel&& o) noexcept {
        if (this != &o) { Release(); meshes = std::move(o.meshes); }
        return *this;
    }

    bool Empty() const { return meshes.empty(); }
    void Release(); // releases all VB/IB/textures

    ~SkinnedModel() { Release(); }
};

// MeshRenderer owns the skinned vertex declaration + skinned shader program
// (VS SkinnedLit + textured PS) and drives the rendering.
class MeshRenderer {
public:
    // Max bone count supported by the VS palette. The PLAYER skeleton has 76 bones (measured on
    // disk from .MOTION; torso/legs -> bones 40..70). Palette PACKED as float4x3 in the HLSL:
    // 80*3=240 registers + WVP(4) + 3 lights(3) = 247 <= 256 (vs_2_0). 40 used to truncate the body
    // (bones >=40 out of array -> null matrix -> collapse to origin = triangular artifact).
    // Must equal the size of the mKeyMatrix[] array in the HLSL.
    static constexpr UINT kMaxBones = 80;

    // ----- DRAW PASS (Model_Render 0x40EBB0 a6 / Model_DrawSkinnedSubset 0x40CA40 a3) ------
    // DO NOT CONFUSE with the "shader pass" (g_CurrentShaderPass 0x194591C in {1,2,3,4,8}):
    // these are two DIFFERENT numbering schemes that cross paths in 0x40CA40. Here: the draw
    // pass, bounded `if ((unsigned)(a6-1) <= 1)` @0x40ebd5 -> a6 in {1,2}, which FILTERS meshes
    // by their blendMode:
    //   pass 1: `if (blendMode == 2) return;`  -> everything EXCEPT alpha blend  (0x40cb14..0x40cb20)
    //   pass 2: `if (blendMode != 2) return;`  -> alpha blend ONLY (0x40cb2c..0x40cb32)
    // The binary draws a model by calling TWICE in a row, pass 1 then pass 2: proven at the 4
    // (and only) call sites of Char_RenderModel 0x527020, grouped into two adjacent pairs -
    //   `push 1` @0x51d359 -> call @0x51d361 ; `push 2` @0x51d3c4 -> call @0x51d3cc
    //   `push 1` @0x51d421 -> call @0x51d429 ; `push 2` @0x51d478 -> call @0x51d480
    //   (Scene_CharSelectRender 0x51CED0; a6 transits via SObject_DrawEx 0x4D9330 a2 @0x4d946d)
    // -> the two passes are ADJACENT PER MODEL, this is NOT a global scene sort.
    static constexpr int kDrawPass_Opaque = 1; // a6=1: meshes blendMode != 2
    static constexpr int kDrawPass_Blend  = 2; // a6=2: meshes blendMode == 2

    // ASSUMED SHIM - VALUE ABSENT FROM THE BINARY (a6 never equals 0 there). `kPassBoth` asks
    // DrawModel to do BOTH sweeps ITSELF (1 then 2). For a single model this is EXACTLY the
    // adjacent call pair proven above -> faithful. For a multi-piece assembly (paperdoll), the
    // binary does "all pieces in pass 1, then all pieces in pass 2": there, the caller must
    // sweep explicitly (see DrawModel below). It exists so 5-argument callers stay correct
    // without regressing.
    static constexpr int kPassBoth = 0;

    ~MeshRenderer() { Shutdown(); }

    // Builds the vertex declaration (g_GxdVertexDecl 0x814A58) + compiles the shaders.
    bool Init(Renderer& renderer);
    void Shutdown();
    bool Ready() const { return dev_ != nullptr && decl_ != nullptr && vs_ != nullptr; }

    // Creates the IDirect3DVertexBuffer9/IndexBuffer9 (+ textures) from a parsed .SOBJECT.
    // Reproduces Mesh_ReadFromFile 0x40BC50 (GPU upload side).
    bool Upload(const asset::SObject& src, SkinnedModel& out);

    // ----- Wiring onto the REAL npk shaders (GXDEffect.npk) - additive (W3-F2) -------
    // Switches the skinned path (pass 2) from the reconstructed HLSL kSkinnedVS/kSkinnedPS to
    // the real Shader03 (VS03_SkinnedLit 0x409AB0) + Shader04 (PS04_Tex 0x409CC0) loaded by
    // ShaderSet.cpp from ./GXDEFFECT/GXDEffect.npk. VS15 (0x40ACB0) also serves the stencil
    // shadow volume (DrawModelShadow). `shaders` is NOT owned (lifetime on the caller side).
    // Additive: if never called, DrawSkinnedSubset falls back to the reconstructed HLSL.
    void AttachShaderSet(const ShaderSet* shaders);

    // ----- Runtime shadow parameters (Model_RenderWithShadow 0x40EEE0) - additive ------
    //   enabled  = g_ShadowsEnabled 0x18C4F14
    //   method   = g_ShadowMethod   0x18C4F18 (0 = Carmack z-fail; 1 = stencil two-sided)
    //   fogNear/fogFar = flt_18C4F08 / flt_18C4F0C (volume<->planar threshold + volume fade)
    //   lightDir = flt_18C53C0/C4/C8 (shadow light direction, negated at compute time)
    //
    // FIX (Pass 4 / W5, shadow-wiring front): `enabled`/`method` are NOT menu options - they are
    // FROZEN CONSTANTS. GXD_InitGlobalState 0x401320 is their sole writer (verified by xrefs) and
    // sets g_ShadowsEnabled=1 (0x4013B2) / g_ShadowMethod=1 (0x4013B8); nothing else ever writes
    // them. Also, the only READER of g_ShadowsEnabled (0x40EEEC) lives inside
    // Model_RenderWithShadow 0x40EEE0 - an UNREACHABLE function -> the global is inert.
    //   CLARIFICATION (Pass 4 / W5b): "dead function" was imprecise - 0x40EEE0 does have 3 CALL
    //   SITES (all in SObject_DrawAnimated 0x4D9050). It is dead by UNREACHABILITY:
    //   the caller closure closes at depth 2 on 3 orphaned roots
    //   (Char_DrawWeaponEffectVariantA 0x568FE0 / Npc_DrawMeshShadow 0x5800E0 /
    //   Char_DrawShadow 0x580CE0, 0 xref each) and `reaches(WinMain 0x4609C0 -> 0x40EEE0)` = false.
    // Corollary: `method==0` (Carmack z-fail, 0x40F671) is UNREACHABLE BY VALUE (the sole
    // writer sets 1). This function documents the binary, it does not drive it: DrawModelShadow()
    // deliberately stays without a caller on the C++ side.
    // Do NOT wire it to the UI option g_Opt_GfxDetailShadows 0x84DEF8: this option does exist
    // (UI_OptionsWnd_OnClick 0x66D140) but it toggles no shadow at all despite its name.
    //   CLARIFICATION (Pass 4 / W5b) - the global has 16 xrefs, not a single reader:
    //     * 4 WRITERS, all in UI_OptionsWnd_OnClick 0x66D140: @0x66DF5B (`sub ecx,1` -> store),
    //       @0x66DF63 (clamp = 0), @0x66DFDB (`add ecx,1` -> store), @0x66DFEA (clamp = 1)
    //       -> option clamped to [0,1] (2-state toggle), persisted by Options_SaveBin 0x4C2280.
    //     * 12 READERS: 0x51B54F, 0x51B8C3, 0x51B8CD, 0x55B351, 0x580840, 0x580E3A, 0x5811EA,
    //       0x581990, 0x66DF52, 0x66DFD2, 0x66DFE1, 0x66F43E.
    //   0x5811EA is ONE of these 12 readers, and it lives INSIDE the LIVE shadow path
    //   (Char_DrawReflection 0x581090, called by Scene_InGameRender @0x52DB09, inside the
    //   0x52D9DC..0x52DB15 shadow bracket): it keeps the branch choice there, but what it
    //   selects is a MODEL VARIANT based on HP (`unk_FC7A8C + 144*type + 36*(3 - hp%/30)`)
    //   - it is not a shadow toggle.
    void SetShadowParams(bool enabled, int method, float fogNear, float fogFar,
                         const D3DXVECTOR3& lightDir);

    // Camera: Object B's view/projection matrices (+748 / +648).
    void SetCamera(const D3DXMATRIX& view, const D3DXMATRIX& proj);

    // World directional light (light @+1120: Direction/Ambient/Diffuse).
    void SetLight(const D3DXVECTOR3& dirWorld,
                  const D3DXVECTOR3& ambient,
                  const D3DXVECTOR3& diffuse);

    // ----- Accessors for the PLANAR SHADOW pass (Scene layer) - additive F_ENTITY3D ------
    // Shadow projection direction = flt_18C53C0/C4/C8 cache, derived from the light by
    // GXD_SetupStencilShadowState 0x404F20 @0x404F26..0x404F62. The Scene layer queries the
    // ground plane (WorldAssets::GetGroundPlaneForShadow) with THIS direction, the exact same one
    // DrawModelPlanarShadow injects (negated) into D3DXMatrixShadow -> pick/projection consistency.
    const D3DXVECTOR3& ShadowLightDir() const { return shadowLightDir_; }
    // Diffuse light color (light+4 = this+1124/1128/1132): SOURCE of the shadow TEXTUREFACTOR's
    // alpha (GXD_SetupStencilShadowState @0x405027..0x40507F: (r+g+b)/3 x128 <<24).
    const D3DXVECTOR3& LightDiffuse() const { return lightDiffuse_; }

    // Model_Render 0x40EBB0: composes world = Scale*RotZ*RotY*RotX*Translate
    // (Euler angles in degrees) then draws all meshes at the requested LOD.
    //
    // `pass` (= a6): see kDrawPass_Opaque/kDrawPass_Blend/kPassBoth above.
    // WIRING TO BE DONE OUTSIDE THIS FILE (Scene/WorldRenderer.cpp, not owned by this front) -
    //   the player paperdoll (WorldRenderer.cpp:412-414) loops over `pd.pieces` calling DrawModel
    //   per piece. The binary, meanwhile, draws ALL pieces in pass 1 then ALL in pass 2
    //   (Char_RenderModel 0x527020 assembles the paperdoll piece by piece and receives the pass
    //   as a parameter from 0x51d359/0x51d3c4). The faithful equivalent is therefore TWO loops:
    //       for (piece : pd.pieces) DrawModel(*piece, pos, rotDeg, scaleVec, pd.palette, 0,
    //                                         gfx::MeshRenderer::kDrawPass_Opaque);
    //       for (piece : pd.pieces) DrawModel(*piece, pos, rotDeg, scaleVec, pd.palette, 0,
    //                                         gfx::MeshRenderer::kDrawPass_Blend);
    //   Until this is done, kPassBoth (default) renders both passes per piece: nothing
    //   disappears, only the inter-piece ORDER differs (a translucent piece can be covered by
    //   an opaque piece drawn after it, blend 2 cutting ZWRITE @0x40cbf5).
    //   Single-piece models (WorldRenderer.cpp:310 and :421, monster/NPC) are ALREADY
    //   exact under kPassBoth: nothing to change there.
    void DrawModel(const SkinnedModel& model,
                   const D3DXVECTOR3&  position,
                   const D3DXVECTOR3&  rotationDeg,
                   const D3DXVECTOR3&  scale,
                   const BonePalette&  palette,
                   int                 lod  = 0,
                   int                 pass = kPassBoth);

    // Model_DrawSkinnedSubset 0x40CA40 (main skinned path, one mesh/LOD):
    // pass filter, blend states by blendMode, shader pass selection (2/3/4),
    // shader bind, bone palette -> mKeyMatrix, WVP/light, textures,
    // SetStreamSource(76)/SetIndices, DrawIndexedPrimitive.
    //
    // `matSrc` (= a1, material override): if non-null, the GEOMETRY stays `mesh`'s own but
    //   the 3 materials (and hence blendMode + the multi-texture pass choice) come from `matSrc`.
    //   Reception proven @0x40ca96..0x40cabc: `if (a1) { v61 = a1; v62 = a7; v56 = a8; }`.
    //   Used for inheritance from mesh[0] (see DrawModel / gap SOBJ-05).
    // `pass`: kPassBoth (default) = NO filter - preserves direct callers' behavior
    //   (Gfx/WorldGeometryRenderer.cpp:1061, world geometry, 4-argument call).
    void DrawSkinnedSubset(const SkinnedMesh& mesh, int lod,
                           const D3DXMATRIX&  world,
                           const BonePalette& palette,
                           const SkinnedMesh* matSrc = nullptr,
                           int                pass   = kPassBoth);

    // Model_RenderWithShadow 0x40EEE0: shadow rendering for a skinned model (additive, W3-F2).
    //   NEAR (camera dist <= fogNear) -> STENCIL SHADOW VOLUME (pass 8 = VS15 + PS NULL):
    //     Model_BuildShadowVolume 0x40DC70 (extruded silhouette, FVF XYZ), z-fail if method==0.
    //   FAR  (dist > fogNear) -> projected planar shadow (Model_RenderPlanarShadow 0x40F720).
    //   The test direction is confirmed by Hex-Rays: `if (flt_18C4F08 >= dist)` (0x40EFC6) -> VOLUME
    //   branch; `else` (0x40EFDE) -> PLANAR branch calling 0x40F720 @0x40F1A9.
    //
    // FUNCTION NEVER CALLED - AND DOUBLY INERT (Pass 4 / W5, shadow-wiring front):
    //   (a) The whole 0x40EEE0/0x40DC70 chain is DEAD CODE in the binary (0 xref on the
    //       3 chain heads; 0 occurrence of the addresses in LE in the image -> no indirect call).
    //       Full detail + proofs in MeshRenderer_Shadow.cpp above DrawModelShadow().
    //   (b) The "FAR -> planar" branch is furthermore DEAD BY VALUE: fogNear = flt_18C4F08
    //       = 999999.0 (frozen by GXD_InitGlobalState @0x40137E, sole writer), so `dist > fogNear`
    //       never happens for a game camera -> only the VOLUME branch would ever be taken if the
    //       function were live. Do NOT confuse this (dead) planar with the game's REAL live
    //       planar shadow, which reaches 0x40F720 via the other chain (SObject_DrawAnimated2 0x4D91C0)
    //       and uses PASS 5 = VS09 (g_GxdSh09_VS), not VS15/pass 8.
    //   The `if (!shaderSet_) return` below is therefore NOT the lock preventing the shadow:
    //   attaching a ShaderSet unblocks nothing, there is no caller to unblock.
    //   `boundRadius` = a2 (bounding diameter).
    //   Pass 4 / W5b (shadow-fidelity front): the extrusion length a9 is DERIVED from it, and
    //   is not a free parameter - a9 = a2 x 2.5, proven at the 3 (and only) call sites of
    //   0x40EEE0 in SObject_DrawAnimated 0x4D9050 (@0x4D90C6/@0x4D9129/@0x4D9178: `a5 * 2.5`).
    //   DrawModelShadow therefore applies `boundRadius * 2.5f` internally: do NOT pre-multiply it
    //   on the caller side should this function ever get wired up.
    void DrawModelShadow(const SkinnedModel& model,
                         const D3DXVECTOR3&  position,
                         const D3DXVECTOR3&  rotationDeg,
                         const D3DXVECTOR3&  scale,
                         const BonePalette&  palette,
                         float               boundRadius);

    // Model_RenderPlanarShadow 0x40F720 - REAL projected planar shadow (LIVE chain [B],
    // reached from Scene_InGameRender 0x52D0B0 via SObject_DrawAnimated2 0x4D91C0;
    // DISTINCT from the dead 0x40EEE0 chain that DrawModelShadow above reproduces).
    // Flattens the skinned mesh onto the ground plane via j_D3DXMatrixShadow @0x40FB28, in PASS 5 =
    // VS09 (g_GxdSh09_VS 0x1945B18, via ShaderSet) + NULL PixelShader; the color is written by
    // the state bracket's TEXTUREFACTOR/TSS (GXD_SetupStencilShadowState 0x404F20), set by the
    // Scene layer AROUND the calls.
    //   `groundShadowPlane` = {a, b, c, -d-0.1} ALREADY READY - it is collision::GroundPlane::shadowPlane
    //     (v45 of 0x40F720 @0x40FACE..0x40FAFC), computed by the Scene layer from the world's
    //     collision geometry. The Gfx renderer RECEIVES the plane, it does NOT compute it (must
    //     not depend on World here).
    //   The light direction comes from the shadowLightDir_ member (negated -> light4 {-dir,0} =
    //     directional light, v38..v41 @0x40FB08..0x40FB24).
    //   Distance-based LOD B7 NOT implemented BY DESIGN: the fade v37 of 0x40F720 saturates to 1.0
    //     (fogNear/fogFar = 999999/1000000) -> LOD 0 systematically; lod=0 is ALREADY faithful
    //     (see Scene/WorldRenderer.h Sec LOD). mesh.lods[0] is drawn (like DrawModel lod=0), so
    //     the shadow silhouette = exactly the body geometry.
    //   Clean no-op if no real ShaderSet (VS09 absent from the reconstructed HLSL) or empty model:
    //     the shadow is not drawn rather than an infidel render.
    void DrawModelPlanarShadow(const SkinnedModel& model,
                               const D3DXVECTOR3&  position,
                               const D3DXVECTOR3&  rotationDeg,
                               const D3DXVECTOR3&  scale,
                               const BonePalette&  palette,
                               const float         groundShadowPlane[4]);

    // FIXED BUG (audit 2026-07-14, see Gfx/WorldGeometryRenderer.cpp::Render()):
    // DrawSkinnedSubset() avoids redundant VS/PS re-binds via `currentPass_`, a
    // cache PURELY LOCAL to this instance. If EXTERNAL code directly sets
    // IDirect3DDevice9::SetVertexShader(nullptr)/SetPixelShader(nullptr) on the SAME
    // shared device (e.g. Gfx/SkyRenderer.h, which draws a fullscreen quad in
    // fixed-function BEFORE every geometry frame), this cache goes stale from the
    // 2nd frame on: `currentPass_` stays at `kPass_SkinnedLit` while the real device
    // no longer has any VS/PS bound -> the next DrawIndexedPrimitive() runs with no
    // shaders, silently. Call this after ANY external code that touches
    // SetVertexShader/SetPixelShader/SetFVF on the shared device, before the
    // next DrawSkinnedSubset()/DrawModel().
    void InvalidateShaderBindingCache() { currentPass_ = 0; }

private:
    bool buildVertexDeclaration();
    bool compileSkinnedProgram();
    static IDirect3DTexture9* createDiffuse(IDirect3DDevice9* dev,
                                            const asset::SObjectTexture& tex);

    // ONE sweep of a model's meshes for ONE draw pass (== body of the
    // `for (i = 0; i < v11[1]; ++i)` loop of Model_Render 0x40EBB0 @0x40ee02..0x40eebf, stride 888).
    // Carries material inheritance from mesh[0] (gap SOBJ-05).
    void drawMeshSweep(const SkinnedModel& model, const D3DXMATRIX& world,
                       const BonePalette& palette, int lod, int pass);

    // Per-material blend states (Model_DrawSkinnedSubset: v14 = blendMode).
    void applyBlendMode(uint32_t blendMode);
    void resetBlendMode(uint32_t blendMode);

    // CPU skinning + extruded silhouette of one LOD (Model_BuildShadowVolume 0x40DC70).
    // Fills shadowVol_ (interleaved XYZ, stride 12) + shadowVolVertCount_; returns false if
    // CPU data is missing, LOD > 10000 vtx/faces, or overflow (>29976 vertices emitted).
    bool buildShadowVolume(const SkinnedLod&  lod,
                           const BonePalette& palette,
                           const D3DXVECTOR3& lightDirObj,
                           float              extrude);

    IDirect3DDevice9*            dev_  = nullptr;
    IDirect3DVertexDeclaration9* decl_ = nullptr; // g_SkinVertexDecl (0x1945918) - ex-VeryOldClient: mDECLForSKIN2 (target = file-scope global, NOT a member)

    // Skinned shader slot (reproduces GXD_ShaderSlot: VS/PS + ID3DXConstantTable + handles).
    // ex-VeryOldClient: mAmbient2_VS_Shader / mAmbient2_PS_Shader (+ _ConstantTable), Shader03/04.
    IDirect3DVertexShader9* vs_    = nullptr; // g_GxdSh03_VS 0x1945970 - ex-VeryOldClient: mAmbient2_VS_Shader
    IDirect3DPixelShader9*  ps_    = nullptr; // g_GxdSh04_PS 0x194598C - ex-VeryOldClient: mAmbient2_PS_Shader
    LPD3DXCONSTANTTABLE     ctVs_  = nullptr; // g_GxdSh03VS_CT 0x194596C - ex-VeryOldClient: mAmbient2_VS_ConstantTable
    LPD3DXCONSTANTTABLE     ctPs_  = nullptr; // g_GxdSh04PS_CT 0x1945988 - ex-VeryOldClient: mAmbient2_PS_ConstantTable
    D3DXHANDLE hKeyMatrix_       = nullptr; // mKeyMatrix[kMaxBones] - g_GxdSh03_hKeyMatrix 0x1945974 - ex-VeryOldClient: mAmbient2_VS_KeyMatrix
    D3DXHANDLE hWorldViewProj_   = nullptr; // mWorldViewProjMatrix - ex-VeryOldClient: mAmbient2_VS_WorldViewProjMatrix
    D3DXHANDLE hLightDirection_  = nullptr; // mLightDirection (object space) - ex-VeryOldClient: mAmbient2_VS_LightDirection
    D3DXHANDLE hLightAmbient_    = nullptr; // mLightAmbient - ex-VeryOldClient: mAmbient2_VS_LightAmbient
    D3DXHANDLE hLightDiffuse_    = nullptr; // mLightDiffuse - ex-VeryOldClient: mAmbient2_VS_LightDiffuse
    UINT       sampler0_         = 0;       // mTexture0 register - ex-VeryOldClient: mAmbient2_PS_Texture0

    D3DXMATRIX  view_;
    D3DXMATRIX  proj_;
    D3DXMATRIX  viewProj_;
    // World camera position (derived from the inverse of view_) - g_CameraEye dword_18C51C0/C4/C8,
    // used by DrawModelShadow to choose volume vs planar shadow (0x40ef8e).
    D3DXVECTOR3 eye_ = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    // ex-VeryOldClient: mLight (v2 / Object B @+1120) - Amb 0.3 / Diff 0.7 / Dir (-1,-1,1).
    // PLAUSIBLE (P-9): floats corroborated by v2; proven bit-exact against IDA at the device chunk
    // (0x402711). v1/v2 discriminant: indeed 0.3/0.7 (v2), NOT 0.4/0.5 (v1).
    D3DXVECTOR3 lightDirWorld_ = D3DXVECTOR3(-1.0f, -1.0f, 1.0f);
    D3DXVECTOR3 lightAmbient_  = D3DXVECTOR3(0.3f, 0.3f, 0.3f);
    D3DXVECTOR3 lightDiffuse_  = D3DXVECTOR3(0.7f, 0.7f, 0.7f);

    // g_CurrentShaderPass (0x194591C): avoids re-binding VS/PS between identical subsets.
    // ex-VeryOldClient: mPresentShaderProgramNumber (PLAUSIBLE, P-12 - offset 0x194591C not
    //   formally named in gxd_findings; not to be confused with currentPassId Object B+526884).
    int currentPass_ = 0;

    // Fallback (identity) palette if no valid palette is supplied.
    D3DXMATRIX identityPalette_[kMaxBones];

    // GX-BONEPAD-01: palette scratch PADDED to kMaxBones. The player skeleton has 76 bones, but a
    // multi-mesh armor (e.g. C003035) can reference a bone >= 76 in its BLENDINDICES. Without
    // padding, mKeyMatrix[76..79] keeps STALE matrices (previous entity) -> scattered vertices +
    // degenerate (dark) normals. The real palette is copied into [0,count) and [count,kMaxBones) is
    // filled with identity (assembled rest pose) before SetMatrixArray(kMaxBones).
    D3DXMATRIX paletteScratch_[kMaxBones];

    // ----- Real npk shader wiring (AttachShaderSet) - additive W3-F2 ------------------
    // Real shader slots loaded from the npk (Shader03/04/15). Not owned. If nullptr -> HLSL fallback.
    const ShaderSet* shaderSet_ = nullptr;
    // Real bound of the mKeyMatrix[] array declared by Shader03.fx (D3DXCONSTANT_DESC.Elements);
    // Model_DrawSkinnedSubset does NOT clamp client-side (0x40d4e8) -> the shader is what bounds it.
    UINT boneArraySize_ = kMaxBones;

    // ----- Runtime shadow state (Model_RenderWithShadow 0x40EEE0) - additive -------------
    // FIDELITY FIX (Pass 4 / W5b, shadow-fidelity front): these defaults used to CONTRADICT the
    // anchors they cited. They now carry the binary's values, set by GXD_InitGlobalState 0x401320
    // (sole writer, verified by xrefs).
    // NB: the `1` for enabled/method is NOT an immediate - it transits via ebx
    //     (`mov ebx, 1` @0x401365, then `mov ds:g_XXX, ebx`).
    bool        shadowsEnabled_ = true;      // g_ShadowsEnabled 0x18C4F14 = 1 (ebx @0x401365 ; store @0x4013B2)
    int         shadowMethod_   = 1;         // g_ShadowMethod   0x18C4F18 = 1 (ebx @0x401365 ; store @0x4013B8)
                                             //   -> consistent with "method==0 UNREACHABLE BY VALUE" above.
    float       fogNear_        = 999999.0f;  // flt_18C4F08 <- flt_7EDBD8 (0x497423F0) @0x401378/0x40137E
    float       fogFar_         = 1000000.0f; // flt_18C4F0C <- flt_7EDB80 (0x49742400) @0x40138A/0x401396
    // WARNING - DERIVED value, NOT a constant read from the binary (the previous comment
    // "flt_18C53C0/C4/C8" implied otherwise, and gave (0,-1,0), which this anchor does NOT
    // produce). flt_18C53C0/C4/C8 is a CACHE recomputed every frame by
    // GXD_SetupStencilShadowState 0x404F20 @0x404F26..0x404F62 from the world light; no writer
    // shows up in absolute xrefs because the write is esi-relative (esi = this =
    // g_GxdRenderer 0x18C4EF8 -> esi+4C8h = 0x18C53C0). Derivation (see Scene/WorldRenderer.h):
    //     shadowLightDir = normalize( normalize(L.x, 0, L.z) then .y := -1 )
    // Applied to lightDirWorld_ = (-1,-1,1) (see above) -> (-0.5, -1/sqrt(2), 0.5).
    // Structural property: since the 1st normalization makes the horizontal component unit
    // length, the norm before the 2nd is ALWAYS sqrt(2) -> y == -1/sqrt(2): a 45-degree
    // downward direction, regardless of the sun's position. (0,-1,0) would only result from
    // L.x == L.z == 0 (D3DXVec3Normalize of the null vector) - a case NOT proven.
    D3DXVECTOR3 shadowLightDir_ = D3DXVECTOR3(-0.5f, -0.70710678f, 0.5f);

    // Shadow volume scratch buffers (original globals -> members, same caps):
    //   worldPos_        = world-space skinned positions, stride 3 (flt_18C69D4)
    //   faceLightFacing_ = 1 byte/face: is the face lit? (g_FaceLightFacing 0x18E3E94)
    //   shadowVol_       = interleaved shadow XYZ vertices, stride 3 (g_ShadowVolumeX 0x18EDAD8)
    //   shadowVolVertCount_ = number of vertices emitted (g_ShadowVolumeVertCount 0x18EDAD4)
    std::vector<float>   worldPos_;
    std::vector<uint8_t> faceLightFacing_;
    std::vector<float>   shadowVol_;
    UINT                 shadowVolVertCount_ = 0;
};

} // namespace ts2::gfx

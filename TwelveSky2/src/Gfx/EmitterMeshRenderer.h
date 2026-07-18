// Gfx/EmitterMeshRenderer.h — GPU upload + drawing of a .MOBJECT2 effect mesh (emitter mesh).
//
// Consumes asset::Mobject2 (FLEET A, already committed: Asset/Mobject2.h) and reproduces the
// original render `Mesh_DrawAnimatedFrame 0x430BE0` (animated flipbook, QPC timer, glow/UV/billboard/alpha).
// STANDALONE MODULE: no writes into the render loop, no hooks. The D3D9 device and world
// matrix are supplied BY THE CALLER (FLEET C will wire it). VB/IB/textures in D3DPOOL_MANAGED
// -> survive a Reset(): OnDeviceLost/OnDeviceReset are no-ops (like WorldGeometryRenderer
// / ModelObjectRenderer / MeshRenderer).
//
// ============================ IDA ANCHORS (sole source of truth, imagebase 0x400000) ===================
//   Mesh_DrawAnimatedFrame 0x430BE0  — render CORE (decompiled/disassembled on 2026-07-17):
//       __userpurge(alpha@eax, overrideTexHolder@ecx, mesh@a3, pass@a4, frame@a5, lod@a6, phase@a7)
//   Mesh_DrawInstancesLOD  0x431A90  — caller (frustum-cull + LOD distance + Rz·Ry·Rx·T·S matrix;
//       sets g_WorldMatrix 0x18C52D4 then calls Mesh_DrawAnimatedFrame per instance).
//   GXD_SetDirectionalLight 0x403980 — glow helper (D3DLIGHT9: Ambient=color, Type=DIRECTIONAL).
//   Mesh_ReadFile 0x430470 / Mesh_LoadMOBJECT2 0x4318C0 — loader (VB 20·N·vc FVF 258 @0x430897,
//       IB 6·fc INDEX16 @0x430A03, tex SOBJECT 56 o @0x430A80) — already ported (asset::Mobject2).
//   Vec3_TransformCoord 0x6BB612 (= D3DXVec3TransformCoord) ; QPC timer dbl_18C4F80/88 (freq/start).
//
// ============================ RESOLUTION OF A FLEET-A RESIDUAL ============================
//   Docs/TS2_DEEP_MOBJECT.md §3.3/§T5 and Asset/Mobject2.h flagged the `20·N` multiplier
//   (N = a1[21]) as "UNPROVEN semantics". `Mesh_DrawAnimatedFrame` PROVES it:
//     - per-frame VB stream offset = `20 * frame * vertexCount[i]`  (@0x431520 GXD / @0x4314BE CPU)
//     - bbox/anchor block offset   = `40 * frame`                   (@0x431207)
//   -> N IS the flipbook's FRAME COUNT (each frame = a contiguous block of `vertexCount` 20-byte
//     vertices). The VB holds N frames; frame selection = SetStreamSource offset, NEVER
//     skinning. The "N==1" fallback is thus no longer needed — we handle N frames faithfully (and
//     N==1 naturally degenerates to a static mesh). Semantics unchanged for the other tables (boneTable
//     40·N = 1 bbox+anchor record PER FRAME, header2 80 o = billboard quad template).
#pragma once
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Asset/Mobject2.h" // asset::Mobject2 / Mobject2Mesh / Mobject2Subset / SObjectTexture (FLEET A)
#include "Gfx/GpuTexture.h"  // gfx::GpuTexture (DDS -> IDirect3DTexture9 bridge, D3DPOOL_MANAGED)

namespace ts2::gfx {

//  GPU (cache) representation of a MOBJECT2 mesh. Move-only (owns VB/IB/textures).

// A texture holder = 1 GPU texture + its blend mode (holder+44 = SObjectTexture::alphaMode).
// In the binary, Mesh_DrawAnimatedFrame reads `*(v43+44)` (blendMode 0/1/2) and `*(v43+52)` (D3D
// texture) on the selected holder (mainTex if not animated, extraTex[idx] if header1[0]==1).
struct EmitterTexHolder {
    GpuTexture tex;              // GPU texture (empty if absent) — a1[51]/a1[66] via Tex_ReadPacked
    uint32_t   blendMode = 0;    // holder+44 : 0=opaque, 1=alpha-test+blend, 2=additive
    bool       present   = false;

    EmitterTexHolder() = default;
    EmitterTexHolder(EmitterTexHolder&&) noexcept = default;
    EmitterTexHolder& operator=(EmitterTexHolder&&) noexcept = default;
    EmitterTexHolder(const EmitterTexHolder&) = delete;
    EmitterTexHolder& operator=(const EmitterTexHolder&) = delete;
};

// A render subset (= a LOD level in Mesh_DrawAnimatedFrame: the caller selects
// ONLY ONE via the LOD factor a6 — cf. loop @0x430DAC). Each VB holds N frames.
// Move-only, OWNS its VB/IB (released in the destructor -> clean clear()/Reset, zero leak).
struct EmitterGpuSubset {
    IDirect3DVertexBuffer9* vb          = nullptr; // 20·N·vertexCount o, FVF 258, MANAGED (a1[47] @0x430897)
    IDirect3DIndexBuffer9*  ib          = nullptr; // 6·faceCount o, INDEX16, MANAGED (a1[50] @0x430A03)
    uint32_t                vertexCount = 0;        // vertices PER FRAME (a1[45][i])
    uint32_t                faceCount   = 0;        // triangles = primCount (a1[48][i])

    EmitterGpuSubset() = default;
    ~EmitterGpuSubset() { if (vb) vb->Release(); if (ib) ib->Release(); }
    EmitterGpuSubset(const EmitterGpuSubset&)            = delete;
    EmitterGpuSubset& operator=(const EmitterGpuSubset&) = delete;
    EmitterGpuSubset(EmitterGpuSubset&& o) noexcept
        : vb(o.vb), ib(o.ib), vertexCount(o.vertexCount), faceCount(o.faceCount) {
        o.vb = nullptr; o.ib = nullptr;
    }
    EmitterGpuSubset& operator=(EmitterGpuSubset&& o) noexcept {
        if (this != &o) {
            if (vb) vb->Release();
            if (ib) ib->Release();
            vb = o.vb; ib = o.ib; vertexCount = o.vertexCount; faceCount = o.faceCount;
            o.vb = nullptr; o.ib = nullptr;
        }
        return *this;
    }
};

// A mesh of the container (GPU mirror of asset::Mobject2Mesh, originally 268 o).
struct EmitterGpuMesh {
    bool     valid      = false; // false if empty mesh (type==0)
    uint32_t frameCount = 1;     // N = a1[21] (RESOLVED: frame count — cf. .h banner)

    // --- decoded header1 (76 o, a1[2..20]) — flags read by Mesh_DrawAnimatedFrame ---
    uint32_t animatedTex       = 0; // header1[0]  (mesh+8)  : ==1 => texture taken from extraTex[] (flipbook)
    int32_t  animTexSpeed      = 0; // header1[1]  (mesh+12) : speed (×0.01) of temporal selection
    int32_t  texMinFrame       = 0; // header1[2]  (mesh+16) : <1 => temporal index ; >=1 => mapped onto [min,max]
    int32_t  texMaxFrame       = 0; // header1[3]  (mesh+20)
    uint32_t glowEnable        = 0; // header1[4]  (mesh+24) : ==1 => animated glow light (GXD_SetDirectionalLight)
    int32_t  glowSpeed         = 0; // header1[5]  (mesh+28) : glow phase (×0.01)
    int32_t  glowFrom[3]       = {};// header1[6..8]  (mesh+32/36/40) : "from" color (×0.01)
    int32_t  glowTo[3]         = {};// header1[10..12](mesh+48/52/56) : "to" color  (×0.01)   (header1[9] unused)
    uint32_t uvEnable          = 0; // header1[14] (mesh+64) : ==1 => texture matrix scroll
    uint32_t uvMode            = 0; // header1[15] (mesh+68) : 1=U, 2=V, 3=UV, else=U/-V
    int32_t  uvSpeed           = 0; // header1[16] (mesh+72) : speed (×0.01)
    uint32_t billboardEnable   = 0; // header1[17] (mesh+76) : ==1 => camera-facing quad (DrawPrimitiveUP)
    uint32_t billboardAxisMode = 0; // header1[18] (mesh+80) : ==1 => symmetric square (width=height)

    std::vector<EmitterGpuSubset> subsets;

    // bbox/anchor block PER FRAME (40·N o, a1[22] / mesh+88). Per frame: min.xyz(12) max.xyz(12)
    // center.xyz(12) radius(4). The billboard reads center (offset+24) + extents to size the quad.
    std::vector<uint8_t> frameBbox;
    // parallel table 4·N (a1[23] / mesh+92): 1 float PER FRAME. Read ONLY in the additive pass
    // (a4!=1, blendMode 2) to modulate alpha: v7 = frameScale[frame] * (255 - alpha)  @0x430D20.
    std::vector<uint8_t> frameScale;
    // header2 (80 o, a1[24..43] / mesh+96..176): USED AS TEMPLATE for the billboard quad — the UVs are
    // baked in; only the xyz positions of the 4 corners are rewritten per frame (@0x431286..0x431378).
    std::vector<uint8_t> billboardTemplate;

    EmitterTexHolder              mainTex;   // a1[51] (mesh+204) : default texture (non-animated case)
    std::vector<EmitterTexHolder> extraTex;  // a1[66] (mesh+264) : 56·extraTexCount (animated case)

    EmitterGpuMesh() = default;
    EmitterGpuMesh(EmitterGpuMesh&&) noexcept = default;
    EmitterGpuMesh& operator=(EmitterGpuMesh&&) noexcept = default;
    EmitterGpuMesh(const EmitterGpuMesh&) = delete;
    EmitterGpuMesh& operator=(const EmitterGpuMesh&) = delete;
};

// GPU representation of an entire .MOBJECT2 container (array of meshes).
struct EmitterGpuObject {
    std::vector<EmitterGpuMesh> meshes;
    bool ok = false;

    EmitterGpuObject() = default;
    EmitterGpuObject(EmitterGpuObject&&) noexcept = default;
    EmitterGpuObject& operator=(EmitterGpuObject&&) noexcept = default;
    EmitterGpuObject(const EmitterGpuObject&) = delete;
    EmitterGpuObject& operator=(const EmitterGpuObject&) = delete;
};

//  Draw parameters — EXACT mirror of Mesh_DrawAnimatedFrame 0x430BE0's arguments.
struct EmitterMeshDrawArgs {
    // World matrix (g_WorldMatrix 0x18C52D4). Built by the caller (Mesh_DrawInstancesLOD:
    // Rz(z°)·Ry(y°)·Rx(x°)·T(pos)·S). Set by SetTransform(D3DTS_WORLD) and used for the billboard anchor.
    D3DXMATRIX world;

    float   frame     = 0.0f; // a5 : frame index (truncated by ftol -> v51)
    int     pass      = 1;    // a4 : ==1 => blendMode 0/1 ; !=1 => blendMode 2 (2 passes: opaque then additive)
    float   lodFactor = 1.0f; // a6 : >=1.0 => max LOD (subset 0) ; else selection by face count
    float   timePhase = 0.0f; // a7 : phase added to the timer (v50 = elapsedTime + a7)
    uint8_t alpha     = 255;  // eax : alpha byte (v7) — modulates TEXTUREFACTOR / additive alpha

    // ecx (a2): override texture holder. nullptr => uses the mesh's texture (mainTex/extraTex).
    const EmitterTexHolder* overrideTex = nullptr;

    // Billboard camera basis (flt_18C5264 for axisMode==1, unk_18C52BC otherwise): right.xyz + up.xyz.
    // Standalone: the caller supplies these axes from its camera. Identity fallback (right=X, up=Y) =
    // world-aligned quad (visible but not camera-facing) — documented, never invented.
    float billboardBasisAxis1[6] = {1.f, 0.f, 0.f,  0.f, 1.f, 0.f};
    float billboardBasisOther[6] = {1.f, 0.f, 0.f,  0.f, 1.f, 0.f};

    // Scene ambient for the "reset" glow path (GXD_SetDirectionalLight mode 1, source
    // a1+1124.. of the GXD singleton, absent standalone). White fallback. Only affects glowEnable!=1.
    float sceneAmbient[3] = {1.f, 1.f, 1.f};
};

//  EmitterMeshRenderer — cache + drawing. Standalone (device supplied by the caller).
class EmitterMeshRenderer {
public:
    EmitterMeshRenderer() = default;
    ~EmitterMeshRenderer();
    EmitterMeshRenderer(const EmitterMeshRenderer&) = delete;
    EmitterMeshRenderer& operator=(const EmitterMeshRenderer&) = delete;

    // Uploads an already-parsed .MOBJECT2 container (asset::Mobject2) to a standalone GPU object.
    // VB/IB (MANAGED) + textures (GpuTexture, MANAGED). Returns false if `dev` is null (out.ok=false).
    // An empty mesh (type==0) is kept but marked invalid (not drawn) — parity with the binary.
    bool Upload(IDirect3DDevice9* dev, const asset::Mobject2& src, EmitterGpuObject& out) const;

    // Lazy cache by path: parse (asset::Mobject2::Load) + Upload on first access.
    // Returns nullptr if device null, parse failed (memoized), or upload failed. Owned by this cache.
    EmitterGpuObject* GetOrLoad(IDirect3DDevice9* dev, const std::string& path);
    void ReleaseCache();
    size_t ResidentCount() const { return cache_.size(); }

    // D3DPOOL_MANAGED: nothing to recreate after Reset(). Documented no-op (symmetric with the other renderers).
    void OnDeviceLost()  {}
    void OnDeviceReset() {}

    // Draws ONE mesh (byte-exact mirror of Mesh_DrawAnimatedFrame 0x430BE0). `dev` = current device.
    void DrawMesh(IDirect3DDevice9* dev, const EmitterGpuMesh& mesh, const EmitterMeshDrawArgs& args);

    // Draws all valid meshes of an object (same args). Empty meshes are skipped.
    void DrawObject(IDirect3DDevice9* dev, const EmitterGpuObject& obj, const EmitterMeshDrawArgs& args);

private:
    // QPC timer (dbl_18C4F80 freq / dbl_18C4F88 start). Lazily initialized on the first draw.
    // v50 = (count - start)/freq + phase  (= elapsed seconds + phase).
    double ElapsedSeconds() const;

    // Uploads a single mesh (helper for Upload). `dev` non-null guaranteed by the caller.
    bool uploadMesh(IDirect3DDevice9* dev, const asset::Mobject2Mesh& src, EmitterGpuMesh& out) const;
    static bool uploadTexture(IDirect3DDevice9* dev, const asset::SObjectTexture& src, EmitterTexHolder& out);

    mutable bool           timerInit_  = false;
    mutable long long      freq_       = 0; // QueryPerformanceFrequency
    mutable long long      startCount_ = 0; // QueryPerformanceCounter on the first draw

    std::unordered_map<std::string, EmitterGpuObject> cache_;      // key = path
    std::unordered_map<std::string, bool>             loadFailed_; // failed parse/upload (do not retry)
};

} // namespace ts2::gfx

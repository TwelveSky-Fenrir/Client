// Gfx/MeshPartMaterial.h — fixed-function material state machine for GXD static-object
// "parts" (.MOBJECT / .WO), LINE-BY-LINE port of MeshPart_RenderFull 0x6B0850.
//
// FRONT B1:meshpart-material (2026-07-17). SOLE ground truth = IDA (idaTs2, TwelveSky2.exe,
// imagebase 0x400000). Each block of the .cpp carries its 0xADDR anchor. No IDB write access.
// Byte-exact recon: Docs/TS2_DEEP_MESHPART_MATERIAL.md (§4 execution order, §5 helpers,
// §6 decode tables) + Docs/TS2_DEEP_MOBJECT.md (§1 MeshPart 408 B layout, §4 sub-paths).
//
// ===========================================================================================
//  ROLE — what this module reproduces
// ===========================================================================================
// MeshPart_RenderFull 0x6B0850 draws ONE part (sub-mesh) of a static decor object
// with its FULL material: up to 9 fixed-function layers stacked by ONE state machine.
// The "base-draw" (SetStreamSource(0,VB,32*frame*B,32) + SetIndices + DrawIndexedPrimitive,
// anchor @0x6B1327) is ALREADY ported (ModelObjectRenderer.cpp / WorldGeometryRenderer.cpp); this
// module reproduces the layers BEYOND the base-draw and re-includes the base-draw to be a
// complete drop-in replacement (cf. Docs/TS2_DEEP_MOBJECT.md §7 T4/T5 —
// "replace the base draw with MeshPartMaterial::Render(...)").
//
//   (1) animated emissive light (triangular ping-pong)     @0x6B08AF  gate mat.lightAnim.Enable
//   (1b) light 0 neutralization                             @0x6B099B  gate mat.noLight
//   (2) view-dependent specular glow (D3DMATERIAL9.Specular + light dir. toward camera + RS29)
//                                                          @0x6B0A11 / re-glow @0x6B168D  gate mat.glow
//   (3) animated flipbook texture (atlas, time-based selection) @0x6B0D33  gate mat.flipbook.Enable
//   (4) UV-scroll texture matrix tex1 @0x6B0F59 / tex2 @0x6B19BB   gate mat.uvScroll.texN.Enable
//   (5) 2nd texture (2nd blended pass, same geometry)       @0x6B19AD  gate (tex.second != nullptr)
//   + projected decal (a4)                                  @0x6B0B9B  argument decal
//   + alpha distance fade (a6)                               (if(a6) blocks)  argument alphaFade
//   + BILLBOARD (face-camera)                                @0x6B107C  gate mat.billboard.Enable
//        -> TODO ANCHOR: the axis bases flt_8001D4 (0x8001D4) / unk_80022C (0x80022C) are an
//           UNPROVEN RUNTIME STATE (cf. TS2_DEEP_MESHPART_MATERIAL.md §10). We do NOT fabricate
//           a camera axis. Honest fallback = indexed draw of the stored geometry (mesh VISIBLE but
//           NOT billboarded). The rest of the state machine (light, return) stays faithful.
//
// ===========================================================================================
//  Gfx_* HELPERS REIMPLEMENTED IN DIRECT D3D9 (the FF singleton g_GfxRenderer 0x7FFE18 is absent)
// ===========================================================================================
//  Gfx_SetMaterialEmissive 0x69D1F0 -> D3DMATERIAL9 { Diffuse(1,1,1,1), Ambient(1,1,1,1),
//     Specular=glow.SpecRGBA, Emissive(0,0,0,1), Power=glow.SpecPower } + SetMaterial.
//  Gfx_SetShadowProjLight 0x69D7A0 -> D3DLIGHT9 DIRECTIONAL slot 1: Specular =
//     (sceneCenter + lightOffset) * intensity; Direction = cameraAt - cameraEye; + LightEnable(1,TRUE)
//     + SetRenderState(SPECULARENABLE, TRUE).
//  Gfx_SetLight 0x69D5C0 (slot 0) -> D3DLIGHT9 DIRECTIONAL, Direction=(-1,-1,1): mode 1 =>
//     Ambient = sceneCenter; mode 2 => Ambient = (r,g,b,a) given.
//  Gfx_ApplyMeshMaterial 0x69D0E0 -> restores the D3DMATERIAL9 (device entry snapshot).
//  Gfx_DisableMeshLighting 0x69D9C0 -> LightEnable(1,FALSE) + SetRenderState(SPECULARENABLE,FALSE).
//  Gfx_SkyboxEndState 0x69D780 -> restores light 0 (device entry snapshot).
//
//  The runtime state the FF singleton kept in globals (world matrix dword_800244, camera eye
//  g_CameraPos 0x800130, camera target +804..812, sun direction flt_800308.., scene AABB
//  center +1204*0.5+1236) is provided by the CALLER via MeshPartRuntime — NO invented value.
//
// ===========================================================================================
//  AUTONOMY
// ===========================================================================================
// STATELESS module: owns NO D3D resource (no VB/IB/texture, no DEFAULT pool) — it only
// sets states and draws with resources SUPPLIED by the caller. So NOTHING to
// recreate on device-lost (no OnDeviceLost/Reset). Not wired into the render loop yet:
// FLEET C/MAIN will call Render() from ModelObjectRenderer/WorldGeometryRenderer.
//
// CALL PRE-CONDITION (faithful to Model_RenderWithShadow_0 0x6A4110), to set BEFORE Render():
//   (a) SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1 = 0x112 = 274) @0x6a4186 — the base-draw and
//       billboard assume 32 B stride; MeshPart_RenderFull does NOT set the FVF itself.
//   (b) SetTransform(D3DTS_WORLD, worldMatrix) @0x6a4299 — MeshPart_RenderFull only sets the
//       world matrix in the billboard branch (identity then restore). rt.world is used to transform
//       node centers (D3DXVec3TransformCoord) and for the billboard restore.
#pragma once
#include "Asset/Model.h"   // asset::MeshPartMaterial (FLEET A — decoded, named fields)
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>

namespace ts2::gfx {

// -------------------------------------------------------------------------------------------
// GPU resources of a part (mirrors MeshPart 408 B, fields consumed by 0x6B0850).
// -------------------------------------------------------------------------------------------
struct MeshPartGpu {
    IDirect3DVertexBuffer9* vb = nullptr;   // this[72] (+288) — 32*A*B B (A contiguous frames)
    IDirect3DIndexBuffer9*  ib = nullptr;   // this[73] (+292) — 6*D B (INDEX16, shared)
    uint32_t vertsPerFrame = 0;             // B = this[64] (+256) — numVerts of DrawIndexedPrimitive
    uint32_t triCount      = 0;             // D = this[66] (+264) — primCount
    uint32_t frameCount    = 1;             // A = this[63] (+252) — defensive frame bound
    // Node/bbox block (A*64 B, this[71]/+284): per frame 64 B — center vec3 @+48 (fresnel glow,
    // billboard), billboard axes @+36 (NOT reproduced, cf. TODO billboard). nullptr => safe skips.
    const uint8_t* frameNodes = nullptr;
};

// -------------------------------------------------------------------------------------------
// Resolved textures (COM pointers extracted from 52 B holders at offset +48).
// -------------------------------------------------------------------------------------------
struct MeshPartTextures {
    IDirect3DTexture9* base       = nullptr; // tex0 diffuse: this[86] (+344); mode this[85]
    int                baseMode   = 0;       // this[85] (+340): 1=alpha-test / 2=blend / other
    IDirect3DTexture9* second     = nullptr; // tex1 2nd texture: this[99] (+396); mode this[98]
    int                secondMode = 0;       // this[98] (+392): 1=alpha-test / 2=blend / other
    // Flipbook: array of texture pointers ALREADY extracted (this[101]+52*i+48), size = this[100].
    const IDirect3DTexture9* const* flipbook = nullptr; // this[101] (+404) reduced to [tex...]
    uint32_t flipbookCount = 0;                          // this[100] (+400) — index modulo
};

// -------------------------------------------------------------------------------------------
// Runtime state the FF singleton g_GfxRenderer 0x7FFE18 kept in globals — SUPPLIED by
// the caller (no invented value; each field carries its original global's anchor).
// -------------------------------------------------------------------------------------------
struct MeshPartRuntime {
    // Current world matrix = dword_800244 (set by the caller via SetTransform(256)). Used to
    // transform node centers (Vec3_TransformCoord @0x6b0a81/@0x6b11a4) and for the billboard
    // branch's WORLD restore. NOT used to (re)set the base-draw's D3DTS_WORLD.
    D3DXMATRIX  world;
    D3DXVECTOR3 cameraEye   = {0.0f, 0.0f, 0.0f}; // g_CameraPos 0x800130 / flt_800134 / flt_800138
    D3DXVECTOR3 cameraAt    = {0.0f, 0.0f, 0.0f}; // camera target +804..812; proj-light dir = at - eye
    D3DXVECTOR3 sunDir      = {0.0f, 0.0f, 0.0f}; // flt_800308/30C/310 (fresnel glow dots -sunDir)
    D3DXVECTOR3 sceneCenter = {0.0f, 0.0f, 0.0f}; // scene AABB center = +1204*0.5 + 1236 (proj-light color)
    bool worldValid = false;                      // rt.world usable (otherwise fresnel/billboard skipped)
    MeshPartRuntime() { D3DXMatrixIdentity(&world); }
};

// -------------------------------------------------------------------------------------------
// Projected decal (argument a4 of 0x6B0850): 52 B texture struct — +44 mode, +48 tex pointer.
// nullptr (or tex==nullptr) = no decal (standard FX path: a4=0).
// -------------------------------------------------------------------------------------------
struct MeshPartDecal {
    int                mode = 0;       // a4+44: 1=alpha-test / 2=blend / other
    IDirect3DTexture9* tex  = nullptr; // a4+48
};

// ===========================================================================================
//  MeshPartMaterialRenderer — STATELESS state machine (static methods). The class name
//  differs from asset::MeshPartMaterial (the descriptor it consumes) to avoid any collision.
// ===========================================================================================
class MeshPartMaterialRenderer {
public:
    // Full port of MeshPart_RenderFull 0x6B0850. Sets the 9 state/matrix layers then
    // draws (base-draw @0x6B1327 + optional 2nd pass @0x6B1C17), and symmetrically restores
    // ALL modified states (flags v73/v74/v91/v92/v38/v64 of the binary).
    //
    //  dev        : D3D9 device (g_GfxRenderer_pDevice 0x800074 in the binary).
    //  mat        : DECODED 120 B material header (asset::MeshPartMaterial, FLEET A). If
    //               mat.decoded == false => NO layer: falls back to the pure base-draw (tex.base).
    //  geo        : VB/IB/B/D + node block (base-draw + frustum/billboard centers).
    //  tex        : resolved tex0/tex1/flipbook.
    //  frame      : morph frame index (a2) — assumed already bounded [0,A-1] by the caller;
    //               defensively re-bounded on geo.frameCount for the stream/node offset.
    //  animTime   : v66 = Terrain_PushRenderState() + a3 (QPC seconds + phase) — master clock
    //               of ALL animated layers (ping-pong, flipbook, UV-scroll). @0x6b0871/@0x6b0883.
    //  glowEnable : a5 (enables specular glow). FX path = 1.
    //  alphaFade  : a6 (distance fade, 0..255) — 0 = fade entirely DEAD (FX path). @0x6b0bdf etc.
    //  decal      : a4 (projected decal) or nullptr.
    static void Render(IDirect3DDevice9* dev,
                       const asset::MeshPartMaterial& mat,
                       const MeshPartGpu& geo,
                       const MeshPartTextures& tex,
                       int frame,
                       float animTime,
                       const MeshPartRuntime& rt,
                       int glowEnable = 1,
                       uint8_t alphaFade = 0,
                       const MeshPartDecal* decal = nullptr);

private:
    MeshPartMaterialRenderer() = delete;
};

} // namespace ts2::gfx

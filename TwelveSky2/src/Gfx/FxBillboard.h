// Gfx/FxBillboard.h — combat/skill FX "Object A" LEAF billboard (.PARTICLE).
//
// FAITHFUL rewrite (bit-exact goal) of TwelveSky2's combat/skill particle path. Ground truth is
// the TwelveSky2.exe IDB (imagebase 0x400000); every block cites its IDA anchor (name + 0xADDR).
// Byte-exact spec: Docs/TS2_EXTRACT_FX_COMBAT.md.
//
// ┌─ What this tier does (F_FXCOMBAT 1/2, foundation) ──────────────────────────────────────┐
// │ (1) LOADER for a .PARTICLE template by INDEX (0..51):                                    │
// │       mirrors Fx_NodeLoadFromFile 0x6A6680 + FxParticle_BuildPath 0x4D9E60                │
// │       ("G03_GDATA\D05_GPARTICLE\%03d.PARTICLE", index+1 -> 001..052.PARTICLE).            │
// │       Lazy table of 52 templates (AssetMgr_InitAllSlots 0x4DEB50 @0x4E03F6,                │
// │       first-use load via SObject_EnsureLoadedK 0x4D9EB0).                                 │
// │ (2) LIFECYCLE of a 48 B POBJECT pool (Object A): Init if uninitialized, UpdateEmit         │
// │       otherwise; RenderBillboards. Lazy-load wrappers:                                     │
// │       SObject_UpdateK 0x4D9F00 / Particle_EnsureLoadedThenUpdateEmit 0x4D9F40 /            │
// │       Particle_EnsureLoadedThenRender 0x4D9F90.                                            │
// └────────────────────────────────────────────────────────────────────────────────────────┘
//
// BOUNDARY (Docs/TS2_EXTRACT_FX_COMBAT.md §7.2): "Object A" = 48 B pool + 232 B template
//   + Particle_RenderBillboards 0x6A70B0, .PARTICLE files. This is THE SAME ENGINE as the
//   .WP zone emitters (Gfx/ZoneFxEmitter.*, Wave C) — same IDA functions (Particle_Init
//   0x6A7020, Particle_UpdateEmit 0x6A7530, Particle_RenderBillboards 0x6A70B0, Particle_Free
//   0x6A6FF0, Particle_ComputeGradients 0x6A6D10). We therefore REUSE the structs IDENTICALLY
//   (FxEmitterTemplate 232 B, FxParticlePool 48 B) and the ZoneFx_* primitives from ZoneFxEmitter.h.
//   Only the LOADER (.PARTICLE instead of embedded .WP) and the 52-template TABLE are
//   combat-specific -> that's what this file adds.
//   DO NOT confuse with "Object B" (Gfx/ParticleSystem.h: PtclDef 236 B / PtclPool 60 B /
//   PtclDef_RenderQuads 0x424430, .PTCL files) — a DISTINCT system (world/weather particles).
//
// Uses only the Windows/Direct3D9 + d3dx9 SDK (like the binary).
#pragma once
#include "Gfx/ZoneFxEmitter.h"   // FxEmitterTemplate 232 B, FxParticlePool 48 B, ZoneFx_* (Object A engine),
                                  // ZoneFxFrameParams, FxFrustumFn, Particle/Billboard_Vertex (shared)
#include <windows.h>
#include <d3d9.h>
#include <cstddef>

namespace ts2::gfx {

// ---------------------------------------------------------------------------------------------
// Number of .PARTICLE templates = `i88 < 52` loop of AssetMgr_InitAllSlots 0x4DEB50 @0x4E03F6
// (stride 336 B). 52 files 001.PARTICLE..052.PARTICLE present on disk (spec §6.4).
inline constexpr int kFxParticleTemplateCount = 52;

// Size of a template-table slot: {loaded@+0, path@+4, node@+104} = 336 B (0x150). Proven
// by SObject_EnsureLoadedK 0x4D9EB0 (Fx_NodeLoadFromFile(this+104, this+4, 1, 1)). Here we model
// the same data as C++ members; byte-exact memory layout is not required
// (we rebuild the data structure, not the byte_1151CBC/byte_86918C runtime address).
inline constexpr int kFxTemplateSlotStride = 336;

// =============================================================================================
//  CONFIGURATION (set ONCE by MAIN before the first load/render)
// =============================================================================================

// Shared D3D9 device (g_GfxRenderer 0x7FFE18 in the binary). Needed to create templates' GPU
// textures (D3DPOOL_MANAGED -> survives a device reset, no recreation to wire). If null at load
// time, the template still loads but without a texture (gpuTex=null).
void FxBillboard_SetDevice(IDirect3DDevice9* device);

// GameData directory root (where G03_GDATA\D05_GPARTICLE\NNN.PARTICLE lives). Default "GameData".
// Final path = <root> + "\\" + FxParticle_BuildPath(index).
void FxBillboard_SetDataRoot(const char* root);

// =============================================================================================
//  LOADER / TEMPLATE TABLE
// =============================================================================================

// FxParticle_BuildPath 0x4D9E60: writes "G03_GDATA\D05_GPARTICLE\%03d.PARTICLE" (index+1) into `dst`
// (dstSize bytes, NUL-terminated). index 0 -> "001.PARTICLE", …, index 51 -> "052.PARTICLE".
void FxBillboard_BuildPath(char* dst, size_t dstSize, int index);

// (Lazily) loads and returns the shared 232 B TEMPLATE for `index` (0..kFxParticleTemplateCount).
// Mirrors SObject_EnsureLoadedK 0x4D9EB0: on first call, opens the .PARTICLE and parses it via the
// Fx_NodeLoadFromFile 0x6A6680 loader (texture + quat track + 144 B of fields). Returns nullptr if
// `index` is out of bounds or the load fails. The pointer is STABLE (static table never resized)
// -> it can be kept in pool->tmpl by ZoneFx_Init.
FxEmitterTemplate* FxBillboard_GetTemplate(int index);

// true if template `index` is already loaded (SObject_EnsureLoadedK @+0 flag). No loading.
bool FxBillboard_IsTemplateLoaded(int index);

// Frees all GPU textures of loaded templates and resets the table (enabled=0, loaded=false).
// Call at renderer shutdown. (D3DPOOL_MANAGED -> unnecessary on device reset.)
void FxBillboard_FreeAllTemplates();

// =============================================================================================
//  LIFECYCLE OF A 48 B POBJECT POOL (the pool is OWNED BY THE CALLER — inline in its FX slot)
//  The wrappers ensure the template is loaded, then drive the shared Object A engine.
// =============================================================================================

// SObject_UpdateK 0x4D9F00: ensure-loaded, then Particle_Init(pool, template) 0x6A7020.
// (Re)allocates the particle array if pool->flag==0. Call when the pool is new.
void FxBillboard_PoolInit(FxParticlePool* pool, int index);

// Particle_EnsureLoadedThenUpdateEmit 0x4D9F40: ensure-loaded, then Particle_UpdateEmit 0x6A7530.
// `dt` = true frame delta; `pos`/`rot` (3 floats) = emission origin/orientation (degrees);
// `frustum` (optional, nullptr => always visible) culls the origin before integration/emission.
void FxBillboard_PoolUpdate(FxParticlePool* pool, int index, float dt,
                            const float pos[3], const float rot[3], FxFrustumFn frustum);

// Convenient per-frame tick = binary's update wiring ("if(flag) UpdateEmit else Init";
// cf. WorldGeometryRenderer::updateFx / loop 2 MapColl_UpdateObjectAnim @0x694AF0, and the combat
// path SObject_UpdateK vs Particle_EnsureLoadedThenUpdateEmit). 1st frame = Init (no emission).
void FxBillboard_PoolTick(FxParticlePool* pool, int index, float dt,
                          const float pos[3], const float rot[3], FxFrustumFn frustum);

// Particle_EnsureLoadedThenRender 0x4D9F90: ensure-loaded, then Particle_RenderBillboards 0x6A70B0.
// Renders billboards of live particles (camera-facing quads via `right`/`up`, internal CPU
// scratch, DrawPrimitiveUP TRIANGLELIST stride 24). `right`/`up` = world billboard basis
// (flt_8001D4..E8 = camera right/up × ...) supplied by the caller from the view matrix. `maxQuads`
// = per-batch quad ceiling (dword_7FFEE0; 0 => no ceiling). `frustum` optional (Cam_FrustumTestPoint6
// 0x69ED30). Returns the number of quads drawn (0 if pool uninitialized / outside frustum).
int FxBillboard_PoolRender(FxParticlePool* pool, int index, IDirect3DDevice9* device,
                           const float right[3], const float up[3], int maxQuads, FxFrustumFn frustum);

// Particle_Free 0x6A6FF0: frees the pool's particle array (balanced HeapFree). No-op if empty.
void FxBillboard_PoolFree(FxParticlePool* pool);

} // namespace ts2::gfx

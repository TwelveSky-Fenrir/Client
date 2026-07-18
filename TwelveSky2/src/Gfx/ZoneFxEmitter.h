// Gfx/ZoneFxEmitter.h — "Object A" particle engine for ZONE EMITTERS (.WP).
//
// FAITHFUL rewrite (byte-exact target) of the ambient zone particle system
// (torches/fires/waterfalls) in TwelveSky2. Source of truth is the TwelveSky2.exe IDB; each
// block cites its IDA anchor (name + 0xADDR). Byte-exact spec: Docs/TS2_EXTRACT_WP_EMITTERS.md.
//
// ┌─ Proven chain (IDA, reachability from Scene_InGameUpdate/Render, 0 dead code) ────┐
// │  Scene_InGameUpdate 0x52C600 -> MapColl_UpdateObjectAnim 0x694A00 (loop 2)         │
// │     -> Particle_Init 0x6A7020 (1st frame) / Particle_UpdateEmit 0x6A7530 (after)   │
// │  Scene_InGameRender 0x52D0B0 -> Terrain_Render 0x698670 (a5==2, 0x698c5a-0x698cd5) │
// │     -> Particle_RenderBillboards 0x6A70B0                                          │
// └───────────────────────────────────────────────────────────────────────────────────┘
//
// Core reversed functions (ported here under ZoneFx_* names = IDA anchors):
//   Particle_ComputeGradients 0x6A6D10  clamps colors/life, DERIVES colorRate[4]
//   Particle_Init             0x6A7020  count = ftol(particleLife*rate), HeapAlloc 56*count
//   Particle_Free             0x6A6FF0  HeapFree of the array, flag=0, tmpl=0
//   Particle_UpdateEmit       0x6A7530  integrates + emits (switch 6 SHAPES, cases 1..6)
//   Particle_RenderBillboards 0x6A70B0  6 vertices/particle 24B, DrawPrimitiveUP TRIANGLELIST
//
// WARNING BOUNDARY (TS2_EXTRACT_WP_EMITTERS.md §4.3): this "Object A" engine (POBJECT 48B,
//   template 232B, 6 shapes, renderer g_GfxRenderer 0x7FFE18) is DISTINCT and NOT
//   MERGEABLE with the "Object B" engine in Gfx/ParticleSystem.h (PtclPool 60B,
//   PtclDef 236B, 10 shapes, g_GxdRenderer 0x18C4EF8). We ONLY REUSE the
//   COMMON primitives from ParticleSystem.h — struct Particle (56B IDENTICAL),
//   Billboard_Vertex (24B), Math_RandFloatRange, Rng_Next, Crt_ftol — never PtclDef/PtclPool.
#pragma once
#include "Gfx/ParticleSystem.h"   // Particle 56B, Billboard_Vertex 24B, Math_RandFloatRange, Rng, Crt_ftol
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <cstddef>   // offsetof
#include <vector>

namespace ts2::gfx {

// ---------------------------------------------------------------------------------------------
// FxEmitterTemplate — emitter TEMPLATE, 232 B (= MapColl this+29[nodeIndex], stride 232).
// Runtime layout PROVEN by Fx_NodeLoadFromHandle 0x6A69F0 (18 sequential ReadFile calls filling
// [+72, +216)) + Particle_ComputeGradients 0x6A6D10 (derives colorRate[4] at [+216, +232)).
// The [+0, +72) header (enabled, texture block, keyframe track) is not re-read by the sim: only
// enabled@+0 (gate), texture@+52 (render's SetTexture) and frameTrack@+56/frameCount@+60/
// frameMatrices@+68 (keyframe path, NOT ported) are used there; the rest is `_gap`.
#pragma pack(push, 4)
struct FxEmitterTemplate {
    int32_t            enabled;        // +0    Init/render gate (=1 for a loaded .WP node, 0x6a6cf4)
    uint8_t            _gap04[48];     // +4    texture block (Tex_LoadCompressedFromHandle) — GPU tex at +52
    IDirect3DTexture9* texture;        // +52   GPU texture (SetTexture(0,*(tmpl+52)) @0x6a745e)
    void*              frameTrack;     // +56   keyframe track (gate template+56 @0x6a787d) — NOT ported
    int32_t            frameCount;     // +60   keyframe count (% frameCount @0x6a78b0) — NOT ported
    uint8_t            _gap40[4];      // +64
    void*              frameMatrices;  // +68   64B matrix base (frame<<6 @0x6a78a4) — NOT ported
    float              emissionDuration;// +72  emission gate (template+72 @0x6a783c)
    float              kfFps;          // +76   keyframe rate (idx = kfFps*elapsedTime @0x6a78ab)
    float              rate;           // +80   particles/s (spawnAccum += dt*rate; sizes the pool)
    float              shape;          // +84   emission shape 1..6 (Crt_ftol @0x6a7a7f)
    float              speed;          // +88   base speed/radius (cases 1..4)
    float              box[3];         // +92   box extent (cases 5/6: Rand(box*-0.5, box*0.5))
    float              particleLife;   // +104  particle lifetime (clamped >=0.01; dies if age>this field)
    float              velMin[3];      // +108  random init velocity min XYZ (Rand(velMin,velMax))
    float              velMax[3];      // +120  random init velocity max XYZ
    float              param0Start;    // +132  particle.param0 at spawn (acceleration divisor)
    float              sizeStart;      // +136  particle.size at spawn (half-size + billboard gray)
    float              startColor[4];  // +140  spawn RGBA color (copied as-is, clamped 0..255)
    float              endColor[4];    // +156  end RGBA color (clamped 0..255; derives colorRate)
    float              forceBase[3];   // +172  constant force (accel = force/param0*dt)
    float              forceRandMin[3];// +184  random force min (force = Rand(min,max)+base, 1x/frame)
    float              forceRandMax[3];// +196  random force max
    float              param0Rate;     // +208  param0 drift/s (clamped >=0)
    float              sizeRate;       // +212  size drift/s (clamped >=0)
    float              colorRate[4];   // +216  RGBA drift/s — DERIVED (ComputeGradients), NOT on disk
};
#pragma pack(pop)
static_assert(sizeof(FxEmitterTemplate) == 232, "FxEmitterTemplate must be 232 B");
static_assert(offsetof(FxEmitterTemplate, enabled)          == 0,   "enabled @+0");
static_assert(offsetof(FxEmitterTemplate, texture)          == 52,  "texture @+52");
static_assert(offsetof(FxEmitterTemplate, frameTrack)       == 56,  "frameTrack @+56");
static_assert(offsetof(FxEmitterTemplate, frameMatrices)    == 68,  "frameMatrices @+68");
static_assert(offsetof(FxEmitterTemplate, emissionDuration) == 72,  "emissionDuration @+72");
static_assert(offsetof(FxEmitterTemplate, rate)             == 80,  "rate @+80");
static_assert(offsetof(FxEmitterTemplate, shape)            == 84,  "shape @+84");
static_assert(offsetof(FxEmitterTemplate, particleLife)     == 104, "particleLife @+104");
static_assert(offsetof(FxEmitterTemplate, velMin)           == 108, "velMin @+108");
static_assert(offsetof(FxEmitterTemplate, param0Start)      == 132, "param0Start @+132");
static_assert(offsetof(FxEmitterTemplate, startColor)       == 140, "startColor @+140");
static_assert(offsetof(FxEmitterTemplate, endColor)         == 156, "endColor @+156");
static_assert(offsetof(FxEmitterTemplate, forceBase)        == 172, "forceBase @+172");
static_assert(offsetof(FxEmitterTemplate, forceRandMin)     == 184, "forceRandMin @+184");
static_assert(offsetof(FxEmitterTemplate, forceRandMax)     == 196, "forceRandMax @+196");
static_assert(offsetof(FxEmitterTemplate, param0Rate)       == 208, "param0Rate @+208");
static_assert(offsetof(FxEmitterTemplate, sizeRate)         == 212, "sizeRate @+212");
static_assert(offsetof(FxEmitterTemplate, colorRate)        == 216, "colorRate @+216");
// The asset::FxNode.fields disk blob (144 B) covers EXACTLY [+72, +216): copied
// as-is to (tmpl+72). Anchor: Fx_NodeLoadFromHandle 0x6A69F0 (this+18..this+53 = bytes 72..215).
static constexpr size_t kFxTemplateDiskOffset = 72;   // runtime +72
static constexpr size_t kFxTemplateDiskSize   = 144;  // [+72, +216)

// ---------------------------------------------------------------------------------------------
// FxParticlePool — POBJECT 48 B (pool runtime state, at FxNode+28). Layout PROVEN by
// Particle_Init 0x6A7020 / Particle_UpdateEmit 0x6A7530 / Particle_RenderBillboards 0x6A70B0.
// = PtclPool 60B from ParticleSystem.h MINUS the scale[3] prefix (all offsets shifted by -12).
struct FxParticlePool {
    int32_t            flag;          // +0   initialized/gate (=1 after Init; 0x6a70a5)
    FxEmitterTemplate* tmpl;          // +4   source template (this+1 = a2; 0x6a7043)
    float              elapsedTime;   // +8   += dt (this+2; 0x6a758f)
    float              position[3];   // +12  emission origin (copied from FxNode+4; 0x6a7594)
    float              rotation[3];   // +24  XYZ rotation deg (copied from FxNode+16; 0x6a75ae)
    float              spawnAccum;    // +36  fractional accumulator (this+9; emits while >=1)
    int32_t            particleCount; // +40  capacity = ftol(particleLife*rate) (this+10; 0x6a7062)
    Particle*          particles;     // +44  heap array particleCount x 56 B (this+11; 0x6a707c)
};
static_assert(sizeof(FxParticlePool) == 48, "FxParticlePool (POBJECT) must be 48 B");
static_assert(offsetof(FxParticlePool, flag)          == 0,  "flag @+0");
static_assert(offsetof(FxParticlePool, tmpl)          == 4,  "tmpl @+4");
static_assert(offsetof(FxParticlePool, elapsedTime)   == 8,  "elapsedTime @+8");
static_assert(offsetof(FxParticlePool, position)      == 12, "position @+12");
static_assert(offsetof(FxParticlePool, rotation)      == 24, "rotation @+24");
static_assert(offsetof(FxParticlePool, spawnAccum)    == 36, "spawnAccum @+36");
static_assert(offsetof(FxParticlePool, particleCount) == 40, "particleCount @+40");
static_assert(offsetof(FxParticlePool, particles)     == 44, "particles @+44");

// ---------------------------------------------------------------------------------------------
// Frustum cull predicate for the pool's origin (Cam_FrustumTestPoint6 0x69ED30 on POBJECT+12).
// The binary tests the origin against g_GfxRenderer's frustum (last posed camera). On the
// ClientSource side this singleton doesn't exist: we inject the test (or nullptr => always visible).
using FxFrustumFn = bool (*)(const float pos[3]);

// Billboard render frame parameters (Particle_RenderBillboards 0x6A70B0). The binary reads the
// billboard basis from renderer globals (flt_8001D4..8001E8 = world camera right/up); here the
// caller (WorldGeometryRenderer) supplies them from the view matrix.
struct ZoneFxFrameParams {
    IDirect3DDevice9*             device   = nullptr; // g_GfxRenderer_pDevice 0x800074
    float                         right[3] = {1,0,0}; // flt_8001D4/D8/DC (world camera right, unit)
    float                         up[3]    = {0,1,0}; // flt_8001E0/E4/E8 (world camera up, unit)
    int                           maxQuads = 0;       // dword_7FFEE0 (quad cap; 0 => no cap)
    std::vector<Billboard_Vertex>* scratch = nullptr; // dword_800080 (CPU vertex buffer, DrawPrimitiveUP)
    FxFrustumFn                   frustum  = nullptr; // Cam_FrustumTestPoint6 (nullptr => always visible)
};

// ---------------------------------------------------------------------------------------------
// Global "engine ready" gate (mirrors dword_800080 != 0: Init/UpdateEmit/Render no-op otherwise).
// The binary gates on the shared VB's existence; on the ClientSource side the module owns its
// own scratch -> ready by default. MAIN can force it if needed.
void ZoneFx_SetReady(bool ready);
bool ZoneFx_Ready();

// ---------------------------------------------------------------------------------------------
// Engine API (names = IDA anchors). All originally __thiscall -> first arg = the pool/template.

// Particle_ComputeGradients 0x6A6D10: clamps kfFps/rate/speed/box >=0, particleLife >=0.01,
// start/endColor 0..255, then DERIVES colorRate[i] = (endColor[i]-startColor[i]) / particleLife.
// Idempotent (re-called on every Init of a pool sharing the template).
void ZoneFx_ComputeGradients(FxEmitterTemplate* tmpl);

// Particle_Init 0x6A7020: (re)allocates the pool if flag==0 && ready && tmpl->enabled. Calls
// ComputeGradients, sets tmpl/elapsedTime/spawnAccum=0, particleCount = ftol(particleLife*rate),
// HeapAlloc(56*count), zero-inits each slot's `alive` flag, flag=1.
void ZoneFx_Init(FxParticlePool* pool, FxEmitterTemplate* tmpl);

// Particle_Free 0x6A6FF0: HeapFree of the array, flag=0, tmpl=0, particles=0. (No-op if unallocated.)
void ZoneFx_Free(FxParticlePool* pool);

// Particle_UpdateEmit 0x6A7530: one simulation frame. `pos`/`rot` = emission origin/orientation
// (3 floats each, copied into the pool). Integrates age/pos/vel/param0/size/color of live
// particles (dies if age>particleLife), then emits via the 6-shape switch while spawnAccum>=1.
// `frustum` (optional) culls the origin: if outside the frustum, does nothing. `dt` = binary's a3
// (= true frame delta, cf. MapColl_UpdateObjectAnim call site Scene_InGameUpdate 0x52c94b).
void ZoneFx_UpdateEmit(FxParticlePool* pool, float dt, const float pos[3], const float rot[3],
                       FxFrustumFn frustum);

// Particle_RenderBillboards 0x6A70B0: builds 6 vertices/live particle (2 triangles, 24 B:
// XYZ + D3DCOLOR + UV) into `params.scratch`, camera-facing quad via the right/up basis x size,
// GRAY color derived from size, then SetTexture(0, tmpl->texture) + DrawPrimitiveUP(TRIANGLELIST, 2*n, .., 24).
// Returns the number of drawn quads (0 if pool empty/outside frustum). Frustum-culls the origin first.
int  ZoneFx_RenderBillboards(FxParticlePool* pool, const ZoneFxFrameParams& params);

// Fills a 232B FxEmitterTemplate from the asset::FxNode disk blob: sets enabled=1,
// GPU texture (at +52), and copies the 144 B of fields to (tmpl+72). `diskFields` = asset::FxNode.fields
// (144 B = runtime [+72,+216)). Does NOT derive colorRate (done by Init/ComputeGradients).
// The keyframe path (frameTrack/frameMatrices) stays null -> frame matrix = identity (else-branch
// 0x6a78c3), faithful to the ambient no-keyframe case (TODO anchor 0x6a787d: bake the quaternion
// track).
void ZoneFx_BuildTemplate(FxEmitterTemplate* tmpl, const uint8_t* diskFields, size_t diskFieldsSize,
                          IDirect3DTexture9* texture);

} // namespace ts2::gfx

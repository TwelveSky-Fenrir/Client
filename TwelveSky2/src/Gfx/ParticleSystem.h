// Gfx/ParticleSystem.h — particle engine of TwelveSky2's "GXD" engine.
//
// FAITHFUL rewrite (bit-exact target) of the GXD particle pool. The source of
// truth is TwelveSky2.exe's IDB; each block cites its IDA anchor (name + 0xADDR).
//
// Three nested structures + the billboard vertex (layouts proven by
// PtclDef_ReadFile 0x422C50 and §2 of Docs/TS2_FX_ROSETTA.md):
//   PtclDef        236 B  — immutable definition/template (.ptcl file)
//   PtclPool        60 B  — runtime instance (live emitter)
//   Particle        56 B  — live particle (loose SoA, heap array)
//   Billboard_Vertex 24 B — quad vertex (pos + D3DCOLOR ARGB + uv)
//
// Core reversed functions:
//   PtclDef_ClampParams     0x422F60  clamps colors/lifetime, DERIVES colorRate
//   PtclDef_AllocPool       0x423280  count = ftol(lifetime*spawnRate), heap alloc
//   PtclDef_FreePool        0x423240  frees the array, resets scale=1
//   PtclDef_UpdateAndSpawn  0x423310  integrates + spawns (switch of 10 shapes)
//   PtclDef_RenderQuads     0x424430  billboards -> shared VB, DrawPrimitiveUP
//   PtclDef_ReadFile        0x422C50  deserializes from a file HANDLE
//   PtclDef_Init/Reset      0x4221E0/0x4222D0  zero-init (texture idx[11] = -1)
//
// WARNING BOUNDARIES (FX Rosetta CONFLICTS §3, IDA wins): no mega-struct
//   EFFECT_OBJECT, no [52] bank, emission shape index 1..10 = IDA's own
//   (NOT transposed from VeryOld). Field field19@0x4C keeps its neutral IDA
//   name: its role (emission-duration gate) is REPRODUCED from the decompiled
//   branch 0x423597, not invented.
//
// Uses only the Windows/Direct3D9 + d3dx9 SDK (like the binary, via its
// j_D3DXVec*/j_D3DXMatrix* thunks @0x6BB6xx).
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <cstddef>   // offsetof

namespace ts2::gfx {

// ---------------------------------------------------------------------------
// Billboard vertex — Billboard_Vertex, 24 B. FVF = XYZ|DIFFUSE|TEX1 (0x142).
// PtclDef_RenderQuads 0x424430 only writes pos (+0) and color (+12); the uv
// (+16) are pre-initialized once in the shared VB (see InitSharedQuadUVs).
struct Billboard_Vertex {
    float    x, y, z;   // +0x00  world position
    D3DCOLOR color;     // +0x0C  ARGB = A<<24 | R<<16 | G<<8 | B
    float    u, v;      // +0x10  texture coords (static in the VB)
};
static_assert(sizeof(Billboard_Vertex) == 24, "Billboard_Vertex must be 24 B");

inline constexpr DWORD kBillboardFVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1; // 0x142

// ---------------------------------------------------------------------------
// Particle — live particle, 56 B (= PARTICLE_FOR_GXD). Offsets proven by
// PtclDef_UpdateAndSpawn 0x423310 (integration) / PtclDef_RenderQuads 0x424430.
struct Particle {
    int   alive;        // +0x00  0 = free slot; 1 = alive
    float age;          // +0x04  elapsed lifetime (dies if age > lifetime)
    float pos[3];       // +0x08  world position (spawn = transformed coord)
    float vel[3];       // +0x14  world velocity
    float param0;       // +0x20  acceleration divisor (vel += force/param0*dt)
    float size;         // +0x24  billboard half-size
    float colorR;       // +0x28  0..255
    float colorG;       // +0x2C
    float colorB;       // +0x30
    float colorA;       // +0x34
};
static_assert(sizeof(Particle) == 56, "Particle must be 56 B");

// ---------------------------------------------------------------------------
// PtclDef — immutable definition, 236 B (= PSYSTEM_FOR_GXD). EXACT order/offsets
// from PtclDef_ReadFile 0x422C50 (sequential read from file).
// The header (enabled + cTexture[56] + motion[16]) is opaque here: texture and
// motion are populated by third-party subsystems (Tex_ReadPacked 0x417740,
// Anim_ReadMotionStream 0x43CDB0) — modeled as faithful byte blocks.
#pragma pack(push, 4)
struct PtclDef {
    int      enabled;            // +0x00  0 = empty def (gates alloc/update/render)
    uint8_t  texture[56];        // +0x04  cTexture 56 B; IDirect3DTexture9* @+0x34
    uint8_t  motion[16];         // +0x3C  MOTION stream (ptr@+0/frameCount@+4/frames@+0xC)
    float    field19;            // +0x4C  emission gate (branch 0x423597) — neutral IDA name
    float    texAnimFPS;         // +0x50  texture anim rate (idx = FPS*elapsedTime)
    float    spawnRate;          // +0x54  particles/s (sizes the pool)
    float    shapeType;          // +0x58  emission shape 1..10 (float -> int)
    float    baseSpeed;          // +0x5C  base speed/radius
    float    boxSize[3];         // +0x60  box/planar extent
    float    lifetime;           // +0x6C  particle lifetime (clamped >= 0.01)
    float    velMin[3];          // +0x70  random initial velocity min XYZ
    float    velMax[3];          // +0x7C  random initial velocity max XYZ
    float    param0Start;        // +0x88  spawn value of particle.param0
    float    sizeStart;          // +0x8C  spawn value of particle.size
    float    startColor[4];      // +0x90  RGBA color at spawn (copied as-is)
    float    endColor[4];        // +0xA0  RGBA end color (derives colorRate)
    float    forceBase[3];       // +0xB0  constant force
    float    forceRandMin[3];    // +0xBC  random force min
    float    forceRandMax[3];    // +0xC8  random force max
    float    param0Rate;         // +0xD4  param0 drift / s (on disk)
    float    sizeRate;           // +0xD8  size drift / s (on disk)
    float    colorRate[4];       // +0xDC  RGBA drift / s — DERIVED (NOT on disk)
};
#pragma pack(pop)
static_assert(sizeof(PtclDef) == 236, "PtclDef must be 236 B");
static_assert(offsetof(PtclDef, field19)    == 0x4C, "field19 @0x4C");
static_assert(offsetof(PtclDef, spawnRate)  == 0x54, "spawnRate @0x54");
static_assert(offsetof(PtclDef, lifetime)   == 0x6C, "lifetime @0x6C");
static_assert(offsetof(PtclDef, startColor) == 0x90, "startColor @0x90");
static_assert(offsetof(PtclDef, colorRate)  == 0xDC, "colorRate @0xDC");

// ---------------------------------------------------------------------------
// PtclPool — runtime instance / emitter, 60 B (= POBJECT_FOR_GXD + scale[3]).
// WARNING scale[3]@0x00 is an IDA-only field (EU build) that shifts +12 B all
// other fields vs VeryOld's POBJECT (§3-C). Follow IDA, never VeryOld.
struct PtclPool {
    float     scale[3];      // +0x00  multiplies particle.pos at render time (default 1.0)
    int       initialized;   // +0x0C  update/render gate (=1 after AllocPool)
    PtclDef*  def;           // +0x10  source definition
    float     elapsedTime;   // +0x14  += dt; drives the texture frame index
    float     position[3];   // +0x18  world emission origin (copied from arg)
    float     rotation[3];   // +0x24  XYZ rotation in degrees -> world matrix
    float     spawnAccum;    // +0x30  fractional accumulator (spawns while >= 1)
    int       particleCount; // +0x34  capacity = ftol(lifetime*spawnRate)
    Particle* particles;     // +0x38  heap array of particleCount x 56 B
};
static_assert(sizeof(PtclPool) == 60, "PtclPool must be 60 B");
static_assert(offsetof(PtclPool, initialized)   == 0x0C, "initialized @0x0C");
static_assert(offsetof(PtclPool, def)           == 0x10, "def @0x10");
static_assert(offsetof(PtclPool, spawnAccum)    == 0x30, "spawnAccum @0x30");
static_assert(offsetof(PtclPool, particles)     == 0x38, "particles @0x38");

// ---------------------------------------------------------------------------
// Frame parameters for billboard rendering (PtclDef_RenderQuads 0x424430).
// The binary reads these values from GXD renderer globals (Object B
// g_GxdRenderer 0x18C4EF8); on the ClientSource side they are supplied by
// the caller (the renderer owns the camera and the VB, not this module).
struct ParticleFrameParams {
    IDirect3DDevice9* device      = nullptr; // g_GxdRenderer_pDevice 0x18C5104
    void*             sharedVB    = nullptr; // g_GxdRenderer_pSpritePool 0x18C5110 (locked CPU memory)
    int               maxQuads    = 0;       // g_MaxQuadsPerBatch 0x18C4F74 (quads/batch cap)
    float             camRight[3] = {1,0,0}; // flt_18C5264/68/6C (camera right x half-size)
    float             camUp[3]    = {0,1,0}; // flt_18C5270/74/78 (camera up x half-size)
    float             eye[3]      = {0,0,0}; // dword_18C51C0/C4/C8 (eye position, for LOD fade)
    float             fadeNear    = 0.0f;    // flt_18C4F08 (distance fade start)
    float             fadeFar     = 1.0f;    // flt_18C4F0C (distance fade end)

    // Frustum_ContainsPoint6 0x406430: frustum test on the pool origin
    // (whole-pool cull). The renderer owns the planes; injected here.
    // nullptr => always visible (no cull).
    bool (*frustumContains)(const float pos[3]) = nullptr;
};

// ---------------------------------------------------------------------------
// Shared VB — g_GxdRenderer_pSpritePool 0x18C5110. The binary uses ONE locked
// CPU buffer for all emitters; it also serves as a gate (AllocPool/Update/Render
// do nothing while it is null). MAIN installs it once the GXD renderer is ready.
void  SetParticleSharedVB(void* cpuBuffer);
void* ParticleSharedVB();

// Pre-fills the static uv (+0x10) of `maxQuads` quads (6 vertices/quad) in
// the shared VB: TL(0,0) TR(1,0) BL(0,1) / BL(0,1) TR(1,0) BR(1,1). RenderQuads
// never writes the uv — they MUST be set once by the VB owner
// (GxdRenderer). Faithful to the fact that 0x424430 only touches pos+color.
void  InitSharedQuadUVs(void* cpuBuffer, int maxQuads);

// ---------------------------------------------------------------------------
// RNG — Rng_Next 0x7603FD: MSVC LCG (state = 214013*state + 2531011;
// returns (state>>16) & 0x7FFF). The binary uses the CRT's per-thread rand
// state; here the algorithm is reproduced with a module-level state (seedable).
unsigned int Rng_Next();
void         SetRandSeed(unsigned int seed);

// Math_RandFloatRange 0x403330: a + (b-a)*((Rng_Next()%10001)/10000); swaps
// the bounds if b <= a (faithful to branch 0x40336D/0x4033A4).
float Math_RandFloatRange(float a, float b);

// Crt_ftol 0x760810: float -> int truncation (toward zero).
inline int Crt_ftol(double v) { return static_cast<int>(v); }

// ---------------------------------------------------------------------------
// Particle pool API (names = IDA anchors).

// PtclDef_Init 0x4221E0 / PtclDef_Reset 0x4222D0: zero-inits a def; sets the
// texture handle (texture[0x2C]) to -1 ("no texture" sentinel).
void PtclDef_Init(PtclDef* def);
void PtclDef_Reset(PtclDef* def);

// PtclDef_ClampParams 0x422F60: clamps texAnimFPS/spawnRate/baseSpeed/boxSize >=0,
// lifetime >= 0.01, colors 0..255, then DERIVES colorRate = (endColor-startColor)/lifetime.
void PtclDef_ClampParams(PtclDef* def);

// PtclDef_AllocPool 0x423280: (re)sizes the pool if uninitialized and if the
// shared VB + def.enabled are ready. particleCount = ftol(lifetime*spawnRate).
void PtclDef_AllocPool(PtclDef* def, PtclPool* pool);

// PtclDef_FreePool 0x423240: frees the array, initialized=0, def=0, scale=1.
void PtclDef_FreePool(PtclPool* pool);

// PtclDef_UpdateAndSpawn 0x423310: one simulation frame. `position`/`rotation`
// (3 floats each) are the emission origin/orientation, copied into the
// pool. Integrates age/pos/vel/param0/size/color, then spawns via the shape switch.
void PtclDef_UpdateAndSpawn(PtclPool* pool, const float position[3],
                            const float rotation[3], float dt);

// PtclDef_RenderQuads 0x424430: billboards live particles -> shared VB
// (`params.sharedVB`), DrawPrimitiveUP(TRIANGLELIST, stride 24). Distance LOD
// fade (draws fewer quads), capped at `params.maxQuads`.
void PtclDef_RenderQuads(PtclPool* pool, const ParticleFrameParams& params);

// PtclDef_ReadFile 0x422C50: deserializes a def from an open file HANDLE.
// `a3`/`a4` = opaque params passed to Tex_ReadPacked (archive context).
// Returns true on success (def.enabled = 1). The texture/motion hooks
// (Tex_ReadPacked/Anim_ReadMotionStream) are injectable; null => zero-read.
using TexReadPackedFn     = bool (*)(void* texture56, HANDLE hFile, int a3, int a4);
using MotionReadStreamFn  = bool (*)(void* motion16, HANDLE hFile);
void SetPtclIoHooks(TexReadPackedFn texHook, MotionReadStreamFn motionHook);
bool PtclDef_ReadFile(PtclDef* def, HANDLE hFile, int a3, int a4);

// GPU texture pointer of a def (SetTexture(0, *(def+0x38)) in RenderQuads).
IDirect3DTexture9* PtclDef_GetTexture(const PtclDef* def);

} // namespace ts2::gfx

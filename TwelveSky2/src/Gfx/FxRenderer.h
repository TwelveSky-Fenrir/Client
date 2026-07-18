// Gfx/FxRenderer.h — renders effect slots attached to entities (fx_slot).
//
// IDA ground truth: TwelveSky2.exe. Reproduces the render DISPATCH of an effect
// slot (weapon-glow / aura / muzzle / hit / …) by TYPE (1..14) and by PASS.
//
// Reversed anchors:
//   Fx_EmitterDraw      0x585E30  switch(type) -> ModelObj_Draw (mesh) or particle
//                                 render; called 3x/frame (passes 1,2,3)
//                                 from Scene_InGameRender 0x52D0B0.
//   Fx_AttachSlotClear  0x584220  reset: state=0 + Particle_Free(pool@+132).
//   Fx_EmitterClear     0x584180  "sub-item" reset (pool ctor/dtor inline).
//   ModelObj_Draw       0x4D71B0  draws a mesh object (model subsystem).
//   Particle_EnsureLoadedThenRender 0x4D9F90  lazy-load SObject -> Particle_RenderBillboards.
//   Particle_RenderBillboards       0x6A70B0  pool billboards (Object A variant).
//   Particle_Free       0x6A6FF0  frees the pool's array (POBJECT, 48 B).
//
// The particle pool of a slot (at +132) follows the **POBJECT 48 B** layout
//   (Object A: PtclPool WITHOUT the scale[3] prefix, offsets shifted -12:
//   initialized@+0, def@+4, particleCount@+0x28, particles@+0x2C). This is a
//   DISTINCT layout from ParticleSystem.h's 60 B PtclPool (Object B). It is not
//   merged: slot billboard rendering goes through an injected hook
//   (Particle_RenderBillboards uses the Object A renderer's globals).
//
// The slot's full layout is NOT entirely proven: only the fields read by
//   Fx_EmitterDraw/AttachSlotClear are (header + mesh indices + param + position +
//   orientation + particle def index + pool). Intermediate regions remain
//   `_gap` (unproven) — no invention (Rosetta §0).
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>

struct IDirect3DDevice9;   // forward (avoids pulling d3d9.h into this lightweight header)

namespace ts2::gfx {

// ---------------------------------------------------------------------------
// FxSlot — container for an FX attach slot. Offsets PROVEN by Fx_EmitterDraw
// 0x585E30 (dword this[N]) and Fx_AttachSlotClear 0x584220 (Particle_Free this+33).
#pragma pack(push, 4)
struct FxSlot {
    int32_t state;          // +0x00  0 = empty slot (render gate, 0x585E3C)
    int32_t type;           // +0x04  effect type 1..14 (switch selector)
    uint8_t _gap08[0x2C];   // +0x08..+0x33  unproven region
    int32_t meshIdxA;       // +0x34  external mesh index (avatar bank, ×150368)
    int32_t meshIdxB;       // +0x38  mid index (avatar bank, ×75184)
    int32_t meshIdxC;       // +0x3C  mesh slot index (×148, all banks)
    float   drawParam;      // +0x40  float parameter passed to ModelObj_Draw (a3)
    float   position[3];    // +0x44  world position (a4 = ModelObj_Draw's dword*)
    float   orient[12];     // +0x50  orientation (a5 = float*; exact shape NOT proven)
    int32_t ptclDefIndex;   // +0x80  particle def index (byte_1151CBC[336·idx])
    uint8_t ptclPool[48];   // +0x84  inline 48 B POBJECT pool (Object A, see above)
};
#pragma pack(pop)
static_assert(offsetof(FxSlot, type)         == 0x04, "type @0x04");
static_assert(offsetof(FxSlot, meshIdxA)     == 0x34, "meshIdxA @0x34");
static_assert(offsetof(FxSlot, meshIdxC)     == 0x3C, "meshIdxC @0x3C");
static_assert(offsetof(FxSlot, drawParam)    == 0x40, "drawParam @0x40");
static_assert(offsetof(FxSlot, position)     == 0x44, "position @0x44");
static_assert(offsetof(FxSlot, ptclDefIndex) == 0x80, "ptclDefIndex @0x80 (this[32])");
static_assert(offsetof(FxSlot, ptclPool)     == 0x84, "ptclPool @0x84 (this+33 dwords = +132)");

// Mesh bank targeted by type (external bases unk_A71410/B551B8/B60AB8).
enum class FxMeshBank {
    AvatarA,   // types 1/2: &unk_A71410 + 150368·A + 75184·B + 148·C
    NpcB,      // types 3/4: &unk_B551B8 + 148·C
    MiscC,     // types 8/9/A/C/D: &unk_B60AB8 + 148·C
};

// ---------------------------------------------------------------------------
// Hooks to subsystems NOT owned here (resolved by MAIN at wiring time).
//
// Draws a mesh object (ModelObj_Draw 0x4D71B0). `bank`+indices locate the
// mesh in the external bank; `pass` = pass (1 or 2); `drawParam`/`pos`/
// `orient` = original a3/a4/a5 args.
using FxModelObjDrawFn = void (*)(FxMeshBank bank, int idxA, int idxB, int idxC,
                                  int pass, float drawParam,
                                  const float pos[3], const float* orient);

// Renders a slot's particles (Particle_EnsureLoadedThenRender 0x4D9F90:
// lazy-load of SObject byte_1151CBC[336·ptclDefIndex] then Particle_RenderBillboards
// 0x6A70B0 on the 48 B POBJECT pool). Injected because it depends on the Object A renderer.
using FxParticleRenderFn = void (*)(int ptclDefIndex, void* pool48);

void SetFxRenderHooks(FxModelObjDrawFn meshDraw, FxParticleRenderFn particleRender);

// ---------------------------------------------------------------------------
// DEFAULT wiring of the hooks onto the Object A LEAF billboard (Gfx/FxBillboard).
//
// Frame anchor for the particle render: mirrors the Object A renderer's globals read by
// Particle_RenderBillboards 0x6A70B0 (world billboard basis flt_8001D4..E8 = camera right/up,
// device dword_800074, quad ceiling dword_7FFEE0). MAIN sets these ONCE per frame, before the
// 3 Fx_EmitterDraw passes (mirrors Scene_InGameRender 0x52D0B0). `frustum` = Cam_FrustumTestPoint6
// 0x69ED30 (nullptr => always visible).
using FxFrustumHook = bool (*)(const float pos[3]);
void Fx_SetParticleFrame(IDirect3DDevice9* device, const float right[3], const float up[3],
                         int maxQuads, FxFrustumHook frustum);

// Wires s_particleRender -> FxBillboard_PoolRender (= Particle_EnsureLoadedThenRender 0x4D9F90
// -> Particle_RenderBillboards 0x6A70B0). s_meshDraw stays NULL (ModelObj_Draw 0x4D71B0 = model
// subsystem not ported at this milestone -> mesh FX types 1-4/8-D stay invisible, TODO anchor).
void Fx_WireLeafHooks();

// ---------------------------------------------------------------------------
// Fx_EmitterDraw 0x585E30 — renders ONE slot for pass `pass` (1 and 2 = meshes,
// 3 = particles). No-op if the slot is empty or the type is unknown.
void Fx_EmitterDraw(FxSlot* slot, int pass);

// Fx_AttachSlotClear 0x584220 — reset before filling: state=0 then frees the
// pool's inline particle array (reproduces Particle_Free 0x6A6FF0).
void Fx_AttachSlotClear(FxSlot* slot);

// Fx_EmitterClear 0x584180 — "sub-item" reset: clears state + the pool's
// pointers (initialized/def/particles) WITHOUT freeing the array (ctor+dtor
// FxEmitter_SubItemCtor/Dtor 0x6A6FD0/0x6A6FE0). Faithful: no HeapFree here.
void Fx_EmitterClear(FxSlot* slot);

} // namespace ts2::gfx

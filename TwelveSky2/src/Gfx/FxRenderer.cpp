// Gfx/FxRenderer.cpp — FAITHFUL implementation of the FX slot render dispatch.
//
// IDA ground truth: TwelveSky2.exe. Every block cites its anchor (name + 0xADDR).
// See FxRenderer.h for the slot map and boundaries (48 B POBJECT pool).
#include "FxRenderer.h"
#include "FxBillboard.h"   // FxBillboard_PoolRender, FxParticlePool (LEAF Object A) — for Fx_WireLeafHooks
#include "ModelObjectRenderer.h" // FRONT F_MOBJ: ModelObjectRenderer_MeshDrawShim (= ModelObj_Draw 0x4D71B0)
#include <cstring>   // std::memcpy

namespace ts2::gfx {

// ---------------------------------------------------------------------------
// External hooks (mesh / Object A particle subsystems not owned here).
static FxModelObjDrawFn   s_meshDraw       = nullptr; // ModelObj_Draw 0x4D71B0
static FxParticleRenderFn s_particleRender = nullptr; // Particle_EnsureLoadedThenRender 0x4D9F90

void SetFxRenderHooks(FxModelObjDrawFn meshDraw, FxParticleRenderFn particleRender) {
    s_meshDraw = meshDraw; s_particleRender = particleRender;
}

// ---------------------------------------------------------------------------
// Frame anchor for the particle render (mirrors the Object A renderer's globals).
static IDirect3DDevice9* s_fxDevice   = nullptr;                 // dword_800074
static float             s_fxRight[3] = { 1.0f, 0.0f, 0.0f };    // flt_8001D4/D8/DC (world camera right)
static float             s_fxUp[3]    = { 0.0f, 1.0f, 0.0f };    // flt_8001E0/E4/E8 (world camera up)
static int               s_fxMaxQuads = 0;                       // dword_7FFEE0 (0 => no ceiling)
static FxFrustumHook     s_fxFrustum  = nullptr;                 // Cam_FrustumTestPoint6 0x69ED30

void Fx_SetParticleFrame(IDirect3DDevice9* device, const float right[3], const float up[3],
                         int maxQuads, FxFrustumHook frustum) {
    s_fxDevice = device;
    if (right) { s_fxRight[0] = right[0]; s_fxRight[1] = right[1]; s_fxRight[2] = right[2]; }
    if (up)    { s_fxUp[0]    = up[0];    s_fxUp[1]    = up[1];    s_fxUp[2]    = up[2];    }
    s_fxMaxQuads = maxQuads;
    s_fxFrustum  = frustum;
}

// Terminal s_particleRender — lazy-load + billboard render of the 48 B POBJECT pool (LEAF Object A).
// Mirrors Particle_EnsureLoadedThenRender 0x4D9F90: (&byte_1151CBC[336·idx], pool@+132).
static void LeafParticleRender(int ptclDefIndex, void* pool48) {
    FxBillboard_PoolRender(reinterpret_cast<FxParticlePool*>(pool48), ptclDefIndex,
                           s_fxDevice, s_fxRight, s_fxUp, s_fxMaxQuads, s_fxFrustum);
}

void Fx_WireLeafHooks() {
    // FRONT F_MOBJ (2026-07-17): s_meshDraw is now WIRED to the model-object renderer
    // (ModelObjectRenderer_MeshDrawShim = ModelObj_Draw 0x4D71B0, MiscC bank E*.MOBJECT). Mesh FX
    // types 8/9/0xA (Deflect/BlockGuard/Parry) become VISIBLE in passes 1/2 as soon as MAIN has
    // done ModelObjectRenderer::Init (the shim is a no-op until an active renderer is registered,
    // so wiring order doesn't matter). Types 1-4 (Avatar/NpcB banks): TODO anchor on the renderer
    // side (V1 = MiscC only). The particle path (types 5-7/0xB/0xE) stays complete via the LEAF
    // Object A (s_particleRender unchanged).
    SetFxRenderHooks(&ModelObjectRenderer_MeshDrawShim, &LeafParticleRender);
}

// ---------------------------------------------------------------------------
// Fx_EmitterDraw 0x585E30 — switch(type) gated by the pass.
void Fx_EmitterDraw(FxSlot* slot, int pass) {
    if (!slot->state) return;                       // 0x585E3C: if (*this)

    switch (slot->type) {                           // 0x585E65: switch (this+4)
        case 1:                                     // 0x585E65
        case 2:
            // Meshes passes 1/2 -> avatar bank (3 indices). ModelObj_Draw @0x585E7D:
            //   base = &unk_A71410 + 150368·A + 75184·B + 148·C.
            if (pass >= 1 && pass <= 2 && s_meshDraw)
                s_meshDraw(FxMeshBank::AvatarA, slot->meshIdxA, slot->meshIdxB, slot->meshIdxC,
                           pass, slot->drawParam, slot->position, slot->orient);
            break;

        case 3:                                     // 0x585E65
        case 4:
            // ModelObj_Draw @0x585F49: base = &unk_B551B8 + 148·C.
            if (pass >= 1 && pass <= 2 && s_meshDraw)
                s_meshDraw(FxMeshBank::NpcB, slot->meshIdxA, slot->meshIdxB, slot->meshIdxC,
                           pass, slot->drawParam, slot->position, slot->orient);
            break;

        case 8:                                     // 0x585E65
        case 9:
        case 0xA:
        case 0xC:
        case 0xD:
            // ModelObj_Draw @0x586219: base = &unk_B60AB8 + 148·C.
            if (pass >= 1 && pass <= 2 && s_meshDraw)
                s_meshDraw(FxMeshBank::MiscC, slot->meshIdxA, slot->meshIdxB, slot->meshIdxC,
                           pass, slot->drawParam, slot->position, slot->orient);
            break;

        case 5:                                     // 0x585E65
        case 6:
        case 7:
        case 0xB:
        case 0xE:
            // Particles pass 3, if the pool exists (*(this+132) != 0 = pool.initialized).
            // Particle_EnsureLoadedThenRender @0x586266:
            //   (&byte_1151CBC[336·ptclDefIndex], pool@+132).
            if (pass == 3) {                        // 0x585FDC/…: if (a2 == 3 && *(this+132))
                uint32_t poolFirst = 0;
                std::memcpy(&poolFirst, slot->ptclPool, 4); // pool POBJECT +0 = initialized
                if (poolFirst != 0 && s_particleRender)
                    s_particleRender(slot->ptclDefIndex, slot->ptclPool);
            }
            break;

        default:                                    // def_585E65
            return;
    }
}

// ---------------------------------------------------------------------------
// Particle_Free 0x6A6FF0 — frees the 48 B POBJECT pool's array (initialized@+0,
// def@+4, particles@+0x2C). Reproduced inline (layout fully proven).
static void Fx_PoolFree(uint8_t* pool48) {
    void* particles = nullptr;
    std::memcpy(&particles, pool48 + 0x2C, sizeof(particles)); // this[11] @0x6A6FF3
    const int32_t zero = 0;
    std::memcpy(pool48 + 0x00, &zero, 4);           // *this = 0        @0x6A6FF8
    std::memcpy(pool48 + 0x04, &zero, 4);           // *(this+1) = 0    @0x6A6FFE
    if (particles) {                                // 0x6A7005
        HeapFree(GetProcessHeap(), 0, particles);   // 0x6A7011
        void* null = nullptr;
        std::memcpy(pool48 + 0x2C, &null, sizeof(null)); // *(this+11) = 0  @0x6A7017
    }
}

// ---------------------------------------------------------------------------
// Fx_AttachSlotClear 0x584220 — state=0 then Particle_Free(pool@+132).
void Fx_AttachSlotClear(FxSlot* slot) {
    slot->state = 0;                                // 0x58422A: *this = 0
    Fx_PoolFree(slot->ptclPool);                    // 0x58423E: Particle_Free(this+33)
}

// ---------------------------------------------------------------------------
// Fx_EmitterClear 0x584180 — clears state + pool pointers WITHOUT HeapFree
// (FxEmitter_SubItemCtor/Dtor 0x6A6FD0/0x6A6FE0: reset +0/+4/+0x2C to 0).
void Fx_EmitterClear(FxSlot* slot) {
    const int32_t zero = 0;
    void* null = nullptr;
    // FxEmitter_SubItemCtor(this+33) @0x5841AF: pool[+0]=+4=+0x2C = 0.
    std::memcpy(slot->ptclPool + 0x00, &zero, 4);
    std::memcpy(slot->ptclPool + 0x04, &zero, 4);
    std::memcpy(slot->ptclPool + 0x2C, &null, sizeof(null));
    slot->state = 0;                                // 0x5841BE: *this = 0
    // FxEmitter_SubItemDtor(this+33) @0x5841CD: same fields (idempotent).
    std::memcpy(slot->ptclPool + 0x00, &zero, 4);
    std::memcpy(slot->ptclPool + 0x04, &zero, 4);
    std::memcpy(slot->ptclPool + 0x2C, &null, sizeof(null));
}

} // namespace ts2::gfx

// Gfx/FxRenderer.cpp — implémentation FIDÈLE du dispatch de rendu des slots FX.
//
// Vérité IDA : TwelveSky2.exe. Chaque bloc cite son ancre (nom + 0xADDR).
// Voir FxRenderer.h pour la carte du slot et les frontières (pool POBJECT 48 o).
#include "FxRenderer.h"
#include <cstring>   // std::memcpy

namespace ts2::gfx {

// ---------------------------------------------------------------------------
// Hooks externes (sous-systèmes mesh / particules Object A non possédés).
static FxModelObjDrawFn   s_meshDraw       = nullptr; // ModelObj_Draw 0x4D71B0
static FxParticleRenderFn s_particleRender = nullptr; // Particle_EnsureLoadedThenRender 0x4D9F90

void SetFxRenderHooks(FxModelObjDrawFn meshDraw, FxParticleRenderFn particleRender) {
    s_meshDraw = meshDraw; s_particleRender = particleRender;
}

// ---------------------------------------------------------------------------
// Fx_EmitterDraw 0x585E30 — switch(type) gaté par la passe.
void Fx_EmitterDraw(FxSlot* slot, int pass) {
    if (!slot->state) return;                       // 0x585E3C : if (*this)

    switch (slot->type) {                           // 0x585E65 : switch (this+4)
        case 1:                                     // 0x585E65
        case 2:
            // Meshes passes 1/2 → banque avatar (3 indices). ModelObj_Draw @0x585E7D :
            //   base = &unk_A71410 + 150368·A + 75184·B + 148·C.
            if (pass >= 1 && pass <= 2 && s_meshDraw)
                s_meshDraw(FxMeshBank::AvatarA, slot->meshIdxA, slot->meshIdxB, slot->meshIdxC,
                           pass, slot->drawParam, slot->position, slot->orient);
            break;

        case 3:                                     // 0x585E65
        case 4:
            // ModelObj_Draw @0x585F49 : base = &unk_B551B8 + 148·C.
            if (pass >= 1 && pass <= 2 && s_meshDraw)
                s_meshDraw(FxMeshBank::NpcB, slot->meshIdxA, slot->meshIdxB, slot->meshIdxC,
                           pass, slot->drawParam, slot->position, slot->orient);
            break;

        case 8:                                     // 0x585E65
        case 9:
        case 0xA:
        case 0xC:
        case 0xD:
            // ModelObj_Draw @0x586219 : base = &unk_B60AB8 + 148·C.
            if (pass >= 1 && pass <= 2 && s_meshDraw)
                s_meshDraw(FxMeshBank::MiscC, slot->meshIdxA, slot->meshIdxB, slot->meshIdxC,
                           pass, slot->drawParam, slot->position, slot->orient);
            break;

        case 5:                                     // 0x585E65
        case 6:
        case 7:
        case 0xB:
        case 0xE:
            // Particules passe 3, si le pool existe (*(this+132) != 0 = pool.initialized).
            // Particle_EnsureLoadedThenRender @0x586266 :
            //   (&byte_1151CBC[336·ptclDefIndex], pool@+132).
            if (pass == 3) {                        // 0x585FDC/… : if (a2 == 3 && *(this+132))
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
// Particle_Free 0x6A6FF0 — libère le tableau du pool POBJECT 48 o (initialized@+0,
// def@+4, particles@+0x2C). Reproduit inline (layout entièrement prouvé).
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
// Fx_AttachSlotClear 0x584220 — state=0 puis Particle_Free(pool@+132).
void Fx_AttachSlotClear(FxSlot* slot) {
    slot->state = 0;                                // 0x58422A : *this = 0
    Fx_PoolFree(slot->ptclPool);                    // 0x58423E : Particle_Free(this+33)
}

// ---------------------------------------------------------------------------
// Fx_EmitterClear 0x584180 — annule state + pointeurs du pool SANS HeapFree
// (FxEmitter_SubItemCtor/Dtor 0x6A6FD0/0x6A6FE0 : remettent +0/+4/+0x2C à 0).
void Fx_EmitterClear(FxSlot* slot) {
    const int32_t zero = 0;
    void* null = nullptr;
    // FxEmitter_SubItemCtor(this+33) @0x5841AF : pool[+0]=+4=+0x2C = 0.
    std::memcpy(slot->ptclPool + 0x00, &zero, 4);
    std::memcpy(slot->ptclPool + 0x04, &zero, 4);
    std::memcpy(slot->ptclPool + 0x2C, &null, sizeof(null));
    slot->state = 0;                                // 0x5841BE : *this = 0
    // FxEmitter_SubItemDtor(this+33) @0x5841CD : mêmes champs (idempotent).
    std::memcpy(slot->ptclPool + 0x00, &zero, 4);
    std::memcpy(slot->ptclPool + 0x04, &zero, 4);
    std::memcpy(slot->ptclPool + 0x2C, &null, sizeof(null));
}

} // namespace ts2::gfx

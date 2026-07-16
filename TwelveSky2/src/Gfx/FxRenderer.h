// Gfx/FxRenderer.h — rendu des slots d'effet attachés aux entités (fx_slot).
//
// Vérité IDA : TwelveSky2.exe. Reproduit le DISPATCH de rendu d'un slot d'effet
// (weapon-glow / aura / muzzle / hit / …) par TYPE (1..14) et par PASSE.
//
// Ancres reversées :
//   Fx_EmitterDraw      0x585E30  switch(type) → ModelObj_Draw (mesh) ou rendu
//                                 particules ; appelé 3×/frame (passes 1,2,3)
//                                 depuis Scene_InGameRender 0x52D0B0.
//   Fx_AttachSlotClear  0x584220  reset : state=0 + Particle_Free(pool@+132).
//   Fx_EmitterClear     0x584180  reset « sous-item » (ctor/dtor du pool inline).
//   ModelObj_Draw       0x4D71B0  dessin d'un objet-mesh (sous-système modèle).
//   Particle_EnsureLoadedThenRender 0x4D9F90  lazy-load SObject → Particle_RenderBillboards.
//   Particle_RenderBillboards       0x6A70B0  billboards du pool (variante Object A).
//   Particle_Free       0x6A6FF0  libère le tableau du pool POBJECT (48 o).
//
// ⚠ Le pool de particules d'un slot (à +132) suit la disposition **POBJECT
//   48 o** (Object A : PtclPool SANS le préfixe scale[3], offsets décalés -12 :
//   initialized@+0, def@+4, particleCount@+0x28, particles@+0x2C). C'est une
//   disposition DISTINCTE du PtclPool 60 o de ParticleSystem.h (Object B). On ne
//   la fusionne pas : le rendu billboard des slots passe par un hook injecté
//   (Particle_RenderBillboards utilise les globales du renderer Object A).
//
// ⚠ Le layout complet du slot n'est PAS entièrement prouvé : seuls les champs
//   lus par Fx_EmitterDraw/AttachSlotClear le sont (en-tête + indices mesh +
//   param + position + orientation + index def particule + pool). Les régions
//   intermédiaires restent des `_gap` (non prouvé) — pas d'invention (§0 Rosetta).
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>

namespace ts2::gfx {

// ---------------------------------------------------------------------------
// FxSlot — conteneur d'un slot d'attache FX. Offsets PROUVÉS par Fx_EmitterDraw
// 0x585E30 (dword this[N]) et Fx_AttachSlotClear 0x584220 (Particle_Free this+33).
#pragma pack(push, 4)
struct FxSlot {
    int32_t state;          // +0x00  0 = slot vide (gate de rendu, 0x585E3C)
    int32_t type;           // +0x04  type d'effet 1..14 (selector du switch)
    uint8_t _gap08[0x2C];   // +0x08..+0x33  région non prouvée
    int32_t meshIdxA;       // +0x34  index externe mesh (banque avatar, ×150368)
    int32_t meshIdxB;       // +0x38  index mid (banque avatar, ×75184)
    int32_t meshIdxC;       // +0x3C  index de slot mesh (×148, toutes banques)
    float   drawParam;      // +0x40  paramètre float passé à ModelObj_Draw (a3)
    float   position[3];    // +0x44  position monde (a4 = dword* de ModelObj_Draw)
    float   orient[12];     // +0x50  orientation (a5 = float* ; forme exacte NON prouvée)
    int32_t ptclDefIndex;   // +0x80  index def particule (byte_1151CBC[336·idx])
    uint8_t ptclPool[48];   // +0x84  pool POBJECT 48 o inline (Object A, cf. ci-dessus)
};
#pragma pack(pop)
static_assert(offsetof(FxSlot, type)         == 0x04, "type @0x04");
static_assert(offsetof(FxSlot, meshIdxA)     == 0x34, "meshIdxA @0x34");
static_assert(offsetof(FxSlot, meshIdxC)     == 0x3C, "meshIdxC @0x3C");
static_assert(offsetof(FxSlot, drawParam)    == 0x40, "drawParam @0x40");
static_assert(offsetof(FxSlot, position)     == 0x44, "position @0x44");
static_assert(offsetof(FxSlot, ptclDefIndex) == 0x80, "ptclDefIndex @0x80 (this[32])");
static_assert(offsetof(FxSlot, ptclPool)     == 0x84, "ptclPool @0x84 (this+33 dwords = +132)");

// Banque de meshes ciblée selon le type (bases externes unk_A71410/B551B8/B60AB8).
enum class FxMeshBank {
    AvatarA,   // types 1/2 : &unk_A71410 + 150368·A + 75184·B + 148·C
    NpcB,      // types 3/4 : &unk_B551B8 + 148·C
    MiscC,     // types 8/9/A/C/D : &unk_B60AB8 + 148·C
};

// ---------------------------------------------------------------------------
// Hooks vers les sous-systèmes NON possédés (résolus par MAIN au câblage).
//
// Dessin d'un objet-mesh (ModelObj_Draw 0x4D71B0). `bank`+indices localisent le
// mesh dans la banque externe ; `pass` = passe (1 ou 2) ; `drawParam`/`pos`/
// `orient` = args a3/a4/a5 d'origine.
using FxModelObjDrawFn = void (*)(FxMeshBank bank, int idxA, int idxB, int idxC,
                                  int pass, float drawParam,
                                  const float pos[3], const float* orient);

// Rendu des particules d'un slot (Particle_EnsureLoadedThenRender 0x4D9F90 :
// lazy-load du SObject byte_1151CBC[336·ptclDefIndex] puis Particle_RenderBillboards
// 0x6A70B0 sur le pool POBJECT 48 o). Injecté car il dépend du renderer Object A.
using FxParticleRenderFn = void (*)(int ptclDefIndex, void* pool48);

void SetFxRenderHooks(FxModelObjDrawFn meshDraw, FxParticleRenderFn particleRender);

// ---------------------------------------------------------------------------
// Fx_EmitterDraw 0x585E30 — rend UN slot pour la passe `pass` (1 et 2 = meshes,
// 3 = particules). No-op si le slot est vide ou le type inconnu.
void Fx_EmitterDraw(FxSlot* slot, int pass);

// Fx_AttachSlotClear 0x584220 — reset avant remplissage : state=0 puis libère le
// tableau de particules du pool inline (reproduit Particle_Free 0x6A6FF0).
void Fx_AttachSlotClear(FxSlot* slot);

// Fx_EmitterClear 0x584180 — reset « sous-item » : annule state + les pointeurs
// du pool (initialized/def/particles) SANS libérer le tableau (ctor+dtor
// FxEmitter_SubItemCtor/Dtor 0x6A6FD0/0x6A6FE0). Fidèle : pas de HeapFree ici.
void Fx_EmitterClear(FxSlot* slot);

} // namespace ts2::gfx

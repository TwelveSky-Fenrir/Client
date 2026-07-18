// Gfx/FxSetters.cpp — global pool of FX slots + combat Fx_Attach* setters.
//
// IDA ground truth: TwelveSky2.exe (imagebase 0x400000). Every block cites its anchor (name + 0xADDR).
// See FxSetters.h for the overview and Docs/TS2_EXTRACT_FX_COMBAT.md for the spec.
//
// Slot write fidelity: setters write `*(this+N)` (DWORD index) EXACTLY like the decompiled
// code. The FxSlot is therefore accessed via a reinterpreted `uint32_t*`/`float*` (the offsets
// are proven and frozen by FxRenderer.h — static_assert). The inline 48 B POBJECT pool (at
// this+33 = +0x84) is driven by the stage-1 LEAF Object A (Gfx/FxBillboard).
#include "Gfx/FxSetters.h"
#include "Gfx/FxBillboard.h"   // FxParticlePool (48 B POBJECT), FxBillboard_PoolInit (SObject_UpdateK)
#include <cstring>

namespace ts2::gfx {
namespace {

// -------------------------------------------------------------------------------------------
// THE single pool dword_17D06F4 (1000 slots × 256 B) + counter g_FxAuraCount 0x168722C.
// The module OWNS the static array (zero-initialized -> all slots free at startup).
// -------------------------------------------------------------------------------------------
FxSlot s_fxSlots[kFxSlotCount];

// g_Options 0x84DEC0 (= GameOptions.ShowSkillEffects idx0): gates HitSpark/HitBurst. Ready by default.
bool s_optionsReady = true;

} // namespace

// =============================================================================================
//  POOL
// =============================================================================================
FxSlot* FxPool_Slots() { return s_fxSlots; }              // &dword_17D06F4[0]
int     FxPool_Count() { return kFxSlotCount; }           // g_FxAuraCount 0x168722C

// Scan for the 1st free slot — pattern shared by all producers (e.g. @0x55ab24):
//   for (j=0; j<g_FxAuraCount && *(this+(j<<8)+1355692); ++j); if (j<g_FxAuraCount) ...
int FxPool_FindFreeSlot() {
    for (int j = 0; j < kFxSlotCount; ++j)
        if (s_fxSlots[j].state == 0)   // slot[j].state == 0 -> free (while-state loop stops)
            return j;
    return -1;                          // pool full (equivalent to j == g_FxAuraCount -> no attach)
}

// Pkt_EnterWorld 0x464160 @0x4642A4: for (i<g_FxAuraCount) Fx_AttachSlotClear(&dword_17D06F4[64·i]).
void FxPool_Reset() {
    for (int i = 0; i < kFxSlotCount; ++i)
        Fx_AttachSlotClear(&s_fxSlots[i]);   // state=0 + Particle_Free(pool@+132)  (FxRenderer.cpp)
}

void Fx_SetOptionsReady(bool ready) { s_optionsReady = ready; }

// =============================================================================================
//  PARTICLE SETTERS (type 5/6/7)
// =============================================================================================

// Fx_AttachMuzzleFlash 0x584260 — muzzle (type 5, subtype 4).
void Fx_AttachMuzzleFlash(FxSlot* slot, const FxEntitySource& e, int side) {
    if (!(side >= 1 && side <= 2 && e.modelReady)) return;   // gate @0x58427d
    Fx_AttachSlotClear(slot);                                 // 0x58428b
    uint32_t* d = reinterpret_cast<uint32_t*>(slot);
    d[0] = 1;                                                 // active state      0x584293
    d[1] = 5;                                                 // type 5 (particle) 0x58429c
    d[2] = 1;                                                 // flag              0x5842a6
    d[3] = e.idHi;                                            // a2[1]             0x5842b6
    d[4] = e.idLo;                                            // a2[2]             0x5842c2
    // *(this+30) = &flt_FABB5C[72·a2[23] + 36·a2[24]] (0x5842e7): bone anchor (model table
    // flt_FABB5C 0xFABB5C, model subsystem not ported). NOT read by the particle path
    // (Docs/TS2_EXTRACT_FX_COMBAT.md §4) -> 0 here. TODO(anchor): wire for the emission position.
    d[30] = 0;
    d[31] = 4;                                                // muzzle subtype    0x58431a
    d[32] = (side != 1) ? 1u : 0u;                            // def index (0/1)   0x58430b
    // SObject_UpdateK(&byte_1151CBC[336·*(this+32)], this+33) 0x58434c = ensure-load + Particle_Init.
    FxBillboard_PoolInit(reinterpret_cast<FxParticlePool*>(slot->ptclPool), static_cast<int>(d[32]));
}

// Fx_AttachMuzzleVariant 0x584360 — directional variant (type 5, flag 2, 7 variants).
void Fx_AttachMuzzleVariant(FxSlot* slot, const FxEntitySource& e, int variant) {
    if (!(variant >= 1 && variant <= 7)) return;             // gate @0x584373
    Fx_AttachSlotClear(slot);                                 // 0x58437d
    uint32_t* d = reinterpret_cast<uint32_t*>(slot);
    d[0] = 1;                                                 // 0x584385
    d[1] = 5;                                                 // type 5            0x58438e
    d[2] = 2;                                                 // flag 2            0x584398
    d[3] = e.idHi;                                            // 0x5843a8
    d[4] = e.idLo;                                            // 0x5843b4
    // *(this+30) = &flt_FF67CC[36·(*(a2[24]+244)) - 36] (0x5843d5): variant anchor (table
    // flt_FF67CC 0xFF67CC via dereference of model def a2[24]+244). Model subsystem,
    // NOT read by the particle path -> 0 here. TODO(anchor).
    d[30] = 0;
    // switch(variant) @0x5843f4: (subtype this+31, def index this+32).
    int subType, defIndex;
    switch (variant) {
        case 1: subType = 1; defIndex = 0; break;   // 0x5843fe/0x584408
        case 2: subType = 2; defIndex = 0; break;   // 0x58441a/0x584424
        case 3: subType = 3; defIndex = 0; break;   // 0x584433/0x58443d
        case 4: subType = 1; defIndex = 1; break;   // 0x58444c/0x584456
        case 5: subType = 2; defIndex = 1; break;   // 0x584465/0x58446f
        case 6: subType = 3; defIndex = 1; break;   // 0x58447e/0x584488
        case 7: subType = 0; defIndex = 2; break;   // 0x584497/0x5844a1
        default: return;                            // (unreachable: gate 1..7)
    }
    d[31] = static_cast<uint32_t>(subType);
    d[32] = static_cast<uint32_t>(defIndex);
    FxBillboard_PoolInit(reinterpret_cast<FxParticlePool*>(slot->ptclPool), defIndex); // 0x5844c9
}

// Fx_AttachDashTrail 0x585D50 — MONSTER dash trail (type 5, flag 2). EXACT twin of
// Fx_AttachMuzzleVariant: same slot header then FxBillboard_PoolInit (= SObject_UpdateK) on
// def 18 (side 1) / 19 (side 2). Producer = Char_SetupAuraFlags 0x5814F0 (sole caller
// Pkt_SpawnMonster 0x467B00 @0x467DA6). Difference from the muzzle: NO gate up front — the
// binary clears the slot, writes the header, and only RE-clears it (state 0) if `side` is
// neither 1 nor 2 (unreachable branch in practice: Char_SetupAuraFlags only ever passes 1 or 2).
void Fx_AttachDashTrail(FxSlot* slot, const FxEntitySource& e, int side) {
    Fx_AttachSlotClear(slot);                                 // 0x585d5c
    uint32_t* d = reinterpret_cast<uint32_t*>(slot);
    d[0] = 1;                                                 // active state      0x585d64
    d[1] = 5;                                                 // type 5 (particle) 0x585d6d
    d[2] = 2;                                                 // flag 2            0x585d77
    d[3] = e.idHi;                                            // a3[1]             0x585d87
    d[4] = e.idLo;                                            // a3[2]             0x585d93
    // *(this+30) = &flt_FF67CC[36·(*(a3[24]+244)) - 36] (0x585db4): model anchor (deref of
    // monster def a3[24]+244 = kindIndexP1). Table flt_FF67CC 0xFF67CC, model subsystem not
    // ported, NOT read by the particle path (like the muzzle) -> 0 here. TODO(anchor).
    d[30] = 0;
    if (side == 1) {                                          // 0x585dc1
        d[31] = 1;                                            // subtype           0x585dce
        d[32] = 18;                                           // def index side 1  0x585dd8
    } else if (side == 2) {                                   // 0x585dc7
        d[31] = 1;                                            // subtype           0x585de7
        d[32] = 19;                                           // def index side 2  0x585df1
    } else {
        Fx_AttachSlotClear(slot);                             // invalid side: re-clears (state 0) 0x585e00
        return;                                               // 0x585e05
    }
    // SObject_UpdateK(&byte_1151CBC[336·*(this+32)], this+33) 0x585e25 = ensure-load + Particle_Init.
    FxBillboard_PoolInit(reinterpret_cast<FxParticlePool*>(slot->ptclPool), static_cast<int>(d[32]));
}

// Fx_AttachHitSpark 0x5844F0 — impact spark (type 6). def by modelClass (a2[23]).
void Fx_AttachHitSpark(FxSlot* slot, const FxEntitySource& e) {
    if (!(s_optionsReady && e.modelReady)) return;           // gate @0x58450a (g_Options && a2[6])
    Fx_AttachSlotClear(slot);                                 // 0x584518
    uint32_t* d = reinterpret_cast<uint32_t*>(slot);
    d[0] = 1;                                                 // 0x584520
    d[1] = 6;                                                 // type 6            0x584529
    d[2] = 1;                                                 // 0x584533
    d[3] = e.idHi;                                            // 0x584543
    d[4] = e.idLo;                                            // 0x58454f
    const int v2 = e.modelClass;                              // a2[23]            0x584558
    if (v2) {                                                 // 0x58455f
        if (v2 == 1)      d[32] = 4;                          // 0x584581
        else if (v2 == 2) d[32] = 5;                          // 0x584590
        // v2 >= 3: *(this+32) is NOT written (binary quirk) — modelClass ∈ {0,1,2} in practice
        // (aTribe RACE 0..2), so this branch is unreachable; d[32] keeps its prior value (0 on first use).
    } else {
        d[32] = 3;                                            // 0x584572
    }
    FxBillboard_PoolInit(reinterpret_cast<FxParticlePool*>(slot->ptclPool), static_cast<int>(d[32])); // 0x5845b8
}

// Fx_AttachHitBurst 0x5845D0 — impact burst (type 7). def by modelClass (a2[23]).
void Fx_AttachHitBurst(FxSlot* slot, const FxEntitySource& e) {
    if (!(s_optionsReady && e.modelReady)) return;           // gate @0x5845ea
    Fx_AttachSlotClear(slot);                                 // 0x5845f8
    uint32_t* d = reinterpret_cast<uint32_t*>(slot);
    d[0] = 1;                                                 // 0x584600
    d[1] = 7;                                                 // type 7            0x584609
    d[2] = 1;                                                 // 0x584613
    d[3] = e.idHi;                                            // 0x584623
    d[4] = e.idLo;                                            // 0x58462f
    const int v2 = e.modelClass;                              // a2[23]            0x584638
    if (v2) {                                                 // 0x58463f
        if (v2 == 1)      d[32] = 7;                          // 0x584661
        else if (v2 == 2) d[32] = 8;                          // 0x584670
        // v2 >= 3: *(this+32) not written (cf. HitSpark).
    } else {
        d[32] = 6;                                            // 0x584652
    }
    FxBillboard_PoolInit(reinterpret_cast<FxParticlePool*>(slot->ptclPool), static_cast<int>(d[32])); // 0x584698
}

// =============================================================================================
//  MESH SETTERS (type 8/9/10) — header ported; mesh bake (SObject_Draw) = TODO anchored
// =============================================================================================
// These effects render via PASSES 1/2 through s_meshDraw (ModelObj_Draw 0x4D71B0), NOT wired at
// this milestone (model subsystem not ported). In the binary, the setter also calls SObject_Draw
// 0x4D8F90 to BAKE the mesh matrices (this+17..) and resolves the motion slot via
// g_ModelMotionArray (PcModel_ResolveEquipSlot 0x4E46A0 / Model_GetNpcMotionSlot 0x4E5960). All
// of that depends on the model renderer -> left as TODO(anchor). We faithfully write the slot's
// HEADER (state/type/flag/id/action id/subtype) so the dispatch (Fx_EmitterDraw) picks the
// right path as soon as s_meshDraw is wired.

// Fx_AttachDeflect 0x5846B0 — deflect (type 8, action 131), on MONSTER.
void Fx_AttachDeflect(FxSlot* slot, const FxEntitySource& e) {
    Fx_AttachSlotClear(slot);                                // 0x5846bc
    uint32_t* d = reinterpret_cast<uint32_t*>(slot);
    float*    f = reinterpret_cast<float*>(slot);
    d[0] = 1;                                                // 0x5846c4
    d[1] = 8;                                                // type 8            0x5846cd
    d[2] = 2;                                                // flag 2            0x5846d7
    d[3] = e.idHi;                                           // *(a4+4)           0x5846e7
    d[4] = e.idLo;                                           // *(a4+8)           0x5846f3
    d[15] = 131;                                             // action id         0x5846f9
    f[16] = 0.0f;                                            // drawParam=0       0x584705
    // *(this+30) = &flt_FF67CC[36·(*(a4[24]+244)) - 36] (0x584726): anchor (model def deref).
    d[30] = 0;                                               // TODO(anchor) model
    d[31] = 2;                                               // subtype           0x58472c
    // Model_GetNpcMotionSlot 0x4E5960 (@0x584754) + SObject_Draw 0x4D8F90 (@0x584795): mesh bake
    // (matrices at this+17, orient at this+20..22) — model subsystem not ported. TODO(anchor).
    f[20] = 0.0f;                                            // 0x58479f
    f[21] = 0.0f;                                            // = *(a4+56) heading — TODO(model) 0x5847ab
    f[22] = 0.0f;                                            // 0x5847b3
    // Snd3D_PlayPositional(flt_1491AFC, …) 0x5847c9: positional audio (out of FX scope). TODO(audio).
}

// Fx_AttachBlockGuard 0x5847E0 — parry/guard (type 9). action 135 (mode==1) else 156.
void Fx_AttachBlockGuard(FxSlot* slot, int mode, const FxEntitySource& e, bool applyDraw) {
    Fx_AttachSlotClear(slot);                                // 0x5847ec
    uint32_t* d = reinterpret_cast<uint32_t*>(slot);
    float*    f = reinterpret_cast<float*>(slot);
    d[0] = 1;                                                // 0x5847f4
    d[1] = 9;                                                // type 9            0x5847fd
    d[2] = 1;                                                // 0x584807
    d[3] = e.idHi;                                           // *(a3+4)           0x584817
    d[4] = e.idLo;                                           // *(a3+8)           0x584823
    d[15] = (mode == 1) ? 135u : 156u;                       // action id         0x58482f/0x58483b
    f[16] = 0.0f;                                            // drawParam=0       0x584847
    // *(this+30) = &flt_FABB5C[72·a3[92] + 36·a3[96]] (0x58486c): bone anchor (model). TODO(anchor).
    d[30] = 0;
    d[31] = 4;                                               // subtype           0x584872
    // PcModel_ResolveEquipSlot 0x4E46A0 (gate a3+576 @0x58487c: equipment loadout) +
    // SObject_Draw 0x4D8F90 (@0x584951, applyDraw = 8th arg): mesh bake — model subsystem
    // not ported. TODO(anchor).
    f[20] = 0.0f;                                            // 0x58495b
    f[21] = 0.0f;                                            // = *(a3+276) heading — TODO(model) 0x584967
    f[22] = 0.0f;                                            // 0x58496f
    (void)applyDraw;                                          // consumed by the mesh bake (TODO)
}

// Fx_AttachParry 0x584980 — parry (type 10, action 135), on MONSTER.
void Fx_AttachParry(FxSlot* slot, const FxEntitySource& e) {
    Fx_AttachSlotClear(slot);                                // 0x58498c
    uint32_t* d = reinterpret_cast<uint32_t*>(slot);
    float*    f = reinterpret_cast<float*>(slot);
    d[0] = 1;                                                // 0x584994
    d[1] = 10;                                               // type 10           0x58499d
    d[2] = 2;                                                // flag 2            0x5849a7
    d[3] = e.idHi;                                           // *(a2+4)           0x5849b7
    d[4] = e.idLo;                                           // *(a2+8)           0x5849c3
    d[15] = 135;                                             // action id         0x5849c9
    f[16] = 0.0f;                                            // drawParam=0       0x5849d5
    // *(this+30) = &flt_FF67CC[36·(*(a2[24]+244)) - 36] (0x5849f6): anchor (model def deref). TODO(anchor).
    d[30] = 0;
    d[31] = 2;                                               // subtype           0x5849fc
    // Model_GetNpcMotionSlot 0x4E5960 (@0x584a24) + SObject_Draw 0x4D8F90 (@0x584a65): mesh bake.
    // Model subsystem not ported. TODO(anchor).
    f[20] = 0.0f;                                            // 0x584a6f
    f[21] = 0.0f;                                            // = *(a2+56) heading — TODO(model) 0x584a7b
    f[22] = 0.0f;                                            // 0x584a83
}

// =============================================================================================
//  EXTENSION (not wired at this milestone) — status/buff: Char_RefreshStatusEffectVisuals 0x570890
// =============================================================================================
// SKELETON + deliberate TODO(anchor) (per brief: "no inventing a skill_id->def table beyond the
// proven switch"). The status/buff producer (via Pkt_SpawnCharacter 0x4646C0 @0x4648BC) loops
// over g_FxAuraCount and attaches, based on character state:
//   - Fx_AttachWeaponGlow{Front 0x584A90 / Back 0x584C20 / Left 0x584DB0 / Right 0x584F20}
//       (type 11/0xB; def by a2[27] weapon-anim id 0x64..0x91 -> {9,10,12,14,16}, hardcoded switch).
//   - if char[144]==1: Fx_AttachSkillAura{Front 0x588800 / … / RightFoot} (0x588800..0x589C00)
//       (def by a2[55] skill id -> HARDCODED switch, e.g. 559->25, 814->26, 1301->24, 1302->27 …).
//   - else if char[28]>=1: Fx_AttachSkillGlow{A..F} (0x585090..0x585B30).
// To be ported like the setters above once the skill_id->def switch is fully extracted from the
// decompiled code (particle path, type 11 -> pass 3 -> LEAF). Pool/dispatch/render are already ready.

} // namespace ts2::gfx

// Gfx/FxSetters.h — global FX slot POOL + combat PRODUCERS (Fx_Attach* setters).
//
// FAITHFUL rewrite (bit-exact target) of stage 2/2 of TwelveSky2's combat/skill FX path.
// Ground truth is the TwelveSky2.exe IDB (imagebase 0x400000); each block cites
// its IDA anchor (name + 0xADDR). Byte-exact spec: Docs/TS2_EXTRACT_FX_COMBAT.md.
//
// ┌─ What this stage does (F_FXCOMBAT 2/2) ───────────────────────────────────────────────┐
// │ (1) single SLOT POOL: dword_17D06F4 (1000 slots × 256 B), counter g_FxAuraCount        │
// │       0x168722C. Fed by ALL producers (combat/spawn/status) AND iterated by            │
// │       the render loop (3 Fx_EmitterDraw passes) and the update loop.                   │
// │       Reset on world entry (Pkt_EnterWorld 0x464160 @0x4642A4).                        │
// │ (2) combat REACTION SETTERS: read the source entity (FxEntitySource = a2[N]),          │
// │       scan for a free slot (done by the caller), write the slot and — for the          │
// │       PARTICLE path (type 5/6/7) — load the .PARTICLE template via the LEAF of         │
// │       stage 1 (Gfx/FxBillboard: FxBillboard_PoolInit = SObject_UpdateK 0x4D9F00).      │
// └───────────────────────────────────────────────────────────────────────────────────────┘
//
// WARNING: TWO distinct render paths (Fx_EmitterDraw 0x585E30, cf. FxRenderer.*):
//   - type 5/6/7 (Muzzle/HitSpark/HitBurst/MuzzleVariant) = PARTICLE (pass 3) → LEAF Object A
//     (Particle_RenderBillboards 0x6A70B0). VISIBLE end-to-end path ported here.
//   - type 8/9/10 (Deflect/BlockGuard/Parry) = MESH (passes 1/2) → ModelObj_Draw 0x4D71B0 +
//     SObject_Draw 0x4D8F90 + motion resolution (g_ModelMotionArray). MODEL subsystem
//     NOT ported at this milestone: mesh setters write the slot HEADER faithfully, the mesh
//     bake remains an anchored TODO (invisible until s_meshDraw is wired up).
#pragma once
#include "Gfx/FxRenderer.h"   // FxSlot (256 B, proven offsets), Fx_AttachSlotClear 0x584220
#include <cstdint>

namespace ts2::gfx {

// =============================================================================================
//  FxEntitySource — POD view of the source-entity fields read by the setters
// =============================================================================================
// The binary passes the entity record ADDRESS (&player[i] = this+908·i+6892; &monster[i] =
// this+280·i+923692); the setters only read a handful of DWORDs (a2[N], byte offset =
// 4·N). So only these fields are exposed here, each anchored to its index. POD by design: this
// header does NOT depend on Game/GameState.h (the caller, in Net/Game, fills the view — cf.
// Net/CombatResultApply.cpp). Unread regions = not modeled (rule: "unproven = absent").
struct FxEntitySource {
    uint32_t idHi = 0;          // a2[1]  entity+4   (network id hi; → slot[3])
    uint32_t idLo = 0;          // a2[2]  entity+8   (network id lo; → slot[4])
    uint32_t modelReady = 0;    // a2[6]  entity+24  (body[0]; GATE muzzle/hitspark/hitburst)
    int32_t  modelClass = 0;    // a2[23] entity+92  (model class; anchor 72· / hitspark-burst index)
    int32_t  modelSubclass = 0; // a2[24] entity+96  (subclass / model def ptr; anchor 36·)
    int32_t  weaponAnimId = 0;  // a2[27] entity+108 (weapon-anim id; weapon-glow — EXTENSION)
    int32_t  skillId = 0;       // a2[55] entity+220 (skill id; skill-aura — EXTENSION)
};

// =============================================================================================
//  GLOBAL FX SLOT POOL (dword_17D06F4 / g_FxAuraCount)
// =============================================================================================

// FIXED pool capacity — cGameData_InitPools 0x5575D0 @0x5575E9 (`*((_DWORD*)this + 1721) =
// 1000` → this+6884 = g_FxAuraCount 0x168722C). The pool itself = dword_17D06F4, slots of
// 256 B (stride 64 DWORD, cf. Pkt_EnterWorld @0x4642CD `&dword_17D06F4[64·i]`).
inline constexpr int kFxSlotCount = 1000;

// &dword_17D06F4[0] — pool base (the module OWNS the static array, cf. .cpp).
FxSlot* FxPool_Slots();
// g_FxAuraCount 0x168722C (= kFxSlotCount). Bound of the render/update/scan loops.
int     FxPool_Count();
// Scan for the 1st free slot (state==0), pattern shared by all producers
// (e.g. @0x55ab24: `for(j=0; j<g_FxAuraCount && slot[j].state; ++j)`). Returns the index or
// -1 if the pool is full (equivalent to `j == g_FxAuraCount` → no attach).
int     FxPool_FindFreeSlot();
// Pkt_EnterWorld 0x464160 @0x4642A4: `for(i<g_FxAuraCount) Fx_AttachSlotClear(&slot[i])`.
// Call on EVERY world entry (state reset + particle pool release).
void    FxPool_Reset();

// HitSpark/HitBurst options gate (`if (g_Options && …)` @0x58450a/0x5845ea; g_Options
// 0x84DEC0 = GameOptions.ShowSkillEffects idx0, default 1, cf. Config/GameOptions.h). Default:
// ready (true). TODO(anchor): reflect ShowSkillEffects if the UI toggles it to 0.
void Fx_SetOptionsReady(bool ready);

// =============================================================================================
//  combat PARTICLE SETTERS (type 5/6/7) — VISIBLE path via LEAF Object A (FxBillboard)
// =============================================================================================
// Each: gate → Fx_AttachSlotClear(slot) → slot header write → FxBillboard_PoolInit
// (= SObject_UpdateK 0x4D9F00: ensure-load .PARTICLE + Particle_Init 0x6A7020). If the gate
// fails, the slot is left INTACT (faithful: the binary touches nothing outside the gate).

// Fx_AttachMuzzleFlash 0x584260: muzzle (type 5, subtype 4). `side` ∈ {1,2} (1→def 0, 2→def 1).
void Fx_AttachMuzzleFlash(FxSlot* slot, const FxEntitySource& e, int side);
// Fx_AttachMuzzleVariant 0x584360: directional variant (type 5, flag 2). `variant` ∈ {1..7}.
void Fx_AttachMuzzleVariant(FxSlot* slot, const FxEntitySource& e, int variant);
// Fx_AttachDashTrail 0x585D50: MONSTER dash trail (type 5, flag 2). EXACT twin of
// Fx_AttachMuzzleVariant (same header write + FxBillboard_PoolInit). `side` ∈ {1,2}
// (1→def 18, 2→def 19). Producer = Char_SetupAuraFlags 0x5814F0 (sole caller Pkt_SpawnMonster
// 0x467B00 @0x467DA6): a SPAWN effect, not combat — only idHi/idLo (a3[1]/a3[2]) are read.
void Fx_AttachDashTrail(FxSlot* slot, const FxEntitySource& e, int side);
// Fx_AttachHitSpark 0x5844F0: hit spark (type 6). def by modelClass {0→3,1→4,2→5}.
void Fx_AttachHitSpark(FxSlot* slot, const FxEntitySource& e);
// Fx_AttachHitBurst 0x5845D0: hit burst (type 7). def by modelClass {0→6,1→7,2→8}.
void Fx_AttachHitBurst(FxSlot* slot, const FxEntitySource& e);

// =============================================================================================
//  combat MESH SETTERS (type 8/9/10) — HEADER ported, mesh bake = anchored TODO (model subsystem)
// =============================================================================================

// Fx_AttachDeflect 0x5846B0: deflect (type 8, action 131), on MONSTER. Mesh.
void Fx_AttachDeflect(FxSlot* slot, const FxEntitySource& e);
// Fx_AttachBlockGuard 0x5847E0: parry/guard (type 9, action 135 if mode==1 else 156).
// `applyDraw` = 8th arg of SObject_Draw (i==0 combat side). Mesh.
void Fx_AttachBlockGuard(FxSlot* slot, int mode, const FxEntitySource& e, bool applyDraw);
// Fx_AttachParry 0x584980: parry (type 10, action 135), on MONSTER. Mesh.
void Fx_AttachParry(FxSlot* slot, const FxEntitySource& e);

} // namespace ts2::gfx

// Gfx/FxSetters.h — POOL global de slots FX + PRODUCTEURS (setters Fx_Attach*) de combat.
//
// Réécriture FIDÈLE (bit-exacte visée) de l'étage 2/2 du chemin FX de combat/skill de
// TwelveSky2. La vérité est l'IDB de TwelveSky2.exe (imagebase 0x400000) ; chaque bloc cite
// son ancre IDA (nom + 0xADDR). Spec octet-exacte : Docs/TS2_EXTRACT_FX_COMBAT.md.
//
// ┌─ Ce que fait cet étage (F_FXCOMBAT 2/2) ──────────────────────────────────────────────┐
// │ (1) POOL DE SLOTS unique : dword_17D06F4 (1000 slots × 256 o), compteur g_FxAuraCount  │
// │       0x168722C. Alimenté par TOUS les producteurs (combat/spawn/status) ET itéré par  │
// │       la boucle de rendu (3 passes Fx_EmitterDraw) et la boucle d'update.              │
// │       Reset à l'entrée monde (Pkt_EnterWorld 0x464160 @0x4642A4).                       │
// │ (2) SETTERS de RÉACTION de combat : lisent l'entité source (FxEntitySource = a2[N]),   │
// │       scannent un slot libre (fait par l'appelant), écrivent le slot et — pour le      │
// │       chemin PARTICULE (type 5/6/7) — chargent le template .PARTICLE via le LEAF de    │
// │       l'étage 1 (Gfx/FxBillboard : FxBillboard_PoolInit = SObject_UpdateK 0x4D9F00).   │
// └────────────────────────────────────────────────────────────────────────────────────────┘
//
// ⚠ DEUX CHEMINS de rendu distincts (Fx_EmitterDraw 0x585E30, cf. FxRenderer.*) :
//   - type 5/6/7 (Muzzle/HitSpark/HitBurst/MuzzleVariant) = PARTICULE (passe 3) → LEAF Object A
//     (Particle_RenderBillboards 0x6A70B0). Chemin VISIBLE end-to-end porté ici.
//   - type 8/9/10 (Deflect/BlockGuard/Parry) = MESH (passes 1/2) → ModelObj_Draw 0x4D71B0 +
//     SObject_Draw 0x4D8F90 + résolution de motion (g_ModelMotionArray). Sous-système MODÈLE
//     NON porté à ce jalon : les setters mesh écrivent le HEADER du slot fidèlement, le bake
//     mesh reste un TODO ancré (invisible tant que s_meshDraw n'est pas câblé).
#pragma once
#include "Gfx/FxRenderer.h"   // FxSlot (256 o, offsets prouvés), Fx_AttachSlotClear 0x584220
#include <cstdint>

namespace ts2::gfx {

// =============================================================================================
//  FxEntitySource — vue POD des champs de l'entité source lus par les setters
// =============================================================================================
// Le binaire passe l'ADRESSE du record d'entité (&player[i] = this+908·i+6892 ; &monster[i] =
// this+280·i+923692) ; les setters n'en lisent qu'une poignée de DWORD (a2[N], offset octet =
// 4·N). On n'expose donc que ces champs, chacun ancré à son index. POD volontaire : ce header
// ne dépend PAS de Game/GameState.h (l'appelant, dans Net/Game, remplit la vue — cf.
// Net/CombatResultApply.cpp). Régions non lues = non modélisées (règle « non prouvé = absent »).
struct FxEntitySource {
    uint32_t idHi = 0;          // a2[1]  entity+4   (id réseau hi ; → slot[3])
    uint32_t idLo = 0;          // a2[2]  entity+8   (id réseau lo ; → slot[4])
    uint32_t modelReady = 0;    // a2[6]  entity+24  (body[0] ; GATE muzzle/hitspark/hitburst)
    int32_t  modelClass = 0;    // a2[23] entity+92  (classe modèle ; ancre 72· / index hitspark-burst)
    int32_t  modelSubclass = 0; // a2[24] entity+96  (sous-classe / ptr def modèle ; ancre 36·)
    int32_t  weaponAnimId = 0;  // a2[27] entity+108 (weapon-anim id ; weapon-glow — EXTENSION)
    int32_t  skillId = 0;       // a2[55] entity+220 (skill id ; skill-aura — EXTENSION)
};

// =============================================================================================
//  POOL GLOBAL DE SLOTS FX (dword_17D06F4 / g_FxAuraCount)
// =============================================================================================

// Capacité FIXE du pool — cGameData_InitPools 0x5575D0 @0x5575E9 (`*((_DWORD*)this + 1721) =
// 1000` → this+6884 = g_FxAuraCount 0x168722C). Le pool lui-même = dword_17D06F4, slots de
// 256 o (stride 64 DWORD, cf. Pkt_EnterWorld @0x4642CD `&dword_17D06F4[64·i]`).
inline constexpr int kFxSlotCount = 1000;

// &dword_17D06F4[0] — base du pool (le module POSSÈDE le tableau statique, cf. .cpp).
FxSlot* FxPool_Slots();
// g_FxAuraCount 0x168722C (= kFxSlotCount). Borne des boucles de rendu/update/scan.
int     FxPool_Count();
// Scan du 1er slot libre (état==0), motif partagé de tous les producteurs
// (ex. @0x55ab24 : `for(j=0; j<g_FxAuraCount && slot[j].state; ++j)`). Renvoie l'index ou
// -1 si le pool est plein (équivalent `j == g_FxAuraCount` → aucun attach).
int     FxPool_FindFreeSlot();
// Pkt_EnterWorld 0x464160 @0x4642A4 : `for(i<g_FxAuraCount) Fx_AttachSlotClear(&slot[i])`.
// À appeler à CHAQUE entrée en monde (reset état + libération des pools particule).
void    FxPool_Reset();

// Gate options de HitSpark/HitBurst (`if (g_Options && …)` @0x58450a/0x5845ea ; g_Options
// 0x84DEC0 = GameOptions.ShowSkillEffects idx0, def 1, cf. Config/GameOptions.h). Défaut :
// prêt (true). TODO(ancre) : refléter ShowSkillEffects si l'UI le bascule à 0.
void Fx_SetOptionsReady(bool ready);

// =============================================================================================
//  SETTERS combat PARTICULE (type 5/6/7) — chemin VISIBLE via le LEAF Object A (FxBillboard)
// =============================================================================================
// Chacun : gate → Fx_AttachSlotClear(slot) → écriture header slot → FxBillboard_PoolInit
// (= SObject_UpdateK 0x4D9F00 : ensure-load .PARTICLE + Particle_Init 0x6A7020). Si la gate
// échoue, le slot est laissé INTACT (fidèle : le binaire ne touche rien hors de la gate).

// Fx_AttachMuzzleFlash 0x584260 : muzzle (type 5, sous-type 4). `side` ∈ {1,2} (1→def 0, 2→def 1).
void Fx_AttachMuzzleFlash(FxSlot* slot, const FxEntitySource& e, int side);
// Fx_AttachMuzzleVariant 0x584360 : variante directionnelle (type 5, flag 2). `variant` ∈ {1..7}.
void Fx_AttachMuzzleVariant(FxSlot* slot, const FxEntitySource& e, int variant);
// Fx_AttachHitSpark 0x5844F0 : étincelle d'impact (type 6). def par modelClass {0→3,1→4,2→5}.
void Fx_AttachHitSpark(FxSlot* slot, const FxEntitySource& e);
// Fx_AttachHitBurst 0x5845D0 : gerbe d'impact (type 7). def par modelClass {0→6,1→7,2→8}.
void Fx_AttachHitBurst(FxSlot* slot, const FxEntitySource& e);

// =============================================================================================
//  SETTERS combat MESH (type 8/9/10) — HEADER porté, bake mesh = TODO ancré (sous-système modèle)
// =============================================================================================

// Fx_AttachDeflect 0x5846B0 : déviation (type 8, action 131), sur MONSTRE. Mesh.
void Fx_AttachDeflect(FxSlot* slot, const FxEntitySource& e);
// Fx_AttachBlockGuard 0x5847E0 : parade/garde (type 9, action 135 si mode==1 sinon 156).
// `applyDraw` = 8e arg de SObject_Draw (i==0 côté combat). Mesh.
void Fx_AttachBlockGuard(FxSlot* slot, int mode, const FxEntitySource& e, bool applyDraw);
// Fx_AttachParry 0x584980 : parade (type 10, action 135), sur MONSTRE. Mesh.
void Fx_AttachParry(FxSlot* slot, const FxEntitySource& e);

} // namespace ts2::gfx

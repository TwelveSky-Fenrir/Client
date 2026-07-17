// Gfx/FxSetters.cpp — pool global de slots FX + setters Fx_Attach* de combat.
//
// Vérité IDA : TwelveSky2.exe (imagebase 0x400000). Chaque bloc cite son ancre (nom + 0xADDR).
// Voir FxSetters.h pour la carte d'ensemble et Docs/TS2_EXTRACT_FX_COMBAT.md pour la spec.
//
// Fidélité d'écriture du slot : les setters écrivent `*(this+N)` (index DWORD) EXACTEMENT comme
// le décompilé. On accède donc au FxSlot via un `uint32_t*`/`float*` réinterprété (les offsets
// sont prouvés et figés par FxRenderer.h — static_assert). Le pool POBJECT 48 o inline (à
// this+33 = +0x84) est piloté par le LEAF Object A de l'étage 1 (Gfx/FxBillboard).
#include "Gfx/FxSetters.h"
#include "Gfx/FxBillboard.h"   // FxParticlePool (POBJECT 48 o), FxBillboard_PoolInit (SObject_UpdateK)
#include <cstring>

namespace ts2::gfx {
namespace {

// -------------------------------------------------------------------------------------------
// LE pool unique dword_17D06F4 (1000 slots × 256 o) + compteur g_FxAuraCount 0x168722C.
// Le module POSSÈDE le tableau statique (zéro-initialisé → tous les slots libres au démarrage).
// -------------------------------------------------------------------------------------------
FxSlot s_fxSlots[kFxSlotCount];

// g_Options 0x84DEC0 (= GameOptions.ShowSkillEffects idx0) : gate de HitSpark/HitBurst. Prêt par défaut.
bool s_optionsReady = true;

} // namespace

// =============================================================================================
//  POOL
// =============================================================================================
FxSlot* FxPool_Slots() { return s_fxSlots; }              // &dword_17D06F4[0]
int     FxPool_Count() { return kFxSlotCount; }           // g_FxAuraCount 0x168722C

// Scan du 1er slot libre — motif partagé de tous les producteurs (ex. @0x55ab24) :
//   for (j=0; j<g_FxAuraCount && *(this+(j<<8)+1355692); ++j); if (j<g_FxAuraCount) ...
int FxPool_FindFreeSlot() {
    for (int j = 0; j < kFxSlotCount; ++j)
        if (s_fxSlots[j].state == 0)   // slot[j].state == 0 → libre (arrêt de la boucle while-state)
            return j;
    return -1;                          // pool plein (équivalent j == g_FxAuraCount → aucun attach)
}

// Pkt_EnterWorld 0x464160 @0x4642A4 : for (i<g_FxAuraCount) Fx_AttachSlotClear(&dword_17D06F4[64·i]).
void FxPool_Reset() {
    for (int i = 0; i < kFxSlotCount; ++i)
        Fx_AttachSlotClear(&s_fxSlots[i]);   // état=0 + Particle_Free(pool@+132)  (FxRenderer.cpp)
}

void Fx_SetOptionsReady(bool ready) { s_optionsReady = ready; }

// =============================================================================================
//  SETTERS PARTICULE (type 5/6/7)
// =============================================================================================

// Fx_AttachMuzzleFlash 0x584260 — muzzle (type 5, sous-type 4).
void Fx_AttachMuzzleFlash(FxSlot* slot, const FxEntitySource& e, int side) {
    if (!(side >= 1 && side <= 2 && e.modelReady)) return;   // gate @0x58427d
    Fx_AttachSlotClear(slot);                                 // 0x58428b
    uint32_t* d = reinterpret_cast<uint32_t*>(slot);
    d[0] = 1;                                                 // état actif        0x584293
    d[1] = 5;                                                 // type 5 (particule) 0x58429c
    d[2] = 1;                                                 // flag              0x5842a6
    d[3] = e.idHi;                                            // a2[1]             0x5842b6
    d[4] = e.idLo;                                            // a2[2]             0x5842c2
    // *(this+30) = &flt_FABB5C[72·a2[23] + 36·a2[24]] (0x5842e7) : ancre d'os (table modèle
    // flt_FABB5C 0xFABB5C, sous-système modèle non porté). NON lue par le chemin particule
    // (Docs/TS2_EXTRACT_FX_COMBAT.md §4) → 0 ici. TODO(ancre) : câbler pour la position d'émission.
    d[30] = 0;
    d[31] = 4;                                                // sous-type muzzle  0x58431a
    d[32] = (side != 1) ? 1u : 0u;                            // index def (0/1)   0x58430b
    // SObject_UpdateK(&byte_1151CBC[336·*(this+32)], this+33) 0x58434c = ensure-load + Particle_Init.
    FxBillboard_PoolInit(reinterpret_cast<FxParticlePool*>(slot->ptclPool), static_cast<int>(d[32]));
}

// Fx_AttachMuzzleVariant 0x584360 — variante directionnelle (type 5, flag 2, 7 variantes).
void Fx_AttachMuzzleVariant(FxSlot* slot, const FxEntitySource& e, int variant) {
    if (!(variant >= 1 && variant <= 7)) return;             // gate @0x584373
    Fx_AttachSlotClear(slot);                                 // 0x58437d
    uint32_t* d = reinterpret_cast<uint32_t*>(slot);
    d[0] = 1;                                                 // 0x584385
    d[1] = 5;                                                 // type 5            0x58438e
    d[2] = 2;                                                 // flag 2            0x584398
    d[3] = e.idHi;                                            // 0x5843a8
    d[4] = e.idLo;                                            // 0x5843b4
    // *(this+30) = &flt_FF67CC[36·(*(a2[24]+244)) - 36] (0x5843d5) : ancre variante (table
    // flt_FF67CC 0xFF67CC via déréférencement du def modèle a2[24]+244). Sous-système modèle,
    // NON lu par le chemin particule → 0 ici. TODO(ancre).
    d[30] = 0;
    // switch(variant) @0x5843f4 : (sous-type this+31, index def this+32).
    int subType, defIndex;
    switch (variant) {
        case 1: subType = 1; defIndex = 0; break;   // 0x5843fe/0x584408
        case 2: subType = 2; defIndex = 0; break;   // 0x58441a/0x584424
        case 3: subType = 3; defIndex = 0; break;   // 0x584433/0x58443d
        case 4: subType = 1; defIndex = 1; break;   // 0x58444c/0x584456
        case 5: subType = 2; defIndex = 1; break;   // 0x584465/0x58446f
        case 6: subType = 3; defIndex = 1; break;   // 0x58447e/0x584488
        case 7: subType = 0; defIndex = 2; break;   // 0x584497/0x5844a1
        default: return;                            // (inatteignable : gate 1..7)
    }
    d[31] = static_cast<uint32_t>(subType);
    d[32] = static_cast<uint32_t>(defIndex);
    FxBillboard_PoolInit(reinterpret_cast<FxParticlePool*>(slot->ptclPool), defIndex); // 0x5844c9
}

// Fx_AttachDashTrail 0x585D50 — traînée de dash de MONSTRE (type 5, flag 2). Jumeau EXACT de
// Fx_AttachMuzzleVariant : même en-tête de slot puis FxBillboard_PoolInit (= SObject_UpdateK) sur
// le def 18 (side 1) / 19 (side 2). Producteur = Char_SetupAuraFlags 0x5814F0 (unique appelant
// Pkt_SpawnMonster 0x467B00 @0x467DA6). ⚠ Différence avec le muzzle : PAS de gate en tête — le
// binaire efface le slot, écrit le header, et ne le RE-efface (état 0) que si `side` n'est ni 1 ni 2
// (branche inatteignable en pratique : Char_SetupAuraFlags ne passe QUE 1 ou 2).
void Fx_AttachDashTrail(FxSlot* slot, const FxEntitySource& e, int side) {
    Fx_AttachSlotClear(slot);                                 // 0x585d5c
    uint32_t* d = reinterpret_cast<uint32_t*>(slot);
    d[0] = 1;                                                 // état actif        0x585d64
    d[1] = 5;                                                 // type 5 (particule) 0x585d6d
    d[2] = 2;                                                 // flag 2            0x585d77
    d[3] = e.idHi;                                            // a3[1]             0x585d87
    d[4] = e.idLo;                                            // a3[2]             0x585d93
    // *(this+30) = &flt_FF67CC[36·(*(a3[24]+244)) - 36] (0x585db4) : ancre modèle (déréf du def
    // monstre a3[24]+244 = kindIndexP1). Table flt_FF67CC 0xFF67CC, sous-système modèle NON porté,
    // NON lue par le chemin particule (comme le muzzle) → 0 ici. TODO(ancre).
    d[30] = 0;
    if (side == 1) {                                          // 0x585dc1
        d[31] = 1;                                            // sous-type         0x585dce
        d[32] = 18;                                           // index def side 1  0x585dd8
    } else if (side == 2) {                                   // 0x585dc7
        d[31] = 1;                                            // sous-type         0x585de7
        d[32] = 19;                                           // index def side 2  0x585df1
    } else {
        Fx_AttachSlotClear(slot);                             // side invalide : ré-efface (état 0) 0x585e00
        return;                                               // 0x585e05
    }
    // SObject_UpdateK(&byte_1151CBC[336·*(this+32)], this+33) 0x585e25 = ensure-load + Particle_Init.
    FxBillboard_PoolInit(reinterpret_cast<FxParticlePool*>(slot->ptclPool), static_cast<int>(d[32]));
}

// Fx_AttachHitSpark 0x5844F0 — étincelle d'impact (type 6). def par modelClass (a2[23]).
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
        // v2 >= 3 : *(this+32) N'EST PAS écrit (quirk binaire) — modelClass ∈ {0,1,2} en pratique
        // (aTribe RACE 0..2), donc branche inatteignable ; d[32] conserve sa valeur (0 au 1er usage).
    } else {
        d[32] = 3;                                            // 0x584572
    }
    FxBillboard_PoolInit(reinterpret_cast<FxParticlePool*>(slot->ptclPool), static_cast<int>(d[32])); // 0x5845b8
}

// Fx_AttachHitBurst 0x5845D0 — gerbe d'impact (type 7). def par modelClass (a2[23]).
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
        // v2 >= 3 : *(this+32) non écrit (cf. HitSpark).
    } else {
        d[32] = 6;                                            // 0x584652
    }
    FxBillboard_PoolInit(reinterpret_cast<FxParticlePool*>(slot->ptclPool), static_cast<int>(d[32])); // 0x584698
}

// =============================================================================================
//  SETTERS MESH (type 8/9/10) — header porté ; bake mesh (SObject_Draw) = TODO ancré
// =============================================================================================
// Ces effets rendent par les PASSES 1/2 via s_meshDraw (ModelObj_Draw 0x4D71B0), NON câblé à ce
// jalon (sous-système modèle non porté). Le binaire, dans le setter, appelle en plus SObject_Draw
// 0x4D8F90 pour BAKER les matrices mesh (this+17..) et résout le slot de motion via
// g_ModelMotionArray (PcModel_ResolveEquipSlot 0x4E46A0 / Model_GetNpcMotionSlot 0x4E5960). Tout
// cela dépend du renderer de modèles → laissé en TODO(ancre). On écrit fidèlement le HEADER du
// slot (état/type/flag/id/action id/sous-type) pour que le dispatch (Fx_EmitterDraw) sélectionne
// le bon chemin dès que s_meshDraw sera câblé.

// Fx_AttachDeflect 0x5846B0 — déviation (type 8, action 131), sur MONSTRE.
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
    // *(this+30) = &flt_FF67CC[36·(*(a4[24]+244)) - 36] (0x584726) : ancre (déréf def modèle).
    d[30] = 0;                                               // TODO(ancre) modèle
    d[31] = 2;                                               // sous-type         0x58472c
    // Model_GetNpcMotionSlot 0x4E5960 (@0x584754) + SObject_Draw 0x4D8F90 (@0x584795) : bake mesh
    // (matrices à this+17, orient à this+20..22) — sous-système modèle non porté. TODO(ancre).
    f[20] = 0.0f;                                            // 0x58479f
    f[21] = 0.0f;                                            // = *(a4+56) heading — TODO(modèle) 0x5847ab
    f[22] = 0.0f;                                            // 0x5847b3
    // Snd3D_PlayPositional(flt_1491AFC, …) 0x5847c9 : audio positionnel (hors périmètre FX). TODO(audio).
}

// Fx_AttachBlockGuard 0x5847E0 — parade/garde (type 9). action 135 (mode==1) sinon 156.
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
    // *(this+30) = &flt_FABB5C[72·a3[92] + 36·a3[96]] (0x58486c) : ancre d'os (modèle). TODO(ancre).
    d[30] = 0;
    d[31] = 4;                                               // sous-type         0x584872
    // PcModel_ResolveEquipSlot 0x4E46A0 (gate a3+576 @0x58487c : loadout d'équipement) +
    // SObject_Draw 0x4D8F90 (@0x584951, applyDraw = 8e arg) : bake mesh — sous-système modèle
    // non porté. TODO(ancre).
    f[20] = 0.0f;                                            // 0x58495b
    f[21] = 0.0f;                                            // = *(a3+276) heading — TODO(modèle) 0x584967
    f[22] = 0.0f;                                            // 0x58496f
    (void)applyDraw;                                          // consommé par le bake mesh (TODO)
}

// Fx_AttachParry 0x584980 — parade (type 10, action 135), sur MONSTRE.
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
    // *(this+30) = &flt_FF67CC[36·(*(a2[24]+244)) - 36] (0x5849f6) : ancre (déréf def modèle). TODO(ancre).
    d[30] = 0;
    d[31] = 2;                                               // sous-type         0x5849fc
    // Model_GetNpcMotionSlot 0x4E5960 (@0x584a24) + SObject_Draw 0x4D8F90 (@0x584a65) : bake mesh.
    // Sous-système modèle non porté. TODO(ancre).
    f[20] = 0.0f;                                            // 0x584a6f
    f[21] = 0.0f;                                            // = *(a2+56) heading — TODO(modèle) 0x584a7b
    f[22] = 0.0f;                                            // 0x584a83
}

// =============================================================================================
//  EXTENSION (non wirée à ce jalon) — status/buff : Char_RefreshStatusEffectVisuals 0x570890
// =============================================================================================
// OSSATURE + TODO(ancre) volontaire (cf. brief : « pas d'invention de table skill_id→def
// au-delà du switch prouvé »). Le producteur status/buff (via Pkt_SpawnCharacter 0x4646C0 @0x4648BC)
// boucle sur g_FxAuraCount et attache, selon l'état du personnage :
//   - Fx_AttachWeaponGlow{Front 0x584A90 / Back 0x584C20 / Left 0x584DB0 / Right 0x584F20}
//       (type 11/0xB ; def par a2[27] weapon-anim id 0x64..0x91 → {9,10,12,14,16}, switch en dur).
//   - si char[144]==1 : Fx_AttachSkillAura{Front 0x588800 / … / RightFoot} (0x588800..0x589C00)
//       (def par a2[55] skill id → switch HARDCODÉ, ex. 559→25, 814→26, 1301→24, 1302→27 …).
//   - sinon si char[28]>=1 : Fx_AttachSkillGlow{A..F} (0x585090..0x585B30).
// À porter comme les setters ci-dessus une fois le switch skill_id→def entièrement relevé depuis
// le décompilé (chemin particule, type 11 → passe 3 → LEAF). Le pool/dispatch/rendu sont déjà prêts.

} // namespace ts2::gfx

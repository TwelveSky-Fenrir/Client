// Game/WeaponTrailResolver.h — resolves the player "weapon trail" (skinned swoosh/glow
// effect that accompanies a cast). PURE logic (no D3D/gfx dependency): the
// weaponAnimSlot->effect-index switch, the action-state->motion-sub-block gate, and the
// SObject stem.
//
// IDA SOURCE (single source of truth) — TWO twin functions, same switch + same gate, only
// the draw primitive differs (see Docs/TS2_EXTRACT_WEAPON_TRAIL_V2.md §1/§4):
//   Char_DrawWeaponTrailEffect     0x55E9D0  (self, OPAQUE pass: SObject_DrawEx 0x4D9330 ->
//                                             Model_Render 0x40EBB0). Caller @0x52DB7C.
//   Char_DrawWeaponEffectVariantB  0x56BF90  (SHADOW pass: SObject_DrawAnimated2 0x4D91C0 ->
//                                             Model_RenderPlanarShadow 0x40F720). Caller @0x52DA41
//                                             (shadow bracket 0x52D9DC..0x52DB15).
// Both loop over g_EntityArray (dword_1687234, stride 908) => PLAYER-ONLY EFFECT
// (monsters dword_1766F74 / NPCs have no weapon trail). The decompiled IDB of 0x56BF90
// is the byte-exact reference used to transcribe the switch (@0x56c001..0x56c3eb) and the
// gate (@0x56c411) below; 0x55E9D0 (disasm loc_55ED14.. re-read) carries identical values.
//
// MASTER GATE (switch entry, @0x56c01b): drawn ONLY if
//     weaponAnimSlot (entity+220 = this+55) != 0   AND   !altWeaponSet (entity+576 = this+144).
// Placement (identical body): entity origin this+63 (=entity+252, world pos) + heading this+69
// (=entity+276 = PlayerEntity::heading); animTime this+62 (=entity+248 = CharAnimState::animFrame,
// the SAME cursor as the body). NO bone attach transform (Model_GetAttachTransform 0x40FDC0
// not called, see doc §5).
#pragma once
#include <string>

namespace ts2::game {

// Switch weaponAnimSlot (this+55 = entity+220) -> trail-effect index v6 in [0,41], or -1 if
// the active anim id has NO associated trail (switch default case). 42 distinct values,
// consistent with the 42 entries of the flt_113E2DC catalog (name-built at boot by
// AssetMgr_InitAllSlots 0x4DEB50). Transcribed BIT-FOR-BIT from the Hex-Rays decompile of
// Char_DrawWeaponEffectVariantB 0x56BF90 — NO invented values (see header banner).
// WARNING: the summary table in Docs/TS2_EXTRACT_WEAPON_TRAIL_V2.md §1 had a typo
// (2266-2275 gave 29 there); the decompile is ground truth: 2266-2275 -> 28, 2276-2285 -> 29.
int ResolveWeaponTrailIndex(int weaponAnimSlot);

// Action-state gate (state = this+61 = entity+244 = CharAnimState::state) -> which of the 3
// MOTION sub-blocks of the effect to draw (switch @0x56c411 of 0x56BF90):
//   0 = sub-block 0 (unk_F54DB4, motionSub 0) — UNCONDITIONAL draw (state==1).
//   1 = sub-block 1 (unk_F54E50, motionSub 1) — UNCONDITIONAL draw (state==2 or 0x20).
//   2 = sub-block 2 (unk_F54EEC, motionSub 2) — draw gated by frameCount>=1 (state==0 or a large
//       set of cast states). The binary only draws this sub-block if
//       Motion_GetFrameCount(unk_F54EEC + 468*v6) >= 1 (@0x56c43e) — see WeaponTrailResolver.cpp.
//  -1 = unhandled state (switch default) -> NO draw.
// The return value IS the motionSub index (0/1/2) passed to MotionCache::GetForWeaponTrail
// (motion cat. 5 "F%03d001%03d" % (v6+1, motionSub+1), folder 005 — see Motion_BuildPathAndLoad
// 0x4D7390 case 5 and Docs/TS2_EXTRACT_WEAPON_TRAIL_V2.md §3).
int ResolveWeaponTrailMotionSub(int actionState);

// true if motion sub-block `motionSub` (0/1/2) requires the frameCount>=1 guard before drawing:
// ONLY sub-block 2 (LABEL_116 branch of 0x56BF90). Sub-blocks 0/1 draw unconditionally
// (identity fallback if the motion is missing). Exposed so the caller applies exactly the
// binary's gate without duplicating the constant "2".
inline bool WeaponTrailMotionSubIsFrameGated(int motionSub) { return motionSub == 2; }

// SObject stem of the effect (category 9, folder 009, prefix 'Y'): "Y%03d001" % (trailIndex+1).
// Format = SObject_BuildPath 0x4D89C0 case 9 ("G03_GDATA\\D04_GSOBJECT\\009\\Y%03d%03d.SOBJECT" %
// (a3+1, a4+1)) with a3=trailIndex (=v6, AssetMgr_InitAllSlots's i80 loop) and a4=0 (inner
// i81<1 loop) -> second field always "001". Resolvable as-is via gfx::ModelCache::Get
// (kSObjectCategories already contains {'Y',9}). Empty string if trailIndex is out of [0,42) —
// no exception (same contract as BuildMonsterStem/BuildNpcStem).
std::string BuildWeaponTrailStem(int trailIndex);

} // namespace ts2::game

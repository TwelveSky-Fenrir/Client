// Gfx/PlayerMotionSlotResolver.h — player animation clip resolver by weapon/skill.
//
// ============================ RATIONALE ================================
// MotionCache::GetForPlayer 0x4E46A0 (Gfx/MotionCache.*) only carries the DEFAULT PATH
// for the player body: it always builds a "C%03d%03d%03d" stem (folder 001,
// category 1). But the real client redirects, depending on the equipped weapon/skill (a8 =
// item/skillId), the animation state toward DEDICATED CLIP REGIONS of ANOTHER motion
// category (category 6, stem "X%03d%03d%03d", folder 006), and even toward
// FIXED action slots ("entry" poses a5 in {1,2,32}). Without this meta-switch, all
// weapon/skill clips fall back to the generic body clip (the default idle).
// This module PORTS that meta-switch — gap A of the backlog Docs/TS2_DEEP_MOTION.md §9.2.
//
// STANDALONE and READ-ONLY module regarding the binary: it ONLY computes the decomposition
// (category, kind, variant, state) of a slot; it opens no file, touches no D3D
// device, and is not wired into any render loop (MAIN consolidation will
// wire its result into MotionCache).
//
// ============================ IDA ANCHORS (sole source of truth, imagebase 0x400000) =========
//   PcModel_ResolveEquipSlot   0x4E46A0  — the meta-switch ported here (a8 -> clip region).
//       thiscall(this=g_ModelMotionArray 0x8E8B30, a2=race, a3=gender, a4=weaponPose,
//                a5=animState, a6=ctxA6, a7=ctxA7, a8=item/skillId) -> &MotionSlot.
//       Guard 0x4e46cc: race>2 || gender>1 || !Motion_IsValidWeaponPose(a4,a5) -> fallback.
//   Motion_IsValidWeaponPose   0x4E3A30  — validity table (weaponPose, animState).
//       WARNING order: called as Motion_IsValidWeaponPose(a4 /*pose*/, a5 /*state*/); the
//       function switches on (a2=state) and bounds a1=pose -> see IsValidPlayerWeaponPose.
//   PcModel_ResolveSlotAndApply 0x4E5A00 — wrapper: ResolveEquipSlot then Motion_GetFrameCount.
//   Motion_BuildPathAndLoad    0x4D7390  — cat 1 "001\\C%03d%03d%03d" %(race+3*gender+1, wp+1,
//       state+1); cat 6 "006\\X%03d%03d%03d" %(race+3*gender+1, wp+1, state+1). SAME formula.
//   AssetMgr_InitAllSlots      0x4DEB50  — POPULATOR = proof of the offset->stem mapping:
//       cat 1 @0x4df00c: slot = this + 479232*race + 159744*gender + 19968*wp + 156*state
//                                       + 2624960, (cat=1, race, gender, wp, state).
//       cat 6 @0x4df32e: slot = this + 479232*race + 159744*gender + 19968*wp + 156*state
//                                       + 4062656, (cat=6, race, gender, wp, state).
//   g_ModelMotionArray         0x8E8B30  — static pool (base `this`).
//
// ============================ OFFSET -> STEM MAPPING (PROVEN, ZERO INVENTION) =======
// Each `return this + OFFSET` of 0x4E46A0 decomposes UNIQUELY via the EXACT arithmetic
// of populator 0x4DEB50 (same strides 479232/159744/19968/156, same stems):
//   * PARAMETERIZED offset = 479232*race + 159744*gender + 19968*wp + 156*state + BASE
//       - BASE = 2624960 -> category 1 (C, folder 001), (race, gender, wp, state) as-is.
//       - BASE = 4062656 -> category 6 (X, folder 006), (race, gender, wp, state) as-is.
//   * FIXED (dedicated) offset = 479232*race + 159744*gender + FIXED   (implicit wp = 0)
//       -> region = (FIXED >= 4062656) ? X : C; rel = FIXED - baseRegion;
//          wp = rel/19968 (== 0), state = rel/156. ALL verified exact (rel % 156 == 0,
//          rel < 19968) — see table below. These FIXED slots ARE POPULATED by the
//          populator (cat 1 states 77/84; cat 6 states 100..127): they are NOT
//          "stem-less bases", they have a PROVEN stem. No invention TODO needed.
//   * fallback guard 0x4e46dd = this + 2644772 (ABSOLUTE, no race/gender term) -> category 1,
//       race=0, gender=0, wp=0, state=127 -> stem "C001001128" (fallback idle).
//
//   Fixed offsets -> (region, wp, state)  [script-verified, all exact]:
//     C : 2636972->state77  2638064->state84  2644772->state127(fallback)
//     X : 4078256->100 4078412->101 4078568->102 4078724->103 4078880->104 4079036->105
//         4079192->106 4079348->107 4079504->108 4079660->109 4079816->110 4079972->111
//         4080128->112 4080284->113 4080440->114 4080596->115 4080752->116 4080908->117
//         4081064->118 4081220->119 4081376->120 4081532->121 4081688->122 4081844->123
//         4082000->124 4082156->125 4082312->126 4082468->127   (all wp=0)
//
// NOTE unreachable special stem: Motion_BuildPathAndLoad case 1 emits "C%03d%03d011"
// (state pinned to 011, kind+6) ONLY if a6==120 in the populator (state index 120).
// The resolver NEVER returns a C slot at state 120 (guard limits state to table
// 0x4E3A30, max 95; the fixed C's are 77/84/127). So this special stem is out of
// scope for this module. (see Docs/TS2_DEEP_MOTION.md §3.)
#pragma once
#include <cstdint>
#include <string>

namespace ts2::gfx {

// Resolved motion category = folder + stem letter (Motion_BuildPathAndLoad 0x4D7390).
enum class PlayerMotionCategory : int {
    BodyC       = 1,   // "001\\C%03d%03d%03d.MOTION" — generic body (cat 1, base 2624960)
    WeaponSkillX = 6,  // "006\\X%03d%03d%03d.MOTION" — weapon/skill clip (cat 6, base 4062656)
};

// Resolution result: the decomposition (category, kind, variant/state) of a &MotionSlot
// returned by PcModel_ResolveEquipSlot 0x4E46A0. Fields are 0-based (raw indices); the
// stem*() accessors apply the +1 of the binary's printf calls.
struct PlayerMotionSlot {
    PlayerMotionCategory category = PlayerMotionCategory::BodyC;

    // Raw indices (0-based) as decomposed from the returned offset (arithmetic 0x4DEB50).
    int race        = 0;   // a2 (0..2) — unchanged by the switch (479232*race term always present)
    int gender      = 0;   // a3 (0..1) — unchanged (159744*gender term always present)
    int weaponIndex = 0;   // stem variant 0-based: a4 (parameterized path) or 0 (fixed slot)
    int stateIndex  = 0;   // stem state 0-based: a5 (parameterized) or the fixed index (77/84/100..127)

    // true if the entry guard failed (race>2 || gender>1 || invalid pose) -> absolute
    // fallback idle slot "C001001128" (0x4e46dd). In that case race/gender/weaponIndex are forced to 0.
    bool guardFallback = false;

    // BYTE offset of the slot in g_ModelMotionArray 0x8E8B30 (= the `this + X` value returned
    // by 0x4E46A0, minus `this`). Provided for verification/tracing (must match the binary's
    // arithmetic byte-for-byte); NOT required to build the stem.
    std::uint32_t slotByteOffset = 0;

    // --- Stem fields (printf Motion_BuildPathAndLoad 0x4D7390, already +1) ---
    // stem = "%c%03d%03d%03d" % (stemLetter, stemKind, stemVariant, stemState).
    int  stemKind()    const { return race + 3 * gender + 1; } // field1: race+3*gender+1
    int  stemVariant() const { return weaponIndex + 1; }       // field2: weaponPose+1
    int  stemState()   const { return stateIndex + 1; }        // field3: animState+1
    char stemLetter()  const { return category == PlayerMotionCategory::BodyC ? 'C' : 'X'; }
    // GMOTION folder (Motion_BuildPathAndLoad): "001" (cat 1) or "006" (cat 6).
    const char* motionFolder() const {
        return category == PlayerMotionCategory::BodyC ? "001" : "006";
    }

    // Full stem WITHOUT extension or folder, e.g. "X002001105" — to pass to a motion cache
    // (same convention as MotionCache::BuildPlayerMotionStem, extended to letter X/folder 006).
    std::string BuildStem() const;
};

// -----------------------------------------------------------------------------
//  Resolver API.
// -----------------------------------------------------------------------------

// Reproduces the Motion_IsValidWeaponPose 0x4E3A30 table (0x4E46A0's entry guard).
// Returns true if the (weaponPose, animState) pair is admitted. NB (anchor 0x4e46cc): the binary
// calls Motion_IsValidWeaponPose(a4=weaponPose, a5=animState); the function switches on state
// and bounds pose -> the argument order here is (weaponPose, animState).
bool IsValidPlayerWeaponPose(int weaponPose, int animState);

// Ports the full meta-switch of PcModel_ResolveEquipSlot 0x4E46A0.
//   race, gender      : a2, a3 (player identity; g_EntityArray this+0x5C/+0x60).
//   weaponPose        : a4 (weapon pose 0..7, see validity table 0x4E3A30).
//   animState         : a5 (animation state; a5 sub-switch in {1,2,32} -> fixed slots).
//   ctxA6, ctxA7      : a6, a7 — OPAQUE CONTEXT passed through as-is by callers
//                       (Char_RenderModel 0x527020 @0x52705a/@0x52753f, Char_*AnimTick_* via
//                       PcModel_ResolveSlotAndApply 0x4E5A00). Sole use: the
//                       LABEL_152 0x4e5708 branch (a6>112 && a5==1 && a4 even -> fixed C clips 78/85,
//                       gated by a7>=1). TODO anchor: fine semantics of a6/a7 not traced on
//                       the caller side — reproduced verbatim, never interpreted here.
//   itemOrSkillId     : a8 — the item/skill id that selects the clip family.
// Returns the decomposition (category, kind, variant, state) — see PlayerMotionSlot.
PlayerMotionSlot ResolvePlayerMotionSlot(int race, int gender, int weaponPose, int animState,
                                         int ctxA6, int ctxA7, int itemOrSkillId);

} // namespace ts2::gfx

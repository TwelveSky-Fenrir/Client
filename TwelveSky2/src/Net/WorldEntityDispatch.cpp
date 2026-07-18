// Net/WorldEntityDispatch.cpp — top-level dispatch switch for Net_OnWorldEntityDispatch
// (opcode 0x5e, EA 0x494870). Parses the sub-opcode + shared payload fields once, then routes
// to the owning handler in the WorldEntityDispatch_{ComboFamilies,BranchSkill,Element,
// Special,War}.cpp split family (see Net/WorldEntityDispatch_Internal.h for the shared
// address constants/helpers and the forward declarations of every handler called below). See
// Net/WorldEntityDispatch.h for the full sub-opcode coverage map.
//
// COVERAGE SUMMARY (Net/WorldEntityDispatch.h is the authoritative, up-to-date map): wired
// sub-opcodes 1..30 (combo families 1/2/3 -- family 3 has 12 sub-cases, not 9), 31/32, 33..115
// (except 110/113), 201..208, 401, 402..410 (Special), 411..417 (Buff), 500..807, 901..903 --
// wired incrementally across several 2026-07-14 missions: SkillLevelTable/ElementPairTable
// exposure (Game/SkillCombat.h::GetSkillLevelTable()/Combat_ReadLocalElementPairs()), the
// "201-208" individual-arena family, "element loadout" 33-50, "sub-range 51-75"/"76..100"
// skill-branch, "duel/challenge 101-115", and the "rest of the function" 500..903 sweep.
// Genuinely NOT wired to date (documented, not an oversight): 425..428 (gate buffer
// byte_1686138 never written anywhere in the binary -- statically insurmountable), 600 and
// 764..770 (confirmed no-op / sparse), 110/113 (documented exclusions), 116..200 (confirmed
// empty in the disassembly, not a TODO). Cf. Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md §2.4 for
// the original completeness audit that seeded this coverage effort.
#include "Net/WorldEntityDispatch.h"
#include "Net/WorldEntityDispatch_Internal.h"
#include "Game/ClientRuntime.h"
#include "Game/GameState.h"
#include "Game/GameDatabase.h"   // GetMonsterInfo / MonsterInfo (resolution 1-based, Apply792to794)
#include "Game/SkillCombat.h"
#include "Game/MapWarp.h"
#include "Game/MotionPoolsCoordResolver.h"

#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>

namespace ts2::game {
using namespace wed_detail;

void ApplyWorldEntityDispatch(const uint8_t* payload, uint32_t len) {
    if (!payload || len < 4) return; // the binary always reads at least the sub-opcode

    uint32_t subOpcode = 0;
    std::memcpy(&subOpcode, payload, 4);

    // idx/arg2: SAME offsets (payload+4, payload+8) for every slot of families 1/2
    // (v702[0..3] / v702[4..7] in the decompilation). Tolerant read (the packet is always
    // 104 o on the wire, cf. Net/PacketDispatch.h::kPacketSize[0x5e]=105, but defensive if
    // called with a shorter buffer, e.g. tests).
    uint32_t idx = 0, arg2 = 0, arg3 = 0;
    if (len >= 8)  std::memcpy(&idx,  payload + 4, 4);
    if (len >= 12) std::memcpy(&arg2, payload + 8, 4);
    // arg3 = payload+12 as an INTEGER (not the tag13 below -- reinterpretation of the same
    // raw field, same convention as the rest of this split family): only read by 59/63
    // (mission "sub-range 51-75").
    if (len >= 16) std::memcpy(&arg3, payload + 12, 4);
    // 13-byte text tag (payload+12..+24): only read by sub-opcode 25 (family 3, cf.
    // ApplyFamily3TagSlot). Defensively zero-terminated (the binary does not do this
    // explicitly, but the packet field is a fixed name potentially NUL-padded).
    char tag13[14] = {};
    if (len >= 25) std::memcpy(tag13, payload + 12, 13);

    if (subOpcode >= 1 && subOpcode <= 9) {
        ApplyComboFamilySlot(kFamily1, static_cast<int>(subOpcode), idx, arg2);
    } else if (subOpcode >= 10 && subOpcode <= 18) {
        ApplyComboFamilySlot(kFamily2, static_cast<int>(subOpcode) - 9, idx, arg2);
    } else if (subOpcode >= 19 && subOpcode <= 24) {
        // Family 3, slots 1..6 (same mechanical shape as families 1/2; strSlot6=235 like
        // family 2, no half-duration at slot4 like family 2 -- verified EA
        // 0x4958a0..0x495d51).
        ApplyComboFamilySlot(kFamily3, static_cast<int>(subOpcode) - 18, idx, arg2);
    } else if (subOpcode == 25) {
        ApplyFamily3TagSlot(idx, arg2, tag13);
    } else if (subOpcode == 26) {
        ApplyFamily3Slot26(idx);
    } else if (subOpcode == 27) {
        ApplyFamily3Slot27(idx, arg2);
    } else if (subOpcode == 28) {
        // Family 3, resumes the standard slot7 after the 3 sub-cases 25..27 (EA
        // 0x496277..0x49636d) -- BUT with a 13-byte tag (payload+12) and its OWN
        // message/timer (str246, timer dword_1675BE4/flt_1675BE8) diverging from the
        // generic slot7 -> routed to ApplyFamily3Slot28 (NOT ApplyComboFamilySlot), now
        // fully wired message included (SkillLevelTable+ElementPairTable exposed, cf. the
        // header comment).
        ApplyFamily3Slot28(idx, arg2, tag13);
    } else if (subOpcode >= 29 && subOpcode <= 30) {
        // Family 3, slots 8..9 standard (EA 0x4962d3..0x49636d) -- IDENTICAL shape to
        // families 1/2, no divergence (unlike slot7/28 above).
        ApplyComboFamilySlot(kFamily3, static_cast<int>(subOpcode) - 21, idx, arg2);
    } else if (subOpcode == 31) {
        // "Exclusion" toggle (branch, type) -- cf. ApplyExclusionToggleSlot, EA 0x4963b0.
        ApplyExclusionToggleSlot(idx, arg2, 0);
    } else if (subOpcode == 32) {
        // Structural twin of 31 (EA 0x496583), written value=1, labels 255..258.
        ApplyExclusionToggleSlot(idx, arg2, 1);
    } else if (subOpcode >= 33 && subOpcode <= 45) {
        // "Element loadout" notices/state (mission "element loadout", 2026-07-14) -- cf.
        // ApplyElementLoadoutFamily for the detail (no relation to the combo/Special/Buff
        // families nor to Char_GetPairedElement).
        ApplyElementLoadoutFamily(subOpcode, payload, len);
    } else if (subOpcode == 46 || subOpcode == 47) {
        // Alliance element pairing -- FEEDS ElementPairTable
        // (Game/SkillCombat.h::Combat_ReadLocalElementPairs()) via the SAME addresses as
        // Char_GetPairedElement/Combat_IsElementAllowedOnMap, cf. ApplyAlliancePairFamily
        // and Docs/TS2_COMBAT_ELEMENT_GATING.md.
        ApplyAlliancePairFamily(subOpcode, idx, arg2, payload, len);
    } else if (subOpcode >= 48 && subOpcode <= 50) {
        // Tail-merge of the alliance pairing (49 replays 47's state) -- cf.
        // ApplyAllianceLabelFamily, mission "element loadout" 2026-07-14.
        ApplyAllianceLabelFamily(subOpcode, payload, len);
    } else if (subOpcode == 51) {
        Apply51(payload, len); // tag at payload+8, NOT +12 (irregular offset, cf. Apply419).
    } else if (subOpcode == 52) {
        // 52 is DISTINCT from 53..55: besides the tier, it does a full scoreboard RAZ (40
        // slots, dword_168653C/16865DC/168667C) -- verified in the disasm (i:0..4 x j:0..10
        // loop), NOT a dead store like the final HUD_ShowFloatingMessage (v677 never
        // filled) -- cf. the ApplyAllianceTierReset/ApplyScoreboardFullReset header comment.
        ApplyAllianceTierReset(1);
        ApplyScoreboardFullReset();
    } else if (subOpcode >= 53 && subOpcode <= 55) {
        // Alliance-tier reset alone -- side effects (StrTable005_Get/grid cleanups/"best
        // slot" search) CONFIRMED DEAD, cf. the ApplyAllianceTierReset header comment
        // (53/54 lack 52's scoreboard-RAZ loop; 55 has a DIFFERENT loop, itself dead).
        ApplyAllianceTierReset(static_cast<int32_t>(subOpcode) - 51); // 53->2,54->3,55->4
    } else if (subOpcode == 56) {
        ApplyAllianceTierReset(0);
    } else if (subOpcode == 57) {
        Apply57(payload, len);
    } else if (subOpcode == 58) {
        Apply58(idx, arg2); // message omitted (name buffer byte_1686334 not written here).
    } else if (subOpcode == 59) {
        Apply59(idx, arg2, static_cast<int32_t>(arg3));
    } else if (subOpcode == 60) {
        ApplyClassTagNotice(idx, tag13, 773);
    } else if (subOpcode == 61) {
        ApplyClassTagNotice(idx, tag13, 774);
    } else if (subOpcode == 62) {
        Apply62(idx, static_cast<int32_t>(arg2));
    } else if (subOpcode == 63) {
        // "Skill branch" family (Skill_GetMotionId2) -- "poll" preamble BEFORE the 69..100
        // cycle, cf. the ApplyBranchPoll header comment.
        ApplyBranchPoll(idx, arg2, arg3);
    } else if (subOpcode == 64) {
        ApplyBranchArm(idx, arg2, 1, 232, 2);
    } else if (subOpcode == 65) {
        ApplyBranchArm(idx, arg2, 2, 233, 3);
    } else if (subOpcode == 66) {
        // 1st iteration of the "flagsA" cycle (state=3) -- reuses ApplyBranchFlagsSlot (same
        // function as 80/87/94, cf. the header comment of block "Sub-opcodes 63..68").
        ApplyBranchFlagsSlot(idx, arg2, 3, 0x1675C0C, 0x1675C10);
    } else if (subOpcode == 67) {
        ApplyBranchFlagsSlot(idx, arg2, 4, 0x1675C34, 0x1675C38); // 1st iteration "flagsB".
    } else if (subOpcode == 68) {
        ApplyBranchSoundOnlySlot(idx, arg2, 5); // 1st iteration "soundOnly".
    } else if (subOpcode == 69) {
        // "Skill branch" cycle 69..100 -- 1st iteration, "probe" slot (state=23,
        // Skill_IsAvailableByBranch result discarded, cf. ApplyBranchProbe/the 76..100
        // header comment).
        ApplyBranchProbe(idx, arg2);
    } else if (subOpcode == 70) {
        ApplyBranchLabel418(idx, arg2); // 1st iteration "L418" (str235 + town return).
    } else if (subOpcode == 71) {
        ApplyBranchLabel420(idx, arg2, 6); // 1st iteration "L420" (sound only, state=6).
    } else if (subOpcode == 72) {
        ApplyBranchLabel422(idx, arg2); // 1st iteration "L422" (str237 + town return).
    } else if (subOpcode == 73) {
        ApplyBranchFlagsSlot(idx, arg2, 7, 0x1675C14, 0x1675C18); // 2nd iteration "flagsA".
    } else if (subOpcode == 74) {
        ApplyBranchFlagsSlot(idx, arg2, 8, 0x1675C3C, 0x1675C40); // 2nd iteration "flagsB".
    } else if (subOpcode == 75) {
        ApplyBranchSoundOnlySlot(idx, arg2, 9); // 2nd iteration "soundOnly".
    } else if (subOpcode == 76 || subOpcode == 83 || subOpcode == 90 || subOpcode == 97) {
        // "Skill branch" family (Skill_GetMotionId2), "probe" slot -- cf. ApplyBranchProbe
        // at the top of WorldEntityDispatch_BranchSkill.cpp for the full 76..100 cycle.
        ApplyBranchProbe(idx, arg2);
    } else if (subOpcode == 77 || subOpcode == 84 || subOpcode == 91 || subOpcode == 98) {
        ApplyBranchLabel418(idx, arg2);
    } else if (subOpcode == 78) {
        ApplyBranchLabel420(idx, arg2, 10);
    } else if (subOpcode == 85) {
        ApplyBranchLabel420(idx, arg2, 14);
    } else if (subOpcode == 92) {
        ApplyBranchLabel420(idx, arg2, 18);
    } else if (subOpcode == 99) {
        ApplyBranchLabel420(idx, arg2, 22);
    } else if (subOpcode == 79 || subOpcode == 86 || subOpcode == 93 || subOpcode == 100) {
        ApplyBranchLabel422(idx, arg2);
    } else if (subOpcode == 80) {
        ApplyBranchFlagsSlot(idx, arg2, 11, 0x1675C1C, 0x1675C20);
    } else if (subOpcode == 87) {
        ApplyBranchFlagsSlot(idx, arg2, 15, 0x1675C24, 0x1675C28);
    } else if (subOpcode == 94) {
        ApplyBranchFlagsSlot(idx, arg2, 19, 0x1675C2C, 0x1675C30);
    } else if (subOpcode == 81) {
        ApplyBranchFlagsSlot(idx, arg2, 12, 0x1675C44, 0x1675C48);
    } else if (subOpcode == 88) {
        ApplyBranchFlagsSlot(idx, arg2, 16, 0x1675C4C, 0x1675C50);
    } else if (subOpcode == 95) {
        ApplyBranchFlagsSlot(idx, arg2, 20, 0x1675C54, 0x1675C58);
    } else if (subOpcode == 82) {
        ApplyBranchSoundOnlySlot(idx, arg2, 13);
    } else if (subOpcode == 89) {
        ApplyBranchSoundOnlySlot(idx, arg2, 17);
    } else if (subOpcode == 96) {
        ApplyBranchSoundOnlySlot(idx, arg2, 21);
    } else if (subOpcode >= 101 && subOpcode <= 115) {
        // Duel/challenge (101-109/112/114) + "branch mastery" (111/115) -- cf.
        // ApplyDuelBranchFamily for the detail (110 out of scope for this sub-range:
        // "branch" family 76-100 above; 113 is entirely gated on an unavailable buffer, cf.
        // the ApplyDuelBranchFamily header comment).
        ApplyDuelBranchFamily(subOpcode, payload, len);
    } else if (subOpcode >= 201 && subOpcode <= 208) {
        // "Individual arena" family (fixed morph 194) -- cf. ApplyIndividualArenaFamily.
        ApplyIndividualArenaFamily(subOpcode, idx);
    } else if (subOpcode == 401) {
        ApplyNotice401();
    } else if (subOpcode >= 402 && subOpcode <= 410) {
        // "Special" family (Skill_GetSpecialMotionId), same mechanical shape as the combo
        // families -- cf. ApplySpecialFamilySlot for the 3 divergences.
        ApplySpecialFamilySlot(static_cast<int>(subOpcode) - 401, idx, arg2);
    } else if (subOpcode >= 755 && subOpcode <= 763) {
        // Combo family 4 (weapon 4, Skill_GetComboMotionId(4, idx)) -- cf.
        // ApplyComboFamily4Slot for the 2 divergences (no timestamp, richer slot4/slot7).
        ApplyComboFamily4Slot(static_cast<int>(subOpcode) - 754, idx, arg2);
    } else if (subOpcode >= 411 && subOpcode <= 415) {
        // "Buff" family (Skill_GetBuffMotionId) -- cf. ApplyBuffFamilySlot for the shape (5
        // sub-cases, no availability gate, tag13 = payload+12 like sub-opcode 25).
        ApplyBuffFamilySlot(static_cast<int>(subOpcode) - 410, idx, tag13);
    } else if (subOpcode == 416) {
        ApplyMiscFlagSlot(idx, 1); // dword_1686120[idx]=1, UNRELATED to the Buff family.
    } else if (subOpcode == 417) {
        ApplyMiscFlagSlot(idx, 0); // dword_1686120[idx]=0.
    } else if (subOpcode == 418) {
        Apply418(static_cast<int32_t>(idx));
    } else if (subOpcode == 419) {
        Apply419(payload, len); // tag at payload+8, NOT +12 (cf. the header comment above).
    } else if (subOpcode == 420) {
        Apply420(static_cast<int32_t>(idx));
    } else if (subOpcode == 421) {
        Apply421();
    } else if (subOpcode == 422 || subOpcode == 424) {
        Apply422Or424(); // state only -- message/warp omitted (buffer byte_1686138 not received here).
    } else if (subOpcode == 423 || subOpcode == 429) {
        ApplyZone291CountNotice(static_cast<int32_t>(idx)); // LABEL_135 tail-merge.
    } else if (subOpcode == 301) {
        Apply301(idx, arg2);
    } else if (subOpcode == 302) {
        Apply302(idx, static_cast<int32_t>(arg2));
    } else if (subOpcode == 500) {
        Apply500();
    } else if (subOpcode >= 601 && subOpcode <= 610) {
        ApplyLevelBonusFamily(subOpcode, payload, len);
    } else if (subOpcode >= 611 && subOpcode <= 615) {
        ApplyElementNotifyFamily(subOpcode, idx, arg2);
    } else if (subOpcode == 620) {
        Apply620(idx);
    } else if (subOpcode == 621) {
        Apply621();
    } else if (subOpcode == 622) {
        Apply622();
    } else if (subOpcode == 624) {
        Apply624(idx);
    } else if (subOpcode == 625) {
        Apply625(idx);
    } else if (subOpcode == 626) {
        Apply626();
    } else if (subOpcode == 628) {
        Apply628(payload, len);
    } else if (subOpcode >= 629 && subOpcode <= 652) {
        // "War declaration / stage" family -- cf. ApplyWarStageFamily for the shape (state
        // dword_168618C[elt] + precise TODOs omitted, at the top of the function).
        ApplyWarStageFamily(subOpcode, payload, len);
    } else if (subOpcode >= 659 && subOpcode <= 669) {
        ApplyArenaFamily(subOpcode, payload, len);
    } else if (subOpcode >= 671 && subOpcode <= 677) {
        ApplyClassTagFamily(subOpcode, payload, len);
    } else if (subOpcode >= 700 && subOpcode <= 729) {
        ApplySiegeStage2Family(subOpcode, payload, len);
    } else if (subOpcode >= 740 && subOpcode <= 749) {
        ApplyRankFamily740(subOpcode, payload, len);
    } else if (subOpcode == 751) {
        Apply751(payload, len);
    } else if (subOpcode == 752 || subOpcode == 753) {
        ApplyRankTable(subOpcode, payload, len);
    } else if (subOpcode == 754) {
        Apply754(payload, len);
    } else if (subOpcode >= 771 && subOpcode <= 774) {
        ApplyCastAnnounce771to774(subOpcode, payload, len);
    } else if (subOpcode == 788 || subOpcode == 789 || subOpcode == 790) {
        ApplySimpleSkillNotice(subOpcode, payload, len);
    } else if (subOpcode == 791) {
        Apply791(payload, len);
    } else if (subOpcode >= 792 && subOpcode <= 794) {
        Apply792to794(subOpcode, payload, len);
    } else if (subOpcode == 795) {
        Apply795(payload, len);
    } else if (subOpcode >= 780 && subOpcode <= 786) {
        ApplyWarEvent780to786(subOpcode, payload, len);
    } else if (subOpcode >= 800 && subOpcode <= 807) {
        // 803 confirmed no-op (dead store on the binary side) -- covered by
        // ApplyWarEvent800to807, which falls through to its own `default: return;` for
        // this value.
        ApplyWarEvent800to807(subOpcode, payload, len);
    } else if (subOpcode >= 901 && subOpcode <= 903) {
        Apply901to903(subOpcode);
    }
    // 33..45 WIRED (ApplyElementLoadoutFamily), 46..47 WIRED (ApplyAlliancePairFamily,
    // mission "wire ElementPairTable" 2026-07-14 -- alliance pairing, now feeds
    // ElementPairTable with real data, cf. Game/SkillCombat.h), 48..50 WIRED
    // (ApplyAllianceLabelFamily) -- the whole 33..50 range is WIRED (mission "element
    // loadout" 2026-07-14). 51..75 and 101..115 dense also WIRED above (301-302/425-428/500/
    // 600/764-770 -- no-op/sparse confirmed; 76..100 WIRED above, mission "sub-range 76..100"
    // 2026-07-14; 201..208 WIRED above, "individual arena" family fixed morph 194, mission
    // "201-208" 2026-07-14): faithful no-op for any remaining sub-opcode not listed, matching
    // the original switch's `default: return;`. 116..200 CONFIRMED EMPTY (no `case` in the
    // full disassembly, "31..170 survey" 2026-07-14) -- not a TODO. 425-428: gate ENTIRELY on
    // byte_1686138 (name buffer not received here, cf. the header comment of
    // ApplyZone291CountNotice/Apply422Or424 in WorldEntityDispatch_Special.cpp) -- no
    // unconditional state write to preserve, unlike 422/424.
}

} // namespace ts2::game

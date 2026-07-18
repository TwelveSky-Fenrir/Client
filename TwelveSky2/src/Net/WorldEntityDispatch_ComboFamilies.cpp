// Net/WorldEntityDispatch_ComboFamilies.cpp — split of Net_OnWorldEntityDispatch (opcode
// 0x5e, EA 0x494870): combo families 1/2/3/4 (Skill_GetComboMotionId weaponType 1/2/3/4) and
// their per-slot/tag helpers. See Net/WorldEntityDispatch.h for the full sub-opcode map and
// Net/WorldEntityDispatch_Internal.h for the shared ComboFamilyCtx/kFamily1/2/3 plumbing.
//
// Scope: sub-opcodes 1..30 (combo families 1, 2 and 3, fully read from the disassembly —
// family 3 has 12 sub-cases, not 9, cf. the ApplyFamily3TagSlot comment below) and 755..763
// (combo family 4, weapon 4 — NOT an immediate continuation of family 3: it is the ONLY other
// weaponType value used by Skill_GetComboMotionId in the whole dispatcher, confirmed by
// exhaustive grep of the full disassembly; no "family 5" of this kind exists).
//
// UPDATE (2026-07-14, Docs/TS2_COMBAT_ELEMENT_GATING.md): SkillLevelTable and ElementPairTable
// are now exposed globally (Game/SkillCombat.h::GetSkillLevelTable()/Combat_ReadLocalElementPairs(),
// same g_Client.Var()/VarGet() escape hatches as g_SelfMorphNpcId). Slots 1/2/3/7 of combo
// families 1/2/3 (including 19/20/21/28 in family 3 via ApplyFamily3Slot28) and 1/2/3 in
// family 4 (755/756/757) are therefore fully wired, message included, below. STILL OMITTED
// (a precise, documented-at-the-site limit): family 4's slot7 (761) — its message decodes a
// 4-digit field at a payload offset not confirmed in this pass (beyond the confirmed 80-byte
// raw block), left TODO rather than risk an unfaithful fabrication. The filtered popup
// (UI_MsgBox_Open, slot2 of every family) stays OUT OF SCOPE (no UI sink modeled in
// ClientRuntime, same convention as other popups noted TODO elsewhere in this split, e.g.
// sub-opcode 601/660): only the chat message (Msg_AppendSystemLine) is wired.
// Slots gated by `motion == g_SelfMorphNpcId` (4/6/8, including 22/24/29, 758/760/762, AND the
// 3 sub-cases specific to family 3: 25/26/27) were ALREADY calculable (direct read of
// g_SelfMorphNpcId via the Var() escape hatch) and hence already fully wired, message included.
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
namespace wed_detail {
namespace {

// Half-duration slot (slot4, family 1 only -- kFamily1.writeHalfDur=true, cf.
// Net/WorldEntityDispatch_Internal.h::ComboFamilyCtx); families 2/3 never write it
// (confirmed asymmetry in the disasm).
constexpr uint32_t kFam1HalfDur = 0x1675BB4;

// Skill availability by level (Skill_IsAvailableByLevel, gate for slots 1/2/3/7 of combo
// families 1/2/3/4) — SkillLevelTable exposed via Game/SkillCombat.h::GetSkillLevelTable(),
// same g_World.self escape hatch for level/levelBonus and Var() for the rebirth tier
// (kRebirthTierAddr).
inline bool ComboSkillAvailable(int comboMotionId) {
    return Skill_IsAvailableByLevel(GetSkillLevelTable(), comboMotionId, g_World.self.level,
                                     g_World.self.levelBonus, g_Client.VarGet(kRebirthTierAddr));
}

} // namespace

// `idx` = payload+4 (skill index within the family); `arg2` = payload+8 (secondary
// parameter, read only by slots 1/4/7). `slot` = position 1..9 within the family
// (sub-opcode - family base).
void ApplyComboFamilySlot(const ComboFamilyCtx& fam, int slot, uint32_t idx, uint32_t arg2) {
    const int  comboMotionId = Skill_GetComboMotionId(fam.weaponType, static_cast<int>(idx));
    const bool isCurrentMorph = (comboMotionId == g_Client.VarGet(kSelfMorphNpcId));

    switch (slot) {
    case 1:
        // (sub-opcode base+0, EA 0x494a17/0x4951b0) — NO state write in the binary.
        // Gate = Skill_IsAvailableByLevel -> WIRED (SkillLevelTable exposed via
        // GetSkillLevelTable()): message "<name> [<arg2>]<str231>".
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " [" + std::to_string(arg2) + "]" + Str(231));
        }
        break;

    case 2:
        // (base+1) state=1, WRITTEN unconditionally (precedes the gate in the decompilation).
        // Gate = Skill_IsAvailableByLevel -> WIRED: message "<name> <str232>". Filtered popup
        // (str242, g_Options.FilterWorldEntity option) + dword_1675BA0=comboMotionId: OUT OF
        // SCOPE (no UI_MsgBox_Open sink modeled in ClientRuntime, same convention as the other
        // popups noted TODO elsewhere in this split, e.g. sub-opcode 601/660).
        g_Client.Var(fam.stateAddr + 4 * idx) = 1;
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(232));
        }
        break;

    case 3:
        // (base+2) state=2, written unconditionally. Gate = Skill_IsAvailableByLevel ->
        // WIRED: message "<name> <str233>" (via the LABEL_9 tail-merge, shared with
        // sub-opcode 402/Special).
        g_Client.Var(fam.stateAddr + 4 * idx) = 2;
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(233));
        }
        break;

    case 4:
        // (base+3) state=3, written unconditionally. Gate = motion==current morph
        // (CALCULABLE) -> fully wired: message "<name> <str234>" + arms the charge bar
        // (family-specific flags + shared timer/elapsed) + (family 1 only) half-duration =
        // arg2/2.
        g_Client.Var(fam.stateAddr + 4 * idx) = 3;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(234));
            g_Client.Var(fam.chargeFlag1Addr) = 1;
            g_Client.Var(fam.chargeFlag0Addr) = 0;
            g_Client.Var(kChargeArmedTimer)   = 1;
            g_Client.VarF(kChargeElapsed)     = 0.0f;
            if (fam.writeHalfDur) {
                g_Client.Var(kFam1HalfDur) = static_cast<int32_t>(arg2) / 2; // family 1 only
            }
        }
        break;

    case 5:
        // (base+4) state=5, timestamp=current day. Gate = availability BUT the result is
        // DISCARDED in the binary (pure function, no branch, no message) -> reproduced
        // identically without the gate.
        g_Client.Var(fam.stateAddr + 4 * idx) = 5;
        g_Client.Var(fam.stampAddr + 4 * idx) = Time_GetMonthDayInt();
        break;

    case 6:
        // (base+5) state=5, timestamp=current day, written unconditionally. Gate =
        // motion==current morph (CALCULABLE) -> fully wired: message "<name> <strSlot6>" +
        // return to faction town.
        g_Client.Var(fam.stateAddr + 4 * idx) = 5;
        g_Client.Var(fam.stampAddr + 4 * idx) = Time_GetMonthDayInt();
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(fam.strSlot6));
            WarpToOwnFactionTown();
        }
        break;

    case 7:
        // (base+6) state=4, timestamp=current day, written unconditionally. arg2 = element
        // id to pair (Char_GetPairedElement). Gate = Skill_IsAvailableByLevel; message
        // depends on the element pairing (ElementPairTable)/class label -> WIRED
        // (GetSkillLevelTable() + Combat_ReadLocalElementPairs(), same shape as the binary's
        // LABEL_17/LABEL_18 tail-merge: short form if unpaired, long form
        // "[argLabel],[pairedLabel]" otherwise). NB: family 3 (sub-opcode 28) does NOT go
        // through this case — distinct form (tag13 + str246 + own timer), cf.
        // ApplyFamily3Slot28 below.
        g_Client.Var(fam.stateAddr + 4 * idx) = 4;
        g_Client.Var(fam.stampAddr + 4 * idx) = Time_GetMonthDayInt();
        if (ComboSkillAvailable(comboMotionId)) {
            const ElementPairTable pairs = Combat_ReadLocalElementPairs();
            const int paired = pairs.Paired(static_cast<int>(arg2));
            std::string msg = SkillName(comboMotionId) + " [" + ClassLabel(static_cast<int>(arg2)) + "]";
            if (paired != -1) msg += ",[" + ClassLabel(paired) + "]";
            msg += " " + Str(236);
            g_Client.msg.System(msg);
        }
        break;

    case 8:
        // (base+7, LABEL_544 tail-merge) state=5, written unconditionally. Gate =
        // motion==current morph (CALCULABLE) -> wired: message "<name> <str237>" + return to
        // faction town.
        g_Client.Var(fam.stateAddr + 4 * idx) = 5;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(237));
            WarpToOwnFactionTown();
        }
        break;

    case 9:
        // (base+8) state=0 (reset). Gate = availability, result discarded, no message in the
        // binary -> reproduced identically.
        g_Client.Var(fam.stateAddr + 4 * idx) = 0;
        break;

    default:
        break;
    }
}

namespace {

// Timer specific to sub-opcode 25 (EA 0x495f47/0x495f53) — distinct from the shared charge
// bar slot4; has no analogue in families 1/2 (cf. ApplyFamily3TagSlot).
constexpr uint32_t kFam3TagArmedTimer = 0x1675BDC, kFam3TagElapsed = 0x1675BE0;

} // namespace

// Family 3 — sub-opcodes 25..27 (EA 0x495d66..0x4960bc), WITH NO analogue in families 1/2:
// they sit between slot6 (24, str235+town return) and the resumed standard slot7 (28, cf.
// case 7 of ApplyComboFamilySlot above). No state/timestamp written (dword_1685F44/1685F6C
// untouched); gate = motion == g_SelfMorphNpcId (CALCULABLE, like slots 4/6/8) -> fully
// wireable, message included (no external table required, unlike slots 1/3/7 gated by
// SkillLevelTable).

// Sub-opcode 25 (EA 0x495d66): idx = payload+4, class branch (0..3) = payload+8, raw 13-byte
// text tag (pairing object/skill name, no table to resolve) = payload+12. Message
// "<name> [<class>] [<tag>] <str243>" (class = str75+branch, same convention as sub-opcodes
// 31/32/35 later in the switch); arms a timer SPECIFIC to this sub-case
// (kFam3TagArmedTimer/kFam3TagElapsed, distinct from the shared slot4 charge bar
// dword_1675BA4/flt_1675BA8).
void ApplyFamily3TagSlot(uint32_t idx, uint32_t classBranch, const char* tag13) {
    const int comboMotionId = Skill_GetComboMotionId(3, static_cast<int>(idx));
    if (comboMotionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    const int classStrId = 75 + static_cast<int>(classBranch & 3); // str75..78 (EA 0x495e0f..0x495f1a)
    g_Client.msg.System(SkillName(comboMotionId) + " [" + Str(classStrId) + "] [" + tag13 + "] " + Str(243));
    g_Client.Var(kFam3TagArmedTimer) = 1;
    g_Client.VarF(kFam3TagElapsed)   = 0.0f;
}

// Sub-opcode 26 (EA 0x495f6e): idx = payload+4. No state, no timer. Message
// "<name> <str244>" alone.
void ApplyFamily3Slot26(uint32_t idx) {
    const int comboMotionId = Skill_GetComboMotionId(3, static_cast<int>(idx));
    if (comboMotionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    g_Client.msg.System(SkillName(comboMotionId) + " " + Str(244));
}

// Sub-opcode 27 (EA 0x496010): idx = payload+4, arg2 = payload+8 (parameter displayed
// as-is, not an id to resolve). No state. Message "<name> [<arg2>]<str245>".
void ApplyFamily3Slot27(uint32_t idx, uint32_t arg2) {
    const int comboMotionId = Skill_GetComboMotionId(3, static_cast<int>(idx));
    if (comboMotionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    g_Client.msg.System(SkillName(comboMotionId) + " [" + std::to_string(arg2) + "]" + Str(245));
}

namespace {

// Timer specific to sub-opcode 28 (dword_1675BE4/flt_1675BE8, EA 0x496255/0x496261) —
// DISTINCT from sub-opcode 25's timer (kFam3TagArmedTimer/kFam3TagElapsed) and from the
// shared slot4 charge bar (kChargeArmedTimer/kChargeElapsed).
constexpr uint32_t kFam3PairArmedTimer = 0x1675BE4, kFam3PairElapsed = 0x1675BE8;

} // namespace

// Sub-opcode 28 (EA 0x496277..0x49636d) — resumes the standard slot7 (state=4,
// timestamp=current day, written unconditionally, same shape as case 7 of
// ApplyComboFamilySlot) BUT with a tag13 (payload+12, the SAME raw field as sub-opcode 25)
// and its OWN string/timer (str246 instead of 236, "[class-tag]" format instead of
// "[class]", kFam3PairArmedTimer/Elapsed timer instead of the shared one) — confirmed
// EXHAUSTIVELY by direct decompilation (diverges from the generic case 7 of
// ApplyComboFamilySlot, routed independently, cf. the top-level dispatch). Gate =
// Skill_IsAvailableByLevel; message = Char_GetPairedElement (ElementPairTable) +
// Str_GetClassLabel, same escape hatches as the generic case 7.
void ApplyFamily3Slot28(uint32_t idx, uint32_t elt, const char* tag13) {
    g_Client.Var(kFam3State + 4 * idx) = 4;
    g_Client.Var(kFam3Stamp + 4 * idx) = Time_GetMonthDayInt();
    const int comboMotionId = Skill_GetComboMotionId(3, static_cast<int>(idx));
    if (!ComboSkillAvailable(comboMotionId)) return;
    const ElementPairTable pairs = Combat_ReadLocalElementPairs();
    const int paired = pairs.Paired(static_cast<int>(elt));
    std::string msg = SkillName(comboMotionId) + " [" + ClassLabel(static_cast<int>(elt)) + "-" + tag13 + "]";
    if (paired != -1) msg += ",[" + ClassLabel(paired) + "]";
    msg += " " + Str(246);
    g_Client.msg.System(msg);
    g_Client.Var(kFam3PairArmedTimer) = 1;
    g_Client.VarF(kFam3PairElapsed)   = 0.0f;
}

namespace {

constexpr uint32_t kFam4State = 0x16862F0;
constexpr uint32_t kFam4ChargeFlag1 = 0x1675E90, kFam4ChargeFlag0 = 0x1675E98, kFam4HalfDur = 0x1675E9C;
constexpr uint32_t kFam4ResetA = 0x1675E94, kFam4ResetB0 = 0x1675EA0,
                    kFam4ResetB1 = 0x1675EA4, kFam4ResetB2 = 0x1675EA8, kFam4ResetB3 = 0x1675EAC;

} // namespace

// Combo family 4 (weapon 4, Skill_GetComboMotionId(4, idx), sub-opcodes 755..763, EA
// 0x4a2159..0x4a293c): SAME 9-slot mechanic, with two divergences confirmed by full
// disassembly reading:
//   - NO daily timestamp table (dword_16862F0 = state ONLY — the only combo family with no
//     daily cooldown at slots 5/6/7/8, unlike families 1/2/3 which all write a timestamp
//     where this one writes none).
//   - slot4 (758) MUCH richer than families 1/2/3: besides arming its OWN flag
//     (dword_1675E90/E98) AND the SHARED charge bar (dword_1675BA4/flt_1675BA8, like families
//     1/2/3), it replays a 2nd sound (flt_1491A3C), shows a 2nd chat line
//     "[halfDuration]<str843>" (dword_1675E9C=arg2/2) and resets a 5-field block
//     (dword_1675E94/EA0[0]/EA4/EA8/EAC) — shape close to the "Special" family's slot4 (same
//     4/5-field reset + halfDur + 2nd message) but WITH the shared-bar touch and 2nd sound
//     that Special lacks.
//   - slot7 (761) does NOT go through Char_GetPairedElement: the payload carries a raw
//     80-byte block (copied UNCONDITIONALLY by the binary into the global dword_1676054, EA
//     0x4a26b7) followed by a 4-digit integer decodable into up to 4 class ids (message per
//     valid digit >=0, Str_GetClassLabel loop). Gate = Skill_IsAvailableByLevel — NOW
//     AVAILABLE (GetSkillLevelTable()) BUT the block/decoding stays OMITTED HERE: the binary
//     reads the 4-digit integer at a payload offset BEYOND the confirmed 80-byte raw block
//     (i.e. beyond idx=payload+4/arg2=payload+8/tag13=payload+12, the ONLY offsets confirmed
//     safe and already used throughout this file) — not re-validated bit-for-bit against the
//     disassembly in this pass, left TODO rather than risk a fabricated offset. The raw
//     80-byte block itself STILL HAS NO consumer modeled in this dispatcher
//     (dword_1676054 is never read afterward) -> not persisted here (no ClientSource-side
//     sink to date), precise TODO.
void ApplyComboFamily4Slot(int slot, uint32_t idx, uint32_t arg2) {
    const int  comboMotionId = Skill_GetComboMotionId(4, static_cast<int>(idx));
    const bool isCurrentMorph = (comboMotionId == g_Client.VarGet(kSelfMorphNpcId));

    switch (slot) {
    case 1: // 755 -- no state write. Gate=Skill_IsAvailableByLevel -> WIRED.
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " [" + std::to_string(arg2) + "]" + Str(231));
        }
        break;
    case 2: // 756 state=1, written unconditionally. Gate -> WIRED (filtered popup OUT OF SCOPE).
        g_Client.Var(kFam4State + 4 * idx) = 1;
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(232));
        }
        break;
    case 3: // 757 state=2, written unconditionally. Gate -> WIRED.
        g_Client.Var(kFam4State + 4 * idx) = 2;
        if (ComboSkillAvailable(comboMotionId)) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(233));
        }
        break;
    case 4: // 758 state=3, written unconditionally. Gate=morph (CALCULABLE) -> wired.
        g_Client.Var(kFam4State + 4 * idx) = 3;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(234));
            g_Client.Var(kFam4ChargeFlag1)  = 1;
            g_Client.Var(kFam4ChargeFlag0)  = 0;
            g_Client.Var(kChargeArmedTimer) = 1;   // shared bar (dword_1675BA4)
            g_Client.VarF(kChargeElapsed)   = 0.0f;
            const int32_t halfDur = static_cast<int32_t>(arg2) / 2;
            g_Client.Var(kFam4HalfDur) = halfDur;
            g_Client.msg.System("[" + std::to_string(halfDur) + "]" + Str(843));
            g_Client.Var(kFam4ResetA)  = 1;
            g_Client.Var(kFam4ResetB0) = 0;
            g_Client.Var(kFam4ResetB1) = 0;
            g_Client.Var(kFam4ResetB2) = 0;
            g_Client.Var(kFam4ResetB3) = 0;
        }
        break;
    case 5: // 759 state=5. NO timestamp (family-4 asymmetry). Gate=availability discarded.
        g_Client.Var(kFam4State + 4 * idx) = 5;
        break;
    case 6: // 760 state=5, gate=morph -> message str860 + return to faction town.
        g_Client.Var(kFam4State + 4 * idx) = 5;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(860));
            WarpToOwnFactionTown();
        }
        break;
    case 7: // 761 state=4, written unconditionally. Gate=Skill_IsAvailableByLevel AVAILABLE
            // (ComboSkillAvailable) but TODO(offset): the message decodes an integer at a
            // payload offset not confirmed in this pass (beyond the 80-byte raw block), cf.
            // the header comment -- not wired to avoid fabricating an offset.
        g_Client.Var(kFam4State + 4 * idx) = 4;
        (void)arg2; // read/copied by the binary (payload+4) but never reused outside the gate.
        break;
    case 8: // 762 state=5, gate=morph -> message str237 + return to faction town.
        g_Client.Var(kFam4State + 4 * idx) = 5;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(comboMotionId) + " " + Str(237));
            WarpToOwnFactionTown();
        }
        break;
    case 9: // 763 state=0. Gate=availability, result discarded.
        g_Client.Var(kFam4State + 4 * idx) = 0;
        break;
    default:
        break;
    }
}

} // namespace wed_detail
} // namespace ts2::game

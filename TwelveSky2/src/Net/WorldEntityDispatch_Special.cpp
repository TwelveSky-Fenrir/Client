// Net/WorldEntityDispatch_Special.cpp — split of Net_OnWorldEntityDispatch (opcode 0x5e, EA
// 0x494870): the standalone HUD notice at 401, the "Special" skill family (402..410,
// Skill_GetSpecialMotionId), the "Buff" family (411..417, Skill_GetBuffMotionId + 2 trivial
// adjacent flags), the "individual arena" family (201..208, fixed morph 194), and the
// zone-291 notification cluster (418..429, "end of the Buff block" mission). See
// Net/WorldEntityDispatch.h for the full sub-opcode map.
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

// ---------------------------------------------------------------------------
// "Individual arena" family (fixed morph 194, RE/dispatch_494870_full.c L.2790-2899,
// sub-opcodes 201..208) — FULLY WIRED (2026-07-14, direct full-disassembly reading). SCALAR
// state dword_1686054 (NOT an array indexed by idx, unlike the combo/Special/4 families —
// only one individual-arena instance possible at a time). NO Skill_IsAvailableByLevel/
// Skill_IsSpecialUsable gate anywhere in this family (verified: all 8 sub-cases write their
// message WITHOUT ever consulting SkillLevelTable, unlike ALL the combo/Special/4 families
// above) — the only gate is g_SelfMorphNpcId==194 (204/206/207). Sound
// (Snd3D_PlayScaledVolume) OUT OF SCOPE audio, same convention as the rest of this file.
//   201 (L.2790): idx=payload+4. UNCONDITIONAL message "<name194> [<idx>]<str231>". No state
//        written (the only sub-case in the family with no state write -- consistent with the
//        combo/Special/4 families' slot1, which also write no state).
//   202 (L.2800): state=1. Unconditional message "<name194> <str232>".
//   203 (L.2808): state=2. Unconditional message "<name194> <str233>".
//   204 (L.2816): state=3. Gate=morph==194 -> message "<name194> <str234>" + an OWN flag
//        (dword_1675C84/flt_1675C88, DISTINCT from the combo/4 families' shared bar
//        kChargeArmedTimer/kChargeElapsed -- NEVER touched here) + FIXED half-duration=600
//        (dword_1675C8C=600 hardcoded, NOT arg2/2 like combo family 1/Special/4 -- idx isn't
//        even read by this sub-case) + 2nd message "<name194> [600]<str843>" + 4-field reset
//        (dword_1675C90[0]/94/98/9C, no prior "ResetA=1" unlike the Special family).
//   205 (L.2840): state=5 only, no message (like the combo/Special families' slot5).
//   206 (L.2844): idx=payload+4 (element to pair). state=4 unconditional. IF idx==-1
//        (0xFFFFFFFF) -> SHORT message "<name194> <str845>" (NO class label, a branch ABSENT
//        from the generic slot7 of the combo families -- specific to this family). ELSE ->
//        Char_GetPairedElement (Combat_ReadLocalElementPairs), message "[<class>]" or
//        "[<class>],[<pair>]" + str236 (same shape as the generic slot7). Message ALWAYS sent
//        (no SkillLevelTable gate). Warp IF morph==194 AND local element != idx AND local
//        element != pair AND g_SelfCharInvBlock[0] (via Map_BeginWarpToFactionTownDefault,
//        NOT BeginWarpToFactionTown: this family never uses the other warps' "dead" guard).
//        TODO(@idx=-1): in the idx==-1 branch, the original local variable "PairedElement" is
//        not recomputed -- it is SHARED by the compiler with ~5 other sub-cases of the same
//        giant switch (BYREF, cf. RE/dispatch_494870_full.c:735), so its exact value on this
//        precise branch is not statically determinable from the pseudocode alone —
//        approximated here as paired=-1 (a fallback documented elsewhere in this file as the
//        default "no pair" behavior, which never blocks the warp on the "!= pair" comparison)
//        — plausible (idx=-1 = "no opponent", the game then returns you to town) but NOT
//        verified bit-for-bit against the binary.
//   207 (L.2883): state=5. Gate=morph==194 -> message "<name194> <str237>"; warp ONLY IF
//        g_SelfCharInvBlock[0] (unlike the combo/Special/4/Buff families' generic slot8,
//        which warp as soon as the morph gate passes, no extra condition).
//   208 (L.2896): state=0 (reset). No message (like the generic slot9).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kIndivArenaState = 0x1686054; // scalar, NOT an indexed array
constexpr int32_t  kIndivArenaMorph = 194;
constexpr uint32_t kIndivArenaOwnFlag    = 0x1675C84; // dword_1675C84 -- OWN flag (not the shared bar)
constexpr uint32_t kIndivArenaOwnElapsed = 0x1675C88; // flt_1675C88
constexpr uint32_t kIndivArenaHalfDur    = 0x1675C8C; // dword_1675C8C -- FIXED=600, never arg2/2 here
constexpr uint32_t kIndivArenaReset0 = 0x1675C90, kIndivArenaReset1 = 0x1675C94,
                    kIndivArenaReset2 = 0x1675C98, kIndivArenaReset3 = 0x1675C9C;
} // namespace

void ApplyIndividualArenaFamily(uint32_t subOpcode, uint32_t idx) {
    switch (subOpcode) {
    case 201:
        g_Client.msg.System(SkillName(kIndivArenaMorph) + " [" + std::to_string(idx) + "]" + Str(231));
        return;
    case 202:
        g_Client.Var(kIndivArenaState) = 1;
        g_Client.msg.System(SkillName(kIndivArenaMorph) + " " + Str(232));
        return;
    case 203:
        g_Client.Var(kIndivArenaState) = 2;
        g_Client.msg.System(SkillName(kIndivArenaMorph) + " " + Str(233));
        return;
    case 204:
        g_Client.Var(kIndivArenaState) = 3;
        if (g_Client.VarGet(kSelfMorphNpcId) == kIndivArenaMorph) {
            g_Client.msg.System(SkillName(kIndivArenaMorph) + " " + Str(234));
            g_Client.Var(kIndivArenaOwnFlag)     = 1;
            g_Client.VarF(kIndivArenaOwnElapsed) = 0.0f;
            g_Client.Var(kIndivArenaHalfDur)     = 600; // FIXED, not arg2/2 (verified in the disasm)
            g_Client.msg.System(SkillName(kIndivArenaMorph) + " [600]" + Str(843));
            g_Client.Var(kIndivArenaReset0) = 0; g_Client.Var(kIndivArenaReset1) = 0;
            g_Client.Var(kIndivArenaReset2) = 0; g_Client.Var(kIndivArenaReset3) = 0;
        }
        return;
    case 205:
        g_Client.Var(kIndivArenaState) = 5;
        return;
    case 206: {
        g_Client.Var(kIndivArenaState) = 4;
        const int32_t elt = static_cast<int32_t>(idx);
        int32_t paired = -1; // cf. TODO(@idx=-1) above if elt==-1 (never recomputed in that case).
        std::string msg;
        if (elt == -1) {
            msg = SkillName(kIndivArenaMorph) + " " + Str(845);
        } else {
            const ElementPairTable pairs = Combat_ReadLocalElementPairs();
            paired = pairs.Paired(elt);
            msg = SkillName(kIndivArenaMorph) + " [" + ClassLabel(elt) + "]";
            if (paired != -1) msg += ",[" + ClassLabel(paired) + "]";
            msg += " " + Str(236);
        }
        g_Client.msg.System(msg);
        const bool hasCharInvBlock = !g_World.self.charInvBlock.empty(); // g_SelfCharInvBlock[0]
        if (g_Client.VarGet(kSelfMorphNpcId) == kIndivArenaMorph &&
            g_World.self.element != elt && g_World.self.element != paired && hasCharInvBlock) {
            BeginWarpToFactionTownDefault(g_World.self.element);
        }
        return;
    }
    case 207:
        g_Client.Var(kIndivArenaState) = 5;
        if (g_Client.VarGet(kSelfMorphNpcId) == kIndivArenaMorph) {
            g_Client.msg.System(SkillName(kIndivArenaMorph) + " " + Str(237));
            if (!g_World.self.charInvBlock.empty()) BeginWarpToFactionTownDefault(g_World.self.element);
        }
        return;
    case 208:
        g_Client.Var(kIndivArenaState) = 0;
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Sub-opcode 401 (EA 0x497c66) — STANDALONE HUD notification, UNRELATED to the "Special"
// family that follows (no Skill_GetSpecialMotionId call, payload not read). Floating banner
// (type=2, flag=2) + system line. Original g_SysMsgColor color (0x84DFD8) not modeled here
// -> default color, same convention as Game/NpcInteraction.cpp.
// ---------------------------------------------------------------------------
void ApplyNotice401() {
    const std::string text = Str(1139);
    g_Client.msg.Floating(2, 2, text);
    g_Client.msg.System(text);
}

// ---------------------------------------------------------------------------
// "Special" family (Skill_GetSpecialMotionId, sub-opcodes 402..410, EA
// 0x49ca89..0x49d1cf): SAME 9-slot mechanic as the combo families (kFamily1/2/3), with 3
// divergences confirmed by full disassembly reading:
//   - unavailability gate = Skill_IsSpecialUsable(id, self, morph, lvlTbl) instead of
//     Skill_IsAvailableByLevel -- WIRED (SpecialSkillUsable(), same SkillLevelTable exposed
//     via GetSkillLevelTable()): slot 1/2/3/7 messages now present.
//   - slot4 (405) does NOT arm the shared charge bar (dword_1675BA4/flt_1675BA8 NEVER
//     touched here, unlike ALL the combo families 1/2/3/4): only an own flag is armed --
//     dword_1675CB0 (int) THEN flt_1675CB4 (FLOAT, confirmed asymmetry: it's a flt_ in the
//     disasm, not a dword_ like the combo families' flag0) -- plus a half-duration
//     (dword_1675CB8=arg2/2) + 2nd chat line "[halfDuration]<str843>" + 4-field reset
//     (dword_1675CBC/CC0[0]/CC4/CC8/CCC), SAME shape as family 4's slot4 (same 4/5-field
//     reset + halfDur + 2nd message) but WITHOUT touching the shared bar or the 2nd sound.
//   - slot7 (408) uses Char_GetPairedElement (ElementPairTable) -- WIRED
//     (Combat_ReadLocalElementPairs()), same message shape as families 1/2's generic slot7
//     (str236, "[argLabel],[pairedLabel]" if paired).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kSpecialState = 0x16860C0;
constexpr uint32_t kSpecialFlag1 = 0x1675CB0;   // dword_1675CB0 (int)
constexpr uint32_t kSpecialFlag0 = 0x1675CB4;   // flt_1675CB4 (FLOAT -- asymmetry vs the combo families)
constexpr uint32_t kSpecialHalfDur = 0x1675CB8;
constexpr uint32_t kSpecialResetA  = 0x1675CBC, kSpecialResetB0 = 0x1675CC0,
                    kSpecialResetB1 = 0x1675CC4, kSpecialResetB2 = 0x1675CC8, kSpecialResetB3 = 0x1675CCC;

// "Special" family availability (Skill_IsSpecialUsable, gate for slots 1/2/3/7 -- confirmed
// by direct decompilation EA 0x49ca89 onward: it IS Skill_IsSpecialUsable, NOT
// Skill_IsAvailableByLevel, that gates this family). CombatMorphState rebuilt on demand from
// the SAME escape hatches used everywhere in this file (kSelfMorphNpcId/kRebirthTierAddr).
inline bool SpecialSkillUsable(int specialMotionId) {
    const CombatMorphState morph{g_Client.VarGet(kSelfMorphNpcId), g_Client.VarGet(kRebirthTierAddr)};
    return Skill_IsSpecialUsable(specialMotionId, g_World.self, morph, GetSkillLevelTable());
}
} // namespace

void ApplySpecialFamilySlot(int slot, uint32_t idx, uint32_t arg2) {
    const int  specialMotionId = Skill_GetSpecialMotionId(static_cast<int>(idx));
    const bool isCurrentMorph = (specialMotionId == g_Client.VarGet(kSelfMorphNpcId));

    switch (slot) {
    case 1: // 402 -- no state write. Gate=Skill_IsSpecialUsable -> WIRED.
        if (SpecialSkillUsable(specialMotionId)) {
            g_Client.msg.System(SkillName(specialMotionId) + " [" + std::to_string(arg2) + "]" + Str(231));
        }
        break;
    case 2: // 403 state=1, written unconditionally. Gate -> WIRED (filtered popup OUT OF SCOPE).
        g_Client.Var(kSpecialState + 4 * idx) = 1;
        if (SpecialSkillUsable(specialMotionId)) {
            g_Client.msg.System(SkillName(specialMotionId) + " " + Str(232));
        }
        break;
    case 3: // 404 state=2, written unconditionally. Gate -> WIRED.
        g_Client.Var(kSpecialState + 4 * idx) = 2;
        if (SpecialSkillUsable(specialMotionId)) {
            g_Client.msg.System(SkillName(specialMotionId) + " " + Str(233));
        }
        break;
    case 4: // 405 state=3, written unconditionally. Gate=morph (CALCULABLE) -> wired.
        g_Client.Var(kSpecialState + 4 * idx) = 3;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(specialMotionId) + " " + Str(234));
            g_Client.Var(kSpecialFlag1)  = 1;
            g_Client.VarF(kSpecialFlag0) = 0.0f;
            const int32_t halfDur = static_cast<int32_t>(arg2) / 2;
            g_Client.Var(kSpecialHalfDur) = halfDur;
            g_Client.msg.System("[" + std::to_string(halfDur) + "]" + Str(843));
            g_Client.Var(kSpecialResetA)  = 1;
            g_Client.Var(kSpecialResetB0) = 0;
            g_Client.Var(kSpecialResetB1) = 0;
            g_Client.Var(kSpecialResetB2) = 0;
            g_Client.Var(kSpecialResetB3) = 0;
        }
        break;
    case 5: // 406 state=5. Gate=availability, result discarded -> reproduced without the gate.
        g_Client.Var(kSpecialState + 4 * idx) = 5;
        break;
    case 6: // 407 state=5, gate=morph -> message str860 + return to faction town.
        g_Client.Var(kSpecialState + 4 * idx) = 5;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(specialMotionId) + " " + Str(860));
            WarpToOwnFactionTown();
        }
        break;
    case 7: // 408 state=4, written unconditionally. Gate=Skill_IsSpecialUsable; message =
            // Char_GetPairedElement (ElementPairTable) + Str_GetClassLabel -> WIRED (same
            // shape as the combo families' generic slot7, str236).
        g_Client.Var(kSpecialState + 4 * idx) = 4;
        if (SpecialSkillUsable(specialMotionId)) {
            const ElementPairTable pairs = Combat_ReadLocalElementPairs();
            const int paired = pairs.Paired(static_cast<int>(arg2));
            std::string msg = SkillName(specialMotionId) + " [" + ClassLabel(static_cast<int>(arg2)) + "]";
            if (paired != -1) msg += ",[" + ClassLabel(paired) + "]";
            msg += " " + Str(236);
            g_Client.msg.System(msg);
        }
        break;
    case 8: // 409 state=5, gate=morph -> message str237 + return to faction town.
        g_Client.Var(kSpecialState + 4 * idx) = 5;
        if (isCurrentMorph) {
            g_Client.msg.System(SkillName(specialMotionId) + " " + Str(237));
            WarpToOwnFactionTown();
        }
        break;
    case 9: // 410 state=0. Gate=availability, result discarded.
        g_Client.Var(kSpecialState + 4 * idx) = 0;
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// "Buff" family (Skill_GetBuffMotionId, sub-opcodes 411..415, EA 0x49d1d9..0x49d565, state
// dword_16860D0[idx]) -- FULLY WIRED (full disassembly reading). The "different from the
// mechanical families 1-9" block flagged during the initial audit: SIMPLER mechanic (5
// sub-cases, not 9) and, crucially, WITHOUT ANY Skill_IsAvailableByLevel/Skill_IsSpecialUsable
// gate -- unlike ALL the combo/Special families above, the "%s [%s] %s" messages are written
// UNCONDITIONALLY (no availability branch before the Msg_AppendSystemLine):
//   - 411 (slot1, EA 0x49d1d9): state=1, message "<name> [<tag>] <str1244>".
//   - 412 (slot2, EA 0x49d2a5) and 413 (slot3, EA 0x49d359): STRICTLY IDENTICAL BODY (state=2,
//     same sound flt_1498DBC, same suffix str1245) -- confirmed by instruction-by-instruction
//     comparison of both blocks, matching the fusion noted in the initial audit ("412/413
//     merged by the compiler").
//   - 414 (slot4, EA 0x49d40d): state=2, message "<name> [<tag>] <str1246>" (same shape,
//     different suffix, NOT merged with 412/413 -- its own sound flt_1498CFC).
//   - 415 (slot5, EA 0x49d4c1): state=0 (reset), WRITTEN UNCONDITIONALLY. The only sub-case
//     with a gate: motion == g_SelfMorphNpcId (CALCULABLE, same escape hatch as the rest of
//     this file) -> message "<name> <str237>" + return to faction town (Map_
//     BeginWarpToFactionTown), EXACTLY the same shape as the combo/Special families' slot8
//     (str237 shared, cf. ApplyComboFamilySlot/ApplySpecialFamilySlot).
// The bracketed tag is the SAME raw 13-byte field (payload+12, unresolved via a table -- a
// raw name/label) as the `tag13` already read by ApplyFamily3TagSlot (sub-opcode 25,
// WorldEntityDispatch_ComboFamilies.cpp): confirmed on the disassembly (411/412/413/414 all
// read a LOCAL address already populated by the giant function's shared prologue, at the same
// rank as the one used by sub-opcode 25 -- same 13-byte memcpy source pattern, never touched
// again before use as a raw %s). Sound (Snd3D_PlayScaledVolume, flt_1498C3C/1498DBC/1498CFC)
// OUT OF SCOPE audio, same convention as the rest of this file (no audio subsystem wired in
// WorldEntityDispatch*.cpp to date, cf. Net/GameVarDispatch.cpp for the no-op
// Snd3D_PlayScaledVolume stub used elsewhere in ClientSource).
//
// IMPORTANT (verified against Game/GameState.h::ActiveBuff and UI/BuffStatusPanel.h):
// dword_16860D0[idx] is NOT a data source for `PlayerEntity::buffs`/`UI::BuffIconId`.
// Skill_GetBuffMotionId(idx) returns an ANIMATION MOTION id (241-330, cf.
// Game/SkillCombat.cpp) — a "cast in progress" state for the spell-announcement bar (like
// dword_1685EAC/1685F14/1685F44/16862F0/16860C0 for the combo/Special families above), NOT a
// catalog of active buffs. `BuffIconId` (UI/BuffStatusPanel.h) is a totally disjoint 0..33
// catalog, fed by ~50 unrelated systems (elemental combos, pair synergy, guild rank, weapon
// gem, dword_16758D8-duration debuffs, etc. -- cf. GameState.h::ActiveBuff header comment).
// Mapping dword_16860D0[idx] -> ActiveBuff{id=idx} would be a FABRICATION (mapping not
// confirmed by the disassembly) -- not done here. The real source of PlayerEntity::buffs
// remains to be reversed case by case (cf. the TODO in GameState.h).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kBuffState = 0x16860D0;
} // namespace

void ApplyBuffFamilySlot(int slot, uint32_t idx, const char* tag13) {
    const int buffMotionId = Skill_GetBuffMotionId(static_cast<int>(idx));

    switch (slot) {
    case 1: // 411 -- state=1, unconditional message.
        g_Client.Var(kBuffState + 4 * idx) = 1;
        g_Client.msg.System(SkillName(buffMotionId) + " [" + tag13 + "] " + Str(1244));
        break;
    case 2: // 412
    case 3: // 413 -- identical body to 412 (verified in the disasm, cf. header comment).
        g_Client.Var(kBuffState + 4 * idx) = 2;
        g_Client.msg.System(SkillName(buffMotionId) + " [" + tag13 + "] " + Str(1245));
        break;
    case 4: // 414 -- state=2, distinct suffix from 412/413.
        g_Client.Var(kBuffState + 4 * idx) = 2;
        g_Client.msg.System(SkillName(buffMotionId) + " [" + tag13 + "] " + Str(1246));
        break;
    case 5: // 415 -- state=0 (reset), written unconditionally. Gate=morph (CALCULABLE)
            // -> message + return to faction town (same shape as the combo/Special slot8).
        g_Client.Var(kBuffState + 4 * idx) = 0;
        if (buffMotionId == g_Client.VarGet(kSelfMorphNpcId)) {
            g_Client.msg.System(SkillName(buffMotionId) + " " + Str(237));
            WarpToOwnFactionTown();
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Sub-opcodes 416/417 (EA 0x49d565/0x49d58d) -- UNRELATED to the "Buff" family above
// (DIFFERENT table, dword_1686120, no Skill_GetBuffMotionId, no message). Adjacent in the
// disassembly and trivial -> wired in the same pass: 416 sets dword_1686120[idx]=1, 417
// resets it to 0 (idx = payload+4, same convention). Exact role of dword_1686120 not
// determined in this pass (candidate: a generic "exclusion" flag similar to dword_1686014
// seen at sub-opcode 31/32, not confirmed).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kMiscFlag1686120 = 0x1686120;
} // namespace

void ApplyMiscFlagSlot(uint32_t idx, int32_t value) {
    g_Client.Var(kMiscFlag1686120 + 4 * idx) = value;
}

// ---------------------------------------------------------------------------
// Sub-opcodes 418..429 (dump L.3215-3364, mission "end of the Buff block" 2026-07-14) — NOT
// the table-driven Buff/combo/Special mechanic (no Skill_Get*MotionId call here, confirmed by
// full disassembly reading): 6 independent notifications + 1 shared tail-merge (423/429,
// disasm LABEL_135) + 4 sub-cases gated on an unavailable buffer (425..428):
//   418  WE_PlaySound_SysLine_418 (EA 0x49d5b5, confirmed by direct disasm): count
//        (payload+4) -> "[count]str1402" if >0 else "str1403" alone. Sound
//        (flt_149947C/flt_14993BC) OUT OF SCOPE audio (same convention as the rest of this
//        file). No state written, no gate.
//   419  class 0..3 (payload+4) + 13-byte TAG at payload+8 (NOT +12 like this file's standard
//        tag13 -- same offset as ApplyClassTagFamily 671..677) -> "[class] [tag]str1444". No
//        state/gate.
//   420  count (payload+4) -> "[count]str1475", floating HUD (cat.3/type1) + chat.
//   421  arms dword_1686134=1 -- CONFIRMED to be WorldMap::flagZ291Variant (World/
//        WorldMap.h, no global WorldMap instance exposed to network handlers here, same
//        limit as SkillLevelTable); message str1476 (HUD cat.3/type2 + chat) unconditional;
//        if morph==291 (CALCULABLE): arms dword_1675CD0/flt_1675CD4 -- CONFIRMED to be row
//        28 of Game/AnimationTick.cpp::kMorphRows (already consumed by the existing generic
//        timer engine) + sound (flt_1499EFC, out of scope) + 2nd system line str1477.
//   422/424  suffix str1478 vs str1480 differ (NOT a strictly "identical body", see the
//        re-verification below): dword_1686134=0 written UNCONDITIONALLY (precedes the gate,
//        same convention as the rest of this file). Message "[name] suffix" + conditional
//        warp both depend on byte_1686138 -- same unavailable-buffer limit as leaderName
//        (629..652 in WorldEntityDispatch_War.cpp).
//   425..428  ENTIRELY gated by `!Crt_Strcmp(byte_1686138, dword_16746A8)`, with NO
//        unconditional state write ahead of it -> NOTHING calculable -- NOT wired (same
//        status as 600/764-770).
//        RE-VERIFIED (mission "425-428 + 500/901-903", 2026-07-14): xrefs_to idaTs2 across
//        the WHOLE binary for byte_1686138 (20 xrefs) and byte_1686145 (6 xrefs, adjacent
//        twin 13-byte buffer, used by the same gates elsewhere -- UI_ClanWarp_Commit/
//        UI_ClanDisband_Commit): 25 of 26 sites are READS; the one remaining site is case 422
//        itself (EA 0x496E37, `Crt_StringInit(&byte_1686138, &byte_1686145)`), which WRITES
//        byte_1686138 -- but by copying from byte_1686145, which is ITSELF never fed real
//        content anywhere in the image (confirmed exhaustively). So byte_1686138 is a
//        deferred copy of a buffer that is perpetually empty: the gate is INSURMOUNTABLE
//        statically -- not for lack of a write site, but because that write site never
//        propagates real content.
//        RE-VERIFIED AGAIN (mission "unblock 425-428", 2026-07-14): the exact mechanism is
//        now nailed down. `Crt_StringInit` (0x75CAB0, alternate entry into `Crt_Strcat`
//        0x75CB00's body at 0x75CB25, skipping the end-of-dest scan 0x75CAE0-0x75CB22) has
//        signature `char* Crt_StringInit(char* dest, const char* src)` -- confirmed by disasm
//        (dest = 1st arg, src = 2nd) -- it is a strcpy, not a "std::string constructor"
//        despite the IDA-hooked name. Case 422 (EA 0x496E37) copies byte_1686145 INTO
//        byte_1686138, then clears byte_1686145 to "" (String @0x7EC95F, confirmed empty) via
//        a 2nd Crt_StringInit (EA 0x496E4E). Case 424 (EA 0x496F5A) does NOT do this copy: it
//        clears byte_1686145 directly (0x496F69) WITHOUT first copying it into
//        byte_1686138 -- a real 422 vs 424 asymmetry (the "identical body" note above refers
//        only to the message suffix, not this copy step). byte_1686145 itself is NEVER
//        written with real content anywhere in the binary (confirmed exhaustively: read-only
//        as the copy source in 422, cleared by 422/424, and read in Char_TickDeathRespawn/
//        UI_ClanWarp_Commit/UI_FactionInfoWnd_Render). No equivalent substitute source exists
//        client-side either: Game/GameState.h::AllianceRoster (g_LocalGuildName 0x168740C,
//        g_AllianceRosterNames 0x16749B8) is a COMPLETELY DISTINCT memory block (no cross-xref
//        with byte_1686138/byte_1686145). CONCLUSION (now established with certainty, not
//        just absence of proof): the original binary never feeds real content into this
//        buffer -- either an incomplete EU-side feature or dead code. 425..428 remain NOT
//        WIRED; an honest TODO, not an analysis gap.
//   423/429  (dump L.3296/3355, TAIL-MERGE LABEL_135 -- strictly identical body, verified
//        instruction by instruction): count=payload+4. If g_SelfMorphNpcId==291
//        (CALCULABLE) -> message "[count]str1479" (chat only, no floating HUD). No state.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kZone291Variant    = 0x1686134; // WorldMap::flagZ291Variant (World/WorldMap.h)
constexpr uint32_t kZone291TimerFlag  = 0x1675CD0; // row 28 of Game/AnimationTick.cpp::kMorphRows
constexpr uint32_t kZone291TimerFrame = 0x1675CD4;
constexpr int32_t  kZone291Morph      = 291;
} // namespace

void Apply418(int32_t count) {
    if (count <= 0) {
        g_Client.msg.System(Str(1403));
    } else {
        g_Client.msg.System("[" + std::to_string(count) + "]" + Str(1402));
    }
}

void Apply419(const uint8_t* payload, uint32_t len) {
    if (len < 21) return; // payload+8..+20 (13 o of tag) must be available
    uint32_t classBranch = 0;
    std::memcpy(&classBranch, payload + 4, 4);
    char tag[14] = {};
    std::memcpy(tag, payload + 8, 13);
    const std::string s = "[" + Str(75 + static_cast<int>(classBranch & 3)) + "] [" + tag + "]" + Str(1444);
    g_Client.msg.System(s);
}

void Apply420(int32_t count) {
    const std::string s = "[" + std::to_string(count) + "]" + Str(1475);
    g_Client.msg.Floating(3, 1, s);
    g_Client.msg.System(s);
}

void Apply421() {
    g_Client.Var(kZone291Variant) = 1;
    const std::string s = Str(1476);
    g_Client.msg.Floating(3, 2, s);
    g_Client.msg.System(s);
    if (g_Client.VarGet(kSelfMorphNpcId) == kZone291Morph) {
        g_Client.Var(kZone291TimerFlag)   = 1;
        g_Client.VarF(kZone291TimerFrame) = 0.0f;
        g_Client.msg.System(Str(1477));
    }
}

// 422/424 -- cf. header comment above (buffer byte_1686138 not available here).
void Apply422Or424() {
    g_Client.Var(kZone291Variant) = 0;
}

// 423/429 -- LABEL_135 tail-merge from the disasm (identical body).
void ApplyZone291CountNotice(int32_t count) {
    if (g_Client.VarGet(kSelfMorphNpcId) != kZone291Morph) return;
    g_Client.msg.System("[" + std::to_string(count) + "]" + Str(1479));
}

} // namespace wed_detail
} // namespace ts2::game

// Net/WorldEntityDispatch_War.cpp — split of Net_OnWorldEntityDispatch (opcode 0x5e, EA
// 0x494870): the "rest of the function" tail — level-bonus/channel family (601..610),
// standalone notices 620..628, war-stage/siege-declaration family (629..652), arena family
// (659..669), class+tag announce family (671..677), 2nd siege-by-element family (700..729),
// rank family/table (740..753), verb+class+tag announce (754), cast-announce family
// (771..774), simple skill notices (788..795), "war 324"/"war 342" event families
// (780..786/800..807), and the final banners (901..903). See Net/WorldEntityDispatch.h for
// the full sub-opcode map.
//
// MISSION "rest of the function" (2026-07-14) — sub-opcodes 500..903, end of the dispatcher.
// Source of truth: RE/dispatch_494870_full.c (full Hex-Rays dump of
// Net_OnWorldEntityDispatch, generated via ida_hexrays.decompile + file write — needed
// because the pseudocode exceeds what a single MCP decompile call can return; kept in RE/ for
// traceability, the line numbers cited below refer to THIS file). Precise EAs are not
// re-cited per sub-case in this section (map_pseudocode_line_to_eas proved unstable/slow on a
// function this size) — the reliable locator is the dump line number, cf. each block's
// comment.
//
// Sub-opcode 600 stays NOT wired (TODO): dump L.3525-3538 only re-arms Crt_StringInit() based
// on a v644 range, with no modelable observable effect (no message/state written) — confirmed
// a harmless no-op.
//
// [Historical note carried over from the original file: an earlier pass in this same mission
// also listed sub-opcode 500 (dump L.3365-3524, a faction/alliance lookup table + ~80
// hardcoded morph ids) as refused for wiring — structurally ambiguous statically (raw payload
// reads v637/v638/v639 into BUFFERS at unconfirmed offsets, and an apparent overlap between
// g_PerElementCounter/dword_1685E48 and g_AlliancePairTable/kElementPairAAddr..D that
// Hex-Rays' local typing could not disambiguate without an x32dbg runtime check). This
// concern was superseded by the later mission "wire ElementPairTable": Apply500
// (WorldEntityDispatch_Element.cpp) now wires the observable part of case 500 directly
// against g_PerElementCounter/dword_1685E48/kElementPairAAddr..D.]
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

// HUD floating (category 1, type 0) + system line — the same call pair reproduced IDENTICALLY
// 22 times, spread across 3 unrelated functions in this file (ApplyWarStageFamily x10,
// ApplySiegeStage2Family x4, ApplyRankFamily740 x8) that otherwise share no code
// (states/addresses/EAs differ) — only this announcement idiom is common to the three.
inline void AnnounceFloating10(const std::string& s) {
    g_Client.msg.Floating(1, 0, s);
    g_Client.msg.System(s);
}

// HUD floating (category 2, type 4) + system line — the same call pair reproduced IDENTICALLY
// 7 times, spread across 3 unrelated functions (ApplyCastAnnounce771to774 x2,
// ApplyWarEvent780to786 x4, ApplyWarEvent800to807 x1) — same factoring rationale as
// AnnounceFloating10 above (audit "dispatch 0x5e" 2026-07-14).
inline void AnnounceFloating24(const std::string& s) {
    g_Client.msg.Floating(2, 4, s);
    g_Client.msg.System(s);
}

template <typename... Args>
inline std::string FormatString(const std::string& fmt, Args... args) {
    char buf[1024];
    std::snprintf(buf, sizeof(buf), fmt.c_str(), args...);
    return std::string(buf);
}

} // namespace

// ---------------------------------------------------------------------------
// "Level bonus / channel" family (dump L.3539-3799, sub-opcodes 601..610, state
// dword_1686058[channel] with channel in {0,1,2}). Channel gate COMMON to 601/602/609/610:
// channel 0 -> g_SelfLevel in [100,112]; channel 1 -> g_SelfLevel in [113,145] AND
// !g_SelfLevelBonus; channel 2 -> g_SelfLevelBonus>0. The channel's target morph =
// channel+297 (dword_1675CD8 in 601, compared directly elsewhere). Precise TODOs omitted
// (same conventions as the rest of this file): popup Skill_CheckBuffState/
// g_Opt_FilterWorldEntity (601), Game_GetTierValue (608), UI_Macro_ClearGrid (604/605/608),
// Snd3D_PlayScaledVolume (603, sound only).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kBonusChanState  = 0x1686058;  // dword_1686058[channel]
constexpr uint32_t kBonusChanFlag2  = 0x1686064;  // dword_1686064[channel]
constexpr uint32_t kBonusSelMorph   = 0x1675CD8;  // dword_1675CD8 = channel+297
constexpr uint32_t kBonusResetCDC = 0x1675CDC, kBonusResetCE0 = 0x1675CE0,
                    kBonusResetCE4 = 0x1675CE4, kBonusResetCE8 = 0x1675CE8;
constexpr uint32_t kBonusAckArr     = 0x1675CEC;  // [class] (stride 4, 0..3)
constexpr uint32_t kBonusD1C        = 0x1675D1C;
constexpr uint32_t kBonusResetArr   = 0x1675D20;  // [class] (stride 4, 0..3)

bool BonusChannelGatePasses(uint32_t chan) {
    if (chan == 0) return g_World.self.level >= 100 && g_World.self.level <= 112;
    if (chan == 1) return g_World.self.level >= 113 && g_World.self.level <= 145 && !g_World.self.levelBonus;
    if (chan == 2) return g_World.self.levelBonus > 0;
    return false;
}

void ResetBonusBlockA() { // dword_1675CDC/CE0/CE4/CE8 + kBonusAckArr[0..3] = 0
    g_Client.Var(kBonusResetCDC) = 0; g_Client.Var(kBonusResetCE0) = 0;
    g_Client.Var(kBonusResetCE4) = 0; g_Client.Var(kBonusResetCE8) = 0;
    for (int c = 0; c < 4; ++c) g_Client.Var(kBonusAckArr + 4 * c) = 0;
}
} // namespace

void ApplyLevelBonusFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    uint32_t chan = 0, arg2 = 0;
    if (len >= 8)  std::memcpy(&chan, payload + 4, 4);
    if (len >= 12) std::memcpy(&arg2, payload + 8, 4);
    const bool onMorph = (static_cast<int32_t>(chan) + 297 == g_Client.VarGet(kSelfMorphNpcId));

    switch (subOpcode) {
    case 601: // dump L.3559
        g_Client.Var(kBonusChanState + 4 * chan) = 0;
        g_Client.Var(kBonusChanFlag2 + 4 * chan) = 1;
        if (!BonusChannelGatePasses(chan)) return;
        g_Client.Var(kBonusSelMorph) = static_cast<int32_t>(chan) + 297;
        g_Client.msg.System(SkillName(static_cast<int32_t>(chan) + 297) + " " + Str(232));
        // TODO(gate): popup Str(1757) if !g_Opt_FilterWorldEntity/Skill_CheckBuffState -- not modeled here.
        return;
    case 602: // dump L.3572
        g_Client.Var(kBonusChanState + 4 * chan) = 1;
        g_Client.Var(kBonusChanFlag2 + 4 * chan) = 0;
        if (!BonusChannelGatePasses(chan)) return;
        g_Client.msg.System(Str(1838));
        if (onMorph) g_Client.msg.System("[" + std::to_string(arg2) + "]" + Str(1839));
        return;
    case 603: // dump L.3603
        g_Client.Var(kBonusChanState + 4 * chan) = 2;
        if (onMorph) {
            g_Client.msg.System(Str(1764));
            g_Client.Var(kBonusResetCDC) = 1; g_Client.Var(kBonusResetCE0) = 1;
            g_Client.Var(kBonusResetCE4) = 1; g_Client.Var(kBonusResetCE8) = 1;
            g_Client.Var(kBonusD1C) = 1;
            for (int c = 0; c < 4; ++c) g_Client.Var(kBonusResetArr + 4 * c) = 0;
        }
        return;
    case 604: // dump L.3623 -- TODO(UI_Macro_ClearGrid) not modeled (external UI).
        g_Client.Var(kBonusChanState + 4 * chan) = 5;
        if (onMorph) ResetBonusBlockA();
        return;
    case 605: // dump L.3639 -- TODO(UI_Macro_ClearGrid).
        g_Client.Var(kBonusChanState + 4 * chan) = 5;
        if (onMorph) {
            g_Client.msg.System(Str(235));
            WarpToOwnFactionTown();
            ResetBonusBlockA();
        }
        return;
    case 606: { // dump L.3660 -- same "ack or warp" message/logic table as case 666 of
                // ApplyArenaFamily below (audit "dispatch 0x5e" 2026-07-14): NOT factored
                // (target state addresses differ -- kBonusAckArr/kBonusResetArr here vs
                // kArenaBlockB_D40/kArenaD74..D80 there -- each site verified independently
                // against its own EA; merging would blur disasm traceability with no real
                // risk reduction, cf. the audit's guidance).
        g_Client.Var(kBonusChanState + 4 * chan) = 3;
        if (!onMorph) return;
        if (arg2 > 3) return; // v693 outside 0..3 -> default: return (faithful)
        static constexpr int kMsg[4] = {1758, 1759, 1760, 1761};
        g_Client.msg.System(Str(kMsg[arg2]));
        if (static_cast<uint32_t>(g_World.self.element) == arg2)
            g_Client.Var(kBonusAckArr + 4 * arg2) = 1;
        else
            WarpToOwnFactionTown();
        for (int c = 0; c < 4; ++c) g_Client.Var(kBonusResetArr + 4 * c) = 0;
        return;
    }
    case 607: // dump L.3698
        g_Client.Var(kBonusChanState + 4 * chan) = 4;
        if (onMorph) g_Client.Var(kBonusD1C) = 0;
        return;
    case 608: // dump L.3704 -- TODO(UI_Macro_ClearGrid, Game_GetTierValue) not modeled.
        g_Client.Var(kBonusChanState + 4 * chan) = 5;
        if (onMorph) {
            g_Client.msg.System(Str(1844));
            WarpToOwnFactionTown();
            ResetBonusBlockA();
        }
        return;
    case 609: // dump L.3728
        g_Client.Var(kBonusChanState + 4 * chan) = 0;
        if (BonusChannelGatePasses(chan)) {
            static constexpr int kMsg[3] = {1762, 1846, 1847};
            const std::string s = Str(chan <= 2 ? kMsg[chan] : 1762);
            g_Client.msg.System(s);
            g_Client.msg.Floating(0, 0, s);
        }
        if (onMorph) {
            ResetBonusBlockA();
            WarpToOwnFactionTown();
        }
        return;
    case 610: // dump L.3776 -- Str(1843) format-table applied to `arg2` in the binary (the
              // %s in the table is actually masking a %d); approximated as an "[n]" prefix
              // (same convention as other "format-from-table" cases in this pass, cf. 661/668).
        if (BonusChannelGatePasses(chan))
            g_Client.msg.System("[" + std::to_string(arg2) + "]" + Str(1843));
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Standalone notices 620..628 (dump L.3852-3902), mechanically unrelated to each other (just
// adjacent in the disassembly) -- wired individually.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kWarDeclaredClass = 0x1686188; // dword_1686188 (624/626)
constexpr uint32_t kZoneResetArr622  = 0x1675DB8; // [0..3] stride 4 (622)
} // namespace

void Apply620(uint32_t count) { g_Client.msg.System(std::to_string(count) + Str(1937)); }
void Apply621() { g_Client.msg.System(Str(1938)); }
void Apply622() { // dump L.3862 -- TODO(World_LoadCurrentZoneModel) not modeled (3D reload).
    g_Client.msg.System(Str(1936));
    for (int c = 0; c < 4; ++c) g_Client.Var(kZoneResetArr622 + 4 * c) = 0;
}
void Apply624(uint32_t classId) {
    g_Client.Var(kWarDeclaredClass) = static_cast<int32_t>(classId);
    g_Client.msg.System(Str(75 + (classId & 3)) + Str(1929));
}
void Apply625(uint32_t count) { g_Client.msg.System(std::to_string(count) + Str(1935)); }
void Apply626() {
    g_Client.Var(kWarDeclaredClass) = -1;
    if (g_Client.VarGet(kSelfMorphNpcId) == 88) {
        g_Client.msg.System(Str(235));
        WarpToOwnFactionTown();
    }
}
void Apply628(const uint8_t* payload, uint32_t len) {
    if (len < 16) return;
    int32_t comboMotionId = 0, classId = 0;
    std::memcpy(&comboMotionId, payload + 8, 4);
    std::memcpy(&classId, payload + 12, 4);
    const std::string s = SkillName(comboMotionId) + Str(classId + 2019);
    g_Client.msg.Floating(2, 1, s);
    g_Client.msg.System(s);
}

// ---------------------------------------------------------------------------
// "War declaration / stage" family (dump L.3903-4228, sub-opcodes 629..652): idx=element 0..3
// (payload+4), gate elt<4. dword_168618C[elt] = stage (0..14). Each stage writes its state
// unconditionally then, if elt==g_World.self.element ("announcement" stages) or if the
// current morph matches the element's siege town (kSiegeTownNpc[elt] = {138,139,165,166},
// the binary's v682 table, DISTINCT from MapWarp::FactionTownNpcId), shows the message and/or
// arms a simple flag.
//
// TWO 13-byte NAMES are CARRIED by the payload (rectification WARP-01/02, wave W11 — the
// earlier banner "not received in THIS packet / data unavailable" was FACTUALLY WRONG):
// name1 = payload+8, name2 = payload+21 (frame derivation: Crt_Memcpy(&var_F8, recvBuf+5,
// 0x64) @0x4948a5; recvBuf+5 = payload+4; var_F4@+8, var_E7@+21). Two distinct name tables
// (bounded on both sides by named neighbors):
//   T1 = unk_168619C: 4 elt x 52 o = 4 slots of 13 (slot_i = +13*i); upper bound =
//        unk_168626C (0x168619C + 208).
//   T2 = unk_168626C: 4 elt x 13 o; upper bound = dword_16862A0 (0x168626C + 52, EXACT).
// Layout PROVEN instruction by instruction (imul 34h for T1, imul 0Dh for T2, elt<4 gate at
// every case, write BEFORE the onSelfElt test):
//   629 -> T2[elt] <- name1 (0x49e9c5)
//   635 -> T1.slot3[elt] <- name1 (0x49ef36) ; T2[elt] <- name2 (0x49ef52)
//   640 -> T1.slot2[elt] <- name1 (0x49f2a7) ; T1.slot3[elt] <- name2 (0x49f2c3)
//   645 -> T1.slot1[elt] <- name1 (0x49f600) ; T1.slot2[elt] <- name2 (0x49f61b)
//   650 -> T1.slot0[elt] <- name1 (0x49f94e) ; T1.slot1[elt] <- name2 (0x49f96a)
//   652 -> T2[elt] <- "" (clear, 0x49fa98, preceded by Map_BeginWarpToFactionTown @0x49fa6d)
// The (slot_k, slot_k+1) pairs OVERLAP from one stage to the next: the pattern is regular and
// indexed by stage, but it is NOT a monotonic "ladder" -> no invented conceptual name here, we
// just describe the facts (stage -> slot pair).
// READERS (NOT ported -> WARP-04, outside this file): Warp_ProcessKeyword 0x5F54E0 /
//   Warp_LookupDest 0x5F5B60 (strcmp T1.slot3/T2 vs dword_16746A8); 0x49f4ae strcmp
//   T1.slot3[elt] vs g_LocalClanName under the kSiegeTownNpc gate -> Map_BeginWarpToFactionTown.
// TODO anchored (not modeled, out of scope for W11): attack-rating recompute
//   (Char_CalcAttackRatingMin/Max on g_EquipSnapshotScratch) at 635/640/645/650;
//   UI_RemoveActiveBuffSlot() at 650; 2nd message Str(2037)+name at 631/632.
//
// The 13-byte name buffers below use BlobStrcpy13 (Net/WorldEntityDispatch_Internal.h) —
// same faithful strcpy(dest, src) helper as WorldEntityDispatch_BranchSkill.cpp's duel-branch
// affiliation writes; 0x16746A8/0x16746BC are BranchSkill's concern, T1/T2 here are a
// SEPARATE pair of long-tail name-table globals, both opened at size 13 via Blob() (which
// freezes its size at the first caller, ClientRuntime.h:179).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kWarStage        = 0x168618C; // dword_168618C[elt]
constexpr uint32_t kWarT1           = 0x168619C; // unk_168619C : 4 elt x 52 o, slot_i = +13*i
constexpr uint32_t kWarT2           = 0x168626C; // unk_168626C : 4 elt x 13 o (bound dword_16862A0)
constexpr int32_t  kSiegeTownNpc[4] = {138, 139, 165, 166}; // v682
constexpr uint32_t kWarFlag1675DD8  = 0x1675DD8; // 634
constexpr uint32_t kWarFlag1675DDC  = 0x1675DDC; // 639
constexpr uint32_t kWarFlag1675DE0  = 0x1675DE0; // 644
constexpr uint32_t kWarFlag1675DE4  = 0x1675DE4; // 649
constexpr uint32_t kWarFloatReset   = 0x1675DE8; // [0..3] stride 4 (652)
} // namespace

void ApplyWarStageFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    uint32_t elt = 0, arg2 = 0;
    if (len >= 8)  std::memcpy(&elt,  payload + 4, 4);
    if (len >= 12) std::memcpy(&arg2, payload + 8, 4);
    if (elt >= 4) return;
    const bool onSiegeMorph = (g_Client.VarGet(kSelfMorphNpcId) == kSiegeTownNpc[elt]);
    const bool onSelfElt    = (static_cast<int32_t>(elt) == g_World.self.element);
    // The two 13-byte names carried by cases 629/635/640/645/650 (arg2 OVERLAPS name1 at
    // payload+8, exactly like the binary: the "announcement" cases 630/631/... read the same
    // offset as an integer). Defensive NUL-bound (real packet = 105 o -> both present).
    char name1[14] = {}, name2[14] = {};
    if (len >= 21) std::memcpy(name1, payload + 8,  13);
    if (len >= 34) std::memcpy(name2, payload + 21, 13);
    // Key of a T1 slot (slot 0..3 of the current element) / of T2.
    const uint32_t t1slot0 = kWarT1 + 52u * elt;
    const uint32_t t2key   = kWarT2 + 13u * elt;

    switch (subOpcode) {
    case 629: { // dump L.3903 -- T2[elt] <- name1 (0x49e9c5), THEN message if onSelfElt.
        BlobStrcpy13(t2key, name1);                    // 0x49e9c5 (written BEFORE the onSelfElt test)
        if (onSelfElt) g_Client.msg.System(std::string(name1) + Str(1986));
        return;
    }
    case 630:
        g_Client.Var(kWarStage + 4 * elt) = 1;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(1984);
            AnnounceFloating10(s);
        }
        if (onSiegeMorph) WarpToOwnFactionTown();
        return;
    case 631: // TODO(leaderName): 2nd message Str(2037)+name omitted (buffer not received here).
        g_Client.Var(kWarStage + 4 * elt) = 2;
        if (onSelfElt) {
            g_Client.msg.System(Str(1985));
            const std::string s = std::to_string(arg2) + Str(1988);
            AnnounceFloating10(s);
        }
        return;
    case 632: // TODO(leaderName) same as 631.
        g_Client.Var(kWarStage + 4 * elt) = 2;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(1988);
            AnnounceFloating10(s);
        }
        return;
    case 633:
        g_Client.Var(kWarStage + 4 * elt) = 3;
        if (onSelfElt) g_Client.msg.System(Str(1987));
        return;
    case 634:
        g_Client.Var(kWarStage + 4 * elt) = 4;
        if (onSelfElt) {
            const std::string s = Str(1989);
            AnnounceFloating10(s);
            if (onSiegeMorph) g_Client.Var(kWarFlag1675DD8) = 1;
        }
        return;
    case 635: // TODO(Char_CalcAttackRatingMin/Max) not modeled -- names + stage.
        BlobStrcpy13(t1slot0 + 13u * 3, name1);        // 0x49ef36 : T1.slot3 <- name1
        BlobStrcpy13(t2key,             name2);        // 0x49ef52 : T2[elt] <- name2
        g_Client.Var(kWarStage + 4 * elt) = 5;
        return;
    case 636:
        g_Client.Var(kWarStage + 4 * elt) = 5;
        return;
    case 637: case 638: // TODO(leaderName + guild-strcmp-conditioned warp) omitted.
        g_Client.Var(kWarStage + 4 * elt) = 6;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(1990);
            AnnounceFloating10(s);
        }
        return;
    case 639:
        g_Client.Var(kWarStage + 4 * elt) = 7;
        if (onSelfElt) {
            const std::string s = Str(1991);
            AnnounceFloating10(s);
            if (onSiegeMorph) g_Client.Var(kWarFlag1675DDC) = 1;
        }
        return;
    case 640: // TODO(Char_CalcAttackRatingMin) -- names + stage.
        BlobStrcpy13(t1slot0 + 13u * 2, name1);        // 0x49f2a7 : T1.slot2 <- name1
        BlobStrcpy13(t1slot0 + 13u * 3, name2);        // 0x49f2c3 : T1.slot3 <- name2
        g_Client.Var(kWarStage + 4 * elt) = 8;
        return;
    case 641:
        g_Client.Var(kWarStage + 4 * elt) = 8;
        return;
    case 642: case 643: // TODO(leaderName + conditioned warp) omitted.
        g_Client.Var(kWarStage + 4 * elt) = 9;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(1992);
            AnnounceFloating10(s);
        }
        return;
    case 644:
        g_Client.Var(kWarStage + 4 * elt) = 10;
        if (onSelfElt) {
            const std::string s = Str(1993);
            AnnounceFloating10(s);
            if (onSiegeMorph) g_Client.Var(kWarFlag1675DE0) = 1;
        }
        return;
    case 645: // TODO(Char_CalcAttackRatingMin) -- names + stage.
        BlobStrcpy13(t1slot0 + 13u * 1, name1);        // 0x49f600 : T1.slot1 <- name1
        BlobStrcpy13(t1slot0 + 13u * 2, name2);        // 0x49f61b : T1.slot2 <- name2
        g_Client.Var(kWarStage + 4 * elt) = 11;
        return;
    case 646:
        g_Client.Var(kWarStage + 4 * elt) = 11;
        return;
    case 647: case 648: // TODO(leaderName + conditioned warp) omitted.
        g_Client.Var(kWarStage + 4 * elt) = 12;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(1994);
            AnnounceFloating10(s);
        }
        return;
    case 649:
        g_Client.Var(kWarStage + 4 * elt) = 13;
        if (onSelfElt) {
            const std::string s = Str(1995);
            AnnounceFloating10(s);
            if (onSiegeMorph) g_Client.Var(kWarFlag1675DE4) = 1;
        }
        return;
    case 650: // TODO(UI_RemoveActiveBuffSlot) -- names + stage.
        BlobStrcpy13(t1slot0 + 13u * 0, name1);        // 0x49f94e : T1.slot0 <- name1
        BlobStrcpy13(t1slot0 + 13u * 1, name2);        // 0x49f96a : T1.slot1 <- name2
        g_Client.Var(kWarStage + 4 * elt) = 14;
        return;
    case 651:
        g_Client.Var(kWarStage + 4 * elt) = 14;
        return;
    case 652:
        if (onSiegeMorph) {
            for (int c = 0; c < 4; ++c) g_Client.VarF(kWarFloatReset + 4 * c) = 0.0f;
            WarpToOwnFactionTown();                     // Map_BeginWarpToFactionTown @0x49fa6d
        }
        g_Client.Var(kWarStage + 4 * elt) = 0;         // dword_168618C[elt]=0 @0x49fa78
        BlobStrcpy13(t2key, "");                        // 0x49fa98 : T2[elt] <- "" (clear)
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// "Arena" family (fixed morph 200, dump L.4229-4380, sub-opcodes 659..669), state
// dword_16862A0 (NO index -- a single global, not per-element). Gate morph =
// g_SelfMorphNpcId==200 (CALCULABLE) for every sub-case except 659/661/662 (state only,
// unconditional message).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kArenaState = 0x16862A0;
constexpr int32_t  kArenaMorph = 200;
constexpr uint32_t kArenaBlockB_D30 = 0x1675D30, kArenaBlockB_D34 = 0x1675D34,
                    kArenaBlockB_D38 = 0x1675D38, kArenaBlockB_D3C = 0x1675D3C;
constexpr uint32_t kArenaBlockB_D40 = 0x1675D40; // [class] stride4 0..3
constexpr uint32_t kArenaD70 = 0x1675D70;
constexpr uint32_t kArenaD74 = 0x1675D74, kArenaD78 = 0x1675D78,
                    kArenaD7C = 0x1675D7C, kArenaD80 = 0x1675D80;

// Reset of block B (dword_1675D30/34/38/3C + kArenaBlockB_D40[0..3]) -- IDENTICAL BODY
// reproduced twice in the disassembly (cases 664/665 AND 669, cf. audit "dispatch 0x5e"
// 2026-07-14) -- factored here, NO behavior change.
void ResetArenaBlockB() {
    g_Client.Var(kArenaBlockB_D30) = 0; g_Client.Var(kArenaBlockB_D34) = 0;
    g_Client.Var(kArenaBlockB_D38) = 0; g_Client.Var(kArenaBlockB_D3C) = 0;
    for (int c = 0; c < 4; ++c) g_Client.Var(kArenaBlockB_D40 + 4 * c) = 0;
}
} // namespace

void ApplyArenaFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    uint32_t v = 0;
    if (len >= 8) std::memcpy(&v, payload + 4, 4);
    const bool onMorph = (g_Client.VarGet(kSelfMorphNpcId) == kArenaMorph);

    switch (subOpcode) {
    case 659:
        g_Client.Var(kArenaState) = 1;
        g_Client.msg.System(std::to_string(v) + Str(1939));
        return;
    case 660:
        g_Client.Var(kArenaState) = 2;
        g_Client.msg.System(Str(1940));
        // TODO(gate): popup Str(2060/2061) if g_SelfLevelBonus==12 && g_Opt_FilterWorldEntity -- omitted.
        return;
    case 661: // format-table Str(1941) applied to v676 -- approximated as an "[n]" prefix (cf. 610).
        g_Client.Var(kArenaState) = 2;
        g_Client.msg.System("[" + std::to_string(v) + "]" + Str(1941));
        return;
    case 662:
        g_Client.Var(kArenaState) = 3;
        g_Client.msg.System(Str(1942));
        return;
    case 663:
        g_Client.Var(kArenaState) = 4;
        if (onMorph) {
            g_Client.msg.System(Str(1764));
            g_Client.Var(kArenaBlockB_D30) = 1; g_Client.Var(kArenaBlockB_D34) = 1;
            g_Client.Var(kArenaBlockB_D38) = 1; g_Client.Var(kArenaBlockB_D3C) = 1;
            g_Client.Var(kArenaD70) = 1;
            g_Client.Var(kArenaD74) = 0; g_Client.Var(kArenaD78) = 0;
            g_Client.Var(kArenaD7C) = 0; g_Client.Var(kArenaD80) = 0;
        }
        return;
    case 664:
    case 665:
        g_Client.Var(kArenaState) = 7;
        if (onMorph) {
            ResetArenaBlockB();
            if (subOpcode == 665) WarpToOwnFactionTown();
        }
        return;
    case 666: { // same "ack or warp" message/logic table as case 606 of ApplyLevelBonusFamily
                // above (audit "dispatch 0x5e" 2026-07-14) -- NOT factored, cf. the case 606
                // header comment for the reason (different state addresses).
        g_Client.Var(kArenaState) = 5;
        if (!onMorph || v > 3) return;
        static constexpr int kMsg[4] = {1758, 1759, 1760, 1761};
        g_Client.msg.System(Str(kMsg[v]));
        if (static_cast<uint32_t>(g_World.self.element) == v)
            g_Client.Var(kArenaBlockB_D40 + 4 * v) = 1;
        else
            WarpToOwnFactionTown();
        g_Client.Var(kArenaD74) = 0; g_Client.Var(kArenaD78) = 0;
        g_Client.Var(kArenaD7C) = 0; g_Client.Var(kArenaD80) = 0;
        return;
    }
    case 667:
        g_Client.Var(kArenaState) = 6;
        if (onMorph) g_Client.Var(kArenaD70) = 0;
        return;
    case 668:
        g_Client.Var(kArenaState) = 7;
        if (onMorph) {
            g_Client.msg.System(Str(1844));
            g_Client.msg.System("[100]" + Str(1845)); // value 100 hardcoded in the binary.
        }
        return;
    case 669:
        g_Client.Var(kArenaState) = 0;
        if (onMorph) {
            ResetArenaBlockB();
            WarpToOwnFactionTown();
        }
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// "Class+tag announce" family (dump L.4380-4480, sub-opcodes 671..677): class 0..3
// (payload+4) + 13-byte text tag (payload+8, NOT payload+12 -- an offset specific to this
// family) -> message "[(class)] tag suffix", HUD floating + chat. Purely notifications, no
// state written.
// ---------------------------------------------------------------------------
void ApplyClassTagFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    if (len < 21) return;
    uint32_t classId = 0; std::memcpy(&classId, payload + 4, 4);
    char tag[14] = {}; std::memcpy(tag, payload + 8, 13);
    const std::string classLabel = Str(75 + static_cast<int>(classId & 3));

    int strId = 0;
    if (subOpcode == 673) {
        uint32_t type = 0; if (len >= 25) std::memcpy(&type, payload + 8, 4); // NB: tag stays at +8 for 673? see below
        // 673 (dump L.4398): v691=class@4, v701=type@8 (21..25), tag@12 (NOT @8) -- re-read
        // the tag at the correct offset for this specific sub-case.
        char tag12[14] = {};
        if (len >= 25) std::memcpy(tag12, payload + 12, 13);
        static constexpr int kMsg[5] = {2210, 2211, 2216, 2217, 2218};
        if (type < 21 || type > 25) return;
        const std::string s = "[(" + classLabel + ")] " + std::string(tag12) + " " + Str(kMsg[type - 21]);
        g_Client.msg.Floating(2, 1, s); g_Client.msg.System(s);
        return;
    }
    if (subOpcode == 676) {
        uint32_t sel = 0; if (len >= 25) std::memcpy(&sel, payload + 8, 4);
        char tag12[14] = {}; if (len >= 25) std::memcpy(tag12, payload + 12, 13);
        if (sel != 4 && sel != 5) return; // otherwise an uninitialized buffer binary-side -> no-op here.
        strId = (sel == 4) ? 2221 : 2222;
        const std::string s = "[(" + classLabel + ")] " + std::string(tag12) + " " + Str(strId);
        g_Client.msg.Floating(2, 1, s); g_Client.msg.System(s);
        return;
    }
    switch (subOpcode) {
    case 671: strId = 2084; break;
    case 672: strId = 2208; break;
    case 674: strId = 2219; break;
    case 675: strId = 2220; break;
    case 677: strId = 2230; break;
    default: return;
    }
    if (subOpcode == 671) {
        const std::string s = classLabel + " " + std::string(tag) + " " + Str(strId);
        g_Client.msg.Floating(2, 1, s); g_Client.msg.System(s);
    } else {
        const std::string s = "[(" + classLabel + ")] " + std::string(tag) + " " + Str(strId);
        g_Client.msg.Floating(2, 1, s); g_Client.msg.System(s);
    }
}

// ---------------------------------------------------------------------------
// 2nd "siege by element" family (dump L.4481-4847, sub-opcodes 700..729), state
// dword_16862A4[elt]. 700/701/702 compare against g_World.self.element (like 611..615);
// 703+ compare against the morph via a 2nd siege-town table kSiegeTownNpc2 = {5,10,15,123}
// (the binary's v672, DISTINCT from kSiegeTownNpc).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kSiege2State        = 0x16862A4;
constexpr int32_t  kSiegeTownNpc2[4]   = {5, 10, 15, 123};
constexpr uint32_t kSiege2Pair703_A = 0x1675DF8, kSiege2Pair703_B = 0x1675DFC;
constexpr uint32_t kSiege2Pair704_A = 0x1675E20, kSiege2Pair704_B = 0x1675E24;
constexpr uint32_t kSiege2Pair710_A = 0x1675E00, kSiege2Pair710_B = 0x1675E04;
constexpr uint32_t kSiege2Pair711_A = 0x1675E28, kSiege2Pair711_B = 0x1675E2C;
constexpr uint32_t kSiege2Pair714_A = 0x1675E08, kSiege2Pair714_B = 0x1675E0C;
constexpr uint32_t kSiege2Pair715_A = 0x1675E30, kSiege2Pair715_B = 0x1675E34;
constexpr uint32_t kSiege2Pair718_A = 0x1675E10, kSiege2Pair718_B = 0x1675E14;
constexpr uint32_t kSiege2Pair719_A = 0x1675E38, kSiege2Pair719_B = 0x1675E3C;
constexpr uint32_t kSiege2Pair724_A = 0x1675E18, kSiege2Pair724_B = 0x1675E1C;
constexpr uint32_t kSiege2Pair725_A = 0x1675E40, kSiege2Pair725_B = 0x1675E44;

void SetSiege2Pair(uint32_t a, uint32_t b) { g_Client.Var(a) = 1; g_Client.Var(b) = 1; }
} // namespace

void ApplySiegeStage2Family(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    uint32_t elt = 0, arg2 = 0;
    if (len >= 8)  std::memcpy(&elt,  payload + 4, 4);
    if (len >= 12) std::memcpy(&arg2, payload + 8, 4);
    if (elt >= 4) return;
    const bool onMorph   = (g_Client.VarGet(kSelfMorphNpcId) == kSiegeTownNpc2[elt]);
    const bool onSelfElt = (static_cast<int32_t>(elt) == g_World.self.element);

    auto twoLineMsg = [&](int primaryStr, int subtitleStr) {
        const std::string s = Str(primaryStr);
        g_Client.msg.Floating(0xC, 0, s);
        g_Client.msg.System(s);
        g_Client.msg.System(Str(subtitleStr));
    };
    auto countFloatOnly = [&](int literalCount, int strId) {
        g_Client.msg.Floating(0xC, 0, std::to_string(literalCount) + Str(strId));
    };

    switch (subOpcode) {
    case 700:
        g_Client.Var(kSiege2State + 4 * elt) = 0;
        if (onSelfElt) {
            const std::string s = std::to_string(arg2) + Str(2064);
            AnnounceFloating10(s);
        }
        return;
    case 701:
        g_Client.Var(kSiege2State + 4 * elt) = 1;
        if (onSelfElt) { const std::string s = Str(2065); AnnounceFloating10(s); }
        return;
    case 702:
        g_Client.Var(kSiege2State + 4 * elt) = 2;
        if (onSelfElt) { const std::string s = Str(2071); AnnounceFloating10(s); }
        return;
    case 703: g_Client.Var(kSiege2State + 4 * elt) = 3;  if (onMorph) SetSiege2Pair(kSiege2Pair703_A, kSiege2Pair703_B); return;
    case 704: g_Client.Var(kSiege2State + 4 * elt) = 4;  if (onMorph) SetSiege2Pair(kSiege2Pair704_A, kSiege2Pair704_B); return;
    case 705: g_Client.Var(kSiege2State + 4 * elt) = 5;  if (onMorph) twoLineMsg(2134, 2146); return;
    case 706: g_Client.Var(kSiege2State + 4 * elt) = 30; return;
    case 707:
        g_Client.Var(kSiege2State + 4 * elt) = 26;
        if (onMorph) {
            const std::string s = std::to_string(arg2) + Str(2076);
            AnnounceFloating10(s);
        }
        return;
    case 708: g_Client.Var(kSiege2State + 4 * elt) = 6; return;
    case 709: g_Client.Var(kSiege2State + 4 * elt) = 7;  if (onMorph) countFloatOnly(1, 2075); return;
    case 710: g_Client.Var(kSiege2State + 4 * elt) = 8;  if (onMorph) SetSiege2Pair(kSiege2Pair710_A, kSiege2Pair710_B); return;
    case 711: g_Client.Var(kSiege2State + 4 * elt) = 9;  if (onMorph) SetSiege2Pair(kSiege2Pair711_A, kSiege2Pair711_B); return;
    case 712: g_Client.Var(kSiege2State + 4 * elt) = 11; if (onMorph) twoLineMsg(2135, 2147); return;
    case 713: g_Client.Var(kSiege2State + 4 * elt) = 12; if (onMorph) countFloatOnly(2, 2075); return;
    case 714: g_Client.Var(kSiege2State + 4 * elt) = 13; if (onMorph) SetSiege2Pair(kSiege2Pair714_A, kSiege2Pair714_B); return;
    case 715: g_Client.Var(kSiege2State + 4 * elt) = 14; if (onMorph) SetSiege2Pair(kSiege2Pair715_A, kSiege2Pair715_B); return;
    case 716: g_Client.Var(kSiege2State + 4 * elt) = 15; if (onMorph) twoLineMsg(2136, 2148); return;
    case 717: g_Client.Var(kSiege2State + 4 * elt) = 16; if (onMorph) countFloatOnly(3, 2075); return;
    case 718: g_Client.Var(kSiege2State + 4 * elt) = 17; if (onMorph) SetSiege2Pair(kSiege2Pair718_A, kSiege2Pair718_B); return;
    case 719: g_Client.Var(kSiege2State + 4 * elt) = 18; if (onMorph) SetSiege2Pair(kSiege2Pair719_A, kSiege2Pair719_B); return;
    case 720: g_Client.Var(kSiege2State + 4 * elt) = 19; if (onMorph) twoLineMsg(2137, 2149); return;
    case 721: g_Client.Var(kSiege2State + 4 * elt) = 20; return;
    case 722: g_Client.Var(kSiege2State + 4 * elt) = 19; return;
    case 723: g_Client.Var(kSiege2State + 4 * elt) = 21; if (onMorph) countFloatOnly(4, 2075); return;
    case 724: g_Client.Var(kSiege2State + 4 * elt) = 22; if (onMorph) SetSiege2Pair(kSiege2Pair724_A, kSiege2Pair724_B); return;
    case 725: g_Client.Var(kSiege2State + 4 * elt) = 23; if (onMorph) SetSiege2Pair(kSiege2Pair725_A, kSiege2Pair725_B); return;
    case 726: g_Client.Var(kSiege2State + 4 * elt) = 24; if (onMorph) twoLineMsg(2138, 2150); return;
    case 727: g_Client.Var(kSiege2State + 4 * elt) = 25; return;
    case 728: g_Client.Var(kSiege2State + 4 * elt) = 30; if (onMorph) countFloatOnly(5, 2075); return;
    case 729:
        g_Client.Var(kSiege2State + 4 * elt) = 0;
        if (onMorph) WarpToOwnFactionTown();
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// 3rd "siege / ranking" family (fixed morph 54, dump L.4848-4964, sub-opcodes 740..749),
// state dword_16862B4 (global, no index). Two sub-cases (740/741/742) gate on
// g_World.self.levelBonus==12 instead of the morph.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kRankState   = 0x16862B4;
constexpr int32_t  kRankMorph   = 54;
constexpr uint32_t kRankArmA = 0x1675E70, kRankArmB = 0x1675E74,
                    kRankArmC = 0x1675E78, kRankArmD = 0x1675E7C;
} // namespace

void ApplyRankFamily740(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    const bool onMorph = (g_Client.VarGet(kSelfMorphNpcId) == kRankMorph);
    switch (subOpcode) {
    case 740: {
        uint32_t count = 0; if (len >= 8) std::memcpy(&count, payload + 4, 4);
        g_Client.Var(kRankState) = 0;
        if (g_World.self.levelBonus == 12) {
            const std::string s = std::to_string(count) + Str(2192);
            AnnounceFloating10(s);
        }
        return;
    }
    case 741:
        g_Client.Var(kRankState) = 1;
        if (g_World.self.levelBonus == 12) { const std::string s = Str(2193); AnnounceFloating10(s); }
        return;
    case 742:
        g_Client.Var(kRankState) = 2;
        if (g_World.self.levelBonus == 12) { const std::string s = Str(2194); AnnounceFloating10(s); }
        return;
    case 743: {
        uint32_t count = 0; if (len >= 8) std::memcpy(&count, payload + 4, 4);
        g_Client.Var(kRankState) = 3;
        if (onMorph) {
            const std::string s = std::to_string(count) + Str(2074);
            AnnounceFloating10(s);
        }
        return;
    }
    case 744:
        g_Client.Var(kRankState) = 6;
        return;
    case 745: {
        int32_t c1 = -1, c2 = -1; uint32_t count = 0;
        if (len >= 8)  std::memcpy(&c1, payload + 4, 4);
        if (len >= 12) std::memcpy(&c2, payload + 8, 4);
        if (len >= 16) std::memcpy(&count, payload + 12, 4);
        g_Client.Var(kRankState) = 6;
        if (!onMorph) return;
        if (c1 >= 0) { const std::string s = Str(75 + (c1 & 3)) + Str(2197); AnnounceFloating10(s); }
        if (c2 >= 0) { const std::string s = Str(75 + (c2 & 3)) + Str(2197); g_Client.msg.Floating(0, 0, s); g_Client.msg.System(s); }
        { const std::string s = std::to_string(count) + Str(2201); g_Client.msg.Floating(3, 0, s); g_Client.msg.System(s); }
        return;
    }
    case 746: {
        uint32_t count = 0; if (len >= 8) std::memcpy(&count, payload + 4, 4);
        g_Client.Var(kRankState) = 5;
        if (onMorph) {
            const std::string s = std::to_string(count) + Str(2074);
            AnnounceFloating10(s);
            g_Client.Var(kRankArmA) = 1; g_Client.Var(kRankArmB) = 1;
            g_Client.Var(kRankArmC) = 1; g_Client.Var(kRankArmD) = 1;
        }
        return;
    }
    case 747:
        g_Client.Var(kRankState) = 7;
        if (onMorph) {
            const std::string s = Str(1702);
            AnnounceFloating10(s);
            WarpToOwnFactionTown();
        }
        return;
    case 748: {
        int32_t cls = -1; if (len >= 8) std::memcpy(&cls, payload + 4, 4);
        g_Client.Var(kRankState) = 6;
        if (onMorph && cls >= 0) {
            const std::string s = Str(75 + (cls & 3)) + Str(2202);
            AnnounceFloating10(s);
        }
        return;
    }
    case 749:
        g_Client.Var(kRankState) = 0;
        if (onMorph) WarpToOwnFactionTown();
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Title/rank table (dump L.4977-5054, sub-opcodes 752/753): dword_184C218[0..11] (state),
// dword_184C248[0..11] (753, a target not consumed by this dispatcher -- no bound in the
// original binary; bounded here at 12 defensively, same convention as the rest of
// ClientSource -- behavioral fidelity BUT NOT bit-exact fidelity to a potential original
// out-of-bounds write).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kRankTable1 = 0x184C218; // [0..11]
constexpr uint32_t kRankTable2 = 0x184C248; // [0..11]
constexpr int32_t  kRankTitleId[12] = {2, 3, 4, 7, 8, 9, 12, 13, 14, 141, 142, 143};
} // namespace

void ApplyRankTable(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    uint32_t idx = 0; int32_t val = 0;
    if (len >= 8)  std::memcpy(&idx, payload + 4, 4);
    if (len >= 12) std::memcpy(&val, payload + 8, 4);
    if (subOpcode == 752) {
        if (idx < 12) { g_Client.Var(kRankTable1 + 4 * idx) = val; }
        if (idx >= 12) return;
        const int32_t cur = g_Client.VarGet(kRankTable1 + 4 * idx);
        if (cur / 100 != 9 && cur / 100 != 1) return;
        const int32_t titleId = kRankTitleId[idx];
        const std::string s = SkillName(titleId) + " " + Str((cur % 100) + 2249) + " " + Str(cur / 100 == 9 ? 2301 : 2300);
        g_Client.msg.Floating(0, 0, s);
        g_Client.msg.System(s);
        return;
    }
    if (subOpcode == 753) {
        if (idx < 12) g_Client.Var(kRankTable2 + 4 * idx) = val;
        return;
    }
}

// ---------------------------------------------------------------------------
// Sub-opcode 751 -- copy of miscellaneous bonus structures + localized class/skill notice.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kBuffMisc1 = 0x16862BC;
constexpr uint32_t kBuffMisc2 = 0x16862CC;
} // namespace

void Apply751(const uint8_t* payload, uint32_t len) {
    if (len < 68) return;
    uint32_t classId = 0, comboMotionId = 0;
    std::memcpy(&classId, payload + 8, 4);
    std::memcpy(&comboMotionId, payload + 12, 4);
    std::memcpy(reinterpret_cast<void*>(kBuffMisc1), payload + 16, 16);
    std::memcpy(reinterpret_cast<void*>(kBuffMisc2), payload + 32, 36);
    const std::string classLabel = Str(static_cast<int>(classId) + 1672);
    const std::string motionName = SkillName(static_cast<int>(comboMotionId));
    const std::string fmt = Str(2199);
    const std::string text = FormatString(fmt, classLabel.c_str(), motionName.c_str());
    g_Client.msg.Floating(2, 1, text);
    g_Client.msg.System(text);
}

// ---------------------------------------------------------------------------
// Sub-opcode 754 (dump L.5055-5111): verb (0/1/2, payload+4) + class 0..3 (payload+8) +
// comboMotionId (payload+12) + 13-byte text tag (payload+16, offset SPECIFIC to this
// sub-case). Message = SkillName + verb + " [class] [tag] " + str284. Out-of-range
// verbs/classes -> no-op (the binary reuses a buffer that was not re-initialized in that
// case, a fragile behavior not reproduced here).
// ---------------------------------------------------------------------------
void Apply754(const uint8_t* payload, uint32_t len) {
    if (len < 29) return;
    uint32_t verb = 0, classId = 0; int32_t comboMotionId = 0;
    std::memcpy(&verb, payload + 4, 4);
    std::memcpy(&classId, payload + 8, 4);
    std::memcpy(&comboMotionId, payload + 12, 4);
    char tag[14] = {}; std::memcpy(tag, payload + 16, 13);
    if (verb > 2 || classId > 3) return;
    static constexpr int kVerb[3] = {2250, 2251, 2252};
    const std::string s = SkillName(comboMotionId) + " " + Str(kVerb[verb]) +
                           " [" + Str(75 + static_cast<int>(classId)) + "] [" + tag + "] " + Str(284);
    g_Client.msg.Floating(0, 0, s);
    g_Client.msg.System(s);
}

// ---------------------------------------------------------------------------
// "Cast announce" class+tag family (dump L.5254-5368, sub-opcodes 771..774) -- same skeleton
// as 671..677 but with a comboMotionId (SkillName) added, and a faithful double-space in the
// original format ("[%s] [%s]  %s %s"). 771/774 also arm a shared timer if the current morph
// is one of {85,99,100,196} (CALCULABLE).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kCastArmA_771 = 0x1675CA0, kCastArmB_771 = 0x1675CA4;
constexpr uint32_t kCastArmA_774 = 0x1675CA8, kCastArmB_774 = 0x1675CAC;

bool IsCastAnnounceMorph(int32_t morph) { return morph == 85 || morph == 99 || morph == 100 || morph == 196; }
} // namespace

void ApplyCastAnnounce771to774(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    if (subOpcode == 772) {
        if (len < 8) return;
        int32_t comboMotionId = 0; std::memcpy(&comboMotionId, payload + 4, 4);
        const std::string s = SkillName(comboMotionId) + " " + Str(2431);
        AnnounceFloating24(s);
        return;
    }
    if (subOpcode == 773) {
        if (len < 12) return;
        uint32_t count = 0; int32_t comboMotionId = 0;
        std::memcpy(&count, payload + 4, 4);
        std::memcpy(&comboMotionId, payload + 8, 4);
        const std::string s = SkillName(comboMotionId) + " " + Str(2432) + " " + std::to_string(count) + Str(79);
        AnnounceFloating24(s);
        return;
    }
    // 771 / 774: class (payload+4) + comboMotionId (payload+8) + tag13 (payload+12).
    if (len < 25) return;
    uint32_t classId = 0; int32_t comboMotionId = 0;
    std::memcpy(&classId, payload + 4, 4);
    std::memcpy(&comboMotionId, payload + 8, 4);
    char tag[14] = {}; std::memcpy(tag, payload + 12, 13);
    if (classId > 3) return;
    const int strId = (subOpcode == 771) ? 2430 : 2433;
    const std::string s = "[" + Str(75 + static_cast<int>(classId)) + "] [" + tag + "]  " +
                           SkillName(comboMotionId) + " " + Str(strId);
    g_Client.msg.Floating(2, (subOpcode == 771) ? 4 : 3, s);
    g_Client.msg.System(s);
    const int32_t morph = g_Client.VarGet(kSelfMorphNpcId);
    if (!IsCastAnnounceMorph(morph)) return;
    if (subOpcode == 771) { g_Client.Var(kCastArmA_771) = 1; g_Client.VarF(kCastArmB_771) = 0.0f; }
    else                  { g_Client.Var(kCastArmA_774) = 1; g_Client.VarF(kCastArmB_774) = 0.0f; }
}

// ---------------------------------------------------------------------------
// Simple "SkillName + suffix" notifications (dump L.5450-5470, sub-opcodes 788/789/790):
// comboMotionId=payload+4, chat message only (no HUD).
// ---------------------------------------------------------------------------
void ApplySimpleSkillNotice(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    if (len < 8) return;
    int32_t comboMotionId = 0; std::memcpy(&comboMotionId, payload + 4, 4);
    int strId = 0;
    switch (subOpcode) {
    case 788: strId = 2462; break;
    case 789: strId = 2502; break;
    case 790: strId = 2463; break;
    default: return;
    }
    g_Client.msg.System(SkillName(comboMotionId) + " " + Str(strId));
}

// Sub-opcode 791 (dump L.5471): comboMotionId + class + tag13 (payload+4/+8/+12, standard
// offsets). HUD+chat message.
void Apply791(const uint8_t* payload, uint32_t len) {
    if (len < 25) return;
    int32_t comboMotionId = 0; uint32_t classId = 0;
    std::memcpy(&comboMotionId, payload + 4, 4);
    std::memcpy(&classId, payload + 8, 4);
    char tag[14] = {}; std::memcpy(tag, payload + 12, 13);
    const std::string s = SkillName(comboMotionId) + " " + Str(2459) + " " + Str(75 + static_cast<int>(classId & 3)) +
                           " " + Str(2460) + " " + tag + " " + Str(2461);
    g_Client.msg.Floating(2, 1, s);
    g_Client.msg.System(s);
}

// Sub-opcodes 792..794 -- item/monster notices with a localized suffix.
void Apply792to794(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    if (len < 12) return;
    int32_t comboMotionId = 0;
    uint32_t monsterId = 0;
    std::memcpy(&comboMotionId, payload + 4, 4);
    std::memcpy(&monsterId, payload + 8, 4);
    // OFF-BY-ONE CORRECTION: the MONSTER getter 0x4C6570 is STRICTLY 1-based
    // (base+944*(id-1)); the old `record(monsterId)` WITHOUT -1 was WRONG. GetMonsterInfo
    // applies the -1 and the "first dword != 0" guard.
    const MonsterInfo* info = GetMonsterInfo(monsterId);
    if (!info) return;

    std::string format;
    switch (subOpcode) {
    case 792: format = Str(2714); break;
    case 793: format = Str(2715); break;
    case 794: format = Str(2716); break;
    default: return;
    }

    const std::string suffix = FormatString(format, info->name); // info->name = record + 4 (char[25])
    const std::string text = SkillName(comboMotionId) + " " + suffix;
    g_Client.msg.System(text);
}

// Sub-opcode 795 (dump L.5526): class (payload+4, offset +1672 -> label), tag13 (payload+8),
// count (payload+21). The Str(2576) format-table is applied to the count in the binary --
// approximated as an "[n]" suffix (same convention as 610/661).
void Apply795(const uint8_t* payload, uint32_t len) {
    if (len < 25) return;
    uint32_t classId = 0; int32_t count = 0;
    std::memcpy(&classId, payload + 4, 4);
    char tag[14] = {}; std::memcpy(tag, payload + 8, 13);
    if (len >= 25) std::memcpy(&count, payload + 21, 4);
    const std::string s = Str(static_cast<int>(classId) + 1672) + " " + tag + "[" + std::to_string(count) + "]" + Str(2576);
    g_Client.msg.System(s);
}

// ---------------------------------------------------------------------------
// "War 324" events (dump L.5369-5450, sub-opcodes 780..786): state dword_1686304
// (rank, -1=none)/dword_1686308 (value). Gate morph = 324. TODO(World_LoadCurrentZoneModel)
// omitted everywhere it is cited (zone-model reload, subsystem not modeled in ClientSource).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kWar324Rank  = 0x1686304;
constexpr uint32_t kWar324Value = 0x1686308;
constexpr int32_t  kWar324Morph = 324;
} // namespace

void ApplyWarEvent780to786(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    const bool onMorph = (g_Client.VarGet(kSelfMorphNpcId) == kWar324Morph);
    switch (subOpcode) {
    case 780: { // TODO(World_LoadCurrentZoneModel(6)) if onMorph.
        uint32_t count = 0; int32_t j = 0;
        if (len >= 8)  std::memcpy(&count, payload + 4, 4);
        if (len >= 12) std::memcpy(&j, payload + 8, 4);
        g_Client.Var(kWar324Value) = j;
        const std::string s = std::to_string(count) + " " + Str(2439);
        AnnounceFloating24(s);
        g_Client.msg.System(Str(2440));
        return;
    }
    case 781: { // TODO(World_LoadCurrentZoneModel(2)) if onMorph.
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar324Value) = j;
        g_Client.Var(kWar324Rank) = -1;
        const std::string s = Str(2441);
        AnnounceFloating24(s);
        return;
    }
    case 782: {
        int32_t rank = 0, j = 0;
        if (len >= 8)  std::memcpy(&rank, payload + 4, 4);
        if (len >= 12) std::memcpy(&j, payload + 8, 4);
        g_Client.Var(kWar324Rank) = rank;
        g_Client.Var(kWar324Value) = j;
        std::string s;
        if (rank >= 0)
            s = Str(2532) + " [ " + Str(rank / 10 + 2685) + " ] " + Str(377) + " [ " + Str(rank % 10 + 2685) + " ] " + Str(2442);
        else
            s = Str(2532) + " " + Str(2447);
        AnnounceFloating24(s);
        g_Client.msg.System(Str(2446));
        return;
    }
    case 784: {
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar324Value) = j;
        const std::string s = Str(2445);
        AnnounceFloating24(s);
        if (onMorph) g_Client.msg.System(Str(2446));
        return;
    }
    case 785: { // TODO(World_LoadCurrentZoneModel(1)) if onMorph.
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar324Value) = j;
        if (onMorph) WarpToOwnFactionTown();
        return;
    }
    case 786: { // NB: reads payload+8 (not +4), faithful to the disasm (v703, not v702).
        int32_t j = 0; if (len >= 12) std::memcpy(&j, payload + 8, 4);
        g_Client.Var(kWar324Rank) = -1;
        g_Client.Var(kWar324Value) = j;
        return;
    }
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// "War 342" events (dump L.5537-5610, sub-opcodes 800..807), same shape as 780..786 with a
// different target morph (342) and a single state dword_1686310. 803 (dump L.5558)
// INVESTIGATED AND CONFIRMED NO-OP: copies 16 o into a LOCAL buffer never re-read (a dead
// store on the original binary side) -- no code added here (the dispatcher's `default:`
// already covers it faithfully).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kWar342Value = 0x1686310;
constexpr int32_t  kWar342Morph = 342;
} // namespace

void ApplyWarEvent800to807(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    const bool onMorph = (g_Client.VarGet(kSelfMorphNpcId) == kWar342Morph);
    switch (subOpcode) {
    case 800: {
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar342Value) = j;
        return;
    }
    case 801: {
        uint32_t count = 0; if (len >= 8) std::memcpy(&count, payload + 4, 4);
        g_Client.msg.System(std::to_string(count) + " " + Str(2761));
        return;
    }
    case 802: { // TODO(World_LoadCurrentZoneModel(1)) if onMorph.
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar342Value) = j;
        const std::string s = Str(2762);
        AnnounceFloating24(s);
        return;
    }
    case 804: { // TODO(World_LoadCurrentZoneModel(2)) if onMorph.
        int32_t j = 0; if (len >= 8) std::memcpy(&j, payload + 4, 4);
        g_Client.Var(kWar342Value) = j;
        const std::string s = Str(2764);
        g_Client.msg.Floating(2, 4, s);
        if (onMorph) { g_Client.Var(kChargeArmedTimer) = 1; g_Client.VarF(kChargeElapsed) = 0.0f; }
        g_Client.msg.System(s);
        return;
    }
    case 805: {
        uint32_t count = 0; if (len >= 8) std::memcpy(&count, payload + 4, 4);
        g_Client.msg.System(std::to_string(count) + " " + Str(2766));
        return;
    }
    case 806: {
        int32_t j = 0, val = 0;
        if (len >= 8)  std::memcpy(&j, payload + 4, 4);
        if (len >= 12) std::memcpy(&val, payload + 8, 4);
        g_Client.Var(kWar342Value) = val;
        g_Client.msg.System(Str(2767));
        if (j == -1) g_Client.msg.System(Str(2765));
        else         g_Client.msg.System("[ " + Str(j + 2685) + " ] " + Str(2442));
        return;
    }
    case 807: // TODO(World_LoadCurrentZoneModel(1)) if onMorph.
        if (onMorph) WarpToOwnFactionTown();
        return;
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Final notices 901..903 (dump L.5610-5630) -- banner + system line, same shape as
// sub-opcode 401 (ApplyNotice401, WorldEntityDispatch_Special.cpp), no state.
// ---------------------------------------------------------------------------
void Apply901to903(uint32_t subOpcode) {
    int strId = 0;
    switch (subOpcode) {
    case 901: strId = 2990; break;
    case 902: strId = 2991; break;
    case 903: strId = 2992; break;
    default: return;
    }
    const std::string s = Str(strId);
    g_Client.msg.Floating(2, 3, s);
    g_Client.msg.System(s);
}

} // namespace wed_detail
} // namespace ts2::game

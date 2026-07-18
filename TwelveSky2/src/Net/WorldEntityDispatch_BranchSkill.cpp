// Net/WorldEntityDispatch_BranchSkill.cpp — split of Net_OnWorldEntityDispatch (opcode 0x5e,
// EA 0x494870): the "skill branch" family (Skill_GetMotionId2, sub-opcodes 31/32 exclusion
// toggle + 63..100 probe/poll/arm/label/flags/sound-only cycle) and the "duel/challenge" +
// "branch mastery" family (sub-opcodes 101..115). See Net/WorldEntityDispatch.h for the full
// sub-opcode map and Net/WorldEntityDispatch_Internal.h for shared plumbing.
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
// Sub-opcodes 31 (EA 0x4963b0) / 32 (EA 0x496583) — "31..170 survey" mission (2026-07-14):
// FIRST mechanism of the dispatcher's "remainder" (confirmed on the full disassembly, NOT a
// continuation of family 3 — Skill_GetComboMotionId is never called here). The two
// sub-opcodes are STRUCTURAL TWINS (same instructions duplicated by the compiler): only the
// written value (0/1) and the 2nd label range (251..254 vs 255..258) differ.
//   branch = payload+4 (str75..78 label, EA 0x4963f5..0x49645e — same convention as
//     ApplyFamily3TagSlot/ApplyRankFamily740).
//   type   = payload+8 (str251..254 label for 31, str255..258 for 32, EA
//     0x4964b8..0x49652a; format " %s" = off_7A6E5C, read DIRECTLY from memory — not a
//     hand-recreated C literal).
// The binary bounds the index (0..3, "ja default") ONLY for the LABEL CHOICE (EA
// 0x4963e2/0x4964a5) — outside 0..3 the corresponding text segment is simply omitted
// (StrTable005_Get not called). The WRITE into dword_1686014 is UNCONDITIONAL and precedes
// these checks (same "state before gate" convention as the combo families). Floating notice
// HUD_ShowFloatingMessage(floatType=3, flag=4) common to both sub-opcodes.
// NO consumer of dword_1686014 is ported in ClientSource to date — confirmed via xrefs_to
// idaTs2: three binary-side readers exist (Combo_CheckTransition 0x4fd650 EA 0x4ff2d6,
// Player_UpdateMovement 0x534500 EA 0x534856, UI_FactionInfoWnd_Render 0x672010 EA
// 0x67246d) -> the write is faithfully persisted but has NO observable effect until those 3
// functions are ported client-side. TODO(@dword_1686014_consumers).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kExclusionTable = 0x1686014; // dword_1686014[4*branche+type], EA 0x4963b0/0x496583
} // namespace

void ApplyExclusionToggleSlot(uint32_t branch, uint32_t type, uint32_t value) {
    g_Client.Var(kExclusionTable + 4 * (4 * branch + type)) = static_cast<int32_t>(value);

    const int typeBase = value ? 255 : 251; // 31(value=0)=251..254 ; 32(value=1)=255..258
    std::string text;
    if (branch <= 3) text += "[" + Str(75 + static_cast<int>(branch)) + "]";
    if (type   <= 3) text += " " + Str(typeBase + static_cast<int>(type));
    g_Client.msg.Floating(3, 4, text);
}

// ---------------------------------------------------------------------------
// "Skill branch" family (Skill_GetMotionId2(branch, type), covering sub-opcodes 33..115 per
// the initial survey — the 76..100 slice is fully wired here (mission "sub-range 76..100",
// 2026-07-14); 63..75 (preamble "poll/arm1/arm2" + 1st/2nd cycle iteration below) wired by
// mission "sub-range 51-75" the same day, reusing the same ApplyBranch* functions defined
// here. 101..115 = a DISTINCT duel/challenge subsystem (cf. ApplyDuelBranchFamily below).
// State dword_1685F94[8*branch+type] (branch=payload+4, type=payload+8, same idx/arg2
// offsets as the rest of the file). DISTINCT from Skill_GetComboMotionId (combo families
// above) — confirmed by direct disassembly reading (switch jump_ea=0x494a17, header EA
// 0x49a514..0x49b4cb for this slice, cf. RE/dispatch_tables.json and
// RE/dispatch_494870_full.c L.2306-2578). Skill_IsAvailableByBranch(lvlTbl, skillId, level,
// levelBonus, element) already EXISTS (Game/SkillSystem.h/.cpp) but is NOT called here: the
// one sub-case that invokes it in this slice (76/83/90/97, "probe") DISCARDS the result —
// confirmed by direct disasm at EA 0x49a58d/0x49a58f/0x49a596: both branches of the `test
// eax,eax` converge to the same jump at `default:`, no observable behavior divergence — same
// "result discarded -> reproduced without the gate" convention as elsewhere in this file (cf.
// ApplyComboFamilySlot case 5/9).
//
// Inside the 76..100 slice, the disassembly reveals a repeating 7-sub-case cycle (76..82,
// 83..89, 90..96, plus a PARTIAL 4th repeat 97..100 — probe/L418/L420 only, the slice ending
// at 100) — VERIFIED by direct disasm on cases 76/78/80 (EA 0x49a514/0x49a670/0x49a7d5,
// exactly matching RE/dispatch_494870_full.c line 2306 onward):
//   1. "probe"     (76/83/90/97, EA 0x49a514/a9ad/ae46/b2df): state=23 ONLY
//      (Skill_IsAvailableByBranch called but result discarded, cf. above).
//   2. "L418"      (77/84/91/98, EA 0x49a59b/aa34/aecd/b366): state=23. Gate=motion==current
//      morph (CALCULABLE) -> message "<name> <str235>" + return to faction town (SAME
//      LABEL_418 tail-merge as the binary, str235 = same id as kFamily2/3.strSlot6).
//   3. "L420"      (78/85/92/99, state=10/14/18/22 respectively, EA 0x49a670/ab09/afa2/b43b):
//      gate=morph -> SOUND ONLY (Snd3D_PlayScaledVolume flt_14972BC, fixed, OUT OF SCOPE
//      audio, same convention as elsewhere here); no message.
//   4. "L422"      (79/86/93/100, EA 0x49a700/ab99/b032/b4cb): state=23. Gate=morph ->
//      message "<name> <str237>" + return to faction town (str237 = same id as the combo/
//      Special families' slot8).
//   5. "flagsA"    (80/87/94, state=11/15/19, EA 0x49a7d5/ac6e/b107): gate=morph -> sound
//      (OUT OF SCOPE) + arms 2 flags SPECIFIC to the iteration, both =1
//      (dword_1675C1C/C20, C24/C28, C2C/C30) — NO shared charge bar/half-duration, unlike
//      the combo families.
//   6. "flagsB"    (81/88/95, state=12/16/20, EA 0x49a879/ad12/b1ab): same shape as flagsA,
//      DIFFERENT flags (dword_1675C44/C48, C4C/C50, C54/C58), SAME sound as that
//      iteration's flagsA.
//   7. "soundOnly" (82/89/96, state=13/17/21, EA 0x49a91d/adb6/b24f): gate=morph -> SOUND
//      ONLY (OUT OF SCOPE), no other observable effect -> reproduced as state write alone
//      (same "result/gate without effect" convention as "probe" above).
// The cycle restarts at 69 for the 1st iteration (69..75) — wired by mission "sub-range
// 51-75" (2026-07-14, cf. the "Sub-opcodes 63..68" block below, which reuses
// ApplyBranchProbe/Label418/Label420/Label422/FlagsSlot/SoundOnlySlot verbatim for 69..75).
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kBranchState = 0x1685F94; // dword_1685F94[8*branch+type]
} // namespace

inline int BranchMotionId(uint32_t branch, uint32_t type) {
    return Skill_GetMotionId2(static_cast<int>(branch), static_cast<int>(type));
}

// "probe" (76/83/90/97): state=23 only, Skill_IsAvailableByBranch never influences the
// outcome (result always discarded binary-side, cf. header comment).
void ApplyBranchProbe(uint32_t branch, uint32_t type) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = 23;
}

// "L418" (77/84/91/98): state=23, message str235 + return to faction town if on morph.
void ApplyBranchLabel418(uint32_t branch, uint32_t type) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = 23;
    const int motionId = BranchMotionId(branch, type);
    if (motionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    g_Client.msg.System(SkillName(motionId) + " " + Str(235));
    WarpToOwnFactionTown();
}

// "L420" (78/85/92/99): state=stateValue (10/14/18/22), sound only if on morph (OUT OF
// SCOPE audio -- no effect modeled beyond the state).
void ApplyBranchLabel420(uint32_t branch, uint32_t type, int32_t stateValue) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = stateValue;
}

// "L422" (79/86/93/100): state=23, message str237 + return to faction town if on morph.
void ApplyBranchLabel422(uint32_t branch, uint32_t type) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = 23;
    const int motionId = BranchMotionId(branch, type);
    if (motionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    g_Client.msg.System(SkillName(motionId) + " " + Str(237));
    WarpToOwnFactionTown();
}

// "flagsA"/"flagsB" (80/81, 87/88, 94/95): state=stateValue, arms 2 flags SPECIFIC to the
// iteration (=1 each) if on morph (sound OUT OF SCOPE audio).
void ApplyBranchFlagsSlot(uint32_t branch, uint32_t type, int32_t stateValue,
                           uint32_t flagAddr1, uint32_t flagAddr2) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = stateValue;
    const int motionId = BranchMotionId(branch, type);
    if (motionId != g_Client.VarGet(kSelfMorphNpcId)) return;
    g_Client.Var(flagAddr1) = 1;
    g_Client.Var(flagAddr2) = 1;
}

// "soundOnly" (82/89/96): state=stateValue only (sound OUT OF SCOPE audio, no other
// observable effect -> no need to compute motionId/gate, cf. ApplyBranchProbe).
void ApplyBranchSoundOnlySlot(uint32_t branch, uint32_t type, int32_t stateValue) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = stateValue;
}

// ---------------------------------------------------------------------------
// Sub-opcodes 63..68 (mission "sub-range 51-75", 2026-07-14, RE/dispatch_494870_full.c
// L.2219-2305) — PREAMBLE of the skill-branch family above, BEFORE the repeating
// "probe/L418/L420/L422/flagsA/flagsB/soundOnly" cycle that formally starts at 69 (cf. header
// comment, "the cycle restarts at 69"): 3 sub-cases "poll/arm1/arm2" with NO analogue in the
// 76..100 cycle (63/64/65 — the ONLY places in the whole branch family where
// Skill_IsAvailableByBranch actually influences a branch; "probe" 69/76/83/90/97 always
// discards its result, cf. above), THEN 3 sub-cases (66/67/68) that are actually the FIRST
// iteration of the "flagsA/flagsB/soundOnly" cycle (states 3/4/5, +4 progression confirmed up
// to 19/20/21 at 94/95/96) — reusing ApplyBranchFlagsSlot/ApplyBranchSoundOnlySlot verbatim:
//   63  "poll" (L.2230): READ-ONLY (dword_1685F94 never written here). Gate =
//       Skill_IsAvailableByBranch AND current state<=0 -> message "<name> [<arg3>]<str231>"
//       (HUD floating cat5/type1 + chat). arg3 = payload+12 (integer — reinterpretation of
//       the same raw field as tag13 elsewhere in this file, as everywhere here).
//   64  "arm1" (L.2245): state=1, written unconditionally. Gate=avail -> message
//       "<name> <str232>" (HUD cat5/type2 + chat).
//   65  "arm2" (L.2259): state=2, written unconditionally. Gate=avail -> message
//       "<name> <str233>" (HUD cat5/type3 + chat).
//   66  "flagsA" 1st iteration (L.2273): state=3, flags dword_1675C0C/1675C10=1 if on
//       current morph (sound OUT OF SCOPE) -> ApplyBranchFlagsSlot(...,3,0x1675C0C,0x1675C10).
//   67  "flagsB" 1st iteration (L.2285): state=4, flags dword_1675C34/1675C38=1 if on morph
//       -> ApplyBranchFlagsSlot(...,4,0x1675C34,0x1675C38).
//   68  "soundOnly" 1st iteration (L.2297): state=5, sound only if on morph (OUT OF SCOPE)
//       -> ApplyBranchSoundOnlySlot(...,5).
// ---------------------------------------------------------------------------
inline bool BranchSkillAvailable(int motionId) {
    return Skill_IsAvailableByBranch(GetSkillLevelTable(), motionId, g_World.self.level,
                                      g_World.self.levelBonus, g_World.self.element);
}

// 63: "poll", the Skill_IsAvailableByBranch result IS actually used (unlike the "probe"
// 69/76/83/90/97 which discards it).
void ApplyBranchPoll(uint32_t branch, uint32_t type, uint32_t arg3) {
    const int motionId = BranchMotionId(branch, type);
    const int32_t state = g_Client.VarGet(kBranchState + 4 * (8 * branch + type));
    if (state > 0 || !BranchSkillAvailable(motionId)) return;
    const std::string msg = SkillName(motionId) + " [" + std::to_string(arg3) + "]" + Str(231);
    g_Client.msg.Floating(5, 1, msg);
    g_Client.msg.System(msg);
}

// 64/65: "arm1"/"arm2", state written unconditionally then message gated by availability.
void ApplyBranchArm(uint32_t branch, uint32_t type, int32_t stateValue, int strId, int floatType) {
    g_Client.Var(kBranchState + 4 * (8 * branch + type)) = stateValue;
    const int motionId = BranchMotionId(branch, type);
    if (!BranchSkillAvailable(motionId)) return;
    const std::string msg = SkillName(motionId) + " " + Str(strId);
    g_Client.msg.Floating(5, floatType, msg);
    g_Client.msg.System(msg);
}

// ---------------------------------------------------------------------------
// 13-byte self-affiliation buffers (WARP-03/duel branch). Faithful strcpy(dest, src) via
// SelfBodyStrcpy13 below (cf. Net/WorldEntityDispatch_Internal.h::BlobStrcpy13 for the
// Crt_StringInit 0x75CAB0 false-friend resolution shared by both helpers). Targets self's
// entity-sheet fields (0x168725C = entity[0]+40 = body+16 ; 0x1687270 = entity[0]+60 =
// body+36): their modeled home is g_World.players[0].body (index 0 = self, GameState.h:122)
// — body+16 has LIVE readers (Scene/WorldRenderer.cpp:803 affiliation, World/
// TerrainPicker.cpp:280), so writing anywhere but the body would create a phantom store.
// kLocalAffilName/kLocalAffilName2 mirror the SAME identity in the standalone long-tail
// globals (0x16746A8/0x16746BC) via BlobStrcpy13 — Blob() FREEZES its size at the first
// caller (ClientRuntime.h:179): 0x16746A8 is already opened at 13 by
// UI/ClanContextMenu.cpp:92 (`BlobNonEmpty -> Blob(addr,13)`), so it must stay opened at 13
// EVERYWHERE (opening it at 16 would overrun the heap slot).
// ---------------------------------------------------------------------------
namespace {
void SelfBodyStrcpy13(size_t bodyOffset, const char* src) {
    auto& players = g_World.players;
    if (players.empty()) return;         // no phantom self (cf. App/App.cpp:770/1161)
    auto& body = players[0].body;
    if (bodyOffset + 13 > body.size()) return;
    size_t n = 0;
    while (n < 13 && src[n] != 0) ++n;
    std::memset(body.data() + bodyOffset, 0, 13);
    std::memcpy(body.data() + bodyOffset, src, n);
}
// Self affiliation identity (EXACT mirror: 0x16746A8/+16/+20 == entity+40/+56/+60).
constexpr uint32_t kLocalAffilName  = 0x16746A8; // dword_16746A8 (= UI/ClanContextMenu::kVarGuildTag)
constexpr uint32_t kLocalAffilName2 = 0x16746BC; // unk_16746BC
constexpr size_t   kSelfBodyAffil   = 40 - 24;   // byte_168725C (= WorldRenderer::kNpBodyAffiliation)
constexpr size_t   kSelfBodyAffil2  = 60 - 24;   // unk_1687270
} // namespace

// ---------------------------------------------------------------------------
// Sub-opcodes 101..115 (dump L.2579-2789, mission "duel/challenge 101-115" 2026-07-14) — TWO
// distinct subsystems identified by the initial audit (cf. Net/WorldEntityDispatch.h,
// "UNCOVERED RANGES" block):
//   - 101..109/112/114: DUEL/CHALLENGE by player NAME comparison — the payload carries raw
//     13-byte names (NOT indices) at offsets DIFFERENT from this file's standard
//     idx/arg2/tag13 convention (re-read directly from `payload` here, confirmed esp-to-esp
//     on the disassembly: v702=payload+4, v703=payload+8, v706=payload+17, v713=payload+30,
//     v715=payload+34, v716=payload+43). Shared state: dword_16746B8/dword_168726C (duel
//     state, 0=none/2=in progress/raw code depending on the case — values confirmed by the
//     writes in 102/107/108/114), dword_1687450 (opponent code/result, reused by
//     102/107/112/114 — 113 excluded, cf. below), dword_168744C (102 only), dword_16747F0
//     (114, 1st gate only), dword_168736C[0]/dword_1687374[0] (min/max attack rating, SAME
//     addresses as A_RatingBaseMin/Max in Net/GameVarDispatch.cpp — duplicated here as local
//     constants, this file shares no address via a header, same convention everywhere here).
//   - 111/115: "branch mastery" notices (StrTable005 ids 1671..1675/2322), SAME shape as
//     96..100/110 (branch 0..3 -> label 1672..1675) but WITHOUT Skill_GetMotionId2/
//     dword_1685F94 — purely a notification, no state written.
// EXCLUDED from this pass (documented, NOT an oversight — cf. the `default` of
// ApplyDuelBranchFamily):
//   - 110: STRUCTURALLY belongs to the "skill branch" family 96..100 (dword_1685F94[8*a+b],
//     Skill_GetMotionId2/Skill_IsAvailableByBranch) — NOT to "duel/challenge" nor "branch
//     mastery", out of scope for this mission (33..100 remains a dense zone not fully wired
//     for a future pass, cf. Net/WorldEntityDispatch.h).
//   - 113: ENTIRELY gated by `!Crt_Strcmp(dword_16746A8, v686)` (comparison against the LOCAL
//     guild/affiliation name — cf. Game/NameplateLogic.h, header comment: "uncertain
//     semantics ... to be confirmed by a future RE pass"), NO unconditional state write to
//     preserve ahead of this gate — SAME status as 425..428 (cf. the "Sub-opcodes 418..429"
//     block in WorldEntityDispatch_Special.cpp): nothing calculable without fabricating an
//     unconfirmed field -> NOT wired.
// The `byte_1673184 == packet_name` gate (102/107/108/114) uses g_World.self.localPlayerName
// (SAME field as Game/GameState.h::SelfState::localPlayerName, already documented as
// "probably byte_1673184" — never populated by any handler to date in ClientSource, so this
// gate fails cleanly/systematically until the login packet populates it — SAME honest
// degradation policy as the rest of this file, NOT a regression introduced here). Several
// argument-less `Crt_StringInit()` calls (Hex-Rays elided the `this` pointer) precede some
// writes in the binary (102/107/109/114) — targets NOT identifiable from this dump -> omitted
// (TODO), ONLY the confirmed scalar writes (dword_167xxxx) are reproduced below.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kDuelStateA       = 0x16746B8; // dword_16746B8 (102/107/108/114)
constexpr uint32_t kDuelStateB       = 0x168726C; // dword_168726C (102/107/108/114)
constexpr uint32_t kDuelOpponentVal  = 0x1687450; // dword_1687450 (102/107/112/114)
constexpr uint32_t kDuelFlag744C     = 0x168744C; // dword_168744C (102 only)
constexpr uint32_t kDuelRatingMin    = 0x168736C; // dword_168736C[0] (== A_RatingBaseMin, GameVarDispatch.cpp)
constexpr uint32_t kDuelRatingMax    = 0x1687374; // dword_1687374[0] (== A_RatingBaseMax, GameVarDispatch.cpp)
constexpr uint32_t kDuelField16747F0 = 0x16747F0; // dword_16747F0 (114, 1st gate only)

// Char_CalcAttackRatingMin/Max (0x4CD970/0x4CE3F0) -- TODO(stat), SAME approximation as
// Net/GameVarDispatch.cpp::Char_CalcAttackRatingMin/Max (the Char_Calc* stat engine is not
// yet ported; returns self's current derived value). Duplicated locally -- this file does not
// share its helpers via a header, same convention everywhere here.
int DuelCalcAttackRatingMin() { return g_World.self.atkRatingMin; }
int DuelCalcAttackRatingMax() { return g_World.self.atkRatingMax; }

// Reads a raw 13-byte name/handle field at `payload+offset`, defensively NUL-terminated
// (same convention as the file's standard `tag13`, cf. ApplyFamily3TagSlot).
std::string ReadName13(const uint8_t* payload, uint32_t offset) {
    char buf[14] = {};
    std::memcpy(buf, payload + offset, 13);
    return std::string(buf);
}

// CLEANUP (audit "dispatch 0x5e" 2026-07-14) — body STRICTLY identical between 111 and 115
// (branch 0..3=payload+4, tag=payload+8, HUD cat.0/type0 + chat), only the suffix differs
// (str1671 for 111, "branch mastery"; str2322 for 115) — confirmed instruction by
// instruction, cf. the original comments of both cases. Factored here, NO behavior change.
void ApplyBranchMasteryNotice(const uint8_t* payload, uint32_t len, int suffixStrId) {
    if (len < 21) return;
    int32_t branch = 0; std::memcpy(&branch, payload + 4, 4);
    if (branch < 0 || branch > 3) return; // outside 0..3: the binary reuses a buffer that was
                                           // not re-initialized (same convention as Apply754).
    static constexpr int kLabel[4] = {1672, 1673, 1674, 1675};
    const std::string tag = ReadName13(payload, 8);
    const std::string s = "[" + Str(kLabel[branch]) + "] " + tag + " " + Str(suffixStrId);
    g_Client.msg.Floating(0, 0, s);
    g_Client.msg.System(s);
}
} // namespace

void ApplyDuelBranchFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    switch (subOpcode) {
    case 101: { // dump L.2579 -- pure notification, no state.
        if (len < 30) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(396));
        return;
    }
    case 102: { // dump L.2587 -- gate = am I the target (name1==my name) -> starts the duel.
        if (len < 38) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        int32_t oppVal = 0, flag744C = 0;
        std::memcpy(&oppVal, payload + 30, 4);
        std::memcpy(&flag744C, payload + 34, 4);
        if (g_World.self.localPlayerName == name1) {
            // Self affiliation identity <- name2 (var_54C = payload+17). RE-PROVEN in W11
            // (WARP-03): these 4 Crt_StringInit calls were elided by Hex-Rays (implicit
            // `this`) but the disassembly gives them unambiguously. NB: kDuelState* is
            // probably a MISNOMER (this global 0x16746B8 is written by the GUILD dispatcher
            // 0x53 and read by clan UI) -> not renamed here (would touch 101..115, out of
            // scope for W11); FLAGGED.
            BlobStrcpy13(kLocalAffilName,      name2.c_str()); // 0x49b73e : 0x16746A8 <- name2
            g_Client.Var(kDuelStateA) = 2;                     // 0x49b746 : 0x16746B8 = 2
            BlobStrcpy13(kLocalAffilName2,     "");            // 0x49b75a : 0x16746BC <- ""
            SelfBodyStrcpy13(kSelfBodyAffil,   name2.c_str()); // 0x49b76e : 0x168725C <- name2
            g_Client.Var(kDuelStateB) = 2;                     // 0x49b776 : 0x168726C = 2
            SelfBodyStrcpy13(kSelfBodyAffil2,  "");            // 0x49b78a : 0x1687270 <- ""
            g_Client.Var(kDuelOpponentVal) = oppVal;           // 0x49b798 : 0x1687450 <- oppVal
            g_Client.Var(kDuelFlag744C) = flag744C;            // 0x49b7a3 : 0x168744C <- flag744C
            g_Client.Var(kDuelRatingMin) = DuelCalcAttackRatingMin();
            g_Client.Var(kDuelRatingMax) = DuelCalcAttackRatingMax();
        }
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(415));
        return;
    }
    case 103: { // dump L.2610 -- pure notification, no state.
        if (len < 30) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(482));
        return;
    }
    case 104: { // dump L.2618 -- pure notification (single name), no state.
        if (len < 17) return;
        g_Client.msg.System("[" + ReadName13(payload, 4) + "]" + Str(563));
        return;
    }
    case 105: { // dump L.2624 -- same as 104, distinct suffix.
        if (len < 17) return;
        g_Client.msg.System("[" + ReadName13(payload, 4) + "]" + Str(480));
        return;
    }
    case 106: { // dump L.2630 -- same as 104, distinct suffix.
        if (len < 17) return;
        g_Client.msg.System("[" + ReadName13(payload, 4) + "]" + Str(564));
        return;
    }
    case 107: { // dump L.2636 -- gate = am I the target -> CANCELS the duel (state=0).
        if (len < 30) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        if (g_World.self.localPlayerName == name1) {
            // Reset self's affiliation identity (4 strcpy to "" -- WARP-03).
            BlobStrcpy13(kLocalAffilName,     "");             // 0x49ba22 : 0x16746A8 <- ""
            g_Client.Var(kDuelStateA) = 0;                     // 0x49ba2f : 0x16746B8 = 0
            BlobStrcpy13(kLocalAffilName2,    "");             // 0x49ba3e : 0x16746BC <- ""
            SelfBodyStrcpy13(kSelfBodyAffil,  "");             // 0x49ba50 : 0x168725C <- ""
            g_Client.Var(kDuelStateB) = 0;                     // 0x49ba5d : 0x168726C = 0
            SelfBodyStrcpy13(kSelfBodyAffil2, "");             // 0x49ba71 : 0x1687270 <- ""
            g_Client.Var(kDuelOpponentVal) = 0;                // 0x49ba79 : 0x1687450 = 0
            g_Client.Var(kDuelFlag744C) = 0;
        }
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(481));
        return;
    }
    case 108: { // dump L.2655 -- gate = am I the target -> state = received code (v656, 1=accepted).
        if (len < 34) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        int32_t code = 0;
        std::memcpy(&code, payload + 30, 4);
        if (g_World.self.localPlayerName == name1) {
            g_Client.Var(kDuelStateA) = code;
            g_Client.Var(kDuelStateB) = code;
        }
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(code == 1 ? 554 : 555));
        return;
    }
    case 109: { // dump L.2672 -- gate = am I the target (resets not calculable, TODO above);
                // suffix depends on a 5-byte tag (payload+30) empty or not (Crt_Strcmp(tag,"")
                // : since "" is empty, the outcome depends ONLY on tag's 1st byte -- a
                // mechanical fact, not a guess about the field's semantics).
        if (len < 35) return;
        const std::string name1 = ReadName13(payload, 4), name2 = ReadName13(payload, 17);
        const bool tagNonEmpty = (payload[30] != 0);
        g_Client.msg.System("[" + name1 + "]" + Str(395) + " [" + name2 + "]" + Str(tagNonEmpty ? 561 : 562));
        return;
    }
    case 111: // dump L.2696 -- "branch mastery": branch 0..3 -> label 1672..1675 (str1671).
        ApplyBranchMasteryNotice(payload, len, 1671);
        return;
    case 112: { // dump L.2725 -- unconditional write + table message (1719+code).
                // NB: the packet name (payload+4) is read by the binary but never reused in
                // the message or the state -- faithful dead read (not copied here).
        if (len < 21) return;
        int32_t code = 0; std::memcpy(&code, payload + 17, 4);
        g_Client.Var(kDuelOpponentVal) = code;
        g_Client.msg.System(Str(1719 + code));
        return;
    }
    case 114: { // dump L.2742 -- TWO independent gates (no message): name1==me -> reset;
                // name3==me -> arms the "in progress" state (same values as 102/108).
        if (len < 47) return;
        const std::string name1 = ReadName13(payload, 4);
        const std::string name3 = ReadName13(payload, 30);
        int32_t v = 0; std::memcpy(&v, payload + 43, 4);
        if (g_World.self.localPlayerName == name1) {
            g_Client.Var(kDuelStateA) = 0;
            g_Client.Var(kDuelStateB) = 0;
            // TODO(@duel_stringinit_114): 2x Crt_StringInit() with no identifiable target, omitted.
            g_Client.Var(kDuelField16747F0) = v;
        }
        if (g_World.self.localPlayerName == name3) {
            g_Client.Var(kDuelStateA) = 2;
            g_Client.Var(kDuelStateB) = 2;
        }
        return;
    }
    case 115: // dump L.2761 -- twin of 111 (str2322 instead of str1671).
        ApplyBranchMasteryNotice(payload, len, 2322);
        return;
    default:
        // 110 (family "branch" 96-100, out of scope for this mission) and 113 fall here --
        // faithful no-op. NB: since W11, dword_16746A8 IS populated (guild cases 1/4/6 +
        // 102/107), so 113's gate `!Crt_Strcmp(dword_16746A8, v686)` would now be
        // evaluable; 113 remains unported nonetheless (out of scope, cf. header comment).
        return;
    }
}

} // namespace wed_detail
} // namespace ts2::game

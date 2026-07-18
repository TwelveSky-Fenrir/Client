// Net/WorldEntityDispatch_Internal.h — shared plumbing for the Net/WorldEntityDispatch.*
// split family (WorldEntityDispatch.cpp + _ComboFamilies/_BranchSkill/_Element/_Special/
// _War.cpp). Included only by that family; not part of the public Net/WorldEntityDispatch.h
// API. Two kinds of content:
//   - inline/constexpr utilities genuinely shared by 2+ split files (address constants,
//     ComboFamilyCtx + kFamily1/2/3, small helper functions) — internal linkage per TU via
//     the anonymous namespace, same convention as Game/AnimationTick_Internal.h.
//   - plain forward declarations (namespace ts2::game::wed_detail, external linkage) for the
//     per-sub-opcode-family entry points: each is DEFINED in exactly one of the 5 split .cpp
//     files and CALLED from the top-level switch (ApplyWorldEntityDispatch), which stays in
//     WorldEntityDispatch.cpp — an ordinary multi-TU declare/define split, required because
//     the switch itself must stay a single function (cf. WorldEntityDispatch.h family map).
#pragma once
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include "Game/ClientRuntime.h"
#include "Game/GameState.h"
#include "Game/MapWarp.h"
#include "Game/MotionPoolsCoordResolver.h" // g_CoordResolver (WarpToOwnFactionTown)

namespace ts2::game {

// Combo family parameters (9 identical slots, cf. Net/WorldEntityDispatch.h). Must have
// external linkage (NOT in the anonymous namespace below): it's used as a by-reference
// parameter type in the wed_detail cross-TU declarations further down, and an
// internal-linkage parameter type there would make the function's identity TU-local
// (MSVC C5046), breaking the link between WorldEntityDispatch.cpp and
// WorldEntityDispatch_ComboFamilies.cpp.
struct ComboFamilyCtx {
    int      weaponType;      // Skill_GetComboMotionId(weaponType, idx) argument
    uint32_t stateAddr;       // dword_1685EAC / dword_1685F14 (state[idx])
    uint32_t stampAddr;       // dword_1685EE0 / dword_1685F2C (timestamp[idx])
    uint32_t chargeFlag1Addr; // slot4: dword_1675BAC / dword_1675BCC
    uint32_t chargeFlag0Addr; // slot4: dword_1675BB0 / dword_1675BD0
    bool     writeHalfDur;    // slot4: family 1 writes dword_1675BB4=arg2/2 ; family 2 does not (confirmed asymmetry)
    int      strSlot6;        // StrTable005 id of the slot6 message (860 family 1, 235 family 2)
};

namespace {

// g_SelfMorphNpcId 0x1675A98 — id of the local player's current action/posture/"morph"
// (cf. Game/SkillCombat.h: CombatMorphState::currentActionId, same address). Read-only here
// via the Var() escape hatch (no CombatMorphState instance is exposed to network handlers
// yet). Used across every split file.
constexpr uint32_t kSelfMorphNpcId = 0x1675A98;

// dword_16747BC — rebirth-tier counter (cf. Game/SkillCombat.h::CombatMorphState::
// rebirthTier, SAME address). Read-only here via the Var() escape hatch, same convention as
// kSelfMorphNpcId (already written by Net/CharStatDeltaDispatch.cpp through that same escape
// hatch — no duplicate storage introduced here). Consumed by the file-local
// ComboSkillAvailable()/SpecialSkillUsable() gates in _ComboFamilies.cpp/_Special.cpp.
constexpr uint32_t kRebirthTierAddr = 0x16747BC;

// Shared charge bar (combo slot 4), common to combo families 1/2/3/4
// (WorldEntityDispatch_ComboFamilies.cpp) AND the war-event-342 arm at sub-opcode 804
// (ApplyWarEvent800to807, WorldEntityDispatch_War.cpp) — cross-file, hence promoted here.
constexpr uint32_t kChargeArmedTimer = 0x1675BA4; // dword_1675BA4
constexpr uint32_t kChargeElapsed    = 0x1675BA8; // flt_1675BA8

// Confirmed combo families (state + daily timestamp per skill index) — feed the
// ComboFamilyCtx instances below (kFamily1/2/3 are consumed only by _ComboFamilies.cpp, but
// are explicitly hosted here per the split plan).
constexpr uint32_t kFam1State = 0x1685EAC, kFam1Stamp = 0x1685EE0;
constexpr uint32_t kFam2State = 0x1685F14, kFam2Stamp = 0x1685F2C;
constexpr uint32_t kFam3State = 0x1685F44, kFam3Stamp = 0x1685F6C; // family 3 (sub-opcodes 19..30)
constexpr uint32_t kFam1ChargeFlag1 = 0x1675BAC, kFam1ChargeFlag0 = 0x1675BB0;
constexpr uint32_t kFam2ChargeFlag1 = 0x1675BCC, kFam2ChargeFlag0 = 0x1675BD0;
constexpr uint32_t kFam3ChargeFlag1 = 0x1675BD4, kFam3ChargeFlag0 = 0x1675BD8; // no half-duration (like family 2)

// kFamily1/2/3 instantiate ComboFamilyCtx, defined above (outside this anonymous namespace,
// for external linkage — see the comment at its definition).
constexpr ComboFamilyCtx kFamily1{1, kFam1State, kFam1Stamp, kFam1ChargeFlag1, kFam1ChargeFlag0, true,  860};
constexpr ComboFamilyCtx kFamily2{2, kFam2State, kFam2Stamp, kFam2ChargeFlag1, kFam2ChargeFlag0, false, 235};
// Family 3: slots 1..6 (sub-opcodes 19..24) and 7..9 (28..30) only — cf.
// ApplyFamily3TagSlot/Slot26/Slot27 (WorldEntityDispatch_ComboFamilies.cpp) for the 3
// sub-cases 25..27 that sit between slot6 and slot7 with NO analogue in families 1/2
// (confirmed by full disassembly reading: EA 0x495d66..0x4960bc).
constexpr ComboFamilyCtx kFamily3{3, kFam3State, kFam3Stamp, kFam3ChargeFlag1, kFam3ChargeFlag0, false, 235};

// StrTable003_Get(dword_84A6A8, comboMotionId) — display name of the skill/motion. Same
// placeholder convention as the rest of the code (cf. GameHandlers_ChatSocial.cpp,
// "Str_GetClassLabel(param) — placeholder"): 003.DAT is not loaded/indexed by motion id here
// -> Str() (StrTable005, stable "#<id>" text) stands in as a placeholder for now.
inline std::string SkillName(int comboMotionId) { return Str(comboMotionId); }

// Str_GetClassLabel 0x557A98 — EXACT transcription (verified by direct idaTs2
// decompilation): Str(75+id) for id in [0,3], empty string otherwise (faithful to the
// original &String fallback, a constant empty string).
inline std::string ClassLabel(int32_t id) {
    return (id >= 0 && id <= 3) ? Str(75 + id) : std::string();
}

// BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver) — local player's
// return to their faction town, called IDENTICALLY 24 times across this split family
// (confirmed by exhaustive grep before factoring, including inside the skill-branch family).
inline void WarpToOwnFactionTown() {
    BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver);
}

// Time_GetMonthDayInt (EA not captured in this pass): "current day" timestamp used for the
// daily skill cooldown (dword_1685EE0/F2C[idx]). Exact encoding (month*100+day, by analogy)
// NOT confirmed bit-for-bit against the binary.
// TODO(@Time_GetMonthDayInt): find the original EA and verify the exact encoding.
inline int32_t Time_GetMonthDayInt() {
    const std::time_t t = std::time(nullptr);
    std::tm tmv{};
    localtime_s(&tmv, &t);
    return (tmv.tm_mon + 1) * 100 + tmv.tm_mday;
}

// 13-byte name buffers written by this module — faithful `strcpy(dest, src)` helper.
// FALSE FRIEND RESOLVED (wave W11): `Crt_StringInit 0x75CAB0` is NOT a std::string
// constructor (despite its IDA-hooked name) — it is `strcpy(dest, src)`: an alternate entry
// point into `Crt_Strcat 0x75CAC0` that skips the end-of-string scan (0x75CAB1 `mov edi,
// [esp+4+arg_0]` = dest as-is; shared body at 0x75CB25 `mov ecx,[esp+4+arg_4]` = src).
// `offset String` 0x7EC95F has byte 0 == 0 -> empty string -> "strcpy(dest, String)" = clear.
// ASSUMED GAP: strcpy leaves the TAIL of dest untouched; here the tail is zero-filled up to
// 13. Not observable (every reader is strcmp/%s, stopping at the first NUL) and
// NUL-terminating, cf. the established idiom in GameHandlers_ChatSocial.cpp:294-299.
// Cross-file (used by both WorldEntityDispatch_War.cpp and WorldEntityDispatch_BranchSkill.cpp),
// hence promoted here.
inline void BlobStrcpy13(uint32_t addr, const char* src) {
    auto& b = g_Client.Blob(addr, 13);
    size_t n = 0;
    while (n < 13 && src[n] != 0) ++n;   // strcpy: stop at the first NUL
    b.assign(13, 0);
    std::memcpy(b.data(), src, n);
}

} // namespace

// Cross-file-needed entry points: each is DEFINED in exactly one WorldEntityDispatch_*.cpp
// (see that file's own header comment for ownership) and called from the top-level switch in
// WorldEntityDispatch.cpp. Declared here (external linkage, namespace wed_detail) rather than
// in the public Net/WorldEntityDispatch.h, since they are not part of the module's public API
// surface (only ApplyWorldEntityDispatch is).
namespace wed_detail {

// -- WorldEntityDispatch_ComboFamilies.cpp --
void ApplyComboFamilySlot(const ComboFamilyCtx& fam, int slot, uint32_t idx, uint32_t arg2);
void ApplyFamily3TagSlot(uint32_t idx, uint32_t classBranch, const char* tag13);
void ApplyFamily3Slot26(uint32_t idx);
void ApplyFamily3Slot27(uint32_t idx, uint32_t arg2);
void ApplyFamily3Slot28(uint32_t idx, uint32_t elt, const char* tag13);
void ApplyComboFamily4Slot(int slot, uint32_t idx, uint32_t arg2);

// -- WorldEntityDispatch_BranchSkill.cpp --
void ApplyExclusionToggleSlot(uint32_t branch, uint32_t type, uint32_t value);
void ApplyBranchProbe(uint32_t branch, uint32_t type);
void ApplyBranchLabel418(uint32_t branch, uint32_t type);
void ApplyBranchLabel420(uint32_t branch, uint32_t type, int32_t stateValue);
void ApplyBranchLabel422(uint32_t branch, uint32_t type);
void ApplyBranchFlagsSlot(uint32_t branch, uint32_t type, int32_t stateValue,
                           uint32_t flagAddr1, uint32_t flagAddr2);
void ApplyBranchSoundOnlySlot(uint32_t branch, uint32_t type, int32_t stateValue);
void ApplyBranchPoll(uint32_t branch, uint32_t type, uint32_t arg3);
void ApplyBranchArm(uint32_t branch, uint32_t type, int32_t stateValue, int strId, int floatType);
void ApplyDuelBranchFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len);

// -- WorldEntityDispatch_Element.cpp --
void ApplyElementLoadoutFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void ApplyAlliancePairFamily(uint32_t subOpcode, uint32_t elemA, uint32_t elemB,
                              const uint8_t* payload, uint32_t len);
void ApplyAllianceLabelFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void ApplyAllianceTierReset(int32_t value);
void ApplyScoreboardFullReset();
void Apply51(const uint8_t* payload, uint32_t len);
void Apply57(const uint8_t* payload, uint32_t len);
void Apply58(uint32_t classId, uint32_t slotIdx);
void Apply59(uint32_t classId, uint32_t slotIdx, int32_t delta);
void ApplyClassTagNotice(uint32_t classId, const char* tag13, int strId);
void Apply62(uint32_t elt, int32_t value);
void ApplyElementNotifyFamily(uint32_t subOpcode, uint32_t elt, uint32_t count);
void Apply301(uint32_t valueId, uint32_t value);
void Apply302(uint32_t idx, int32_t value);
void Apply500();

// -- WorldEntityDispatch_Special.cpp --
void ApplyNotice401();
void ApplySpecialFamilySlot(int slot, uint32_t idx, uint32_t arg2);
void ApplyBuffFamilySlot(int slot, uint32_t idx, const char* tag13);
void ApplyMiscFlagSlot(uint32_t idx, int32_t value);
void ApplyIndividualArenaFamily(uint32_t subOpcode, uint32_t idx);
void Apply418(int32_t count);
void Apply419(const uint8_t* payload, uint32_t len);
void Apply420(int32_t count);
void Apply421();
void Apply422Or424();
void ApplyZone291CountNotice(int32_t count);

// -- WorldEntityDispatch_War.cpp --
void ApplyLevelBonusFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void Apply620(uint32_t count);
void Apply621();
void Apply622();
void Apply624(uint32_t classId);
void Apply625(uint32_t count);
void Apply626();
void Apply628(const uint8_t* payload, uint32_t len);
void ApplyWarStageFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void ApplyArenaFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void ApplyClassTagFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void ApplySiegeStage2Family(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void ApplyRankFamily740(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void Apply751(const uint8_t* payload, uint32_t len);
void ApplyRankTable(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void Apply754(const uint8_t* payload, uint32_t len);
void ApplyCastAnnounce771to774(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void ApplySimpleSkillNotice(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void Apply791(const uint8_t* payload, uint32_t len);
void Apply792to794(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void Apply795(const uint8_t* payload, uint32_t len);
void ApplyWarEvent780to786(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void ApplyWarEvent800to807(uint32_t subOpcode, const uint8_t* payload, uint32_t len);
void Apply901to903(uint32_t subOpcode);

} // namespace wed_detail
} // namespace ts2::game

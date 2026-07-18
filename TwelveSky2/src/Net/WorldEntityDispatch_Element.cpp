// Net/WorldEntityDispatch_Element.cpp — split of Net_OnWorldEntityDispatch (opcode 0x5e, EA
// 0x494870): the local player's "element loadout" family (33..45), alliance element pairing
// (46/47) + label tail-merge (48..50), the "scoreboard"/alliance-tier notices (51..62), the
// element-notify family (611..615), and two small self-contained sub-opcodes that share
// memory with the element-loadout reset block (301/302) plus the alliance/town-return block
// (500). See Net/WorldEntityDispatch.h for the full sub-opcode map.
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
// Sub-opcodes 51..62 (mission "sub-range 51-75", 2026-07-14, RE/dispatch_494870_full.c
// L.1970-2229) — FULLY AUDITED by direct decompilation. DISTINCT from the alliance pairing
// 46/47 (g_AlliancePairTable, out of this range) AND from the skill-branch family 63+
// (dword_1685F94, WorldEntityDispatch_BranchSkill.cpp): a "ranking/scoreboard" system by
// class (0..3) and slot (0..9), plus 4 independent notices adjacent to it (51, 60, 61, 62)
// that touch none of the scoreboard state:
//   51  notice "[class] [tag]str259" — class=payload+4, 13-byte tag=payload+8 (NOT +12
//       standard, same irregularity as Apply419/ApplyClassTagFamily). No state.
//   52..55  "alliance-tier reset" (dword_1685E74[0]/78/7C/80 = 1/2/3/4). Each also calls
//       StrTable005_Get/grid cleanups (52..54) or a "best slot" search v729 (55) — the final
//       HUD_ShowFloatingMessage of EACH sub-case is CONFIRMED DEAD (v677 never filled —
//       Crt_StringInit() clears it right before with no intervening
//       Crt_Vsnprintf/Crt_Strcat — and 55's v729 is never reused afterward), same convention
//       as the dead store already identified at 803 (cf. the ApplyWarEvent800to807 header
//       comment) — BUT 52 ALONE has a REAL side effect (NOT dead, re-checked 2026-07-14): a
//       nested i=0..4/j=0..10 loop that zeroes ALL THREE scoreboard tables IN FULL
//       (dword_168653C/16865DC/168667C, the SAME as 57/58/59 below) — verified line by line
//       on RE/dispatch_494870_full.c L.2001-2019 (the per-slot Crt_StringInit() inside the
//       loop most likely targets the name buffer byte_1686334[130*i+13*j], though NOT
//       confirmed from the pseudocode alone — OUT OF SCOPE, same limit as the rest of this
//       file for that buffer). 53/54/55/56 do NOT have this loop (verified: 53/54 = just the
//       4 scalar writes; 55 = a DIFFERENT loop, dead, cf. above). State
//       (dword_1685E74/78/7C/80) + full scoreboard RAZ (52 only) are wired — cf.
//       ApplyAllianceTierReset/ApplyScoreboardFullReset.
//   56  same state reset to 0 (full reset), no side call.
//   57  registers a ranking slot (class=+4, slotIdx 0..9=+8, 13-byte tag=+12 [standard],
//       score=+25, rank=+29): message "[class] [tag]str766" (chat) THEN
//       dword_168653C[10*class+slotIdx]=score, dword_16865DC[...]=rank, dword_168667C[...]=0.
//   58  unregisters a slot: the original message "[class] [<stored name>]str767" reads
//       byte_1686334[130*class+13*slotIdx] — a NAME buffer CONFIRMED NEVER WRITTEN in this
//       function (exhaustive grep of RE/dispatch_494870_full.c, same limit as byte_1686138
//       in 422/424/425-428, cf. WorldEntityDispatch_Special.cpp) -> message OMITTED, only the
//       state (the 3 tables zeroed for that slot) is wired.
//   59  increments the slot counter (dword_168667C[10*class+slotIdx] += delta=+12).
//   60/61  notice "[class] [tag]str773/774" — class=+4, tag 13o=+12 [standard]. v720=+8 read
//       but never reused (same convention as kFam4State case7/arg2). No state.
//   62  conditional notice: gate = g_LocalElement==elt(+4) -> message "str812(value)"
//       (value=+8) — chat only, no state.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kAllianceTierA     = 0x1685E74; // dword_1685E74[0] (scalar despite the "table" name)
constexpr uint32_t kAllianceTierB     = 0x1685E78;
constexpr uint32_t kAllianceTierC     = 0x1685E7C;
constexpr uint32_t kAllianceTierD     = 0x1685E80;
constexpr uint32_t kScoreboardScore   = 0x168653C; // [10*class+slotIdx]
constexpr uint32_t kScoreboardRank    = 0x16865DC; // [10*class+slotIdx]
constexpr uint32_t kScoreboardCounter = 0x168667C; // [10*class+slotIdx]
} // namespace

// 51: class+tag at an irregular offset (+8, not +12) -- self-contained, same style as Apply419.
void Apply51(const uint8_t* payload, uint32_t len) {
    if (len < 21) return; // payload+8..+20 (13 o of tag) must be available
    uint32_t classId = 0;
    std::memcpy(&classId, payload + 4, 4);
    char tag[14] = {};
    std::memcpy(tag, payload + 8, 13);
    g_Client.msg.System("[" + ClassLabel(static_cast<int32_t>(classId)) + "] [" + tag + "]" + Str(259));
}

// 52..56: tier reset (dword_1685E74/78/7C/80 = value). value=0 for 56.
void ApplyAllianceTierReset(int32_t value) {
    g_Client.Var(kAllianceTierA) = value;
    g_Client.Var(kAllianceTierB) = value;
    g_Client.Var(kAllianceTierC) = value;
    g_Client.Var(kAllianceTierD) = value;
}

// 52 ALONE (besides the tier above): full RAZ of the scoreboard, all 3 tables for the 4
// classes x 10 slots -- cf. header comment (a REAL loop, unlike the dead final HUD).
void ApplyScoreboardFullReset() {
    for (uint32_t classId = 0; classId < 4; ++classId) {
        for (uint32_t slotIdx = 0; slotIdx < 10; ++slotIdx) {
            const uint32_t slot = 10 * classId + slotIdx;
            g_Client.Var(kScoreboardScore   + 4 * slot) = 0;
            g_Client.Var(kScoreboardRank    + 4 * slot) = 0;
            g_Client.Var(kScoreboardCounter + 4 * slot) = 0;
        }
    }
}

// 57: registers a ranking slot -- self-contained (score/rank at an offset not shared with
// the standard idx/arg2/tag13, cf. header comment).
void Apply57(const uint8_t* payload, uint32_t len) {
    if (len < 33) return; // payload+25..+32 (score+rank) must be available
    uint32_t classId = 0, slotIdx = 0, score = 0, rank = 0;
    std::memcpy(&classId, payload + 4, 4);
    std::memcpy(&slotIdx, payload + 8, 4);
    char tag[14] = {};
    std::memcpy(tag, payload + 12, 13); // standard offset (tag13)
    std::memcpy(&score, payload + 25, 4);
    std::memcpy(&rank,  payload + 29, 4);
    g_Client.msg.System("[" + ClassLabel(static_cast<int32_t>(classId)) + "] [" + tag + "]" + Str(766));
    const uint32_t slot = 10 * classId + slotIdx;
    g_Client.Var(kScoreboardScore   + 4 * slot) = static_cast<int32_t>(score);
    g_Client.Var(kScoreboardRank    + 4 * slot) = static_cast<int32_t>(rank);
    g_Client.Var(kScoreboardCounter + 4 * slot) = 0;
}

// 58: unregister -- message omitted (name buffer byte_1686334 never written here, cf. header comment).
void Apply58(uint32_t classId, uint32_t slotIdx) {
    const uint32_t slot = 10 * classId + slotIdx;
    g_Client.Var(kScoreboardScore   + 4 * slot) = 0;
    g_Client.Var(kScoreboardRank    + 4 * slot) = 0;
    g_Client.Var(kScoreboardCounter + 4 * slot) = 0;
}

// 59: slot counter increment.
void Apply59(uint32_t classId, uint32_t slotIdx, int32_t delta) {
    const uint32_t slot = 10 * classId + slotIdx;
    g_Client.Var(kScoreboardCounter + 4 * slot) = g_Client.VarGet(kScoreboardCounter + 4 * slot) + delta;
}

// 60/61: standard class+tag notice (tag13 = payload+12), reusable as-is by the dispatcher
// (idx/tag13 already extracted in common).
void ApplyClassTagNotice(uint32_t classId, const char* tag13, int strId) {
    g_Client.msg.System("[" + ClassLabel(static_cast<int32_t>(classId)) + "] [" + tag13 + "]" + Str(strId));
}

// 62: notice conditioned on the local element.
void Apply62(uint32_t elt, int32_t value) {
    if (static_cast<int32_t>(elt) != g_World.self.element) return;
    g_Client.msg.System(Str(812) + "(" + std::to_string(value) + ")");
}

// ---------------------------------------------------------------------------
// "Element notify" family (dump L.3800-3850, sub-opcodes 611..615) — same shape as the
// simple slots already wired elsewhere: idx=element (payload+4), gate = element==
// g_World.self.element, optional message with a count (payload+8).
// ---------------------------------------------------------------------------
void ApplyElementNotifyFamily(uint32_t subOpcode, uint32_t elt, uint32_t count) {
    if (static_cast<int32_t>(elt) != g_World.self.element) return;
    std::string s;
    switch (subOpcode) {
    case 611: s = std::to_string(count) + " " + Str(1859); break;
    case 612: s = Str(1860); break;
    case 613: s = Str(1862); break;
    case 614: s = Str(1861); break;
    case 615: s = std::to_string(count) + " " + Str(1944); break;
    default: return;
    }
    g_Client.msg.Floating(0, 0, s);
    g_Client.msg.System(s);
}

// ---------------------------------------------------------------------------
// Sub-opcode 301 (dump, "various float/int settings") and 302 (writes the SAME memory as
// ApplyElementLoadoutFamily's case 45 reset block, kElemResetB0..B3 = 0x16860B0..BC — hence
// colocated here rather than with the Special/Buff sub-opcodes it sits next to in the
// original file). Str(75..78) class labels in 302 match the file's usual branch/class
// convention.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kMiscFloat21 = 0x1686070, kMiscFloat22 = 0x1686074, kMiscFloat23 = 0x1686078,
                    kMiscFloat24 = 0x168607C, kMiscFloat31 = 0x1686080, kMiscFloat32 = 0x1686084,
                    kMiscFloat33 = 0x1686088, kMiscFloat34 = 0x168608C, kMiscFloat41 = 0x1686090,
                    kMiscFloat42 = 0x1686094, kMiscFloat43 = 0x1686098, kMiscFloat44 = 0x168609C;
constexpr uint32_t kMiscInt51   = 0x16860A0, kMiscInt52   = 0x16860A4, kMiscInt53   = 0x16860A8,
                    kMiscInt54   = 0x16860AC;
} // namespace

void Apply301(uint32_t valueId, uint32_t value) {
    const float scaled = static_cast<float>(value) * 0.1f;
    switch (valueId) {
    case 21: g_Client.VarF(kMiscFloat21) = scaled; break;
    case 22: g_Client.VarF(kMiscFloat22) = scaled; break;
    case 23: g_Client.VarF(kMiscFloat23) = scaled; break;
    case 24: g_Client.VarF(kMiscFloat24) = scaled; break;
    case 31: g_Client.VarF(kMiscFloat31) = scaled; break;
    case 32: g_Client.VarF(kMiscFloat32) = scaled; break;
    case 33: g_Client.VarF(kMiscFloat33) = scaled; break;
    case 34: g_Client.VarF(kMiscFloat34) = scaled; break;
    case 41: g_Client.VarF(kMiscFloat41) = scaled; break;
    case 42: g_Client.VarF(kMiscFloat42) = scaled; break;
    case 43: g_Client.VarF(kMiscFloat43) = scaled; break;
    case 44: g_Client.VarF(kMiscFloat44) = scaled; break;
    case 51: g_Client.Var(kMiscInt51) = static_cast<int32_t>(value); break;
    case 52: g_Client.Var(kMiscInt52) = static_cast<int32_t>(value); break;
    case 53: g_Client.Var(kMiscInt53) = static_cast<int32_t>(value); break;
    case 54: g_Client.Var(kMiscInt54) = static_cast<int32_t>(value); break;
    default: return;
    }
}

void Apply302(uint32_t idx, int32_t value) {
    g_Client.Var(0x16860B0u + 4u * idx) = value;

    std::string text;
    switch (idx) {
    case 0: text = "[" + Str(75) + "]"; break;
    case 1: text = "[" + Str(76) + "]"; break;
    case 2: text = "[" + Str(77) + "]"; break;
    case 3: text = "[" + Str(78) + "]"; break;
    default: break;
    }
    switch (value) {
    case 0: text += " " + Str(934); break;
    case 1: text += " " + Str(930); break;
    case 2: text += " " + Str(931); break;
    case 3: text += " " + Str(932); break;
    case 4: text += " " + Str(933); break;
    default: break;
    }
    g_Client.msg.System(text);
}

// ---------------------------------------------------------------------------
// Sub-opcode 500 -- alliance/town-return block; the observable part is wired.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kPerElementCounterAddr = 0x1685E44; // g_PerElementCounter[2*elem]
constexpr uint32_t kPerElementFlagAddr    = 0x1685E48; // dword_1685E48[2*elem]
constexpr uint32_t kAllySlot46ArmFlag      = 0x1675BFC; // kMorphRows[2].flagAddr
constexpr uint32_t kAllySlot46ArmFrame     = 0x1675C00; // kMorphRows[2].frameAddr
constexpr uint32_t kAllySlot47ArmFlag      = 0x1675C04; // kMorphRows[3].flagAddr
constexpr uint32_t kAllySlot47ArmFrame     = 0x1675C08; // kMorphRows[3].frameAddr
} // namespace

void Apply500() {
    const int32_t count = 0; // v636 (displayed value)
    for (uint32_t k = 0; k < 4; ++k) {
        g_Client.Var(kPerElementCounterAddr + 8u * k) = count;
        g_Client.Var(kPerElementFlagAddr + 8u * k)    = 1;
    }

    const ElementPairTable pairs = Combat_ReadLocalElementPairs();
    const int32_t v633 = pairs.Paired(g_World.self.element);

    g_Client.Var(kElementPairAAddr) = -1;
    g_Client.Var(kElementPairBAddr) = -1;
    g_Client.Var(kElementPairCAddr) = -1;
    g_Client.Var(kElementPairDAddr) = -1;

    const std::string s = Str(1670);
    g_Client.msg.System(s);
    g_Client.msg.Floating(10, 2, s);

    if (g_Client.VarGet(kSelfMorphNpcId) == 37) {
        g_Client.Var(kAllySlot47ArmFlag)   = 1;
        g_Client.VarF(kAllySlot47ArmFrame) = 0.0f;
    }

    switch (v633) {
    case 0:
        switch (g_Client.VarGet(kSelfMorphNpcId)) {
        case 1: case 2: case 3: case 4: case 16: case 17: case 18: case 40: case 43: case 46:
        case 56: case 59: case 62: case 63: case 64: case 76: case 80: case 91: case 95:
        case 202: case 206:
            BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
            break;
        default:
            break;
        }
        break;
    case 1:
        switch (g_Client.VarGet(kSelfMorphNpcId)) {
        case 6: case 7: case 8: case 9: case 22: case 23: case 24: case 41: case 44: case 47:
        case 57: case 60: case 65: case 66: case 67: case 77: case 81: case 92: case 96:
        case 203: case 207:
            BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
            break;
        default:
            break;
        }
        break;
    case 2:
        switch (g_Client.VarGet(kSelfMorphNpcId)) {
        case 11: case 12: case 13: case 14: case 28: case 29: case 30: case 42: case 45:
        case 48: case 58: case 61: case 68: case 69: case 70: case 78: case 83: case 93:
        case 97: case 204: case 208:
            BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
            break;
        default:
            break;
        }
        break;
    case 3:
        switch (g_Client.VarGet(kSelfMorphNpcId)) {
        case 79: case 83: case 94: case 98: case 140: case 141: case 142: case 143: case 205:
        case 209:
            BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }

    const int32_t morph = g_Client.VarGet(kSelfMorphNpcId);
    if (morph > 145) {
        if (morph == 310) BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
    } else if (morph >= 144 || morph == 39 || morph == 74) {
        BeginWarpToFactionTown(static_cast<int32_t>(g_World.self.element), false, 0, &g_CoordResolver);
    }
}

// ---------------------------------------------------------------------------
// Sub-opcodes 33..45 (dump L.1328-1675, mission "element loadout" 2026-07-14) — notices/state
// of the local player's element "loadout" (dword_1685E10/18/1C/20/24/28, dword_16860B0..BC) —
// StrTable005 ids 271..288; ALL of 33..45 write BOTH the floating HUD and a system line
// (verified instruction by instruction — unlike 46..50 below, which write ONLY the HUD).
// Warp conditioned on g_SelfMorphNpcId (35=38, 38=38/39/74/144-145/310, 40=table
// {50,52,85,99,100,170,196}). No relation to Skill_GetComboMotionId/GetSpecialMotionId/
// GetBuffMotionId nor to Char_GetPairedElement: an independent system, confirmed by full
// disassembly reading.
//   33 (dump L.1328): count=payload+4 -> "[count]str271", HUD cat2/type1. No state.
//   34 (dump L.1335): no payload read. "str272" only, HUD cat2/type2. No state.
//   35 (dump L.1341): class 0..3=payload+4, tag13=payload+8 (NOT +12 -- own offset, verified
//       esp-to-esp) -> "[class] [tag] str273", HUD cat2/type4. Gate CALCULABLE (morph==38) ->
//       arms an OWN timer (dword_1675BEC/flt_1675BF0), UNRELATED to the combo families'
//       shared charge bar. No state written.
//   36 (dump L.1377): no payload read. "str274" only, HUD cat2/type4. No state.
//   37 (dump L.1383): count=payload+4 -> "[count]str275", HUD cat2/type4. No state.
//   38 (dump L.1390): class 0..3=payload+4, tag13=payload+8 (same non-standard offset as
//       35) -> STATE WRITTEN UNCONDITIONALLY (dword_1685E08=class, dword_1685E0C=current
//       day), message "[class] [tag] str276", HUD cat2/type3. Then CALCULABLE warp:
//       morph==38 -> arms an OWN timer distinct from 35 (dword_1675BF4/flt_1675BF8, no
//       warp); morph in {39,74,144,145} -> return to faction town; morph==310 AND
//       g_LocalElement != class AND !dword_16757A8 (a re-entrancy guard never written
//       elsewhere, read-only here via Var(), same status as g_SelfCharInvBlock) -> return
//       to faction town; any other morph -> nothing.
//   39 (dump L.1448): count=payload+4 -> "[count]str277", HUD cat3/type1. No state.
//   40 (dump L.1455): no payload read. Loadout RESET (hardcoded default values, NOT a
//       uniform pattern): dword_1685E10=1, g_ElementLoadout (0x1685E14)=0, dword_1685E18=1,
//       dword_1685E1C=2, dword_1685E20=3; message "str278" (HUD cat3/type2); THEN
//       UNCONDITIONAL warp if morph in {50,52,85,99,100,170,196} (hardcoded table); THEN
//       mastery reset: g_ElementMastery(0x1675680)=0, dword_1675678=0, dword_168746C=0.
//   41 (dump L.1483): selector 0..4=payload+4 (labels str279..283), class 0..3=payload+8,
//       tag13=payload+12 (STANDARD offset, unlike 35/38) -> "<label> [class] [tag] str284",
//       HUD cat3/type3. PURE notification (unlike 42, same label, which WRITES).
//   42 (dump L.1543): selector 0..4=payload+4, value(class 0..3)=payload+8 -> WRITES the
//       loadout slot designated by the selector (0->g_ElementLoadout, 1->dword_1685E18,
//       2->dword_1685E1C, 3->dword_1685E20, 4->dword_1685E24 + dword_1685E28=current day,
//       the ONLY slot with a timestamp); message "<label> [class] str285", HUD cat3/type4.
//   43 (dump L.1623): selector 0..4=payload+4 -> "<label> str286", HUD cat3/type5. No state.
//   44 (dump L.1658): count=payload+4 -> "[count]str287", HUD cat3/type6. No state.
//   45 (dump L.1665): no payload read. RESET: dword_1685E10=0, dword_16860B0/B4/B8/BC=0 (4
//       distinct fields, NOT the 1685E14..24 slots), message "str288", HUD cat3/type7.
// Precise EAs not re-captured individually -- reliable location = the dump (RE/
// dispatch_494870_full.c) line number cited per case.
// ---------------------------------------------------------------------------
namespace {
constexpr uint32_t kElemLoadoutFlag       = 0x1685E10; // "loadout armed" (40=1, 45=0)
constexpr uint32_t kElemLoadoutSlot0      = 0x1685E14; // == g_ElementLoadout
constexpr uint32_t kElemLoadoutSlot1      = 0x1685E18;
constexpr uint32_t kElemLoadoutSlot2      = 0x1685E1C;
constexpr uint32_t kElemLoadoutSlot3      = 0x1685E20;
constexpr uint32_t kElemLoadoutSlot4      = 0x1685E24;
constexpr uint32_t kElemLoadoutSlot4Stamp = 0x1685E28;
constexpr uint32_t kElemLastClass         = 0x1685E08; // 38: received class
constexpr uint32_t kElemLastStamp         = 0x1685E0C; // 38: current day
constexpr uint32_t kElemResetB0 = 0x16860B0, kElemResetB1 = 0x16860B4,
                    kElemResetB2 = 0x16860B8, kElemResetB3 = 0x16860BC; // 45
constexpr uint32_t kElemMastery     = 0x1675680; // == g_ElementMastery (40, reset)
constexpr uint32_t kElemMasteryAux1 = 0x1675678; // 40, reset
constexpr uint32_t kElemMasteryAux2 = 0x168746C; // 40, reset
constexpr uint32_t kElem35TimerFlag  = 0x1675BEC, kElem35TimerFrame = 0x1675BF0; // 35, gate morph==38
constexpr uint32_t kElem38TimerFlag  = 0x1675BF4, kElem38TimerFrame = 0x1675BF8; // 38, gate morph==38
constexpr uint32_t kElemWarpGuard310 = 0x16757A8; // 38, guard morph==310 (read-only here)

// Loadout component label (str279..283, selector 0..4) -- shared by 41/42/43.
inline std::string LoadoutComponentLabel(uint32_t selector) {
    static constexpr int kIds[5] = {279, 280, 281, 282, 283};
    return (selector <= 4) ? Str(kIds[selector]) : std::string();
}
} // namespace

void ApplyElementLoadoutFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    switch (subOpcode) {
    case 33: { // dump L.1328
        if (len < 8) return;
        int32_t count = 0; std::memcpy(&count, payload + 4, 4);
        const std::string s = "[" + std::to_string(count) + "]" + Str(271);
        g_Client.msg.Floating(2, 1, s);
        g_Client.msg.System(s);
        return;
    }
    case 34: { // dump L.1335
        const std::string s = Str(272);
        g_Client.msg.Floating(2, 2, s);
        g_Client.msg.System(s);
        return;
    }
    case 35: { // dump L.1341 -- tag at payload+8 (not +12).
        if (len < 21) return;
        int32_t cls = 0; std::memcpy(&cls, payload + 4, 4);
        char tag[14] = {}; std::memcpy(tag, payload + 8, 13);
        const std::string s = "[" + ClassLabel(cls) + "] [" + tag + "] " + Str(273);
        g_Client.msg.Floating(2, 4, s);
        g_Client.msg.System(s);
        if (g_Client.VarGet(kSelfMorphNpcId) == 38) {
            g_Client.Var(kElem35TimerFlag)   = 1;
            g_Client.VarF(kElem35TimerFrame) = 0.0f;
        }
        return;
    }
    case 36: { // dump L.1377
        const std::string s = Str(274);
        g_Client.msg.Floating(2, 4, s);
        g_Client.msg.System(s);
        return;
    }
    case 37: { // dump L.1383
        if (len < 8) return;
        int32_t count = 0; std::memcpy(&count, payload + 4, 4);
        const std::string s = "[" + std::to_string(count) + "]" + Str(275);
        g_Client.msg.Floating(2, 4, s);
        g_Client.msg.System(s);
        return;
    }
    case 38: { // dump L.1390 -- tag at payload+8 (like 35). State written BEFORE the message (faithful).
        if (len < 21) return;
        int32_t cls = 0; std::memcpy(&cls, payload + 4, 4);
        char tag[14] = {}; std::memcpy(tag, payload + 8, 13);
        g_Client.Var(kElemLastClass) = cls;
        g_Client.Var(kElemLastStamp) = Time_GetMonthDayInt();
        const std::string s = "[" + ClassLabel(cls) + "] [" + tag + "] " + Str(276);
        g_Client.msg.Floating(2, 3, s);
        g_Client.msg.System(s);
        const int32_t morph = g_Client.VarGet(kSelfMorphNpcId);
        if (morph == 38) {
            g_Client.Var(kElem38TimerFlag)   = 1;
            g_Client.VarF(kElem38TimerFrame) = 0.0f;
        } else if (morph == 39 || morph == 74 || morph == 144 || morph == 145) {
            BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver);
        } else if (morph == 310 && g_World.self.element != cls && !g_Client.VarGet(kElemWarpGuard310)) {
            BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver);
        }
        return;
    }
    case 39: { // dump L.1448
        if (len < 8) return;
        int32_t count = 0; std::memcpy(&count, payload + 4, 4);
        const std::string s = "[" + std::to_string(count) + "]" + Str(277);
        g_Client.msg.Floating(3, 1, s);
        g_Client.msg.System(s);
        return;
    }
    case 40: { // dump L.1455 -- loadout reset (hardcoded default values) + warp by morph
               // table + mastery reset. No payload read.
        g_Client.Var(kElemLoadoutFlag)  = 1;
        g_Client.Var(kElemLoadoutSlot0) = 0;
        g_Client.Var(kElemLoadoutSlot1) = 1;
        g_Client.Var(kElemLoadoutSlot2) = 2;
        g_Client.Var(kElemLoadoutSlot3) = 3;
        const std::string s = Str(278);
        g_Client.msg.Floating(3, 2, s);
        g_Client.msg.System(s);
        switch (static_cast<int32_t>(g_Client.VarGet(kSelfMorphNpcId))) {
        case 50: case 52: case 85: case 99: case 100: case 170: case 196:
            BeginWarpToFactionTown(g_World.self.element, false, 0, &g_CoordResolver);
            break;
        default:
            break;
        }
        g_Client.Var(kElemMastery)     = 0;
        g_Client.Var(kElemMasteryAux1) = 0;
        g_Client.Var(kElemMasteryAux2) = 0;
        return;
    }
    case 41: { // dump L.1483 -- tag at payload+12 (standard offset, unlike 35/38).
               // Pure notification (unlike 42, same label, which WRITES).
        if (len < 25) return;
        uint32_t selector = 0, cls = 0;
        std::memcpy(&selector, payload + 4, 4);
        std::memcpy(&cls, payload + 8, 4);
        char tag[14] = {}; std::memcpy(tag, payload + 12, 13);
        const std::string s = LoadoutComponentLabel(selector) + " [" + ClassLabel(static_cast<int32_t>(cls)) +
                               "] [" + tag + "] " + Str(284);
        g_Client.msg.Floating(3, 3, s);
        g_Client.msg.System(s);
        return;
    }
    case 42: { // dump L.1543 -- WRITES the loadout slot designated by the selector.
        if (len < 12) return;
        uint32_t selector = 0; std::memcpy(&selector, payload + 4, 4);
        int32_t value = 0; std::memcpy(&value, payload + 8, 4);
        switch (selector) {
        case 0: g_Client.Var(kElemLoadoutSlot0) = value; break;
        case 1: g_Client.Var(kElemLoadoutSlot1) = value; break;
        case 2: g_Client.Var(kElemLoadoutSlot2) = value; break;
        case 3: g_Client.Var(kElemLoadoutSlot3) = value; break;
        case 4:
            g_Client.Var(kElemLoadoutSlot4)      = value;
            g_Client.Var(kElemLoadoutSlot4Stamp) = Time_GetMonthDayInt();
            break;
        default:
            break;
        }
        const std::string s = LoadoutComponentLabel(selector) + " [" + ClassLabel(value) + "] " + Str(285);
        g_Client.msg.Floating(3, 4, s);
        g_Client.msg.System(s);
        return;
    }
    case 43: { // dump L.1623 -- pure notification, no state.
        if (len < 8) return;
        uint32_t selector = 0; std::memcpy(&selector, payload + 4, 4);
        const std::string s = LoadoutComponentLabel(selector) + " " + Str(286);
        g_Client.msg.Floating(3, 5, s);
        g_Client.msg.System(s);
        return;
    }
    case 44: { // dump L.1658
        if (len < 8) return;
        int32_t count = 0; std::memcpy(&count, payload + 4, 4);
        const std::string s = "[" + std::to_string(count) + "]" + Str(287);
        g_Client.msg.Floating(3, 6, s);
        g_Client.msg.System(s);
        return;
    }
    case 45: { // dump L.1665 -- reset (4 distinct fields, NOT the 1685E14..24 slots).
               // No payload read.
        g_Client.Var(kElemLoadoutFlag) = 0;
        g_Client.Var(kElemResetB0) = 0;
        g_Client.Var(kElemResetB1) = 0;
        g_Client.Var(kElemResetB2) = 0;
        g_Client.Var(kElemResetB3) = 0;
        const std::string s = Str(288);
        g_Client.msg.Floating(3, 7, s);
        g_Client.msg.System(s);
        return;
    }
    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Sub-opcodes 46 (EA 0x497ce4) / 47 (EA 0x497d76) — local player's ALLIANCE element pairing
// (mission "wire ElementPairTable", 2026-07-14, Docs/TS2_COMBAT_ELEMENT_GATING.md addendum).
// FIX to an earlier note (cf. Net/WorldEntityDispatch.h, former banner "46..47 ... DIFFERENT
// from Char_GetPairedElement"): confirmed by idaTs2 address resolution (list_globals) —
// g_AlliancePairTable IS EXACTLY g_LocalPlayerSheet+0x71C (0x1685E64) and dword_1685E68 IS
// EXACTLY g_LocalPlayerSheet+0x720 (0x1685E68), i.e. the `a`/`b` fields of
// Game/SkillCombat.h::ElementPairTable (this[455]/this[456] read by Char_GetPairedElement
// 0x557C00 on `this = g_LocalPlayerSheet`). So it IS the same memory: 46 ESTABLISHES a pair
// (free slot 0..1, i.e. a/b OR c/d); 47 CLEARS the matching pair (-1/-1). BEFORE this pass,
// Combat_ReadLocalElementPairs() (SkillCombat.cpp) read a g_Client.Var() that was NEVER
// WRITTEN -> ElementPairTable stayed {0,0,0,0} permanently (safe "no pair" fallback, but
// never updated by the server, cf. the fidelity note in SkillCombat.h). This block closes the
// WRITE-side gap (the decoding/gating formula is unchanged, cf.
// Game/ComboPickupTick.cpp::Combat_IsElementAllowedOnMap).
//   elemA = payload+4 (=idx), elemB = payload+8 (=arg2): the 2 elements to pair (46) or
//     unpair (47).
//   46 only: `useSlot2` picks the free slot by testing whether slot 1 (a/b, FIXED index 0,
//     regardless of the target slot) is occupied — occupied (a!=-1 || b!=-1) -> writes into
//     slot 2 (c/d).
//   47 only: `useSlot2` = TRUE unless slot 1 (a,b) matches (elemA,elemB) in either order —
//     then clears slot 1, otherwise slot 2. elemC = payload+12, elemD = payload+16: new
//     g_PerElementCounter[elemA]/[elemB] counters (NO identified consumer elsewhere in
//     ClientSource — written faithfully for future traceability, same addresses as the
//     disassembly). Floating notice floatType=10 (0xA), flag=1 (46) / flag=2 (47); if
//     g_SelfMorphNpcId==37, arms a generic timer ALREADY modeled by
//     Game/AnimationTick.cpp::kMorphRows[2] (46, 0x1675BFC/0x1675C00) or kMorphRows[3] (47,
//     0x1675C04/0x1675C08) — SAME convention as Apply421 (cf. kZone291TimerFlag in
//     WorldEntityDispatch_Special.cpp): armed here via the Var() escape hatch, consumed by
//     the existing generic timer engine.
// ---------------------------------------------------------------------------
// CLEANUP (audit "dispatch 0x5e" 2026-07-14) — the "unpair" write block is reproduced
// IDENTICALLY (same 6 Var() writes, same matchesSlot1/useSlot2 calculation) in sub-opcode 47
// (ApplyAlliancePairFamily) AND in the 49 tail-merge (ApplyAllianceLabelFamily, cf. its own
// "47 state tail-merge" comment) — verified line by line, written independently by 2
// different missions ("wire ElementPairTable" and "element loadout"). Factored here, NO
// behavior change (slot1A/slot1B are re-read fresh at each call, as every individual call
// site already did — no write intervenes between the read and the use in either caller, so
// this is bit-for-bit equivalent to the binary's ordering).
void ApplyAllianceUnpairState(uint32_t elemA, uint32_t elemB, uint32_t elemC, uint32_t elemD) {
    const int32_t slot1A = g_Client.VarGet(kElementPairAAddr);
    const int32_t slot1B = g_Client.VarGet(kElementPairBAddr);
    const bool matchesSlot1 =
        (slot1A == static_cast<int32_t>(elemA) && slot1B == static_cast<int32_t>(elemB)) ||
        (slot1B == static_cast<int32_t>(elemA) && slot1A == static_cast<int32_t>(elemB));
    const bool useSlot2 = !matchesSlot1;

    g_Client.Var(kPerElementCounterAddr + 8 * elemA) = static_cast<int32_t>(elemC);
    g_Client.Var(kPerElementFlagAddr    + 8 * elemA) = 1;
    g_Client.Var(kPerElementCounterAddr + 8 * elemB) = static_cast<int32_t>(elemD);
    g_Client.Var(kPerElementFlagAddr    + 8 * elemB) = 1;
    g_Client.Var(kElementPairAAddr + (useSlot2 ? 8u : 0u)) = -1;
    g_Client.Var(kElementPairBAddr + (useSlot2 ? 8u : 0u)) = -1;
}

void ApplyAlliancePairFamily(uint32_t subOpcode, uint32_t elemA, uint32_t elemB,
                              const uint8_t* payload, uint32_t len) {
    // Current slot 1 (fixed index 0), read BEFORE any write -- used to pick the target slot
    // in both sub-cases (faithful to the binary's ordering).
    const int32_t slot1A = g_Client.VarGet(kElementPairAAddr);
    const int32_t slot1B = g_Client.VarGet(kElementPairBAddr);

    if (subOpcode == 46) {
        const bool useSlot2 = (slot1A != -1 || slot1B != -1);
        g_Client.Var(kPerElementCounterAddr + 8 * elemA) = 0;
        g_Client.Var(kPerElementFlagAddr    + 8 * elemA) = 0;
        g_Client.Var(kPerElementCounterAddr + 8 * elemB) = 0;
        g_Client.Var(kPerElementFlagAddr    + 8 * elemB) = 0;
        g_Client.Var(kElementPairAAddr + (useSlot2 ? 8u : 0u)) = static_cast<int32_t>(elemA);
        g_Client.Var(kElementPairBAddr + (useSlot2 ? 8u : 0u)) = static_cast<int32_t>(elemB);

        const std::string s = "[" + ClassLabel(static_cast<int32_t>(elemA)) + "]" + Str(377) +
                               " [" + ClassLabel(static_cast<int32_t>(elemB)) + "]" + Str(378);
        g_Client.msg.Floating(10, 1, s);
        // FIX (2026-07-14, mission "element loadout" 33-50): NO Msg_AppendSystemLine here --
        // verified esp-to-esp on RE/dispatch_494870_full.c L.1676-1745 (case 46): only
        // HUD_ShowFloatingMessage is called, no Msg_AppendSystemLine anywhere in the case
        // body (the previous one since 33..45 is at L.1674, the next at L.1999/case 51) -- an
        // earlier pass had added a g_Client.msg.System(s) here by mistake (copied from
        // 33..45, which DO write both), removed to stay faithful.
        if (g_Client.VarGet(kSelfMorphNpcId) == 37) {
            g_Client.Var(kAllySlot46ArmFlag)   = 1;
            g_Client.VarF(kAllySlot46ArmFrame) = 0.0f;
        }
    } else { // 47
        uint32_t elemC = 0, elemD = 0;
        if (len >= 16) std::memcpy(&elemC, payload + 12, 4);
        if (len >= 20) std::memcpy(&elemD, payload + 16, 4);

        ApplyAllianceUnpairState(elemA, elemB, elemC, elemD);

        const std::string s = "[" + ClassLabel(static_cast<int32_t>(elemA)) + "]" + Str(377) +
                               " [" + ClassLabel(static_cast<int32_t>(elemB)) + "]" + Str(379);
        g_Client.msg.Floating(10, 2, s);
        // NB (verified against the disasm, dump L.1746-1818): NO Msg_AppendSystemLine here
        // EITHER (like 46 above -- cf. the fix note): only the floating HUD is written for
        // 46/47/48/49/50.
        if (g_Client.VarGet(kSelfMorphNpcId) == 37) {
            g_Client.Var(kAllySlot47ArmFlag)   = 1;
            g_Client.VarF(kAllySlot47ArmFrame) = 0.0f;
        }
    }
}

// ---------------------------------------------------------------------------
// Sub-opcodes 48..50 (dump L.1819-1969, tail-merge of the "alliance pairing" family 46/47
// above, mission "element loadout" 2026-07-14): 48/49 SHARE the label shape
// "<selector str380..383> [class] [tag] <suffix>" with 41/42 (ApplyElementLoadoutFamily) but
// remain DISTINCT (different suffixes/HUD: floatType=10 here vs 3 for 41/42) — confirmed by
// full disassembly reading. NONE of the three writes a system line (only the floating HUD is
// called — verified: Msg_AppendSystemLine absent from the binary for 48/49/50, a DIVERGENCE
// vs 33..45 which always write both):
//   48 (dump L.1819): selector 0..3=payload+4 (str380..383), class 0..3=payload+8,
//     tag13=payload+12 -> "<selector> [class] [tag] str284", HUD cat.10/type3. PURE
//     notification (no state written).
//   49 (dump L.1874): tail-merge of 47's STATE (RE-READS/RE-WRITES the EXACT same 4 fields
//     as ApplyAlliancePairFamily(47) -- elemA/B=payload+4/8, elemC/D=payload+12/16, SAME
//     kElementPairAAddr/B/kPerElementCounterAddr/kPerElementFlagAddr addresses -- verified
//     esp-to-esp), THEN adds selector 0..3=payload+20 (str380..383) + class
//     0..3=payload+24 + tag13=payload+28 -> "<selector> [class] [tag] str285", HUD
//     cat.10/type4. NO morph==37 gate/timer here (unlike 47 -- the `if (g_SelfMorphNpcId==37)`
//     block is ABSENT from 49's body in the disassembly).
//   50 (dump L.1941): selector 0..3=payload+4 (str380..383) -> "<selector> str286", HUD
//     cat.10/type5. Pure notification, no state.
// ---------------------------------------------------------------------------
namespace {
// "Alliance" label (str380..383, selector 0..3) -- distinct from the "loadout" label
// (str279..283, LoadoutComponentLabel) even though the message SHAPE is identical.
inline std::string AllianceComponentLabel(uint32_t selector) {
    static constexpr int kIds[4] = {380, 381, 382, 383};
    return (selector <= 3) ? Str(kIds[selector]) : std::string();
}
} // namespace

void ApplyAllianceLabelFamily(uint32_t subOpcode, const uint8_t* payload, uint32_t len) {
    switch (subOpcode) {
    case 48: { // dump L.1819
        if (len < 25) return;
        uint32_t selector = 0, cls = 0;
        std::memcpy(&selector, payload + 4, 4);
        std::memcpy(&cls, payload + 8, 4);
        char tag[14] = {}; std::memcpy(tag, payload + 12, 13);
        const std::string s = AllianceComponentLabel(selector) + " [" + ClassLabel(static_cast<int32_t>(cls)) +
                               "] [" + tag + "] " + Str(284);
        g_Client.msg.Floating(10, 3, s);
        return;
    }
    case 49: { // dump L.1874 -- full replay of 47's state (same addresses) + a message
               // specific to 48's "selector/class/tag" format (suffix str285).
        if (len < 41) return;
        uint32_t elemA = 0, elemB = 0, elemC = 0, elemD = 0, selector = 0, cls = 0;
        std::memcpy(&elemA, payload + 4, 4);
        std::memcpy(&elemB, payload + 8, 4);
        std::memcpy(&elemC, payload + 12, 4);
        std::memcpy(&elemD, payload + 16, 4);
        std::memcpy(&selector, payload + 20, 4);
        std::memcpy(&cls, payload + 24, 4);
        char tag[14] = {}; std::memcpy(tag, payload + 28, 13);

        ApplyAllianceUnpairState(elemA, elemB, elemC, elemD);

        const std::string s = AllianceComponentLabel(selector) + " [" + ClassLabel(static_cast<int32_t>(cls)) +
                               "] [" + tag + "] " + Str(285);
        g_Client.msg.Floating(10, 4, s);
        return;
    }
    case 50: { // dump L.1941
        if (len < 8) return;
        uint32_t selector = 0; std::memcpy(&selector, payload + 4, 4);
        const std::string s = AllianceComponentLabel(selector) + " " + Str(286);
        g_Client.msg.Floating(10, 5, s);
        return;
    }
    default:
        return;
    }
}

} // namespace wed_detail
} // namespace ts2::game

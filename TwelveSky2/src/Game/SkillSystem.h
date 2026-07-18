// Game/SkillSystem.h — Skill system of the TwelveSky2 client (ts2::game).
//
// CLEAN rewrite (not byte-exact) of the skill logic found in
// the TwelveSky2.exe disassembly (imagebase 0x400000). Truth = the DISASSEMBLY
// (MCP idaTs2) + Docs/TS2_GAMEPLAY_LOGIC.md §6. All functions operate on
// g_World.db.skill / g_World.db.item (.IMG tables) and SelfState ("self" block).
//
// Original functions reproduced here:
//   Skill_CostById              0x4CD0E0  -> Skill_CostById
//   Skill_ResolveLevelSlot      0x4FB370  -> Skill_ResolveLevelSlot
//   Skill_IsLearned             0x4FBCB0  -> SkillLearnFlags::IsLearned
//   Skill_GetValueByClassA      0x54E620  -> Skill_GetValueByClassA
//   Skill_GetValueByClassB      0x54E980  -> Skill_GetValueByClassB
//   Skill_UnpackTreeNodes       0x54C090  -> Skill_UnpackTreeNodes
//   Skill_CountTreeNodes        0x54BF70  -> Skill_CountTreeNodes
//   SkillGrowthTbl_GetRecord    0x4C4E90  -> Skill_GetRecord
//   SkillGrowthTbl_InterpStat   0x4C4EE0  -> Skill_InterpStat
//   Skill_IsAvailableByLevel    0x4FAF40  -> Skill_IsAvailableByLevel
//   Skill_IsAvailableByBranch   0x4FC390  -> Skill_IsAvailableByBranch
//   Skill_GetUpgradeCostTier    0x54F4D0  -> Skill_GetUpgradeCostTier
//   Char_CalcRegen              0x4D67F0  -> Skill_CalcRegenPct
//   (cast MP) Skill_CastStored  0x53E740  -> Skill_CalcRealMpCost / Skill_TryConsumeMp
//   (learn)   Pkt_ItemActionDispatch      -> Skill_Learn (g_LearnedSkills bar 0x16742BC)
//             switch section @0x46A4B0       (real anchor: the 4-case switch on
//                                             rec[+0x21C]; the old mention
//                                             "0x46A456" did not designate any EA on this
//                                             path — fixed 2026-07-16)
//
// SKILL TREE LAYOUT — CONFIRMED_FAITHFUL (2026-07-14, re-verified by a new
// idaTs2 decompilation pass the same day, including xrefs_to on
// SkillGrowthTbl_GetRecord 0x4C4E90: the ONLY UI-side readers of this
// table are UI_SkillLearn_OnLDown/Draw/OnMove — no other function
// in the binary draws an alternate "tree" widget). Verdict: the original
// game does NOT have a per-node custom-position tree layout or
// parent-child connection lines — the "real" layout IS a simple grid,
// as documented below. This is no longer a research limitation,
// it is a confirmed fact of the binary.
//
// UI_SkillLearn_Enter/OnLDown/OnLUp/Draw/OnMove (0x5E1BA0..0x5E2450)
// form a FIXED grid of 3 columns (weapon branch, i=0..2) x 8 rows
// (tier, j=0..7), exact pixel position (formula, not a per-node
// coordinate table):
//   x = x0 + 76*i + 35   y = y0 + 54*j + 71     (cell ~50x50 px)
//   x0 = nWidth/2 - bgWidth/2, y0 = nHeight/2 - bgHeight/2 (centered panel)
// The skillId of each cell comes from *(this+2)+2076+32*i+4*j, where *(this+2) is
// the currently open trainer NPC (confirmed by UI_SkillLearn_Enter 0x5E1BC5:
// *(this+2)+1312 is compared to g_LocalElement, an NPC faction/element field): THE
// 3x8 GRID IS SPECIFIC TO EACH TRAINER NPC, it is NOT a single global tree
// browsable at will. UI_SkillLearn_Draw (0x5E2200) draws NO
// parent-child connection line (verified twice: the body only contains
// background + cell icons + SP counter text) — CONFIRMED structural
// absence, not a research limitation. Prerequisite filtering (cases 1..4
// in UI_SkillLearn_OnLDown 0x5E1E89, gated on the 4 ranges of
// g_LearnedSkills) matches exactly the logic already ported here in
// Skill_Learn (switch on kOffSection).
//
// SEPARATE and still real point (not covered by the CONFIRMED_FAITHFUL above):
// SkillTreeWindow.cpp (UI/) does NOT wire the 3x8 grid as-is, not
// because the layout would be unknown (it IS CONFIRMED_FAITHFUL), but
// because the "which skillId in which cell" data is specific to each
// NPC and has no equivalent in GameDatabases/DataTable on the rewritten client
// side (no NPC->taught-skills table has been ported), and because opening
// this window is, in the original, an NPC interaction event — not a
// global keyboard shortcut ('K') as GameWindows.h currently does.
// This is a documented architecture choice (missing backend + different
// opening contract), NOT an unrecovered layout. Cf. detailed comment
// in UI/SkillTreeWindow.cpp/.h for the choice made.
#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <unordered_map>
#include "Game/GameState.h"

namespace ts2::game {

// Crt_ftol truncation (0x760810): cast (int), truncation TOWARD ZERO (= floor if
// positive). Every gameplay float multiplication goes through this.
inline int Skill_Ftol(double x) { return static_cast<int>(x); }

inline int32_t Skill_ReadI32(const uint8_t* rec, std::size_t off) {
    int32_t v;
    std::memcpy(&v, rec + off, sizeof(v));
    return v;
}

// SKILL_INFO record layout (776 bytes, 1-based indexed). Offsets = 4*idx dword.
namespace skillinfo {
inline constexpr std::size_t kRecordSize     = 776;
inline constexpr std::size_t kOffSkillId     = 0x000; // dword0  : id (sentinel, 0 = empty slot)
inline constexpr std::size_t kOffName        = 0x004; // idx1    : C string name embedded in the record.
                                                        // Confirmed by UI_SkillLearn_OnLDown 0x5E20A5:
                                                        // Crt_Vsnprintf(v15, "[%s]%s", v14 + 4, ...) where v14
                                                        // is the raw record pointer (v14+4 read DIRECTLY
                                                        // as const char*, not via StrTable). Field length
                                                        // not confirmed (only the trailing NUL
                                                        // guarantees the end) -> Skill_GetName() makes NO
                                                        // fixed-width assumption, cf. .cpp.
inline constexpr std::size_t kOffIconIndex   = 0x224; // idx137  : REAL icon index (internal atlas of the
                                                        // original client), DISTINCT from skillId. Confirmed
                                                        // by UI_SkillLearn_Draw 0x5E2328: Sprite2D_Draw
                                                        // ((int)&unk_A1BD60 + 148*v16, ...) with
                                                        // v16 = v12[137] - 1 (+1 more if the node
                                                        // is the hovered/pending node). The old
                                                        // "iconIdx == skillId" assumption in
                                                        // SkillTreeWindow.cpp (ResolveSkillIconPath) is
                                                        // therefore WRONG; fixed to read this field.
inline constexpr std::size_t kOffSection     = 0x21C; // idx135  : section/element -> bar range
inline constexpr std::size_t kOffCategory    = 0x220; // idx136  : category (4/5 = stance)
// CAST PREREQUISITES (0x228 / 0x22C) — WARNING, HISTORICAL MISNOMER.
// The two constants kOffReqWeapon/kOffReqBranch below have their NAMES
// SWAPPED relative to the binary's real semantics. The OFFSETS are correct,
// only the names are transposed. Proof (Player_CastSkill 0x53BC40):
//   +0x228 @0x53BD84: `cmp [edx+228h],1` (1 = neutral) otherwise `[eax+228h] - 2`
//                      is compared to g_LocalElementSecondary (0x1673198) -> this is
//                      the required ELEMENT, NOT the weapon. (msg 145 @0x53BDAE)
//   +0x22C @0x53BDD2: `cmp [eax+22Ch],1` (1 = neutral) otherwise compared to
//                      ITEM_INFO[+188] - 0Bh of the equipped weapon (dword_1673248)
//                      -> this is the required WEAPON TYPE. (msg 146 @0x53BE20)
// Game/GameDatabase.h:136-137 (field552 = req_element, field556 = req_weapon_type)
// is CORRECT and authoritative; it is this file that carried the error.
// The old names are KEPT because UI/SkillTreeWindow.cpp:599-600 consumes
// them (file not owned by this front-end): renaming them would break its
// compilation. Prefer the two correct aliases for all NEW code.
// TO WIRE OUT OF SCOPE: UI/SkillTreeWindow.cpp:602 displays "weapon %d,
// branch %d" with the two values SWAPPED (labels need to be swapped).
inline constexpr std::size_t kOffReqWeapon   = 0x228; // idx138  : MISNOMER — this is the required ELEMENT (cf. banner)
inline constexpr std::size_t kOffReqBranch   = 0x22C; // idx139  : MISNOMER — this is the required WEAPON TYPE (cf. banner)
// Correct aliases (same offsets, proven semantics) — use these from now on.
inline constexpr std::size_t kOffReqElement    = 0x228; // idx138 : required element (1 = neutral) [Player_CastSkill 0x53BD84]
inline constexpr std::size_t kOffReqWeaponType = 0x22C; // idx139 : required weapon type (1 = neutral) [Player_CastSkill 0x53BDD2]
inline constexpr std::size_t kOffSpCost      = 0x230; // idx140  : cost in skill points
inline constexpr std::size_t kOffLevelNorm   = 0x234; // idx141  : interpolation denominator
inline constexpr std::size_t kOffStatMin     = 0x240; // idx144  : stat_min[0..24] (stat#1 = MP cost)
inline constexpr std::size_t kOffStatMax     = 0x2A4; // idx169  : stat_max[0..24]
} // namespace skillinfo

// ITEM_INFO: fields consulted by the skill system.
namespace iteminfo {
inline constexpr std::size_t kOffTypeCode    = 188;   // idx47 : 0xD..0x15 = weapon class
inline constexpr std::size_t kOffTaughtSkill = 0x15C; // idx87 : id of the taught skill
inline constexpr std::size_t kOffRegen       = 360;   // MP cost % reduction (summed over 13 slots)
} // namespace iteminfo

// Rare runtime context: mirrors dword_16851B8. Active in Skill_InterpStat
// for stat#7 (range distance) of skills 112..120: ×0.7 penalty if
// != 3. Settable by the calling code (e.g. window/mode context).
inline int g_Skill112RangeMode = 0;

// Min/max access-level table per skill (SkillLevelTable_GetMin/Max
// 0x4FAB00/0x4FAB30). Flat dword array: record(id-1) = {min:i32, max:i32}
// (stride 8), skillId 1..350. Not present in GameDatabases -> injected table.
struct SkillLevelTable {
    DataTable table; // expected stride = 8 (min @+0, max @+4)
    int Min(int skillId) const;
    int Max(int skillId) const;
};

// Learned skills bar (g_LearnedSkills 0x16742BC): 40 slots, stride 8
// = {skillId @+0, spCost @+4}. A slot is free when skillId == 0.
struct SkillBarSlot {
    uint32_t skillId = 0;
    int32_t  spCost  = 0;
};
struct SkillBar {
    std::array<SkillBarSlot, 40> slots{};
    // First free slot in [begin, end); -1 if none (faithful: test skillId<1).
    int FindFree(int begin, int end) const;
    void Clear() { slots.fill(SkillBarSlot{}); }
};

// Sparse "learned skill" flags (Skill_IsLearned 0x4FBCB0). Only a fixed
// set of ids is tracked (mastery/stances/tribe); any other id -> not learned.
struct SkillLearnFlags {
    std::unordered_map<int, int> flags; // skillId -> raw value (learned iff ==1)
    // Does the id belong to the set tracked by Skill_IsLearned?
    static bool IsTrackable(int skillId);
    void Set(int skillId, int value) { flags[skillId] = value; }
    bool IsLearned(int skillId) const; // IsTrackable(id) && flags[id]==1
};

// Result of an MP-consumption attempt on cast.
struct SkillCastResult {
    bool ok   = false; // true = sufficient MP, debited
    int  cost = 0;     // real MP cost computed (after regen)
};

// ========================= System API ==========================================

// 1-based access to the SKILL_INFO record (null if id out of bounds or record empty).
const uint8_t* Skill_GetRecord(const DataTable& skillTbl, int skillId);

// 1-based access to the ITEM_INFO record (mirrors MobDb_GetEntry 0x4C3C00: `base+436*(id-1)`,
// null if id<1, id>count, or empty slot (dword0 == 0)). Was a local helper in
// SkillSystem.cpp; EXPOSED (2026-07-16) for Game/SkillCombat.cpp (weapon guard of
// Player_CastSkill 0x53BDD2, which calls MobDb_GetEntry(&mITEM, dword_1673248)) — avoids
// duplicating yet another reduced copy of it.
const uint8_t* Skill_ItemRecord(const DataTable& itemTbl, uint32_t itemId);

// Skill name (skillinfo::kOffName, C string embedded in the record -
// NOT a StrTable entry). Empty string if rec == nullptr. The buffer points INTO
// rec (lifetime tied to the DataTable, do not keep the pointer beyond that).
const char* Skill_GetName(const uint8_t* rec);

// Real icon index (skillinfo::kOffIconIndex), distinct from skillId. 0 if rec
// == nullptr. Cf. kOffIconIndex comment: the original atlas (unk_A1BD60,
// no identified .IMG source) is not reproducible as-is on the rewritten client
// side; this value serves as the best index candidate for any per-file
// icon resolution (cf. NoteSkillIcon, UI/SkillTreeWindow.cpp).
int Skill_GetIconIndex(const uint8_t* rec);

// Pivot formula: interpolates a skill stat between min and max based on
// level. statIndex 1..25 (1 = MP cost, 6 = range/speed, 7 = range dist).
// Returns double (the caller applies Skill_Ftol). Cf. §6.1.
double Skill_InterpStat(const DataTable& skillTbl, int skillId, int level, int statIndex);

// NOMINAL displayed MP cost (tooltip/UI). Hardcoded table 1..138; out of table:
// class of the equipped weapon (self.equip[7]). Cf. §6.2 "nominal".
int Skill_CostById(int skillId, const SelfState& self, const DataTable& itemTbl);

// Sum of the MP-cost % reduction = Σ ITEM_INFO+360 over the 13 equipment slots.
int Skill_CalcRegenPct(const SelfState& self, const DataTable& itemTbl);

// REAL debited MP cost: ftol(InterpStat #1) reduced by regen %. Cf. §6.2 "real".
int Skill_CalcRealMpCost(const DataTable& skillTbl, int skillId, int level, int regenPct);

// Checks and debits MP (mirrors Skill_CastStoredAtTarget): if self.mp >= cost
// then self.mp -= cost and ok=true; otherwise ok=false (message 147 client-side).
SkillCastResult Skill_TryConsumeMp(SelfState& self, const DataTable& skillTbl,
                                   const DataTable& itemTbl, int skillId, int level);

// Availability by level: lvlEff=level+levelBonus within [min,max] of the skill, with
// a rebirth gate (rebirth>=7) for 295/296/322/323. Cf. §6.3.
bool Skill_IsAvailableByLevel(const SkillLevelTable& lvlTbl, int skillId,
                              int level, int levelBonus, int rebirth);

// Availability by weapon branch: + level within [min,max] AND expected element.
bool Skill_IsAvailableByBranch(const SkillLevelTable& lvlTbl, int skillId,
                               int level, int levelBonus, int element);

// Decodes the current level into a (row, col) pair. (-1,-1) if outside any
// tier. Cf. Skill_ResolveLevelSlot 0x4FB370.
// DOC FIX (verified via xrefs_to on the IDB): the ONLY caller of this
// function in the original binary is UI_FactionInfoWnd_Render 0x672010 (via
// call 0x673160) — NOT a "skill tree" window. (row,col) actually marks
// the current rank/tier on the "Faction Info"/rebirth window
// (hence the rebirth parameter). Do NOT wire this to SkillTreeWindow: the real
// skill-learning widget (UI_SkillLearn_Draw 0x5E2200 and friends)
// does NOT use this function — cf. comment block at the top of the file.
void Skill_ResolveLevelSlot(const SkillLevelTable& lvlTbl, int level, int levelBonus,
                            int rebirth, int& outRow, int& outCol);

// Values by class / tier (tier 1..12). A: classes 1/2/3/5. B: classes 3/7.
int Skill_GetValueByClassA(int classId, int tier);
int Skill_GetValueByClassB(int classId, int tier);

// Upgrade cost by level 0..12 (0,3500,4500,... ,11000).
int Skill_GetUpgradeCostTier(int level);

// Talent tree: 3 dwords (12 bytes) -> 5 node values (v = b_lo + 1000*b_hi).
// Return = sentinel byte (b[1]); 0 => no node (out zeroed).
int Skill_UnpackTreeNodes(uint32_t w0, uint32_t w1, uint32_t w2, int out[5]);
// Number of consecutive non-null node pairs (0..5).
int Skill_CountTreeNodes(uint32_t w0, uint32_t w1, uint32_t w2);

// Learning: places the taught skill in the bar based on its section,
// debits self.skillPoints by the SP cost. Returns the slot index, or -1 on failure
// (record not found, unknown section, bar full). Cf. §8.1 group G0.
int Skill_Learn(SkillBar& bar, SelfState& self, const DataTable& skillTbl, uint32_t taughtSkillId);

// Id of the skill taught by an item (ITEM_INFO+0x15C).
uint32_t Skill_TaughtSkillIdFromItem(const uint8_t* itemRec);

} // namespace ts2::game

// Game/SkillCombat.h — Skill combat integration (ts2::game).
//
// Complement to SkillSystem.h (not edited): active-stance resolution, animation-motion
// selection by skill/weapon, hotkeys, and combat cast attempts. Truth
// = the TwelveSky2.exe disassembly (imagebase 0x400000) via MCP idaTs2.
//
// Original functions reproduced here:
//   Skill_GetActiveStance       0x4FB210  -> Skill_GetActiveStance
//   Skill_GetActiveStance2      0x4FCD40  -> Skill_GetActiveStance2
//   Skill_IsCurrentStanceSet    0x4FB0F0  -> Skill_IsCurrentStanceSet
//   Skill_GetComboMotionId      0x4FAD00  -> Skill_GetComboMotionId
//   Skill_GetMotionId2          0x4FC160  -> Skill_GetMotionId2
//   Skill_GetSpecialMotionId    0x4FC6D0  -> Skill_GetSpecialMotionId
//   Skill_IsSpecialUsable       0x4FC730  -> Skill_IsSpecialUsable
//   Skill_IsCurrentSpecial      0x4FC800  -> Skill_IsCurrentSpecial
//   Skill_GetBuffMotionId       0x4FC840  -> Skill_GetBuffMotionId
//   Skill_CheckBuffState        0x4FC950  -> Skill_CheckBuffState
//   Skill_GetBuffLevel          0x4FCB70  -> Skill_GetBuffLevel
//   Skill_IsCurrentBuff         0x4FCBC0  -> Skill_IsCurrentBuff
//   Skill_RemapByWeapon         0x501350  -> Skill_RemapByWeapon (+ sub_4FAB60 0x4FAB60)
//   Skill_IsHotkeyPressed       0x511340  -> Skill_IsHotkeyPressed
//   Skill_CanCastAtCursor       0x540E60  -> Skill_CanCastAtCursor
//   Skill_CastStoredAtTarget    0x53E740  -> Skill_CastStoredAtTarget
//   Skill_IsUsableOnCurrentMap  0x55D3B0  -> Skill_IsUsableOnCurrentMap (+ Char_CompareSkillLoadout 0x557B00)
//   Skill_HitTestSlot           0x662980  -> Skill_HitTestSlot
//   Skill_IsCurrentAttackSet    0x4FABC0  -> Skill_IsCurrentAttackSet
//   Skill_IsCurrentComboSet     0x4FC5D0  -> Skill_IsCurrentComboSet
//   Skill_IsCurrentSet138       0x4FCC00  -> Skill_IsCurrentSet138
//   Skill_IsCurrentSet5         0x4FCC70  -> Skill_IsCurrentSet5
//
// Original globals not modeled in GameState.h/SkillSystem.h and introduced here
// (local structures, do NOT modify any existing file):
//   g_SelfMorphNpcId   0x1675A98 (current action/stance/"morph" id of the local player)
//     WARNING MISLEADING IDA NAME (established in wave W10, not fixed here to avoid
//     rippling): this global is actually the CURRENT ZONE/MAP ID, not a morph. Three
//     independent proofs: (a) World_LoadCurrentZoneModel 0x4DD6E0 reads it to choose the
//     Z%03d.WM file to load — cf. World/WorldMap.h:159 `SetCurrentZoneId(...)
//     // g_SelfMorphNpcId 0x1675a98` and Scene/SceneManager.cpp:374 which passes it the
//     zoneId; (b) Combat_CanTargetOnMap 0x558740 @0x558759 does
//     `Map_GetPvpMode(g_MotionFrameRangeTable, g_SelfMorphNpcId)` then branches on
//     291/138/139/165/166/324/342/270-274/54 = MAPS (291 even has its two
//     variants Z291_1.WM/Z291_2.WM, cf. WorldMap::flagZ291Variant); (c) the `Map_`
//     prefix of the function that consumes it. Do NOT propagate the word "morph" in new
//     code: World/TerrainPicker.cpp reads `g_World.zoneId` for this value.
//   dword_16747BC      (rebirth-tier counter: 0 normal, 4..6 simple
//                        rebirth, >=7 high rebirth)
//   g_MotionFrameRangeTable 0x14A9350 (350-entry SoA table, reused by
//                        SkillLevelTable; the {comboGroup,flag} block at +700 dwords is
//                        modeled here via SkillBranchTable, cf. sub_4FAB60 0x4FAB60)
//   g_LocalPlayerSheet 0x1685748 (+455..458 element pairs -> ElementPairTable;
//                        +4052/+4104 loadout tags -> SkillLoadoutTable)
//   byte_1673184       (current weapon-branch tag, 13 bytes, "currentTag")
//   g_Container5 (dword_1674400/04/16743FC, indexed [dword_1675B1C][dword_1675B20]) ->
//                        SelectedCastSlot (selected spell-bar slot)
//   flt_1687330        (local player's world position -> `selfPos` parameter)
//   dword_168735C/60/64 (skill/level/param pending cast) -> PendingSkillCast
//
// OUT OF SCOPE (guards/tables faithfully reproduced; the external ACTION is delegated to
// the caller via an interface):
//   Terrain_PickRayScreen 0x699A80 / World_IsPointBlocked 0x540DA0 / MapColl_GetGroundHeight
//   0x697130 (screen picking + terrain collision -> 3D rendering): cf. ITerrainPicker.
//   Player_QueueSkill_opNN 0x5137D0..0x517320 (~30 outgoing opcode network builders, cf.
//   Docs/TS2_PROTOCOL_SPEC.md): cf. ISkillCastSink. The skillId -> opcode group mapping
//   IS faithfully reproduced (Skill_ResolveCastOpGroup).
#pragma once
#include <cstdint>
#include <cstring>
#include <array>
#include <vector>
#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/SkillSystem.h"
#include "Game/StatFormulas.h"
#include "Game/ClientRuntime.h"
// world::CollisionSlot (ITerrainPicker::PickRayScreen, cf. G-PICK-03 below). NO
// cycle: World/WorldMap.h is a LEAF module (only includes <array>/<cstdint>/<string>/
// <vector> + forward asset declarations), it includes no Game/ header.
#include "World/WorldMap.h"

namespace ts2::game {

// Runtime "morph"/rebirth state — absent from SelfState (cf. file header).
struct CombatMorphState {
    int currentActionId = 0; // g_SelfMorphNpcId 0x1675A98
    int rebirthTier      = 0; // dword_16747BC
};

// {comboGroup,flag} block of g_MotionFrameRangeTable (0x14A9350), at +700 dwords from the
// {min,max} block already modeled by SkillLevelTable. Mirrors sub_4FAB60 0x4FAB60: returns
// the comboGroup field (weapon branch associated with the skill), -1 if id is out of [1,350].
struct SkillBranchTable {
    DataTable table; // expected stride = 8 (comboGroup @+0)
    int Get(int skillId) const {
        if (skillId < 1 || skillId > 350) return -1;
        const uint8_t* rec = table.record(static_cast<uint32_t>(skillId - 1));
        return rec ? Skill_ReadI32(rec, 0) : -1;
    }
};

// Character element-pair table (mirrors g_LocalPlayerSheet+455..458 dwords,
// Char_GetPairedElement 0x557C00): 2 bidirectional pairs {a<->b, c<->d}.
struct ElementPairTable {
    int a = -1, b = -1, c = -1, d = -1;
    int Paired(int element) const {
        if (a == element) return b;
        if (b == element) return a;
        if (c == element) return d;
        if (d == element) return c;
        return -1;
    }
};

// GLOBAL EXPOSURE (2026-07-14 mission, Docs/TS2_COMBAT_ELEMENT_GATING.md §3/§4) —
// unblocks the SkillLevelTable/ElementPairTable gates for Net/WorldEntityDispatch.cpp
// (and any future caller) without duplicating an algorithm already ported above/in
// SkillSystem.h.
//
// 1) g_AlliancePairTable (== g_LocalPlayerSheet+0x71C..0x728, EXACTLY the block
//    mirrored by ElementPairTable::a/b/c/d) has NO persistent C++ instance: its
//    real source, on the binary side, is the memory escape hatch itself — NOW
//    POPULATED (mission "ElementPairTable wiring", 2026-07-14, cf. addendum
//    Docs/TS2_COMBAT_ELEMENT_GATING.md) by Net/WorldEntityDispatch.cpp ::
//    ApplyAlliancePairFamily, wiring Net_OnWorldEntityDispatch sub-opcode 46
//    (establishes a pair) / 47 (clears it), EA 0x497ce4/0x497d76. Combat_ReadLocalElementPairs()
//    builds an INSTANTANEOUS ElementPairTable by re-reading g_Client.VarGet() at the 4
//    original addresses, the SAME convention as g_SelfMorphNpcId (0x1675A98) used
//    everywhere else in ClientSource — these 4 addresses are now actually written
//    by a network handler (before this pass, no writer existed: the table
//    stayed frozen at {0,0,0,0}, the initial BSS value). Fidelity note kept: this
//    initial {0,0,0,0} state behaves EXACTLY like the {-1,-1,-1,-1} fallback of
//    ElementPairTable's default constructor (for any K in [0,3], Paired(K)
//    returns either -1 or K itself depending on whether a/b/c/d are 0 or not, and
//    Combat_IsElementAllowedOnMap treats "P==-1" and "P==K" IDENTICALLY, cf.
//    Docs/TS2_COMBAT_ELEMENT_GATING.md §2) — so NO explicit fallback to −1 is
//    needed, reading as-is remains correct before the first pair is established.
inline constexpr uint32_t kElementPairAAddr = 0x1685E64u; // g_LocalPlayerSheet+0x71C (a) == g_AlliancePairTable[0]
inline constexpr uint32_t kElementPairBAddr = 0x1685E68u; // +0x720 (b) == dword_1685E68[0]
inline constexpr uint32_t kElementPairCAddr = 0x1685E6Cu; // +0x724 (c) == g_AlliancePairTable[2]
inline constexpr uint32_t kElementPairDAddr = 0x1685E70u; // +0x728 (d) == dword_1685E68[2]

// Snapshot of g_AlliancePairTable, ready for ElementPairTable::Paired(...). Read
// only here (writing is done by Net/WorldEntityDispatch.cpp ::
// ApplyAlliancePairFamily, sub-opcodes 46/47, cf. banner above).
ElementPairTable Combat_ReadLocalElementPairs();

// 2) SkillLevelTable (Game/SkillSystem.h) — NO .IMG loader exists for this
//    table (unlike g_World.db.skill/level/item/monster/socketT, cf.
//    Game/GameDatabase.h): Motion_InitFrameTable 0x4F1380 builds it at
//    App_Init (EA 0x46227c) via a switch(i) of 350 ENTIRELY HARDCODED cases in the
//    binary (i = skillId-1; same {min@+0,max@+4} read by
//    SkillLevelTable_GetMin/Max 0x4FAB00/0x4FAB30) — NO derivation is possible from
//    g_World.db.skill (SKILL_INFO, a DISTINCT .IMG table, cf. skillinfo::kOffStatMin/Max
//    in Game/SkillSystem.h, an entirely different system). GetSkillLevelTable() faithfully
//    transcribes this switch (verified case-by-case against the full disassembly of
//    Motion_InitFrameTable, EA 0x4F1380..0x4F69E7) and builds the SINGLE instance once
//    (local static, cf. .cpp). UI/GameWindows.cpp (SkillTreeWindow audit,
//    2026-07-14) now binds GetSkillLevelTable() directly to SkillTreeWindow::Bind
//    instead of the old empty local member "for lack of identifying this source" —
//    the required levels displayed per node are therefore real, never 0..0 again.
const SkillLevelTable& GetSkillLevelTable();

// DirectInput keyboard snapshot (bit 7 of a state byte = key pressed, tested "<0"
// as a signed int8 in the binary). 14 slots (1..9, 0, F1..F4) per key set:
//   modeA = g_MorphUiMode==1: byte_80140F..801418 (10) + byte_8013D6..8013D9 (4)
//   modeB = otherwise       : byte_8013D6..8013DF (10) + byte_8013E4..8013E7 (4)
// Cf. Skill_IsHotkeyPressed 0x511340 / Game_UseFirstReadySkill 0x538190.
struct HotkeySnapshot {
    std::array<int8_t, 14> modeA{};
    std::array<int8_t, 14> modeB{};
    bool Pressed(int slot, int morphUiMode) const {
        if (slot < 0 || slot >= 14) return false;
        const auto& arr = (morphUiMode == 1) ? modeA : modeB;
        return arr[static_cast<size_t>(slot)] < 0;
    }
};

// Key<->hotbar-slot binding table, EMBEDDED copy in the original UI object
// (dwords +11427 = bound opcode/skillId, +11429 = "bound" flag==1; stride 3
// dwords/slot, 42 dwords/page = 14 slots). Mirrors the access
// this[42*page+3*slot+11427/11429] in Skill_IsHotkeyPressed 0x511340.
struct HotkeyBindTable {
    struct Bind {
        int32_t opcode = 0; // +11427 : identifier compared by the caller (original a2)
        int32_t bound  = 0; // +11429 : ==1 if the slot is bound to a key
    };
    std::vector<std::array<Bind, 14>> pages;
};

// Learned-buff tag grids (mirrors unk_16869C0/unk_1686AC4/unk_1686BC8):
// 4 rows (element) x 5 columns, 13-byte branch tag per cell. Compared against
// the current branch tag (byte_1673184) in Skill_CheckBuffState 0x4FC950.
struct BuffLearnedGrid {
    std::array<std::array<std::array<char, 13>, 5>, 4> cells{};
};
struct BuffLearnedGrids {
    BuffLearnedGrid g297; // unk_16869C0 (skill 297)
    BuffLearnedGrid g298; // unk_1686AC4 (skill 298)
    BuffLearnedGrid g299; // unk_1686BC8 (skill 299)
};

// Current buff levels (mirrors dword_1686064[0]/dword_1686068/dword_168606C).
struct BuffLevels {
    int32_t v297 = 0, v298 = 0, v299 = 0;
};

// Local character's "skill loadout" compatibility table (mirrors
// g_LocalPlayerSheet+4052.. and +4104.., Char_CompareSkillLoadout 0x557B00): for each
// weapon branch (0..3), one "primary" tag (13 bytes) and 12 "alternate" tags
// (13 bytes), compared against the current branch tag.
struct SkillLoadoutTable {
    std::array<std::array<char, 13>, 4>                 primary{}; // this+4052+13*branch
    std::array<std::array<std::array<char, 13>, 12>, 4> alt{};     // this+4104+156*branch+13*i

    // 0 = no match, 1 = primary tag, 2 = alternate tag.
    int Compare(const char currentTag[13], int branch) const;
};

// Spell-bar slot currently selected for a "cursor-targeted" cast
// (mirrors dword_1674404[i] (bound flag), dword_16743FC[i] (type code),
// dword_1674400[i] (level value), with i=42*dword_1675B1C+3*dword_1675B20, and
// selected = (dword_1675B20 != -1)). Cf. Skill_CanCastAtCursor 0x540E60.
struct SelectedCastSlot {
    bool    selected = false;
    int32_t bound    = 0; // dword_1674404[...] : ==1 -> active slot
    int32_t typeCode = 0; // dword_16743FC[...] : 3/22/41 -> locked-target spell
    int32_t level    = 0; // dword_1674400[...] : additive range term
};

// Integration point for screen picking/terrain collision (3D rendering, OUT OF
// SCOPE for pure gameplay):
//   IsPointBlocked  <- World_IsPointBlocked 0x540DA0 (+ MapColl_GetGroundHeight 0x697130)
//   PickRayScreen   <- Terrain_PickRayScreen 0x699A80
// REAL IMPLEMENTER: ts2::world::TerrainPicker (World/TerrainPicker.h) — wired to
// WorldAssets (actually decoded .WM/.WJ meshes). Before wave W10, this interface
// had NO implementer (G-PICK-06).
//
// WARNING: TWO FIDELITY FIXES (wave W10, re-proven against the disassembly):
//
// 1) `slot` (G-PICK-03) — the binary does NOT always query the same collision mesh.
//    Terrain_PickRayScreen is a __thiscall method whose `this` IS the targeted MapColl:
//      Skill_CanCastAtCursor @0x540F83: `mov ecx, offset dword_14A88E4` -> .WM  (Main)
//                            @0x540FC4: `mov ecx, offset dword_14A898C` -> .WJ  (WJ)
//                            @0x54105F: `mov ecx, offset dword_14A88E4` -> .WM  (Main)
//    Identity of the two meshes PROVEN by offset arithmetic: 0x14A898C - 0x14A88E4
//    = 0xA8, and World/WorldMap.h:90-94 sets Main = base+0xA8 / WJ = base+0x150 (also an
//    0xA8 gap) -> base g_GameWorld = 0x14A883C, so dword_14A88E4 == Main (.WM) and
//    dword_14A898C == WJ (.WJ). Corroborated by xrefs_to: 24 refs on 0x14A88E4 (the
//    game's primary mesh) vs 3 on 0x14A898C.
//
// 2) `twoSide` (formerly `wantEntityHit`, MISNOMER fixed) — the 6th argument of
//    Terrain_PickRayScreen 0x699A80 has nothing to do with "wants to hit an entity": it
//    is passed AS-IS and ONLY to MapColl_RaycastNearest 0x6960C0 @0x699BA9 as the
//    last parameter, whose role World/WorldMap.h:319-321 establishes as `twoSide`
//    (accept faces oriented on BOTH sides). Renaming it here removes a false meaning
//    that would have misled any future implementer.
struct ITerrainPicker {
    virtual ~ITerrainPicker() = default;
    virtual bool IsPointBlocked(const float pos[3]) = 0;
    // `slot`    : collision mesh queried (Main = .WM, WJ = .WJ) — cf. §1 above.
    // `twoSide` : original 6th arg of Terrain_PickRayScreen (0 or 1) — cf. §2 above.
    // outPos receives the 3D hit point. Returns false if nothing is hit.
    virtual bool PickRayScreen(int screenX, int screenY, world::CollisionSlot slot,
                                bool twoSide, float outPos[3]) = 0;
};

// Network constructors Player_QueueSkill_opNN (0x5137D0..0x517320, ~30 builders,
// outgoing opcode): OUT OF SCOPE (cf. Docs/TS2_PROTOCOL_SPEC.md). The
// skillId -> opcode group mapping IS faithfully reproduced (Skill_ResolveCastOpGroup);
// the actual send is delegated to the caller through this interface.
struct ISkillCastSink {
    virtual ~ISkillCastSink() = default;
    // Returns true if the packet was successfully built/sent (mirrors the
    // nonzero return value of the original Player_QueueSkill_opNN).
    virtual bool QueueSkillCast(int opGroup, int skillId, int level, int param,
                                 const float pos[3], int32_t targetHi, int32_t targetLo,
                                 int32_t targetKind) = 0;
};

// Skill pending cast (mirrors dword_168735C/1687360/1687364).
struct PendingSkillCast {
    int32_t skillId = 0; // dword_168735C
    int32_t level   = 0; // dword_1687360
    int32_t param   = 0; // dword_1687364
};

// Failure reasons for a cast attempt (Skill_CastStoredAtTarget 0x53E740 and
// Skill_CheckCastPrereqs / Player_CastSkill 0x53BC40).
enum class SkillCastFailReason {
    None = 0,
    NotEnoughMp     = 147,  // StrTable005 id 147
    IncompatibleForm = 1920, // StrTable005 id 1920 (morph 88/54 + stance skill)
    StanceRequired  = 1146, // StrTable005 id 1146 (stance/special/level>=70 required)
    MorphBlocked    = 1212, // StrTable005 id 1212 (transformed morph 234..240)
    // --- Guards specific to Player_CastSkill 0x53BC40 (cf. Skill_CheckCastPrereqs) ---
    ElementMismatch = 145,  // StrTable005 id 145 (0x91) [Player_CastSkill @0x53BDAE]
    WeaponMismatch  = 146,  // StrTable005 id 146 (0x92) [Player_CastSkill @0x53BE20]
    UnknownSkill    = -1,   // record not found or opcode group not mapped
    SinkRejected    = -2,   // ISkillCastSink::QueueSkillCast returned false
    // Equipped weapon without an ITEM_INFO record: the binary exits SILENTLY (no message),
    // NOT to be confused with WeaponMismatch/msg 146 [Player_CastSkill @0x53BDEF-0x53BDF7].
    WeaponRecordMissing = -3,
};

struct SkillCastAttemptResult {
    bool                ok = false;
    int                 mpCost = 0;
    SkillCastFailReason reason = SkillCastFailReason::None;
};

// ========================= System API ==========================================

// Selects the active stance skill (49/120/154/295/296) based on the current
// effective level. The 295->296 transition is gated by rebirthTier (>=7 -> 296,
// [4,6] or <4 -> 295, cf. §. 0 if no tier matches.
int Skill_GetActiveStance(const SelfState& self, const CombatMorphState& morph,
                           const SkillLevelTable& lvlTbl);

// Second-wave variant (319-323): rebirthTier>=7 -> 323 else 322 for the
// terminal tier; 0 if no tier matches.
int Skill_GetActiveStance2(const SelfState& self, const CombatMorphState& morph,
                            const SkillLevelTable& lvlTbl);

// Is the current action id a stance/guard (49/51/53/120-122/146-164/295/296/
// 319-323)?
bool Skill_IsCurrentStanceSet(int currentActionId);

// Table (weaponType 1..4, index) -> combo motion id. -1 if out of table.
int Skill_GetComboMotionId(int weaponType, int index);

// Second table (branch 0..3, index 0..7) -> motion id. -1 if out of table.
int Skill_GetMotionId2(int branch, int index);

// Table (index 0..3) -> special motion id (267/268/269/250). -1 if out of table.
int Skill_GetSpecialMotionId(int index);

// Is the special skill (250 or 267..269) usable at the current effective
// level? Additional guards: 250 requires rebirthTier>0 OR levelBonus==12;
// 269 requires rebirthTier==0 AND levelBonus<12.
bool Skill_IsSpecialUsable(int specialId, const SelfState& self, const CombatMorphState& morph,
                            const SkillLevelTable& lvlTbl);

// Is the current action id a special skill (250 or 267..269)?
bool Skill_IsCurrentSpecial(int currentActionId);

// Table (index 0..19) -> buff motion id (241-249/292-294/311-312/325-330).
// -1 if out of table.
int Skill_GetBuffMotionId(int index);

// Checks the learned branch tag for a buff (297/298/299) against the current
// branch tag: 0 = same element, 1 = different element, 2 = not found/unknown.
int Skill_CheckBuffState(int buffSkillId, const BuffLearnedGrids& grids,
                          const char currentTag[13], int localElement);

// Current level of a buff (297/298/299). 0 if id unknown.
int Skill_GetBuffLevel(int buffSkillId, const BuffLevels& levels);

// Is the current action id a buff (297..299)?
bool Skill_IsCurrentBuff(int currentActionId);

// Remaps a weapon skill (skillId) to its equivalent for weaponType if the
// skill's current weapon type diverges (neither identical nor paired). Faithfully
// reproduces the 4 fallback tables (weaponType 0..3); skillId unchanged if not mappable
// or already compatible.
int Skill_RemapByWeapon(int weaponType, int skillId, const SkillBranchTable& branch,
                         const ElementPairTable& pairs);

// Is the key bound to hotbar slot [page][slot] currently pressed and actually
// bound to `opcode`? False if no slot selected (slot==-1), if the
// slot isn't bound, or if the bound opcode diverges.
bool Skill_IsHotkeyPressed(const HotkeyBindTable& binds, int page, int slot,
                            const HotkeySnapshot& keys, int morphUiMode, int opcode);

// Is a cast at the screen cursor (screenX,screenY) valid? Two branches:
//  - selected bar slot with locked target (typeCode 3/22/41): requires
//    Skill_IsCurrentAttackSet, computes range via Skill_InterpStat (stat#6) + elemental
//    resistance (CalcElementResist), compares against the 3D distance to the
//    targeted point (locked picking), with a minimum of 10.0.
//  - otherwise: free picking, refused if the clicked point is blocked (off-ground/in a wall).
bool Skill_CanCastAtCursor(const float selfPos[3], const SelfState& self, const GameDatabases& db,
                            const CombatMorphState& morph, const SelectedCastSlot& slot,
                            int screenX, int screenY, ITerrainPicker& picker);

// Network opcode group (Player_QueueSkill_opNN) associated with a cast skillId, or -1
// if unmapped. 1:1 transcription of the big switch in Skill_CastStoredAtTarget 0x53E740.
int Skill_ResolveCastOpGroup(int skillId);

// Attempts to cast the pending skill (`pending`) at the given position/target.
// Checks the MP cost (existing Skill_CalcRealMpCost / Skill_CalcRegenPct), the
// incompatible-form guard (morph 88/54 + stance skill), the required-stance
// guard for group 38 (skillId 4/23/42), and the "transformed morph" block
// (234..240) before delegating to `sink`. The real MP debit, on success,
// DELEGATES to Skill_TryConsumeMp (SkillSystem.h) — cf. implementation comment.
SkillCastAttemptResult Skill_CastStoredAtTarget(SelfState& self, const GameDatabases& db,
                                                 const CombatMorphState& morph,
                                                 const PendingSkillCast& pending,
                                                 const float pos[3], int32_t targetHi,
                                                 int32_t targetLo, int32_t targetKind,
                                                 ISkillCastSink& sink);

// Prerequisite guards of the MAIN cast path — Player_CastSkill 0x53BC40.
//
// WARNING CONSUMPTION STATE (honesty required, do not delete without wiring): this
// function is written but is CALLED BY NOBODY today. Player_CastSkill
// 0x53BC40 is not ported, and NONE of its 8 call sites / 4 player entry
// functions are either (verified via xrefs_to 0x53BC40 + exhaustive grep of src/):
//   Game_OnWorldLeftClick   0x536690 (@0x536863, @0x536EFA)
//   Game_OnHotkey           0x537330 (@0x5377F7)
//   Game_UseFirstReadySkill 0x538190 (@0x53855F)
//   AutoPlay_Update         0x45E770 (@0x45E953, @0x45EA46, @0x45EBD0, @0x45ED3D)
// OPEN DEBT: to be called from whichever front-end ports one of these entries, at the
// HEAD of the cast (the whole prerequisite chain precedes any network send in the
// binary); a result of `ok == false` must abort the cast without emitting anything.
//
// WARNING: do NOT call it from Skill_CastStoredAtTarget (0x53E740): that variant
// is the AUTOPLAY path (exclusive callers Player_AutoInteractPlayer 0x5396F0 and
// Player_AutoInteractMonster 0x53A170) and does NOT carry these guards — the disassembly
// 0x53E740..0x53E7F9 goes straight to the MP cost. Adding them there would be an
// INFIDELITY.
//
// EXACT order reproduced — COMPLETE prerequisite chain, EA 0x53BD12..0x53BEA0:
//   0. RECORD @0x53BD12: SkillGrowthTbl_GetRecord; NUL -> `xor eax,eax` @0x53BD20,
//      SILENT failure (no message) -> UnknownSkill.
//   1. FORM @0x53BD27: g_SelfMorphNpcId (0x1675A98) == 0x58 (88) OR 0x36 (54), AND
//      (category(+0x220) == 4 || == 5 || rec[0] == 0x4E (78)) -> msg 1920 @0x53BD60, fail.
//      WARNING: this particular message is NOT gated by arg_C (no `cmp [ebp+arg_C],0`
//      before 0x53BD59) — unlike the three that follow. Faithfully reproduced: emitted
//      regardless of `showErr`.
//   2. ELEMENT @0x53BD84: reqElement(+0x228) != 1 && (reqElement - 2) != self
//      .elementSecondary (g_LocalElementSecondary 0x1673198) -> msg 145 @0x53BDAE, fail.
//   3. WEAPON @0x53BDD2: reqWeaponType(+0x22C) != 1 -> ITEM_INFO record of the equipped
//      weapon (dword_1673248 == self.equip[7].itemId); NUL record -> SILENT failure
//      @0x53BDEF-0x53BDF7; otherwise reqWeaponType != (ITEM_INFO+188 - 0x0B) -> msg 146
//      @0x53BE20, fail.
//   4. MP COST @0x53BE41: ftol(InterpStat(#1, rec[0], level)) reduced by regen%
//      (INTEGER division @0x53BE72-0x53BE86); self.mp < cost -> msg 147, fail. TWO
//      emission sites for msg 147 on this failure path: @0x53BEA0 (gated by arg_C /
//      showErr) THEN @0x53BECA (gated by g_InvDirtyEnable(0x16755AC)==1, the master
//      auto-hunt flag — NOT by showErr); both can fire.
// `showErr` (original arg_C) gates messages 145/146 and the FIRST emission of 147
// (@0x53BDA1, @0x53BE13, @0x53BE93); the SECOND emission of 147 (@0x53BEBA) is gated by
// the auto-hunt flag, not by showErr. Failures occur whether showErr is true
// or false, only the message is conditional. Does NOT debit MP (the binary only debits
// after the network builder succeeds, much further down the function).
//
// NOTE: guard 1 (FORM) is the SAME one already ported in
// Skill_CastStoredAtTarget (0x53E740) — both functions carry it independently
// in the binary; this is therefore not a duplication introduced here.
SkillCastAttemptResult Skill_CheckCastPrereqs(const SelfState& self, const GameDatabases& db,
                                               const CombatMorphState& morph,
                                               int skillId, int level, bool showErr);

// Is weapon branch `mapZoneIndex` (0..3) usable on the current map?
// Requires currentActionId to be in the associated quadruplet (cf. implementation) AND a
// loadout match (SkillLoadoutTable::Compare != 0).
bool Skill_IsUsableOnCurrentMap(int mapZoneIndex, int currentActionId,
                                 const SkillLoadoutTable& loadout, const char currentTag[13]);

// Tests position (cursorX,cursorY) against the 2x5 skill-bar grid
// anchored at (anchorX,anchorY) (cf. UI_ProjectSpriteToScreen 0x50F5D0, computed by
// the caller — OUT OF SCOPE here). Returns the FREE slot index found (valid
// drag&drop destination), or -1 (no slot under the cursor, slot already occupied, or
// classOffset out of table). panelVisible==false -> immediate -1 (mirrors this[2]==0).
int Skill_HitTestSlot(bool panelVisible, int anchorX, int anchorY, int cursorX, int cursorY,
                       int classOffset, const SkillBar& bar);

// Does the current action id belong to the "standard" attack/skill set?
bool Skill_IsCurrentAttackSet(int currentActionId);
// ... to the combo set (19-36/175-193)?
bool Skill_IsCurrentComboSet(int currentActionId);
// ... to the set {138,139,165,166}?
bool Skill_IsCurrentSet138(int currentActionId);
// ... to the set {5,10,15,123}?
bool Skill_IsCurrentSet5(int currentActionId);

} // namespace ts2::game

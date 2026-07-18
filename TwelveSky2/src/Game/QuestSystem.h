// Game/QuestSystem.h — Quest system for the TwelveSky2 client (ts2::game).
//
// CLEAN rewrite (not byte-exact, except QuestInfo which is a memory overlay) of the
// quest logic found in the TwelveSky2.exe disassembly (imagebase 0x400000).
// Truth = the DISASSEMBLY (MCP idaTs2). Fills the TODO(state) left by
// Net/GameHandlers_Misc.cpp for packet 0x27 (QuestInteractResult, alias
// Pkt_SmithUpgradeResult 0x48E7D0 — misnamed in the original IDB) by providing the
// missing pieces: quest definition table, current step record
// (equivalent to g_pCurQuestStepRecord / dword_18231B4), and objective evaluation.
//
// Original functions reproduced here (truth = actual decompilation, one by one):
//   QuestTbl_ValidateRecord      0x4C84C0 -> QuestTbl_ValidateRecord
//   QuestTbl_LoadImg             0x4C8630 -> LoadQuestTable
//   QuestTbl_GetRecord           0x4C88B0 -> QuestTbl_GetRecord
//   QuestTbl_FindByGroupAndName  0x4C8900 -> QuestTbl_FindByGroupAndName
//   QuestTbl_FindFirstByGroup    0x4C89C0 -> QuestTbl_FindFirstByGroup
//   QuestTbl_FindByGroupAndStage 0x4C8A60 -> QuestTbl_FindByGroupAndStage
//   QuestTbl_CountByGroup        0x4C8AF0 -> QuestTbl_CountByGroup
//   QuestTbl_CountByGroupUpTo    0x4C8B60 -> QuestTbl_CountByGroupUpTo
//   QuestTbl_FindFirstOfGroup    0x4C8BD0 -> QuestTbl_FindFirstOfGroup
//   QuestTbl_FindPrevOfGroup     0x4C8C40 -> QuestTbl_FindPrevOfGroup
//   QuestTbl_FindNextOfGroup     0x4C8CC0 -> QuestTbl_FindNextOfGroup
//   Quest_CheckObjectiveState    0x50FF10 -> Quest_CheckObjectiveState
//   Quest_IsObjectiveComplete    0x5103F0 -> Quest_IsObjectiveComplete
//   Quest_GetObjectiveResult     0x510520 -> Quest_GetObjectiveResult
//   Quest_GetRewardItemId        0x510A10 -> Quest_GetRewardItemId
//   Quest_IsRewardItemActive     0x510A90 -> Quest_IsRewardItemActive
//   Quest_IsItemAllowed          0x54F0B0 -> Quest_IsItemAllowed
// Supporting functions decompiled to faithfully complete packet 0x27 (direct callees
// of Pkt_SmithUpgradeResult, same struct offsets as Quest_CheckObjectiveState):
//   Inventory_RemoveItem         0x510C40 -> Quest_RemoveTrackedItem
//   Inventory_ReplaceItem        0x510B40 -> Quest_ReplaceTrackedItem
//   Util_SumExceeds2Billion      0x53F660 -> Quest_SumExceeds2Billion
//   Pkt_SmithUpgradeResult       0x48E7D0 -> ApplyQuestInteractResultState (STATE part
//                                             only — the MESSAGE part is already
//                                             handled by Net/GameHandlers_Misc.cpp)
//
// TWO DISTINCT "quest" data sources exist in the binary:
//
//  (A) QuestTbl — 999 x 84 bytes, file G03_GDATA\D01_GIMAGE2D\005\005_00007.IMG (no
//      XOR on the count, no embedded table name — unlike the 5 tables
//      in GameDatabase.cpp). Story PROGRESSION table (group/stage/value),
//      queried by the help UI (mHELP -> UI_CharListWnd_*, xrefs verified). This is the
//      QUEST_INFO deduced/loaded here (struct QuestInfo, DataTable g_QuestTable).
//
//  (B) Table mQUEST — 1000 rows x 8444 bytes (005_00006.IMG). Quest_CheckObjectiveState/
//      IsObjectiveComplete/GetObjectiveResult/GetRewardItemId/IsRewardItemActive AND
//      g_pCurQuestStepRecord (dword_18231B4) ALL operate on THIS record type
//      (offsets +64/+72/+92/+96/+116/+120/+124/+136..+156 confirmed by cross-decompilation
//      of the 5 functions + Pkt_SmithUpgradeResult). Modeled here via
//      QuestStepRecord — a CLEAN VIEW limited to the fields actually consumed (not an
//      8444-byte memory overlay), projected from game::QuestDefRecord (Game/ExtraDatabases.h).
//
//      The old "OUT OF SCOPE — no loader assigned" banner was STALE: mQUEST
//      0x8E71E4 IS loaded, via `call NpcTbl_LoadImg 0x4C8090` at EA 0x4621A0 in
//      App_Init (failure => MessageBoxA "[Error::mQUEST.Init()]" + App_Init returns 0). On
//      the C++ side it is too, in g_ExtraDb.quest (Game/ExtraDatabases.cpp:47).
//
//      RESOLUTION — the binary has NO indirection: it hard-calls
//      `NpcTbl_FindByTypeAndId(mQUEST, element0, questId)` 0x4C8340 (EA 0x50FF65, 0x50FFCA,
//      0x510A37, 0x510AB7, 0x664A67, 0x510E40, 0x510ECC). LookupQuestStep() therefore
//      resolves DIRECTLY against g_ExtraDb.quest. The QuestStepLookup function pointer
//      (below) is a rewrite invention, from when the table wasn't modeled yet; it NEVER had
//      an implementer -> objective evaluation was entirely dead code (the lookup returned
//      nullptr forever). It is kept ONLY as a test override.
//
// RULE: this file does NOT edit any existing header. It includes Game/GameState.h (DataTable,
// SelfState unused), Game/GameDatabase.h (ItemInfo/GetItemInfo for
// Quest_IsRewardItemActive), and Game/ClientRuntime.h (g_Client.Var/VarF for the long
// tail of scalar globals + InventoryState for the inventory grid).
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <array>
#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/ClientRuntime.h"

namespace ts2::game {

// (A) QUEST_INFO — story progression table (005_00007.IMG).
#pragma pack(push, 1)

// QuestInfo — 84 bytes. Layout deduced from QuestTbl_ValidateRecord 0x4C84C0:
//   +0  id     (1-based, MUST equal index+1; 0 = empty slot -> valid but inert row)
//   +4  name[5][13]  5 candidate names (C string, NUL terminator WITHIN the 13 bytes -> 12
//                    usable chars max; validate fails if no NUL is found)
//   +72 group  1..4   (the QuestTbl_Find* functions compare "group-1 == group0")
//   +76 stage  1..145 (stage within the group)
//   +80 value  1..999 (free parameter — never interpreted by the QuestTbl_* functions themselves)
// (69 -> 72: 3 bytes of natural padding to realign the next int32, same as the original
//  compiler — no #pragma pack needed here, but set explicitly to
//  guarantee sizeof==84 on any target.)
struct QuestInfo {
    uint32_t id;             // +0
    char     name[5][13];    // +4  (65 bytes)
    uint8_t  _pad69[3];      // +69 alignment padding (never read)
    uint32_t group;          // +72
    uint32_t stage;          // +76
    uint32_t value;           // +80
};
static_assert(sizeof(QuestInfo) == 84, "QuestInfo doit faire 84 o (cf. QuestTbl_ValidateRecord 0x4C84C0)");

#pragma pack(pop)

// Quest definition table (999 rows x 84 bytes), separate from GameDatabases (not
// editable) — loaded by LoadQuestTable() below.
inline DataTable g_QuestTable;

// Raw 0-based access to a row (nullptr if out of bounds) — internal utility exposed for
// tests.
inline const QuestInfo* QuestRecordAt(const DataTable& table, uint32_t row0) {
    const uint8_t* r = table.record(row0);
    return reinterpret_cast<const QuestInfo*>(r);
}

// QuestTbl_ValidateRecord 0x4C84C0 — validates 0-based row `row0` of `table`.
// An empty row (id==0) is VALID (free slot). Checks id bounds/consistency, NUL of the 5
// names, group/stage/value. `row0` must be < table.count (like the original caller, which
// only loops over [0,count)); out of bounds -> false.
bool QuestTbl_ValidateRecord(const DataTable& table, int row0);

// QuestTbl_LoadImg 0x4C8630 — loads G03_GDATA\D01_GIMAGE2D\005\005_00007.IMG into
// `g_QuestTable` (envelope [rawSize][packedSize][zlib] -> [count:u32][999*84 bytes], WITHOUT
// XOR on the count or embedded table name, unlike the GameDatabase.cpp tables).
// Requires count==999 (hard integrity guard). Validates each row via
// QuestTbl_ValidateRecord; returns false (table still populated) at the first invalid
// row, like the original. `gameDataDir` = "GameData" root (same convention as
// LoadGameDatabases).
bool LoadQuestTable(const std::string& gameDataDir);

// QuestTbl_GetRecord 0x4C88B0 — 1-based access; nullptr if id is out of [1,count] or slot
// is empty.
const QuestInfo* QuestTbl_GetRecord(const DataTable& table, int id1based);

// QuestTbl_FindByGroupAndName 0x4C8900 — searches by (group0, name) among the 5 names of
// each row in the group (case-insensitive comparison, Crt_Stricmp 0x76668B).
// `group0` = group-1 (convention common to ALL the QuestTbl_Find* functions below:
// the binary always compares `field72 - 1 == a2`). Returns a POINTER to the row
// (unlike the following functions which return an ID — faithful to the binary, which
// derefs or not depending on the function).
const QuestInfo* QuestTbl_FindByGroupAndName(const DataTable& table, int group0, const char* name);

// QuestTbl_FindFirstByGroup 0x4C89C0 — does nothing if !(flagA3==1 && flagA4==0) (original
// guard, exact meaning unknown — raw a2/a3 parameters from the binary). Otherwise, returns
// the ID (1-based, == index+1) of the first row in the group with stage==1, or 0 if none.
int QuestTbl_FindFirstByGroup(const DataTable& table, int group0, int flagA3, int flagA4);

// QuestTbl_FindByGroupAndStage 0x4C8A60 — returns the ID (1-based) of the exact
// (group0, stage) row, or 0 if none. This is the function whose result classically feeds
// g_pCurQuestStepRecord-like caches on the UI side (via QuestTbl_GetRecord afterward).
int QuestTbl_FindByGroupAndStage(const DataTable& table, int group0, int stage);

// QuestTbl_CountByGroup 0x4C8AF0 — number of non-empty rows in the group.
int QuestTbl_CountByGroup(const DataTable& table, int group0);

// QuestTbl_CountByGroupUpTo 0x4C8B60 — same, restricted to the first `rowLimit` rows
// (0-based, NOT bounded by table.count — faithful to the binary).
int QuestTbl_CountByGroupUpTo(const DataTable& table, int group0, int rowLimit);

// QuestTbl_FindFirstOfGroup 0x4C8BD0 — ID (1-based) of the first row of the group, 0 if
// none.
int QuestTbl_FindFirstOfGroup(const DataTable& table, int group0);

// QuestTbl_FindPrevOfGroup 0x4C8C40 — scans BACKWARD starting from 0-based row
// (fromId1based - 2) to find the previous row of the group; returns its ID, or
// `fromId1based` UNCHANGED if none found (faithful: the binary returns the argument
// as-is on failure, NOT 0).
int QuestTbl_FindPrevOfGroup(const DataTable& table, int group0, int fromId1based);

// QuestTbl_FindNextOfGroup 0x4C8CC0 — symmetric, scans FORWARD starting from 0-based row
// fromId1based; returns `fromId1based` unchanged if none found.
int QuestTbl_FindNextOfGroup(const DataTable& table, int group0, int fromId1based);

// (B) Current quest step — view over the mQUEST-type NPC table (OUT OF SCOPE,
//     see banner at top of file). Fields = union of those consumed by
//     Quest_CheckObjectiveState/GetObjectiveResult/GetRewardItemId/IsRewardItemActive and
//     by Pkt_SmithUpgradeResult (0x48E7D0, alias QuestInteractResult op 0x27).
struct QuestStepRecord {
    uint32_t field64  = 0; // +64  levelReq: LEVEL gate for the "implicit" branch
                            //      (Quest_CheckObjectiveState @0x50FF86: `cmp g_SelfLevel,
                            //      [rec+0x40]` then `jge` -> 1). Intro data: 1,1,1,1,2,4,
                            //      6,8,10,12 = a level scale.
    uint32_t category = 0; // +72  1..8: objective type — selector for the 8-case switch
                            //      in Quest_CheckObjectiveState @0x50FFFC, and source of
                            //      g_QuestObjType 0x16745FC (Pkt_SmithUpgradeResult @0x48E929
                            //      reads `[rec+0x48]`). The "1..6" comment was WRONG: the
                            //      validator bounds it to 1..8 and the data covers 1..8
                            //      ({1:227,2:57,3:38,4:69,5:127,6:7,7:88,8:75}).
    uint32_t field92  = 0; // +92  v10[23]: "result" id/value (variant A)
    uint32_t field96  = 0; // +96  v10[24]: "result" id/value (variant B, case 4/6.1)
    uint32_t field116 = 0; // +116 v10[29]: "result" id/value if objective fulfilled
    uint32_t targetId = 0; // +120 v10[30]: target id of the current objective (mob/item/npc)
    uint32_t required = 0; // +124 v10[31]: required quantity/value
    struct { uint32_t type = 0; uint32_t value = 0; } reward[3]; // +136/+140,+144/+148,
                                                                  // +152/+156: up to 3
                                                                  // rewards (type==6
                                                                  // => value = item id,
                                                                  // cf Quest_GetRewardItemId)
};

// LookupQuestStep — DIRECT resolution against g_ExtraDb.quest via
// FindQuestDefByElementAndId (= NpcTbl_FindByTypeAndId 0x4C8340), projected into
// QuestStepRecord. Faithful: the binary calls the resolver hard-coded, with no indirection.
// `zoneId` is a MISLEADING LEGACY NAME kept for out-of-scope callers
// (Game/ComboPickupTick.cpp:179/188, UI/GameHud.cpp:971, UI/QuestTrackerWindow.cpp): this
// parameter is actually the 0-based LOCAL ELEMENT (g_LocalElement 0x1673194 ==
// game::g_World.self.element), NOT a zone/map. Proof: Quest_GetRewardItemId 0x510A10
// reads `[this+0xA024]` with this = g_PlayerCmdController 0x1669170 (@0x5E01C5) and
// 0x1669170+0xA024 = 0x1673194 = g_LocalElement; UI_EventNoticeWnd_Open 0x6649F0 @0x664A67
// literally calls NpcTbl_FindByTypeAndId(mQUEST, g_LocalElement, g_CurQuestId).
// -> Callers that display it as a zone name (StrTable003/zoneNames) are in
//    error; flagged to the orchestrator, out of scope for this file.
const QuestStepRecord* LookupQuestStep(int zoneId, int npcQuestId);

// Test override ONLY (the binary has no such indirection — see banner (B)).
// nullptr (default) => LookupQuestStep resolves against the real table.
using QuestStepLookup = const QuestStepRecord* (*)(int zoneId, int npcQuestId);
void SetQuestStepLookup(QuestStepLookup fn);

// Accessor equivalent to g_pCurQuestStepRecord (dword_18231B4) = record of the
// CURRENT quest step. Resolved DIRECTLY (body in QuestSystem.cpp).
//
// VERIFIED IN IDA (2026-07-16): g_pCurQuestStepRecord 0x18231B4 is NEVER WRITTEN
// anywhere in the binary — all 20 references to this address are in Pkt_SmithUpgradeResult
// 0x48E7D0 and are ALL READS (no store; xrefs re-verified). But the binary is a running
// game: this pointer cannot be NULL at runtime when case 1/2/3/4 dereference it, so its
// runtime value must be the current step record. Proven equivalence:
// g_pCurQuestStepRecord == NpcTbl_FindByTypeAndId(mQUEST, g_LocalElement, g_CurQuestId) — in
// case 2, the reward loop reads g_pCurQuestStepRecord+8*i+136 (@0x48EB0E) and
// Quest_GetRewardItemId 0x510A10 re-reads Find(element, g_CurQuestId) @0x510A37 for the SAME
// reward -> same record.
// => CurrentQuestStepRecord() therefore resolves DIRECTLY via LookupQuestStep (like the
//    other 7 consumers) instead of returning a g_curStepRecord that NO code writes. The old
//    version returned nullptr forever -> the case 1/2/3/4 state of
//    ApplyQuestInteractResultState AND the QuestTracker visibility guard
//    (UI/QuestTrackerWindow.cpp:48, sibling file) were DEAD CODE. This does NOT fabricate a
//    writer for g_curStepRecord (no invented store): it is an on-demand resolution,
//    faithful to reading a pointer that, in game, holds this record. The `if (npc)` guards
//    in ApplyQuestInteractResultState remain (LookupQuestStep can return nullptr if the
//    table isn't loaded or npcQuestId==0) = the only non-UB behavior.
// => SetCurrentQuestStepRecord remains as a TEST OVERRIDE (takes priority); g_curStepRecord
//    stays nullptr in production.
// WIRING NOTE (outside this module): resolution depends on g_QuestProgress.npcQuestId (==
// g_CurQuestId 0x16745F4) and g_World.self.element (== g_LocalElement 0x1673194), populated
// by the NETWORK. Until the network front-end populates them, resolution returns nullptr
// (correct gating: no active quest).
const QuestStepRecord* CurrentQuestStepRecord();
void SetCurrentQuestStepRecord(const QuestStepRecord* record);

// Player progress state — mirrors fields of the large player struct
// g_PlayerCmdController 0x1669170 consumed by Quest_CheckObjectiveState & friends.
// WARNING: the Hex-Rays indices (+10249, +11553, ...) are `int*`-SCALED, not byte
// offsets. Actual byte offsets = index*4, verified against the disassembly.
//
// TWO fields from the old version were REMOVED because they duplicated state that
// already exists elsewhere (and that NOBODY wrote -> dead evaluation):
//   * `totalKillCount` (+10254 -> byte 0xA038 -> 0x1669170+0xA038 = 0x16731A8 =
//     g_SelfLevel) was NOT a kill counter but the player's LEVEL. The "implicit"
//     branch is a level gate: @0x50FF80 `mov ecx,[edx+0xA038]` then @0x50FF86
//     `cmp ecx,[eax+0x40]` / `jge` -> 1. Now read from g_World.self.level
//     (GameState.h::SelfState, the established mirror of g_SelfLevel, written by the
//     network: Net/CharStatDeltaDispatch.cpp:239, Game/EntityManager.cpp:523).
//   * `killTrack` (2x64 slots of 6 dwords, "+10320") was a phantom duplicate of the
//     INVENTORY GRID, always zero and therefore never satisfied. Byte 10320*4 = 0xA140 ->
//     0x1669170+0xA140 = 0x16732B0 = g_InvMain (IDA name), and the binary's `384*i + 6*j`
//     indexing is exactly `0x600*row + 0x18*col`, already modeled by
//     ClientRuntime.h::InventoryState. The IDA functions `Inventory_RemoveItem 0x510C40` /
//     `Inventory_ReplaceItem 0x510B40` are therefore CORRECTLY NAMED (the old comment
//     claiming they were misnamed was wrong): they operate on g_Client.inv.
struct QuestProgressState {
    // +10249 -> byte 0xA024 -> 0x1669170+0xA024 = 0x1673194 = g_LocalElement.
    // MISLEADING NAME kept (out-of-scope callers): this is the 0-based local ELEMENT,
    // NOT a zone. See LookupQuestStep above. Must equal g_World.self.element.
    int zoneId           = 0;
    int npcQuestId        = 0; // +11553 -> 0xB484 = g_CurQuestId 0x16745F4
    int objectiveMode     = 0; // +11554 -> 0xB488 = g_QuestObjMode 0x16745F8 (0 = level gate, 1 = active objective)
    int objectiveType     = 0; // +11555 -> 0xB48C = g_QuestObjType 0x16745FC (1..8)
    int objectiveTarget   = 0; // +11556 -> 0xB490 = g_QuestObjParam1 0x1674600 (target id / type-6 phase)
    int objectiveProgress = 0; // +11557 -> 0xB494 = g_QuestObjParam2 0x1674604 (progress counter)
};

// Objective evaluation — faithful translation (branches/thresholds/order unchanged).

// Quest_CheckObjectiveState 0x50FF10 — state of the current objective:
//   "implicit" branch (mode/type/target/progress all 0) -> 0 or 1 (bool)
//   "active" branch (mode==1) -> 0 (invalid/not found), 2 (in progress), 3 (fulfilled, cases
//   1-5/8), 4 (case 6.2 not fulfilled), or 5 (case 6.2 fulfilled)
int Quest_CheckObjectiveState(const QuestProgressState& s);

// Quest_IsObjectiveComplete 0x5103F0 — maps Quest_CheckObjectiveState to a bool based on
// objectiveType (==3 for 1/2/3/4/5/8, ==5 for 6, ==2 for 7; otherwise false).
bool Quest_IsObjectiveComplete(const QuestProgressState& s);

// Quest_GetObjectiveResult 0x510520 — same branches as CheckObjectiveState but returns
// a QuestStepRecord field (field92/96/116) instead of a state code.
int Quest_GetObjectiveResult(const QuestProgressState& s);

// Quest_GetRewardItemId 0x510A10 — item id of the first type==6 reward (0 if
// none or record not found).
int Quest_GetRewardItemId(const QuestProgressState& s);

// Quest_IsRewardItemActive 0x510A90 — true if the type==6 reward exists AND
// ITEM_INFO(itemId).typeCode == 2 (mirrors MobDb_GetEntry 0x4C3C00 + comparison +188==2).
bool Quest_IsRewardItemActive(const QuestProgressState& s);

// Quest_IsItemAllowed 0x54F0B0 — item `itemId` is present in the whitelist (14 slots)
// of container `containerIndex` (globals dword_1675808[14*a1+i] / dword_1687494[0] —
// long tail not modeled, addressed via g_Client.Var()). containerIndex==1 additionally
// requires g_Client.VarGet(0x1687494)==1 (original guard).
bool Quest_IsItemAllowed(int containerIndex, int itemId);

// Direct callees of Pkt_SmithUpgradeResult (op 0x27) operating on QuestProgressState.

// Inventory_RemoveItem 0x510C40 — searches for `itemId` in rows 0..1 of the INVENTORY
// GRID (g_InvMain 0x16732B0, `384*i + 6*j + 10320` == `0x600*row + 0x18*col`), zeroes
// the cell's 6 dwords (EA 0x510CBF..0x510D63), and returns the row index
// (0/1), or -1 if absent (@0x510D7D). The IDA name `Inventory_*` is CORRECT: it really
// is the inventory (row 0 = main bag, row 1 = bonus page).
int Quest_RemoveTrackedItem(InventoryState& inv, uint32_t itemId);

// Inventory_ReplaceItem 0x510B40 — searches for `oldItemId` in rows 0..1 of the grid,
// replaces the id (+0 @0x510BC2), then zeroes flag/color/durability (+12/+16/+20 =
// dwords 10323/10324/10325, EA 0x510BDE/0x510BFF/0x510C20). gridX/gridY (+4/+8 = 10321/
// 10322) are UNCHANGED — faithful: the original only writes +0/+12/+16/+20. Returns the
// row index, or -1 if absent.
int Quest_ReplaceTrackedItem(InventoryState& inv, uint32_t oldItemId, uint32_t newItemId);

// Util_SumExceeds2Billion 0x53F660 — (a+b) > 2,000,000,000 in 64-bit arithmetic.
inline bool Quest_SumExceeds2Billion(int64_t a, int64_t b) { return (a + b) > 2000000000LL; }

// Packet 0x27 QuestInteractResult (alias Pkt_SmithUpgradeResult 0x48E7D0). Clean struct
// (no dependency on Net/RecvPackets.h): [resultCode][invRow][invSlot][gridX][gridY].
struct QuestInteractResultPacket {
    uint32_t resultCode = 0; // 1..9
    int32_t  invRow      = -1; // -1 = no inventory write (faithful signed comparison)
    uint32_t invSlot     = 0;
    uint32_t gridX       = 0;
    uint32_t gridY       = 0;
};

// ApplyQuestInteractResultState — STATE PART of Pkt_SmithUpgradeResult 0x48E7D0
// (resultCode 1..9). The MESSAGE part (StrTable005_Get + Msg_AppendSystemLine for codes
// 109/432..439) is ALREADY handled by Net/GameHandlers_Misc.cpp (0x27); this function does
// NOT DUPLICATE it, except for messages 427..431 of the case-2 reward loop (str
// 427=type6, 428=type2/weight, 429=type3/currency, 430=type4, 431=type5), which are NOT
// covered by the TODO in GameHandlers_Misc.cpp (that file only handles the top-level
// message based on resultCode, not the inner loop over the 3 reward slots).
// Uses CurrentQuestStepRecord() for ALL step-record accesses; this accessor now resolves
// DIRECTLY via LookupQuestStep(g_World.self.element, g_QuestProgress.npcQuestId),
// exactly the key the binary re-reads via NpcTbl_FindByTypeAndId(mQUEST, element, g_CurQuestId)
// in Quest_GetRewardItemId/IsRewardItemActive (@0x510A37) — hence the equivalence with the
// cached global g_pCurQuestStepRecord 0x18231B4 (see the doc block on CurrentQuestStepRecord
// above).
// PRECISE TODO (out of scope — audio/UI, not state logic):
//   Snd3D_PlayScaledVolume 0x4DA380, UI_EventNoticeWnd_Open 0x6649F0 (cases 1/2/5),
//   cGameHud_ResetUiState 0x62AFB0 (cases 2/3/4).
void ApplyQuestInteractResultState(const QuestInteractResultPacket& p,
                                    QuestProgressState& progress,
                                    InventoryState& inv);

// Single global instance (clean mirror of quest progress state, similar
// to g_Guild/g_Warehouse). Used by GameHandlers_Core (written via
// ApplyQuestInteractResultState) and by QuestTrackerWindow (read for display).
inline QuestProgressState g_QuestProgress;

} // namespace ts2::game

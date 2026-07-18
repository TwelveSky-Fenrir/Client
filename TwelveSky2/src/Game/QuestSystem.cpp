// Game/QuestSystem.cpp — implementation of the quest system (ts2::game).
// Faithful transcription of the TwelveSky2.exe disassembly. Cf. QuestSystem.h for the EAs.
#include "Game/QuestSystem.h"
#include "Game/ExtraDatabases.h" // g_ExtraDb.quest + FindQuestDefByElementAndId (NpcTbl_FindByTypeAndId 0x4C8340)
#include "Asset/ImgFile.h"
#include "Core/Log.h"
#include <cstring>
#include <vector>

namespace ts2::game {
namespace {

// Subdirectory shared by the .IMG 005_* tables (same convention as GameDatabase.cpp).
const char kTablesDir[] = "G03_GDATA\\D01_GIMAGE2D\\005";
const char kQuestFile[] = "005_00007.IMG";

std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// Case-insensitive, bounded C-string comparison (mirrors Crt_Stricmp 0x76668B
// applied to buffers not necessarily null-terminated within the QuestInfo field's 13 bytes).
bool StriEqualBounded(const char* a, const char* b, size_t maxLen) {
    size_t i = 0;
    for (; i < maxLen; ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 'a' + 'A');
        if (ca != cb) return false;
        if (ca == 0) return true; // both terminate here
    }
    return true;
}

// Test-only override (nullptr = direct resolution against the real table — cf. banner
// (B) of QuestSystem.h : the binary has NO indirection here).
QuestStepLookup g_stepLookup = nullptr;
const QuestStepRecord* g_curStepRecord = nullptr;

// Projection cache QuestDefRecord (8444 bytes) -> QuestStepRecord (view of only the
// consumed fields). ONE entry per row of g_ExtraDb.quest, indexed by the SAME row index
// as the binary: it mirrors `base + 8444*i` @0x4C83CE, so it's a STABLE pointer, UNIQUE per
// row. Essential: Quest_GetRewardItemId/IsRewardItemActive re-resolve while
// ApplyQuestInteractResultState already holds an `npc` — a shared static buffer would alias.
std::vector<QuestStepRecord> g_stepCache;

// Projects an mQUEST row onto the QuestStepRecord view. Offsets proven by cross-
// decompilation (Quest_CheckObjectiveState 0x50FF10, Quest_GetObjectiveResult 0x510520,
// Quest_GetRewardItemId 0x510A10, Pkt_SmithUpgradeResult 0x48E7D0).
void ProjectQuestStep(const QuestDefRecord& def, QuestStepRecord& out) {
    out.field64  = def.levelReq;           // +64  level gate (@0x50FF86 `cmp g_SelfLevel, [rec+0x40]`)
    out.category = def.fieldE;             // +72  objective type 1..8 (@0x50FFFC switch ; @0x48E929 -> g_QuestObjType)
    out.field92  = def.fieldG;             // +92  v10[23] (@0x510520)
    out.field96  = def.fieldH[0];          // +96  v10[24] (@0x510520)
    out.field116 = def.fieldI;             // +116 v10[29] (@0x510520)
    out.targetId = def.objectiveTarget;    // +120 (@0x510012 etc. ; @0x48E881 -> item id g_InvMain)
    out.required = def.objectiveRequired;  // +124 (@0x51002A `progress >= [v10+124]`)
    for (int i = 0; i < 3; ++i) {          // +136 + 8*i / +140 + 8*i (@0x510A62 / @0x510A72)
        out.reward[i].type  = def.rewards[i].category;
        out.reward[i].value = def.rewards[i].value;
    }
}

} // namespace

// ===========================================================================
// (A) QuestTbl_* — QUEST_INFO table (005_00007.IMG).
// ===========================================================================

bool QuestTbl_ValidateRecord(const DataTable& table, int row0) {
    const QuestInfo* r = QuestRecordAt(table, static_cast<uint32_t>(row0));
    if (!r) return false; // out-of-bounds row (the original never loops outside [0,count))

    if (r->id == 0) return true; // empty slot -> valid

    if (r->id < 1 || r->id > 999) return false;
    if (r->id != static_cast<uint32_t>(row0 + 1)) return false;

    for (int i = 0; i < 5; ++i) {
        int j = 0;
        for (; j < 13 && r->name[i][j] != '\0'; ++j) {}
        if (j == 13) return false; // no NUL found within the 13 bytes
    }

    if (r->group < 1 || r->group > 4) return false;
    if (r->stage < 1 || r->stage > 145) return false;
    return (r->value >= 1 && r->value <= 999);
}

bool LoadQuestTable(const std::string& gameDataDir) {
    const std::string path = JoinPath(JoinPath(gameDataDir, kTablesDir), kQuestFile);

    asset::ImgFile img;
    if (!img.Load(path)) {
        TS2_ERR("Quest : .IMG illisible : %s", path.c_str());
        return false;
    }
    const std::vector<uint8_t>& payload = img.Payload();
    if (payload.size() < 4) {
        TS2_ERR("Quest : payload trop court (%zu o)", payload.size());
        return false;
    }

    // count = first dword, WITHOUT XOR (unlike GameDatabase.cpp's 5 tables).
    uint32_t count = 0;
    std::memcpy(&count, payload.data(), 4);

    // Hardcoded integrity guard (QuestTbl_LoadImg 0x4C8630 : `if (v10 == 999)`).
    if (count != 999) {
        TS2_ERR("Quest : compteur invalide (%u, attendu 999)", count);
        return false;
    }

    constexpr uint32_t kStride = 84;
    constexpr uint32_t kHeader = 4; // records directly follow the counter
    const size_t need = static_cast<size_t>(kHeader) + static_cast<size_t>(count) * kStride;
    if (payload.size() < need) {
        TS2_ERR("Quest : payload tronque (%zu < %zu o)", payload.size(), need);
        return false;
    }

    const uint8_t* rec = payload.data() + kHeader;
    g_QuestTable.data.assign(rec, rec + static_cast<size_t>(count) * kStride);
    g_QuestTable.count  = count;
    g_QuestTable.stride = kStride;

    TS2_LOG("Quest : %u enregistrements x %u o", g_QuestTable.count, g_QuestTable.stride);

    // Row-by-row validation (faithful: stops at the first invalid row, table left
    // populated as in the original — no rollback).
    for (uint32_t i = 0; i < g_QuestTable.count; ++i) {
        if (!QuestTbl_ValidateRecord(g_QuestTable, static_cast<int>(i))) {
            TS2_ERR("Quest : ligne %u invalide", i);
            return false;
        }
    }
    return true;
}

const QuestInfo* QuestTbl_GetRecord(const DataTable& table, int id1based) {
    if (id1based < 1 || static_cast<uint32_t>(id1based) > table.count) return nullptr;
    const QuestInfo* r = QuestRecordAt(table, static_cast<uint32_t>(id1based - 1));
    if (!r || r->id == 0) return nullptr;
    return r;
}

const QuestInfo* QuestTbl_FindByGroupAndName(const DataTable& table, int group0, const char* name) {
    if (!name) return nullptr;
    for (uint32_t i = 0; i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (!r || r->id == 0) continue;
        if (static_cast<int>(r->group) - 1 != group0) continue;
        for (int j = 0; j < 5; ++j) {
            if (StriEqualBounded(r->name[j], name, 13)) return r;
        }
    }
    return nullptr;
}

int QuestTbl_FindFirstByGroup(const DataTable& table, int group0, int flagA3, int flagA4) {
    if (flagA3 != 1 || flagA4 != 0) return 0;
    for (uint32_t i = 0; i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (!r || r->id == 0) continue;
        if (static_cast<int>(r->group) - 1 != group0) continue;
        if (r->stage != 1) continue;
        return static_cast<int>(r->id);
    }
    return 0;
}

int QuestTbl_FindByGroupAndStage(const DataTable& table, int group0, int stage) {
    for (uint32_t i = 0; i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (!r || r->id == 0) continue;
        if (static_cast<int>(r->group) - 1 != group0) continue;
        if (static_cast<int>(r->stage) != stage) continue;
        return static_cast<int>(r->id);
    }
    return 0;
}

int QuestTbl_CountByGroup(const DataTable& table, int group0) {
    int n = 0;
    for (uint32_t i = 0; i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (r && r->id != 0 && static_cast<int>(r->group) - 1 == group0) ++n;
    }
    return n;
}

int QuestTbl_CountByGroupUpTo(const DataTable& table, int group0, int rowLimit) {
    int n = 0;
    for (int i = 0; i < rowLimit; ++i) {
        const QuestInfo* r = QuestRecordAt(table, static_cast<uint32_t>(i));
        if (r && r->id != 0 && static_cast<int>(r->group) - 1 == group0) ++n;
    }
    return n;
}

int QuestTbl_FindFirstOfGroup(const DataTable& table, int group0) {
    for (uint32_t i = 0; i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (r && r->id != 0 && static_cast<int>(r->group) - 1 == group0)
            return static_cast<int>(r->id);
    }
    return 0;
}

int QuestTbl_FindPrevOfGroup(const DataTable& table, int group0, int fromId1based) {
    for (int i = fromId1based - 2; i >= 0; --i) {
        const QuestInfo* r = QuestRecordAt(table, static_cast<uint32_t>(i));
        if (r && r->id != 0 && static_cast<int>(r->group) - 1 == group0)
            return static_cast<int>(r->id);
    }
    return fromId1based; // faithful: returns the argument unchanged if nothing found
}

int QuestTbl_FindNextOfGroup(const DataTable& table, int group0, int fromId1based) {
    // Faithful: the binary loops `for (i = fromId1based; i < count; ++i)` without
    // guarding against a negative fromId1based (memory UB in C). We clamp to 0 for
    // safety ; in normal usage fromId1based is always >= 1 (id or -1 never passed here).
    for (uint32_t i = static_cast<uint32_t>(fromId1based < 0 ? 0 : fromId1based); i < table.count; ++i) {
        const QuestInfo* r = QuestRecordAt(table, i);
        if (r && r->id != 0 && static_cast<int>(r->group) - 1 == group0)
            return static_cast<int>(r->id);
    }
    return fromId1based; // faithful: returns the argument unchanged if nothing found
}

// ===========================================================================
// (B) Current quest step.
// ===========================================================================

void SetQuestStepLookup(QuestStepLookup fn) { g_stepLookup = fn; }

// LookupQuestStep — mirrors `NpcTbl_FindByTypeAndId(mQUEST, element0, questId)` 0x4C8340,
// called HARDCODED by the binary (EA 0x50FF65, 0x50FFCA, 0x510A37, 0x510AB7, 0x664A67,
// 0x510E40, 0x510ECC). `zoneId` = 0-based local element (misleading historical name, cf.
// QuestSystem.h). No table-loaded guard: on an empty table FindQuestDefByElementAndId's
// scan finds nothing and returns nullptr — exactly the binary's `xor eax,eax` @0x4C83D2.
const QuestStepRecord* LookupQuestStep(int zoneId, int npcQuestId) {
    if (g_stepLookup) return g_stepLookup(zoneId, npcQuestId); // test override

    const QuestDefRecord* def = FindQuestDefByElementAndId(zoneId, npcQuestId);
    if (!def) return nullptr;

    // Row index == the binary's: (rec - base) / 8444 (imul stride @0x4C836D).
    const DataTable& t = g_ExtraDb.quest;
    const uint8_t* base = t.data.data();
    const size_t row = static_cast<size_t>(reinterpret_cast<const uint8_t*>(def) - base)
                     / static_cast<size_t>(t.stride);

    if (g_stepCache.size() != t.count) g_stepCache.assign(t.count, QuestStepRecord{});
    if (row >= g_stepCache.size()) return nullptr; // unreachable (def comes from the table)

    ProjectQuestStep(*def, g_stepCache[row]);
    return &g_stepCache[row];
}

// CurrentQuestStepRecord — mirrors g_pCurQuestStepRecord 0x18231B4 (record of the CURRENT
// quest step), RESOLVED DIRECTLY. The binary READS this global (Pkt_SmithUpgradeResult
// 0x48E7D0: 20 xrefs @0x18231B4, ALL reads, no store) but never writes it statically. The
// game RUNS, so at execution time this pointer is NOT null when cases 1/2/3/4 dereference
// it: its value is the current step's record. Proven equivalence
// g_pCurQuestStepRecord == NpcTbl_FindByTypeAndId(mQUEST, g_LocalElement, g_CurQuestId): in
// case 2, the reward loop reads g_pCurQuestStepRecord+8*i+136 (@0x48EB0E) and
// Quest_GetRewardItemId 0x510A10 re-reads Find(element, g_CurQuestId) @0x510A37 for the SAME
// reward written into the inventory -> both must designate the same record. We therefore
// resolve via LookupQuestStep (like the 7 other consumers) instead of returning
// g_curStepRecord, which NO code writes (used to return nullptr forever => cases 1/2/3/4 of
// ApplyQuestInteractResultState AND the QuestTracker UI's visibility gate
// (QuestTrackerWindow.cpp:48) were DEAD CODE). This is NOT fabricating a writer for
// g_curStepRecord: on-demand resolution, faithful to the pointer read. element =
// g_World.self.element (== g_LocalElement 0x1673194) ; questId = g_QuestProgress.npcQuestId
// (== g_CurQuestId 0x16745F4). npcQuestId==0 (no quest) => Find(element,0) => nullptr =>
// consumers gated off (correct gating).
const QuestStepRecord* CurrentQuestStepRecord() {
    if (g_curStepRecord) return g_curStepRecord; // test override (takes priority)
    return LookupQuestStep(static_cast<int>(g_World.self.element), g_QuestProgress.npcQuestId);
}
void SetCurrentQuestStepRecord(const QuestStepRecord* record) { g_curStepRecord = record; }

namespace {
// Mirrors the `for (i<2) for (j<64)` loops of Quest_CheckObjectiveState (EA 0x510058/
// 0x510070 and twins): `*(this + 384*i + 6*j + 10320) == target`. Offset 10320*4 = 0xA140
// from g_PlayerCmdController 0x1669170 gives 0x16732B0 = g_InvMain -> it's the INVENTORY
// GRID (row 0 = bag, row 1 = bonus page), not a dedicated tracking table. The binary reads
// a global; so we read g_Client.inv, its established mirror (ClientRuntime.h).
bool InvRows01Contains(uint32_t value) {
    for (uint32_t row = 0; row < 2; ++row)                       // for (i = 0; i < 2; ++i)
        for (uint32_t col = 0; col < InventoryState::kCols; ++col) // for (j = 0; j < 64; ++j)
            if (g_Client.inv.At(row, col).itemId == value) return true;
    return false;
}
} // namespace

int Quest_CheckObjectiveState(const QuestProgressState& s) {
    if (s.objectiveMode == 0 && s.objectiveType == 0 && s.objectiveTarget == 0 && s.objectiveProgress == 0) {
        // "Implicit" branch = LEVEL GATE on the NEXT quest (npcQuestId + 1).
        // EA 0x50FF6A : Find(mQUEST, element, questId+1) ; EA 0x50FF80/0x50FF86 :
        // `mov ecx,[edx+0xA038]` (= g_SelfLevel 0x16731A8) then `cmp ecx,[eax+0x40]` / `jge`.
        const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId + 1);
        return (npc && g_World.self.level >= static_cast<int>(npc->field64)) ? 1 : 0;
    }
    if (s.objectiveMode != 1) return 0;

    const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId);
    if (!npc) return 0;

    switch (s.objectiveType) {
        case 1:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= static_cast<int>(npc->required)) ? 3 : 2;
        case 2:
        case 3:
        case 4:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return InvRows01Contains(npc->targetId) ? 3 : 2;
        case 5:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= 1) ? 3 : 2;
        case 6:
            if (s.objectiveTarget == 1) {
                if (s.objectiveProgress != static_cast<int>(npc->targetId)) return 0;
                return InvRows01Contains(npc->targetId) ? 3 : 2;
            }
            if (s.objectiveTarget == 2) {
                if (s.objectiveProgress != static_cast<int>(npc->required)) return 0;
                return InvRows01Contains(npc->required) ? 5 : 4;
            }
            return 0;
        case 7:
            return (s.objectiveTarget == static_cast<int>(npc->targetId)) ? 2 : 0;
        case 8:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= 1) ? 3 : 2;
        default:
            return 0;
    }
}

bool Quest_IsObjectiveComplete(const QuestProgressState& s) {
    switch (s.objectiveType) {
        case 1: case 2: case 3: case 4: case 5: case 8:
            return Quest_CheckObjectiveState(s) == 3;
        case 6:
            return Quest_CheckObjectiveState(s) == 5;
        case 7:
            return Quest_CheckObjectiveState(s) == 2;
        default:
            return false;
    }
}

int Quest_GetObjectiveResult(const QuestProgressState& s) {
    if (s.objectiveMode == 0 && s.objectiveType == 0 && s.objectiveTarget == 0 && s.objectiveProgress == 0) {
        // Same level gate as Quest_CheckObjectiveState (g_SelfLevel vs rec+64).
        const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId + 1);
        if (!npc) return 0;
        return (g_World.self.level >= static_cast<int>(npc->field64)) ? static_cast<int>(npc->field92) : 0;
    }
    if (s.objectiveMode != 1) return 0;

    const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId);
    if (!npc) return 0;

    switch (s.objectiveType) {
        case 1:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= static_cast<int>(npc->required))
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
        case 2:
        case 3:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return InvRows01Contains(npc->targetId)
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
        case 4:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return InvRows01Contains(npc->targetId)
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field96);
        case 5:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= 1)
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
        case 6:
            if (s.objectiveTarget == 1) {
                if (s.objectiveProgress != static_cast<int>(npc->targetId)) return 0;
                return InvRows01Contains(npc->targetId)
                           ? static_cast<int>(npc->field96) : static_cast<int>(npc->field92);
            }
            if (s.objectiveTarget == 2) {
                if (s.objectiveProgress != static_cast<int>(npc->required)) return 0;
                return InvRows01Contains(npc->required)
                           ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
            }
            return 0;
        case 7:
            return (s.objectiveTarget == static_cast<int>(npc->targetId)) ? static_cast<int>(npc->field116) : 0;
        case 8:
            if (s.objectiveTarget != static_cast<int>(npc->targetId)) return 0;
            return (s.objectiveProgress >= 1)
                       ? static_cast<int>(npc->field116) : static_cast<int>(npc->field92);
        default:
            return 0;
    }
}

int Quest_GetRewardItemId(const QuestProgressState& s) {
    const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId);
    if (!npc) return 0;
    for (const auto& r : npc->reward)
        if (r.type == 6) return static_cast<int>(r.value);
    return 0;
}

bool Quest_IsRewardItemActive(const QuestProgressState& s) {
    const QuestStepRecord* npc = LookupQuestStep(s.zoneId, s.npcQuestId);
    if (!npc) return false;
    for (const auto& r : npc->reward) {
        if (r.type != 6) continue;
        const ItemInfo* item = GetItemInfo(r.value);
        return item != nullptr && item->typeCode == 2;
    }
    return false;
}

bool Quest_IsItemAllowed(int containerIndex, int itemId) {
    if (containerIndex == 1 && g_Client.VarGet(0x1687494) != 1) return false;
    for (int i = 0; i < 14; ++i) {
        const uint32_t addr = 0x1675808u + 4u * (14u * static_cast<uint32_t>(containerIndex) + static_cast<uint32_t>(i));
        if (g_Client.VarGet(addr) == itemId) return true;
    }
    return false;
}

// ===========================================================================
// Direct callees of Pkt_SmithUpgradeResult — inventory grid (g_InvMain 0x16732B0),
// rows 0..1 (main bag + bonus page).
// ===========================================================================

// Inventory_RemoveItem 0x510C40.
int Quest_RemoveTrackedItem(InventoryState& inv, uint32_t itemId) {
    for (int i = 0; i < 2; ++i) {                                   // for (i = 0; i < 2; ++i) @0x510C49
        for (uint32_t j = 0; j < InventoryState::kCols; ++j) {      // for (j = 0; j < 64; ++j) @0x510C65
            InvCell& c = inv.At(static_cast<uint32_t>(i), j);
            if (c.itemId == itemId) {                               // @0x510CA0
                c = InvCell{}; // the 6 dwords 10320..10325 reset to 0 (@0x510CBF..0x510D63)
                return i;                                           // @0x510D6E
            }
        }
    }
    return -1;                                                      // @0x510D7D
}

// Inventory_ReplaceItem 0x510B40.
int Quest_ReplaceTrackedItem(InventoryState& inv, uint32_t oldItemId, uint32_t newItemId) {
    for (int i = 0; i < 2; ++i) {                                   // @0x510B49
        for (uint32_t j = 0; j < InventoryState::kCols; ++j) {      // @0x510B65
            InvCell& c = inv.At(static_cast<uint32_t>(i), j);
            if (c.itemId == oldItemId) {                            // @0x510BA0
                c.itemId = newItemId;                               // +0  @0x510BC2
                // Faithful: the original only writes 10323/10324/10325 ; gridX/gridY
                // (10321/10322) stay UNCHANGED.
                c.flag = 0;                                         // +12 @0x510BDE
                c.color = 0;                                        // +16 @0x510BFF
                c.durability = 0;                                   // +20 @0x510C20
                return i;                                           // @0x510C2B
            }
        }
    }
    return -1;                                                      // @0x510C3A
}

// ===========================================================================
// Packet 0x27 — STATE part.
// ===========================================================================

void ApplyQuestInteractResultState(const QuestInteractResultPacket& p,
                                    QuestProgressState& progress,
                                    InventoryState& inv) {
    const QuestStepRecord* npc = CurrentQuestStepRecord();

    // The binary reads the element from the global g_LocalElement 0x1673194 (= base+0xA024)
    // on EVERY resolution ; our `progress.zoneId` mirrors it. We resynchronize it here
    // against its single source (g_World.self.element == g_LocalElement, cf.
    // Net/NetClient.h:111): without this it would stay 0 forever and the composite resolver
    // would return element 1's row for 3 out of 4 players.
    progress.zoneId = static_cast<int>(g_World.self.element);

    switch (p.resultCode) {
    case 1: {
        g_Client.Var(0x1675B08) = 0; // g_GmCmdCooldownLatch
        if (p.invRow != -1 && npc) {
            inv.Set(static_cast<uint32_t>(p.invRow), p.invSlot, npc->targetId, p.gridX, p.gridY,
                    /*count*/0, /*durability*/0, /*serial*/0);
        }
        // Quest state: written into progress.* (= the SAME memory as the binary's
        // 0x16745F4..0x1674604 globals, cf. QuestProgressState). The previous version wrote
        // to g_Client.Var(0x16745F4..) — a 2nd representation that NOBODY re-read (all
        // consumers read g_QuestProgress): the state was written then lost. Only one
        // representation now.
        progress.npcQuestId += 1;    // g_CurQuestId 0x16745F4 @0x48E911/0x48E914
        progress.objectiveMode = 1;  // g_QuestObjMode 0x16745F8 @0x48E91A
        if (npc) {
            progress.objectiveType = static_cast<int>(npc->category); // g_QuestObjType 0x16745FC @0x48E929
            // @0x48E938 / @0x48E95E : two independent `cmp [rec+0x48], 6` tests.
            if (npc->category == 6) {
                progress.objectiveTarget = 1;                                   // @0x48E94E
                progress.objectiveProgress = static_cast<int>(npc->targetId);   // @0x48E975/0x48E978
            } else {
                progress.objectiveTarget = static_cast<int>(npc->targetId);     // @0x48E943/0x48E946
                progress.objectiveProgress = 0;                                 // @0x48E964
            }
        }
        // TODO(state) out of scope: Snd3D_PlayScaledVolume 0x4DA380, UI_EventNoticeWnd_Open(1) 0x6649F0.
        break;
    }
    case 2: {
        g_Client.Var(0x1675B08) = 0;
        if (p.invRow != -1) {
            const int rewardItemId = Quest_GetRewardItemId(progress);
            const bool active = Quest_IsRewardItemActive(progress);
            inv.Set(static_cast<uint32_t>(p.invRow), p.invSlot, static_cast<uint32_t>(rewardItemId),
                    p.gridX, p.gridY, active ? 1u : 0u, /*durability*/0, /*serial*/0);
            g_Client.Var(0x18398F8) = p.invRow; // dword_18398F8
            // TODO(state) out of scope: cGameHud_ResetUiState 0x62AFB0.
        }
        if (npc) {
            for (const auto& r : npc->reward) {
                switch (r.type) {
                case 2: // weight (g_InvWeight)
                    if (!Quest_SumExceeds2Billion(inv.weight, r.value)) {
                        inv.weight += r.value;
                        g_Client.msg.System(Str(428));
                    }
                    break;
                case 3: // currency (g_Currency)
                    inv.currency += r.value;
                    g_Client.msg.System(Str(429));
                    break;
                case 4:
                    g_Client.msg.System(Str(430));
                    break;
                case 5: // dword_16746A4 (long tail)
                    g_Client.Var(0x16746A4) += static_cast<int32_t>(r.value);
                    g_Client.msg.System(Str(431));
                    break;
                case 6: // item — already written to the grid above
                    g_Client.msg.System(Str(427));
                    break;
                default:
                    break;
                }
            }
            switch (progress.objectiveType) { // g_QuestObjType (persists from an earlier case 1)
            case 2: case 3: case 4:
                Quest_RemoveTrackedItem(inv, npc->targetId);
                break;
            case 6:
                Quest_RemoveTrackedItem(inv, npc->required);
                break;
            default:
                break;
            }
        }
        progress.objectiveMode = 0;
        progress.objectiveType = 0;
        progress.objectiveTarget = 0;
        progress.objectiveProgress = 0;
        g_Client.Var(0x1675AE8) = 0;
        g_Client.VarF(0x1675AEC) = g_World.gameTimeSec - 590.0f;
        break;
    }
    case 3: {
        g_Client.Var(0x1675B08) = 0;
        // Faithful: NO invRow!=-1 guard here in the binary (unlike cases 1/2/4) —
        // unconditional write. We clamp via InventoryState::At (avoids memory UB if
        // invRow==-1, where the binary would have read/written out of bounds).
        if (npc) {
            inv.Set(static_cast<uint32_t>(p.invRow), p.invSlot, npc->targetId, p.gridX, p.gridY,
                    0, 0, 0);
        }
        g_Client.Var(0x18398F8) = p.invRow;
        // TODO(state) out of scope: cGameHud_ResetUiState 0x62AFB0.
        break;
    }
    case 4: {
        g_Client.Var(0x1675B08) = 0;
        int32_t replaceResult = -1;
        if (npc) replaceResult = static_cast<int32_t>(Quest_ReplaceTrackedItem(inv, npc->targetId, npc->required));
        if (!replaceResult || (replaceResult == 1 && g_Client.VarGet(0x16732A8) > 0)) { // g_Inv_ExtraPageCount
            g_Client.Var(0x18398F8) = replaceResult;
            // TODO(state) out of scope: cGameHud_ResetUiState 0x62AFB0.
        }
        progress.objectiveTarget = 2;                                              // g_QuestObjParam1 0x1674600
        if (npc) progress.objectiveProgress = static_cast<int>(npc->required);     // g_QuestObjParam2 0x1674604
        break;
    }
    case 5: {
        // TODO(state) out of scope: UI_EventNoticeWnd_Open(2) 0x6649F0.
        progress.objectiveMode = 0;
        progress.objectiveType = 0;
        progress.objectiveTarget = 0;
        progress.objectiveProgress = 0;
        g_Client.Var(0x1675AE8) = 0;
        g_Client.VarF(0x1675AEC) = g_World.gameTimeSec - 590.0f;
        break;
    }
    case 6:
    case 8:
    case 9:
        progress.objectiveProgress += 1; // g_QuestObjParam2 0x1674604
        break;
    case 7:
    default:
        break; // nothing to do (case 7: message only, already handled elsewhere)
    }
}

} // namespace ts2::game

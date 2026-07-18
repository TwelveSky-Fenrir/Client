// Net/RecvPackets_InvDispatch.h — InvDispatch incoming packet structs (S->C), split from RecvPackets.h.
// Framing: [opcode:u8][payload]. Each struct decodes the payload (LE, byte-exact).
// State-update logic (globals/entity arrays) lives in the matching Net/GameHandlers_InvDispatch.cpp.
#pragma once
#include <cstdint>
#include "Asset/ByteReader.h"

namespace ts2::net {

// Net_OnPlayerEquipVisual (opcode 0x83) — loads the 7 equipment appearance strings (per element).
// visual (364 bytes) = 4 element blocks * 91 bytes ; each block = 7 slots * 13 bytes (appearance name).
struct PlayerEquipVisual {
    uint8_t visual[364]; // payload+0 (visual[91*element + 13*slot], element chosen by g_LocalElement, slot 0..6)
    static PlayerEquipVisual Parse(const uint8_t* payload, size_t len);
};

inline PlayerEquipVisual PlayerEquipVisual::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PlayerEquipVisual p{};
    r.Read(p.visual, sizeof(p.visual)); // 364 bytes
    return p;
}

// Net_OnItemBatchUpdate (opcode 0x95) — batched inventory-cell update via base-10/100 packed indices.
struct ItemBatchUpdate {
    uint32_t header;      // payload+0  (header%1000 = sub-code ; header/1000 = item count, 0..8)
    uint32_t rowPacked;   // payload+4  (inventory ROW indices packed base-10, 1 digit/item)
    uint32_t colPackedLo; // payload+8  (COLUMN indices base-100 for items 0..3)
    uint32_t colPackedHi; // payload+12 (COLUMN indices base-100 for items 4..7)
    uint32_t posPackedLo; // payload+16 (grid positions base-100 for items 0..3)
    uint32_t posPackedHi; // payload+20 (grid positions base-100 for items 4..7)
    uint32_t itemIds[8];  // payload+24 (item id per cell)
    static ItemBatchUpdate Parse(const uint8_t* payload, size_t len);
};

inline ItemBatchUpdate ItemBatchUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemBatchUpdate p{};
    p.header      = r.U32();
    p.rowPacked   = r.U32();
    p.colPackedLo = r.U32();
    p.colPackedHi = r.U32();
    p.posPackedLo = r.U32();
    p.posPackedHi = r.U32();
    r.Read(p.itemIds, sizeof(p.itemIds)); // 32 bytes (8 dwords)
    return p;
}

// Net_OnItemUpgradeResult (opcode 0xa8) — item upgrade result: reapplies the cell + 100-gold cost.
struct ItemUpgradeResult {
    uint32_t status;         // payload+0 (-1 / 0 / 1: success variants, different message)
    uint32_t itemId;         // payload+4  -> g_InvMain
    uint32_t gridX;          // payload+8  -> g_InvGrid_GridX
    uint32_t gridY;          // payload+12 -> g_InvGrid_GridY
    uint32_t count;          // payload+16 -> g_InvGrid_Count
    uint32_t durability;     // payload+20 -> g_InvGrid_Durability
    uint32_t instanceSerial; // payload+24 -> g_InvGrid_InstanceSerial
    static ItemUpgradeResult Parse(const uint8_t* payload, size_t len);
};

inline ItemUpgradeResult ItemUpgradeResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemUpgradeResult p{};
    p.status         = r.U32();
    p.itemId         = r.U32();
    p.gridX          = r.U32();
    p.gridY          = r.U32();
    p.count          = r.U32();
    p.durability     = r.U32();
    p.instanceSerial = r.U32();
    return p;
}

// Net_OnItemEnchantDispatch (opcode 0x75 / 117) — ea=0x4A7410 — tiered enchant result.
// Mega-dispatcher: tier = code%100 (1..5), message index = code/100.
struct ItemEnchantDispatch {
    uint32_t status;  // payload+0  (per-tier sub-state: 0 = success, 1..3 = failures)
    uint32_t code;    // payload+4  (code%100 = tier 1..5 ; code/100 = StrTable offset)
    uint32_t aux0;    // payload+8  (-> g_InvAux)
    uint32_t aux1;    // payload+12 (-> dword_1674ABC)
    uint32_t aux2;    // payload+16 (-> dword_1674AC0)
    static ItemEnchantDispatch Parse(const uint8_t* payload, size_t len);
};

inline ItemEnchantDispatch ItemEnchantDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemEnchantDispatch p{};
    p.status = r.U32();
    p.code   = r.U32();
    p.aux0   = r.U32();  // v35
    p.aux1   = r.U32();  // v37 (payload+12)
    p.aux2   = r.U32();  // v36 (payload+16)
    return p;
}

// Net_OnItemRefineResult (opcode 0x7c / 124, size 13) — ea=0x4A97A0 — item refine result.
struct ItemRefineResult {
    uint32_t status;      // payload+0 (init -1 ; 0/1/2 = result cases)
    uint32_t goldCost;    // payload+4 (g_Currency -= goldCost)
    uint32_t attribDelta; // payload+8 (attribute delta via Bits_AddByte2)
    static ItemRefineResult Parse(const uint8_t* payload, size_t len);
};

inline ItemRefineResult ItemRefineResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemRefineResult p{};
    p.status      = r.U32();
    p.goldCost    = r.U32();
    p.attribDelta = r.U32();
    return p;
}

// Net_OnBulkItemConsume (opcode 0x8d / 141) — ea=0x4AB1F0 — bulk consumption: refund + clears cells.
// Fields packed base-10/base-100 (decoded locally, cf. notes).
struct BulkItemConsume {
    uint32_t code;         // payload+0  (code%1000 = error status 0..13 ; code/1000 = cell count)
    uint32_t rowPack;      // payload+4  (rows packed base-10, digit i = row of cell i)
    uint32_t colPackA;     // payload+8  (columns packed base-100, cells 0..3)
    uint32_t colPackB;     // payload+12 (columns packed base-100, cells 4..7)
    uint32_t gridPackA;    // payload+16 (grid index packed base-100, cells 0..3)
    uint32_t gridPackB;    // payload+20 (grid index packed base-100, cells 4..7)
    uint32_t currencyType; // payload+24 (1/2/3 -> dword_16756F8 / dword_167478C / dword_1674790)
    uint32_t unitPrice;    // payload+28 (amount refunded per cell: currency -= count*unitPrice)
    uint32_t itemIds[8];   // payload+32 (32 bytes: item ids per cell)
    static BulkItemConsume Parse(const uint8_t* payload, size_t len);
};

inline BulkItemConsume BulkItemConsume::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BulkItemConsume p{};
    p.code         = r.U32();
    p.rowPack      = r.U32();
    p.colPackA     = r.U32();
    p.colPackB     = r.U32();
    p.gridPackA    = r.U32();
    p.gridPackB    = r.U32();
    p.currencyType = r.U32();
    p.unitPrice    = r.U32();
    r.Read(p.itemIds, sizeof(p.itemIds));
    return p;
}

// Net_OnItemFuseResult (opcode 0xa9 / 169) — ea=0x4AE750 — fusion of two items (source F08 + target F2C).
struct ItemFuseResult {
    uint32_t status;   // payload+0  (0 = update source only, 1 = update source + target)
    uint32_t subMode;  // payload+4  (effect variant: 1..6, 11, 12)
    uint32_t aux0;     // payload+8  (byte0 delta: Bits_AddByte0)
    uint32_t aux1;     // payload+12 (byte1 delta: Bits_AddByte1)
    uint32_t aux2;     // payload+16 (byte2 delta: Bits_SetByte2)
    static ItemFuseResult Parse(const uint8_t* payload, size_t len);
};

inline ItemFuseResult ItemFuseResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemFuseResult p{};
    p.status  = r.U32();
    p.subMode = r.U32();
    p.aux0    = r.U32();  // v13
    p.aux1    = r.U32();  // v14[0] (payload+12)
    p.aux2    = r.U32();  // v15[0] (payload+16)
    return p;
}

// Net_OnItemDropResult (opcode 0xb3 / 179) — ea=0x4B3440 — item drop result, sets an inventory cell.
struct ItemDropResult {
    uint32_t status;      // payload+0  (0 = success)
    uint32_t goldOrValue; // payload+4  (dword_1675644 = goldOrValue)
    uint32_t invRow;      // payload+8  (inventory row/bag)
    uint32_t invCol;      // payload+12 (inventory column)
    uint32_t itemCell[6]; // payload+16 (24 bytes: id, gridX, gridY, count, durability, serial)
    static ItemDropResult Parse(const uint8_t* payload, size_t len);
};

inline ItemDropResult ItemDropResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemDropResult p{};
    p.status      = r.U32();
    p.goldOrValue = r.U32();
    p.invRow      = r.U32();
    p.invCol      = r.U32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

// Net_OnInventoryBulkLoad (opcode 0x77 / 119 dec) — bulk item load (packed coordinates).
// Payload : [header:u32][rowPacked:u32][colPackedA:u32][colPackedB:u32]
//           [posPackedA:u32][posPackedB:u32][itemIds:u32[8]][durPacked:u32] = 60 bytes.
struct InventoryBulkLoad {
    uint32_t header;      // payload+0 : /1000 = message code, %1000 = item count (<=8)
    uint32_t rowPacked;   // payload+4 : rows packed base-10 (digit i = rowPacked%(10*10^i)/10^i)
    uint32_t colPackedA;  // payload+8 : columns packed base-100 for i<4 (v11)
    uint32_t colPackedB;  // payload+12 : columns packed base-100 for i>=4 (v23)
    uint32_t posPackedA;  // payload+16 : grid positions base-100 for i<4 (v21)
    uint32_t posPackedB;  // payload+20 : grid positions base-100 for i>=4 (v17)
    uint32_t itemIds[8];  // payload+24 : 8 item IDs (v18)
    uint32_t durPacked;   // payload+56 : packed durability/flags (v24)
    // STATE: implemented — Net/GameHandlers_InvDispatch.cpp (0x77): decodes row/column/position
    //   (base-10/100) and writes g_InvMain/g_InvGrid_* for i<header%1000 (<=8), durability via
    //   Bits_PackByte012(durPacked) (default path). Remaining TODO(item)/TODO(msg) (outside
    //   TODO(state)/TODO(send)): MobDb_GetEntry(itemIds[i]) guard and message str1849/1788/2999
    //   selection by header/1000 — require ItemDefTbl, absent from the client model (cf. identical
    //   justification in GameHandlers_BossWorld.cpp ## MountTicketPrompt).
    static InventoryBulkLoad Parse(const uint8_t* payload, size_t len);
};

inline InventoryBulkLoad InventoryBulkLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    InventoryBulkLoad p{};
    p.header = r.U32();
    p.rowPacked = r.U32();
    p.colPackedA = r.U32();
    p.colPackedB = r.U32();
    p.posPackedA = r.U32();
    p.posPackedB = r.U32();
    for (int i = 0; i < 8; ++i) p.itemIds[i] = r.U32();
    p.durPacked = r.U32();
    return p;
}

// Net_OnMultiItemRemove (opcode 0x97 / 151 dec) — removal of several inventory cells (packed coordinates).
// Payload : [resultCode:u32][rowPackedA:u32][rowPackedB:u32][colPackedA:u32][colPackedB:u32][count:u32] = 24 bytes.
struct MultiItemRemove {
    uint32_t resultCode;  // payload+0 : 0 = removal, 1 = error(str2246), 2 = error(str2247)
    uint32_t rowPackedA;  // payload+4 : 4 row indices packed base-100 (v22)
    uint32_t rowPackedB;  // payload+8 : 5th row index (v23 % 100)
    uint32_t colPackedA;  // payload+12 : 4 column indices packed base-100 (v8)
    uint32_t colPackedB;  // payload+16 : 5th column index (v9 % 100)
    uint32_t count;       // payload+20 : number of cells to clear - 1 (loop i<count+1)
    // STATE: implemented — Net/GameHandlers_InvDispatch.cpp (0x97): decodes up to 5 (row,col)
    //   base-100 and clears g_InvMain/g_InvGrid_* for k in [0,count] (message str2259) ;
    //   resultCode 1/2 -> messages str2246/str2247.
    static MultiItemRemove Parse(const uint8_t* payload, size_t len);
};

inline MultiItemRemove MultiItemRemove::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    MultiItemRemove p{};
    p.resultCode = r.U32();
    p.rowPackedA = r.U32();
    p.rowPackedB = r.U32();
    p.colPackedA = r.U32();
    p.colPackedB = r.U32();
    p.count = r.U32();
    return p;
}

// Net_OnItemSocketDispatch (opcode 0xab / 171 dec) — MEGA-DISPATCHER socketing/gems (5 cases).
// Payload : [resultCode:u32][actionType:u32][cost:u32][itemSnapshot:u32[6]] = 36 bytes.
struct ItemSocketDispatch {
    uint32_t resultCode;       // payload+0 : main selector (switch 0..4)
    uint32_t actionType;       // payload+4 : action sub-type (v25[0]) driving messages & logic
    uint32_t cost;             // payload+8 : gold cost (subtracted from g_Currency)
    uint32_t itemSnapshot[6];  // payload+12 : item snapshot (id, gridX, gridY, count, durability, serial) (v23)
    // STATE: implemented — Net/GameHandlers_InvDispatch.cpp (0xab): g_Currency-=cost ; switch
    //   resultCode 0 (writes itemSnapshot into the pending-move slot), 1 (moves from pending),
    //   2/3/4 (clears 1/2/3 cells). Remaining TODO(state): MobDb_GetEntry(dword_1822F08) guard
    //   (ItemDefTbl absent, cf. justification in GameHandlers_BossWorld.cpp), skill node recompute
    //   (Skill_UnpackTreeNodes, actionType in {1,6,7} — source operands unconfirmed for this call
    //   site) and exact message selection by actionType (str222/3390/2710).
    static ItemSocketDispatch Parse(const uint8_t* payload, size_t len);
};

inline ItemSocketDispatch ItemSocketDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSocketDispatch p{};
    p.resultCode = r.U32();
    p.actionType = r.U32();
    p.cost = r.U32();
    for (int i = 0; i < 6; ++i) p.itemSnapshot[i] = r.U32();
    return p;
}

// Net_OnStatSyncDispatch (opcode 0xb4 / 180 dec) — gold/weight sync + inventory-action result (4 cases).
// Payload : [resultCode:u32][invWeight:u32][currency:u32][counter:u32][durability:u32] = 20 bytes.
struct StatSyncDispatch {
    uint32_t resultCode;  // payload+0 : selector (switch 0..3)
    uint32_t invWeight;   // payload+4 : new inventory weight -> g_InvWeight
    uint32_t currency;    // payload+8 : new gold -> g_Currency and dword_1687254[0]
    uint32_t counter;     // payload+12 : counter -> dword_16746E8
    uint32_t durability;  // payload+16 : durability -> dword_1822F18 (pending-move item)
    // STATE: implemented — Net/GameHandlers_InvDispatch.cpp (0xb4): unconditionally applies
    //   g_InvWeight/g_Currency/dword_16746E8, then switch resultCode 0/1/2 (writes the pending-move
    //   item with durability, clears the old slot, message str2748/2749/654) and 3 (clears both
    //   slots, str224). MobDb_GetEntry(dword_1822F08) guard and its sound (item type 6..21) not
    //   reproduced (ItemDefTbl/audio outside the network scope).
    static StatSyncDispatch Parse(const uint8_t* payload, size_t len);
};

inline StatSyncDispatch StatSyncDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    StatSyncDispatch p{};
    p.resultCode = r.U32();
    p.invWeight = r.U32();
    p.currency = r.U32();
    p.counter = r.U32();
    p.durability = r.U32();
    return p;
}

// Net_OnItemRefineDispatch (opcode 0xac) — MEGA-DISPATCHER item refine/upgrade result.
// Huge switch on `op` (many sub-cases) ; only the 4-u32 header is read from the payload.
struct ItemRefineDispatch {
    uint32_t op; // payload+0   sub-opcode / result code driving the switch
    uint32_t a;  // payload+4   parameter 1 (context depends on op)
    uint32_t b;  // payload+8   parameter 2
    uint32_t c;  // payload+12  parameter 3
    static ItemRefineDispatch Parse(const uint8_t* payload, size_t len);
};

inline ItemRefineDispatch ItemRefineDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemRefineDispatch p{};
    p.op = r.U32();
    p.a  = r.U32();
    p.b  = r.U32();
    p.c  = r.U32();
    return p;
}

// Net_OnItemEnhanceResult (opcode 0xaf / 175) — item enhance/enchant result. Payload 12 bytes (size_table = 13).
struct ItemEnhanceResult {
    uint32_t resultCode;  // payload+0 (v7) — 1 = success, 2 = failure/downgrade
    uint32_t cost;        // payload+4 (v6) — cost (g_Currency -= cost)
    uint32_t enhanceByte; // payload+8 (v5) — level value written into durability byte 2
    static ItemEnhanceResult Parse(const uint8_t* payload, size_t len);
};

inline ItemEnhanceResult ItemEnhanceResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemEnhanceResult p{};
    p.resultCode = r.U32(); p.cost = r.U32(); p.enhanceByte = r.U32();
    return p;
}

// Net_OnItemSocketResult (opcode 0x9b) — item socket result.
// Payload = 16 bytes.
struct ItemSocketResult {
    uint32_t status;    // payload+0  : combined (status/100 = message variant, status%100 = case 0/1)
    uint32_t socket0;   // payload+4  : socket 0 value (-> dword_1822F20)
    uint32_t socket1;   // payload+8  : socket 1 value (-> dword_1822F24)
    uint32_t socket2;   // payload+12 : socket 2 value (-> dword_1822F28)
    static ItemSocketResult Parse(const uint8_t* payload, size_t len);
};

inline ItemSocketResult ItemSocketResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSocketResult p{};
    p.status  = r.U32();
    p.socket0 = r.U32();
    p.socket1 = r.U32();
    p.socket2 = r.U32();
    return p;
}

// Net_OnItemEnhanceResult2 (opcode 0xb0) — item enhance result variant (6 fields).
// Payload = 24 bytes. The 4 statByte fields are repacked into durability via Bits_PackByte012 + Bits_SetByte3.
struct ItemEnhanceResult2 {
    uint32_t resultCode;  // payload+0  : 1 = enhance success ; 10..14 = transmutation branch (-1000 currency)
    uint32_t newItemId;   // payload+4  : new item id (used in branch 10..14 -> dword_1822F08)
    uint32_t statByte0;   // payload+8  : durability byte 0 (Bits_PackByte012 arg0)
    uint32_t statByte1;   // payload+12 : byte 1 (arg1)
    uint32_t statByte2;   // payload+16 : byte 2 (arg2)
    uint32_t statByte3;   // payload+20 : byte 3 (Bits_SetByte3)
    static ItemEnhanceResult2 Parse(const uint8_t* payload, size_t len);
};

inline ItemEnhanceResult2 ItemEnhanceResult2::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemEnhanceResult2 p{};
    p.resultCode = r.U32();
    p.newItemId  = r.U32();
    p.statByte0  = r.U32();
    p.statByte1  = r.U32();
    p.statByte2  = r.U32();
    p.statByte3  = r.U32();
    return p;
}

// Pkt_ItemUpgradeResult (opcode 0x1b / 27, size 13) — ea=0x488DE0 — raw item
// upgrade/enchant result (historical IDA name colliding with the "ItemUpgradeResult"
// struct above, which actually covers Net_OnItemUpgradeResult 0xa8 ;
// disambiguated here as LegacyItemUpgradeResult — AUDIT: opcode absent from RE/handler_domains.json).
struct LegacyItemUpgradeResult {
    uint32_t resultCode;    // payload+0 (switch 0..7 : 0/1=success, 2=failure, 3/6=degradation, 5/7=other)
    uint32_t cost;          // payload+4 (gold, dword_16732AC -= cost)
    uint32_t newLevelDelta; // payload+8 (merged via Bits_AddByte0(dword_1822F18, val))
    static LegacyItemUpgradeResult Parse(const uint8_t* payload, size_t len);
};

inline LegacyItemUpgradeResult LegacyItemUpgradeResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    LegacyItemUpgradeResult p{};
    p.resultCode    = r.U32();
    p.cost          = r.U32();
    p.newLevelDelta = r.U32();
    return p;
}

// Pkt_ItemRefineResult (opcode 0x1c / 28, size 9) — ea=0x48A530 — raw item
// refine/socket result (historical IDA name colliding with the "ItemRefineResult"
// struct above, which actually covers Net_OnItemRefineResult 0x7c ;
// disambiguated here as LegacyItemRefineResult — AUDIT: opcode absent from RE/handler_domains.json).
struct LegacyItemRefineResult {
    uint32_t resultCode; // payload+0 (switch 0..3 : 0=success, 1=success+2nd item, 2=failure, 3=other)
    uint32_t cost;       // payload+4 (gold, dword_16732AC -= cost)
    static LegacyItemRefineResult Parse(const uint8_t* payload, size_t len);
};

inline LegacyItemRefineResult LegacyItemRefineResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    LegacyItemRefineResult p{};
    p.resultCode = r.U32();
    p.cost       = r.U32();
    return p;
}

} // namespace ts2::net

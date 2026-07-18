// Net/RecvPackets_InvCells.h — InvCells incoming packet structs (S->C), split from RecvPackets.h.
// Framing: [opcode:u8][payload]. Each struct decodes the payload (LE, byte-exact).
// State-update logic (globals/entity arrays) lives in the matching Net/GameHandlers_InvCells.cpp.
#pragma once
#include <cstdint>
#include "Asset/ByteReader.h"

namespace ts2::net {

// Pkt_ItemResultSimple (opcode 0x21) — simple item-action result: rewrites an inventory cell.
struct ItemResultSimple {
    uint32_t status;         // payload+0 (0 = success, reapplies the cell)
    uint32_t itemId;         // payload+4  -> g_InvMain
    uint32_t gridX;          // payload+8  -> g_InvGrid_GridX
    uint32_t gridY;          // payload+12 -> g_InvGrid_GridY
    uint32_t count;          // payload+16 -> g_InvGrid_Count
    uint32_t durability;     // payload+20 -> g_InvGrid_Durability
    uint32_t instanceSerial; // payload+24 -> g_InvGrid_InstanceSerial
    static ItemResultSimple Parse(const uint8_t* payload, size_t len);
};

inline ItemResultSimple ItemResultSimple::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemResultSimple p{};
    p.status         = r.U32();
    p.itemId         = r.U32();
    p.gridX          = r.U32();
    p.gridY          = r.U32();
    p.count          = r.U32();
    p.durability     = r.U32();
    p.instanceSerial = r.U32();
    return p;
}

// Pkt_ItemCombineResult (opcode 0x1d) — item combine/craft result (size table = 33).
struct ItemCombineResult {
    uint32_t resultCode;   // payload+0   0/1/10 -> success/failure/variant
    uint32_t weightDelta;  // payload+4   weight removed from inventory (g_InvWeight -= v)
    uint32_t itemId;       // payload+8   resulting item id
    uint32_t gridX;        // payload+12
    uint32_t gridY;        // payload+16
    uint32_t count;        // payload+20  quantity
    uint32_t durability;   // payload+24
    uint32_t serial;       // payload+28  instance serial
    static ItemCombineResult Parse(const uint8_t* payload, size_t len);
};

inline ItemCombineResult ItemCombineResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCombineResult p{};
    p.resultCode  = r.U32();
    p.weightDelta = r.U32();
    p.itemId      = r.U32();
    p.gridX       = r.U32();
    p.gridY       = r.U32();
    p.count       = r.U32();
    p.durability  = r.U32();
    p.serial      = r.U32();
    return p;
}

// Net_OnItemCellSet (opcode 0x69) — places an item (6 dwords) into an inventory grid cell.
struct ItemCellSet {
    uint32_t resultCode; // payload+0   0=success (places the item), other=ignored
    uint32_t itemId;     // payload+4
    uint32_t gridX;      // payload+8
    uint32_t gridY;      // payload+12
    uint32_t count;      // payload+16
    uint32_t durability;  // payload+20
    uint32_t serial;     // payload+24  instance serial
    static ItemCellSet Parse(const uint8_t* payload, size_t len);
};

inline ItemCellSet ItemCellSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCellSet p{};
    p.resultCode = r.U32();
    p.itemId     = r.U32();
    p.gridX      = r.U32();
    p.gridY      = r.U32();
    p.count      = r.U32();
    p.durability = r.U32();
    p.serial     = r.U32();
    return p;
}

// Net_OnItemCombineResult (opcode 0x70) — combine/socket result, updates 1 or 2 grid cells.
struct ItemCombineResult2 {
    uint32_t resultCode;  // payload+0   0/1/2 -> update variants
    uint32_t weightDelta; // payload+4   weight removed (g_InvWeight -= v)
    uint32_t itemId;      // payload+8
    uint32_t gridX;       // payload+12
    uint32_t gridY;       // payload+16
    uint32_t count;       // payload+20
    uint32_t durability;  // payload+24
    uint32_t serial;      // payload+28  instance serial
    static ItemCombineResult2 Parse(const uint8_t* payload, size_t len);
};

inline ItemCombineResult2 ItemCombineResult2::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCombineResult2 p{};
    p.resultCode  = r.U32();
    p.weightDelta = r.U32();
    p.itemId      = r.U32();
    p.gridX       = r.U32();
    p.gridY       = r.U32();
    p.count       = r.U32();
    p.durability  = r.U32();
    p.serial      = r.U32();
    return p;
}

// Net_OnItemPlaceResult (opcode 0x7a) — result of placing an item into a cell (6 u32).
struct ItemPlaceResult {
    uint32_t resultCode; // payload+0   0/1=placed, 2=error
    uint32_t itemId;     // payload+4
    uint32_t bagRow;     // payload+8   bag/tab (row index, *384)
    uint32_t slotCol;    // payload+12  column (index, *6)
    uint32_t cellIndex;  // payload+16  gridX = cellIndex%8, gridY = cellIndex/8
    uint32_t durability; // payload+20
    static ItemPlaceResult Parse(const uint8_t* payload, size_t len);
};

inline ItemPlaceResult ItemPlaceResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemPlaceResult p{};
    p.resultCode = r.U32();
    p.itemId     = r.U32();
    p.bagRow     = r.U32();
    p.slotCol    = r.U32();
    p.cellIndex  = r.U32();
    p.durability = r.U32();
    return p;
}

// Net_OnItemCountNotice (opcode 0x8c) — item quantity notification (str 2074/1351).
struct ItemCountNotice {
    uint32_t subop;  // payload+0   0 or 1 (label choice)
    uint32_t count;  // payload+4   displayed quantity
    static ItemCountNotice Parse(const uint8_t* payload, size_t len);
};

inline ItemCountNotice ItemCountNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCountNotice p{};
    p.subop = r.U32();
    p.count = r.U32();
    return p;
}

// Net_OnItemMoveResult (opcode 0x92) — item move result, writes a cell (str 223/117/119).
struct ItemMoveResult {
    uint32_t resultCode; // payload+0   0=success (writes the cell), 1/2=errors
    uint32_t itemId;     // payload+4
    uint32_t bagRow;     // payload+8   bag/tab (*384)
    uint32_t slotCol;    // payload+12  column (*6)
    uint32_t cellIndex;  // payload+16  gridX = cellIndex%8, gridY = cellIndex/8
    static ItemMoveResult Parse(const uint8_t* payload, size_t len);
};

inline ItemMoveResult ItemMoveResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemMoveResult p{};
    p.resultCode = r.U32();
    p.itemId     = r.U32();
    p.bagRow     = r.U32();
    p.slotCol    = r.U32();
    p.cellIndex  = r.U32();
    return p;
}

// Net_OnItemCellReset (opcode 0xb6) — clears a grid cell and stores coordinates (size table = 25).
struct ItemCellReset {
    uint32_t flag;      // payload+0    indicator/state (read as 4 bytes, only context matters)
    uint32_t bagRow;    // payload+4    bag/tab (*384)
    uint32_t slotCol;   // payload+8    column (*6)
    uint32_t coordA;    // payload+12   -> dword_1675118
    uint32_t coordB;    // payload+16   -> dword_167511C
    uint32_t coordC;    // payload+20   -> dword_1675120
    static ItemCellReset Parse(const uint8_t* payload, size_t len);
};

inline ItemCellReset ItemCellReset::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCellReset p{};
    p.flag    = r.U32();
    p.bagRow  = r.U32();
    p.slotCol = r.U32();
    p.coordA  = r.U32();
    p.coordB  = r.U32();
    p.coordC  = r.U32();
    return p;
}

// Pkt_ItemSwapResultA (opcode 0x1e / 30) — item move/swap result.
// Payload read: resultCode(+0) weightDelta(+4) itemCell[6](+8, 24 bytes). Total 32 bytes.
struct ItemSwapResultA {
    uint32_t resultCode;   // payload+0 : 0 = plain OK, 1 = OK with item, 2 = OK swap
    int32_t  weightDelta;  // payload+4 : subtracted from g_InvWeight
    uint32_t itemCell[6];  // payload+8 : {itemId, gridX, gridY, count, durability, instanceSerial}
    static ItemSwapResultA Parse(const uint8_t* payload, size_t len);
};

inline ItemSwapResultA ItemSwapResultA::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSwapResultA p{};
    p.resultCode = r.U32();
    p.weightDelta = r.I32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

// Net_OnItemSellResult (opcode 0x6a / 106) — item sell result (adds gold, refills the cell).
// Payload read: resultCode(+0) weightDelta(+4) itemCell[6](+8, 24 bytes). Total 32 bytes.
struct ItemSellResult {
    uint32_t resultCode;   // payload+0 : 0 = success, 1 = failure
    int32_t  weightDelta;  // payload+4 : added to g_InvWeight
    uint32_t itemCell[6];  // payload+8 : {itemId, gridX, gridY, count, durability, instanceSerial}
    static ItemSellResult Parse(const uint8_t* payload, size_t len);
};

inline ItemSellResult ItemSellResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSellResult p{};
    p.resultCode = r.U32();
    p.weightDelta = r.I32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

// Net_OnItemCellClear (opcode 0x8a / 138, size table 21) — sets/clears an inventory grid cell.
// Payload read: resultCode(+0) itemId(+4) invPage(+8) invSlot(+12) gridPos(+16). Total 20 bytes.
struct ItemCellClear {
    uint32_t resultCode;  // payload+0  : 0 = apply
    uint32_t itemId;      // payload+4
    uint32_t invPage;     // payload+8  (v2)
    uint32_t invSlot;     // payload+12 (v4)
    uint32_t gridPos;     // payload+16 (v1 ; gridX=gridPos%8, gridY=gridPos/8)
    static ItemCellClear Parse(const uint8_t* payload, size_t len);
};

inline ItemCellClear ItemCellClear::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemCellClear p{};
    p.resultCode = r.U32();
    p.itemId = r.U32();
    p.invPage = r.U32();
    p.invSlot = r.U32();
    p.gridPos = r.U32();
    return p;
}

// Net_OnItemBuyResult (opcode 0xa4 / 164, size table 29) — purchase result, deducts money, fills the cell.
// Payload read: resultCode(+0) itemCell[6](+4, 24 bytes). Total 28 bytes.
struct ItemBuyResult {
    uint32_t resultCode;   // payload+0 : 0 or 1
    uint32_t itemCell[6];  // payload+4 : {itemId, gridX, gridY, count, durability, instanceSerial}
    static ItemBuyResult Parse(const uint8_t* payload, size_t len);
};

inline ItemBuyResult ItemBuyResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemBuyResult p{};
    p.resultCode = r.U32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

// Net_OnItemSlotRefresh (opcode 0xad / 173) — refreshes a cell from 6 dwords + sound, deducts gold.
// Payload read: resultCode(+0) goldDelta(+4) itemCell[6](+8, 24 bytes). Total 32 bytes.
struct ItemSlotRefresh {
    uint32_t resultCode;   // payload+0 : 0/10 = success, 1/2 = failure
    int32_t  goldDelta;    // payload+4 : deducted from g_Currency
    uint32_t itemCell[6];  // payload+8 : {itemId, gridX, gridY, count, durability, instanceSerial}
    static ItemSlotRefresh Parse(const uint8_t* payload, size_t len);
};

inline ItemSlotRefresh ItemSlotRefresh::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSlotRefresh p{};
    p.resultCode = r.U32();
    p.goldDelta = r.I32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

// Pkt_ItemSwapResultB (opcode 0x1f / 31) — item move/swap result (variant).
// Fixed 32-byte payload. resultCode selects 3 branches (0 / 1 / 2).
struct ItemSwapResultB {
    uint32_t resultCode;  // payload+0  (v9) — 0=direct commit, 1=commit via scratch, 2=swap
    uint32_t weightDelta; // payload+4  (v7) — weight removed (g_InvWeight -= weightDelta)
    uint32_t item[6];     // payload+8  (v8) — itemId, gridX, gridY, count, durability, instanceSerial
    static ItemSwapResultB Parse(const uint8_t* payload, size_t len);
};

inline ItemSwapResultB ItemSwapResultB::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemSwapResultB p{};
    p.resultCode = r.U32(); p.weightDelta = r.U32();
    r.Read(p.item, sizeof(p.item));
    return p;
}

// Net_OnGambleResult (opcode 0x6b / 107) — lottery/gamble result ; disconnects on failure. Payload 8 bytes (size_table = 9).
struct GambleResult {
    uint32_t selector; // payload+0 (v4) — 1=win, 2=end/failure, 3=info
    uint32_t value;    // payload+4 (v6) — displayed amount/counter
    static GambleResult Parse(const uint8_t* payload, size_t len);
};

inline GambleResult GambleResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GambleResult p{};
    p.selector = r.U32(); p.value = r.U32();
    return p;
}

// Net_OnEquipSlotUpdate (opcode 0x78 / 120) — updates an equipment slot and clears the source cell. Payload 28 bytes (size_table = 29).
struct EquipSlotUpdate {
    uint32_t invRow;  // payload+0  (v6) — source inventory row
    uint32_t invCol;  // payload+4  (v7) — source inventory column
    uint32_t contRow; // payload+8  (v1) — equipment container row (g_Container5, stride 42)
    uint32_t contCol; // payload+12 (v2) — container column (stride 3)
    uint32_t itemId;  // payload+16 (v4) — equipped item id
    uint32_t field1;  // payload+20 (v5) -> dword_1674400
    uint32_t field2;  // payload+24 (v3) -> dword_1674404
    static EquipSlotUpdate Parse(const uint8_t* payload, size_t len);
};

inline EquipSlotUpdate EquipSlotUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    EquipSlotUpdate p{};
    p.invRow = r.U32(); p.invCol = r.U32();
    p.contRow = r.U32(); p.contCol = r.U32();
    p.itemId = r.U32(); p.field1 = r.U32(); p.field2 = r.U32();
    return p;
}

// Net_OnChargeStackUpdate (opcode 0xa5 / 165) — updates charge stacks (auto-potion belt). Payload 12 bytes.
struct ChargeStackUpdate {
    uint32_t flag;  // payload+0 (v3) — 0 => apply
    uint32_t mode;  // payload+4 (v1) — 0 = consumption, 1 = recharge
    uint32_t index; // payload+8 (v2) — slot index (dword_16757D8[index], g_AutoPotionBelt[index])
    static ChargeStackUpdate Parse(const uint8_t* payload, size_t len);
};

inline ChargeStackUpdate ChargeStackUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ChargeStackUpdate p{};
    p.flag = r.U32(); p.mode = r.U32(); p.index = r.U32();
    return p;
}

// Pkt_ItemDiscardResult (opcode 0x20, size 37 = 1 opcode + 36 payload) — item discard/removal result.
// Payload = 36 bytes.
struct ItemDiscardResult {
    uint32_t resultCode;      // payload+0  : code (0/1 = messages, 40 = 3 cells restored, 100/101 = aux slot)
    uint32_t itemId;          // payload+4  : item id written into the source cell
    uint32_t gridX;           // payload+8  : grid column
    uint32_t gridY;           // payload+12 : grid row
    uint32_t count;           // payload+16 : quantity
    uint32_t durability;      // payload+20 : durability
    uint32_t instanceSerial;  // payload+24 : instance serial
    uint32_t adjustMode;      // payload+28 : 1 = adjust weight, 2 = adjust currency
    uint32_t amount;          // payload+32 : delta (weight or currency) to subtract
    static ItemDiscardResult Parse(const uint8_t* payload, size_t len);
};

inline ItemDiscardResult ItemDiscardResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ItemDiscardResult p{};
    p.resultCode     = r.U32();
    p.itemId         = r.U32();
    p.gridX          = r.U32();
    p.gridY          = r.U32();
    p.count          = r.U32();
    p.durability     = r.U32();
    p.instanceSerial = r.U32();
    p.adjustMode     = r.U32();
    p.amount         = r.U32();
    return p;
}

// Net_OnCraftResultNotice (opcode 0x74, size 13 = 1 opcode + 12 payload) — craft/production result message.
// Payload = 12 bytes.
struct CraftResultNotice {
    uint32_t mode;    // payload+0 : 0 or 1 (selects the message branch)
    uint32_t count;   // payload+4 : quantity (if >1 in mode 1 -> StrTable005(1479))
    uint32_t value;   // payload+8 : displayed value/id ("[%d]%s")
    static CraftResultNotice Parse(const uint8_t* payload, size_t len);
};

inline CraftResultNotice CraftResultNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    CraftResultNotice p{};
    p.mode  = r.U32();
    p.count = r.U32();
    p.value = r.U32();
    return p;
}

// Net_OnUpgradeCountNotice (opcode 0x8e) — upgrade-counter notices.
// Payload = 8 bytes.
struct UpgradeCountNotice {
    uint32_t mode;    // payload+0 : 0, 1 or 2 (message string choice)
    uint32_t count;   // payload+4 : displayed counter ("%d%s")
    static UpgradeCountNotice Parse(const uint8_t* payload, size_t len);
};

inline UpgradeCountNotice UpgradeCountNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    UpgradeCountNotice p{};
    p.mode  = r.U32();
    p.count = r.U32();
    return p;
}

} // namespace ts2::net

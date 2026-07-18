// Net/RecvPackets_VendorTrade.h — VendorTrade incoming packet structs (S->C), split from RecvPackets.h.
// Framing: [opcode:u8][payload]. Each struct decodes the payload (LE, byte-exact).
// State-update logic (globals/entity arrays) lives in the matching Net/GameHandlers_VendorTrade.cpp.
#pragma once
#include <cstdint>
#include "Asset/ByteReader.h"

namespace ts2::net {

// Pkt_PlayerShopBuyResult (opcode 0x88) — player-shop buy/update result + resyncs the vendor grid.
// shopBlock (824 bytes): a string prefix (vendor name, used as "%s%s"), then a 5x5 item-entry grid
// (5 dwords/cell starting at +16, stride 20 bytes, row base 100 bytes) and a 5x5 price block (3 dwords/cell at +516, stride 12 bytes, row base 60 bytes).
struct PlayerShopBuyResult {
    uint32_t resultCode;   // payload+0   (0/1000 = resync success ; 100/1..5/'e'..'i' = error codes -> messages)
    uint8_t  shopBlock[824];// payload+4   -> vendor name + 5x5 grid (dword_18229EC..) + prices (dword_1822D70..)
    uint32_t dstRow;       // payload+828 (destination inventory row, base *384)
    uint32_t dstCol;       // payload+832 (column, base *6)
    uint32_t itemCell[10]; // payload+836 (item+aux cell: id,gridX,gridY,count,durab,serial,aux0,aux1,aux2,weight)
    static PlayerShopBuyResult Parse(const uint8_t* payload, size_t len);
};

inline PlayerShopBuyResult PlayerShopBuyResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PlayerShopBuyResult p{};
    p.resultCode = r.U32();
    r.Read(p.shopBlock, sizeof(p.shopBlock)); // 824 bytes
    p.dstRow = r.U32();
    p.dstCol = r.U32();
    r.Read(p.itemCell, sizeof(p.itemCell));   // 40 bytes (10 dwords)
    return p;
}

// Pkt_DuelResult (opcode 0x2c) — duel/warehouse result ; opens the warehouse window if flag==1.
struct DuelResult {
    uint32_t param; // payload+0 (passed to UI_Warehouse_Open as the 2nd arg)
    uint32_t flag;  // payload+4 (==1 : opens the warehouse in mode 2)
    static DuelResult Parse(const uint8_t* payload, size_t len);
};

inline DuelResult DuelResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    DuelResult p{};
    p.param = r.U32();
    p.flag  = r.U32();
    return p;
}

// Pkt_TradeActionResult (opcode 0x33) — trade accept/cancel result, resets trade state.
struct TradeActionResult {
    uint32_t code; // payload+0 (0=cancelled, 1/2=accepted by self/partner, 3=completed)
    static TradeActionResult Parse(const uint8_t* payload, size_t len);
};

inline TradeActionResult TradeActionResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeActionResult p{};
    p.code = r.U32();
    return p;
}

// Net_OnVendorInventoryLoad (opcode 0x6d) — loads the vendor item table and rebuilds the per-tab grid.
struct VendorInventoryLoad {
    uint32_t status;             // payload+0 (0 = builds the grid ; !=0 = sets dword_1837E64=1)
    uint32_t param;              // payload+4 -> dword_1837E6C (vendor id/param)
    uint32_t shopItemTable[2240];// payload+8 -> g_ShopItemTable (8960 bytes = 2240 dwords)
    static VendorInventoryLoad Parse(const uint8_t* payload, size_t len);
};

inline VendorInventoryLoad VendorInventoryLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    VendorInventoryLoad p{};
    p.status = r.U32();
    p.param  = r.U32();
    r.Read(p.shopItemTable, sizeof(p.shopItemTable)); // 8960 bytes
    return p;
}

// Pkt_WarehouseOpen (opcode 0x22 / 34) — ea=0x48CB00 — warehouse open/result (1232-byte blob).
struct WarehouseOpen {
    uint32_t status;      // payload+0 (0 = open, 100..105 = result messages)
    uint8_t  blob[1232];  // payload+4 (warehouse grid, copied into dword_18229CC)
    static WarehouseOpen Parse(const uint8_t* payload, size_t len);
};

inline WarehouseOpen WarehouseOpen::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WarehouseOpen p{};
    p.status = r.U32();
    r.Read(p.blob, sizeof(p.blob));
    return p;
}

// Pkt_PlayerShopGoldResult (opcode 0x89 / 137) — ea=0x48E660 — player-shop gold/settlement result.
struct PlayerShopGoldResult {
    uint32_t status;       // payload+0 (0 = success, 1..4 = errors)
    uint32_t weightDelta;  // payload+4 (g_InvWeight += weightDelta)
    uint32_t goldDelta;    // payload+8 (dword_1675620 += goldDelta)
    static PlayerShopGoldResult Parse(const uint8_t* payload, size_t len);
};

inline PlayerShopGoldResult PlayerShopGoldResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PlayerShopGoldResult p{};
    p.status      = r.U32();
    p.weightDelta = r.U32();
    p.goldDelta   = r.U32();
    return p;
}

// Pkt_RepairResult (opcode 0x2d / 45) — ea=0x48F7B0 — repair result / gold change.
struct RepairResult {
    uint32_t status;        // payload+0 (0..7)
    uint32_t goldRemaining; // payload+4  (new gold ; dword_1823B4C = goldRemaining)
    uint32_t invRow;        // payload+8  (inventory row/bag)
    uint32_t invCol;        // payload+12 (inventory column)
    uint32_t itemCell[6];   // payload+16 (24 bytes: id, gridX, gridY, count, durability, serial)
    static RepairResult Parse(const uint8_t* payload, size_t len);
};

inline RepairResult RepairResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RepairResult p{};
    p.status        = r.U32();
    p.goldRemaining = r.U32();
    p.invRow        = r.U32();
    p.invCol        = r.U32();
    r.Read(p.itemCell, sizeof(p.itemCell));
    return p;
}

// Net_OnVendorClose (opcode 0x6e / 110, size 1) — ea=0x4A6830 — closes the vendor window.
// NO field read from the payload (the handler only touches globals).
struct VendorClose {
    static VendorClose Parse(const uint8_t* payload, size_t len);
};

inline VendorClose VendorClose::Parse(const uint8_t* payload, size_t len) {
    (void)payload; (void)len;
    return VendorClose{};
}

// Pkt_WarehouseClose (opcode 0x23 / 35 dec) — closes the warehouse/storage UI.
// Payload : [mode:u32] = 4 bytes.
struct WarehouseClose {
    uint32_t mode;  // payload+0 : 1 = close+refocus editbox, 2 = commit grid+reopen
    // STATE: implemented — Net/GameHandlers_VendorTrade.cpp (0x23): mode==1 sets
    //   dword_1687428=0 and dword_18229B8=1 (if dword_1822998) ; mode==2 calls
    //   g_Warehouse.CommitAllToInventory() + message str2110. Remaining pure TODO(ui)
    //   (UI_FocusEditBox/cGameHud_ResetUiState/UI_StorageWin_Open — no additional state).
    static WarehouseClose Parse(const uint8_t* payload, size_t len);
};

inline WarehouseClose WarehouseClose::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WarehouseClose p{};
    p.mode = r.U32();
    return p;
}

// Pkt_WarehouseUpdate (opcode 0x24) — warehouse/storage chest update.
struct WarehouseUpdate {
    uint32_t mode;        // payload+0    0/3=data, 1/2=result messages
    uint8_t  data[1232];  // payload+4    warehouse block (copied into dword_18229CC if mode 0/3)
    static WarehouseUpdate Parse(const uint8_t* payload, size_t len);
};

inline WarehouseUpdate WarehouseUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WarehouseUpdate p{};
    p.mode = r.U32();
    r.Read(p.data, sizeof(p.data));
    return p;
}

// Pkt_VendorItemEntry (opcode 0x25 / 37) — one vendor/search list entry.
// Payload read (unaligned offsets due to name[13]): itemId(+0) name[13](+4) f17(+17) f21(+21) blob[36](+25) price0(+61) price1(+65) price2(+69) listId(+73). Total 77 bytes.
struct VendorItemEntry {
    uint32_t itemId;    // payload+0  -> dword_182613C[idx]
    char     name[13];  // payload+4  (vendor/item name)
    uint32_t field17;   // payload+17 -> dword_182A3A4[idx]
    uint32_t field21;   // payload+21 -> dword_182B344[idx]
    uint8_t  blob[36];  // payload+25 -> unk_182C2E4 + 36*idx
    uint32_t price0;    // payload+61 -> dword_1834F84[3*idx]
    uint32_t price1;    // payload+65 -> dword_1834F88[3*idx]
    uint32_t price2;    // payload+69 -> dword_1834F8C[3*idx]
    uint32_t listId;    // payload+73 -> session/list id (dword_1826138)
    static VendorItemEntry Parse(const uint8_t* payload, size_t len);
};

inline VendorItemEntry VendorItemEntry::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    VendorItemEntry p{};
    p.itemId = r.U32();
    r.Read(p.name, sizeof(p.name));
    p.field17 = r.U32();
    p.field21 = r.U32();
    r.Read(p.blob, sizeof(p.blob));
    p.price0 = r.U32();
    p.price1 = r.U32();
    p.price2 = r.U32();
    p.listId = r.U32();
    return p;
}

// Pkt_TradeResult (opcode 0x26 / 38) — transaction result (sell/warehouse). Payload 52 bytes (size_table = 53).
// resultCode: switch 0..8 (0=sold, 1..5/7/8=errors, 6=cancel/refund).
struct TradeResult {
    uint32_t resultCode;  // payload+0  (v25) — selector 0..8
    uint32_t weightDelta; // payload+4  (v20) — weight added/removed
    uint32_t invRow;      // payload+8  (v21) — inventory tab/row
    uint32_t invCol;      // payload+12 (v19) — inventory column
    uint32_t item[6];     // payload+16 (v23) — itemId, gridX, gridY, count, durability, instanceSerial
    uint32_t aux0;        // payload+40 (v22) — g_InvAux
    uint32_t aux1;        // payload+44 (v26) — dword_1674ABC
    uint32_t aux2;        // payload+48 (v24) — dword_1674AC0
    static TradeResult Parse(const uint8_t* payload, size_t len);
};

inline TradeResult TradeResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeResult p{};
    p.resultCode = r.U32(); p.weightDelta = r.U32();
    p.invRow = r.U32(); p.invCol = r.U32();
    r.Read(p.item, sizeof(p.item));
    p.aux0 = r.U32(); p.aux1 = r.U32(); p.aux2 = r.U32();
    return p;
}

// Pkt_TradeRequestPrompt (opcode 0x31 / 49) — incoming trade invite. Payload 20 bytes.
struct TradeRequestPrompt {
    uint32_t partner[3]; // payload+0  (v7) — partner identity: idLo, val1, val2
    uint32_t promptId;   // payload+12 (v4) — id shown in brackets (%d)
    uint32_t extra;      // payload+16 (v5) — additional value -> dword_1675D84
    static TradeRequestPrompt Parse(const uint8_t* payload, size_t len);
};

inline TradeRequestPrompt TradeRequestPrompt::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeRequestPrompt p{};
    r.Read(p.partner, sizeof(p.partner));
    p.promptId = r.U32(); p.extra = r.U32();
    return p;
}

// Pkt_PlayerShopOpen (opcode 0x87) — opens a player's personal shop grid.
// Payload = 832 bytes. blob = structured sale grid (see notes for the internal layout).
struct PlayerShopOpen {
    uint32_t focusState;   // payload+0 : UI focus state (1 = name editor, 2, 3)
    uint32_t resultCode;   // payload+4 : 0 = open OK, 1 = MsgBox, 100/101/102/103 = system messages
    uint8_t  blob[824];    // payload+8 : shop data (name + 25 item cells + 25 price/qty cells + 2 dwords)
    static PlayerShopOpen Parse(const uint8_t* payload, size_t len);
};

inline PlayerShopOpen PlayerShopOpen::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PlayerShopOpen p{};
    p.focusState = r.U32();
    p.resultCode = r.U32();
    r.Read(p.blob, sizeof(p.blob));
    return p;
}

// Pkt_TradeRequestResult (opcode 0x32, size 5 = 1 opcode + 4 payload) — trade-request result line.
// Payload = 4 bytes.
struct TradeRequestResult {
    uint32_t code;   // payload+0 : code shown in the trade message ("%s [%d]%s")
    static TradeRequestResult Parse(const uint8_t* payload, size_t len);
};

inline TradeRequestResult TradeRequestResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeRequestResult p{};
    p.code = r.U32();
    return p;
}

// Net_OnWarehouseMoveResult (opcode 0x6c) — warehouse move result / weapon rack.
// Payload = 28 bytes.
struct WarehouseMoveResult {
    uint32_t status;   // payload+0  : status code (0 = success for actions 1..4)
    uint32_t action;   // payload+4  : action selector (1..5)
    uint32_t index;    // payload+8  : slot/rack index concerned
    uint32_t invRow;   // payload+12 : target inventory row (384*invRow)
    uint32_t invCol;   // payload+16 : target inventory column (6*invCol)
    uint32_t gridPos;  // payload+20 : linear position (gridX = %8, gridY = /8)
    uint32_t itemId;   // payload+24 : item id placed into the cell
    static WarehouseMoveResult Parse(const uint8_t* payload, size_t len);
};

inline WarehouseMoveResult WarehouseMoveResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WarehouseMoveResult p{};
    p.status  = r.U32();
    p.action  = r.U32();
    p.index   = r.U32();
    p.invRow  = r.U32();
    p.invCol  = r.U32();
    p.gridPos = r.U32();
    p.itemId  = r.U32();
    return p;
}

} // namespace ts2::net

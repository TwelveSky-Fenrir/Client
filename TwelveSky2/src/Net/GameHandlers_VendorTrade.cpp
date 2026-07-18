// Net/GameHandlers_VendorTrade.cpp — routes VENDOR / TRADE / WAREHOUSE /
// PLAYER-SHOP / REPAIR packets to runtime state (game::g_Client).
//
// "vendor_trade" domain (RE/handler_domains.json). Faithful translation of the
// original semantics described in RE/net_handler_notes.md: each handler only
// updates visible state (inventory, gold/weight, message log, UI flags);
// automatic sends (Net_SendOp*) and exact UI rendering are left as
// `// TODO(send)` / `// TODO(ui)`. Long-tail scalar globals (dword_XXXX)
// go through the g_Client.Var(addr) escape hatch.
//   0x22 WarehouseOpen   0x23 WarehouseClose  0x24 WarehouseUpdate
//   0x25 VendorItemEntry 0x26 TradeResult     0x2c DuelResult
//   0x2d RepairResult    0x31 TradeRequestPrompt 0x32 TradeRequestResult
//   0x33 TradeActionResult 0x6c WarehouseMoveResult 0x6d VendorInventoryLoad
//   0x6e VendorClose     0x87 PlayerShopOpen   0x88 PlayerShopBuyResult
//   0x89 PlayerShopGoldResult
#include "Net/GameHandlers.h"
#include "Net/ClientState.h"   // ts2::net::g_GmCmdCooldownLatch
#include "Net/SendPackets.h"
#include "Game/ClientRuntime.h"
#include "Game/WarehouseSystem.h" // game::g_Warehouse (byte-exact grid dword_18229CC, 1232 bytes)
#include "Game/GameDatabase.h"    // game::GetItemInfo / ItemInfo::field212 (== MobDb_GetEntry 0x4C3C00)
#include "Game/GameState.h"       // game::g_World.self.elementSecondary (g_LocalElementSecondary 0x1673198)
#include <cstdint>
#include <cstring>
#include <string>

namespace ts2::net {

// Original addresses (globals not modeled as a proper field, via g_Client.Var).
namespace {
constexpr uint32_t kTradePartnerIdLo = 0x168741C; // g_TradePartnerIdLo (partner[0])
constexpr uint32_t kTradePartnerVal1 = 0x1687420; // dword_1687420      (partner[1])
// dword_1687424 = 3rd identity word of the partner, SENT BY THE SERVER (Pkt_TradeRequestPrompt
// 0x48fd20: `dword_1687424[0] = v7[2]` EA 0x48fda0). This is NOT a "local agreement": it's the
// value that Pkt_TradeActionResult 0x48FEA0 compares against the received action code.
constexpr uint32_t kTradePartnerVal2 = 0x1687424;
constexpr uint32_t kTradeState       = 0x1675B24; // g_PendingOrderKind (trade state)
constexpr uint32_t kTradeExtra       = 0x1675D84; // dword_1675D84
constexpr uint32_t kOpenServiceWindow= 0x1823198; // g_OpenServiceWindow
constexpr uint32_t kWarehouseOpen    = 0x1822ED4; // g_WarehouseWindowOpen

// Throwing-weapon rack: TWO PARALLEL arrays of 10 dwords, not one flat array
// of 20. Proven by the address arithmetic of Net_OnWarehouseMoveResult 0x4A61F0 case 5:
//   g_ThrowWeaponRack = 0x16749FC; dword_1674A00 = base+4   -> rack[i+1];
//   dword_1674A20     = base+0x24 -> rack[9] (last)          ; the 2nd array starts at
//   dword_1674A24     = base+0x28 = index 10                 ; dword_1674A28 = aux[i+1];
//   dword_1674A48     = 0x1674A24+0x24 -> aux[9]             ; the cursor dword_1674A4C immediately
// follows the TWO arrays (so it is NOT a rack bound).
constexpr uint32_t kThrowWeaponRack       = 0x16749FC; // g_ThrowWeaponRack[10] (cf. Net/ItemActionDispatch.cpp)
constexpr uint32_t kThrowWeaponRackAux    = 0x1674A24; // dword_1674A24[10] — 2nd parallel array
constexpr uint32_t kThrowWeaponRackCursor = 0x1674A4C; // dword_1674A4C — selection cursor
constexpr int      kThrowWeaponRackCapacity = 10;

constexpr uint32_t kShopGridItemBase = 0x18229EC; // dword_18229EC.. — 5x5 item grid (9 dword/cell)
constexpr uint32_t kShopGridPriceBase= 0x1822D70; // dword_1822D70.. — 5x5 price/qty grid (3 dword/cell)
constexpr uint32_t kInvAuxBase       = 0x1674AB8; // g_InvAux[] — 3 dwords PER inventory cell
constexpr uint32_t kVendorGrid       = 0x1837E70; // g_VendorGrid — 3 tabs x 400 dwords (0x12C0 bytes)
constexpr uint32_t kVendorTabRows    = 0x1823B50; // dword_1823B50[4] — active row count per tab
constexpr uint32_t kShopItemTable    = 0x1823B60; // g_ShopItemTable (8960 bytes)

// g_InvAux 0x1674AB8: PARALLEL array indexed PER CELL — 3 contiguous dwords
// (0x1674AB8 / 0x1674ABC / 0x1674AC0) at dword index `192*row + 3*col`, i.e. byte
// offset `4*(192*row + 3*col)`. Same stride already documented in
// Game/WarehouseSystem.h:78-80 (base 0x1674AB8, stride 192*page+3*slot, EA 0x5d32a5).
// Anchors: Pkt_PlayerShopBuyResult 0x48e30f / 0x48e328 / 0x48e341; Pkt_TradeResult
// 0x48d2fa / 0x48d313 / 0x48d32c (case 0) and 0x48d775 / 0x48d78f / 0x48d7a9 (case 6).
uint32_t InvAuxAddr(uint32_t row, uint32_t col, uint32_t which) {
    return kInvAuxBase + 4u * (192u * row + 3u * col) + 4u * which;
}

// Pkt_PlayerShopOpen (0x87) / Pkt_PlayerShopBuyResult (0x88) share the SAME 824-byte
// blob layout: [0..16) vendor name, [16..516) 5x5 item grid (20 bytes/cell in SOURCE),
// [516..816) 5x5 price/qty grid (12 bytes/cell), then 2 dwords at +816/+820.
//
// WARNING — the destination item grid is SPARSE, not a flat copy:
//   dword_18229EC[45*i + 9*j + d] = blob[100*i + 16 + 20*j + 4*d]   (d = 0..4)
//   dword_1822A00[45*i + 9*j + e] = -1                              (e = 0..3, padding)
// i.e. 9 dwords (36 bytes) per destination cell for 5 dwords (20 bytes) in the source, and
// 180 bytes per destination row for 100 bytes in the source. Span = [0x18229EC, 0x1822D70) =
// 900 bytes, adjoining the 0x1822D70 price base — which locks the layout in.
// Anchors: Pkt_PlayerShopOpen 0x48dabc-0x48dc1c; Pkt_PlayerShopBuyResult 0x48e051-0x48e1b1
// (`imul 0B4h` = 180 bytes/dst row, `imul 24h` = 36 bytes/dst cell, `imul 64h`/`imul 14h` =
// 100/20 bytes src) and 0x48e151-0x48e1b1 (the 4 padding dwords set to -1).
//
// The price grid, however, has IDENTICAL source and destination strides (60 bytes/row,
// 12 bytes/cell: `dword_1822D70[15*i + 3*j] = blob[60*i + 516 + 12*j]` @0x48e1ea): the
// range is therefore contiguous and the flat 75-dword loop is faithful to it — don't
// "fix" it.
void DecodePlayerShopGrid(const uint8_t* blob824) {
    for (int i = 0; i < 5; ++i) {              // row (0x48dfd0 / 0x48da3b)
        for (int j = 0; j < 5; ++j) {          // column (0x48dff8 / 0x48da63)
            const uint32_t dst = kShopGridItemBase + 4u * (45u * i + 9u * j);
            const int      src = 100 * i + 16 + 20 * j;
            for (int d = 0; d < 5; ++d) {      // 5 dwords copied (0x48e051..0x48e135)
                uint32_t v; std::memcpy(&v, blob824 + src + 4 * d, 4);
                game::g_Client.Var(dst + 4u * d) = static_cast<int32_t>(v);
            }
            for (int e = 5; e < 9; ++e)        // 4 padding dwords set to -1 (0x48e151..0x48e1b1)
                game::g_Client.Var(dst + 4u * e) = -1;
        }
    }
    // Price/qty grid: contiguous range of 75 dwords (0x48e1ea / 0x48e220 / 0x48e256).
    for (int i = 0; i < 75; ++i) {
        uint32_t v; std::memcpy(&v, blob824 + 516 + 4 * i, 4);
        game::g_Client.Var(kShopGridPriceBase + 4u * i) = static_cast<int32_t>(v);
    }
}

// Vendor name: `Crt_Vsnprintf(byte_18229D0, "%s%s", blob[0..16), Str(2053))`.
// Anchors: Pkt_PlayerShopOpen 0x48d9fb-0x48da12; Pkt_PlayerShopBuyResult 0x48dfa7-0x48dfc8.
// No dedicated CString field in this scalar module -> stored as-is via g_Client.Blob.
void StorePlayerShopVendorName(const uint8_t* blob) {
    std::string vendorName(reinterpret_cast<const char*>(blob),
                           strnlen(reinterpret_cast<const char*>(blob), 16));
    std::string composed = vendorName + game::Str(2053);
    auto& b = game::g_Client.Blob(0x18229D0, composed.size() + 1);
    if (b.size() != composed.size() + 1) b.assign(composed.size() + 1, 0);
    std::memcpy(b.data(), composed.c_str(), composed.size() + 1);
}

// "warehouse open on service 33" guard (Pkt_TradeResult 0x48d35b/0x48d364;
// Pkt_PlayerShopBuyResult 0x48e378). Its exact complement `!open || service != 33`
// drives the drag-and-drop reset (Item_DragState_CancelIfActive 0x5B15F0).
bool VendorResyncGate() {
    return game::g_Client.Var(kWarehouseOpen) != 0 && game::g_Client.Var(kOpenServiceWindow) == 33;
}

// Vendor list pagination reset — IDENTICAL block in Pkt_TradeResult cases
// 0..5,7,8 (0x48d366 / 0x48d403 / 0x48d4a0 / 0x48d53d / 0x48d5da / 0x48d677 / 0x48d832 /
// 0x48d8cf) and in Pkt_PlayerShopBuyResult (0x48e37a).
void VendorListResetPagination() {
    game::g_Client.Var(0x1826128) = 1;    // dword_1826128 (page count)         0x48d366
    game::g_Client.Var(0x182612C) = 0;    // dword_182612C (page offset)        0x48d370
    game::g_Client.Var(0x1826130) = -1;   // dword_1826130 (selection)          0x48d37a
    game::g_Client.Var(0x1826134) = 0;    // dword_1826134 (entry count)        0x48d384
}

// "vendor resync" block shared by cases 0..5,7,8 of Pkt_TradeResult (anchors above).
void TradeResultVendorResync() {
    if (!VendorResyncGate()) return;
    VendorListResetPagination();
    // TODO(send) [anchor 0x48d3a0]: Net_SendPacket_Op34(&g_AutoPlayMgr, dword_1826120,
    //   dword_1826124). DO NOT FORCE: dword_1826120/dword_1826124 (the 2 EXACT builder
    //   arguments) aren't modeled anywhere in this module — probably a vendor id/session
    //   set by an earlier, not yet reversed packet. The builder
    //   Net_SendPacket_Op34(NetClient&, int8_t, int8_t) already exists (Net/SendPackets.h) and
    //   is ready to be called as soon as these two globals are modeled.
}
} // namespace

void RegisterVendorTradeHandlers(NetSystem& sys) {
    using namespace game;   // g_Client, Str()

    // 0x22 WarehouseOpen — warehouse open/result (1232-byte blob).
    OnPacket<WarehouseOpen>(sys, 0x22, [](const WarehouseOpen& p) {
        g_GmCmdCooldownLatch = 0;
        switch (p.status) {
        case 0:   // open: arms the warehouse window state + focus.
            g_Client.Var(0x1687428) = 1;   // dword_1687428
            g_Client.Var(0x18229B8) = 2;   // dword_18229B8 (edit-box focus)
            g_Warehouse.DecodeBlob(p.blob, sizeof p.blob); // -> dword_18229CC (byte-exact grid)
            // TODO(state): init unk_168742C string (name, outside the WarehouseGrid model — cf.
            //   "reserved header" in Game/WarehouseSystem.h, not attributed with certainty).
            // TODO(ui): cGameHud_Hide; if g_UIEditBoxMgr==14 UI_FocusEditBox(0).
            break;
        case 100: // grid committed.
            g_Warehouse.DecodeBlob(p.blob, sizeof p.blob); // -> dword_18229CC
            g_Warehouse.CommitAllToInventory();            // UI_StorageWin_CommitGrid (EA 0x48cc04)
            g_Client.msg.System(Str(2031));
            break;
        case 101: g_Client.msg.System(Str(2035)); break;
        case 102: g_Client.msg.System(Str(2034)); break;
        case 103: g_Client.msg.System(Str(2032)); break;
        case 104: g_Client.msg.System(Str(2015)); break;
        case 105: g_Client.msg.System(Str(2033)); break;
        default: break;
        }
    });

    // 0x23 WarehouseClose — closes warehouse/storage UI (mode 1/2).
    OnPacket<WarehouseClose>(sys, 0x23, [](const WarehouseClose& p) {
        if (p.mode == 1) {                 // plain close + refocus.
            g_Client.Var(0x1687428) = 0;   // dword_1687428
            if (g_Client.Var(0x1822998))   // if the grid was active
                g_Client.Var(0x18229B8) = 1;
            // TODO(ui): UI_FocusEditBox(14); cGameHud_ResetUiState.
        } else if (p.mode == 2) {          // commit grid + reopen.
            g_Warehouse.CommitAllToInventory(); // UI_StorageWin_CommitGrid (EA 0x48cdfe)
            g_Client.msg.System(Str(2110));
            // TODO(ui): UI_StorageWin_Open(3, ...) — reopens the warehouse in deposit
            //   mode; pure presentation (no extra state to model here).
        }
    });

    // 0x24 WarehouseUpdate — chest update (data or message).
    OnPacket<WarehouseUpdate>(sys, 0x24, [](const WarehouseUpdate& p) {
        switch (p.mode) {
        case 0:
        case 3:
            g_Warehouse.DecodeBlob(p.data, sizeof p.data); // -> dword_18229CC (full warehouse state)
            break;
        case 1: g_Client.msg.System(Str(583)); break;
        case 2: g_Client.msg.System(Str(584)); break;
        default: break;
        }
    });

    // 0x25 VendorItemEntry — appends a row to the current vendor list/search.
    OnPacket<VendorItemEntry>(sys, 0x25, [](const VendorItemEntry& p) {
        // Resets the list when the session id (dword_1826138) changes.
        // Pkt_VendorItemEntry 0x48cf40, block 0x48d01a-0x48d045: FIVE writes — the
        // dword_182612C (page/scroll offset) was missing (EA 0x48d024).
        if (g_Client.Var(0x1826138) != static_cast<int32_t>(p.listId)) {
            g_Client.Var(0x1826128) = 1;    // pagination      0x48d01a
            g_Client.Var(0x182612C) = 0;    // page offset      0x48d024
            g_Client.Var(0x1826130) = -1;   // selection        0x48d02e
            g_Client.Var(0x1826134) = 0;    // count            0x48d038
            g_Client.Var(0x1826138) = static_cast<int32_t>(p.listId); // 0x48d045
        }
        const int32_t idx = g_Client.Var(0x1826134);
        if (idx < 1000) {
            // Faithful indexed write (original base + stride*idx addressing).
            g_Client.Var(0x182613C + 4u * idx)  = static_cast<int32_t>(p.itemId);  // dword_182613C[idx]
            g_Client.Var(0x182A3A4 + 4u * idx)  = static_cast<int32_t>(p.field17); // dword_182A3A4[idx]
            g_Client.Var(0x182B344 + 4u * idx)  = static_cast<int32_t>(p.field21); // dword_182B344[idx]
            g_Client.Var(0x1834F84 + 12u * idx) = static_cast<int32_t>(p.price0);  // dword_1834F84[3*idx]
            g_Client.Var(0x1834F88 + 12u * idx) = static_cast<int32_t>(p.price1);  // dword_1834F88[3*idx]
            g_Client.Var(0x1834F8C + 12u * idx) = static_cast<int32_t>(p.price2);  // dword_1834F8C[3*idx]
            // p.name -> unk_18270DC + 13*idx; p.blob[36] -> unk_182C2E4 + 36*idx: deliberately NOT
            //   stored here (no non-scalar array field in this module). UI/VendorShopWindow.h
            //   resolves name + icon from the ONLY reliable field above (itemId), via ITEM_INFO —
            //   see that header for the full justification (documented workaround, not an oversight).
            g_Client.Var(0x1826134) = idx + 1;                 // dword_1826134 (entry count)
            // Page count = OLD index / 10 + 1. The binary increments dword_1826134 THEN
            // re-subtracts 1 before the division: `add eax,1 ; mov [dword_1826134],eax ;
            // mov eax,[dword_1826134] ; sub eax,1 ; cdq ; idiv ecx(=10) ; add eax,1`
            // (0x48d11c-0x48d13c, decisive instruction `sub eax,1` @0x48d12e — Hex-Rays renders
            // it as `dword_1826134++ / 10 + 1`). Dividing the NEW index would show one
            // extra empty page at every exact multiple of 10.
            g_Client.Var(0x1826128) = idx / 10 + 1;            // dword_1826128 (page count) 0x48d13c
        }
    });

    // 0x26 TradeResult — transaction result (sale/warehouse).
    // Pkt_TradeResult 0x48d150. WARNING: `g_GmCmdCooldownLatch = 0` and the drag-and-drop
    // reset are posed WITHIN each case EXCEPT case 6 (refund), which attacks directly
    // `g_InvWeight += v20` @0x48d6c4. Hoisting them above the switch would disarm the
    // latch where the original leaves it armed.
    // Latch: 0x48d21d (0), 0x48d3aa (1), 0x48d447 (2), 0x48d4e4 (3), 0x48d581 (4),
    //         0x48d61e (5), 0x48d7d9 (7), 0x48d876 (8) — no write in case 6.
    OnPacket<TradeResult>(sys, 0x26, [](const TradeResult& p) {
        switch (p.resultCode) {
        case 0: // sold: credits the cell + aux, deducts weight.
            g_GmCmdCooldownLatch = 0;                       // 0x48d21d
            // TODO(ui) [anchor 0x48d23e]: `if (!VendorResyncGate()) Item_DragState_CancelIfActive(g_DragCtx)`
            //   (0x5B15F0). UI/InventoryWindow::CancelDrag() exists but is out of scope for this module.
            g_Client.inv.weight -= p.weightDelta;           // 0x48d24c
            g_Client.inv.Set(p.invRow, p.invCol, p.item[0], p.item[1], p.item[2],
                             p.item[3], p.item[4], p.item[5]);
            // aux: PARALLEL array indexed by cell, not 3 global scalars.
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 0)) = static_cast<int32_t>(p.aux0); // 0x48d2fa
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 1)) = static_cast<int32_t>(p.aux1); // 0x48d313
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 2)) = static_cast<int32_t>(p.aux2); // 0x48d32c
            g_Client.msg.System(Str(594));                  // 0x48d344
            TradeResultVendorResync();                      // 0x48d35b-0x48d3a0
            break;
        case 6: // refund: returns the weight and clears the cell (NEITHER latch NOR drag reset).
            g_Client.inv.weight += p.weightDelta;           // 0x48d6c4
            g_Client.inv.ClearCell(p.invRow, p.invCol);     // 0x48d6d9-0x48d75b
            // inv.ClearCell doesn't touch aux: the binary zeroes it explicitly.
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 0)) = 0; // 0x48d775
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 1)) = 0; // 0x48d78f
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 2)) = 0; // 0x48d7a9
            g_Client.msg.System(Str(593));                  // 0x48d7c4
            break;
        // Error cases: latch + drag reset (same guard) + message + vendor resync.
        case 1: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(583));  TradeResultVendorResync(); break;
        case 2: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(584));  TradeResultVendorResync(); break;
        case 3: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(595));  TradeResultVendorResync(); break;
        case 4: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(596));  TradeResultVendorResync(); break;
        case 5: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(597));  TradeResultVendorResync(); break;
        case 7: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(732));  TradeResultVendorResync(); break;
        case 8: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(2237)); TradeResultVendorResync();
                // TODO(ui) [anchor 0x48d913]: UI_StorageWin_CommitGrid(dword_1822990).
                break;
        default: break;
        }
        // TODO(ui) [anchors 0x48d23e/0x48d3cb/0x48d468/0x48d505/0x48d5a2/0x48d63f/0x48d7fa/0x48d897]:
        //   Item_DragState_CancelIfActive(g_DragCtx) in cases 0..5,7,8 under the guard
        //   `if (!g_WarehouseWindowOpen || g_OpenServiceWindow != 33)` (= !VendorResyncGate()) —
        //   and NEVER in case 6.
    });

    // 0x2c DuelResult — despite the name, opens the warehouse if flag==1:
    //   `if (v1 == 1) UI_Warehouse_Open(&dword_1822EC8, 2, v2)` (Pkt_DuelResult 0x48f760,
    //   call @0x48f7a5). This is NOT simple presentation: the callee emits an OUTGOING
    //   packet (Net_SendOp91) and mutates about a dozen globals.
    //
    // Decoding the callee UI_Warehouse_Open 0x5f3db0: `mov ecx, offset dword_1822EC8`
    // @0x48f7a0 -> `this` is the ADDRESS 0x1822EC8, so every `*(this + N)` in the decompiled
    // code is the absolute global `0x1822EC8 + 4*N`. Verified by 4 cross-checks against
    // known symbols:
    //   *(this+3)     = 0x1822ED4 = g_WarehouseWindowOpen
    //   *(this+180)   = 0x1823198 = g_OpenServiceWindow
    //   *(this+1926)  = 0x1824CE0 = g_VendorRawFeed
    //   *(this+21482) = 0x1837E70 = g_VendorGrid
    // and by *(this+21479)/*(this+21480) = 0x1837E64/0x1837E68, the pair that handler
    // 0x6e below already manipulates with the same Net_SendOp91.
    OnPacket<DuelResult>(sys, 0x2c, [&sys](const DuelResult& p) {
        if (p.flag != 1) return;                                   // 0x48f792

        // (a) pending vendor resync -> emits the outgoing opcode. 0x5f3dc4-0x5f3de4.
        if (g_Client.Var(0x1837E64)) {
            Net_SendOp91(sys.Client());       // 0x5f3dd2 (Net_SendOp91 0x4bc4a0)
            g_Client.Var(0x1837E64) = 0;      // 0x5f3dd7
            g_Client.Var(0x1837E68) = 0;      // 0x5f3de4 (*(this+21480))
        }

        // (b) TODO [anchor 0x5f3df3]: `if (Map_IsArenaZone()) { msg.System(Str(1352)); return; }`
        //   — Map_IsArenaZone 0x54B690 isn't modeled ANYWHERE in ClientSource (same
        //   gap as Scene/SceneManager.cpp:934, which hardcodes it to `false`). We follow
        //   the same precedent here (non-arena zone): the guard is NOT emulated. Wire it in
        //   as soon as the arena-zone concept exists, OTHERWISE the warehouse opens in arenas
        //   where the original refuses.

        // (c) a2 == 2 -> the `if (a2 == 1)` branch (UI_CloseAllDialogs + Net_SendOp41) is
        //   dead for this path; start directly from the mode. 0x5f3e26.
        g_Client.Var(kOpenServiceWindow) = 21;                     // *(this+180)  0x5f3e88
        for (int i = 0; i < 100; ++i)                              // *(this+i+70) 0x5f3e92-0x5f3eb0
            g_Client.Var(0x1822FE0 + 4u * i) = 0;
        g_Client.Var(0x1823B4C) = static_cast<int32_t>(p.param);   // *(this+801)  0x5f3ec3
        // g_LocalElementSecondary 0x1673198 @0x5f3ecf. Source of truth = the modeled field
        // game::g_World.self.elementSecondary (Game/GameState.h:329) — and NOT g_Client.Var(0x1673198),
        // which is a second modeling of the same global, with no writer (see report).
        const int32_t page = game::g_World.self.elementSecondary;
        g_Client.Var(0x1823B58) = 1;                               // *(this+804)  0x5f3ed5

        // (d) copy of the 10x10 page of the current tab: g_VendorGrid[400*page + …]
        //   -> g_VendorRawFeed[…]. The 4 dwords are first set to -1 then overwritten
        //   (0x5f3f30-0x5f40a8), which we reproduce with just the final write.
        for (int j = 0; j < 10; ++j) {
            for (int k = 0; k < 10; ++k) {
                for (int d = 0; d < 4; ++d) {
                    const uint32_t src = kVendorGrid + 4u * (400u * page + 40u * j + 4u * k + d);
                    const uint32_t dst = 0x1824CE0   + 4u * (40u * j + 4u * k + d);
                    g_Client.Var(dst) = g_Client.Var(src);         // 0x5f3fda-0x5f40a8
                }
            }
            // active row count: reads the itemId field (dword +1) of cell k=0.
            if (g_Client.Var(kVendorGrid + 4u * (400u * page + 40u * j + 1u)) > 0)  // 0x5f40d2 (signed)
                g_Client.Var(0x1823B58) = j + 1;                   // 0x5f40dd
        }
        g_Client.Var(0x1825E60) = 0;    // *(this+3046) 0x5f40eb
        g_Client.Var(0x1825E64) = 0;    // *(this+3047) 0x5f40f8
        g_Client.Var(0x1825E68) = -1;   // *(this+3048) 0x5f4105
        // TODO(ui) [anchor 0x5f4114]: cGameHud_ResetUiState(dword_1839568).
    });

    // 0x2d RepairResult — repair result / gold change.
    OnPacket<RepairResult>(sys, 0x2d, [](const RepairResult& p) {
        g_GmCmdCooldownLatch = 0;
        switch (p.status) {
        case 0: { // success: credits artisan XP, rewrites the cell.
            // Pkt_RepairResult 0x48f7b0 case 0:
            //   `dword_16756F8 += (dword_1823B4C - v18) / 10; dword_1823B4C = v18;`
            // The delta base is the OLD value of the PRIVATE LATCH dword_1823B4C (0x1823B4C),
            // read BEFORE being overwritten — and NOT g_Currency 0x1673180 (= g_Client.inv.currency,
            // cf. Game/ClientRuntime.h:82): two distinct globals, which the binary never
            // confuses (this handler neither reads nor writes g_Currency).
            const int32_t oldGold = g_Client.Var(0x1823B4C);                         // 0x48f842
            // MANDATORY cast: p.goldRemaining is uint32_t (RecvPackets.h:231); without it
            // the usual conversions would make the expression UNSIGNED and the division
            // unsigned, whereas the binary does `cdq ; idiv` (SIGNED) @0x48f85c. The
            // negative delta is guaranteed here (BSS latch=0 on the 1st repair -> 0 - gold < 0).
            const int32_t delta = oldGold - static_cast<int32_t>(p.goldRemaining);    // 0x48f83f
            g_Client.Var(0x16756F8) += delta / 10;                                    // artisan XP 0x48f85c-0x48f864
            g_Client.Var(0x1823B4C) = static_cast<int32_t>(p.goldRemaining);          // remaining gold 0x48f86c
            // NB: `v15[1] = delta / 500` (@0x48f850) is a dead store within the local
            //   stack frame — not observable, deliberately not ported.
            g_Client.inv.Set(p.invRow, p.invCol, p.itemCell[0], p.itemCell[1], p.itemCell[2],
                             p.itemCell[3], p.itemCell[4], p.itemCell[5]);
            g_Client.msg.System(Str(681));
            break;
        }
        case 1: g_Client.msg.System(Str(682)); break;
        case 2: g_Client.msg.System(Str(683)); break;
        case 3: // closes the NPC window + rearms vendor state.
            g_Client.Var(0x1837E64) = 1;
            g_Client.Var(0x1837E68) = 0;
            g_Client.msg.System(Str(683));
            // TODO(ui): UI_NpcWin_CloseRestore.
            break;
        case 4: g_Client.msg.System(Str(683)); break;
        case 5: g_Client.msg.System(Str(2241)); break;
        case 6: g_Client.msg.System(Str(2242)); break;
        case 7: g_Client.msg.System(Str(2267)); break;
        default: break;
        }
    });

    // 0x31 TradeRequestPrompt — incoming trade invite: remembers the partner.
    OnPacket<TradeRequestPrompt>(sys, 0x31, [](const TradeRequestPrompt& p) {
        // TODO(audio) [anchor 0x48fd7d]: Snd3D_PlayScaledVolume(flt_148B7FC, 0, 100, 1),
        //   played BEFORE any state mutation (g_PendingOrderKind = 0 @0x48fd82). Not wired:
        //   the C++ API is an instance method (audio::Emitter::PlayScaledVolume,
        //   Audio/Sound3D.h:91) and no registry links the emitter flt_148B7FC to a
        //   reachable Emitter from this module (same out-of-scope precedent as
        //   Game/ComboPickupTick.h:266 and Game/QuestSystem.cpp:424).
        g_Client.Var(kTradeState)        = 0;                                 // 0x48fd82
        g_Client.Var(kTradePartnerIdLo)  = static_cast<int32_t>(p.partner[0]); // 0x48fd8f
        g_Client.Var(kTradePartnerVal1)  = static_cast<int32_t>(p.partner[1]); // 0x48fd97
        g_Client.Var(kTradePartnerVal2)  = static_cast<int32_t>(p.partner[2]); // 0x48fda0
        g_Client.Var(kTradeExtra)        = static_cast<int32_t>(p.extra);      // 0x48fdac
        // `Crt_Vsnprintf(v6, "%s [%d]%s", Str(314), promptId, Str(315))` @0x48fde4:
        //   v3 = Str(315) @0x48fdc0 (SUFFIX), v0 = Str(314) @0x48fdd2 (PREFIX).
        // Same format as sibling handler 0x32 below (Str(314) + " [%d]" + Str(316)).
        g_Client.msg.System(Str(314) + " [" + std::to_string(p.promptId) + "]" + Str(315));
    });

    // 0x32 TradeRequestResult — trade-request result line.
    OnPacket<TradeRequestResult>(sys, 0x32, [](const TradeRequestResult& p) {
        g_Client.msg.System(Str(314) + " [" + std::to_string(p.code) + "]" + Str(316));
    });

    // 0x33 TradeActionResult — trade accept/cancel, resets state to zero.
    OnPacket<TradeActionResult>(sys, 0x33, [](const TradeActionResult& p) {
        switch (p.code) {
        case 0:
            g_Client.msg.System(Str(317));
            break;
        case 1:
        case 2: // compares the 3rd partner identity word (set by 0x31 from v7[2], on the
                // SERVER side — cf. kTradePartnerVal2 above) against the received action code.
                // Anchor: Pkt_TradeActionResult 0x48FEA0, `cmp dword_1687424, v7` @0x48fefe.
            g_Client.msg.System(g_Client.Var(kTradePartnerVal2) == static_cast<int32_t>(p.code)
                                    ? Str(318) : Str(319));
            break;
        case 3:
            g_Client.msg.System(Str(320));
            break;
        default: break;
        }
        // Systematic reset of trade state.
        g_Client.Var(kTradeState)       = 0;
        g_Client.Var(kTradePartnerIdLo) = 0;
        g_Client.Var(kTradePartnerVal1) = 0;
        g_Client.Var(kTradePartnerVal2) = 0;
        g_Client.Var(kTradeExtra)       = 1;
    });

    // 0x6c WarehouseMoveResult — warehouse move / throwing-weapon rack.
    OnPacket<WarehouseMoveResult>(sys, 0x6c, [](const WarehouseMoveResult& p) {
        switch (p.action) {
        case 1: if (p.status == 0) g_Client.Var(kThrowWeaponRackCursor) = static_cast<int32_t>(p.index); break; // 0x4a62ac
        case 2: if (p.status == 0) g_Client.Var(kThrowWeaponRackCursor) = -1;  break; // 0x4a62c2
        case 3: if (p.status == 0) g_Client.Var(kThrowWeaponRackCursor) += 10; break; // 0x4a62e5
        case 4: if (p.status == 0) g_Client.Var(kThrowWeaponRackCursor) -= 10; break; // 0x4a6303
        case 5:
            if (p.status == 0) {
                // Removes entry p.index and compacts the following ones by one slot, on the
                // TWO parallel arrays of 10 (cf. kThrowWeaponRack above). The C++ used to
                // treat the rack as a flat array of 20: at i==9 it executed
                // rack[9] = aux[0], injecting the neighboring array's data where the binary
                // writes 0, then shifted aux[0..9] from index 0 instead of compacting
                // from idx -> misalignment of the two arrays.
                // Anchors: Net_OnWarehouseMoveResult 0x4A61F0 case 5, 0x4a631a-0x4a6388.
                const int idx = static_cast<int>(p.index);
                if (idx >= 0 && idx < kThrowWeaponRackCapacity) {   // safety guard (the binary has none)
                    g_Client.Var(kThrowWeaponRack    + 4u * idx) = 0;  // 0x4a631a
                    g_Client.Var(kThrowWeaponRackAux + 4u * idx) = 0;  // 0x4a6328
                }
                g_Client.Var(kThrowWeaponRackCursor) = -1;             // 0x4a6333 — UNCONDITIONAL
                for (int i = (idx > 0 ? idx : 0); i < 9; ++i) {        // 0x4a6340-0x4a6375, bound 9
                    g_Client.Var(kThrowWeaponRack    + 4u * i) = g_Client.Var(kThrowWeaponRack    + 4u * (i + 1)); // 0x4a6361
                    g_Client.Var(kThrowWeaponRackAux + 4u * i) = g_Client.Var(kThrowWeaponRackAux + 4u * (i + 1)); // 0x4a6375
                }
                g_Client.Var(kThrowWeaponRack    + 4u * 9) = 0;        // 0x4a637e (= 0x1674A20) — UNCONDITIONAL
                g_Client.Var(kThrowWeaponRackAux + 4u * 9) = 0;        // 0x4a6388 (= 0x1674A48) — UNCONDITIONAL
                // The loop is self-bounded for idx>=9; the clamp to 0 only guards against a
                // negative index coming from the network. These two deviations (guard + clamp)
                // are assumed safety deviations, everything else is bit-faithful.
                g_Client.inv.Set(p.invRow, p.invCol, p.itemId, p.gridPos % 8, p.gridPos / 8, 0, 0, 0); // 0x4a63a4-0x4a6436
                g_Client.msg.System(Str(1519));                        // 0x4a6462
            } else if (p.status == 1) {
                g_Client.msg.System(Str(1518));                        // 0x4a649a
            } else if (p.status == 2) {
                g_Client.msg.System(Str(117));                         // 0x4a64d0
            }
            // TODO(audio) [anchors 0x4a644c (status 0) / 0x4a6485 (1) / 0x4a64bd (2)]:
            //   Snd3D_PlayScaledVolume(flt_1495ABC, 0, 100, 1) in all THREE branches,
            //   before the message. Not wired (same reason as at 0x31 above).
            break;
        default: break;
        }
    });

    // 0x6d VendorInventoryLoad — loads the vendor item table (8960 bytes) + grid.
    //
    // g_VendorRawFeed is NOT a separate staging buffer: it's an INTERNAL ALIAS of the
    // table copied by this packet. Arithmetic proof (independent of xrefs):
    //   memcpy(g_ShopItemTable=0x1823B60, payload+8, 0x2300=8960) @0x4a6558, and
    //   g_VendorRawFeed = 0x1824CE0 = 0x1823B60 + 0x1180 -> offset 4480 bytes < 8960.
    // So `g_VendorRawFeed[n]` == `p.shopItemTable[1120 + n]`, and reading the feed directly
    // from the packet is faithful (the binary reads it right after its own memcpy).
    // NB: the claim "no other writer in the image" that was circulating is FALSE —
    //   UI_Warehouse_Open 0x5f3db0 writes this same buffer this-relative (*(this+1926),
    //   @0x5f3fda), which produces no data xref. This doesn't change the conclusion.
    OnPacket<VendorInventoryLoad>(sys, 0x6d, [](const VendorInventoryLoad& p) {
        g_Client.Var(0x1837E6C) = static_cast<int32_t>(p.param); // dword_1837E6C 0x4a653f
        // Raw copy (faithful memcpy @0x4a6558) — internal structure of g_ShopItemTable
        // (dword_1823B60) not decoded further here.
        std::memcpy(g_Client.Blob(kShopItemTable, sizeof p.shopItemTable).data(),
                    p.shopItemTable, sizeof p.shopItemTable);
        if (p.status != 0) {
            g_Client.Var(0x1837E64) = 1;                         // 0x4a6566
            return;
        }
        // (1) Crt_Memset(g_VendorGrid, -1, 0x12C0) @0x4a6581 — 4800 bytes = 1200 dwords.
        for (int i = 0; i < 1200; ++i)
            g_Client.Var(kVendorGrid + 4u * i) = -1;

        // (2) per-tab cursors: v9 = page, v6 = slot (0x4a6589-0x4a65a7).
        int page[3] = {0, 0, 0};
        int slot[3] = {0, 0, 0};

        // (3) triple loop i<3 / j<14 / k<10 (0x4a65ae-0x4a6790).
        const uint32_t* feed = &p.shopItemTable[1120];            // == g_VendorRawFeed 0x1824CE0
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 14; ++j) {
                for (int k = 0; k < 10; ++k) {
                    // MobDb_GetEntry(mITEM, feed[40*j + 1 + 4*k]) @0x4a6625 == GetItemInfo:
                    //   same 1-based semantics, same id!=0 guard (Game/GameDatabase.cpp:131).
                    const game::ItemInfo* e = game::GetItemInfo(feed[40 * j + 1 + 4 * k]);
                    if (!e) continue;                             // 0x4a662c
                    const uint32_t cat = e->field212;             // *(Entry+212) 0x4a6639
                    if (cat != 1 && cat != static_cast<uint32_t>(i + 2)) continue; // 0x4a664b
                    // No bounds guard: it would be UNFAITHFUL and pointless. The binary
                    //   can overflow g_VendorGrid if more than 100 items pass a tab's
                    //   filter (page reaches 14 -> index 1360 > 1199) and then overwrites
                    //   its neighbors; here storage is an address->int32 table (Var),
                    //   so an out-of-range index simply creates an inert entry — no
                    //   memory overflow possible. Clamping, on the other hand, would change
                    //   the page/slot cursors relative to the binary.
                    const uint32_t dst = kVendorGrid + 4u * (400u * i + 40u * page[i] + 4u * slot[i]);
                    for (int d = 0; d < 4; ++d)                   // 0x4a668f-0x4a6759
                        g_Client.Var(dst + 4u * d) = static_cast<int32_t>(feed[40 * j + d + 4 * k]);
                    if (++slot[i] > 9) { slot[i] = 0; ++page[i]; } // 0x4a6776-0x4a6790
                }
            }
        }
        // (4) active row count per tab: dword_1823B50[m] (0x4a67a3-0x4a680e).
        //     SIGNED `> 0` comparison @0x4a6803; tab m==2 is skipped.
        //     NB: 0x1823B50 = g_ShopItemTable - 0x10, right BEFORE the table.
        for (int m = 0; m < 4; ++m) {
            g_Client.Var(kVendorTabRows + 4u * m) = 1;             // 0x4a67be
            if (m == 2) continue;                                  // 0x4a67cd
            for (int n = 0; n < 10; ++n)
                if (static_cast<int32_t>(p.shopItemTable[560 * m + 40 * n]) > 0) // 0x4a6803
                    g_Client.Var(kVendorTabRows + 4u * m) = n + 1; // 0x4a680e
        }
        g_Client.Var(0x1837E68) = 1;                               // 0x4a6819 — LAST
    });

    // 0x6e VendorClose — closes the vendor window (no payload).
    OnTrigger(sys, 0x6e, [&sys]() {
        g_Client.Var(0x1837E64) = 1;
        g_Client.Var(0x1837E68) = 0;
        if (g_Client.Var(kWarehouseOpen) == 1 && g_Client.Var(kOpenServiceWindow) == 21) {
            Net_SendOp91(sys.Client());  // warehouse resync (opcode 0x5B, "this"=&unk_846C08=NetClient, no payload).
            g_Client.Var(0x1837E64) = 0;
            // TODO(ui): UI_NpcWin_CloseRestore(&dword_1822EC8).
        }
    });

    // 0x87 PlayerShopOpen — opens the personal shop grid (824-byte blob).
    // Pkt_PlayerShopOpen 0x48d940. The focus sub-switch is reached via the
    // `default:`/LABEL_12 label (@0x48dd9e) AND via a `goto LABEL_12` at the end of case 0
    // (@0x48dcf0): it therefore runs both for resultCode==0 AND for any code outside
    // {0,1,100,101,102,103} — hence its placement outside the `if (resultCode == 0)`.
    OnPacket<PlayerShopOpen>(sys, 0x87, [](const PlayerShopOpen& p) {
        bool runFocus = false;
        switch (p.resultCode) {
        case 0:
            // Vendor name: "%s%s"(blob[0..16), Str(2053)) -> byte_18229D0. 0x48d9fb-0x48da12.
            StorePlayerShopVendorName(p.blob);
            g_Client.Var(0x18229CC) = 0;   // dword_18229CC 0x48da1a
            {
                uint32_t d816 = 0, d820 = 0;
                std::memcpy(&d816, p.blob + 816, 4);
                std::memcpy(&d820, p.blob + 820, 4);
                g_Client.Var(0x1822EB4) = static_cast<int32_t>(d816); // blob[816] 0x48da2a
                g_Client.Var(0x1822EB8) = static_cast<int32_t>(d820); // blob[820] 0x48da36
            }
            DecodePlayerShopGrid(p.blob);  // item grid dword_18229EC.. + price dword_1822D70..
            g_Client.Var(0x1822998) = 1;   // dword_1822998 0x48dcd2
            g_Client.Var(0x1822EBC) = 1;   // dword_1822EBC 0x48dcdc
            // TODO(ui) [anchor 0x48dceb]: cGameHud_ResetUiState(dword_1839568).
            runFocus = true;               // goto LABEL_12 @0x48dcf0
            break;
        case 1:
            // TODO(ui) [anchor 0x48dd13]: UI_MsgBox_Open(dword_1822438, 54, v13, Str(2088)) —
            //   v13 = local 200-byte buffer zeroed @0x48d97d (never populated here).
            //   The string id IS documented and proven: 2088 (@0x48dcff); it's the TARGET
            //   that's missing. Not wired onto g_Client.prompt: PromptState models the
            //   dword_1822440/1822450 register (UI_NoticeDlg/ConfirmPrompt), whereas
            //   UI_MsgBox_Open 0x5C08C0 operates on dword_1822438 — a distinct object,
            //   correspondence NOT proven.
            break;
        case 100:
        case 102:
        case 103: g_Client.msg.System(Str(2032)); break;  // 0x48dd2e
        case 101: g_Client.msg.System(Str(2054)); break;  // 0x48dd54
        default:  runFocus = true; break;                 // LABEL_12 @0x48dd9e
        }
        if (!runFocus) return;
        // Edit-box focus handling (dword_18229B8 = 5/6/7 per focusState 1/2/3).
        switch (p.focusState) {
        case 1:
            // TODO(ui) [anchors 0x48ddbb/0x48ddcc]: UI_FocusEditBox(&g_UIEditBoxMgr, 14) then
            //   SetWindowTextA(dword_1668FF8, &String).
            g_Client.Var(0x18229B8) = 5;   // 0x48ddd2
            g_Client.Var(0x1822EC0) = 2;   // 0x48dddc
            break;
        case 2: g_Client.Var(0x18229B8) = 6; break;  // 0x48dde8
        case 3: g_Client.Var(0x18229B8) = 7; break;  // 0x48ddf4
        default: break;
        }
    });

    // 0x88 PlayerShopBuyResult — purchase result + vendor grid resync.
    OnPacket<PlayerShopBuyResult>(sys, 0x88, [](const PlayerShopBuyResult& p) {
        g_GmCmdCooldownLatch = 0;
        if (p.resultCode == 0 || p.resultCode == 1000) { // success (LABEL_8).
            // Vendor name: "%s%s"(shopBlock[0..16), Str(2053)) -> byte_18229D0. 0x48dfa7-0x48dfc8.
            StorePlayerShopVendorName(p.shopBlock);
            DecodePlayerShopGrid(p.shopBlock); // 5x5 item/price grid (same layout as PlayerShopOpen)
            g_Client.inv.Set(p.dstRow, p.dstCol, p.itemCell[0], p.itemCell[1], p.itemCell[2],
                             p.itemCell[3], p.itemCell[4], p.itemCell[5]);  // 0x48e279-0x48e2f6
            // aux: PARALLEL array indexed by cell (not 3 global scalars shared by
            // every cell, which is what inv.aux0/1/2 used to do — those fields were
            // otherwise write-only: no reader anywhere in src/).
            g_Client.Var(InvAuxAddr(p.dstRow, p.dstCol, 0)) = static_cast<int32_t>(p.itemCell[6]); // 0x48e30f
            g_Client.Var(InvAuxAddr(p.dstRow, p.dstCol, 1)) = static_cast<int32_t>(p.itemCell[7]); // 0x48e328
            g_Client.Var(InvAuxAddr(p.dstRow, p.dstCol, 2)) = static_cast<int32_t>(p.itemCell[8]); // 0x48e341
            g_Client.inv.weight -= p.itemCell[9];                                                  // 0x48e350
            // TODO(ui) [anchor 0x48e35a]: Item_DragState_CancelIfActive(g_DragCtx) — here
            //   UNCONDITIONAL (no guard, unlike Pkt_TradeResult).
            if (VendorResyncGate() && p.resultCode == 1000) {   // 0x48e378
                VendorListResetPagination();                    // 0x48e37a-0x48e398
                // TODO(send) [anchor 0x48e3b5]: Net_SendPacket_Op34(&g_AutoPlayMgr,
                //   dword_1826120, dword_1826124) — blocked, cf. TradeResultVendorResync().
            }
            g_Client.msg.System(p.resultCode == 1000 ? Str(2112) : Str(2113)); // 0x48e3d3 / 0x48e3f6
        } else {
            // Error codes (main switch 0x48df41/0x48df5a/0x48df7f): each branch
            // does UI_StorageWin_CommitGrid + cGameHud_Hide + Item_RestoreDragImmediate
            // (TODO ui) then the system line below.
            // The previous table was shifted: it enumerated the strings in the order
            // (2032,2066,2055,2056,2057,2058,2237) for 1,2,3,4,5,101,default. REAL mapping:
            switch (p.resultCode) {
            case 1:
            case 2:
            case 3:
            case 4:   g_Client.msg.System(Str(2032)); break;  // loc_48E40B, 0x48e43a
            case 5:   g_Client.msg.System(Str(2066)); break;  // loc_48E44F, 0x48e47d
            case 100: g_Client.msg.System(Str(2054)); break;  // 0x48e4c1
            case 101: g_Client.msg.System(Str(2055)); break;  // 'e', 0x48e505
            case 102: g_Client.msg.System(Str(2056)); break;  // 'f', 0x48e548
            case 103: g_Client.msg.System(Str(2057)); break;  // 'g', 0x48e58c
            case 104: g_Client.msg.System(Str(2058)); break;  // 'h', 0x48e5cd
            case 105: g_Client.msg.System(Str(2237)); break;  // 'i', 0x48e60d
            default:  break;  // def_48DF5A (0x48e61d) = return: NO message. Do not
                              // show Str(2237) here — the original stays silent.
            }
        }
    });

    // 0x89 PlayerShopGoldResult — player-shop gold/settlement result.
    OnPacket<PlayerShopGoldResult>(sys, 0x89, [](const PlayerShopGoldResult& p) {
        g_GmCmdCooldownLatch = 0;
        switch (p.status) {
        case 0: // settlement succeeded.
            g_Client.Var(0x1822EB4) = 0;
            g_Client.Var(0x1822EB8) = 0;
            g_Client.inv.weight += p.weightDelta;
            g_Client.Var(0x1675620) += static_cast<int32_t>(p.goldDelta); // dword_1675620 (points)
            g_Client.msg.System(Str(2051));
            break;
        case 1: g_Client.msg.System(Str(101));  break;
        case 2: g_Client.msg.System(Str(2050)); break;
        case 3: g_Client.msg.System(Str(2032)); break;
        case 4: g_Client.msg.System(Str(2052)); break;
        default: break;
        }
    });
}

} // namespace ts2::net

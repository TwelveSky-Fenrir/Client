// Net/GameHandlers_InvCells.cpp — inventory cell results (buy/sell/
// combine/move/discard/craft/gamble/count).
//
// "inv_cells" domain (RE/handler_domains.json). Faithfully translates the
// state-update logic of the original handlers (RE/net_handler_notes.md) to
// the game::g_Client hub (inventory grid, currency/weight, message log,
// pending-item-move state). Anticheat/audio/exact UI rendering out of scope.
//
// Opcodes covered (20):
//   0x1d ItemCombineResult  0x1e ItemSwapResultA  0x1f ItemSwapResultB
//   0x20 ItemDiscardResult  0x21 ItemResultSimple 0x69 ItemCellSet
//   0x6a ItemSellResult     0x6b GambleResult     0x70 ItemCombineResult2
//   0x74 CraftResultNotice  0x78 EquipSlotUpdate  0x7a ItemPlaceResult
//   0x8a ItemCellClear      0x8c ItemCountNotice   0x8e UpgradeCountNotice
//   0x92 ItemMoveResult     0xa4 ItemBuyResult     0xa5 ChargeStackUpdate
//   0xad ItemSlotRefresh    0xb6 ItemCellReset
#include "Net/GameHandlers.h"
#include "Game/ClientRuntime.h"
#include "Net/ClientState.h"   // ts2::net::g_GmCmdCooldownLatch
#include <string>
#include <cstring>
#include <cstdint>

namespace {
using namespace ts2::game;

// NAMING TRAP (Game/GameState.h:373, file NOT owned here — do not rename):
// InvCell fields are semantically shifted relative to the binary's 6 SoA
// arrays. The OFFSETS are exact (stride 0x18 = 6 dwords), so the bytes
// written are correct. REASON IN POSITIONS, never in names:
//   itemId -> g_InvMain          gridX -> g_InvGrid_GridX   gridY -> g_InvGrid_GridY
//   flag   -> g_InvGrid_Count    color -> g_InvGrid_Durability
//   durability -> g_InvGrid_InstanceSerial

// Writes the SOURCE cell of the "pending move" — original index
// [384*g_PendingMove_SrcRow0 + 6*dword_1822EF0] = (pendingMoveRow, pendingMoveCol) —
// from 6 raw dwords {itemId, gridX, gridY, count, durability, serial}.
inline void WriteSrcCell(uint32_t itemId, uint32_t gridX, uint32_t gridY,
                         uint32_t count, uint32_t durability, uint32_t serial) {
    g_Client.inv.Set(static_cast<uint32_t>(g_Client.pendingMoveRow),
                     static_cast<uint32_t>(g_Client.pendingMoveCol),
                     itemId, gridX, gridY, count, durability, serial);
}

// Applies the source slot from the pending item snapshot
// (dword_1822F08.. = g_Client.pendingItem), without going through the payload.
//
// The binary does NOT OVERWRITE Count: it INCREMENTS it (`g_InvGrid_Count[...] +=
// dword_1822F14[0]`) at the 8 sites with a pending source — anchors 0x48b23c (0x1d rc1),
// 0x48b878 / 0x48ba01 (0x1e rc1/rc2), 0x48bfb8 / 0x48c141 (0x1f rc1/rc2),
// 0x4a6d62 / 0x4a6eeb (0x70 rc1/rc2), 0x4adb2a (0xa4 rc1). The other 5 fields
// are plain assignment.
inline void WriteSrcCellFromPending() {
    InvCell& e = g_Client.inv.At(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                 static_cast<uint32_t>(g_Client.pendingMoveCol));
    const InvCell& s = g_Client.pendingItem;
    e.itemId     = s.itemId;      // dword_1822F08 -> g_InvMain
    e.gridX      = s.gridX;       // dword_1822F0C -> g_InvGrid_GridX
    e.gridY      = s.gridY;       // dword_1822F10 -> g_InvGrid_GridY
    e.flag      += s.flag;        // dword_1822F14 -> g_InvGrid_Count  (`+=`, anchor 0x4a6d62)
    e.color      = s.color;       // dword_1822F18 -> g_InvGrid_Durability
    e.durability = s.durability;  // dword_1822F1C -> g_InvGrid_InstanceSerial
}

// --- PER-CELL "aux" arrays (original index [192*row + 3*col]) ------------
// The InventoryState model (Game/ClientRuntime.h:85, not owned here) only exposes
// three GLOBAL scalars aux0/aux1/aux2 — a wrong model: the binary has three ARRAYS
// indexed by cell. We therefore go through the Var(originalAddress) escape hatch
// blessed by the header (ClientRuntime.h:10-12), following the precedent already in
// place in Game/WarehouseSystem.cpp:13-15,124-126.
constexpr uint32_t kInvAuxBase  = 0x1674AB8; // g_InvAux
constexpr uint32_t kInvAux1Base = 0x1674ABC; // dword_1674ABC
constexpr uint32_t kInvAux2Base = 0x1674AC0; // dword_1674AC0

// Byte offset of the (row,col) cell in the aux arrays — proven stride
// 192 dwords/row, 3 dwords/cell (anchor Net_OnItemSlotRefresh 0x4b2390 @0x4b26be).
inline uint32_t InvAuxOff(int32_t row, int32_t col) {
    return 4u * static_cast<uint32_t>(192 * row + 3 * col);
}

// Writes the 3 aux dwords of (row,col) from the pending snapshot dword_1822F20/F24/F28
// (anchors 0x4a6aa8 / 0x4a6ac9 / 0x4a6aea).
inline void WriteCellAuxFromPending(int32_t row, int32_t col) {
    if (row < 0 || col < 0) return;
    const int32_t a0 = g_Client.Var(0x1822F20);
    const int32_t a1 = g_Client.Var(0x1822F24);
    const int32_t a2 = g_Client.Var(0x1822F28);
    const uint32_t off = InvAuxOff(row, col);
    g_Client.Var(kInvAuxBase  + off) = a0;
    g_Client.Var(kInvAux1Base + off) = a1;
    g_Client.Var(kInvAux2Base + off) = a2;
}

// Writes the 3 aux dwords of (row,col) from an arbitrary snapshot (base = address of
// the 1st dword; e.g. dword_1822F44 for the destination cell, anchor 0x4a7092).
inline void WriteCellAuxFromSnapshot(int32_t row, int32_t col, uint32_t base) {
    if (row < 0 || col < 0) return;
    const int32_t a0 = g_Client.Var(base);
    const int32_t a1 = g_Client.Var(base + 4);
    const int32_t a2 = g_Client.Var(base + 8);
    const uint32_t off = InvAuxOff(row, col);
    g_Client.Var(kInvAuxBase  + off) = a0;
    g_Client.Var(kInvAux1Base + off) = a1;
    g_Client.Var(kInvAux2Base + off) = a2;
}

// Clears the 3 aux dwords of (row,col) (anchors 0x4a6bc1 / 0x4a6be1 / 0x4a6c00 for the
// destination cell; 0x48c806 / 0x48c825 / 0x48c844 for the source cell of 0x20 case 100).
inline void ClearCellAux(int32_t row, int32_t col) {
    if (row < 0 || col < 0) return;
    const uint32_t off = InvAuxOff(row, col);
    g_Client.Var(kInvAuxBase  + off) = 0;
    g_Client.Var(kInvAux1Base + off) = 0;
    g_Client.Var(kInvAux2Base + off) = 0;
}

// --- Move DESTINATION cell ([384*dword_1822EDC + 6*dword_1822EF4]) ---------
// Guarded on >= 0 like the ClearExchangeCell precedent (Net/GameHandlers_InvDispatch.cpp:57):
// after reset these globals are -1 (don't overwrite a real slot).

// Writes cell (row,col) from a snapshot of 6 consecutive dwords (base = 1st).
// `addCount`: the binary does `+=` on aux Count at anchors 0x4a6fcc (0x70 rc2),
// 0x48bae2 (0x1e rc2) and 0x48c222 (0x1f rc2), but a plain ASSIGNMENT at anchor
// 0x48c5f7 (0x20 case 40) — hence the parameter.
inline void WriteCellFromSnapshot(int32_t row, int32_t col, uint32_t base, bool addCount) {
    if (row < 0 || col < 0) return;
    InvCell& e = g_Client.inv.At(static_cast<uint32_t>(row), static_cast<uint32_t>(col));
    const uint32_t itemId = static_cast<uint32_t>(g_Client.Var(base));       // e.g. dword_1822F2C
    const uint32_t gridX  = static_cast<uint32_t>(g_Client.Var(base + 4));   //     dword_1822F30
    const uint32_t gridY  = static_cast<uint32_t>(g_Client.Var(base + 8));   //     dword_1822F34
    const uint32_t count  = static_cast<uint32_t>(g_Client.Var(base + 12));  //     dword_1822F38 -> Count
    const uint32_t durab  = static_cast<uint32_t>(g_Client.Var(base + 16));  //     dword_1822F3C -> Durability
    const uint32_t serial = static_cast<uint32_t>(g_Client.Var(base + 20));  //     dword_1822F40 -> InstanceSerial
    e.itemId = itemId; e.gridX = gridX; e.gridY = gridY;
    if (addCount) e.flag += count; else e.flag = count;   // position 4 = g_InvGrid_Count
    e.color      = durab;                                 // position 5 = g_InvGrid_Durability
    e.durability = serial;                                // position 6 = g_InvGrid_InstanceSerial
}

// Clears the 6 fields of the destination cell (anchor Net_OnItemCombineResult 0x4a68f0
// @0x4a6b05..0x4a6ba2).
inline void ClearDestCell() {
    const int32_t r = g_Client.Var(0x1822EDC);
    const int32_t c = g_Client.Var(0x1822EF4);
    if (r >= 0 && c >= 0)
        g_Client.inv.ClearCell(static_cast<uint32_t>(r), static_cast<uint32_t>(c));
}

// --- "Pending move" reset ---------------------------------------
// The 4-write pattern is NOT universal: exhaustive audit of the 10 callers
// (0x1d/0x1e/0x1f/0x20/0x21/0x69/0x6a/0x70/0xa4/0xad) against their anchors — 3 variants.

// ONE write: g_PendingMove_SrcRow0 = -1 alone. Anchors: Net_OnItemSellResult
// 0x4a5ed0 @0x4a6007; Pkt_ItemCombineResult 0x48af50 @0x48b169 (rc0) / @0x48b2e8 (rc1).
inline void ResetPendingRow() {
    g_Client.pendingMoveRow = -1;
}

// TWO writes: + dword_1822EDC = -1. Anchors: 0x4a6c0b/@0x4a6c15 (0x70 rc0),
// 0x48b721/@0x48b72b (0x1e rc0), 0x48be61/@0x48be6b (0x1f rc0), 0x4ada48/@0x4ada52
// (0xa4 rc0), 0x48b4b1/@0x48b4bb (0x1d rc10).
inline void ResetPendingRow2() {
    g_Client.pendingMoveRow = -1;
    g_Client.Var(0x1822EDC) = -1;
}

// FOUR writes: + dword_1822EE0/EE4 = -1. Anchors: Net_OnItemCellSet 0x4a5d70
// @0x4a5e7b/@0x4a5e85/@0x4a5e8f/@0x4a5e99 (0x69); 0x48c8f1.. (0x20); 0x48caab..
// (0x21); 0x4b2707.. (0xad). ONLY these four opcodes write all 4.
inline void ResetPendingMove() {
    g_Client.pendingMoveRow = -1;
    g_Client.Var(0x1822EDC) = -1;
    g_Client.Var(0x1822EE0) = -1;
    g_Client.Var(0x1822EE4) = -1;
}
} // namespace

namespace ts2::net {

void RegisterInvCellHandlers(NetSystem& sys) {
    using namespace game;   // g_Client, Str()

    // 0x1d ItemCombineResult — item combine/craft result.
    // Pkt_ItemCombineResult 0x48af50: THREE distinct branches (0 / 1 / 10) tested
    // at anchors @0x48afb4 (if v15) / @0x48afba (== 1) / @0x48afc4 (== 10). Any other
    // value produces NOTHING (@0x48b4e6) — no generic "else" branch.
    OnPacket<ItemCombineResult>(sys, 0x1d, [](const ItemCombineResult& p) {
        if (p.resultCode == 0) {                     // anchor @0x48afcf
            g_GmCmdCooldownLatch = 0;                // @0x48afcf
            // TODO [anchor 0x48afe2]: guard `if (MobDb_GetEntry(mITEM, v8))` where v8 =
            //   itemId from the PAYLOAD (not the pending snapshot, unlike
            //   0x1e/0x1f/0x70/0xa4). Null DB entry -> no write, no reset, no
            //   message. MobDb isn't exposed to the network front: not modeled.
            g_Client.inv.weight -= p.weightDelta;    // @0x48b043
            // Source cell from the PAYLOAD, Count as ASSIGNMENT (@0x48b0c3).
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.serial); // @0x48b069..0x48b0ff
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol);  // @0x48b120/@0x48b141/@0x48b162
            ResetPendingRow();                       // @0x48b169 — ONE write only
            g_Client.msg.System(Str(716));           // @0x48b183
        } else if (p.resultCode == 1) {              // anchor @0x48b198
            g_GmCmdCooldownLatch = 0;                // @0x48b198
            // Branch WITHOUT MobDb guard and WITHOUT g_InvWeight (unlike 0/10).
            WriteSrcCellFromPending();               // @0x48b1bc..0x48b27e (Count `+=` @0x48b23c)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol);  // @0x48b29f/@0x48b2c0/@0x48b2e1
            ResetPendingRow();                       // @0x48b2e8 — ONE write only
            g_Client.msg.System(Str(715));           // @0x48b302
        } else if (p.resultCode == 10) {             // anchor @0x48b317
            g_GmCmdCooldownLatch = 0;                // @0x48b317
            // TODO [anchor 0x48b32a]: guard `if (MobDb_GetEntry(mITEM, v8))` (itemId from
            //   the PAYLOAD) — same note as branch 0.
            g_Client.inv.weight -= p.weightDelta;    // @0x48b38b
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.serial); // @0x48b3b1..0x48b447 (Count `=` @0x48b40b)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol);  // @0x48b468/@0x48b489/@0x48b4aa
            ResetPendingRow2();                      // @0x48b4b1/@0x48b4bb — TWO writes
            g_Client.msg.System(Str(2697));          // @0x48b4d6
        }
        // resultCode not in {0,1,10}: no effect (@0x48b4e6).
    });

    // 0x1e ItemSwapResultA — confirms a pending item move/swap.
    // Pkt_ItemSwapResultA 0x48b520. The latch AND the reset are INTERNAL to each branch:
    // for resultCode >= 3 the binary does strictly NOTHING (@0x48bc26).
    OnPacket<ItemSwapResultA>(sys, 0x1e, [](const ItemSwapResultA& p) {
        if (p.resultCode == 0) {         // plain OK: cell from the payload — @0x48b584
            g_GmCmdCooldownLatch = 0;    // @0x48b584
            // TODO [anchor 0x48b59a]: guard `if (MobDb_GetEntry(mITEM, dword_1822F08[0]))`
            //   (itemId from the pending snapshot) — not modeled (MobDb out of network front scope).
            g_Client.inv.weight -= p.weightDelta;                    // @0x48b5fb
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]); // @0x48b621..0x48b6b7 (Count `=` @0x48b67b)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x48b6d8/@0x48b6f9/@0x48b71a
            ResetPendingRow2();                                      // @0x48b721/@0x48b72b
            g_Client.msg.System(Str(222));                           // @0x48b746
        } else if (p.resultCode == 1) {  // OK via pending snapshot (count add) — @0x48b75b
            g_GmCmdCooldownLatch = 0;    // @0x48b75b
            // TODO [anchor 0x48b770]: guard MobDb_GetEntry(mITEM, dword_1822F08[0]).
            g_Client.inv.weight -= p.weightDelta;                    // @0x48b7d1
            WriteSrcCellFromPending();                               // @0x48b7f8..0x48b8ba (Count `+=` @0x48b878)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x48b8db/@0x48b8fc/@0x48b91d
            ResetPendingRow2();                                      // @0x48b924/@0x48b92e
            g_Client.msg.System(Str(223));                           // @0x48b948
        } else if (p.resultCode == 2) {  // source <-> destination swap — @0x48b95d
            g_GmCmdCooldownLatch = 0;    // @0x48b95d — branch WITHOUT MobDb guard and WITHOUT weight
            const int32_t srcRow = g_Client.pendingMoveRow, srcCol = g_Client.pendingMoveCol;
            const int32_t dstRow = g_Client.Var(0x1822EDC), dstCol = g_Client.Var(0x1822EF4);
            WriteSrcCellFromPending();                               // @0x48b981..0x48ba43 (Count `+=` @0x48ba01)
            WriteCellFromSnapshot(dstRow, dstCol, 0x1822F2C, true);  // @0x48ba64..0x48bb24 (Count `+=` @0x48bae2)
            WriteCellAuxFromPending(srcRow, srcCol);                 // @0x48bb45/@0x48bb66/@0x48bb87
            WriteCellAuxFromSnapshot(dstRow, dstCol, 0x1822F44);     // @0x48bba8/@0x48bbc9/@0x48bbea
            ResetPendingRow2();                                      // @0x48bbf1/@0x48bbfb
            g_Client.msg.System(Str(726));                           // @0x48bc16
        }
    });

    // 0x1f ItemSwapResultB — move/swap variant (same 3 branches).
    // Pkt_ItemSwapResultB 0x48bc60: identical structure to 0x1e, only the branch-2
    // message changes (727 instead of 726). Latch/reset INTERNAL to each branch —
    // resultCode >= 3: no effect (@0x48c366).
    OnPacket<ItemSwapResultB>(sys, 0x1f, [](const ItemSwapResultB& p) {
        if (p.resultCode == 0) {         // direct commit from the payload — @0x48bcc4
            g_GmCmdCooldownLatch = 0;    // @0x48bcc4
            // TODO [anchor 0x48bcda]: guard MobDb_GetEntry(mITEM, dword_1822F08[0]).
            g_Client.inv.weight -= p.weightDelta;                    // @0x48bd3b
            WriteSrcCell(p.item[0], p.item[1], p.item[2],
                         p.item[3], p.item[4], p.item[5]);           // @0x48bd61..0x48bdf7 (Count `=` @0x48bdbb)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x48be18/@0x48be39/@0x48be5a
            ResetPendingRow2();                                      // @0x48be61/@0x48be6b
            g_Client.msg.System(Str(222));                           // @0x48be86
        } else if (p.resultCode == 1) {  // commit via pending snapshot — @0x48be9b
            g_GmCmdCooldownLatch = 0;    // @0x48be9b
            // TODO [anchor 0x48beb0]: guard MobDb_GetEntry(mITEM, dword_1822F08[0]).
            g_Client.inv.weight -= p.weightDelta;                    // @0x48bf11
            WriteSrcCellFromPending();                               // @0x48bf38..0x48bffa (Count `+=` @0x48bfb8)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x48c01b/@0x48c03c/@0x48c05d
            ResetPendingRow2();                                      // @0x48c064/@0x48c06e
            g_Client.msg.System(Str(223));                           // @0x48c088
        } else if (p.resultCode == 2) {  // swap: source + destination — @0x48c09d
            g_GmCmdCooldownLatch = 0;    // @0x48c09d — branch WITHOUT MobDb guard and WITHOUT weight
            const int32_t srcRow = g_Client.pendingMoveRow, srcCol = g_Client.pendingMoveCol;
            const int32_t dstRow = g_Client.Var(0x1822EDC), dstCol = g_Client.Var(0x1822EF4);
            WriteSrcCellFromPending();                               // @0x48c0c1..0x48c183 (Count `+=` @0x48c141)
            WriteCellFromSnapshot(dstRow, dstCol, 0x1822F2C, true);  // @0x48c1a4..0x48c264 (Count `+=` @0x48c222)
            WriteCellAuxFromPending(srcRow, srcCol);                 // @0x48c285/@0x48c2a6/@0x48c2c7
            WriteCellAuxFromSnapshot(dstRow, dstCol, 0x1822F44);     // @0x48c2e8/@0x48c309/@0x48c32a
            ResetPendingRow2();                                      // @0x48c331/@0x48c33b
            g_Client.msg.System(Str(727));                           // @0x48c356
        }
    });

    // 0x20 ItemDiscardResult — item discard/removal result.
    // Pkt_ItemDiscardResult 0x48c3a0. Always writes the source (pending) cell from
    // the payload, adjusts weight/currency per adjustMode, then dispatches resultCode.
    OnPacket<ItemDiscardResult>(sys, 0x20, [](const ItemDiscardResult& p) {
        g_GmCmdCooldownLatch = 0;   // @0x48c400 — unconditional, BEFORE everything else
        WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.instanceSerial); // @0x48c432..0x48c4cd (Count `=` @0x48c48f)
        if (p.adjustMode == 1)      g_Client.inv.weight   -= p.amount;   // @0x48c4e3
        else if (p.adjustMode == 2) g_Client.inv.currency -= p.amount;   // @0x48c4fa (+ mirror dword_1687254[0] @0x48c508)
        switch (p.resultCode) {     // @0x48c527
            case 0:
                g_Client.msg.System(Str(730));    // @0x48c53f
                break;
            case 1:
                g_Client.msg.System(Str(2310));   // @0x48c565
                break;
            case 40: {
                // Restores THREE full cells from three snapshots, to three
                // distinct (row,col) pairs. Count as ASSIGNMENT (@0x48c5f7 /
                // @0x48c6bd / @0x48c783) — unlike the `+=` of the swap branches.
                WriteCellFromSnapshot(g_Client.Var(0x1822EDC), g_Client.Var(0x1822EF4),
                                      0x1822F2C, false);   // @0x48c594..0x48c639
                WriteCellFromSnapshot(g_Client.Var(0x1822EE0), g_Client.Var(0x1822EF8),
                                      0x1822F50, false);   // @0x48c65a..0x48c6ff
                WriteCellFromSnapshot(g_Client.Var(0x1822EE4), g_Client.Var(0x1822EFC),
                                      0x1822F74, false);   // @0x48c720..0x48c7c5
                g_Client.msg.System(Str(2310));            // @0x48c7dc
                break;
            }
            case 100:
                // Clears the 3 aux dwords of THE SOURCE CELL [192*row0 + 3*col0] — not the
                // global scalars (@0x48c806/@0x48c825/@0x48c844).
                ClearCellAux(g_Client.pendingMoveRow, g_Client.pendingMoveCol);
                g_Client.msg.System(Str(730));             // @0x48c860
                break;
            case 101:
                ClearCellAux(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x48c886/@0x48c8a6/@0x48c8c5
                g_Client.msg.System(Str(2310));            // @0x48c8e1
                break;
            default:
                break;
        }
        ResetPendingMove();   // @0x48c8f1/@0x48c8fb/@0x48c905/@0x48c90f — 4 writes
    });

    // 0x21 ItemResultSimple — simple result: reapplies a cell to the pending slot.
    OnPacket<ItemResultSimple>(sys, 0x21, [](const ItemResultSimple& p) {
        if (p.status == 0) {
            g_GmCmdCooldownLatch = 0;
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.instanceSerial);
            ResetPendingMove();
            g_Client.msg.System(Str(731));
        }
        // status != 0: no effect.
    });

    // 0x69 ItemCellSet — places an item (6 dwords) into the pending move slot.
    OnPacket<ItemCellSet>(sys, 0x69, [](const ItemCellSet& p) {
        if (p.resultCode == 0) {
            g_GmCmdCooldownLatch = 0;
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.serial);
            ResetPendingMove();
            g_Client.msg.System(Str(1304));
        }
    });

    // 0x6a ItemSellResult — item sale: credits weight, reloads the source cell.
    // Net_OnItemSellResult 0x4a5ed0. No MobDb guard, no aux writes in this handler.
    OnPacket<ItemSellResult>(sys, 0x6a, [](const ItemSellResult& p) {
        if (p.resultCode == 0) {
            g_GmCmdCooldownLatch = 0;                   // @0x4a5f2a
            g_Client.inv.weight += p.weightDelta;       // @0x4a5f4d (`+=`, not `-=`)
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]); // @0x4a5f6a..0x4a6000 (Count `=` @0x4a5fc4)
            // ONE reset write only (@0x4a6007): 0x6a does NOT touch
            // dword_1822EDC/EE0/EE4, unlike 0x69 (0x4a5d70 @0x4a5e7b..0x4a5e99).
            ResetPendingRow();                          // @0x4a6007
            g_Client.msg.System(Str(1305));             // @0x4a6022
        } else if (p.resultCode == 1) {
            g_Client.msg.System(Str(1912));             // @0x4a6044
        }
    });

    // 0x6b GambleResult — lottery/gamble result (disconnects on a dry failure).
    // Net_OnGambleResult 0x4a6060: all THREE display branches go through the SAME
    // format aDS_2 "[%d]%s" (0x7a6d88) with value as prefix, and pass the literal
    // color 2 to Msg_AppendSystemLine (@0x4a6102/@0x4a6158/@0x4a61cf).
    OnPacket<GambleResult>(sys, 0x6b, [&sys](const GambleResult& p) {
        if (p.selector == 1) {           // win — @0x4a60d6/@0x4a60ec
            g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(1351), 2);
            // (sound played on the audio side, out of scope)
        } else if (p.selector == 2) {    // end/failure
            if (static_cast<int32_t>(p.value) <= 0) {   // @0x4a6120
                NetCloseSocket(sys.Client()); // disconnect on dry failure — @0x4a6174
                g_Client.Var(0x1676180) = 2;  // g_SceneMgr      = 2 — @0x4a6179
                g_Client.Var(0x1676184) = 0;  // g_SceneSubState = 0 — @0x4a6183
                g_Client.Var(0x1676188) = 0;  // dword_1676188   = 0 — @0x4a618d
                // TODO [anchors 0x4a6179/0x4a6183]: these three writes go through the
                //   Var() escape hatch and are NOT read back by anyone — the real
                //   scene switch lives in ts2::g_SceneMgr / ts2::g_SceneSubState
                //   (Scene/SceneManager.h), not owned by this front. The return to
                //   the character-select screen therefore doesn't happen: wiring to be added.
            } else {                                    // @0x4a612c/@0x4a6142
                g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(1350), 2);
            }
        } else if (p.selector == 3) {    // info — @0x4a61a3/@0x4a61b9
            g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(1394), 2);
        }
    });

    // 0x70 ItemCombineResult2 — combine/gem-setting: updates 1 or 2 cells.
    // Net_OnItemCombineResult 0x4a68f0: THREE branches with DISTINCT behavior
    // (@0x4a6939 `if (v9)` / @0x4a693f `== 1` / @0x4a6949 `== 2`). Branches 0 and 1
    // share NEITHER the data source NOR the message.
    OnPacket<ItemCombineResult2>(sys, 0x70, [](const ItemCombineResult2& p) {
        if (p.resultCode == 0) {                        // anchor @0x4a6954
            g_GmCmdCooldownLatch = 0;                   // @0x4a6954
            // TODO [anchor 0x4a696a]: guard `if (MobDb_GetEntry(mITEM, dword_1822F08[0]))`
            //   — null DB entry -> no write, no reset, no message. Not modeled
            //   (MobDb/mITEM aren't exposed to the network front).
            g_Client.inv.weight -= p.weightDelta;       // @0x4a69cb
            // Source cell from the PAYLOAD (v8 = Crt_Memcpy(0x8156C9,0x18) @0x4a6927),
            // Count as ASSIGNMENT (@0x4a6a4b) — definitely NOT from the pending snapshot.
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.serial); // @0x4a69f1..0x4a6a87
            // ... but the 3 aux dwords DO come from the PENDING snapshot, not the payload.
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol);  // @0x4a6aa8/@0x4a6ac9/@0x4a6aea
            // The DESTINATION cell is fully zeroed (9 fields).
            ClearDestCell();                                                    // @0x4a6b05..0x4a6ba2
            ClearCellAux(g_Client.Var(0x1822EDC), g_Client.Var(0x1822EF4));     // @0x4a6bc1/@0x4a6be1/@0x4a6c00
            ResetPendingRow2();                         // @0x4a6c0b/@0x4a6c15
            g_Client.msg.System(Str(222));              // @0x4a6c30 — 222, NOT 1645
        } else if (p.resultCode == 1) {                 // anchor @0x4a6c45
            g_GmCmdCooldownLatch = 0;                   // @0x4a6c45
            // TODO [anchor 0x4a6c5a]: guard MobDb_GetEntry(mITEM, dword_1822F08[0]).
            g_Client.inv.weight -= p.weightDelta;       // @0x4a6cbb
            WriteSrcCellFromPending();                  // @0x4a6ce2..0x4a6da4 (Count `+=` @0x4a6d62)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x4a6dc5/@0x4a6de6/@0x4a6e07
            // NO clearing of the destination cell in this branch.
            ResetPendingRow2();                         // @0x4a6e0e/@0x4a6e18
            g_Client.msg.System(Str(1645));             // @0x4a6e32 — 1645, NOT 222
        } else if (p.resultCode == 2) {                 // anchor @0x4a6e47
            g_GmCmdCooldownLatch = 0;                   // @0x4a6e47
            // Branch WITHOUT MobDb guard and WITHOUT g_InvWeight: the `g_InvWeight -= v7`
            // only exists at anchors 0x4a69cb (rc0) and 0x4a6cbb (rc1). Don't deduct anything.
            const int32_t srcRow = g_Client.pendingMoveRow, srcCol = g_Client.pendingMoveCol;
            const int32_t dstRow = g_Client.Var(0x1822EDC), dstCol = g_Client.Var(0x1822EF4);
            WriteSrcCellFromPending();                              // @0x4a6e6b..0x4a6f2d (Count `+=` @0x4a6eeb)
            WriteCellFromSnapshot(dstRow, dstCol, 0x1822F2C, true); // @0x4a6f4e..0x4a700e (Count `+=` @0x4a6fcc)
            WriteCellAuxFromPending(srcRow, srcCol);                // @0x4a702f/@0x4a7050/@0x4a7071
            WriteCellAuxFromSnapshot(dstRow, dstCol, 0x1822F44);    // @0x4a7092/@0x4a70b3/@0x4a70d4
            ResetPendingRow2();                                     // @0x4a70db/@0x4a70e5
            g_Client.msg.System(Str(1645));                         // @0x4a7100
        }
        // resultCode not in {0,1,2}: no effect (@0x4a7110).
    });

    // 0x74 CraftResultNotice — craft/production result message.
    OnPacket<CraftResultNotice>(sys, 0x74, [](const CraftResultNotice& p) {
        if (p.mode == 1) {
            if (p.count <= 1)
                g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(316), 1);
            else
                g_Client.msg.System("[" + std::to_string(p.count) + "]" + Str(1479), 1);
        } else if (p.mode == 0 && p.value > 0) {
            g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(1839), 1);
        }
    });

    // 0x78 EquipSlotUpdate — writes an equipment slot (skill/equip container, stride
    // 42 dwords, cell 3 dwords) and clears the source inventory cell.
    OnPacket<EquipSlotUpdate>(sys, 0x78, [](const EquipSlotUpdate& p) {
        const uint32_t idx  = 42u * p.contRow + 3u * p.contCol;   // dword index
        const uint32_t base = idx * 4u;                            // byte offset
        g_Client.Var(0x16743FC + base) = static_cast<int32_t>(p.itemId);  // g_Container5_ItemId
        g_Client.Var(0x1674400 + base) = static_cast<int32_t>(p.field1);  // dword_1674400
        g_Client.Var(0x1674404 + base) = static_cast<int32_t>(p.field2);  // dword_1674404
        g_Client.inv.ClearCell(p.invRow, p.invCol);
    });

    // 0x7a ItemPlaceResult — item placement result into a slot (coords from payload).
    OnPacket<ItemPlaceResult>(sys, 0x7a, [](const ItemPlaceResult& p) {
        if (p.resultCode == 1) {
            g_Client.inv.Set(p.bagRow, p.slotCol, p.itemId,
                             p.cellIndex % 8, p.cellIndex / 8, 0, p.durability, 0);
            g_Client.msg.System(Str(1911));
        } else if (p.resultCode == 2) {
            g_Client.msg.System(Str(117));
        }
    });

    // 0x8a ItemCellClear — places a "bare" cell (item without count/durability) at the payload slot.
    OnPacket<ItemCellClear>(sys, 0x8a, [](const ItemCellClear& p) {
        if (p.resultCode == 0) {
            g_Client.inv.Set(p.invPage, p.invSlot, p.itemId,
                             p.gridPos % 8, p.gridPos / 8, 0, 0, 0);
        }
    });

    // 0x8c ItemCountNotice — count notification (floating HUD + system line).
    // Net_OnItemCountNotice 0x4aab90: two SUCCESSIVE `if`s with return (@0x4aabd0 then
    // @0x4aabd6), not a ternary — subop not in {0,1} exits SILENTLY (@0x4aac68). Same shape
    // as sibling handler 0x8e (Net_OnUpgradeCountNotice 0x4aae70), already faithful.
    OnPacket<ItemCountNotice>(sys, 0x8c, [](const ItemCountNotice& p) {
        if (p.subop == 0) {                                            // @0x4aabd0
            const std::string t = std::to_string(p.count) + Str(2074); // @0x4aabe4 / "%d%s" @0x4aabf7
            g_Client.msg.Floating(1, 0, t);                            // @0x4aac11
            g_Client.msg.System(t, 1);                                 // @0x4aac63
        } else if (p.subop == 1) {                                     // @0x4aabd6
            const std::string t = std::to_string(p.count) + Str(1351); // @0x4aac22 / @0x4aac35
            g_Client.msg.Floating(1, 0, t);                            // @0x4aac4f
            g_Client.msg.System(t, 1);                                 // @0x4aac4f
        }
        // subop not in {0,1}: no message (@0x4aac68).
    });

    // 0x8e UpgradeCountNotice — upgrade-counter notices.
    OnPacket<UpgradeCountNotice>(sys, 0x8e, [](const UpgradeCountNotice& p) {
        if (p.mode == 0) {
            const std::string t = std::to_string(p.count) + Str(2074);
            g_Client.msg.Floating(1, 0, t);
            g_Client.msg.System(t, 1);
        } else if (p.mode == 1) {
            const std::string t = std::to_string(p.count) + Str(1351);
            g_Client.msg.Floating(1, 0, t);
            g_Client.msg.System(t, 1);
        } else if (p.mode == 2) {
            if (static_cast<int32_t>(p.count) - 1 > 0) {
                const std::string t = std::to_string(p.count - 1) + Str(2195);
                g_Client.msg.Floating(1, 0, t);
                g_Client.msg.System(t, 1);
            }
            if (p.count > 0) {
                const std::string t = std::to_string(p.count) + Str(2196);
                g_Client.msg.Floating(1, 0, t);
                g_Client.msg.System(t, 1);
            }
        }
    });

    // 0x92 ItemMoveResult — item move result (coords from payload).
    OnPacket<ItemMoveResult>(sys, 0x92, [](const ItemMoveResult& p) {
        if (p.resultCode == 0) {
            g_Client.inv.Set(p.bagRow, p.slotCol, p.itemId,
                             p.cellIndex % 8, p.cellIndex / 8, 0, 0, 0);
            g_Client.msg.System(Str(119));
        } else if (p.resultCode == 1) {
            g_Client.msg.System(Str(223));
        } else if (p.resultCode == 2) {
            g_Client.msg.System(Str(117));
        }
    });

    // 0xa4 ItemBuyResult — purchase: deducts the cost, reloads the source (pending) cell.
    // Net_OnItemBuyResult 0x4ad8a0.
    OnPacket<ItemBuyResult>(sys, 0xa4, [](const ItemBuyResult& p) {
        g_GmCmdCooldownLatch = 0;          // @0x4ad8cc
        g_Client.inv.weight -= 10000000;   // @0x4ad8e2: `g_InvWeight -= (int)&unk_989680`
                                           //   — it's the ADDRESS 0x989680 (= 10,000,000)
                                           //   used as the constant, not its content.
        // TODO [anchor 0x4ad8f3]: GLOBAL guard `if (MobDb_GetEntry(mITEM, dword_1822F08[0]))` —
        //   it wraps BOTH branches (@0x4ad8ff). Null DB entry -> no write, no reset,
        //   no message (the weight deduction above has already happened, though).
        //   Not modeled: MobDb out of network front scope.
        if (p.resultCode == 0) {           // @0x4ad910
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]); // @0x4ad948..0x4ad9de (Count `=` @0x4ad9a2)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x4ad9ff/@0x4ada20/@0x4ada41
            ResetPendingRow2();            // @0x4ada48/@0x4ada52 — TWO writes, not 4
            g_Client.msg.System(Str(2388));// @0x4ada6d
        } else if (p.resultCode == 1) {    // @0x4ad916
            WriteSrcCellFromPending();     // @0x4adaac..0x4adb6c (Count `+=` @0x4adb2a)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x4adb8d/@0x4adbae/@0x4adbcf
            ResetPendingRow2();            // @0x4adbd6/@0x4adbe0
            g_Client.msg.System(Str(2389));// @0x4adbfb
        }
    });

    // 0xa5 ChargeStackUpdate — auto-potion belt (charge stacks dword_16757D8).
    OnPacket<ChargeStackUpdate>(sys, 0xa5, [](const ChargeStackUpdate& p) {
        g_GmCmdCooldownLatch = 0;
        if (p.mode == 1 && p.flag == 0) {          // paid recharge
            g_Client.Var(0x16757D8 + 4u * p.index) = 10;
            g_Client.inv.currency -= 10;
        } else if (p.mode == 0 && p.flag == 0) {   // consumption / slot switch
            g_Client.Var(0x1675800) = static_cast<int32_t>(p.index); // active slot
            g_Client.Var(0x16757D8 + 4u * p.index) -= 1;
            g_Client.Var(0x1675804) = 60;          // cooldown
            // TODO: if the slot item == 878, recompute attack bounds;
            //        clean up the old slot if depleted (Char_CalcAttackRating*).
        }
    });

    // 0xad ItemSlotRefresh — refreshes the source (pending) cell and deducts gold.
    // Net_OnItemSlotRefresh 0x4b2390 (switch @0x4b23e9). No MobDb guard here.
    OnPacket<ItemSlotRefresh>(sys, 0xad, [](const ItemSlotRefresh& p) {
        if (p.resultCode == 0 || p.resultCode == 10) {   // @0x4b25d6 (cases 0 and 10 merged)
            g_GmCmdCooldownLatch = 0;                    // @0x4b25d6
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]); // @0x4b2607..0x4b269d (Count `=` @0x4b2661)
            // The 3 aux dwords come from the pending snapshot — BEFORE the reset (which sets row0 to -1).
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x4b26be/@0x4b26df/@0x4b2700
            ResetPendingMove();                          // @0x4b2707..0x4b2725 — 4 writes
            g_Client.inv.currency -= p.goldDelta;        // @0x4b2738 (+ mirror dword_1687254[0] @0x4b2746)
            g_Client.msg.System(Str(2563));              // @0x4b275c
        } else if (p.resultCode == 1) {
            g_Client.msg.System(Str(2569));              // @0x4b259b
        } else if (p.resultCode == 2) {
            g_Client.msg.System(Str(2561));              // @0x4b25c1
        }
    });

    // 0xb6 ItemCellReset — clears a cell (coords from payload) and remembers 3 coords.
    OnPacket<ItemCellReset>(sys, 0xb6, [](const ItemCellReset& p) {
        g_GmCmdCooldownLatch = 0;
        g_Client.inv.ClearCell(p.bagRow, p.slotCol);
        g_Client.msg.System(Str(2773));
        g_Client.Var(0x1675118) = static_cast<int32_t>(p.coordA);
        g_Client.Var(0x167511C) = static_cast<int32_t>(p.coordB);
        g_Client.Var(0x1675120) = static_cast<int32_t>(p.coordC);
    });
}

} // namespace ts2::net

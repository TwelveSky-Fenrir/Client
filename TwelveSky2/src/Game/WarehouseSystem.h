// Game/WarehouseSystem.h — WAREHOUSE/Storage system of the TS2 client.
//
// Fills the "1232-byte blob undecoded" TODO left in
// Net/GameHandlers_VendorTrade.cpp (0x22 Pkt_WarehouseOpen, 0x23 Pkt_WarehouseClose,
// 0x24 Pkt_WarehouseUpdate): the 1232-byte network blob is copied AS-IS
// (Crt_Memcpy) into dword_18229CC there without being parsed at the packet level.
// This module provides the BYTE-EXACT layout of that blob (deduced from
// UI_StorageWin_CommitGrid/UI_StorageWin_Open, the only functions that read/write
// its fields), plus the associated slot-search / move logic.
//
// Function <-> original-address correspondence (see also the .cpp for line-by-
// line detail):
//   Pkt_WarehouseOpen           0x48cb00   Pkt_WarehouseClose        0x48cd90
//   Pkt_WarehouseUpdate         0x48ce40   Net_OnWarehouseMoveResult 0x4a61f0
//   Warehouse_FindFreeSlot      0x54e240   Warehouse_FindItemCell    0x65ee10
//   Game_IsNearWarehouseSpot    0x5d8390   UI_Warehouse_SlotAt       0x5f5310
//   UI_Warehouse_Click          0x5f4500   Net_WarehouseAction_3     0x5cb5d0
//   Net_WarehouseAction_4       0x5cb6d0
//   UI_StorageWin_CommitGrid    0x5d2f70   UI_StorageWin_Open        0x5d27a0
//     (these last two are not in the required list but are what actually
//      DECODES the blob — needed to deduce its layout)
//
// IMPORTANT GAP found during RE (documented, not hidden):
//   Net_OnWarehouseMoveResult, Net_WarehouseAction_3/4, UI_Warehouse_SlotAt and
//   UI_Warehouse_Click do NOT touch dword_18229CC (the 1232-byte blob). They
//   operate on dword_1674A4C (0..19 cursor of a "rack" g_ThrowWeaponRack,
//   9 slots, EA 0x4a631a/0x4a6328) and on a much larger UI structure
//   (this+802..+3048, 560/40/4 dword paging) that does not correspond to any
//   of the addresses above. This module faithfully implements WHAT THESE
//   functions actually do and documents the gap at each relevant point.
//
// +-- CORRECTION (Pass 4 / W11, gap INV-02) ----------------------------------+
// | This banner used to conclude that the IDA "Warehouse_*" naming was       |
// | "partly wrong or reused for a neighboring system." THAT IS FALSE, and    |
// | this incorrect RE conclusion is retracted: it would mislead later passes |
// | (same kind of false friend as Crt_StringInit / Crt_FreeBase). The slip   |
// | was on the C++ side, NOT on the IDA side.                                |
// |                                                                          |
// | Warehouse_FindFreeSlot 0x54E240 does NOT scan the 5x5 blob: it scans a   |
// | DIFFERENT container, the REAL vault = 2 pages x 28 slots at dword_1673F3C.|
// | IDA head comment of 0x54E240, verbatim:                                  |
// |   "[game] find free cell in 2-page x 28-slot warehouse grid"             |
// | Re-proven at the instruction level (2026-07-16), 4 independent           |
// | derivations:                                                             |
// |  1. 0x54E240: `imul eax, 1C0h` (page = 448 bytes = 112 dw) / `shl ecx, 4`|
// |     (slot = 16 bytes = 4 dw) / `cmp [ebp+var_8], 1Ch` (28 slots);        |
// |     448/16 = 28 OK; `cmp dword_1673F34, 0 / jle` -> 1 or 2 pages;        |
// |     `retn 8` => __stdcall(int* page, int* slot).                         |
// |  2. Inv_AddItemQuantity case 12 (aux): `imul 150h` (336 bytes) / `imul   |
// |     0Ch` (12 bytes) -> 336/12 = 28 slots/page, INDEPENDENT derivation of |
// |     112/4.                                                                |
// |  3. Memory boundary: 0x16751B4 + 0x2A0 = 0x1675454 (aux stall);          |
// |     0x2A0 = 672 = 56 x 12 = 2 pages x 28 EXACTLY.                        |
// |  4. UI_Refine_HitTestEmptySlot 0x5E3750: `for(i<28)` over a 4x7 grid,    |
// |     `dword_1673F3C[112*page + 4*i] >= 1` = occupied.                     |
// | It is the UI_Refine_* family that drives THIS vault — THAT is the one    |
// | misnamed. The TWO containers coexist: the 5x5 blob (dword_18229CC,       |
// | WarehouseGrid below, correct and proven model) and the 2x28 vault        |
// | (dword_1673F3C, modeled further below — it was not modeled at all).      |
// +----------------------------------------------------------------------------+
//
// Project rule: this file does not edit any existing header; it includes
// Game/GameState.h and Game/ClientRuntime.h read-only.
#pragma once
#include <cstdint>
#include <cstddef>
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"

namespace ts2::game {

#pragma pack(push, 1)

// ===========================================================================
// Warehouse-grid "item" cell — 36 bytes (9 dword).
// Stride confirmed by the indices used in UI_StorageWin_CommitGrid/Open:
// `a1[45*row + 9*col + 23 + k]`, k=0..8 -> 9 consecutive fields, column
// advances by 9 dword (36 bytes), row by 45 dword (180 bytes = 5 columns x 36 bytes).
// ===========================================================================
struct WarehouseItemCell {
    int32_t itemId;         // +0  (idx+23) -> g_InvMain[...]               EA 0x5d3072
    int32_t count;          // +4  (idx+24) -> g_InvGrid_Count[...]         EA 0x5d318c
    int32_t durability;     // +8  (idx+25) -> g_InvGrid_Durability[...]    EA 0x5d31ea
    int32_t instanceSerial; // +12 (idx+26) -> g_InvGrid_InstanceSerial[..] EA 0x5d3248
    int32_t unknown14;      // +16 (idx+27) never copied by CommitGrid (only
                             //     set to 0/-1 on clear) — role unconfirmed.
                             //     TODO EA 0x5d2e00 (Open, clear) / 0x5d3f6b (Commit clear).
    int32_t destPage;       // +20 (idx+28) destination page in g_InvMain (x384 mult.) EA 0x5d3072
    int32_t destSlot;       // +24 (idx+29) destination slot in g_InvMain (x6 mult.)   EA 0x5d3072
    int32_t gridX;          // +28 (idx+30) -> g_InvGrid_GridX[...]          EA 0x5d30d0
    int32_t gridY;          // +32 (idx+31) -> g_InvGrid_GridY[...]          EA 0x5d312e

    bool Empty() const { return itemId < 1; } // EA 0x5d3018: `a1[...+23] >= 1` = occupied
};
static_assert(sizeof(WarehouseItemCell) == 36, "WarehouseItemCell must be 36 bytes (9 dword)");

// ===========================================================================
// "Aux" cell (options/sockets attached to the item) — 12 bytes (3 dword).
// Stride: `a1[15*row + 3*col + 248 + k]`, k=0..2, column=3 dword (12 bytes),
// row=15 dword (60 bytes = 5 columns x 12 bytes).
// ===========================================================================
struct WarehouseAuxCell {
    int32_t aux0; // -> g_InvAux[...]     (base 0x1674AB8, stride 192*page+3*slot) EA 0x5d32a5
    int32_t aux1; // -> dword_1674ABC[...] (base 0x1674ABC, same stride)           EA 0x5d3303
    int32_t aux2; // -> dword_1674AC0[...] (base 0x1674AC0, same stride)           EA 0x5d335d/0x5d3361
};
static_assert(sizeof(WarehouseAuxCell) == 12, "WarehouseAuxCell must be 12 bytes (3 dword)");

// ===========================================================================
// Warehouse grid — BYTE-EXACT image of the 1232-byte network blob.
//
// Origin: Pkt_WarehouseOpen 0x48cb00 (`Crt_Memcpy(&dword_18229CC, v13, 1232)`
// EA 0x48cbb0 open mode / EA 0x48cbf7 mode 100) and Pkt_WarehouseUpdate
// 0x48ce40 (EA 0x48ce77 network receive -> 1232-byte local buffer, EA 0x48cf02
// copy -> dword_18229CC modes 0/3). The blob is copied AS-IS (no per-field
// parsing) over the shared UI "storage window" (dword_1822990, also reused
// by the vendor/trade window — see UI_StorageWin_Open 0x5d27a0 modes 1/2/3/4/5),
// starting at dword index 15 (dword_18229CC == dword_1822990 + 0x3C == this+15).
//
// Layout deduced by cross-referencing the indices read/written by
// UI_StorageWin_CommitGrid 0x5d2f70 (copies grid -> g_InvMain/g_InvGrid_*/
// g_InvAux/dword_1674ABC/AC0, EA 0x5d3018..0x5d3ed2) and UI_StorageWin_Open
// 0x5d27a0 (init/clear of the same grid, EA 0x5d28f8..0x5d2ee6):
//
//   offset 0x000 (32 bytes) : header/reserved. In the shared window, this
//                          range (this+15..+22) overlaps the TAIL of two
//                          CStrings (this+11 and this+16, initialized by
//                          Crt_StringInit EA 0x5d28dc/0x5d28f0) used by the
//                          OTHER modes of this same window (vendor name/
//                          trade partner — see also Pkt_PlayerShopOpen,
//                          which writes this+16, EA 0x48da1a/0x48da0d). No
//                          field in this range is re-read by the warehouse
//                          path: treated here as reserved.
//   offset 0x020 (900 bytes): 5x5 grid of WarehouseItemCell (36 bytes/cell),
//                          EA 0x5d3018 (occupied test) .. 0x5d3248 (copy),
//                          init/clear EA 0x5d2946..0x5d29bd.
//   offset 0x3A4 (300 bytes): 5x5 grid of WarehouseAuxCell (12 bytes/cell),
//                          EA 0x5d32a5..0x5d3361 (copy), 0x5d29db..0x5d2a16 (init).
//   total 32 + 900 + 300 = 1232 bytes exactly (verified: the last aux cell
//   (row 4, col 4) ends exactly at byte 1232 — EA 0x5d3e16/0x5d3ed2).
//
// Semantic note: these 5x5 = 25 cells form the deposit/withdraw/sort window
// displayed by the client (UI_StorageWin_Open modes 3=deposit/4=withdraw/
// 5=sort, EA 0x5d2c32..0x5d2f44, guarded by Game_IsNearWarehouseSpot 0x5d8390
// and dword_167565C = purchased warehouse capacity, EA 0x5d2c3f) — this is
// the entirety of what the network protocol exchanges for the warehouse in
// these 3 packets.
// ===========================================================================
struct WarehouseGrid {
    static constexpr int kRows = 5;
    static constexpr int kCols = 5;
    static constexpr int kSlotCount = kRows * kCols; // 25

    uint8_t            reservedHeader[32];      // offset 0    (32 bytes, see note above)
    WarehouseItemCell  cells[kRows][kCols];     // offset 32   (900 bytes)
    WarehouseAuxCell   auxCells[kRows][kCols];  // offset 932  (300 bytes)
};

#pragma pack(pop)

static_assert(offsetof(WarehouseGrid, cells) == 32,
              "the reserved header must be 32 bytes (8 dword)");
static_assert(offsetof(WarehouseGrid, auxCells) == 932,
              "the item block must be 900 bytes (32 + 900 = 932)");
static_assert(sizeof(WarehouseGrid) == 1232,
              "WarehouseGrid must be EXACTLY 1232 bytes (network blob size)");

// ===========================================================================
// Pending cell selection (item move not yet confirmed).
// Clean model of the selection cursor used UI-side before sending an action
// (deposit/withdraw/sort) — mirrors the g_Client.pendingMove* pattern in
// ClientRuntime.h, scoped here to the warehouse grid.
// ===========================================================================
struct WarehousePendingMove {
    bool               active = false;
    int32_t            srcRow = -1;
    int32_t            srcCol = -1;
    WarehouseItemCell  snapshot{};   // copy of the cell at selection time
    WarehouseAuxCell   snapshotAux{};

    void Clear() { *this = WarehousePendingMove{}; }
};

// ===========================================================================
// WarehouseState — full client-side state of the warehouse system.
// ===========================================================================
struct WarehouseState {
    // dword_1687428 (0/1): warehouse window armed — EA 0x48cb81/0x48cdc4.
    bool isOpen = false;
    // dword_18229B8 / a1[10] (mode 1..5 of the shared window) — EA 0x48cbd7/0x5d2aa3.
    int32_t mode = 0;
    // dword_1822998: the grid has already been committed at least once — EA 0x48cdd5.
    bool gridCommitted = false;

    WarehouseGrid         grid{};        // dword_18229CC — byte-exact blob (1232 bytes)
    WarehousePendingMove  pendingMove{}; // selection pending confirmation

    // -----------------------------------------------------------------
    // Network decode (Pkt_WarehouseOpen EA 0x48cb45, Pkt_WarehouseUpdate EA 0x48ce77).
    // Raw copy of `size` bytes from `data` into `grid` (identical to
    // `Crt_Memcpy(&dword_18229CC, buf, 1232)`). Fails if size < 1232.
    // -----------------------------------------------------------------
    bool DecodeBlob(const uint8_t* data, size_t size);

    // -----------------------------------------------------------------
    // First free slot (itemId < 1) of the 5x5 BLOB dword_18229CC, scanned
    // row then column.
    //
    // WARNING: DOES NOT CORRESPOND TO ANY BINARY FUNCTION. This is NOT a port
    // of Warehouse_FindFreeSlot 0x54E240 (which the C++ used to wrongly
    // attribute it to — see CORRECTION at the top of the file): 0x54E240
    // scans the 2x28 vault dword_1673F3C, ported separately and faithfully
    // by Vault_FindFreeSlot() below. Do NOT re-attribute this method to
    // 0x54E240: that would be a 3rd wrong attribution.
    //
    // Blob-only helper, kept for DepositIntoFreeSlot. Neither has a caller
    // to date.
    // -----------------------------------------------------------------
    bool FindFreeSlot(int& outRow, int& outCol) const;

    // -----------------------------------------------------------------
    // Search for the cell containing `itemId`, mirroring
    // Warehouse_FindItemCell 0x65ee10 (`for(page) for(j<64)
    // if(id==target) return` loop), adapted to the 5x5 grid.
    // -----------------------------------------------------------------
    bool FindItemCell(int32_t itemId, int& outRow, int& outCol) const;

    // -----------------------------------------------------------------
    // Selects a cell (records a snapshot) ahead of a move — no-op if the
    // cell is empty.
    // -----------------------------------------------------------------
    bool SelectPendingMove(int row, int col);
    void CancelPendingMove() { pendingMove.Clear(); }

    // -----------------------------------------------------------------
    // Applies a warehouse-grid cell into the general inventory
    // (g_Client.inv), mirroring UI_StorageWin_CommitGrid 0x5d2f70
    // (EA 0x5d3072..0x5d335d): no-op if the cell is empty
    // (`itemId < 1`, EA 0x5d3018), otherwise writes to g_InvMain/g_InvGrid_*
    // (via InventoryState::Set) AND to g_InvAux/dword_1674ABC/dword_1674AC0
    // (via the g_Client.Var escape hatch, same addressing as the binary:
    // base + 4*(192*destPage + 3*destSlot)). Then clears the warehouse
    // cell (the binary only does this explicitly in mode 5/sort,
    // EA 0x5d3ef4.., but that is the correct behavior for a "withdrawal").
    // -----------------------------------------------------------------
    bool CommitCellToInventory(int row, int col);

    // Applies the entire grid (5x5 loop, like cases 1/2/4 of
    // UI_StorageWin_CommitGrid EA 0x5d2fc6/0x5d338c/0x5d374e).
    void CommitAllToInventory();

    // -----------------------------------------------------------------
    // Deposits an item into the first free BLOB cell — combines
    // FindFreeSlot + write. Blob-only helper: does NOT port any binary
    // function (the old note "mirrors the pair Warehouse_FindFreeSlot
    // 0x54e240 + slot assignment" was the wrong attribution corrected at
    // the top of the file). The binary's real deposit = Warehouse_TryDepositFromInventory(),
    // on the 2x28 vault. Currently uncalled.
    // -----------------------------------------------------------------
    bool DepositIntoFreeSlot(const WarehouseItemCell& item, const WarehouseAuxCell& aux,
                              int& outRow, int& outCol);

    // -----------------------------------------------------------------
    // Swaps the content of two grid cells (mode 5 = "sort",
    // UI_StorageWin_Open case 5 EA 0x5d2c32).
    // -----------------------------------------------------------------
    bool SwapCells(int rowA, int colA, int rowB, int colB);

    // Fully clears the grid (itemId=count=durability=serial=unknown14=0,
    // destPage=destSlot=gridX=gridY=-1, aux=0), mirroring the init/clear of
    // UI_StorageWin_Open EA 0x5d28f8..0x5d2a16 / UI_StorageWin_CommitGrid
    // case 6/7 EA 0x5d4068..0x5d41fe.
    void ClearGrid();

    // -----------------------------------------------------------------
    // Pure state transitions (no UI/network part) corresponding to the
    // status codes of the 3 packets. `blob1232`, if provided and non-null,
    // must point to exactly 1232 bytes.
    // -----------------------------------------------------------------

    // Pkt_WarehouseOpen 0x48cb00: status = v15 (unk_8156C1, first 4 payload bytes).
    //   0   -> open: arms isOpen, decodes the blob, sets mode=2 (edit-box)    EA 0x48cb7a..0x48cbe1
    //   100 -> grid committed: decodes the blob, CommitAllToInventory        EA 0x48cbf7..0x48cc04
    //   101/102/103/104/105 -> error messages only (no blob)                 EA 0x48cc3e..0x48cce2
    // Returns the StrTable005 string id to display (0 = none), letting the
    // caller (network/UI layer) do `g_Client.msg.System(Str(id))`.
    int32_t ApplyOpenResult(int32_t status, const uint8_t* blob1232 = nullptr);

    // Pkt_WarehouseClose 0x48cd90: mode = v3 (unk_8156C1).
    //   1 -> simple close (disarms isOpen)                                   EA 0x48cdba..0x48cdf2
    //   2 -> commit grid + reopen in deposit mode (mode=3)                   EA 0x48cdc0..0x48ce32
    // Returns the string id to display (0 = none), same contract as ApplyOpenResult.
    int32_t ApplyCloseResult(int32_t mode);

    // Pkt_WarehouseUpdate 0x48ce40: mode = v6 (unk_8156C1).
    //   0/3 -> decodes the blob (grid update)                                EA 0x48ce9b..0x48cf13
    //   1/2 -> error messages only                                           EA 0x48cec3..0x48cf00
    int32_t ApplyUpdateResult(int32_t mode, const uint8_t* blob1232 = nullptr);
};

// Single global instance.
inline WarehouseState g_Warehouse;

// ===========================================================================
// THE REAL VAULT — 2 pages x 28 slots, base dword_1673F3C (gap INV-02).
//
// Container DISTINCT from the 5x5 blob above (see CORRECTION at the top of
// the file). It was not modeled ANYWHERE in ClientSource: a grep for
// 1673F3C|1673F40|1673F44|1673F48|16751B4|1673F38 across all of src/ returned
// only ONE hit, and that was the page flag (GameVarDispatch.cpp:480).
//
// SoA layout proven by Inv_AddItemQuantity case 12 (0x5B11BE..0x5B12A6), whose
// argument pattern is IDENTICAL to the already-named case 6
// (g_QuiverMain/g_QuiverCount/g_QuiverSocket/g_QuiverSerial) => the
// {itemId, count, socket, serial} semantics is PROVEN, not guessed:
//   dword_1673F3C[112*page + 4*slot] = itemId   (a1[7])
//   dword_1673F40[112*page + 4*slot] += count   (a1[8])   <-- ACCUMULATION
//   dword_1673F44[112*page + 4*slot] = socket   (a1[9])
//   dword_1673F48[112*page + 4*slot] = serial   (a1[10])
//   dword_16751B4/B8/BC[84*page + 3*slot] = aux0/aux1/aux2 (a1[13]/[14]/[15])
//
// Stored via the g_Client.Var(originalAddress) escape hatch (ClientRuntime.h:10-12,
// 163) rather than a dedicated field: ClientRuntime.h is not owned by this
// front, and a new parallel store would create TWO sources of truth
// (documented pitfall).
// ===========================================================================
constexpr uint32_t kVaultItemId        = 0x1673F3C; // [112*page + 4*slot]
constexpr uint32_t kVaultCount         = 0x1673F40;
constexpr uint32_t kVaultSocket        = 0x1673F44;
constexpr uint32_t kVaultSerial        = 0x1673F48;
constexpr uint32_t kVaultAux0          = 0x16751B4; // [84*page + 3*slot]
constexpr uint32_t kVaultAux1          = 0x16751B8;
constexpr uint32_t kVaultAux2          = 0x16751BC;
// "Page 2 unlocked" flag: set by Pkt_SetGameVar case 89 (0x4690F8, already
// ported in Net/GameVarDispatch.cpp:480); read by 0x54E265, UI_Refine_Draw 0x5E2E75,
// UI_GameHud_Render 0x67E972/0x67E988.
constexpr uint32_t kVaultPage2Unlocked = 0x1673F34;
// Vault currency — 32-bit DWORD, NOT an int64: Inv_AddItemQuantity case 13
// (0x5B12B5-BE `mov edx, ds:…; add edx, [ecx+1Ch]; mov ds:…, edx`, 3 instructions
// on a single dword; no lo/hi pair).
constexpr uint32_t kVaultCurrency      = 0x1673F38;

// BYTE offsets into the vault arrays (strides proven above).
inline uint32_t VaultItemOff(int32_t page, int32_t slot) {
    return 4u * static_cast<uint32_t>(112 * page + 4 * slot);
}
inline uint32_t VaultAuxOff(int32_t page, int32_t slot) {
    return 4u * static_cast<uint32_t>(84 * page + 3 * slot);
}

// ---------------------------------------------------------------------------
// Vault_FindFreeSlot — FAITHFUL port of Warehouse_FindFreeSlot 0x54E240
// ("[game] find free cell in 2-page x 28-slot warehouse grid"), 1:1 mirror:
//   pages = (dword_1673F34 > 0) ? 2 : 1        0x54E257/0x54E25E/0x54E265/0x54E267
//   for (i < pages) for (j < 28)               0x54E280/0x54E29A
//     if (!dword_1673F3C[112*i + 4*j])         0x54E2AF
//       { *page = i; *slot = j; return 1; }    0x54E2BF/0x54E2C7/0x54E2C9
//   return 0;                                  0x54E2D4
// NB: occupancy test = `!= 0` here (0x54E2AF `cmp …, 0 / jnz`), not to be
// confused with the `>= 1` of UI_Refine_HitTestEmptySlot 0x5E3895 — this
// reproduces the form of 0x54E240, which is the function being ported.
// ---------------------------------------------------------------------------
bool Vault_FindFreeSlot(int& outPage, int& outSlot);

// ---------------------------------------------------------------------------
// Warehouse_TryDepositFromInventory — pure state routine of the RIGHT-CLICK
// DEPOSIT (gap INV-03). Mirrors cGameHud_OnRButtonDown 0x6318E0, jumptable
// 0x6319E4 **case 6**, sequence 0x6319EB..0x631B24 — sole caller of the
// Net_SendVaultReq_250 0x592190 builder (@0x631B05) AND of Warehouse_FindFreeSlot (@0x631A9D).
//
// (invPage, invSlot) = output of the hit-test cGameHud_InvCellAt 0x64F9F0
// (var_404 = page, var_418 = slot). Returns true IFF opcode 250 was emitted.
// The binary itself returns eax=1 on ALL these paths (success as well as
// refusal): that is the UI dispatcher's "handled" flag, owned by the caller,
// not by this routine.
//
// WARNING: WIRING OUT OF SCOPE (not owned by this front): the natural caller
// is UI/InventoryWindow (OnRButtonDown override) + the UI/GameWindows.h:87-88
// adapter, neither of which belongs to W11. The infrastructure already exists
// UI-side (UIManager.h:130 `virtual bool OnRButtonDown`, dispatch
// UIManager.cpp:299, and UIManager.h:126-127 already documents
// cGameHud_OnRButtonDown 0x6318E0 @0x5AD7E4). See wiringTodoForOrchestrator
// in the W11 report.
// ---------------------------------------------------------------------------
bool Warehouse_TryDepositFromInventory(int invPage, int invSlot);

// ===========================================================================
// Game_IsNearWarehouseSpot 0x5d8390: true if the player is within 1000 units
// of the fixed warehouse spot associated with (zoneElement, npcMorphId).
// 4 known locations (zone/element -> npc -> coordinates {x,y,z}):
//   element 0, npc 1   -> ( 4.0,    0.0, -2.0)     EA 0x5d83d9..0x5d840f
//   element 1, npc 6   -> (-189.0,  0.0, 1150.0)   EA 0x5d8435..0x5d846b
//   element 2, npc 11  -> ( 449.0,  1.0,  439.0)   EA 0x5d8491..0x5d84c7
//   element 3, npc 140 -> ( 452.0,  0.0,  487.0)   EA 0x5d84e4..0x5d851a
// Any other combination -> false. Threshold: 3D distance < 1000.0.
// ===========================================================================
bool IsNearWarehouseSpot(int32_t zoneElement, int32_t npcMorphId, float x, float y, float z);

// ===========================================================================
// DOCUMENTED GAP (see file-header comment): the two functions below
// correspond to Net_WarehouseAction_3 0x5cb5d0 / Net_WarehouseAction_4
// 0x5cb6d0. They do NOT operate on WarehouseGrid but on the selection cursor
// of the throwing-weapon "rack" (dword_1674A4C, updated by
// Net_OnWarehouseMoveResult 0x4a61f0 / opcode 0x6c already handled in
// Net/GameHandlers_VendorTrade.cpp via g_Client.Var(0x1674A4C)). Exposed here
// to faithfully cover the requested EAs, as pure predicates (no network send,
// no message — left to the caller).
//   Net_WarehouseAction_3: valid if rackCursor in [0, 10)                 EA 0x5cb646
//   Net_WarehouseAction_4: valid if rackCursor in [10, 20)                EA 0x5cb74c
// Both also require rackCursor != -1 (EA 0x5cb611/0x5cb711) and are invalid
// in an arena zone (Map_IsArenaZone, not modeled here — caller TODO).
// -----------------------------------------------------------------
bool Warehouse_RackActionValid_Deposit(int32_t rackCursor);
bool Warehouse_RackActionValid_Withdraw(int32_t rackCursor);

} // namespace ts2::game

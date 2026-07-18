// Game/WarehouseSystem.cpp — implementation of the WAREHOUSE system.
// See WarehouseSystem.h for the layout documentation and original EAs.
#include "Game/WarehouseSystem.h"
#include "Game/GameDatabase.h"  // game::GetItemInfo — ≡ MobDb_GetEntry(mITEM, id) 0x4C3C00
#include "Net/NetClient.h"      // net::GlobalNetClient() — singleton g_NetClient 0x8156A0
#include "Net/SendPackets.h"    // net::Net_SendVaultReq_250 — ≡ 0x592190
#include "Net/ClientState.h"    // net::g_MorphInProgress / g_GmCmdCooldownLatch / flt_1675B0C
#include <cstring>
#include <cmath>
#include <utility>

namespace ts2::game {

namespace {
    // Original addresses of the per-inventory-cell "aux" arrays
    // (192 dword/page = 64 slots x 3 dword, see EA 0x5d32a5/0x5d3303/0x5d335d).
    constexpr uint32_t kInvAuxBase   = 0x1674AB8; // g_InvAux
    constexpr uint32_t kInvAux1Base  = 0x1674ABC; // dword_1674ABC
    constexpr uint32_t kInvAux2Base  = 0x1674AC0; // dword_1674AC0

    inline uint32_t InvAuxIndexBytes(int32_t page, int32_t slot) {
        // EA 0x5d32a5: `g_InvAux[192 * page + 3 * slot]` -> byte offset = 4*index.
        return 4u * static_cast<uint32_t>(192 * page + 3 * slot);
    }
}

// ===========================================================================
// DecodeBlob — Pkt_WarehouseOpen EA 0x48cb45 (`Crt_Memcpy(v13, &unk_8156C5, 1232)`
// then EA 0x48cbb0/0x48cbf7 `Crt_Memcpy(&dword_18229CC, v13, 1232)`);
// Pkt_WarehouseUpdate EA 0x48ce77/0x48cf02 (same pattern). Raw copy: no byte
// is reinterpreted at the packet level, exactly like the binary.
// ===========================================================================
bool WarehouseState::DecodeBlob(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(WarehouseGrid)) return false;
    std::memcpy(&grid, data, sizeof(WarehouseGrid));
    return true;
}

// ===========================================================================
// FindFreeSlot — first free slot of the 5x5 BLOB (dword_18229CC).
//
// WARNING: DOES NOT PORT ANY BINARY FUNCTION: this is NOT Warehouse_FindFreeSlot
// 0x54E240 (which scans the 2x28 vault dword_1673F3C -> Vault_FindFreeSlot below).
// The previous attribution — and the "IDA naming Warehouse_* is wrong" conclusion
// it caused to be written in the .h — was wrong: see the header's CORRECTION.
// Blob-only helper, currently uncalled (only DepositIntoFreeSlot uses it, and
// that method itself has no caller).
// ===========================================================================
bool WarehouseState::FindFreeSlot(int& outRow, int& outCol) const {
    for (int row = 0; row < WarehouseGrid::kRows; ++row) {
        for (int col = 0; col < WarehouseGrid::kCols; ++col) {
            if (grid.cells[row][col].Empty()) {
                outRow = row;
                outCol = col;
                return true;
            }
        }
    }
    return false; // grid full
}

// ===========================================================================
// FindItemCell — search for an itemId in the 5x5 BLOB (dword_18229CC).
//
// WARNING: DOES NOT PORT Warehouse_FindItemCell 0x65EE10: re-verified against
// the decompilation (2026-07-16), 0x65EE10 scans a THIRD container, still
// different from the other two — `unk_16694C0[2522*a1 + 384*i + 6*j]`, 2 pages x
// 64 slots (0x65EE19/0x65EE31/0x65EE6E), with a 2522-dword stride driven by its
// 1st argument. Neither the 5x5 blob nor the 2x28 vault dword_1673F3C.
// TODO [ancre 0x65EE10]: identify the unk_16694C0 container and its consumer
// chain before porting further. Do NOT attribute this method to 0x65EE10 on
// name alone (exactly the same slip that was fixed for FindFreeSlot).
// Blob-only helper, currently uncalled.
// ===========================================================================
bool WarehouseState::FindItemCell(int32_t itemId, int& outRow, int& outCol) const {
    for (int row = 0; row < WarehouseGrid::kRows; ++row) {
        for (int col = 0; col < WarehouseGrid::kCols; ++col) {
            if (grid.cells[row][col].itemId == itemId) {
                outRow = row;
                outCol = col;
                return true;
            }
        }
    }
    return false; // item not present in the blob
}

bool WarehouseState::SelectPendingMove(int row, int col) {
    if (row < 0 || row >= WarehouseGrid::kRows || col < 0 || col >= WarehouseGrid::kCols)
        return false;
    const WarehouseItemCell& cell = grid.cells[row][col];
    if (cell.Empty()) return false;

    pendingMove.active = true;
    pendingMove.srcRow = row;
    pendingMove.srcCol = col;
    pendingMove.snapshot = cell;
    pendingMove.snapshotAux = grid.auxCells[row][col];
    return true;
}

// ===========================================================================
// CommitCellToInventory — mirrors the loop body shared by cases 1/2/4/5 of
// UI_StorageWin_CommitGrid 0x5d2f70:
//   if (a1[...+23] >= 1) {                              // EA 0x5d3018
//     g_InvMain[384*page+6*slot]              = itemId;  // EA 0x5d3072
//     g_InvGrid_GridX[384*page+6*slot]        = gridX;    // EA 0x5d30d0
//     g_InvGrid_GridY[384*page+6*slot]        = gridY;    // EA 0x5d312e
//     g_InvGrid_Count[384*page+6*slot]        = count;    // EA 0x5d318c
//     g_InvGrid_Durability[384*page+6*slot]   = durability;// EA 0x5d31ea
//     g_InvGrid_InstanceSerial[384*page+6*slot]= serial;   // EA 0x5d3248
//     g_InvAux[192*page+3*slot]               = aux0;      // EA 0x5d32a5
//     dword_1674ABC[192*page+3*slot]          = aux1;      // EA 0x5d3303
//     dword_1674AC0[192*page+3*slot]          = aux2;      // EA 0x5d335d/0x5d3361
//   }
// The binary indexes (page,slot) into g_InvMain as-is (see ClientRuntime.h,
// InventoryState::Set(row,col,...) uses this exact addressing, row=page,
// col=slot). The source warehouse cell is then cleared (behavior of the
// binary's case 5/"sort", EA 0x5d3ef4.., generalized here to any individual
// commit since it is semantically a withdrawal).
// ===========================================================================
bool WarehouseState::CommitCellToInventory(int row, int col) {
    if (row < 0 || row >= WarehouseGrid::kRows || col < 0 || col >= WarehouseGrid::kCols)
        return false;

    WarehouseItemCell& cell = grid.cells[row][col];
    if (cell.Empty()) return false; // EA 0x5d3018: nothing to commit

    const WarehouseAuxCell& aux = grid.auxCells[row][col];

    g_Client.inv.Set(static_cast<uint32_t>(cell.destPage), static_cast<uint32_t>(cell.destSlot),
                      static_cast<uint32_t>(cell.itemId), static_cast<uint32_t>(cell.gridX),
                      static_cast<uint32_t>(cell.gridY), static_cast<uint32_t>(cell.count),
                      static_cast<uint32_t>(cell.durability), static_cast<uint32_t>(cell.instanceSerial));

    const uint32_t auxOff = InvAuxIndexBytes(cell.destPage, cell.destSlot);
    g_Client.Var(kInvAuxBase  + auxOff) = aux.aux0;
    g_Client.Var(kInvAux1Base + auxOff) = aux.aux1;
    g_Client.Var(kInvAux2Base + auxOff) = aux.aux2;

    // Clears the warehouse cell (item removed) — mirrors
    // UI_StorageWin_CommitGrid case 5 EA 0x5d3ef4..0x5d403c.
    cell = WarehouseItemCell{};
    grid.auxCells[row][col] = WarehouseAuxCell{};
    return true;
}

void WarehouseState::CommitAllToInventory() {
    // 5x5 loop, mirroring cases 1/2/4 of UI_StorageWin_CommitGrid
    // EA 0x5d2fc6/0x5d338c/0x5d374e (these do NOT clear the cell; here we
    // choose to always do so — see note on CommitCellToInventory).
    for (int row = 0; row < WarehouseGrid::kRows; ++row)
        for (int col = 0; col < WarehouseGrid::kCols; ++col)
            CommitCellToInventory(row, col);
    gridCommitted = true; // dword_1822998, EA 0x48cdd5 (read by ApplyCloseResult)
}

// DepositIntoFreeSlot — blob helper (FindFreeSlot + write). Like FindFreeSlot,
// it does NOT port any binary function: the old note "mirrors the pair
// Warehouse_FindFreeSlot 0x54e240 + slot assignment" (header) was the same
// wrong attribution. The binary's real deposit path goes through
// Warehouse_TryDepositFromInventory (0x6318E0 case 6) on the 2x28 vault, below.
// Currently uncalled.
bool WarehouseState::DepositIntoFreeSlot(const WarehouseItemCell& item, const WarehouseAuxCell& aux,
                                          int& outRow, int& outCol) {
    int row = 0, col = 0;
    if (!FindFreeSlot(row, col)) return false; // blob full
    grid.cells[row][col] = item;
    grid.auxCells[row][col] = aux;
    outRow = row;
    outCol = col;
    return true;
}

bool WarehouseState::SwapCells(int rowA, int colA, int rowB, int colB) {
    if (rowA < 0 || rowA >= WarehouseGrid::kRows || colA < 0 || colA >= WarehouseGrid::kCols ||
        rowB < 0 || rowB >= WarehouseGrid::kRows || colB < 0 || colB >= WarehouseGrid::kCols)
        return false;
    std::swap(grid.cells[rowA][colA], grid.cells[rowB][colB]);
    std::swap(grid.auxCells[rowA][colA], grid.auxCells[rowB][colB]);
    return true;
}

// ===========================================================================
// ClearGrid — mirrors the init of UI_StorageWin_Open (modes 3/4/5, EA
// 0x5d2d3b..0x5d2ee6): numeric fields -> 0, BUT destPage/destSlot/gridX/
// gridY -> -1 ("no destination" sentinel), see EA 0x5d2e1e..0x5d2e78.
// ===========================================================================
void WarehouseState::ClearGrid() {
    for (int row = 0; row < WarehouseGrid::kRows; ++row) {
        for (int col = 0; col < WarehouseGrid::kCols; ++col) {
            WarehouseItemCell& c = grid.cells[row][col];
            c.itemId = 0;
            c.count = 0;
            c.durability = 0;
            c.instanceSerial = 0;
            c.unknown14 = 0;
            c.destPage = -1;
            c.destSlot = -1;
            c.gridX = -1;
            c.gridY = -1;
            grid.auxCells[row][col] = WarehouseAuxCell{};
        }
    }
    pendingMove.Clear();
}

// ===========================================================================
// ApplyOpenResult — Pkt_WarehouseOpen 0x48cb00, switch(v15) EA 0x48cb7a.
// ===========================================================================
int32_t WarehouseState::ApplyOpenResult(int32_t status, const uint8_t* blob1232) {
    switch (status) {
    case 0: // open — EA 0x48cb81..0x48cbe1
        isOpen = true;                 // dword_1687428 = 1, EA 0x48cb81
        if (blob1232) DecodeBlob(blob1232, sizeof(WarehouseGrid)); // EA 0x48cbb0
        mode = 2;                      // dword_18229B8 = 2 (edit-box), EA 0x48cbd7
        return 0;                      // no system message
    case 100: // grid committed — EA 0x48cbf7..0x48cc04
        if (blob1232) DecodeBlob(blob1232, sizeof(WarehouseGrid)); // EA 0x48cbf7
        CommitAllToInventory();        // UI_StorageWin_CommitGrid, EA 0x48cc04
        return 2031;                   // StrTable005_Get(2031), EA 0x48cc24
    case 101: return 2035; // EA 0x48cc49
    case 102: return 2034; // EA 0x48cc6f
    case 103: return 2032; // EA 0x48cc92
    case 104: return 2015; // EA 0x48ccb4
    case 105: return 2033; // EA 0x48ccd7
    default:  return 0;    // EA 0x48ccec (default: no handling)
    }
}

// ===========================================================================
// ApplyCloseResult — Pkt_WarehouseClose 0x48cd90, if/else if(v3) EA 0x48cdba.
// ===========================================================================
int32_t WarehouseState::ApplyCloseResult(int32_t mode_) {
    if (mode_ == 1) {                  // EA 0x48cdba..0x48cdf2
        isOpen = false;                // dword_1687428 = 0, EA 0x48cdc4
        if (gridCommitted) {           // dword_1822998, EA 0x48cdd5
            mode = 1;                  // dword_18229B8 = 1, EA 0x48cdd7
            // TODO(ui) EA 0x48cde8/0x48cdf2: UI_FocusEditBox(14) + cGameHud_ResetUiState
            // — purely presentational, out of scope for this module.
        }
        return 0;
    }
    if (mode_ == 2) {                  // EA 0x48cdc0..0x48ce32
        CommitAllToInventory();        // UI_StorageWin_CommitGrid, EA 0x48cdfe
        // TODO(ui) EA 0x48ce32: UI_StorageWin_Open(3, ...) — reopens the warehouse in
        // deposit mode; call driven by the network/UI layer, not by this state module.
        mode = 3;
        return 2110;                   // StrTable005_Get(2110), EA 0x48ce14
    }
    return 0;
}

// ===========================================================================
// ApplyUpdateResult — Pkt_WarehouseUpdate 0x48ce40, switch(v6) EA 0x48ce9b.
// ===========================================================================
int32_t WarehouseState::ApplyUpdateResult(int32_t mode_, const uint8_t* blob1232) {
    switch (mode_) {
    case 0:
    case 3: // EA 0x48ce9b..0x48cf13
        if (blob1232) DecodeBlob(blob1232, sizeof(WarehouseGrid)); // EA 0x48cf02
        return 0;
    case 1: return 583; // EA 0x48cece
    case 2: return 584; // EA 0x48cef0
    default: return 0;  // EA 0x48cf16 (default)
    }
}

// ===========================================================================
// IsNearWarehouseSpot — Game_IsNearWarehouseSpot 0x5d8390.
// 3D Euclidean distance (Math_Dist3D 0x53faa0), strict threshold < 1000.0.
// ===========================================================================
bool IsNearWarehouseSpot(int32_t zoneElement, int32_t npcMorphId, float x, float y, float z) {
    float refX, refY, refZ;
    switch (npcMorphId) {
    case 1: // EA 0x5d83bf/0x5d83ca
        if (zoneElement != 0) return false;
        refX = 4.0f; refY = 0.0f; refZ = -2.0f; // EA 0x5d83d9..0x5d83e7
        break;
    case 6: // EA 0x5d8426
        if (zoneElement != 1) return false;
        refX = -189.0f; refY = 0.0f; refZ = 1150.0f; // EA 0x5d8435..0x5d8443
        break;
    case 11: // EA 0x5d8482
        if (zoneElement != 2) return false;
        refX = 449.0f; refY = 1.0f; refZ = 439.0f; // EA 0x5d8491..0x5d849f
        break;
    case 140: // EA 0x5d84d8
        if (zoneElement != 3) return false;
        refX = 452.0f; refY = 0.0f; refZ = 487.0f; // EA 0x5d84e4..0x5d84f2
        break;
    default:
        return false; // EA 0x5d8527
    }
    // Math_Dist3D(a3, &ref) — classic 3D Euclidean distance (x,y,z).
    const float dx = x - refX, dy = y - refY, dz = z - refZ;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    return 1000.0f > dist; // EA 0x5d840f (strict comparison, operand order preserved)
}

// ===========================================================================
// Warehouse_RackActionValid_Deposit/Withdraw — Net_WarehouseAction_3 0x5cb5d0 /
// Net_WarehouseAction_4 0x5cb6d0. See DOCUMENTED GAP in the .h: these
// functions operate on dword_1674A4C (rack cursor), not on WarehouseGrid.
// ===========================================================================
bool Warehouse_RackActionValid_Deposit(int32_t rackCursor) {
    // EA 0x5cb611 (== -1 -> invalid) then EA 0x5cb646 (range [0,10)).
    if (rackCursor == -1) return false;
    return static_cast<uint32_t>(rackCursor) < 10u;
}

bool Warehouse_RackActionValid_Withdraw(int32_t rackCursor) {
    // EA 0x5cb711 (== -1 -> invalid) then EA 0x5cb74c (range [10,20)).
    if (rackCursor == -1) return false;
    return rackCursor >= 10 && rackCursor <= 19;
}

// ===========================================================================
// Vault_FindFreeSlot — FAITHFUL port of Warehouse_FindFreeSlot 0x54E240,
// on the REAL vault dword_1673F3C (2 pages x 28 slots). 1:1 mirror; see the
// header for the 4 derivations that prove the layout.
// ===========================================================================
bool Vault_FindFreeSlot(int& outPage, int& outSlot) {
    // 0x54E257 `mov [ebp+var_4], 1` ; 0x54E25E `cmp ds:dword_1673F34, 0` ;
    // 0x54E265 `jle` -> stays 1; else 0x54E267 `mov [ebp+var_4], 2`.
    // `jle` = SIGNED comparison: the test is indeed `> 0`, not `!= 0`.
    const int pages = (g_Client.VarGet(kVaultPage2Unlocked) > 0) ? 2 : 1;

    for (int page = 0; page < pages; ++page) {           // 0x54E280 `cmp ecx, [ebp+var_4] / jge`
        for (int slot = 0; slot < 28; ++slot) {          // 0x54E29A `cmp [ebp+var_8], 1Ch / jge`
            // 0x54E2AF `cmp ds:dword_1673F3C[eax+ecx], 0 / jnz` -> occupied if != 0.
            if (g_Client.VarGet(kVaultItemId + VaultItemOff(page, slot)) == 0) {
                outPage = page;                          // 0x54E2BF `mov [edx], eax`
                outSlot = slot;                          // 0x54E2C7 `mov [ecx], edx`
                return true;                             // 0x54E2C9 `mov eax, 1`
            }
        }
    }
    return false;                                        // 0x54E2D4 `xor eax, eax`
}

// ===========================================================================
// Warehouse_TryDepositFromInventory — RIGHT-CLICK deposit into the vault.
// Mirrors cGameHud_OnRButtonDown 0x6318E0, jumptable 0x6319E4 case 6
// (sequence 0x6319EB..0x631B24). Guard order STRICTLY matches the binary.
// ===========================================================================
bool Warehouse_TryDepositFromInventory(int invPage, int invSlot) {
    // --- NPC page selector (0x6319AF `mov edx, ds:g_OpenServiceWindow` ;
    // 0x6319C1 `sub eax, 6` ; 0x6319CA/D1 `cmp 45h / ja def_6319E4`): jumptable
    // case 6 is only reached when g_OpenServiceWindow == 6 = "refine/compound"
    // NPC page, set by UI_Refine_Enter 0x5E25C0 @0x5E2605
    // (`mov [ecx+2D0h], 6`, this = dword_1822EC8; 0x1822EC8 + 0x2D0 = 0x1823198).
    // Without this guard, opcode 250 would be emitted in states where the
    // original NEVER emits it.
    if (g_Client.VarGet(0x1823198) != 6) return false;

    // --- Vault window open (0x6319EB `cmp ds:g_WarehouseWindowOpen, 0` ;
    // 0x6319F2 `jnz` -> continue; else 0x6319F4 `jmp def_6319E4`). THIS is,
    // and only here, the "window not open" guard: SILENT refusal, NO message.
    if (g_Client.VarGet(0x1822ED4) == 0) return false;

    // --- Bounds (0x6319F9..0x631A1B): page in [0,1] and slot in [0,0x3F].
    // Grid = 2 rows x 64 columns (row stride 0x600 = 64 x 0x18), NOT 8x8.
    if (invPage < 0 || invPage > 1 || invSlot < 0 || invSlot > 0x3F) return false;

    // --- DB resolution (0x631A27..0x631A49):
    //     record = MobDb_GetEntry(mITEM, g_InvMain[0x600*page + 0x18*slot]).
    const InvCell& cell = g_Client.inv.At(static_cast<uint32_t>(invPage),
                                          static_cast<uint32_t>(invSlot));
    const ItemInfo* info = GetItemInfo(cell.itemId);

    // DEFENSIVE GUARD, NEVER TAKEN — not to be mistaken for a fix.
    // The binary dereferences `[eax]` @0x631A5A WITHOUT a null check, and this is
    // safe BY CONSTRUCTION: (invPage, invSlot) comes from the hit-test cGameHud_InvCellAt
    // 0x64F9F0, which only writes `*a7 = page` / `*a8 = slot` when the cell is
    // occupied (`g_InvMain[384*page + 6*k] >= 1` @0x64FB59) AND
    // MobDb_GetEntry(...) != 0 (@0x64FB95); any other path writes `*a7 = -1`,
    // which the `invPage < 0` bound above rejects. The null-deref at 0x631A5A is
    // therefore UNREACHABLE — same class as InventoryWindow.cpp:556-559.
    if (!info) return false;

    // --- Blacklist (0x631A5A `cmp dword ptr [eax], 345h` ; 0x631A60 `jnz`).
    // ItemInfo.itemId is at +0 (GameDatabase.h:59, 1-based) => `record[0] == 837`
    // means "the item IS item 837": a blacklist of a SINGLE item, not an item
    // "type" nor a "vault not open" state (that guard is above).
    // If EQUAL -> message 0x8F6 = 2294 then exit (0x631A62..0x631A88).
    if (info->itemId == 0x345) {
        g_Client.msg.System(Str(2294));
        return false;
    }

    // --- Free vault slot (0x631A8D..0x631A9D); if 0 -> msg 0x905 = 2309
    // (0x631B36..0x631B52). This is the ONLY caller of Warehouse_FindFreeSlot.
    int whPage = 0, whSlot = 0;
    if (!Vault_FindFreeSlot(whPage, whSlot)) {
        g_Client.msg.System(Str(2309));
        return false;
    }

    // --- Morph/lock guard (0x631AAA `cmp ds:g_MorphInProgress, 1 / jz` ->
    // abort; 0x631AB3 `cmp ds:g_GmCmdCooldownLatch, 0 / jz` -> continue):
    // only emits when morph != 1 AND latch == 0; else SILENT abort (0x631ABC).
    if (net::g_MorphInProgress == 1 || net::g_GmCmdCooldownLatch != 0) return false;

    // g_NetClient 0x8156A0 is a GLOBAL (builders address it directly).
    // This path is only reached in-game (post-handshake, NPC window open): the
    // `if (!nc)` is a DEFENSIVE guard, not dead code — same precedent as
    // UI/InventoryWindow.cpp:553-559.
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return false;

    // --- Emission (0x631AC6..0x631B05). Right-to-left pushes =>
    // args = (invRow, invCol, count, whPage, whSlot, 0, 0); count is re-read from
    // g_InvGrid_Count[0x600*row + 0x18*col] @0x631AEA (= InvCell.flag: the C++
    // name is offset, the POSITION is correct — see InventoryState::Set `e.flag = count`).
    net::Net_SendVaultReq_250(*nc, invPage, invSlot, static_cast<int32_t>(cell.flag),
                              whPage, whSlot, 0, 0);                 // 0x631B05

    // --- Epilogue (0x631B0A..0x631B24).
    g_Client.Var(0x182238C) = 1;                   // g_VaultOpPending = 1     0x631B0A
    net::g_GmCmdCooldownLatch = 1;                 //                          0x631B14
    net::flt_1675B0C = net::g_GameTimeSec;         // fld/fstp                 0x631B1E-24
    return true;
}

} // namespace ts2::game

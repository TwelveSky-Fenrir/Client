// Game/WarehouseSystem.cpp — implémentation du système ENTREPÔT.
// Voir WarehouseSystem.h pour la documentation de layout et les EAs d'origine.
#include "Game/WarehouseSystem.h"
#include <cstring>
#include <cmath>
#include <utility>

namespace ts2::game {

namespace {
    // Adresses d'origine des tableaux « aux » par cellule d'inventaire
    // (192 dword/page = 64 slots × 3 dword, cf. EA 0x5d32a5/0x5d3303/0x5d335d).
    constexpr uint32_t kInvAuxBase   = 0x1674AB8; // g_InvAux
    constexpr uint32_t kInvAux1Base  = 0x1674ABC; // dword_1674ABC
    constexpr uint32_t kInvAux2Base  = 0x1674AC0; // dword_1674AC0

    inline uint32_t InvAuxIndexBytes(int32_t page, int32_t slot) {
        // EA 0x5d32a5 : `g_InvAux[192 * page + 3 * slot]` -> décalage en octets = 4*index.
        return 4u * static_cast<uint32_t>(192 * page + 3 * slot);
    }
}

// ===========================================================================
// DecodeBlob — Pkt_WarehouseOpen EA 0x48cb45 (`Crt_Memcpy(v13, &unk_8156C5, 1232)`
// puis EA 0x48cbb0/0x48cbf7 `Crt_Memcpy(&dword_18229CC, v13, 1232)`) ;
// Pkt_WarehouseUpdate EA 0x48ce77/0x48cf02 (même schéma). Recopie brute :
// aucun octet n'est réinterprété au niveau paquet, exactement comme le binaire.
// ===========================================================================
bool WarehouseState::DecodeBlob(const uint8_t* data, size_t size) {
    if (!data || size < sizeof(WarehouseGrid)) return false;
    std::memcpy(&grid, data, sizeof(WarehouseGrid));
    return true;
}

// ===========================================================================
// FindFreeSlot — à l'image de Warehouse_FindFreeSlot 0x54e240 :
//   for (i = 0; i < pages; ++i)
//     for (j = 0; j < 28; ++j)
//       if (!occupé[i][j]) { *row=i; *col=j; return true; }
// adapté à la grille réelle 5×5 issue du blob (25 cellules, une seule "page").
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
    return false; // EA 0x54e2d6 : aucun slot libre
}

// ===========================================================================
// FindItemCell — à l'image de Warehouse_FindItemCell 0x65ee10 :
//   for (i = 0; i < pages; ++i)
//     for (j = 0; j < 64; ++j)
//       if (id[i][j] == cible) { *row=i; *col=j; return true; }
// adapté à la grille 5×5.
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
    return false; // EA 0x65ee91
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
// CommitCellToInventory — à l'image du corps de boucle commun aux cases
// 1/2/4/5 de UI_StorageWin_CommitGrid 0x5d2f70 :
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
// Le binaire indexe (page,slot) tel quel dans g_InvMain (cf. ClientRuntime.h,
// InventoryState::Set(row,col,...) reprend exactement cet adressage row=page,
// col=slot). La cellule entrepôt source est ensuite vidée (comportement de la
// case 5/« tri » du binaire, EA 0x5d3ef4.., généralisé ici à toute application
// individuelle car il s'agit sémantiquement d'un retrait).
// ===========================================================================
bool WarehouseState::CommitCellToInventory(int row, int col) {
    if (row < 0 || row >= WarehouseGrid::kRows || col < 0 || col >= WarehouseGrid::kCols)
        return false;

    WarehouseItemCell& cell = grid.cells[row][col];
    if (cell.Empty()) return false; // EA 0x5d3018 : rien à committer

    const WarehouseAuxCell& aux = grid.auxCells[row][col];

    g_Client.inv.Set(static_cast<uint32_t>(cell.destPage), static_cast<uint32_t>(cell.destSlot),
                      static_cast<uint32_t>(cell.itemId), static_cast<uint32_t>(cell.gridX),
                      static_cast<uint32_t>(cell.gridY), static_cast<uint32_t>(cell.count),
                      static_cast<uint32_t>(cell.durability), static_cast<uint32_t>(cell.instanceSerial));

    const uint32_t auxOff = InvAuxIndexBytes(cell.destPage, cell.destSlot);
    g_Client.Var(kInvAuxBase  + auxOff) = aux.aux0;
    g_Client.Var(kInvAux1Base + auxOff) = aux.aux1;
    g_Client.Var(kInvAux2Base + auxOff) = aux.aux2;

    // Vide la cellule entrepôt (objet retiré) — miroir de UI_StorageWin_CommitGrid
    // case 5 EA 0x5d3ef4..0x5d403c.
    cell = WarehouseItemCell{};
    grid.auxCells[row][col] = WarehouseAuxCell{};
    return true;
}

void WarehouseState::CommitAllToInventory() {
    // Boucle 5×5, à l'image des cases 1/2/4 de UI_StorageWin_CommitGrid
    // EA 0x5d2fc6/0x5d338c/0x5d374e (celles-ci NE vident PAS la cellule ; on
    // choisit ici de le faire systématiquement — cf. note sur CommitCellToInventory).
    for (int row = 0; row < WarehouseGrid::kRows; ++row)
        for (int col = 0; col < WarehouseGrid::kCols; ++col)
            CommitCellToInventory(row, col);
    gridCommitted = true; // dword_1822998, EA 0x48cdd5 (lu par ApplyCloseResult)
}

bool WarehouseState::DepositIntoFreeSlot(const WarehouseItemCell& item, const WarehouseAuxCell& aux,
                                          int& outRow, int& outCol) {
    int row = 0, col = 0;
    if (!FindFreeSlot(row, col)) return false; // EA 0x54e2d6
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
// ClearGrid — à l'image de l'init de UI_StorageWin_Open (modes 3/4/5, EA
// 0x5d2d3b..0x5d2ee6) : champs numériques -> 0, MAIS destPage/destSlot/gridX/
// gridY -> -1 (sentinelle "aucune destination"), cf. EA 0x5d2e1e..0x5d2e78.
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
    case 0: // ouverture — EA 0x48cb81..0x48cbe1
        isOpen = true;                 // dword_1687428 = 1, EA 0x48cb81
        if (blob1232) DecodeBlob(blob1232, sizeof(WarehouseGrid)); // EA 0x48cbb0
        mode = 2;                      // dword_18229B8 = 2 (edit-box), EA 0x48cbd7
        return 0;                      // pas de message système
    case 100: // grille committée — EA 0x48cbf7..0x48cc04
        if (blob1232) DecodeBlob(blob1232, sizeof(WarehouseGrid)); // EA 0x48cbf7
        CommitAllToInventory();        // UI_StorageWin_CommitGrid, EA 0x48cc04
        return 2031;                   // StrTable005_Get(2031), EA 0x48cc24
    case 101: return 2035; // EA 0x48cc49
    case 102: return 2034; // EA 0x48cc6f
    case 103: return 2032; // EA 0x48cc92
    case 104: return 2015; // EA 0x48ccb4
    case 105: return 2033; // EA 0x48ccd7
    default:  return 0;    // EA 0x48ccec (default : pas de traitement)
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
            // TODO(ui) EA 0x48cde8/0x48cdf2 : UI_FocusEditBox(14) + cGameHud_ResetUiState
            // — purement présentation, hors périmètre de ce module.
        }
        return 0;
    }
    if (mode_ == 2) {                  // EA 0x48cdc0..0x48ce32
        CommitAllToInventory();        // UI_StorageWin_CommitGrid, EA 0x48cdfe
        // TODO(ui) EA 0x48ce32 : UI_StorageWin_Open(3, ...) — réouvre l'entrepôt en
        // mode dépôt ; appel piloté par la couche réseau/UI, pas par ce module d'état.
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
// Distance 3D euclidienne (Math_Dist3D 0x53faa0), seuil strict < 1000.0.
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
    // Math_Dist3D(a3, &ref) — distance euclidienne 3D classique (x,y,z).
    const float dx = x - refX, dy = y - refY, dz = z - refZ;
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    return 1000.0f > dist; // EA 0x5d840f (comparaison stricte, ordre opérandes conservé)
}

// ===========================================================================
// Warehouse_RackActionValid_Deposit/Withdraw — Net_WarehouseAction_3 0x5cb5d0 /
// Net_WarehouseAction_4 0x5cb6d0. Voir ÉCART DOCUMENTÉ dans le .h : ces
// fonctions portent sur dword_1674A4C (curseur râtelier), pas sur WarehouseGrid.
// ===========================================================================
bool Warehouse_RackActionValid_Deposit(int32_t rackCursor) {
    // EA 0x5cb611 (== -1 -> invalide) puis EA 0x5cb646 (plage [0,10)).
    if (rackCursor == -1) return false;
    return static_cast<uint32_t>(rackCursor) < 10u;
}

bool Warehouse_RackActionValid_Withdraw(int32_t rackCursor) {
    // EA 0x5cb711 (== -1 -> invalide) puis EA 0x5cb74c (plage [10,20)).
    if (rackCursor == -1) return false;
    return rackCursor >= 10 && rackCursor <= 19;
}

} // namespace ts2::game

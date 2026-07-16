// Game/WarehouseSystem.cpp — implémentation du système ENTREPÔT.
// Voir WarehouseSystem.h pour la documentation de layout et les EAs d'origine.
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
// FindFreeSlot — premier slot libre du BLOB 5×5 (dword_18229CC).
//
// ⚠ NE PORTE AUCUNE FONCTION DU BINAIRE : ce n'est PAS Warehouse_FindFreeSlot
// 0x54E240 (qui balaie le coffre 2×28 dword_1673F3C -> Vault_FindFreeSlot plus bas).
// Le rattachement précédent — et la conclusion « nommage IDA Warehouse_* erroné »
// qu'il avait fait écrire dans le .h — étaient faux : cf. RECTIFICATION du header.
// Helper propre au blob ; sans appelant à ce jour (seul DepositIntoFreeSlot
// l'utilise, lui-même sans appelant).
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
    return false; // grille pleine
}

// ===========================================================================
// FindItemCell — recherche d'un itemId dans le BLOB 5×5 (dword_18229CC).
//
// ⚠ NE PORTE PAS Warehouse_FindItemCell 0x65EE10 : re-vérifié à la décompilation
// (2026-07-16), 0x65EE10 balaie un TROISIÈME conteneur, encore différent des deux
// autres — `unk_16694C0[2522*a1 + 384*i + 6*j]`, 2 pages × 64 slots (0x65EE19/
// 0x65EE31/0x65EE6E), avec un stride de 2522 dwords piloté par son 1er argument.
// Ni le blob 5×5, ni le coffre 2×28 dword_1673F3C.
// TODO [ancre 0x65EE10] : identifier le conteneur unk_16694C0 et sa chaîne de
// consommation avant tout portage. NE PAS rattacher cette méthode à 0x65EE10 sur
// la seule foi du nom (c'est exactement le glissement corrigé pour FindFreeSlot).
// Helper propre au blob ; sans appelant à ce jour.
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
    return false; // objet absent du blob
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

// DepositIntoFreeSlot — helper de blob (FindFreeSlot + écriture). Comme FindFreeSlot,
// ne porte AUCUNE fonction du binaire : l'ancienne mention « à l'image du couple
// Warehouse_FindFreeSlot 0x54e240 + affectation de slot » (header) était le même faux
// rattachement. Le dépôt réel du binaire passe par Warehouse_TryDepositFromInventory
// (0x6318E0 case 6) sur le coffre 2×28, plus bas. Sans appelant à ce jour.
bool WarehouseState::DepositIntoFreeSlot(const WarehouseItemCell& item, const WarehouseAuxCell& aux,
                                          int& outRow, int& outCol) {
    int row = 0, col = 0;
    if (!FindFreeSlot(row, col)) return false; // blob plein
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

// ===========================================================================
// Vault_FindFreeSlot — portage FIDÈLE de Warehouse_FindFreeSlot 0x54E240,
// sur le VRAI coffre dword_1673F3C (2 pages × 28 slots). Miroir 1:1 ; voir le
// header pour les 4 dérivations qui prouvent le layout.
// ===========================================================================
bool Vault_FindFreeSlot(int& outPage, int& outSlot) {
    // 0x54E257 `mov [ebp+var_4], 1` ; 0x54E25E `cmp ds:dword_1673F34, 0` ;
    // 0x54E265 `jle` -> reste à 1 ; sinon 0x54E267 `mov [ebp+var_4], 2`.
    // `jle` = comparaison SIGNÉE : le test est bien `> 0`, pas `!= 0`.
    const int pages = (g_Client.VarGet(kVaultPage2Unlocked) > 0) ? 2 : 1;

    for (int page = 0; page < pages; ++page) {           // 0x54E280 `cmp ecx, [ebp+var_4] / jge`
        for (int slot = 0; slot < 28; ++slot) {          // 0x54E29A `cmp [ebp+var_8], 1Ch / jge`
            // 0x54E2AF `cmp ds:dword_1673F3C[eax+ecx], 0 / jnz` -> occupé si != 0.
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
// Warehouse_TryDepositFromInventory — dépôt au coffre par CLIC DROIT.
// Miroir de cGameHud_OnRButtonDown 0x6318E0, jumptable 0x6319E4 case 6
// (séquence 0x6319EB..0x631B24). Ordre des gardes STRICTEMENT celui du binaire.
// ===========================================================================
bool Warehouse_TryDepositFromInventory(int invPage, int invSlot) {
    // --- Sélecteur de page PNJ (0x6319AF `mov edx, ds:g_OpenServiceWindow` ;
    // 0x6319C1 `sub eax, 6` ; 0x6319CA/D1 `cmp 45h / ja def_6319E4`) : le case 6
    // de la jumptable n'est atteint que si g_OpenServiceWindow == 6 = page PNJ
    // « refine/compound », posée par UI_Refine_Enter 0x5E25C0 @0x5E2605
    // (`mov [ecx+2D0h], 6`, this = dword_1822EC8 ; 0x1822EC8 + 0x2D0 = 0x1823198).
    // Sans cette garde, on émettrait l'opcode 250 dans des états où l'original
    // ne l'émet JAMAIS.
    if (g_Client.VarGet(0x1823198) != 6) return false;

    // --- Fenêtre coffre ouverte (0x6319EB `cmp ds:g_WarehouseWindowOpen, 0` ;
    // 0x6319F2 `jnz` -> suite ; sinon 0x6319F4 `jmp def_6319E4`). C'est ICI, et
    // seulement ici, la garde « fenêtre non ouverte » : refus MUET, AUCUN message.
    if (g_Client.VarGet(0x1822ED4) == 0) return false;

    // --- Bornes (0x6319F9..0x631A1B) : page ∈ [0,1] et slot ∈ [0,0x3F].
    // Grille = 2 lignes × 64 colonnes (stride ligne 0x600 = 64 × 0x18), PAS 8×8.
    if (invPage < 0 || invPage > 1 || invSlot < 0 || invSlot > 0x3F) return false;

    // --- Résolution DB (0x631A27..0x631A49) :
    //     record = MobDb_GetEntry(mITEM, g_InvMain[0x600*page + 0x18*slot]).
    const InvCell& cell = g_Client.inv.At(static_cast<uint32_t>(invPage),
                                          static_cast<uint32_t>(invSlot));
    const ItemInfo* info = GetItemInfo(cell.itemId);

    // GARDE-FOU DÉFENSIF, JAMAIS PRIS — à ne pas prendre pour un correctif.
    // Le binaire déréférence `[eax]` @0x631A5A SANS test de nullité, et c'est sûr
    // PAR CONSTRUCTION : (invPage, invSlot) vient du hit-test cGameHud_InvCellAt
    // 0x64F9F0, qui n'écrit `*a7 = page` / `*a8 = slot` que si la cellule est
    // occupée (`g_InvMain[384*page + 6*k] >= 1` @0x64FB59) ET que
    // MobDb_GetEntry(...) != 0 (@0x64FB95) ; tout autre chemin y écrit `*a7 = -1`,
    // que la borne `invPage < 0` ci-dessus rejette. Le null-deref de 0x631A5A est
    // donc INATTEIGNABLE — même classe que InventoryWindow.cpp:556-559.
    if (!info) return false;

    // --- Liste noire (0x631A5A `cmp dword ptr [eax], 345h` ; 0x631A60 `jnz`).
    // ItemInfo.itemId est à +0 (GameDatabase.h:59, 1-based) => `record[0] == 837`
    // signifie « l'objet EST l'item 837 » : une liste noire d'UN SEUL item, et non
    // un « type » d'objet ni un « coffre non ouvert » (cette garde-là est plus haut).
    // Si ÉGAL -> message 0x8F6 = 2294 puis sortie (0x631A62..0x631A88).
    if (info->itemId == 0x345) {
        g_Client.msg.System(Str(2294));
        return false;
    }

    // --- Slot libre dans le coffre (0x631A8D..0x631A9D) ; si 0 -> msg 0x905 = 2309
    // (0x631B36..0x631B52). C'est le SEUL appelant de Warehouse_FindFreeSlot.
    int whPage = 0, whSlot = 0;
    if (!Vault_FindFreeSlot(whPage, whSlot)) {
        g_Client.msg.System(Str(2309));
        return false;
    }

    // --- Garde morph/verrou (0x631AAA `cmp ds:g_MorphInProgress, 1 / jz` ->
    // abandon ; 0x631AB3 `cmp ds:g_GmCmdCooldownLatch, 0 / jz` -> on continue) :
    // on n'émet que si morph != 1 ET latch == 0 ; sinon abandon MUET (0x631ABC).
    if (net::g_MorphInProgress == 1 || net::g_GmCmdCooldownLatch != 0) return false;

    // g_NetClient 0x8156A0 est un GLOBAL (les builders l'adressent directement).
    // Ce chemin n'est atteint qu'en jeu (post-handshake, fenêtre PNJ ouverte) : le
    // `if (!nc)` est un garde-fou DÉFENSIF, pas du code mort — même précédent que
    // UI/InventoryWindow.cpp:553-559.
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return false;

    // --- Émission (0x631AC6..0x631B05). Pushes droite->gauche =>
    // args = (invRow, invCol, count, whPage, whSlot, 0, 0) ; count est relu dans
    // g_InvGrid_Count[0x600*row + 0x18*col] @0x631AEA (= InvCell.flag : le nom C++
    // est décalé, la POSITION est la bonne — cf. InventoryState::Set `e.flag = count`).
    net::Net_SendVaultReq_250(*nc, invPage, invSlot, static_cast<int32_t>(cell.flag),
                              whPage, whSlot, 0, 0);                 // 0x631B05

    // --- Épilogue (0x631B0A..0x631B24).
    g_Client.Var(0x182238C) = 1;                   // g_VaultOpPending = 1     0x631B0A
    net::g_GmCmdCooldownLatch = 1;                 //                          0x631B14
    net::flt_1675B0C = net::g_GameTimeSec;         // fld/fstp                 0x631B1E-24
    return true;
}

} // namespace ts2::game

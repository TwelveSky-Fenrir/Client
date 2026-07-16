// Net/GameHandlers_InvCells.cpp — résultats/cellules d'inventaire (achat/vente/
// combine/déplacement/discard/craft/gamble/count).
//
// Domaine « inv_cells » (RE/handler_domains.json). Traduit fidèlement la logique de
// mise à jour d'état des handlers d'origine (RE/net_handler_notes.md) vers le hub
// game::g_Client (grille d'inventaire, monnaie/poids, journal de messages, état de
// déplacement d'objet en attente). Anticheat/son/UI-rendu exact hors périmètre.
//
// Opcodes couverts (20) :
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

// PIÈGE DE NOMMAGE (Game/GameState.h:373, fichier NON possédé — ne pas renommer) :
// les champs d'InvCell portent des noms sémantiquement décalés vs les 6 tableaux SoA
// du binaire. Les OFFSETS sont exacts (stride 0x18 = 6 dwords), donc les octets écrits
// sont justes. RAISONNER EN POSITIONS, jamais en noms :
//   itemId -> g_InvMain          gridX -> g_InvGrid_GridX   gridY -> g_InvGrid_GridY
//   flag   -> g_InvGrid_Count    color -> g_InvGrid_Durability
//   durability -> g_InvGrid_InstanceSerial

// Écrit la cellule SOURCE du « pending move » — index d'origine
// [384*g_PendingMove_SrcRow0 + 6*dword_1822EF0] = (pendingMoveRow, pendingMoveCol) —
// depuis 6 dwords bruts {itemId, gridX, gridY, count, durability, serial}.
inline void WriteSrcCell(uint32_t itemId, uint32_t gridX, uint32_t gridY,
                         uint32_t count, uint32_t durability, uint32_t serial) {
    g_Client.inv.Set(static_cast<uint32_t>(g_Client.pendingMoveRow),
                     static_cast<uint32_t>(g_Client.pendingMoveCol),
                     itemId, gridX, gridY, count, durability, serial);
}

// Applique la case source depuis le snapshot d'objet en attente
// (dword_1822F08.. = g_Client.pendingItem), sans passer par le payload.
//
// Le binaire n'ÉCRASE PAS le Count : il l'INCRÉMENTE (`g_InvGrid_Count[...] +=
// dword_1822F14[0]`) sur les 8 sites à source pending — ancres 0x48b23c (0x1d rc1),
// 0x48b878 / 0x48ba01 (0x1e rc1/rc2), 0x48bfb8 / 0x48c141 (0x1f rc1/rc2),
// 0x4a6d62 / 0x4a6eeb (0x70 rc1/rc2), 0x4adb2a (0xa4 rc1). Les 5 autres champs
// sont en affectation.
inline void WriteSrcCellFromPending() {
    InvCell& e = g_Client.inv.At(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                 static_cast<uint32_t>(g_Client.pendingMoveCol));
    const InvCell& s = g_Client.pendingItem;
    e.itemId     = s.itemId;      // dword_1822F08 -> g_InvMain
    e.gridX      = s.gridX;       // dword_1822F0C -> g_InvGrid_GridX
    e.gridY      = s.gridY;       // dword_1822F10 -> g_InvGrid_GridY
    e.flag      += s.flag;        // dword_1822F14 -> g_InvGrid_Count  (`+=`, ancre 0x4a6d62)
    e.color      = s.color;       // dword_1822F18 -> g_InvGrid_Durability
    e.durability = s.durability;  // dword_1822F1C -> g_InvGrid_InstanceSerial
}

// --- Tableaux « aux » PAR CELLULE (index d'origine [192*row + 3*col]) ------------
// Le modèle InventoryState (Game/ClientRuntime.h:85, non possédé) n'expose que trois
// scalaires GLOBAUX aux0/aux1/aux2 — modèle faux : le binaire a trois TABLEAUX indexés
// par cellule. On passe donc par l'échappatoire Var(adresseOrigine) bénie par le header
// (ClientRuntime.h:10-12), selon le précédent déjà en place dans
// Game/WarehouseSystem.cpp:13-15,124-126.
constexpr uint32_t kInvAuxBase  = 0x1674AB8; // g_InvAux
constexpr uint32_t kInvAux1Base = 0x1674ABC; // dword_1674ABC
constexpr uint32_t kInvAux2Base = 0x1674AC0; // dword_1674AC0

// Décalage en octets de la cellule (row,col) dans les tableaux aux — stride prouvé
// 192 dwords/ligne, 3 dwords/cellule (ancre Net_OnItemSlotRefresh 0x4b2390 @0x4b26be).
inline uint32_t InvAuxOff(int32_t row, int32_t col) {
    return 4u * static_cast<uint32_t>(192 * row + 3 * col);
}

// Écrit les 3 aux de (row,col) depuis le snapshot pending dword_1822F20/F24/F28
// (ancres 0x4a6aa8 / 0x4a6ac9 / 0x4a6aea).
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

// Écrit les 3 aux de (row,col) depuis un snapshot quelconque (base = adresse du 1er
// dword ; ex. dword_1822F44 pour la cellule destination, ancre 0x4a7092).
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

// Vide les 3 aux de (row,col) (ancres 0x4a6bc1 / 0x4a6be1 / 0x4a6c00 pour la cellule
// destination ; 0x48c806 / 0x48c825 / 0x48c844 pour la cellule source du 0x20 case 100).
inline void ClearCellAux(int32_t row, int32_t col) {
    if (row < 0 || col < 0) return;
    const uint32_t off = InvAuxOff(row, col);
    g_Client.Var(kInvAuxBase  + off) = 0;
    g_Client.Var(kInvAux1Base + off) = 0;
    g_Client.Var(kInvAux2Base + off) = 0;
}

// --- Cellule DESTINATION du move ([384*dword_1822EDC + 6*dword_1822EF4]) ---------
// Gardées sur >= 0 comme le précédent ClearExchangeCell (Net/GameHandlers_InvDispatch.cpp:57)
// : après reset ces globals valent -1 (ne pas écraser une vraie case).

// Écrit la cellule (row,col) depuis un snapshot de 6 dwords consécutifs (base = 1er).
// `addCount` : le binaire fait `+=` sur le Count aux ancres 0x4a6fcc (0x70 rc2),
// 0x48bae2 (0x1e rc2) et 0x48c222 (0x1f rc2), mais une AFFECTATION à l'ancre
// 0x48c5f7 (0x20 case 40) — d'où le paramètre.
inline void WriteCellFromSnapshot(int32_t row, int32_t col, uint32_t base, bool addCount) {
    if (row < 0 || col < 0) return;
    InvCell& e = g_Client.inv.At(static_cast<uint32_t>(row), static_cast<uint32_t>(col));
    const uint32_t itemId = static_cast<uint32_t>(g_Client.Var(base));       // ex. dword_1822F2C
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

// Vide les 6 champs de la cellule destination (ancre Net_OnItemCombineResult 0x4a68f0
// @0x4a6b05..0x4a6ba2).
inline void ClearDestCell() {
    const int32_t r = g_Client.Var(0x1822EDC);
    const int32_t c = g_Client.Var(0x1822EF4);
    if (r >= 0 && c >= 0)
        g_Client.inv.ClearCell(static_cast<uint32_t>(r), static_cast<uint32_t>(c));
}

// --- Réinitialisation du « pending move » ---------------------------------------
// Le motif à 4 écritures N'EST PAS universel : audit exhaustif des 10 appelants
// (0x1d/0x1e/0x1f/0x20/0x21/0x69/0x6a/0x70/0xa4/0xad) contre leurs ancres — 3 variantes.

// UNE écriture : g_PendingMove_SrcRow0 = -1 seul. Ancres : Net_OnItemSellResult
// 0x4a5ed0 @0x4a6007 ; Pkt_ItemCombineResult 0x48af50 @0x48b169 (rc0) / @0x48b2e8 (rc1).
inline void ResetPendingRow() {
    g_Client.pendingMoveRow = -1;
}

// DEUX écritures : + dword_1822EDC = -1. Ancres : 0x4a6c0b/@0x4a6c15 (0x70 rc0),
// 0x48b721/@0x48b72b (0x1e rc0), 0x48be61/@0x48be6b (0x1f rc0), 0x4ada48/@0x4ada52
// (0xa4 rc0), 0x48b4b1/@0x48b4bb (0x1d rc10).
inline void ResetPendingRow2() {
    g_Client.pendingMoveRow = -1;
    g_Client.Var(0x1822EDC) = -1;
}

// QUATRE écritures : + dword_1822EE0/EE4 = -1. Ancres : Net_OnItemCellSet 0x4a5d70
// @0x4a5e7b/@0x4a5e85/@0x4a5e8f/@0x4a5e99 (0x69) ; 0x48c8f1.. (0x20) ; 0x48caab..
// (0x21) ; 0x4b2707.. (0xad). SEULS ces quatre opcodes écrivent les 4.
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

    // 0x1d ItemCombineResult — résultat de combinaison/craft d'objet.
    // Pkt_ItemCombineResult 0x48af50 : TROIS branches distinctes (0 / 1 / 10) testées
    // aux ancres @0x48afb4 (if v15) / @0x48afba (== 1) / @0x48afc4 (== 10). Toute autre
    // valeur ne produit RIEN (@0x48b4e6) — pas de branche « else » générique.
    OnPacket<ItemCombineResult>(sys, 0x1d, [](const ItemCombineResult& p) {
        if (p.resultCode == 0) {                     // ancre @0x48afcf
            g_GmCmdCooldownLatch = 0;                // @0x48afcf
            // TODO [ancre 0x48afe2] : garde `if (MobDb_GetEntry(mITEM, v8))` où v8 =
            //   itemId du PAYLOAD (et non du snapshot pending, contrairement à
            //   0x1e/0x1f/0x70/0xa4). Entrée DB nulle -> ni écriture, ni reset, ni
            //   message. MobDb n'est pas exposé au front réseau : non modélisé.
            g_Client.inv.weight -= p.weightDelta;    // @0x48b043
            // Cellule source depuis le PAYLOAD, Count en AFFECTATION (@0x48b0c3).
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.serial); // @0x48b069..0x48b0ff
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol);  // @0x48b120/@0x48b141/@0x48b162
            ResetPendingRow();                       // @0x48b169 — UNE seule écriture
            g_Client.msg.System(Str(716));           // @0x48b183
        } else if (p.resultCode == 1) {              // ancre @0x48b198
            g_GmCmdCooldownLatch = 0;                // @0x48b198
            // Branche SANS garde MobDb et SANS g_InvWeight (contrairement à 0/10).
            WriteSrcCellFromPending();               // @0x48b1bc..0x48b27e (Count `+=` @0x48b23c)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol);  // @0x48b29f/@0x48b2c0/@0x48b2e1
            ResetPendingRow();                       // @0x48b2e8 — UNE seule écriture
            g_Client.msg.System(Str(715));           // @0x48b302
        } else if (p.resultCode == 10) {             // ancre @0x48b317
            g_GmCmdCooldownLatch = 0;                // @0x48b317
            // TODO [ancre 0x48b32a] : garde `if (MobDb_GetEntry(mITEM, v8))` (itemId du
            //   PAYLOAD) — même remarque que la branche 0.
            g_Client.inv.weight -= p.weightDelta;    // @0x48b38b
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.serial); // @0x48b3b1..0x48b447 (Count `=` @0x48b40b)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol);  // @0x48b468/@0x48b489/@0x48b4aa
            ResetPendingRow2();                      // @0x48b4b1/@0x48b4bb — DEUX écritures
            g_Client.msg.System(Str(2697));          // @0x48b4d6
        }
        // resultCode ∉ {0,1,10} : aucun effet (@0x48b4e6).
    });

    // 0x1e ItemSwapResultA — confirme un déplacement/échange d'objet en attente.
    // Pkt_ItemSwapResultA 0x48b520. Le latch ET le reset sont INTERNES à chaque branche :
    // pour resultCode >= 3 le binaire ne fait strictement RIEN (@0x48bc26).
    OnPacket<ItemSwapResultA>(sys, 0x1e, [](const ItemSwapResultA& p) {
        if (p.resultCode == 0) {         // OK simple : cellule depuis le payload — @0x48b584
            g_GmCmdCooldownLatch = 0;    // @0x48b584
            // TODO [ancre 0x48b59a] : garde `if (MobDb_GetEntry(mITEM, dword_1822F08[0]))`
            //   (itemId du snapshot pending) — non modélisée (MobDb hors front réseau).
            g_Client.inv.weight -= p.weightDelta;                    // @0x48b5fb
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]); // @0x48b621..0x48b6b7 (Count `=` @0x48b67b)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x48b6d8/@0x48b6f9/@0x48b71a
            ResetPendingRow2();                                      // @0x48b721/@0x48b72b
            g_Client.msg.System(Str(222));                           // @0x48b746
        } else if (p.resultCode == 1) {  // OK via snapshot pending (add sur count) — @0x48b75b
            g_GmCmdCooldownLatch = 0;    // @0x48b75b
            // TODO [ancre 0x48b770] : garde MobDb_GetEntry(mITEM, dword_1822F08[0]).
            g_Client.inv.weight -= p.weightDelta;                    // @0x48b7d1
            WriteSrcCellFromPending();                               // @0x48b7f8..0x48b8ba (Count `+=` @0x48b878)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x48b8db/@0x48b8fc/@0x48b91d
            ResetPendingRow2();                                      // @0x48b924/@0x48b92e
            g_Client.msg.System(Str(223));                           // @0x48b948
        } else if (p.resultCode == 2) {  // échange source <-> destination — @0x48b95d
            g_GmCmdCooldownLatch = 0;    // @0x48b95d — branche SANS garde MobDb et SANS poids
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

    // 0x1f ItemSwapResultB — variante de déplacement/échange (mêmes 3 branches).
    // Pkt_ItemSwapResultB 0x48bc60 : structure identique à 0x1e, seul le message de la
    // branche 2 change (727 au lieu de 726). Latch/reset INTERNES à chaque branche —
    // resultCode >= 3 : aucun effet (@0x48c366).
    OnPacket<ItemSwapResultB>(sys, 0x1f, [](const ItemSwapResultB& p) {
        if (p.resultCode == 0) {         // commit direct depuis le payload — @0x48bcc4
            g_GmCmdCooldownLatch = 0;    // @0x48bcc4
            // TODO [ancre 0x48bcda] : garde MobDb_GetEntry(mITEM, dword_1822F08[0]).
            g_Client.inv.weight -= p.weightDelta;                    // @0x48bd3b
            WriteSrcCell(p.item[0], p.item[1], p.item[2],
                         p.item[3], p.item[4], p.item[5]);           // @0x48bd61..0x48bdf7 (Count `=` @0x48bdbb)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x48be18/@0x48be39/@0x48be5a
            ResetPendingRow2();                                      // @0x48be61/@0x48be6b
            g_Client.msg.System(Str(222));                           // @0x48be86
        } else if (p.resultCode == 1) {  // commit via snapshot pending — @0x48be9b
            g_GmCmdCooldownLatch = 0;    // @0x48be9b
            // TODO [ancre 0x48beb0] : garde MobDb_GetEntry(mITEM, dword_1822F08[0]).
            g_Client.inv.weight -= p.weightDelta;                    // @0x48bf11
            WriteSrcCellFromPending();                               // @0x48bf38..0x48bffa (Count `+=` @0x48bfb8)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x48c01b/@0x48c03c/@0x48c05d
            ResetPendingRow2();                                      // @0x48c064/@0x48c06e
            g_Client.msg.System(Str(223));                           // @0x48c088
        } else if (p.resultCode == 2) {  // swap : source + destination — @0x48c09d
            g_GmCmdCooldownLatch = 0;    // @0x48c09d — branche SANS garde MobDb et SANS poids
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

    // 0x20 ItemDiscardResult — résultat de jet/suppression d'objet.
    // Pkt_ItemDiscardResult 0x48c3a0. Écrit toujours la cellule source (pending) depuis
    // le payload, ajuste poids/monnaie selon adjustMode, puis dispatch resultCode.
    OnPacket<ItemDiscardResult>(sys, 0x20, [](const ItemDiscardResult& p) {
        g_GmCmdCooldownLatch = 0;   // @0x48c400 — inconditionnel, AVANT tout le reste
        WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.instanceSerial); // @0x48c432..0x48c4cd (Count `=` @0x48c48f)
        if (p.adjustMode == 1)      g_Client.inv.weight   -= p.amount;   // @0x48c4e3
        else if (p.adjustMode == 2) g_Client.inv.currency -= p.amount;   // @0x48c4fa (+ miroir dword_1687254[0] @0x48c508)
        switch (p.resultCode) {     // @0x48c527
            case 0:
                g_Client.msg.System(Str(730));    // @0x48c53f
                break;
            case 1:
                g_Client.msg.System(Str(2310));   // @0x48c565
                break;
            case 40: {
                // Restaure TROIS cellules complètes depuis trois snapshots, vers trois
                // couples (row,col) distincts. Count en AFFECTATION (@0x48c5f7 /
                // @0x48c6bd / @0x48c783) — contrairement aux `+=` des branches d'échange.
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
                // Vide les 3 aux DE LA CELLULE SOURCE [192*row0 + 3*col0] — pas des
                // scalaires globaux (@0x48c806/@0x48c825/@0x48c844).
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
        ResetPendingMove();   // @0x48c8f1/@0x48c8fb/@0x48c905/@0x48c90f — 4 écritures
    });

    // 0x21 ItemResultSimple — résultat simple : ré-applique une cellule au slot pending.
    OnPacket<ItemResultSimple>(sys, 0x21, [](const ItemResultSimple& p) {
        if (p.status == 0) {
            g_GmCmdCooldownLatch = 0;
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.instanceSerial);
            ResetPendingMove();
            g_Client.msg.System(Str(731));
        }
        // status != 0 : aucun effet.
    });

    // 0x69 ItemCellSet — place un objet (6 dwords) dans la case du move pending.
    OnPacket<ItemCellSet>(sys, 0x69, [](const ItemCellSet& p) {
        if (p.resultCode == 0) {
            g_GmCmdCooldownLatch = 0;
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.serial);
            ResetPendingMove();
            g_Client.msg.System(Str(1304));
        }
    });

    // 0x6a ItemSellResult — vente d'objet : crédite le poids, recharge la cellule source.
    // Net_OnItemSellResult 0x4a5ed0. Ni garde MobDb, ni écriture des aux dans ce handler.
    OnPacket<ItemSellResult>(sys, 0x6a, [](const ItemSellResult& p) {
        if (p.resultCode == 0) {
            g_GmCmdCooldownLatch = 0;                   // @0x4a5f2a
            g_Client.inv.weight += p.weightDelta;       // @0x4a5f4d (`+=`, pas `-=`)
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]); // @0x4a5f6a..0x4a6000 (Count `=` @0x4a5fc4)
            // UNE seule écriture de reset (@0x4a6007) : 0x6a ne touche PAS
            // dword_1822EDC/EE0/EE4, contrairement à 0x69 (0x4a5d70 @0x4a5e7b..0x4a5e99).
            ResetPendingRow();                          // @0x4a6007
            g_Client.msg.System(Str(1305));             // @0x4a6022
        } else if (p.resultCode == 1) {
            g_Client.msg.System(Str(1912));             // @0x4a6044
        }
    });

    // 0x6b GambleResult — résultat de loterie/pari (déconnecte à l'échec sec).
    // Net_OnGambleResult 0x4a6060 : les TROIS branches d'affichage passent par le MÊME
    // format aDS_2 "[%d]%s" (0x7a6d88) avec value en préfixe, et transmettent la
    // couleur littérale 2 à Msg_AppendSystemLine (@0x4a6102/@0x4a6158/@0x4a61cf).
    OnPacket<GambleResult>(sys, 0x6b, [&sys](const GambleResult& p) {
        if (p.selector == 1) {           // gain — @0x4a60d6/@0x4a60ec
            g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(1351), 2);
            // (son joué côté audio, hors périmètre)
        } else if (p.selector == 2) {    // fin/échec
            if (static_cast<int32_t>(p.value) <= 0) {   // @0x4a6120
                NetCloseSocket(sys.Client()); // déconnexion sur échec sec — @0x4a6174
                g_Client.Var(0x1676180) = 2;  // g_SceneMgr      = 2 — @0x4a6179
                g_Client.Var(0x1676184) = 0;  // g_SceneSubState = 0 — @0x4a6183
                g_Client.Var(0x1676188) = 0;  // dword_1676188   = 0 — @0x4a618d
                // TODO [ancres 0x4a6179/0x4a6183] : ces trois écritures passent par
                //   l'échappatoire Var() et ne sont RELUES par personne — la vraie
                //   bascule de scène vit dans ts2::g_SceneMgr / ts2::g_SceneSubState
                //   (Scene/SceneManager.h), non possédé par ce front. Le retour à
                //   l'écran de sélection n'a donc pas lieu : câblage à poser.
            } else {                                    // @0x4a612c/@0x4a6142
                g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(1350), 2);
            }
        } else if (p.selector == 3) {    // info — @0x4a61a3/@0x4a61b9
            g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(1394), 2);
        }
    });

    // 0x70 ItemCombineResult2 — combine/sertissage : met à jour 1 ou 2 cases.
    // Net_OnItemCombineResult 0x4a68f0 : TROIS branches aux comportements DISTINCTS
    // (@0x4a6939 `if (v9)` / @0x4a693f `== 1` / @0x4a6949 `== 2`). Les branches 0 et 1
    // ne partagent NI la source des données NI le message.
    OnPacket<ItemCombineResult2>(sys, 0x70, [](const ItemCombineResult2& p) {
        if (p.resultCode == 0) {                        // ancre @0x4a6954
            g_GmCmdCooldownLatch = 0;                   // @0x4a6954
            // TODO [ancre 0x4a696a] : garde `if (MobDb_GetEntry(mITEM, dword_1822F08[0]))`
            //   — entrée DB nulle -> ni écriture, ni reset, ni message. Non modélisée
            //   (MobDb/mITEM ne sont pas exposés au front réseau).
            g_Client.inv.weight -= p.weightDelta;       // @0x4a69cb
            // Cellule source depuis le PAYLOAD (v8 = Crt_Memcpy(0x8156C9,0x18) @0x4a6927),
            // Count en AFFECTATION (@0x4a6a4b) — surtout PAS depuis le snapshot pending.
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.serial); // @0x4a69f1..0x4a6a87
            // ... mais les 3 aux viennent bien du snapshot PENDING, pas du payload.
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol);  // @0x4a6aa8/@0x4a6ac9/@0x4a6aea
            // La cellule DESTINATION est intégralement remise à zéro (9 champs).
            ClearDestCell();                                                    // @0x4a6b05..0x4a6ba2
            ClearCellAux(g_Client.Var(0x1822EDC), g_Client.Var(0x1822EF4));     // @0x4a6bc1/@0x4a6be1/@0x4a6c00
            ResetPendingRow2();                         // @0x4a6c0b/@0x4a6c15
            g_Client.msg.System(Str(222));              // @0x4a6c30 — 222, PAS 1645
        } else if (p.resultCode == 1) {                 // ancre @0x4a6c45
            g_GmCmdCooldownLatch = 0;                   // @0x4a6c45
            // TODO [ancre 0x4a6c5a] : garde MobDb_GetEntry(mITEM, dword_1822F08[0]).
            g_Client.inv.weight -= p.weightDelta;       // @0x4a6cbb
            WriteSrcCellFromPending();                  // @0x4a6ce2..0x4a6da4 (Count `+=` @0x4a6d62)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x4a6dc5/@0x4a6de6/@0x4a6e07
            // AUCUN effacement de la cellule destination dans cette branche.
            ResetPendingRow2();                         // @0x4a6e0e/@0x4a6e18
            g_Client.msg.System(Str(1645));             // @0x4a6e32 — 1645, PAS 222
        } else if (p.resultCode == 2) {                 // ancre @0x4a6e47
            g_GmCmdCooldownLatch = 0;                   // @0x4a6e47
            // Branche SANS garde MobDb et SANS g_InvWeight : le `g_InvWeight -= v7`
            // n'existe qu'aux ancres 0x4a69cb (rc0) et 0x4a6cbb (rc1). Ne rien retrancher.
            const int32_t srcRow = g_Client.pendingMoveRow, srcCol = g_Client.pendingMoveCol;
            const int32_t dstRow = g_Client.Var(0x1822EDC), dstCol = g_Client.Var(0x1822EF4);
            WriteSrcCellFromPending();                              // @0x4a6e6b..0x4a6f2d (Count `+=` @0x4a6eeb)
            WriteCellFromSnapshot(dstRow, dstCol, 0x1822F2C, true); // @0x4a6f4e..0x4a700e (Count `+=` @0x4a6fcc)
            WriteCellAuxFromPending(srcRow, srcCol);                // @0x4a702f/@0x4a7050/@0x4a7071
            WriteCellAuxFromSnapshot(dstRow, dstCol, 0x1822F44);    // @0x4a7092/@0x4a70b3/@0x4a70d4
            ResetPendingRow2();                                     // @0x4a70db/@0x4a70e5
            g_Client.msg.System(Str(1645));                         // @0x4a7100
        }
        // resultCode ∉ {0,1,2} : aucun effet (@0x4a7110).
    });

    // 0x74 CraftResultNotice — message de résultat de craft/production.
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

    // 0x78 EquipSlotUpdate — écrit un slot d'équipement (conteneur skill/équip, stride
    // 42 dwords, cellule 3 dwords) et vide la cellule d'inventaire source.
    OnPacket<EquipSlotUpdate>(sys, 0x78, [](const EquipSlotUpdate& p) {
        const uint32_t idx  = 42u * p.contRow + 3u * p.contCol;   // index dword
        const uint32_t base = idx * 4u;                            // décalage octets
        g_Client.Var(0x16743FC + base) = static_cast<int32_t>(p.itemId);  // g_Container5_ItemId
        g_Client.Var(0x1674400 + base) = static_cast<int32_t>(p.field1);  // dword_1674400
        g_Client.Var(0x1674404 + base) = static_cast<int32_t>(p.field2);  // dword_1674404
        g_Client.inv.ClearCell(p.invRow, p.invCol);
    });

    // 0x7a ItemPlaceResult — résultat de pose d'objet dans une case (coords du payload).
    OnPacket<ItemPlaceResult>(sys, 0x7a, [](const ItemPlaceResult& p) {
        if (p.resultCode == 1) {
            g_Client.inv.Set(p.bagRow, p.slotCol, p.itemId,
                             p.cellIndex % 8, p.cellIndex / 8, 0, p.durability, 0);
            g_Client.msg.System(Str(1911));
        } else if (p.resultCode == 2) {
            g_Client.msg.System(Str(117));
        }
    });

    // 0x8a ItemCellClear — pose une cellule « nue » (objet sans quantité/dura) au slot payload.
    OnPacket<ItemCellClear>(sys, 0x8a, [](const ItemCellClear& p) {
        if (p.resultCode == 0) {
            g_Client.inv.Set(p.invPage, p.invSlot, p.itemId,
                             p.gridPos % 8, p.gridPos / 8, 0, 0, 0);
        }
    });

    // 0x8c ItemCountNotice — notification de quantité (HUD flottant + ligne système).
    // Net_OnItemCountNotice 0x4aab90 : deux `if` SUCCESSIFS avec return (@0x4aabd0 puis
    // @0x4aabd6), et non un ternaire — subop ∉ {0,1} sort MUET (@0x4aac68). Même forme
    // que le handler voisin 0x8e (Net_OnUpgradeCountNotice 0x4aae70), déjà fidèle.
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
        // subop ∉ {0,1} : aucun message (@0x4aac68).
    });

    // 0x8e UpgradeCountNotice — notices de compteur d'amélioration.
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

    // 0x92 ItemMoveResult — résultat de déplacement d'objet (coords du payload).
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

    // 0xa4 ItemBuyResult — achat : déduit le coût, recharge la cellule source (pending).
    // Net_OnItemBuyResult 0x4ad8a0.
    OnPacket<ItemBuyResult>(sys, 0xa4, [](const ItemBuyResult& p) {
        g_GmCmdCooldownLatch = 0;          // @0x4ad8cc
        g_Client.inv.weight -= 10000000;   // @0x4ad8e2 : `g_InvWeight -= (int)&unk_989680`
                                           //   — c'est l'ADRESSE 0x989680 (= 10 000 000)
                                           //   qui sert de constante, pas son contenu.
        // TODO [ancre 0x4ad8f3] : garde `if (MobDb_GetEntry(mITEM, dword_1822F08[0]))`
        //   GLOBALE — elle enveloppe les DEUX branches (@0x4ad8ff). Entrée DB nulle ->
        //   ni écriture, ni reset, ni message (le retrait de poids ci-dessus, lui, a
        //   déjà eu lieu). Non modélisée : MobDb hors front réseau.
        if (p.resultCode == 0) {           // @0x4ad910
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]); // @0x4ad948..0x4ad9de (Count `=` @0x4ad9a2)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x4ad9ff/@0x4ada20/@0x4ada41
            ResetPendingRow2();            // @0x4ada48/@0x4ada52 — DEUX écritures, pas 4
            g_Client.msg.System(Str(2388));// @0x4ada6d
        } else if (p.resultCode == 1) {    // @0x4ad916
            WriteSrcCellFromPending();     // @0x4adaac..0x4adb6c (Count `+=` @0x4adb2a)
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x4adb8d/@0x4adbae/@0x4adbcf
            ResetPendingRow2();            // @0x4adbd6/@0x4adbe0
            g_Client.msg.System(Str(2389));// @0x4adbfb
        }
    });

    // 0xa5 ChargeStackUpdate — ceinture auto-potion (stacks de charge dword_16757D8).
    OnPacket<ChargeStackUpdate>(sys, 0xa5, [](const ChargeStackUpdate& p) {
        g_GmCmdCooldownLatch = 0;
        if (p.mode == 1 && p.flag == 0) {          // recharge payante
            g_Client.Var(0x16757D8 + 4u * p.index) = 10;
            g_Client.inv.currency -= 10;
        } else if (p.mode == 0 && p.flag == 0) {   // consommation / bascule de slot
            g_Client.Var(0x1675800) = static_cast<int32_t>(p.index); // slot actif
            g_Client.Var(0x16757D8 + 4u * p.index) -= 1;
            g_Client.Var(0x1675804) = 60;          // cooldown
            // TODO : si l'item du slot == 878, recalcul des bornes d'attaque ;
            //        nettoyage de l'ancien slot si épuisé (Char_CalcAttackRating*).
        }
    });

    // 0xad ItemSlotRefresh — rafraîchit la cellule source (pending) et déduit l'or.
    // Net_OnItemSlotRefresh 0x4b2390 (switch @0x4b23e9). Pas de garde MobDb ici.
    OnPacket<ItemSlotRefresh>(sys, 0xad, [](const ItemSlotRefresh& p) {
        if (p.resultCode == 0 || p.resultCode == 10) {   // @0x4b25d6 (cases 0 et 10 fusionnées)
            g_GmCmdCooldownLatch = 0;                    // @0x4b25d6
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]); // @0x4b2607..0x4b269d (Count `=` @0x4b2661)
            // Les 3 aux viennent du snapshot pending — AVANT le reset (qui met row0 à -1).
            WriteCellAuxFromPending(g_Client.pendingMoveRow, g_Client.pendingMoveCol); // @0x4b26be/@0x4b26df/@0x4b2700
            ResetPendingMove();                          // @0x4b2707..0x4b2725 — 4 écritures
            g_Client.inv.currency -= p.goldDelta;        // @0x4b2738 (+ miroir dword_1687254[0] @0x4b2746)
            g_Client.msg.System(Str(2563));              // @0x4b275c
        } else if (p.resultCode == 1) {
            g_Client.msg.System(Str(2569));              // @0x4b259b
        } else if (p.resultCode == 2) {
            g_Client.msg.System(Str(2561));              // @0x4b25c1
        }
    });

    // 0xb6 ItemCellReset — vide une case (coords du payload) et mémorise 3 coords.
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

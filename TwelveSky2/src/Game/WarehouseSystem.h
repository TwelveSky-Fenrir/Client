// Game/WarehouseSystem.h — système ENTREPÔT (Warehouse/Storage) du client TS2.
//
// Comble le TODO « blob 1232 o non décodé » laissé dans
// Net/GameHandlers_VendorTrade.cpp (0x22 Pkt_WarehouseOpen, 0x23 Pkt_WarehouseClose,
// 0x24 Pkt_WarehouseUpdate) : le blob réseau de 1232 octets y est recopié TEL QUEL
// (Crt_Memcpy) dans dword_18229CC sans être parsé côté paquet. Ce module fournit le
// layout BYTE-EXACT de ce blob (déduit de UI_StorageWin_CommitGrid/UI_StorageWin_Open,
// qui sont les seules fonctions à en relire/écrire les champs), ainsi que la logique
// de recherche de slot / déplacement associée.
//
// Correspondance fonction ↔ adresse d'origine (voir aussi le .cpp pour le détail
// ligne à ligne) :
//   Pkt_WarehouseOpen           0x48cb00   Pkt_WarehouseClose        0x48cd90
//   Pkt_WarehouseUpdate         0x48ce40   Net_OnWarehouseMoveResult 0x4a61f0
//   Warehouse_FindFreeSlot      0x54e240   Warehouse_FindItemCell    0x65ee10
//   Game_IsNearWarehouseSpot    0x5d8390   UI_Warehouse_SlotAt       0x5f5310
//   UI_Warehouse_Click          0x5f4500   Net_WarehouseAction_3     0x5cb5d0
//   Net_WarehouseAction_4       0x5cb6d0
//   UI_StorageWin_CommitGrid    0x5d2f70   UI_StorageWin_Open        0x5d27a0
//     (ces deux dernières ne sont pas dans la liste imposée mais sont ce qui
//      DÉCODE réellement le blob — indispensables pour en déduire le layout)
//
// ÉCART IMPORTANT constaté en RE (à documenter, pas à cacher) :
//   Net_OnWarehouseMoveResult, Net_WarehouseAction_3/4, UI_Warehouse_SlotAt et
//   UI_Warehouse_Click NE TOUCHENT PAS dword_18229CC (le blob 1232 o). Elles
//   opèrent sur dword_1674A4C (curseur 0..19 d'un « râtelier » g_ThrowWeaponRack
//   9 emplacements, EA 0x4a631a/0x4a6328) et sur une bien plus grosse structure
//   UI (this+802..+3048, pagination 560/40/4 dwords) qui ne correspond à aucune
//   des adresses ci-dessus. Ce module implémente fidèlement CE QUE FONT ces
//   fonctions et documente l'écart à chaque endroit concerné.
//
// ┌─ RECTIFICATION (Passe 4 / W11, gap INV-02) ────────────────────────────────┐
// │ Ce bandeau concluait auparavant que le nommage IDA « Warehouse_* » serait  │
// │ « partiellement erroné ou réutilisé pour un système voisin ». C'EST FAUX,  │
// │ et cette conclusion RE erronée est retirée : elle induirait les passes     │
// │ suivantes en erreur (faux ami du même genre que Crt_StringInit /           │
// │ Crt_FreeBase). Le glissement était côté C++, PAS côté IDA.                 │
// │                                                                            │
// │ Warehouse_FindFreeSlot 0x54E240 ne balaie PAS le blob 5×5 : il balaie un   │
// │ AUTRE conteneur, le VRAI coffre = 2 pages × 28 slots à dword_1673F3C.      │
// │ Commentaire de tête IDA de 0x54E240, mot pour mot :                        │
// │   « [game] find free cell in 2-page x 28-slot warehouse grid »             │
// │ Re-prouvé à l'instruction (2026-07-16), 4 dérivations indépendantes :      │
// │  1. 0x54E240 : `imul eax, 1C0h` (page = 448 o = 112 dw) / `shl ecx, 4`     │
// │     (slot = 16 o = 4 dw) / `cmp [ebp+var_8], 1Ch` (28 slots) ;             │
// │     448/16 = 28 ✔  ; `cmp dword_1673F34, 0 / jle` -> 1 ou 2 pages ;        │
// │     `retn 8` => __stdcall(int* page, int* slot).                           │
// │  2. Inv_AddItemQuantity case 12 (aux) : `imul 150h` (336 o) / `imul 0Ch`   │
// │     (12 o) -> 336/12 = 28 slots/page, dérivation INDÉPENDANTE de 112/4.    │
// │  3. Frontière mémoire : 0x16751B4 + 0x2A0 = 0x1675454 (aux étal) ;         │
// │     0x2A0 = 672 = 56 × 12 = 2 pages × 28 EXACTEMENT.                       │
// │  4. UI_Refine_HitTestEmptySlot 0x5E3750 : `for(i<28)` sur grille 4×7,      │
// │     `dword_1673F3C[112*page + 4*i] >= 1` = occupé.                         │
// │ C'est la famille UI_Refine_* qui pilote CE coffre — c'est ELLE qui est mal │
// │ nommée. Les DEUX conteneurs coexistent : le blob 5×5 (dword_18229CC,       │
// │ WarehouseGrid ci-dessous, modèle correct et prouvé) et le coffre 2×28      │
// │ (dword_1673F3C, modélisé plus bas — il ne l'était pas du tout).            │
// └────────────────────────────────────────────────────────────────────────────┘
//
// Règle du projet : ce fichier n'édite AUCUN header existant ; il inclut
// Game/GameState.h et Game/ClientRuntime.h en lecture seule.
#pragma once
#include <cstdint>
#include <cstddef>
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"

namespace ts2::game {

#pragma pack(push, 1)

// ===========================================================================
// Cellule « objet » de la grille entrepôt — 36 o (9 dword).
// Stride confirmé par les index utilisés dans UI_StorageWin_CommitGrid/Open :
// `a1[45*ligne + 9*colonne + 23 + k]`, k=0..8 → 9 champs consécutifs, colonne
// avance de 9 dword (36 o), ligne de 45 dword (180 o = 5 colonnes × 36 o).
// ===========================================================================
struct WarehouseItemCell {
    int32_t itemId;         // +0  (idx+23) -> g_InvMain[...]               EA 0x5d3072
    int32_t count;          // +4  (idx+24) -> g_InvGrid_Count[...]         EA 0x5d318c
    int32_t durability;     // +8  (idx+25) -> g_InvGrid_Durability[...]    EA 0x5d31ea
    int32_t instanceSerial; // +12 (idx+26) -> g_InvGrid_InstanceSerial[..] EA 0x5d3248
    int32_t unknown14;      // +16 (idx+27) jamais recopié par CommitGrid (seulement
                             //     mis à 0/-1 au clear) — rôle non confirmé.
                             //     TODO EA 0x5d2e00 (Open, clear) / 0x5d3f6b (Commit clear).
    int32_t destPage;       // +20 (idx+28) page destination dans g_InvMain (mult. ×384) EA 0x5d3072
    int32_t destSlot;       // +24 (idx+29) slot destination dans g_InvMain (mult. ×6)   EA 0x5d3072
    int32_t gridX;          // +28 (idx+30) -> g_InvGrid_GridX[...]          EA 0x5d30d0
    int32_t gridY;          // +32 (idx+31) -> g_InvGrid_GridY[...]          EA 0x5d312e

    bool Empty() const { return itemId < 1; } // EA 0x5d3018 : `a1[...+23] >= 1` = occupé
};
static_assert(sizeof(WarehouseItemCell) == 36, "WarehouseItemCell doit faire 36 octets (9 dword)");

// ===========================================================================
// Cellule « aux » (options/sockets liés à l'objet) — 12 o (3 dword).
// Stride : `a1[15*ligne + 3*colonne + 248 + k]`, k=0..2, colonne=3 dword (12 o),
// ligne=15 dword (60 o = 5 colonnes × 12 o).
// ===========================================================================
struct WarehouseAuxCell {
    int32_t aux0; // -> g_InvAux[...]     (base 0x1674AB8, stride 192*page+3*slot) EA 0x5d32a5
    int32_t aux1; // -> dword_1674ABC[...] (base 0x1674ABC, même stride)           EA 0x5d3303
    int32_t aux2; // -> dword_1674AC0[...] (base 0x1674AC0, même stride)           EA 0x5d335d/0x5d3361
};
static_assert(sizeof(WarehouseAuxCell) == 12, "WarehouseAuxCell doit faire 12 octets (3 dword)");

// ===========================================================================
// Grille d'entrepôt — image BYTE-EXACTE du blob réseau de 1232 octets.
//
// Origine : Pkt_WarehouseOpen 0x48cb00 (`Crt_Memcpy(&dword_18229CC, v13, 1232)`
// EA 0x48cbb0 mode ouverture / EA 0x48cbf7 mode 100) et Pkt_WarehouseUpdate
// 0x48ce40 (EA 0x48ce77 réception réseau -> tampon local 1232 o, EA 0x48cf02
// recopie -> dword_18229CC modes 0/3). Le blob est copié TEL QUEL (aucun parsing
// paquet par paquet) par-dessus la fenêtre UI partagée « storage window »
// (dword_1822990, réutilisée aussi par le marchand/l'échange — cf.
// UI_StorageWin_Open 0x5d27a0 modes 1/2/3/4/5), à partir de l'index dword 15
// (dword_18229CC == dword_1822990 + 0x3C == this+15).
//
// Layout déduit en recoupant les indices lus/écrits par UI_StorageWin_CommitGrid
// 0x5d2f70 (recopie grille -> g_InvMain/g_InvGrid_*/g_InvAux/dword_1674ABC/AC0,
// EA 0x5d3018..0x5d3ed2) et UI_StorageWin_Open 0x5d27a0 (init/clear de la même
// grille, EA 0x5d28f8..0x5d2ee6) :
//
//   offset 0x000 (32 o)  : en-tête/réservé. Dans la fenêtre partagée, cette plage
//                          (this+15..+22) recouvre la QUEUE de deux CString
//                          (this+11 et this+16, initialisées par Crt_StringInit
//                          EA 0x5d28dc/0x5d28f0) utilisées par les AUTRES modes de
//                          cette même fenêtre (nom vendeur/partenaire d'échange —
//                          cf. aussi Pkt_PlayerShopOpen qui écrit this+16, EA
//                          0x48da1a/0x48da0d). Aucun champ de cette plage n'est
//                          relu par le chemin entrepôt : traité ici comme réservé.
//   offset 0x020 (900 o) : grille 5×5 de WarehouseItemCell (36 o/cellule),
//                          EA 0x5d3018 (test occupé) .. 0x5d3248 (recopie),
//                          init/clear EA 0x5d2946..0x5d29bd.
//   offset 0x3A4 (300 o) : grille 5×5 de WarehouseAuxCell (12 o/cellule),
//                          EA 0x5d32a5..0x5d3361 (recopie), 0x5d29db..0x5d2a16 (init).
//   total 32 + 900 + 300 = 1232 o exactement (vérifié : la dernière cellule aux
//   (ligne 4, colonne 4) se termine pile à l'octet 1232 — EA 0x5d3e16/0x5d3ed2).
//
// NB sémantique : ces 5×5 = 25 cellules forment la fenêtre de dépôt/retrait/tri
// affichée par le client (UI_StorageWin_Open modes 3=dépôt/4=retrait/5=tri,
// EA 0x5d2c32..0x5d2f44, gardés par Game_IsNearWarehouseSpot 0x5d8390 et
// dword_167565C = capacité d'entrepôt achetée, EA 0x5d2c3f) — c'est la totalité
// de ce que le protocole réseau échange pour l'entrepôt dans ces 3 paquets.
// ===========================================================================
struct WarehouseGrid {
    static constexpr int kRows = 5;
    static constexpr int kCols = 5;
    static constexpr int kSlotCount = kRows * kCols; // 25

    uint8_t            reservedHeader[32];      // offset 0    (32 o, cf. note ci-dessus)
    WarehouseItemCell  cells[kRows][kCols];     // offset 32   (900 o)
    WarehouseAuxCell   auxCells[kRows][kCols];  // offset 932  (300 o)
};

#pragma pack(pop)

static_assert(offsetof(WarehouseGrid, cells) == 32,
              "l'en-tête réservé doit faire 32 octets (8 dword)");
static_assert(offsetof(WarehouseGrid, auxCells) == 932,
              "le bloc objets doit faire 900 octets (32 + 900 = 932)");
static_assert(sizeof(WarehouseGrid) == 1232,
              "WarehouseGrid doit faire EXACTEMENT 1232 octets (taille du blob reseau)");

// ===========================================================================
// Sélection en attente d'une cellule (déplacement objet non encore confirmé).
// Modélisation propre du curseur de sélection utilisé côté UI avant l'envoi
// d'une action (dépôt/retrait/tri) — cf. pattern g_Client.pendingMove* dans
// ClientRuntime.h, ici scopé à la grille entrepôt.
// ===========================================================================
struct WarehousePendingMove {
    bool               active = false;
    int32_t            srcRow = -1;
    int32_t            srcCol = -1;
    WarehouseItemCell  snapshot{};   // copie de la cellule au moment de la sélection
    WarehouseAuxCell   snapshotAux{};

    void Clear() { *this = WarehousePendingMove{}; }
};

// ===========================================================================
// WarehouseState — état complet du système entrepôt côté client.
// ===========================================================================
struct WarehouseState {
    // dword_1687428 (0/1) : fenêtre entrepôt armée — EA 0x48cb81/0x48cdc4.
    bool isOpen = false;
    // dword_18229B8 / a1[10] (mode 1..5 de la fenêtre partagée) — EA 0x48cbd7/0x5d2aa3.
    int32_t mode = 0;
    // dword_1822998 : la grille a déjà été committée au moins une fois — EA 0x48cdd5.
    bool gridCommitted = false;

    WarehouseGrid         grid{};        // dword_18229CC — blob byte-exact (1232 o)
    WarehousePendingMove  pendingMove{}; // sélection en attente de confirmation

    // -----------------------------------------------------------------
    // Décodage réseau (Pkt_WarehouseOpen EA 0x48cb45, Pkt_WarehouseUpdate EA 0x48ce77).
    // Recopie brute des `size` octets de `data` dans `grid` (identique à
    // `Crt_Memcpy(&dword_18229CC, buf, 1232)`). Échoue si size < 1232.
    // -----------------------------------------------------------------
    bool DecodeBlob(const uint8_t* data, size_t size);

    // -----------------------------------------------------------------
    // Premier slot libre (itemId < 1) du BLOB 5×5 dword_18229CC, balayage
    // ligne puis colonne.
    //
    // ⚠ NE CORRESPOND À AUCUNE FONCTION DU BINAIRE. Ce n'est PAS le portage de
    // Warehouse_FindFreeSlot 0x54E240 (que le C++ lui rattachait à tort — cf.
    // RECTIFICATION en tête de fichier) : 0x54E240 balaie le coffre 2×28
    // dword_1673F3C, porté séparément et fidèlement par Vault_FindFreeSlot()
    // ci-dessous. Ne PAS re-rattacher cette méthode à 0x54E240 : ce serait un
    // 3e faux rattachement.
    //
    // Helper propre au blob, conservé pour DepositIntoFreeSlot. Aucun des deux
    // n'a d'appelant à ce jour.
    // -----------------------------------------------------------------
    bool FindFreeSlot(int& outRow, int& outCol) const;

    // -----------------------------------------------------------------
    // Recherche de la cellule contenant `itemId`, à l'image de
    // Warehouse_FindItemCell 0x65ee10 (boucle `for(page) for(j<64)
    // if(id==cible) return`), adaptée à la grille 5×5.
    // -----------------------------------------------------------------
    bool FindItemCell(int32_t itemId, int& outRow, int& outCol) const;

    // -----------------------------------------------------------------
    // Sélectionne une cellule (mémorise un instantané) en vue d'un
    // déplacement — no-op si la cellule est vide.
    // -----------------------------------------------------------------
    bool SelectPendingMove(int row, int col);
    void CancelPendingMove() { pendingMove.Clear(); }

    // -----------------------------------------------------------------
    // Applique une cellule de la grille entrepôt vers l'inventaire général
    // (g_Client.inv), à l'image de UI_StorageWin_CommitGrid 0x5d2f70
    // (EA 0x5d3072..0x5d335d) : ne fait rien si la cellule est vide
    // (`itemId < 1`, EA 0x5d3018), sinon écrit dans g_InvMain/g_InvGrid_*
    // (via InventoryState::Set) ET dans g_InvAux/dword_1674ABC/dword_1674AC0
    // (via l'échappatoire g_Client.Var, adressage identique au binaire :
    // base + 4*(192*destPage + 3*destSlot)). Vide ensuite la cellule
    // entrepôt (le binaire ne le fait explicitement qu'en mode 5/tri,
    // EA 0x5d3ef4.., mais c'est le comportement correct pour un « retrait »).
    // -----------------------------------------------------------------
    bool CommitCellToInventory(int row, int col);

    // Applique la grille entière (boucle 5×5, comme les cases 1/2/4 de
    // UI_StorageWin_CommitGrid EA 0x5d2fc6/0x5d338c/0x5d374e).
    void CommitAllToInventory();

    // -----------------------------------------------------------------
    // Dépose un objet dans la première cellule libre du BLOB — combine
    // FindFreeSlot + écriture. Helper propre au blob : ne porte AUCUNE
    // fonction du binaire (l'ancienne mention « à l'image du couple
    // Warehouse_FindFreeSlot 0x54e240 + affectation de slot » relevait du
    // faux rattachement corrigé en tête de fichier). Le dépôt réel du
    // binaire = Warehouse_TryDepositFromInventory(), sur le coffre 2×28.
    // Sans appelant à ce jour.
    // -----------------------------------------------------------------
    bool DepositIntoFreeSlot(const WarehouseItemCell& item, const WarehouseAuxCell& aux,
                              int& outRow, int& outCol);

    // -----------------------------------------------------------------
    // Échange le contenu de deux cellules de la grille (mode 5 = « tri »,
    // UI_StorageWin_Open case 5 EA 0x5d2c32).
    // -----------------------------------------------------------------
    bool SwapCells(int rowA, int colA, int rowB, int colB);

    // Vide entièrement la grille (itemId=count=durability=serial=unknown14=0,
    // destPage=destSlot=gridX=gridY=-1, aux=0), à l'image de l'init/clear de
    // UI_StorageWin_Open EA 0x5d28f8..0x5d2a16 / UI_StorageWin_CommitGrid
    // case 6/7 EA 0x5d4068..0x5d41fe.
    void ClearGrid();

    // -----------------------------------------------------------------
    // Transitions d'état pures (sans partie UI/réseau) correspondant aux
    // status codes des 3 paquets. `blob1232`, s'il est fourni et non nul,
    // doit pointer sur exactement 1232 octets.
    // -----------------------------------------------------------------

    // Pkt_WarehouseOpen 0x48cb00 : status = v15 (unk_8156C1, 4 premiers octets payload).
    //   0   -> ouverture : arme isOpen, décode le blob, marque mode=2 (edit-box)   EA 0x48cb7a..0x48cbe1
    //   100 -> grille committée : décode le blob, CommitAllToInventory            EA 0x48cbf7..0x48cc04
    //   101/102/103/104/105 -> messages d'erreur uniquement (pas de blob)         EA 0x48cc3e..0x48cce2
    // Retourne l'id de chaîne StrTable005 à afficher (0 = aucun message), pour
    // laisser l'appelant (couche réseau/UI) faire `g_Client.msg.System(Str(id))`.
    int32_t ApplyOpenResult(int32_t status, const uint8_t* blob1232 = nullptr);

    // Pkt_WarehouseClose 0x48cd90 : mode = v3 (unk_8156C1).
    //   1 -> fermeture simple (désarme isOpen)                                    EA 0x48cdba..0x48cdf2
    //   2 -> commit grille + réouverture en dépôt (mode=3)                        EA 0x48cdc0..0x48ce32
    // Retourne l'id de chaîne à afficher (0 = aucun), comme ApplyOpenResult.
    int32_t ApplyCloseResult(int32_t mode);

    // Pkt_WarehouseUpdate 0x48ce40 : mode = v6 (unk_8156C1).
    //   0/3 -> décode le blob (mise à jour grille)                                EA 0x48ce9b..0x48cf13
    //   1/2 -> messages d'erreur uniquement                                       EA 0x48cec3..0x48cf00
    int32_t ApplyUpdateResult(int32_t mode, const uint8_t* blob1232 = nullptr);
};

// Instance globale unique.
inline WarehouseState g_Warehouse;

// ===========================================================================
// LE VRAI COFFRE — 2 pages × 28 slots, base dword_1673F3C (gap INV-02).
//
// Conteneur DISTINCT du blob 5×5 ci-dessus (cf. RECTIFICATION en tête de
// fichier). Il n'était modélisé NULLE PART dans ClientSource : un grep de
// 1673F3C|1673F40|1673F44|1673F48|16751B4|1673F38 sur tout src/ ne renvoyait
// qu'UNE occurrence, et c'était le flag de page (GameVarDispatch.cpp:480).
//
// Layout SoA prouvé par Inv_AddItemQuantity case 12 (0x5B11BE..0x5B12A6), dont
// le motif d'arguments est IDENTIQUE à celui de la case 6 déjà NOMMÉE
// (g_QuiverMain/g_QuiverCount/g_QuiverSocket/g_QuiverSerial) => la sémantique
// {itemId, count, socket, serial} est PROUVÉE, pas devinée :
//   dword_1673F3C[112*page + 4*slot] = itemId   (a1[7])
//   dword_1673F40[112*page + 4*slot] += count   (a1[8])   <-- ACCUMULATION
//   dword_1673F44[112*page + 4*slot] = socket   (a1[9])
//   dword_1673F48[112*page + 4*slot] = serial   (a1[10])
//   dword_16751B4/B8/BC[84*page + 3*slot] = aux0/aux1/aux2 (a1[13]/[14]/[15])
//
// On stocke via l'échappatoire g_Client.Var(adresseOrigine) (ClientRuntime.h:10-12,
// 163) plutôt qu'en champ propre : ClientRuntime.h n'appartient pas à ce front, et
// un nouveau store parallèle créerait DEUX sources de vérité (piège documenté).
// ===========================================================================
constexpr uint32_t kVaultItemId        = 0x1673F3C; // [112*page + 4*slot]
constexpr uint32_t kVaultCount         = 0x1673F40;
constexpr uint32_t kVaultSocket        = 0x1673F44;
constexpr uint32_t kVaultSerial        = 0x1673F48;
constexpr uint32_t kVaultAux0          = 0x16751B4; // [84*page + 3*slot]
constexpr uint32_t kVaultAux1          = 0x16751B8;
constexpr uint32_t kVaultAux2          = 0x16751BC;
// Flag « page 2 débloquée » : posé par Pkt_SetGameVar case 89 (0x4690F8, déjà
// porté Net/GameVarDispatch.cpp:480) ; lu par 0x54E265, UI_Refine_Draw 0x5E2E75,
// UI_GameHud_Render 0x67E972/0x67E988.
constexpr uint32_t kVaultPage2Unlocked = 0x1673F34;
// Devise du coffre — DWORD 32 bits, PAS un int64 : Inv_AddItemQuantity case 13
// (0x5B12B5-BE `mov edx, ds:…; add edx, [ecx+1Ch]; mov ds:…, edx`, 3 instructions
// sur un seul dword ; aucun couple lo/hi).
constexpr uint32_t kVaultCurrency      = 0x1673F38;

// Décalages en OCTETS dans les tableaux du coffre (strides prouvés ci-dessus).
inline uint32_t VaultItemOff(int32_t page, int32_t slot) {
    return 4u * static_cast<uint32_t>(112 * page + 4 * slot);
}
inline uint32_t VaultAuxOff(int32_t page, int32_t slot) {
    return 4u * static_cast<uint32_t>(84 * page + 3 * slot);
}

// ---------------------------------------------------------------------------
// Vault_FindFreeSlot — portage FIDÈLE de Warehouse_FindFreeSlot 0x54E240
// (« [game] find free cell in 2-page x 28-slot warehouse grid »), miroir 1:1 :
//   pages = (dword_1673F34 > 0) ? 2 : 1        0x54E257/0x54E25E/0x54E265/0x54E267
//   for (i < pages) for (j < 28)               0x54E280/0x54E29A
//     if (!dword_1673F3C[112*i + 4*j])         0x54E2AF
//       { *page = i; *slot = j; return 1; }    0x54E2BF/0x54E2C7/0x54E2C9
//   return 0;                                  0x54E2D4
// NB : test d'occupation = `!= 0` ici (0x54E2AF `cmp …, 0 / jnz`), à ne pas
// confondre avec le `>= 1` de UI_Refine_HitTestEmptySlot 0x5E3895 — on reproduit
// la forme de 0x54E240, qui est la fonction portée.
// ---------------------------------------------------------------------------
bool Vault_FindFreeSlot(int& outPage, int& outSlot);

// ---------------------------------------------------------------------------
// Warehouse_TryDepositFromInventory — routine d'état pure du DÉPÔT PAR CLIC DROIT
// (gap INV-03). Miroir de cGameHud_OnRButtonDown 0x6318E0, jumptable 0x6319E4
// **case 6**, séquence 0x6319EB..0x631B24 — seul appelant du builder
// Net_SendVaultReq_250 0x592190 (@0x631B05) ET de Warehouse_FindFreeSlot (@0x631A9D).
//
// (invPage, invSlot) = sortie du hit-test cGameHud_InvCellAt 0x64F9F0
// (var_404 = page, var_418 = slot). Retourne true SSI l'opcode 250 a été émis.
// Le binaire, lui, renvoie eax=1 sur TOUS ces chemins (succès comme refus) : c'est
// le « handled » du dispatcher UI, à la charge de l'appelant, pas de cette routine.
//
// ⚠ CÂBLAGE HORS PÉRIMÈTRE (non tenu par ce front) : l'appelant naturel est
// UI/InventoryWindow (override OnRButtonDown) + l'adaptateur UI/GameWindows.h:87-88,
// aucun des deux n'appartenant à W11. L'infrastructure existe déjà côté UI
// (UIManager.h:130 `virtual bool OnRButtonDown`, dispatch UIManager.cpp:299, et
// UIManager.h:126-127 documente déjà cGameHud_OnRButtonDown 0x6318E0 @0x5AD7E4).
// Voir wiringTodoForOrchestrator du rapport W11.
// ---------------------------------------------------------------------------
bool Warehouse_TryDepositFromInventory(int invPage, int invSlot);

// ===========================================================================
// Game_IsNearWarehouseSpot 0x5d8390 : vrai si le joueur est à moins de 1000
// unités du point d'entrepôt fixe associé à (zoneElement, npcMorphId).
// 4 emplacements connus (zone/élément -> npc -> coordonnées {x,y,z}) :
//   élément 0, npc 1   -> ( 4.0,    0.0, -2.0)     EA 0x5d83d9..0x5d840f
//   élément 1, npc 6   -> (-189.0,  0.0, 1150.0)   EA 0x5d8435..0x5d846b
//   élément 2, npc 11  -> ( 449.0,  1.0,  439.0)   EA 0x5d8491..0x5d84c7
//   élément 3, npc 140 -> ( 452.0,  0.0,  487.0)   EA 0x5d84e4..0x5d851a
// Toute autre combinaison -> false. Seuil : distance 3D < 1000.0.
// ===========================================================================
bool IsNearWarehouseSpot(int32_t zoneElement, int32_t npcMorphId, float x, float y, float z);

// ===========================================================================
// ÉCART DOCUMENTÉ (cf. commentaire de tête de fichier) : les deux fonctions
// suivantes correspondent à Net_WarehouseAction_3 0x5cb5d0 / Net_WarehouseAction_4
// 0x5cb6d0. Elles NE portent PAS sur WarehouseGrid mais sur le curseur de
// sélection du « râtelier » d'armes de jet (dword_1674A4C, mis à jour par
// Net_OnWarehouseMoveResult 0x4a61f0 / opcode 0x6c déjà géré dans
// Net/GameHandlers_VendorTrade.cpp via g_Client.Var(0x1674A4C)). Exposées ici
// pour couvrir fidèlement les EAs demandées, sous forme de prédicats purs
// (pas d'envoi réseau, pas de message — laissés à l'appelant).
//   Net_WarehouseAction_3 : valide si rackCursor ∈ [0, 10)                 EA 0x5cb646
//   Net_WarehouseAction_4 : valide si rackCursor ∈ [10, 20)                EA 0x5cb74c
// Les deux exigent aussi rackCursor != -1 (EA 0x5cb611/0x5cb711) et sont
// invalides en zone d'arène (Map_IsArenaZone, non modélisé ici — TODO appelant).
// -----------------------------------------------------------------
bool Warehouse_RackActionValid_Deposit(int32_t rackCursor);
bool Warehouse_RackActionValid_Withdraw(int32_t rackCursor);

} // namespace ts2::game

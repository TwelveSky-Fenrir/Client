// Net/GameHandlers_VendorTrade.cpp — routage des paquets MARCHAND / ÉCHANGE /
// ENTREPÔT / BOUTIQUE-JOUEUR / RÉPARATION vers l'état runtime (game::g_Client).
//
// Domaine « vendor_trade » (RE/handler_domains.json). Traduction fidèle de la
// sémantique d'origine décrite dans RE/net_handler_notes.md : chaque handler ne
// fait QUE mettre à jour l'état visible (inventaire, or/poids, journal de
// messages, flags UI) ; les envois automatiques (Net_SendOp*) et le rendu UI
// exact sont laissés en `// TODO(send)` / `// TODO(ui)`. Les globals scalaires de
// la longue traîne (dword_XXXX) passent par l'échappatoire g_Client.Var(addr).
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
#include "Game/WarehouseSystem.h" // game::g_Warehouse (grille byte-exacte dword_18229CC, 1232 o)
#include "Game/GameDatabase.h"    // game::GetItemInfo / ItemInfo::field212 (≡ MobDb_GetEntry 0x4C3C00)
#include "Game/GameState.h"       // game::g_World.self.elementSecondary (g_LocalElementSecondary 0x1673198)
#include <cstdint>
#include <cstring>
#include <string>

namespace ts2::net {

// Adresses d'origine (globals non modélisés en champ propre, via g_Client.Var).
namespace {
constexpr uint32_t kTradePartnerIdLo = 0x168741C; // g_TradePartnerIdLo (partner[0])
constexpr uint32_t kTradePartnerVal1 = 0x1687420; // dword_1687420      (partner[1])
// dword_1687424 = 3e mot d'identité du partenaire, ENVOYÉ PAR LE SERVEUR (Pkt_TradeRequestPrompt
// 0x48fd20 : `dword_1687424[0] = v7[2]` EA 0x48fda0). Ce n'est PAS un « accord local » : c'est la
// valeur que Pkt_TradeActionResult 0x48FEA0 compare au code d'action reçu (EA 0x48fefe
// `cmp dword_1687424, v7`, v7 = code d'action).
constexpr uint32_t kTradePartnerVal2 = 0x1687424;
constexpr uint32_t kTradeState       = 0x1675B24; // g_PendingOrderKind (état d'échange)
constexpr uint32_t kTradeExtra       = 0x1675D84; // dword_1675D84
constexpr uint32_t kOpenServiceWindow= 0x1823198; // g_OpenServiceWindow
constexpr uint32_t kWarehouseOpen    = 0x1822ED4; // g_WarehouseWindowOpen

// Râtelier d'armes de jet : DEUX tableaux PARALLÈLES de 10 dwords, et non un tableau plat
// de 20. Prouvé par l'arithmétique d'adresses de Net_OnWarehouseMoveResult 0x4A61F0 case 5 :
//   g_ThrowWeaponRack = 0x16749FC ; dword_1674A00 = base+4   -> rack[i+1] ;
//   dword_1674A20     = base+0x24 -> rack[9] (dernier)       ; le 2e tableau démarre à
//   dword_1674A24     = base+0x28 = index 10                 ; dword_1674A28 = aux[i+1] ;
//   dword_1674A48     = 0x1674A24+0x24 -> aux[9]             ; le curseur dword_1674A4C suit
// immédiatement les DEUX tableaux (ce n'est donc PAS une borne du râtelier).
constexpr uint32_t kThrowWeaponRack       = 0x16749FC; // g_ThrowWeaponRack[10] (cf. Net/ItemActionDispatch.cpp)
constexpr uint32_t kThrowWeaponRackAux    = 0x1674A24; // dword_1674A24[10] — 2e tableau parallèle
constexpr uint32_t kThrowWeaponRackCursor = 0x1674A4C; // dword_1674A4C — curseur de sélection
constexpr int      kThrowWeaponRackCapacity = 10;

constexpr uint32_t kShopGridItemBase = 0x18229EC; // dword_18229EC.. — grille objets 5x5 (9 dword/cellule)
constexpr uint32_t kShopGridPriceBase= 0x1822D70; // dword_1822D70.. — grille prix/qté 5x5 (3 dword/cellule)
constexpr uint32_t kInvAuxBase       = 0x1674AB8; // g_InvAux[] — 3 dwords PAR CELLULE d'inventaire
constexpr uint32_t kVendorGrid       = 0x1837E70; // g_VendorGrid — 3 onglets x 400 dwords (0x12C0 o)
constexpr uint32_t kVendorTabRows    = 0x1823B50; // dword_1823B50[4] — nb de lignes actives par onglet
constexpr uint32_t kShopItemTable    = 0x1823B60; // g_ShopItemTable (8960 o)

// g_InvAux 0x1674AB8 : tableau PARALLÈLE indexé PAR CELLULE — 3 dwords contigus
// (0x1674AB8 / 0x1674ABC / 0x1674AC0) à l'index dword `192*row + 3*col`, soit l'offset
// octet `4*(192*row + 3*col)`. Même foulée que celle déjà documentée dans
// Game/WarehouseSystem.h:78-80 (base 0x1674AB8, stride 192*page+3*slot, EA 0x5d32a5).
// Ancres : Pkt_PlayerShopBuyResult 0x48e30f / 0x48e328 / 0x48e341 ; Pkt_TradeResult
// 0x48d2fa / 0x48d313 / 0x48d32c (case 0) et 0x48d775 / 0x48d78f / 0x48d7a9 (case 6).
uint32_t InvAuxAddr(uint32_t row, uint32_t col, uint32_t which) {
    return kInvAuxBase + 4u * (192u * row + 3u * col) + 4u * which;
}

// Pkt_PlayerShopOpen (0x87) / Pkt_PlayerShopBuyResult (0x88) partagent le MÊME layout de
// blob 824 o : [0..16) nom vendeur, [16..516) grille 5x5 d'objets (20 o/cellule en SOURCE),
// [516..816) grille 5x5 prix/qté (12 o/cellule), puis 2 dwords à +816/+820.
//
// ATTENTION — la grille objets de DESTINATION est ÉPARSE, pas une recopie plate :
//   dword_18229EC[45*i + 9*j + d] = blob[100*i + 16 + 20*j + 4*d]   (d = 0..4)
//   dword_1822A00[45*i + 9*j + e] = -1                              (e = 0..3, padding)
// soit 9 dwords (36 o) par cellule en destination pour 5 dwords (20 o) en source, et
// 180 o par ligne en destination pour 100 o en source. Span = [0x18229EC, 0x1822D70) =
// 900 o, jointif avec la base prix 0x1822D70 — ce qui verrouille le layout.
// Ancres : Pkt_PlayerShopOpen 0x48dabc-0x48dc1c ; Pkt_PlayerShopBuyResult 0x48e051-0x48e1b1
// (`imul 0B4h` = 180 o/ligne dst, `imul 24h` = 36 o/cellule dst, `imul 64h`/`imul 14h` =
// 100/20 o src) et 0x48e151-0x48e1b1 (les 4 dwords de padding à -1).
//
// La grille prix, elle, a des foulées source et destination IDENTIQUES (60 o/ligne,
// 12 o/cellule : `dword_1822D70[15*i + 3*j] = blob[60*i + 516 + 12*j]` @0x48e1ea) : la
// plage est donc contiguë et la boucle plate de 75 dwords lui est fidèle — ne pas la
// « corriger ».
void DecodePlayerShopGrid(const uint8_t* blob824) {
    for (int i = 0; i < 5; ++i) {              // ligne (0x48dfd0 / 0x48da3b)
        for (int j = 0; j < 5; ++j) {          // colonne (0x48dff8 / 0x48da63)
            const uint32_t dst = kShopGridItemBase + 4u * (45u * i + 9u * j);
            const int      src = 100 * i + 16 + 20 * j;
            for (int d = 0; d < 5; ++d) {      // 5 dwords copiés (0x48e051..0x48e135)
                uint32_t v; std::memcpy(&v, blob824 + src + 4 * d, 4);
                game::g_Client.Var(dst + 4u * d) = static_cast<int32_t>(v);
            }
            for (int e = 5; e < 9; ++e)        // 4 dwords de padding à -1 (0x48e151..0x48e1b1)
                game::g_Client.Var(dst + 4u * e) = -1;
        }
    }
    // Grille prix/qté : plage contiguë de 75 dwords (0x48e1ea / 0x48e220 / 0x48e256).
    for (int i = 0; i < 75; ++i) {
        uint32_t v; std::memcpy(&v, blob824 + 516 + 4 * i, 4);
        game::g_Client.Var(kShopGridPriceBase + 4u * i) = static_cast<int32_t>(v);
    }
}

// Nom du vendeur : `Crt_Vsnprintf(byte_18229D0, "%s%s", blob[0..16), Str(2053))`.
// Ancres : Pkt_PlayerShopOpen 0x48d9fb-0x48da12 ; Pkt_PlayerShopBuyResult 0x48dfa7-0x48dfc8.
// Pas de champ CString dédié dans ce module scalaire -> stocké tel quel via g_Client.Blob.
void StorePlayerShopVendorName(const uint8_t* blob) {
    std::string vendorName(reinterpret_cast<const char*>(blob),
                           strnlen(reinterpret_cast<const char*>(blob), 16));
    std::string composed = vendorName + game::Str(2053);
    auto& b = game::g_Client.Blob(0x18229D0, composed.size() + 1);
    if (b.size() != composed.size() + 1) b.assign(composed.size() + 1, 0);
    std::memcpy(b.data(), composed.c_str(), composed.size() + 1);
}

// Garde « entrepôt ouvert sur le service 33 » (Pkt_TradeResult 0x48d35b/0x48d364 ;
// Pkt_PlayerShopBuyResult 0x48e378). Son complément exact `!ouvert || service != 33`
// commande le reset du glisser-déposer (Item_DragState_CancelIfActive 0x5B15F0).
bool VendorResyncGate() {
    return game::g_Client.Var(kWarehouseOpen) != 0 && game::g_Client.Var(kOpenServiceWindow) == 33;
}

// Reset de pagination de la liste marchand — bloc IDENTIQUE dans Pkt_TradeResult cases
// 0..5,7,8 (0x48d366 / 0x48d403 / 0x48d4a0 / 0x48d53d / 0x48d5da / 0x48d677 / 0x48d832 /
// 0x48d8cf) et dans Pkt_PlayerShopBuyResult (0x48e37a).
void VendorListResetPagination() {
    game::g_Client.Var(0x1826128) = 1;    // dword_1826128 (nb de pages)      0x48d366
    game::g_Client.Var(0x182612C) = 0;    // dword_182612C (offset de page)   0x48d370
    game::g_Client.Var(0x1826130) = -1;   // dword_1826130 (sélection)        0x48d37a
    game::g_Client.Var(0x1826134) = 0;    // dword_1826134 (nb d'entrées)     0x48d384
}

// Bloc « resync marchand » commun aux cases 0..5,7,8 de Pkt_TradeResult (ancres ci-dessus).
void TradeResultVendorResync() {
    if (!VendorResyncGate()) return;
    VendorListResetPagination();
    // TODO(send) [ancre 0x48d3a0] : Net_SendPacket_Op34(&g_AutoPlayMgr, dword_1826120,
    //   dword_1826124). NE PAS FORCER : dword_1826120/dword_1826124 (les 2 arguments EXACTS
    //   du builder) ne sont modélisés nulle part dans ce module — probablement id/session
    //   marchand posés par un paquet antérieur non encore reversé. Le builder
    //   Net_SendPacket_Op34(NetClient&, int8_t, int8_t) existe déjà (Net/SendPackets.h) et
    //   est prêt à être appelé dès que ces deux globals seront modélisés.
}
} // namespace

void RegisterVendorTradeHandlers(NetSystem& sys) {
    using namespace game;   // g_Client, Str()

    // 0x22 WarehouseOpen — ouverture/résultat entrepôt (blob 1232 o).
    OnPacket<WarehouseOpen>(sys, 0x22, [](const WarehouseOpen& p) {
        g_GmCmdCooldownLatch = 0;
        switch (p.status) {
        case 0:   // ouverture : arme l'état fenêtre entrepôt + focus.
            g_Client.Var(0x1687428) = 1;   // dword_1687428
            g_Client.Var(0x18229B8) = 2;   // dword_18229B8 (focus edit-box)
            g_Warehouse.DecodeBlob(p.blob, sizeof p.blob); // -> dword_18229CC (grille byte-exacte)
            // TODO(state): init chaîne unk_168742C (nom, hors modèle WarehouseGrid — cf.
            //   « en-tête réservé » dans Game/WarehouseSystem.h, non attribué avec certitude).
            // TODO(ui): cGameHud_Hide ; si g_UIEditBoxMgr==14 UI_FocusEditBox(0).
            break;
        case 100: // grille commitée.
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

    // 0x23 WarehouseClose — fermeture UI entrepôt/stockage (mode 1/2).
    OnPacket<WarehouseClose>(sys, 0x23, [](const WarehouseClose& p) {
        if (p.mode == 1) {                 // fermeture simple + refocus.
            g_Client.Var(0x1687428) = 0;   // dword_1687428
            if (g_Client.Var(0x1822998))   // si la grille était active
                g_Client.Var(0x18229B8) = 1;
            // TODO(ui): UI_FocusEditBox(14) ; cGameHud_ResetUiState.
        } else if (p.mode == 2) {          // commit grille + réouverture.
            g_Warehouse.CommitAllToInventory(); // UI_StorageWin_CommitGrid (EA 0x48cdfe)
            g_Client.msg.System(Str(2110));
            // TODO(ui): UI_StorageWin_Open(3, ...) — réouvre l'entrepôt en mode dépôt ;
            //   purement présentation (pas d'état supplémentaire à modéliser ici).
        }
    });

    // 0x24 WarehouseUpdate — mise à jour du coffre (data ou message).
    OnPacket<WarehouseUpdate>(sys, 0x24, [](const WarehouseUpdate& p) {
        switch (p.mode) {
        case 0:
        case 3:
            g_Warehouse.DecodeBlob(p.data, sizeof p.data); // -> dword_18229CC (état entrepôt complet)
            break;
        case 1: g_Client.msg.System(Str(583)); break;
        case 2: g_Client.msg.System(Str(584)); break;
        default: break;
        }
    });

    // 0x25 VendorItemEntry — ajoute une ligne à la liste marchand/recherche courante.
    OnPacket<VendorItemEntry>(sys, 0x25, [](const VendorItemEntry& p) {
        // Réinitialise la liste si l'id de session (dword_1826138) change.
        // Pkt_VendorItemEntry 0x48cf40, bloc 0x48d01a-0x48d045 : CINQ écritures — le
        // dword_182612C (offset de page/scroll) manquait (EA 0x48d024).
        if (g_Client.Var(0x1826138) != static_cast<int32_t>(p.listId)) {
            g_Client.Var(0x1826128) = 1;    // pagination      0x48d01a
            g_Client.Var(0x182612C) = 0;    // offset de page   0x48d024
            g_Client.Var(0x1826130) = -1;   // sélection        0x48d02e
            g_Client.Var(0x1826134) = 0;    // count            0x48d038
            g_Client.Var(0x1826138) = static_cast<int32_t>(p.listId); // 0x48d045
        }
        const int32_t idx = g_Client.Var(0x1826134);
        if (idx < 1000) {
            // Écriture fidèle par index (adressage d'origine base + stride*idx).
            g_Client.Var(0x182613C + 4u * idx)  = static_cast<int32_t>(p.itemId);  // dword_182613C[idx]
            g_Client.Var(0x182A3A4 + 4u * idx)  = static_cast<int32_t>(p.field17); // dword_182A3A4[idx]
            g_Client.Var(0x182B344 + 4u * idx)  = static_cast<int32_t>(p.field21); // dword_182B344[idx]
            g_Client.Var(0x1834F84 + 12u * idx) = static_cast<int32_t>(p.price0);  // dword_1834F84[3*idx]
            g_Client.Var(0x1834F88 + 12u * idx) = static_cast<int32_t>(p.price1);  // dword_1834F88[3*idx]
            g_Client.Var(0x1834F8C + 12u * idx) = static_cast<int32_t>(p.price2);  // dword_1834F8C[3*idx]
            // p.name -> unk_18270DC + 13*idx ; p.blob[36] -> unk_182C2E4 + 36*idx : délibérément NON
            //   stockés ici (pas de champ tableau non-scalaire dans ce module). UI/VendorShopWindow.h
            //   résout nom + icône à partir du SEUL champ fiable ci-dessus (itemId), via ITEM_INFO —
            //   voir ce header pour la justification complète (contournement documenté, pas un oubli).
            g_Client.Var(0x1826134) = idx + 1;                 // dword_1826134 (nb d'entrées)
            // Nb de pages = ANCIEN index / 10 + 1. Le binaire incrémente dword_1826134 PUIS
            // re-soustrait 1 avant la division : `add eax,1 ; mov [dword_1826134],eax ;
            // mov eax,[dword_1826134] ; sub eax,1 ; cdq ; idiv ecx(=10) ; add eax,1`
            // (0x48d11c-0x48d13c, instruction décisive `sub eax,1` @0x48d12e — Hex-Rays le
            // rend par `dword_1826134++ / 10 + 1`). Diviser le NOUVEL index affichait une
            // page vide de trop à chaque multiple exact de 10.
            g_Client.Var(0x1826128) = idx / 10 + 1;            // dword_1826128 (nb de pages) 0x48d13c
        }
    });

    // 0x26 TradeResult — résultat de transaction (vente/entrepôt).
    // Pkt_TradeResult 0x48d150. ATTENTION : `g_GmCmdCooldownLatch = 0` et le reset du
    // glisser-déposer sont posés DANS chaque case SAUF le case 6 (remboursement), qui
    // attaque directement `g_InvWeight += v20` @0x48d6c4. Les hisser au-dessus du switch
    // désarmait le latch là où l'original le laisse armé.
    // Latch : 0x48d21d (0), 0x48d3aa (1), 0x48d447 (2), 0x48d4e4 (3), 0x48d581 (4),
    //         0x48d61e (5), 0x48d7d9 (7), 0x48d876 (8) — aucune écriture dans le case 6.
    OnPacket<TradeResult>(sys, 0x26, [](const TradeResult& p) {
        switch (p.resultCode) {
        case 0: // vendu : crédite la cellule + aux, retire le poids.
            g_GmCmdCooldownLatch = 0;                       // 0x48d21d
            // TODO(ui) [ancre 0x48d23e] : `if (!VendorResyncGate()) Item_DragState_CancelIfActive(g_DragCtx)`
            //   (0x5B15F0). UI/InventoryWindow::CancelDrag() existe mais est hors périmètre de ce module.
            g_Client.inv.weight -= p.weightDelta;           // 0x48d24c
            g_Client.inv.Set(p.invRow, p.invCol, p.item[0], p.item[1], p.item[2],
                             p.item[3], p.item[4], p.item[5]);
            // aux : tableau PARALLÈLE indexé par cellule, pas 3 scalaires globaux.
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 0)) = static_cast<int32_t>(p.aux0); // 0x48d2fa
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 1)) = static_cast<int32_t>(p.aux1); // 0x48d313
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 2)) = static_cast<int32_t>(p.aux2); // 0x48d32c
            g_Client.msg.System(Str(594));                  // 0x48d344
            TradeResultVendorResync();                      // 0x48d35b-0x48d3a0
            break;
        case 6: // remboursement : rend le poids et vide la cellule (NI latch NI reset drag).
            g_Client.inv.weight += p.weightDelta;           // 0x48d6c4
            g_Client.inv.ClearCell(p.invRow, p.invCol);     // 0x48d6d9-0x48d75b
            // inv.ClearCell ne touche pas l'aux : le binaire le remet à zéro explicitement.
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 0)) = 0; // 0x48d775
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 1)) = 0; // 0x48d78f
            g_Client.Var(InvAuxAddr(p.invRow, p.invCol, 2)) = 0; // 0x48d7a9
            g_Client.msg.System(Str(593));                  // 0x48d7c4
            break;
        // Cases d'erreur : latch + reset drag (même garde) + message + resync marchand.
        case 1: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(583));  TradeResultVendorResync(); break;
        case 2: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(584));  TradeResultVendorResync(); break;
        case 3: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(595));  TradeResultVendorResync(); break;
        case 4: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(596));  TradeResultVendorResync(); break;
        case 5: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(597));  TradeResultVendorResync(); break;
        case 7: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(732));  TradeResultVendorResync(); break;
        case 8: g_GmCmdCooldownLatch = 0; g_Client.msg.System(Str(2237)); TradeResultVendorResync();
                // TODO(ui) [ancre 0x48d913] : UI_StorageWin_CommitGrid(dword_1822990).
                break;
        default: break;
        }
        // TODO(ui) [ancres 0x48d23e/0x48d3cb/0x48d468/0x48d505/0x48d5a2/0x48d63f/0x48d7fa/0x48d897] :
        //   Item_DragState_CancelIfActive(g_DragCtx) dans les cases 0..5,7,8 sous la garde
        //   `if (!g_WarehouseWindowOpen || g_OpenServiceWindow != 33)` (= !VendorResyncGate()) —
        //   et JAMAIS dans le case 6.
    });

    // 0x2c DuelResult — malgré le nom, ouvre l'entrepôt si flag==1 :
    //   `if (v1 == 1) UI_Warehouse_Open(&dword_1822EC8, 2, v2)` (Pkt_DuelResult 0x48f760,
    //   appel @0x48f7a5). Ce n'est PAS de la simple présentation : la callee émet un paquet
    //   SORTANT (Net_SendOp91) et mute une dizaine de globals.
    //
    // Décodage de la callee UI_Warehouse_Open 0x5f3db0 : `mov ecx, offset dword_1822EC8`
    // @0x48f7a0 -> `this` est l'ADRESSE 0x1822EC8, donc tout `*(this + N)` du décompilé est
    // le global absolu `0x1822EC8 + 4*N`. Vérifié par 4 recoupements de symboles connus :
    //   *(this+3)     = 0x1822ED4 = g_WarehouseWindowOpen
    //   *(this+180)   = 0x1823198 = g_OpenServiceWindow
    //   *(this+1926)  = 0x1824CE0 = g_VendorRawFeed
    //   *(this+21482) = 0x1837E70 = g_VendorGrid
    // et par *(this+21479)/*(this+21480) = 0x1837E64/0x1837E68, le couple que le handler
    // 0x6e ci-dessous manipule déjà avec le même Net_SendOp91.
    OnPacket<DuelResult>(sys, 0x2c, [&sys](const DuelResult& p) {
        if (p.flag != 1) return;                                   // 0x48f792

        // (a) resync marchand en attente -> émet l'opcode sortant. 0x5f3dc4-0x5f3de4.
        if (g_Client.Var(0x1837E64)) {
            Net_SendOp91(sys.Client());       // 0x5f3dd2 (Net_SendOp91 0x4bc4a0)
            g_Client.Var(0x1837E64) = 0;      // 0x5f3dd7
            g_Client.Var(0x1837E68) = 0;      // 0x5f3de4 (*(this+21480))
        }

        // (b) TODO [ancre 0x5f3df3] : `if (Map_IsArenaZone()) { msg.System(Str(1352)); return; }`
        //   — Map_IsArenaZone 0x54B690 n'est modélisé NULLE PART dans ClientSource (même
        //   lacune que Scene/SceneManager.cpp:934, qui la câble en dur à `false`). On suit
        //   ici le même précédent (zone non-arène) : la garde N'EST PAS émulée. À brancher
        //   dès que la notion de zone arène existera, SINON l'entrepôt s'ouvre en arène là
        //   où l'original le refuse.

        // (c) a2 == 2 -> la branche `if (a2 == 1)` (UI_CloseAllDialogs + Net_SendOp41) est
        //   morte pour ce chemin ; on part directement du mode. 0x5f3e26.
        g_Client.Var(kOpenServiceWindow) = 21;                     // *(this+180)  0x5f3e88
        for (int i = 0; i < 100; ++i)                              // *(this+i+70) 0x5f3e92-0x5f3eb0
            g_Client.Var(0x1822FE0 + 4u * i) = 0;
        g_Client.Var(0x1823B4C) = static_cast<int32_t>(p.param);   // *(this+801)  0x5f3ec3
        // g_LocalElementSecondary 0x1673198 @0x5f3ecf. Source de vérité = le champ modélisé
        // game::g_World.self.elementSecondary (Game/GameState.h:329) — et NON g_Client.Var(0x1673198),
        // qui est une seconde modélisation du même global, sans écrivain (cf. rapport).
        const int32_t page = game::g_World.self.elementSecondary;
        g_Client.Var(0x1823B58) = 1;                               // *(this+804)  0x5f3ed5

        // (d) recopie de la page 10x10 de l'onglet courant : g_VendorGrid[400*page + …]
        //   -> g_VendorRawFeed[…]. Les 4 dwords sont d'abord mis à -1 puis écrasés
        //   (0x5f3f30-0x5f40a8), ce que l'on reproduit par la seule écriture finale.
        for (int j = 0; j < 10; ++j) {
            for (int k = 0; k < 10; ++k) {
                for (int d = 0; d < 4; ++d) {
                    const uint32_t src = kVendorGrid + 4u * (400u * page + 40u * j + 4u * k + d);
                    const uint32_t dst = 0x1824CE0   + 4u * (40u * j + 4u * k + d);
                    g_Client.Var(dst) = g_Client.Var(src);         // 0x5f3fda-0x5f40a8
                }
            }
            // nb de lignes actives : lit le champ itemId (dword +1) de la cellule k=0.
            if (g_Client.Var(kVendorGrid + 4u * (400u * page + 40u * j + 1u)) > 0)  // 0x5f40d2 (signé)
                g_Client.Var(0x1823B58) = j + 1;                   // 0x5f40dd
        }
        g_Client.Var(0x1825E60) = 0;    // *(this+3046) 0x5f40eb
        g_Client.Var(0x1825E64) = 0;    // *(this+3047) 0x5f40f8
        g_Client.Var(0x1825E68) = -1;   // *(this+3048) 0x5f4105
        // TODO(ui) [ancre 0x5f4114] : cGameHud_ResetUiState(dword_1839568).
    });

    // 0x2d RepairResult — résultat réparation / changement d'or.
    OnPacket<RepairResult>(sys, 0x2d, [](const RepairResult& p) {
        g_GmCmdCooldownLatch = 0;
        switch (p.status) {
        case 0: { // succès : crédite l'XP artisan, réécrit la cellule.
            // Pkt_RepairResult 0x48f7b0 case 0 :
            //   `dword_16756F8 += (dword_1823B4C - v18) / 10; dword_1823B4C = v18;`
            // La base du delta est l'ANCIENNE valeur du LATCH PRIVÉ dword_1823B4C (0x1823B4C),
            // lu AVANT d'être écrasé — et NON g_Currency 0x1673180 (= g_Client.inv.currency,
            // cf. Game/ClientRuntime.h:82) : deux globals distincts, que le binaire ne
            // confond jamais (ce handler ne lit ni n'écrit g_Currency).
            const int32_t oldGold = g_Client.Var(0x1823B4C);                         // 0x48f842
            // Cast OBLIGATOIRE : p.goldRemaining est uint32_t (RecvPackets.h:231) ; sans lui
            // les conversions usuelles rendraient l'expression NON SIGNÉE et la division
            // non signée, alors que le binaire fait `cdq ; idiv` (SIGNÉ) @0x48f85c. Or le
            // delta négatif est garanti (latch BSS=0 à la 1re réparation -> 0 - or < 0).
            const int32_t delta = oldGold - static_cast<int32_t>(p.goldRemaining);    // 0x48f83f
            g_Client.Var(0x16756F8) += delta / 10;                                    // XP artisan 0x48f85c-0x48f864
            g_Client.Var(0x1823B4C) = static_cast<int32_t>(p.goldRemaining);          // or restant 0x48f86c
            // NB : `v15[1] = delta / 500` (@0x48f850) est un dead store dans le cadre de pile
            //   local — non observable, délibérément non porté.
            g_Client.inv.Set(p.invRow, p.invCol, p.itemCell[0], p.itemCell[1], p.itemCell[2],
                             p.itemCell[3], p.itemCell[4], p.itemCell[5]);
            g_Client.msg.System(Str(681));
            break;
        }
        case 1: g_Client.msg.System(Str(682)); break;
        case 2: g_Client.msg.System(Str(683)); break;
        case 3: // ferme la fenêtre PNJ + réarme l'état vendeur.
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

    // 0x31 TradeRequestPrompt — invite d'échange entrante : mémorise le partenaire.
    OnPacket<TradeRequestPrompt>(sys, 0x31, [](const TradeRequestPrompt& p) {
        // TODO(audio) [ancre 0x48fd7d] : Snd3D_PlayScaledVolume(flt_148B7FC, 0, 100, 1),
        //   joué AVANT toute mutation d'état (g_PendingOrderKind = 0 @0x48fd82). Non câblé :
        //   l'API C++ est une méthode d'instance (audio::Emitter::PlayScaledVolume,
        //   Audio/Sound3D.h:91) et aucun registre ne relie l'émetteur flt_148B7FC à un
        //   Emitter atteignable depuis ce module (même précédent hors périmètre que
        //   Game/ComboPickupTick.h:266 et Game/QuestSystem.cpp:424).
        g_Client.Var(kTradeState)        = 0;                                 // 0x48fd82
        g_Client.Var(kTradePartnerIdLo)  = static_cast<int32_t>(p.partner[0]); // 0x48fd8f
        g_Client.Var(kTradePartnerVal1)  = static_cast<int32_t>(p.partner[1]); // 0x48fd97
        g_Client.Var(kTradePartnerVal2)  = static_cast<int32_t>(p.partner[2]); // 0x48fda0
        g_Client.Var(kTradeExtra)        = static_cast<int32_t>(p.extra);      // 0x48fdac
        // `Crt_Vsnprintf(v6, "%s [%d]%s", Str(314), promptId, Str(315))` @0x48fde4 :
        //   v3 = Str(315) @0x48fdc0 (SUFFIXE), v0 = Str(314) @0x48fdd2 (PRÉFIXE).
        // Même format que le handler frère 0x32 ci-dessous (Str(314) + " [%d]" + Str(316)).
        g_Client.msg.System(Str(314) + " [" + std::to_string(p.promptId) + "]" + Str(315));
    });

    // 0x32 TradeRequestResult — ligne de résultat de demande d'échange.
    OnPacket<TradeRequestResult>(sys, 0x32, [](const TradeRequestResult& p) {
        g_Client.msg.System(Str(314) + " [" + std::to_string(p.code) + "]" + Str(316));
    });

    // 0x33 TradeActionResult — accept/annulation d'échange, remet l'état à zéro.
    OnPacket<TradeActionResult>(sys, 0x33, [](const TradeActionResult& p) {
        switch (p.code) {
        case 0:
            g_Client.msg.System(Str(317));
            break;
        case 1:
        case 2: // compare le 3e mot data partenaire (posé par 0x31 depuis v7[2], côté
                // SERVEUR — cf. kTradePartnerVal2 ci-dessus) au code d'action reçu.
                // Ancre : Pkt_TradeActionResult 0x48FEA0, `cmp dword_1687424, v7` @0x48fefe.
            g_Client.msg.System(g_Client.Var(kTradePartnerVal2) == static_cast<int32_t>(p.code)
                                    ? Str(318) : Str(319));
            break;
        case 3:
            g_Client.msg.System(Str(320));
            break;
        default: break;
        }
        // Reset systématique de l'état d'échange.
        g_Client.Var(kTradeState)       = 0;
        g_Client.Var(kTradePartnerIdLo) = 0;
        g_Client.Var(kTradePartnerVal1) = 0;
        g_Client.Var(kTradePartnerVal2) = 0;
        g_Client.Var(kTradeExtra)       = 1;
    });

    // 0x6c WarehouseMoveResult — déplacement d'entrepôt / râtelier d'armes.
    OnPacket<WarehouseMoveResult>(sys, 0x6c, [](const WarehouseMoveResult& p) {
        switch (p.action) {
        case 1: if (p.status == 0) g_Client.Var(kThrowWeaponRackCursor) = static_cast<int32_t>(p.index); break; // 0x4a62ac
        case 2: if (p.status == 0) g_Client.Var(kThrowWeaponRackCursor) = -1;  break; // 0x4a62c2
        case 3: if (p.status == 0) g_Client.Var(kThrowWeaponRackCursor) += 10; break; // 0x4a62e5
        case 4: if (p.status == 0) g_Client.Var(kThrowWeaponRackCursor) -= 10; break; // 0x4a6303
        case 5:
            if (p.status == 0) {
                // Retire l'entrée p.index et compacte les suivantes d'un cran, sur les DEUX
                // tableaux parallèles de 10 (cf. kThrowWeaponRack ci-dessus). Le C++ traitait
                // le râtelier comme un tableau plat de 20 : à i==9 il exécutait
                // rack[9] = aux[0], injectant la donnée du tableau voisin là où le binaire
                // écrit 0, puis décalait aux[0..9] depuis l'index 0 au lieu de compacter
                // depuis idx -> désalignement des deux tableaux.
                // Ancres : Net_OnWarehouseMoveResult 0x4A61F0 case 5, 0x4a631a-0x4a6388.
                const int idx = static_cast<int>(p.index);
                if (idx >= 0 && idx < kThrowWeaponRackCapacity) {   // garde de sûreté (le binaire n'en a pas)
                    g_Client.Var(kThrowWeaponRack    + 4u * idx) = 0;  // 0x4a631a
                    g_Client.Var(kThrowWeaponRackAux + 4u * idx) = 0;  // 0x4a6328
                }
                g_Client.Var(kThrowWeaponRackCursor) = -1;             // 0x4a6333 — INCONDITIONNEL
                for (int i = (idx > 0 ? idx : 0); i < 9; ++i) {        // 0x4a6340-0x4a6375, borne 9
                    g_Client.Var(kThrowWeaponRack    + 4u * i) = g_Client.Var(kThrowWeaponRack    + 4u * (i + 1)); // 0x4a6361
                    g_Client.Var(kThrowWeaponRackAux + 4u * i) = g_Client.Var(kThrowWeaponRackAux + 4u * (i + 1)); // 0x4a6375
                }
                g_Client.Var(kThrowWeaponRack    + 4u * 9) = 0;        // 0x4a637e (= 0x1674A20) — INCONDITIONNEL
                g_Client.Var(kThrowWeaponRackAux + 4u * 9) = 0;        // 0x4a6388 (= 0x1674A48) — INCONDITIONNEL
                // La boucle est auto-bornée pour idx>=9 ; le clamp à 0 ne protège que d'un
                // index négatif venu du réseau. Ces deux écarts (garde + clamp) sont des
                // déviations de sûreté assumées, tout le reste est bit-fidèle.
                g_Client.inv.Set(p.invRow, p.invCol, p.itemId, p.gridPos % 8, p.gridPos / 8, 0, 0, 0); // 0x4a63a4-0x4a6436
                g_Client.msg.System(Str(1519));                        // 0x4a6462
            } else if (p.status == 1) {
                g_Client.msg.System(Str(1518));                        // 0x4a649a
            } else if (p.status == 2) {
                g_Client.msg.System(Str(117));                         // 0x4a64d0
            }
            // TODO(audio) [ancres 0x4a644c (status 0) / 0x4a6485 (1) / 0x4a64bd (2)] :
            //   Snd3D_PlayScaledVolume(flt_1495ABC, 0, 100, 1) dans les TROIS branches,
            //   avant le message. Non câblé (même raison qu'en 0x31 ci-dessus).
            break;
        default: break;
        }
    });

    // 0x6d VendorInventoryLoad — charge la table d'objets vendeur (8960 o) + grille.
    //
    // g_VendorRawFeed N'EST PAS un staging séparé : c'est un ALIAS INTERNE de la table
    // copiée par ce paquet. Preuve arithmétique (indépendante des xrefs) :
    //   memcpy(g_ShopItemTable=0x1823B60, payload+8, 0x2300=8960) @0x4a6558, et
    //   g_VendorRawFeed = 0x1824CE0 = 0x1823B60 + 0x1180 -> offset 4480 o < 8960.
    // Donc `g_VendorRawFeed[n]` == `p.shopItemTable[1120 + n]`, et lire le feed directement
    // dans le paquet est fidèle (le binaire le lit juste après son propre memcpy).
    // NB : l'affirmation « aucun autre écrivain dans l'image » qui circulait est FAUSSE —
    //   UI_Warehouse_Open 0x5f3db0 écrit ce même buffer en this-relatif (*(this+1926),
    //   @0x5f3fda), ce qui ne génère aucun xref data. Cela ne change pas la conclusion.
    OnPacket<VendorInventoryLoad>(sys, 0x6d, [](const VendorInventoryLoad& p) {
        g_Client.Var(0x1837E6C) = static_cast<int32_t>(p.param); // dword_1837E6C 0x4a653f
        // Recopie brute (memcpy fidèle @0x4a6558) — structure interne de g_ShopItemTable
        // (dword_1823B60) non décodée plus finement ici.
        std::memcpy(g_Client.Blob(kShopItemTable, sizeof p.shopItemTable).data(),
                    p.shopItemTable, sizeof p.shopItemTable);
        if (p.status != 0) {
            g_Client.Var(0x1837E64) = 1;                         // 0x4a6566
            return;
        }
        // (1) Crt_Memset(g_VendorGrid, -1, 0x12C0) @0x4a6581 — 4800 o = 1200 dwords.
        for (int i = 0; i < 1200; ++i)
            g_Client.Var(kVendorGrid + 4u * i) = -1;

        // (2) curseurs par onglet : v9 = page, v6 = slot (0x4a6589-0x4a65a7).
        int page[3] = {0, 0, 0};
        int slot[3] = {0, 0, 0};

        // (3) triple boucle i<3 / j<14 / k<10 (0x4a65ae-0x4a6790).
        const uint32_t* feed = &p.shopItemTable[1120];            // == g_VendorRawFeed 0x1824CE0
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 14; ++j) {
                for (int k = 0; k < 10; ++k) {
                    // MobDb_GetEntry(mITEM, feed[40*j + 1 + 4*k]) @0x4a6625 ≡ GetItemInfo :
                    //   même sémantique 1-based, même garde id!=0 (Game/GameDatabase.cpp:131).
                    const game::ItemInfo* e = game::GetItemInfo(feed[40 * j + 1 + 4 * k]);
                    if (!e) continue;                             // 0x4a662c
                    const uint32_t cat = e->field212;             // *(Entry+212) 0x4a6639
                    if (cat != 1 && cat != static_cast<uint32_t>(i + 2)) continue; // 0x4a664b
                    // Aucune garde de bornes : elle serait INFIDÈLE et inutile. Le binaire
                    //   peut déborder g_VendorGrid si plus de 100 objets passent le filtre
                    //   d'un onglet (page atteint 14 -> index 1360 > 1199) et écrase alors
                    //   ses voisins ; ici le stockage est une table adresse->int32 (Var),
                    //   donc un index hors plage crée simplement une entrée inerte — pas
                    //   de débordement mémoire possible. Clamper changerait en revanche les
                    //   curseurs page/slot par rapport au binaire.
                    const uint32_t dst = kVendorGrid + 4u * (400u * i + 40u * page[i] + 4u * slot[i]);
                    for (int d = 0; d < 4; ++d)                   // 0x4a668f-0x4a6759
                        g_Client.Var(dst + 4u * d) = static_cast<int32_t>(feed[40 * j + d + 4 * k]);
                    if (++slot[i] > 9) { slot[i] = 0; ++page[i]; } // 0x4a6776-0x4a6790
                }
            }
        }
        // (4) nb de lignes actives par onglet : dword_1823B50[m] (0x4a67a3-0x4a680e).
        //     Comparaison SIGNÉE `> 0` @0x4a6803 ; l'onglet m==2 est sauté.
        //     NB : 0x1823B50 = g_ShopItemTable - 0x10, juste AVANT la table.
        for (int m = 0; m < 4; ++m) {
            g_Client.Var(kVendorTabRows + 4u * m) = 1;             // 0x4a67be
            if (m == 2) continue;                                  // 0x4a67cd
            for (int n = 0; n < 10; ++n)
                if (static_cast<int32_t>(p.shopItemTable[560 * m + 40 * n]) > 0) // 0x4a6803
                    g_Client.Var(kVendorTabRows + 4u * m) = n + 1; // 0x4a680e
        }
        g_Client.Var(0x1837E68) = 1;                               // 0x4a6819 — EN DERNIER
    });

    // 0x6e VendorClose — ferme la fenêtre marchand (aucun payload).
    OnTrigger(sys, 0x6e, [&sys]() {
        g_Client.Var(0x1837E64) = 1;
        g_Client.Var(0x1837E68) = 0;
        if (g_Client.Var(kWarehouseOpen) == 1 && g_Client.Var(kOpenServiceWindow) == 21) {
            Net_SendOp91(sys.Client());  // resync entrepôt (opcode 0x5B, "this"=&unk_846C08=NetClient, sans payload).
            g_Client.Var(0x1837E64) = 0;
            // TODO(ui): UI_NpcWin_CloseRestore(&dword_1822EC8).
        }
    });

    // 0x87 PlayerShopOpen — ouverture de la grille de boutique personnelle (blob 824 o).
    // Pkt_PlayerShopOpen 0x48d940. Le sous-switch de focus est atteint par l'étiquette
    // `default:`/LABEL_12 (@0x48dd9e) ET par un `goto LABEL_12` en fin de case 0 (@0x48dcf0) :
    // il s'exécute donc pour resultCode==0 ET pour tout code hors {0,1,100,101,102,103} —
    // d'où sa sortie du `if (resultCode == 0)`.
    OnPacket<PlayerShopOpen>(sys, 0x87, [](const PlayerShopOpen& p) {
        bool runFocus = false;
        switch (p.resultCode) {
        case 0:
            // Nom du vendeur : "%s%s"(blob[0..16), Str(2053)) -> byte_18229D0. 0x48d9fb-0x48da12.
            StorePlayerShopVendorName(p.blob);
            g_Client.Var(0x18229CC) = 0;   // dword_18229CC 0x48da1a
            {
                uint32_t d816 = 0, d820 = 0;
                std::memcpy(&d816, p.blob + 816, 4);
                std::memcpy(&d820, p.blob + 820, 4);
                g_Client.Var(0x1822EB4) = static_cast<int32_t>(d816); // blob[816] 0x48da2a
                g_Client.Var(0x1822EB8) = static_cast<int32_t>(d820); // blob[820] 0x48da36
            }
            DecodePlayerShopGrid(p.blob);  // grille objets dword_18229EC.. + prix dword_1822D70..
            g_Client.Var(0x1822998) = 1;   // dword_1822998 0x48dcd2
            g_Client.Var(0x1822EBC) = 1;   // dword_1822EBC 0x48dcdc
            // TODO(ui) [ancre 0x48dceb] : cGameHud_ResetUiState(dword_1839568).
            runFocus = true;               // goto LABEL_12 @0x48dcf0
            break;
        case 1:
            // TODO(ui) [ancre 0x48dd13] : UI_MsgBox_Open(dword_1822438, 54, v13, Str(2088)) —
            //   v13 = tampon local de 200 o mis à zéro @0x48d97d (jamais renseigné ici).
            //   L'id de chaîne EST documenté et prouvé : 2088 (@0x48dcff) ; c'est la CIBLE
            //   qui manque. Non câblé sur g_Client.prompt : PromptState modélise le registre
            //   dword_1822440/1822450 (UI_NoticeDlg/ConfirmPrompt), alors que UI_MsgBox_Open
            //   0x5C08C0 opère sur dword_1822438 — objet distinct, correspondance NON prouvée.
            break;
        case 100:
        case 102:
        case 103: g_Client.msg.System(Str(2032)); break;  // 0x48dd2e
        case 101: g_Client.msg.System(Str(2054)); break;  // 0x48dd54
        default:  runFocus = true; break;                 // LABEL_12 @0x48dd9e
        }
        if (!runFocus) return;
        // Pilotage du focus des edit-box (dword_18229B8 = 5/6/7 selon focusState 1/2/3).
        switch (p.focusState) {
        case 1:
            // TODO(ui) [ancres 0x48ddbb/0x48ddcc] : UI_FocusEditBox(&g_UIEditBoxMgr, 14) puis
            //   SetWindowTextA(dword_1668FF8, &String).
            g_Client.Var(0x18229B8) = 5;   // 0x48ddd2
            g_Client.Var(0x1822EC0) = 2;   // 0x48dddc
            break;
        case 2: g_Client.Var(0x18229B8) = 6; break;  // 0x48dde8
        case 3: g_Client.Var(0x18229B8) = 7; break;  // 0x48ddf4
        default: break;
        }
    });

    // 0x88 PlayerShopBuyResult — résultat d'achat + resync grille marchand.
    OnPacket<PlayerShopBuyResult>(sys, 0x88, [](const PlayerShopBuyResult& p) {
        g_GmCmdCooldownLatch = 0;
        if (p.resultCode == 0 || p.resultCode == 1000) { // succès (LABEL_8).
            // Nom vendeur : "%s%s"(shopBlock[0..16), Str(2053)) -> byte_18229D0. 0x48dfa7-0x48dfc8.
            StorePlayerShopVendorName(p.shopBlock);
            DecodePlayerShopGrid(p.shopBlock); // grille objets/prix 5x5 (même layout que PlayerShopOpen)
            g_Client.inv.Set(p.dstRow, p.dstCol, p.itemCell[0], p.itemCell[1], p.itemCell[2],
                             p.itemCell[3], p.itemCell[4], p.itemCell[5]);  // 0x48e279-0x48e2f6
            // aux : tableau PARALLÈLE indexé par cellule (et non 3 scalaires globaux partagés
            // par toutes les cellules, ce que faisait inv.aux0/1/2 — champs par ailleurs
            // write-only : aucun lecteur dans tout src/).
            g_Client.Var(InvAuxAddr(p.dstRow, p.dstCol, 0)) = static_cast<int32_t>(p.itemCell[6]); // 0x48e30f
            g_Client.Var(InvAuxAddr(p.dstRow, p.dstCol, 1)) = static_cast<int32_t>(p.itemCell[7]); // 0x48e328
            g_Client.Var(InvAuxAddr(p.dstRow, p.dstCol, 2)) = static_cast<int32_t>(p.itemCell[8]); // 0x48e341
            g_Client.inv.weight -= p.itemCell[9];                                                  // 0x48e350
            // TODO(ui) [ancre 0x48e35a] : Item_DragState_CancelIfActive(g_DragCtx) — ici
            //   INCONDITIONNEL (pas de garde, contrairement à Pkt_TradeResult).
            if (VendorResyncGate() && p.resultCode == 1000) {   // 0x48e378
                VendorListResetPagination();                    // 0x48e37a-0x48e398
                // TODO(send) [ancre 0x48e3b5] : Net_SendPacket_Op34(&g_AutoPlayMgr,
                //   dword_1826120, dword_1826124) — bloqué, cf. TradeResultVendorResync().
            }
            g_Client.msg.System(p.resultCode == 1000 ? Str(2112) : Str(2113)); // 0x48e3d3 / 0x48e3f6
        } else {
            // Codes d'erreur (switch principal 0x48df41/0x48df5a/0x48df7f) : chaque branche
            // fait UI_StorageWin_CommitGrid + cGameHud_Hide + Item_RestoreDragImmediate
            // (TODO ui) puis la ligne système ci-dessous.
            // La table précédente était décalée : elle énumérait les chaînes dans l'ordre
            // (2032,2066,2055,2056,2057,2058,2237) sur 1,2,3,4,5,101,default. Mapping RÉEL :
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
            default:  break;  // def_48DF5A (0x48e61d) = return : AUCUN message. Ne pas
                              // afficher Str(2237) ici — l'original reste muet.
            }
        }
    });

    // 0x89 PlayerShopGoldResult — résultat or/règlement boutique joueur.
    OnPacket<PlayerShopGoldResult>(sys, 0x89, [](const PlayerShopGoldResult& p) {
        g_GmCmdCooldownLatch = 0;
        switch (p.status) {
        case 0: // règlement réussi.
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

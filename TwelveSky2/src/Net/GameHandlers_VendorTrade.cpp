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
#include <cstdint>
#include <cstring>
#include <string>

namespace ts2::net {

// Adresses d'origine (globals non modélisés en champ propre, via g_Client.Var).
namespace {
constexpr uint32_t kTradePartnerIdLo = 0x168741C; // g_TradePartnerIdLo (partner[0])
constexpr uint32_t kTradePartnerVal1 = 0x1687420; // dword_1687420      (partner[1])
constexpr uint32_t kTradePartnerVal2 = 0x1687424; // dword_1687424 (accord local)
constexpr uint32_t kTradeState       = 0x1675B24; // dword_1675B24 (état d'échange)
constexpr uint32_t kTradeExtra       = 0x1675D84; // dword_1675D84
constexpr uint32_t kOpenServiceWindow= 0x1823198; // g_OpenServiceWindow
constexpr uint32_t kWarehouseOpen    = 0x1822ED4; // g_WarehouseWindowOpen
constexpr uint32_t kThrowWeaponRack  = 0x16749FC; // g_ThrowWeaponRack[D] (cf. Net/ItemActionDispatch.cpp)
constexpr int       kThrowWeaponRackCapacity = 20; // dword_1674A4C (curseur râtelier) couvre [0,20)
constexpr uint32_t kShopGridItemBase = 0x18229EC; // dword_18229EC.. — grille objets 5x5 (125 dword)
constexpr uint32_t kShopGridPriceBase= 0x1822D70; // dword_1822D70.. — grille prix/qté 5x5 (75 dword)

// Pkt_PlayerShopOpen (0x87) / Pkt_PlayerShopBuyResult (0x88) partagent le MÊME layout
// de blob (RE/net_handler_notes.md ## PlayerShopOpen) : [0..16) nom (non modélisé, cf.
// TODO ci-dessous), [16..516) grille 5x5 d'objets (20 o/cellule = 5 dword), [516..816)
// grille 5x5 prix/qté (12 o/cellule = 3 dword). Les deux plages sont des memcpy bruts
// contigus vers dword_18229EC.. et dword_1822D70.. -> reproduites ici par lecture
// dword-à-dword (pas de réinterprétation de structure, fidèle au binaire).
void DecodePlayerShopGrid(const uint8_t* blob824) {
    for (int i = 0; i < 125; ++i) {
        uint32_t v; std::memcpy(&v, blob824 + 16 + 4 * i, 4);
        game::g_Client.Var(kShopGridItemBase + 4u * i) = static_cast<int32_t>(v);
    }
    for (int i = 0; i < 75; ++i) {
        uint32_t v; std::memcpy(&v, blob824 + 516 + 4 * i, 4);
        game::g_Client.Var(kShopGridPriceBase + 4u * i) = static_cast<int32_t>(v);
    }
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
        if (g_Client.Var(0x1826138) != static_cast<int32_t>(p.listId)) {
            g_Client.Var(0x1826138) = static_cast<int32_t>(p.listId);
            g_Client.Var(0x1826128) = 1;    // pagination
            g_Client.Var(0x1826134) = 0;    // count
            g_Client.Var(0x1826130) = -1;   // sélection courante
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
            g_Client.Var(0x1826128) = (idx + 1) / 10 + 1;      // dword_1826128 (nb de pages)
        }
    });

    // 0x26 TradeResult — résultat de transaction (vente/entrepôt).
    OnPacket<TradeResult>(sys, 0x26, [](const TradeResult& p) {
        g_GmCmdCooldownLatch = 0;
        switch (p.resultCode) {
        case 0: // vendu : crédite la cellule + aux, retire le poids.
            g_Client.inv.weight -= p.weightDelta;
            g_Client.inv.Set(p.invRow, p.invCol, p.item[0], p.item[1], p.item[2],
                             p.item[3], p.item[4], p.item[5]);
            g_Client.inv.aux0 = p.aux0;
            g_Client.inv.aux1 = p.aux1;
            g_Client.inv.aux2 = p.aux2;
            g_Client.msg.System(Str(594));
            // TODO(send): si entrepôt ouvert (g_WarehouseWindowOpen && g_OpenServiceWindow==33) ->
            //   dword_1826128=1, dword_182612C=0, dword_1826130=-1, dword_1826134=0 (reset pagination,
            //   distinct de la resync gérée par VendorItemEntry ci-dessus), PUIS
            //   Net_SendPacket_Op34(dword_1826120, dword_1826124) — EA 0x48c5xx (Pkt_TradeResult case 0).
            //   NE PAS FORCER : dword_1826120/dword_1826124 (les 2 arguments EXACTS du builder, vus dans
            //   le décompilé RE/net_batches/recv_5.json::Pkt_TradeResult) ne sont modélisés nulle part
            //   dans ce module (aucun équivalent g_Client.Var pour ces deux globals) — probablement
            //   id/session marchand posés par un paquet antérieur (WarehouseOpen/VendorInventoryLoad) non
            //   encore reversé. Le builder Net_SendPacket_Op34(NetClient&, int8_t, int8_t) existe déjà
            //   (Net/SendPackets.h) et est prêt à être appelé dès que ces deux globals seront modélisés.
            break;
        case 6: // remboursement : rend le poids et vide la cellule.
            g_Client.inv.weight += p.weightDelta;
            g_Client.inv.ClearCell(p.invRow, p.invCol);
            g_Client.msg.System(Str(593));
            break;
        case 1: g_Client.msg.System(Str(583)); break;  // erreurs + reset drag (TODO ui).
        case 2: g_Client.msg.System(Str(584)); break;
        case 3: g_Client.msg.System(Str(595)); break;
        case 4: g_Client.msg.System(Str(596)); break;
        case 5: g_Client.msg.System(Str(597)); break;
        case 7: g_Client.msg.System(Str(732)); break;
        case 8: g_Client.msg.System(Str(2237));         // + UI_StorageWin_CommitGrid (TODO ui).
                break;
        default: break;
        }
        // TODO(ui): reset drag (sub_5B15F0) sur les cas d'erreur.
    });

    // 0x2c DuelResult — malgré le nom, ouvre l'entrepôt si flag==1.
    OnPacket<DuelResult>(sys, 0x2c, [](const DuelResult& p) {
        if (p.flag == 1) {
            // TODO(ui): UI_Warehouse_Open(2, p.param) — seule action observée.
            (void)p.param;
        }
    });

    // 0x2d RepairResult — résultat réparation / changement d'or.
    OnPacket<RepairResult>(sys, 0x2d, [](const RepairResult& p) {
        g_GmCmdCooldownLatch = 0;
        switch (p.status) {
        case 0: { // succès : crédite l'XP artisan, réécrit la cellule.
            const int64_t oldGold = g_Client.inv.currency;
            g_Client.Var(0x16756F8) += static_cast<int32_t>((oldGold - p.goldRemaining) / 10); // XP artisan
            g_Client.Var(0x1823B4C) = static_cast<int32_t>(p.goldRemaining);                   // or restant
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
        g_Client.Var(kTradeState)        = 0;
        g_Client.Var(kTradePartnerIdLo)  = static_cast<int32_t>(p.partner[0]);
        g_Client.Var(kTradePartnerVal1)  = static_cast<int32_t>(p.partner[1]);
        g_Client.Var(kTradePartnerVal2)  = static_cast<int32_t>(p.partner[2]);
        g_Client.Var(kTradeExtra)        = static_cast<int32_t>(p.extra);
        // Message '[promptId] ...' (StrTable005 314/315 selon contexte — 314 par défaut).
        g_Client.msg.System("[" + std::to_string(p.promptId) + "]" + Str(314));
        // TODO(audio): jouer un son de notification.
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
        case 2: // accord local (dword_1687424) confirmé ou non.
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
        case 1: if (p.status == 0) g_Client.Var(0x1674A4C) = static_cast<int32_t>(p.index); break; // curseur râtelier
        case 2: if (p.status == 0) g_Client.Var(0x1674A4C) = -1; break;
        case 3: if (p.status == 0) g_Client.Var(0x1674A4C) += 10; break;
        case 4: if (p.status == 0) g_Client.Var(0x1674A4C) -= 10; break;
        case 5:
            if (p.status == 0) {
                // Retire l'entrée p.index du râtelier (g_ThrowWeaponRack) et compacte les
                // entrées suivantes d'un cran (RE/net_handler_notes.md ## WarehouseMoveResult).
                const int idx = static_cast<int>(p.index);
                if (idx >= 0 && idx < kThrowWeaponRackCapacity) {
                    for (int i = idx; i < kThrowWeaponRackCapacity - 1; ++i)
                        g_Client.Var(kThrowWeaponRack + 4u * i) = g_Client.Var(kThrowWeaponRack + 4u * (i + 1));
                    g_Client.Var(kThrowWeaponRack + 4u * (kThrowWeaponRackCapacity - 1)) = 0;
                }
                g_Client.inv.Set(p.invRow, p.invCol, p.itemId, p.gridPos % 8, p.gridPos / 8, 0, 0, 0);
                g_Client.msg.System(Str(1519));
            } else if (p.status == 1) {
                g_Client.msg.System(Str(1518));
            } else if (p.status == 2) {
                g_Client.msg.System(Str(117));
            }
            break;
        default: break;
        }
    });

    // 0x6d VendorInventoryLoad — charge la table d'objets vendeur (8960 o) + grille.
    OnPacket<VendorInventoryLoad>(sys, 0x6d, [](const VendorInventoryLoad& p) {
        g_Client.Var(0x1837E6C) = static_cast<int32_t>(p.param); // dword_1837E6C (id/param vendeur)
        // Recopie brute (memcpy fidèle, cf. RE/net_handler_notes.md ## VendorInventoryLoad) —
        // structure interne de g_ShopItemTable (dword_1823B60) non décodée plus finement ici.
        std::memcpy(g_Client.Blob(0x1823B60, sizeof p.shopItemTable).data(),
                    p.shopItemTable, sizeof p.shopItemTable);
        if (p.status != 0) {
            g_Client.Var(0x1837E64) = 1;
        } else {
            // TODO(state): memset g_VendorGrid(-1, 4800) puis reconstruire la grille par onglet
            //   (triple boucle i<3/j<14/k<10, MobDb_GetEntry(g_VendorRawFeed[...]), catégorie template+212)
            //   + dword_1823B50[m] (nb de lignes actives par onglet). BLOQUÉ (pas juste non câblé) :
            //   g_VendorRawFeed est un STAGING SÉPARÉ, absent de ce payload et de tout autre paquet
            //   déjà reversé dans ce domaine (RE/net_handler_notes.md ## VendorInventoryLoad le note
            //   explicitement : « à vérifier si les deux pointent le même buffer »). Tant que son
            //   origine réseau n'est pas identifiée, reconstruire cette grille ici produirait un
            //   résultat non fidèle (données inventées) plutôt qu'un TODO honnête.
            g_Client.Var(0x1837E68) = 1;
        }
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
    OnPacket<PlayerShopOpen>(sys, 0x87, [](const PlayerShopOpen& p) {
        if (p.resultCode == 0) {
            DecodePlayerShopGrid(p.blob); // grille 5x5 objets dword_18229EC.. + prix dword_1822D70..
            uint32_t d816 = 0, d820 = 0;
            std::memcpy(&d816, p.blob + 816, 4);
            std::memcpy(&d820, p.blob + 820, 4);
            g_Client.Var(0x1822EB4) = static_cast<int32_t>(d816); // blob[816] -> dword_1822EB4
            g_Client.Var(0x1822EB8) = static_cast<int32_t>(d820); // blob[820] -> dword_1822EB8
            g_Client.Var(0x1822998) = 1;   // dword_1822998
            g_Client.Var(0x1822EBC) = 1;   // dword_1822EBC
            // TODO(ui): cGameHud_ResetUiState.
            // Pilotage du focus des edit-box (dword_18229B8 = 5/6/7 selon focusState 1/2/3).
            switch (p.focusState) {
            case 1: g_Client.Var(0x18229B8) = 5; break;
            case 2: g_Client.Var(0x18229B8) = 6; break;
            case 3: g_Client.Var(0x18229B8) = 7; break;
            default: break;
            }
        }
        // TODO(ui): autres resultCode (1=MsgBox, 100..103=messages système) — str ids non documentés.
    });

    // 0x88 PlayerShopBuyResult — résultat d'achat + resync grille marchand.
    OnPacket<PlayerShopBuyResult>(sys, 0x88, [](const PlayerShopBuyResult& p) {
        g_GmCmdCooldownLatch = 0;
        if (p.resultCode == 0 || p.resultCode == 1000) { // succès.
            DecodePlayerShopGrid(p.shopBlock); // grille objets/prix 5x5 (même layout que PlayerShopOpen)
            // Nom vendeur : "%s%s"(nom shopBlock[0..16), str2053) -> unk_18229D0 (RE/
            // net_handler_notes.md ## PlayerShopBuyResult). Pas de champ CString dédié dans ce
            // module scalaire -> stocké tel quel via g_Client.Blob (aucun lecteur UI câblé
            // encore, mais l'écriture d'état est fidèle).
            {
                std::string vendorName(reinterpret_cast<const char*>(p.shopBlock),
                                        strnlen(reinterpret_cast<const char*>(p.shopBlock), 16));
                std::string composed = vendorName + Str(2053);
                auto& blob = g_Client.Blob(0x18229D0, composed.size() + 1);
                if (blob.size() != composed.size() + 1) blob.assign(composed.size() + 1, 0);
                std::memcpy(blob.data(), composed.c_str(), composed.size() + 1);
            }
            g_Client.inv.Set(p.dstRow, p.dstCol, p.itemCell[0], p.itemCell[1], p.itemCell[2],
                             p.itemCell[3], p.itemCell[4], p.itemCell[5]);
            g_Client.inv.aux0 = p.itemCell[6];
            g_Client.inv.aux1 = p.itemCell[7];
            g_Client.inv.aux2 = p.itemCell[8];
            g_Client.inv.weight -= p.itemCell[9];
            g_Client.msg.System(p.resultCode == 1000 ? Str(2112) : Str(2113));
            // TODO(send): si entrepôt ouvert (g_WarehouseWindowOpen && g_OpenServiceWindow==33 &&
            //   p.resultCode==1000) -> même reset pagination + Net_SendPacket_Op34(dword_1826120,
            //   dword_1826124) que dans TradeResult ci-dessus (RE/net_batches/recv_0.json::
            //   Pkt_PlayerShopBuyResult). NE PAS FORCER, même raison : dword_1826120/1826124 non modélisés.
        } else {
            // Codes d'erreur : ferme/restaure le drag (TODO ui) + ligne système.
            switch (p.resultCode) {
            case 100: g_Client.msg.System(Str(2054)); break;
            case 1:   g_Client.msg.System(Str(2032)); break;
            case 2:   g_Client.msg.System(Str(2066)); break;
            case 3:   g_Client.msg.System(Str(2055)); break;
            case 4:   g_Client.msg.System(Str(2056)); break;
            case 5:   g_Client.msg.System(Str(2057)); break;
            case 101: g_Client.msg.System(Str(2058)); break;   // 'e' — reste 'f'..'i' -> 2237 (approx.).
            default:  g_Client.msg.System(Str(2237)); break;
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

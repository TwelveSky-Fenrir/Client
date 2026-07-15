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
inline void WriteSrcCellFromPending() {
    g_Client.inv.At(static_cast<uint32_t>(g_Client.pendingMoveRow),
                    static_cast<uint32_t>(g_Client.pendingMoveCol)) = g_Client.pendingItem;
}

// Réinitialise l'état de déplacement en attente : g_PendingMove_SrcRow0 = -1 et
// dword_1822EDC/EE0/EE4 = -1 (destinations/scratch du move, longue traîne fidèle).
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
    // 0/10 : poids -= weightDelta, écrit la cellule résultat (payload) au slot pending
    //        puis reset pending. 1 : applique le snapshot pending seul. latch remis à 0.
    OnPacket<ItemCombineResult>(sys, 0x1d, [](const ItemCombineResult& p) {
        g_GmCmdCooldownLatch = 0;
        if (p.resultCode == 0 || p.resultCode == 10) {
            g_Client.inv.weight -= p.weightDelta;
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.serial);
            // TODO : 3 champs aux dword_1822F20/24/28 (snapshot pending) non modélisés.
            ResetPendingMove();
            g_Client.msg.System(Str(p.resultCode == 0 ? 715 : 716));
        } else if (p.resultCode == 1) {
            WriteSrcCellFromPending();   // dword_1822F08.. (add sur count côté serveur)
            ResetPendingMove();
            g_Client.msg.System(Str(715));
        } else {
            // TODO : variante d'échec (message 2697) à confirmer.
            g_Client.msg.System(Str(2697));
        }
    });

    // 0x1e ItemSwapResultA — confirme un déplacement/échange d'objet en attente.
    OnPacket<ItemSwapResultA>(sys, 0x1e, [](const ItemSwapResultA& p) {
        if (p.resultCode == 0) {         // OK simple : cellule depuis le payload
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]);
            g_Client.inv.weight -= p.weightDelta;
            g_Client.msg.System(Str(222));
        } else if (p.resultCode == 1) {  // OK via snapshot pending (add sur count)
            WriteSrcCellFromPending();
            g_Client.msg.System(Str(223));
        } else if (p.resultCode == 2) {  // échange source <-> destination
            WriteSrcCellFromPending();
            // TODO : cellule destination [384*dword_1822EDC + 6*dword_1822EF4] depuis
            //        le snapshot dest (dword_1822F2C..), non modélisé.
            g_Client.msg.System(Str(726));
        }
        ResetPendingMove();
    });

    // 0x1f ItemSwapResultB — variante de déplacement/échange (mêmes 3 branches).
    OnPacket<ItemSwapResultB>(sys, 0x1f, [](const ItemSwapResultB& p) {
        g_GmCmdCooldownLatch = 0;
        if (p.resultCode == 0) {         // commit direct depuis le payload
            WriteSrcCell(p.item[0], p.item[1], p.item[2],
                         p.item[3], p.item[4], p.item[5]);
            g_Client.inv.weight -= p.weightDelta;
            g_Client.msg.System(Str(222));
        } else if (p.resultCode == 1) {  // commit via snapshot pending
            WriteSrcCellFromPending();
            g_Client.msg.System(Str(223));
        } else if (p.resultCode == 2) {  // swap : source + destination
            WriteSrcCellFromPending();
            // TODO : cellule destination (dword_1822EDC/EF4 + snapshot dword_1822F2C..).
            g_Client.msg.System(Str(727));
        }
        ResetPendingMove();
    });

    // 0x20 ItemDiscardResult — résultat de jet/suppression d'objet.
    // Écrit toujours la cellule source (pending) depuis le payload, ajuste poids/monnaie
    // selon adjustMode, puis dispatch resultCode.
    OnPacket<ItemDiscardResult>(sys, 0x20, [](const ItemDiscardResult& p) {
        WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.instanceSerial);
        if (p.adjustMode == 1)      g_Client.inv.weight   -= p.amount;
        else if (p.adjustMode == 2) g_Client.inv.currency -= p.amount;
        switch (p.resultCode) {
            case 0:
            case 1:
                // TODO(msg) : ligne système (identifiant str non documenté par les notes).
                break;
            case 40:
                // TODO : restaure 3 cellules d'échange (dword_1822EDC/EE0/EE4 ->
                //        snapshots dword_1822F2C../F50../F74..), non modélisés.
                break;
            case 100:
            case 101:
                // Vide la cellule « aux » de déplacement (g_InvAux/1674ABC/1674AC0).
                g_Client.inv.aux0 = g_Client.inv.aux1 = g_Client.inv.aux2 = 0;
                break;
            default:
                break;
        }
        ResetPendingMove();
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
    OnPacket<ItemSellResult>(sys, 0x6a, [](const ItemSellResult& p) {
        if (p.resultCode == 0) {
            g_GmCmdCooldownLatch = 0;
            g_Client.inv.weight += p.weightDelta;
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]);
            ResetPendingMove();
            g_Client.msg.System(Str(1305));
        } else if (p.resultCode == 1) {
            g_Client.msg.System(Str(1912));
        }
    });

    // 0x6b GambleResult — résultat de loterie/pari (déconnecte à l'échec sec).
    OnPacket<GambleResult>(sys, 0x6b, [&sys](const GambleResult& p) {
        if (p.selector == 1) {           // gain
            g_Client.msg.System("[" + std::to_string(p.value) + "]" + Str(1351));
            // (son joué côté audio, hors périmètre)
        } else if (p.selector == 2) {    // fin/échec
            if (static_cast<int32_t>(p.value) <= 0) {
                NetCloseSocket(sys.Client()); // déconnexion sur échec sec
                g_Client.Var(0x1676180) = 2; // g_SceneMgr = 2 (retour écran de sélection)
                // g_SceneSubState = 0 : sous-état de scène non modélisé (TODO).
            } else {
                g_Client.msg.System(Str(1350));
            }
        } else if (p.selector == 3) {    // info
            g_Client.msg.System(Str(1394));
        }
    });

    // 0x70 ItemCombineResult2 — combine/sertissage : met à jour 1 ou 2 cases.
    OnPacket<ItemCombineResult2>(sys, 0x70, [](const ItemCombineResult2& p) {
        g_GmCmdCooldownLatch = 0;
        if (p.resultCode == 0 || p.resultCode == 1) {
            g_Client.inv.weight -= p.weightDelta;
            WriteSrcCellFromPending();   // case source depuis dword_1822F08.. (count +=)
            ResetPendingMove();
            g_Client.msg.System(Str(p.resultCode == 0 ? 1645 : 222));
        } else if (p.resultCode == 2) {
            g_Client.inv.weight -= p.weightDelta;
            WriteSrcCellFromPending();
            // TODO : cellule destination (dword_1822EDC/EF4 + snapshot dword_1822F2C..).
            ResetPendingMove();
            g_Client.msg.System(Str(1645));
        }
        // NB : le payload (itemId..serial) n'est utilisé que dans le chemin « else »
        // sans pending de l'original ; toute la donnée objet vient du snapshot pending.
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
    OnPacket<ItemCountNotice>(sys, 0x8c, [](const ItemCountNotice& p) {
        const std::string t = std::to_string(p.count) + Str(p.subop == 0 ? 2074 : 1351);
        g_Client.msg.Floating(1, 0, t);
        g_Client.msg.System(t, 1);
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
    OnPacket<ItemBuyResult>(sys, 0xa4, [](const ItemBuyResult& p) {
        g_GmCmdCooldownLatch = 0;
        g_Client.inv.weight -= 10000000;   // unk_989680 (0x989680), constante de coût
        if (p.resultCode == 0) {
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]);
            ResetPendingMove();
            g_Client.msg.System(Str(2388));
        } else if (p.resultCode == 1) {
            WriteSrcCellFromPending();      // snapshot pending (add sur count)
            ResetPendingMove();
            g_Client.msg.System(Str(2389));
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
    OnPacket<ItemSlotRefresh>(sys, 0xad, [](const ItemSlotRefresh& p) {
        if (p.resultCode == 0 || p.resultCode == 10) {
            g_GmCmdCooldownLatch = 0;
            WriteSrcCell(p.itemCell[0], p.itemCell[1], p.itemCell[2],
                         p.itemCell[3], p.itemCell[4], p.itemCell[5]);
            ResetPendingMove();
            g_Client.inv.currency -= p.goldDelta;   // g_Currency & dword_1687254[0]
            g_Client.msg.System(Str(2563));
        } else if (p.resultCode == 1) {
            g_Client.msg.System(Str(2569));
        } else if (p.resultCode == 2) {
            g_Client.msg.System(Str(2561));
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

// Net/GameHandlers_InvDispatch.cpp — méga-dispatchers d'objets (enchant/refine/
// socket/fuse/upgrade/enhance) + chargements/retraits groupés (batch/bulk/multi),
// sync stats et apparence d'équipement.
//
// Domaine « inv_dispatch » (RE/handler_domains.json). Traduit fidèlement la logique
// de mise à jour d'état des handlers d'origine (RE/net_handler_notes.md) vers le hub
// game::g_Client (grille d'inventaire, monnaie/poids, journal de messages, état de
// déplacement d'objet en attente). Anticheat/son/UI-rendu exact hors périmètre.
// Rappel modèle : g_Client.inv.currency représente g_Currency ET son miroir
// dword_1687254[0] ; g_Client.pendingItem = snapshot objet en attente dword_1822F08..
// (avec .color = durabilité bit-packée dword_1822F18, .durability = serial).
//
// Opcodes couverts (18) :
//   0x1b LegacyItemUpgradeResult (*) 0x1c LegacyItemRefineResult (*)
//   0x75 ItemEnchantDispatch  0x77 InventoryBulkLoad   0x7c ItemRefineResult
//   0x83 PlayerEquipVisual    0x8d BulkItemConsume     0x95 ItemBatchUpdate
//   0x97 MultiItemRemove      0x9b ItemSocketResult    0xa8 ItemUpgradeResult
//   0xa9 ItemFuseResult       0xab ItemSocketDispatch  0xac ItemRefineDispatch
//   0xaf ItemEnhanceResult    0xb0 ItemEnhanceResult2  0xb3 ItemDropResult
//   0xb4 StatSyncDispatch
// (*) 0x1b/0x1c : trou de couverture comblé — absents de RE/handler_domains.json et de
//     tout Register*Handlers avant cet ajout (cf. Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md).
//     Noms IDA historiques Pkt_ItemUpgradeResult/Pkt_ItemRefineResult, DISTINCTS des
//     handlers Net_On* de même thème (0xa8/0x7c) déjà couverts ci-dessous.
#include "Net/GameHandlers.h"
#include "Game/ClientRuntime.h"
#include "Game/GameState.h"    // game::g_World (self.element = g_LocalElement)
#include "Net/ClientState.h"   // ts2::net::g_GmCmdCooldownLatch
#include <string>
#include <cstring>
#include <cstdint>

namespace {
using namespace ts2::game;

// --- Cellules d'inventaire pilotées par le « pending move » -----------------

// Écrit la cellule SOURCE du move en attente — index d'origine
// [384*g_PendingMove_SrcRow0 + 6*dword_1822EF0] = (pendingMoveRow, pendingMoveCol) —
// depuis 6 dwords bruts {itemId, gridX, gridY, count, durability, serial}.
inline void WriteSrcCell(uint32_t itemId, uint32_t gridX, uint32_t gridY,
                         uint32_t count, uint32_t durability, uint32_t serial) {
    g_Client.inv.Set(static_cast<uint32_t>(g_Client.pendingMoveRow),
                     static_cast<uint32_t>(g_Client.pendingMoveCol),
                     itemId, gridX, gridY, count, durability, serial);
}

// Applique la cellule source depuis le snapshot d'objet en attente
// (dword_1822F08.. = g_Client.pendingItem), sans passer par le payload.
inline void WriteSrcCellFromPending() {
    g_Client.inv.At(static_cast<uint32_t>(g_Client.pendingMoveRow),
                    static_cast<uint32_t>(g_Client.pendingMoveCol)) = g_Client.pendingItem;
}

// Vide la cellule d'ÉCHANGE/cible du move — [384*dword_1822EDC + 6*dword_1822EF4].
// Gardée sur >=0 : après reset ces globals valent -1 (ne pas écraser une vraie case).
inline void ClearExchangeCell() {
    const int32_t r = g_Client.Var(0x1822EDC);
    const int32_t c = g_Client.Var(0x1822EF4);
    if (r >= 0 && c >= 0)
        g_Client.inv.ClearCell(static_cast<uint32_t>(r), static_cast<uint32_t>(c));
}

// Réinitialise l'état de déplacement en attente : g_PendingMove_SrcRow0 = -1 et
// dword_1822EDC/EE0/EE4 = -1 (destinations/scratch du move, longue traîne fidèle).
inline void ResetPendingMove() {
    g_Client.pendingMoveRow = -1;
    g_Client.Var(0x1822EDC) = -1;
    g_Client.Var(0x1822EE0) = -1;
    g_Client.Var(0x1822EE4) = -1;
}

// --- Manipulation d'octets de la durabilité bit-packée (helpers Bits_* d'origine) ---
inline uint32_t PackByte012(uint32_t b0, uint32_t b1, uint32_t b2) {
    return (b0 & 0xFFu) | ((b1 & 0xFFu) << 8) | ((b2 & 0xFFu) << 16);
}
inline uint32_t SetByte2(uint32_t x, uint32_t v) { return (x & 0xFF00FFFFu) | ((v & 0xFFu) << 16); }
inline uint32_t SetByte3(uint32_t x, uint32_t v) { return (x & 0x00FFFFFFu) | ((v & 0xFFu) << 24); }
inline uint32_t AddByte0(uint32_t x, uint32_t v) { return (x & 0xFFFFFF00u) | (((x & 0xFFu) + v) & 0xFFu); }
inline uint32_t AddByte1(uint32_t x, uint32_t v) {
    return (x & 0xFFFF00FFu) | (((((x >> 8) & 0xFFu) + v) & 0xFFu) << 8);
}
inline uint32_t AddByte2(uint32_t x, uint32_t v) {
    return (x & 0xFF00FFFFu) | (((((x >> 16) & 0xFFu) + v) & 0xFFu) << 16);
}
inline uint32_t ClearByte0(uint32_t x)  { return x & 0xFFFFFF00u; }
inline uint32_t ClearByte1(uint32_t x)  { return x & 0xFFFF00FFu; }
inline uint32_t ClearByte12(uint32_t x) { return x & 0xFF0000FFu; }

// --- Décodage des indices compactés (tables base-10 / base-100 codées en dur) -----
inline uint32_t Dig10(uint32_t packed, uint32_t i) {
    static const uint32_t m[8] = {1u, 10u, 100u, 1000u, 10000u, 100000u, 1000000u, 10000000u};
    return (packed / m[i & 7u]) % 10u;
}
inline uint32_t Dig100(uint32_t packed, uint32_t i) {
    static const uint32_t m[4] = {1u, 100u, 10000u, 1000000u};
    return (packed / m[i & 3u]) % 100u;
}
} // namespace

namespace ts2::net {

void RegisterInvDispatchHandlers(NetSystem& sys) {
    using namespace game;   // g_Client, g_World, Str()

    // 0x1b LegacyItemUpgradeResult (Pkt_ItemUpgradeResult, ea=0x488DE0) — trou de couverture
    // comblé (cf. Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md). switch resultCode 0..7
    // (0/1=succès, 2=échec, 3/6=dégradation, 5/7=autre — Docs/TS2_PROTOCOL_SPEC.md #0x1b).
    // Effets confirmés par la spec : coût en or toujours débité, delta de niveau fondu dans
    // l'octet 0 de la durabilité (Bits_AddByte0) sur succès, cellule réécrite depuis le
    // snapshot pending, move réinitialisé.
    OnPacket<LegacyItemUpgradeResult>(sys, 0x1b, [](const LegacyItemUpgradeResult& p) {
        g_GmCmdCooldownLatch = 0;
        g_Client.inv.currency -= p.cost;   // dword_16732AC
        // TODO(cost) : dword_1673180 (monnaie secondaire) -= 50 pour la catégorie "arme" —
        //   dépend du type d'objet (MobDb_GetEntry), non déductible de ce payload seul.
        if (p.resultCode == 0 || p.resultCode == 1) {
            g_Client.pendingItem.color = AddByte0(g_Client.pendingItem.color, p.newLevelDelta);
            WriteSrcCellFromPending();
        }
        ResetPendingMove();
        // TODO(msg) : StrTable005_Get(222/223/224/1399/1401) selon resultCode — mapping exact
        //   des 8 cas non confirmé par la spec.
        g_Client.msg.System(Str(222));
    });

    // 0x1c LegacyItemRefineResult (Pkt_ItemRefineResult, ea=0x48A530) — trou de couverture
    // comblé. switch resultCode 0..3 (0=succès, 1=succès+2e objet, 2=échec, 3=autre —
    // Docs/TS2_PROTOCOL_SPEC.md #0x1c). Même forme que 0x7c ItemRefineResult ci-dessous
    // (Bits_AddByte1(dword_1822F18,1) sur succès, messages StrTable005 222/223/224).
    OnPacket<LegacyItemRefineResult>(sys, 0x1c, [](const LegacyItemRefineResult& p) {
        g_GmCmdCooldownLatch = 0;
        g_Client.inv.currency -= p.cost;   // dword_16732AC
        if (p.resultCode == 0) {
            g_Client.pendingItem.color = AddByte1(g_Client.pendingItem.color, 1);
            WriteSrcCellFromPending();
            ResetPendingMove();
            g_Client.msg.System(Str(222));
        } else if (p.resultCode == 1) {
            g_Client.pendingItem.color = AddByte1(g_Client.pendingItem.color, 1);
            WriteSrcCellFromPending();
            // TODO(item) : un 2e objet est également écrit dans la grille AUX
            //   (dword_1674AB8..AC0) — layout non confirmé par ce payload de 8 o seul.
            ResetPendingMove();
            g_Client.msg.System(Str(223));
        } else {
            ResetPendingMove();
            g_Client.msg.System(Str(224));
        }
    });

    // 0x75 ItemEnchantDispatch — résultat d'enchantement par paliers (tier = code%100).
    // Succès (status==0) : écrit les 3 aux (g_InvAux/1674ABC/1674AC0), réécrit la cellule
    // objet depuis le snapshot pending, reset du move. Message selon tier/status.
    OnPacket<ItemEnchantDispatch>(sys, 0x75, [](const ItemEnchantDispatch& p) {
        g_GmCmdCooldownLatch = 0;
        if (p.status == 0) {
            g_Client.inv.aux0 = p.aux0;   // g_InvAux
            g_Client.inv.aux1 = p.aux1;   // dword_1674ABC
            g_Client.inv.aux2 = p.aux2;   // dword_1674AC0
            WriteSrcCellFromPending();     // objet depuis dword_1822F08..
            ResetPendingMove();
            // TODO(cost) : décrément g_Currency dépendant du palier (tier 5 : 10000..30000
            //   via Skill_UnpackTreeNodes(aux0,aux1,aux2)) ; montant exact non documenté.
        }
        // TODO(msg) : StrTable005 1771..1784/1799/1802/871/223/2740 selon tier (code%100)
        //   et status (0=succès, 1..3=échecs) ; sélection exacte non documentée.
        g_Client.msg.System(Str(1771));
    });

    // 0x77 InventoryBulkLoad — chargement en masse d'objets (indices compactés).
    // count = header%1000 (<=8). Décode ligne (base-10), colonne & position (base-100)
    // puis écrit chaque cellule d'inventaire.
    OnPacket<InventoryBulkLoad>(sys, 0x77, [](const InventoryBulkLoad& p) {
        const uint32_t count = p.header % 1000u;
        for (uint32_t i = 0; i < count && i < 8u; ++i) {
            const uint32_t row = Dig10(p.rowPacked, i);
            const uint32_t col = (i < 4u) ? Dig100(p.colPackedA, i) : Dig100(p.colPackedB, i - 4u);
            const uint32_t pos = (i < 4u) ? Dig100(p.posPackedA, i) : Dig100(p.posPackedB, i - 4u);
            // TODO(item) : Count/Durability dépendent du type d'objet (MobDb_GetEntry) ;
            //   voie par défaut = Bits_PackByte012(durPacked). Count = 0 par défaut.
            g_Client.inv.Set(row, col, p.itemIds[i], pos % 8u, pos / 8u, 0,
                             PackByte012(p.durPacked, p.durPacked >> 8, p.durPacked >> 16), 0);
        }
        // TODO(msg) : header/1000 pilote str1849/1788 ou MobDb_GetEntry/str2999.
    });

    // 0x7c ItemRefineResult — raffinage d'objet (données objet depuis le snapshot pending).
    OnPacket<ItemRefineResult>(sys, 0x7c, [](const ItemRefineResult& p) {
        g_GmCmdCooldownLatch = 0;
        if (p.status == 0) {
            g_Client.inv.currency -= p.goldCost;
            g_Client.pendingItem.color = AddByte2(g_Client.pendingItem.color, p.attribDelta); // durab. octet2
            WriteSrcCellFromPending();
            ResetPendingMove();
            g_Client.msg.System(Str(222));
        } else if (p.status == 1) {
            g_Client.inv.currency -= p.goldCost;
            // TODO : Bits_AddByte2 conditionné à Item_GetAttribByte2>0 (MobDb, non modélisé).
            g_Client.pendingItem.color = AddByte2(g_Client.pendingItem.color, p.attribDelta);
            WriteSrcCellFromPending();
            ResetPendingMove();
            g_Client.msg.System(Str(223));
        } else if (p.status == 2) {
            g_Client.inv.currency -= p.goldCost;
            ResetPendingMove();
            g_Client.msg.System(Str(224));
        }
    });

    // 0x83 PlayerEquipVisual — 7 chaînes d'apparence d'équipement du bloc élément courant.
    // Disposition : 4 éléments * 91 o ; 91 = 7 slots * 13 o. Sélecteur = g_LocalElement.
    OnPacket<PlayerEquipVisual>(sys, 0x83, [](const PlayerEquipVisual& p) {
        const int element = g_World.self.element;                 // g_LocalElement 0x1673194
        const int base = 91 * ((element >= 0 && element < 4) ? element : 0);
        for (int slot = 0; slot < 7; ++slot) {
            const char* s = reinterpret_cast<const char*>(&p.visual[base + 13 * slot]);
            std::string name(s, strnlen(s, 13));
            (void)name;
            // TODO(state) : Str_ClearNameSlot(slot, name) — applique le nom d'apparence ;
            //   le modèle d'apparence visuelle d'équipement n'est pas (encore) dans g_Client.
        }
    });

    // 0x8d BulkItemConsume — consommation en masse : remboursement + pose de cellules.
    // status = code%1000, nb = code/1000. status!=0 -> message d'erreur (log artisan).
    OnPacket<BulkItemConsume>(sys, 0x8d, [](const BulkItemConsume& p) {
        g_GmCmdCooldownLatch = 0;
        const uint32_t status = p.code % 1000u;
        const uint32_t nb     = p.code / 1000u;
        if (status != 0) {
            static const int err[13] = {2190, 214, 871, 2174, 686, 2227, 2223,
                                        117, 2224, 2225, 2226, 2237, 2822};
            if (status >= 1u && status <= 13u) g_Client.msg.System(Str(err[status - 1u]));
            return;
        }
        const int32_t refund = static_cast<int32_t>(nb * p.unitPrice);
        switch (p.currencyType) {          // remboursement selon la monnaie
            case 1: g_Client.Var(0x16756F8) -= refund; break;  // dword_16756F8
            case 2: g_Client.Var(0x167478C) -= refund; break;  // dword_167478C
            case 3: g_Client.Var(0x1674790) -= refund; break;  // dword_1674790
            default: break;
        }
        for (uint32_t i = 0; i < nb && i < 8u; ++i) {
            const uint32_t row  = Dig10(p.rowPack, i);
            const uint32_t col  = (i < 4u) ? Dig100(p.colPackA, i)  : Dig100(p.colPackB, i - 4u);
            const uint32_t grid = (i < 4u) ? Dig100(p.gridPackA, i) : Dig100(p.gridPackB, i - 4u);
            g_Client.inv.Set(row, col, p.itemIds[i], grid % 8u, grid / 8u, 0, 0, 0);
        }
        g_Client.msg.System(Str(681));
    });

    // 0x95 ItemBatchUpdate — maj groupée de cellules via indices packés (base-10/100).
    OnPacket<ItemBatchUpdate>(sys, 0x95, [](const ItemBatchUpdate& p) {
        const uint32_t subcode = p.header % 1000u;
        const uint32_t count   = p.header / 1000u;
        if (subcode == 0) {
            for (uint32_t i = 0; i < count && i < 8u; ++i) {
                const uint32_t row = Dig10(p.rowPacked, i);
                const uint32_t col = (i < 4u) ? Dig100(p.colPackedLo, i) : Dig100(p.colPackedHi, i - 4u);
                const uint32_t pos = (i < 4u) ? Dig100(p.posPackedLo, i) : Dig100(p.posPackedHi, i - 4u);
                g_Client.inv.Set(row, col, p.itemIds[i], pos % 8u, pos / 8u, 0, 0, 0);
            }
            g_Client.msg.System(Str(2170));
        } else if (subcode == 1) {
            g_Client.msg.System(Str(2169));
        } else if (subcode == 2) {
            g_Client.msg.System(Str(117));
        } else if (subcode == 3) {
            g_Client.msg.System(Str(2249));
        }
    });

    // 0x97 MultiItemRemove — retrait de plusieurs cellules (jusqu'à 5, coords base-100).
    OnPacket<MultiItemRemove>(sys, 0x97, [](const MultiItemRemove& p) {
        if (p.resultCode == 0) {
            const uint32_t n = p.count + 1u;      // boucle i<count+1
            for (uint32_t k = 0; k < n && k < 5u; ++k) {
                const uint32_t row = (k < 4u) ? Dig100(p.rowPackedA, k) : (p.rowPackedB % 100u);
                const uint32_t col = (k < 4u) ? Dig100(p.colPackedA, k) : (p.colPackedB % 100u);
                g_Client.inv.ClearCell(row, col);
            }
            g_Client.msg.System(Str(2259));
        } else if (p.resultCode == 1) {
            g_Client.msg.System(Str(2246));
        } else if (p.resultCode == 2) {
            g_Client.msg.System(Str(2247));
        }
    });

    // 0x9b ItemSocketResult — résultat de sertissage : msgVariant = status/100, cas = status%100.
    OnPacket<ItemSocketResult>(sys, 0x9b, [](const ItemSocketResult& p) {
        g_GmCmdCooldownLatch = 0;
        const uint32_t msgVariant = p.status / 100u;
        const uint32_t branch     = p.status % 100u;
        if (branch == 0) {
            g_Client.Var(0x1822F20) = static_cast<int32_t>(p.socket0);   // valeur gemme/sertissage 0
            g_Client.Var(0x1822F24) = static_cast<int32_t>(p.socket1);   // 1
            g_Client.Var(0x1822F28) = static_cast<int32_t>(p.socket2);   // 2
            WriteSrcCellFromPending();    // objet en attente -> cellule source
            ClearExchangeCell();          // vide la cellule d'échange (dword_1822EDC/EF4)
            ResetPendingMove();
            g_Client.msg.System(Str(static_cast<int>(msgVariant + 1771u)));
        } else if (branch == 1) {
            WriteSrcCellFromPending();
            ClearExchangeCell();
            ResetPendingMove();
            g_Client.msg.System(Str(2068));
        }
    });

    // 0xa8 ItemUpgradeResult — amélioration d'objet : cellule depuis le payload au slot
    // pending, coût 100 or. status -1/0/1 = variantes de succès (messages distincts).
    OnPacket<ItemUpgradeResult>(sys, 0xa8, [](const ItemUpgradeResult& p) {
        if (p.status == 0xFFFFFFFFu || p.status == 0 || p.status == 1) {
            g_GmCmdCooldownLatch = 0;
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.instanceSerial);
            ResetPendingMove();
            g_Client.inv.currency -= 100;    // g_Currency & dword_1687254[0]
            if (p.status == 1) {
                g_Client.inv.aux0 = g_Client.inv.aux1 = g_Client.inv.aux2 = 0; // vide g_InvAux/1674ABC/1674AC0
                g_Client.msg.System(Str(730));
            } else if (p.status == 0xFFFFFFFFu) {
                g_Client.msg.System(Str(2310));
            } else {
                g_Client.msg.System(Str(730));
            }
        }
    });

    // 0xa9 ItemFuseResult — fusion de deux objets (source depuis le snapshot pending).
    // status 0 : modifie la durabilité source selon subMode + vide la cellule cible.
    OnPacket<ItemFuseResult>(sys, 0xa9, [](const ItemFuseResult& p) {
        g_GmCmdCooldownLatch = 0;
        // Requiert MobDb_GetEntry(dword_1822F08) & MobDb_GetEntry(dword_1822F2C) (non modélisés).
        if (p.status == 0) {
            uint32_t& dur = g_Client.pendingItem.color;   // durabilité bit-packée (dword_1822F18)
            switch (p.subMode) {
                case 1:              dur = AddByte0(dur, p.aux0); break;
                case 2: case 11:     dur = AddByte1(dur, p.aux1); break;
                case 3:              dur = ClearByte1(ClearByte0(dur)); break;   // clear octets 0+1
                case 4:              dur = SetByte2(dur, p.aux2); break;
                case 5:              dur = ClearByte0(dur); break;
                case 6: case 12:     dur = ClearByte1(dur); break;
                default: break;
            }
            WriteSrcCellFromPending();
            ClearExchangeCell();      // vide la cellule cible [384*dword_1822EDC + 6*dword_1822EF4]
            ResetPendingMove();
            g_Client.msg.System(Str(222));
        } else if (p.status == 1) {
            WriteSrcCellFromPending();     // cellule source depuis pending
            // TODO(state) : cellule cible depuis le snapshot pending dword_1822F2C.. (non modélisé).
            ResetPendingMove();
            // TODO(msg) : StrTable005 2508/2509/2510/2551/2552 selon subMode (1..6, 11, 12).
            g_Client.msg.System(Str(2508));
        }
    });

    // 0xab ItemSocketDispatch — MÉGA-DISPATCHER sertissage/gemmes (switch resultCode 0..4).
    // Retranche cost de l'argent puis écrit/déplace/efface des cellules d'inventaire.
    OnPacket<ItemSocketDispatch>(sys, 0xab, [](const ItemSocketDispatch& p) {
        g_Client.inv.currency -= p.cost;   // g_Currency & dword_1687254[0]
        switch (p.resultCode) {
            case 0:
                // Écrit le snapshot (payload) dans la cellule source (slot pending).
                WriteSrcCell(p.itemSnapshot[0], p.itemSnapshot[1], p.itemSnapshot[2],
                             p.itemSnapshot[3], p.itemSnapshot[4], p.itemSnapshot[5]);
                // TODO(state) : si actionType in {1,6,7}, recalc des noeuds de skill
                //   (Skill_UnpackTreeNodes) dans g_InvAux.
                // TODO(msg) : StrTable005 222/3390/2710 selon actionType.
                g_Client.msg.System(Str(222));
                break;
            case 1:
                WriteSrcCellFromPending();     // déplace l'objet en attente vers le slot
                ClearExchangeCell();
                ResetPendingMove();
                g_Client.msg.System(Str(2622));
                break;
            case 2:
            case 3:
            case 4:
                // Efface 1/2/3 cellules (source pending + dword_1822EDC/EE0/EE4).
                g_Client.inv.ClearCell(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                       static_cast<uint32_t>(g_Client.pendingMoveCol));
                if (p.resultCode >= 3) ClearExchangeCell();
                // TODO(state) : cellule additionnelle dword_1822EE0 (resultCode 4) non modélisée.
                ResetPendingMove();
                g_Client.msg.System(Str(p.resultCode == 2 ? 2622 : 3389));
                break;
            default: break;
        }
    });

    // 0xac ItemRefineDispatch — MÉGA-DISPATCHER raffinage/amélioration (~7.7 Ko).
    // Seul l'en-tête (op/a/b/c) est lu ; toute la maj de grille utilise les globals
    // pending. Effets communs fidèles : latch remis à 0 + reset du move en attente.
    OnPacket<ItemRefineDispatch>(sys, 0xac, [](const ItemRefineDispatch& p) {
        g_GmCmdCooldownLatch = 0;
        (void)p;   // op/a/b/c pilotent le switch d'origine (à décoder).
        ResetPendingMove();
        // TODO(dispatch) : réimplémenter chaque sous-cas de `op` (raffinage succès/échec,
        //   sertissage, gemmes) : réécrit 1 à 2 cellules g_InvMain/g_InvGrid_* depuis le
        //   snapshot pending (dword_1822F08..4C), ajuste g_InvWeight, messages StrTable005.
    });

    // 0xaf ItemEnhanceResult — amélioration/enchant (opère sur l'objet en attente).
    OnPacket<ItemEnhanceResult>(sys, 0xaf, [](const ItemEnhanceResult& p) {
        if (p.resultCode == 1) {           // succès : monte le niveau
            g_Client.inv.currency -= p.cost;
            uint32_t& dur = g_Client.pendingItem.color;         // durabilité (dword_1822F18)
            dur = AddByte1(SetByte2(dur, p.enhanceByte), 1);    // octet2 = enhanceByte, +1 niveau (octet1)
            WriteSrcCellFromPending();
            ResetPendingMove();
            g_Client.msg.System(Str(222));
        } else if (p.resultCode == 2) {    // échec : reset des octets 1 et 2 (niveau)
            g_Client.inv.currency -= p.cost;
            g_Client.pendingItem.color = ClearByte12(g_Client.pendingItem.color);
            WriteSrcCellFromPending();
            ResetPendingMove();
            g_Client.msg.System(Str(2680));
        }
    });

    // 0xb0 ItemEnhanceResult2 — variante d'amélioration (durabilité repackée sur 4 octets).
    OnPacket<ItemEnhanceResult2>(sys, 0xb0, [](const ItemEnhanceResult2& p) {
        g_GmCmdCooldownLatch = 0;
        const uint32_t durability =
            SetByte3(PackByte012(p.statByte0, p.statByte1, p.statByte2), p.statByte3);
        if (p.resultCode == 1) {                                   // succès amélioration
            g_Client.pendingItem.color = durability;              // durabilité (dword_1822F18)
            WriteSrcCellFromPending();
            ResetPendingMove();
            g_Client.msg.System(Str(2694));
        } else if (p.resultCode >= 10 && p.resultCode <= 14) {     // branche transmutation
            g_Client.pendingItem.itemId = p.newItemId;            // dword_1822F08 = newItemId
            g_Client.pendingItem.color  = durability;
            g_Client.inv.currency -= 1000;                        // g_Currency & dword_1687254[0]
            WriteSrcCellFromPending();
            ResetPendingMove();
            g_Client.msg.System(Str(2759));
        }
    });

    // 0xb3 ItemDropResult — résultat de lâcher d'objet : pose une cellule (coords du payload).
    OnPacket<ItemDropResult>(sys, 0xb3, [](const ItemDropResult& p) {
        g_GmCmdCooldownLatch = 0;
        if (p.status == 0) {
            g_Client.Var(0x1675644) = static_cast<int32_t>(p.goldOrValue);  // dword_1675644
            g_Client.inv.Set(p.invRow, p.invCol,
                             p.itemCell[0], p.itemCell[1], p.itemCell[2],
                             p.itemCell[3], p.itemCell[4], p.itemCell[5]);
            g_Client.msg.System(Str(681));
        }
    });

    // 0xb4 StatSyncDispatch — sync argent/poids/compteur (inconditionnelle) + résultat
    // d'action inventaire (switch resultCode 0..3, objet depuis le snapshot pending).
    OnPacket<StatSyncDispatch>(sys, 0xb4, [](const StatSyncDispatch& p) {
        g_Client.inv.weight     = p.invWeight;                      // g_InvWeight
        g_Client.inv.currency   = p.currency;                       // g_Currency & dword_1687254[0]
        g_Client.Var(0x16746E8) = static_cast<int32_t>(p.counter);  // dword_16746E8
        switch (p.resultCode) {
            case 0:
            case 1:
            case 2:
                g_Client.pendingItem.color = p.durability;   // durabilité imposée (dword_1822F18)
                WriteSrcCellFromPending();
                ClearExchangeCell();                         // vide l'ancien slot (dword_1822EDC/EF4)
                ResetPendingMove();
                g_Client.msg.System(Str(p.resultCode == 0 ? 2748 : (p.resultCode == 1 ? 2749 : 654)));
                break;
            case 3:
                g_Client.inv.ClearCell(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                       static_cast<uint32_t>(g_Client.pendingMoveCol));
                ClearExchangeCell();
                ResetPendingMove();
                g_Client.msg.System(Str(224));
                break;
            default: break;
        }
    });
}

} // namespace ts2::net

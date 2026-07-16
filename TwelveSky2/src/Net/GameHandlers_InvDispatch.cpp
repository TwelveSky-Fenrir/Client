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
//
// ============================================================================
// SÉMANTIQUE DE LA CELLULE SOURCE — carte re-dérivée en Passe 4 (W8), preuve au
// niveau INSTRUCTION (recherche exhaustive de `add exx, ds:dword_1822F14|F38`
// sur [0x488000, 0x4b4300]). Deux familles DISTINCTES, à ne surtout pas fondre :
//
//   ACCUMULATION `g_InvGrid_Count[...] += dword_1822F14` (load / add / store) :
//     0x1b (0x488f4a…), 0x1c (0x48a692…), 0x7c (0x4a9952, 0x4a9b84),
//     0xa9 (0x4ae9f3, 0x4aecb2), 0xab cas 1 (0x4af646), 0xac (13 corps, 0x4b0b43…),
//     0xaf (0x4b2934, 0x4b2b63), 0xb0 (0x4b2e9b, 0x4b325c), 0xb4 cas 0/1/2 (0x4b370f…)
//
//   AFFECTATION `g_InvGrid_Count[...] = dword_1822F14` (load / store, AUCUN add) :
//     0x75 (0x4a761b, 0x4a77e5, 0x4a7a07, 0x4a7c2c, 0x4a7e6b — 5/5 sites),
//     0x9b (0x4acd3c, 0x4acfd3)
//
// D'où DEUX helpers (AccumSrcCellFromPending / WriteSrcCellFromPending). L'ancien
// helper unique en `=` corrompait silencieusement la pile des 9 opcodes accumulants.
//
// PROFONDEUR DE RESET du move en attente — carte re-dérivée de la même façon
// (recherche de `mov ds:dword_1822EE0|EE4, 0FFFFFFFFh` sur [0x480000, 0x4c0000]) :
//   4 champs (ED8+EDC+EE0+EE4) : 0xa8 (0x4ae419/23/2d), 0xb0 (0x4b30af/b9/c3),
//                                0xad Net_OnItemSlotRefresh (hors de ce module)
//   2 champs (ED8+EDC seuls)   : 0x1b, 0x1c, 0x75, 0x7c, 0x9b, 0xa9, 0xac, 0xaf, 0xb4
//   0xab : cas 0 = 2 + conditionnel actionType 12/13 ; 1 = 2 ; 2 = 2 ; 3 = 3 ; 4 = 4
// ============================================================================
#include "Net/GameHandlers.h"
#include "Game/ClientRuntime.h"
#include "Game/GameState.h"    // game::g_World (self.element = g_LocalElement)
#include "Game/GameDatabase.h" // game::GetItemInfo — ≡ MobDb_GetEntry(mITEM,id) 0x4C3C00
#include "Game/SkillSystem.h"  // game::Skill_UnpackTreeNodes — ≡ 0x54C090
#include "Game/BitPacking.h"   // game::Bits_SetByteN — ≡ 0x54BF30
#include "Net/ClientState.h"   // ts2::net::g_GmCmdCooldownLatch
#include <string>
#include <cstring>
#include <cstdint>

namespace {
using namespace ts2::game;

// --- Résolution d'objet — garde MobDb_GetEntry(mITEM, id) 0x4C3C00 -----------
// L'original : `if (id < 1 || id > count) return 0; if (!record[0]) return 0;`.
// game::GetItemInfo (Game/GameDatabase.cpp, lookup 1-based `record(id-1)`) a la
// MÊME sémantique — y compris le rejet total quand la table n'est pas chargée
// (count == 0), exactement comme l'original. Une garde nulle => le handler ne
// fait RIEN (ni écriture de cellule, ni message) : c'est le comportement du binaire.
inline const ItemInfo* PendingItemDef() {
    return GetItemInfo(g_Client.pendingItem.itemId);   // dword_1822F08[0]
}

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
// (dword_1822F08.. = g_Client.pendingItem) — variante AFFECTATION du compteur.
// Ancres : 0x75 EA 0x4a761b (`mov ecx, ds:dword_1822F14` / `mov ds:g_InvGrid_Count[edx+eax], ecx`,
// AUCUN `add` intercalé) ; 0x9b EA 0x4acd3c. RÉSERVÉ à 0x75 et 0x9b.
inline void WriteSrcCellFromPending() {
    g_Client.inv.At(static_cast<uint32_t>(g_Client.pendingMoveRow),
                    static_cast<uint32_t>(g_Client.pendingMoveCol)) = g_Client.pendingItem;
}

// Idem mais le compteur de pile est ACCUMULÉ, pas écrasé — variante majoritaire.
// Ancre canonique : 0xb4 EA 0x4b3708 `mov edx, ds:g_InvGrid_Count[eax+ecx]` /
// 0x4b370f `add edx, ds:dword_1822F14` / 0x4b3729 `mov ds:g_InvGrid_Count[ecx+eax], edx`.
// Les 5 autres champs restent en affectation pure (cf. 0x4b36ab..0x4b376b).
inline void AccumSrcCellFromPending() {
    InvCell& e = g_Client.inv.At(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                 static_cast<uint32_t>(g_Client.pendingMoveCol));
    const InvCell& s = g_Client.pendingItem;
    e.itemId     = s.itemId;      // g_InvMain             <- dword_1822F08
    e.gridX      = s.gridX;       // g_InvGrid_GridX       <- dword_1822F0C
    e.gridY      = s.gridY;       // g_InvGrid_GridY       <- dword_1822F10
    e.flag      += s.flag;        // g_InvGrid_Count       += dword_1822F14  <-- ACCUMULATION
    e.color      = s.color;       // g_InvGrid_Durability  <- dword_1822F18
    e.durability = s.durability;  // g_InvGrid_InstanceSerial <- dword_1822F1C
}

// Vide la cellule d'ÉCHANGE/cible du move — [384*dword_1822EDC + 6*dword_1822EF4].
// Gardée sur >=0 : après reset ces globals valent -1 (ne pas écraser une vraie case).
inline void ClearExchangeCell() {
    const int32_t r = g_Client.Var(0x1822EDC);
    const int32_t c = g_Client.Var(0x1822EF4);
    if (r >= 0 && c >= 0)
        g_Client.inv.ClearCell(static_cast<uint32_t>(r), static_cast<uint32_t>(c));
}

// Vide une cellule de move désignée par un couple (globalLigne, globalColonne) —
// sert aux 3e/4e cellules de 0xab (EE0/EF8 puis EE4/EFC). Même garde que
// ClearExchangeCell (indispensable : Var() vaut 0 par défaut, pas -1).
inline void ClearCellGuarded(uint32_t rowAddr, uint32_t colAddr) {
    const int32_t r = g_Client.Var(rowAddr);
    const int32_t c = g_Client.Var(colAddr);
    if (r >= 0 && c >= 0)
        g_Client.inv.ClearCell(static_cast<uint32_t>(r), static_cast<uint32_t>(c));
}

// --- Tableaux « aux » PAR CELLULE -------------------------------------------
// Le binaire n'a PAS trois scalaires globaux : il a TROIS TABLEAUX indexés par
// cellule. Ancre Net_OnItemEnchantDispatch 0x4A7410 :
//   0x4A7535 `imul ecx, 300h`  -> ligne   = 0x300 o = 192 dwords (64 col x 3 dw)
//   0x4A7541 `imul edx, 0Ch`   -> cellule = 0x0C  o =   3 dwords
//   0x4A7547 g_InvAux[ecx+edx] / 0x4A7566 dword_1674ABC[..] / 0x4A7585 dword_1674AC0[..]
// Le commentaire de tête IDA de g_InvAux 0x1674AB8 le dit mot pour mot :
// « inventaire auxiliaire SoA, cellule 0x0C (3 dw), ligne 0x300. base+row*0x300+col*0x0C ».
//
// InventoryState (Game/ClientRuntime.h:85 — NON possédé par ce front) n'expose que
// trois scalaires GLOBAUX aux0/aux1/aux2 : la cellule courante y écrase celle de
// TOUTE autre cellule (128 triplets fondus en 1). On passe donc par l'échappatoire
// Var(adresseOrigine) bénie par le header lui-même (ClientRuntime.h:10-12), selon le
// précédent EXACT déjà en place dans Net/GameHandlers_InvCells.cpp:71-105 et
// Game/WarehouseSystem.cpp:13-20 — on réutilise le modèle, on n'en invente pas un 3e.
constexpr uint32_t kInvAuxBase  = 0x1674AB8; // g_InvAux
constexpr uint32_t kInvAux1Base = 0x1674ABC; // dword_1674ABC
constexpr uint32_t kInvAux2Base = 0x1674AC0; // dword_1674AC0

// Décalage en OCTETS de la cellule (row,col) dans les 3 tableaux aux.
// NON GARDÉ sur row/col < 0 — contrairement à InvCells.cpp:84/110 — parce que le
// handler 0xa8 s'appuie sur un OOB D'ORIGINE qu'il faut reproduire (cf. WriteAuxAt).
// L'arithmétique non signée (complément à deux) reproduit exactement l'adresse
// calculée par le `imul` signé du binaire une fois ajoutée à la base.
inline uint32_t InvAuxOff(int32_t row, int32_t col) {
    return 4u * static_cast<uint32_t>(192 * row + 3 * col);
}

// Écrit les 3 aux de la cellule (row,col). Les 3 écritures partagent le même index,
// recalculé à chaque fois par le binaire (0x4A7535/0x4A7554/0x4A7573).
inline void WriteAuxAt(int32_t row, int32_t col, int32_t a0, int32_t a1, int32_t a2) {
    const uint32_t off = InvAuxOff(row, col);
    g_Client.Var(kInvAuxBase  + off) = a0;
    g_Client.Var(kInvAux1Base + off) = a1;
    g_Client.Var(kInvAux2Base + off) = a2;
}

// Réinitialise l'état de déplacement en attente. `depth` = nombre de champs remis
// à -1 dans l'ordre ED8, EDC, EE0, EE4 — le binaire N'EN REMET PAS TOUJOURS 4 (voir
// la carte en tête de fichier). Défaut = 2 (ED8+EDC), le profil très majoritaire :
// ancre canonique 0xb4 EA 0x4b38ef/0x4b38f9, 0x75 EA 0x4a7664/0x4a766e.
inline void ResetPendingMove(int depth = 2) {
    g_Client.pendingMoveRow = -1;              // g_PendingMove_SrcRow0[0] = -1
    g_Client.Var(0x1822EDC) = -1;              // dword_1822EDC = -1
    if (depth >= 3) g_Client.Var(0x1822EE0) = -1;
    if (depth >= 4) g_Client.Var(0x1822EE4) = -1;
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
// Item_GetAttribByte2 0x545670 : `Crt_Memcpy(v2, &a1, 4); return v2[2];` = octet 2.
inline uint32_t GetByte2(uint32_t x) { return (x >> 16) & 0xFFu; }
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
    // Passe 4 (W8) : le compteur de pile est ACCUMULÉ — 9 sites `add exx, ds:dword_1822F14`
    // (0x488f4a, 0x48910e, 0x4893ab, 0x489648, 0x48996d, 0x489bea, 0x489e80, 0x48a0ca,
    // 0x48a347), AUCUN site en affectation. Reset = 2 champs (0x489010/0x48901a : ni EE0 ni
    // EE4 n'apparaissent dans cette fonction).
    OnPacket<LegacyItemUpgradeResult>(sys, 0x1b, [](const LegacyItemUpgradeResult& p) {
        g_GmCmdCooldownLatch = 0;
        g_Client.inv.currency -= p.cost;   // dword_16732AC
        // TODO(cost) : dword_1673180 (monnaie secondaire) -= 50 pour la catégorie "arme" —
        //   dépend du type d'objet (MobDb_GetEntry), non déductible de ce payload seul.
        if (p.resultCode == 0 || p.resultCode == 1) {
            g_Client.pendingItem.color = AddByte0(g_Client.pendingItem.color, p.newLevelDelta);
            AccumSrcCellFromPending();   // 0x488f4a (`add`, pas `mov`)
        }
        ResetPendingMove();              // 2 champs — 0x489010/0x48901a
        // TODO(msg) : StrTable005_Get(222/223/224/1399/1401) selon resultCode — mapping exact
        //   des 8 cas non confirmé par la spec.
        g_Client.msg.System(Str(222));
    });

    // 0x1c LegacyItemRefineResult (Pkt_ItemRefineResult, ea=0x48A530) — trou de couverture
    // comblé. switch resultCode 0..3 (0=succès, 1=succès+2e objet, 2=échec, 3=autre —
    // Docs/TS2_PROTOCOL_SPEC.md #0x1c). Même forme que 0x7c ItemRefineResult ci-dessous
    // (Bits_AddByte1(dword_1822F18,1) sur succès, messages StrTable005 222/223/224).
    // Passe 4 (W8) : compteur ACCUMULÉ — 0x48a692, 0x48a894, 0x48abdb, 0x48addf (+ 0x48a976
    // sur dword_1822F38 = 2e snapshot, non modélisé). Reset = 2 champs (0x48a758/0x48a762).
    OnPacket<LegacyItemRefineResult>(sys, 0x1c, [](const LegacyItemRefineResult& p) {
        g_GmCmdCooldownLatch = 0;
        g_Client.inv.currency -= p.cost;   // dword_16732AC
        if (p.resultCode == 0) {
            g_Client.pendingItem.color = AddByte1(g_Client.pendingItem.color, 1);
            AccumSrcCellFromPending();     // 0x48a692 (`add`)
            ResetPendingMove();            // 2 champs
            g_Client.msg.System(Str(222));
        } else if (p.resultCode == 1) {
            g_Client.pendingItem.color = AddByte1(g_Client.pendingItem.color, 1);
            AccumSrcCellFromPending();     // 0x48a894 (`add`)
            // TODO(model) : un 2e objet est également accumulé dans la cellule d'échange via
            //   le 2e snapshot pending dword_1822F2C..F4C (compteur dword_1822F38, EA 0x48a976).
            //   Ce 2e snapshot n'est pas modélisé dans g_Client (seul dword_1822F08.. l'est).
            ResetPendingMove();            // 2 champs
            g_Client.msg.System(Str(223));
        } else {
            ResetPendingMove();            // 2 champs
            g_Client.msg.System(Str(224));
        }
    });

    // 0x75 ItemEnchantDispatch (Net_OnItemEnchantDispatch 0x4A7410) — résultat d'enchantement
    // par paliers. Sous-dispatcher à DEUX niveaux : tier = code%100 (0x4a7480, switch 0x4a74b7,
    // jumptable 1..5) x status (switch 0x4a74c8 / 0x4a7898 / 0x4a7aba).
    // Un tier hors 1..5 tombe sur `default: return result` (0x4a7f48) => AUCUN message et
    // AUCUNE écriture d'inventaire (l'ancien code émettait Str(1771) et écrivait la grille
    // pour n'importe quel tier).
    // Le compteur de pile est en AFFECTATION ici (0x4a761b/0x4a77e5/0x4a7a07/0x4a7c2c/0x4a7e6b
    // : `mov ecx, ds:dword_1822F14` suivi de `mov ds:g_InvGrid_Count[..], ecx`, sans `add`)
    // -> WriteSrcCellFromPending, PAS la variante accumulante.
    OnPacket<ItemEnchantDispatch>(sys, 0x75, [](const ItemEnchantDispatch& p) {
        g_GmCmdCooldownLatch = 0;              // 0x4a7491 — avant le switch (fidèle)
        const uint32_t tier  = p.code % 100u;  // 0x4a7480 (v32)
        const uint32_t shift = p.code / 100u;  // 0x4a748e (v38)

        // Bloc d'écriture commun aux branches status==0 des tiers 1..5, scindé en deux
        // pour respecter l'ORDRE d'origine du tier 1, qui intercale le décrément de poids
        // (0x4a7598) ENTRE les écritures aux et les écritures de cellule.
        // aux : 0x4a7547/0x4a7566/0x4a7585.
        auto ApplyAux = [&]() {
            // Indexé par les globals PENDING (g_PendingMove_SrcRow0 0x1822ED8 /
            // dword_1822EF0), et NON par le row/col du paquet : le binaire lit ces
            // globals (0x4A752F/0x4A753B). Ils sont VALIDES ici — leur reset
            // (0x4A7664/0x4A766E) est POSTÉRIEUR aux 3 écritures aux.
            //   g_InvAux[192*row + 3*col]      <- v35   0x4A7547
            //   dword_1674ABC[192*row + 3*col] <- v37   0x4A7566
            //   dword_1674AC0[192*row + 3*col] <- v36   0x4A7585
            WriteAuxAt(g_Client.pendingMoveRow, g_Client.pendingMoveCol,
                       static_cast<int32_t>(p.aux0), static_cast<int32_t>(p.aux1),
                       static_cast<int32_t>(p.aux2));
        };
        // cellule : 0x4a75b8..0x4a765d (AFFECTATION) ; reset : 0x4a7664/0x4a766e (2 champs).
        auto ApplyCellAndReset = [&]() {
            WriteSrcCellFromPending();
            ResetPendingMove();           // 2 champs
        };

        switch (tier) {
        case 1:   // 0x4a74b7 case 1 — sous-switch 0x4a74c8, SANS default (status>=4 => rien)
            switch (p.status) {
            case 1:
            case 2: g_Client.msg.System(Str(1771)); break;   // 0x4a74db
            case 3: g_Client.msg.System(Str(214));  break;   // 0x4a750b
            case 0:
                ApplyAux();
                // Décrément de poids UNIQUE au tier 1 / status 0 (imm. 0x5F5E100) : absent
                // des branches status==0 des tiers 2/3/4/5.
                g_Client.inv.weight -= 100000000;            // 0x4a7598
                ApplyCellAndReset();
                g_Client.msg.System(Str(static_cast<int>(shift + 1771u)));  // 0x4a768d
                break;
            default: break;                                  // aucun default en IDA
            }
            break;

        case 2:   // 0x4a74b7 case 2
            if (p.status == 1) {
                // 0x4a76a6 `goto LABEL_11` : partage la chaîne 1778 avec le tier 3 / status 1.
                g_Client.msg.System(Str(1778));              // 0x4a76b9
            } else if (p.status == 2) {
                g_Client.msg.System(Str(1777));              // 0x4a76ea
            } else if (p.status != 0) {
                g_Client.msg.System(Str(223));               // LABEL_51 0x4a7f38
            } else {
                ApplyAux(); ApplyCellAndReset();             // 0x4a7725..0x4a7838 (PAS de poids)
                g_Client.msg.System(Str(1779));              // 0x4a7852
            }
            break;

        case 3:   // 0x4a74b7 case 3 — sous-switch 0x4a7898
            switch (p.status) {
            case 1: g_Client.msg.System(Str(1778)); break;   // 0x4a76b9 (LABEL_11)
            case 2: g_Client.msg.System(Str(1780)); break;   // 0x4a78db
            case 3: g_Client.msg.System(Str(1799)); break;   // 0x4a790c
            default:
                if (p.status) { g_Client.msg.System(Str(223)); break; }  // 0x4a792a -> LABEL_51
                ApplyAux(); ApplyCellAndReset();             // 0x4a7947..0x4a7a5a
                g_Client.msg.System(Str(1781));              // 0x4a7a75
                break;
            }
            break;

        case 4:   // 0x4a74b7 case 4 — sous-switch 0x4a7aba
            switch (p.status) {
            case 1: g_Client.msg.System(Str(1782)); break;   // 0x4a7acd
            case 2: g_Client.msg.System(Str(1783)); break;   // 0x4a7afe
            case 3: g_Client.msg.System(Str(1802)); break;   // 0x4a7b2e
            default:
                if (p.status) { g_Client.msg.System(Str(223)); break; }  // 0x4a7b4c -> LABEL_51
                ApplyAux(); ApplyCellAndReset();             // 0x4a7b6a..0x4a7c7f
                g_Client.msg.System(Str(1784));              // 0x4a7c9a
                break;
            }
            break;

        case 5:   // 0x4a74b7 case 5 — test 0x4a7ce0
            if (p.status == 0) {
                // Coût par palier d'arbre de talents. Skill_UnpackTreeNodes 0x54C090 est
                // appelé avec (v35, v37, v36) = (aux0, aux1, aux2) — EA 0x4a7d25 — et son
                // retour (octet 1 de aux0) sélectionne l'immédiat : EA 0x4a7d35/0x4a7d44/
                // 0x4a7d53/0x4a7d62/0x4a7d71.
                int nodes[5] = {0, 0, 0, 0, 0};
                const int lvl = Skill_UnpackTreeNodes(p.aux0, p.aux1, p.aux2, nodes);
                int cost = 0;                                // v31 = 0 (0x4a7d28)
                switch (lvl) {                               // switch 0x4a7d33
                case 1: cost = 10000; break;
                case 2: cost = 15000; break;
                case 3: cost = 20000; break;
                case 4: cost = 25000; break;
                case 5: cost = 30000; break;
                default: break;
                }
                // g_Currency -= v31 (0x4a7d80) ET dword_1687254[0] -= v31 (0x4a7d8e) : DEUX
                // globals dans le binaire, UN SEUL champ dans le modèle — inv.currency est
                // déclaré (ClientRuntime.h:82) comme g_Currency *et* son miroir dword_1687254[0].
                // Un second décrément sur Var(0x1687254) créerait un compteur fantôme.
                g_Client.inv.currency -= cost;
                ApplyAux(); ApplyCellAndReset();             // 0x4a7dab..0x4a7ebe
                g_Client.msg.System(Str(2740));              // 0x4a7ed9
            } else if (p.status != 1 && p.status != 2) {     // 0x4a7ef9
                if (p.status == 3) g_Client.msg.System(Str(871));   // 0x4a7f13
                else               g_Client.msg.System(Str(223));   // LABEL_51 0x4a7f38
            }
            // status 1 et 2 : le binaire retombe sur `default: return` => AUCUN message.
            break;

        default: break;   // tier hors 1..5 : `default: return result` (0x4a7f48) => rien
        }
    });

    // 0x77 InventoryBulkLoad (Net_OnInventoryBulkLoad 0x4A7F60) — chargement en masse.
    // count = header%1000 (0x4a815f), code = header/1000 (0x4a814b, switch 0x4a8184).
    OnPacket<InventoryBulkLoad>(sys, 0x77, [](const InventoryBulkLoad& p) {
        g_GmCmdCooldownLatch = 0;                  // 0x4a8133
        const uint32_t code  = p.header / 1000u;   // v13
        const uint32_t count = p.header % 1000u;   // v14

        // --- switch d'en-tête (0x4a8184) : messages + reset d'état, avant la boucle ---
        switch (code) {
        case 0:
            g_Client.msg.System(Str(1849));        // 0x4a819c
            break;
        case 1:
            g_Client.msg.System(Str(1788));        // 0x4a81c2
            g_Client.Var(0x1674780) = 0;           // 0x4a81d2 — reset d'état PUR
            break;
        case 2: case 3: case 4: case 5:
            // MobDb_GetEntry(mITEM, v13 + 807) (0x4a81f2) : si NULL, la fonction RETOURNE
            // (0x4a8204) — toute la boucle de chargement est sautée.
            if (!GetItemInfo(code + 807u)) return;
            // LABEL_5 (0x4a820b) : Crt_Vsnprintf(v22, Str(2999), record+4 = nom de l'objet)
            //   puis Snd3D + Msg (0x4a8235/0x4a826d). Le son est hors périmètre (cf. en-tête).
            g_Client.msg.System(Str(2999));        // 0x4a8235 (nom formaté dans le gabarit)
            break;
        case 6:
            if (!GetItemInfo(835u)) return;        // 0x4a8281 / 0x4a8293
            g_Client.msg.System(Str(2999));        // même LABEL_5
            break;
        case 7:
            // 0x4a830d : son seul (hors périmètre), puis LABEL_9.
            break;
        default: break;                            // `default: goto LABEL_9` — boucle seule
        }

        // --- LABEL_9 (0x4a8312) : boucle de pose des cellules ---
        // NOTE : le binaire n'a PAS de borne i<8 (`for (i=0;;++i) if (i>=v14) break;`) ; il
        //   lirait hors des tableaux v19/v26 si header%1000 > 8. On conserve le garde-fou
        //   i<8 (divergence DÉFENSIVE assumée : reproduire une lecture hors-pile n'a pas de sens).
        for (uint32_t i = 0; i < count && i < 8u; ++i) {
            const uint32_t row = Dig10(p.rowPacked, i);                                  // 0x4a8365
            const uint32_t col = (i < 4u) ? Dig100(p.colPackedA, i) : Dig100(p.colPackedB, i - 4u);
            const uint32_t pos = (i < 4u) ? Dig100(p.posPackedA, i) : Dig100(p.posPackedB, i - 4u);

            // Garde d'existence PAR OBJET : `Entry = MobDb_GetEntry(mITEM, v18[i])` (0x4a8438)
            // puis `if (Entry)` (0x4a8445) — si NULL, la cellule n'est PAS écrite du tout.
            const ItemInfo* it = GetItemInfo(p.itemIds[i]);
            if (!it) continue;

            // Count = 12 si typeCode == 2 (0x4a84dc/0x4a84f3), sinon 0 (0x4a8515).
            const uint32_t cnt = (it->typeCode == 2u) ? 12u : 0u;

            // Durabilité : switch(Entry[47]) = switch(typeCode) — 0x4a852d.
            uint32_t dur;
            switch (it->typeCode) {
            case 3:                                     dur = 0; break;  // 0x4a8544
            case 0x16:
                // 0x4a8567..0x4a8608 : Item_MeetsStatRequirement(dword_8E717C, itemId, 1, 1.0, &v8)
                //   puis `itemId == 12001 ? unk_8E718C : Crt_ftol(dword_8E717C[v8] * 0.5)`.
                // TODO(model) [ancres 0x4a8593 / 0x4a85d2 / 0x4a85ee] : les tables 0x8E717C et
                //   0x8E718C (bloc voisin de mITEM 0x8E71EC) ne sont pas modélisées côté client
                //   -> valeur non calculable ici. On pose 0, comme les autres cas spéciaux
                //   (3 / 0x1C / 0x1F / 0x20), plutôt que la voie PackByte012 qui serait fausse.
                dur = 0; break;
            case 0x1C:                                  dur = 0; break;  // 0x4a8638
            case 0x1F: case 0x20:                       dur = 0; break;  // 0x4a8678
            default:
                // Bits_PackByte012(v24, 0, 0) — EA 0x4a8685 : `push 0 / push 0 / mov eax,
                // [ebp+var_28] / push eax`, __stdcall droite-à-gauche => (durPacked, 0, 0).
                // Bits_PackByte012 0x5458C0 : v4[0]=a1, v4[1]=a2, v4[2]=a3, v4[3]=0, et a1..a3
                // sont des `char` => SEUL l'octet 0 de durPacked survit.
                dur = PackByte012(p.durPacked, 0, 0);   // 0x4a86ac
                break;
            }
            g_Client.inv.Set(row, col, p.itemIds[i], pos % 8u, pos / 8u, cnt, dur, 0);  // serial=0 (0x4a86c8)
        }
    });

    // 0x7c ItemRefineResult (Net_OnItemRefineResult 0x4A97A0) — raffinage d'objet.
    // Le latch est remis à 0 À L'INTÉRIEUR de chaque branche (0x4a9819 / 0x4a9a37 /
    // 0x4a9c6a) : un status >= 3 tombe sur `return result` et ne réarme rien.
    // Chaque branche est gardée par MobDb_GetEntry(mITEM, dword_1822F08[0]) — 0x4a982f
    // (status 0), 0x4a9a4d (status 1), 0x4a9c80 (status 2) : garde nulle => AUCUN effet
    // (ni décrément de monnaie, ni message).
    OnPacket<ItemRefineResult>(sys, 0x7c, [](const ItemRefineResult& p) {
        if (p.status == 0) {
            g_GmCmdCooldownLatch = 0;                        // 0x4a9819
            if (!PendingItemDef()) return;                   // 0x4a982f / 0x4a983b
            g_Client.inv.currency -= p.goldCost;             // 0x4a9890
            // Bits_AddByte2 INCONDITIONNEL sur cette branche (0x4a98b3).
            g_Client.pendingItem.color = AddByte2(g_Client.pendingItem.color, p.attribDelta);
            AccumSrcCellFromPending();                       // 0x4a9952 (`add`)
            ResetPendingMove();                              // 2 champs — 0x4a99fe/0x4a9a08
            g_Client.msg.System(Str(222));                   // 0x4a9a22
        } else if (p.status == 1) {
            g_GmCmdCooldownLatch = 0;                        // 0x4a9a37
            if (!PendingItemDef()) return;                   // 0x4a9a4d / 0x4a9a59
            g_Client.inv.currency -= p.goldCost;             // 0x4a9aae
            // Ici le Bits_AddByte2 est CONDITIONNÉ (différence fine vs status 0) :
            // `if (Item_GetAttribByte2(dword_1822F18[0]) > 0)` — EA 0x4a9ad0.
            if (GetByte2(g_Client.pendingItem.color) > 0)
                g_Client.pendingItem.color = AddByte2(g_Client.pendingItem.color, p.attribDelta);  // 0x4a9ae7
            AccumSrcCellFromPending();                       // 0x4a9b84 (`add`)
            ResetPendingMove();                              // 2 champs — 0x4a9c30/0x4a9c3a
            g_Client.msg.System(Str(223));                   // 0x4a9c55
        } else if (p.status == 2) {
            g_GmCmdCooldownLatch = 0;                        // 0x4a9c6a
            if (!PendingItemDef()) return;                   // 0x4a9c80 / 0x4a9c8c
            g_Client.inv.currency -= p.goldCost;             // 0x4a9ce1
            ResetPendingMove();                              // 2 champs — 0x4a9cf0/0x4a9cfa
            g_Client.msg.System(Str(224));                   // 0x4a9d14
        }
        // status >= 3 : aucune branche => rien (pas même le latch).
    });

    // 0x83 PlayerEquipVisual (Net_OnPlayerEquipVisual 0x4AA770) — 7 chaînes d'apparence
    // d'équipement du bloc élément courant. Disposition : 4 éléments * 91 o ; 91 = 7 slots
    // * 13 o. Sélecteur = g_LocalElement (0x4aa7f3).
    // Str_ClearNameSlot 0x5CCD60 relu au désassemblage : `if (slot <= 6)
    //   Crt_StringInit(this + 13*slot + 8, src)` (EA 0x5ccd6b/0x5ccd7a/0x5ccd80/0x5ccd85)
    // => la table des 7 noms vit à byte_1822730 + 8 = 0x1822738, 7 slots de 13 o.
    // On écrit donc la chaîne à son ADRESSE D'ORIGINE via Blob : la chaîne réseau->état
    // n'est plus rompue (l'ancien code calculait le nom puis faisait `(void)name`).
    OnPacket<PlayerEquipVisual>(sys, 0x83, [](const PlayerEquipVisual& p) {
        const int element = g_World.self.element;                 // g_LocalElement 0x1673194
        // Le binaire ne borne PAS g_LocalElement (91*4+78 > 368 => lecture hors pile) ;
        // garde-fou défensif conservé.
        const int base = 91 * ((element >= 0 && element < 4) ? element : 0);
        for (int slot = 0; slot < 7; ++slot) {                    // boucle 0x4aa7b9
            auto& dst = g_Client.Blob(0x1822738u + 13u * static_cast<uint32_t>(slot), 13);
            std::memcpy(dst.data(), &p.visual[base + 13 * slot], 13);
        }
    });

    // 0x8d BulkItemConsume (Net_OnBulkItemConsume 0x4AB1F0) — consommation en masse.
    // status = code%1000, nb = code/1000.
    // PIÈGE DE COUVERTURE (relu au désassemblage) : `jz def_4AB3FF` (0x4ab3ce, status==0)
    // ET `ja def_4AB3FF` (0x4ab3f3, status-1 > 0Ch donc status >= 14) sautent TOUS LES DEUX
    // sur def_4AB3FF = 0x4ab73c, qui EST le chemin de consommation. La jumptable ne couvre
    // que status 1..13 : toute autre valeur consomme.
    OnPacket<BulkItemConsume>(sys, 0x8d, [](const BulkItemConsume& p) {
        g_GmCmdCooldownLatch = 0;
        const uint32_t status = p.code % 1000u;
        const uint32_t nb     = p.code / 1000u;
        if (status >= 1u && status <= 13u) {          // seuls 1..13 sortent en erreur
            static const int err[13] = {2190, 214, 871, 2174, 686, 2227, 2223,
                                        117, 2224, 2225, 2226, 2237, 2822};
            g_Client.msg.System(Str(err[status - 1u]));
            // TODO(port) [ancres 0x4ab43c … 0x4ab732] : chaque cas d'erreur émet une SECONDE
            //   sortie — un 2e StrTable005_Get suivi de TribeSkillTrainer_PushLogLine(
            //   byte_184C2F8, str) — soit 13 paires MSG+PUSHLOG. Ni la fonction 0x68EBF0 ni
            //   son tampon byte_184C2F8 (journal du formateur de compétences de tribu) ne sont
            //   portés dans ClientSource (grep « PushLogLine|184C2F8|CraftLog » = 0 occurrence)
            //   -> câblage HORS de ce front (créer Game/TribeSkillLog.* puis doubler les 14 msg).
            return;
        }
        // status == 0 OU status >= 14 -> def_4AB3FF (0x4ab73c) = consommation.
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
        g_Client.msg.System(Str(681));                // 0x4ab998
        // TODO(port) [ancre 0x4ab9b2] : le chemin succès double lui aussi son message —
        //   TribeSkillTrainer_PushLogLine(byte_184C2F8, Str(681)). Même blocage que ci-dessus.
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

    // 0x97 MultiItemRemove (Net_OnMultiItemRemove 0x4AC5F0) — retrait de plusieurs cellules
    // (jusqu'à 5, coords base-100). Le latch est remis à 0 en tête de CHACUNE des 3 branches
    // (0x4ac6d3 = resultCode 0, 0x4ac8ba = 1, 0x4ac8e7 = 2) — donc PAS pour resultCode >= 3.
    OnPacket<MultiItemRemove>(sys, 0x97, [](const MultiItemRemove& p) {
        if (p.resultCode == 0) {
            g_GmCmdCooldownLatch = 0;             // 0x4ac6d3 (loc_4AC6D3, tête de branche)
            const uint32_t n = p.count + 1u;      // boucle i<count+1
            for (uint32_t k = 0; k < n && k < 5u; ++k) {
                const uint32_t row = (k < 4u) ? Dig100(p.rowPackedA, k) : (p.rowPackedB % 100u);
                const uint32_t col = (k < 4u) ? Dig100(p.colPackedA, k) : (p.colPackedB % 100u);
                g_Client.inv.ClearCell(row, col);
            }
            g_Client.msg.System(Str(2259));
        } else if (p.resultCode == 1) {
            g_GmCmdCooldownLatch = 0;             // 0x4ac8ba
            g_Client.msg.System(Str(2246));
        } else if (p.resultCode == 2) {
            g_GmCmdCooldownLatch = 0;             // 0x4ac8e7
            g_Client.msg.System(Str(2247));
        }
    });

    // 0x9b ItemSocketResult (Net_OnItemSocketResult 0x4ACB80) — msgVariant = status/100
    // (0x4acc1d), cas = status%100 (0x4acc2b). Le latch est remis à 0 AVANT le branchement
    // (0x4acc2e) : le poser en tête du lambda est FIDÈLE (ce n'était pas un défaut).
    // Compteur en AFFECTATION (0x4acd3c branche 0, 0x4acfd3 branche 1) -> WriteSrcCellFromPending.
    OnPacket<ItemSocketResult>(sys, 0x9b, [](const ItemSocketResult& p) {
        g_GmCmdCooldownLatch = 0;                                    // 0x4acc2e
        const uint32_t msgVariant = p.status / 100u;
        const uint32_t branch     = p.status % 100u;
        if (branch == 0) {
            // DOUBLE garde d'existence, propre à cette branche :
            //   MobDb_GetEntry(mITEM, dword_1822F2C) (0x4acc67) PUIS
            //   MobDb_GetEntry(mITEM, dword_1822F08[0]) (0x4acc8c).
            // Si l'une des deux échoue : ni écriture de cellule, ni message.
            if (!GetItemInfo(static_cast<uint32_t>(g_Client.Var(0x1822F2C)))) return;
            if (!PendingItemDef()) return;
            g_Client.Var(0x1822F20) = static_cast<int32_t>(p.socket0);   // 0x4acca8
            g_Client.Var(0x1822F24) = static_cast<int32_t>(p.socket1);   // 0x4accb1
            g_Client.Var(0x1822F28) = static_cast<int32_t>(p.socket2);   // 0x4accb9
            WriteSrcCellFromPending();    // 0x4accd9..0x4acd7e (affectation)
            ClearExchangeCell();          // 0x4acdfc.. (dword_1822EDC/EF4)
            ResetPendingMove();           // 2 champs — 0x4acf02/0x4acf0c
            g_Client.msg.System(Str(static_cast<int>(msgVariant + 1771u)));  // 0x4acf25
        } else if (branch == 1) {
            // Branche 1 : AUCUNE garde MobDb (asymétrie vérifiée, 0x4acc51 -> 0x4acf70).
            WriteSrcCellFromPending();    // 0x4acf70..0x4ad015 (affectation)
            ClearExchangeCell();          // 0x4ad093..
            ResetPendingMove();           // 2 champs — 0x4ad199/0x4ad1a3
            g_Client.msg.System(Str(2068));                              // 0x4ad1bd
        }
    });

    // 0xa8 ItemUpgradeResult (Net_OnItemUpgradeResult 0x4AE2F0) — amélioration d'objet :
    // cellule depuis le payload au slot pending, coût 100 or. status -1/0/1 = variantes de
    // succès (messages distincts). Reset = 4 CHAMPS ici (0x4ae419/0x4ae423/0x4ae42d), l'un
    // des trois seuls profils 4 de la tranche.
    OnPacket<ItemUpgradeResult>(sys, 0xa8, [](const ItemUpgradeResult& p) {
        if (p.status == 0xFFFFFFFFu || p.status == 0 || p.status == 1) {
            g_GmCmdCooldownLatch = 0;
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.instanceSerial);
            ResetPendingMove(4);             // 4 champs — 0x4ae40f..0x4ae42d
            g_Client.inv.currency -= 100;    // g_Currency & dword_1687254[0]
            if (p.status == 1) {
                // BUG D'ORIGINE — À REPRODUIRE, NE PAS « CORRIGER » (règle de fidélité).
                // Ancre : le reset de la LIGNE à -1 (0x4AE67F `mov ds:g_PendingMove_SrcRow0,
                // 0FFFFFFFFh`) PRÉCÈDE la relecture de ce même global par les 3 écritures aux
                // (0x4AE6A7 / 0x4AE6C6 / 0x4AE6E5, chacune suivie de `imul ..., 300h` = -768),
                // alors que la COLONNE dword_1822EF0 n'est PAS reset (le reset ne touche que
                // les 4 LIGNES ED8/EDC/EE0/EE4 — cf. commentaire IDA de 0x1822ED8). Les 3
                // écritures (0x4AE6BB / 0x4AE6DA / 0x4AE6FA) tapent donc g_InvAux[-192 + 3*col],
                // soit la plage OOB [0x16747B8, 0x1674AAC) qui contient g_StallSlots 0x16747F8
                // (« Stalle boutique perso : 28 slots de 4 dwords ») : l'original corrompt la
                // stalle perso à chaque upgrade status==1.
                //
                // PIÈGE HEX-RAYS : le pseudocode ne folde le -192 que sur la PREMIÈRE écriture
                // (`g_InvAux[3*dword_1822EF0[0] - 192]`) et laisse les deux autres sous la forme
                // `192*g_PendingMove_SrcRow0[0] + 3*col`, ce qui donne l'illusion que seule la
                // 1re est OOB. Au niveau INSTRUCTION les trois relisent le global à -1 : les
                // trois sont OOB. C'est le désassemblage qui fait foi.
                //
                // Deux propriétés rendent la reproduction sûre côté C++ : (1) Var() est une map
                // adressée par CLÉ -> l'OOB n'écrase aucune mémoire C++, il atterrit sur la clé
                // d'origine exacte ; (2) le calcul non signé boucle en complément à deux et
                // donne pile la bonne adresse : 0x1674AB8 + 0xFFFFFD00 = 0x16747B8.
                // NE PAS réutiliser les helpers gardés de InvCells.cpp:84/110 (`if (row < 0)
                // return;`) : ils SUPPRIMERAIENT le bug d'origine.
                //
                // pendingMoveRow vaut -1 ici par construction (ResetPendingMove(4) ci-dessus,
                // miroir de 0x4AE67F) : on relit le champ plutôt que d'écrire -1 en dur, pour
                // reproduire le MÉCANISME (relecture après reset) et pas seulement son effet.
                WriteAuxAt(g_Client.pendingMoveRow, g_Client.pendingMoveCol, 0, 0, 0);
                g_Client.msg.System(Str(730));
            } else if (p.status == 0xFFFFFFFFu) {
                g_Client.msg.System(Str(2310));
            } else {
                g_Client.msg.System(Str(730));
            }
        }
    });

    // 0xa9 ItemFuseResult (Net_OnItemFuseResult 0x4AE750) — fusion de deux objets (source
    // depuis le snapshot pending). Compteur ACCUMULÉ (0x4ae9f3 status 0, 0x4aecb2 status 1).
    // Reset = 2 champs (0x4aebf4, 0x4aeec8 : ni EE0 ni EE4 dans cette fonction).
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
            AccumSrcCellFromPending();   // 0x4ae9f3 (`add edx, ds:dword_1822F14`)
            ClearExchangeCell();      // vide la cellule cible [384*dword_1822EDC + 6*dword_1822EF4]
            ResetPendingMove();       // 2 champs — 0x4aebf4
            g_Client.msg.System(Str(222));
        } else if (p.status == 1) {
            AccumSrcCellFromPending();   // 0x4aecb2 (`add ecx, ds:dword_1822F14`)
            // TODO(model) [ancre 0x4aedf7] : la cellule cible est accumulée depuis le 2e
            //   snapshot pending dword_1822F2C.. (compteur dword_1822F38) — non modélisé.
            ResetPendingMove();       // 2 champs — 0x4aeec8
            // TODO(msg) : StrTable005 2508/2509/2510/2551/2552 selon subMode (1..6, 11, 12).
            g_Client.msg.System(Str(2508));
        }
    });

    // 0xab ItemSocketDispatch (Net_OnItemSocketDispatch 0x4AEFB0) — MÉGA-DISPATCHER
    // sertissage/gemmes (switch resultCode 0..4, EA 0x4af056 ; `default: return` => rien).
    // g_Currency/dword_1687254 -= cost AVANT le switch (0x4af00b/0x4af01a) : inconditionnel.
    // CHAQUE cas géré : (1) latch = 0 ; (2) garde MobDb_GetEntry(mITEM, dword_1822F08[0])
    //   — 0x4af072 / 0x4af579 / 0x4af876 / 0x4afb83 / 0x4aff3d — garde nulle => SEUL le latch
    //   a été remis à 0 ; (3) corps.
    // Nombre de cellules touchées, re-vérifié cas par cas : 1 / 2 / 2 / 3 / 4.
    OnPacket<ItemSocketDispatch>(sys, 0xab, [](const ItemSocketDispatch& p) {
        g_Client.inv.currency -= p.cost;   // 0x4af00b + 0x4af01a (g_Currency & dword_1687254[0])
        switch (p.resultCode) {
            case 0:
                g_GmCmdCooldownLatch = 0;                      // 0x4af05d
                if (!PendingItemDef()) return;                 // 0x4af072 / 0x4af07e
                // Écrit le snapshot (payload) dans la cellule source — compteur en AFFECTATION
                // (v25[3] à 0x4af132), donc WriteSrcCell(payload) est correct.
                WriteSrcCell(p.itemSnapshot[0], p.itemSnapshot[1], p.itemSnapshot[2],
                             p.itemSnapshot[3], p.itemSnapshot[4], p.itemSnapshot[5]);
                if (p.actionType == 1 || p.actionType == 6 || p.actionType == 7) {  // 0x4af34f
                    // Recalc des noeuds d'arbre : Skill_UnpackTreeNodes(dword_1822F20[0],
                    // dword_1822F24[0], dword_1822F28[0], …) — EA 0x4af387 — puis
                    // g_InvAux[…] = 0 (0x4af39e) ; = Bits_SetByteN(1, retour, aux) (0x4af3eb) ;
                    // dword_1674ABC[…] = 0 (0x4af406) ; dword_1674AC0[…] = 0 (0x4af426).
                    int nodes[5] = {0, 0, 0, 0, 0};
                    const int n = Skill_UnpackTreeNodes(
                        static_cast<uint32_t>(g_Client.Var(0x1822F20)),
                        static_cast<uint32_t>(g_Client.Var(0x1822F24)),
                        static_cast<uint32_t>(g_Client.Var(0x1822F28)), nodes);
                    // Indexé par les globals PENDING, VALIDES ici : leur reset
                    // (0x4AF431/0x4AF43B) est POSTÉRIEUR à ces écritures.
                    // La VALEUR 0u passée à Bits_SetByteN est prouvée, pas supposée : le
                    // binaire met la cellule à 0 (0x4AF39E) puis RELIT cette même cellule
                    // (0x4AF3BE) pour la passer en 3e argument -> l'entrée vaut donc 0.
                    // Seul l'INDEX était faux ici.
                    WriteAuxAt(g_Client.pendingMoveRow, g_Client.pendingMoveCol,
                               static_cast<int32_t>(Bits_SetByteN(1, static_cast<int8_t>(n), 0u)),
                               0, 0);
                }
                ResetPendingMove();                            // 0x4af431 / 0x4af43b (2 champs)
                // Reset conditionnel supplémentaire piloté par actionType (0x4af449-0x4af467).
                if (p.actionType == 12) {
                    g_Client.Var(0x1822EE0) = -1;              // 0x4af44b
                } else if (p.actionType == 13) {
                    g_Client.Var(0x1822EE0) = -1;              // 0x4af45d
                    g_Client.Var(0x1822EE4) = -1;              // 0x4af467
                }
                // Message sélectionné par actionType (switch 0x4af4de) — PAS inconditionnel.
                switch (p.actionType) {
                    case 1: case 2: case 3: case 4: case 6: case 7:
                        g_Client.msg.System(Str(222));  break; // 0x4af4f1
                    case 11: case 12: case 13:
                        g_Client.msg.System(Str(3390)); break; // 0x4af525
                    case 21:
                        g_Client.msg.System(Str(2710)); break; // 0x4af54e
                    default: break;                            // aucun autre cas => aucun message
                }
                break;

            case 1:
                g_GmCmdCooldownLatch = 0;                      // 0x4af563
                if (!PendingItemDef()) return;                 // 0x4af579 / 0x4af585
                AccumSrcCellFromPending();                     // 0x4af646 (`add`) — SEUL cas
                ClearExchangeCell();                           // 0x4af721..
                ResetPendingMove();                            // 0x4af826/0x4af830 (2 champs)
                g_Client.msg.System(Str(2622));                // 0x4af84b
                break;

            case 2:   // 2 cellules
                g_GmCmdCooldownLatch = 0;                      // 0x4af860
                if (!PendingItemDef()) return;                 // 0x4af876 / 0x4af882
                g_Client.inv.ClearCell(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                       static_cast<uint32_t>(g_Client.pendingMoveCol));  // 0x4af8d7
                ClearExchangeCell();                           // 0x4afa05
                ResetPendingMove();                            // 0x4afb0b/0x4afb15 (2 champs)
                // Message piloté par actionType (0x4afb23), PAS par resultCode.
                g_Client.msg.System(Str(p.actionType == 11 ? 3389 : 2622));  // 0x4afb36 / 0x4afb58
                break;

            case 3:   // 3 cellules
                g_GmCmdCooldownLatch = 0;                      // 0x4afb6d
                if (!PendingItemDef()) return;                 // 0x4afb83 / 0x4afb8f
                g_Client.inv.ClearCell(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                       static_cast<uint32_t>(g_Client.pendingMoveCol));  // 0x4afbaa
                ClearExchangeCell();                           // 0x4afcc4
                ClearCellGuarded(0x1822EE0, 0x1822EF8);        // 0x4afdde
                ResetPendingMove(3);                           // 0x4afee4/0x4afeee/0x4afef8
                g_Client.msg.System(Str(3389));                // 0x4aff13
                break;

            case 4:   // 4 cellules
                g_GmCmdCooldownLatch = 0;                      // 0x4aff28
                if (!PendingItemDef()) return;                 // 0x4aff3d / 0x4aff49
                g_Client.inv.ClearCell(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                       static_cast<uint32_t>(g_Client.pendingMoveCol));  // 0x4aff65
                ClearExchangeCell();                           // 0x4b007f
                ClearCellGuarded(0x1822EE0, 0x1822EF8);        // 0x4b0199
                ClearCellGuarded(0x1822EE4, 0x1822EFC);        // 0x4b02b3
                ResetPendingMove(4);                           // 0x4b03b8..0x4b03d6
                g_Client.msg.System(Str(3389));                // 0x4b03f1
                break;

            default: break;   // `default: return result` (0x4b0401) — pas même le latch
        }
        // TODO(model) [ancres 0x4af9a7 / 0x4afac1 / 0x4afe9a / 0x4b036f] : chaque effacement
        //   de cellule vide AUSSI son triplet aux g_InvAux/dword_1674ABC/dword_1674AC0 indexé
        //   [192*row + 3*col]. Le modèle client n'a qu'UN triplet global (ClientRuntime.h:85)
        //   au lieu d'un par cellule -> le zérotage par cellule n'est pas représentable ici.
        //   Câblage hors de ce front : étendre InvCell (Game/GameState.h) + InventoryState
        //   (Game/ClientRuntime.h) avec aux0/aux1/aux2 PAR CELLULE, puis les zéroer dans
        //   ClearCell. Zéroter le triplet global en substitut serait une NOUVELLE divergence.
    });

    // 0xac ItemRefineDispatch (Net_OnItemRefineDispatch 0x4B0440, ~7.9 Ko) — MÉGA-DISPATCHER
    // raffinage/amélioration. switch 45 cas (0x4b04ab `cmp [ebp+var_404], 2Ch`).
    //
    // Table de saut RE-DÉRIVÉE DEPUIS LES OCTETS BRUTS (get_bytes), pas depuis un comptage de
    // handlers — cf. le piège des ordinaux d'ItemEffectDispatch. Il N'A PAS frappé ici : les
    // numéros de cas sont bien les valeurs de `op`.
    //   byte_4B2288[45] = 00 01 02 02 0d 03 04 0d 0d 0d 0d 05 05 05 0d 0d 0d 0d 0d 0d 0d
    //                     06 06 0d 0d 0d 0d 0d 0d 0d 0d 07 08 07 0d 0d 0d 0d 0d 0d 0d
    //                     09 0a 0b 0c
    //   jpt_4B04C5[14]  = 4b04cc 4b0793 4b0a04 4b0c29 4b0e51 4b1070 4b13c6 4b1596
    //                     4b188b 4b1b05 4b1cdd 4b1eb5 4b207d 4b2240(def)
    // => op 0->corps0, 1->corps1, 2/3->corps2, 5->corps3, 6->corps4, 11/12/13->corps5,
    //    21/22->corps6, 31/33->corps7, 32->corps8, 41->corps9, 42->corpsA, 43->corpsB,
    //    44->corpsC ; op 4, 7-10, 14-20, 23-30, 34-40 (et >44) -> def_4B04C5 = 0x4b2240,
    //    qui ne contient QUE l'épilogue (cookie/leave/retn) => STRICTEMENT RIEN.
    //    (L'ancien code faisait latch=0 + ResetPendingMove() inconditionnellement.)
    //
    // Payload : op = +0 (0x4b045e), a = +4 (0x4b0474), b = +8 (0x4b0487), c = +12 (0x4b049a).
    // Usages : g_InvWeight -= a ; dword_1822F18[0] = b ; c = vararg de Crt_Vsnprintf.
    // Le son (Snd3D_PlayScaledVolume, sélection par record+188 et Item_GetAttribByte0) est
    // HORS PÉRIMÈTRE (cf. en-tête de fichier) — y compris l'ordre son-avant-garde des op 41/42.
    OnPacket<ItemRefineDispatch>(sys, 0xac, [](const ItemRefineDispatch& p) {
        // Corps commun « LABEL_79/LABEL_83 » : poids, durabilité imposée, cellule (compteur
        // ACCUMULÉ), aux, reset 2 champs. `setDurability=false` reproduit LABEL_34 (op 1 et 6),
        // les DEUX seuls corps qui n'affectent PAS dword_1822F18.
        auto ApplyBody = [&](bool setDurability) {
            g_Client.inv.weight -= p.a;                        // 0x4b0a92 / 0x4b0ee0 / …
            if (setDurability)
                g_Client.pendingItem.color = p.b;              // dword_1822F18[0] = v33 (0x4b0aa5)
            AccumSrcCellFromPending();                         // compteur `+=` (0x4b0b43, 13 corps)
            // TODO(model) [ancres 0x4b0ba6/0x4b0bc7/0x4b0be8] : les 13 corps recopient aussi
            //   g_InvAux/1674ABC/1674AC0[192*row+3*col] depuis dword_1822F20/F24/F28 (2e moitié
            //   du snapshot pending). Ni ce snapshot ni l'aux PAR CELLULE ne sont modélisés.
            ResetPendingMove();                                // 2 champs (0x4b0bef/0x4b0bf9)
        };

        switch (p.op) {
        case 0: {  // corps 0x4b04cc
            g_GmCmdCooldownLatch = 0;                          // 0x4b04cc
            const ItemInfo* def = PendingItemDef();            // 0x4b04e2
            if (!def) return;                                  // 0x4b04f4
            // Message SUPPLÉMENTAIRE réservé au type 6 : switch(record+188) 0x4b0536,
            // `case 6:` -> Crt_Vsnprintf(v32, Str(2624), c) + Msg (0x4b055b / 0x4b0582).
            // Les types 7..0x15 ne jouent qu'un son ; tous les types rejoignent LABEL_79.
            if (def->typeCode == 6u) g_Client.msg.System(Str(2624));
            ApplyBody(true);                                   // -> LABEL_79
            g_Client.msg.System(Str(222));                     // 0x4b1ea0
            break;
        }

        case 1:    // corps 0x4b0793
            g_GmCmdCooldownLatch = 0;                          // 0x4b0793
            if (!PendingItemDef()) return;                     // 0x4b07a8 / 0x4b07ba
            ApplyBody(false);                                  // -> LABEL_34 : PAS de F18 = b
            g_Client.msg.System(Str(223));                     // 0x4b105b
            break;

        case 2: case 3:    // corps 0x4b0a04
            g_GmCmdCooldownLatch = 0;                          // 0x4b0a04
            if (!PendingItemDef()) return;                     // 0x4b0a19 / 0x4b0a2b
            ApplyBody(true);
            g_Client.msg.System(Str(2570));                    // 0x4b0c14
            break;

        case 5:    // corps 0x4b0c29
            g_GmCmdCooldownLatch = 0;                          // 0x4b0c29
            if (!PendingItemDef()) return;                     // 0x4b0c3f / 0x4b0c51
            ApplyBody(true);
            g_Client.msg.System(Str(2571));                    // 0x4b0e3c
            break;

        case 6:    // corps 0x4b0e51
            g_GmCmdCooldownLatch = 0;                          // 0x4b0e51
            if (!PendingItemDef()) return;                     // 0x4b0e67 / 0x4b0e79
            ApplyBody(false);                                  // -> LABEL_34 : PAS de F18 = b
            g_Client.msg.System(Str(223));                     // 0x4b105b
            break;

        case 11: case 12: case 13: {  // corps 0x4b1070
            g_GmCmdCooldownLatch = 0;                          // 0x4b1070
            const ItemInfo* def = PendingItemDef();            // 0x4b1086
            if (!def) return;                                  // 0x4b1098
            // Réservé au type 6 (switch 0x4b10da `case 6:`) : sous-switch sur op (0x4b10f5)
            // -> Vsnprintf du gabarit 2624/2626/2627 + Msg. Types 7..0x15 : son seul.
            if (def->typeCode == 6u)
                g_Client.msg.System(Str(p.op == 11 ? 2624 : (p.op == 12 ? 2626 : 2627)));  // 0x4b1105/0x4b114b/0x4b118e
            ApplyBody(true);                                   // -> LABEL_79
            g_Client.msg.System(Str(222));                     // 0x4b1ea0
            break;
        }

        case 21: case 22:    // corps 0x4b13c6 — SEUL corps SANS garde MobDb et SANS son
            g_GmCmdCooldownLatch = 0;                          // 0x4b13c6
            ApplyBody(true);                                   // F18 = b bien présent (0x4b13e3)
            g_Client.msg.System(Str(p.op == 21 ? 2683 : 2680));  // 0x4b1558 / 0x4b1581
            break;

        case 31: case 33: {  // corps 0x4b1596
            g_GmCmdCooldownLatch = 0;                          // 0x4b1596
            const ItemInfo* def = PendingItemDef();            // 0x4b15ab
            if (!def) return;                                  // 0x4b15bd
            // Message SUPPLÉMENTAIRE réservé au type 6 (switch 0x4b15ff `case 6:`) :
            // Vsnprintf(Str(2624), c) + Msg (0x4b1624 / 0x4b164c).
            if (def->typeCode == 6u) g_Client.msg.System(Str(2624));
            ApplyBody(true);
            g_Client.msg.System(Str(p.op == 31 ? 222 : 2680)); // 0x4b184d / 0x4b1876
            break;
        }

        case 32:    // corps 0x4b188b
            g_GmCmdCooldownLatch = 0;                          // 0x4b188b
            if (!PendingItemDef()) return;                     // 0x4b18a0 / 0x4b18b2
            ApplyBody(true);
            g_Client.msg.System(Str(2257));                    // 0x4b1af0
            break;

        case 41:    // corps 0x4b1b05
            g_GmCmdCooldownLatch = 0;                          // 0x4b1b05
            // 0x4b1b1a : Snd3D AVANT la garde (hors périmètre) ; garde à 0x4b1b2b.
            if (!PendingItemDef()) return;                     // 0x4b1b2b / 0x4b1b3d
            ApplyBody(true);                                   // -> LABEL_79
            g_Client.msg.System(Str(222));                     // 0x4b1ea0
            break;

        case 42:    // corps 0x4b1cdd
            g_GmCmdCooldownLatch = 0;                          // 0x4b1cdd
            // 0x4b1cf2 : Snd3D AVANT la garde (hors périmètre) ; garde à 0x4b1d03.
            if (!PendingItemDef()) return;                     // 0x4b1d03 / 0x4b1d15
            ApplyBody(true);                                   // LABEL_79 (site physique)
            g_Client.msg.System(Str(222));                     // 0x4b1ea0
            break;

        case 43:    // corps 0x4b1eb5
            g_GmCmdCooldownLatch = 0;                          // 0x4b1eb5
            if (!PendingItemDef()) return;                     // 0x4b1ecb / 0x4b1edd
            ApplyBody(true);                                   // -> LABEL_83
            g_Client.msg.System(Str(223));                     // 0x4b2230
            break;

        case 44:    // corps 0x4b207d
            g_GmCmdCooldownLatch = 0;                          // 0x4b207d
            if (!PendingItemDef()) return;                     // 0x4b2093 / 0x4b20a5
            ApplyBody(true);                                   // LABEL_83 (site physique)
            g_Client.msg.System(Str(223));                     // 0x4b2230
            break;

        default: break;   // def_4B04C5 = 0x4b2240 : épilogue nu => RIEN
        }
    });

    // 0xaf ItemEnhanceResult (Net_OnItemEnhanceResult 0x4B2790) — amélioration/enchant.
    // Compteur ACCUMULÉ (0x4b2934 resultCode 1, 0x4b2b63 resultCode 2).
    // Reset = 2 champs (0x4b2a04, 0x4b2c33 : ni EE0 ni EE4 dans cette fonction).
    OnPacket<ItemEnhanceResult>(sys, 0xaf, [](const ItemEnhanceResult& p) {
        if (p.resultCode == 1) {           // succès : monte le niveau
            g_Client.inv.currency -= p.cost;
            uint32_t& dur = g_Client.pendingItem.color;         // durabilité (dword_1822F18)
            dur = AddByte1(SetByte2(dur, p.enhanceByte), 1);    // octet2 = enhanceByte, +1 niveau (octet1)
            AccumSrcCellFromPending();     // 0x4b2934 (`add ecx, ds:dword_1822F14`)
            ResetPendingMove();            // 2 champs — 0x4b2a04
            g_Client.msg.System(Str(222));
        } else if (p.resultCode == 2) {    // échec : reset des octets 1 et 2 (niveau)
            g_Client.inv.currency -= p.cost;
            g_Client.pendingItem.color = ClearByte12(g_Client.pendingItem.color);
            AccumSrcCellFromPending();     // 0x4b2b63 (`add ecx, ds:dword_1822F14`)
            ResetPendingMove();            // 2 champs — 0x4b2c33
            g_Client.msg.System(Str(2680));
        }
    });

    // 0xb0 ItemEnhanceResult2 (Net_OnItemEnhanceResult2 0x4B2CA0) — variante d'amélioration
    // (durabilité repackée sur 4 octets). Compteur ACCUMULÉ (0x4b2e9b, 0x4b325c).
    // Reset = 4 CHAMPS (0x4b30af/0x4b30b9/0x4b30c3 et 0x4b332d/0x4b3337/0x4b3341) — profil
    // rare, à ne pas confondre avec ses voisins 0xa9/0xaf qui n'en remettent que 2.
    OnPacket<ItemEnhanceResult2>(sys, 0xb0, [](const ItemEnhanceResult2& p) {
        g_GmCmdCooldownLatch = 0;
        const uint32_t durability =
            SetByte3(PackByte012(p.statByte0, p.statByte1, p.statByte2), p.statByte3);
        if (p.resultCode == 1) {                                   // succès amélioration
            g_Client.pendingItem.color = durability;              // durabilité (dword_1822F18)
            AccumSrcCellFromPending();                            // 0x4b2e9b (`add`)
            // TODO(model) [ancre 0x4b2f96] : cette branche accumule AUSSI la cellule d'échange
            //   [384*dword_1822EDC + 6*dword_1822EF4] += dword_1822F38 (2e snapshot pending,
            //   0x4b2f7c) — snapshot non modélisé dans g_Client.
            ResetPendingMove(4);                                  // 4 champs — 0x4b30af..0x4b30c3
            g_Client.msg.System(Str(2694));
        } else if (p.resultCode >= 10 && p.resultCode <= 14) {     // branche transmutation
            g_Client.pendingItem.itemId = p.newItemId;            // dword_1822F08 = newItemId
            g_Client.pendingItem.color  = durability;
            g_Client.inv.currency -= 1000;                        // g_Currency & dword_1687254[0]
            AccumSrcCellFromPending();                            // 0x4b325c (`add`)
            ResetPendingMove(4);                                  // 4 champs — 0x4b332d..0x4b3341
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

    // 0xb4 StatSyncDispatch (Net_OnStatSyncDispatch 0x4B3590) — sync argent/poids/compteur
    // (INCONDITIONNELLE, 0x4b35f8..0x4b3615) + résultat d'action inventaire (switch 0x4b362e,
    // `default: return` => rien).
    // CHAQUE cas : latch = 0 (0x4b3635 / 0x4b3929 / 0x4b3c1c / 0x4b3f31) PUIS garde
    //   MobDb_GetEntry(mITEM, dword_1822F08[0]) (0x4b364b / 0x4b393e / 0x4b3c32 / 0x4b3f46).
    // Compteur ACCUMULÉ (0x4b370f / 0x4b3a02 / 0x4b3cf6) ; reset = 2 champs (0x4b38ef/0x4b38f9).
    OnPacket<StatSyncDispatch>(sys, 0xb4, [](const StatSyncDispatch& p) {
        g_Client.inv.weight     = p.invWeight;                      // g_InvWeight  (0x4b35f8)
        g_Client.inv.currency   = p.currency;                       // g_Currency & dword_1687254[0]
        g_Client.Var(0x16746E8) = static_cast<int32_t>(p.counter);  // dword_16746E8 (0x4b3615)
        switch (p.resultCode) {
            case 0:
            case 1:
            case 2:
                g_GmCmdCooldownLatch = 0;                    // 0x4b3635 / 0x4b3929 / 0x4b3c1c
                if (!PendingItemDef()) return;               // 0x4b364b / 0x4b393e / 0x4b3c32
                g_Client.pendingItem.color = p.durability;   // dword_1822F18[0] = v18 (0x4b368b)
                AccumSrcCellFromPending();                   // 0x4b370f (`add edx, ds:dword_1822F14`)
                ClearExchangeCell();                         // 0x4b37e9.. (dword_1822EDC/EF4)
                ResetPendingMove();                          // 2 champs — 0x4b38ef/0x4b38f9
                if (p.resultCode == 0) {
                    g_Client.msg.System(Str(2748));          // 0x4b3914
                } else if (p.resultCode == 1) {
                    g_Client.msg.System(Str(2749));          // 0x4b3c07
                } else {
                    // Cas 2 : DEUX lignes système, pas une.
                    g_Client.msg.System(Str(2749));          // 0x4b3efb
                    g_Client.msg.System(Str(654));           // 0x4b3f1c
                }
                break;
            case 3:
                g_GmCmdCooldownLatch = 0;                    // 0x4b3f31
                if (!PendingItemDef()) return;               // 0x4b3f46 / 0x4b3f52
                // NOTE : le cas 3 n'affecte PAS dword_1822F18 (contrairement aux cas 0/1/2).
                g_Client.inv.ClearCell(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                       static_cast<uint32_t>(g_Client.pendingMoveCol));  // 0x4b3f97
                ClearExchangeCell();                         // 0x4b40b1
                ResetPendingMove();                          // 2 champs — 0x4b41b7/0x4b41c1
                // Cas 3 : DEUX lignes système, pas une.
                g_Client.msg.System(Str(2749));              // 0x4b41db
                g_Client.msg.System(Str(224));               // 0x4b41fc
                break;
            default: break;   // `default: return result` (0x4b420c) — pas même le latch
        }
    });
}

} // namespace ts2::net

// Net/GameHandlers_InvDispatch.cpp — item mega-dispatchers (enchant/refine/
// socket/fuse/upgrade/enhance) + batch/bulk/multi loads-and-removals,
// stat sync, and equipment appearance.
//
// "inv_dispatch" domain (RE/handler_domains.json). Faithfully translates the
// state-update logic of the original handlers (RE/net_handler_notes.md) to
// the game::g_Client hub (inventory grid, currency/weight, message log,
// pending-item-move state). Anticheat/audio/exact UI rendering out of scope.
// Model reminder: g_Client.inv.currency represents g_Currency AND its mirror
// dword_1687254[0]; g_Client.pendingItem = pending-item snapshot dword_1822F08..
// (with .color = bit-packed durability dword_1822F18, .durability = serial).
//
// Opcodes covered (18):
//   0x1b LegacyItemUpgradeResult (*) 0x1c LegacyItemRefineResult (*)
//   0x75 ItemEnchantDispatch  0x77 InventoryBulkLoad   0x7c ItemRefineResult
//   0x83 PlayerEquipVisual    0x8d BulkItemConsume     0x95 ItemBatchUpdate
//   0x97 MultiItemRemove      0x9b ItemSocketResult    0xa8 ItemUpgradeResult
//   0xa9 ItemFuseResult       0xab ItemSocketDispatch  0xac ItemRefineDispatch
//   0xaf ItemEnhanceResult    0xb0 ItemEnhanceResult2  0xb3 ItemDropResult
//   0xb4 StatSyncDispatch
// (*) 0x1b/0x1c: coverage gap closed — absent from RE/handler_domains.json and from
//     any Register*Handlers before this addition (cf. Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md).
//     Historical IDA names Pkt_ItemUpgradeResult/Pkt_ItemRefineResult, DISTINCT from the
//     same-themed Net_On* handlers (0xa8/0x7c) already covered below.
//
// ============================================================================
// SOURCE CELL SEMANTICS — map re-derived in Pass 4 (W8), evidence at
// INSTRUCTION level (exhaustive search for `add exx, ds:dword_1822F14|F38`
// over [0x488000, 0x4b4300]). Two DISTINCT families, must not be merged:
//
//   ACCUMULATION `g_InvGrid_Count[...] += dword_1822F14` (load / add / store):
//     0x1b (0x488f4a…), 0x1c (0x48a692…), 0x7c (0x4a9952, 0x4a9b84),
//     0xa9 (0x4ae9f3, 0x4aecb2), 0xab case 1 (0x4af646), 0xac (13 bodies, 0x4b0b43…),
//     0xaf (0x4b2934, 0x4b2b63), 0xb0 (0x4b2e9b, 0x4b325c), 0xb4 cases 0/1/2 (0x4b370f…)
//
//   ASSIGNMENT `g_InvGrid_Count[...] = dword_1822F14` (load / store, NO add):
//     0x75 (0x4a761b, 0x4a77e5, 0x4a7a07, 0x4a7c2c, 0x4a7e6b — 5/5 sites),
//     0x9b (0x4acd3c, 0x4acfd3)
//
// Hence TWO helpers (AccumSrcCellFromPending / WriteSrcCellFromPending). The old
// single `=` helper was silently corrupting the stack for the 9 accumulating opcodes.
//
// PENDING-MOVE RESET DEPTH — map re-derived the same way
// (search for `mov ds:dword_1822EE0|EE4, 0FFFFFFFFh` over [0x480000, 0x4c0000]):
//   4 fields (ED8+EDC+EE0+EE4): 0xa8 (0x4ae419/23/2d), 0xb0 (0x4b30af/b9/c3),
//                                0xad Net_OnItemSlotRefresh (outside this module)
//   2 fields (ED8+EDC only)   : 0x1b, 0x1c, 0x75, 0x7c, 0x9b, 0xa9, 0xac, 0xaf, 0xb4
//   0xab: case 0 = 2 + conditional actionType 12/13; 1 = 2; 2 = 2; 3 = 3; 4 = 4
// ============================================================================
#include "Net/GameHandlers.h"
#include "Game/ClientRuntime.h"
#include "Game/GameState.h"    // game::g_World (self.element = g_LocalElement)
#include "Game/GameDatabase.h" // game::GetItemInfo — == MobDb_GetEntry(mITEM,id) 0x4C3C00
#include "Game/SkillSystem.h"  // game::Skill_UnpackTreeNodes — == 0x54C090
#include "Game/BitPacking.h"   // game::Bits_SetByteN — == 0x54BF30
#include "Net/ClientState.h"   // ts2::net::g_GmCmdCooldownLatch
#include <string>
#include <cstring>
#include <cstdint>

namespace {
using namespace ts2::game;

// --- Item lookup — MobDb_GetEntry(mITEM, id) 0x4C3C00 guard -----------------
// Original: `if (id < 1 || id > count) return 0; if (!record[0]) return 0;`.
// game::GetItemInfo (Game/GameDatabase.cpp, 1-based `record(id-1)` lookup) has the
// SAME semantics — including the total rejection when the table isn't loaded
// (count == 0), exactly like the original. A null guard => the handler does
// NOTHING (neither cell write nor message): this is the binary's behavior.
inline const ItemInfo* PendingItemDef() {
    return GetItemInfo(g_Client.pendingItem.itemId);   // dword_1822F08[0]
}

// --- Inventory cells driven by the "pending move" -----------------

// Writes the SOURCE cell of the pending move — original index
// [384*g_PendingMove_SrcRow0 + 6*dword_1822EF0] = (pendingMoveRow, pendingMoveCol) —
// from 6 raw dwords {itemId, gridX, gridY, count, durability, serial}.
inline void WriteSrcCell(uint32_t itemId, uint32_t gridX, uint32_t gridY,
                         uint32_t count, uint32_t durability, uint32_t serial) {
    g_Client.inv.Set(static_cast<uint32_t>(g_Client.pendingMoveRow),
                     static_cast<uint32_t>(g_Client.pendingMoveCol),
                     itemId, gridX, gridY, count, durability, serial);
}

// Applies the source cell from the pending item snapshot
// (dword_1822F08.. = g_Client.pendingItem) — ASSIGNMENT variant of the counter.
// Anchors: 0x75 EA 0x4a761b (`mov ecx, ds:dword_1822F14` / `mov ds:g_InvGrid_Count[edx+eax], ecx`,
// NO intervening `add`); 0x9b EA 0x4acd3c. RESERVED for 0x75 and 0x9b.
inline void WriteSrcCellFromPending() {
    g_Client.inv.At(static_cast<uint32_t>(g_Client.pendingMoveRow),
                    static_cast<uint32_t>(g_Client.pendingMoveCol)) = g_Client.pendingItem;
}

// Same but the stack counter is ACCUMULATED, not overwritten — the majority variant.
// Canonical anchor: 0xb4 EA 0x4b3708 `mov edx, ds:g_InvGrid_Count[eax+ecx]` /
// 0x4b370f `add edx, ds:dword_1822F14` / 0x4b3729 `mov ds:g_InvGrid_Count[ecx+eax], edx`.
// The other 5 fields stay plain assignment (cf. 0x4b36ab..0x4b376b).
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

// Clears the EXCHANGE/target cell of the move — [384*dword_1822EDC + 6*dword_1822EF4].
// Guarded on >=0: after reset these globals are -1 (don't overwrite a real slot).
inline void ClearExchangeCell() {
    const int32_t r = g_Client.Var(0x1822EDC);
    const int32_t c = g_Client.Var(0x1822EF4);
    if (r >= 0 && c >= 0)
        g_Client.inv.ClearCell(static_cast<uint32_t>(r), static_cast<uint32_t>(c));
}

// Clears a move cell designated by a (globalRow, globalCol) pair — used for the
// 3rd/4th cells of 0xab (EE0/EF8 then EE4/EFC). Same guard as
// ClearExchangeCell (mandatory: Var() defaults to 0, not -1).
inline void ClearCellGuarded(uint32_t rowAddr, uint32_t colAddr) {
    const int32_t r = g_Client.Var(rowAddr);
    const int32_t c = g_Client.Var(colAddr);
    if (r >= 0 && c >= 0)
        g_Client.inv.ClearCell(static_cast<uint32_t>(r), static_cast<uint32_t>(c));
}

// --- PER-CELL "aux" arrays -------------------------------------------
// The binary does NOT have three global scalars: it has THREE ARRAYS indexed by
// cell. Anchor Net_OnItemEnchantDispatch 0x4A7410:
//   0x4A7535 `imul ecx, 300h`  -> row     = 0x300 bytes = 192 dwords (64 col x 3 dw)
//   0x4A7541 `imul edx, 0Ch`   -> cell    = 0x0C  bytes =   3 dwords
//   0x4A7547 g_InvAux[ecx+edx] / 0x4A7566 dword_1674ABC[..] / 0x4A7585 dword_1674AC0[..]
// The g_InvAux 0x1674AB8 IDA head comment says it verbatim:
// "auxiliary inventory SoA, cell 0x0C (3 dw), row 0x300. base+row*0x300+col*0x0C".
//
// InventoryState (Game/ClientRuntime.h:85 — NOT owned by this front) only exposes
// three GLOBAL scalars aux0/aux1/aux2: the current cell there overwrites that of
// EVERY other cell (128 triplets collapsed into 1). We therefore go through the
// Var(originalAddress) escape hatch blessed by the header itself (ClientRuntime.h:10-12),
// following the EXACT precedent already in place in
// Net/GameHandlers_InvCells.cpp:71-105 and Game/WarehouseSystem.cpp:13-20 — reusing
// the model, not inventing a 3rd one.
constexpr uint32_t kInvAuxBase  = 0x1674AB8; // g_InvAux
constexpr uint32_t kInvAux1Base = 0x1674ABC; // dword_1674ABC
constexpr uint32_t kInvAux2Base = 0x1674AC0; // dword_1674AC0

// BYTE offset of the (row,col) cell in the 3 aux arrays.
// NOT GUARDED for row/col < 0 — unlike InvCells.cpp:84/110 — because
// handler 0xa8 relies on an ORIGINAL OOB that must be reproduced (cf. WriteAuxAt).
// Unsigned (two's complement) arithmetic reproduces exactly the address
// computed by the binary's signed `imul` once added to the base.
inline uint32_t InvAuxOff(int32_t row, int32_t col) {
    return 4u * static_cast<uint32_t>(192 * row + 3 * col);
}

// Writes the 3 aux dwords of cell (row,col). The 3 writes share the same index,
// recomputed each time by the binary (0x4A7535/0x4A7554/0x4A7573).
inline void WriteAuxAt(int32_t row, int32_t col, int32_t a0, int32_t a1, int32_t a2) {
    const uint32_t off = InvAuxOff(row, col);
    g_Client.Var(kInvAuxBase  + off) = a0;
    g_Client.Var(kInvAux1Base + off) = a1;
    g_Client.Var(kInvAux2Base + off) = a2;
}

// Resets pending-move state. `depth` = number of fields set back to -1
// in order ED8, EDC, EE0, EE4 — the binary does NOT ALWAYS reset 4 (see
// the map at the top of the file). Default = 2 (ED8+EDC), the largely dominant
// profile: canonical anchor 0xb4 EA 0x4b38ef/0x4b38f9, 0x75 EA 0x4a7664/0x4a766e.
inline void ResetPendingMove(int depth = 2) {
    g_Client.pendingMoveRow = -1;              // g_PendingMove_SrcRow0[0] = -1
    g_Client.Var(0x1822EDC) = -1;              // dword_1822EDC = -1
    if (depth >= 3) g_Client.Var(0x1822EE0) = -1;
    if (depth >= 4) g_Client.Var(0x1822EE4) = -1;
}

// --- Bit-packed durability byte manipulation (original Bits_* helpers) ---
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
// Item_GetAttribByte2 0x545670: `Crt_Memcpy(v2, &a1, 4); return v2[2];` = byte 2.
inline uint32_t GetByte2(uint32_t x) { return (x >> 16) & 0xFFu; }
inline uint32_t ClearByte0(uint32_t x)  { return x & 0xFFFFFF00u; }
inline uint32_t ClearByte1(uint32_t x)  { return x & 0xFFFF00FFu; }
inline uint32_t ClearByte12(uint32_t x) { return x & 0xFF0000FFu; }

// --- Decoding packed indices (hardcoded base-10 / base-100 tables) -----
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

    // 0x1b LegacyItemUpgradeResult (Pkt_ItemUpgradeResult, ea=0x488DE0) — coverage gap
    // closed (cf. Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md). resultCode 0..7 switch
    // (0/1=success, 2=failure, 3/6=downgrade, 5/7=other — Docs/TS2_PROTOCOL_SPEC.md #0x1b).
    // Effects confirmed by the spec: gold cost always deducted, level delta folded into
    // byte 0 of the durability (Bits_AddByte0) on success, cell rewritten from the
    // pending snapshot, move reset.
    // Pass 4 (W8): the stack counter is ACCUMULATED — 9 sites `add exx, ds:dword_1822F14`
    // (0x488f4a, 0x48910e, 0x4893ab, 0x489648, 0x48996d, 0x489bea, 0x489e80, 0x48a0ca,
    // 0x48a347), NO site as assignment. Reset = 2 fields (0x489010/0x48901a: neither EE0 nor
    // EE4 appears in this function).
    OnPacket<LegacyItemUpgradeResult>(sys, 0x1b, [](const LegacyItemUpgradeResult& p) {
        g_GmCmdCooldownLatch = 0;
        g_Client.inv.currency -= p.cost;   // dword_16732AC
        // TODO(cost): dword_1673180 (secondary currency) -= 50 for the "weapon" category —
        //   depends on item type (MobDb_GetEntry), not derivable from this payload alone.
        if (p.resultCode == 0 || p.resultCode == 1) {
            g_Client.pendingItem.color = AddByte0(g_Client.pendingItem.color, p.newLevelDelta);
            AccumSrcCellFromPending();   // 0x488f4a (`add`, not `mov`)
        }
        ResetPendingMove();              // 2 fields — 0x489010/0x48901a
        // TODO(msg): StrTable005_Get(222/223/224/1399/1401) per resultCode — exact mapping
        //   of the 8 cases not confirmed by the spec.
        g_Client.msg.System(Str(222));
    });

    // 0x1c LegacyItemRefineResult (Pkt_ItemRefineResult, ea=0x48A530) — coverage gap
    // closed. resultCode 0..3 switch (0=success, 1=success+2nd item, 2=failure, 3=other —
    // Docs/TS2_PROTOCOL_SPEC.md #0x1c). Same shape as 0x7c ItemRefineResult below
    // (Bits_AddByte1(dword_1822F18,1) on success, StrTable005 222/223/224 messages).
    // Pass 4 (W8): counter ACCUMULATED — 0x48a692, 0x48a894, 0x48abdb, 0x48addf (+ 0x48a976
    // on dword_1822F38 = 2nd snapshot, not modeled). Reset = 2 fields (0x48a758/0x48a762).
    OnPacket<LegacyItemRefineResult>(sys, 0x1c, [](const LegacyItemRefineResult& p) {
        g_GmCmdCooldownLatch = 0;
        g_Client.inv.currency -= p.cost;   // dword_16732AC
        if (p.resultCode == 0) {
            g_Client.pendingItem.color = AddByte1(g_Client.pendingItem.color, 1);
            AccumSrcCellFromPending();     // 0x48a692 (`add`)
            ResetPendingMove();            // 2 fields
            g_Client.msg.System(Str(222));
        } else if (p.resultCode == 1) {
            g_Client.pendingItem.color = AddByte1(g_Client.pendingItem.color, 1);
            AccumSrcCellFromPending();     // 0x48a894 (`add`)
            // TODO(model): a 2nd item is also accumulated into the exchange cell via
            //   the 2nd pending snapshot dword_1822F2C..F4C (counter dword_1822F38, EA 0x48a976).
            //   This 2nd snapshot isn't modeled in g_Client (only dword_1822F08.. is).
            ResetPendingMove();            // 2 fields
            g_Client.msg.System(Str(223));
        } else {
            ResetPendingMove();            // 2 fields
            g_Client.msg.System(Str(224));
        }
    });

    // 0x75 ItemEnchantDispatch (Net_OnItemEnchantDispatch 0x4A7410) — tiered enchant
    // result. TWO-level sub-dispatcher: tier = code%100 (0x4a7480, switch 0x4a74b7,
    // jump table 1..5) x status (switch 0x4a74c8 / 0x4a7898 / 0x4a7aba).
    // A tier outside 1..5 falls into `default: return result` (0x4a7f48) => NO message and
    // NO inventory write (the old code emitted Str(1771) and wrote the grid
    // for any tier).
    // The stack counter is ASSIGNMENT here (0x4a761b/0x4a77e5/0x4a7a07/0x4a7c2c/0x4a7e6b
    // : `mov ecx, ds:dword_1822F14` followed by `mov ds:g_InvGrid_Count[..], ecx`, no `add`)
    // -> WriteSrcCellFromPending, NOT the accumulating variant.
    OnPacket<ItemEnchantDispatch>(sys, 0x75, [](const ItemEnchantDispatch& p) {
        g_GmCmdCooldownLatch = 0;              // 0x4a7491 — before the switch (faithful)
        const uint32_t tier  = p.code % 100u;  // 0x4a7480 (v32)
        const uint32_t shift = p.code / 100u;  // 0x4a748e (v38)

        // Write block shared by the status==0 branches of tiers 1..5, split in two
        // to respect the ORIGINAL ORDER of tier 1, which interleaves the weight decrement
        // (0x4a7598) BETWEEN the aux writes and the cell writes.
        // aux: 0x4a7547/0x4a7566/0x4a7585.
        auto ApplyAux = [&]() {
            // Indexed by the PENDING globals (g_PendingMove_SrcRow0 0x1822ED8 /
            // dword_1822EF0), and NOT by the packet's row/col: the binary reads these
            // globals (0x4A752F/0x4A753B). They are VALID here — their reset
            // (0x4A7664/0x4A766E) happens AFTER the 3 aux writes.
            //   g_InvAux[192*row + 3*col]      <- v35   0x4A7547
            //   dword_1674ABC[192*row + 3*col] <- v37   0x4A7566
            //   dword_1674AC0[192*row + 3*col] <- v36   0x4A7585
            WriteAuxAt(g_Client.pendingMoveRow, g_Client.pendingMoveCol,
                       static_cast<int32_t>(p.aux0), static_cast<int32_t>(p.aux1),
                       static_cast<int32_t>(p.aux2));
        };
        // cell: 0x4a75b8..0x4a765d (ASSIGNMENT); reset: 0x4a7664/0x4a766e (2 fields).
        auto ApplyCellAndReset = [&]() {
            WriteSrcCellFromPending();
            ResetPendingMove();           // 2 fields
        };

        switch (tier) {
        case 1:   // 0x4a74b7 case 1 — sub-switch 0x4a74c8, WITHOUT default (status>=4 => nothing)
            switch (p.status) {
            case 1:
            case 2: g_Client.msg.System(Str(1771)); break;   // 0x4a74db
            case 3: g_Client.msg.System(Str(214));  break;   // 0x4a750b
            case 0:
                ApplyAux();
                // Weight decrement UNIQUE to tier 1 / status 0 (imm. 0x5F5E100): absent
                // from the status==0 branches of tiers 2/3/4/5.
                g_Client.inv.weight -= 100000000;            // 0x4a7598
                ApplyCellAndReset();
                g_Client.msg.System(Str(static_cast<int>(shift + 1771u)));  // 0x4a768d
                break;
            default: break;                                  // no default in IDA
            }
            break;

        case 2:   // 0x4a74b7 case 2
            if (p.status == 1) {
                // 0x4a76a6 `goto LABEL_11`: shares string 1778 with tier 3 / status 1.
                g_Client.msg.System(Str(1778));              // 0x4a76b9
            } else if (p.status == 2) {
                g_Client.msg.System(Str(1777));              // 0x4a76ea
            } else if (p.status != 0) {
                g_Client.msg.System(Str(223));               // LABEL_51 0x4a7f38
            } else {
                ApplyAux(); ApplyCellAndReset();             // 0x4a7725..0x4a7838 (NO weight)
                g_Client.msg.System(Str(1779));              // 0x4a7852
            }
            break;

        case 3:   // 0x4a74b7 case 3 — sub-switch 0x4a7898
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

        case 4:   // 0x4a74b7 case 4 — sub-switch 0x4a7aba
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
                // Cost per skill-tree tier. Skill_UnpackTreeNodes 0x54C090 is
                // called with (v35, v37, v36) = (aux0, aux1, aux2) — EA 0x4a7d25 — and its
                // return value (byte 1 of aux0) selects the immediate: EA 0x4a7d35/0x4a7d44/
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
                // g_Currency -= v31 (0x4a7d80) AND dword_1687254[0] -= v31 (0x4a7d8e): TWO
                // globals in the binary, ONE field in the model — inv.currency is
                // declared (ClientRuntime.h:82) as g_Currency *and* its mirror dword_1687254[0].
                // A second decrement on Var(0x1687254) would create a phantom counter.
                g_Client.inv.currency -= cost;
                ApplyAux(); ApplyCellAndReset();             // 0x4a7dab..0x4a7ebe
                g_Client.msg.System(Str(2740));              // 0x4a7ed9
            } else if (p.status != 1 && p.status != 2) {     // 0x4a7ef9
                if (p.status == 3) g_Client.msg.System(Str(871));   // 0x4a7f13
                else               g_Client.msg.System(Str(223));   // LABEL_51 0x4a7f38
            }
            // status 1 and 2: the binary falls into `default: return` => NO message.
            break;

        default: break;   // tier outside 1..5: `default: return result` (0x4a7f48) => nothing
        }
    });

    // 0x77 InventoryBulkLoad (Net_OnInventoryBulkLoad 0x4A7F60) — bulk load.
    // count = header%1000 (0x4a815f), code = header/1000 (0x4a814b, switch 0x4a8184).
    OnPacket<InventoryBulkLoad>(sys, 0x77, [](const InventoryBulkLoad& p) {
        g_GmCmdCooldownLatch = 0;                  // 0x4a8133
        const uint32_t code  = p.header / 1000u;   // v13
        const uint32_t count = p.header % 1000u;   // v14

        // --- header switch (0x4a8184): messages + state reset, before the loop ---
        switch (code) {
        case 0:
            g_Client.msg.System(Str(1849));        // 0x4a819c
            break;
        case 1:
            g_Client.msg.System(Str(1788));        // 0x4a81c2
            g_Client.Var(0x1674780) = 0;           // 0x4a81d2 — PURE state reset
            break;
        case 2: case 3: case 4: case 5:
            // MobDb_GetEntry(mITEM, v13 + 807) (0x4a81f2): if NULL, the function RETURNS
            // (0x4a8204) — the whole load loop is skipped.
            if (!GetItemInfo(code + 807u)) return;
            // LABEL_5 (0x4a820b): Crt_Vsnprintf(v22, Str(2999), record+4 = item name)
            //   then Snd3D + Msg (0x4a8235/0x4a826d). Audio is out of scope (cf. header).
            g_Client.msg.System(Str(2999));        // 0x4a8235 (name formatted into the template)
            break;
        case 6:
            if (!GetItemInfo(835u)) return;        // 0x4a8281 / 0x4a8293
            g_Client.msg.System(Str(2999));        // same LABEL_5
            break;
        case 7:
            // 0x4a830d: sound only (out of scope), then LABEL_9.
            break;
        default: break;                            // `default: goto LABEL_9` — loop only
        }

        // --- LABEL_9 (0x4a8312): cell-placement loop ---
        // NOTE: the binary has NO i<8 bound (`for (i=0;;++i) if (i>=v14) break;`); it
        //   would read out of bounds of arrays v19/v26 if header%1000 > 8. We keep the
        //   i<8 guard-rail (assumed DEFENSIVE divergence: reproducing an out-of-bounds
        //   read makes no sense).
        for (uint32_t i = 0; i < count && i < 8u; ++i) {
            const uint32_t row = Dig10(p.rowPacked, i);                                  // 0x4a8365
            const uint32_t col = (i < 4u) ? Dig100(p.colPackedA, i) : Dig100(p.colPackedB, i - 4u);
            const uint32_t pos = (i < 4u) ? Dig100(p.posPackedA, i) : Dig100(p.posPackedB, i - 4u);

            // PER-ITEM existence guard: `Entry = MobDb_GetEntry(mITEM, v18[i])` (0x4a8438)
            // then `if (Entry)` (0x4a8445) — if NULL, the cell is NOT written at all.
            const ItemInfo* it = GetItemInfo(p.itemIds[i]);
            if (!it) continue;

            // Count = 12 if typeCode == 2 (0x4a84dc/0x4a84f3), else 0 (0x4a8515).
            const uint32_t cnt = (it->typeCode == 2u) ? 12u : 0u;

            // Durability: switch(Entry[47]) = switch(typeCode) — 0x4a852d.
            uint32_t dur;
            switch (it->typeCode) {
            case 3:                                     dur = 0; break;  // 0x4a8544
            case 0x16:
                // 0x4a8567..0x4a8608: Item_MeetsStatRequirement(dword_8E717C, itemId, 1, 1.0, &v8)
                //   then `itemId == 12001 ? unk_8E718C : Crt_ftol(dword_8E717C[v8] * 0.5)`.
                // TODO(model) [anchors 0x4a8593 / 0x4a85d2 / 0x4a85ee]: tables 0x8E717C and
                //   0x8E718C (block neighboring mITEM 0x8E71EC) aren't modeled on the client
                //   side -> value not computable here. We set 0, like the other special
                //   cases (3 / 0x1C / 0x1F / 0x20), rather than the PackByte012 path, which would
                //   be wrong.
                dur = 0; break;
            case 0x1C:                                  dur = 0; break;  // 0x4a8638
            case 0x1F: case 0x20:                       dur = 0; break;  // 0x4a8678
            default:
                // Bits_PackByte012(v24, 0, 0) — EA 0x4a8685: `push 0 / push 0 / mov eax,
                // [ebp+var_28] / push eax`, __stdcall right-to-left => (durPacked, 0, 0).
                // Bits_PackByte012 0x5458C0: v4[0]=a1, v4[1]=a2, v4[2]=a3, v4[3]=0, and a1..a3
                // are `char` => ONLY byte 0 of durPacked survives.
                dur = PackByte012(p.durPacked, 0, 0);   // 0x4a86ac
                break;
            }
            g_Client.inv.Set(row, col, p.itemIds[i], pos % 8u, pos / 8u, cnt, dur, 0);  // serial=0 (0x4a86c8)
        }
    });

    // 0x7c ItemRefineResult (Net_OnItemRefineResult 0x4A97A0) — item refining.
    // The latch is reset to 0 INSIDE each branch (0x4a9819 / 0x4a9a37 /
    // 0x4a9c6a): a status >= 3 falls into `return result` and rearms nothing.
    // Each branch is guarded by MobDb_GetEntry(mITEM, dword_1822F08[0]) — 0x4a982f
    // (status 0), 0x4a9a4d (status 1), 0x4a9c80 (status 2): null guard => NO effect
    // (neither currency decrement nor message).
    OnPacket<ItemRefineResult>(sys, 0x7c, [](const ItemRefineResult& p) {
        if (p.status == 0) {
            g_GmCmdCooldownLatch = 0;                        // 0x4a9819
            if (!PendingItemDef()) return;                   // 0x4a982f / 0x4a983b
            g_Client.inv.currency -= p.goldCost;             // 0x4a9890
            // Bits_AddByte2 UNCONDITIONAL on this branch (0x4a98b3).
            g_Client.pendingItem.color = AddByte2(g_Client.pendingItem.color, p.attribDelta);
            AccumSrcCellFromPending();                       // 0x4a9952 (`add`)
            ResetPendingMove();                              // 2 fields — 0x4a99fe/0x4a9a08
            g_Client.msg.System(Str(222));                   // 0x4a9a22
        } else if (p.status == 1) {
            g_GmCmdCooldownLatch = 0;                        // 0x4a9a37
            if (!PendingItemDef()) return;                   // 0x4a9a4d / 0x4a9a59
            g_Client.inv.currency -= p.goldCost;             // 0x4a9aae
            // Here the Bits_AddByte2 is CONDITIONAL (subtle difference vs status 0):
            // `if (Item_GetAttribByte2(dword_1822F18[0]) > 0)` — EA 0x4a9ad0.
            if (GetByte2(g_Client.pendingItem.color) > 0)
                g_Client.pendingItem.color = AddByte2(g_Client.pendingItem.color, p.attribDelta);  // 0x4a9ae7
            AccumSrcCellFromPending();                       // 0x4a9b84 (`add`)
            ResetPendingMove();                              // 2 fields — 0x4a9c30/0x4a9c3a
            g_Client.msg.System(Str(223));                   // 0x4a9c55
        } else if (p.status == 2) {
            g_GmCmdCooldownLatch = 0;                        // 0x4a9c6a
            if (!PendingItemDef()) return;                   // 0x4a9c80 / 0x4a9c8c
            g_Client.inv.currency -= p.goldCost;             // 0x4a9ce1
            ResetPendingMove();                              // 2 fields — 0x4a9cf0/0x4a9cfa
            g_Client.msg.System(Str(224));                   // 0x4a9d14
        }
        // status >= 3: no branch => nothing (not even the latch).
    });

    // 0x83 PlayerEquipVisual (Net_OnPlayerEquipVisual 0x4AA770) — 7 equipment
    // appearance strings from the current element's block. Layout: 4 elements * 91 bytes;
    // 91 = 7 slots * 13 bytes. Selector = g_LocalElement (0x4aa7f3).
    // Str_ClearNameSlot 0x5CCD60 re-read from the disassembly: `if (slot <= 6)
    //   Crt_StringInit(this + 13*slot + 8, src)` (EA 0x5ccd6b/0x5ccd7a/0x5ccd80/0x5ccd85)
    // => the table of 7 names lives at byte_1822730 + 8 = 0x1822738, 7 slots of 13 bytes.
    // We therefore write the string to its ORIGINAL ADDRESS via Blob: the network->state
    // chain is no longer broken (the old code computed the name then did `(void)name`).
    OnPacket<PlayerEquipVisual>(sys, 0x83, [](const PlayerEquipVisual& p) {
        const int element = g_World.self.element;                 // g_LocalElement 0x1673194
        // The binary does NOT bound g_LocalElement (91*4+78 > 368 => out-of-bounds read);
        // defensive guard-rail kept.
        const int base = 91 * ((element >= 0 && element < 4) ? element : 0);
        for (int slot = 0; slot < 7; ++slot) {                    // loop 0x4aa7b9
            auto& dst = g_Client.Blob(0x1822738u + 13u * static_cast<uint32_t>(slot), 13);
            std::memcpy(dst.data(), &p.visual[base + 13 * slot], 13);
        }
    });

    // 0x8d BulkItemConsume (Net_OnBulkItemConsume 0x4AB1F0) — bulk consumption.
    // status = code%1000, nb = code/1000.
    // COVERAGE TRAP (re-read from the disassembly): `jz def_4AB3FF` (0x4ab3ce, status==0)
    // AND `ja def_4AB3FF` (0x4ab3f3, status-1 > 0Ch, i.e. status >= 14) BOTH jump
    // to def_4AB3FF = 0x4ab73c, which IS the consumption path. The jump table only covers
    // status 1..13: any other value consumes.
    OnPacket<BulkItemConsume>(sys, 0x8d, [](const BulkItemConsume& p) {
        g_GmCmdCooldownLatch = 0;
        const uint32_t status = p.code % 1000u;
        const uint32_t nb     = p.code / 1000u;
        if (status >= 1u && status <= 13u) {          // only 1..13 exit as an error
            static const int err[13] = {2190, 214, 871, 2174, 686, 2227, 2223,
                                        117, 2224, 2225, 2226, 2237, 2822};
            g_Client.msg.System(Str(err[status - 1u]));
            // TODO(port) [anchors 0x4ab43c … 0x4ab732]: each error case emits a SECOND
            //   output — a 2nd StrTable005_Get followed by TribeSkillTrainer_PushLogLine(
            //   byte_184C2F8, str) — i.e. 13 MSG+PUSHLOG pairs. Neither function 0x68EBF0 nor
            //   its buffer byte_184C2F8 (tribe skill trainer log) is ported into ClientSource
            //   (grep "PushLogLine|184C2F8|CraftLog" = 0 hits)
            //   -> wiring OUTSIDE this module (create Game/TribeSkillLog.* then duplicate the 14 messages).
            return;
        }
        // status == 0 OR status >= 14 -> def_4AB3FF (0x4ab73c) = consumption.
        const int32_t refund = static_cast<int32_t>(nb * p.unitPrice);
        switch (p.currencyType) {          // refund per currency
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
        // TODO(port) [anchor 0x4ab9b2]: the success path also doubles its message —
        //   TribeSkillTrainer_PushLogLine(byte_184C2F8, Str(681)). Same blocker as above.
    });

    // 0x95 ItemBatchUpdate — batch cell update via packed indices (base-10/100).
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

    // 0x97 MultiItemRemove (Net_OnMultiItemRemove 0x4AC5F0) — removes several cells
    // (up to 5, base-100 coords). The latch is reset to 0 at the top of EACH of the 3
    // branches (0x4ac6d3 = resultCode 0, 0x4ac8ba = 1, 0x4ac8e7 = 2) — so NOT for resultCode >= 3.
    OnPacket<MultiItemRemove>(sys, 0x97, [](const MultiItemRemove& p) {
        if (p.resultCode == 0) {
            g_GmCmdCooldownLatch = 0;             // 0x4ac6d3 (loc_4AC6D3, branch head)
            const uint32_t n = p.count + 1u;      // loop i<count+1
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
    // (0x4acc1d), branch = status%100 (0x4acc2b). The latch is reset to 0 BEFORE the branching
    // (0x4acc2e): placing it at the top of the lambda is FAITHFUL (this wasn't a bug).
    // Counter as ASSIGNMENT (0x4acd3c branch 0, 0x4acfd3 branch 1) -> WriteSrcCellFromPending.
    OnPacket<ItemSocketResult>(sys, 0x9b, [](const ItemSocketResult& p) {
        g_GmCmdCooldownLatch = 0;                                    // 0x4acc2e
        const uint32_t msgVariant = p.status / 100u;
        const uint32_t branch     = p.status % 100u;
        if (branch == 0) {
            // DOUBLE existence guard, specific to this branch:
            //   MobDb_GetEntry(mITEM, dword_1822F2C) (0x4acc67) THEN
            //   MobDb_GetEntry(mITEM, dword_1822F08[0]) (0x4acc8c).
            // If either fails: no cell write, no message.
            if (!GetItemInfo(static_cast<uint32_t>(g_Client.Var(0x1822F2C)))) return;
            if (!PendingItemDef()) return;
            g_Client.Var(0x1822F20) = static_cast<int32_t>(p.socket0);   // 0x4acca8
            g_Client.Var(0x1822F24) = static_cast<int32_t>(p.socket1);   // 0x4accb1
            g_Client.Var(0x1822F28) = static_cast<int32_t>(p.socket2);   // 0x4accb9
            WriteSrcCellFromPending();    // 0x4accd9..0x4acd7e (assignment)
            ClearExchangeCell();          // 0x4acdfc.. (dword_1822EDC/EF4)
            ResetPendingMove();           // 2 fields — 0x4acf02/0x4acf0c
            g_Client.msg.System(Str(static_cast<int>(msgVariant + 1771u)));  // 0x4acf25
        } else if (branch == 1) {
            // Branch 1: NO MobDb guard (asymmetry verified, 0x4acc51 -> 0x4acf70).
            WriteSrcCellFromPending();    // 0x4acf70..0x4ad015 (assignment)
            ClearExchangeCell();          // 0x4ad093..
            ResetPendingMove();           // 2 fields — 0x4ad199/0x4ad1a3
            g_Client.msg.System(Str(2068));                              // 0x4ad1bd
        }
    });

    // 0xa8 ItemUpgradeResult (Net_OnItemUpgradeResult 0x4AE2F0) — item upgrade:
    // cell from the payload into the pending slot, 100-gold cost. status -1/0/1 = success
    // variants (distinct messages). Reset = 4 FIELDS here (0x4ae419/0x4ae423/0x4ae42d), one
    // of only three profile-4 handlers in this range.
    OnPacket<ItemUpgradeResult>(sys, 0xa8, [](const ItemUpgradeResult& p) {
        if (p.status == 0xFFFFFFFFu || p.status == 0 || p.status == 1) {
            g_GmCmdCooldownLatch = 0;
            WriteSrcCell(p.itemId, p.gridX, p.gridY, p.count, p.durability, p.instanceSerial);
            ResetPendingMove(4);             // 4 fields — 0x4ae40f..0x4ae42d
            g_Client.inv.currency -= 100;    // g_Currency & dword_1687254[0]
            if (p.status == 1) {
                // ORIGINAL BUG — TO BE REPRODUCED, NOT "FIXED" (fidelity rule).
                // Anchor: the ROW reset to -1 (0x4AE67F `mov ds:g_PendingMove_SrcRow0,
                // 0FFFFFFFFh`) PRECEDES this same global being read back by the 3 aux
                // writes (0x4AE6A7 / 0x4AE6C6 / 0x4AE6E5, each followed by `imul ..., 300h` = -768),
                // while the COLUMN dword_1822EF0 is NOT reset (the reset only touches
                // the 4 ROWS ED8/EDC/EE0/EE4 — cf. the IDA comment on 0x1822ED8). The 3
                // writes (0x4AE6BB / 0x4AE6DA / 0x4AE6FA) therefore hit g_InvAux[-192 + 3*col],
                // i.e. the OOB range [0x16747B8, 0x1674AAC) which contains g_StallSlots 0x16747F8
                // ("personal shop stall: 28 slots of 4 dwords"): the original corrupts the
                // personal stall on every status==1 upgrade.
                //
                // HEX-RAYS TRAP: the pseudocode only folds the -192 on the FIRST write
                // (`g_InvAux[3*dword_1822EF0[0] - 192]`) and leaves the other two in the
                // form `192*g_PendingMove_SrcRow0[0] + 3*col`, giving the illusion that only
                // the 1st is OOB. At the INSTRUCTION level all three read back the global at -1: all
                // three are OOB. The disassembly is authoritative.
                //
                // Two properties make the reproduction safe on the C++ side: (1) Var() is a map
                // keyed by address -> the OOB doesn't overwrite any C++ memory, it lands on the
                // exact original key; (2) the unsigned two's-complement wraparound
                // gives exactly the right address: 0x1674AB8 + 0xFFFFFD00 = 0x16747B8.
                // DO NOT reuse the guarded helpers from InvCells.cpp:84/110 (`if (row < 0)
                // return;`): they would REMOVE the original bug.
                //
                // pendingMoveRow is -1 here by construction (ResetPendingMove(4) above,
                // mirroring 0x4AE67F): we read the field back instead of hardcoding -1, to
                // reproduce the MECHANISM (read-back after reset) and not just its effect.
                WriteAuxAt(g_Client.pendingMoveRow, g_Client.pendingMoveCol, 0, 0, 0);
                g_Client.msg.System(Str(730));
            } else if (p.status == 0xFFFFFFFFu) {
                g_Client.msg.System(Str(2310));
            } else {
                g_Client.msg.System(Str(730));
            }
        }
    });

    // 0xa9 ItemFuseResult (Net_OnItemFuseResult 0x4AE750) — fuses two items (source
    // from the pending snapshot). Counter ACCUMULATED (0x4ae9f3 status 0, 0x4aecb2 status 1).
    // Reset = 2 fields (0x4aebf4, 0x4aeec8: neither EE0 nor EE4 in this function).
    OnPacket<ItemFuseResult>(sys, 0xa9, [](const ItemFuseResult& p) {
        g_GmCmdCooldownLatch = 0;
        // Requires MobDb_GetEntry(dword_1822F08) & MobDb_GetEntry(dword_1822F2C) (not modeled).
        if (p.status == 0) {
            uint32_t& dur = g_Client.pendingItem.color;   // bit-packed durability (dword_1822F18)
            switch (p.subMode) {
                case 1:              dur = AddByte0(dur, p.aux0); break;
                case 2: case 11:     dur = AddByte1(dur, p.aux1); break;
                case 3:              dur = ClearByte1(ClearByte0(dur)); break;   // clear bytes 0+1
                case 4:              dur = SetByte2(dur, p.aux2); break;
                case 5:              dur = ClearByte0(dur); break;
                case 6: case 12:     dur = ClearByte1(dur); break;
                default: break;
            }
            AccumSrcCellFromPending();   // 0x4ae9f3 (`add edx, ds:dword_1822F14`)
            ClearExchangeCell();      // clears the target cell [384*dword_1822EDC + 6*dword_1822EF4]
            ResetPendingMove();       // 2 fields — 0x4aebf4
            g_Client.msg.System(Str(222));
        } else if (p.status == 1) {
            AccumSrcCellFromPending();   // 0x4aecb2 (`add ecx, ds:dword_1822F14`)
            // TODO(model) [anchor 0x4aedf7]: the target cell is accumulated from the 2nd
            //   pending snapshot dword_1822F2C.. (counter dword_1822F38) — not modeled.
            ResetPendingMove();       // 2 fields — 0x4aeec8
            // TODO(msg): StrTable005 2508/2509/2510/2551/2552 per subMode (1..6, 11, 12).
            g_Client.msg.System(Str(2508));
        }
    });

    // 0xab ItemSocketDispatch (Net_OnItemSocketDispatch 0x4AEFB0) — gem-setting
    // MEGA-DISPATCHER (resultCode switch 0..4, EA 0x4af056; `default: return` => nothing).
    // g_Currency/dword_1687254 -= cost BEFORE the switch (0x4af00b/0x4af01a): unconditional.
    // EACH handled case: (1) latch = 0; (2) guard MobDb_GetEntry(mITEM, dword_1822F08[0])
    //   — 0x4af072 / 0x4af579 / 0x4af876 / 0x4afb83 / 0x4aff3d — null guard => ONLY the latch
    //   was reset; (3) body.
    // Number of cells touched, re-verified case by case: 1 / 2 / 2 / 3 / 4.
    OnPacket<ItemSocketDispatch>(sys, 0xab, [](const ItemSocketDispatch& p) {
        g_Client.inv.currency -= p.cost;   // 0x4af00b + 0x4af01a (g_Currency & dword_1687254[0])
        switch (p.resultCode) {
            case 0:
                g_GmCmdCooldownLatch = 0;                      // 0x4af05d
                if (!PendingItemDef()) return;                 // 0x4af072 / 0x4af07e
                // Writes the snapshot (payload) into the source cell — counter as ASSIGNMENT
                // (v25[3] at 0x4af132), so WriteSrcCell(payload) is correct.
                WriteSrcCell(p.itemSnapshot[0], p.itemSnapshot[1], p.itemSnapshot[2],
                             p.itemSnapshot[3], p.itemSnapshot[4], p.itemSnapshot[5]);
                if (p.actionType == 1 || p.actionType == 6 || p.actionType == 7) {  // 0x4af34f
                    // Recompute tree nodes: Skill_UnpackTreeNodes(dword_1822F20[0],
                    // dword_1822F24[0], dword_1822F28[0], …) — EA 0x4af387 — then
                    // g_InvAux[…] = 0 (0x4af39e); = Bits_SetByteN(1, return, aux) (0x4af3eb);
                    // dword_1674ABC[…] = 0 (0x4af406); dword_1674AC0[…] = 0 (0x4af426).
                    int nodes[5] = {0, 0, 0, 0, 0};
                    const int n = Skill_UnpackTreeNodes(
                        static_cast<uint32_t>(g_Client.Var(0x1822F20)),
                        static_cast<uint32_t>(g_Client.Var(0x1822F24)),
                        static_cast<uint32_t>(g_Client.Var(0x1822F28)), nodes);
                    // Indexed by the PENDING globals, VALID here: their reset
                    // (0x4AF431/0x4AF43B) happens AFTER these writes.
                    // The 0u VALUE passed to Bits_SetByteN is proven, not assumed: the
                    // binary sets the cell to 0 (0x4AF39E) then RE-READS this same cell
                    // (0x4AF3BE) to pass it as the 3rd argument -> the input is therefore 0.
                    // Only the INDEX was wrong here.
                    WriteAuxAt(g_Client.pendingMoveRow, g_Client.pendingMoveCol,
                               static_cast<int32_t>(Bits_SetByteN(1, static_cast<int8_t>(n), 0u)),
                               0, 0);
                }
                ResetPendingMove();                            // 0x4af431 / 0x4af43b (2 fields)
                // Additional conditional reset driven by actionType (0x4af449-0x4af467).
                if (p.actionType == 12) {
                    g_Client.Var(0x1822EE0) = -1;              // 0x4af44b
                } else if (p.actionType == 13) {
                    g_Client.Var(0x1822EE0) = -1;              // 0x4af45d
                    g_Client.Var(0x1822EE4) = -1;              // 0x4af467
                }
                // Message selected by actionType (switch 0x4af4de) — NOT unconditional.
                switch (p.actionType) {
                    case 1: case 2: case 3: case 4: case 6: case 7:
                        g_Client.msg.System(Str(222));  break; // 0x4af4f1
                    case 11: case 12: case 13:
                        g_Client.msg.System(Str(3390)); break; // 0x4af525
                    case 21:
                        g_Client.msg.System(Str(2710)); break; // 0x4af54e
                    default: break;                            // no other case => no message
                }
                break;

            case 1:
                g_GmCmdCooldownLatch = 0;                      // 0x4af563
                if (!PendingItemDef()) return;                 // 0x4af579 / 0x4af585
                AccumSrcCellFromPending();                     // 0x4af646 (`add`) — ONLY case
                ClearExchangeCell();                           // 0x4af721..
                ResetPendingMove();                            // 0x4af826/0x4af830 (2 fields)
                g_Client.msg.System(Str(2622));                // 0x4af84b
                break;

            case 2:   // 2 cells
                g_GmCmdCooldownLatch = 0;                      // 0x4af860
                if (!PendingItemDef()) return;                 // 0x4af876 / 0x4af882
                g_Client.inv.ClearCell(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                       static_cast<uint32_t>(g_Client.pendingMoveCol));  // 0x4af8d7
                ClearExchangeCell();                           // 0x4afa05
                ResetPendingMove();                            // 0x4afb0b/0x4afb15 (2 fields)
                // Message driven by actionType (0x4afb23), NOT by resultCode.
                g_Client.msg.System(Str(p.actionType == 11 ? 3389 : 2622));  // 0x4afb36 / 0x4afb58
                break;

            case 3:   // 3 cells
                g_GmCmdCooldownLatch = 0;                      // 0x4afb6d
                if (!PendingItemDef()) return;                 // 0x4afb83 / 0x4afb8f
                g_Client.inv.ClearCell(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                       static_cast<uint32_t>(g_Client.pendingMoveCol));  // 0x4afbaa
                ClearExchangeCell();                           // 0x4afcc4
                ClearCellGuarded(0x1822EE0, 0x1822EF8);        // 0x4afdde
                ResetPendingMove(3);                           // 0x4afee4/0x4afeee/0x4afef8
                g_Client.msg.System(Str(3389));                // 0x4aff13
                break;

            case 4:   // 4 cells
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

            default: break;   // `default: return result` (0x4b0401) — not even the latch
        }
        // TODO(model) [anchors 0x4af9a7 / 0x4afac1 / 0x4afe9a / 0x4b036f]: each cell clear
        //   also zeroes its g_InvAux/dword_1674ABC/dword_1674AC0 triplet indexed
        //   [192*row + 3*col]. The client model only has ONE global triplet (ClientRuntime.h:85)
        //   instead of one per cell -> per-cell zeroing isn't representable here.
        //   Wiring outside this module: extend InvCell (Game/GameState.h) + InventoryState
        //   (Game/ClientRuntime.h) with per-cell aux0/aux1/aux2, then zero them in
        //   ClearCell. Zeroing the global triplet as a substitute would be a NEW divergence.
    });

    // 0xac ItemRefineDispatch (Net_OnItemRefineDispatch 0x4B0440, ~7.9 KB) — refine/
    // upgrade MEGA-DISPATCHER. 45-case switch (0x4b04ab `cmp [ebp+var_404], 2Ch`).
    //
    // Jump table RE-DERIVED FROM THE RAW BYTES (get_bytes), not from a handler
    // count — cf. the ItemEffectDispatch ordinal trap. It did NOT strike here: the
    // case numbers really are the values of `op`.
    //   byte_4B2288[45] = 00 01 02 02 0d 03 04 0d 0d 0d 0d 05 05 05 0d 0d 0d 0d 0d 0d 0d
    //                     06 06 0d 0d 0d 0d 0d 0d 0d 0d 07 08 07 0d 0d 0d 0d 0d 0d 0d
    //                     09 0a 0b 0c
    //   jpt_4B04C5[14]  = 4b04cc 4b0793 4b0a04 4b0c29 4b0e51 4b1070 4b13c6 4b1596
    //                     4b188b 4b1b05 4b1cdd 4b1eb5 4b207d 4b2240(def)
    // => op 0->body0, 1->body1, 2/3->body2, 5->body3, 6->body4, 11/12/13->body5,
    //    21/22->body6, 31/33->body7, 32->body8, 41->body9, 42->bodyA, 43->bodyB,
    //    44->bodyC; op 4, 7-10, 14-20, 23-30, 34-40 (and >44) -> def_4B04C5 = 0x4b2240,
    //    which contains ONLY the epilogue (cookie/leave/retn) => STRICTLY NOTHING.
    //    (The old code unconditionally did latch=0 + ResetPendingMove().)
    //
    // Payload: op = +0 (0x4b045e), a = +4 (0x4b0474), b = +8 (0x4b0487), c = +12 (0x4b049a).
    // Usage: g_InvWeight -= a; dword_1822F18[0] = b; c = vararg of Crt_Vsnprintf.
    // The sound (Snd3D_PlayScaledVolume, selected by record+188 and Item_GetAttribByte0) is
    // OUT OF SCOPE (cf. file header) — including the sound-before-guard ordering of op 41/42.
    OnPacket<ItemRefineDispatch>(sys, 0xac, [](const ItemRefineDispatch& p) {
        // "LABEL_79/LABEL_83" common body: weight, forced durability, cell (counter
        // ACCUMULATED), aux, 2-field reset. `setDurability=false` reproduces LABEL_34 (op 1 and 6),
        // the ONLY two bodies that do NOT affect dword_1822F18.
        auto ApplyBody = [&](bool setDurability) {
            g_Client.inv.weight -= p.a;                        // 0x4b0a92 / 0x4b0ee0 / …
            if (setDurability)
                g_Client.pendingItem.color = p.b;              // dword_1822F18[0] = v33 (0x4b0aa5)
            AccumSrcCellFromPending();                         // compteur `+=` (0x4b0b43, 13 bodies)
            // TODO(model) [anchors 0x4b0ba6/0x4b0bc7/0x4b0be8]: the 13 bodies also copy
            //   g_InvAux/1674ABC/1674AC0[192*row+3*col] from dword_1822F20/F24/F28 (2nd half
            //   of the pending snapshot). Neither this snapshot nor the per-cell aux are modeled.
            ResetPendingMove();                                // 2 fields (0x4b0bef/0x4b0bf9)
        };

        switch (p.op) {
        case 0: {  // body at 0x4b04cc
            g_GmCmdCooldownLatch = 0;                          // 0x4b04cc
            const ItemInfo* def = PendingItemDef();            // 0x4b04e2
            if (!def) return;                                  // 0x4b04f4
            // EXTRA message reserved for type 6: switch(record+188) 0x4b0536,
            // `case 6:` -> Crt_Vsnprintf(v32, Str(2624), c) + Msg (0x4b055b / 0x4b0582).
            // Types 7..0x15 only play a sound; all types rejoin LABEL_79.
            if (def->typeCode == 6u) g_Client.msg.System(Str(2624));
            ApplyBody(true);                                   // -> LABEL_79
            g_Client.msg.System(Str(222));                     // 0x4b1ea0
            break;
        }

        case 1:    // body at 0x4b0793
            g_GmCmdCooldownLatch = 0;                          // 0x4b0793
            if (!PendingItemDef()) return;                     // 0x4b07a8 / 0x4b07ba
            ApplyBody(false);                                  // -> LABEL_34: NO F18 = b
            g_Client.msg.System(Str(223));                     // 0x4b105b
            break;

        case 2: case 3:    // body at 0x4b0a04
            g_GmCmdCooldownLatch = 0;                          // 0x4b0a04
            if (!PendingItemDef()) return;                     // 0x4b0a19 / 0x4b0a2b
            ApplyBody(true);
            g_Client.msg.System(Str(2570));                    // 0x4b0c14
            break;

        case 5:    // body at 0x4b0c29
            g_GmCmdCooldownLatch = 0;                          // 0x4b0c29
            if (!PendingItemDef()) return;                     // 0x4b0c3f / 0x4b0c51
            ApplyBody(true);
            g_Client.msg.System(Str(2571));                    // 0x4b0e3c
            break;

        case 6:    // body at 0x4b0e51
            g_GmCmdCooldownLatch = 0;                          // 0x4b0e51
            if (!PendingItemDef()) return;                     // 0x4b0e67 / 0x4b0e79
            ApplyBody(false);                                  // -> LABEL_34: NO F18 = b
            g_Client.msg.System(Str(223));                     // 0x4b105b
            break;

        case 11: case 12: case 13: {  // body at 0x4b1070
            g_GmCmdCooldownLatch = 0;                          // 0x4b1070
            const ItemInfo* def = PendingItemDef();            // 0x4b1086
            if (!def) return;                                  // 0x4b1098
            // Reserved for type 6 (switch 0x4b10da `case 6:`): sub-switch on op (0x4b10f5)
            // -> Vsnprintf of template 2624/2626/2627 + Msg. Types 7..0x15: sound only.
            if (def->typeCode == 6u)
                g_Client.msg.System(Str(p.op == 11 ? 2624 : (p.op == 12 ? 2626 : 2627)));  // 0x4b1105/0x4b114b/0x4b118e
            ApplyBody(true);                                   // -> LABEL_79
            g_Client.msg.System(Str(222));                     // 0x4b1ea0
            break;
        }

        case 21: case 22:    // body at 0x4b13c6 — ONLY body WITHOUT MobDb guard and WITHOUT sound
            g_GmCmdCooldownLatch = 0;                          // 0x4b13c6
            ApplyBody(true);                                   // F18 = b present indeed (0x4b13e3)
            g_Client.msg.System(Str(p.op == 21 ? 2683 : 2680));  // 0x4b1558 / 0x4b1581
            break;

        case 31: case 33: {  // body at 0x4b1596
            g_GmCmdCooldownLatch = 0;                          // 0x4b1596
            const ItemInfo* def = PendingItemDef();            // 0x4b15ab
            if (!def) return;                                  // 0x4b15bd
            // EXTRA message reserved for type 6 (switch 0x4b15ff `case 6:`):
            // Vsnprintf(Str(2624), c) + Msg (0x4b1624 / 0x4b164c).
            if (def->typeCode == 6u) g_Client.msg.System(Str(2624));
            ApplyBody(true);
            g_Client.msg.System(Str(p.op == 31 ? 222 : 2680)); // 0x4b184d / 0x4b1876
            break;
        }

        case 32:    // body at 0x4b188b
            g_GmCmdCooldownLatch = 0;                          // 0x4b188b
            if (!PendingItemDef()) return;                     // 0x4b18a0 / 0x4b18b2
            ApplyBody(true);
            g_Client.msg.System(Str(2257));                    // 0x4b1af0
            break;

        case 41:    // body at 0x4b1b05
            g_GmCmdCooldownLatch = 0;                          // 0x4b1b05
            // 0x4b1b1a: Snd3D BEFORE the guard (out of scope); guard at 0x4b1b2b.
            if (!PendingItemDef()) return;                     // 0x4b1b2b / 0x4b1b3d
            ApplyBody(true);                                   // -> LABEL_79
            g_Client.msg.System(Str(222));                     // 0x4b1ea0
            break;

        case 42:    // body at 0x4b1cdd
            g_GmCmdCooldownLatch = 0;                          // 0x4b1cdd
            // 0x4b1cf2: Snd3D BEFORE the guard (out of scope); guard at 0x4b1d03.
            if (!PendingItemDef()) return;                     // 0x4b1d03 / 0x4b1d15
            ApplyBody(true);                                   // LABEL_79 (physical site)
            g_Client.msg.System(Str(222));                     // 0x4b1ea0
            break;

        case 43:    // body at 0x4b1eb5
            g_GmCmdCooldownLatch = 0;                          // 0x4b1eb5
            if (!PendingItemDef()) return;                     // 0x4b1ecb / 0x4b1edd
            ApplyBody(true);                                   // -> LABEL_83
            g_Client.msg.System(Str(223));                     // 0x4b2230
            break;

        case 44:    // body at 0x4b207d
            g_GmCmdCooldownLatch = 0;                          // 0x4b207d
            if (!PendingItemDef()) return;                     // 0x4b2093 / 0x4b20a5
            ApplyBody(true);                                   // LABEL_83 (physical site)
            g_Client.msg.System(Str(223));                     // 0x4b2230
            break;

        default: break;   // def_4B04C5 = 0x4b2240: bare epilogue => NOTHING
        }
    });

    // 0xaf ItemEnhanceResult (Net_OnItemEnhanceResult 0x4B2790) — enhancement/enchant.
    // Counter ACCUMULATED (0x4b2934 resultCode 1, 0x4b2b63 resultCode 2).
    // Reset = 2 fields (0x4b2a04, 0x4b2c33: neither EE0 nor EE4 in this function).
    OnPacket<ItemEnhanceResult>(sys, 0xaf, [](const ItemEnhanceResult& p) {
        if (p.resultCode == 1) {           // success: raises the level
            g_Client.inv.currency -= p.cost;
            uint32_t& dur = g_Client.pendingItem.color;         // bit-packed durability (dword_1822F18)
            dur = AddByte1(SetByte2(dur, p.enhanceByte), 1);    // byte2 = enhanceByte, +1 level (byte1)
            AccumSrcCellFromPending();     // 0x4b2934 (`add ecx, ds:dword_1822F14`)
            ResetPendingMove();            // 2 fields — 0x4b2a04
            g_Client.msg.System(Str(222));
        } else if (p.resultCode == 2) {    // failure: resets bytes 1 and 2 (level)
            g_Client.inv.currency -= p.cost;
            g_Client.pendingItem.color = ClearByte12(g_Client.pendingItem.color);
            AccumSrcCellFromPending();     // 0x4b2b63 (`add ecx, ds:dword_1822F14`)
            ResetPendingMove();            // 2 fields — 0x4b2c33
            g_Client.msg.System(Str(2680));
        }
    });

    // 0xb0 ItemEnhanceResult2 (Net_OnItemEnhanceResult2 0x4B2CA0) — enhancement
    // variant (durability repacked over 4 bytes). Counter ACCUMULATED (0x4b2e9b, 0x4b325c).
    // Reset = 4 FIELDS (0x4b30af/0x4b30b9/0x4b30c3 and 0x4b332d/0x4b3337/0x4b3341) — a
    // rare profile, not to be confused with its neighbors 0xa9/0xaf which only reset 2.
    OnPacket<ItemEnhanceResult2>(sys, 0xb0, [](const ItemEnhanceResult2& p) {
        g_GmCmdCooldownLatch = 0;
        const uint32_t durability =
            SetByte3(PackByte012(p.statByte0, p.statByte1, p.statByte2), p.statByte3);
        if (p.resultCode == 1) {                                   // enhancement success
            g_Client.pendingItem.color = durability;              // durability (dword_1822F18)
            AccumSrcCellFromPending();                            // 0x4b2e9b (`add`)
            // TODO(model) [anchor 0x4b2f96]: this branch ALSO accumulates the exchange cell
            //   [384*dword_1822EDC + 6*dword_1822EF4] += dword_1822F38 (2nd pending snapshot,
            //   0x4b2f7c) — snapshot not modeled in g_Client.
            ResetPendingMove(4);                                  // 4 fields — 0x4b30af..0x4b30c3
            g_Client.msg.System(Str(2694));
        } else if (p.resultCode >= 10 && p.resultCode <= 14) {     // transmutation branch
            g_Client.pendingItem.itemId = p.newItemId;            // dword_1822F08 = newItemId
            g_Client.pendingItem.color  = durability;
            g_Client.inv.currency -= 1000;                        // g_Currency & dword_1687254[0]
            AccumSrcCellFromPending();                            // 0x4b325c (`add`)
            ResetPendingMove(4);                                  // 4 fields — 0x4b332d..0x4b3341
            g_Client.msg.System(Str(2759));
        }
    });

    // 0xb3 ItemDropResult — item drop result: places a cell (coords from payload).
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

    // 0xb4 StatSyncDispatch (Net_OnStatSyncDispatch 0x4B3590) — gold/weight/counter sync
    // (UNCONDITIONAL, 0x4b35f8..0x4b3615) + inventory action result (switch 0x4b362e,
    // `default: return` => nothing).
    // EACH case: latch = 0 (0x4b3635 / 0x4b3929 / 0x4b3c1c / 0x4b3f31) THEN guard
    //   MobDb_GetEntry(mITEM, dword_1822F08[0]) (0x4b364b / 0x4b393e / 0x4b3c32 / 0x4b3f46).
    // Counter ACCUMULATED (0x4b370f / 0x4b3a02 / 0x4b3cf6); reset = 2 fields (0x4b38ef/0x4b38f9).
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
                ResetPendingMove();                          // 2 fields — 0x4b38ef/0x4b38f9
                if (p.resultCode == 0) {
                    g_Client.msg.System(Str(2748));          // 0x4b3914
                } else if (p.resultCode == 1) {
                    g_Client.msg.System(Str(2749));          // 0x4b3c07
                } else {
                    // Case 2: TWO system lines, not one.
                    g_Client.msg.System(Str(2749));          // 0x4b3efb
                    g_Client.msg.System(Str(654));           // 0x4b3f1c
                }
                break;
            case 3:
                g_GmCmdCooldownLatch = 0;                    // 0x4b3f31
                if (!PendingItemDef()) return;               // 0x4b3f46 / 0x4b3f52
                // NOTE: case 3 does NOT touch dword_1822F18 (unlike cases 0/1/2).
                g_Client.inv.ClearCell(static_cast<uint32_t>(g_Client.pendingMoveRow),
                                       static_cast<uint32_t>(g_Client.pendingMoveCol));  // 0x4b3f97
                ClearExchangeCell();                         // 0x4b40b1
                ResetPendingMove();                          // 2 fields — 0x4b41b7/0x4b41c1
                // Case 3: TWO system lines, not one.
                g_Client.msg.System(Str(2749));              // 0x4b41db
                g_Client.msg.System(Str(224));               // 0x4b41fc
                break;
            default: break;   // `default: return result` (0x4b420c) — not even the latch
        }
    });
}

} // namespace ts2::net

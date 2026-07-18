// Game/ItemPickupSystem.h — ground item pickup + quantity validation (stack split).
//
// CLEAN C++ rewrite of:
//   Item_PickupTarget            EA 0x539ec0 — pickup confirmation on arrival (range guard)
//   Item_InteractGround          EA 0x539dc0 — approach + confirmation (same range guard, pathing)
//   Item_QtyDialog_OnLButtonUp   EA 0x5b1650 — OK click of the quantity dialog (input clamping)
//
// /!\ CORRECTED BANNER — Pass 4 / wave W7, "npc-array-unify" front. WHAT FOLLOWED HERE WAS
// WRONG AND TURNED OUT TO BE THE OPPOSITE. The old text claimed that the IDA names of block
// 0x1764D14 ("g_NpcRenderArray", "g_NpcCount", "Scene_RayHitNpcBox") were "mislabeled by an
// automatic renaming pass" and that this array held ground items. RE-PROVEN in IDA (W7):
// it's the OPPOSITE — the IDA names are CORRECT, and it's this module's "ground items"
// hypothesis that was wrong.
//
// Ground truth (re-proven by fresh decompilation, cf. Game/GameState.h::NpcRenderEntry
// for the complete offset table):
//   - 0x1764D14 (stride 88, bounded by g_NpcCount 0x1687220 = 100) = NPC RENDER/TARGETING
//     array. SOLE WRITER: cGameData_LoadZoneNpcInfo 0x5578E0, which copies into it the
//     per-zone STATIC NPC table mZONENPCINFO 0x14AA930 (mNPC id, position, angle). NO ground
//     item is ever written to it — no ground-item network handler touches it.
//   - ALL its readers treat it as NPC: Npc_DrawMesh 0x57FF00, Npc_RenderSlotTick 0x5803A0,
//     Scene_RayHitNpcBox 0x541680, World_PickEntityAtCursor 0x538AB0 (loop j), and click
//     category 4 routes to Npc_ApproachAndInteract @0x53723F (0x539DC0) then
//     UI_NpcWin_Open 0x5DB530 — an NPC SERVICE window, consistent with an NPC, not
//     with a ground pickup.
//   - Loot bags are ELSEWHERE: click category 6 -> Npc_Interact @0x536AB9 on
//     dword_17AB534 (stride 152, bounded by dword_1687228), via Scene_RayHitItemModel 0x5418B0
//     @0x539129.
//
// CONSEQUENCE — SUSPECT MODULE, TO BE REEVALUATED (flagged to the orchestrator, NOT fixed
// here): the premise of `FindNearestGroundItem` (looking for a ground item in pool
// 0x1764D14) is WRONG: this pool only contains NPCs. Per fidelity policy ("no opportunistic
// refactor", "a stub of the binary stays a stub"), W7 only CORRECTS THE TYPING and this
// banner; the module's semantics are left UNCHANGED. EAs 0x539EC0/0x539DC0/0x5B1650 cited
// below remain valid in themselves — it's their ATTACHMENT to a "ground item" notion that
// needs to be re-judged.
// TODO [anchor 0x538AB0 / 0x539DC0]: re-decompile Item_PickupTarget/Item_InteractGround to
// determine whether they are actually the NPC APPROACH/INTERACTION functions, their IDA
// names "Item_*" then being the false friends, symmetrically to Npc_ApproachAndInteract
// 0x539DC0 which shares the SAME EA as "Item_InteractGround" above — a strong hint of a
// naming duplicate.
//
// See Docs/TS2_GAMEPLAY_LOGIC.md, memory docs ts2-entity-model / ts2-gameplay-logic.
#pragma once
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/EntityManager.h"
#include <cstdint>

namespace ts2::game {

// Ground pickup.

// Range threshold faithful to the binary: Math_Dist3D(target, player) <= 20.0, hardcoded
// constant identical in both source functions:
//   Item_PickupTarget   EA 0x539eef : if (Math_Dist3D(...) <= 20.0)
//   Item_InteractGround EA 0x539def : if (Math_Dist3D(...) <= 20.0)
inline constexpr float kPickupRange = 20.0f;

// Overflow guard (Util_SumExceeds2Billion, EA 0x53f660):
//   return a2 + (__int64)a1 > 2000000000;
// used in this subsystem to validate a weight transfer BEFORE applying it
// (pattern verified in Npc_AutoInteract EA 0x53aa87: "if (Util_SumExceeds2Billion(g_InvWeight,
// itemWeight)) return 0;", and a related pattern in AutoPlay_ScanGroundItems EA 0x45e5d8 with
// a hardcoded threshold of 1,900,000,000 applied to g_InvWeight when filtering auto-loot items).
//
// IMPORTANT (fidelity): NEITHER Item_PickupTarget (EA 0x539ec0) NOR Item_InteractGround
// (EA 0x539dc0) contain any weight/capacity check in their disassembly — the range guard
// (20.0) is the ONLY "before pickup" validation present in these two specific functions.
// The weight guard below is reproduced from the closest verified item-transfer logic in
// the binary (cited above) to satisfy this mission's weight-validation requirement.
// TODO (EA 0x539ec0 / 0x539dc0): confirm dynamically whether a separate weight cap is
// applied server-side at the actual moment of pickup (no client-side clue in these two
// functions).
inline constexpr int64_t kWeightOverflowGuard = 2000000000; // EA 0x53f660

// Resolved pickup target.
struct GroundPickupTarget {
    // Index into g_World.npcRenderEntries (== a1 of Item_InteractGround). This is a SLOT
    // index of pool 0x1764D14, stable and aligned with mZONENPCINFO[i] since W7 (the pool has
    // 100 fixed slots, no more compaction) -- this is exactly the index the binary sends to
    // the server: Net_QueueRunTo(..., 4, index, ...) @0x539E78, index resolved by
    // World_PickEntityAtCursor (`*a4 = j` @0x538E8F).
    int             index    = -1;
    // nullptr if no active slot found. Retyped by W7: GroundItem -> NpcRenderEntry
    // (Game/GameState.h) -- same type, name corrected (cf. header banner: this pool
    // holds NPCs, not ground items).
    NpcRenderEntry* item     = nullptr;
    float           distance = 0.0f; // 3D distance to the local player (Math_Dist3D, EA 0x53faa0)
};

// Finds the nearest active ground item to the local player (g_World.Self()).
//
// CLEAN rewrite (not byte-exact): the binary does NOT "search" for the target in
// Item_PickupTarget/Item_InteractGround — these functions consult an index already resolved
// by the mouse click (Game_OnWorldLeftClick EA 0x536690 -> World_PickEntityAtCursor EA
// 0x538ab0) and stored on the self side (dword_1687354, "g_SelfAttackOrder_GridX" — reused
// here as the pickup target index, cf. Char_TickLootPickupState EA 0x57ca50). This models
// an explicit nearest-neighbor resolution to allow a standalone call (auto-pickup /
// keyboard shortcut) without depending on the UI click state, which is out of scope for
// this mission (screen rendering/cursor/raycast, EA 0x538ab0).
GroundPickupTarget FindNearestGroundItem(GameWorld& world);

// Faithfully reproduces the range guard of Item_PickupTarget/Item_InteractGround:
// 3D distance to the local player <= 20.0 (EA 0x539eef / 0x539def, kPickupRange). Returns
// false if `target` is not active or if the distance exceeds the threshold.
bool IsWithinPickupRange(const GameWorld& world, const NpcRenderEntry& target);

// Reproduces the overflow guard pattern (Util_SumExceeds2Billion, EA 0x53f660)
// applied when adding an item's weight to the current inventory weight. Returns true if
// the pickup MUST be refused (currentWeight + addedItemWeight > kWeightOverflowGuard).
bool WouldExceedWeightCapacity(int64_t currentWeight, int64_t addedItemWeight);

// Outcome of evaluating a pickup attempt.
enum class PickupOutcome {
    Ok,                 // target in range and weight ok: ready to send the network request
    NoTarget,            // no active slot nearby (no match in g_World.npcRenderEntries)
    OutOfRange,          // outside the 20.0 radius (EA 0x539eef / 0x539def)
    WouldExceedWeight,   // would exceed the overflow guard (EA 0x53f660)
};

// High-level entry point: resolves the nearest pickup target then validates
// range (EA 0x539eef/0x539def) and weight (EA 0x53f660, cf. WouldExceedWeightCapacity).
// Fills `outTarget` with the resolved target (even on failure, for UI diagnostics).
//
// NO NETWORK SEND HERE. In the binary, a confirmed pickup (range ok) only triggers
// Net_QueueMoveResume (EA 0x511870, stops the movement queue) then
// UI_NpcWin_Open (EA 0x5db530, opens a window — builds a service menu from the target;
// NO Net_SendPacket_Op* is called in Item_PickupTarget or Item_InteractGround). The pickup
// confirmation network request is presumably emitted by the selection/OK callback of that
// window (rendering/UI, out of this mission's scope — TODO EA 0x5db530: trace the callbacks
// of the window opened by UI_NpcWin_Open to identify the exact Net_Send* builder for the
// pickup). See also Inv_RemoveItemQuantity (EA 0x5b0340, called by the quantity dialog
// below): this function does ONLY local bookkeeping (decrementing the inventory arrays)
// and also emits NO network packet.
PickupOutcome EvaluatePickup(GameWorld& world, GroundPickupTarget& outTarget,
                              int64_t itemWeight = 0);

// Quantity dialog (stack split) — Item_QtyDialog_OnLButtonUp, EA 0x5b1650.
//
// The binary repeats EXACTLY the same clamping algorithm for each container category
// (switch on this[4]: cases 1, 5, 6, 7, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF, 0x15,
// 0x17..0x1A) — only the source of the "max available" changes (main inventory grid,
// ground pickup grid / g_Container5, quiver, weight, warehouse, stall...). The common
// algorithm, illustrated by case 1 (main inventory grid, EA 0x5b1705-0x5b179b), is:
//
//   quantite = aSaisieUtilisateur ? quantiteDemandee : maxDisponible;  // EA 0x5b1713/0x5b1731
//                                                                       // (this[19]>=1 ? atoi(buf) : maxDisponible)
//   if (quantite > maxDisponible) quantite = maxDisponible;            // EA 0x5b1767/0x5b1785
//   if (quantite < 1) -> cancel (equiv. sub_5B02D0, EA 0x5b1791),      // EA 0x5b178c
//       the dialog closes without calling Inv_RemoveItemQuantity;
//   else -> quantity validated, stored, then Inv_RemoveItemQuantity(this) is called.
//
// This same pattern recurs identically in the 14 other switch cases (e.g. case 5,
// ground pickup grid: EA 0x5b17ff-0x5b188a, source = dword_1674400[42*row+3*col],
// EXACTLY EntityManager::GroundPickupSlot::count; cases 7 and 0x15: fixed cap 99,
// EA 0x5b194d-0x5b198a and EA 0x5b1d4c-0x5b1d89).
//
// Returns the quantity clamped to [1, maxDisponible], or 0 if the request is invalid
// (maxDisponible <= 0, or resolved quantity < 1) — in the binary this case closes the dialog
// via sub_5B02D0(this) instead of calling Inv_RemoveItemQuantity.
//
// `aSaisieUtilisateur` reproduces the `this[19] >= 1` guard (dialog input field length,
// EA 0x5b1713): true = a quantity was typed by the player (quantiteDemandee is then the
// value already converted via Crt_Atoi, EA 0x7603a6); false = no input, the binary then
// uses `maxDisponible` directly as the starting value (the dialog's default behavior when
// it opens: quantity pre-filled at the maximum). The keyboard/text input itself
// (slider/dialog rendering) is out of scope for this mission.
int32_t ValidateSplitQuantity(int32_t maxDisponible, int32_t quantiteDemandee,
                               bool aSaisieUtilisateur = true);

// Convenience variant for a main inventory grid cell (case this[4]==1 of the binary,
// EA 0x5b1705-0x5b179b: g_InvGrid_Count[384*row+6*col]; the counter is stored
// in InvCell::flag, cf. ClientRuntime.h InventoryState::Set()).
int32_t ValidateSplitQuantity(const InvCell& cellule, int32_t quantiteDemandee,
                               bool aSaisieUtilisateur = true);

// Convenience variant for a ground pickup grid slot (case this[4]==5 of the binary,
// EA 0x5b17ff-0x5b188a: dword_1674400[42*conteneur+3*slot], EXACTLY
// EntityManager::GroundPickupSlot::count, cf. EntityManager.h / g_Container5_ItemId).
int32_t ValidateSplitQuantity(const GroundPickupSlot& cellule, int32_t quantiteDemandee,
                               bool aSaisieUtilisateur = true);

} // namespace ts2::game

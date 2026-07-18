// Game/ConsumableBarLogic.h — interaction logic for the 28-CELL CONSUMABLE PANEL
// (slot assignment/trigger, NOT pixel rendering).
//
// =============================================================================
// WARNING: THIS MODULE DOES **NOT** MODEL THE HUD BAR (W9 clarification, 2026-07-16)
// =============================================================================
// Two DISTINCT objects in the binary were long confused here:
//
//   (a) the 28-cell PANEL           — UI_ConsumableBar_* 0x68E270+: 4 x 7
//       grid, 52 px stride, FIXED item catalog, close button, visibility guard
//       `*(this+2)`, opens a DRAG (Item_BeginDragTransaction 0x5AFDF0). This is THIS
//       module. Its RENDERING (UI_ConsumableBar_Render 0x68E6E0) is not ported, nor is
//       its caller: the functions below therefore have NO real caller today, except
//       TriggerSlot/TriggerSlotByHotkey, called by UI/ConsumableBarWindow (see below).
//
//   (b) the HUD BAR ("quickbar")    — UI_GameHud_Render 0x67A3C0, block
//       0x684CA8-0x685177: 1 x 14 grid, 30 px stride, NO private catalog (it reads
//       `g_Container5` @0x16743FC: 3 pages x 14 slots x 3 dwords), 3 slot types
//       (skill/tab/item), recharge overlay, page number. It is ported by
//       UI/ConsumableBarWindow.{h,cpp}, which now reads `g_Container5` DIRECTLY and
//       no longer uses this module for its rendering.
//
// Practical consequence: the grid constants (kGridColumns=4, kGridStride=52…),
// `InitConsumableBar` (fixed catalog), `HitTestConsumableBar`, `OnClick`,
// `OnMouseUp`, `OnRightClick` and `ConsumableBarState` describe (a) and MUST NOT be
// used for (b). They stay here, without a caller, pending the port of the 28-cell
// panel — a faithful port of a binary stub stays a stub (we don't delete a faithful
// port just because its caller isn't written yet).
//
// The REAL click path of the HUD bar is cGameHud_OnMouseDown 0x62B080: NOT reverse
// engineered to date. `UI/ConsumableBarWindow::OnClick` therefore still calls
// `TriggerSlot` below for lack of anything better — this is a documented stopgap,
// NOT evidence for the HUD bar.
//
// -----------------------------------------------------------------------------
// CLEAN but faithful C++ rewrite of the following functions from the unpacked
// TwelveSky2.exe binary (imagebase 0x400000, IDB idaTs2):
//   UI_ConsumableBar_Init         0x68E270  -> InitConsumableBar
//   UI_ConsumableBar_HitTest      0x68E9D0  -> HitTestConsumableBar
//   UI_ConsumableBar_OnClick      0x68E3C0  -> OnClick (+ TriggerSlot)
//   UI_ConsumableBar_ProcInput    0x68E5A0  -> OnMouseUp
//   UI_ConsumableBar_OnRightClick 0x68E940  -> OnRightClick (+ TriggerSlot)
//   sub_68E3A0 (unnamed helper, called by ProcInput)                 -> inlined into OnMouseUp
//   UI_ConsumableBar_Render       0x68E6E0  -> OUT OF SCOPE (pixel rendering, see banner at bottom of file)
//
// BINARY <-> MODULE GAP (read before using this file):
// The original panel (0x68E270) is a FIXED CATALOG of 28 cells (only 14 populated
// by Init; the next 14 remain empty/reserved), which displays ONLY ITEM_INFO (none
// of the 6 functions ever references SKILL_INFO) and serves to open a DRAG
// (Item_BeginDragTransaction, EA 0x5AFDF0) toward the inventory or the real hotbar —
// it is NOT itself a hotkey/cooldown editor. The mission nonetheless requires a
// generic "item/skill" decision engine operating on ts2::ui::QuickSlot (10 cells,
// already declared in UI/GameHud.h, which explicitly provisions
// QuickSlotType::Skill and documents the DIK 0x02..0x0B hotkeys in a comment): that
// is exactly the hook left as a TODO by UI/GameHud.cpp::OnMouseDown ("trigger slot
// usage"). This module therefore provides THAT engine, faithful to the binary's
// guards for the Item part (the only branch actually present in the EAs above),
// and explicitly marks the Skill branch Unsupported/TODO: none of the 6 assigned
// EAs resolve a skill, and the real hotbar-trigger-with-cooldown function
// (distinct from UI_QuickSlot_AssignHotkey 0x5BDF00, which only does auto-hunt
// configuration, and cQuickSlotWin_* 0x65F4F0-0x6627F0, which manages the edit
// window but not — verified — hotkey execution) was not decompiled as part of
// this task.
#pragma once
#include <array>
#include <cstdint>

#include "UI/GameHud.h"

namespace ts2::game {

using ConsumableSlots = std::array<ui::QuickSlot, ui::kQuickSlotCount>;

// Action decided by TriggerSlot/OnClick/OnMouseUp/OnRightClick. Describes the
// intent WITHOUT producing a side effect (no network send, no real
// Item_BeginDragTransaction/Item_DrawTooltip call): it's up to the caller to
// materialize the decision.
enum class ConsumableAction : uint8_t {
    None,           // nothing to do (empty slot, off-grid, or failed guard)
    Ignored,        // bar hidden -> event not consumed (EA 0x68E3CD/0x68E5AB/0x68E94C)
    Invalid,        // assigned itemId not found in the ITEM_INFO database (EA 0x68E48F/0x68E491)
    BeginItemDrag,  // left-click on a full Item slot -> Item_BeginDragTransaction (EA 0x68E46E..0x68E545)
    ShowTooltip,    // right-click on a full slot -> Item_DrawTooltip (EA 0x68E9B2)
    ArmCloseButton, // left-click on the close button -> arm it (EA 0x68E56C/0x68E588)
    ClosePanel,     // armed release on the close button -> closes the panel (EA 0x68E654/0x68E667 + sub_68E3A0)
    Unsupported,    // Skill-type slot: out of scope of the RE EAs, see banner above
};

struct ConsumableDecision {
    ConsumableAction action = ConsumableAction::None;
    int      slotIndex      = -1;
    uint32_t refId          = 0;     // itemId (or skillId for Unsupported)
    bool     consumed       = false; // original int return value (0/1): event consumed?

    // Only set for BeginItemDrag, typeCode==2 branch (EA 0x68E4A5):
    bool promptQuantity = false; // byte_8013FE < 0 (EA 0x68E4B4) — exact flag meaning TODO
    int  dragCount      = 0;     // 0 = quantity requested (prompt), 99 = fixed quantity (EA 0x68E4E6/0x68E513/0x68E540)
    int  dragCursorX    = 0;     // a2-52 (EA 0x68E4E6), set only if promptQuantity
    int  dragCursorY    = 0;     // a3-72 (EA 0x68E4E6), set only if promptQuantity

    bool usable = false; // mission extension (not in the EAs): see IsSlotUsable/InventoryCount
};

// Complementary state to the QuickSlot array: what the ConsumableBar object
// tracks besides the catalog (EA 0x68E270+).
struct ConsumableBarState {
    bool visible          = false; // *(this+2) — panel shown/active
    bool closeButtonArmed = false; // *(this+3) — close button pressed (mousedown), pending mouseup
};

// EA 0x68E270 — initializes `slots`: clears then fills the fixed default item
// catalog (potions/scrolls). The binary populates 14 of the 28 cells (the next 14
// remain 0/empty); only the first ui::kQuickSlotCount (10) values fit in this
// type. The last 4 values of the original catalog (index 10..13: itemId 1241,
// 1244, 1242, 1243 — EA 0x68E308/0x68E312/0x68E31C/0x68E326) are therefore
// truncated here for lack of room (TODO if kQuickSlotCount should ever cover
// all 14 original entries).
void InitConsumableBar(ConsumableSlots& slots);

// Grid constants taken as-is from UI_ConsumableBar_HitTest
// (EA 0x68EA2D..0x68EA65): 4 columns, 52 px stride, active cell
// [x+11,x+61] x [y+37,y+87] relative to the anchor (originX, originY).
inline constexpr int kGridColumns = 4;
inline constexpr int kGridStride  = 52;
inline constexpr int kGridCellX0  = 11, kGridCellX1 = 61;
inline constexpr int kGridCellY0  = 37, kGridCellY1 = 87;

// EA 0x68E9D0 — grid hit-test. `originX`/`originY` = screen anchor ALREADY
// resolved by rendering (the anchor computation via UI_ProjectSpriteToScreen
// 0x50F5D0 + Sprite2D_GetWidth 0x4D6CD0 is a background-sprite-dependent
// recomputation, out of logical scope — TODO EA 0x68E9F4-0x68EA2A, to be handled
// by rendering). Returns -1 if no FULL cell is under (mouseX, mouseY) — an
// empty cell that is hit also returns -1 (EA 0x68EAE3, `*(this+i+4) <= 0` guard).
int HitTestConsumableBar(const ConsumableSlots& slots, int originX, int originY,
                          int mouseX, int mouseY);

// EA 0x68E46E..0x68E545 (OnClick full-slot branch) and EA 0x68E9B2 (OnRightClick)
// — decides the action for slot `index` WITHOUT executing it. `rightClick=false`
// => OnClick logic (prepares a drag); `true` => OnRightClick logic (tooltip, no
// existence check in the binary). Skill slot -> Unsupported (none of this
// module's EAs resolve a skill, see banner at the top of the file).
ConsumableDecision TriggerSlot(const ConsumableSlots& slots, int index, bool rightClick = false);

// EA 0x68E3C0 in full: visibility guard -> HUD parent short-circuit ->
// hit-test -> TriggerSlot, or close-button hit-test if no cell is hit.
// `closeButtonHit` = result of Sprite2D_HitTest(unk_8F3798, ...)
// (EA 0x68E56C), computed by rendering (sprite size not available here,
// out of scope). `parentHudConsumed` reproduces the cGameHud_OnMouseDown
// 0x62B080 short-circuit (EA 0x68E433), subsystem not implemented here.
ConsumableDecision OnClick(ConsumableBarState& state, const ConsumableSlots& slots,
                            int originX, int originY, int mouseX, int mouseY,
                            bool closeButtonHit, bool parentHudConsumed = false);

// EA 0x68E5A0 in full (mouse release). `closeButtonHit` = same hit-test as
// OnClick but evaluated at the release position (EA 0x68E654).
// `parentHudConsumed` reproduces cGameHud_OnMouseUp 0x62DFA0 (EA 0x68E611).
ConsumableDecision OnMouseUp(ConsumableBarState& state, bool closeButtonHit,
                              bool parentHudConsumed = false);

// EA 0x68E940 in full (right-click -> tooltip). `tooltipDispatchConsumed`
// reproduces cGameHud_DrawTooltipDispatch 0x64EA30 (EA 0x68E963).
ConsumableDecision OnRightClick(const ConsumableBarState& state, const ConsumableSlots& slots,
                                 int originX, int originY, int mouseX, int mouseY,
                                 bool tooltipDispatchConsumed = false);

// ---------------------------------------------------------------------------
// Extensions requested by the mission ("disable if item insufficient/skill
// unavailable") — ABSENT from the 6 RE EAs (the original catalog never queries
// the owned inventory, only the static ITEM_INFO table). Implemented here via
// game::g_World to make TriggerSlot usable by a real inventory-driven hotbar.
// ---------------------------------------------------------------------------

// Owned quantity of `itemId` in the local player's inventory (sum of
// game::g_Client.inv cells whose itemId matches; `flag` field = counter,
// see Game/ClientRuntime.h InventoryState::Set — source-of-truth model, NOT
// game::g_World.self.inventory, removed during the reconciliation of the two
// competing inventory models, "inventory" mission, 2026-07-14).
uint32_t InventoryCount(uint32_t itemId);

// True if the slot is triggerable right now: Item -> known in the ITEM_INFO
// database AND owned (>=1 copy); Skill -> always false here
// (TODO: needs Game/SkillSystem.h, outside this module's header contract,
// + the real cooldown/hotkey function not located, see banner at the top).
// NO CALLER since W9 (2026-07-16): its only consumer tinted "missing item"
// cells red on the HUD bar — an INVENTED effect (the binary draws no rectangle
// on this bar, see UI/ConsumableBarWindow.h), removed along with the rest of
// the non-faithful rendering. Kept as a declared extension point.
bool IsSlotUsable(const ConsumableSlots& slots, int index);

// Numeric-key trigger: mapping DIK_1..DIK_9 = 0x02..0x0A,
// DIK_0 = 0x0B -> slot index 0..9 (convention already documented by
// UI/GameHud.h lines 27-30 and Input/InputSystem.h; not included here to stay
// self-contained). None of this module's 6 RE EAs read the keyboard directly —
// this mapping is taken from the existing convention, NOT translated from an
// EA of the ConsumableBar panel. Returns {None} if `dikScanCode` is out of range.
ConsumableDecision TriggerSlotByHotkey(const ConsumableSlots& slots, uint8_t dikScanCode);

} // namespace ts2::game

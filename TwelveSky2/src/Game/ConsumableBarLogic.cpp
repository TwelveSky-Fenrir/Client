// Game/ConsumableBarLogic.cpp — see Game/ConsumableBarLogic.h for the EA <-> function
// mapping table and the binary/module gap banner.
//
// WARNING: This file implements the 28-CELL PANEL (UI_ConsumableBar_* 0x68E270+, 4x7
// grid, stride 52), NOT the HUD bar (UI_GameHud_Render 0x67A3C0 @0x684CA8-0x685177, 1x14
// grid, stride 30, source g_Container5 0x16743FC) — the latter is ported by
// UI/ConsumableBarWindow.{h,cpp}, which reads g_Container5 directly. See the
// "THIS MODULE DOES NOT MODEL THE HUD BAR" banner at the top of the header before
// calling anything from here.
#include "Game/ConsumableBarLogic.h"

#include <cstddef>

#include "Game/GameState.h"     // game::InvCell
#include "Game/GameDatabase.h"  // game::GetItemInfo
#include "Game/ClientRuntime.h" // game::g_Client.inv (InventoryState) + g_Client.Var (byte_8013FE)

namespace ts2::game {

namespace {

// EA 0x68E2A4..0x68E2FE: the first 10 of the 14 values in the original fixed
// catalog (potion/scroll itemId). The next 4 (1241, 1244, 1242, 1243 — EA
// 0x68E308/0x68E312/0x68E31C/0x68E326) are truncated: the ts2::ui::QuickSlot
// type only holds ui::kQuickSlotCount (10) cells.
constexpr std::array<uint32_t, 10> kDefaultCatalog = {
    540, 565, 541, 542, 543, 544, 545, 546, 539, 1240,
};

} // namespace

void InitConsumableBar(ConsumableSlots& slots) {
    // EA 0x68E279..0x68E297: zeroes the binary's 28 cells -> here the
    // ui::kQuickSlotCount cells of the array.
    for (auto& s : slots) s = ui::QuickSlot{};

    // EA 0x68E2A4..0x68E2FE (truncated, see kDefaultCatalog above).
    for (std::size_t i = 0; i < slots.size() && i < kDefaultCatalog.size(); ++i) {
        slots[i].type  = ui::QuickSlotType::Item;
        slots[i].refId = kDefaultCatalog[i];
    }
}

int HitTestConsumableBar(const ConsumableSlots& slots, int originX, int originY,
                          int mouseX, int mouseY) {
    // EA 0x68EA2D..0x68EA65: loop until the first cell whose rectangle contains
    // (mouseX, mouseY), or the end of the array.
    for (int i = 0; i < static_cast<int>(slots.size()); ++i) {
        const int col = i % kGridColumns;
        const int row = i / kGridColumns;
        const int x0 = originX + kGridStride * col + kGridCellX0;
        const int x1 = originX + kGridStride * col + kGridCellX1;
        const int y0 = originY + kGridStride * row + kGridCellY0;
        const int y1 = originY + kGridStride * row + kGridCellY1;
        if (mouseX >= x0 && mouseX <= x1 && mouseY >= y0 && mouseY <= y1) {
            // EA 0x68EAE3: cell hit but empty -> -1 anyway.
            return slots[static_cast<std::size_t>(i)].empty() ? -1 : i;
        }
    }
    return -1; // EA 0x68EAD3: no cell hit.
}

ConsumableDecision TriggerSlot(const ConsumableSlots& slots, int index, bool rightClick) {
    ConsumableDecision d;
    d.slotIndex = index;
    if (index < 0 || index >= static_cast<int>(slots.size())) return d; // out of bounds -> None

    const ui::QuickSlot& slot = slots[static_cast<std::size_t>(index)];
    if (slot.empty()) return d; // None, no original EA acts on an empty cell

    d.refId = slot.refId;

    if (slot.type == ui::QuickSlotType::Skill) {
        // See gap banner at the top of Game/ConsumableBarLogic.h: none of this
        // module's 6 EAs resolve a SKILL_INFO.
        d.action   = ConsumableAction::Unsupported;
        d.consumed = true;
        return d;
    }

    // slot.type == Item.
    if (rightClick) {
        // EA 0x68E9B2: Item_DrawTooltip(a2, a3, itemId, 0, 0, 2, 0, 0, 0),
        // called without checking the ITEM_INFO entry exists.
        d.action   = ConsumableAction::ShowTooltip;
        d.consumed = true;
        return d;
    }

    // EA 0x68E480..0x68E491: MobDb_GetEntry(&mITEM, itemId).
    const ItemInfo* info = GetItemInfo(slot.refId);
    if (!info) {
        // EA 0x68E48F/0x68E491: entry not found -> click consumed, no effect.
        d.action   = ConsumableAction::Invalid;
        d.consumed = true;
        return d;
    }

    d.action   = ConsumableAction::BeginItemDrag;
    d.consumed = true;

    if (info->typeCode == 2) { // EA 0x68E4A5: *(v6+188) == 2
        // EA 0x68E4B4: branch driven by byte_8013FE (exact meaning TODO — not
        // documented elsewhere in the disassembly reviewed for this mission).
        // Exposed faithfully via the ClientRuntime escape hatch.
        const bool negative = g_Client.VarGet(0x8013FEu) < 0;
        if (negative) {
            // EA 0x68E4E6: Item_BeginDragTransaction(..., itemId, 0,0,0,0,0,
            // 1, a2-52, a3-72) -> drag with a quantity prompt near the cursor.
            d.promptQuantity = true;
            d.dragCount      = 0;
        } else {
            // EA 0x68E513: Item_BeginDragTransaction(..., itemId, 99, 0,0,0,0,
            // 0,0,0) -> drag of a fixed quantity (full stack of 99).
            d.promptQuantity = false;
            d.dragCount      = 99;
        }
    } else {
        // EA 0x68E540: Item_BeginDragTransaction(..., itemId, 0,0,0,0,0,0,0,0).
        d.promptQuantity = false;
        d.dragCount      = 0;
    }

    d.usable = InventoryCount(slot.refId) > 0; // mission extension, not tied to an EA
    return d;
}

ConsumableDecision OnClick(ConsumableBarState& state, const ConsumableSlots& slots,
                            int originX, int originY, int mouseX, int mouseY,
                            bool closeButtonHit, bool parentHudConsumed) {
    ConsumableDecision d;

    if (!state.visible) { // EA 0x68E3CD
        d.action = ConsumableAction::Ignored;
        return d; // consumed=false: event not consumed
    }

    // EA 0x68E433: cGameHud_OnMouseDown short-circuits before any hit-test.
    // HUD subsystem out of scope here — reproduced via the parameter.
    if (parentHudConsumed) {
        d.consumed = true;
        return d;
    }

    const int slot = HitTestConsumableBar(slots, originX, originY, mouseX, mouseY);
    if (slot != -1) {
        d = TriggerSlot(slots, slot, /*rightClick=*/false);
        if (d.action == ConsumableAction::BeginItemDrag && d.promptQuantity) {
            d.dragCursorX = mouseX - 52; // EA 0x68E4E6
            d.dragCursorY = mouseY - 72;
        }
        return d;
    }

    // EA 0x68E45D..0x68E596: no full cell hit -> test the close button. The
    // actual rectangle depends on the size of sprite unk_8F3798
    // (Sprite2D_HitTest 0x4D6C50, EA 0x68E56C): out of logical scope,
    // supplied by the caller (rendering) via `closeButtonHit`.
    if (closeButtonHit) {
        state.closeButtonArmed = true; // EA 0x68E588
        d.action   = ConsumableAction::ArmCloseButton;
        d.consumed = true;
        return d;
    }

    return d; // EA 0x68E596: None, not consumed
}

ConsumableDecision OnMouseUp(ConsumableBarState& state, bool closeButtonHit,
                              bool parentHudConsumed) {
    ConsumableDecision d;

    if (!state.visible) { // EA 0x68E5AB
        d.action = ConsumableAction::Ignored;
        return d;
    }

    if (parentHudConsumed) { // EA 0x68E611: cGameHud_OnMouseUp
        d.consumed = true;
        return d;
    }

    if (!state.closeButtonArmed) return d; // EA 0x68E624: return 0, not consumed

    state.closeButtonArmed = false; // EA 0x68E62D

    if (closeButtonHit) {
        // EA 0x68E654/0x68E667 -> sub_68E3A0(this): resets *(this+2) to 0.
        state.visible  = false;
        d.action       = ConsumableAction::ClosePanel;
    }

    d.consumed = true; // EA 0x68E675: always 1 once the button is armed
    return d;
}

ConsumableDecision OnRightClick(const ConsumableBarState& state, const ConsumableSlots& slots,
                                 int originX, int originY, int mouseX, int mouseY,
                                 bool tooltipDispatchConsumed) {
    ConsumableDecision d;

    if (!state.visible) return d; // EA 0x68E94C: not consumed

    if (tooltipDispatchConsumed) { // EA 0x68E963: cGameHud_DrawTooltipDispatch
        d.consumed = true;
        return d;
    }

    const int slot = HitTestConsumableBar(slots, originX, originY, mouseX, mouseY);
    if (slot == -1) return d; // EA 0x68E98A: not consumed

    return TriggerSlot(slots, slot, /*rightClick=*/true); // EA 0x68E9B2
}

uint32_t InventoryCount(uint32_t itemId) {
    // Single model game::g_Client.inv (InventoryState, Game/ClientRuntime.h) —
    // NOT game::g_World.self.inventory (old "simplified model", removed during
    // the reconciliation of the two competing inventory models, "inventory"
    // mission, 2026-07-14). Scans all cells (all pages combined): cheap
    // (2048 InvCell) and no risk of under-counting an item stored on a page
    // currently not displayed by the UI.
    uint32_t total = 0;
    for (const InvCell& cell : g_Client.inv.cells) {
        if (cell.itemId == itemId) total += cell.flag; // flag = stack counter
    }
    return total;
}

bool IsSlotUsable(const ConsumableSlots& slots, int index) {
    if (index < 0 || index >= static_cast<int>(slots.size())) return false;
    const ui::QuickSlot& slot = slots[static_cast<std::size_t>(index)];
    if (slot.empty()) return false;

    if (slot.type == ui::QuickSlotType::Skill) {
        // TODO: needs Game/SkillSystem.h (outside this module's header
        // contract) and the real cooldown/hotkey function, not located for
        // this mission.
        return false;
    }

    const ItemInfo* info = GetItemInfo(slot.refId);
    if (!info) return false;
    return InventoryCount(slot.refId) > 0;
}

ConsumableDecision TriggerSlotByHotkey(const ConsumableSlots& slots, uint8_t dikScanCode) {
    // DIK_1..DIK_9 = 0x02..0x0A, DIK_0 = 0x0B (see Input/InputSystem.h,
    // UI/GameHud.h lines 27-30, Docs/TS2_CLIENT_SHELL.md §4). Convention
    // already established elsewhere in this codebase — NOT a translation of
    // an EA from this module (none of the 6 ConsumableBar EAs read the keyboard).
    if (dikScanCode < 0x02 || dikScanCode > 0x0B) return ConsumableDecision{};
    const int index = static_cast<int>(dikScanCode) - 0x02; // DIK_1->0 .. DIK_0->9
    return TriggerSlot(slots, index, /*rightClick=*/false);
}

} // namespace ts2::game

// UI/ItemTooltip.h — item tooltip. Transposition of Item_DrawTooltip 0x652AD0.
//
// -----------------------------------------------------------------------------
// PREMISE CORRECTION (W9 revision) — this file's old banner justified a
// freely-invented widget by claiming "the original client displays NO item
// tooltip whatsoever". That is FACTUALLY FALSE, and the error served as an
// excuse for hardcoded French labels, literal D3DCOLORs, and a background
// FillRect. Ground truth:
//   - Item_DrawTooltip 0x652AD0 EXISTS and has 14+ callers (`xrefs_to`: Shop,
//     Warehouse, ShareBox, Consign, ItemList, Storage x3, Refine, NpcShop,
//     SkillBoard, CharSelect x3...).
//   - It is reached EVERY FRAME: UI_RenderAllDialogs 0x5AE2D0 ends with an
//     UNCONDITIONAL tail call `call UI_RouteRButtonExamine` @0x5ae5c9 followed by
//     `mov esp,ebp / pop ebp / retn` @0x5ae5ce — no guard.
//   - UI_RouteRButtonExamine 0x5AE5E0 (1 single caller: 0x5ae5c9) is a
//     first-responder chain of ~35 links; each hover leaf
//     (UI_Shop_ShowItemTooltip 0x5C9360 @0x5ae6b1, UI_Warehouse_ShowItemTooltip
//     0x5CB4A0 @0x5ae702, cGameHud_DrawTooltipDispatch 0x64EA30 @0x5ae7a4,
//     cQuickSlotWin_DrawTooltip 0x6620E0 @0x5ae7bf...) returns 1 if it drew.
//
// The reconstruction below therefore follows the binary, NOT ergonomics: the
// actual tiled geometry, labels from 005.DAT, colors by palette INDEX. Main
// switch cases not proven line-by-line remain explicit TODOs rather than
// inventions (see UI/ItemTooltip.cpp).
//
// -----------------------------------------------------------------------------
// TODO [anchors 0x5AE2D0 / 0x5AE5E0] — WIRING OUTSIDE THIS FILE (gap TT-01).
// `DrawItemTooltip` has NO caller: without the hook below it remains dead code.
// UIManager::Render (UI/UIManager.cpp:286-327) already computes `cx, cy` (l.295)
// and renders dialogs in reverse order across 2 phases, but never runs the
// final tooltip-routing pass that, in the binary, follows dialog rendering.
// Hook to add, IN BOTH PHASES, right after the
// `for (auto it = dialogs_.rbegin(); ...)` loop of each pass (UIManager.cpp l.315
// for the Panels pass, l.325 for the Text pass):
//
//     // UI_RenderAllDialogs 0x5AE2D0 @0x5ae5c9: unconditional tail call to
//     // UI_RouteRButtonExamine 0x5AE5E0 (first-responder chain).
//     RouteItemTooltip(ctx_, cx, cy);
//
// where `RouteItemTooltip` queries each grid window in routing order for the
// hovered cell, and stops at the first one that responds — mirroring the
// 0x5AE5E0 chain. Cell resolution itself belongs to each window
// (InventoryWindow::InvCellAt already exists, InventoryWindow.cpp:227); on the
// HUD side the binary does NOT route by cell in a generic pass:
// cGameHud_DrawTooltipDispatch 0x64EA30 resolves Equip/Quiver/Loot/Inv itself
// and is guarded by visible (this[175]) + tab (this[226]==1), while the shop
// (UI_Shop_ShowItemTooltip 0x5C9360) is PURE hover, with no visibility guard
// (only `if (!*(this+2)) return 0`). These files are outside this front's
// scope: the router must be written by UIManager.cpp's owner.
#pragma once
#include "UI/UIManager.h"
#include "Game/GameDatabase.h"
#include "Game/GameState.h"
#include "Game/ItemSystem.h"
#include <cstdint>

namespace ts2::ui {

// Draws the tooltip for item `itemId`, anchored at (x,y) [client cursor
// position]. `socketWord` = the cell's bit-packed attribute word (grade/gem/
// enchant); it's Item_DrawTooltip's arg_10, whose prologue decodes the FOUR
// bytes (Item_GetAttribByte0..3 0x545610/0x545640/0x545670/0x5456A0, EA 0x652B1C /
// 0x652B2D / 0x652B4B / 0x652B7C). Pass 0 for a bare item — that's exactly what
// the shop does: `Item_DrawTooltip(x, y, itemId, 0, 0, 1, 0, 0, 0)` @0x5c9471.
//
// Draws nothing if `itemId` resolves to no ITEM_INFO (MobDb_GetEntry @0x652afa,
// nullptr -> direct jump to the exit @0x652b0e).
//
// THE ANCHOR IS NOT "to the right/below the cursor": the panel is placed TO
// THE LEFT of the cursor and CENTERED VERTICALLY on it
// (`x -= cols*13` @0x65e262-0x65e26a, `y -= (rows*15)/2` @0x65e276-0x65e283), with
// no screen clamp at all. See the .cpp.
//
// Must be called in BOTH phases (UiPhase::Panels then UiPhase::Text) — like all
// ctx draw calls, the primitives self-filter based on ctx.phase.
void DrawItemTooltip(const UiContext& ctx, int x, int y, uint32_t itemId,
                     uint32_t socketWord = 0);

// Convenience for grid windows: `InvCell::color` IS the attribute word
// (see Net/ItemActionDispatch.cpp: `eq.socket = cell.color;`). No-op if the cell
// is empty.
void DrawItemTooltip(const UiContext& ctx, int x, int y, const game::InvCell& cell);

} // namespace ts2::ui

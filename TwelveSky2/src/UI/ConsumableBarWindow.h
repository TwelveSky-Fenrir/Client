// UI/ConsumableBarWindow.h — rendering + interaction for the HUD shortcut bar
// ("quickbar"), port of UI_GameHud_Render 0x67A3C0, block 0x684CA8-0x685177.
//
// =============================================================================
// SLOT SOURCE OF TRUTH (fixed in wave W9, 2026-07-16) — READ BEFORE USE
// =============================================================================
// This widget NO LONGER reads the `slots` parameter: it reads `g_Container5`
// like the binary. Reason (proven by exhaustive grep + IDA):
//
//  1. `GameHud::slots_` (std::array<QuickSlot,10>, GameHud.h:270) has NO
//     writer anywhere in src/: 6 reads, 0 writes; `GameHud::Slot(int)`
//     (non-const accessor) has NO caller; `game::InitConsumableBar` has
//     NO caller. `slots_` therefore stays {Empty×10} forever -> with the
//     old rendering path, `s.empty()` was ALWAYS true and the bar only
//     drew empty frames. All the rest of the rendering logic was
//     unreachable (dead code).
//
//  2. The binary never consults a private catalog. It reads `g_Container5`,
//     a block of 3 pages x 14 slots x 3 dwords (page stride 0xA8 = 168 = 14*12,
//     slot stride 0xC):
//       itemId/skillId  0x16743FC + 0xA8*page + 0xC*i   [0x684E55, 0x684ECF,
//                                                        0x684FE7, 0x685061]
//       counter         0x1674400 + 0xA8*page + 0xC*i   [0x684EB2, 0x684F70,
//                                                        0x6850E4, 0x685103]
//       slot type       0x1674404 + 0xA8*page + 0xC*i   [0x684E0B -> var_8D4]
//       current page    dword_1675B1C                   [0x684D85, 0x684DF6]
//       selected slot   dword_1675B20                   [0x684E81]
//     Loop 0x684DE9: `cmp var_438, 0Eh / jge` -> **14 slots**, not 10.
//
//  3. This source IS already fed by the network on the C++ side:
//     Net/GameHandlers_InvCells.cpp:462-464 writes the three dwords via
//     `g_Client.Var(...)`; concurrent purge Net/CharStatDeltaDispatch.cpp:584-587.
//
// Consequence: the `slots` parameter of the public methods is KEPT (the
// signature stays compatible with the existing calls from GameHud.cpp:1127/1184/
// 1313-1314 — no rework of GameHud.cpp required) but is NO LONGER READ by
// Render(). It is still read by OnClick/OnRightClick/OnHotkey, see below.
//
// `ui::kQuickSlotCount` (=10, GameHud.h:62) contradicts the binary (14) and
// `QuickSlotType` (GameHud.h:65) ignores type 2 (see kSlotType* below):
// reading `g_Container5` internally works around both WITHOUT touching
// GameHud.h (outside this front's scope). Flagged to the orchestrator.
//
// =============================================================================
// WHAT THE BINARY DRAWS (and thus what this widget draws) — 0x684D40..0x685177
// =============================================================================
// ONLY atlas blits (`Sprite2D_Draw` x4) and two numbers
// (`UI_DrawNumberValue` x2). NO colored rectangle, NO frame, NO hover state,
// NO feedback message: all of that has been removed from the rendering path
// (it was invented). The bar is ALWAYS VISIBLE at the bottom of the screen, so
// NOT a ts2::ui::Dialog: nothing to route through UIManager, the caller
// (GameHud) pushes the events.
#pragma once
#include <array>
#include <cstdint>
#include <string>

#include "UI/UIManager.h"           // ts2::ui::UiContext
#include "UI/GameHud.h"             // ts2::ui::QuickSlot, kQuickSlotCount
#include "Game/ConsumableBarLogic.h" // ts2::game::TriggerSlot / TriggerSlotByHotkey / decisions
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"    // GPU icon cache for .IMG (same pattern as InventoryWindow/VendorShopWindow)

namespace ts2::ui {

// REAL number of slots on the HUD bar — loop 0x684DE9 `cmp var_438, 0Eh / jge
// loc_68515A`. Deliberately local to this widget: `ui::kQuickSlotCount` (=10)
// describes a DIFFERENT model (see header banner) and cannot be changed from
// this front.
inline constexpr int kBarSlotCount = 14;

// Values of the "slot type" dword (`dword_1674404`, switch 0x684E18-0x684E35).
// The binary has THREE branches; `ui::QuickSlotType` only knows two
// (Empty/Item/Skill), hence the use of the raw value here.
inline constexpr int32_t kSlotTypeSkill  = 1; // -> loc_684E40 (SKILL_INFO, cat.3 atlas)
inline constexpr int32_t kSlotTypeTabIcon= 2; // -> loc_684FD3 (cQuickSlotWin_GetTabIcon 0x662750)
inline constexpr int32_t kSlotTypeItem   = 3; // -> loc_68504C (ITEM_INFO, cat.2 atlas)

// Small utility class (NOT a Dialog: always visible, outside the UIManager
// routing chain). Just keeps the last on-screen layout (to align hit-test
// and rendering, like MsgBoxDialog::Layout).
class ConsumableBarWindow {
public:
    ConsumableBarWindow() = default;

    // Rendering (called TWICE per frame by the UI pipeline: Panels phase then
    // Text phase — see UIManager.h). `slots` is IGNORED (see header banner: the
    // real source is g_Container5); kept for call-site compatibility.
    // `cursorX`/`cursorY` are ignored too: the binary draws no hover state on
    // this bar.
    void Render(const UiContext& ctx, const std::array<QuickSlot, kQuickSlotCount>& slots,
                int cursorX = -1, int cursorY = -1);

    // Left-click down: arms the latch if the cursor is over the bar
    // (blocks the click from passing through to the 3D scene behind the HUD,
    // like GameHud::OnMouseDown / InventoryWindow::OnMouseDown). Does NOT
    // execute any action — the action is decided on release (OnClick), as in
    // the Button pattern from Widgets.h.
    bool OnMouseDown(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Left-click released = validated click -> game::TriggerSlot(slots, index).
    // Returns true if the event is consumed.
    bool OnClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Right-click = tooltip -> game::TriggerSlot(slots, index, /*rightClick=*/true).
    bool OnRightClick(int x, int y, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Direct keyboard shortcut (DIK_1..DIK_9=0x02..0x0A, DIK_0=0x0B), for
    // hooking into Input/InputSystem.h without going through the mouse.
    bool OnHotkey(uint8_t dikScanCode, const std::array<QuickSlot, kQuickSlotCount>& slots);

    // Last computed decision (BeginItemDrag/Invalid/Unsupported/...).
    const game::ConsumableDecision& LastDecision() const { return lastDecision_; }
    // Last message computed by the click path. NO LONGER DRAWN by
    // Render() (the binary shows no message above the bar):
    // kept for the caller/log (GameHud.cpp:1317 already logs it).
    const std::string& LastMessage() const { return lastMessage_; }

    // SHARED GPU icon cache (memory pooling, cf. Gfx/IconTextureCache.h).
    // On the binary side, AssetMgr_InitAllSlots 0x4DEB50 builds a SINGLE global array of
    // Sprite2D slots (base 0x8E8B30): HUD/inventory/warehouse/shop all index into it
    // -> each .IMG is loaded ONCE. To reproduce this pooling, the caller must inject
    // here the same instance as InventoryWindow/WarehouseWindow/EnchantWindow/
    // VendorShopWindow (UI/GameWindows.cpp:36-39).
    // nullptr (fallback, common case today: GameHud owns this widget and has no
    // pointer to GameWindows' cache) => local ownIconCache_. See TEX-1 in the
    // front's report: wiring to add at UI/GameHud.cpp:419 (outside this front).
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }

private:
    struct SlotRect {
        int x = 0, y = 0, w = 0, h = 0;
        bool Contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };
    struct Layout {
        SlotRect bar; // bar background (encloses the 14 slots)
        std::array<SlotRect, kBarSlotCount> cells{};
    };

    // A slot as the binary reads it from g_Container5 (3 dwords).
    struct LiveSlot {
        int32_t refId = 0; // dword_16743FC[...]: itemId or skillId
        int32_t count = 0; // dword_1674400[...]: stack counter / charges
        int32_t type  = 0; // dword_1674404[...]: kSlotType* (0 = empty slot)
    };

    // Reads the 3 dwords of slot `i` on page `page` via game::g_Client.Var
    // (original-address escape hatch, cf. Game/ClientRuntime.h).
    // Anchor: 0x684DF6-0x684E12 (address computation `0xA8*page + 0xC*i`).
    static LiveSlot ReadContainer5(int page, int i);

    // Recomputes the geometry (bottom of screen, centered) from current dims.
    static Layout ComputeLayout(int screenW, int screenH);

    // Hit-test against the LAST drawn layout (aligns rendering and click, like
    // MsgBoxDialog::lastScreenW_/lastScreenH_: the click is routed between two
    // frames, so we reuse the geometry actually displayed).
    int HitTest(int mx, int my) const;

    // Applies a decision from game::ConsumableBarLogic: stores the message
    // (log/caller only — no longer drawn, cf. LastMessage()).
    void ApplyDecision(const game::ConsumableDecision& d, int index,
                        const std::array<QuickSlot, kQuickSlotCount>& slots);

    // A slot's texture from the global Sprite2D table, by CATEGORY and 1-based
    // file number — reproduces Sprite2D_BuildPath 0x4D68E0:
    //   cat.1 "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG"  (generic UI atlas, 4500)
    //   cat.2 "G03_GDATA\D01_GIMAGE2D\002\002_%05d.IMG"  (item icons, 4000)
    //   cat.3 "G03_GDATA\D01_GIMAGE2D\003\003_%05d.IMG"  (skill icons, 760)
    // The file number is `slotIndex + 1` (the binary's `a3 + 1` parameter).
    // TODO [g_UseTRVariant 0x1669190, Sprite2D_BuildPath 0x4D6913]: categories
    // 1 and 4 have a "\TR\" variant when g_UseTRVariant==1; this flag is not
    // modeled in C++ (EU build) -> non-TR path only, as in
    // UI/BuffStatusPanel.cpp::GxdCategory1Path.
    gfx::GpuTexture* GetCatTex(const UiContext& ctx, int category, int fileNo);
    // Actual item icon: ITEM_INFO::iconId (ITEM_INFO+192, 1-based), atlas
    // category 2. The binary blits `g_AssetMgr_ItemIconSlots + 148*(iconId-1)`
    // (0x68508D/0x6850C5) -> slot iconId-1 -> file (iconId-1)+1 = iconId.
    gfx::GpuTexture* GetIconTex(const UiContext& ctx, uint32_t itemId);
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    Layout lastLayout_{};
    int    lastScreenW_ = ts2::kRefWidth;
    int    lastScreenH_ = ts2::kRefHeight;

    game::ConsumableDecision lastDecision_{};
    std::string lastMessage_;

    // Icon cache (file path -> GPU texture): see SetIconCache()/
    // ActiveIconCache() above. ownIconCache_ = local fallback (used as long
    // as no caller injects GameWindows' shared cache).
    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;
};

} // namespace ts2::ui

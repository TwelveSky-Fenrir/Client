// UI/EnchantWindow.h — "Enchant" window: selecting one of the local player's
// 13 equipment slots and previewing the stat delta the NEXT enchant level
// of the item equipped in that slot would bring.
//
// Relies on:
//   - Game/GameState.h  : game::g_World.self.equip[13] (EquipSlot{itemId, socket, ..}),
//     socket = bit-packed word (byte3 = current enchant level 1..59, cf.
//     Item_GetAttribByte3, Game/ItemSystem.h).
//   - Game/ItemSystem.h : Item_GetEnchantStatDelta(itemClass, slot, socketWord, key)
//     0x553D50 — large table (class, slot, level, key) -> signed delta.
//
// Icons = real .IMG icon of the equipped item (resolved by itemId, same pattern as
// UI/InventoryWindow.cpp), with fallback to the generic per-slot-index colored square
// (SlotColor) if the texture fails to load or the slot is empty.
//
// -----------------------------------------------------------------------------
// class/slot MAPPING HYPOTHESIS (DOCUMENTED, not certified by the disassembly
// for this specific window — Item_GetEnchantStatDelta is directly callable
// but its 1st parameter `itemClass` is normally resolved by
// Item_ClassifyRecord/Item_ClassifyById (0x5509A0/0x550800), which are INTERNAL and
// NOT EXPORTED (static in Game/StatFormulas.cpp — cf. UI/ItemTooltip.h which
// documents the same limitation). We therefore reproduce HERE, locally, the subset of
// mapping EXPLICITLY described in the Item_GetEnchantStatDelta comment
// (Game/ItemSystem.h lines 155-161):
//   - slot == 1                      -> itemClass 8 ("special case", ONLY this slot)
//   - slot in {0,2,3,4,5}            -> itemClass 4 (armor)
//   - slot == 7                      -> itemClass 1 (weapon)
//   - other slots (6,8,9,10,11,12)   -> not covered by the known enchant table
//     (the original stat-engine loop explicitly skips index 8;
//     the page-2 slots — jewelry/quiver, cf. Docs TS2_GAMEPLAY_LOGIC.md
//     "tab 2 = slots 9..12" — have no confirmed entry in this table).
// This window DISPLAYS this status ("not supported") rather than inventing
// a class for these slots.
// -----------------------------------------------------------------------------
#pragma once
#include "UI/UIManager.h"
#include "Game/ItemSystem.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"

#include <unordered_map>

namespace ts2::ui {

// Number of equipment slots (SelfState::equip).
inline constexpr int kEnchantSlotCount = 13;

// Stat keys exposed by Item_GetEnchantStatDelta (Game/ItemSystem.h §Enchant delta).
// Values in HUNDREDTHS for 10/20/30/40 (converted /100.0 for display), in UNITS
// for 50/60/70/80/90 (faithful to the original comment).
enum class EnchantStatKey : int {
    AtkExt    = 10,
    AtkInt    = 20,
    DefExt    = 30,
    DefInt    = 40,
    MaxHp     = 50,
    MaxMp     = 60,
    Precision = 70,
    Evasion   = 80,
    Rating    = 90,
};
inline constexpr int kEnchantStatKeyCount = 9;
inline constexpr EnchantStatKey kEnchantStatKeys[kEnchantStatKeyCount] = {
    EnchantStatKey::AtkExt, EnchantStatKey::AtkInt, EnchantStatKey::DefExt,
    EnchantStatKey::DefInt, EnchantStatKey::MaxHp,  EnchantStatKey::MaxMp,
    EnchantStatKey::Precision, EnchantStatKey::Evasion, EnchantStatKey::Rating,
};

// -----------------------------------------------------------------------------
// EnchantWindow — lightweight modal dialog (closable), not draggable. Reads
// game::g_World.self.equip[] on every Render (no duplicated item state;
// only the SELECTED slot index is state owned by the window).
class EnchantWindow : public Dialog {
public:
    void Open() override;                       // centers + re-arms latches + resets selection
    // Close() inherited as-is (bOpen_=false).

    bool OnMouseDown(int x, int y) override;     // arms close/slot/enchant if hovered
    bool OnClick(int x, int y) override;         // validates close/slot/enchant if released on it
    bool OnKey(int vk) override;                 // Escape -> closes

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // Index of the currently selected slot (-1 = none). Exposed for tests/tools.
    int SelectedSlot() const { return selectedSlot_; }

    // SHARED GPU icon cache (memory pooling, cf. Gfx/IconTextureCache.h):
    // injected by UI/GameWindows.cpp, same instance as InventoryWindow/WarehouseWindow/
    // VendorShopWindow. nullptr (fallback) => local ownIconCache_ (never the case in
    // production, cf. InventoryWindow::SetIconCache).
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }

private:
    struct Rect { int x = 0, y = 0, w = 0, h = 0; };

    struct Layout {
        Rect box;                          // full panel
        Rect titleBar;                     // title bar
        Rect closeBtn;                     // close button (top-right corner)
        Rect slot[kEnchantSlotCount];       // 13 equipment squares
        Rect panel;                        // info/preview panel (right side)
        Rect enchantBtn;                   // "Enchanter" button (bottom)
    };
    void ComputeLayout(int screenW, int screenH, Layout& L) const;

    // Mapping DOCUMENTED at the top of the file — returns -1 if the slot is not covered
    // by the Item_GetEnchantStatDelta table.
    static int GuessItemClass(int slot);
    static const char* ItemClassLabel(int itemClass);

    // Generic label + color for a slot (independent of its content).
    static const char* SlotLabel(int slot);
    static D3DCOLOR     SlotColor(int slot);

    // Previewed delta for key `key`, at enchant level `previewLevel`
    // (replaces byte3 of the socket word of the item equipped in `slot`). Returns 0
    // if the slot is empty or outside the table (itemClass == -1).
    int PreviewDelta(int slot, int previewLevel, EnchantStatKey key) const;

    // --- Item icon (same pattern as InventoryWindow/WarehouseWindow: resolver +
    // lazy cache + fallback to SlotColor if the texture fails to load). Device taken as a
    // parameter (no device_ member: Dialog has no dedicated Init()).
    gfx::GpuTexture* GetIconTex(IDirect3DDevice9* dev, uint32_t itemId);
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;

    // "click-down -> released on it" latches (CharacterStatsWindow/MsgBoxDialog pattern).
    bool closeArmed_    = false;
    bool enchantArmed_  = false;
    bool slotArmed_[kEnchantSlotCount] = {};

    int selectedSlot_ = -1; // -1 = no selection

    // Screen dims stored at the last Render, to align the hit-test (routed
    // between two frames) with the geometry actually drawn.
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;
};

} // namespace ts2::ui

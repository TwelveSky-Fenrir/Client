// UI/InventoryWindow.cpp — inventory & equipment window implementation.
// See UI/InventoryWindow.h and Docs/TS2_CLIENT_SHELL.md §2.3.
//
// Include order: Net/ FIRST (NetClient.h pulls <winsock2.h> before
// <windows.h>, which UI/InventoryWindow.h pulls directly at the top) — same
// convention as UI/ChatWindow.cpp / UI/WarehouseWindow.cpp.
#include "Net/SendPackets.h"   // -> Net/NetClient.h: winsock2 before windows (order matters) + Net_SendVaultReq_* builders
#include "Net/NetClient.h"     // net::GlobalNetClient() (singleton g_NetClient 0x8156A0)
#include "Net/ClientState.h"   // net::g_MorphInProgress / g_GmCmdCooldownLatch / flt_1675B0C / g_GameTimeSec
#include "UI/InventoryWindow.h"
#include "Asset/ImgFile.h"
#include "Game/GameDatabase.h"
#include "Game/ItemSystem.h"   // game::Item_MeetsEquipRequirement 0x64ECD0 (equip eligibility guard)
#include "Game/ClientRuntime.h" // game::g_Client.inv (InventoryState): source-of-truth model (see UI/InventoryWindow.h)
#include "Core/Log.h"

#include <cstring>
#include <cstdio>

namespace ts2::ui {

namespace {
// .IMG icon path for an item, derived from its itemId — SHARED REFERENCE PATTERN
// (duplicated identically in WarehouseWindow.cpp / EnchantWindow.cpp for this mission:
// IconPathResolver is a NON-capturing function pointer on the InventoryWindow side, so no
// practical common header without touching the existing architecture).
//
// CONFIRMED by disassembly (Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md, decompile of
// cGameHud_Render 0x64A900 + Sprite2D_BuildPath 0x4D68E0): the file index is NOT
// itemId (old hypothesis, FALSE, inferred by probing .IMG sizes without IDA — the scale
// gap between 99999 declared itemIds and the 4000 actual slots in the 002\ pool should
// have ruled it out). The real index is the SEPARATE field ITEM_INFO+192 ("IconID",
// game::ItemInfo::iconId, 1-based), read via the existing game::GetItemInfo() accessor.
// The path format "002\002_%05u.IMG" (folder `002`, template `002_%05d.IMG`) was already
// correct, though.
std::string ResolveItemIconPath(uint32_t itemId) {
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return std::string(buf);
}
} // namespace

// Rect table for the 13 equipment slots, RELATIVE to the base (baseX, baseY).
// Pulled as-is from cGameHud_InitLayout 0x62A5B0 (this[4*slot+2..+5]).
// The order follows slotIds[13] = {2,3,4,5,6,7,99,9,10,11,12,13,14}.
// Slot 6 (id 99, unused costume) is {0,0,0,0} absolute in the original.
namespace {
constexpr int kEquipDelta[13][4] = {
    { 57,  86,  81, 110}, // 0  id 2   (accessory ~24x24)
    {193,  60, 243, 110}, // 1  id 3
    {139,  60, 189, 110}, // 2  id 4
    { 85, 115, 135, 165}, // 3  id 5
    { 57,  60,  81,  84}, // 4  id 6   (~24x24)
    {139, 115, 189, 165}, // 5  id 7
    {  0,   0,   0,   0}, // 6  id 99  (unused)
    { 85,  60, 135, 110}, // 7  id 9
    {193, 115, 243, 165}, // 8  id 10
    { 34,  87,  84, 137}, // 9  id 11  (page 2)
    { 86,  87, 136, 137}, // 10 id 12
    {138,  87, 188, 137}, // 11 id 13
    {190,  87, 240, 137}, // 12 id 14
};
} // namespace

// ============================================================================
// Init / Shutdown / configuration
// ============================================================================
bool InventoryWindow::Init(gfx::Renderer& renderer, gfx::Font* font) {
    device_ = renderer.Device();
    font_   = font;
    if (!device_) return false;
    if (!sprite_.Create(device_)) return false;
    // Default wiring of the item icon resolver (see ResolveItemIconPath above) —
    // does NOT override a resolver already set by the caller via SetIconResolver() before Init().
    if (!iconResolver_) SetIconResolver(&ResolveItemIconPath);
    RecomputeLayout();

    // 1x1 white texture for FillRect() (generic flat-fill rectangle utility, kept
    // shared even though no active caller in this window since the drag visual
    // feedback fix — see the Render() comment) — same technique as
    // UI/UIManager.cpp::CreateWhiteTexture. Non-fatal on failure: FillRect()
    // tolerates whiteTex_==nullptr (no visual feedback, no crash).
    if (SUCCEEDED(device_->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
                                         D3DPOOL_MANAGED, &whiteTex_, nullptr))) {
        D3DLOCKED_RECT lr{};
        if (SUCCEEDED(whiteTex_->LockRect(0, &lr, nullptr, 0))) {
            *reinterpret_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu;
            whiteTex_->UnlockRect(0);
        }
    } else {
        TS2_WARN("InventoryWindow : CreateTexture(1x1 blanche) a echoue.");
        whiteTex_ = nullptr;
    }
    return true;
}

void InventoryWindow::Shutdown() {
    if (drag_.active) CancelDrag();
    ownIconCache_.Clear(); // sharedIconCache_ (if injected) is not owned by this window — not released here
    background_.Release();
    if (whiteTex_) { whiteTex_->Release(); whiteTex_ = nullptr; }
    sprite_.Destroy();
    device_ = nullptr;
    font_   = nullptr;
}

// UI/GameHud.cpp::OnDeviceLost/OnDeviceReset pattern: sprite_ (ID3DXSprite owned
// by this window) must be released before Reset() and rebuilt after. The
// textures (background_/iconCache_/whiteTex_) are D3DPOOL_MANAGED: the D3D9
// runtime restores them on its own, no further handling needed here.
void InventoryWindow::OnDeviceLost()  { sprite_.OnLostDevice(); }
void InventoryWindow::OnDeviceReset() { sprite_.OnResetDevice(); }

// Flat-fill rectangle — see UI/InventoryWindow.h::FillRect (same technique as
// UI/UIManager.cpp::FillRect: blit of a scaled 1x1 white texture, modulated by
// `color`, compensatePos=true so (x,y) stays the final position).
void InventoryWindow::FillRect(int x, int y, int w, int h, D3DCOLOR color) {
    if (!whiteTex_ || w <= 0 || h <= 0) return;
    static const RECT kUnitSrc = {0, 0, 1, 1};
    sprite_.DrawSpriteScaled(whiteTex_, &kUnitSrc, x, y,
                             static_cast<float>(w), static_cast<float>(h),
                             color, /*compensatePos=*/true);
}

bool InventoryWindow::SetBackgroundImage(const std::string& imgPath) {
    if (!device_) return false;
    asset::ImgFile img;
    if (!img.Load(imgPath)) return false;
    if (!background_.CreateFromImgFile(device_, img)) return false;
    bgHalfW_ = static_cast<int>(background_.Width())  / 2;
    bgHalfH_ = static_cast<int>(background_.Height()) / 2;
    RecomputeLayout();
    return true;
}

void InventoryWindow::SetScreenSize(int width, int height) {
    screenW_ = (width  > 0) ? width  : ts2::kRefWidth;
    screenH_ = (height > 0) ? height : ts2::kRefHeight;
    RecomputeLayout();
}

// UI_ProjectSpriteToScreen 0x50F5D0: anchors the reference coords (kRefX/kRefY)
// scaled to screen/1024x768. The original centers on HUD sprite #299; here we
// center on the panel background if present (bgHalfW/H), else anchor the top-left corner.
void InventoryWindow::RecomputeLayout() {
    baseX_ = (screenW_ * kRefX) / ts2::kRefWidth  - bgHalfW_;
    baseY_ = (screenH_ * kRefY) / ts2::kRefHeight - bgHalfH_;
}

// ============================================================================
// Lifecycle
// ============================================================================
void InventoryWindow::Open() {
    visible_     = true;                     // this[175] = 1
    activeTab_   = 1;                        // this[226] = 1 (inventory/equipment)
    equipSubTab_ = EquipSubTab::EquipPage1;  // this[227] = 1
    bagPage_     = 0;                        // this[228] = 0
    RecomputeLayout();
}

void InventoryWindow::Close() {
    if (drag_.active) CancelDrag();
    visible_ = false;                        // this[175] = 0
}

void InventoryWindow::Toggle() { visible_ ? Close() : Open(); }

// ============================================================================
// Geometry / hit-tests
// ============================================================================
InventoryWindow::SlotRect InventoryWindow::EquipSlotRect(int slot) const {
    if (slot < 0 || slot >= 13) return {0, 0, 0, 0};
    const int* d = kEquipDelta[slot];
    if (d[0] == 0 && d[1] == 0 && d[2] == 0 && d[3] == 0)
        return {0, 0, 0, 0}; // slot 99 unused (absolute-zero rect)
    return { baseX_ + d[0], baseY_ + d[1], baseX_ + d[2], baseY_ + d[3] };
}

// Equipment slot under the cursor, in the current sub-tab (filled OR empty).
int InventoryWindow::EquipSlotRectAt(int mx, int my) const {
    int lo, hi;
    switch (equipSubTab_) {
        case EquipSubTab::EquipPage1: lo = 0; hi = 9;  break;
        case EquipSubTab::EquipPage2: lo = 9; hi = 13; break;
        default:                      return -1; // quiver: handled elsewhere
    }
    for (int i = lo; i < hi; ++i) {
        const SlotRect r = EquipSlotRect(i);
        if (r.r > r.l && mx >= r.l && mx <= r.r && my >= r.t && my <= r.b)
            return i;
    }
    return -1;
}

// cGameHud_EquipSlotAtFilled 0x64EFC0: occupied slot (g_EquipMain[4*i] > 0) under the cursor.
int InventoryWindow::EquipSlotAt(int mx, int my) const {
    if (!visible_ || activeTab_ != 1) return -1;
    const int s = EquipSlotRectAt(mx, my);
    if (s < 0) return -1;
    return (game::g_World.self.equip[static_cast<size_t>(s)].itemId > 0) ? s : -1;
}

// "8x8 grid" portion of cGameHud_InvCellAt 0x64F9F0: finds the cell (col,row)
// under the cursor. rect X = base+26*i+34..+59; rect Y = base+26*j+193..+218.
bool InventoryWindow::GridCellAt(int mx, int my, int& col, int& row) const {
    for (int i = 0; i < kGridCols; ++i) {
        const int l = baseX_ + kCellStep * i + kCellOffX;
        const int r = l + kCellSize;
        if (mx < l || mx > r) continue;
        for (int j = 0; j < kGridRows; ++j) {
            const int t = baseY_ + kCellStep * j + kCellOffY;
            const int b = t + kCellSize;
            if (my < t || my > b) continue;
            col = i; row = j;
            return true;
        }
    }
    return false;
}

// cGameHud_InvCellAt 0x64F9F0: cell -> the item covering it (1x1/2x2 size test).
// Returns the SLOT (0..63, StorageCol) in game::g_Client.inv at the current
// bagPage_, or -1. The page IS the index (row) in g_Client.inv — no need to
// filter a separate "page" field (see Game/GameState.h::InvCell, removed): only
// the displayed page is scanned, so no collision with the other page is possible.
int InventoryWindow::InvCellAt(int mx, int my) const {
    if (!visible_ || activeTab_ != 1) return -1;
    int col, row;
    if (!GridCellAt(mx, my, col, row)) return -1;
    const uint32_t page = static_cast<uint32_t>(bagPage_);
    for (uint32_t c = 0; c < game::InventoryState::kCols; ++c) {
        const game::InvCell& cell = game::g_Client.inv.At(page, c);
        if (cell.itemId == 0) continue;
        const int sz = ItemGridSize(cell.itemId);
        if (col >= static_cast<int>(cell.gridX) && col < static_cast<int>(cell.gridX) + sz &&
            row >= static_cast<int>(cell.gridY) && row < static_cast<int>(cell.gridY) + sz)
            return static_cast<int>(c);
    }
    return -1;
}

// Item grid size: ITEM_INFO +188 (type). 2/7/11 => 1x1, else 2x2
// (see MobDb_GetEntry(&mITEM,...) in 0x64F9F0). Defaults to 1x1 if DB not loaded.
int InventoryWindow::ItemGridSize(uint32_t itemId) {
    const game::DataTable& db = game::g_World.db.item;
    const uint8_t* rec = db.record(itemId);
    if (rec && db.stride >= 192) {
        const int type = *reinterpret_cast<const int*>(rec + 188);
        return (type == 2 || type == 7 || type == 11) ? 1 : 2;
    }
    return 1;
}

bool InventoryWindow::PointInPanel(int mx, int my) const {
    const int w = background_.Valid() ? static_cast<int>(background_.Width())  : 300;
    const int h = background_.Valid() ? static_cast<int>(background_.Height()) : 420;
    return mx >= baseX_ && mx <= baseX_ + w && my >= baseY_ && my <= baseY_ + h;
}

// ============================================================================
// Icons
// ============================================================================
gfx::GpuTexture* InventoryWindow::GetIconTex(uint32_t itemId) {
    // Cache SHARED by file path (see SetIconCache/ActiveIconCache): an icon
    // already loaded by WarehouseWindow/EnchantWindow/VendorShopWindow (same .IMG
    // file, same ITEM_INFO::iconId) is reused without re-decoding/re-uploading to VRAM.
    if (!iconResolver_ || !device_) return nullptr;
    const std::string path = iconResolver_(itemId);
    return ActiveIconCache().GetOrLoad(device_, path);
}

// Draws the icon in the cell; fallback = item name as text (deferred font pass).
void InventoryWindow::DrawItemIcon(uint32_t itemId, int x, int y, int wPx, int hPx, int count) {
    gfx::GpuTexture* g = GetIconTex(itemId);
    if (g && g->Handle() && g->Width() > 0 && g->Height() > 0) {
        const float sx = static_cast<float>(wPx) / static_cast<float>(g->Width());
        const float sy = static_cast<float>(hPx) / static_cast<float>(g->Height());
        // compensatePos=true => (x,y) stays the exact position despite the scale matrix.
        sprite_.DrawSpriteScaled(g->Handle(), nullptr, x, y, sx, sy, gfx::kSpriteWhite, true);
    } else {
        const game::ItemInfo* info = game::GetItemInfo(itemId);
        if (info && info->name[0] != '\0') {
            pendingText_.push_back({ x + 2, y + 2,
                                     std::string(info->name, strnlen(info->name, sizeof(info->name))),
                                     kLabelColor });
        }
    }
    if (count > 1)
        pendingText_.push_back({ x + wPx - 14, y + hPx - 14, std::to_string(count), kCountColor });
}

// ============================================================================
// Rendering (inventory/equipment subset of cGameHud_Render 0x64A900)
// ============================================================================
void InventoryWindow::Render() {
    if (!visible_ || activeTab_ != 1) return;
    if (!sprite_.Ready()) return;

    pendingText_.clear();
    sprite_.Begin(D3DXSPRITE_ALPHABLEND);

    // Panel background.
    if (background_.Valid())
        sprite_.DrawSprite(background_.Handle(), nullptr, baseX_, baseY_, gfx::kSpriteWhite);

    // Drag visual feedback: NO grayed-out cell. CORRECTED by disassembly
    // ("drag&drop visual feedback" mission, 2026-07-14, decompile of
    // Item_BeginDragTransaction 0x5AFDF0 + Inv_RemoveItemQuantity 0x5B0340 +
    // UI_StorageWin_Draw 0x5D6610 + maybe_UI_QuickSlotBar_Render 0x5BE340):
    // in the binary, the source cell is simply CLEARED (itemId set to 0) at
    // pickup time, so its icon is no longer drawn AT ALL — no gray tint on top.
    // BeginPickup() below already clears `equip[es]`/`src` the same way, so the
    // Equipment/Bag loops further down draw NOTHING for that cell automatically:
    // no further handling is needed here. The old FillRect(dragSourceRect_,
    // kDragSourceCol) — an approximation not confirmed by the binary — was
    // removed (along with the dragSourceRect_ field/kDragSourceCol color, which
    // became unused).

    // Equipment: occupied slots of the current sub-tab.
    {
        int lo = 0, hi = 0;
        if (equipSubTab_ == EquipSubTab::EquipPage1) { lo = 0; hi = 9;  }
        else if (equipSubTab_ == EquipSubTab::EquipPage2) { lo = 9; hi = 13; }
        const auto& eq = game::g_World.self.equip;
        for (int i = lo; i < hi; ++i) {
            if (eq[static_cast<size_t>(i)].itemId == 0) continue;
            const SlotRect r = EquipSlotRect(i);
            const int w = r.r - r.l, h = r.b - r.t;
            if (w <= 0 || h <= 0) continue;
            DrawItemIcon(eq[static_cast<size_t>(i)].itemId, r.l, r.t, w, h, 1);
        }
    }

    // Bag: items on the current page (bagPage_), read from game::g_Client.inv —
    // the page IS the index (row) in the grid, so no further filtering is
    // needed (only the 64 slots of THIS page are iterated).
    {
        const uint32_t page = static_cast<uint32_t>(bagPage_);
        for (uint32_t k = 0; k < game::InventoryState::kCols; ++k) {
            const game::InvCell& c = game::g_Client.inv.At(page, k);
            if (c.itemId == 0) continue;
            const int sz = ItemGridSize(c.itemId);
            const int x  = baseX_ + kCellStep * static_cast<int>(c.gridX) + kCellOffX;
            const int y  = baseY_ + kCellStep * static_cast<int>(c.gridY) + kCellOffY;
            const int px = kCellStep * sz - 1;
            DrawItemIcon(c.itemId, x, y, px, px, static_cast<int>(c.flag ? c.flag : 1));
        }
    }

    // Item being dragged, stuck under the cursor.
    if (drag_.active && drag_.itemId) {
        const int sz = ItemGridSize(drag_.itemId);
        const int px = kCellStep * sz - 1;
        DrawItemIcon(drag_.itemId, cursorX_ - drag_.grabOffsetX, cursorY_ - drag_.grabOffsetY,
                     px, px, drag_.count);
    }

    sprite_.End();

    // Text pass (stack counters + id fallback) — OUTSIDE the sprite batch (the font has its own).
    if (font_ && font_->Ready() && !pendingText_.empty()) {
        font_->BeginBatch();
        for (const TextItem& t : pendingText_)
            font_->DrawTextStyled(t.text.c_str(), t.x, t.y, t.color, gfx::kStyleShadow);
        font_->EndBatch();
    }
}

// ============================================================================
// Drag&drop — "click to pick up / click again to drop"
// ============================================================================
uint32_t InventoryWindow::DragColor() const {
    return (drag_.srcType == DragSource::Bag) ? dragBagCell_.color : dragEquipCell_.socket;
}
uint32_t InventoryWindow::DragDurability() const {
    return (drag_.srcType == DragSource::Bag) ? dragBagCell_.durability : dragEquipCell_.extra0;
}

// Item_BeginDragTransaction 0x5AFDF0 + Inv_RemoveItemQuantity 0x5B0340:
// capture the item onto the cursor AND remove it from its source.
// Faithful order: equipment tested before the bag (see cGameHud_OnMouseDown).
bool InventoryWindow::BeginPickup(int mx, int my) {
    const int es = EquipSlotAt(mx, my);
    if (es >= 0) {
        game::EquipSlot& e = game::g_World.self.equip[static_cast<size_t>(es)];
        dragEquipCell_     = e;
        drag_.active       = true;
        drag_.pendingAck   = false;       // *(g_DragCtx+0x0C) = 0 on PICKUP (0x5AFDF0)
        drag_.srcType      = DragSource::Equip;
        drag_.srcPage      = 0;
        drag_.srcSlot      = es;
        drag_.itemId       = e.itemId;
        // +0x20 (a6): EQUIP source -> durability (0x62B199). THIS is the field
        // VaultReq_213 reads (arg3, see header banner). game::EquipSlot::extra0 == durability.
        drag_.aux20        = static_cast<int>(e.extra0);
        drag_.count        = 1;
        drag_.grabOffsetX  = kCellSize / 2;
        drag_.grabOffsetY  = kCellSize / 2;
        e = game::EquipSlot{};            // removed from source (cell rendered empty, see Render())
        return true;
    }
    const int ic = InvCellAt(mx, my);
    if (ic >= 0) {
        game::InvCell& src = game::g_Client.inv.At(static_cast<uint32_t>(bagPage_),
                                                    static_cast<uint32_t>(ic));
        dragBagCell_     = src;
        drag_.active     = true;
        drag_.pendingAck = false;         // *(g_DragCtx+0x0C) = 0 on PICKUP (0x5AFDF0)
        drag_.srcType    = DragSource::Bag;
        drag_.srcPage    = bagPage_;
        drag_.srcSlot    = ic;
        drag_.itemId     = dragBagCell_.itemId;
        // +0x20 (a6): BAG source -> gridX (0x62B5FB). Not emitted by the bag builders
        // (208 reads +0x28 count), kept for fidelity to Item_BeginDragTransaction's layout.
        drag_.aux20      = static_cast<int>(dragBagCell_.gridX);
        drag_.count      = static_cast<int>(dragBagCell_.flag ? dragBagCell_.flag : 1);
        drag_.grabOffsetX = kCellSize / 2;
        drag_.grabOffsetY = kCellSize / 2;
        src = game::InvCell{};            // removed from source (cell rendered empty, see Render())
        return true;
    }
    return false;
}

// ============================================================================
// Drop = EMISSION (mirrors UI_MainInventory_OnLButtonUp 0x5B20B0)
// ----------------------------------------------------------------------------
// The binary WRITES NO destination cell and NEVER SWAPS: it EMITS the VaultReq
// matching the (srcType, target) pair, sets g_DragCtx+0x0C=1 (ack pending) then
// waits for the server reply (incoming handlers) — the item stays on the cursor
// until then. On REFUSAL (guard), it RESTORES the source and closes the drag
// (Inv_AddItemQuantity 0x5B0D70 + Item_DragState_Clear 0x5B02D0 == CancelDrag here).
// The old "swap" + local destination write was an INVENTION (EnchantWindow/Pass 3
// style defect): removed.
// ============================================================================
bool InventoryWindow::PlaceDrag(int mx, int my) {
    if (!drag_.active) return false;

    // --- Target = equipment slot? (cGameHud_EquipSlotAtEmpty 0x64F140: EMPTY slot) ---
    // EquipSlotRectAt = slot under the cursor (filled OR empty); equip only succeeds
    // (0x64F140) if the slot is EMPTY — on an occupied slot it returns -1: no swap.
    const int es = EquipSlotRectAt(mx, my);
    if (es >= 0) {
        const bool empty = (game::g_World.self.equip[static_cast<size_t>(es)].itemId == 0);
        if (drag_.srcType == DragSource::Bag && empty)
            return EmitEquipFromBag(es);                 // VaultReq_210 (bag->equip) @0x5B2555
        // Occupied slot, OR non-bag source (equip->equip = re-slot): the binary emits
        // nothing here and would fall through to the next targets. We consume the
        // click WITHOUT swapping (faithful); the item stays on the cursor.
        // TODO [anchor 0x5B20B0] equip->equip (re-slot): path not isolated within scope.
        return true;
    }

    // --- Target = bag cell? ---
    int col, row;
    if (GridCellAt(mx, my, col, row)) {
        const int occ = InvCellAt(mx, my);               // occupied slot under the cursor, or -1
        if (drag_.srcType == DragSource::Bag)
            return EmitMoveBagToBag(col, row, occ);      // VaultReq_208 (bag->bag) @0x5B22FC
        if (drag_.srcType == DragSource::Equip)
            return EmitUnequipToBag(col, row, occ);      // VaultReq_213 (equip->bag) @0x5BA28C
        // srcType Quiver: a separate widget, not handled by InventoryWindow.
        return true;
    }

    return false; // target outside the panel: item stays on the cursor (faithful)
}

// Universal guard (bag->bag 0x5B2297-0x5B22A7, equip 0x5B23E7-0x5B23F7, unequip
// same pair): morph in progress OR request already in flight -> SILENT refusal.
bool InventoryWindow::EmissionBlockedByMorphOrLatch() const {
    return net::g_MorphInProgress == 1 || net::g_GmCmdCooldownLatch != 0;  // 0x1675A88 / 0x1675B08
}

// Epilogue common to ANY emission (0x5B2301-0x5B232D / 0x5BA291-0x5BA2BD): sets
// the pending ack, arms the anti-spam lock, timestamps, marks inventory dirty.
// The drag is NOT closed here (Item_DragState_Clear is called ONLY on refusal) ->
// the item stays stuck to the cursor until the server reply (see header banner:
// ASSUMED structural gap, drag_ being a MEMBER rather than the global g_DragCtx
// that incoming handlers would clear — do NOT "fix" with an optimistic local reset).
void InventoryWindow::MarkEmissionPending() {
    drag_.pendingAck = true;                       // *(g_DragCtx+0x0C) = 1  (0x5B2307 / 0x5BA297)
    net::g_GmCmdCooldownLatch = 1;                 // 0x5B230E / 0x5BA29E
    // Timestamp: SAME proven limitation as CharacterStatsWindow (net::g_GameTimeSec is a
    // STUB never fed; the binary only has ONE flt_815180). No functional effect here
    // (unlock comes from the latch, cleared by the incoming handler). Reported.
    net::flt_1675B0C = net::g_GameTimeSec;         // 0x5B2318 / 0x5BA2A8
    // if (g_InvDirtyEnable == 1) g_InvDirtyFlag = 1;  (0x5B2324 / 0x5BA2B4). Via the Var-space
    // (dual representation: g_InvDirtyEnable also has AutoPlayExternalState::invDirtyEnable —
    // see report). Permissive default (0) = no dirty, no observable HUD effect.
    if (game::g_Client.VarGet(0x16755AC) == 1)     // g_InvDirtyEnable 0x16755AC
        game::g_Client.Var(0x815140) = 1;          // g_InvDirtyFlag 0x815140
}

// Bag -> bag: VaultReq_208 @0x5B22FC. Destination via cGameHud_PlaceItemIntoBag 0x650470
// (free cell = move; occupied cell = STACK MERGE only if type +188 == 2 &&
// same itemId && sum <= 99, else failure with no emission). Layout 208 (7 fields, all 4 bytes LE):
//   (srcPage +0x14, srcSlot +0x18, count +0x28, dstPage, dstSlot, dstGridX, dstGridY).
bool InventoryWindow::EmitMoveBagToBag(int col, int row, int occ) {
    const uint32_t page = static_cast<uint32_t>(bagPage_);
    int dstSlot = 0, dstGridX = 0, dstGridY = 0;

    if (occ < 0) {
        // No stack under the cursor: the binary (cursor branch of 0x650470, when
        // cGameHud_InvCellAt 0x64F9F0 returns *a6==-1) delegates to
        // cGameHud_FindInvPlacement 0x64FCA0 (placement accounting for the 1x1/2x2
        // footprint). This exact geometry is not reproduced here: the cell is
        // anchored at the cursor's (col,row) instead.
        // TODO [anchor 0x64FCA0] footprint-aware placement (impact: overlapping 2x2 items).
        dstSlot  = static_cast<int>(StorageCol(static_cast<uint32_t>(col), static_cast<uint32_t>(row)));
        dstGridX = col;
        dstGridY = row;
    } else {
        // Occupied cell: merge condition VERIFIED word-for-word in 0x650470
        // (branch `else if (*(this+175))`, test @ `*(v24+188)!=2 || g_InvMain[dst]!=a4 ||
        //  a5+g_InvGrid_Count[dst] > 99`): type(dragged)+188 == 2 && same itemId && sum <= 99.
        const game::InvCell& tgt = game::g_Client.inv.At(page, static_cast<uint32_t>(occ));
        const game::DataTable& db = game::g_World.db.item;
        const uint8_t* rec = db.record(drag_.itemId);
        const int type = (rec && db.stride >= 192) ? *reinterpret_cast<const int*>(rec + 188) : -1;
        const bool mergeable = (type == 2) && (tgt.itemId == drag_.itemId) &&
                               (drag_.count + static_cast<int>(tgt.flag ? tgt.flag : 1) <= 99);
        if (!mergeable) {
            // Placement failure (v254 == -1 @0x5B21FA): the binary EMITS NOTHING and
            // falls through to the next targets (down to "drop" with an MsgBox
            // confirmation). "Drop" is out of scope (builder 209 vs 212 not
            // discriminated) AND DESTRUCTIVE: not reproduced here, the drag is left
            // active WITHOUT any swap.
            // TODO [anchor 0x5B2EBD] drop (VaultReq_212) / [0x5B9EAE] (VaultReq_209).
            return true;
        }
        dstSlot  = occ;                              // the existing stack
        dstGridX = static_cast<int>(tgt.gridX);
        dstGridY = static_cast<int>(tgt.gridY);
    }

    // Post-placement guards, binary ORDER:
    // (1) dword_1822998 != 0 (move grid already active) -> msg 598 + refusal (0x5B2204).
    if (game::g_Client.VarGet(0x1822998) != 0) {
        game::g_Client.msg.System(game::Str(598));   // StrTable005_Get(0x256) @0x5B2213
        CancelDrag();
        return true;
    }
    // (2) g_WarehouseWindowOpen != 0 -> msg 598 + refusal (0x5B224D).
    if (game::g_Client.VarGet(0x1822ED4) != 0) {
        game::g_Client.msg.System(game::Str(598));
        CancelDrag();
        return true;
    }
    // (3) morph || latch -> silent refusal (0x5B2297).
    if (EmissionBlockedByMorphOrLatch()) { CancelDrag(); return true; }

    // g_NetClient 0x8156A0 is a GLOBAL: the builders address it directly (no socket
    // parameter). net::GlobalNetClient() is set by ConnectGameServer; this window
    // only opens in-game (post-handshake) -> nc is non-null whenever this path is
    // reached: the `if (!nc)` below is a DEFENSIVE safety net, NOT dead code
    // (unlike the old always-null Bind()/net_ pair).
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) { CancelDrag(); return true; }          // out of session: restore, no emission
    net::Net_SendVaultReq_208(*nc, drag_.srcPage, drag_.srcSlot, drag_.count,
                              static_cast<int>(page), dstSlot, dstGridX, dstGridY); // 0x5B22FC
    MarkEmissionPending();
    return true;
}

// Bag -> equipment (EQUIP): VaultReq_210 @0x5B2555. Layout:
//   (srcPage +0x14, srcSlot +0x18, count +0x28, 0, equipSlot, 0, 0).
// equipSlot = return value of cGameHud_EquipSlotAtEmpty 0x64F140, assumed identical
// to the g_World.self.equip array index (mirror of g_EquipMain) — see report (not re-mapped).
bool InventoryWindow::EmitEquipFromBag(int equipSlot) {
    // Equip guards (binary order), reproduced for the MODELED state only:
    //
    // TODO [anchor 0x5B2362: dword_1675B00 / g_SelfActionState 0x1687328] guard
    // "(dword_1675B00 != 0 || g_SelfActionState != 1) -> msg 120" NOT reproduced:
    // g_SelfActionState is unreliable here (Var-space unfed -> would read 0 ->
    // would refuse ALL equips; see known TODO PlayerInputController.cpp:227). Left permissive.
    //
    // (b) Item_MeetsEquipRequirement 0x64ECD0: item not eligible -> silent refusal (0x5B23C7).
    const game::ItemInfo* info = game::GetItemInfo(drag_.itemId);
    if (info && !game::Item_MeetsEquipRequirement(*info, equipSlot)) {
        CancelDrag();
        return true;
    }
    //
    // TODO [anchor 0x5B242C: g_SpecialFormActive 0x16760D4 / Npc_IsSpecialType 0x54EE60]
    // special-form guard (g_SpecialFormActive>0 && Npc_IsSpecialType(g_SelfMorphNpcId)==1
    // -> refusal) NOT reproduced: Npc_IsSpecialType not modeled (permissive default: not morphed).
    //
    // TODO [anchor 0x5B246A: Item_ClassifyById 0x550800 / Item_GetAttribByte0 0x545610]
    // gem guard (classify ∈ {1,4,8,9} && attribByte0 >= 100 -> msg 2562) NOT reproduced:
    // classifiers not modeled here. Left permissive.
    //
    // (e) morph || latch -> silent refusal (0x5B23E7).
    if (EmissionBlockedByMorphOrLatch()) { CancelDrag(); return true; }

    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) { CancelDrag(); return true; }
    net::Net_SendVaultReq_210(*nc, drag_.srcPage, drag_.srcSlot, drag_.count,
                              0, equipSlot, 0, 0);      // 0x5B2555
    MarkEmissionPending();
    return true;
}

// Equipment -> bag (UNEQUIP): VaultReq_213 @0x5BA28C. Layout:
//   (0, srcSlot +0x18, aux20 +0x20, dstPage, dstSlot, dstGridX, dstGridY).
// The 3rd field is +0x20 (durability, see BeginPickup) and NOT +0x28: the EQUIP
// source lays out its arguments by TYPE (see header banner). Destination via
// cGameHud_FindInvPlacement 0x64FCA0 (not disassembled here): derived from the
// cursor's cell if FREE; occupied cell = TODO (cross-container merge not proven).
bool InventoryWindow::EmitUnequipToBag(int col, int row, int occ) {
    if (occ >= 0) {
        // Occupied cell: FindInvPlacement handles merge/free-slot; not reproduced ->
        // no emission, drag left active. TODO [anchor 0x64FCA0] exact dst derivation.
        return true;
    }
    // morph || latch -> silent refusal.
    if (EmissionBlockedByMorphOrLatch()) { CancelDrag(); return true; }

    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) { CancelDrag(); return true; }
    const int dstPage = bagPage_;
    const int dstSlot = static_cast<int>(StorageCol(static_cast<uint32_t>(col), static_cast<uint32_t>(row)));
    net::Net_SendVaultReq_213(*nc, 0, drag_.srcSlot, drag_.aux20,
                              dstPage, dstSlot, col, row);  // 0x5BA28C
    MarkEmissionPending();
    return true;
}

// Returns the item to its source (close, failure) — no item loss. Restores at the
// EXACT slot (srcPage, srcSlot) it was removed from by BeginPickup, rather than
// appending it at the end of the collection like the old vector model did.
void InventoryWindow::CancelDrag() {
    if (!drag_.active) return;
    if (drag_.srcType == DragSource::Equip && drag_.srcSlot >= 0 && drag_.srcSlot < 13)
        game::g_World.self.equip[static_cast<size_t>(drag_.srcSlot)] = dragEquipCell_;
    else if (drag_.srcType == DragSource::Bag && drag_.srcSlot >= 0 &&
             drag_.srcSlot < static_cast<int>(game::InventoryState::kCols))
        game::g_Client.inv.At(static_cast<uint32_t>(drag_.srcPage),
                              static_cast<uint32_t>(drag_.srcSlot)) = dragBagCell_;
    drag_.reset();
}

// ============================================================================
// Mouse events
// ============================================================================
bool InventoryWindow::OnMouseDown(int mouseX, int mouseY) {
    cursorX_ = mouseX; cursorY_ = mouseY;
    if (!visible_ || activeTab_ != 1) return false;

    if (drag_.active) {              // click again -> drop
        PlaceDrag(mouseX, mouseY);
        return true;
    }
    if (BeginPickup(mouseX, mouseY)) // click -> pick up
        return true;

    // Click in the panel with no item: consumed ("first consumer wins").
    return PointInPanel(mouseX, mouseY);
}

bool InventoryWindow::OnMouseUp(int mouseX, int mouseY) {
    cursorX_ = mouseX; cursorY_ = mouseY;
    if (!visible_ || activeTab_ != 1) return false;
    // "Click to pick up / click again to drop" model: dropping happens on click
    // (OnMouseDown), not on release. Only consumed if over the panel.
    return PointInPanel(mouseX, mouseY);
}

} // namespace ts2::ui

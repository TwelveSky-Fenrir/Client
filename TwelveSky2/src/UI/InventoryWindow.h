// UI/InventoryWindow.h — "character" window: inventory (8x8 grid x 2 pages
// max = 128 cells) + equipment (13 slots).
//
// Inventory/equipment subset of the cGameHud class (singleton dword_1839568,
// tab this[226]==1) — VERIFIED by decompile (2026-07-14): equipment
// (cGameHud_EquipSlotAtFilled 0x64EFC0) and bag (cGameHud_InvCellAt
// 0x64F9F0) are dispatched in the SAME `case 1` branch of cGameHud_OnMouseDown
// 0x62B080 and drawn by the same subset of cGameHud_Render — ONE SINGLE
// window in the binary, never two separate windows. This class respects
// that organization (one InventoryWindow handles both). Clean C++ rewrite
// on top of the Gfx/Game/Asset building blocks. Disassembly ref (idaTs2 server,
// see Docs/TS2_CLIENT_SHELL.md §2.3):
//   - cGameHud_InitLayout        0x62A5B0  rect table for the 13 slots (relative to base)
//   - cGameHud_ResetUiState      0x62AFB0  open (this[175]=1, tab 1)
//   - cGameHud_Hide              0x62B050  close (this[175]=0)
//   - cGameHud_OnMouseDown       0x62B080  item pickup / tabs
//   - cGameHud_OnMouseUp         0x62DFA0  drop / button actions
//   - cGameHud_Render            0x64A900  rendering
//   - cGameHud_EquipSlotAtFilled 0x64EFC0  hit-test on occupied equip slots
//   - cGameHud_InvCellAt         0x64F9F0  hit-test 8x8 grid -> item cell
//   - Item_BeginDragTransaction  0x5AFDF0  starts pickup (g_DragCtx 0x1822380)
//   - UI_ProjectSpriteToScreen   0x50F5D0  anchors reference coords -> screen
//
// Drag&drop model = "click to pick up / click again to drop" (NOT press-drag-release,
// confirmed by Item_BeginDragTransaction 0x5AFDF0 — KEPT as-is here, do not rewrite
// as press-hold-release which would break binary fidelity).
//
// ===========================================================================
// NETWORK — W6 FIX (this file's old banner was PROVEN WRONG)
// ===========================================================================
// The old text claimed "NO outgoing builder ... the move stays 100% LOCAL ...
// no confirmed opcode". BOTH halves are refuted by IDA:
//
//  1. The DROP handler is NOT cGameHud_OnMouseUp 0x62DFA0 (wrongly anchored here):
//     its call sites only contain skill/stat code, ZERO item emission. The real
//     handler is UI_MainInventory_OnLButtonUp 0x5B20B0 (0xBDDB bytes), a giant switch
//     `switch(g_DragCtx+0x10 /*srcType*/)` (this = g_DragCtx 0x1822380).
//  2. The builders ALL ALREADY EXIST in Net/SendPackets.h (Net_SendVaultReq_*,
//     56 sub-codes of network opcode 0x13/Op19). Verified 1:1 via xrefs_to on
//     each sub-code: every EA below was re-read in the IDB.
//
// Emissions from THIS window (bag + equipment; quiver/quickbar are other
// widgets) — universal 7-field layout, all promoted to 4 bytes LE:
//   (srcPage, srcSlot, amount, dstPage, dstSlot, dstGridX, dstGridY)
//
//   Bag -> bag (move/merge)          VaultReq_208  @0x5B22FC
//        args (+0x14, +0x18, +0x28, dstPage, dstSlot, dstGridX, dstGridY)
//   Bag -> equipment (EQUIP)         VaultReq_210  @0x5B2555
//        args (+0x14, +0x18, +0x28, 0, equipSlot, 0, 0)
//   Equipment -> bag (UNEQUIP)       VaultReq_213  @0x5BA28C
//        args (0, +0x18, +0x20, dstPage, dstSlot, dstGridX, dstGridY)
//
// WARNING (+0x20 vs +0x28): the 3rd field is NOT the same depending on the source,
// because Item_BeginDragTransaction 0x5AFDF0 lays out its arguments by TYPE (see
// DragContext):
//   - BAG pickup   (0x62B5FB): a6=gridX -> +0x20; a8=count      -> +0x28 (208 reads +0x28)
//   - EQUIP pickup (0x62B199): a6=durability -> +0x20; a8=serial -> +0x28 (213 reads +0x20)
//
// OPTIMISTIC MODEL — NO: the binary writes NOTHING locally on drop.
// cGameHud_PlaceItemIntoBag 0x650470 (misleading name) is a PURE REQUEST: it only
// reads g_InvMain/g_InvGrid_* and writes ONLY its out-params. Drop is therefore
// limited to (a) computing the destination, (b) emitting, (c) setting
// g_DragCtx+0x0C = 1 (ack pending) WITHOUT calling Item_DragState_Clear -> the item
// STAYS on the cursor until the server reply. The cell is only cleared on PICKUP
// (Inv_RemoveItemQuantity). -> PlaceDrag() below therefore writes NO destination cell.
//
// SWAP: the binary NEVER swaps. On an occupied cell, 0x650470 only succeeds for a
// STACK MERGE (type==2 && same itemId && sum <= 99); otherwise *a6 = -1 -> refusal +
// restore. On an occupied equipment slot, cGameHud_EquipSlotAtEmpty 0x64F140 returns
// -1 (`if (g_EquipMain[4*i] >= 1) return -1;`) -> no target. PlaceDrag's old "swap"
// was an INVENTION: removed.
//
// NO Bind(NetClient*): the binary addresses g_NetClient 0x8156A0 as a GLOBAL (the
// builders never receive a socket). The old Bind()/net_ pair was PROVEN DEAD CODE —
// `inventory_.Bind(...)` existed NOWHERE in the composition (only skillTree_.Bind(),
// UI/GameWindows.cpp:72) -> net_ always null -> NotifyServerItemMove()'s
// `if (!net_) return` blocked all sends. Now uses net::GlobalNetClient()
// (Net/NetClient.h:67-68), set by ConnectGameServer. Same fix as UI/GuildWindow.h (W6).
// ===========================================================================
//
// DATA MODEL (reconciliation, "inventory" mission, 2026-07-14): this window
// PREVIOUSLY read/wrote game::g_World.self.inventory (vector<InvCell>, free x/y
// coordinates, "simplified model" — see git history), a SECOND inventory model
// SEPARATE from game::g_Client.inv (fixed row/col grid) that ALL the already-wired
// network handlers write to (Net/GameHandlers_InvCells.cpp,
// Net/ItemActionDispatch.cpp, Net/GameHandlers_VendorTrade.cpp, ... 20 files).
// VERDICT (confirmed by disassembly, see the game::InventoryState comment in
// Game/ClientRuntime.h): game::g_Client.inv IS the faithful model — addressing
// [384*row + 6*col] (Pkt_ItemUpgradeResult 0x488DE0) / [(row%100)*0x600 +
// (col%100)*0x18] (Pkt_ItemActionDispatch 0x46A320), both reducing to
// InventoryState::At(row,col) with kCols=64. game::g_World.self.inventory was
// REMOVED; this window was migrated to read/write game::g_Client.inv directly,
// with `bagPage_` as `row` (0 or 1, see kMaxBagPages) and a slot `col` (0..63)
// derived from the visual position (gridX,gridY) of the ANCHOR cell — a
// convention already independently established by Game/AutoPlaySystem.cpp
// (`g_Client.inv.cells[page*InventoryState::kCols + col]`).
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

#include "Core/Types.h"
#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"
#include "Game/GameState.h"
#include "Game/ClientRuntime.h" // game::g_Client.inv (InventoryState): source-of-truth model, see banner above

namespace ts2::ui {

// Drag&drop source (mirrors g_DragCtx.srcType, g_DragCtx 0x1822380 +0x10).
enum class DragSource : int {
    None   = 0,
    Bag    = 1,   // bag        (g_InvMain   0x16732B0)
    Equip  = 2,   // equipment  (g_EquipMain 0x16731D8)
    Quiver = 6,   // quiver     (g_QuiverMain 0x1673EB4)
};

// "click to pick up / click again to drop" context — partial mirror of g_DragCtx
// 0x1822380. Offsets PROVEN by decompiling Item_BeginDragTransaction 0x5AFDF0
// (`*(this+N) = aM`, this typed int* -> offset = 4*N).
//
// ASSUMED STRUCTURAL GAP (reported in W6): in the binary g_DragCtx is a GLOBAL
// shared by ALL drop widgets (inventory, warehouse, quiver, quickbar, merge) AND by
// the incoming network handlers, which call Item_DragState_Clear 0x5B02D0 when the
// ack arrives. Here the context is a MEMBER of InventoryWindow: so
// Net/GameHandlers_InvCells.cpp cannot clear it (it only clears
// net::g_GmCmdCooldownLatch). Faithful but incomplete consequence: after an emitted
// drop, the item stays stuck to the cursor (pendingAck) until a future front
// re-centralizes g_DragCtx. Do NOT "fix" this with an optimistic local reset: the
// binary doesn't do it either (see header banner).
struct DragContext {
    bool       active      = false;             // +0x08 active   (entry guard of 0x5AFDF0)
    bool       pendingAck  = false;             // +0x0C ack pending: set to 0 on PICKUP
                                                //       (`*(this+3) = 0` @0x5AFDF0), to 1 after
                                                //       EACH emission (e.g. @0x5B2301, 0x5BA297)
    DragSource srcType     = DragSource::None;  // +0x10 srcType (a2)
    int        srcPage     = 0;                 // +0x14 srcPage (a3)
    int        srcSlot     = -1;                // +0x18 srcSlot (a4)
    uint32_t   itemId      = 0;                 // +0x1C itemId  (a5)
    // +0x20 (a6): field DEPENDS ON SOURCE TYPE (see header banner).
    //   srcType 1 (bag)   -> gridX      (0x62B5FB)
    //   srcType 2 (equip) -> durability (0x62B199) — THIS is the field VaultReq_213 reads
    int        aux20       = 0;
    int        count       = 0;                 // +0x28 count (a8) — read by VaultReq_208/210/212
    int        grabOffsetX = 0;                 // +0x44 grabOffsetX (a12)
    int        grabOffsetY = 0;                 // +0x48 grabOffsetY (a13)
    void reset() { *this = DragContext{}; }
};

// Equipment sub-tab (cGameHud invSubTab this[227] / +0x38C).
enum class EquipSubTab : int {
    EquipPage1 = 1, // slots 0..8  (9 slots)
    EquipPage2 = 2, // slots 9..12 (4 slots)
    Quiver     = 3, // quiver (not drawn here)
};

class InventoryWindow {
public:
    // itemId -> icon .IMG file path resolver (empty string if unknown).
    // Wired by default in Init() to ResolveItemIconPath (InventoryWindow.cpp):
    // "G03_GDATA\D01_GIMAGE2D\002\002_%05u.IMG" formatted with ITEM_INFO(itemId).IconID
    // (game::ItemInfo::iconId, field +192, SEPARATE from itemId) — CONFIRMED by disassembly,
    // see Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md and the detailed comment in InventoryWindow.cpp.
    // Replaceable via SetIconResolver() before/after Init().
    using IconPathResolver = std::string (*)(uint32_t itemId);

    // Takes the renderer's device + a font (optional, can be nullptr).
    bool Init(gfx::Renderer& renderer, gfx::Font* font);
    void Shutdown();

    // Around a D3D9 device Reset() (UI/GameHud.h::OnDeviceLost/Reset pattern):
    // sprite_ is an ID3DXSprite owned by this window (not UIManager's shared batch)
    // — it MUST be released before Reset() and rebuilt after, or it will
    // crash/corrupt state (D3DERR_DEVICELOST). background_/ownIconCache_/
    // sharedIconCache_/whiteTex_ are all D3DPOOL_MANAGED: restored automatically, nothing to do.
    void OnDeviceLost();
    void OnDeviceReset();

    // NO Bind(net::NetClient*): the binary addresses g_NetClient 0x8156A0 as a GLOBAL
    // (the 234 Net_Send* builders never receive a socket). Emission therefore goes
    // through net::GlobalNetClient() (see header banner + Emit* helpers). The old
    // Bind()/net_ pair was PROVEN DEAD CODE (no `inventory_.Bind(...)` anywhere in the
    // composition — only skillTree_.Bind() exists, UI/GameWindows.cpp:72): removed.

    void SetIconResolver(IconPathResolver r) { iconResolver_ = r; }

    // SHARED icon GPU cache (memory pooling, see header banner of
    // Gfx/IconTextureCache.h): injected by UI/GameWindows.cpp (same instance as
    // WarehouseWindow/EnchantWindow/VendorShopWindow) so an icon common to several
    // windows is loaded/uploaded to VRAM only ONCE.
    // nullptr (fallback) => uses ownIconCache_ below, private to this window
    // (never happens in production, only if this window is built outside
    // GameWindows, e.g. an isolated unit test).
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }
    // Panel background (HUD sprite #299 in the original); recenters the anchor.
    bool SetBackgroundImage(const std::string& imgPath);
    // Current screen size -> recomputes the base (UI_ProjectSpriteToScreen).
    void SetScreenSize(int width, int height);
    // Up-to-date cursor position (to draw the item being dragged).
    void SetCursorPos(int x, int y) { cursorX_ = x; cursorY_ = y; }

    // Lifecycle (cGameHud_ResetUiState 0x62AFB0 / cGameHud_Hide 0x62B050).
    void Open();
    void Close();
    void Toggle();
    bool IsOpen() const { return visible_; }

    void SetEquipSubTab(EquipSubTab t) { equipSubTab_ = t; }
    // cGameHud_OnMouseDown case 1 (button unk_93F88C 0x62bc2b): page 1 is only
    // offered if g_Inv_ExtraPageCount >= 1. Read directly via
    // game::g_Client.VarGet(0x16732A8) — NOT via a dedicated SelfState field (such a
    // field briefly existed then was removed: nothing wrote it,
    // Net/GameVarDispatch.cpp case 88 exclusively feeds the Var() escape hatch, the
    // same source already used by Game/QuestSystem.cpp — a second unwritten field
    // would have been yet another silent duplication). Otherwise the click is
    // silently refused here (the binary displays StrTable005_Get(156), not
    // reproduced — out of scope for the system HUD). See kMaxBagPages below: the
    // binary never exposes a page > 1.
    void SetBagPage(int page) {
        if (page != 0 && game::g_Client.VarGet(0x16732A8) < 1) return;
        bagPage_ = (page != 0) ? 1 : 0;
    }

    // Rendering (inventory/equipment subset of cGameHud_Render 0x64A900).
    void Render();

    // Mouse events. Returns true => consumed ("first consumer wins" rule).
    bool OnMouseDown(int mouseX, int mouseY); // cGameHud_OnMouseDown 0x62B080
    bool OnMouseUp(int mouseX, int mouseY);   // cGameHud_OnMouseUp   0x62DFA0

    const DragContext& Drag() const { return drag_; }

private:
    struct SlotRect { int l, t, r, b; };
    struct TextItem { int x, y; std::string text; D3DCOLOR color; };

    // --- Geometry (faithful to the disassembly) ---
    void     RecomputeLayout();                          // base via UI_ProjectSpriteToScreen
    SlotRect EquipSlotRect(int slot) const;              // this[4*slot+2..+5]
    int      EquipSlotRectAt(int mx, int my) const;      // slot under cursor (filled or not)
    int      EquipSlotAt(int mx, int my) const;          // cGameHud_EquipSlotAtFilled 0x64EFC0
    bool     GridCellAt(int mx, int my, int& col, int& row) const; // 8x8 (0x64F9F0)
    // Occupied slot (0..63) under the cursor in game::g_Client.inv, page bagPage_ — NOT
    // a vector index (see file header banner: single g_Client.inv model).
    int      InvCellAt(int mx, int my) const;
    static int ItemGridSize(uint32_t itemId);            // 1x1 (type 2/7/11) else 2x2
    // Anchor slot (0..63) of a cell within a page of game::g_Client.inv, derived
    // from its visual 8x8 position — a convention already independently established
    // by Game/AutoPlaySystem.cpp (page*InventoryState::kCols + col). ALSO used as
    // dstSlot for bag->bag / equip->bag emissions (see EmitMoveBagToBag/
    // EmitUnequipToBag): the binary computes its destination via
    // cGameHud_PlaceItemIntoBag 0x650470 / cGameHud_FindInvPlacement 0x64FCA0 (not
    // fully disassembled); this cursor-cell -> slot mapping is the client-side
    // coherent approximation (see report).
    static uint32_t StorageCol(uint32_t gridX, uint32_t gridY) {
        return gridY * static_cast<uint32_t>(kGridCols) + gridX;
    }

    // --- Rendering ---
    void               DrawItemIcon(uint32_t itemId, int x, int y, int wPx, int hPx, int count);
    gfx::GpuTexture*   GetIconTex(uint32_t itemId);      // lazy-load + cache (shared, see SetIconCache)
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    // --- Drag&drop ---
    bool BeginPickup(int mx, int my);   // pickup (removes from source, Item_BeginDragTransaction 0x5AFDF0)
    bool PlaceDrag(int mx, int my);     // drop: emits the matching VaultReq (see helpers below)
    void CancelDrag();                  // return to source (= Inv_AddItemQuantity + Item_DragState_Clear)
    uint32_t DragColor() const;
    uint32_t DragDurability() const;

    // --- Network emission — mirrors UI_MainInventory_OnLButtonUp 0x5B20B0
    // (switch(g_DragCtx+0x10 /*srcType*/)). Each helper addresses g_NetClient
    // 0x8156A0 via net::GlobalNetClient() (the binary's GLOBAL pattern, see header
    // banner): NO socket injection. They reproduce the proven guards, emit, set
    // pendingAck, BUT write NO destination cell (the binary emits then waits for
    // the server reply). Return true = click consumed.
    bool EmitMoveBagToBag(int col, int row, int occ);   // VaultReq_208 @0x5B22FC (bag->bag)
    bool EmitEquipFromBag(int equipSlot);               // VaultReq_210 @0x5B2555 (bag->equip)
    bool EmitUnequipToBag(int col, int row, int occ);   // VaultReq_213 @0x5BA28C (equip->bag)
    // Universal guard (0x5B2297 / 0x5B23E7 / 0x5BA312): morph in progress OR
    // request already in flight -> true (caller refuses: restores + closes the drag).
    bool EmissionBlockedByMorphOrLatch() const;
    // Common post-emission epilogue (0x5B2301.. / 0x5BA291..): pendingAck, anti-spam
    // lock g_GmCmdCooldownLatch, timestamp flt_1675B0C, dirty flag.
    void MarkEmissionPending();

    bool PointInPanel(int mx, int my) const;

    // Flat-fill rectangle (same technique as UI/UIManager.cpp::FillRect: 1x1 white
    // texture stretched + modulated by `color`). MUST be called INSIDE the
    // sprite_ batch (between Begin/End). Used for the neutral background under
    // item icons (see DrawItemIcon) — NO LONGER used to gray out the source cell
    // of a drag: see the Render() comment (CORRECTED by disassembly, 2026-07-14 —
    // the binary does not gray out the source, it empties it).
    void FillRect(int x, int y, int w, int h, D3DCOLOR color);

    IDirect3DDevice9*  device_  = nullptr;
    gfx::Font*         font_    = nullptr;
    gfx::SpriteBatch   sprite_;
    gfx::GpuTexture    background_;
    // Icon cache: see SetIconCache()/ActiveIconCache() above. ownIconCache_ is only
    // a fallback (never used when this window is owned by UI::GameWindows, which
    // always injects a SHARED cache in Init()).
    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;
    IconPathResolver   iconResolver_ = nullptr;
    IDirect3DTexture9* whiteTex_ = nullptr; // 1x1 white texture (FillRect utility above)

    bool        visible_     = false;                    // this[175] / +0x2BC
    int         activeTab_   = 1;                        // this[226] / +0x388 (1=inv/equip)
    EquipSubTab equipSubTab_ = EquipSubTab::EquipPage1;  // this[227] / +0x38C
    int         bagPage_     = 0;                        // this[228] / +0x390

    int screenW_ = ts2::kRefWidth;
    int screenH_ = ts2::kRefHeight;
    int baseX_ = 0, baseY_ = 0;      // this[0]/this[1] (panel screen origin)
    int bgHalfW_ = 0, bgHalfH_ = 0;  // background half-dimensions (recentering)
    int cursorX_ = 0, cursorY_ = 0;

    DragContext      drag_;
    game::InvCell    dragBagCell_{};   // source backup (bag) for cancel/exchange
    game::EquipSlot  dragEquipCell_{}; // source backup (equip) for cancel/exchange

    std::vector<TextItem> pendingText_; // deferred text pass (outside sprite batch)

    // --- Geometry constants pulled from the disassembly ---
    // ACTUAL CAPACITY (verified by decompiling cGameHud_InvCellAt 0x64F9F0 and
    // Inv_FindFreeCellForItem 0x650FA0, 2026-07-14): grid = 8 columns x 8 rows
    // = 64 cells PER PAGE (loops `for(i<8) for(j<8)` / `for(k<64)` confirmed in
    // both functions), NOT a bigger grid. The bag has AT MOST 2 pages
    // (kMaxBagPages): page 0 always active, page 1 conditional on
    // game::g_Client.VarGet(0x16732A8) >= 1 (g_Inv_ExtraPageCount, see SetBagPage
    // above) — cGameHud_OnMouseDown 0x62B080 case 1 only has 2 page buttons
    // (unk_93F7F8 -> page 0, unk_93F88C -> page 1, the latter gated by
    // g_Inv_ExtraPageCount): the binary never exposes a 3rd page.
    // Total bag capacity = kMaxBagPages * kGridCols * kGridRows = 128 cells max.
    static constexpr int kMaxBagPages = 2;
    static constexpr int kRefX     = 764; // arg a3 of UI_ProjectSpriteToScreen (cGameHud)
    static constexpr int kRefY     = 182; // arg a4
    static constexpr int kGridCols = 8;
    static constexpr int kGridRows = 8;
    static constexpr int kCellStep = 26;  // cell step (base+26*i+34 / base+26*j+193)
    static constexpr int kCellOffX = 34;  // X offset of the 1st column
    static constexpr int kCellOffY = 193; // Y offset of the 1st row
    static constexpr int kCellSize = 25;  // 59-34 == 218-193

    static constexpr D3DCOLOR kLabelColor    = 0xFFFFEE66u; // item id fallback (pale yellow)
    static constexpr D3DCOLOR kCountColor    = 0xFFFFFFFFu; // stack counter (white)
};

} // namespace ts2::ui

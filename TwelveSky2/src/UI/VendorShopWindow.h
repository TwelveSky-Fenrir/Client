// UI/VendorShopWindow.h — VENDOR (NPC shop) window of the TS2 client.
//
// DIRECT reverse-engineering of the vendor catalog state, which is NOT grouped
// into a dedicated struct on the binary side (unlike the warehouse/WarehouseSystem):
// it's a long tail of scalar globals populated entry by entry by the inbound
// handler Pkt_VendorItemEntry (opcode 0x25), see
// Net/GameHandlers_VendorTrade.cpp (DO NOT edit):
//   dword_1826134            (Var 0x1826134) = number of loaded catalog entries
//   dword_1826128            (Var 0x1826128) = number of pages (10 entries/page)
//   dword_1826130            (Var 0x1826130) = current selection (-1 = none)
//   dword_182613C[idx]       (Var(0x182613C + 4*idx))  = itemId of entry idx
//   dword_1834F84[3*idx..+8] (Var(0x1834F84 + 12*idx) + 4 + 8) = price (3 components)
// These globals are accessed via the g_Client.Var()/VarGet() escape hatch from
// Game/ClientRuntime.h (already written, DO NOT edit) — NO dedicated field is
// required since the catalog already fully lives at these addresses.
//
// The window itself only DISPLAYS/PAGINATES this catalog and drives the LOCAL
// selection (Var(0x1826130)).
//
// PURCHASE (updated 2026-07-16, wave W6 — fresh IDA evidence): the real purchase
// handler is UI_NpcShop_OnRDown_Buy 0x5e5000 (RIGHT click on a vendor slot), the
// ONLY emission point of the UI_NpcShop_* family (Enter 0x5e43f0 / OnLDown 0x5e44a0 /
// OnLUp 0x5e4760 are 100% local). Its guard chain is now faithfully reproduced in
// HandleBuyClick (see the .cpp), but EMISSION stays blocked: 4 of the 7 packet
// fields (page/freeSlot/col/row) come from Inventory_FindFreeGridSlot 0x54DDE0 +
// Inventory_FindFreePageSlot 0x54E1D0, which have no C++ equivalent (inventory
// allocation logic belonging to Game/). The transport itself is KNOWN and
// AVAILABLE (Net_SendPacket_Op19, sub-code 215, 100-byte block) — cf. TODO
// [anchor 0x5e562a] in the .cpp and the wave report.
//
// Icon + name of an entry: NOT reverse-engineered from the network packet itself —
// the raw server name (p.name, 13 bytes, VendorItemEntry 0x25) is a TODO(state) not
// stored on the Net/GameHandlers_VendorTrade.cpp side (unk_18270DC + 13*idx). Name +
// icon are therefore resolved locally from the ONLY reliable field already captured,
// the itemId (dword_182613C[idx]), via the already-loaded ITEM_INFO database
// (Game/GameDatabase.h) — same source as InventoryWindow (ItemInfo::name +4,
// ItemInfo::iconId +192). Icon file path identical to the
// InventoryWindow.cpp/ResolveItemIconPath pattern:
// "G03_GDATA\D01_GIMAGE2D\002\002_%05u.IMG" formatted with ItemInfo::iconId (NOT itemId).
//
// Project rule: this file does NOT edit any existing header; it includes
// UI/UIManager.h and Game/ClientRuntime.h read-only.
#pragma once
#include "UI/UIManager.h"
#include "Game/ClientRuntime.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace ts2::ui {

class VendorShopWindow : public Dialog {
public:
    VendorShopWindow();

    // Opening: realigns the current page with the existing selection (if an
    // entry is already selected by a network handler) and resets the purchase
    // quantity to 1. Closing: cancels the current selection (Var(0x1826130)=-1)
    // to avoid leaving anything "dangling" between two openings, like WarehouseWindow.
    void Open() override;
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // SHARED GPU icon cache (memory pooling, cf. Gfx/IconTextureCache.h): injected
    // by UI/GameWindows.cpp, same instance as InventoryWindow/WarehouseWindow/
    // EnchantWindow. nullptr (fallback) => local ownIconCache_ (never the case in
    // production, cf. InventoryWindow::SetIconCache).
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }

private:
    struct Rect { int x, y, w, h; };

    // --- Catalog access (original addresses, via g_Client.Var/VarGet) ---
    static constexpr uint32_t kAddrEntryCount = 0x1826134; // dword_1826134
    static constexpr uint32_t kAddrPageCount  = 0x1826128; // dword_1826128
    static constexpr uint32_t kAddrSelection  = 0x1826130; // dword_1826130
    static constexpr uint32_t kAddrItemIdBase = 0x182613C; // dword_182613C[idx]
    static constexpr uint32_t kAddrPriceBase  = 0x1834F84; // dword_1834F84[3*idx]
    static constexpr uint32_t kAddrVendorNpc  = 0x1837E6C; // dword_1837E6C (vendor id/param, Pkt_VendorInventoryLoad)

    int  EntryCount() const;                 // Var(kAddrEntryCount), clamped >=0
    int  PageCount() const;                  // Var(kAddrPageCount), clamped >=1
    int  Selection() const;                  // Var(kAddrSelection) (-1 = none)
    void SetSelection(int idx);
    uint32_t ItemIdAt(int idx) const;         // Var(kAddrItemIdBase + 4*idx)
    uint32_t PriceAt(int idx, int component) const; // Var(kAddrPriceBase + 12*idx + 4*component)
    std::string ItemNameAt(int idx) const;    // ITEM_INFO(itemId).name, falls back to "#<id>" if db absent/slot unknown

    // --- Icon (lazy load + cache, InventoryWindow::GetIconTex pattern) ---
    gfx::GpuTexture* GetIconTex(const UiContext& ctx, uint32_t itemId);
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    // Repositions (x_, y_) EVERY FRAME from the REAL screen dimensions
    // (ctx.screenW/H), instead of the calculation frozen once at construction (bug
    // fixed 2026-07-14 — cf. .cpp header comment for the IDA evidence:
    // UI_NpcShop_Draw/OnLDown/HitTestWindow all reproduce the same design anchor
    // (764,182) via UI_ProjectSpriteToScreen 0x50f5d0). Same pattern as
    // AutoPlayWindow::RecomputeLayout / PlayerTradeWindow::Layout / OptionsWindow::Layout.
    void RecomputeAnchor(int screenW, int screenH);

    // --- Geometry (panel coordinates, reference 1024x768) ---
    Rect PanelRect() const;
    Rect CloseButtonRect() const;
    Rect RowRect(int rowInPage) const;        // row 0..kRowsPerPage-1
    Rect PrevPageButtonRect() const;
    Rect NextPageButtonRect() const;
    Rect QtyMinusButtonRect() const;
    Rect QtyPlusButtonRect() const;
    Rect BuyButtonRect() const;
    bool RowAt(int mx, int my, int& outRowInPage) const;
    bool PointInPanel(int mx, int my) const;

    // --- Actions ---
    void HandleRowClick(int rowInPage);
    void HandleBuyClick();
    void ClampPage();

    // Purchase refusal: reproduces `Msg_AppendSystemLine(g_ChatManager,
    // StrTable005_Get(g_LangId, id), g_SysMsgColor)` — the refusal idiom of
    // UI_NpcShop_OnRDown_Buy 0x5e5000 (11 sites, EA 0x5e5243..0x5e566c).
    // `strTableId` = EXACT StrTable005 id found in the disassembly.
    void Refuse(int strTableId);

    static constexpr int kRowsPerPage = 10;   // faithful: "10 entries/page"
    static constexpr int kGridPad     = 12;
    static constexpr int kHeaderH     = 26;
    static constexpr int kRowH        = 22;
    static constexpr int kRowGap      = 2;
    static constexpr int kCloseSize   = 18;
    static constexpr int kPageBtnW    = 24;
    static constexpr int kPageBtnH    = 20;
    static constexpr int kQtyBtnW     = 20;
    static constexpr int kQtyBtnH     = 20;
    static constexpr int kBuyBtnW     = 110;
    static constexpr int kBuyBtnH     = 26;
    static constexpr int kFooterH     = 84;
    // Vertical offsets of the 3 footer lines (relative to footerTop), computed
    // once here so geometry and rendering stay in sync.
    static constexpr int kFooterRow1Y = 6;                                   // pagination
    static constexpr int kFooterRow2Y = kFooterRow1Y + kPageBtnH + 8;        // qty + Buy
    static constexpr int kFooterRow3Y = kFooterRow2Y + kBuyBtnH + 8;         // status/gold

    static constexpr int kPanelW = 340;
    static constexpr int kPanelH = kHeaderH + kGridPad
        + kRowsPerPage * kRowH + (kRowsPerPage - 1) * kRowGap
        + kGridPad + kFooterH;

    // --- Palette (D3DCOLOR = 0xAARRGGBB, cf. mission spec) ---
    static constexpr D3DCOLOR kColPanelBg   = 0xE0202028u; // panel background
    static constexpr D3DCOLOR kColFrame     = 0xFF808080u; // frame
    static constexpr D3DCOLOR kColTitle     = 0xFFFFDD66u; // title
    static constexpr D3DCOLOR kColText      = 0xFFFFFFFFu; // text
    static constexpr D3DCOLOR kColTextDim   = 0xFFAAAAAAu; // dimmed text
    static constexpr D3DCOLOR kColSelect    = 0xFF4060A0u; // hover/selection
    static constexpr D3DCOLOR kColError     = 0xFFFF6060u; // error
    static constexpr D3DCOLOR kColSuccess   = 0xFF60FF60u; // success
    static constexpr D3DCOLOR kColHeaderBg  = 0xFF2A2A34u; // title bar
    static constexpr D3DCOLOR kColRowBg     = 0xFF34343Eu; // row (even)
    static constexpr D3DCOLOR kColRowBgAlt  = 0xFF2C2C34u; // row (odd)
    static constexpr D3DCOLOR kColBtnBg     = 0xFF3A3A46u; // active button
    static constexpr D3DCOLOR kColBtnBgOff  = 0xFF262629u; // disabled button
    static constexpr D3DCOLOR kColIconFallback = 0xFF484858u; // icon fallback (resolution/texture failed)

    int         curPage_   = 1;   // currently displayed page (1-based), local to the window
    int         qty_       = 1;   // purchase quantity (1..99), local to the window
    std::string statusText_;      // last action status (purchase), shown in the window footer

    // SHARED icon cache (file path -> GPU texture), lazy-loaded on the 1st
    // occurrence of a path; also remembers failures (invalid texture) to avoid
    // retrying every frame, cf. InventoryWindow::GetIconTex / Gfx/IconTextureCache.h.
    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;
};

} // namespace ts2::ui

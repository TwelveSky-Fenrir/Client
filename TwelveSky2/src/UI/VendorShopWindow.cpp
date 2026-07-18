// UI/VendorShopWindow.cpp — vendor shop window implementation.
// See UI/VendorShopWindow.h for the interaction contract and the TODO(send) items.
#include "UI/VendorShopWindow.h"
#include "UI/PanelSkin.h"
#include "Asset/ImgFile.h"
#include "Game/GameDatabase.h"
#include "Game/GameState.h"    // game::g_World.self.level (g_SelfLevel 0x16731A8)

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace ts2::ui {

namespace {

// ---------------------------------------------------------------------------
// Original addresses consulted by the purchase guard chain
// (UI_NpcShop_OnRDown_Buy 0x5e5000). Accessed via the g_Client.Var/VarGet escape
// hatch (Game/ClientRuntime.h) — same convention as Game/QuestSystem.cpp (`Var(0x1675B08)`)
// and especially Game/MapWarp.cpp:191-197, which ALREADY guards an emission with the SAME
// morph/lock pair (EA 0x55CAF9) via WarpAddr::MorphInProgress/CooldownLatch.
// ---------------------------------------------------------------------------
constexpr uint32_t kAddrMorphInProgress = 0x1675A88; // g_MorphInProgress
constexpr uint32_t kAddrCooldownLatch   = 0x1675B08; // g_GmCmdCooldownLatch
constexpr uint32_t kAddrSelfMorphNpcId  = 0x1675A98; // g_SelfMorphNpcId (== 291 => 10% discount)
constexpr uint32_t kAddrVar16747BC      = 0x16747BC; // dword_16747BC (progression tier)
constexpr uint32_t kAddrVar16851B8      = 0x16851B8; // dword_16851B8  (current map/faction)
constexpr uint32_t kAddrVar167616C      = 0x167616C; // dword_167616C  (guard for item 2314)

// EXACT StrTable005 ids found in the disassembly of UI_NpcShop_OnRDown_Buy 0x5e5000
// (convention from Game/CharSelectFlow.cpp:39: the id is the source of truth, the text
// comes from game::Str() which resolves the real 005.DAT table loaded by App::Init).
// (id 117 "bag full", EA 0x5e5661, is not listed: its guard depends on
//  Inventory_FindFreeGridSlot, not modeled — cf. TODO (f) in HandleBuyClick)
constexpr int kStrMsg214  = 214;  // insufficient resource vs cost (EA 0x5e54aa)
constexpr int kStrMsg606  = 606;  // level 113 required (EA 0x5e5243)
constexpr int kStrMsg1414 = 1414; // insufficient currency vs ITEM_INFO+228 (EA 0x5e54e1)
constexpr int kStrMsg1416 = 1416; // dword_16747BC < 1 (EA 0x5e5281)
constexpr int kStrMsg1909 = 1909; // dword_16747BC < 7 (EA 0x5e52c0)
constexpr int kStrMsg2307 = 2307; // level 145 required (EA 0x5e5302)
constexpr int kStrMsg2385 = 2385; // dword_16851B8 == 50 (EA 0x5e5340)
constexpr int kStrMsg2055 = 2055; // item forbidden here (EA 0x5e556b)
constexpr int kStrMsg2547 = 2547; // dword_167616C >= 1, item 2314 (EA 0x5e55d3, `push 9F3h`)

// itemIds tested by the prerequisite `switch (*v32)` (EA 0x5e51aa). Values in hex
// in the binary, reported as-is for traceability.
constexpr uint32_t kItemPrereqLvl113A = 0x44A; // 1098
constexpr uint32_t kItemPrereqLvl113B = 0x449; // 1097
constexpr uint32_t kItemPrereqLvl113C = 0x417; // 1047
constexpr uint32_t kItemPrereqStage1  = 0x59A; // 1434
constexpr uint32_t kItemPrereqStage7  = 0x219; // 537
constexpr uint32_t kItemPrereqLvl145  = 0x29B; // 667
constexpr uint32_t kItemPrereqMap50   = 0x3D3; // 979

// Real panel background (best effort): (400,440) template from the UI atlas folder
// G03_GDATA/D01_GIMAGE2D/001 — candidate NOT CONFIRMED by IDA, chosen by ratio
// proximity with the Vendor panel (340x372, near-identical ratio; cf. detailed
// methodology in UI/PanelSkin.h). Distinct index from the one used by
// OptionsWindow (same size cluster, different files). Automatic fallback to
// kColPanelBg if absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_03640.IMG");

// Truncates `s` (in place) so its rendered width does not exceed `maxW` px, by
// appending an ellipsis ".." — same spirit as InventoryWindow's id/name fallback,
// but measured via the UI font (ctx.MeasureText) since the ITEM_INFO name has
// variable length (unlike the fixed "#<id>" label displayed previously). No-op
// if the font is unavailable (MeasureText -> 0).
void TruncateToWidth(const UiContext& ctx, std::string& s, int maxW) {
    if (maxW <= 0) return;
    if (ctx.MeasureText(s.c_str()) <= maxW) return;
    while (!s.empty() && ctx.MeasureText((s + "..").c_str()) > maxW)
        s.pop_back();
    s += "..";
}
} // namespace

VendorShopWindow::VendorShopWindow() {
    // Default position before the first Render (reference 1024x768); ACTUALLY
    // recomputed EVERY frame by RecomputeAnchor(ctx.screenW, ctx.screenH), see
    // Render() below. Do NOT remove this call in Render(): freezing it here as
    // before (fixed bug) desyncs x_/y_ from the real display resolution as soon
    // as it differs from the reference at object construction time.
    RecomputeAnchor(ts2::kRefWidth, ts2::kRefHeight);
}

// REAL anchor (fresh IDA evidence, 2026-07-14, "broken coordinates" re-audit):
// UI_NpcShop_Draw (0x5e4910), UI_NpcShop_OnLDown (0x5e44a0), and
// UI_NpcShop_HitTestWindow (0x5e4f60) — the binary's 3 functions of the NPC
// "vendor" window (case 8 of dispatcher UI_NpcWin_Draw_Dispatch 0x5de180) —
// ALL literally execute the same sequence:
//   UI_ProjectSpriteToScreen(&g_PlayerCmdController, /*sprite*/299, /*x*/764, /*y*/182,
//                            &this->x, &this->y);      // 0x50f5d0
//   this->x = (this->x + 23) - Sprite2D_GetWidth(panelSprite);   // X post-adjustment
//   // this->y is NOT re-adjusted after the projection.
// UI_ProjectSpriteToScreen (0x50f5d0) computes:
//   outX = round(scaleX * (764 + spriteW/2)) - spriteW/2   with scaleX = current screen / reference (1024)
//   outY = round(scaleY * (182 + spriteH/2)) - spriteH/2   with scaleY = current screen / reference (768)
// i.e. a design-space ANCHOR (764,182), NOT a screen centering — and the
// actual/reference scale factor IS indeed applied by the original engine (so
// NOT a simple frozen `(kRefWidth-kPanelW)/2` like the old constructor did).
// kPanelW/kPanelH stand in as an approximation of the real width/height of the
// background sprite (not statically extractable, loaded from a .IMG at runtime).
// At scale 1 (resolution 1024x768 = reference, cf. the `/0/0/2/1024/768` launch
// in CLAUDE.md), the formula reduces exactly to x_=764+23-kPanelW,
// y_=182 — this is the position used for verification screenshots.
void VendorShopWindow::RecomputeAnchor(int screenW, int screenH) {
    constexpr int kAnchorX = 764; // design-space (UI_NpcShop_Draw/OnLDown/HitTestWindow)
    constexpr int kAnchorY = 182;
    const float scaleX = static_cast<float>(screenW)  / static_cast<float>(ts2::kRefWidth);
    const float scaleY = static_cast<float>(screenH) / static_cast<float>(ts2::kRefHeight);
    const int projX = static_cast<int>(scaleX * (kAnchorX + kPanelW / 2.0f) + 0.5f) - kPanelW / 2;
    const int projY = static_cast<int>(scaleY * (kAnchorY + kPanelH / 2.0f) + 0.5f) - kPanelH / 2;
    x_ = projX + 23 - kPanelW; // X post-adjustment confirmed in the 3 IDA functions
    y_ = projY;                // y not re-adjusted after the projection (same as binary)
}

// ============================================================================
// Catalog access (original addresses, via g_Client.Var/VarGet)
// ============================================================================
int VendorShopWindow::EntryCount() const {
    const int n = game::g_Client.VarGet(kAddrEntryCount);
    return n > 0 ? n : 0;
}

int VendorShopWindow::PageCount() const {
    const int n = game::g_Client.VarGet(kAddrPageCount);
    return n > 0 ? n : 1;
}

int VendorShopWindow::Selection() const {
    return game::g_Client.VarGet(kAddrSelection);
}

void VendorShopWindow::SetSelection(int idx) {
    game::g_Client.Var(kAddrSelection) = idx;
}

uint32_t VendorShopWindow::ItemIdAt(int idx) const {
    return static_cast<uint32_t>(game::g_Client.VarGet(kAddrItemIdBase + 4u * static_cast<uint32_t>(idx)));
}

uint32_t VendorShopWindow::PriceAt(int idx, int component) const {
    return static_cast<uint32_t>(game::g_Client.VarGet(
        kAddrPriceBase + 12u * static_cast<uint32_t>(idx) + 4u * static_cast<uint32_t>(component)));
}

// Real name via the local ITEM_INFO database (cf. UI/VendorShopWindow.h: the raw
// server name, p.name 13 bytes, is not stored on the GameHandlers_VendorTrade.cpp
// side). Falls back to "#<id>" if the database isn't loaded or if the itemId slot
// is empty/out of bounds (game::GetItemInfo returns nullptr) — stays at least as
// informative as the old raw "#<id>" display it replaces.
std::string VendorShopWindow::ItemNameAt(int idx) const {
    const uint32_t id = ItemIdAt(idx);
    if (const game::ItemInfo* info = game::GetItemInfo(id))
        return std::string(info->name, strnlen(info->name, sizeof(info->name)));
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%u", id);
    return buf;
}

// Icon for a catalog entry: path derived from ITEM_INFO::iconId (NOT itemId,
// cf. detailed comment in InventoryWindow.cpp/ResolveItemIconPath — same icon
// pool G03_GDATA\D01_GIMAGE2D\002\, template "002_%05u.IMG"). Lazy loading +
// per-itemId cache (single attempt, failures included, no per-frame retry),
// same pattern as InventoryWindow::GetIconTex.
gfx::GpuTexture* VendorShopWindow::GetIconTex(const UiContext& ctx, uint32_t itemId) {
    // Cache SHARED by file path (cf. SetIconCache/ActiveIconCache): an icon
    // already loaded by InventoryWindow/WarehouseWindow/EnchantWindow is reused
    // without re-decoding/re-uploading to VRAM (same .IMG file, same ITEM_INFO::iconId).
    if (!ctx.renderer || !ctx.renderer->Device()) return nullptr;
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return nullptr;
    char path[64];
    std::snprintf(path, sizeof(path), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return ActiveIconCache().GetOrLoad(ctx.renderer->Device(), path);
}

// ============================================================================
// Lifecycle
// ============================================================================
void VendorShopWindow::Open() {
    Dialog::Open();
    statusText_.clear();
    qty_ = 1;
    // Realigns the current page with the already-active selection (e.g.: reopened
    // after being pushed to the background by another modal window), otherwise
    // keeps the last visited page, clamped to the actual page count.
    const int sel = Selection();
    if (sel >= 0 && sel < EntryCount())
        curPage_ = sel / kRowsPerPage + 1;
    ClampPage();
}

void VendorShopWindow::Close() {
    // Does not leave a "dangling" selection between two openings (faithful to
    // the handlers' behavior, which reset Var(0x1826130)=-1 on every list
    // reload — cf. Net/GameHandlers_VendorTrade.cpp, opcode 0x25).
    SetSelection(-1);
    statusText_.clear();
    Dialog::Close();
}

void VendorShopWindow::ClampPage() {
    const int pc = PageCount();
    if (curPage_ < 1)  curPage_ = 1;
    if (curPage_ > pc) curPage_ = pc;
}

// ============================================================================
// Geometry
// ============================================================================
VendorShopWindow::Rect VendorShopWindow::PanelRect() const {
    return { x_, y_, kPanelW, kPanelH };
}

VendorShopWindow::Rect VendorShopWindow::CloseButtonRect() const {
    return { x_ + kPanelW - kGridPad - kCloseSize, y_ + (kHeaderH - kCloseSize) / 2,
             kCloseSize, kCloseSize };
}

VendorShopWindow::Rect VendorShopWindow::RowRect(int rowInPage) const {
    return { x_ + kGridPad, y_ + kHeaderH + kGridPad + rowInPage * (kRowH + kRowGap),
             kPanelW - 2 * kGridPad, kRowH };
}

VendorShopWindow::Rect VendorShopWindow::PrevPageButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kGridPad, footerTop + kFooterRow1Y, kPageBtnW, kPageBtnH };
}

VendorShopWindow::Rect VendorShopWindow::NextPageButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kPanelW - kGridPad - kPageBtnW, footerTop + kFooterRow1Y, kPageBtnW, kPageBtnH };
}

VendorShopWindow::Rect VendorShopWindow::QtyMinusButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kGridPad, footerTop + kFooterRow2Y + (kBuyBtnH - kQtyBtnH) / 2, kQtyBtnW, kQtyBtnH };
}

VendorShopWindow::Rect VendorShopWindow::QtyPlusButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    static constexpr int kQtyNumW = 30; // width reserved for the quantity display
    return { x_ + kGridPad + kQtyBtnW + kQtyNumW, footerTop + kFooterRow2Y + (kBuyBtnH - kQtyBtnH) / 2,
             kQtyBtnW, kQtyBtnH };
}

VendorShopWindow::Rect VendorShopWindow::BuyButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kPanelW - kGridPad - kBuyBtnW, footerTop + kFooterRow2Y, kBuyBtnW, kBuyBtnH };
}

bool VendorShopWindow::RowAt(int mx, int my, int& outRowInPage) const {
    for (int r = 0; r < kRowsPerPage; ++r) {
        const Rect rr = RowRect(r);
        if (PointInRect(mx, my, rr.x, rr.y, rr.w, rr.h)) {
            outRowInPage = r;
            return true;
        }
    }
    return false;
}

bool VendorShopWindow::PointInPanel(int mx, int my) const {
    return PointInRect(mx, my, x_, y_, kPanelW, kPanelH);
}

// ============================================================================
// Mouse / keyboard events
// ============================================================================
bool VendorShopWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    // Consumes the press-down click if it falls within the panel ("first
    // consumer wins" rule); all the action logic happens on release
    // (OnClick), as documented in the Dialog contract.
    return PointInPanel(x, y);
}

bool VendorShopWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    if (!PointInPanel(x, y)) return false;

    const Rect closeBtn = CloseButtonRect();
    if (PointInRect(x, y, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h)) {
        Close();
        return true;
    }

    int rowInPage = -1;
    if (RowAt(x, y, rowInPage)) {
        HandleRowClick(rowInPage);
        return true;
    }

    const Rect prevBtn = PrevPageButtonRect();
    if (PointInRect(x, y, prevBtn.x, prevBtn.y, prevBtn.w, prevBtn.h)) {
        if (curPage_ > 1) { --curPage_; statusText_.clear(); }
        return true;
    }

    const Rect nextBtn = NextPageButtonRect();
    if (PointInRect(x, y, nextBtn.x, nextBtn.y, nextBtn.w, nextBtn.h)) {
        if (curPage_ < PageCount()) { ++curPage_; statusText_.clear(); }
        return true;
    }

    const bool hasSel = Selection() >= 0 && Selection() < EntryCount();

    const Rect qtyMinus = QtyMinusButtonRect();
    if (hasSel && PointInRect(x, y, qtyMinus.x, qtyMinus.y, qtyMinus.w, qtyMinus.h)) {
        qty_ = std::max(1, qty_ - 1);
        return true;
    }

    const Rect qtyPlus = QtyPlusButtonRect();
    if (hasSel && PointInRect(x, y, qtyPlus.x, qtyPlus.y, qtyPlus.w, qtyPlus.h)) {
        qty_ = std::min(99, qty_ + 1);
        return true;
    }

    const Rect buyBtn = BuyButtonRect();
    if (hasSel && PointInRect(x, y, buyBtn.x, buyBtn.y, buyBtn.w, buyBtn.h)) {
        HandleBuyClick();
        return true;
    }

    // Click inside the panel but outside any active zone (background, header...):
    // consumed anyway, the window is de facto modal while open.
    return true;
}

bool VendorShopWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) {
        Close();
        return true;
    }
    return false;
}

// ============================================================================
// Actions
// ============================================================================
void VendorShopWindow::HandleRowClick(int rowInPage) {
    const int idx = (curPage_ - 1) * kRowsPerPage + rowInPage;
    if (idx < 0 || idx >= EntryCount())
        return; // empty row on the current page: nothing to select.

    if (Selection() == idx) {
        // Re-click on the already selected entry: deselect.
        SetSelection(-1);
        statusText_.clear();
        return;
    }

    SetSelection(idx);
    qty_ = 1; // new selection: purchase quantity reset to 1.
    statusText_.clear();
}

// Refusal: `Msg_AppendSystemLine(g_ChatManager, StrTable005_Get(g_LangId, id),
// g_SysMsgColor)` idiom from UI_NpcShop_OnRDown_Buy 0x5e5000.
// Fidelity TODO [anchor 0x84DFD8]: the original g_SysMsgColor color is not
// modeled — MessageLog applies its default white color (same limitation
// already documented in Game/NpcInteraction.cpp:209). The StrTable005 id itself
// is REALLY resolved by game::Str() (005.DAT table loaded by App::Init).
void VendorShopWindow::Refuse(int strTableId) {
    statusText_ = game::Str(strTableId);
    game::g_Client.msg.System(statusText_);
}

// ============================================================================
// UI_NpcShop_OnRDown_Buy 0x5e5000 — ONLY emission point of the UI_NpcShop_* family.
// The guard chain is reproduced in the EXACT ORDER of the binary; each guard
// carries the EA of the Hex-Rays decompilation.
//
// ASSUMED MODEL DIVERGENCE (inherited, out of scope for this wave): the binary
// reads the item from the NPC ENTRY (`*(*(this+2) + 112*category + 1740 + 4*slot)`,
// EA 0x5e5117); this window reads the dword_182613C catalog (cf. .h header banner).
// The packet's npcId also comes from the NPC entry (`*(_DWORD *)*(this+2)`).
// ============================================================================
void VendorShopWindow::HandleBuyClick() {
    const int sel = Selection();
    if (sel < 0 || sel >= EntryCount()) {
        statusText_ = "Aucun objet selectionne.";
        return;
    }
    const uint32_t catalogItemId = ItemIdAt(sel);

    // (d) MobDb_GetEntry(mITEM, itemId) — entry absent => SILENT abandon,
    //     no message (EA 0x5e5117, test 0x5e5123).
    const game::ItemInfo* entry = game::GetItemInfo(catalogItemId);
    if (!entry) return;

    const uint32_t itemId = entry->itemId; // binary's `*v32` (ITEM_INFO+0)

    // (e) quantity: ITEM_INFO+188 (typeCode == v32[47]) == 2 => 99, else 0
    //     (EA 0x5e5134/0x5e5136/0x5e513f).
    //     FIDELITY NOTE: UI_NpcShop has NO quantity selector — qty_ (this window's
    //     -/+ buttons) is an inherited local construct that does NOT feed the
    //     packet. THIS is the wire quantity. Cf. report (residual).
    const int qty = (entry->typeCode == 2u) ? 99 : 0;

    // (f)(g)(h) Inventory_FindFreeGridSlot 0x54DDE0 -> (page, slot2), else Msg 117
    //     (EA 0x5e515e -> 0x5e5659); then freeSlot = Inventory_FindFreePageSlot(page)
    //     0x54E1D0 (EA 0x5e5172), col = slot2 % 8 (EA 0x5e5185), row = slot2 / 8
    //     (EA 0x5e5191), and SILENT abandon if freeSlot == -1 (EA 0x5e519b).
    //
    // TODO [anchor 0x5e515e / 0x5e5172]: neither of these two functions has a C++
    // equivalent. They depend on Inventory_BuildOccupancyGrid 0x54E010,
    // g_Inv_ExtraPageCount 0x16732A8, and the self block g_SelfCharInvBlock 0x1673170
    // (indexed [384*page + 80 + 6*slot]) — this is inventory ALLOCATION, which
    // belongs to Game/, not this window. As long as they're missing, page/freeSlot/
    // col/row are UNKNOWN: the chain continues to faithfully reproduce the
    // following refusals, but the final emission stays blocked (see (p)).

    // (i) per-item prerequisites: `switch (*v32)` (EA 0x5e51aa).
    const int     selfLevel   = game::g_World.self.level;                    // g_SelfLevel 0x16731A8
    const int32_t var16747BC  = game::g_Client.VarGet(kAddrVar16747BC);
    const int32_t var16851B8  = game::g_Client.VarGet(kAddrVar16851B8);
    switch (itemId) {
    case kItemPrereqLvl113A: // EA 0x5e51b3
    case kItemPrereqLvl113B: // EA 0x5e51f1
    case kItemPrereqLvl113C: // EA 0x5e5230
        if (selfLevel < 113) { Refuse(kStrMsg606); return; }
        break;
    case kItemPrereqStage1:  // EA 0x5e526f
        if (var16747BC < 1) { Refuse(kStrMsg1416); return; }
        break;
    case kItemPrereqStage7:  // EA 0x5e52ad
        if (var16747BC < 7) { Refuse(kStrMsg1909); return; }
        break;
    case kItemPrereqLvl145:  // EA 0x5e52ef
        if (selfLevel < 145) { Refuse(kStrMsg2307); return; }
        break;
    case kItemPrereqMap50:   // EA 0x5e532e
        if (var16851B8 == 50) { Refuse(kStrMsg2385); return; }
        break;
    default:
        // Default branch (EA 0x5e53a1): if (dword_1685E74|78|7C|80) is nonzero
        // AND itemId is in {1447,1448,1449}, the binary scans
        // `Crt_Strcmp(&byte_1686334[130*g_LocalElement + 13*i], byte_1673184)` for
        // i < 10 and refuses with Msg 1506 (EA 0x5e5401) if a match is found.
        // TODO [anchor 0x5e53a1]: the name table byte_1686334 (10 x 13 bytes per
        // element), g_LocalElement 0x1673194, and the local player name byte_1673184
        // are not modeled in this window — guard not reproducible here (cf. report).
        break;
    }

    // (j) cost (EA 0x5e5420..0x5e548b): unit price = ITEM_INFO+220 (v32[55],
    //     iBuyCost); g_SelfMorphNpcId == 291 => Crt_ftol(price * 0.9) — truncation
    //     toward zero, the float constant is the binary's own (0.9f as a double,
    //     EA 0x5e543d / 0x5e5478); stackable (v32[47]==2) => cost = qty * price.
    const int32_t unitPriceRaw = static_cast<int32_t>(entry->field220);
    const bool    morph291     = (game::g_Client.VarGet(kAddrSelfMorphNpcId) == 291);
    const int32_t unitPrice    = morph291
        ? static_cast<int32_t>(static_cast<double>(unitPriceRaw) * 0.8999999761581421) // Crt_ftol 0x760810
        : unitPriceRaw;
    const int64_t cost = (entry->typeCode == 2u)
        ? static_cast<int64_t>(qty) * unitPrice  // EA 0x5e5446 / 0x5e5458
        : static_cast<int64_t>(unitPrice);       // EA 0x5e547d / 0x5e548b

    // (k) g_InvWeight (0x16732AC) < cost => Msg 214 (EA 0x5e5497 -> 0x5e54aa).
    //     NOTE: the binary does compare the field labeled g_InvWeight to the COST; the
    //     "weight" label should therefore be taken with a grain of salt (cf. report). The
    //     comparison is reproduced as-is, without reinterpreting it.
    if (game::g_Client.inv.weight < cost) { Refuse(kStrMsg214); return; }

    // (l) g_Currency (0x1673180) < ITEM_INFO+228 (v32[57]) => Msg 1414 (EA 0x5e54ce -> 0x5e54e1).
    if (game::g_Client.inv.currency < static_cast<int64_t>(entry->field228)) {
        Refuse(kStrMsg1414);
        return;
    }

    // (m) g_MorphInProgress != 1 && !g_GmCmdCooldownLatch (EA 0x5e5506): otherwise
    //     SILENT abandon (no message). Same guard pair, same idiom as
    //     Game/MapWarp.cpp:191-197 (EA 0x55CAF9) -> same representation (Var).
    if (game::g_Client.VarGet(kAddrMorphInProgress) == 1) return;
    if (game::g_Client.VarGet(kAddrCooldownLatch) != 0) return;

    // (n) (itemId == 2141 && dword_16851B8 != 2) || (itemId == 574 && dword_16851B8 == 40)
    //     => Msg 2055 (EA 0x5e5559 -> LABEL_68 0x5e555b/0x5e556b).
    if ((itemId == 2141u && var16851B8 != 2) || (itemId == 574u && var16851B8 == 40)) {
        Refuse(kStrMsg2055);
        return;
    }

    // (o) itemId == 2314 (EA 0x5e5589): dword_16851B8 != 40 => Msg 2055 (EA 0x5e5592);
    //     THEN dword_167616C >= 1 => Msg 2547 (EA 0x5e55c1: `cmp ds:dword_167616C,1 ;
    //     jl` -> `push 9F3h` -> Msg_AppendSystemLine). This 2nd test is indeed NESTED
    //     inside `itemId == 2314`, it is NOT a top-level guard.
    if (itemId == 2314u) {
        if (var16851B8 != 40) { Refuse(kStrMsg2055); return; }
        if (game::g_Client.VarGet(kAddrVar167616C) >= 1) { Refuse(kStrMsg2547); return; }
    }

    // (p) EMISSION (EA 0x5e562a) — the binary emits HERE, unconditionally once
    //     all the guards above are cleared:
    //         Net_SendVaultReq_215(npcId, itemId, qty, page, freeSlot, col, row)  // 0x590a10
    //     Args cross-checked against the disassembly of UI_NpcShop_OnRDown_Buy (0x5e562a):
    //         npcId    = *(_DWORD *)*(this+2)                   (1st dword of the NPC entry)
    //         itemId   = *(*(this+2) + 112*cat + 1740 + 4*slot) (vendor slot)
    //         qty      = v38 (99 or 0)
    //         page     = v36 ; freeSlot = FreePageSlot ; col = v33 % 8 ; row = v33 / 8
    //     where (v36 page, v33 slot2) come from Inventory_FindFreeGridSlot 0x54DDE0 (guard f:
    //     Msg 117 if bag full) and FreePageSlot from Inventory_FindFreePageSlot 0x54E1D0
    //     (guard h: -1 => silent abandon).
    //
    // BUILDER (cross-checked against IDA this wave): net::Net_SendVaultReq_215 now EXISTS and
    // is BYTE-EXACT (SendPackets.cpp:1423, int32_t params; frame opcode 0x13 | sub-code
    // 215 (u32 @+9) | 100-byte block = 7 int32 [npcId,itemId,qty,page,freeSlot,col,row] + 72
    // zero bytes | total 113) — verified against Net_SendVaultReq_215 0x590a10. The old note
    // "BROKEN wrapper / opcode 0xD7 / int8_t params" is OUTDATED (the builder was fixed by
    // the dedicated wave). The TRANSPORT is therefore NO LONGER the blocker.
    //
    // REAL BLOCKER = STATE (outside this file) — we do NOT emit (rule #8: never
    // guess):
    //   1. page/freeSlot/col/row (4 of the 7 fields) require Inventory_FindFreeGridSlot
    //      0x54DDE0 + Inventory_FindFreePageSlot 0x54E1D0 — BAG allocation (Game/), with no
    //      C++ equivalent (only WarehouseState::FindFreeSlot 0x54e240 exists, a different
    //      subsystem). Fabricating these values would place the item in an arbitrary bag
    //      slot AND skip the "bag full" refusal (Msg 117): STRICTLY WORSE than nothing.
    //   2. npcId comes from the NPC entry (*(this+2)); this window reads the
    //      dword_182613C catalog — preexisting model divergence (cf. .h header banner).
    // Once Game/ exposes these two bag allocators, the call becomes direct and faithful:
    //   if (net::NetClient* nc = net::GlobalNetClient())                    // g_NetClient 0x8156A0
    //       net::Net_SendVaultReq_215(*nc, npcId, itemId, qty, page, freeSlot, col, row);
    // Cf. `missingBuilders`/missing-state section of the wave report.
    //
    // Local effects of the binary, deliberately NOT reproduced: they ALL happen
    // AFTER the emission (g_VaultOpPending=1 EA 0x5e562f, g_GmCmdCooldownLatch=1
    // EA 0x5e5639, flt_1675B0C=g_GameTimeSec EA 0x5e5649). Setting them without having
    // emitted would lock the client on a request that never went out.
    // No optimistic effect on inventory: the binary doesn't do one either, it
    // waits for the response (Pkt_TradeResult 0x26 / Net_OnItemBuyResult 0xa4, already
    // routed by Net/GameHandlers_VendorTrade.cpp).
    // LOCAL feedback to the player: window footer ONLY. On its emission path
    // (0x5e562a+), the binary pushes NO chat line — only the refusal branches
    // (Refuse) call Msg_AppendSystemLine. We therefore do NOT write to
    // game::g_Client.msg.System here (rule #4: no effect the binary doesn't have on
    // the success path). As long as emission stays blocked (missing bag state, cf. (p)),
    // we settle for a non-intrusive window indicator.
    char line[96];
    std::snprintf(line, sizeof(line), "Achat : objet #%u x%d (envoi bloque : etat de sac manquant).", itemId, qty);
    statusText_ = line;
}

// ============================================================================
// Rendering
// ============================================================================
void VendorShopWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    if (!bOpen_) return;

    // Coordinate audit 2026-07-14 ("mis-aligned windows" re-verification): the
    // header comment above and VendorShopWindow.h announce a recompute on EVERY
    // FRAME via RecomputeAnchor(ctx.screenW, ctx.screenH), but this call was
    // missing here -- only the constructor invoked it, once, with the literal
    // kRefWidth/kRefHeight constants. (x_, y_) therefore stayed frozen at the
    // "reference resolution" position for the object's entire lifetime instead
    // of tracking ctx.screenW/H as documented and as the original engine does
    // (UI_ProjectSpriteToScreen 0x50f5d0). Invisible at the default launch
    // resolution (1024x768 == kRefWidth/kRefHeight => scaleX=scaleY=1, hence
    // the same result), but desynced as soon as ctx.screenW/H differs from
    // the reference.
    RecomputeAnchor(ctx.screenW, ctx.screenH);

    const Rect panel    = PanelRect();
    const Rect closeBtn = CloseButtonRect();
    const Rect prevBtn  = PrevPageButtonRect();
    const Rect nextBtn  = NextPageButtonRect();
    const Rect qtyMinus = QtyMinusButtonRect();
    const Rect qtyPlus  = QtyPlusButtonRect();
    const Rect buyBtn   = BuyButtonRect();

    const int  entryCount = EntryCount();
    const int  pageCount  = PageCount();
    const int  sel        = Selection();
    const bool hasSel      = sel >= 0 && sel < entryCount;
    const bool canPrev     = curPage_ > 1;
    const bool canNext     = curPage_ < pageCount;

    if (ctx.phase == UiPhase::Panels) {
        // Panel background + frame.
        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColPanelBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColFrame, 1);

        // Title bar.
        ctx.FillRect(panel.x, panel.y, panel.w, kHeaderH, kColHeaderBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, kHeaderH, kColFrame, 1);

        // Close button (cross), red on hover.
        const bool closeHover = PointInRect(cursorX, cursorY, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h);
        ctx.FillRect(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, closeHover ? kColError : kColBtnBg);
        ctx.DrawFrame(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, kColFrame, 1);

        // Catalog rows (current page).
        for (int r = 0; r < kRowsPerPage; ++r) {
            const int idx = (curPage_ - 1) * kRowsPerPage + r;
            const Rect rr = RowRect(r);
            const bool valid    = idx < entryCount;
            const bool selected = valid && idx == sel;
            const bool hovered  = valid && PointInRect(cursorX, cursorY, rr.x, rr.y, rr.w, rr.h);

            D3DCOLOR bg = (r % 2 == 0) ? kColRowBg : kColRowBgAlt;
            if (selected)      bg = kColSelect;
            else if (hovered)  bg = kColSelect;

            ctx.FillRect(rr.x, rr.y, rr.w, rr.h, bg);
            ctx.DrawFrame(rr.x, rr.y, rr.w, rr.h, kColFrame, 1);

            if (!valid) continue;

            // Icon (square anchored to the left of the row, kRowH-4 px). Real texture if
            // resolved via ITEM_INFO::iconId; otherwise falls back to a colored rect +
            // frame (mission: "fall back to colored rect if the icon doesn't resolve").
            const int iconSize = kRowH - 4;
            const int iconX = rr.x + 3;
            const int iconY = rr.y + (rr.h - iconSize) / 2;
            gfx::GpuTexture* icon = GetIconTex(ctx, ItemIdAt(idx));
            if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 &&
                ctx.sprites && ctx.sprites->Ready()) {
                const float sx = static_cast<float>(iconSize) / static_cast<float>(icon->Width());
                const float sy = static_cast<float>(iconSize) / static_cast<float>(icon->Height());
                ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, iconX, iconY, sx, sy,
                                              gfx::kSpriteWhite, /*compensatePos=*/true);
            } else {
                ctx.FillRect(iconX, iconY, iconSize, iconSize, kColIconFallback);
                ctx.DrawFrame(iconX, iconY, iconSize, iconSize, kColFrame, 1);
            }
        }

        // Pagination (previous/next).
        ctx.FillRect(prevBtn.x, prevBtn.y, prevBtn.w, prevBtn.h, canPrev ? kColBtnBg : kColBtnBgOff);
        ctx.DrawFrame(prevBtn.x, prevBtn.y, prevBtn.w, prevBtn.h, kColFrame, 1);
        ctx.FillRect(nextBtn.x, nextBtn.y, nextBtn.w, nextBtn.h, canNext ? kColBtnBg : kColBtnBgOff);
        ctx.DrawFrame(nextBtn.x, nextBtn.y, nextBtn.w, nextBtn.h, kColFrame, 1);

        // Quantity (-/+), active only when an entry is selected.
        ctx.FillRect(qtyMinus.x, qtyMinus.y, qtyMinus.w, qtyMinus.h, hasSel ? kColBtnBg : kColBtnBgOff);
        ctx.DrawFrame(qtyMinus.x, qtyMinus.y, qtyMinus.w, qtyMinus.h, kColFrame, 1);
        ctx.FillRect(qtyPlus.x, qtyPlus.y, qtyPlus.w, qtyPlus.h, hasSel ? kColBtnBg : kColBtnBgOff);
        ctx.DrawFrame(qtyPlus.x, qtyPlus.y, qtyPlus.w, qtyPlus.h, kColFrame, 1);

        // Buy button, active only when an entry is selected.
        const bool buyHover = hasSel && PointInRect(cursorX, cursorY, buyBtn.x, buyBtn.y, buyBtn.w, buyBtn.h);
        ctx.FillRect(buyBtn.x, buyBtn.y, buyBtn.w, buyBtn.h,
                     !hasSel ? kColBtnBgOff : (buyHover ? kColSelect : kColBtnBg));
        ctx.DrawFrame(buyBtn.x, buyBtn.y, buyBtn.w, buyBtn.h, kColFrame, 1);
        return;
    }

    // --- Text phase ---
    ctx.Text("Marchand", panel.x + kGridPad, panel.y + (kHeaderH - 12) / 2, kColTitle);

    const int closeLblW = ctx.MeasureText("X");
    ctx.Text("X", closeBtn.x + (closeBtn.w - closeLblW) / 2, closeBtn.y + 2, kColText);

    for (int r = 0; r < kRowsPerPage; ++r) {
        const int idx = (curPage_ - 1) * kRowsPerPage + r;
        if (idx >= entryCount) continue;
        const Rect rr = RowRect(r);

        // Price: main component (payload+61) always shown; secondary
        // components (payload+65/+69) only if nonzero (exact semantics of
        // the 3 components not documented server-side).
        const uint32_t p0 = PriceAt(idx, 0);
        const uint32_t p1 = PriceAt(idx, 1);
        const uint32_t p2 = PriceAt(idx, 2);
        char priceLabel[64];
        if (p1 == 0 && p2 == 0)
            std::snprintf(priceLabel, sizeof(priceLabel), "%u", p0);
        else
            std::snprintf(priceLabel, sizeof(priceLabel), "%u / %u / %u", p0, p1, p2);
        const int priceW = ctx.MeasureText(priceLabel);
        const int priceX = rr.x + rr.w - priceW - 6;
        ctx.Text(priceLabel, priceX, rr.y + (rr.h - 12) / 2, kColTextDim);

        // Name (ITEM_INFO), truncated to avoid overlapping the price. Anchored to the
        // right of the icon drawn in the Panels phase (kRowH-4 px at rr.x+3).
        const int iconSize = kRowH - 4;
        const int nameX = rr.x + 3 + iconSize + 6;
        std::string name = ItemNameAt(idx);
        TruncateToWidth(ctx, name, priceX - nameX - 6);
        ctx.Text(name.c_str(), nameX, rr.y + (rr.h - 12) / 2, kColText);
    }

    // Pagination: arrow labels + "Page X / Y".
    ctx.Text("<", prevBtn.x + (prevBtn.w - ctx.MeasureText("<")) / 2, prevBtn.y + 3,
              canPrev ? kColText : kColTextDim);
    ctx.Text(">", nextBtn.x + (nextBtn.w - ctx.MeasureText(">")) / 2, nextBtn.y + 3,
              canNext ? kColText : kColTextDim);
    char pageLabel[32];
    std::snprintf(pageLabel, sizeof(pageLabel), "Page %d / %d", curPage_, pageCount);
    const int pageLabelW = ctx.MeasureText(pageLabel);
    ctx.Text(pageLabel, panel.x + (panel.w - pageLabelW) / 2, prevBtn.y + 3, kColText);

    // Quantity (-/+) + current value.
    ctx.Text("-", qtyMinus.x + (qtyMinus.w - ctx.MeasureText("-")) / 2, qtyMinus.y + 2,
              hasSel ? kColText : kColTextDim);
    ctx.Text("+", qtyPlus.x + (qtyPlus.w - ctx.MeasureText("+")) / 2, qtyPlus.y + 2,
              hasSel ? kColText : kColTextDim);
    char qtyLabel[16];
    std::snprintf(qtyLabel, sizeof(qtyLabel), "%d", qty_);
    const int qtyLabelW = ctx.MeasureText(qtyLabel);
    const int qtyLabelX = qtyMinus.x + qtyMinus.w + ((qtyPlus.x - (qtyMinus.x + qtyMinus.w)) - qtyLabelW) / 2;
    ctx.Text(qtyLabel, qtyLabelX, qtyMinus.y + 2, hasSel ? kColText : kColTextDim);

    // Buy button label.
    const char* buyLabel = hasSel ? "Acheter" : "(selectionner)";
    const int buyLabelW = ctx.MeasureText(buyLabel);
    ctx.Text(buyLabel, buyBtn.x + (buyBtn.w - buyLabelW) / 2,
              buyBtn.y + (buyBtn.h - 12) / 2, hasSel ? kColText : kColTextDim);

    // Current gold (game::g_Client.inv, cf. Game/ClientRuntime.h) + purchase status,
    // on the 3rd line of the window footer (below pagination and qty/Buy).
    const int footerTop = panel.y + panel.h - kFooterH;
    const int row3Y = footerTop + kFooterRow3Y;
    char goldLine[64];
    std::snprintf(goldLine, sizeof(goldLine), "Or: %lld",
                  static_cast<long long>(game::g_Client.inv.currency));
    const int goldW = ctx.MeasureText(goldLine);
    ctx.Text(goldLine, panel.x + panel.w - kGridPad - goldW, row3Y, kColTextDim);

    if (!statusText_.empty())
        ctx.Text(statusText_.c_str(), panel.x + kGridPad, row3Y, kColSuccess);
}

} // namespace ts2::ui

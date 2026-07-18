// UI/PlayerTradeWindow.cpp — implementation of the PLAYER-TO-PLAYER TRADE window.
// See UI/PlayerTradeWindow.h for the documented gaps (grid unproven, Open()/Close()
// wiring not done on the network handler side).
#include "UI/PlayerTradeWindow.h"
#include "UI/PanelSkin.h"
#include "Game/GameDatabase.h"    // game::GetItemInfo / ItemInfo::iconId (real icons)
#include "Gfx/Renderer.h"         // ctx.renderer->Device()
#include "Gfx/IconTextureCache.h" // .IMG icon cache (file-local, see trade model below)

#include <cstdio>
#include <cstring> // std::strcmp (self/partner grid selection by label)

namespace ts2::ui {

// Original addresses — DUPLICATED from Net/GameHandlers_VendorTrade.cpp (file
// off-limits for editing in this mission). Same values, same role: see that
// file's header comment for detailed semantics.
namespace {
// Real panel background (best effort): (446,440) template from the UI atlas
// folder G03_GDATA/D01_GIMAGE2D/001 — NOT CONFIRMED by IDA, chosen by ratio
// proximity to the Trade panel (344x310; see the methodology in
// UI/PanelSkin.h). Distinct index from those used by GuildWindow/
// CharacterStatsWindow (same size cluster, different files). Falls back to
// kColBg automatically if missing.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01341.IMG");

constexpr uint32_t kTradePartnerIdLo = 0x168741C; // g_TradePartnerIdLo (partner word 0)
constexpr uint32_t kTradePartnerVal1 = 0x1687420; // dword_1687420      (partner word 1)
// dword_1687424: 3rd PARTNER data word written by the server (0x8156C1+8),
// via Pkt_TradeRequestPrompt 0x48FD20 (@0x48FDA0 dword_1687424[0]=v7[2]). Compared
// against the action code by Pkt_TradeActionResult 0x48FEA0 (@0x48FEFE 'if(dword_1687424==v7)').
// NOT a local-agreement flag (W4-F3 interpretation refuted).
constexpr uint32_t kTradePartnerWord2 = 0x1687424; // dword_1687424 (partner word 2)
// FIX W4-F3 (data_refs on 0x1675B24), RE-CONFIRMED W6 by fresh decompilation:
// the old kTradeState=0x1675B24 was WRONG. This global is g_PendingOrderKind (world
// order / targeting type: AutoPlay_UpdateTargeting 0x45D080, Game_OnWorldLeftClick
// 0x536690, Player_InteractWithPlayer 0x5392E0, Player_CastSkill 0x53BC40) — NOTHING to
// do with trading. It's only zeroed by Pkt_TradeRequestPrompt 0x48FD20 (@0x48fd82) /
// Pkt_TradeActionResult 0x48FEA0 (@0x48ff66) as a side effect (targeting reset when a
// trade prompt arrives). Removed -> no longer displayed as "trade state".
//
// kTradeExtra=0x1675D84: also removed from the display, but NOT for the reason noted
// in W4-F3 ("never proven") — inaccurate wording, corrected here. Its WRITE side IS
// proven: Pkt_TradeRequestPrompt loads it from 0x8156D1 (`dword_1675D84 = v5` @0x48fdac)
// and Pkt_TradeActionResult forces it to 1 (@0x48ff8e). It's its SEMANTICS that remain
// undetermined -> not displayed rather than inventing a role for it ("never guess" rule).

// Trade model — OWNED by this .cpp (PlayerTradeWindow.h cannot carry a
// cache/state member without a layout divergence). Objects offered by self /
// partner + half-lock flags.
//
// WARNING: THIS MODEL IS A REINVENTION, AND WILL STAY EMPTY FOREVER. This is not
// pending wiring: the binary has NO trade table at all, so no handler will ever
// populate these arrays (exhaustive evidence at the top of UI/PlayerTradeWindow.h).
// The old "TODO [handler 0x48FD20/0x48FEA0] to capture dynamically via x32dbg" from
// W4-F3 implied there was an original grid to recover: REMOVED, it was moot. The 3
// trade handlers don't populate any grid BECAUSE NO GRID EXISTS. The rendering below
// is therefore structurally an empty shell, kept only because the window is
// instantiated by UI/GameWindows.h:190 (header not owned by this front). Removing it
// is an architectural decision.
struct TradeCell { uint32_t itemId = 0, count = 0, color = 0, durability = 0; };
constexpr int kTradeCols  = 5;                     // == PlayerTradeWindow::kGridCols
constexpr int kTradeRows  = 5;                     // == PlayerTradeWindow::kGridRows
constexpr int kTradeCells = kTradeCols * kTradeRows; // 25
struct TradeModel {
    TradeCell self[kTradeCells];
    TradeCell partner[kTradeCells];
    bool selfLocked = false, partnerLocked = false;
};
TradeModel g_tradeModel;

// File-local .IMG icon cache (PlayerTradeWindow.h has no cache member, cf.
// InventoryWindow::sharedIconCache_; can't inject it without editing the .h).
// Keyed by file path, same lazy-load/cached-failure policy as other windows
// (Gfx/IconTextureCache.h).
gfx::IconTextureCache g_tradeIconCache;

// Real icon for an offered item: path derived from ITEM_INFO::iconId (+192, 1-based),
// template "002\002_%05u.IMG" — SAME confirmed pattern as ConsumableBar/Inventory/
// VendorShop (Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md). nullptr if device unavailable, item
// not in ITEM_INFO, or iconId is zero.
gfx::GpuTexture* ResolveTradeIcon(const UiContext& ctx, uint32_t itemId) {
    if (itemId == 0 || !ctx.renderer || !ctx.renderer->Device()) return nullptr;
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return nullptr;
    char path[64];
    std::snprintf(path, sizeof(path), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return g_tradeIconCache.GetOrLoad(ctx.renderer->Device(), path);
}
} // namespace

// Lifecycle
void PlayerTradeWindow::Open() {
    Dialog::Open(); // bOpen_ = true
    closePressed_ = acceptPressed_ = cancelPressed_ = false;
}

void PlayerTradeWindow::Close() {
    Dialog::Close(); // bOpen_ = false
    closePressed_ = acceptPressed_ = cancelPressed_ = false;
}

// Geometry
void PlayerTradeWindow::Layout(int screenW, int screenH, Rect& box, Rect& closeBtn,
                                Rect& acceptBtn, Rect& cancelBtn,
                                Rect& selfGrid, Rect& partnerGrid) const {
    box.x = screenW / 2 - kPanelW / 2;
    box.y = screenH / 2 - kPanelH / 2;
    box.w = kPanelW;
    box.h = kPanelH;

    // Close button (X), top-right corner of the title bar.
    closeBtn = { box.x + box.w - kCloseSize - 6, box.y + (kHeaderH - kCloseSize) / 2,
                 kCloseSize, kCloseSize };

    // Two grids side by side, below the title bar + label.
    const int gridsY = box.y + kHeaderH + kPanelPad + kGridLabelH;
    selfGrid    = { box.x + kPanelPad,                          gridsY, kGridW, kGridH };
    partnerGrid = { box.x + kPanelPad + kGridW + kGridGap,       gridsY, kGridW, kGridH };

    // Button row at the bottom of the window.
    const int btnY = box.y + box.h - kPanelPad - kBtnH;
    acceptBtn = { box.x + box.w / 2 - kBtnW - 8, btnY, kBtnW, kBtnH };
    cancelBtn = { box.x + box.w / 2 + 8,         btnY, kBtnW, kBtnH };
}

// Rendering
void PlayerTradeWindow::DrawGridPlaceholder(const UiContext& ctx, const Rect& grid,
                                             const char* label) const {
    // Selects the trade model half (self vs partner) via the label supplied
    // by the two only call sites in this file ("Vous" / "Partenaire").
    const bool isSelf = (label && std::strcmp(label, "Vous") == 0);
    const TradeCell* cells = isSelf ? g_tradeModel.self : g_tradeModel.partner;
    const bool locked = isSelf ? g_tradeModel.selfLocked : g_tradeModel.partnerLocked;

    // --- Panels phase: outer frame + actual rendering of the kGridRows x kGridCols
    //     cells (background, frame, .IMG icon for the offered item if present) ---
    if (ctx.phase == UiPhase::Panels) {
        // A locked frame (validated half) is tinted to signal it.
        ctx.DrawFrame(grid.x - 2, grid.y - 2, grid.w + 4, grid.h + 4,
                      locked ? kColHover : kColFrame, locked ? 2 : 1);
        for (int r = 0; r < kGridRows; ++r) {
            for (int c = 0; c < kGridCols; ++c) {
                const int cx = grid.x + c * (kCellSize + kCellGap);
                const int cy = grid.y + r * (kCellSize + kCellGap);
                const TradeCell& cell = cells[r * kGridCols + c];

                ctx.FillRect(cx, cy, kCellSize, kCellSize, kColCellBg);

                if (cell.itemId != 0) {
                    gfx::GpuTexture* icon = ResolveTradeIcon(ctx, cell.itemId);
                    if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 &&
                        ctx.sprites && ctx.sprites->Ready()) {
                        const float sx = static_cast<float>(kCellSize) / static_cast<float>(icon->Width());
                        const float sy = static_cast<float>(kCellSize) / static_cast<float>(icon->Height());
                        ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, cx, cy, sx, sy,
                                                      gfx::kSpriteWhite, /*compensatePos=*/true);
                    } else {
                        // Fallback WITHOUT icon = accented filled cell (same policy as
                        // VendorShopWindow when icon resolution fails).
                        ctx.FillRect(cx, cy, kCellSize, kCellSize, kColBtnBg);
                    }
                }

                ctx.DrawFrame(cx, cy, kCellSize, kCellSize, kColFrame, 1);
            }
        }
        return;
    }

    // --- Text phase: label above + quantity badge per occupied cell ---
    const int labelW = ctx.MeasureText(label);
    ctx.Text(label, grid.x + (grid.w - labelW) / 2, grid.y - kGridLabelH, kColTitle);

    for (int r = 0; r < kGridRows; ++r) {
        for (int c = 0; c < kGridCols; ++c) {
            const TradeCell& cell = cells[r * kGridCols + c];
            if (cell.itemId == 0 || cell.count <= 1) continue;
            const int cx = grid.x + c * (kCellSize + kCellGap);
            const int cy = grid.y + r * (kCellSize + kCellGap);
            char qty[12];
            std::snprintf(qty, sizeof(qty), "%u", cell.count);
            const int qw = ctx.MeasureText(qty);
            ctx.Text(qty, cx + kCellSize - qw - 1, cy + kCellSize - 12, kColText);
        }
    }

    if (locked) {
        const char* lk = "verrouillé";
        const int lw = ctx.MeasureText(lk);
        ctx.Text(lk, grid.x + (grid.w - lw) / 2, grid.y + grid.h + 4, kColHover);
    }
}

void PlayerTradeWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Cache the current screen dims (hit-test routed across frames),
    // like MsgBoxDialog::Render.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Rect box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid;
    Layout(ctx.screenW, ctx.screenH, box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid);

    // LIVE read of the first 2 PARTNER IDENTITY words (never cached: these
    // globals change between frames via network handlers). This is NOT a
    // "trade state": no such state exists in the binary (see the header of
    // UI/PlayerTradeWindow.h). Written by Pkt_TradeRequestPrompt 0x48FD20
    // (@0x48fd8f/0x48fd97) and zeroed by Pkt_TradeActionResult 0x48FEA0
    // (@0x48ff70/0x48ff7a).
    const int32_t partnerIdLo = game::g_Client.VarGet(kTradePartnerIdLo);
    const int32_t partnerVal1 = game::g_Client.VarGet(kTradePartnerVal1);
    const bool    hasPartner  = (partnerIdLo != 0) || (partnerVal1 != 0);

    if (ctx.phase == UiPhase::Panels) {
        // Background + frame + title bar.
        kPanelBg.Draw(ctx, box.x, box.y, box.w, box.h, kColBg);
        ctx.FillRect(box.x, box.y, box.w, kHeaderH, kColHeaderBg);
        ctx.DrawFrame(box.x, box.y, box.w, box.h, kColFrame, 2);
        ctx.DrawFrame(box.x, box.y, box.w, kHeaderH, kColFrame, 1);

        // Close button (X).
        const bool closeHover = PointInRect(cursorX, cursorY, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h);
        ctx.FillRect(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h,
                     closePressed_ ? kColBtnDown : (closeHover ? kColDanger : kColCloseBg));
        ctx.DrawFrame(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, kColFrame, 1);

        // Two placeholder grids.
        DrawGridPlaceholder(ctx, selfGrid, "Vous");
        DrawGridPlaceholder(ctx, partnerGrid, "Partenaire");

        // Accept / Cancel buttons.
        const bool acceptHover = PointInRect(cursorX, cursorY, acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h);
        ctx.FillRect(acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h,
                     acceptPressed_ ? kColBtnDown : (acceptHover ? kColHover : kColBtnBg));
        ctx.DrawFrame(acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h, kColFrame, 1);

        const bool cancelHover = PointInRect(cursorX, cursorY, cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h);
        ctx.FillRect(cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h,
                     cancelPressed_ ? kColBtnDown : (cancelHover ? kColDanger : kColBtnBg));
        ctx.DrawFrame(cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h, kColFrame, 1);
        return;
    }

    // --- Text phase ---
    // Title centered in the header.
    const char* title = "Échange";
    const int titleW = ctx.MeasureText(title);
    ctx.Text(title, box.x + (box.w - titleW) / 2, box.y + (kHeaderH - 12) / 2, kColTitle);
    ctx.Text("X", closeBtn.x + 5, closeBtn.y + 2, kColText);

    // Partner/agreement diagnostic line (above the button row).
    char line1[96];
    if (hasPartner) {
        std::snprintf(line1, sizeof(line1), "Partenaire : #%d / #%d", partnerIdLo, partnerVal1);
    } else {
        std::snprintf(line1, sizeof(line1), "En attente d'une invitation d'échange...");
    }
    const int infoY = box.y + box.h - kPanelPad - kBtnH - kInfoH;
    ctx.Text(line1, box.x + kPanelPad, infoY, kColText);
    // "Local agreement: yes/no" label REMOVED: 0x1687424 (kTradePartnerWord2) is not
    // a local-agreement flag but the 3rd partner data word supplied by the server,
    // compared against the action code by Pkt_TradeActionResult 0x48FEA0 (@0x48FEFE).
    // (W4-F3 refuted.)

    // Button labels.
    const char* acceptLbl = "Accepter";
    const int acceptLblW = ctx.MeasureText(acceptLbl);
    ctx.Text(acceptLbl, acceptBtn.x + (acceptBtn.w - acceptLblW) / 2, acceptBtn.y + 6, kColText);

    const char* cancelLbl = "Annuler";
    const int cancelLblW = ctx.MeasureText(cancelLbl);
    ctx.Text(cancelLbl, cancelBtn.x + (cancelBtn.w - cancelLblW) / 2, cancelBtn.y + 6, kColText);
}

// Button actions
//
// NO EMISSION HERE — AND THAT IS THE FAITHFUL ANSWER, NOT A GAP.
//
// The old "TODO(send): lock/accept builder not found" is REMOVED: it asked a moot
// question. There is NO "lock / accept / cancel a trade" packet in TwelveSky2.exe at
// all — not because it wasn't found, but because the binary has no trade table at all
// (exhaustive evidence at the top of UI/PlayerTradeWindow.h: UI_InitAllDialogs 0x5ABF50
// + UI_StorageWin_Open 0x5D27A0 + the 3 purely textual handlers 0x48FD20/0x48FE10/
// 0x48FEA0).
// The ONLY outbound packet in the "trade" domain is Net_SendOp43 (op 0x2B, [name13@9]
// [flag i32@22], len 26 — Net/SendPackets.h:75), and its 2 only xrefs are the two
// buttons of UI_ClanWin_OnLUp 0x5D92A0 page 2 (@0x5D9F8A flag=2, @0x5DA0F1 flag=1):
// it's the PLAYER CONTEXT MENU that emits it, not this window. This front doesn't own
// that file -> deferred to the wave (see report W6, section "to wire elsewhere").
// Net_SendOp49/50/51 (outbound 0x31/0x32/0x33) remain DISCARDED: numeric coincidence
// only — Net_SendOp49 is the confirmed "ALLIANCE invitation reply" flow
// (UI_MsgBox_OnLButtonUp @0x5c109d). Do not rewire them here.
void PlayerTradeWindow::HandleAccept() {
    Close();
}

void PlayerTradeWindow::HandleCancel() {
    Close();
}

// Mouse / keyboard events
bool PlayerTradeWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    Rect box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid;
    Layout(lastScreenW_, lastScreenH_, box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid);

    if (PointInRect(x, y, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h)) {
        closePressed_ = true;
    } else if (PointInRect(x, y, acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h)) {
        acceptPressed_ = true;
    } else if (PointInRect(x, y, cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h)) {
        cancelPressed_ = true;
    }
    return true; // modal: consumes every click while open (like MsgBoxDialog)
}

bool PlayerTradeWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    Rect box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid;
    Layout(lastScreenW_, lastScreenH_, box, closeBtn, acceptBtn, cancelBtn, selfGrid, partnerGrid);

    if (closePressed_ && PointInRect(x, y, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h)) {
        HandleCancel(); // closing via X = cancel the trade (standard UX behavior)
    } else if (acceptPressed_ && PointInRect(x, y, acceptBtn.x, acceptBtn.y, acceptBtn.w, acceptBtn.h)) {
        HandleAccept();
    } else if (cancelPressed_ && PointInRect(x, y, cancelBtn.x, cancelBtn.y, cancelBtn.w, cancelBtn.h)) {
        HandleCancel();
    }
    closePressed_ = acceptPressed_ = cancelPressed_ = false;
    return true; // modal
}

bool PlayerTradeWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) { HandleCancel(); return true; }
    if (vk == VK_RETURN) { HandleAccept(); return true; }
    return true; // modal: swallows every key while open
}

} // namespace ts2::ui

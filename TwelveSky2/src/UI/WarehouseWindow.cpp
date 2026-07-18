// UI/WarehouseWindow.cpp — warehouse window implementation.
// See UI/WarehouseWindow.h for the interaction contract and network wiring.
//
// Include order: Net/ FIRST (NetClient.h pulls <winsock2.h> before <windows.h>,
// which UI/WarehouseWindow.h transitively pulls via <d3d9.h>) — same convention
// as UI/ChatWindow.cpp.
#include "Net/SendPackets.h"   // -> Net/NetClient.h: winsock2 then windows (order matters)
#include "Net/NetClient.h"
#include "UI/WarehouseWindow.h"
#include "Asset/ImgFile.h"
#include "Game/GameDatabase.h"

#include <cstdio>

namespace ts2::ui {

namespace {
// Item icon resolver — IDENTICAL to ResolveItemIconPath from UI/InventoryWindow.cpp
// (the mission's reference pattern, duplicated here for lack of a common header,
// without touching the existing architecture). CONFIRMED by disassembly
// (Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md): the file index is NOT itemId (old
// hypothesis, FALSE) but the SEPARATE field ITEM_INFO+192 ("IconID",
// game::ItemInfo::iconId, 1-based), read via game::GetItemInfo().
std::string ResolveItemIconPath(uint32_t itemId) {
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return std::string(buf);
}
} // namespace

WarehouseWindow::WarehouseWindow() {
    // DEFAULT centering (reference resolution 1024x768), only valid before the
    // very first Render(). FIX (re-verification of the "misaligned windows"
    // mission, 2026-07-14): x_/y_ used to be computed ONCE here, from the
    // design constants kRefWidth/kRefHeight, and NEVER refreshed afterward —
    // unlike EnchantWindow::ComputeLayout (recomputed from ctx.screenW/screenH
    // on EVERY Render()) and MsgBoxDialog::Layout (same), and unlike the
    // contract documented by Dialog::x_/y_ itself ("screen position, recentered
    // every frame", cf. UI/UIManager.h). Bug result: at any REAL display
    // resolution other than 1024x768, the panel stayed frozen at the position
    // computed for the design resolution instead of being recentered on the
    // real screen — exactly the reported "coordinates broken at some
    // resolution" symptom. The real recentering now happens in
    // RecomputeCenter(), called from Render(ctx,...) with ctx.screenW/
    // ctx.screenH (current real values, cf. UI/UIManager.h::UiContext).
    RecomputeCenter(ts2::kRefWidth, ts2::kRefHeight);
}

// cf. constructor comment above: panel dimensions (kPanelW/kPanelH) are FIXED
// (measured on the real 5x5 grid, cf. Game/WarehouseSystem.h), only the
// ORIGIN (x_,y_) is recentered on the current real display resolution.
void WarehouseWindow::RecomputeCenter(int screenW, int screenH) {
    x_ = (screenW  - kPanelW) / 2;
    y_ = (screenH - kPanelH) / 2;
}

// ============================================================================
// Lifecycle
// ============================================================================
void WarehouseWindow::Open() {
    Dialog::Open();
    statusText_.clear();
}

// Close — UI_StorageWin_OnLUp case 2, lock +12 (EA 0x5d579e/0x5d57ce): the
// "close" button (sprite unk_8F3798 @ (x+8, y+6)) calls UI_StorageWin_CommitGrid(this)
// (EA 0x5d57e4) and NOTHING else — unlike cases 1/3/5/7, there is no
// cGameHud_Hide here.
// UI_StorageWin_CommitGrid 0x5d2f70: `if (a1[2])` (EA 0x5d2f7e) then `switch (a1[10])`
// (= mode); case 2 = WAREHOUSE (EA 0x5d338c) dumps the 5x5 grid into the
// inventory arrays THEN emits Net_SendPacket_Op32(&g_AutoPlayMgr, 1) — UNCONDITIONAL
// (EA 0x5d373f). bOpen_ stands in as the analog of a1[2] (cf. .h header banner).
void WarehouseWindow::Close() {
    if (bOpen_) SendStorageCommit(); // Op32(1) — EA 0x5d373f (via CommitGrid case 2)

    // TODO [anchor 0x5d338c..0x5d3727]: case 2 of UI_StorageWin_CommitGrid ALSO dumps
    // the entire 5x5 grid to g_InvMain/g_InvGrid_*/g_InvAux before emitting.
    // NOT reproduced here ON PURPOSE: the C++ model ALREADY commits per action
    // (WarehouseState::CommitCellToInventory on "Withdraw"), whereas the binary uses
    // the grid as a staging buffer dumped all at once. Calling
    // CommitAllToInventory() here as well would move to the bag any item LEFT in
    // the warehouse (the dump only filters on `itemId >= 1`, EA 0x5d33de, not on
    // any particular destination) — unproven local effect, hence discarded.
    // Preexisting structural divergence of Game/WarehouseSystem.h (not owned), cf. report.

    // Does not leave a "dangling" selection between two openings.
    game::g_Warehouse.CancelPendingMove();
    statusText_.clear();
    Dialog::Close();
}

// ============================================================================
// Geometry
// ============================================================================
WarehouseWindow::Rect WarehouseWindow::PanelRect() const {
    return { x_, y_, kPanelW, kPanelH };
}

WarehouseWindow::Rect WarehouseWindow::CloseButtonRect() const {
    return { x_ + kPanelW - kGridPad - kCloseSize, y_ + (kHeaderH - kCloseSize) / 2,
             kCloseSize, kCloseSize };
}

WarehouseWindow::Rect WarehouseWindow::WithdrawButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kGridPad, footerTop + 28, kBtnW, kBtnH };
}

// "Validate" button = lock +24 of the binary (UI_StorageWin_OnLUp case 2, sprite
// unk_901064 tested at (x+167, y+411), EA 0x5d592d).
WarehouseWindow::Rect WarehouseWindow::ValidateButtonRect() const {
    const int footerTop = y_ + kPanelH - kFooterH;
    return { x_ + kPanelW - kGridPad - kBtnW, footerTop + 28, kBtnW, kBtnH };
}

WarehouseWindow::Rect WarehouseWindow::CellRect(int row, int col) const {
    return { x_ + kGridPad + col * (kCellSize + kCellGap),
             y_ + kHeaderH + kGridPad + row * (kCellSize + kCellGap),
             kCellSize, kCellSize };
}

bool WarehouseWindow::CellAt(int mx, int my, int& outRow, int& outCol) const {
    for (int row = 0; row < game::WarehouseGrid::kRows; ++row) {
        for (int col = 0; col < game::WarehouseGrid::kCols; ++col) {
            const Rect r = CellRect(row, col);
            if (PointInRect(mx, my, r.x, r.y, r.w, r.h)) {
                outRow = row;
                outCol = col;
                return true;
            }
        }
    }
    return false;
}

bool WarehouseWindow::PointInPanel(int mx, int my) const {
    return PointInRect(mx, my, x_, y_, kPanelW, kPanelH);
}

// ============================================================================
// Mouse / keyboard events
// ============================================================================
bool WarehouseWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    // Simply consumes the press-down click if it falls within the panel, to
    // prevent it from falling through to the 3D world ("first consumer wins"
    // rule). All the action logic happens on release (OnClick), as documented
    // in the Dialog contract ("release click = confirmed").
    return PointInPanel(x, y);
}

bool WarehouseWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    if (!PointInPanel(x, y)) return false;

    const Rect closeBtn = CloseButtonRect();
    if (PointInRect(x, y, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h)) {
        Close();
        return true;
    }

    int row = -1, col = -1;
    if (CellAt(x, y, row, col)) {
        HandleCellClick(row, col);
        return true;
    }

    if (game::g_Warehouse.pendingMove.active) {
        const Rect wbtn = WithdrawButtonRect();
        if (PointInRect(x, y, wbtn.x, wbtn.y, wbtn.w, wbtn.h)) {
            HandleWithdrawClick();
            return true;
        }
    }

    // "Validate" (lock +24, EA 0x5d58f8): always active — the binary does not
    // gate this button on any state (neither selection, morph, nor lock).
    const Rect vbtn = ValidateButtonRect();
    if (PointInRect(x, y, vbtn.x, vbtn.y, vbtn.w, vbtn.h)) {
        HandleValidateClick();
        return true;
    }

    // Click inside the panel but outside any active zone (background, header...):
    // consumed anyway, the window is de facto modal while open.
    return true;
}

bool WarehouseWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    // Fidelity to UI_StorageWin_OnKey 0x5d6330: with the warehouse window ACTIVE
    // (*(this+2)), the handler CONSUMES the key (return 1) without ever closing or
    // emitting — 0x5d6330 contains NO call to Net_Send nor UI_StorageWin_CommitGrid.
    // Commit/close only comes from the mouse: the X button (lock +12 -> CommitGrid
    // case 2 -> Op32(1), EA 0x5d373f) and the Validate button (lock +24 -> Op32(1),
    // EA 0x5d5947). The old code did ESC -> Close() -> SendStorageCommit() ->
    // Op32(1), thus emitting a packet the binary does NOT send on keyboard: the 19
    // xrefs of UI_StorageWin_CommitGrid 0x5d2f70 are ALL mouse-driven (X,
    // cGameHud_OnMouseUp) or network handlers, NO keyboard caller. We therefore
    // consume ESC WITHOUT closing (return true, like 0x5d6330) -> no more spurious
    // emission. Closing (and its faithful Op32(1)) remains possible via the X button.
    if (vk == VK_ESCAPE) return true;
    return false;
}

// ============================================================================
// Actions
// ============================================================================
void WarehouseWindow::HandleCellClick(int row, int col) {
    game::WarehouseState& wh = game::g_Warehouse;
    game::WarehousePendingMove& pm = wh.pendingMove;

    if (!pm.active) {
        // Nothing to do on an empty cell without an active selection.
        if (wh.grid.cells[row][col].Empty()) return;
        wh.SelectPendingMove(row, col);
        statusText_.clear();
        return;
    }

    if (pm.srcRow == row && pm.srcCol == col) {
        // Re-click on the already selected cell: deselect.
        wh.CancelPendingMove();
        statusText_.clear();
        return;
    }

    // Re-click on ANOTHER cell -> PURELY LOCAL swap ("sort").
    // NO EMISSION — and this is FAITHFUL: the binary emits nothing for grid
    // manipulation (UI_StorageWin_OnLDown 0x5d4240 and UI_StorageWin_OnKey
    // 0x5d6330 contain no `call Net_Send*`; exhaustive scan of range
    // 0x5d2770..0x5d8900: only Open, CommitGrid, and OnLUp emit). The grid is a
    // client-side STAGING BUFFER; it only reaches the server on "Validate"/
    // "Close", as an Op32(1). The old SendGridCommit(kind=5) emitted an Op31
    // selector 5 that DOES NOT EXIST ANYWHERE in the binary (cf. .h header banner).
    if (wh.SwapCells(pm.srcRow, pm.srcCol, row, col)) {
        statusText_ = "Objets echanges.";
    } else {
        statusText_ = "Echange impossible.";
    }
    pm.Clear();
}

// "Validate" button: UI_StorageWin_OnLUp case 2, lock +24 (EA 0x5d58f8/0x5d592d).
// Emits Net_SendPacket_Op32(&g_AutoPlayMgr, 1) at EA 0x5d5947 — UNCONDITIONAL:
// no morph/lock guard (unlike case 1/5 which guard their Op31 with
// `g_MorphInProgress == 1 || g_GmCmdCooldownLatch`), and NO lock set afterward
// (neither g_GmCmdCooldownLatch nor flt_1675B0C) — add nothing here.
void WarehouseWindow::HandleValidateClick() {
    SendStorageCommit();
    statusText_ = "Entrepot valide.";
}

void WarehouseWindow::HandleWithdrawClick() {
    game::WarehouseState& wh = game::g_Warehouse;
    game::WarehousePendingMove& pm = wh.pendingMove;
    if (!pm.active) return;

    const int row = pm.srcRow, col = pm.srcCol;

    // Withdraw -> LOCAL commit via WarehouseState::CommitCellToInventory (writes
    // into game::g_Client.inv, mirroring the dump done by UI_StorageWin_CommitGrid
    // EA 0x5d3438..0x5d3727). NO EMISSION here — faithful: in the binary, withdrawal
    // is done by drag-and-drop, which produces no packet (staging only leaves on
    // "Validate"/"Close"). The old SendGridCommit(kind=4) emitted an Op31
    // selector 4 that DOES NOT EXIST in the binary.
    if (wh.CommitCellToInventory(row, col)) {
        statusText_ = "Objet retire vers le sac.";
    } else {
        statusText_ = "Retrait impossible (cellule vide).";
    }
    pm.Clear();
}

// Net_SendPacket_Op32 (Net/SendPackets.cpp:1370, byte-exact vs Net_SendPacket_Op32
// 0x4b64e0): opcode 0x20, one char field emitted as 4 bytes LE, total 13 bytes.
// The selector is the literal 1 at BOTH warehouse sites (EA 0x5d5947 and
// EA 0x5d373f); selector 2 also exists (EA 0x5d6127) but belongs to
// case 6 = player shop, NOT the warehouse.
//
// Target = net::GlobalNetClient(): the binary addresses g_NetClient 0x8156A0 as a
// GLOBAL (Net_SendPacket_Op32 reads it directly, it receives no socket).
// This pointer is set by ConnectGameServer (Net/Login.cpp:311) — it is
// therefore genuinely non-null in session; this is NOT the previous dead
// `if (!net_)` (net_ was never bound). Same idiom as Game/MapWarp.cpp:86. Outside
// a session (self-test Tools/UiWindowSelfTest.cpp), silent no-op.
void WarehouseWindow::SendStorageCommit() {
    if (net::NetClient* nc = net::GlobalNetClient())
        net::Net_SendPacket_Op32(*nc, 1);
}

std::string WarehouseWindow::CellLabel(const game::WarehouseItemCell& cell) {
    const game::ItemInfo* info = game::GetItemInfo(cell.itemId);
    if (!info || info->name[0] == '\0') return {};
    std::string s(info->name, strnlen(info->name, sizeof(info->name)));
    if (cell.count > 1) s += " x" + std::to_string(cell.count);
    return s;
}

// ============================================================================
// Icons (same lazy-load+cache pattern as InventoryWindow::GetIconTex)
// ============================================================================
gfx::GpuTexture* WarehouseWindow::GetIconTex(IDirect3DDevice9* dev, uint32_t itemId) {
    // Cache SHARED by file path (cf. SetIconCache/ActiveIconCache): an icon already
    // loaded by InventoryWindow/EnchantWindow/VendorShopWindow is reused without
    // re-decoding/re-uploading to VRAM (same .IMG file, same ITEM_INFO::iconId).
    const std::string path = ResolveItemIconPath(itemId);
    return ActiveIconCache().GetOrLoad(dev, path);
}

// ============================================================================
// Rendering
// ============================================================================
void WarehouseWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Recentering on the current REAL display resolution — cf. constructor comment:
    // MUST run before any use of x_/y_ below (Panels phase AND Text phase),
    // otherwise the Text phase would redraw on the OLD frame's rects in case of
    // a resize in between. Calling it with the window closed would be useless
    // (bOpen_ guards early) but harmless; we return early if closed.
    RecomputeCenter(ctx.screenW, ctx.screenH);
    if (!bOpen_) return;

    const game::WarehouseState& wh = game::g_Warehouse;
    const Rect panel      = PanelRect();
    const Rect closeBtn   = CloseButtonRect();
    const Rect withdrawBt = WithdrawButtonRect();
    const Rect validateBt = ValidateButtonRect();
    const bool hasSel     = wh.pendingMove.active;

    if (ctx.phase == UiPhase::Panels) {
        // Panel background + frame.
        ctx.FillRect(panel.x, panel.y, panel.w, panel.h, kColPanelBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColFrame, 1);

        // Title bar.
        ctx.FillRect(panel.x, panel.y, panel.w, kHeaderH, kColHeaderBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, kHeaderH, kColFrame, 1);

        // Close button (cross), red on hover.
        const bool closeHover = PointInRect(cursorX, cursorY, closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h);
        ctx.FillRect(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, closeHover ? kColError : kColBtnBg);
        ctx.DrawFrame(closeBtn.x, closeBtn.y, closeBtn.w, closeBtn.h, kColFrame, 1);

        // 5x5 grid. Real icon (.IMG, resolved by itemId) if available, otherwise
        // falls back to the original filled colored cell. Hover on a potential
        // target -> blue frame/background (kColSelect). The SOURCE cell of an
        // ongoing drag gets NO special treatment here: it is rendered EXACTLY like
        // an empty cell (cf. `selected` below) — CONFIRMED by disassembly
        // (Item_BeginDragTransaction 0x5AFDF0 + Inv_RemoveItemQuantity 0x5B0340
        // case 18/19/27/28: the picked-up item is removed from the source grid as
        // soon as it's clicked; UI_StorageWin_Draw 0x5D6610 only draws the icon IF
        // `field >= 1`, so NOTHING is drawn for that cell while the drag is active —
        // no grayed-out tint, cf. UI/WarehouseWindow.h header banner). The picked-up
        // item is drawn separately, stuck to the cursor, further below.
        IDirect3DDevice9* dev = ctx.renderer ? ctx.renderer->Device() : nullptr;
        for (int row = 0; row < game::WarehouseGrid::kRows; ++row) {
            for (int col = 0; col < game::WarehouseGrid::kCols; ++col) {
                const Rect r = CellRect(row, col);
                const game::WarehouseItemCell& cell = wh.grid.cells[row][col];
                const bool selected = hasSel && wh.pendingMove.srcRow == row && wh.pendingMove.srcCol == col;
                // `selected` counts as empty for RENDERING (the item is still in memory
                // inside pendingMove.snapshot for cancellation/swap, but the screen must
                // show it as withdrawn, as in the binary).
                const bool empty    = cell.Empty() || selected;
                const bool hovered  = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
                const bool highlight = hovered && (!empty || hasSel);

                gfx::GpuTexture* icon = empty ? nullptr : GetIconTex(dev, cell.itemId);
                if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 && ctx.sprites) {
                    ctx.FillRect(r.x, r.y, r.w, r.h, kColCellBg); // neutral background under the icon
                    const float sx = static_cast<float>(r.w) / static_cast<float>(icon->Width());
                    const float sy = static_cast<float>(r.h) / static_cast<float>(icon->Height());
                    ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, r.x, r.y, sx, sy,
                                                   gfx::kSpriteWhite, /*compensatePos=*/true);
                } else {
                    D3DCOLOR bg = empty ? kColEmptyCell : kColCellBg;
                    if (highlight) bg = kColSelect;
                    ctx.FillRect(r.x, r.y, r.w, r.h, bg);
                }

                const D3DCOLOR frameCol = (icon && highlight) ? kColSelect : kColFrame;
                const int      frameT   = (icon && highlight) ? 2 : 1;
                ctx.DrawFrame(r.x, r.y, r.w, r.h, frameCol, frameT);
            }
        }

        // Item currently being dragged, stuck to the cursor — CONFIRMED by disassembly
        // (maybe_UI_QuickSlotBar_Render 0x5BE340, the only function that references
        // g_DragCtx 0x1822380 for READ during rendering — called by UI_RenderAllDialogs
        // 0x5AE2D0 AFTER UI_StorageWin_Draw, hence above the grid: case 27/28 = warehouse
        // type in the switch on `g_DragCtx.kind`. The icon is drawn there CENTERED on the
        // cursor (`a4 - width/2, a5 - height/2`, NO grab offset), absent from the previous
        // implementation which drew ONLY the grayed-out source cell — the opposite of the
        // binary (nothing at the source, icon at the cursor). Added here.
        if (hasSel && dev && ctx.sprites) {
            gfx::GpuTexture* dragIcon = GetIconTex(dev, wh.pendingMove.snapshot.itemId);
            if (dragIcon && dragIcon->Handle() && dragIcon->Width() > 0 && dragIcon->Height() > 0) {
                const int dx = cursorX - kCellSize / 2;
                const int dy = cursorY - kCellSize / 2;
                const float sx = static_cast<float>(kCellSize) / static_cast<float>(dragIcon->Width());
                const float sy = static_cast<float>(kCellSize) / static_cast<float>(dragIcon->Height());
                ctx.sprites->DrawSpriteScaled(dragIcon->Handle(), nullptr, dx, dy, sx, sy,
                                               gfx::kSpriteWhite, /*compensatePos=*/true);
            }
        }

        // "Withdraw -> Bag" button (active only when a cell is selected).
        const bool wbtnHover = hasSel && PointInRect(cursorX, cursorY, withdrawBt.x, withdrawBt.y, withdrawBt.w, withdrawBt.h);
        ctx.FillRect(withdrawBt.x, withdrawBt.y, withdrawBt.w, withdrawBt.h,
                     !hasSel ? kColBtnBgOff : (wbtnHover ? kColSelect : kColBtnBg));
        ctx.DrawFrame(withdrawBt.x, withdrawBt.y, withdrawBt.w, withdrawBt.h, kColFrame, 1);

        // "Validate" button (lock +24, EA 0x5d592d): ALWAYS active — the binary
        // does not gate it on any state.
        const bool vbtnHover = PointInRect(cursorX, cursorY, validateBt.x, validateBt.y, validateBt.w, validateBt.h);
        ctx.FillRect(validateBt.x, validateBt.y, validateBt.w, validateBt.h,
                     vbtnHover ? kColSelect : kColBtnBg);
        ctx.DrawFrame(validateBt.x, validateBt.y, validateBt.w, validateBt.h, kColFrame, 1);
        return;
    }

    // --- Text phase ---
    ctx.Text("Entrepot", panel.x + kGridPad, panel.y + (kHeaderH - 12) / 2, kColTitle);

    const int closeLblW = ctx.MeasureText("X");
    ctx.Text("X", closeBtn.x + (closeBtn.w - closeLblW) / 2, closeBtn.y + 2, kColText);

    for (int row = 0; row < game::WarehouseGrid::kRows; ++row) {
        for (int col = 0; col < game::WarehouseGrid::kCols; ++col) {
            const game::WarehouseItemCell& cell = wh.grid.cells[row][col];
            if (cell.Empty()) continue;
            const Rect r = CellRect(row, col);
            const std::string label = CellLabel(cell);
            ctx.Text(label.c_str(), r.x + 3, r.y + 3, kColText);
            if (cell.durability > 0) {
                const std::string dur = "d:" + std::to_string(cell.durability);
                ctx.Text(dur.c_str(), r.x + 3, r.y + kCellSize - 15, kColTextDim);
            }
        }
    }

    // Quantity of the item being dragged, below the icon stuck to the cursor
    // (cf. Panels phase above) — same condition (hasSel), same data source
    // (pendingMove.snapshot).
    if (hasSel && wh.pendingMove.snapshot.count > 1) {
        const std::string qty = "x" + std::to_string(wh.pendingMove.snapshot.count);
        ctx.Text(qty.c_str(), cursorX - kCellSize / 2 + 2, cursorY + kCellSize / 2 - 14, kColText);
    }

    // Window footer: weight / currency (g_Client.inv, cf. Game/ClientRuntime.h).
    const int footerTop = panel.y + panel.h - kFooterH;
    char line[96];
    std::snprintf(line, sizeof(line), "Poids: %lld    Or: %lld",
                  static_cast<long long>(game::g_Client.inv.weight),
                  static_cast<long long>(game::g_Client.inv.currency));
    ctx.Text(line, panel.x + kGridPad, footerTop + 4, kColText);

    // Withdraw button label.
    const char* btnLabel = hasSel ? "Retirer -> Sac" : "(selectionner)";
    const int btnLabelW = ctx.MeasureText(btnLabel);
    ctx.Text(btnLabel, withdrawBt.x + (withdrawBt.w - btnLabelW) / 2,
              withdrawBt.y + (withdrawBt.h - 12) / 2, hasSel ? kColText : kColTextDim);

    // Validate button label (the window's only explicit emitter, EA 0x5d5947).
    const int vLabelW = ctx.MeasureText("Valider");
    ctx.Text("Valider", validateBt.x + (validateBt.w - vLabelW) / 2,
              validateBt.y + (validateBt.h - 12) / 2, kColText);

    // Last action status (swap/withdraw), below the button.
    if (!statusText_.empty())
        ctx.Text(statusText_.c_str(), panel.x + kGridPad, footerTop + kFooterH - 14, kColSuccess);
}

} // namespace ts2::ui

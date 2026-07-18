// UI/WarehouseWindow.h — WAREHOUSE (Warehouse/Storage) window of the TS2 client.
//
// 5x5 view onto game::g_Warehouse (Game/WarehouseSystem.h, already written). The
// 1232-byte network blob (WarehouseGrid) is already decoded on the
// Pkt_WarehouseOpen/Pkt_WarehouseUpdate handler side; this window DISPLAYS the
// resulting 5x5 grid and drives the selection/swap/withdraw actions:
//   - click on a non-empty cell, no selection currently active
//       -> WarehouseState::SelectPendingMove(row,col). VISUAL FEEDBACK — FIXED
//          by disassembly ("drag-and-drop visual feedback" mission, 2026-07-14,
//          decompilation of Item_BeginDragTransaction 0x5AFDF0 +
//          Inv_RemoveItemQuantity 0x5B0340 case 18/19/27/28 + UI_StorageWin_Draw
//          0x5D6610 + maybe_UI_QuickSlotBar_Render 0x5BE340): the old
//          "grayed-out source cell" hypothesis (kColDragSource/kColDragSourceOverlay)
//          was FALSE — the binary removes the item from the source grid as soon
//          as it's picked up (Inv_RemoveItemQuantity sets its itemId to 0 there),
//          so UI_StorageWin_Draw (which only draws the icon IF `field >= 1`)
//          draws NOTHING at that spot: the source cell becomes an ORDINARY EMPTY
//          cell, with no particular tint. In exchange, the picked-up item is
//          drawn CENTERED ON THE CURSOR every frame while the drag is active
//          (maybe_UI_QuickSlotBar_Render, dispatch on g_DragCtx, case 27/28 =
//          warehouse type), which the previous implementation did NOT do at all.
//          See Render() in the .cpp.
//   - re-click on the SAME already-selected cell
//       -> deselect (WarehouseState::CancelPendingMove)
//   - re-click on ANOTHER grid cell (empty or occupied)
//       -> WarehouseState::SwapCells (local swap, "sort"). NO PACKET.
//   - click on "Retirer -> Sac" button (active only when a cell is
//     selected) -> WarehouseState::CommitCellToInventory. NO PACKET.
//   - click on "Valider" -> Net_SendPacket_Op32(nc, 1), UNCONDITIONAL.
//   - close -> Net_SendPacket_Op32(nc, 1) (via UI_StorageWin_CommitGrid path).
//
// ===========================================================================
// NETWORK — REWRITTEN 2026-07-16 (wave W6) on IDA evidence. The previous state
// was WRONG on three points, all fixed here:
//
//  1. INVENTED PACKET. The old code emitted Net_SendPacket_Op31 with kind=5
//     ("sort") / kind=4 ("withdraw"). Scanning UI_StorageWin_OnLUp
//     0x5d5400 finds ONLY TWO Op31 sites in the entire binary, and neither
//     is the warehouse: EA 0x5d576c = case 1 (my stall) selector 1, and
//     EA 0x5d5dd6 = case 5 (player shop) selector 2. Op31 selectors 4 and 5
//     DO NOT EXIST ANYWHERE. The warehouse (mode 2) NEVER emits Op31.
//
//  2. PER-ACTION EMISSION. The binary emits NOTHING for grid manipulation:
//     UI_StorageWin_OnLDown 0x5d4240 and UI_StorageWin_OnKey 0x5d6330
//     contain no `call Net_Send*`. Drag-and-drop, cell swap, and quantity
//     entry are 100% LOCAL STAGING. Only "Validate" and "Close" emit, and
//     they emit Net_SendPacket_Op32(&g_AutoPlayMgr, 1):
//       - Validate (lock +24, sprite unk_901064 @ (x+167, y+411)): EA 0x5d5947,
//         UNCONDITIONAL — no morph/lock guard, no lock set afterward.
//       - Close (lock +12, sprite unk_8F3798 @ (x+8, y+6)): EA 0x5d57ce ->
//         UI_StorageWin_CommitGrid(this) 0x5d2f70, whose case 2 (= WAREHOUSE)
//         dumps the 5x5 grid then emits Op32(1) at EA 0x5d373f.
//     Pagination (locks +16/+20): purely local (page 0..4, EA 0x5d585b /
//     0x5d58dc) — warehouse "tabs" do not exist; the tabs (+1328) and gold
//     transfer (Net_SendOp110, EA 0x5d5ea3) belong to mode 5 = PLAYER SHOP,
//     not the warehouse.
//
//  3. DEAD CODE. The old SendGridCommit() began with `if (!net_) return;`
//     while Bind() was never called anywhere -> net_ ALWAYS null -> no
//     emission possible. The binary addresses g_NetClient 0x8156A0 as a
//     GLOBAL (the 234 builders read it directly, never as a parameter). This
//     pattern is restored via net::GlobalNetClient() (Net/NetClient.h:67-68),
//     set by ConnectGameServer — same idiom as Game/MapWarp.cpp:86. Bind()/net_
//     are therefore REMOVED (no caller: verified across the whole tree).
//
// Analog of a1[2]: the binary guards the entry point of UI_StorageWin_OnLUp with
// `if (!*(this+8)) return 0;` (dword_1822998, EA 0x5d540d) and the body of
// UI_StorageWin_CommitGrid with `if (a1[2])` (EA 0x5d2f7e) — this is the
// "window active" flag. bOpen_ (Dialog) is taken as the LIVING analog of this
// flag: same role and same position in the flow.
// ===========================================================================
//
// Project rule: this file does NOT edit any existing header; it includes
// UI/UIManager.h, Game/WarehouseSystem.h, and Game/ClientRuntime.h read-only.
#pragma once
#include "UI/UIManager.h"
#include "Game/WarehouseSystem.h"
#include "Game/ClientRuntime.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace ts2::ui {

class WarehouseWindow : public Dialog {
public:
    WarehouseWindow();

    // SHARED GPU icon cache (memory pooling, cf. Gfx/IconTextureCache.h): injected
    // by UI/GameWindows.cpp, same instance as InventoryWindow/EnchantWindow/
    // VendorShopWindow. nullptr (fallback) => local ownIconCache_ (never the case
    // in production, cf. InventoryWindow::SetIconCache).
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }

    // Open/close (Dialog::Open/Close). Opening does not affect the grid
    // (already populated by the network handlers); closing cancels any
    // pending selection so nothing is left "dangling".
    void Open() override;
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x, y, w, h; };

    Rect PanelRect() const;
    Rect CloseButtonRect() const;
    Rect WithdrawButtonRect() const;
    // "Validate" button = binary lock +24 (sprite unk_901064 @ (x+167, y+411),
    // UI_StorageWin_OnLUp case 2, EA 0x5d592d): the ONLY explicit emission of the
    // warehouse window -> Net_SendPacket_Op32(nc, 1) (EA 0x5d5947).
    Rect ValidateButtonRect() const;
    Rect CellRect(int row, int col) const;
    bool CellAt(int mx, int my, int& outRow, int& outCol) const;
    bool PointInPanel(int mx, int my) const;

    // Recomputes x_/y_ (centering) from the current REAL screen dimensions —
    // called EVERY frame by Render() (cf. .cpp header banner: "window frozen at
    // design position" bug fixed, misaligned-windows mission). Same pattern as
    // EnchantWindow::ComputeLayout / MsgBoxDialog::Layout, matching the contract
    // documented by Dialog::x_/y_ ("screen position recentered every frame",
    // cf. UI/UIManager.h).
    void RecomputeCenter(int screenW, int screenH);

    void HandleCellClick(int row, int col);
    void HandleWithdrawClick();
    void HandleValidateClick();

    // Net_SendPacket_Op32(&g_AutoPlayMgr, 1) — the ONLY packet of the warehouse
    // window (opcode 0x20, 1 char field emitted as 4 bytes LE, total 13).
    // Emitted by the "Validate" button (EA 0x5d5947) and by closing via
    // UI_StorageWin_CommitGrid case 2 (EA 0x5d373f) — UNCONDITIONAL in both
    // cases. Target: net::GlobalNetClient() (global g_NetClient 0x8156A0),
    // cf. file header banner.
    void SendStorageCommit();

    static std::string CellLabel(const game::WarehouseItemCell& cell);

    // --- Item icons (same pattern as InventoryWindow: resolver + lazy cache +
    // fallback to the existing colored cell if the texture fails to load). The D3D9
    // device is only accessible at render time (ctx.renderer), hence taking the
    // device as a parameter here rather than a device_ member like InventoryWindow
    // (this window has no dedicated Init() — Dialog provides no device at
    // construction time).
    gfx::GpuTexture* GetIconTex(IDirect3DDevice9* dev, uint32_t itemId);
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;

    // --- Geometry (fixed panel dimensions, reference 1024x768; x_/y_ ORIGIN (inherited
    // from Dialog) recentered every frame by RecomputeCenter() above — NOT frozen) ---
    static constexpr int kCellSize  = 48;
    static constexpr int kCellGap   = 4;
    static constexpr int kGridPad   = 12;
    static constexpr int kHeaderH   = 26;
    static constexpr int kFooterH   = 58;
    static constexpr int kCloseSize = 18;
    // Two side-by-side buttons in the window footer ("Retirer -> Sac" and
    // "Valider"): 2*122 + 8 gutter = 252 <= kPanelW - 2*kGridPad = 256.
    static constexpr int kBtnW      = 122;
    static constexpr int kBtnH      = 24;
    static constexpr int kBtnGap    = 8;

    static constexpr int kPanelW = kGridPad * 2
        + game::WarehouseGrid::kCols * kCellSize
        + (game::WarehouseGrid::kCols - 1) * kCellGap;
    static constexpr int kPanelH = kHeaderH + kGridPad
        + game::WarehouseGrid::kRows * kCellSize
        + (game::WarehouseGrid::kRows - 1) * kCellGap
        + kGridPad + kFooterH;

    // --- Palette (D3DCOLOR = 0xAARRGGBB, cf. mission spec) ---
    static constexpr D3DCOLOR kColPanelBg   = 0xE0202028u; // panel background
    static constexpr D3DCOLOR kColFrame     = 0xFF808080u; // frame
    static constexpr D3DCOLOR kColTitle     = 0xFFFFDD66u; // title
    static constexpr D3DCOLOR kColText      = 0xFFFFFFFFu; // text
    static constexpr D3DCOLOR kColTextDim   = 0xFFAAAAAAu; // dimmed text
    static constexpr D3DCOLOR kColSelect    = 0xFF4060A0u; // hover (potential target)
    static constexpr D3DCOLOR kColError     = 0xFFFF6060u; // error
    static constexpr D3DCOLOR kColSuccess   = 0xFF60FF60u; // success
    static constexpr D3DCOLOR kColHeaderBg  = 0xFF2A2A34u; // title bar
    static constexpr D3DCOLOR kColCellBg    = 0xFF34343Eu; // occupied cell
    static constexpr D3DCOLOR kColEmptyCell = 0xFF1A1A20u; // empty cell
    static constexpr D3DCOLOR kColBtnBg     = 0xFF3A3A46u; // active button
    static constexpr D3DCOLOR kColBtnBgOff  = 0xFF262629u; // disabled button

    std::string statusText_; // last action result (swap/withdraw), shown in the window footer
};

} // namespace ts2::ui

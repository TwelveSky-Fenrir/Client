// UI/ShareBoxWindow.h — UI_ShareBoxDlg window (dword_1822560, ctor 0x5CDDD0).
//
// ###########################################################################
// # NAME WARNING: this is NOT a "shared chest".                             #
// #                                                                         #
// # The IDB symbol `UI_ShareBoxDlg` is MISLEADING (name inherited from an   #
// # earlier naming pass). Decompilation proves this window is the          #
// # AUTO-POTION BELT CONFIGURATION PANEL (consumables):                    #
// # UI_ShareBoxDlg_Draw 0x5CE4D0 renders the 10 slots of                   #
// # g_AutoPotionBelt 0x16757B0 with their charge counter                   #
// # dword_16757D8 ("%d / %d" out of 30). No relation to the warehouse      #
// # (UI_StorageWin_* / dword_1822990 / opcodes 0x22-0x24), already ported  #
// # by UI/WarehouseWindow.*. The file/class name follows the IDA symbol    #
// # ("IDA = single source of truth" rule); the SEMANTICS are as described  #
// # here.                                                                  #
// ###########################################################################
//
// Original functions transcribed (all re-proven by decompilation on 2026-07-16):
//   UI_ShareBoxDlg_InitBtnMap 0x5CDFB0 -> button constants below
//   UI_ShareBoxDlg_Open       0x5CE0C0 -> Open()
//   UI_ShareBoxDlg_Close      0x5CE100 -> Close()
//   UI_ShareBoxDlg_OnLDown    0x5CE120 -> OnMouseDown()
//   UI_ShareBoxDlg_OnLUp      0x5CE330 -> OnClick()
//   UI_ShareBoxDlg_Draw       0x5CE4D0 -> Render()
//   UI_ShareBox_MoveItem      0x5CEAB0 -> MoveItem()  [static: free __stdcall, no `this`]
//
// NOT ported (deliberately): UI_ShareBox_Withdraw 0x5CEC40 — 0 xrefs, DEAD function.
//
// CORRECTIONS TO THE GAP TRACKER (USD-01), proven by decompilation
//
//  1. "MoveItem(a1, a2): selector 1=deposit / 2=withdraw" -> WRONG. The real
//     signature is MoveItem(a1 = VERBOSE flag, a2 = action code):
//     `a1` only gates the display of error messages (`if (a1)` at
//     EA 0x5CEAD3 / 0x5CEB0E / 0x5CEB89 / 0x5CEBBB), it selects NOTHING.
//  2. Both live callers literally pass `(1, 1)`:
//     `push 1 ; push 1` at EA 0x5CE3EA-0x5CE3EC (OnLUp) and 0x679FE8-0x679FEA
//     (UI_GameHud_ProcNet case 47). Value 2 is emitted by NO path:
//     the `a2 != 1` branch (indexed by dword_1675800) is UNREACHABLE in
//     practice. It's still transcribed (fidelity to the function's body),
//     and flagged as such in the .cpp.
//  3. `dword_1822588` is NOT a HUD-distinct "guard": it's
//     `0x1822560 + 40` = field `*(this+10)` = bOpen itself (EA 0x5CE0CC).
//     The HUD therefore does a simple bOpen toggle, not a second-flag test.
//  4. EA 0x5CE3F1 is NOT a "drag & drop": it's the click of button 3946.
//
// NETWORK — NO EMISSION POSSIBLE TODAY (missing builder, cf. .cpp)
//
// The window's only network outlet is
// `Net_QueueAction16(&g_PlayerCmdController, a2)` (0x512B90, EA 0x5CEC28), which
// does NOT exist on the C++ side (exhaustive grep: 0 occurrences). It depends on
// three building blocks that are themselves unported (g_SelfMoveStateBlock
// 0x1687324, Char_IsAttackAction 0x558A50, g_PlayerCmdController+51600 lock)
// and belongs to the network backlog (math-01 / W8). MoveItem() therefore
// faithfully transcribes ALL the guards and ALL the messages, and stops at a
// `// TODO [anchor 0x5CEC28]` instead of inventing a call — cf. wave report.
//
// Project rule: this file edits NO existing header; it includes
// UI/UIManager.h, Game/ClientRuntime.h and Gfx/* read-only.
#pragma once
#include "UI/UIManager.h"
#include "Game/ClientRuntime.h"
#include "Gfx/GpuTexture.h"
#include "Gfx/IconTextureCache.h"

#include <cstdint>

namespace ts2::ui {

class ShareBoxWindow : public Dialog {
public:
    ShareBoxWindow();
    ~ShareBoxWindow() override;

    // SHARED GPU icon cache (cf. Gfx/IconTextureCache.h): injected by
    // UI/GameWindows.cpp, same instance as InventoryWindow/WarehouseWindow/
    // EnchantWindow/VendorShopWindow. nullptr (fallback) -> local ownIconCache_.
    void SetIconCache(gfx::IconTextureCache* c) { sharedIconCache_ = c; }

    // UI_ShareBoxDlg_Open 0x5CE0C0: *(this+10)=1 then loop i<4 zeroing
    // *(this+11+i)=0 (EA 0x5CE0D3). Only 2 of the 4 latches are actually
    // used (+11 = action button, +13 = close button); +12 and +14 are
    // zeroed but never read — reproduced as-is (cf. .cpp).
    void Open() override;
    void Close() override;                       // UI_ShareBoxDlg_Close 0x5CE100

    bool OnMouseDown(int x, int y) override;     // UI_ShareBoxDlg_OnLDown 0x5CE120
    bool OnClick(int x, int y) override;         // UI_ShareBoxDlg_OnLUp   0x5CE330

    void Render(const UiContext& ctx, int cursorX, int cursorY) override; // Draw 0x5CE4D0

    // -----------------------------------------------------------------------
    // WIRING API — the 3 proven triggers live in files NOT owned by this front
    // (UI/GameHud.cpp = wave W9, Net/ItemActionDispatch.cpp = network backlog).
    // We therefore expose STATIC entry points so each is a one-liner with no
    // dependency on GameWindows:
    //
    //   (1) Net/ItemActionDispatch.cpp::HandleAutoPotionBelt, after l.296 —
    //       mirrors EA 0x46AF6C (`call UI_ShareBoxDlg_Open` in
    //       Pkt_ItemActionDispatch 0x46A320, typeCode 26):
    //           ui::ShareBoxWindow::OpenActive();
    //
    //   (2) UI/GameHud.cpp (HUD toggle) — mirrors 0x6799A9 -> 0x6799CF:
    //           if (auto* w = ui::ShareBoxWindow::Active()) {
    //               if (!w->IsOpen()) { UIManager::Instance().CloseAll(); w->Open(); }
    //               else               w->Close();
    //           }
    //
    //   (3) UI/GameHud.cpp::ProcNet case 47 — mirrors EA 0x679FF1:
    //           ui::ShareBoxWindow::MoveItem(/*verbose=*/1, /*action=*/1);
    //
    // Active() returns the registered instance (only one across the whole
    // process, owned by GameWindows) or nullptr outside a session.
    // -----------------------------------------------------------------------
    static ShareBoxWindow* Active();
    static void            OpenActive();

    // UI_ShareBox_MoveItem 0x5CEAB0 — FREE __stdcall in the binary (uses only
    // globals, no `this`): so static here too.
    //   verbose : `a1` — gates error message display (NOT a selector).
    //   action  : `a2` — action code; ONLY value 1 is ever emitted
    //             (cf. header banner, correction #2).
    static void MoveItem(int verbose, int action);

    // Number of belt slots (loops `i < 10`, EA 0x5CE5E4 / 0x5CE185 /
    // 0x5CE89D). The binary's 3 loops are all bounded to 10, like MoveItem's
    // `>= 0xA` guards (EA 0x5CEAC4 / 0x5CEB3F).
    static constexpr int kSlots = 10;

private:
    struct Rect { int x, y, w, h; };

    // Screen recentering, RECOMPUTED ON EVERY EVENT like the binary:
    // Draw (EA 0x5CE567/0x5CE58F), OnLDown (0x5CE15D/0x5CE182) and OnLUp
    // (0x5CE36B/0x5CE390) ALL THREE redo
    //   x = nWidth/2  - Sprite2D_GetWidth(unk_977404)/2
    //   y = nHeight/2 - Sprite2D_GetHeight(unk_977404)/2
    // before any hit-test. We reproduce this systematic recentering.
    void RecomputeCenter(int screenW, int screenH);

    Rect PanelRect() const;
    Rect SlotRect(int i) const;        // (x + 55*(i%5) + 19, y + 55*(i/5) + 41)
    Rect ActionButtonRect() const;     // buttons 3946/3947 -> (158, 165)
    Rect CloseButtonRect() const;      // buttons 3950/3951 -> (229, 165)

    // Slot hit-test — STRICT `>` / `<` comparisons from the binary
    // (EA 0x5CE209), exclusive bounds on BOTH sides: `a4 > ox+19 && a4 < ox+74`.
    // PointInRect (inclusive low / exclusive high) is NOT equivalent: the
    // comparison is therefore written out by hand to stay bit-faithful.
    bool SlotAt(int mx, int my, int& outSlot) const;

    gfx::GpuTexture* GetIconTex(IDirect3DDevice9* dev, uint32_t itemId);
    gfx::IconTextureCache& ActiveIconCache() { return sharedIconCache_ ? *sharedIconCache_ : ownIconCache_; }

    gfx::IconTextureCache  ownIconCache_;
    gfx::IconTextureCache* sharedIconCache_ = nullptr;

    // --- Button latches (fields +11 and +13 of the original dialog) ---
    // Armed on click-down (OnLDown: *(this+11)=1 EA 0x5CE283; *(this+13)=1
    // EA 0x5CE2E4), disarmed on release (OnLUp EA 0x5CE39F / 0x5CE409).
    bool actionLatch_ = false;   // +11 (button 3946 pressed -> sprite 3947)
    bool closeLatch_  = false;   // +13 (button 3950 pressed -> sprite 3951)

    // GEOMETRY — literals captured in the binary (exact), except the panel's
    // EXTENT and the buttons' SIZE, which derive from the sprites
    // (Sprite2D_GetWidth/Height of unk_977404 / unk_977498 / unk_9776E8) and are
    // therefore NOT known statically: fallback values sized to contain all
    // proven elements (cf. .cpp).
    static constexpr int kSlotPitch = 55;  // 55*(i%5) / 55*(i/5)  (EA 0x5CE629/0x5CE64A)
    static constexpr int kSlotOx    = 19;  // +19                  (EA 0x5CE629)
    static constexpr int kSlotOy    = 41;  // +41                  (EA 0x5CE64A)
    static constexpr int kSlotCols  = 5;   // i%5 / i/5 -> 5 columns x 2 rows
    static constexpr int kCountDx   = 44;  // text center          (EA 0x5CE8FF)
    static constexpr int kCountDy   = 77;  // text line            (EA 0x5CE93F)
    static constexpr int kMaxCharges = 30; // literal 30 in "%d / %d" (EA 0x5CE8E1)

    // UI_ShareBoxDlg_InitBtnMap 0x5CDFB0 — BtnPosMapA literals:
    //   3946/3947 -> (158, 165)  EA 0x5CE01D / 0x5CE034   (action button)
    //   3950/3951 -> (229, 165)  EA 0x5CE04B / 0x5CE062   (close button)
    // Entries 3942/3943/3944 (25,-33), 3945 (357,286) and 3952 (-26,-55) are
    // inserted by InitBtnMap but NEVER read by Draw/OnLDown/OnLUp -> not ported.
    static constexpr int kBtnActionX = 158;
    static constexpr int kBtnActionY = 165;
    static constexpr int kBtnCloseX  = 229;
    static constexpr int kBtnCloseY  = 165;

    // Fallbacks (derived from sprites, see above).
    static constexpr int kBtnW   = 62;
    static constexpr int kBtnH   = 24;
    static constexpr int kPanelW = 310;  // contains the grid (19..294) and the close button (229+62)
    static constexpr int kPanelH = 200;  // contains the counters (y=173) and the buttons (165+24)

    // --- Palette (D3DCOLOR = 0xAARRGGBB), same convention as WarehouseWindow ---
    static constexpr D3DCOLOR kColPanelBg   = 0xE0202028u;
    static constexpr D3DCOLOR kColFrame     = 0xFF808080u;
    static constexpr D3DCOLOR kColTitle     = 0xFFFFDD66u;
    static constexpr D3DCOLOR kColText      = 0xFFFFFFFFu;
    // No "empty slot" color: the binary draws NOTHING for an empty slot
    // (`>= 1` guard, EA 0x5CE60B) — cf. Render() in the .cpp.
    static constexpr D3DCOLOR kColSlotBg    = 0xFF34343Eu; // fallback if the icon fails to load
    static constexpr D3DCOLOR kColBtnBg     = 0xFF3A3A46u;
    static constexpr D3DCOLOR kColBtnHover  = 0xFF4060A0u;
    static constexpr D3DCOLOR kColBtnDown   = 0xFF5878C0u;
    // Highlights: unk_94D970 (selected inventory slot, EA 0x5CE6CA) and
    // unk_947A0C (selected belt slot, EA 0x5CE6E9). Overlay sprites
    // in the binary -> rendered here as colored frames (fallback, cf. .cpp).
    static constexpr D3DCOLOR kColSelInv    = 0xFFFFCC33u; // unk_94D970
    static constexpr D3DCOLOR kColSelBelt   = 0xFF33FF99u; // unk_947A0C
};

} // namespace ts2::ui

// UI/ShareBoxWindow.cpp — implementation of UI_ShareBoxDlg (0x5CDDD0 / dword_1822560)
// = AUTO-POTION BELT CONFIGURATION PANEL.
// See UI/ShareBoxWindow.h for the real semantics (the IDA symbol is misleading),
// the corrections to the gap tracker, and the wiring contract.
//
// No Net/ include: this window emits NOTHING today (Net_QueueAction16
// 0x512B90 doesn't exist on the C++ side, cf. MoveItem() below) — so no
// <winsock2.h>/<windows.h> ordering constraint here (same include profile as
// UI/SocialWindow.cpp / UI/QuestTrackerWindow.cpp).
#include "UI/ShareBoxWindow.h"
#include "UI/PanelSkin.h"
#include "Game/GameDatabase.h"   // game::GetItemInfo / game::ItemInfo::iconId (+192)
#include "Game/GameState.h"      // game::g_World.self.mode (= g_SelfActionState 0x1687328)
#include "Net/ClientState.h"     // ts2::net::g_MorphInProgress (= g_MorphInProgress 0x1675A88)

#include <cstdio>

namespace ts2::ui {

namespace {

// Active instance (only one across the whole process, owned by GameWindows).
// Serves the 3 static wiring entry points documented in the .h.
ShareBoxWindow* g_activeShareBox = nullptr;

// Real "best effort" panel background — automatic fallback to kColPanelBg if
// missing (cf. methodology in UI/PanelSkin.h). The real background is sprite
// unk_977404 (EA 0x5CE5DF), whose .IMG file isn't statically resolved.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_00300.IMG");

// --- Original addresses (long tail, stored via game::g_Client.Var) -------------
// REUSED as-is, NOT duplicated: these are exactly the same slots written by
// Net/ItemActionDispatch.cpp:98-99 (kAutoPotionBelt / kAutoPotionTimer),
// Net/GameVarDispatch.cpp:537-578 and Net/GameHandlers_InvCells.cpp:570-574.
constexpr uint32_t kAutoPotionBelt  = 0x16757B0; // g_AutoPotionBelt[i]  : item ID
constexpr uint32_t kAutoPotionTimer = 0x16757D8; // dword_16757D8[i]     : charges (/30)
constexpr uint32_t kInvSelSlot      = 0x1675800; // dword_1675800        : selected inventory slot
constexpr uint32_t kInvSelCount     = 0x1675804; // dword_1675804        : associated counter
constexpr uint32_t kBeltSelSlot     = 0x16760E0; // dword_16760E0        : selected belt slot
constexpr uint32_t kGateA           = 0x1687310; // dword_1687310[0]     : MoveItem guard
constexpr uint32_t kGateB           = 0x1687474; // dword_1687474[0]     : MoveItem guard
constexpr uint32_t kSysMsgColorAddr = 0x84DFD8;  // g_SysMsgColor

// --- StrTable005 ids captured in UI_ShareBox_MoveItem 0x5CEAB0 ------
constexpr int kStrSlotOutOfRange = 2398; // EA 0x5CEAE5 / 0x5CEB60
constexpr int kStrSlotEmpty      = 2399; // EA 0x5CEB21 / 0x5CEB9C
constexpr int kStrBadActionState = 925;  // EA 0x5CEBCD
constexpr int kStrGateRefused    = 1186; // EA 0x5CEC0D

// g_SysMsgColor 0x84DFD8 — not modeled as a dedicated field, long-tail via Var()
// (same convention as UI/PartyWindow.cpp:47 / Game/SocialSystem.cpp:68).
uint32_t SysMsgColor() {
    return static_cast<uint32_t>(game::g_Client.VarGet(kSysMsgColorAddr));
}

int32_t BeltItemId(int i)  { return game::g_Client.VarGet(kAutoPotionBelt  + 4u * static_cast<uint32_t>(i)); }
int32_t BeltCharges(int i) { return game::g_Client.VarGet(kAutoPotionTimer + 4u * static_cast<uint32_t>(i)); }

// Item icon resolver — IDENTICAL to UI/WarehouseWindow.cpp:23 and
// UI/InventoryWindow.cpp (reference pattern, duplicated for lack of a common header).
// The binary indexes `&g_AssetMgr_ItemIconSlots + 148 * (ITEM_INFO[+192] - 1)`
// (EA 0x5CE685/0x5CE6A2): the atlas's 0-based slot = iconId - 1, which
// corresponds to file 002_%05u.IMG at index `iconId` (1-based) — same
// convention as the 4 other icon windows (cf. Docs/TS2_UI_ICON_ATLAS_CONFIRMED.md).
std::string ResolveItemIconPath(uint32_t itemId) {
    const game::ItemInfo* info = game::GetItemInfo(itemId);
    if (!info || info->iconId == 0) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\002\\002_%05u.IMG", info->iconId);
    return std::string(buf);
}

} // namespace

// Lifecycle / active instance
ShareBoxWindow::ShareBoxWindow() {
    // Default centering (reference resolution) — actually recomputed on every
    // event by RecomputeCenter(), like the binary.
    RecomputeCenter(ts2::kRefWidth, ts2::kRefHeight);
    g_activeShareBox = this;
}

ShareBoxWindow::~ShareBoxWindow() {
    if (g_activeShareBox == this) g_activeShareBox = nullptr;
}

ShareBoxWindow* ShareBoxWindow::Active() { return g_activeShareBox; }

// Mirrors EA 0x46AF6C: `call UI_ShareBoxDlg_Open` at the end of typeCode 26 of
// Pkt_ItemActionDispatch 0x46A320 (server branch, currently ABANDONED on the
// C++ side — cf. Net/ItemActionDispatch.cpp::HandleAutoPotionBelt, TODO l.296).
void ShareBoxWindow::OpenActive() {
    if (g_activeShareBox) g_activeShareBox->Open();
}

// UI_ShareBoxDlg_Open 0x5CE0C0: *(this+10) = 1 (EA 0x5CE0CC) then loop
// `for (i=0; i<4; ++i) *(this+i+11) = 0` (EA 0x5CE0D3).
// FIDELITY: the loop zeroes 4 latches while only 2 are ever used
// (+11 = action, +13 = close; +12 and +14 are read NOWHERE in
// Draw/OnLDown/OnLUp). Zeroing the two modeled latches is therefore
// STRICTLY equivalent — the two phantom fields have no reader.
void ShareBoxWindow::Open() {
    Dialog::Open();
    actionLatch_ = false;
    closeLatch_  = false;
}

// UI_ShareBoxDlg_Close 0x5CE100: *(this+10) = 0, nothing else — no
// emission, no selection reset.
void ShareBoxWindow::Close() {
    Dialog::Close();
}

// Geometry
// Draw 0x5CE567/0x5CE58F, OnLDown 0x5CE15D/0x5CE182, OnLUp 0x5CE36B/0x5CE390:
//   x = nWidth/2  - Sprite2D_GetWidth(unk_977404)/2
//   y = nHeight/2 - Sprite2D_GetHeight(unk_977404)/2
// Since the background sprite's width/height can't be known statically,
// we use the fallback extent kPanelW/kPanelH (cf. .h). The CENTERING itself is
// exact and redone on every event just like the binary.
void ShareBoxWindow::RecomputeCenter(int screenW, int screenH) {
    x_ = screenW / 2 - kPanelW / 2;
    y_ = screenH / 2 - kPanelH / 2;
}

ShareBoxWindow::Rect ShareBoxWindow::PanelRect() const {
    return { x_, y_, kPanelW, kPanelH };
}

// EA 0x5CE629 / 0x5CE64A : (x + 55*(i%5) + 19, y + 55*(i/5) + 41).
ShareBoxWindow::Rect ShareBoxWindow::SlotRect(int i) const {
    return { x_ + kSlotPitch * (i % kSlotCols) + kSlotOx,
             y_ + kSlotPitch * (i / kSlotCols) + kSlotOy,
             kSlotPitch, kSlotPitch };
}

ShareBoxWindow::Rect ShareBoxWindow::ActionButtonRect() const {
    return { x_ + kBtnActionX, y_ + kBtnActionY, kBtnW, kBtnH };
}

ShareBoxWindow::Rect ShareBoxWindow::CloseButtonRect() const {
    return { x_ + kBtnCloseX, y_ + kBtnCloseY, kBtnW, kBtnH };
}

// EA 0x5CE209 — STRICT comparisons on both sides (exclusive bounds):
//   a4 > x+55*(i%5)+19 && a4 < x+55*(i%5)+74 && a5 > y+55*(i/5)+41 && a5 < y+55*(i/5)+96
// (74 = 19+55, 96 = 41+55). This is NOT PointInRect: the comparison is written
// out by hand to stay faithful (a click exactly on the edge doesn't count).
bool ShareBoxWindow::SlotAt(int mx, int my, int& outSlot) const {
    for (int i = 0; i < kSlots; ++i) {
        if (BeltItemId(i) < 1) continue;                       // `>= 1` (EA 0x5CE209)
        const Rect r = SlotRect(i);
        if (mx > r.x && mx < r.x + kSlotPitch &&
            my > r.y && my < r.y + kSlotPitch) {
            outSlot = i;
            return true;
        }
    }
    return false;
}

// Mouse events
// UI_ShareBoxDlg_OnLDown 0x5CE120.
bool ShareBoxWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;                                  // EA 0x5CE12D

    // The binary recenters BEFORE the hit-test (EA 0x5CE144..0x5CE182). We don't
    // have screen dimensions here (Dialog::OnMouseDown doesn't receive an
    // UiContext): x_/y_ carry the centering from the last rendered frame, which
    // is equivalent as long as the resolution doesn't change between two frames
    // — same compromise as WarehouseWindow/MsgBoxDialog (cf. lastScreenW_ in
    // UI/UIManager.h).
    int slot = -1;
    if (SlotAt(x, y, slot)) {
        // [audio] Snd3D_PlayScaledVolume(flt_1487E3C, .., 100, 1) (EA 0x5CE216).
        // Not ported: no by-address 3D emitter registry exists on the C++ side
        // (established convention: UI/Widgets.cpp:60, UI/NpcDialogWindow.cpp:287,
        // UI/PartyWindow.cpp:307 note the sound without porting it).
        game::g_Client.Var(kBeltSelSlot) = slot;                // dword_16760E0 = i (EA 0x5CE21E)
        return true;                                            // EA 0x5CE229
    }

    const Rect act = ActionButtonRect();
    if (PointInRect(x, y, act.x, act.y, act.w, act.h)) {
        // [audio] flt_1487E3C (EA 0x5CE27B) — see above.
        actionLatch_ = true;                                    // *(this+11)=1 (EA 0x5CE283)
        return true;
    }

    const Rect cls = CloseButtonRect();
    if (PointInRect(x, y, cls.x, cls.y, cls.w, cls.h)) {
        // [audio] flt_1487E3C (EA 0x5CE2DC) — see above.
        closeLatch_ = true;                                     // *(this+13)=1 (EA 0x5CE2E4)
        return true;
    }

    // Fallback: the binary returns Sprite2D_HitTest(unk_977404, x, y, a4, a5)
    // (EA 0x5CE313) = "is the click inside the background?". The window therefore
    // consumes the click as soon as it lands on the panel, without triggering anything.
    const Rect panel = PanelRect();
    return PointInRect(x, y, panel.x, panel.y, panel.w, panel.h);
}

// UI_ShareBoxDlg_OnLUp 0x5CE330 — EXCLUSIVE if/else-if structure (the binary
// tests +11, ELSE +13; never both), and each branch disarms its latch
// BEFORE the hit-test, then returns 1 even if the hit-test fails (EA 0x5CE3E3 /
// 0x5CE44D). Reproduced as-is.
bool ShareBoxWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;                                  // EA 0x5CE33B

    if (actionLatch_) {
        actionLatch_ = false;                                   // EA 0x5CE39F
        const Rect act = ActionButtonRect();
        if (PointInRect(x, y, act.x, act.y, act.w, act.h))
            MoveItem(1, 1);                                     // `push 1; push 1` EA 0x5CE3EA-0x5CE3F1
        return true;                                            // EA 0x5CE3E3
    }

    if (closeLatch_) {
        closeLatch_ = false;                                    // EA 0x5CE409
        const Rect cls = CloseButtonRect();
        if (PointInRect(x, y, cls.x, cls.y, cls.w, cls.h))
            Close();                                            // UI_ShareBoxDlg_Close (EA 0x5CE457)
        return true;                                            // EA 0x5CE44D
    }

    return false;                                               // EA 0x5CE463
}

// UI_ShareBox_MoveItem 0x5CEAB0 — full transcription
// `verbose` = a1 (message gate), `action` = a2 (action code).
//
// UNREACHABLE IN PRACTICE: the `action != 1` branch (indexed by
// dword_1675800) is reached by NO caller — the two only live call sites
// pass (1,1): EA 0x5CE3EA-0x5CE3EC (OnLUp, above) and
// EA 0x679FE8-0x679FEA (UI_GameHud_ProcNet case 47). It's transcribed to
// stay faithful to the function's BODY, not because it ever executes.
//
// Note two REAL asymmetries in the binary, reproduced as-is:
//   - message 1186 (kStrGateRefused) is NOT gated by `verbose` (EA 0x5CEC02),
//     unlike messages 2398/2399/925;
//   - the morph guard (`g_MorphInProgress == 1`) returns SILENTLY, with
//     no message at all (EA 0x5CEBE6).
void ShareBoxWindow::MoveItem(int verbose, int action) {
    const uint32_t beltSel = static_cast<uint32_t>(game::g_Client.VarGet(kBeltSelSlot));
    const uint32_t invSel  = static_cast<uint32_t>(game::g_Client.VarGet(kInvSelSlot));

    bool reachedCommonGates = false;

    if (action == 1) {                                          // EA 0x5CEABB
        if (beltSel >= static_cast<uint32_t>(kSlots)) {         // EA 0x5CEAC4 (UNSIGNED comparison)
            if (verbose) game::g_Client.msg.System(game::Str(kStrSlotOutOfRange), SysMsgColor()); // EA 0x5CEAE5
            return;
        }
        if (BeltCharges(static_cast<int>(beltSel)) < 1) {       // EA 0x5CEB08
            if (verbose) game::g_Client.msg.System(game::Str(kStrSlotEmpty), SysMsgColor());      // EA 0x5CEB21
            return;
        }
        reachedCommonGates = true;                              // goto LABEL_19
    } else {
        if (invSel < static_cast<uint32_t>(kSlots)) {           // EA 0x5CEB3F
            if (BeltCharges(static_cast<int>(invSel)) >= 1) {   // EA 0x5CEB83
                reachedCommonGates = true;                      // LABEL_19
            } else {
                if (verbose) game::g_Client.msg.System(game::Str(kStrSlotEmpty), SysMsgColor());  // EA 0x5CEB9C
                return;
            }
        } else {
            if (verbose) game::g_Client.msg.System(game::Str(kStrSlotOutOfRange), SysMsgColor()); // EA 0x5CEB60
            return;
        }
    }

    if (!reachedCommonGates) return;

    // --- LABEL_19: common guards ---------------------------------------
    // g_SelfActionState[0] 0x1687328 ≡ game::g_World.self.mode — equivalence
    // ESTABLISHED and documented by Net/ItemActionDispatch.cpp:255-257
    // ("self.mode ≡ g_SelfActionState, read by CombatResultApply").
    if (game::g_World.self.mode != 1) {                         // EA 0x5CEBB5
        if (verbose) game::g_Client.msg.System(game::Str(kStrBadActionState), SysMsgColor());     // EA 0x5CEBCD
        return;
    }

    // g_MorphInProgress 0x1675A88 -> ts2::net::g_MorphInProgress (Net/ClientState.h:18),
    // actually maintained by Net/GameHandlers_Misc.cpp:248. SILENT return.
    if (net::g_MorphInProgress == 1) return;                    // EA 0x5CEBE6

    if (game::g_Client.VarGet(kGateA) && !game::g_Client.VarGet(kGateB)) { // EA 0x5CEBFA
        // NOT gated by `verbose` — faithful (EA 0x5CEC02).
        game::g_Client.msg.System(game::Str(kStrGateRefused), SysMsgColor());                     // EA 0x5CEC0D
        return;
    }

    // --- Emission -----------------------------------------------------------
    // TODO [anchor 0x5CEC28] missing builder: Net_QueueAction16(&g_PlayerCmdController, action)
    //   (0x512B90). Layout proven by decompilation:
    //     - guard `*(g_PlayerCmdController + 51600) == 0`      (EA 0x512BA5)
    //     - guard `Char_IsAttackAction(g_LocalPlayerSheet)`     (0x558A50, EA 0x512BB3) — NOT ported
    //     - memcpy(v5, g_SelfMoveStateBlock 0x1687324, 0x48)    (EA 0x512BCC)   — NOT modeled
    //     - 72-byte block: [0]=0, [1]=16, [2]=0.0f, [3..5]=pos(v6,v7,v8), [6..8]=0.0f,
    //                   [9]=[10]=v9 (cap), [11]=0, [12]=-1, [13]=0, [14]=action,
    //                   [15..17]=0                              (EA 0x512BD4..0x512C3B)
    //     - Net_SendPacket_Op15(&g_AutoPlayMgr, bloc72)         (EA 0x512C5C)
    //     - then *(ctrl+51600)=1, *(ctrl+51604)=g_GameTimeSec,
    //       and if g_SelfActionState in {2,32} -> =1 + g_SelfAnimFrame=0 (EA 0x512C67..0x512CAE)
    //   Net_SendPacket_Op15(NetClient&, const void* data72) EXISTS (Net/SendPackets.h:105),
    //   but wrapping it requires g_SelfMoveStateBlock + Char_IsAttackAction +
    //   g_PlayerCmdController: that's the scope of math-01 / wave W8 (network
    //   backlog), outside this front. NO invented call here (rule #8).
}

// Icons
gfx::GpuTexture* ShareBoxWindow::GetIconTex(IDirect3DDevice9* dev, uint32_t itemId) {
    const std::string path = ResolveItemIconPath(itemId);
    return ActiveIconCache().GetOrLoad(dev, path);
}

// UI_ShareBoxDlg_Draw 0x5CE4D0
void ShareBoxWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Recentering every frame (EA 0x5CE54B..0x5CE58F) — before any use of x_/y_.
    RecomputeCenter(ctx.screenW, ctx.screenH);
    if (!bOpen_) return;                                        // EA 0x5CE4F0

    const Rect panel = PanelRect();
    const Rect act   = ActionButtonRect();
    const Rect cls   = CloseButtonRect();

    const int32_t invSelSlot  = game::g_Client.VarGet(kInvSelSlot);   // dword_1675800
    const int32_t invSelCount = game::g_Client.VarGet(kInvSelCount);  // dword_1675804
    const int32_t beltSelSlot = game::g_Client.VarGet(kBeltSelSlot);  // dword_16760E0

    if (ctx.phase == UiPhase::Panels) {
        // Sprite2D_HitTest(unk_977404, ...) -> Util_SetClampedU8Field(dword_8E714C, 0)
        // (EA 0x5CE5B2/0x5CE5C2): resets the cursor to slot 0 when hovering the panel.
        // NOT ported: CursorSet::SetActiveSlot has no caller anywhere in the tree and
        // `cursors_` is a PRIVATE member of App (App/App.h:43) — this is gap UTIL-01,
        // whose fix (exposing the instance) is explicitly out of scope for this front.
        // TODO [anchor 0x5CE5C2]: Cursors().SetActiveSlot(0) on hover, once UTIL-01 is wired.

        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColPanelBg); // Sprite2D_Draw(unk_977404) EA 0x5CE5DF
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColFrame, 1);

        IDirect3DDevice9* dev = ctx.renderer ? ctx.renderer->Device() : nullptr;

        // Loop over the 10 slots (EA 0x5CE5E4).
        for (int i = 0; i < kSlots; ++i) {
            const int32_t itemId = BeltItemId(i);
            const Rect r = SlotRect(i);

            // Empty slot -> the binary draws NOTHING (`>= 1` guard, EA 0x5CE60B):
            // no cell, no frame, no tint — the background sprite's art is authoritative.
            // We STRICTLY stick to this (fidelity rule): don't add a dark cell
            // "to see the grid", that would be a visual invention.
            if (itemId < 1) continue;

            // MobDb_GetEntry(mITEM, belt[i]) (EA 0x5CE662): if the entry can't be
            // found, the binary draws NOTHING (`if (Entry)` guard EA 0x5CE66F).
            const game::ItemInfo* info = game::GetItemInfo(static_cast<uint32_t>(itemId));
            if (!info) continue;

            gfx::GpuTexture* icon = GetIconTex(dev, static_cast<uint32_t>(itemId));
            if (icon && icon->Handle() && ctx.sprites) {
                // Sprite2D_Draw(&g_AssetMgr_ItemIconSlots + 148*(iconId-1), v21, v26)
                // (EA 0x5CE6A2): blits at the sprite's NATIVE size, no scaling
                // -> DrawSprite (not DrawSpriteScaled).
                ctx.sprites->DrawSprite(icon->Handle(), nullptr, r.x, r.y, gfx::kSpriteWhite);
            } else {
                ctx.FillRect(r.x, r.y, r.w, r.h, kColSlotBg);   // fallback if the icon fails to load
            }

            // Highlight unk_94D970: `dword_1675800 == i && dword_1675804 > 0`
            // (EA 0x5CE6B8) — overlay sprite in the binary, colored frame here.
            if (invSelSlot == i && invSelCount > 0)
                ctx.DrawFrame(r.x, r.y, r.w, r.h, kColSelInv, 2);   // EA 0x5CE6CA

            // Highlight unk_947A0C: `dword_16760E0 == i` (EA 0x5CE6D7).
            if (beltSelSlot == i)
                ctx.DrawFrame(r.x, r.y, r.w, r.h, kColSelBelt, 2);  // EA 0x5CE6E9
        }

        // --- Buttons -------------------------------------------------------
        // FIDELITY: the binary only draws a button if it's pressed ("down"
        // sprite) or hovered ("up" sprite) — the idle state is NOT drawn,
        // since its visual belongs to the BACKGROUND sprite. We reproduce this
        // logic (nothing at idle) while keeping a visible fallback rectangle for
        // when the .IMG background couldn't be loaded... which we can't
        // distinguish here: so we draw the idle button in a neutral tint, an
        // ACCEPTED and documented deviation (without it, buttons would be
        // invisible against the fallback background).
        const bool actHover = PointInRect(cursorX, cursorY, act.x, act.y, act.w, act.h);
        ctx.FillRect(act.x, act.y, act.w, act.h,
                     actionLatch_ ? kColBtnDown : (actHover ? kColBtnHover : kColBtnBg)); // EA 0x5CE7C3 / 0x5CE784
        ctx.DrawFrame(act.x, act.y, act.w, act.h, kColFrame, 1);

        const bool clsHover = PointInRect(cursorX, cursorY, cls.x, cls.y, cls.w, cls.h);
        ctx.FillRect(cls.x, cls.y, cls.w, cls.h,
                     closeLatch_ ? kColBtnDown : (clsHover ? kColBtnHover : kColBtnBg));  // EA 0x5CE898 / 0x5CE859
        ctx.DrawFrame(cls.x, cls.y, cls.w, cls.h, kColFrame, 1);
        return;
    }

    // --- Text phase --------------------------------------------------------
    ctx.Text("Ceinture", panel.x + 8, panel.y + 6, kColTitle);

    // Second loop over the 10 slots (EA 0x5CE89D) — note its guard is
    // `if (g_AutoPotionBelt[i])` (!= 0, EA 0x5CE8BC), NOT `>= 1` like the
    // icon loop: a negative itemID would therefore show its counter without
    // an icon. Reproduced as-is.
    for (int i = 0; i < kSlots; ++i) {
        if (BeltItemId(i) == 0) continue;                       // EA 0x5CE8BC

        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d / %d",              // aD D « %d / %d » (EA 0x5CE8E1)
                      static_cast<int>(BeltCharges(i)), kMaxCharges);

        // v22 = x + 55*(i%5) + 44 - UI_MeasureNumberText(buf)/2  (EA 0x5CE8FF..0x5CE91E)
        // v26 = y + 55*(i/5) + 77                                 (EA 0x5CE93F)
        // UI_DrawNumberValue(buf, v22, v26, 1) — color 1 (EA 0x5CE95B).
        const int cx = x_ + kSlotPitch * (i % kSlotCols) + kCountDx;
        const int cy = y_ + kSlotPitch * (i / kSlotCols) + kCountDy;
        ctx.Text(buf, cx - ctx.MeasureText(buf) / 2, cy, kColText);
    }

    // Fallback labels for the two buttons (the binary writes NO text: the
    // labels are part of sprites unk_977498 / unk_9776E8). Accepted deviation,
    // necessary until the sprites are resolved.
    const char* actLbl = "Assigner";
    ctx.Text(actLbl, act.x + (act.w - ctx.MeasureText(actLbl)) / 2, act.y + (act.h - 12) / 2, kColText);
    const char* clsLbl = "Fermer";
    ctx.Text(clsLbl, cls.x + (cls.w - ctx.MeasureText(clsLbl)) / 2, cls.y + (cls.h - 12) / 2, kColText);
}

} // namespace ts2::ui

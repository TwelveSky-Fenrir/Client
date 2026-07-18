// UI/CharacterStatsWindow.cpp — implementation of the character sheet.
// See UI/CharacterStatsWindow.h for RE references (StatFormulas.h,
// GameState.h, Net_SendVaultReq_206 0x590430 emission -> opcode 0x13 / sub-code 206).
#include "UI/CharacterStatsWindow.h"
#include "UI/PanelSkin.h"
// Attribute point spend emission (cDrawWin_OnCommit 0x6291F0):
// Net_SendVaultReq_206 (Net/SendPackets.h) + g_NetClient singleton 0x8156A0 restored
// via net::GlobalNetClient() (Net/NetClient.h) + locks g_GmCmdCooldownLatch
// 0x1675B08 / g_MorphInProgress 0x1675A88 / flt_1675B0C 0x1675B0C (Net/ClientState.h).
// NB: Net/SendPackets.h already includes NetClient.h and ClientState.h.
#include "Net/SendPackets.h"

#include <cstdio>

namespace ts2::ui {

// Palette (ARGB, D3DCOLOR = 0xAARRGGBB) — cf. UI contract.
namespace {

// Real panel background (best effort): (446,440) template from the UI atlas
// folder G03_GDATA/D01_GIMAGE2D/001 — candidate NOT CONFIRMED by IDA, chosen
// by aspect-ratio proximity to the character sheet (480x380; cf. detailed
// methodology in UI/PanelSkin.h). Distinct index from the one used by
// GuildWindow (same size cluster, different files). Automatic fallback to
// kColBg if missing.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01338.IMG");

constexpr D3DCOLOR kColBg        = Argb(0xE0, 0x20, 0x20, 0x28); // panel background
constexpr D3DCOLOR kColTitleBg   = Argb(0xF0, 0x18, 0x18, 0x20); // title bar (darker)
constexpr D3DCOLOR kColFrame     = Argb(0xFF, 0x80, 0x80, 0x80); // frame
constexpr D3DCOLOR kColText      = Argb(0xFF, 0xFF, 0xFF, 0xFF); // normal text
constexpr D3DCOLOR kColTitle     = Argb(0xFF, 0xFF, 0xDD, 0x66); // title
constexpr D3DCOLOR kColLabel     = Argb(0xFF, 0xC0, 0xC0, 0xC8); // labels (light gray)
constexpr D3DCOLOR kColHover     = Argb(0xFF, 0x40, 0x60, 0xA0); // hover
constexpr D3DCOLOR kColBtn       = Argb(0xFF, 0x38, 0x40, 0x50); // normal button
constexpr D3DCOLOR kColBtnDown   = Argb(0xFF, 0x58, 0x84, 0xC8); // pressed button
constexpr D3DCOLOR kColHp        = Argb(0xFF, 0xE0, 0x40, 0x40); // "HP" tint
constexpr D3DCOLOR kColMp        = Argb(0xFF, 0x40, 0x60, 0xE0); // "MP" tint
constexpr D3DCOLOR kColUnspent   = Argb(0xFF, 0x80, 0xE0, 0x80); // unspent points (green)
constexpr D3DCOLOR kColDivider   = Argb(0xFF, 0x50, 0x50, 0x58); // separator line

// --- Geometry constants ---
constexpr int kBoxW      = 480;
constexpr int kBoxH      = 380;
constexpr int kTitleH    = 28;
constexpr int kCloseSize = 18;
constexpr int kRowH      = 24;
constexpr int kPlusSize  = 18;

// 2x2 grid of primary attributes — REAL (cDrawWin_Draw 0x629C9E/0x629D66,
// cf. .h's CONFIRMED_FAITHFUL banner): values right-aligned at (+107,+110)
// and (+203,+110)/(+107,+132)/(+203,+132) from the panel origin.
constexpr int kAttrRowY0      = 110; // top row (ExtForce / IntForce)
constexpr int kAttrRowY1      = 132; // bottom row (Defensive / Offensive)
constexpr int kAttrValueColX0 = 107; // left column (right edge of value text)
constexpr int kAttrValueColX1 = 203; // right column

constexpr int kStatsStartYOff = 200; // from box.y (below the separator) — not re-audited this pass
} // namespace

// Primary attribute labels / accessors
const char* CharacterStatsWindow::AttrLabel(PrimaryAttr a) {
    switch (a) {
        case PrimaryAttr::ExtForce:  return "Force Externe";
        case PrimaryAttr::IntForce:  return "Force Interne";
        case PrimaryAttr::Defensive: return "Défensif";
        case PrimaryAttr::Offensive: return "Offensif";
    }
    return "?";
}

int CharacterStatsWindow::AttrValue(const game::SelfState& s, PrimaryAttr a) {
    switch (a) {
        case PrimaryAttr::ExtForce:  return s.attrExtForce;
        case PrimaryAttr::IntForce:  return s.attrIntForce;
        case PrimaryAttr::Defensive: return s.attrDefensive;
        case PrimaryAttr::Offensive: return s.attrOffensive;
    }
    return 0;
}

// Layout — real PROPORTIONAL ANCHOR (CONFIRMED_FAITHFUL, cf. .h banner):
// NOT screen centering. Reproduces exactly UI_ProjectSpriteToScreen 0x50F5D0
// as called by cDrawWin_Draw/cDrawWin_OnMouseDown (0x6299AA/0x628ED0):
//   x = round((kDesignAnchorX + w/2) * screenW / kRefWidth)  - w/2
//   y = round((kDesignAnchorY + h/2) * screenH / kRefHeight) - h/2
// where w/h = panel dimensions (kBoxW/kBoxH; real values not confirmed, cf. .h).
// At the reference resolution (kRefWidth x kRefHeight), reduces exactly to
// (x,y) = (kDesignAnchorX, kDesignAnchorY) = (115,105) — TOP-LEFT corner of
// the screen, not the center.
void CharacterStatsWindow::ComputeLayout(int screenW, int screenH, Layout& L) const {
    L.box.w = kBoxW;
    L.box.h = kBoxH;

    // Formula identical to UI_ProjectSpriteToScreen 0x50F5D0 (anchors the
    // panel's CENTER to the same screen fraction as its design position,
    // panel pixel size not scaled).
    const long long centerXNum = static_cast<long long>(kDesignAnchorX + kBoxW / 2) * screenW;
    const long long centerYNum = static_cast<long long>(kDesignAnchorY + kBoxH / 2) * screenH;
    const int centerX = static_cast<int>(centerXNum / ts2::kRefWidth);
    const int centerY = static_cast<int>(centerYNum / ts2::kRefHeight);
    L.box.x = centerX - kBoxW / 2;
    L.box.y = centerY - kBoxH / 2;

    L.titleBar = Rect{ L.box.x, L.box.y, L.box.w, kTitleH };

    // Real close button: fixed offset (8,6) from the panel origin
    // (TOP-LEFT), cf. cDrawWin_OnMouseDown 0x629188.
    L.closeBtn = Rect{ L.box.x + kCloseOffX, L.box.y + kCloseOffY,
                        kCloseSize, kCloseSize };

    // Real "+1" and "+5" buttons: two fixed 2x2 grids (NOT a column), same
    // rows (kPlusOffY), distinct columns (kPlusOffX / kPlus5OffX) — cf. .h
    // banner (cDrawWin_OnMouseDown 0x628F02.. for "+1", 0x62904D.. for "+5").
    for (int i = 0; i < kPrimaryAttrCount; ++i) {
        const int col = i % 2;
        const int row = i / 2;
        L.plusBtn[i]  = Rect{ L.box.x + kPlusOffX[col],  L.box.y + kPlusOffY[row],
                              kPlusSize, kPlusSize };
        L.plus5Btn[i] = Rect{ L.box.x + kPlus5OffX[col], L.box.y + kPlusOffY[row],
                              kPlusSize, kPlusSize };
    }
}

// Emission — common block for all 8 buttons ("+1" args 1..4 / "+5" args 5..8).
//
// Reproduces exactly the body of each cDrawWin_OnCommit 0x6291F0 branch.
// Reference branch ("+1 Force Externe", arg=1):
//     if (g_MorphInProgress == 1) return 1;        // 0x629276 — SILENT refusal
//     if (g_GmCmdCooldownLatch)   return 1;        // 0x629289 — SILENT refusal
//     Net_SendVaultReq_206(1);                     // 0x62929C
//     g_GmCmdCooldownLatch = 1;                    // 0x6292A1
//     flt_1675B0C = g_GameTimeSec;                 // 0x6292B1
//     --g_SelfUnspentAttrPoints;                   // 0x6292C0
// The 7 other branches are identical, differing only in the (arg, cost) pair:
//   arg 2 -> 0x62934A/0x62936E   arg 3 -> 0x6293F8/0x62941C   arg 4 -> 0x6294A9/0x6294CD
//   arg 5 -> 0x629554/0x629578   arg 6 -> 0x629602/0x629626   arg 7 -> 0x6296B0/0x6296D4
//   arg 8 -> 0x629761/0x629785   (args 5..8: g_SelfUnspentAttrPoints -= 5)
//
// OPTIMISTIC LOCAL EFFECT — INTENTIONAL AND PROVEN: the decrement of
// g_SelfUnspentAttrPoints (0x16731D0 == SelfState::unspentAttr) IS done HERE,
// immediately, in the same block as the send. The previous pass's comment
// ("we do NOT optimistically decrement self.unspentAttr") was WRONG relative
// to the binary; it was only consistent as long as nothing was emitted. The
// attribute VALUES themselves are NOT touched locally: they come back from the
// server (Net_OnCultivationDispatch 0x493180 / Pkt_CharStatDelta 0x465D90),
// which also resets g_GmCmdCooldownLatch to 0 (cf. Net/GameHandlers_Misc.cpp,
// dispatch 0x58) — that's what unlocks the button.
//
// The binary does NOT re-test g_SelfUnspentAttrPoints here: the test (> 0 for
// "+1", >= 5 for "+5") only happens at ARMING time (cDrawWin_OnMouseDown
// 0x628EDC / 0x629027). We therefore don't add it either, including in the
// edge case where the counter would drop to 0 between press and release (the
// binary would send and decrement anyway).
void CharacterStatsWindow::CommitAttrSpend(int arg, int cost) {
    if (net::g_MorphInProgress == 1) return; // 0x629276 — morph in progress: silent refusal
    if (net::g_GmCmdCooldownLatch)  return; // 0x629289 — request already in flight: silent refusal

    // g_NetClient 0x8156A0 is a GLOBAL in the binary: Net_SendVaultReq_206
    // (0x590430) receives no socket, it calls Net_SendPacket_Op19(&g_AutoPlayMgr,
    // 206, &arg) which addresses the network client directly. On the C++ side the
    // singleton is restored by net::GlobalNetClient() (Net/NetClient.h:67-68), set
    // by ConnectGameServer (Net/Login.cpp). This path is actually reached: the
    // character sheet can only be opened once in the world, so well after the
    // handshake -> the pointer is non-null (this is NOT dead `if (nc)` code).
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return; // out of session: no send -> therefore no decrement either

    // Sub-code 206, payload[0..3] = arg int32 LE (the builder promotes the char to
    // 4 bytes, cf. Crt_Memcpy(v2, &a1, 4u) 0x590454).
    net::Net_SendVaultReq_206(*nc, static_cast<int8_t>(arg)); // 0x62929C
    net::g_GmCmdCooldownLatch = 1;                            // 0x6292A1
    // TODO [anchor 0x815180] timestamp written with net::g_GameTimeSec (Net/ClientState.h:12)
    // for CONSISTENCY with all other guarded emitters (Net_SendGuarded_* use this
    // same symbol)... but this symbol is a STUB never fed (always 0.0f). The
    // binary has only ONE g_GameTimeSec (flt_815180); the rewrite has TWO that
    // both claim to model it: net::g_GameTimeSec (dead) and gfx::g_GameTimeSec
    // (Gfx/SpriteBatch.cpp:11, ACTUALLY fed by App.cpp:630 = gameClockSec_).
    // flt_1675B0C therefore starts at 0 instead of the game clock. No effect on
    // THIS front (the button unlock comes from the latch, reset by the inbound
    // dispatch 0x58, not from a timeout), but flt_1675B0C IS read elsewhere
    // (AutoPlay_CheckReturnScroll 0x45C8E1, Game_OnHotkey 0x537B7E,
    // Npc_AutoSelectNearest 0x53AD47…). Fix = merge the two symbols in
    // Net/ClientState.h — a file NOT owned by this front-end, flagged in the report.
    net::flt_1675B0C = net::g_GameTimeSec;                    // 0x6292B1
    game::g_World.self.unspentAttr -= cost;                   // 0x6292C0 (-1) / 0x629578 (-5)
}

// Lifecycle
void CharacterStatsWindow::Open() {
    Dialog::Open();
    closeArmed_ = false;
    for (bool& b : plusArmed_)  b = false;
    for (bool& b : plus5Armed_) b = false;
}

// Mouse
// Arming — EXACT ORDER of cDrawWin_OnMouseDown 0x628EA0: the 4 "+1" first
// (gate g_SelfUnspentAttrPoints > 0, 0x628EDC), then the 4 "+5" (gate >= 5, 0x629027:
// `cmp` then jump over the 4 tests), then the close button (0x629188), then
// the panel background (0x6291D3). Order matters: the "+1"/"+5" rects
// overlap by 3 px with kPlusSize=18 (cf. .h's TODO) — testing "+1" first
// attributes the shared zone to "+1", like the binary.
// NB: the binary also plays a click sound on every arm (Snd3D_PlayScaledVolume
// (flt_1487E3C, 0, 100, 1), 0x628F16 and following) — not reproduced here, this
// window has no audio access (out of front-end scope, no packet involved).
bool CharacterStatsWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false; // *(this+2) == 0 -> return 0 (0x628EAA)

    Layout L;
    ComputeLayout(lastScreenW_, lastScreenH_, L);

    const game::SelfState& self = game::g_World.self;

    if (self.unspentAttr > 0) { // 0x628EDC
        for (int i = 0; i < kPrimaryAttrCount; ++i) {
            const Rect& r = L.plusBtn[i];
            if (PointInRect(x, y, r.x, r.y, r.w, r.h)) { // 0x628F02..0x628FF3
                plusArmed_[i] = true;                     // *(this+3..+6) = 1
                return true;
            }
        }
    }

    if (self.unspentAttr >= 5) { // 0x629027 (`cmp g_SelfUnspentAttrPoints, 5` / `jl`)
        for (int i = 0; i < kPrimaryAttrCount; ++i) {
            const Rect& r = L.plus5Btn[i];
            if (PointInRect(x, y, r.x, r.y, r.w, r.h)) { // 0x62904D..0x62913E
                plus5Armed_[i] = true;                    // *(this+7..+10) = 1
                return true;
            }
        }
    }

    if (PointInRect(x, y, L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h)) { // 0x629188
        closeArmed_ = true;                                                           // *(this+11) = 1
        return true;
    }

    // Click anywhere else in the panel: consumed (prevents the click from
    // "passing through" to the 3D world behind the window) but arms nothing — the
    // binary returns here the hit-test of the unk_8F3704 background sprite (0x6291D3).
    if (PointInRect(x, y, L.box.x, L.box.y, L.box.w, L.box.h)) return true;

    return false;
}

// Commit — if/else-if chain of cDrawWin_OnCommit 0x6291F0, in its exact order:
// *(this+3..+6) = "+1" (args 1..4), *(this+7..+10) = "+5" (args 5..8), *(this+11) =
// close. The binary only processes ONE latch per call (else-if chain) and
// disarms the tested latch BEFORE re-hit-testing; the click is consumed
// (return 1) whether the release lands back on the button or not (0x629265 vs
// 0x62928B).
// The button -> arg mapping is PROVEN by the geometry: cDrawWin_Draw draws
// each attribute's value right next to its button — Char_SumAttrField292/296/
// 300/304 at (107,110)/(203,110)/(107,132)/(203,132) (0x629C76/0x629CD7/0x629D3B/
// 0x629D9F), i.e. the PrimaryAttr order {ExtForce=0, IntForce=1, Defensive=2,
// Offensive=3} mapped to a grid (col=i%2, row=i/2) => arg = i+1 ("+1") / i+5 ("+5").
bool CharacterStatsWindow::OnClick(int x, int y) {
    if (!bOpen_) return false; // *(this+2) == 0 -> return 0 (0x6291FA)

    Layout L;
    ComputeLayout(lastScreenW_, lastScreenH_, L);

    // "+1": args 1..4, cost 1 (emissions 0x62929C/0x62934A/0x6293F8/0x6294A9).
    for (int i = 0; i < kPrimaryAttrCount; ++i) {
        if (!plusArmed_[i]) continue;
        plusArmed_[i] = false;                              // *(this+3..+6) = 0 (0x629235)
        const Rect& r = L.plusBtn[i];
        if (PointInRect(x, y, r.x, r.y, r.w, r.h))          // 0x62925C — re-hit-test on release
            CommitAttrSpend(i + 1, 1);
        return true;                                        // consumed either way
    }

    // "+5": args 5..8, cost 5 (emissions 0x629554/0x629602/0x6296B0/0x629761).
    for (int i = 0; i < kPrimaryAttrCount; ++i) {
        if (!plus5Armed_[i]) continue;
        plus5Armed_[i] = false;                             // *(this+7..+10) = 0 (0x6294ED)
        const Rect& r = L.plus5Btn[i];
        if (PointInRect(x, y, r.x, r.y, r.w, r.h))          // 0x629514
            CommitAttrSpend(i + 5, 5);
        return true;
    }

    if (closeArmed_) {                                      // *(this+11) (0x629795)
        closeArmed_ = false;                                // 0x62979E
        if (PointInRect(x, y, L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h)) // 0x6297C5
            Close();                                        // cDrawWin_Close 0x628E80
        return true;                                        // 0x6297CE
    }

    // Released anywhere in the panel: consumed.
    if (PointInRect(x, y, L.box.x, L.box.y, L.box.w, L.box.h)) return true;

    return false;                                           // 0x6297E4
}

bool CharacterStatsWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) {
        Close();
        return true;
    }
    return false;
}

// Rendering
void CharacterStatsWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Caches the current screen dims so the hit-test (routed across two
    // frames) aligns with the actually drawn geometry. Done in both
    // sub-passes (Panels then Text), like MsgBoxDialog.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Layout L;
    ComputeLayout(ctx.screenW, ctx.screenH, L);

    const game::SelfState& self = game::g_World.self;
    // Draw gates for both button sets, identical to the arming gates:
    // "+1" if g_SelfUnspentAttrPoints > 0, "+5" if >= 5 (cDrawWin_Draw 0x62A3D3,
    // cDrawWin_OnMouseDown 0x628EDC / 0x629027).
    const bool hasPoints  = self.unspentAttr > 0;
    const bool hasPoints5 = self.unspentAttr >= 5;

    char buf[96];

    if (ctx.phase == UiPhase::Panels) {
        // --- Background + frame + title bar ---
        kPanelBg.Draw(ctx, L.box.x, L.box.y, L.box.w, L.box.h, kColBg);
        ctx.FillRect(L.titleBar.x, L.titleBar.y, L.titleBar.w, L.titleBar.h, kColTitleBg);
        ctx.DrawFrame(L.box.x, L.box.y, L.box.w, L.box.h, kColFrame, 2);
        ctx.FillRect(L.box.x, L.box.y + kTitleH, L.box.w, 1, kColDivider);

        // --- Close button ---
        const bool closeHover = PointInRect(cursorX, cursorY, L.closeBtn.x, L.closeBtn.y,
                                            L.closeBtn.w, L.closeBtn.h);
        const D3DCOLOR closeCol = closeArmed_ ? kColBtnDown : (closeHover ? kColHover : kColBtn);
        ctx.FillRect(L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h, closeCol);
        ctx.DrawFrame(L.closeBtn.x, L.closeBtn.y, L.closeBtn.w, L.closeBtn.h, kColFrame, 1);

        // --- Separator before the derived stats ---
        const int sepY = L.box.y + kStatsStartYOff - 12;
        ctx.FillRect(L.box.x + 16, sepY, L.box.w - 32, 1, kColDivider);

        // --- "+1" buttons per primary attribute (only if points remain) ---
        if (hasPoints) {
            for (int i = 0; i < kPrimaryAttrCount; ++i) {
                const Rect& r = L.plusBtn[i];
                const bool hover = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
                const D3DCOLOR col = plusArmed_[i] ? kColBtnDown : (hover ? kColHover : kColBtn);
                ctx.FillRect(r.x, r.y, r.w, r.h, col);
                ctx.DrawFrame(r.x, r.y, r.w, r.h, kColFrame, 1);
            }
        }
        // --- "+5" buttons (sprite unk_940260): only drawn once unspent
        // points reach 5 (cDrawWin_Draw 0x62A3D3) ---
        if (hasPoints5) {
            for (int i = 0; i < kPrimaryAttrCount; ++i) {
                const Rect& r = L.plus5Btn[i];
                const bool hover = PointInRect(cursorX, cursorY, r.x, r.y, r.w, r.h);
                const D3DCOLOR col = plus5Armed_[i] ? kColBtnDown : (hover ? kColHover : kColBtn);
                ctx.FillRect(r.x, r.y, r.w, r.h, col);
                ctx.DrawFrame(r.x, r.y, r.w, r.h, kColFrame, 1);
            }
        }
        return;
    }

    // --- Text phase -----------------------------------------------------
    // Title centered in the bar.
    const int titleW = ctx.MeasureText("Personnage");
    ctx.Text("Personnage", L.box.x + (L.box.w - titleW) / 2, L.titleBar.y + 6, kColTitle);
    ctx.Text("X", L.closeBtn.x + 5, L.closeBtn.y + 2, kColText);

    // Level line + unspent points.
    std::snprintf(buf, sizeof(buf), "Niveau %d", self.level);
    ctx.Text(buf, L.box.x + 20, L.box.y + kTitleH + 12, kColText);
    std::snprintf(buf, sizeof(buf), "Points non dépensés : %d", self.unspentAttr);
    ctx.Text(buf, L.box.x + 180, L.box.y + kTitleH + 12,
             hasPoints ? kColUnspent : kColLabel);

    // Primary attributes — REAL grid, 2 columns x 2 rows (cf. .h's
    // CONFIRMED_FAITHFUL banner: cDrawWin_Draw draws the 4 values at
    // (+107,+110)/(+203,+110)/(+107,+132)/(+203,+132) right-aligned, "+1"
    // button just left of each column). The text label (absent from the
    // original binary — probably baked into the background bitmap) remains a
    // pragmatic addition, positioned right before each value.
    for (int i = 0; i < kPrimaryAttrCount; ++i) {
        const auto attr = static_cast<PrimaryAttr>(i);
        const int col = i % 2;
        const int row = i / 2;
        const int y = L.box.y + (row == 0 ? kAttrRowY0 : kAttrRowY1);
        const int valueRightX = L.box.x + (col == 0 ? kAttrValueColX0 : kAttrValueColX1);

        std::snprintf(buf, sizeof(buf), "%d", AttrValue(self, attr));
        const int vw = ctx.MeasureText(buf);
        ctx.Text(buf, valueRightX - vw, y, kColText);

        std::snprintf(buf, sizeof(buf), "%s :", AttrLabel(attr));
        const int lw = ctx.MeasureText(buf);
        ctx.Text(buf, valueRightX - vw - lw - 4, y, kColLabel);

        if (hasPoints) {
            const Rect& r = L.plusBtn[i];
            const int plusW = ctx.MeasureText("+");
            ctx.Text("+", r.x + (r.w - plusW) / 2, r.y + 1, kColText);
        }
        if (hasPoints5) {
            const Rect& r = L.plus5Btn[i];
            const int plus5W = ctx.MeasureText("5");
            ctx.Text("5", r.x + (r.w - plus5W) / 2, r.y + 1, kColText);
        }
    }

    // Derived stats — 2 columns x 6 rows (byte-exact, read from SelfState,
    // computed by StatEngine::Recompute via Game/StatFormulas.h/.cpp).
    struct StatRow { const char* label; int value; D3DCOLOR color; };
    const StatRow col1[6] = {
        { "Vie Max",            self.maxHp,       kColHp   },
        { "Mana Max",           self.maxMp,       kColMp   },
        { "Attaque Externe",    self.extAtk,      kColText },
        { "Attaque Interne",    self.intAtk,      kColText },
        { "Défense Externe",    self.extDef,      kColText },
        { "Défense Interne",    self.intDef,      kColText },
    };
    const StatRow col2[6] = {
        { "Précision",          self.accuracy,     kColText },
        { "Esquive",            self.evasion,      kColText },
        { "Taux Critique",      self.critRate,     kColText },
        { "Rating Att. Min",    self.atkRatingMin, kColText },
        { "Rating Att. Max",    self.atkRatingMax, kColText },
        { "Vitesse Attaque",    self.attackSpeed,  kColText },
    };

    const int col1LabelX = L.box.x + 20;
    const int col1ValueX = L.box.x + 210;
    const int col2LabelX = L.box.x + 250;
    const int col2ValueX = L.box.x + 445;

    for (int i = 0; i < 6; ++i) {
        const int y = L.box.y + kStatsStartYOff + i * kRowH;

        std::snprintf(buf, sizeof(buf), "%s :", col1[i].label);
        ctx.Text(buf, col1LabelX, y, kColLabel);
        std::snprintf(buf, sizeof(buf), "%d", col1[i].value);
        const int v1w = ctx.MeasureText(buf);
        ctx.Text(buf, col1ValueX - v1w, y, col1[i].color);

        std::snprintf(buf, sizeof(buf), "%s :", col2[i].label);
        ctx.Text(buf, col2LabelX, y, kColLabel);
        std::snprintf(buf, sizeof(buf), "%d", col2[i].value);
        const int v2w = ctx.MeasureText(buf);
        ctx.Text(buf, col2ValueX - v2w, y, col2[i].color);
    }
}

} // namespace ts2::ui

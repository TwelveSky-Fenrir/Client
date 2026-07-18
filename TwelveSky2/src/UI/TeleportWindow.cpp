// UI/TeleportWindow.cpp — page 76 of cNpcWin: paid teleportation (service code 76).
// See UI/TeleportWindow.h for the full anchoring and call-chain details.
//
// Include order: Net/ FIRST (NetClient.h pulls <winsock2.h> before <windows.h>) —
// same rule as UI/NpcDialogWindow.cpp and UI/GuildWindow.cpp.
#include "Net/SendPackets.h"      // net::Net_SendWarpRequest (i32 alias of Op20 0x4B5000)
#include "Net/NetClient.h"        // net::GlobalNetClient() == &g_NetClient 0x8156A0
#include "Net/Rng.h"              // net::DefaultRng() (Rng_Next 0x7603FD)
#include "UI/TeleportWindow.h"
#include "UI/PanelSkin.h"
#include "Game/ClientRuntime.h"   // game::g_Client (Var/VarF/VarGet/msg), game::Str(id)
#include "Game/GameState.h"       // game::g_World.self (level/levelBonus/element)
#include "Game/MapWarp.h"         // game::WarpAddr::*
#include "Game/MotionPools.h"     // game::GetFrameRange (mirror of g_MotionFrameRangeTable 0x14A9350)
#include "Game/StringTables.h"    // game::g_Strings.zoneNames (StrTable003 / mZONENAME)

#include <cstdio>                 // std::snprintf (row label, best-effort)
#include <string>

namespace ts2::ui {

namespace {

// Panel background (best-effort): the real background is sprite `unk_8FE3E0` from the UI atlas
// (cTeleportWin_Draw 0x628030 @0x6280b3 `Sprite2D_Draw(&unk_8FE3E0, *this, *(this+1))`) —
// atlas slot not resolved on the ClientSource side, see the dims TODO in the .h. Falls back to kColBg.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_02463.IMG");

// g_SysMsgColor 0x84DFD8 — not modeled as a dedicated field; long tail via Var(), same
// convention as UI/NpcDialogWindow.cpp:32 and Game/SocialSystem.cpp:69.
constexpr uint32_t kSysMsgColorAddr = 0x84dfd8u;

// dword_167589C — scalar read by the guard @0x627f0e (`if (dword_167589C >= 1)`), set by
// Net/GameVarDispatch.cpp cases 148/149. Long tail via Var().
constexpr uint32_t kWarpEnableVarAddr = 0x167589Cu;

// Destination coordinates — a SINGLE constant in the binary. PROVEN in IDA:
// Motion_GetComboOffsetTable 0x5025E0 is a switch(element 0..3) where every branch contains
// the same 4 mapId cases {313,316,331,334}; all 16 cases (4 elements x 4 mapIds) write
// the SAME triplet and return 1:
//   arg2[0] = flt_7A9144 = 0x45262000 = 2658.0   (EA type 0x5037da/0x503807/... , 16 sites)
//   arg2[1] = flt_7ED9FC = 0x40000000 =    2.0
//   arg2[2] = flt_7A9140 = 0x43C18000 =  387.0
// No dependency on element, mapId, or clan state (contrast: case 291 of the
// same function does compare byte_1686138/dword_16746A8). The coordinate resolver is
// therefore NOT a blocker for this page — the value is hardcoded. Verified case by case (get_bytes).
constexpr float kTeleportDestPos[3] = { 2658.0f, 2.0f, 387.0f };

} // namespace

TeleportWindow::TeleportWindow() {
    x_ = (ts2::kRefWidth  - kPanelW) / 2;
    y_ = (ts2::kRefHeight - kPanelH) / 2;
}

// ============================================================================
// cTeleportWin_GetDestMapId 0x6282D0 — slot 0..3 -> mapId, default 0.
// ============================================================================
int32_t TeleportWindow::DestMapId(int slot) {
    switch (slot) {
    case 0: return 313; // 0x6282ef
    case 1: return 316; // 0x6282f6
    case 2: return 331; // 0x6282fd
    case 3: return 334; // 0x628304
    default: return 0;  // 0x62830b
    }
}

// ============================================================================
// Lifecycle — cTeleportWin_Init 0x627BA0 / UI_NpcWin_CloseRestore 0x5DC1F0
// ============================================================================
void TeleportWindow::Open() {
    // 0x627bac: *(this+180) = 76 (page id). This class IS page 76: no id field.
    // 0x627bb6: for(i<100) *(this+i+70) = 0 — clears the 100 latches; only close (+70) and
    // the 4 rows (+71..74) are used here.
    closeLatch_ = false;
    for (int i = 0; i < kSlotCount; ++i) slotLatch_[i] = false;
    Dialog::Open();          // *(this+3)=1 equivalent (visible)
}

void TeleportWindow::Close() {
    // The real closing goes through UI_NpcWin_CloseRestore 0x5DC1F0 (emits NOTHING; restores
    // material slots if needed — moot for this page, which deposits nothing).
    closeLatch_ = false;
    for (int i = 0; i < kSlotCount; ++i) slotLatch_[i] = false;
    Dialog::Close();
}

// ============================================================================
// Geometry
// ============================================================================
void TeleportWindow::Recenter(int screenW, int screenH) {
    // *this     = nWidth/2  - Sprite2D_GetWidth(&unk_8FE3E0)/2
    // *(this+1) = nHeight/2 - Sprite2D_GetHeight(&unk_8FE3E0)/2   (dims -> kPanelW/H)
    x_ = screenW / 2 - kPanelW / 2;
    y_ = screenH / 2 - kPanelH / 2;
}

bool TeleportWindow::RowHit(int i, int mx, int my) const {
    // STRICT inequalities from the binary (OnCommit 0x627e4c..0x627e85, OnMouseDown same):
    //   a2 > *this+37 && a2 < *this+217 && a3 > *(this+1)+18*i+26 && a3 < *(this+1)+18*i+38
    return mx > x_ + 37 && mx < x_ + 217
        && my > y_ + 18 * i + 26 && my < y_ + 18 * i + 38;
}

bool TeleportWindow::CloseButtonHit(int mx, int my) const {
    // Sprite2D_HitTest(&unk_8F3798, *this+235, *(this+1)+4, mx, my) — sprite dims -> kCloseW/H.
    return PointInRect(mx, my, x_ + 235, y_ + 4, kCloseW, kCloseH);
}

// ============================================================================
// System messages
// ============================================================================
void TeleportWindow::SysMsg(int strId) {
    const uint32_t sysColor = static_cast<uint32_t>(game::g_Client.VarGet(kSysMsgColorAddr));
    game::g_Client.msg.System(game::Str(strId), sysColor);
}

// ============================================================================
// Arming + Op20 emission (mode 6) — tail of cTeleportWin_OnCommit 0x627f65..0x628008.
// Body IDENTICAL to UI/NpcDialogWindow.cpp::ArmWarpAndSendOp20 (mode 6), transcribed locally
// (the binary has no shared function; ArmFullWarp from MapWarp.cpp is not exported).
// ============================================================================
void TeleportWindow::ArmWarpAndSendOp20(int32_t zoneId, const float pos[3]) {
    using namespace ts2::game;
    g_Client.Var (WarpAddr::MorphInProgress) = 1;      // 0x627f65 : g_MorphInProgress = 1
    g_Client.Var (WarpAddr::WarpModeCode)    = 6;      // 0x627f6f : dword_1675A8C = 6
    g_Client.Var (WarpAddr::WarpSub)         = 0;      // 0x627f79 : dword_1675A90 = 0
    g_Client.Var (WarpAddr::WarpTargetNpc)   = zoneId; // 0x627f86 : g_TargetZoneId = mapId
    // 0x627f95: Crt_Memset(&dword_1675AA0, 0, 0x48) — not reproducible on the Var map (not
    // a contiguous memory image); only the fields REWRITTEN right after are set, no
    // ClientSource reader depends on the 0x1675ACC..0x1675AE4 leftover. Same tradeoff and same
    // justification as UI/NpcDialogWindow.cpp and Game/MapWarp.cpp::ArmFullWarp.
    g_Client.Var (WarpAddr::WarpFlagA0)      = 0;      // 0x627f9d : dword_1675AA0 = 0
    g_Client.Var (WarpAddr::WarpFlagA4)      = 1;      // 0x627fa7 : dword_1675AA4 = 1
    g_Client.VarF(WarpAddr::WarpDelay)       = 0.0f;   // 0x627fb3 : flt_1675AA8 = 0.0
    g_Client.VarF(WarpAddr::WarpPosX)        = pos[0]; // 0x627fbc : flt_1675AAC = v13[0]
    g_Client.VarF(WarpAddr::WarpPosY)        = pos[1]; // 0x627fc5 : flt_1675AB0 = v13[1]
    g_Client.VarF(WarpAddr::WarpPosZ)        = pos[2]; // 0x627fce : flt_1675AB4 = v13[2]
    // 0x627fe7: flt_1675AC4 = (float)(Rng_Next() % 360) — a SINGLE draw, copied. Drawn BEFORE
    // Op20's 4 internal nonces: the shared RNG order is preserved.
    const float facing = static_cast<float>(ts2::net::DefaultRng().NextMod(360));
    g_Client.VarF(WarpAddr::WarpFacingA)     = facing; // 0x627fe7 : flt_1675AC4
    g_Client.VarF(WarpAddr::WarpFacingB)     = facing; // 0x627ff3 : flt_1675AC8 = flt_1675AC4

    // 0x628008: Net_SendPacket_Op20(&g_AutoPlayMgr, dword_1675A8C /*=6*/, mapId). The singleton
    // g_NetClient 0x8156A0 is read as a GLOBAL by 0x4B5000 (no socket passed). The i32 ALIAS
    // (Net_SendWarpRequest) is MANDATORY: mapIds 313/316/331/334 exceed 127 and
    // would be sign-extended by the int8_t builder (see Net/SendPackets.h:257-265).
    if (ts2::net::NetClient* client = ts2::net::GlobalNetClient())
        ts2::net::Net_SendWarpRequest(*client, /*warpModeCode=*/6, zoneId);
}

// ============================================================================
// cTeleportWin_OnMouseDown 0x627BF0 — hit-test, arms a latch (close OR row).
// ============================================================================
bool TeleportWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    Recenter(lastScreenW_, lastScreenH_);   // 0x627c04: recentering BEFORE the hit-test

    if (CloseButtonHit(x, y)) {
        // 0x627c7c: Snd3D_PlayScaledVolume(flt_1487E3C, ..., 0, 100, 1) — click (not wired).
        closeLatch_ = true;                 // 0x627c84: *(this+70) = 1
    } else {
        const int32_t morphNpc = game::g_Client.VarGet(game::WarpAddr::SelfMorphNpcId); // g_SelfMorphNpcId
        for (int i = 0; i < kSlotCount; ++i) {
            // 0x627d11: row skipped if its destination == the player's current town/NPC.
            if (DestMapId(i) == morphNpc) continue;
            if (!RowHit(i, x, y)) continue;
            // 0x627d1e: click sound. 0x627d29: *(this+i+71) = 1. 0x627d34: immediate return.
            slotLatch_[i] = true;
            return true;
        }
    }
    return true;   // modal: the dispatcher (page 76) always consumes the click
}

// ============================================================================
// cTeleportWin_OnCommit 0x627D50 — release: validates and arms/emits, or closes.
// ============================================================================
bool TeleportWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    Recenter(lastScreenW_, lastScreenH_);   // 0x627d64: recentering BEFORE the hit-test

    if (closeLatch_) {                       // 0x627da8: if (*(this+70))
        closeLatch_ = false;                 // 0x627db4: clear
        if (CloseButtonHit(x, y)) {          // 0x627de0
            Close();                         // 0x627df1 : UI_NpcWin_CloseRestore
            return true;
        }
        return true;                         // else: end -> return result (consumed)
    }

    // 0x627dfb: loop over the 4 rows; handles the FIRST latched one then returns.
    for (int i = 0; i < kSlotCount; ++i) {
        if (!slotLatch_[i]) continue;        // 0x627e24: if (*(this+i+71))
        slotLatch_[i] = false;               // 0x627e36: UNCONDITIONAL clear

        if (RowHit(i, x, y)) {               // 0x627e4c..0x627e85
            const int32_t mapId = DestMapId(i);      // 0x627e93
            if (mapId) {                             // 0x627e9f: mapId 0 -> silent abandon
                // 0x627eac: v6 = g_SelfLevelBonus + g_SelfLevel. Level guard via the
                // g_MotionFrameRangeTable 0x14A9350 table: SkillLevelTable_GetMin/Max(table, mapId) =
                // table[2*mapId-2]/table[2*mapId-1] == GetFrameRange(mapId).start/.end (Motion_
                // InitFrameTable sets {157,157} for rows 312/315/330/333 -> mapId
                // 313/316/331/334: the page is usable ONLY at level EXACTLY 157).
                const int level = game::g_World.self.level + game::g_World.self.levelBonus;
                const game::MotionFrameRange* fr = game::GetFrameRange(mapId);
                // nullptr (table not initialized) => {0,0}, mirroring the zeroed BSS the
                // binary would have read before Motion_InitFrameTable (App_Init) — the guard would then fail.
                const int lvlMin = fr ? fr->start : 0;
                const int lvlMax = fr ? fr->end   : 0;
                if (level >= lvlMin && level <= lvlMax) {          // 0x627ee0
                    if (game::g_Client.VarGet(kWarpEnableVarAddr) >= 1) {   // 0x627f0e
                        // 0x627f4a: Motion_GetComboOffsetTable(g_LocalElement, mapId, v13). For
                        // the 4 mapIds the function ALWAYS returns 1 and writes the CONSTANT
                        // kTeleportDestPos (proven case by case, see the anonymous namespace above): the
                        // `if (result)` @0x627f51 is therefore always taken.
                        if (!game::g_Client.VarGet(game::WarpAddr::MorphInProgress))  // 0x627f5f
                            ArmWarpAndSendOp20(mapId, kTeleportDestPos);   // 0x627f65..0x628008
                        // 0x628010: CloseRestore is OUTSIDE the `if (!g_MorphInProgress)` but
                        // INSIDE the `if (result)` — the window closes even if the morph blocked
                        // arming. Reproduced as-is.
                        Close();
                        return true;
                    }
                    SysMsg(2821);            // 0x627f21: StrTable005_Get(g_LangId, 2821)
                    return true;
                }
                SysMsg(227);                 // 0x627ef2: StrTable005_Get(g_LangId, 227)
                return true;
            }
        }
        return true;   // 0x627f02: latch consumed -> return result (even if the row miss)
    }
    return true;       // 0x62801c: modal
}

bool TeleportWindow::OnKey(int vk) {
    // UI_NpcWin_OnKey 0x5DE030 emits NOTHING and filters no key for page 76; Escape =
    // porting convention (same affordance as UI/NpcDialogWindow.cpp, not from the binary).
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) { Close(); return true; }
    return false;
}

// ============================================================================
// Rendering — cTeleportWin_Draw 0x628030
// ============================================================================
void TeleportWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    if (!bOpen_) return;
    lastScreenW_ = ctx.screenW;             // memoized for hit-test recentering
    lastScreenH_ = ctx.screenH;
    Recenter(ctx.screenW, ctx.screenH);     // 0x628054: Draw also recenters

    const int32_t morphNpc = game::g_Client.VarGet(game::WarpAddr::SelfMorphNpcId); // g_SelfMorphNpcId

    if (ctx.phase == UiPhase::Panels) {
        // 0x6280b3: Sprite2D_Draw(&unk_8FE3E0, *this, *(this+1)) — panel background.
        kPanelBg.Draw(ctx, x_, y_, kPanelW, kPanelH, kColBg);
        ctx.DrawFrame(x_, y_, kPanelW, kPanelH, kColBorder, 2);

        // 0x6280be: if (*(this+70)) Sprite2D_Draw(&unk_8F3798, *this+235, *(this+1)+4). In the
        // binary the close button's RESTING state is baked into the panel sprite unk_8FE3E0, and
        // unk_8F3798 is only a "pressed" OVERLAY drawn when *(this+70). Since the C++
        // panel is a flat fill with no button, the button is materialized permanently (visible fallback),
        // tinted "pressed" when the latch is armed.
        ctx.FillRect(x_ + 235, y_ + 4, kCloseW, kCloseH, closeLatch_ ? kColClose : kColBorder);

        // 0x6280ed: for(i<4) — each row, EXCEPT if its destination == g_SelfMorphNpcId
        // (0x628128); color state 2=latched (0x6281a9), 3=hover (0x628191), 1=resting.
        for (int i = 0; i < kSlotCount; ++i) {
            if (DestMapId(i) == morphNpc) continue;
            const int state = slotLatch_[i] ? 2 : (RowHit(i, cursorX, cursorY) ? 3 : 1);
            const D3DCOLOR col = (state == 2) ? kColPressed : (state == 3) ? kColHover : kColRest;
            // The clickable row spans x in (x+37, x+217), y = y+18i+26..+38. Materialized
            // here as a flat fill (the real rendering is a text label, see the Text phase).
            ctx.FillRect(x_ + 37, y_ + 18 * i + 26, 180, 12, (col & 0x00FFFFFFu) | 0x40000000u);
        }
        return;
    }

    // --- Text phase ---
    // 0x6281bd: cTeleportWin_FormatEntryLabel(i) = Crt_Vsnprintf("%s %s",
    //   StrTable003_Get(dword_84A6A8, mapId), StrTable005_Get(g_LangId, 225)); then
    // 0x628220: UI_DrawNumberValue(label, *this+127 - UI_MeasureNumberText(label)/2,
    //   *(this+1)+18*i+26, state). Label horizontally centered on x+127.
    const std::string suffix = game::Str(225);
    for (int i = 0; i < kSlotCount; ++i) {
        const int32_t mapId = DestMapId(i);
        if (mapId == morphNpc) continue;
        // StrTable003_Get(dword_84A6A8, mapId) == game::g_Strings.zoneNames.Get(mapId) (1-based,
        // "" out of bounds — same fallback as the binary's &String 0x7ec95f).
        const char* zoneName = game::g_Strings.zoneNames.Get(mapId);
        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "%s %s", zoneName, suffix.c_str());
        const int w = ctx.MeasureText(lbl);
        const D3DCOLOR col = slotLatch_[i] ? kColPressed
                            : (RowHit(i, cursorX, cursorY) ? kColHover : kColRest);
        ctx.Text(lbl, x_ + 127 - w / 2, y_ + 18 * i + 26, col);
    }
}

} // namespace ts2::ui

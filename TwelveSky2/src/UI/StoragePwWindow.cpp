// UI/StoragePwWindow.cpp — implementation of UI_StoragePwWnd (0x666900 / dword_1839950)
// = LEAVE / DISBAND ALLIANCE window (the IDA symbol is misleading, see the .h).
//
// No Net/ include: this window emits NOTHING — that is PROVEN, not an omission
// (see the "DECISIVE FACT" banner in UI/StoragePwWindow.h). No <winsock2.h>/<windows.h>
// ordering constraint here (same include profile as UI/SocialWindow.cpp).
#include "UI/StoragePwWindow.h"
#include "UI/PanelSkin.h"
#include "Game/GameState.h"      // game::g_World.allianceRoster / .self.localPlayerName
#include "Scene/SceneManager.h"  // ts2::g_SceneSubState (mirror of g_SceneSubState 0x1676184)

#include <cstddef>

namespace ts2::ui {

namespace {

// Active instance (only one in the whole process, owned by GameWindows).
StoragePwWindow* g_activeStoragePw = nullptr;

// Real "best effort" panel background — automatic fallback to kColPanelBg if absent
// (see methodology in UI/PanelSkin.h). The real background is sprite unk_8F70D4
// (EA 0x667082), whose .IMG file is not statically resolved.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_00301.IMG");

constexpr uint32_t kSysMsgColorAddr = 0x84DFD8; // g_SysMsgColor
constexpr uint32_t kSelfMorphNpcId  = 0x1675A98; // g_SelfMorphNpcId

// g_SysMsgColor 0x84DFD8 — long tail via Var() (convention UI/PartyWindow.cpp:47).
uint32_t SysMsgColor() {
    return static_cast<uint32_t>(game::g_Client.VarGet(kSysMsgColorAddr));
}

// Map_IsArenaZone 0x54B690: `return g_SelfMorphNpcId >= 270 && g_SelfMorphNpcId <= 274;`
// (global 0x1675A98 is g_SelfMorphNpcId despite the IDB comment's "map id" label).
// Ported file-local, EXACTLY like UI/PartyWindow.cpp:40-43 — this front does not
// own Game/MapWarp.h, where this helper should eventually be shared.
bool Map_IsArenaZone() {
    const int32_t morphNpcId = game::g_Client.VarGet(kSelfMorphNpcId);
    return morphNpcId >= 270 && morphNpcId <= 274;   // EA 0x54B6BE
}

// Mirror of Crt_Stricmp 0x76668B (case-insensitive ASCII comparison) —
// same file-local convention as Game/QuestSystem.cpp:25 (StriEqualBounded).
// Used ONLY by the "disband" button: see the Strcmp/Stricmp asymmetry
// documented in HandleDisbandClick().
bool StriEqual(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        unsigned char ca = static_cast<unsigned char>(a[i]);
        unsigned char cb = static_cast<unsigned char>(b[i]);
        if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 'a' + 'A');
        if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 'a' + 'A');
        if (ca != cb) return false;
    }
    return true;
}

// g_AllianceRosterNames[0] 0x16749B8 = mini-roster slot 0 = LEADER.
// Modeled by game::g_World.allianceRoster.memberNames[0] (Game/GameState.h:534).
const std::string& RosterLeader() {
    return game::g_World.allianceRoster.memberNames[0];
}

// byte_1673184 = local player name -> game::g_World.self.localPlayerName
// (Game/GameState.h:427). Populated by UI/LoginScene.cpp on character selection
// (see UI/GuildWindow.cpp:71). Same usage as UI/GuildWindow.cpp:79.
const std::string& SelfName() {
    return game::g_World.self.localPlayerName;
}

} // namespace

// ============================================================================
// Lifecycle / active instance
// ============================================================================
StoragePwWindow::StoragePwWindow() {
    RecomputeCenter(ts2::kRefWidth, ts2::kRefHeight);
    g_activeStoragePw = this;
}

StoragePwWindow::~StoragePwWindow() {
    if (g_activeStoragePw == this) g_activeStoragePw = nullptr;
}

StoragePwWindow* StoragePwWindow::Active() { return g_activeStoragePw; }

// UI_StoragePwWnd_Open 0x666960.
void StoragePwWindow::Open() {
    // (a) Arena guard: TOTAL refusal, system message, NO opening (EA 0x66696E).
    if (Map_IsArenaZone()) {
        game::g_Client.msg.System(game::Str(kStrArenaRefused), SysMsgColor()); // EA 0x666987/0x666992
        return;
    }

    // (b) `Crt_Strcmp(g_AllianceRosterNames, &String)` (EA 0x6669A3): `String` 0x7EC95F
    // is the EMPTY STRING (proven by read_cstring, length 0) -> this test means
    // "is roster slot 0 empty?". If so (strcmp == 0): message #355,
    // no opening (EA 0x6669B5..0x6669CB).
    if (RosterLeader().empty()) {
        game::g_Client.msg.System(game::Str(kStrNoAlliance), SysMsgColor());   // EA 0x6669C0/0x6669CB
        return;
    }

    // *(this+2) = 1 (EA 0x6669D5) + loop `for (i=0;i<3;++i) *(this+i+3)=0` (EA 0x6669DC).
    Dialog::Open();
    leaveLatch_   = false;
    disbandLatch_ = false;
    closeLatch_   = false;
}

// UI_StoragePwWnd_Close 0x666A10: `if (*(this+2)) *(this+2) = 0;` — nothing else.
void StoragePwWindow::Close() {
    Dialog::Close();
}

// ============================================================================
// UI_StoragePwWnd_ProcNet 0x666EB0 — toggle via keyboard command 25 (DIK_P)
// ============================================================================
// Full transcription, including the proven ASYMMETRY: the CLOSE path
// (window already open, EA 0x666EBC) is NOT guarded by the scene — only the
// OPEN path is (`g_SceneMgr == 6 && g_SceneSubState == 4`, EA 0x666ED6).
//
// The binary scans the command queue for the FIRST "pressed" record
// (`g_UiCmdQueueFlags[5*i] & 0x80`) then tests `== 25`. On the C++ side this
// scan is ALREADY ported (App/PlayerInputController.cpp:138
// `in.FirstKeyDownDik()`, transcription of the same loop 0x50C726): `cmdId` is
// therefore exactly the scan result, and the `cmdId == 25` test is the faithful
// equivalent (not a "is 25 anywhere in the queue" test).
//
// g_SceneMgr == 6: no global mirror on the C++ side (the current scene lives in
// SceneManager, private). This is NOT a gap here — the only intended caller,
// PlayerInputController::RouteSceneKey, is only reachable AFTER the entry
// guard `scene != Scene::InGame || g_SceneSubState != 4` in
// PlayerInputController::Update (App/PlayerInputController.cpp:30, transcription of
// 0x50B7EC): `g_SceneMgr == 6` is therefore ALREADY guaranteed at the call site. The
// sub-state is still checked, since it IS available as a global (ts2::g_SceneSubState).
bool StoragePwWindow::ProcKeyCommand(int cmdId) {
    StoragePwWindow* w = g_activeStoragePw;
    if (!w) return false;

    if (w->IsOpen()) {                       // EA 0x666EBC
        if (cmdId == kCmdToggle) {           // EA 0x666FB5
            w->Close();                      // EA 0x666FBC
            return true;                     // EA 0x666FC1
        }
        return false;                        // EA 0x666FC8
    }

    if (g_SceneSubState != 4) return false;  // the "g_SceneSubState == 4" half of EA 0x666ED6

    if (cmdId == kCmdToggle) {               // EA 0x666F39
        // UI_CloseAllDialogs(dword_1821D4C, 1) (EA 0x666F44): opening this window
        // closes all the others — UIManager::CloseAll() is the modeled mirror
        // (UI_CloseAllDialogs 0x5AC590, see UI/UIManager.h:217).
        UIManager::Instance().CloseAll();
        w->Open();                           // EA 0x666F4C — may REFUSE (arena / no alliance)
        return true;                         // EA 0x666F51 — the binary returns 1 even when refused
    }
    return false;                            // EA 0x666F58
}

// ============================================================================
// Geometry
// ============================================================================
void StoragePwWindow::RecomputeCenter(int screenW, int screenH) {
    x_ = screenW / 2 - kPanelW / 2;
    y_ = screenH / 2 - kPanelH / 2;
}

StoragePwWindow::Rect StoragePwWindow::PanelRect() const {
    return { x_, y_, kPanelW, kPanelH };
}

StoragePwWindow::Rect StoragePwWindow::LeaveButtonRect() const {
    return { x_ + kBtnX, y_ + kBtnLeaveY, kBtnW, kBtnH };
}

// Band DRAWN / tested on mouse-down: +110 (EA 0x66711E, 0x666B00).
StoragePwWindow::Rect StoragePwWindow::DisbandButtonRect() const {
    return { x_ + kBtnX, y_ + kBtnDisbandDrawY, kBtnW, kBtnH };
}

// Band tested on CLICK: +100 (EA 0x666D48) — binary quirk, see the banner in the .h.
StoragePwWindow::Rect StoragePwWindow::DisbandClickRect() const {
    return { x_ + kBtnX, y_ + kBtnDisbandClickY, kBtnW, kBtnH };
}

StoragePwWindow::Rect StoragePwWindow::CloseButtonRect() const {
    return { x_ + kBtnX, y_ + kBtnCloseY, kBtnW, kBtnH };
}

// ============================================================================
// UI_StoragePwWnd_OnMouseDown 0x666A30
// ============================================================================
bool StoragePwWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;                                  // EA 0x666A3B

    // [audio] Snd3D_PlayScaledVolume(flt_1487E3C, .., 100, 1) on the 3 buttons
    // (EA 0x666AC7 / 0x666B14 / 0x666B61). Not ported: no by-address 3D emitter
    // registry on the C++ side (established convention: UI/Widgets.cpp:60,
    // UI/NpcDialogWindow.cpp:287, UI/PartyWindow.cpp:307).

    const Rect leave = LeaveButtonRect();
    if (PointInRect(x, y, leave.x, leave.y, leave.w, leave.h)) {  // EA 0x666AB3 (+56)
        leaveLatch_ = true;                                       // *(this+3)=1 (EA 0x666ACF)
        return true;
    }

    // NOTE: mouse-DOWN uses +110 (EA 0x666B00), while CLICK uses
    // +100 (EA 0x666D48). Quirk reproduced — see the banner in the .h.
    const Rect disband = DisbandButtonRect();
    if (PointInRect(x, y, disband.x, disband.y, disband.w, disband.h)) {
        disbandLatch_ = true;                                     // *(this+4)=1 (EA 0x666B1C)
        return true;
    }

    const Rect cls = CloseButtonRect();
    if (PointInRect(x, y, cls.x, cls.y, cls.w, cls.h)) {          // EA 0x666B4D (+164)
        closeLatch_ = true;                                       // *(this+5)=1 (EA 0x666B69)
        return true;
    }

    // Fallback: `return Sprite2D_HitTest(unk_8F70D4, *this, *(this+1), a4, a5)`
    // (EA 0x666B98) — consumes the click if it lands on the background.
    const Rect panel = PanelRect();
    return PointInRect(x, y, panel.x, panel.y, panel.w, panel.h);
}

// ============================================================================
// UI_StoragePwWnd_OnClick 0x666BB0
// ============================================================================
// if / else-if structure, MUTUALLY EXCLUSIVE on latches +3, +4, +5 (the binary never
// tests two latches in the same call), each branch disarming its
// latch BEFORE the hit-test and returning 1 even if the hit-test fails.
bool StoragePwWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;                                  // EA 0x666BBB

    if (leaveLatch_) {                                          // EA 0x666C16
        leaveLatch_ = false;                                    // EA 0x666C23
        const Rect leave = LeaveButtonRect();
        if (!PointInRect(x, y, leave.x, leave.y, leave.w, leave.h))
            return true;                                        // EA 0x666C58
        HandleLeaveClick();
        return true;                                            // EA 0x666CD4
    }

    if (disbandLatch_) {                                        // EA 0x666D14
        disbandLatch_ = false;                                  // EA 0x666D21
        // QUIRK: CLICK band at +100 (EA 0x666D48), not +110. See the .h.
        const Rect hit = DisbandClickRect();
        if (!PointInRect(x, y, hit.x, hit.y, hit.w, hit.h))
            return true;                                        // EA 0x666D51
        HandleDisbandClick();
        return true;                                            // EA 0x666DD2
    }

    if (closeLatch_) {                                          // EA 0x666E0F
        closeLatch_ = false;                                    // EA 0x666E18
        const Rect cls = CloseButtonRect();
        if (PointInRect(x, y, cls.x, cls.y, cls.w, cls.h))
            Close();                                            // EA 0x666E55
        return true;                                            // EA 0x666E4B
    }

    return false;                                               // EA 0x666E61
}

// --- Button 1: LEAVE the alliance (non-leaders only) ----------------
// Crt_Strcmp comparison = CASE-SENSITIVE (EA 0x666C67 / 0x666CA8).
void StoragePwWindow::HandleLeaveClick() {
    // `!Crt_Strcmp(roster[0], &String)` -> roster[0] empty (EA 0x666C67): LABEL_7.
    if (RosterLeader().empty()) {
        game::g_Client.msg.System(game::Str(kStrNoAlliance), SysMsgColor());   // EA 0x666C84
        return;
    }

    // `Crt_Strcmp(roster[0], byte_1673184)` != 0 -> I am NOT the leader (EA 0x666CA8).
    // (Model equivalent: !game::g_World.allianceRoster.IsLeader(SelfName()) — the
    // `!name.empty()` guard in IsLeader changes NOTHING here, roster[0] already proven
    // non-empty by the test above. The literal comparison is written out to make
    // visible the Strcmp/Stricmp asymmetry with button 2.)
    if (RosterLeader() != SelfName()) {
        // MsgBox type 11, body Str(364), title = `&String` = EMPTY string (EA 0x666CFA).
        // CRITICAL FIDELITY: type 11 has NO reader (data_refs(0x1822450) = 10
        // readers, types 8/9/10/14/19/20/37/38 only) and the acceptance jump table
        // 0x5C2DC3 falls through to its default `mov eax,1 ; retn 8`. Clicking "Yes"
        // therefore triggers NOTHING in the original binary: no packet is emitted, here
        // or there. DO NOT add an emission (see the "DECISIVE FACT" banner in the .h).
        game::g_Client.prompt.Open(kMsgBoxLeave, game::Str(kStrLeaveConfirm), std::string());
        Close();                                                               // EA 0x666D02
    } else {
        // I AM the leader -> a leader cannot "leave" (EA 0x666CC4).
        game::g_Client.msg.System(game::Str(kStrLeaderCant), SysMsgColor());
    }
}

// --- Button 2: DISBAND the alliance (leader only) --------------------
// REAL BINARY ASYMMETRY, reproduced as-is: this button compares with
// Crt_Stricmp (case-INsensitive, EA 0x666D6F / 0x666DA5), while
// button 1 uses Crt_Strcmp (case-sensitive). This is not a misread:
// the two calls are distinct symbols (0x75CF20 vs 0x76668B).
void StoragePwWindow::HandleDisbandClick() {
    // `!Crt_Stricmp(roster[0], &String)` -> roster[0] empty (EA 0x666D6F): LABEL_7.
    if (RosterLeader().empty()) {
        game::g_Client.msg.System(game::Str(kStrNoAlliance), SysMsgColor());   // EA 0x666C84
        return;
    }

    // `Crt_Stricmp(roster[0], byte_1673184)` != 0 -> I am not the leader (EA 0x666DA5).
    if (!StriEqual(RosterLeader(), SelfName())) {
        game::g_Client.msg.System(game::Str(kStrNotLeader), SysMsgColor());    // EA 0x666DC2
        return;
    }

    // MsgBox type 12, body Str(365), EMPTY title (EA 0x666DF8) — same fidelity
    // note as for type 11: NO consumer, NO emission.
    game::g_Client.prompt.Open(kMsgBoxDisband, game::Str(kStrDisbandConf), std::string());
    Close();                                                                   // EA 0x666E00
}

// ============================================================================
// UI_StoragePwWnd_Render 0x666FE0
// ============================================================================
void StoragePwWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    RecomputeCenter(ctx.screenW, ctx.screenH);                  // EA 0x667000..0x66703E
    if (!bOpen_) return;                                        // EA 0x666FEB

    const Rect panel   = PanelRect();
    const Rect leave   = LeaveButtonRect();
    const Rect disband = DisbandButtonRect();                   // DRAW at +110 (EA 0x66711E)
    const Rect cls     = CloseButtonRect();

    if (ctx.phase == UiPhase::Panels) {
        // Sprite2D_HitTest(unk_8F70D4) -> Util_SetClampedU8Field(dword_8E714C, 0)
        // (EA 0x66705B/0x66706B): cursor slot 0 on panel hover. NOT ported —
        // gap UTIL-01 (CursorSet::SetActiveSlot has no caller, `cursors_` private in
        // App/App.h:43), fix explicitly out of scope for this front.
        // TODO [ancre 0x66706B]: Cursors().SetActiveSlot(0) on hover, once UTIL-01 is wired.

        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColPanelBg); // EA 0x667082
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColFrame, 1);

        // FIDELITY: each button is drawn ONLY when pressed (sprite "down":
        // unk_8F71FC / unk_8F7324 / unk_8F744C) or hovered (sprite "up": unk_8F7168 /
        // unk_8F7290 / unk_8F73B8) — the resting state is NOT drawn, its visual
        // belonging to the BACKGROUND sprite. Since the fallback background doesn't contain
        // these visuals, the resting button is drawn in a neutral tint: an ACCEPTED and
        // documented deviation (otherwise buttons would be invisible with the fallback background).
        const bool leaveHover = PointInRect(cursorX, cursorY, leave.x, leave.y, leave.w, leave.h);
        ctx.FillRect(leave.x, leave.y, leave.w, leave.h,
                     leaveLatch_ ? kColBtnDown : (leaveHover ? kColBtnHover : kColBtnBg)); // EA 0x6670F0 / 0x6670D1
        ctx.DrawFrame(leave.x, leave.y, leave.w, leave.h, kColFrame, 1);

        const bool disbandHover = PointInRect(cursorX, cursorY, disband.x, disband.y, disband.w, disband.h);
        ctx.FillRect(disband.x, disband.y, disband.w, disband.h,
                     disbandLatch_ ? kColBtnDown : (disbandHover ? kColBtnHover : kColBtnBg)); // EA 0x66715E / 0x66713F
        ctx.DrawFrame(disband.x, disband.y, disband.w, disband.h, kColFrame, 1);

        const bool clsHover = PointInRect(cursorX, cursorY, cls.x, cls.y, cls.w, cls.h);
        ctx.FillRect(cls.x, cls.y, cls.w, cls.h,
                     closeLatch_ ? kColBtnDown : (clsHover ? kColBtnHover : kColBtnBg));       // EA 0x6671D4 / 0x6671B2
        ctx.DrawFrame(cls.x, cls.y, cls.w, cls.h, kColFrame, 1);
        return;
    }

    // --- Text phase --------------------------------------------------------
    // The binary writes NO text: labels are part of the button sprites.
    // Fallback labels, accepted deviation (see above), worded according to the
    // PROVEN semantics of the two actions (leave / disband).
    ctx.Text("Alliance", panel.x + 8, panel.y + 6, kColTitle);

    const char* leaveLbl = "Quitter";
    ctx.Text(leaveLbl, leave.x + (leave.w - ctx.MeasureText(leaveLbl)) / 2,
             leave.y + (leave.h - 12) / 2, kColText);

    const char* disbandLbl = "Dissoudre";
    ctx.Text(disbandLbl, disband.x + (disband.w - ctx.MeasureText(disbandLbl)) / 2,
             disband.y + (disband.h - 12) / 2, kColText);

    const char* clsLbl = "Fermer";
    ctx.Text(clsLbl, cls.x + (cls.w - ctx.MeasureText(clsLbl)) / 2,
             cls.y + (cls.h - 12) / 2, kColText);
}

} // namespace ts2::ui

// UI/LoginScene_Login.cpp — Login scene (Scene_Login* : 0x51A8D0 / 0x51B020 / 0x51B5D0 /
// 0x51B780). Mechanical split of LoginScene.cpp (see LoginScene.cpp for the class lifecycle/
// dispatch and LoginScene.h for the full class declaration).
#include "UI/LoginScene.h"
#include "UI/LoginScene_Internal.h" // kColText, kBtnW/kBtnH (shared by the split family)
#include "UI/UiProjection.h"       // ui::ProjectDesignAnchor (UI_ProjectSpriteToScreen 0x50F5D0)
#include "Config/GameOptions.h"    // ts2::config::Cfg_SaveLastServer (G02_GINFO\010.BIN, write-only)
#include "Net/Login.h"             // ConnectLoginServer / LoginRequest / ConnectGameServer
#include "Net/CharSelectPackets.h" // AccountKeepAlive/CreateCharacter/CharSlotAction/ReqEnterCharInfo/ReqCancelEnter
#include "Net/Rng.h"                // DefaultRng() — Rng_Next() % 360 for spawnRotationDeg (see GameState.h)
#include "Net/GameServerDomains.h"  // SelectGameServerHost / g_ServerMode (Net_SelectServerDomain 0x53FE90)
#include "Game/GameState.h"        // game::g_World.zoneId (consumed by EnterWorldFlow)
#include "Game/StringTables.h"     // game::g_Strings.bannedWords (001.DAT, 1432 banned words — creation filter)
#include "Game/ClientRuntime.h"    // game::Str(id) — real StrTable005 text for CharSelect notices
#include "Game/GameDatabase.h"     // game::GetItemInfo / WeaponClassFromTypeCode (entry motion 0x4CC870)
#include "Game/MiscManagers.h"     // game::Cursors() / kCursorDefault (scene-entry cursor reset, 0x4C1110)
#include "Asset/ImgFile.h"         // asset::ImgFile (.IMG loader, real ServerSelect/Login background)
#include "Gfx/Camera.h"            // gfx::Camera — application projection (Gfx_InitDevice 0x69BFC6)
#include "Core/Log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace ts2::ui {

namespace {

// Fidelity anchors: login panel (unk_8E9368) hardcoded offsets (Scene_LoginRender 0x51B020):
// fields at +126,+60 / +126,+95; OK button at +298,+126; Quit button at +374,+126; shadow
// checkbox at +21,+130. Panel size below is NOMINAL (the real one comes from the UI atlas
// sprite, cf. LoginRender/LoginOnMouseDown/LoginOnMouseUp).
constexpr int kPanelW = 470, kPanelH = 210;

} // namespace

// Scene 3 — Login (Scene_Login* : 0x51A8D0 / 0x51B020 / 0x51B5D0 / 0x51B780)
void LoginScene::SetFocus(int field) {
    focusField_ = field;                    // dword_1668FC0
    idBox_.SetFocused(field == 1);
    pwBox_.SetFocused(field == 2);
}

// Literal order ALIGNED on the real binary (EA 0x51A946 then 0x51A954 then 0x51A965/
// 0x51A976 — Docs/TS2_LOGINSCENE_AUDIT.md §3.8, closed): the binary resets the frame
// counter to 0 and refocuses the ID field BEFORE clearing the Win32 EDIT controls
// (`SetWindowTextA(ID,"")` then `SetWindowTextA(PW,"")`), not after. No behavioral
// difference at the next frame (Clear()/SetFocus() are independent), but the call
// sequence now reproduces the exact disassembly order.
void LoginScene::ResetLoginFields() {
    frame_ = 0;                             // EA 0x51A946 (a1[2] = 0)
    SetFocus(1);                            // EA 0x51A954 — focus EDIT ID (UI_FocusEditBox(1))
    idBox_.Clear();                         // EA 0x51A965 — SetWindowTextA(ID, "")
    pwBox_.Clear();                         // EA 0x51A976 — SetWindowTextA(PW, "")
}

// Action kind=2 of UI_NoticeDlg_OnLButtonUp (cf. declaration). Closing the socket before the
// scene change is faithful (Net_CloseSocket EA 0x5C04DF, before *a1=2/a1[1]=0 EA
// 0x5C04E4-0x5C04F8); loginSub_=Init prepares a clean redisplay if the user comes back to
// Login later (same pattern as exitBtn_.SetOnClick, cf. Init()).
void LoginScene::AbortLoginToServerSelect() {
    if (net_) net::NetCloseSocket(net_->Client()); // EA 0x5C04DF
    pending_  = ts2::Scene::ServerSelect;           // EA 0x5C04E4 (g_SceneMgr = 2)
    loginSub_ = LoginSub::Init;                     // EA 0x5C04EE (g_SceneSubState = 0)
    frame_    = 0;                                  // EA 0x5C04F8 (dword_1676188 = 0) — 3rd
    // field cleared by UI_NoticeDlg_OnLButtonUp kind=2, missing here until this pass
    // (fresh decompilation of 0x5C03F0, 2026-07-14): frame_ is the SAME shared counter
    // (this[2]) that ServerSelectUpdate()/LoginUpdate() increment; without this reset it used
    // to resume with the value left by the previous screen instead of 0.
}

// (LoginPanelOrigin() removed 2026-07-15: dead function, not declared in the class and never
//  called. LoginRender / LoginOnMouseDown / LoginOnMouseUp recompute the panel origin inline
//  from the REAL size of sprite slot 14, cf. LoginRender.)

// Hardcoded login-panel offsets. EXACT (fixed) distinction between the field ZONE (EDIT
// control, UI_CreateEditBoxes 0x50E460: ID @+(118,54), PW @+(118,90), 319x21) and the drawn
// TEXT position (Scene_LoginRender 0x51B020: UI_DrawNumberValue at +(126,60)/(126,95), i.e.
// +8/+6 into the field). The hit-test/focus uses the real 319x21 zone; the text
// (DrawFieldValue) is still drawn at +(126,60)/(126,95). Buttons: OK +(298,126), Quit
// +(374,126), shadow checkbox +(21,130).
void LoginScene::LayoutLogin(int px, int py) {
    idBox_.SetBounds(px + 118, py + 54, 319, 21); // g_hEditLoginId — real EDIT zone (319x21), Scene_LoginOnMouseDown 0x51B658 refs dword_166901C..28
    pwBox_.SetBounds(px + 118, py + 90, 319, 21); // g_hEditLoginPw — real EDIT zone (319x21), Scene_LoginOnMouseDown 0x51B695 refs dword_166902C..38
    okBtn_.SetBounds(px + 298, py + 126, kBtnW,  kBtnH);    // unk_8E9084, render/hit EA 0x51B48D/0x51B80E
    exitBtn_.SetBounds(px + 374, py + 126, kBtnW, kBtnH);   // unk_8E9240, render/hit EA 0x51B50F/0x51B85C
    optBtn_.SetBounds(px + 21,  py + 130, 16, 16);          // unk_9555BC: render EA 0x51B571; hit-test at y+131 EA 0x51B74D/0x51B8B6
}

// Scene_LoginUpdate 0x51A8D0: sub-state machine (this[1]).
void LoginScene::LoginUpdate() {
    switch (loginSub_) {
    case LoginSub::Init:                    // case 0 — literal order EA 0x51A8FD-0x51A976
        // EA 0x51A8FD: Util_SetClampedU8Field(&dword_8E714C, 0) — resets the cursor shape to
        // slot 0 (arrow) on Login entry. WIRED (C-cursor): game::Cursors() is the UNIQUE
        // singleton (mPOINTER) now ticked by App -> SetActiveSlot(0) takes effect. Invariant
        // across the 5 scene-entry sites: the reset is placed right BEFORE
        // UI_FocusEditBox/SetFocus(0) (here EA 0x51A909). The FULL triplet
        // ResetAllDialogs->cursor->focus is only literal at the EnterWorld site @0x52C044
        // (Login has no ResetAllDialogs before this reset).
        game::Cursors().SetActiveSlot(game::kCursorDefault); // 0x51A8FD / 0x4C1110
        SetFocus(0);                        // EA 0x51A909 — generic defocus before the reset
        okBtn_.Reset(); exitBtn_.Reset(); optBtn_.Reset(); // EA 0x51A90E-0x51A92F (a1[3..5],
        // identified subset of the 150 reset dwords; the remaining 147 dw belong to shared
        // cSceneMgr fields not modeled in LoginScene, cf. audit §3.3).
        loginSub_ = LoginSub::Idle;         // EA 0x51A93C (a1[1] = 1)
        frame_    = 0;                      // EA 0x51A946 (a1[2] = 0)
        ResetLoginFields();                 // EA 0x51A954-0x51A976: focus ID + clear ID/PW —
        // internal order (focus BEFORE clear) now conforms, cf. body of
        // ResetLoginFields() (Docs/TS2_LOGINSCENE_AUDIT.md §3.8, closed).
        break;
    case LoginSub::Idle:                     // case 1
        ++frame_;                            // this[2]++
        // Faithful: every 30 frames, Ac_GameGuard_Heartbeat 0x6DE3F7; if
        // != 1877 -> g_QuitFlag = 1. GameGuard is out of scope for the UI shell.
        break;
    case LoginSub::Trigger:                  // case 2
        loginSub_ = LoginSub::DoLogin;       // this[1] = 3
        frame_    = 0;
        break;
    case LoginSub::DoLogin:                  // case 3
        DoLogin();
        break;
    case LoginSub::NoticeWait:               // case 4 — NO real case 4 (default: no-op).
        // Verified by disassembly (cf. LoginSub::NoticeWait): the exit is driven by the notice's
        // OK click (OpenNotice(..., AbortLoginToServerSelect) in DoLogin()), NOT by this
        // function. Doing nothing here is the faithful reproduction.
        break;
    }
}

// Sub-state 3: reads ID/PW, ConnectLoginServer then LoginRequest (op 0x0B, extra =
// net::kLoginExtra = 90218 = 0x1606A). FIX (W5b): this comment used to say "ver 106" — WRONG,
// 106 = 0x6A is only the LOW byte of the real value. Proof: `push 1606Ah` EA 0x51ab0e right
// before `call Net_LoginRequest` EA 0x51ab20 in Scene_LoginUpdate 0x51A8D0 (the SOLE caller of
// Net_LoginRequest 0x51B8E0). Do NOT "re-fix" back to 106: cf. Net/Login.h:39-41, which
// documents the original mistake.
//
// The 4 notices below ALL have kind=2 in the binary (EA 0x51AA3D, 0x51AA92, 0x51AF09 and the
// rest of the switch, `push 2` confirmed by disassembly before EVERY UI_NoticeDlg_Open call —
// cf. LoginSub::NoticeWait): clicking OK on ANY of them closes the socket and returns to
// ServerSelect (AbortLoginToServerSelect), even for the first two (empty field), where the
// sub-state IMMEDIATELY returns to Idle regardless — both effects are real and not exclusive:
// Idle takes over right away (the player can keep typing while the notice stays visible,
// faithful to the absence of a noticeOpen_ gate on state 1), then the OK click, when it
// happens, overrides everything and goes back to ServerSelect.
// Pre-zeroing EA 0x51A9F1/0x51AA05 (Docs/TS2_LOGINSCENE_AUDIT.md §3.7, closed WITHOUT porting
// code — not applicable): the binary does `Crt_Memset(g_AccountName, 0, 128)` /
// `Crt_Memset(byte_1669214, 0, 128)` BEFORE each `GetWindowTextA`, because these are fixed C
// buffers reused frame to frame — without this memset, a failing `GetWindowTextA` would leave
// the previous frame's bytes behind (a leaked residual old password in memory).
// `idBox_.Text()`/`pwBox_.Text()` (std::string) don't have this bug class: each call reflects
// EXACTLY the widget's current content, there's no residual state to clear. Replicating a
// "memset" here would have neither a syntactic equivalent nor an observable effect — this is
// not a behavior gap, it's an implementation detail made obsolete by the std::string
// abstraction. Intentionally left as is.
void LoginScene::DoLogin() {
    // The binary: GetWindowTextA(ID)/GetWindowTextA(PW); empty -> NoticeDlg
    // (StrTable005 2905/2906) and back to the idle sub-state.
    if (idBox_.Text().empty()) {
        OpenNotice(game::Str(2905),                                   // empty ID -> StrTable005 str2905 (real)
                   [this] { AbortLoginToServerSelect(); });
        loginSub_ = LoginSub::Idle; frame_ = 0; return;
    }
    if (pwBox_.Text().empty()) {
        OpenNotice(game::Str(2906),                                   // empty PW -> StrTable005 str2906 (real)
                   [this] { AbortLoginToServerSelect(); });
        loginSub_ = LoginSub::Idle; frame_ = 0; return;
    }
    if (!net_ || serverState_.servers.empty()) {
        // Defensive guard (outside the binary: the server is hardcoded) -> generic str6 message.
        OpenNotice(game::Str(6), [this] { AbortLoginToServerSelect(); });
        loginSub_ = LoginSub::NoticeWait; frame_ = 0; return;
    }

    // Selected server host/port (serverState_ = faithful module table). The game::ServerEntry
    // entry carries the hostname in `name` (SingleServer: real login host
    // "12sky2-login.geniusorc.com", cf. game::BuildServerList / BuildServerList()). Read under
    // lock (the status worker writes other fields of these entries concurrently); host+port are
    // copied locally before the blocking network call.
    std::string svHost;
    uint16_t    svPort = 0;
    {
        std::lock_guard<std::mutex> lk(serverMutex_);
        int idx = serverState_.selectedServer;               // this[15374] (written by OnServerClicked)
        if (idx < 0 || idx >= static_cast<int>(serverState_.servers.size())) idx = 0; // guard (normal flow: >= 0)
        const game::ServerEntry& sv = serverState_.servers[static_cast<size_t>(idx)];
        svHost = sv.name;
        svPort = sv.port;
    }

    // Synchronous/blocking login handshake (17-byte banner -> XOR key). Operates on the shared
    // network system's socket (net_->Client()).
    const int rc = net::ConnectLoginServer(net_->Client(), svHost.c_str(), svPort);
    if (rc != net::kNetOk) {
        OpenNotice(ConnectErrText(rc), [this] { AbortLoginToServerSelect(); });
        loginSub_ = LoginSub::NoticeWait; frame_ = 0; return;
    }

    // id/pw request (op 0x0B). On success, LoginRequest copies the account token into
    // net::g_AccountName (reused by ConnectGameServer) + the GM level into
    // net::g_GmAuthLevel.
    int result = 0;
    net::LoginRequest(net_->Client(), idBox_.Text().c_str(), pwBox_.Text().c_str(),
                      net::kLoginExtra, result);
    if (result == 0) {
        loggedUser_ = idBox_.Text();
        // Re-initializes the CharSelect flow (Init sub-state -> slot occupancy recomputed 30
        // frames later, cf. Game/CharSelectFlow.h). The real game server (host/port) comes
        // from Net_ReqEnterCharInfo (opcode 22), NOT a placeholder cached here (cf.
        // Docs/TS2_CHARSELECT_AUDIT.md §2.5).
        charState_ = game::CharSelectState{};
        pending_  = ts2::Scene::CharSelect; // scene_id = 4
        loginSub_ = LoginSub::Init;         // re-init in case of a later return
    } else {
        OpenNotice(LoginErrText(result), [this] { AbortLoginToServerSelect(); });
        loginSub_ = LoginSub::NoticeWait;   // this[1] = 4
        frame_    = 0;
    }
}

// Scene_LoginRender 0x51B020: centered panel, fields (text at +126,+60/+95 with a
// caret), OK/Quit buttons and the "shadows" checkbox.
void LoginScene::LoginRender() {
    // Real panel (unk_8E9368, slot 14 -> 001_00015.IMG, 470x167 logical): REAL texture
    // dimensions when loaded (more faithful centering than the old nominal template
    // kPanelW/kPanelH), falls back to that same nominal template otherwise (file
    // missing/unreadable).
    gfx::GpuTexture* panelTex = GetAtlasSprite(14);
    const bool panelValid = panelTex && panelTex->Valid() && panelTex->Width() > 0 && panelTex->Height() > 0;
    const int panelW = panelValid ? static_cast<int>(panelTex->Width())  : kPanelW;
    const int panelH = panelValid ? static_cast<int>(panelTex->Height()) : kPanelH;
    const int px = screenW_ / 2 - panelW / 2;
    const int py = screenH_ / 2 - panelH / 2;
    LayoutLogin(px, py);

    const POINT mp = CursorClient();
    // Update the buttons' internal hover state (Hovered()).
    okBtn_.OnMouseMove(mp.x, mp.y);
    exitBtn_.OnMouseMove(mp.x, mp.y);
    optBtn_.OnMouseMove(mp.x, mp.y);

    if (sprites_.Ready()) {
        sprites_.Begin();
        // Fullscreen background: real atlas[bgIndex] sprite unk_8E8B50 (Scene_LoginRender
        // 0x51B020, EA 0x51B207 — SAME this[168] as ServerSelect, cf. GetAtlasSprite).
        // ZERO fallback: if the texture isn't loaded, nothing is drawn.
        DrawFullscreenBg(bgAtlasSlot_);
        // Panel (unk_8E9368 = slot 14 -> 001_00015.IMG): real texture, blitted unstretched at
        // its native size. ZERO fallback (faithful to Sprite2D_Draw): nothing if not loaded.
        if (panelValid)
            sprites_.DrawSprite(panelTex->Handle(), nullptr, px, py, gfx::kSpriteWhite);
        // ID / PW fields: Scene_LoginRender 0x51B020 NEVER draws a background rect for these
        // fields (the outline is baked into the panel texture). No fill.
        // OK/Quit buttons: real "Confirm"/"Cancel" hover/pressed sprite (slots 9/10 and
        // 12/13, cf. ApplyConfirmCancelSkin) — invisible in idle state, like the binary (no
        // Sprite2D_Draw outside hover/press, EA 0x51B48D/0x51B50F). No fallback fill.
        okBtn_.DrawSkin(sprites_);
        exitBtn_.DrawSkin(sprites_);
        // "Shadows" option checkbox (unk_9555BC = slot 3007 -> 001_03008.IMG): the binary only
        // draws this real sprite if g_Opt_GfxDetailShadows==1 (EA 0x51B556). Real sprite,
        // no fill.
        if (shadowsEnabled_) {
            if (gfx::GpuTexture* t = GetAtlasSprite(3007))
                sprites_.DrawSprite(t->Handle(), nullptr, optBtn_.X(), optBtn_.Y(), gfx::kSpriteWhite);
        }
        // Real field caret (Sprite2D_Draw(unk_8EA42C = slot 43) at panel+textWidth+127, y —
        // EA 0x51B34F (ID) / 0x51B445 (PW)): drawn in the SPRITE batch when the field is
        // focused (g_UIEditBoxMgr==1/2 test), CONTINUOUSLY (the binary does NO blink test).
        // Falls back to a text caret "|" (Font batch) if the sprite is unavailable. Same
        // offsets as DrawFieldValue (panel+126,+60 / +126,+95).
        DrawFieldCaretSprite(idBox_, px + 126, py + 60);
        DrawFieldCaretSprite(pwBox_, px + 126, py + 95);
        sprites_.End();
    }

    if (font_.Ready()) {
        font_.BeginBatch();
        // Scene_LoginRender 0x51B020 contains STRICTLY NO text-drawing call outside
        // UI_DrawNumberValue (the ID/PW field values): no title, no field labels, no
        // button/checkbox caption (0 StrTable005_Get calls and 0 Font_*/UI_Text* calls in the
        // entire function — this text is baked into the panel texture unk_8E9368). So we only
        // draw the fields' real value. No invented fallback text.
        DrawFieldValue(idBox_, px + 126, py + 60); // text at panel+126,+60
        DrawFieldValue(pwBox_, px + 126, py + 95); //          panel+126,+95 (masked '*')
        font_.EndBatch();
    }
}

// Draws a field's VALUE (masked with '*' for a password) + a fallback text caret "|". The REAL
// caret is a sprite (slot 43, cf. DrawFieldCaretSprite, drawn in the sprite batch): here we
// only draw the "|" if that sprite is unavailable. Faithful: the binary draws the caret
// CONTINUOUSLY while the field is focused (Sprite2D_Draw unk_8EA42C under g_UIEditBoxMgr==1/2
// test, EA 0x51B322/0x51B418 — WITHOUT blinking); the old blink behavior (frame_/15) was
// invented and has been removed.
void LoginScene::DrawFieldValue(const EditBox& box, int tx, int ty) {
    std::string disp = box.Text();
    if (box.IsPassword())
        disp.assign(disp.size(), kPasswordMaskChar); // Crt_Memset(buf, 42, len)
    if (!disp.empty())
        font_.DrawTextAt(disp.c_str(), tx, ty, kColText);
    if (box.Focused() && !CaretSpriteReady()) {
        const int cx = tx + (disp.empty() ? 0 : font_.MeasureText(disp.c_str()));
        font_.DrawTextAt("|", cx + 1, ty, kColText);
    }
}

// true if the real caret sprite (slot 43 of the UI atlas = unk_8EA42C) is loadable. Determines
// whether the REAL caret will be drawn (sprite batch) or its text fallback "|" (font batch,
// DrawFieldValue) — both batches stay consistent through this same test.
bool LoginScene::CaretSpriteReady() {
    gfx::GpuTexture* t = GetAtlasSprite(43);
    return t && t->Handle();
}

// Real caret: Sprite2D_Draw(unk_8EA42C = slot 43) at panel+textWidth+127, y (EA 0x51B34F ID /
// 0x51B445 PW). Displayed CONTINUOUSLY while the field is focused (faithful, no blinking).
// tx/ty = anchor of the value text (panel+126,+60 / +126,+95); the binary's caret origin is
// +127 (i.e. tx+1) after the text width. No-op if the sprite is unavailable (text fallback
// guaranteed by DrawFieldValue).
void LoginScene::DrawFieldCaretSprite(const EditBox& box, int tx, int ty) {
    if (!box.Focused() || !sprites_.Ready()) return;
    gfx::GpuTexture* caret = GetAtlasSprite(43);
    if (!caret || !caret->Handle()) return;
    std::string disp = box.Text();
    if (box.IsPassword())
        disp.assign(disp.size(), kPasswordMaskChar);
    const int w = disp.empty() ? 0 : font_.MeasureText(disp.c_str());
    sprites_.DrawSprite(caret->Handle(), nullptr, tx + w + 1, ty, gfx::kSpriteWhite);
}

// Scene_LoginOnMouseDown 0x51B5D0: guarded by this[1]==1 (idle).
void LoginScene::LoginOnMouseDown(int x, int y) {
    if (loginSub_ != LoginSub::Idle) return;
    // Panel origin = REAL size of sprite 14, same as LoginRender/IDA
    // (Scene_LoginOnMouseDown 0x51B5D0: Sprite2D_GetWidth/Height unk_8E9368 at EA 0x51B608/0x51B62B).
    gfx::GpuTexture* panelTex = GetAtlasSprite(14);
    const bool panelValid = panelTex && panelTex->Valid() && panelTex->Width() > 0 && panelTex->Height() > 0;
    const int panelW = panelValid ? static_cast<int>(panelTex->Width())  : kPanelW;
    const int panelH = panelValid ? static_cast<int>(panelTex->Height()) : kPanelH;
    LayoutLogin(screenW_ / 2 - panelW / 2, screenH_ / 2 - panelH / 2);

    // Each EditBox focuses if the click lands inside it, defocuses otherwise.
    idBox_.OnMouseDown(x, y);
    pwBox_.OnMouseDown(x, y);
    focusField_ = idBox_.Focused() ? 1 : (pwBox_.Focused() ? 2 : 0);

    // Buttons: arm the latch (the action fires on release inside the button).
    okBtn_.OnMouseDown(x, y);   // this[3]=1
    exitBtn_.OnMouseDown(x, y); // this[4]=1
    optBtn_.OnMouseDown(x, y);  // this[5]=1
}

// Scene_LoginOnMouseUp 0x51B780: validates OK/Quit/Shadows if still hovered.
// The SetOnClick callbacks (Init) trigger Trigger / return-to-ServerSelect / toggle.
void LoginScene::LoginOnMouseUp(int x, int y) {
    if (loginSub_ != LoginSub::Idle) return;
    // Same real origin as the render: Scene_LoginOnMouseUp 0x51B780,
    // Sprite2D_GetWidth/Height unk_8E9368 at EA 0x51B7B8/0x51B7D7.
    gfx::GpuTexture* panelTex = GetAtlasSprite(14);
    const bool panelValid = panelTex && panelTex->Valid() && panelTex->Width() > 0 && panelTex->Height() > 0;
    const int panelW = panelValid ? static_cast<int>(panelTex->Width())  : kPanelW;
    const int panelH = panelValid ? static_cast<int>(panelTex->Height()) : kPanelH;
    LayoutLogin(screenW_ / 2 - panelW / 2, screenH_ / 2 - panelH / 2);

    okBtn_.OnMouseUp(x, y);   // -> onClick: loginSub_ = Trigger (sub-state 2)
    exitBtn_.OnMouseUp(x, y); // -> onClick: pending_ = ServerSelect (scene_id = 2)
    optBtn_.OnMouseUp(x, y);  // -> onClick: toggle shadowsEnabled_ (0x84DEF8)
}

// Server result code (Net_LoginRequest) -> localized message. StrTable005 ids CORRECTED
// (Docs/TS2_LOGINSCENE_AUDIT.md §3.4) against the real switch(v36) of Scene_LoginUpdate
// (EA 0x51AB3F-0x51AE83): 20 distinct outcomes (1..18+101+102), whereas only 8 cases used to
// be covered here (6/7/8/9/10/13/14/15/16/17/18 wrongly fell back to the generic message).
// Case 11 is an original double message (str369 + str616 as a parameter) — not representable
// by this single const char*: the main message (str369) is returned with a note.
std::string LoginScene::LoginErrText(int result) {
    // REAL StrTable005 messages (game::Str) — EXACT mapping of Scene_LoginUpdate 0x51A8D0
    // switch(v36). (case 11 = original double message str369 + str616 as TITLE on the binary
    // side; this single return gives the main message str369.) No more invented French string.
    switch (result) {
    case 1:   return game::Str(7);
    case 2:   return game::Str(8);
    case 3:   return game::Str(9);
    case 4:   return game::Str(10);
    case 5:   return game::Str(11);
    case 6:   return game::Str(12);
    case 7:   return game::Str(13);
    case 8:   return game::Str(14);
    case 9:   return game::Str(15);
    case 10:  return game::Str(16);
    case 11:  return game::Str(369);
    case 12:  return game::Str(813);
    case 13:  return game::Str(817);
    case 14:  return game::Str(1347);
    case 15:  return game::Str(1349);
    case 16:  return game::Str(229);
    case 17:  return game::Str(1840);
    case 18:  return game::Str(2453);
    case 101: return game::Str(17);
    case 102: return game::Str(18);
    default:  return game::Str(19);
    }
}

} // namespace ts2::ui

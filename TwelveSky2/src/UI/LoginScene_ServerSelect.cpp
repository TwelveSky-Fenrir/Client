// UI/LoginScene_ServerSelect.cpp — ServerSelect scene (Scene_ServerSelect* : 0x518B30 /
// 0x519250 / 0x519780). Mechanical split of LoginScene.cpp (see LoginScene.cpp for the class
// lifecycle/dispatch and LoginScene.h for the full class declaration).
#include "UI/LoginScene.h"
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

inline RECT MakeRect(int x, int y, int w, int h) {
    RECT r; r.left = x; r.top = y; r.right = x + w; r.bottom = y + h; return r;
}

// Point-in-rect test (the binary did not expose this helper; provided locally for the
// server-row/char-slot hit-tests).
inline bool RectContains(const RECT& r, int x, int y) {
    return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

} // namespace

// Scene 2 — ServerSelect (Scene_ServerSelect* : 0x518B30 / 0x519250 / 0x519780)
void LoginScene::ServerSelectUpdate() {
    // Faithful flow (Scene_ServerSelectUpdate 0x518B30, 2-sub-state machine) delegated to
    // game::UpdateServerSelect: the Init sub-state counts 30 frames (0x1E) then switches to
    // Idle. The list is already built in Init() (serverState_.listBuilt=true) -> no rebuild
    // here, only the frame counter advances. Fixed dt (30 FPS, flt_815188).
    game::UpdateServerSelect(serverState_, 1.0f / 30.0f);

    // On the Init->Idle transition, ONE-TIME launch of the server-status worker — faithful to
    // CreateThread(Net_ServerStatusThread 0x518AB0) triggered at the end of sub-state 0 (at
    // frame 30). The (bounded blocking) TCP polling runs on this thread, NEVER in this 30 FPS
    // tick (which would freeze the UI).
    if (serverState_.subState == game::ServerSelectSubState::Idle && !statusThreadLaunched_) {
        LaunchServerStatusThread();
        // Front-end BGM (Scene_ServerSelectUpdate 0x518BF7: Snd_LoadOggToBuffers(
        // "G03_GDATA\D10_WORLDBGM\Z000.BGM", loop)). Loaded+played ONCE here (same Init->Idle
        // transition, statusThreadLaunched_ guarantees uniqueness); continuous loop covering
        // both ServerSelect AND Login. Ogg->PCM decoding via the ogg callback of
        // AudioSystem::Init.
        if (audio::Audio().Available() &&
            bgm_.LoadFromPath("G03_GDATA/D10_WORLDBGM/Z000.BGM", audio::PlayMode::Loop)) {
            bgm_.Play(audio::Audio().MasterVolume());
            TS2_LOG("LoginScene : BGM front-end Z000.BGM chargee et jouee (boucle).");
        }
    }
    // The transition to Login (scene_id=3) is triggered immediately on the server click
    // (ServerSelectOnMouseDown -> game::OnServerClicked), like the binary which writes
    // g_SceneMgr[0]=3 from OnMouseDown — no need to consume UpdateServerSelect's return value
    // here (Update() stops calling this function as soon as pending_ != None).
}

// Server-status worker — reproduces Net_ServerStatusThread 0x518AB0: `for (i =
// dword_16851B0; i <= dword_16851B4; ++i)` (= range [selectedGroupBtnLo..selectedGroupBtnHi])
// polls each server (name, port) and writes maxPopulation/loadStep/currentPopulation. The
// polling (ts2::net::QueryServerStatusLive = Net_QueryServerStatus 0x519CC0, TCP connect +
// recv 17 bytes, BLOCKING bounded by a timeout) runs OUTSIDE the render loop; results are
// published UNDER LOCK (serverMutex_) — the render (ServerSelectRender) reads a protected
// snapshot "as it comes in" (currentPopulation == -1 = still polling).
//
// Equivalent to game::PollServerStatuses (same range, same parsing/write semantics), unrolled
// here by hand to publish EACH server as soon as it answers (incremental) while keeping each
// write atomic with respect to rendering.
void LoginScene::LaunchServerStatusThread() {
    if (statusThreadLaunched_) return;
    statusThreadLaunched_ = true;
    if (!serverHost_.QueryServerStatus) return;

    // Snapshot of the targets (index, name, port) in the active range, under lock. The vector
    // serverState_.servers is FROZEN after BuildServerList() (no reallocation afterward), so
    // the indices stay valid for the entire worker lifetime.
    struct Target { int32_t index; std::string name; uint16_t port; };
    std::vector<Target> targets;
    {
        std::lock_guard<std::mutex> lk(serverMutex_);
        const int32_t lo   = serverState_.selectedGroupBtnLo;
        const int32_t hi   = serverState_.selectedGroupBtnHi;
        const int32_t last = static_cast<int32_t>(serverState_.servers.size()) - 1;
        for (int32_t i = (lo > 0 ? lo : 0); i <= hi && i <= last; ++i) {
            targets.push_back({ i, serverState_.servers[static_cast<size_t>(i)].name,
                                serverState_.servers[static_cast<size_t>(i)].port });
        }
        serverState_.statusThreadActive = true;
    }
    if (targets.empty()) return;

    statusThread_ = std::thread([this, targets] {
        for (const Target& t : targets) {
            const game::ServerStatus st = serverHost_.QueryServerStatus(t.name, t.port); // bounded blocking, outside the lock
            std::lock_guard<std::mutex> lk(serverMutex_);
            if (t.index < 0 || t.index >= static_cast<int32_t>(serverState_.servers.size())) continue;
            game::ServerEntry& s = serverState_.servers[static_cast<size_t>(t.index)];
            if (st.currentPopulation >= 0) {   // 17-byte record received -> update max/load/cur (faithful to Net_QueryServerStatus)
                s.maxPopulation     = st.maxPopulation;
                s.loadStep          = st.loadStep;
                s.currentPopulation = st.currentPopulation;
                // Diagnostic: the server becomes CLICKABLE iff 0 <= pop < max (OnServerClicked).
                TS2_LOG("ServerSelect : statut '%s:%u' recu -> population %d/%d%s.",
                        t.name.c_str(), t.port, st.currentPopulation, st.maxPopulation,
                        (st.currentPopulation < st.maxPopulation) ? " (CLIQUABLE)" : " (PLEIN)");
            } else {                           // failure/disconnect -> -1 (max/load unchanged, faithful)
                s.currentPopulation = -1;
                TS2_WARN("ServerSelect : statut '%s:%u' INJOIGNABLE (sonde -> -1) -> clic serveur "
                         "bloque (fidele Net_QueryServerStatus). Verifier connectivite/serveur.",
                         t.name.c_str(), t.port);
            }
        }
    });
}

// ALIGNED on the geometry ACTUALLY drawn by ts2::ui::ServerSelectRender
// (UI/ServerSelectRender.cpp::Render, loop branch EA 0x5194DD, SingleServer mode ACTUALLY
// ACTIVE, cf. Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md): same panel origin (centered on the
// REAL dimensions of sprite 001_01786.IMG once loaded) + same per-index offsets
// (serverselect_layout::ButtonOffsetX/Y, extracted from ServerSelect_GetButtonX/Y
// 0x519F40/0x51A0A0) + same hit-test size (kButtonW/kButtonH — the real texture is 153x23,
// smaller, cf. the fidelity note in ServerSelectRender.cpp, but the binary does hit-test on
// these nominal button dimensions, not on the texture). The hit-test intentionally uses the
// NOMINAL centering kPanelW/kPanelH (instead of the texture's real dimensions used by
// rendering): ServerRowRect() is called BEFORE ServerSelectRender() has had a chance to
// trigger the first load of the panel texture in certain sequences (e.g. a click on the very
// first frame) — a documented minor fidelity gap, with no observable consequence once the
// panel is loaded (same dimensions).
//
// REAL SCOPE i=0 ONLY (SingleServer mode, cf. the header comment of
// ServerSelectRender()/BuildServerList() below): `i` beyond 0 is neither drawn nor clickable
// in the original binary for the documented launch command (`/0/0/2/1024/768` ->
// g_ServerModeFlag=0 -> a SINGLE server entry built).
RECT LoginScene::ServerRowRect(int i) const {
    const int baseX = screenW_ / 2 - serverselect_layout::kPanelW / 2;
    const int baseY = screenH_ / 2 - serverselect_layout::kPanelH / 2;
    return MakeRect(baseX + serverselect_layout::ButtonOffsetX(i),
                     baseY + serverselect_layout::ButtonOffsetY(i),
                     serverselect_layout::kButtonW, serverselect_layout::kButtonH);
}

// Delegates drawing to ts2::ui::ServerSelectRender (REAL positions/dimensions extracted from
// Scene_ServerSelectRender 0x519250, cf. UI/ServerSelectRender.h) — the FLOW/STATE now comes
// from the faithful module game::ServerSelectState (serverState_), fed by the status worker
// (REAL populations). The renderer never mutates the state it's given. Hit-test (ServerRowRect)
// AND drawing share the SAME geometry (serverselect_layout::ButtonOffsetX/Y) — no more mismatch
// between the click and the grid.
void LoginScene::ServerSelectRender() {
    const POINT mp = CursorClient();

    // Protected copy of the server state: the status worker writes the (int32) populations of
    // serverState_.servers in the background; we take a locked snapshot then render from this
    // copy (no race, no blocking of the worker during GPU calls). Bounds/selection/background
    // come directly from the module (game::BuildServerList: selectedGroupBtnLo/Hi,
    // backgroundImageId, maxPopulation fed by the server -> REAL load bar).
    game::ServerSelectState state;
    {
        std::lock_guard<std::mutex> lk(serverMutex_);
        state = serverState_;
    }

    UiContext ctx;
    ctx.sprites  = &sprites_;
    ctx.font     = &font_;
    ctx.whiteTex = whiteTex_;
    ctx.screenW  = screenW_;
    ctx.screenH  = screenH_;

    // Scene_ServerSelectRender does `if (g_ServerModeFlag) { <big number> } else {
    // <button loop> }` (EA 0x5194CB) — the RENDER branch choice is keyed SOLELY on the flag,
    // NOT on the entry count. Reproduced faithfully via singleServerMode = (serverModeFlag_ != 0):
    //   - flag == 0 (the ONLY mode active for `/0/0/2/1024/768`) -> `else`/loop branch
    //     (singleServerMode=false); BuildServerList() built 1 entry -> 1 button.
    //   - flag != 0 (1/2/MultiChannel) -> "big number" branch (singleServerMode=true), entry 0's
    //     population in large digits (UI_DrawNumberValue), faithful to the binary which does NOT
    //     draw a grid in this case even when the table has 6 channels.
    const bool singleServerMode = (serverModeFlag_ != 0);
    if (sprites_.Ready()) {
        sprites_.Begin();
        ctx.phase = UiPhase::Panels;
        serverSelectRender_.Render(ctx, state, mp.x, mp.y, singleServerMode);
        sprites_.End();
    }
    if (font_.Ready()) {
        font_.BeginBatch();
        ctx.phase = UiPhase::Text;
        serverSelectRender_.Render(ctx, state, mp.x, mp.y, singleServerMode);
        font_.EndBatch();
    }

    // Shared modal MsgBox (dword_1822438), drawn LAST for the scene (UI_RenderAllDialogs
    // 0x5AE2D0) — painted ON TOP of the ServerSelect screen. Draws nothing if not open.
    RenderMsgBox();
}

// Scene_ServerSelectOnMouseDown 0x519780: the real server hit-test loop is
// `for (i = this[15372]; i <= this[15373]; ++i)` (EA 0x519974). Bounded here on the active
// range [selectedGroupBtnLo..selectedGroupBtnHi] of the module (SingleServer mode -> [0,0]).
//
// FAITHFUL FLOW: selection is delegated to game::OnServerClicked (EA 0x5199a2-0x519a3f), which
// accepts the click ONLY if the population is known (currentPopulation >= 0) AND strictly
// < maxPopulation (this[12371], fed by the server via the status worker). As long as the
// server hasn't answered (maxPopulation == 0 / curPop == -1), the click is IGNORED, EXACTLY
// like the binary — no transition to Login on a full server or one whose status hasn't arrived
// yet. OnServerClicked also persists the choice (host.SaveLastServer -> Cfg_SaveLastServer)
// and writes selectedServer (this[15374]).
void LoginScene::ServerSelectOnMouseDown(int x, int y) {
    // Priority modal MsgBox (UI_MsgBox_OnLButtonDown 0x5C0980): consumes the click.
    if (msgBox_.IsOpen()) {
        msgBox_.OnMouseDown([this](int s) { return GetAtlasSprite(s); }, x, y, screenW_, screenH_);
        return;
    }
    {
        std::lock_guard<std::mutex> lk(serverMutex_); // OnServerClicked reads populations written by the worker
        const int hi = static_cast<int>(serverState_.servers.size()) - 1;
        const int lo = serverState_.selectedGroupBtnLo > 0 ? serverState_.selectedGroupBtnLo : 0;
        for (int i = lo; i <= hi && i <= serverState_.selectedGroupBtnHi; ++i) {
            if (RectContains(ServerRowRect(i), x, y)) {
                if (game::OnServerClicked(serverState_, serverHost_, i)) {
                    loginSub_ = LoginSub::Init;      // re-init the login screen
                    pending_  = ts2::Scene::Login;   // scene_id = 3 (the binary writes g_SceneMgr[0]=3 here)
                }
                return; // click consumed (accepted, or rejected due to population)
            }
        }
    }
    // No server hit: arm the action/exit button latch (slot 4, anchor 891,701).
    // Scene_ServerSelectOnMouseDown 0x519A79-0x519AAF: UI_ProjectSpriteToScreen(slot 4,891,701)
    // then Sprite2D_HitTest(unk_8E8DA0) -> this[3]=1 if the cursor is over the sprite.
    UiContext ctx;
    ctx.screenW = screenW_;
    ctx.screenH = screenH_;
    serverSelectRender_.OnActionButtonMouseDown(x, y, ctx);
}

// Scene_ServerSelectOnMouseUp 0x519AC0: on release, if the action button was armed (this[3])
// AND the cursor is still over it, opens the exit confirmation (UI_MsgBox_Open dword_1822438,
// action_id=1, body = StrTable005_Get(g_LangId,1) EA 0x519B31, call EA 0x519B3E). Otherwise
// nothing. When the overlay is already open, routes the click to its Yes/No buttons.
void LoginScene::ServerSelectOnMouseUp(int x, int y) {
    // Priority modal MsgBox (UI_MsgBox_OnLButtonUp 0x5C0A90): OK -> exit action, else close.
    if (msgBox_.IsOpen()) {
        msgBox_.OnMouseUp([this](int s) { return GetAtlasSprite(s); }, x, y, screenW_, screenH_);
        return;
    }
    UiContext ctx;
    ctx.screenW = screenW_;
    ctx.screenH = screenH_;
    if (serverSelectRender_.OnActionButtonMouseUp(x, y, ctx)) {
        // UI_MsgBox_Open(dword_1822438, 1, StrTable005_Get(g_LangId,1), "") @0x519B3E. OK action
        // (type 1) = UI_MsgBox_OnLButtonUp case 1 (EA 0x5C0BEC-0x5C0BFB): logs
        // "[ABNORMAL_END] ( 4 )" (0x7BA830) then g_QuitFlag=1 -> PostQuitMessage(0) (project idiom).
        msgBox_.Open(game::Str(1), 1, [] {
            TS2_LOG("[ABNORMAL_END] ( 4 )");
            PostQuitMessage(0);
        });
    }
}

// Server table construction — FAITHFUL reproduction of Scene_ServerSelectUpdate
// 0x518B30 (branch EA 0x518CF6 `if (g_ServerModeFlag)`), keyed on serverModeFlag_
// (= dword_166918C = GameConfig::buildVariant, 1st `/N/...` token of the command line, parsed
// by WinMain EA 0x4609F1/0x460BAE via Crt_Atoi; cf. LoginScene.h::Init).
//
// RE-VERIFIED by direct decompilation + disassembly (idaTs2, 2026-07-15) — hosts/ports checked
// byte by byte below. The IDB IS AUTHORITATIVE: these values CORRECT
// Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md §2.4, which wrongly listed `141.95.12.155` as the 1st
// MultiChannel channel — it is actually the SingleServer host of mode 2; the real 1st
// MultiChannel channel is `test_ts2_login.co.kr` (EA 0x518F92).
//
// The binary writes the HOSTNAME into a1[371] (g_ServerNameTable) and reuses it AS-IS as the
// connection target (Net_ConnectLoginServer host arg, Scene_LoginUpdate EA 0x51AAEB): there is
// no separate "display name". We reproduce this model (name == host == hostname) instead of a
// fabricated label ("TwelveSky2"/"Channel N").
//
// serverModeFlag_ defaults to 0 (Init param default) = the ONLY mode reached by the documented
// command `/0/0/2/1024/768`. Modes 1/2/MultiChannel are only built if SceneManager wires a
// non-zero buildVariant (cf. "Points to watch" of the session doc) — reproduced here for
// fidelity, NOT because they are active. Notable fidelity point: modes 0/1/2 = A SINGLE server
// (a1[370]=1, port 8088); only the "other" mode (≠0,1,2) builds 6 channels — so "else
// multi-channel" means flag ∉ {0,1,2}.
void LoginScene::BuildServerList() {
    // STRUCTURE/FLOW via the faithful module game::BuildServerList (count, ports, button
    // bounds selectedGroupBtnLo/Hi, selectedServer=-1, background): SingleServer mode for
    // flag ∈ {0,1,2} (1 server, port 8088), MultiChannel otherwise (6 channels, EXACT ports).
    const game::ServerListMode mode =
        (serverModeFlag_ == 0 || serverModeFlag_ == 1 || serverModeFlag_ == 2)
            ? game::ServerListMode::SingleServer
            : game::ServerListMode::MultiChannel;
    game::BuildServerList(serverState_, mode);

    // Injection of the REAL HOSTS (checked byte by byte, idaTs2) into .name: the binary writes
    // the hostname into a1[371] (g_ServerNameTable) and reuses it AS-IS both as the connection
    // target (Net_ConnectLoginServer, Scene_LoginUpdate EA 0x51AAEB) AND for status polling
    // (Net_QueryServerStatus 0x519CC0) — name == host == hostname, no separate display label.
    // game::BuildServerList places a generic name ("12sky2-login..." / "Channel N"); we replace
    // it with the EXACT hostname per mode.
    auto setName = [this](size_t i, const char* host) {
        if (i < serverState_.servers.size()) serverState_.servers[i].name = host;
    };
    switch (serverModeFlag_) {
    case 0: setName(0, net::kLoginHostCom); break; // EA 0x518D77 (.com) — already set by the module
    case 1: setName(0, net::kLoginHostOrg); break; // EA 0x518E2F (.org, "EUTest" variant)
    case 2: setName(0, "141.95.12.155");   break;  // EA 0x518EE7 (literal IP)
    default:                                        // MultiChannel — EA 0x518F6E-0x519198
        setName(0, "test_ts2_login.co.kr"); // EA 0x518F92 / port 10005
        setName(1, "192.168.0.93");         // EA 0x518FEA / port 10205
        setName(2, "192.168.0.93");         // EA 0x519042 / port 10305
        setName(3, "125.61.95.145");        // EA 0x51909A / port 11096
        setName(4, "192.168.0.91");         // EA 0x5190F2 / port 11095
        setName(5, "192.168.0.201");        // EA 0x51914A / port 11092
        break;
    }
    // selectedServer left at -1 by the module (this[15374]); OnServerClicked writes the
    // validated index there, DoLogin() guards against -1 (normal flow: selection before login).
}

// kNet* codes (Net/Login.h) -> localized message (the binary maps to StrTable005). FIXED
// (Docs/TS2_LOGINSCENE_AUDIT.md §3.5): the real switch(v35) of Scene_LoginUpdate 0x51A8D0
// (EA 0x51AB03-0x51AF03) only distinguishes 1..4; EVERYTHING else (5/6/7/12/default) falls
// back to the generic str6 message — including kNetErrHost(12), which used to have (wrongly)
// a dedicated "DNS" message never actually shown by the real client from this screen.
std::string LoginScene::ConnectErrText(int code) {
    // REAL StrTable005 messages (game::Str = StrTable005_Get(g_LangId, id)) — EXACT mapping of
    // Scene_LoginUpdate 0x51A8D0 switch(v35): 1->str2, 2->str3, 3->str4, 4->str5,
    // default->str6. No more invented French string (the 005.DAT asset carries the real
    // localized text).
    switch (code) {
    case net::kNetErrState:      return game::Str(2);
    case net::kNetErrSocketSend: return game::Str(3);
    case net::kNetErrConnect:    return game::Str(4);
    case net::kNetErrRecv:       return game::Str(5);
    default:                     return game::Str(6);
    }
}

} // namespace ts2::ui

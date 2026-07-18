// Game/ServerSelectFlow.h — state flow of the server-selection screen (scene 2).
//
// Faithful rewrite of the FLOW/STATE of:
//   Scene_ServerSelectUpdate        0x518B30  (~1.8 KB, 2-substate machine)
//   Scene_ServerSelectOnMouseDown   0x519780  (group click / server click)
//   Scene_ServerSelectOnMouseUp     0x519AC0  (action button -> back confirmation, out of scope here)
//   Net_ServerStatusThread          0x518AB0  (worker thread: per-server population)
//   Net_QueryServerStatus           0x519CC0  (ping/population of a server)
//   Cfg_SaveLastServer              0x519C40  (persistence of the last choice)
// as decompiled and already documented in Docs/TS2_CLIENT_SHELL.md §2.10 ("Connection &
// server/character selection flow") and RE/shell_findings.json (key "login_flow").
// The IDB (idaTs2, RE/TwelveSky2.exe.i64) remains the source of truth; this module faithfully
// reproduces the flow already extracted there, for lack of direct IDA MCP access in this
// session (idaTs2 not connected here) — no data is invented, any gap/uncertainty
// inherited from the source doc is reported as a TODO below.
//
// ORIGINAL FLOW (faithful to the binary, cf. g_SceneMgr @0x1676180: [+0]=scene_id,
// [+4]=substate, [+8]=frame counter):
//
//   Substate 0 (Init): increments the frame counter; at 30 frames (0x1E):
//     - resets the UI flat (TODO rendering: UI_ResetAllDialogs 0x5AC3F0)
//     - loads BGM Z000.BGM in mode 3 (TODO audio: Snd_LoadOggToBuffers)
//     - picks a random BACKGROUND sprite id (this[168] = 2380 or 2381, Rng_Next()%2,
//       EA 0x518c29) -> files 001_02381.IMG / 001_02382.IMG
//     - builds the server table based on g_ServerListMode/dword_166918C
//       (0/1/2 -> single-server list "12sky2-login.geniusorc.com" port 8088; otherwise ->
//       multi-channel list ports 10005/10205/10305/11096/11095/11092 — EXACT order of the
//       binary, 11096 BEFORE 11095, EA 0x518fac-0x519164)
//     - shuffles the group display order (this[170..], TODO faithful RNG)
//     - starts CreateThread(Net_ServerStatusThread) -> moves to substate 1. The thread
//       queries servers at index [this[15372]..this[15373]] (= dword_16851B0..B4,
//       the SAME words as selectedGroupBtnLo/Hi) and fills maxPopulation/loadStep/
//       currentPopulation of each from a 17-byte status record.
//   Substate 1 (Idle): waits for a click; the status thread fills in
//     populations in the background (server_status[i], -1 = still being queried).
//     Anti-cheat heartbeat (sub_6DE3F7, every 30 frames) IGNORED HERE — out of
//     scope for this mission (see CLAUDE.md: do not reverse-engineer the anticheat).
//   Click on a group/channel: OnGroupClicked -> sel_group=i, re-triggers the group's
//     status query, persists the choice (Cfg_SaveLastServer).
//   Click on a server: OnServerClicked -> if population known (>=0) AND < max
//     capacity -> sel_server=i, TRANSITION ready to Login (scene_id=3); otherwise (server
//     full or still unknown) no effect, exactly like the binary.
//   Release on the action button (OnMouseUp 0x519AC0): opens a modal back
//     confirmation — no state-flow logic here (TODO UI).
//
// SCOPE: pure flow/state logic — NO visual list rendering (sprites,
// localized StrTable labels, row/button layout -> precise TODOs marked
// below), NO real threading/networking (the status "thread" is modeled as
// an injectable synchronous integration point, PollServerStatuses(), to be called
// periodically by the caller, which can wire it to a real thread if it wants to
// reproduce the exact asynchrony), NO direct coupling to ts2::SceneManager
// (the caller reads UpdateServerSelect()/OnServerClicked() and performs the
// transition to ts2::Scene::Login itself).
//
// Self-contained: this module includes ONLY the STL — ServerSelect touches no field of
// GameState.h / ClientRuntime.h / AutoPlaySystem.h (no player/world involved here).
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ts2::game {

// Server-list construction mode (original dword_166918C, also
// used by the GameGuard bootstrap to choose "TwelveSky2EU" vs "TwelveSky2EUTest").
// TODO fidelity: the exact value driving this mode in the real game (0/1/2 vs other) has not
// been confirmed dynamically (see open_questions in shell_findings.json); BOTH
// branches are reproduced, the active-branch choice is left to the caller.
//
// NOTE (2026-07-14 re-audit of UI/LoginScene.cpp, full re-decompilation of WinMain
// 0x4609C0 + Scene_ServerSelectUpdate 0x518B30 + Scene_ServerSelectRender 0x519250):
// SingleServer is NOT a minor branch. WinMain (EA 0x4609F1, then 0x460BAE) sets
// dword_166918C from the 1st `/`-delimited command-line token (Crt_Atoi); with the launch
// command documented here (CLAUDE.md: `TwelveSky2.exe /0/0/2/1024/768`), that token is "0"
// -> SingleServer. Also, Scene_ServerSelectRender draws a grid of MULTIPLE server buttons
// ONLY when dword_166918C==0 (EA 0x5194CB `if (g_ServerModeFlag)`; otherwise falls back to
// a "single number" render, UI_DrawNumberValue, reflecting ONLY server index 0) — so even
// with MultiChannel active, channels beyond index 0 are never rendered as clickable
// buttons. Current consumer: UI/LoginScene.cpp does NOT call this module (BuildServerList()
// below is dead code from LoginScene's view, which builds its own local list) — see
// LoginScene.cpp::BuildServerList()/ServerSelectRender() for detail.
enum class ServerListMode : int32_t {
    SingleServer = 0, // g_ServerListMode == 0, 1, or 2 -> single-server list, fixed port 8088
    MultiChannel = 1, // any other value -> multi-channel list (6 observed ports)
};

// Internal substates of Scene_ServerSelectUpdate (= this[1] of g_SceneMgr in scene 2).
enum class ServerSelectSubState : int32_t {
    Init = 0, // waits 30 frames then (re)builds the list + starts the status thread
    Idle = 1, // list built, status thread active in the background, waiting for a click
};

// One entry of the server table. The binary keeps 4 parallel arrays
// (server_names this[371], server_ports this[10371], server_maxpop this[12371],
// server_status this[14371]); merged here into a single record for readability.
struct ServerEntry {
    std::string name;                   // this[371]  : host name / label (stride 40 bytes in the binary, cf. Net_ServerStatusThread &g_ServerNameTable[40*i])
    uint16_t    port = 0;               // this[10371]: connection port
    int32_t     maxPopulation = 0;      // this[12371]: max capacity — POPULATED by the server (status record bytes 5-8, Net_QueryServerStatus 0x519CC0). 0 before receipt.
    int32_t     loadStep = 0;           // this[13371]: population step per load-bar tier — POPULATED by the server (status record bytes 9-12). 0 before receipt.
    int32_t     currentPopulation = -1; // this[14371]: current population (status record bytes 13-16 = return value); -1 = query in progress/failed
    int32_t     groupIndex = 0;         // group this server belongs to (this[169] groups, 5 server buttons/group: buttons 5*g..5*g+4)
};

// Complete state of the ServerSelect screen (mirrors the relevant portion of
// g_SceneMgr @0x1676180 — see Docs/TS2_CLIENT_SHELL.md for the binary's real offsets).
struct ServerSelectState {
    ServerSelectSubState subState = ServerSelectSubState::Init;
    int32_t frameCounter = 0;          // this[2] : frame counter of the current substate

    bool    listBuilt = false;         // true after the initial table build
    bool    statusThreadActive = false;// true as long as the status query is supposed to be running (TODO real networking)
    int32_t backgroundImageId = 0;     // this[168] : background sprite id, randomly picked 2380/2381 (Rng_Next()%2, EA 0x518c29)

    int32_t groupCount = 0;                // this[169] : number of GROUPS; the binary fixes it to 1 in ALL modes (EA 0x518cff/0x518e0b/0x518f6e)
    std::vector<int32_t> groupOrder;       // this[170..] : shuffled group display order

    std::vector<ServerEntry> servers;      // merged table (see ServerEntry)

    int32_t selectedGroup = -1;        // this[15371] : group currently displayed/highlighted (0 after build)
    int32_t selectedGroupBtnLo = -1;   // this[15372] : low bound of button index (== dword_16851B0, status-thread range). Group click -> 5*group (EA 0x5198ba)
    int32_t selectedGroupBtnHi = -1;   // this[15373] : high bound of button index (== dword_16851B4). Group click -> 5*group+4 for groups 0/1 (EA 0x519904)
    int32_t selectedServer = -1;       // this[15374] : validated server (read by Login to connect)

    // SYNTHETIC bookkeeping (absent from the binary): the binary writes
    // g_SceneMgr[0]=3 directly from OnMouseDown, without going back through Update. To
    // expose an UpdateServerSelect()->bool API decoupled from SceneManager (per the
    // mission), this flag is armed in OnServerClicked() and UpdateServerSelect() consumes
    // it (edge, once) on the next tick.
    bool transitionRequested = false;
};

// --- Network integration point (optional callbacks, nullptr = neutral behavior) ---
// The binary builds the server table directly in memory (no I/O); only the
// population query is asynchronous (CreateThread). So ONLY this integration point is
// exposed, keeping BuildServerList() pure/synchronous.

// Result of Net_QueryServerStatus 0x519CC0 (TCP connect + recv of a 17-byte status
// record). The binary deposits 3 dwords: maxPopulation (bytes 5-8 -> this[12371+i]),
// loadStep (bytes 9-12 -> this[13371+i]), and returns currentPopulation (bytes 13-16 ->
// this[14371+i]). On failure (socket/gethostbyname/connect/incomplete recv) the binary
// returns -1 WITHOUT writing maxPopulation/loadStep (they keep their previous value) —
// modeled here as currentPopulation < 0 = failed/in progress.
struct ServerStatus {
    int32_t maxPopulation     = 0;   // record[5..8]  : server max capacity
    int32_t loadStep          = 0;   // record[9..12] : population step per load-bar tier
    int32_t currentPopulation = -1;  // record[13..16] (return value): -1 = failed/in progress
};

struct ServerSelectHost {
    // Net_QueryServerStatus 0x519CC0 : queries a server (name, port) and returns its
    // status record. currentPopulation < 0 = query still in progress / failed
    // (faithful to -1; maxPopulation/loadStep then ignored by the caller).
    std::function<ServerStatus(const std::string& name, uint16_t port)> QueryServerStatus;

    // Cfg_SaveLastServer 0x519C40 : persists the last group/server chosen (user config,
    // G02_GINFO\010.BIN). Real byte-exact implementation available in
    // Config/GameOptions.h::ts2::config::Cfg_SaveLastServer(int32_t) — since this reference
    // module is not wired into the compiled flow (see file header), the actual wiring
    // lives in UI/LoginScene.cpp::ServerSelectOnMouseDown; a future consumer of
    // ServerSelectFlow.cpp only needs to hook this callback to it.
    std::function<void(int32_t groupIndex, int32_t serverIndex)> SaveLastServer;
};

// Builds/rebuilds the server table (Scene_ServerSelectUpdate, substate 0,
// at 30 frames). Faithful: SingleServer mode -> a single server "12sky2-login.geniusorc.com"
// port 8088 (this[15372]=this[15373]=0); MultiChannel mode -> 6 servers, EXACT ports IN
// ORDER as in the binary 10005/10205/10305/11096/11095/11092 (11096 BEFORE 11095, EA
// 0x518fac-0x519164, this[15373]=2). Also sets backgroundImageId=2380, selectedGroup=0,
// selectedGroupBtnLo=0. `shuffleGroups` is an optional integration point to reproduce
// the binary's group-order shuffle (this[170..], trivial order with 1 group).
void BuildServerList(ServerSelectState& state, ServerListMode mode,
                      const std::function<void(std::vector<int32_t>&)>& shuffleGroups = nullptr);

// Queries (SYNCHRONOUSLY here) the status of the servers in the active button RANGE
// [selectedGroupBtnLo..selectedGroupBtnHi] (= dword_16851B0..B4, exactly what
// Net_ServerStatusThread 0x518AB0 loops over), via host.QueryServerStatus. Applies
// maxPopulation/loadStep/currentPopulation of each server from the returned record (on
// failure, currentPopulation=-1, maxPopulation/loadStep unchanged — faithful). Reproduces
// the thread's role without real threading: it's up to the caller to drive it
// periodically (or launch it on a thread) to reproduce the exact asynchrony.
void PollServerStatuses(ServerSelectState& state, const ServerSelectHost& host);

// Per-frame update (fixed 30 FPS — dt accepted for API consistency with the rest of
// the project, but the original logic counts whole FRAMES, not seconds: one
// call = one fixed step, like App_FrameTick -> cSceneMgr_Update). Advances the
// Init->Idle substate after 30 frames (builds the list along the way if needed) and
// consumes the flag armed by OnServerClicked(). Returns true EXACTLY ONCE, the tick where
// the transition to Login (scene_id=3) is ready to be applied by the caller.
bool UpdateServerSelect(ServerSelectState& state, float dt);

// Click on a group/channel header (before selecting a specific server). Faithful
// (Scene_ServerSelectOnMouseDown 0x519780, "group click" branch, EA 0x5197eb-0x51995b):
// no-op if the group is already selected (guard this[15371] != group); otherwise sets
// selectedGroup=group, selectedGroupBtnLo=5*group, selectedGroupBtnHi=5*group+4 (for
// groups 0 and 1; beyond that btnHi unchanged), persists (host.SaveLastServer), resets the
// range's status to -1, then re-triggers the query (PollServerStatuses).
void OnGroupClicked(ServerSelectState& state, const ServerSelectHost& host, int32_t groupIndex);

// Click on a specific server. Faithful (EA 0x5199a2-0x519a3f): clickable only if
// population is known (currentPopulation >= 0) AND strictly currentPopulation < maxPopulation
// (this[12371], populated by the server). WARNING: NO "maxPopulation<=0" escape hatch
// — as long as the server hasn't returned its capacity (maxPopulation==0), no click is
// validated, EXACTLY like the binary. If valid: records the selection (selectedServer),
// persists the choice (host.SaveLastServer), and arms the transition; returns true
// (transition ready, consumable immediately OR on the next UpdateServerSelect()).
bool OnServerClicked(ServerSelectState& state, const ServerSelectHost& host, int32_t serverIndex);

// Click release on the action/exit button (Scene_ServerSelectOnMouseUp 0x519AC0):
// in the binary, this button is NOT a "back" action but a GAME EXIT — it
// opens the shared confirmation MsgBox UI_MsgBox_Open(dword_1822438, action_id=1, EA
// 0x519B3E) whose "Yes" sets g_QuitFlag=1 (UI_MsgBox_OnLButtonUp case 1, EA 0x5C0BFB).
// This behavior (latch on down + confirmation on up + quit) is wired on the render/input
// side in UI/LoginScene.cpp (ServerSelectOnMouseDown/Up + ExitConfirmRender) and
// UI/ServerSelectRender.cpp (OnActionButtonMouseDown/Up); this stub has no flow
// logic (no ServerSelectState field is modified by the binary here).
inline void OnActionButtonReleased(ServerSelectState& /*state*/) {
}

} // namespace ts2::game

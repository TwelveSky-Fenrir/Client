// Game/ServerSelectFlow.cpp — implementation of the ServerSelect flow state machine.
// See ServerSelectFlow.h for original EAs and flow-discovery detail.
#include "Game/ServerSelectFlow.h"
#include <algorithm> // std::max (clamps the polled button range)

namespace ts2::game {

namespace {

// Initial wait of substate 0 before (re)building the list, EXACTLY
// 30 frames (0x1E) in the binary, like other scene-transition delays
// (see Docs/TS2_CLIENT_SHELL.md: "frame counter ... 0x1E=1s").
constexpr int32_t kInitWaitFrames = 30;

// EXACT ports IN ORDER as found in Scene_ServerSelectUpdate 0x518B30 for the
// multi-channel list (branch taken when g_ServerModeFlag/dword_166918C is neither 0, 1,
// nor 2). WATCH the order: 11096 (this[10374], EA 0x5190b4) precedes 11095 (this[10375],
// EA 0x51910c); the 6th server (11092) is written at SPARSE index 40 (this[10411], EA
// 0x519164), merged here into a dense 6th entry.
constexpr uint16_t kMultiChannelPorts[] = {10005, 10205, 10305, 11096, 11095, 11092};
constexpr int32_t  kMultiChannelCount   = static_cast<int32_t>(sizeof(kMultiChannelPorts) / sizeof(kMultiChannelPorts[0]));

// EXACT port of the single-server list (branch g_ServerModeFlag == 0, 1, or 2). EA 0x518d91.
constexpr uint16_t kSingleServerPort = 8088;

// Login host HARDCODED by the binary into this[371] (string 0x7A96F8, EA
// 0x518d77) for g_ServerModeFlag==0 = net::kLoginHostCom. Reproduced as-is (the binary
// hardcodes this string, no config file — see Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md §2).
constexpr const char* kSingleServerHost = "12sky2-login.geniusorc.com";

// Background sprite id (this[168]): the binary picks 2380 or 2381 (Rng_Next()%2, EA
// 0x518c29-0x518c40). 2380 defaulted here (REAL value, not invented) absent an injected RNG.
constexpr int32_t kDefaultBackgroundImageId = 2380;

} // namespace

void BuildServerList(ServerSelectState& state, ServerListMode mode,
                      const std::function<void(std::vector<int32_t>&)>& shuffleGroups) {
    state.servers.clear();
    state.groupOrder.clear();

    // this[169] = number of GROUPS: the binary fixes it to 1 in ALL branches
    // (EA 0x518cff SingleServer, 0x518e0b mode 1/2, 0x518f6e MultiChannel). MultiChannel
    // "channels" are SERVERS within the single group, not distinct groups.
    state.groupCount = 1;

    if (mode == ServerListMode::SingleServer) {
        ServerEntry e;
        e.name              = kSingleServerHost; // this[371] = "12sky2-login.geniusorc.com" (EA 0x518d77)
        e.port              = kSingleServerPort; // this[10371] = 0x1F98 = 8088 (EA 0x518d91)
        e.maxPopulation     = 0;                 // this[12371] = 0 (EA 0x518dab) — populated later by the server
        e.loadStep          = 0;                 // this[13371] = 0 (EA 0x518db8)
        e.currentPopulation = -1;                // this[14371] = -1 (EA 0x518dc5)
        e.groupIndex        = 0;
        state.servers.push_back(e);
        state.selectedGroupBtnHi = 0;            // this[15373] = 0 (EA 0x518dec): only 1 button visible
    } else {
        for (int32_t i = 0; i < kMultiChannelCount; ++i) {
            ServerEntry e;
            e.name              = "Channel " + std::to_string(i + 1); // label not stored by the binary (graphical buttons)
            e.port              = kMultiChannelPorts[i];
            e.maxPopulation     = 0;
            e.loadStep          = 0;
            e.currentPopulation = -1;
            e.groupIndex        = 0;             // all in the single group 0
            state.servers.push_back(e);
        }
        state.selectedGroupBtnHi = 2;            // this[15373] = 2 (EA 0x5191bf): buttons 0..2 visible
    }

    // Group display order (this[170..170+groupCount-1]): identity by default,
    // shuffled if a shuffler is provided. With 1 group, it's trivially [0].
    state.groupOrder.resize(state.groupCount);
    for (int32_t i = 0; i < state.groupCount; ++i) {
        state.groupOrder[i] = i;
    }
    if (shuffleGroups) {
        shuffleGroups(state.groupOrder);
    }

    state.backgroundImageId  = kDefaultBackgroundImageId; // this[168] = 2380/2381 (Rng_Next()%2, EA 0x518c29)
    state.selectedGroup      = 0;   // this[15371] = 0 (EA 0x518dd2 / 0x5191a5)
    state.selectedGroupBtnLo = 0;   // this[15372] = 0 (EA 0x518ddf / 0x5191b2)
    state.selectedServer     = -1;  // this[15374] = -1 (EA 0x5191cc)
    state.listBuilt          = true;
}

void PollServerStatuses(ServerSelectState& state, const ServerSelectHost& host) {
    if (!host.QueryServerStatus) {
        return;
    }
    // Net_ServerStatusThread 0x518AB0: `for (i = dword_16851B0; i <= dword_16851B4; ++i)`
    // — these two globals ARE the words this[15372]/this[15373] (= selectedGroupBtnLo/Hi).
    // So EXACTLY this button range is polled.
    const int32_t lo = state.selectedGroupBtnLo;
    const int32_t hi = state.selectedGroupBtnHi;
    const int32_t last = static_cast<int32_t>(state.servers.size()) - 1;
    for (int32_t i = std::max(0, lo); i <= hi && i <= last; ++i) {
        ServerEntry& s = state.servers[static_cast<size_t>(i)];
        const ServerStatus st = host.QueryServerStatus(s.name, s.port);
        // Faithful to Net_QueryServerStatus: on success (17-byte record received) the binary
        // writes maxPopulation (a3), loadStep (a4), AND currentPopulation (return value); on
        // failure it returns -1 WITHOUT touching a3/a4 (maxPopulation/loadStep preserved).
        if (st.currentPopulation >= 0) {
            s.maxPopulation     = st.maxPopulation;
            s.loadStep          = st.loadStep;
            s.currentPopulation = st.currentPopulation;
        } else {
            s.currentPopulation = -1;
        }
    }
    state.statusThreadActive = true;
}

bool UpdateServerSelect(ServerSelectState& state, float /*dt*/) {
    switch (state.subState) {
    case ServerSelectSubState::Init: {
        ++state.frameCounter;
        if (state.frameCounter < kInitWaitFrames) {
            return false;
        }
        state.frameCounter = 0;

        // TODO rendering: UI_ResetAllDialogs 0x5AC3F0 (resets the UI flat on scene entry).
        // TODO audio: Snd_LoadOggToBuffers("Z000.BGM", mode 3) + Snd_Play3D if g_BgmEnabled==1.
        if (!state.listBuilt) {
            // TODO fidelity: real mode driven by g_ServerListMode (dword_166918C) on the
            // caller side; SingleServer used by default absent external wiring.
            BuildServerList(state, ServerListMode::SingleServer);
        }

        // CreateThread(Net_ServerStatusThread 0x518AB0) in the binary: here we just
        // arm the flag — it's up to the caller to drive PollServerStatuses()
        // periodically (or on a real thread) to reproduce the exact asynchrony.
        state.statusThreadActive = true;

        state.subState = ServerSelectSubState::Idle;
        return false;
    }
    case ServerSelectSubState::Idle: {
        // Idle: waits for a click (OnGroupClicked / OnServerClicked). The anti-cheat
        // heartbeat (sub_6DE3F7, every 30 frames in the binary) is
        // deliberately IGNORED here — out of scope for this mission (see CLAUDE.md).
        if (state.transitionRequested) {
            state.transitionRequested = false;
            return true;
        }
        return false;
    }
    }
    return false;
}

void OnGroupClicked(ServerSelectState& state, const ServerSelectHost& host, int32_t groupIndex) {
    if (groupIndex < 0 || groupIndex >= state.groupCount) {
        return; // out of range: no effect, faithful to the binary (failed hit-test)
    }
    // Faithful (EA 0x51987e): guard `this[15371] != group` — clicking the already
    // selected group is a no-op (no sound, no status re-query). NOTE: the binary
    // resolves the group via the display order this[i+170] (groupOrder); here groupIndex
    // is already the resolved group id.
    if (groupIndex == state.selectedGroup) {
        return;
    }

    state.selectedGroup = groupIndex;
    // btnLo = 5*group; btnHi = 5*group+4 for groups 0 and 1 (5 server buttons/group,
    // EA 0x5198ba-0x519904). Beyond that (group>=2) the binary does NOT update btnHi.
    state.selectedGroupBtnLo = 5 * groupIndex;
    if (groupIndex == 0 || groupIndex == 1) {
        state.selectedGroupBtnHi = 5 * groupIndex + 4;
    }

    // Cfg_SaveLastServer BEFORE the status re-query (binary's order, EA 0x51990d).
    if (host.SaveLastServer) {
        host.SaveLastServer(groupIndex, state.selectedServer);
    }

    // Resets the status of the whole group's button range to -1 (EA 0x51991b-0x51993d),
    // then re-triggers the query (CreateThread(Net_ServerStatusThread) EA 0x51995b —
    // PollServerStatuses loops over the SAME range [btnLo..btnHi]).
    const int32_t last = static_cast<int32_t>(state.servers.size()) - 1;
    for (int32_t i = std::max(0, state.selectedGroupBtnLo); i <= state.selectedGroupBtnHi && i <= last; ++i) {
        state.servers[static_cast<size_t>(i)].currentPopulation = -1;
    }
    PollServerStatuses(state, host);
}

bool OnServerClicked(ServerSelectState& state, const ServerSelectHost& host, int32_t serverIndex) {
    if (serverIndex < 0 || serverIndex >= static_cast<int32_t>(state.servers.size())) {
        return false; // out of range: no effect
    }

    const ServerEntry& s = state.servers[serverIndex];

    // Faithful: population still unknown (query in progress) -> no selection.
    if (s.currentPopulation < 0) {
        return false;
    }
    // Faithful (EA 0x519a19): selection ONLY if currentPopulation < maxPopulation
    // (this[12371], populated by the server). NO "maxPopulation<=0" escape hatch:
    // as long as capacity hasn't been received (maxPopulation==0), the click is refused,
    // EXACTLY like the binary (0 < 0 is false, so no selection).
    if (s.currentPopulation >= s.maxPopulation) {
        return false;
    }

    state.selectedServer = serverIndex;
    state.transitionRequested = true; // consumed by UpdateServerSelect() -> true

    if (host.SaveLastServer) {
        host.SaveLastServer(state.selectedGroup, serverIndex);
    }

    return true; // transition to Login (scene_id=3) ready
}

} // namespace ts2::game

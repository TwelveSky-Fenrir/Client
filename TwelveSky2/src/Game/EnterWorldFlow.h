// Game/EnterWorldFlow.h — WORLD ENTRY state machine (EnterWorld scene = 5).
//
// Faithful C++ rewrite of Scene_EnterWorldUpdate 0x52BFF0 (dispatched by
// cSceneMgr_Update 0x517BF0 when g_SceneMgr.sceneId == Scene::EnterWorld). SOLE
// source of truth: Hex-Rays decompilation via idaTs2 (see session report).
//
// ACTUAL FLOW DISCOVERED (4 explicit sub-states + 1 terminal failure state):
//
//   0 WaitBeforeUnload  : waits 30 frames (0x1E) then purges residual CharSelect
//                         UI/audio, resets the zone preload index, captures
//                         zoneId-1 as the "previous zone" -> state 1.
//   1 LoadZoneResources : every 10 frames (0xA), calls
//                         World_LoadZoneResource(dword_14A883C, dword_1675A9C, idx) with idx
//                         = 0..19 (raw counter, NOT a lookup table); after the 20th
//                         increment (20 calls) -> state 2. => 200 frames (~6.67 s at 30 FPS).
//                         AUDIT 2026-07-14 (direct decompilation of World_LoadZoneResource
//                         0x4DCB60, see World/WorldMap.h::ResourceKind): `idx` IS
//                         directly the a3 parameter of the dispatch, with NO indirection. The
//                         0x4DCB60 switch only covers values 1..12 (= the 12 existing
//                         ResourceKind values, so WorldMap.h::ResourceKind is ALREADY
//                         COMPLETE, not a subset). idx==0 and idx==13..19 (8 of the 20
//                         values) fall into the original switch's `default`, which does
//                         NOTHING (returns a3 truncated to char, no side effect) — these are
//                         NOT 20 distinct "resource types" but 12 real loads + 8 no-op
//                         iterations that only serve to burn time (fidelity of the 200-frame
//                         timing, not of the loading). Correct wiring on the
//                         host.LoadZoneResource side: cast idx to world::ResourceKind and call
//                         WorldMap::LoadZoneResource(zoneId, kind) for idx∈[1,12]; idx==0/[13,19]
//                         can be ignored without breaking anything (the binary does nothing for
//                         those values either).
//   2 SendEnterRequest  : waits 30 frames (0x1E) then sends the world-entry
//                         request (Net_SendPacket_Op12, opcode 12, 222 bytes). Success -> state 3
//                         (WaitServerAck). Send failure -> error notice (StrTable005
//                         id 67) -> state 4 (Failed).
//   3 WaitServerAck     : PASSIVE wait with a 5000-frame timeout (0x1388, ~166 s). The
//                         client does NOT itself trigger the switch to InGame: it's the
//                         server packet Pkt_EnterWorld (incoming opcode 12, EA 0x464160,
//                         NETWORK — out of scope for this module) that, upon receiving it,
//                         directly writes g_SceneMgr.sceneId=InGame and subState=0. If the ACK
//                         never arrives before the timeout: notice (StrTable005 id 68) ->
//                         state 4 (Failed). update() can therefore ONLY detect the timeout;
//                         the normal exit of this state is observed by the caller via the
//                         scene change (SceneManager), not via the return value.
//   4 Failed            : terminal, no progress (faithful behavior: the original switch's
//                         "default" does nothing and returns).
//
// Known gap / TODO: the original code also, on the state-2 transition, manipulates
// g_SelfMorphNpcId (saving it to dword_1675A94 then overwriting it with the target
// zone, dword_1675A9C) and, upon receiving Pkt_EnterWorld, resets several counters
// (dword_16760D8/DC/E0), recomputes a growth tier (dword_1675D90 = f(g_GrowthIndex))
// and may re-emit pending GM commands (teleport/vault, dword_1675A8C == 5/8/9).
// This part belongs to the morph/teleport system (SkillCombat/MapWarp) and to
// Pkt_EnterWorld itself (network packet): DELIBERATELY NOT reproduced here (out of
// scope for "pure scene flow"), simply documented for later wiring.
//
// GameGuard/anticheat (Ac_*): Scene_EnterWorldUpdate does NOT contain any directly
// (GameGuard polling is done by the OTHER online scenes, see Scene_InGameUpdate on the
// InGameTickFlow side) — nothing to skip here, confirmed by reading the disassembly.
//
// 3D rendering: no rendering in this original function (it is pure logic/state) — no
// rendering TODO to note for this module.
//
// Standalone: this module only includes the STL. Side-effecting actions (UI I/O, network,
// asset loading) are exposed via EnterWorldFlowHost (callbacks), to be wired by the
// caller — NO coupling to Scene/SceneManager.h or to a global singleton.
#pragma once
#include <functional>

namespace ts2::game {

enum class EnterWorldState : int {
    WaitBeforeUnload  = 0, // case 0 @0x52BFF9
    LoadZoneResources = 1, // case 1 @0x52C0CF
    SendEnterRequest  = 2, // case 2 @0x52C149
    WaitServerAck     = 3, // case 3 @0x52C1F3
    Failed            = 4, // default @0x52C232 (terminal state, no dedicated scene code anymore)
};

// Persistent state machine (equivalent to g_SceneMgr.subState/frameCounter while
// sceneId == EnterWorld, + the 2 scratch fields specific to this scene: zone resource
// index (this+15726 originally) and captured previous zone (this+15727)).
struct EnterWorldFlowState {
    EnterWorldState state = EnterWorldState::WaitBeforeUnload;
    int frameCounter       = 0;  // g_SceneMgr.frameCounter (dword_1676188 originally)
    int zoneResourceIndex  = 0;  // 0..19 preload index (this+15726)
    int previousZoneId     = -1; // zoneId - 1 captured on entering LoadZoneResources (this+15727)
};

// Integration points (side effects out of scope for this module — network/UI/assets).
struct EnterWorldFlowHost {
    // UI_ResetAllDialogs(&unk_1821D4C) 0x5AC3F0 + sub_4C1110(0) 0x4C1110 (reset tooltip) +
    // UI_FocusEditBox(&g_UIEditBoxMgr,0) 0x50F4A0 + Snd_ReleaseBuffers 0x6A80D0: grouped
    // purge of residual CharSelect UI and audio. A single hook.
    std::function<void()> ResetUiAndAudio;

    // World_LoadZoneResource(dword_14A883C, zoneId, resourceIndex) 0x4DCB60: `resourceIndex`
    // (0..19) IS DIRECTLY the a3 parameter of the original dispatch (no indirection) —
    // see the audit at the top of the file. Only values 1..12 map to a real
    // world::ResourceKind (World/WorldMap.h) and trigger a load; 0 and
    // 13..19 are faithful no-ops (the original binary does nothing for those
    // values either — `default` switch case). The caller (SceneManager) must cast
    // resourceIndex to world::ResourceKind and call WorldMap::LoadZoneResource(zoneId, kind)
    // directly; this module remains deliberately decoupled from World/WorldMap.h (leaf,
    // STL-only, see "Standalone" note at the top of the file). Blocking on the original side.
    std::function<void(int zoneId, int resourceIndex)> LoadZoneResource;

    // Net_SendPacket_Op12(client, g_AccountName, ..., ...) 0x4B43C0: sends the
    // world-entry request (222 bytes: 128-byte name + 13 bytes + 72 bytes). Return = send
    // success, TESTED by the original code (if(result)) to choose WaitServerAck vs Failed.
    std::function<bool()> SendEnterWorldRequest;

    // UI_NoticeDlg_Open(2, StrTable005_Get(g_LangId, strId), "") 0x5C0280: modal
    // error notice. strId = 67 (Op12 send failure) or 68 (server ACK timeout).
    std::function<void(int strId)> ShowErrorNotice;
};

// Scene_EnterWorldUpdate 0x52BFF0. Call once per frame (30 FPS) while the active scene is
// EnterWorld. `zoneId` = target zone identifier to load (dword_1675A9C originally,
// already resolved upstream by CharSelect/Pkt_GameServerConnectResult — supplied by the
// caller, NOT read from a global here).
//
// Return: false once the Failed state is reached (no further progress possible without
// out-of-scope UI action); true otherwise — INCLUDING during WaitServerAck, where the
// caller must detect the real exit (transition to InGame) via an external channel
// (network dispatch -> scene change), not via this return value.
bool EnterWorldFlow_Update(EnterWorldFlowState& state, const EnterWorldFlowHost& host, int zoneId);

} // namespace ts2::game

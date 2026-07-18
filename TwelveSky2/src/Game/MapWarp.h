// Game/MapWarp.h — FACTION WARP system (return to town / auxiliary teleport).
// Clean C++ rewrite (byte-exact on thresholds/offsets/formulas) of:
//   Map_BeginWarpToFactionTown     0x55C510 (__thiscall(this=g_LocalPlayerSheet, a2=mode))
//   Map_BeginWarpToFactionTownDefault 0x55C740 (no this/mode — "always forced" variant)
//   Map_BeginWarpToMap37           0x55C8A0 (__thiscall(this=g_LocalPlayerSheet) — fixed map 37)
//   Map_BeginWarpToFactionTownEx   0x55C9A0 (__thiscall(this=g_LocalPlayerSheet, a2=mode))
//
// REFERENCED BY (TODOs to resolve via this module):
//   Net/GameHandlers_BossWorld.cpp  — 0x60 ZoneBuffStatus  (TODO Map_BeginWarpToFactionTown(0))
//   Net/GameHandlers_InvDispatch.cpp / GameHandlers_Misc.cpp — 0x58 CultivationDispatch,
//     0x16 SetGameVar (long tail of "value<=0 -> warp" cases)
//   Net/GameVarDispatch.cpp — local stubs Map_BeginWarpToFactionTown[Ex] (0x468370)
//   Game/AutoPlaySystem.h   — host.WarpToFactionTown (Map_BeginWarpToFactionTownEx(0))
//
// SCOPE (mandated by the mission): this module computes the RESOLUTION (target town/NPC,
// coordinates if a resolver is supplied, warp code) and the GUARDS (death/cooldown/
// morph already in progress), and sets the warp INTENT in the "long tail" globals
// (g_Client.Var, same addresses as the binary). When a NetClient is supplied, it now
// sends the actual warp packet (Net_SendWarpRequest = Op20 0x4B5000) AND the auto-hunt
// Op99 (Net_SendAutoHuntSync = 0x4BD140, zero blobs until the auto-hunt config is
// modeled — see MapWarp.cpp EmitAutoHuntSync). STILL OUT OF SCOPE (precise TODO at each
// use site, EA cited): world rendering (World_LoadMap, already written in World/WorldMap.*)
// and the op0/op1 emitters Net_QueueMoveTo/RespawnMove (PlayerCmdController module not implemented).
//
// Derivation of "*(this+1784)" (this=g_LocalPlayerSheet 0x1685748, DWORD index):
//   0x1685748 + 1784*4 = 0x1685748 + 0x1BE0 = 0x1687328.
//   0x1687328 - 0x1687234 (dword_1687234, player table, stride 908/0x38C) = 0xF4 = 244.
//   => This is EXACTLY players[0]+244, the action FSM state field documented in
//   Game/ActionStateMachine.h (CharActionState, entity+244 — not included here to stay
//   self-contained; the numeric value 12 = CharActionState::DeathRespawn is duplicated there
//   as a local constant kDeathRespawnState). g_World.players[0] == self (see GameState.h).
//   The field is read in PlayerEntity::body at offset 220 (244-24, the body starting at
//   +0x18) — same convention as Game/EntityManager.cpp (kPActionState=220).
#pragma once
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/EntityManager.h"
#include <cstdint>

// Forward-decl only (avoids pulling winsock/Net into the ~8 consumers of
// MapWarp.h): the full Net/SendPackets.h include is only done in MapWarp.cpp,
// same proven pattern as Game/ComboPickupTick.
namespace ts2::net { struct NetClient; }

namespace ts2::game {

// Original addresses of the "long tail" globals used by the warp (stable keys
// for g_Client.Var/VarF — same addresses as Net/GameVarDispatch.cpp and
// Net/ClientState.h, not included here to stay self-contained w.r.t. networking).
namespace WarpAddr {
    constexpr uint32_t SelfMorphNpcId  = 0x1675A98; // g_SelfMorphNpcId — player's "current" NPC/town id
    constexpr uint32_t MorphInProgress = 0x1675A88; // g_MorphInProgress — 1 = a morph/warp is already armed
    constexpr uint32_t CooldownLatch   = 0x1675B08; // g_GmCmdCooldownLatch — anti-spam latch (Ex only)
    constexpr uint32_t InvDirtyEnable  = 0x16755AC; // g_InvDirtyEnable — disabled during a move/warp

    // "Warp being armed" block (dword_1675A8C..flt_1675AC8), zeroed by
    // Crt_Memset(&dword_1675AA0, 0, 72) then rewritten field by field in the binary.
    constexpr uint32_t WarpModeCode    = 0x1675A8C; // dword_1675A8C — code sent to the server (3, 7, or 11)
    constexpr uint32_t WarpSub         = 0x1675A90; // dword_1675A90 — always 0 in the 4 functions
    constexpr uint32_t WarpTargetNpc   = 0x1675A9C; // dword_1675A9C — target NPC/town id (== townNpcId);
                                                    //   SAME global as the binary's g_TargetZoneId (Warp_SendTeleport 0x5F5CE0, EA 0x5f5d46)

    constexpr uint32_t WarpFlagA0      = 0x1675AA0; // dword_1675AA0 — always 0
    constexpr uint32_t WarpFlagA4      = 0x1675AA4; // dword_1675AA4 — 0 (forced mode) or 1 (normal mode)
    constexpr uint32_t WarpDelay       = 0x1675AA8; // flt_1675AA8 — always 0.0
    constexpr uint32_t WarpPosX        = 0x1675AAC; // flt_1675AAC
    constexpr uint32_t WarpPosY        = 0x1675AB0; // flt_1675AB0
    constexpr uint32_t WarpPosZ        = 0x1675AB4; // flt_1675AB4
    constexpr uint32_t WarpFacingA     = 0x1675AC4; // flt_1675AC4 — random angle 0..359
    constexpr uint32_t WarpFacingB     = 0x1675AC8; // flt_1675AC8 — mirror of WarpFacingA
} // namespace WarpAddr

// Numeric value of CharActionState::DeathRespawn (Game/ActionStateMachine.h, 0x0C) —
// duplicated here (constant, no cross-include) to test the player's "dead" state.
inline constexpr int32_t kDeathRespawnState = 12;

// Offset of the action state field within PlayerEntity::body (== Game/EntityManager.cpp
// kPActionState). players[0] == self (GameState.h).
inline constexpr std::size_t kSelfActionStateOffset = 220;

// Town coordinate resolution — OUT OF SCOPE (asset data/NPC tables):
//   Motion_GetComboOffsetTable 0x5025E0 (element, npcId) -> vec3, a "combo" table linked
//   to morph animations, fails (returns 0) outside certain contexts;
//   GInfo2_GetVec3 0x4FD4C0 (flt_1555D08, npcId) -> vec3, global NPC info table
//   (systematic fallback in the 3 faction functions). flt_1555D08 = base of an NPC
//   record table loaded from the .IMG files (out of scope for this module).
// The calling layer (which has access to the already-loaded .IMG/NPC tables) can wire
// up this resolver; without it, `x`/`y`/`z` stay at 0 in the resolution (townNpcId stays
// correct and usable by the world renderer to find the position some other way).
class IFactionTownCoordResolver {
public:
    virtual ~IFactionTownCoordResolver() = default;
    // Faithful to the original call: first Motion_GetComboOffsetTable(element, townNpcId),
    // then on failure GInfo2_GetVec3(npcId) — the implementation reproduces either (or
    // both) depending on what's available on the data side. Returns false if no position
    // could be resolved (x/y/z left at 0 by the caller).
    virtual bool ResolveTownCoords(int32_t element, int32_t townNpcId, float& x, float& y, float& z) const = 0;
};

// Concrete action that the binary would have triggered for this resolution (the real
// network/world layer stays out of scope — see TODOs in MapWarp.cpp).
enum class WarpAction : uint8_t {
    None,        // nothing to do (unknown faction, already there [Town/Default], blocked)
    ArmFullWarp, // arms the full warp + Net_SendPacket_Op20 (map teleport)
    MoveInPlace, // already at the right town (Ex only) -> Net_QueueMoveTo/RespawnMove + Op99
};

// Result of resolving a faction warp request — NO rendering/network side effect,
// just the decision + the parameters that a caller (network/world layer) must
// then execute (precise TODO cited in the comment of each relevant field).
struct FactionWarpResolution {
    bool valid = false;   // false if `element` doesn't map to any town (switch default)
    WarpAction action = WarpAction::None;

    // Guards that blocked the request (mutually exclusive in the original trace,
    // exposed separately for caller diagnostics/logging).
    bool blockedByDeath           = false; // *(g_LocalPlayerSheet+1784) tied to the dead/alive state (see derivation at top of file)
    bool blockedByMorphInProgress = false; // g_MorphInProgress already 1
    bool blockedByCooldown        = false; // g_GmCmdCooldownLatch (Ex only, "not already there" branch)

    int32_t element    = 0;  // g_LocalElement passed in
    int32_t townNpcId  = 0;  // target NPC/town id (table below); also serves as the server-side
                              // "zone" identifier for the warp packet (Net_SendPacket_Op20).
    bool     coordsResolved = false; // true if an IFactionTownCoordResolver provided a position
    float x = 0.0f, y = 0.0f, z = 0.0f; // target position (0 if unresolved)

    int32_t warpModeCode = 0; // dword_1675A8C armed (3=forced Town, 7=normal Town/Default/Ex, 11=Map37)
    float   facingDeg    = 0.0f; // flt_1675AC4/AC8 armed (random angle 0..359)
};

// Faction -> EXACT town table (switch(g_LocalElement) identical in the 3 faction
// functions 0x55C510/0x55C740/0x55C9A0): element 0..3 -> town NPC id. Any other
// value (unknown/transient observer element) -> 0 (no town, warp aborted).
// NB: element==3 (observer mode, see Net_OnToggleObserver 0x28) does resolve to 140 —
// consistent with the observer having its own logical "town".
inline int32_t FactionTownNpcId(int32_t element) {
    switch (element) {
    case 0: return 1;
    case 1: return 6;
    case 2: return 11;
    case 3: return 140;
    default: return 0;
    }
}

// Main API.

// Map_BeginWarpToFactionTown 0x55C510 (ex=false) / Map_BeginWarpToFactionTownEx 0x55C9A0
// (ex=true). `mode` == original a2 (always 0 at every observed call site EXCEPT
// Char_TickDeathRespawn, which calls the Ex variant with mode=1 on player death).
// `resolver` may be nullptr (x/y/z stay at 0, see IFactionTownCoordResolver).
// `nc` (trailing, default nullptr): if supplied, the actual network send (Net_SendWarpRequest
// = Op20 0x4B5000) is performed; otherwise the "resolution only" behavior is preserved (all
// current callers pass nullptr). Actual NetClient wiring is left to later fronts.
FactionWarpResolution BeginWarpToFactionTown(int32_t element, bool ex = false, int32_t mode = 0,
                                              const IFactionTownCoordResolver* resolver = nullptr,
                                              ts2::net::NetClient* nc = nullptr);

// Map_BeginWarpToFactionTownDefault 0x55C740 — same faction->town table, BUT without the
// "dead" guard (*(this+1784)!=12): used when the caller already knows the warp must
// happen unconditionally (no caller found in the disassembly — function is
// probably meant for an external entry point/tool, kept for fidelity).
FactionWarpResolution BeginWarpToFactionTownDefault(int32_t element,
                                                     const IFactionTownCoordResolver* resolver = nullptr,
                                                     ts2::net::NetClient* nc = nullptr);

// Map_BeginWarpToMap37 0x55C8A0 — fixed teleport to map/NPC 37, EXACT coordinates
// hardcoded in the binary (6.0, 97.0, -3259.0). Only known caller: sub_4A55E0
// (bare trampoline, exact role not elucidated — probably a tool/debug command
// or a UI shortcut out of scope for normal player gameplay).
FactionWarpResolution BeginWarpToMap37(ts2::net::NetClient* nc = nullptr);

// Warp_SendTeleport 0x5F5CE0 — keyword/summon teleport to one of the 4 zones
// v3[4]={138,139,165,166} (EA 0x5f5ce9/f0/f7/fe). Guarded by (zoneSel<=3 && !g_MorphInProgress,
// EA 0x5f5d1a); body = ArmFullWarp(mode=6, flagA4=1, townNpcId=v3[zoneSel], pos) + Op20(nc,6,
// v3[zoneSel]) (EA 0x5f5d20..0x5f5dd6). `pos` = 3 floats {x,y,z}. If nc!=nullptr, actually sends
// the packet (Net_SendWarpRequest, i32 alias: zoneId >=128 zero-extended). Returns true if armed.
// Called by Warp_ProcessKeyword 0x5F54E0 (12x) and Net_OnSummonSpawn 0x4AA810 (not owned here).
bool Warp_SendTeleport(uint16_t zoneSel, const float* pos, ts2::net::NetClient* nc = nullptr);

} // namespace ts2::game

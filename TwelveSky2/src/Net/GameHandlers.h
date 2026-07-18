// Net/GameHandlers.h — installs the REAL incoming-packet handlers.
//
// Replaces NetSystem's default trace handler with real routing:
//   opcode -> RecvPackets::Xxx::Parse(payload,len) -> update state
//   (game::g_World via EntityManager, game::g_Client via ClientRuntime, UI…).
//
// Routing is split by DOMAIN (one .cpp per family) to allow parallel
// generation without file collisions. Each module implements its own
// RegisterXxxHandlers function; InstallGameHandlers calls them all.
// Fidelity reference: RE/net_handler_notes.md (original per-opcode semantics).
#pragma once
#include "Net/NetSystem.h"
#include "Net/RecvPackets.h"
#include "Net/ClientState.h"   // net::g_GmCmdCooldownLatch (reset to 0 by many handlers)
#include <utility>

namespace ts2::net {

// Helper: registers a typed handler. Parses the payload into T then calls fn(const T&).
//   OnPacket<SpawnNpc>(sys, 0x13, [](const SpawnNpc& p){ ... });
template <class T, class F>
inline void OnPacket(NetSystem& sys, std::uint8_t op, F&& fn) {
    sys.On(op, [fn = std::forward<F>(fn)](std::uint8_t /*op*/,
                                          const std::uint8_t* payload,
                                          std::uint32_t len) mutable {
        fn(T::Parse(payload, static_cast<std::size_t>(len)));
    });
}

// Helper: payload-less handler (trigger opcode). Calls fn().
template <class F>
inline void OnTrigger(NetSystem& sys, std::uint8_t op, F&& fn) {
    sys.On(op, [fn = std::forward<F>(fn)](std::uint8_t, const std::uint8_t*,
                                          std::uint32_t) mutable { fn(); });
}

// --- Domain modules (one .cpp each; split per RE/handler_domains.json) ---
void RegisterEntityHandlers     (NetSystem& sys); // 0x0c/0f/10/11/12/13/15/19/91: entities (EntityManager)
void RegisterInvCellHandlers    (NetSystem& sys); // inventory cell results (buy/sell/combine/move)
void RegisterInvDispatchHandlers(NetSystem& sys); // item mega-dispatchers (enchant/refine/socket/fuse/upgrade/batch)
void RegisterPartyGuildHandlers (NetSystem& sys); // party/guild/alliance/team
void RegisterChatSocialHandlers (NetSystem& sys); // chat/whisper/friends/notices/prompts/dialogs
void RegisterVendorTradeHandlers(NetSystem& sys); // vendor/trade/warehouse/player shop/repair
void RegisterBossWorldHandlers  (NetSystem& sys); // boss/zone/map/instance/battlefield/rankings
void RegisterMiscHandlers       (NetSystem& sys); // game-vars/connection/script/timers/quickslot/pet/skill/misc

// Faithful overrides derived from direct IDA decompilation (ts2-ida-gameplay-core workflow):
// 0x11/0x15/0x16/0x1a. Registered LAST in InstallGameHandlers (replaces the
// simplified versions installed by the domain modules above).
void RegisterCoreOverrideHandlers(NetSystem& sys);

// Installs ALL real handlers on the NetSystem (called by NetSystem::Init).
void InstallGameHandlers(NetSystem& sys);

} // namespace ts2::net

// Net/GameHandlers_Core.cpp — wires the mega-dispatchers reverse-engineered DIRECTLY
// from IDA (ts2-ida-gameplay-core workflow) into the network layer. These
// implementations are more faithful/complete than the simplified versions set up by
// the domain modules (GameHandlers_Misc for 0x16, GameHandlers_Entity for 0x11/0x15):
// this module is therefore registered LAST in InstallGameHandlers to replace them
// (the dispatcher has only one slot per opcode — the last one wins).
//
//   0x11 CharStatDelta       -> game::ApplyCharStatDelta       (32 sub-cases, EA 0x465d90)
//   0x15 OnCombatResult      -> game::ApplyCombatResult        (76-byte block, EA 0x55a380)
//   0x16 SetGameVar          -> game::ApplySetGameVar          (133 cases,    EA 0x468370)
//   0x1a ItemActionDispatch  -> game::ApplyItemActionDispatch  (gap filled, EA 0x46a320)
//   0x5e WorldEntityDispatch -> game::ApplyWorldEntityDispatch (PARTIALLY filled gap,
//        sub-opcodes 1..18/~300, EA 0x494870 — see Net/WorldEntityDispatch.h for the map
//        and the TODOs for the rest of the mega-switch)
//   0x27 QuestInteractResult -> completes (state) the existing GameHandlers_Misc handler
//        (high-level messages already set up there; ApplyQuestInteractResultState is added
//        here: inventory write + quest counters + reward-loop messages).
//
// The raw payloads match byte-for-byte the layouts expected by these functions
// (same offsets as the equivalent RecvPackets structs): they are passed to them
// directly, without going through an intermediate Parse().
#include "Net/GameHandlers.h"
#include "Net/GameVarDispatch.h"
#include "Net/CharStatDeltaDispatch.h"
#include "Net/CombatResultApply.h"
#include "Net/ItemActionDispatch.h"
#include "Net/WorldEntityDispatch.h"
#include "Game/QuestSystem.h"
#include "Game/ClientRuntime.h"

namespace ts2::net {

void RegisterCoreOverrideHandlers(NetSystem& sys) {
    sys.On(0x11, [](std::uint8_t, const std::uint8_t* payload, std::uint32_t len) {
        game::ApplyCharStatDelta(payload, len);
    });
    sys.On(0x15, [](std::uint8_t, const std::uint8_t* payload, std::uint32_t len) {
        game::ApplyCombatResult(payload, len);
    });
    sys.On(0x16, [](std::uint8_t, const std::uint8_t* payload, std::uint32_t len) {
        game::ApplySetGameVar(payload, len);
    });
    sys.On(0x1a, [](std::uint8_t, const std::uint8_t* payload, std::uint32_t len) {
        game::ApplyItemActionDispatch(payload, len);
    });
    sys.On(0x5e, [](std::uint8_t, const std::uint8_t* payload, std::uint32_t len) {
        game::ApplyWorldEntityDispatch(payload, len);
    });

    // 0x27 QuestInteractResult: replicates the high-level messages from
    // GameHandlers_Misc (to avoid depending on registration order), then
    // applies the real state (inventory + counters + reward messages).
    OnPacket<QuestInteractResult>(sys, 0x27, [](const QuestInteractResult& p) {
        using namespace game;
        switch (p.resultCode) {
        case 1: g_Client.msg.System(Str(109)); break;
        case 2: g_Client.msg.System(Str(432)); break;
        case 3: g_Client.msg.System(Str(433)); break;
        case 4: g_Client.msg.System(Str(434)); break;
        case 5: g_Client.msg.System(Str(435)); break;
        case 6: g_Client.msg.System(Str(436)); break;
        case 7: g_Client.msg.System(Str(438)); break;
        case 8: g_Client.msg.System(Str(437)); break;
        case 9: g_Client.msg.System(Str(439)); break;
        default: break;
        }
        game::QuestInteractResultPacket pkt;
        pkt.resultCode = p.resultCode;
        pkt.invRow     = static_cast<int32_t>(p.invRow);
        pkt.invSlot     = p.invSlot;
        pkt.gridX       = p.gridX;
        pkt.gridY       = p.gridY;
        game::ApplyQuestInteractResultState(pkt, g_QuestProgress, g_Client.inv);
    });
}

} // namespace ts2::net

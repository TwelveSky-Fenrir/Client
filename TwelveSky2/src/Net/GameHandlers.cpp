// Net/GameHandlers.cpp — aggregator: installs all handler modules.
#include "Net/GameHandlers.h"
#include "Core/Log.h"

namespace ts2::net {

void InstallGameHandlers(NetSystem& sys) {
    RegisterEntityHandlers(sys);
    RegisterInvCellHandlers(sys);
    RegisterInvDispatchHandlers(sys);
    RegisterPartyGuildHandlers(sys);
    RegisterChatSocialHandlers(sys);
    RegisterVendorTradeHandlers(sys);
    RegisterBossWorldHandlers(sys);
    RegisterMiscHandlers(sys);
    RegisterCoreOverrideHandlers(sys); // faithful override for 0x11/0x15/0x16/0x1a/0x5e (direct IDA decompilation)
    TS2_LOG("Net : 8 modules de handlers + overrides core installes (165/165 opcodes reseau, "
            "0x1a + 0x5e inclus ; 0x5e Net_OnWorldEntityDispatch couvert TRES MAJORITAIREMENT "
            "en interne (familles combo 1-4/Special/Buff/branche/duel/arene/guerre-siege/rang, "
            "sous-opcodes 1-115/201-208/401-429/601-903), restes TODO precis documentes "
            "(110/113/301-302/425-428/500/751/764-770/792-794 ; 116-200 confirme vide), "
            "cf. Net/WorldEntityDispatch.h et Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md).");
}

} // namespace ts2::net

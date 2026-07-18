// Net/GameServerDomains.cpp — implementation of Net_SelectServerDomain 0x53FE90.
// Source of truth = full disassembly (idaTs2, 288 instr. disasm). See the header +
// Docs/TS2_GAMESERVER_DOMAINS.md.
#include "Net/GameServerDomains.h"

namespace ts2::net {

// 1:1 transcription of the `switch(g_ServerModeFlag) { switch(domainId) }` tree proven
// by disasm. Every out-of-range branch falls back to "0.0.0.0" (0x7A9714), like the binary.
std::string ResolveGameServerDomainRaw(int serverMode, int domainId) {
    switch (serverMode) {                                   // switch g_ServerModeFlag @0x53FE99
    case 0:                                                 // LIVE
        if (domainId >= 1 && domainId <= 20)                // switch domainId @0x53FED1 (jpt_53FEE4)
            return kGameServerHostsLive[domainId - 1];      // cases 0x53FEEB..0x54007B
        return kGameServerHostInvalid;                      // @0x53FEB2
    case 1:                                                 // staging @0x540091
    case 2:                                                 // staging @0x5400F9 (same literals)
        if (domainId >= 1 && domainId <= 2)
            return kGameServerHostsStaging[domainId - 1];   // @0x5400D0.. / @0x540138..
        return kGameServerHostInvalid;                      // @0x5400A6 / @0x54010E
    default:                                                // dev/MultiChannel @0x540161
        if (domainId >= 1 && domainId <= 10)                // jpt_54019F : 10 cases, same literal
            return kGameServerHostDev;                      // @0x5401A6..
        return kGameServerHostInvalid;                      // @0x54016D
    }
}

std::string SelectGameServerHost(int domainId, const char* loginFallbackHost) {
    if (!kResolveRealGameHosts)                             // guard OFF -> no prod connection
        return loginFallbackHost ? std::string(loginFallbackHost)
                                 : std::string(kGameServerHostInvalid);
    std::string h = ResolveGameServerDomainRaw(g_ServerMode, domainId);
    if (h == kGameServerHostInvalid && loginFallbackHost)  // out-of-range index -> graceful fallback
        return std::string(loginFallbackHost);
    return h;
}

} // namespace ts2::net

// Net/GameServerDomains.cpp — implémentation de Net_SelectServerDomain 0x53FE90.
// Vérité = désassemblage intégral (idaTs2, disasm 288 instr.). Cf. le header + la doc
// Docs/TS2_GAMESERVER_DOMAINS.md.
#include "Net/GameServerDomains.h"

namespace ts2::net {

// Transcription 1:1 de l'arbre `switch(g_ServerModeFlag) { switch(domainId) }` prouvé au
// disasm. Chaque branche hors-plage retombe sur "0.0.0.0" (0x7A9714), comme le binaire.
std::string ResolveGameServerDomainRaw(int serverMode, int domainId) {
    switch (serverMode) {                                   // switch g_ServerModeFlag @0x53FE99
    case 0:                                                 // LIVE
        if (domainId >= 1 && domainId <= 20)                // switch domainId @0x53FED1 (jpt_53FEE4)
            return kGameServerHostsLive[domainId - 1];      // cases 0x53FEEB..0x54007B
        return kGameServerHostInvalid;                      // @0x53FEB2
    case 1:                                                 // staging @0x540091
    case 2:                                                 // staging @0x5400F9 (mêmes littéraux)
        if (domainId >= 1 && domainId <= 2)
            return kGameServerHostsStaging[domainId - 1];   // @0x5400D0.. / @0x540138..
        return kGameServerHostInvalid;                      // @0x5400A6 / @0x54010E
    default:                                                // dev/MultiChannel @0x540161
        if (domainId >= 1 && domainId <= 10)                // jpt_54019F : 10 cas, même littéral
            return kGameServerHostDev;                      // @0x5401A6..
        return kGameServerHostInvalid;                      // @0x54016D
    }
}

std::string SelectGameServerHost(int domainId, const char* loginFallbackHost) {
    if (!kResolveRealGameHosts)                             // garde OFF -> aucune connexion prod
        return loginFallbackHost ? std::string(loginFallbackHost)
                                 : std::string(kGameServerHostInvalid);
    std::string h = ResolveGameServerDomainRaw(g_ServerMode, domainId);
    if (h == kGameServerHostInvalid && loginFallbackHost)  // index hors-plage -> repli gracieux
        return std::string(loginFallbackHost);
    return h;
}

} // namespace ts2::net

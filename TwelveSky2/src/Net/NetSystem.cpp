// Net/NetSystem.cpp
#include "Net/NetSystem.h"
#include "Net/GameHandlers.h"
#include "Core/Log.h"

namespace ts2::net {

bool NetSystem::Init() {
    if (!NetStartup(nc_)) {
        TS2_ERR("Net : WSAStartup a echoue.");
        return false;
    }
    // Installs the REAL game handlers (156 opcodes -> runtime state via
    // EntityManager / ClientRuntime). Opcodes without a registered handler are
    // simply ignored by the dispatcher (empty handler).
    InstallGameHandlers(*this);
    TS2_LOG("Net : initialise (WSAStartup OK, handlers de jeu armes).");
    return true;
}

void NetSystem::Shutdown() {
    NetCloseSocket(nc_);
    NetCleanup(nc_);
}

void NetSystem::OnSocketMessage(WPARAM wParam, LPARAM lParam) {
    (void)wParam; // = the socket in question (== nc_.sock)
    const std::uint16_t netEvent = WSAGETSELECTEVENT(lParam);
    if (dispatch_.OnSocketEvent(nc_.sock, netEvent) == RecvResult::Closed)
        NetCloseSocket(nc_);
}

} // namespace ts2::net

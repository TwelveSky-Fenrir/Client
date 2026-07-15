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
    // Installe les VRAIS handlers de jeu (156 opcodes -> état runtime via
    // EntityManager / ClientRuntime). Les opcodes sans handler enregistré sont
    // simplement ignorés par le dispatcher (handler vide).
    InstallGameHandlers(*this);
    TS2_LOG("Net : initialise (WSAStartup OK, handlers de jeu armes).");
    return true;
}

void NetSystem::Shutdown() {
    NetCloseSocket(nc_);
    NetCleanup(nc_);
}

void NetSystem::OnSocketMessage(WPARAM wParam, LPARAM lParam) {
    (void)wParam; // = la socket concernée (== nc_.sock)
    const std::uint16_t netEvent = WSAGETSELECTEVENT(lParam);
    if (dispatch_.OnSocketEvent(nc_.sock, netEvent) == RecvResult::Closed)
        NetCloseSocket(nc_);
}

} // namespace ts2::net

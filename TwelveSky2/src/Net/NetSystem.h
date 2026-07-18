// Net/NetSystem.h — glues the socket (NetClient) to the packet dispatcher.
// Reunites what the original client scatters around dword_8156A0: the socket
// object + the receive/dispatch loop (Net_RecvDispatch 0x463040), driven by
// window message 0x401 (WM_USER+1) routed by App_WndProc 0x461930.
#pragma once
#include "Net/NetClient.h"
#include "Net/PacketDispatch.h"

namespace ts2::net {

class NetSystem {
public:
    // WSAStartup + handler registration (default trace for now).
    bool Init();
    void Shutdown();

    // From App_WndProc, on socket message 0x401: pumps recv + drain.
    void OnSocketMessage(WPARAM wParam, LPARAM lParam);

    NetClient&        Client()     { return nc_; }
    PacketDispatcher& Dispatcher() { return dispatch_; }

    // Registers a handler for an incoming opcode (delegates to the dispatcher).
    void On(std::uint8_t opcode, PacketDispatcher::Handler h) {
        dispatch_.SetHandler(opcode, std::move(h));
    }

private:
    NetClient        nc_;
    PacketDispatcher dispatch_;
};

} // namespace ts2::net

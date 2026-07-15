// Net/NetSystem.h — colle le socket (NetClient) au dispatcher de paquets.
// Réunit ce que le client d'origine éparpille autour de dword_8156A0 : l'objet
// socket + la boucle de réception/dispatch (Net_RecvDispatch 0x463040), piloté par
// le message fenêtre 0x401 (WM_USER+1) routé par App_WndProc 0x461930.
#pragma once
#include "Net/NetClient.h"
#include "Net/PacketDispatch.h"

namespace ts2::net {

class NetSystem {
public:
    // WSAStartup + enregistrement des handlers (trace par défaut pour l'instant).
    bool Init();
    void Shutdown();

    // Depuis App_WndProc, sur le message socket 0x401 : pompe recv + drain.
    void OnSocketMessage(WPARAM wParam, LPARAM lParam);

    NetClient&        Client()     { return nc_; }
    PacketDispatcher& Dispatcher() { return dispatch_; }

    // Enregistre un handler pour un opcode entrant (délègue au dispatcher).
    void On(std::uint8_t opcode, PacketDispatcher::Handler h) {
        dispatch_.SetHandler(opcode, std::move(h));
    }

private:
    NetClient        nc_;
    PacketDispatcher dispatch_;
};

} // namespace ts2::net

// Net/NetClient.cpp — primitives socket + XOR du cœur réseau TwelveSky2.
//
// Vérité = désassemblage (idaTs2). Ce fichier implémente les morceaux « bas
// niveau » de l'objet réseau g_NetClient (0x8156A0) :
//   Net_Init 0x462790            -> NetStartup
//   boucle send des builders     -> NetSend   (cf. Net_SendPacket_Op12 0x4B43C0 etc.)
// La connexion/handshake (Net_ConnectLoginServer 0x462870, Net_ConnectGameServer
// 0x462A70) vit dans Net/Login.cpp ; la boucle de réception (Net_RecvDispatch
// 0x463040) dans Net/PacketDispatch.cpp ; la fermeture (Net_CloseSocket 0x463000)
// dans Net/NetClient.h.
#include "Net/NetClient.h"

#include <cstring>

namespace ts2::net {

// ---------------------------------------------------------------------------
// Net_Init 0x462790
//   Crt_Memset(&WSAData, 0, 400);
//   if (WSAStartup(0x202, &WSAData)) return 0;   // échec : objet inchangé
//   *this = 1; *(this+8) = 0; *(this+12) = -1;   // succès
// ---------------------------------------------------------------------------
bool NetStartup(NetClient& nc) {
    WSADATA wsaData;
    std::memset(&wsaData, 0, sizeof(wsaData));       // memset(...,400) — sizeof(WSADATA)=0x190
    if (WSAStartup(0x0202u, &wsaData) != 0)          // MAKEWORD(2,2)
        return false;                                // return 0 (objet non modifié)

    nc.wsaReady   = 1;                               // *this = 1
    nc.loginReady = 0;                               // *(this+8) = 0
    nc.sock       = INVALID_SOCKET;                  // *(this+12) = -1 (0xFFFFFFFF)
    return true;
}

// ---------------------------------------------------------------------------
// Pendant de Net_Init (WSACleanup au shutdown, App_Shutdown 0x462480).
// ---------------------------------------------------------------------------
void NetCleanup(NetClient& nc) {
    NetCloseSocket(nc);
    WSACleanup();
    nc.wsaReady = 0;
}

// ---------------------------------------------------------------------------
// Boucle d'émission des builders Net_Send* (identique à l'octet près) :
//   while (len > 0) {
//     n = send(s, this + off, len, 0);
//     if (n == -1) {
//       if (WSAGetLastError() != 10035) { Net_CloseSocket(&g_NetClient); return 0; }
//       // WSAEWOULDBLOCK : on réessaie le MÊME fragment (busy-retry)
//     } else { len -= n; off += n; }
//   }
// ---------------------------------------------------------------------------
bool NetSend(NetClient& nc, const void* data, int len) {
    const char* p = static_cast<const char*>(data);
    int off = 0;
    while (len > 0) {
        int n = send(nc.sock, p + off, len, 0);
        if (n == SOCKET_ERROR) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) { // != 10035 : erreur fatale
                NetCloseSocket(nc);
                return false;
            }
            continue;                                  // WSAEWOULDBLOCK : re-tenter le fragment
        }
        len -= n;
        off += n;
    }
    return true;
}

} // namespace ts2::net

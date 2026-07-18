// Net/NetClient.cpp — socket + XOR primitives at the core of TwelveSky2 networking.
//
// Source of truth = disassembly (idaTs2). This file implements the "low
// level" pieces of the network object g_NetClient (0x8156A0):
//   Net_Init 0x462790            -> NetStartup
//   builder send loop            -> NetSend   (cf. Net_SendPacket_Op12 0x4B43C0 etc.)
// The connection/handshake (Net_ConnectLoginServer 0x462870, Net_ConnectGameServer
// 0x462A70) lives in Net/Login.cpp; the receive loop (Net_RecvDispatch
// 0x463040) in Net/PacketDispatch.cpp; the close (Net_CloseSocket 0x463000)
// in Net/NetClient.h.
#include "Net/NetClient.h"

#include <cstring>

namespace ts2::net {

// ---------------------------------------------------------------------------
// Net_Init 0x462790
//   Crt_Memset(&WSAData, 0, 400);
//   if (WSAStartup(0x202, &WSAData)) return 0;   // failure: object unchanged
//   *this = 1; *(this+8) = 0; *(this+12) = -1;   // success
// ---------------------------------------------------------------------------
bool NetStartup(NetClient& nc) {
    WSADATA wsaData;
    std::memset(&wsaData, 0, sizeof(wsaData));       // memset(...,400) — sizeof(WSADATA)=0x190
    if (WSAStartup(0x0202u, &wsaData) != 0)          // MAKEWORD(2,2)
        return false;                                // return 0 (object unmodified)

    nc.wsaReady   = 1;                               // *this = 1
    nc.loginReady = 0;                               // *(this+8) = 0
    nc.sock       = INVALID_SOCKET;                  // *(this+12) = -1 (0xFFFFFFFF)
    return true;
}

// ---------------------------------------------------------------------------
// Counterpart of Net_Init (WSACleanup at shutdown, App_Shutdown 0x462480).
// ---------------------------------------------------------------------------
void NetCleanup(NetClient& nc) {
    NetCloseSocket(nc);
    WSACleanup();
    nc.wsaReady = 0;
}

// ---------------------------------------------------------------------------
// Send loop for the Net_Send* builders (byte-exact):
//   while (len > 0) {
//     n = send(s, this + off, len, 0);
//     if (n == -1) {
//       if (WSAGetLastError() != 10035) { Net_CloseSocket(&g_NetClient); return 0; }
//       // WSAEWOULDBLOCK: retry the SAME fragment (busy-retry)
//     } else { len -= n; off += n; }
//   }
// ---------------------------------------------------------------------------
bool NetSend(NetClient& nc, const void* data, int len) {
    const char* p = static_cast<const char*>(data);
    int off = 0;
    while (len > 0) {
        int n = send(nc.sock, p + off, len, 0);
        if (n == SOCKET_ERROR) {
            if (WSAGetLastError() != WSAEWOULDBLOCK) { // != 10035: fatal error
                NetCloseSocket(nc);
                return false;
            }
            continue;                                  // WSAEWOULDBLOCK: retry the fragment
        }
        len -= n;
        off += n;
    }
    return true;
}

} // namespace ts2::net

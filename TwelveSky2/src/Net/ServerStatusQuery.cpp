// Net/ServerStatusQuery.cpp — faithful implementation of Net_QueryServerStatus 0x519CC0.
//
// Truth = disassembly (idaTs2). Confirmed decompilation of 0x519CC0:
//   s = socket(2,1,0);                         // AF_INET / SOCK_STREAM
//   if (s == -1) return -1;
//   sa.sa_family = 2;
//   host = gethostbyname(name); if (!host) return -1;
//   memcpy(&sa.sa_data[2], host->h_addr_list[0], 4);
//   *(u16*)sa.sa_data = ntohs(port);
//   if (connect(s, &sa, 16) == -1) { WSAGetLastError(); closesocket(s); return -1; }
//   for (v7=0; (n=recv(s, &buf[v7], 1000-v7, 0)) > 0; ) {
//       v7 += n;
//       if (v7 == 17) { closesocket(s);
//           memcpy(outMaxPop,   buf+5,  4);   // maxPopulation
//           memcpy(outLoadStep, buf+9,  4);   // loadStep
//           memcpy(&ret,        buf+13, 4);   // currentPopulation = return value
//           return ret; } }
//   closesocket(s); return -1;
//
// The client SENDS NOTHING: it connects and the server pushes 17 bytes.
#include "Net/ServerStatusQuery.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace ts2::net {

namespace {

// Idempotent WSAStartup. The network core (NetClient::NetStartup / Net_Init 0x462790)
// already calls WSAStartup(2.2) globally while the client runs. But this status
// request can be issued independently (server selection screen); so we guarantee
// Winsock is initialized at least once, WITHOUT ever calling WSACleanup
// (to avoid decrementing the global counter shared with NetClient).
void EnsureWinsock() {
    static bool started = false;
    if (!started) {
        WSADATA wsaData;
        std::memset(&wsaData, 0, sizeof(wsaData));
        if (WSAStartup(0x0202u, &wsaData) == 0) // MAKEWORD(2,2)
            started = true;
    }
}

// Waits for socket `s` to be ready for read OR write (select), at most
// `timeoutMs`. `forWrite` = true to wait for a non-blocking connect to finish,
// false to wait for incoming data. Returns true if ready, false on
// timeout or error.
bool WaitReady(SOCKET s, bool forWrite, uint32_t timeoutMs) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);

    timeval tv;
    tv.tv_sec  = static_cast<long>(timeoutMs / 1000u);
    tv.tv_usec = static_cast<long>((timeoutMs % 1000u) * 1000u);

    // The 1st argument is ignored under Winsock; pass 0.
    int r = forWrite ? select(0, nullptr, &set, nullptr, &tv)
                     : select(0, &set, nullptr, nullptr, &tv);
    return r > 0 && FD_ISSET(s, &set);
}

} // namespace

// ---------------------------------------------------------------------------
// Net_QueryServerStatus 0x519CC0
// ---------------------------------------------------------------------------
LiveServerStatus QueryServerStatusLive(const std::string& host, uint16_t port,
                                       uint32_t timeoutMs) {
    LiveServerStatus st; // maxPop=0, loadStep=0, currentPop=-1, ok=false

    EnsureWinsock();

    // socket(2, 1, 0) — AF_INET / SOCK_STREAM.
    SOCKET s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET)
        return st; // return -1

    // sa.sa_family = 2; address resolved by gethostbyname; port = ntohs(port).
    // NOTE: the binary writes *(u16*)sa_data = ntohs(port) — same byteswap as htons;
    // we reproduce the exact call (ntohs).
    // FIDELITY GAP (documented): gethostbyname is BLOCKING and NOT bounded by
    // `timeoutMs` (only connect/recv are, via select). This matches the binary
    // (Net_QueryServerStatus 0x519CC0 also does a blocking gethostbyname). In practice
    // resolution is near-instant (DNS cache after the 1st lookup) and bounded by the
    // OS DNS timeout (a few seconds) — so LoginScene::Shutdown()'s join cannot
    // stay blocked indefinitely. An async resolution (GetAddrInfoEx) would be
    // needed for a strict DNS timeout, out of scope (not faithful to the binary).
    sockaddr sa{};
    sa.sa_family = AF_INET;
    hostent* he = gethostbyname(host.c_str());
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        closesocket(s);
        return st; // return -1 (gethostbyname failed)
    }
    std::memcpy(&sa.sa_data[2], he->h_addr_list[0], 4);
    *reinterpret_cast<uint16_t*>(sa.sa_data) = ntohs(port);

    // FIDELITY GAP (documented, see header): NON-BLOCKING connect + select to
    // bound the wait to `timeoutMs`. The binary does a plain blocking connect.
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);

    if (connect(s, &sa, 16) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        // non-blocking connect: WSAEWOULDBLOCK = in progress -> wait for write.
        if (err != WSAEWOULDBLOCK || !WaitReady(s, /*forWrite=*/true, timeoutMs)) {
            closesocket(s);
            return st; // return -1 (connect failed / timeout)
        }
        // Check the actual connect result (SO_ERROR).
        int soErr = 0;
        int soLen = sizeof(soErr);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&soErr), &soLen) != 0 || soErr != 0) {
            closesocket(s);
            return st; // return -1
        }
    }

    // Receive: "recv until we have the 17-byte status record" loop.
    // buf[0..4] = ignored header; [5..8]=maxPop, [9..12]=loadStep, [13..16]=curPop.
    // ROBUSTNESS (fidelity-equivalent): each recv is capped to the REMAINING bytes needed
    // (17 - total) instead of 1000 - total. Against the real server (which sends EXACTLY
    // 17 bytes then closes, see RE/probe_server_status.py live 2026-07-15), the behavior
    // is IDENTICAL to the binary (Net_QueryServerStatus 0x519CC0, `v7 == 17` test); but if
    // a server coalesced a larger payload into the same TCP segment, this bound
    // guarantees we stop exactly at 17 (the binary would overshoot 17 and fail by
    // never landing on `== 17`). No byte beyond 17 is required by the
    // protocol (the client closes the socket right after).
    unsigned char buf[17];
    int total = 0;
    while (total < 17) {
        // select before each recv to never block indefinitely.
        if (!WaitReady(s, /*forWrite=*/false, timeoutMs)) {
            closesocket(s);
            return st; // return -1 (timeout: never got 17 bytes)
        }
        int n = recv(s, reinterpret_cast<char*>(buf) + total, 17 - total, 0);
        if (n <= 0) {
            closesocket(s);
            return st; // return -1 (recv<=0: disconnect / error before 17 bytes)
        }
        total += n;
    }
    closesocket(s);
    std::memcpy(&st.maxPopulation,     buf + 5,  4); // [5..8]
    std::memcpy(&st.loadStep,          buf + 9,  4); // [9..12]
    std::memcpy(&st.currentPopulation, buf + 13, 4); // [13..16] = return value
    st.ok = true;
    return st;
}

} // namespace ts2::net

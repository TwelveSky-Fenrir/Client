// Net/NetClient.h — TwelveSky2 client network object (faithful rewrite).
//
// Replica of the global object `dword_8156A0` surveyed in the disassembly
// (see project memory ts2-network-protocol). Only the field SEMANTICS are
// reproduced; the original offsets (32-bit build) are noted for reference.
//
// Project Winsock convention: <winsock2.h> BEFORE <windows.h>.
//
// Network subsystem split (disassembly anchors distributed to keep components
// separated):
//   Net_Init               0x462790  -> NetStartup            (this file / .cpp)
//   Net_CloseSocket        0x463000  -> NetCloseSocket        (this file)
//   builder send loop                -> NetSend               (this file / .cpp)
//   Net_ConnectLoginServer 0x462870  -> ConnectLoginServer    (Net/Login.*)
//   Net_ConnectGameServer  0x462A70  -> ConnectGameServer     (Net/Login.*)
//   Net_RecvDispatch       0x463040  -> PacketDispatcher      (Net/PacketDispatch.*)
//   XOR encryption (byte_8156A4/A5)  -> Cipher                (Net/Cipher.*)
#pragma once
#include <winsock2.h>
#include <windows.h>
#include <cstdint>

#include "Net/Cipher.h"

#pragma comment(lib, "ws2_32.lib")

namespace ts2::net {

// Receive buffer size (offset +32 of the object, 200000 bytes in the binary).
inline constexpr int kRecvBufSize = 200000;

// Network client object — `dword_8156A0`.
//   +4   byte_8156A4  single-byte XOR key
//   +5   byte_8156A5  sequence/session byte
//   +8                "login server handshake done" flag
//   +12  s            active socket
//   +16               sockaddr of the last connect (16 bytes)
//   +32               receive buffer (200000 bytes)
//   +200032           write cursor / accumulated length
struct NetClient {
    /* +0      */ uint32_t wsaReady    = 0;               // Net_Init sets 1 (WSAStartup OK)
    /* +4      */ uint8_t  xorKey      = 0;               // byte_8156A4 — XOR key
    /* +5      */ uint8_t  seq         = 0;               // byte_8156A5 — sequence/session
    /* +8      */ uint32_t loginReady  = 0;               // 1 = login server negotiated
    /* +12     */ SOCKET   sock        = INVALID_SOCKET;  // s
    /* +16     */ sockaddr addr{};                        // sockaddr_in (16 bytes)
    /* +32     */ char     recvBuf[kRecvBufSize] = {};    // receive buffer
    /* +200032 */ uint32_t recvCursor  = 0;               // write index / length

    // Cipher view (key + sequence) — XOR primitive shared with the outgoing
    // builders (Framing::PacketWriter). Snapshot of bytes +4/+5.
    Cipher MakeCipher() const { return Cipher(xorKey, seq); }
    void   StoreCipher(const Cipher& c) { xorKey = c.Key(); seq = c.Seq(); }
};

// --- Single network instance (recovers the binary's globality) --------------
// The binary has only ONE network object: g_NetClient 0x8156A0 (singleton addressed
// DIRECTLY by the 234 builders — Net_SendPacket_Op20 0x4B5000, Op19 0x4B4E70
// @0x4b4fc2 Net_CloseSocket(&g_NetClient), etc. — never received as a parameter). The
// rewrite nests it inside NetSystem::nc_ (== App::net_.nc_) and passes it by
// reference; this pointer restores the "long tail" access for emitters that, in
// the binary, read g_NetClient without receiving it as a parameter (e.g.
// Map_BeginWarpToFactionTown 0x55C510 -> Op20). Set by ConnectLoginServer/
// ConnectGameServer (Net/Login.cpp) on the active object; nullptr until a
// connection has been initiated.
// NB: we store a POINTER (not a 2nd NetClient) — a second object would have neither
// socket nor negotiated XOR key, diverging from the object the handshake fills in.
inline NetClient* g_NetClientPtr = nullptr;                 // &g_NetClient 0x8156A0
inline NetClient* GlobalNetClient() { return g_NetClientPtr; }

// Net_CloseSocket (0x463000): if the login session is active, closes it and
// resets it. Reproduces the binary's behavior byte-exact:
//   if (loginReady) { loginReady = 0; closesocket(sock); sock = -1; }
inline void NetCloseSocket(NetClient& nc) {
    if (nc.loginReady) {
        nc.loginReady = 0;
        closesocket(nc.sock);
        nc.sock = INVALID_SOCKET;
    }
}

// Semantic alias ("Disconnect" as expected by the network API).
inline void NetDisconnect(NetClient& nc) { NetCloseSocket(nc); }

// Net_Init (0x462790): WSAStartup(2.2) then reset to "ready, not connected"
// state (wsaReady=1, loginReady=0, sock=INVALID_SOCKET). The binary
// memset(&WSAData,0,400) before the call. Returns false if WSAStartup fails
// (the binary returns 0 without touching the object).
bool NetStartup(NetClient& nc);

// Counterpart of Net_Init: WSACleanup + socket close (App_Shutdown 0x462480).
void NetCleanup(NetClient& nc);

// Partial send loop common to the 234 Net_Send* builders (C->S). The 'data'
// buffer must ALREADY be framed (9-byte header) and XOR-encrypted by the
// builder; NetSend only pushes the bytes. Tolerates WSAEWOULDBLOCK (10035) by
// retrying the same fragment; any other error closes the socket
// (Net_CloseSocket) and returns false. Uses nc.sock.
//
// NB: distinct from the blocking SendAll of the handshake (Net/Login.cpp) —
// here the socket is already asynchronous (WSAAsyncSelect armed by ConnectGameServer).
bool NetSend(NetClient& nc, const void* data, int len);

// --- Shared account state (set by LoginRequest, re-read by ConnectGameServer) ---
// byte_1669194: ID entered on the login screen, then OVERWRITTEN by the account
//                token (128 bytes) returned by the login server; this token is
//                then sent back to the game server as-is in the auth packet.
inline uint8_t  g_AccountName[128] = {};
// dword_1673194: "local element" (0..3; 3 = independent) appended to the game
// auth packet at offset +137 (cf. Net_ConnectGameServer 0x462A70, read
// &g_LocalElement EA 0x462d5d). SINGLE SOURCE: the binary has only ONE global
// 0x1673194; the rewrite had duplicated it (here + game::g_World.self.element)
// -> the element started at 0 at handshake time. Alias reference onto
// game::g_World.self.element: reading/writing via net::g_LocalElement OR via
// game::g_World.self.element hits the SAME 32-bit integer (signed/unsigned
// aliasing rule respected). Defined in Net/Login.cpp (which includes GameState.h)
// so as not to pull GameState.h into every network TU.
extern uint32_t& g_LocalElement;   // == game::g_World.self.element (dword_1673194)
// dword_1669294: GM level returned by the login server.
inline uint32_t g_GmAuthLevel      = 0;

// --- Secondary password / PIN (login response fields, reified this pass) ---
// dword_16692A4: "PIN wizard required" flag = ABSOLUTE offset recvBuf+0x95 (=149) of
// the Net_LoginRequest 0x51B8E0 response (Crt_Memcpy(&dword_16692A4, recvBuf+0x95, 4)
// EA 0x51BBE7). WARNING: this is NOT offset 148 (that one is relative to the payload,
// which starts at recvBuf+1). Nonzero => Scene_CharSelectUpdate opens the PIN wizard
// (EA 0x51beae). Reset to 0 by secondary opcodes 13/14/15/16 (Net_AccountReq_op13..16).
// NOT a GM/test-account flag (cf. [A12] Game/CharSelectFlow.h).
inline int32_t g_SecondaryPwRequired = 0;
// unk_16692A8: stored 5-byte PIN (C-string), ABSOLUTE offset recvBuf+0x99 (=153) of
// the login response (Crt_Memcpy EA 0x51BBFB). Non-empty => a PIN is ALREADY set on
// the account (wizard in VERIFY=2 mode); empty => SET=1 mode (Crt_Strcmp(unk_16692A8,"")
// EA 0x51bf3d). 5 bytes = the binary's buffer capacity.
inline char g_StoredSecondaryPw[5] = {};

// --- Raw character records (returned by LoginRequest, persisted here) ---
// The Net_LoginRequest response blob (30659 bytes) contains, after the account/GM
// level, 3 RAW character records of 10088 bytes (0x2768) each — `unk_1669380`,
// `unk_166BAE8`, `unk_166E250` in the binary (Crt_Memcpy 0x2768 bytes each time,
// EA 0x51bc56/0x51bc6d/0x51bc84 of Net_LoginRequest 0x51B8E0, RE-CONFIRMED by
// fresh decompilation session 2026-07-14; cross-checked against
// Docs/TS2_LOGINSCENE_AUDIT.md §3.9). The binary reuses these globals directly; here
// they are persisted into a dedicated array (the receive buffer nc.recvBuf, on the
// other hand, IS overwritten by subsequent network calls — cf.
// Net/Login.cpp::LoginRequest). Detailed internal layout (name@20, job@36,
// faction@44, face@48, hairColor@52, power@56, lookPresetId@216, zoneId@5468,
// local position x/y/z@5472/5476/5480): cf. Net/CharSelectPackets.h.
inline constexpr int kCharRecordSize  = 10088; // 0x2768, one record
inline constexpr int kCharRecordCount = 3;     // 3 slots, cf. game::kMaxCharSlots
inline uint8_t g_CharRecords[kCharRecordCount][kCharRecordSize] = {};

} // namespace ts2::net

// Net/Login.h — TwelveSky2 client connection sequence and handshake.
//
// Faithful rewrite of:
//   - Net_ConnectLoginServer  0x462870  (login/version server connection)
//   - Net_LoginRequest        0x51B8E0  (send id/pw, opcode 0x0B)
//   - Net_ConnectGameServer   0x462A70  (game-server authentication handshake)
//
// Encryption = single-byte XOR, key negotiated at handshake.
// Outgoing frame: [nonce1:u32][nonce2_lo:3B][seq:u8@7][opcode:u8@8][payload] XOR key.
#pragma once
#include "Net/NetClient.h"
#include <cstdint>
#include <functional>   // g_LoginNoticeHook (post-login notice, see below)

namespace ts2::net {

// --- Return codes (EXACT values written by the binary) ---
// Common to both connections (ConnectLoginServer / ConnectGameServer):
inline constexpr int kNetOk             = 0;
inline constexpr int kNetErrState       = 1;   // login: already connected / game: login not ready
inline constexpr int kNetErrSocketSend  = 2;   // socket() failed, or game send()/recv() failed
inline constexpr int kNetErrConnect     = 3;   // connect() failed (non-retryable error)
inline constexpr int kNetErrRecv        = 4;   // banner recv() failed
inline constexpr int kNetErrAuthRej1    = 5;   // game: auth response == 1
inline constexpr int kNetErrAsyncSelect = 6;   // game: WSAAsyncSelect failed
inline constexpr int kNetErrAuthRejN    = 7;   // game: auth response != 0 and != 1
inline constexpr int kNetErrHost        = 12;  // gethostbyname() failed
// Specific to LoginRequest (otherwise the returned code is the server's, 0..18):
inline constexpr int kLoginErrSend      = 101; // send() failed
inline constexpr int kLoginErrRecv      = 102; // recv() failed

// 3rd argument of Net_LoginRequest, emitted as 4 bytes LE at [265..268] of the
// opcode 0x0B packet (Crt_Memcpy(v23, &a3, 4u) EA 0x51b9e6). LITERAL value from the
// single caller Scene_LoginUpdate 0x51A8D0: `push 1606Ah` EA 0x51ab0e = 0x1606A = 90218
// (push order __stdcall/retn 10h: a4=&outResult EA 0x51ab0d, a3=1606Ah,
// a2=byte_1669214/password EA 0x51ab13, a1=g_AccountName EA 0x51ab18; the contiguous
// 128-byte buffers 0x1669194/0x1669214 confirm the a1/a2 pairing).
// Likely a client version/build stamp — SEMANTICS UNPROVEN, only the value is.
// CORRECTION: used to read 106 (= 0x6A) — the old comment "106 = 0x6A" betrays the
// original mistake, the LOW byte of 1606Ah read as the integer operand.
// No other caller (xrefs_to 0x51B8E0 -> 1 single site, EA 0x51ab20).
inline constexpr uint32_t kLoginExtra = 90218u; // 0x1606A

// Login/version server hosts (Scene_ServerSelectUpdate 0x518D77 / 0x518E2F).
inline constexpr char kLoginHostCom[] = "12sky2-login.geniusorc.com"; // faithful .com login host (Scene_ServerSelectUpdate 0x518D77); "127.0.0.1" test leftover reverted
inline constexpr char kLoginHostOrg[] = "12sky2-login.geniusorc.org";

// --- "Post-login deltas" notice (Net_LoginRequest 0x51B8E0, EA 0x51bd3e-0x51bd75) ---
// 1-based index into StrTable005 (005.DAT) of the text shown when at least one of
// the 3 record deltas is > 0: `push 6F9h` EA 0x51bd5e = 1785, consumed by
// StrTable005_Get(g_LangId, 1785) 0x4C1D10 (EA 0x51bd68).
inline constexpr int32_t kLoginDeltaNoticeStrId = 1785;

// The binary opens the notice FROM the network layer:
//   UI_NoticeDlg_Open(byte_18225C8, /*type*/1, StrTable005_Get(g_LangId, 1785), /*""*/&String)
//   (0x5C0280, EA 0x51bd75; the 4th argument is the EMPTY string — read_cstring 0x7ec95f -> "").
// The port separates Net and UI: we reproduce the call via this hook, same pattern as
// Game/CharSelectFlow.h::CharSelectHost::ShowNotice (std::function<void(int32_t strId)>,
// wired by UI/LoginScene.cpp onto OpenNotice(game::Str(strId))). game::Str(id) is
// the faithful equivalent of StrTable005_Get (1-based, "" out of bounds).
// ASSUMED GAPS (same ones already documented for ShowNotice): the `type`=1 parameter
// and the empty 4th argument are not modeled (they don't change control flow).
// Hook NOT SET = safe no-op: the notice does not appear, the deltas still apply
// regardless (the binary adds BEFORE testing, cf. EA 0x51bd15 < 0x51bd3e).
extern std::function<void(int32_t strId)> g_LoginNoticeHook;

// Connect to the login/version server: opens the socket, connects (retries on
// CONNREFUSED/NETUNREACH/TIMEDOUT), reads a 17-byte banner then derives the XOR
// key (byte [1] of the banner) and the session byte (key + 127).
// (Net_ConnectLoginServer 0x462870). Returns a kNet* code (0 = success).
int ConnectLoginServer(NetClient& nc, const char* host, uint16_t port);

// Login request (id / password) over the login server socket, opcode 0x0B.
// `username` / `password`: strings (128-byte logical buffers on the client side).
// `extra`: 3rd argument emitted as 4 bytes LE (kLoginExtra on the client).
// `outResult` receives the server code (0 = success, 1..18 = rejection) or 101/102
// on socket error. (Net_LoginRequest 0x51B8E0). Returns the same value as outResult.
// NB: the binary returns in eax either the NEW value of record[0]+16
// (EA 0x51bd0f), or the return of UI_NoticeDlg_Open (EA 0x51bd75) — a DEAD value:
// the single caller Scene_LoginUpdate ignores eax and re-reads the out-param (EA
// 0x51ab25 `mov edx, [ebp+var_4]`). Deliberate, unobservable gap; this port's
// contract (return == outResult) is more useful and strictly equivalent in practice.
int LoginRequest(NetClient& nc, const char* username, const char* password,
                 uint32_t extra, int& outResult);

// Connect to the game server: opens the socket, connects, reads a 5-byte banner,
// derives a local XOR key, builds and sends the 141-byte authentication packet
// (2 nonces + session + opcode 0x0B + 128-byte account token + 4-byte local
// element), reads the 5-byte response, then arms WSAAsyncSelect(WM_USER+1).
// `notifyWnd`: window receiving the async socket notifications.
// (Net_ConnectGameServer 0x462A70). Requires nc.loginReady != 0.
// Returns a kNet* code (0 = success).
int ConnectGameServer(NetClient& nc, const char* host, uint16_t port, HWND notifyWnd);

} // namespace ts2::net

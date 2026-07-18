// Net/Login.cpp — faithful implementation of the TwelveSky2 connection sequence.
//
// Source of truth = disassembly (idaTs2). Anchors:
//   Net_ConnectLoginServer 0x462870, Net_LoginRequest 0x51B8E0,
//   Net_ConnectGameServer 0x462A70, Net_CloseSocket 0x463000, Rng_Next 0x7603FD.
#include "Net/Login.h"
#include "Core/Types.h"   // ts2::kWM_Socket (WM_USER+1 = 0x401)
#include "Game/GameState.h" // game::g_World.self.element = dword_1673194 (single source of the local element)
#include "Net/Rng.h"      // DefaultRng() — SINGLE _holdrand stream (Rng_Next 0x7603FD)

#include <cstdint>
#include <cstring>
#include <functional>

namespace ts2::net {

// Definition of the alias reference declared `extern` in NetClient.h. Binds
// net::g_LocalElement to game::g_World.self.element (int, same address as
// dword_1673194 in the binary). reinterpret_cast<uint32_t&> of an int: signed/
// unsigned aliasing is allowed (same representation and size), no UB. The init
// only computes an address on g_World (static-duration object) -> no static
// init-order fiasco, the address is valid even before its dynamic constructor.
// Eliminates the duplication that left the element at 0 at handshake time
// (cf. Net_ConnectGameServer 0x462A70, &g_LocalElement EA 0x462d5d).
uint32_t& g_LocalElement = reinterpret_cast<uint32_t&>(game::g_World.self.element);

// Definition of the notice hook declared `extern` in Login.h (UI_NoticeDlg_Open
// 0x5C0280, called by Net_LoginRequest 0x51B8E0 EA 0x51bd75). Null by default:
// to be set by the UI layer (UI/LoginScene.cpp) — file NOT owned by this front.
std::function<void(int32_t strId)> g_LoginNoticeHook;

namespace {

// Rng_Next 0x7603FD = MSVC CRT rand(), instruction for instruction:
//   Ptd  = Crt_GetPtd();                  // 0x76D464 (_getptd_noexit)  EA 0x7603fd
//   v1   = 214013 * Ptd[5] + 2531011;     // Ptd[5] = _holdrand (+20)   EA 0x76040b
//   Ptd[5] = v1;                          //                            EA 0x760411
//   return HIWORD(v1) & 0x7FFF;           // RAND_MAX = 32767           EA 0x76041e
// SINGLE SHARED STREAM: the binary has only one _holdrand (per thread), seeded by
// srand(time(NULL)) in App_Init 0x461C20 (EA 0x461C35 time / 0x461C3E srand).
// Network nonces, the ServerSelect/CharSelect backgrounds, spawn rotation
// (Rng_Next() % 360), and the initial job (% 3) ALL draw from the same sequence.
// We therefore tap net::DefaultRng() (Net/Rng.h), which rematerializes this single
// _holdrand — and NOT std::rand(), which here would be a SECOND independent stream
// (order/value drift vs the binary).
inline int RngNext() { return DefaultRng().Next(); }

// The two header nonces = product of two draws % 10000 (int, max 99,980,001 —
// no overflow). CONSUMPTION ORDER from the binary (Net_LoginRequest 0x51B8E0):
//   v4  = Rng_Next() % 10000;           // draw 1   EA 0x51b91f
//   v8  = Rng_Next() % 10000 * v4;      // draw 2   EA 0x51b931  -> nonce1
//   v5  = Rng_Next() % 10000;           // draw 3   EA 0x51b944
//   v12 = Rng_Next() % 10000 * v5;      // draw 4   EA 0x51b956  -> nonce2
// Same order here (commutative product -> identical values).
inline void MakeNonces(uint32_t& nonce1, uint32_t& nonce2) {
    int a = RngNext() % 10000;
    int b = RngNext() % 10000;
    nonce1 = static_cast<uint32_t>(a * b);
    int c = RngNext() % 10000;
    int d = RngNext() % 10000;
    nonce2 = static_cast<uint32_t>(c * d);
}

// Copies a string into a 128-byte zero-filled field. Reproduces byte-for-byte the
// client buffer, which Scene_LoginUpdate (0x51A9F1) zero-initializes via
// Crt_Memset then fills via GetWindowTextA before sending 128 raw bytes.
void CopyField128(uint8_t* dst, const char* src) {
    std::memset(dst, 0, 128);
    if (src) {
        size_t n = 0;
        while (n < 128 && src[n] != '\0') ++n;
        std::memcpy(dst, src, n);
    }
}

// Fills a sockaddr (interpreted as sockaddr_in): AF_INET family, address
// resolved via gethostbyname, port. Returns false if the host cannot be found.
// NB: the binary writes the port via ntohs(port) — same byteswap as htons;
// we reproduce the exact call.
bool FillSockAddr(sockaddr& out, const char* host, uint16_t port) {
    sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(&out);
    sin->sin_family = AF_INET;                       // *(WORD*)(this+16) = 2
    hostent* he = gethostbyname(host);
    if (!he) return false;
    std::memcpy(&sin->sin_addr, he->h_addr_list[0], 4); // this+20
    sin->sin_port = ntohs(port);                     // *(WORD*)(this+18) = ntohs(port)
    return true;
}

// True if the connect() error is "retryable" (the binary loops back on it):
// WSAECONNREFUSED (10061), WSAENETUNREACH (10051), WSAETIMEDOUT (10060).
inline bool IsRetryableConnectError(int err) {
    return err == WSAECONNREFUSED || err == WSAENETUNREACH || err == WSAETIMEDOUT;
}

// Blocking receive of exactly `need` bytes into nc.recvBuf (from offset 0).
// Reproduces the loop "for (i=0; i != need; i += n) recv(...)".
// Returns true if `need` bytes were read, false if recv() <= 0.
bool RecvExact(NetClient& nc, int need) {
    for (int i = 0; i != need; ) {
        int n = recv(nc.sock, nc.recvBuf + i, kRecvBufSize - i, 0);
        if (n <= 0) return false;
        i += n;
    }
    return true;
}

// Full blocking send of `len` bytes. Reproduces the loop
// "while (len > 0) send(...)". Returns true if everything was sent, false if
// send() == SOCKET_ERROR.
bool SendAll(SOCKET s, const uint8_t* buf, int len) {
    int off = 0;
    while (len > 0) {
        int n = send(s, reinterpret_cast<const char*>(buf) + off, len, 0);
        if (n == SOCKET_ERROR) return false;
        len -= n;
        off += n;
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Net_ConnectLoginServer 0x462870
// ---------------------------------------------------------------------------
int ConnectLoginServer(NetClient& nc, const char* host, uint16_t port) {
    // Publishes the active object as a global singleton (&g_NetClient 0x8156A0): in the
    // binary the network object is unique and addressed directly by builders that don't
    // receive it as a parameter (cf. Net/NetClient.h g_NetClientPtr). `nc` == App::net_.nc_.
    g_NetClientPtr = &nc;                     // &g_NetClient 0x8156A0
    if (nc.loginReady)                       // *(this+8) already set
        return kNetErrState;                 // *a4 = 1

    // The binary writes socket/address directly into the object (this+12 / this+16).
    for (;;) {
        // socket(2,1,0): the binary pushes protocol 0 (push 0; protocol
        // 0x46289d), not IPPROTO_TCP. Result stored in the object at [ecx+0Ch]
        // (EA 0x4628ac). Protocol 0 = default protocol for SOCK_STREAM = TCP.
        nc.sock = socket(AF_INET, SOCK_STREAM, 0); // socket(2,1,0)
        if (nc.sock == INVALID_SOCKET)
            return kNetErrSocketSend;         // *a4 = 2

        if (!FillSockAddr(nc.addr, host, port))
            return kNetErrHost;               // *a4 = 12

        if (connect(nc.sock, &nc.addr, sizeof(sockaddr)) != SOCKET_ERROR)
            break;

        int err = WSAGetLastError();
        if (!IsRetryableConnectError(err)) {
            closesocket(nc.sock);
            return kNetErrConnect;             // *a4 = 3
        }
        closesocket(nc.sock);                  // otherwise: close and retry
    }

    // Banner: exactly 17 bytes.
    if (!RecvExact(nc, 17)) {
        closesocket(nc.sock);
        return kNetErrRecv;                    // *a4 = 4
    }

    // Derivation: key = byte [1] of the banner; session = key + 127.
    uint8_t keyByte = static_cast<uint8_t>(nc.recvBuf[1]);
    nc.xorKey     = keyByte;                   // *(this+4)
    nc.seq        = static_cast<uint8_t>(keyByte + 127); // *(this+5)
    nc.loginReady = 1;                         // *(this+8) = 1
    nc.recvCursor = 0;                         // *(this+200032) = 0
    return kNetOk;                             // *a4 = 0
}

// ---------------------------------------------------------------------------
// Net_LoginRequest 0x51B8E0  (opcode 0x0B: id / password)
// ---------------------------------------------------------------------------
int LoginRequest(NetClient& nc, const char* username, const char* password,
                 uint32_t extra, int& outResult) {
    // 269-byte packet: [nonce1:4][nonce2_lo:3][seq:1@7][op:1@8]
    //                  [user:128@9][pass:128@137][extra:4@265].
    uint8_t pkt[269] = {};
    uint32_t nonce1, nonce2;
    MakeNonces(nonce1, nonce2);

    std::memcpy(pkt + 0, &nonce1, 4);          // [0..3]  nonce1
    std::memcpy(pkt + 4, &nonce2, 4);          // [4..7]  nonce2 (byte 7 gets overwritten)
    pkt[7] = nc.seq;                           // [7]     byte_8156A5 (session)
    pkt[8] = 11;                               // [8]     opcode 0x0B
    CopyField128(pkt + 9,   username);         // [9..136]   username
    CopyField128(pkt + 137, password);         // [137..264] password
    std::memcpy(pkt + 265, &extra, 4);         // [265..268] 3rd arg, 4 bytes LE

    for (int i = 0; i < 269; ++i)              // Full XOR with byte_8156A4
        pkt[i] ^= nc.xorKey;

    if (!SendAll(nc.sock, pkt, 269)) {
        NetCloseSocket(nc);                    // Net_CloseSocket(&dword_8156A0)
        outResult = kLoginErrSend;             // *a4 = 101
        return kLoginErrSend;
    }
    ++nc.seq;                                  // ++byte_8156A5

    // Response: 30659-byte blob.
    if (!RecvExact(nc, 30659)) {
        NetCloseSocket(nc);
        outResult = kLoginErrRecv;             // *a4 = 102
        return kLoginErrRecv;
    }

    // Result code = 4 bytes at recvBuf[1] (unencrypted).
    int32_t result;
    std::memcpy(&result, nc.recvBuf + 1, 4);
    outResult = result;                        // *a4 = v24

    if (result == 0) {
        // The server returns the account token (later sent back to the game server)
        // and the GM level.
        std::memcpy(g_AccountName, nc.recvBuf + 5, 128);   // byte_1669194 <- recvBuf+5
        std::memcpy(&g_GmAuthLevel, nc.recvBuf + 133, 4);  // dword_1669294 <- recvBuf+133

        // Secondary password / PIN — REIFIED this pass (ABSOLUTE recvBuf offsets):
        //   dword_16692A4 <- recvBuf+0x95 (=149), 4 bytes   (Crt_Memcpy EA 0x51BBE7)
        //   unk_16692A8   <- recvBuf+0x99 (=153), 5 bytes   (Crt_Memcpy EA 0x51BBFB)
        // Consumed by the CharSelect PIN wizard (hooks IsSecondaryPasswordRequired /
        // HasStoredSecondaryPassword). WARNING: without them, a PIN-enabled account takes
        // the WRONG branch on entering CharSelect (the PIN branch shuffles a block via
        // >=10 Rng_Next draws), which DESYNCS the SHARED _holdrand stream (background,
        // spawn rotation, initial job).
        // See Net/NetClient.h::g_SecondaryPwRequired/g_StoredSecondaryPw and [A12] CharSelectFlow.h.
        std::memcpy(&g_SecondaryPwRequired, nc.recvBuf + 0x95, 4); // dword_16692A4  EA 0x51BBE7
        std::memcpy(g_StoredSecondaryPw,    nc.recvBuf + 0x99, 5); // unk_16692A8    EA 0x51BBFB

        // 3 RAW character records of 10088 bytes each (unk_1669380/unk_166BAE8/
        // unk_166E250 in the binary) — offsets RE-CONFIRMED by fresh decompilation
        // of Net_LoginRequest 0x51B8E0 (EA 0x51bc56/0x51bc6d/0x51bc84,
        // Docs/TS2_LOGINSCENE_AUDIT.md §3.9): 367 / 10456 / 20545 relative to the start
        // of recvBuf. COPIED (not just read) into g_CharRecords, which SURVIVES
        // subsequent network calls — unlike nc.recvBuf, reused/overwritten by
        // ConnectGameServer and then by CharSelectPackets requests. This is the
        // completeness gap documented by the audit (the remaining ~20 KB used to be
        // lost): now persisted, usable by CharSelectPackets::LoadCharacterSlotsFromRecords.
        //
        // Ancillary blob fields (unk_1669298.. dword_16692B8, offsets 137..365 — GM
        // account/secondary password overlay system). The TWO fields driving the PIN
        // wizard (dword_16692A4 @149 + unk_16692A8 @153) are now reified above; the
        // REST of this range (other offsets 137..365) stays out of scope (not persisted).
        std::memcpy(g_CharRecords[0], nc.recvBuf + 367,   kCharRecordSize);
        std::memcpy(g_CharRecords[1], nc.recvBuf + 10456, kCharRecordSize);
        std::memcpy(g_CharRecords[2], nc.recvBuf + 20545, kCharRecordSize);

        // 3 balance deltas added onto record[i]+16 (Net_LoginRequest 0x51B8E0):
        //   dword_1669390[0] += v15  (EA 0x51bd15) — 0x1669390-0x1669380(record[0]) = 16
        //   dword_166BAF8    += v16  (EA 0x51bd26) — 0x166BAF8-0x166BAE8(record[1]) = 16
        //   dword_166E260    += v17  (EA 0x51bd38) — 0x166E260-0x166E250(record[2]) = 16
        // Sources v15/v16/v17 = recvBuf+30634/30638/30642 (EA 0x51bc9a/0x51bcb0/0x51bcc6,
        // MEMORY[0x81CE6A/6E/72] - recvBuf base 0x8156C0 = 0x77AA/0x77AE/0x77B2 = 30634/38/42).
        // The binary reads/writes the record[i] globals directly; here the records are
        // copied into g_CharRecords -> we apply the delta to the same +16 field (int32).
        // SIGNED int32, plain addition: no saturation, no clamping (bare `add`, EA 0x51bd0f/
        // 0x51bd20/0x51bd32). Semantics of record[i]+16: counter displayed in decimal
        // on the CharSelect screen (Scene_CharSelectRender EA 0x51da5d: `imul eax, 2768h`
        // then `mov ecx, ds:dword_1669390[eax]`, formatted "%d" -> UI_DrawNumberValue).
        // The blob closes here: last field at +0x77BF..+0x77C2, total 0x77C3 = 30659
        // = the EXACT bound of the receive loop (EA 0x51bad0). The 5 UNREAD padding
        // bytes (+0x16E/+0x28D7/+0x5040/+0x77A9/+0x77B6) explain the "misaligned"
        // offsets — these are not survey errors.
        int32_t d0, d1, d2;
        std::memcpy(&d0, nc.recvBuf + 30634, 4); // v15 EA 0x51bc9a
        std::memcpy(&d1, nc.recvBuf + 30638, 4); // v16 EA 0x51bcb0
        std::memcpy(&d2, nc.recvBuf + 30642, 4); // v17 EA 0x51bcc6
        auto addAt16 = [](uint8_t* rec, int32_t d) {
            int32_t v; std::memcpy(&v, rec + 16, 4); v += d; std::memcpy(rec + 16, &v, 4);
        };
        addAt16(g_CharRecords[0], d0); // record[0]+16 (dword_1669390)
        addAt16(g_CharRecords[1], d1); // record[1]+16 (dword_166BAF8)
        addAt16(g_CharRecords[2], d2); // record[2]+16 (dword_166E260)

        // Notice if AT LEAST ONE delta > 0 — guard reproduced instruction for
        // instruction (Net_LoginRequest 0x51B8E0):
        //   cmp [ebp+var_404], 0 ; jg loc_51BD59   EA 0x51bd3e/0x51bd45   (d0)
        //   cmp [ebp+var_400], 0 ; jg loc_51BD59   EA 0x51bd47/0x51bd4e   (d1)
        //   cmp [ebp+var_3FC], 0 ; jle loc_51BD7A  EA 0x51bd50/0x51bd57   (d2)
        // SIGNED comparisons (jg/jle): a negative delta does NOT open the notice.
        // FAITHFUL ORDER: the binary adds the 3 deltas (EA 0x51bd0f-0x51bd38) THEN
        // tests — hence the test AFTER the addAt16 calls above.
        // Here the binary calls UI_NoticeDlg_Open(byte_18225C8, 1, StrTable005_Get(
        // g_LangId, 1785), &String/*""*/) (EA 0x51bd68/0x51bd75); the port goes through
        // g_LoginNoticeHook (cf. Login.h), null until UI/LoginScene.cpp sets it.
        if ((d0 > 0 || d1 > 0 || d2 > 0) && g_LoginNoticeHook)
            g_LoginNoticeHook(kLoginDeltaNoticeStrId); // push 6F9h = 1785, EA 0x51bd5e

        // NOT PORTED — the 3 trailing int32 <- recvBuf+30647/30651/30655
        // (EA 0x51bcda/0x51bcee/0x51bd02), verified via xrefs_to on each global:
        //  - unk_1676170 (0x1676170) and unk_1676174 (0x1676174): DEAD STORAGE — their
        //    ONLY xref is the write above, never read back anywhere in the binary.
        //    Omission is definitively correct. (The old justification "no += in
        //    the binary, plain persistence" was LEGALLY WRONG: it's not the
        //    absence of += that disqualifies them, it's that they're write-only.)
        //  - dword_167616C (0x167616C): ALIVE — 5 xrefs, 4 of them OUTSIDE this function,
        //    and it genuinely IS +=-ed elsewhere. Pending warehouse-operation counter:
        //      · Pkt_DispatchStorageResponse 0x58A0F0: `cmp [ebp+var_4C], 90Ah` (2314)
        //        EA 0x58bc63 -> `add edx, 1` EA 0x58bc72 / store EA 0x58bc75;
        //      · UI_NpcShop_OnRDown_Buy 0x5E5000: `cmp ds:dword_167616C, 1 ; jl` EA
        //        0x5e55ba/0x5e55c1 -> if >= 1, system line StrTable005_Get(g_LangId,
        //        0x9F3 /*2547*/) then `jmp loc_5E5671` EA 0x5e55e3 which SKIPS the
        //        entire purchase branch (loc_5E55E8) -> purchase REFUSED;
        //      · UI_MainInventory_OnLButtonUp 0x5B20B0 (EA 0x5bb3d8).
        //    The seed sent AT LOGIN thus has a real gameplay effect (can block
        //    shop purchases right from game entry).
        //    TODO [anchor 0x51bcda]: its natural home is Net/NetClient.h (next to
        //    g_GmAuthLevel) and its 3 consumers are not modeled — files NOT
        //    owned by this front. Residual flagged to the orchestrator.
    }
    return result;
}

// ---------------------------------------------------------------------------
// Net_ConnectGameServer 0x462A70  (game-server authentication handshake)
// ---------------------------------------------------------------------------
int ConnectGameServer(NetClient& nc, const char* host, uint16_t port, HWND notifyWnd) {
    // Same as ConnectLoginServer: publishes the active object as a global singleton
    // (&g_NetClient 0x8156A0). Guarantees GlobalNetClient() is valid before any
    // warp (which only happens in-game, post-handshake).
    g_NetClientPtr = &nc;                      // &g_NetClient 0x8156A0
    if (!nc.loginReady)                        // *(this+8) required
        return kNetErrState;                   // *a4 = 1

    // The binary works on LOCAL variables (socket s, sockaddr v9) and only
    // commits them to the object on full success.
    SOCKET   s = INVALID_SOCKET;
    sockaddr addr{};

    for (;;) {
        // socket(2,1,0): literal protocol 0 in the binary (push 0; protocol
        // 0x462ab1), not IPPROTO_TCP. Result stored in [ebp+s] (EA 0x462abd).
        s = socket(AF_INET, SOCK_STREAM, 0); // socket(2,1,0)
        if (s == INVALID_SOCKET)
            return kNetErrSocketSend;          // *a4 = 2

        if (!FillSockAddr(addr, host, port))
            return kNetErrHost;                // *a4 = 12

        if (connect(s, &addr, sizeof(sockaddr)) != SOCKET_ERROR)
            break;

        int err = WSAGetLastError();
        if (!IsRetryableConnectError(err)) {
            closesocket(s);
            return kNetErrConnect;             // *a4 = 3
        }
        closesocket(s);
    }

    // Banner: 5 bytes -> LOCAL XOR key + session.
    {
        // RecvExact reads into nc.recvBuf (the binary also reads into this+32).
        for (int i = 0; i != 5; ) {
            int n = recv(s, nc.recvBuf + i, kRecvBufSize - i, 0);
            if (n <= 0) {
                closesocket(s);
                return kNetErrRecv;            // *a4 = 4
            }
            i += n;
        }
    }
    uint8_t key  = static_cast<uint8_t>(nc.recvBuf[1]);
    uint8_t sess = static_cast<uint8_t>(key + 127);

    // 141-byte auth packet (Net_ConnectGameServer 0x462A70):
    //   [nonce1:4][nonce2_lo:3][sess:1@7][op=0x0B:1@8][account_token:128@9]
    //   [local_element:4@137].
    //   +9   = byte_1669194 / g_AccountName (Crt_Memcpy EA 0x462d47);
    //   +137 = dword_1673194 / g_LocalElement (Crt_Memcpy &g_LocalElement EA 0x462d5d,
    //          v27 = buf+137, buf = ebp-3F0h -> 0x3F0-0x367 = 0x89 = 137).
    uint8_t pkt[141] = {};
    uint32_t nonce1, nonce2;
    MakeNonces(nonce1, nonce2);

    std::memcpy(pkt + 0, &nonce1, 4);          // [0..3]  nonce1
    std::memcpy(pkt + 4, &nonce2, 4);          // [4..7]  nonce2 (byte 7 overwritten)
    pkt[7] = sess;                             // [7]     session
    pkt[8] = 11;                               // [8]     opcode 0x0B
    std::memcpy(pkt + 9,   g_AccountName, 128);// [9..136]   byte_1669194 (EA 0x462d47)
    // [137..140] local element (Net_ConnectGameServer 0x462A70, Crt_Memcpy
    // &g_LocalElement EA 0x462d5d; v27 = buf+137). g_LocalElement = dword_1673194 =
    // SINGLE SOURCE aliased onto game::g_World.self.element (read HERE as-is, no
    // transformation). In the binary, this value is set just before the handshake
    // by Scene_CharSelectUpdate 0x51BD90 (Crt_Memcpy g_SelfCharInvBlock 0x1673170 <-
    // selected slot's record, EA 0x51c707): g_LocalElement = g_SelfCharInvBlock+0x24
    // = character's charRecord[+36] (field named "job"@36 in CharSelectFlow.h).
    // POPULATING it from the slot record is the responsibility of the CharSelect flow
    // (LoginScene, outside this front's scope): without it, the alias stays 0 even if
    // the duplication is eliminated — cf. NOTE below.
    std::memcpy(pkt + 137, &g_LocalElement, 4);// [137..140] dword_1673194

    for (int j = 0; j < 141; ++j)              // Full XOR with the local key
        pkt[j] ^= key;

    if (!SendAll(s, pkt, 141)) {
        closesocket(s);
        return kNetErrSocketSend;              // *a4 = 2
    }
    ++sess;                                     // ++session after send

    // Response: 5 bytes -> result code in [1..4] (unencrypted).
    {
        for (int i = 0; i != 5; ) {
            int n = recv(s, nc.recvBuf + i, kRecvBufSize - i, 0);
            if (n <= 0) {
                closesocket(s);
                return kNetErrSocketSend;      // *a4 = 2
            }
            i += n;
        }
    }
    int32_t result;
    std::memcpy(&result, nc.recvBuf + 1, 4);
    if (result != 0) {
        closesocket(s);
        return (result == 1) ? kNetErrAuthRej1 : kNetErrAuthRejN; // *a4 = 5 or 7
    }

    // Async socket notification: WM_USER+1, FD_READ | FD_CLOSE events (33).
    if (WSAAsyncSelect(s, notifyWnd, ts2::kWM_Socket, FD_READ | FD_CLOSE) == SOCKET_ERROR) {
        closesocket(s);
        return kNetErrAsyncSelect;             // *a4 = 6
    }

    // Success: switch over to the new game socket.
    nc.xorKey = key;                           // *(this+4) = key
    nc.seq    = sess;                          // *(this+5) = session (key + 128)
    closesocket(nc.sock);                      // close the old socket (login)
    nc.sock   = s;                             // *(this+12) = s
    std::memcpy(&nc.addr, &addr, sizeof(sockaddr)); // this+16 = v9
    nc.recvCursor = 0;                         // *(this+200032) = 0
    return kNetOk;                             // *a4 = 0
}

} // namespace ts2::net

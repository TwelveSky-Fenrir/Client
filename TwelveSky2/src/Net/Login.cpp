// Net/Login.cpp — implémentation fidèle de la séquence de connexion TwelveSky2.
//
// Vérité = désassemblage (idaTs2). Ancrages :
//   Net_ConnectLoginServer 0x462870, Net_LoginRequest 0x51B8E0,
//   Net_ConnectGameServer 0x462A70, Net_CloseSocket 0x463000, Rng_Next 0x7603FD.
#include "Net/Login.h"
#include "Core/Types.h"   // ts2::kWM_Socket (WM_USER+1 = 0x401)

#include <cstdint>
#include <cstring>
#include <cstdlib>

namespace ts2::net {

namespace {

// Rng_Next (0x7603FD) : c'est exactement rand() de la CRT MSVC —
// s = 214013*s + 2531011 ; renvoie (s >> 16) & 0x7FFF. Même algorithme, même
// consommation de flux que le binaire (le client fait « rand() % 10000 »).
inline int Rng() { return std::rand(); }

// Deux nonces d'en-tête = produit de deux tirages % 10000 (int, max 99 980 001).
// L'ordre des tirages est explicité : le binaire consomme le RNG dans l'ordre
// (nonce1.a, nonce1.b, nonce2.a, nonce2.b).
inline void MakeNonces(uint32_t& nonce1, uint32_t& nonce2) {
    int a = Rng() % 10000;
    int b = Rng() % 10000;
    nonce1 = static_cast<uint32_t>(a * b);
    int c = Rng() % 10000;
    int d = Rng() % 10000;
    nonce2 = static_cast<uint32_t>(c * d);
}

// Copie une chaîne dans un champ de 128 octets zéro-rempli. Reproduit à l'octet
// près le tampon du client, que Scene_LoginUpdate (0x51A9F1) zéro-initialise via
// Crt_Memset puis remplit par GetWindowTextA avant l'envoi de 128 octets bruts.
void CopyField128(uint8_t* dst, const char* src) {
    std::memset(dst, 0, 128);
    if (src) {
        size_t n = 0;
        while (n < 128 && src[n] != '\0') ++n;
        std::memcpy(dst, src, n);
    }
}

// Remplit un sockaddr (interprété en sockaddr_in) : famille AF_INET, adresse
// résolue par gethostbyname, port. Retourne false si l'hôte est introuvable.
// NB : le binaire écrit le port via ntohs(port) — byteswap identique à htons ;
// on reproduit l'appel exact.
bool FillSockAddr(sockaddr& out, const char* host, uint16_t port) {
    sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(&out);
    sin->sin_family = AF_INET;                       // *(WORD*)(this+16) = 2
    hostent* he = gethostbyname(host);
    if (!he) return false;
    std::memcpy(&sin->sin_addr, he->h_addr_list[0], 4); // this+20
    sin->sin_port = ntohs(port);                     // *(WORD*)(this+18) = ntohs(port)
    return true;
}

// Vrai si l'erreur connect() est « récupérable » (le binaire re-boucle dessus) :
// WSAECONNREFUSED (10061), WSAENETUNREACH (10051), WSAETIMEDOUT (10060).
inline bool IsRetryableConnectError(int err) {
    return err == WSAECONNREFUSED || err == WSAENETUNREACH || err == WSAETIMEDOUT;
}

// Réception bloquante d'exactement `need` octets dans nc.recvBuf (à partir de
// l'offset 0). Reproduit la boucle « for (i=0; i != need; i += n) recv(...) ».
// Retourne true si `need` octets ont été lus, false si recv() <= 0.
bool RecvExact(NetClient& nc, int need) {
    for (int i = 0; i != need; ) {
        int n = recv(nc.sock, nc.recvBuf + i, kRecvBufSize - i, 0);
        if (n <= 0) return false;
        i += n;
    }
    return true;
}

// Envoi bloquant intégral de `len` octets. Reproduit la boucle
// « while (len > 0) send(...) ». Retourne true si tout est parti, false si
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
    if (nc.loginReady)                       // *(this+8) déjà positionné
        return kNetErrState;                 // *a4 = 1

    // Le binaire écrit socket/adresse directement dans l'objet (this+12 / this+16).
    for (;;) {
        // socket(2,1,0) : le binaire pousse le protocole 0 (push 0; protocol
        // 0x46289d), pas IPPROTO_TCP. Résultat stocké dans l'objet en [ecx+0Ch]
        // (EA 0x4628ac). Protocole 0 = protocole par défaut de SOCK_STREAM = TCP.
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
        closesocket(nc.sock);                  // sinon : ferme et re-boucle
    }

    // Bannière : 17 octets exactement.
    if (!RecvExact(nc, 17)) {
        closesocket(nc.sock);
        return kNetErrRecv;                    // *a4 = 4
    }

    // Dérivation : clé = octet [1] de la bannière ; session = clé + 127.
    uint8_t keyByte = static_cast<uint8_t>(nc.recvBuf[1]);
    nc.xorKey     = keyByte;                   // *(this+4)
    nc.seq        = static_cast<uint8_t>(keyByte + 127); // *(this+5)
    nc.loginReady = 1;                         // *(this+8) = 1
    nc.recvCursor = 0;                         // *(this+200032) = 0
    return kNetOk;                             // *a4 = 0
}

// ---------------------------------------------------------------------------
// Net_LoginRequest 0x51B8E0  (opcode 0x0B : id / mot de passe)
// ---------------------------------------------------------------------------
int LoginRequest(NetClient& nc, const char* username, const char* password,
                 uint32_t extra, int& outResult) {
    // Paquet de 269 octets : [nonce1:4][nonce2_lo:3][seq:1@7][op:1@8]
    //                        [user:128@9][pass:128@137][extra:4@265].
    uint8_t pkt[269] = {};
    uint32_t nonce1, nonce2;
    MakeNonces(nonce1, nonce2);

    std::memcpy(pkt + 0, &nonce1, 4);          // [0..3]  nonce1
    std::memcpy(pkt + 4, &nonce2, 4);          // [4..7]  nonce2 (l'octet 7 est écrasé)
    pkt[7] = nc.seq;                           // [7]     byte_8156A5 (session)
    pkt[8] = 11;                               // [8]     opcode 0x0B
    CopyField128(pkt + 9,   username);         // [9..136]   identifiant
    CopyField128(pkt + 137, password);         // [137..264] mot de passe
    std::memcpy(pkt + 265, &extra, 4);         // [265..268] 3e arg sur 4 octets LE

    for (int i = 0; i < 269; ++i)              // XOR intégral avec byte_8156A4
        pkt[i] ^= nc.xorKey;

    if (!SendAll(nc.sock, pkt, 269)) {
        NetCloseSocket(nc);                    // Net_CloseSocket(&dword_8156A0)
        outResult = kLoginErrSend;             // *a4 = 101
        return kLoginErrSend;
    }
    ++nc.seq;                                  // ++byte_8156A5

    // Réponse : blob de 30659 octets.
    if (!RecvExact(nc, 30659)) {
        NetCloseSocket(nc);
        outResult = kLoginErrRecv;             // *a4 = 102
        return kLoginErrRecv;
    }

    // Code résultat = 4 octets à recvBuf[1] (non chiffrés).
    int32_t result;
    std::memcpy(&result, nc.recvBuf + 1, 4);
    outResult = result;                        // *a4 = v24

    if (result == 0) {
        // Le serveur renvoie le jeton de compte (renvoyé ensuite au serveur de jeu)
        // et le niveau GM.
        std::memcpy(g_AccountName, nc.recvBuf + 5, 128);   // byte_1669194 <- recvBuf+5
        std::memcpy(&g_GmAuthLevel, nc.recvBuf + 133, 4);  // dword_1669294 <- recvBuf+133

        // 3 fiches personnage BRUTES de 10088 o chacune (unk_1669380/unk_166BAE8/
        // unk_166E250 dans le binaire) — offsets RE-CONFIRMÉS par décompilation
        // fraîche de Net_LoginRequest 0x51B8E0 (EA 0x51bc56/0x51bc6d/0x51bc84,
        // Docs/TS2_LOGINSCENE_AUDIT.md §3.9) : 367 / 10456 / 20545 relatifs au début
        // de recvBuf. COPIÉES (pas juste lues) dans g_CharRecords, qui SURVIT aux
        // appels réseau suivants — contrairement à nc.recvBuf, réutilisé/écrasé par
        // ConnectGameServer puis par les requêtes CharSelectPackets. C'est l'écart de
        // complétude documenté par l'audit (les ~20 Ko restants étaient perdus) :
        // désormais persistés, exploitables par CharSelectPackets::LoadCharacterSlotsFromRecords.
        //
        // Champs annexes du blob (unk_1669298.. dword_16692B8, offsets 137..365 —
        // système d'overlay compte-GM/mot-de-passe secondaire, cf. Game/CharSelectFlow.h
        // en tête de fichier « ÉCART CONNU… DÉLIBÉRÉMENT NON REPRODUIT ») : hors
        // périmètre de cette mission (flux CharSelect standard 3 emplacements
        // uniquement), délibérément non persistés ici non plus.
        std::memcpy(g_CharRecords[0], nc.recvBuf + 367,   kCharRecordSize);
        std::memcpy(g_CharRecords[1], nc.recvBuf + 10456, kCharRecordSize);
        std::memcpy(g_CharRecords[2], nc.recvBuf + 20545, kCharRecordSize);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Net_ConnectGameServer 0x462A70  (handshake d'authentification serveur de jeu)
// ---------------------------------------------------------------------------
int ConnectGameServer(NetClient& nc, const char* host, uint16_t port, HWND notifyWnd) {
    if (!nc.loginReady)                        // *(this+8) requis
        return kNetErrState;                   // *a4 = 1

    // Le binaire travaille sur des variables LOCALES (socket s, sockaddr v9) et ne
    // les valide dans l'objet qu'en cas de succès complet.
    SOCKET   s = INVALID_SOCKET;
    sockaddr addr{};

    for (;;) {
        // socket(2,1,0) : protocole littéral 0 dans le binaire (push 0; protocol
        // 0x462ab1), pas IPPROTO_TCP. Résultat stocké dans [ebp+s] (EA 0x462abd).
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

    // Bannière : 5 octets → clé XOR + session LOCALES.
    {
        // RecvExact lit dans nc.recvBuf (le binaire lit aussi dans this+32).
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

    // Paquet d'auth de 141 octets : [nonce1:4][nonce2_lo:3][sess:1@7][op:1@8]
    //                               [jeton_compte:128@9][element_local:4@137].
    uint8_t pkt[141] = {};
    uint32_t nonce1, nonce2;
    MakeNonces(nonce1, nonce2);

    std::memcpy(pkt + 0, &nonce1, 4);          // [0..3]  nonce1
    std::memcpy(pkt + 4, &nonce2, 4);          // [4..7]  nonce2 (octet 7 écrasé)
    pkt[7] = sess;                             // [7]     session
    pkt[8] = 11;                               // [8]     opcode 0x0B
    std::memcpy(pkt + 9,   g_AccountName, 128);// [9..136]   byte_1669194
    std::memcpy(pkt + 137, &g_LocalElement, 4);// [137..140] dword_1673194

    for (int j = 0; j < 141; ++j)              // XOR intégral avec la clé locale
        pkt[j] ^= key;

    if (!SendAll(s, pkt, 141)) {
        closesocket(s);
        return kNetErrSocketSend;              // *a4 = 2
    }
    ++sess;                                     // ++session après envoi

    // Réponse : 5 octets → code résultat en [1..4] (non chiffré).
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
        return (result == 1) ? kNetErrAuthRej1 : kNetErrAuthRejN; // *a4 = 5 ou 7
    }

    // Notification socket asynchrone : WM_USER+1, événements FD_READ | FD_CLOSE (33).
    if (WSAAsyncSelect(s, notifyWnd, ts2::kWM_Socket, FD_READ | FD_CLOSE) == SOCKET_ERROR) {
        closesocket(s);
        return kNetErrAsyncSelect;             // *a4 = 6
    }

    // Succès : bascule vers la nouvelle socket de jeu.
    nc.xorKey = key;                           // *(this+4) = clé
    nc.seq    = sess;                          // *(this+5) = session (clé + 128)
    closesocket(nc.sock);                      // ferme l'ancienne socket (login)
    nc.sock   = s;                             // *(this+12) = s
    std::memcpy(&nc.addr, &addr, sizeof(sockaddr)); // this+16 = v9
    nc.recvCursor = 0;                         // *(this+200032) = 0
    return kNetOk;                             // *a4 = 0
}

} // namespace ts2::net

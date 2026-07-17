// Net/Login.cpp — implémentation fidèle de la séquence de connexion TwelveSky2.
//
// Vérité = désassemblage (idaTs2). Ancrages :
//   Net_ConnectLoginServer 0x462870, Net_LoginRequest 0x51B8E0,
//   Net_ConnectGameServer 0x462A70, Net_CloseSocket 0x463000, Rng_Next 0x7603FD.
#include "Net/Login.h"
#include "Core/Types.h"   // ts2::kWM_Socket (WM_USER+1 = 0x401)
#include "Game/GameState.h" // game::g_World.self.element = dword_1673194 (source unique de l'élément local)
#include "Net/Rng.h"      // DefaultRng() — flux _holdrand UNIQUE (Rng_Next 0x7603FD)

#include <cstdint>
#include <cstring>
#include <functional>

namespace ts2::net {

// Définition de la référence-alias déclarée `extern` dans NetClient.h. Lie
// net::g_LocalElement à game::g_World.self.element (int, même adresse que
// dword_1673194 dans le binaire). reinterpret_cast<uint32_t&> d'un int : aliasing
// signé/non-signé autorisé (mêmes représentation et taille), pas d'UB. L'init ne
// fait qu'un calcul d'adresse sur g_World (objet à durée statique) -> pas de
// fiasco d'ordre d'init statique, l'adresse est valide même avant son constructeur
// dynamique. Élimine le dédoublement qui laissait l'élément à 0 au handshake
// (cf. Net_ConnectGameServer 0x462A70, &g_LocalElement EA 0x462d5d).
uint32_t& g_LocalElement = reinterpret_cast<uint32_t&>(game::g_World.self.element);

// Définition du hook de notice déclaré `extern` dans Login.h (UI_NoticeDlg_Open
// 0x5C0280, appelé par Net_LoginRequest 0x51B8E0 EA 0x51bd75). Nul par défaut :
// à poser par la couche UI (UI/LoginScene.cpp) — fichier NON possédé par ce front.
std::function<void(int32_t strId)> g_LoginNoticeHook;

namespace {

// Rng_Next 0x7603FD = rand() de la CRT MSVC, à l'instruction près :
//   Ptd  = Crt_GetPtd();                  // 0x76D464 (_getptd_noexit)  EA 0x7603fd
//   v1   = 214013 * Ptd[5] + 2531011;     // Ptd[5] = _holdrand (+20)   EA 0x76040b
//   Ptd[5] = v1;                          //                            EA 0x760411
//   return HIWORD(v1) & 0x7FFF;           // RAND_MAX = 32767           EA 0x76041e
// FLUX UNIQUE PARTAGÉ : le binaire n'a qu'un seul _holdrand (par thread), semé par
// srand(time(NULL)) en App_Init 0x461C20 (EA 0x461C35 time / 0x461C3E srand). Les
// nonces réseau, les fonds ServerSelect/CharSelect, la rotation de spawn
// (Rng_Next() % 360) et le job initial (% 3) puisent TOUS dans la même séquence.
// On tape donc sur net::DefaultRng() (Net/Rng.h), qui rématérialise ce _holdrand
// unique — et NON sur std::rand(), qui constituait ici un SECOND flux indépendant
// (écart d'ordre/valeurs vs le binaire).
inline int RngNext() { return DefaultRng().Next(); }

// Deux nonces d'en-tête = produit de deux tirages % 10000 (int, max 99 980 001 —
// pas de débordement). ORDRE DE CONSOMMATION du binaire (Net_LoginRequest 0x51B8E0) :
//   v4  = Rng_Next() % 10000;           // tirage 1   EA 0x51b91f
//   v8  = Rng_Next() % 10000 * v4;      // tirage 2   EA 0x51b931  -> nonce1
//   v5  = Rng_Next() % 10000;           // tirage 3   EA 0x51b944
//   v12 = Rng_Next() % 10000 * v5;      // tirage 4   EA 0x51b956  -> nonce2
// Même ordre ici (produit commutatif -> valeurs identiques).
inline void MakeNonces(uint32_t& nonce1, uint32_t& nonce2) {
    int a = RngNext() % 10000;
    int b = RngNext() % 10000;
    nonce1 = static_cast<uint32_t>(a * b);
    int c = RngNext() % 10000;
    int d = RngNext() % 10000;
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
    // Publie l'objet actif comme singleton global (&g_NetClient 0x8156A0) : dans le
    // binaire l'objet réseau est unique et adressé directement par les builders qui ne
    // le reçoivent pas (cf. Net/NetClient.h g_NetClientPtr). `nc` == App::net_.nc_.
    g_NetClientPtr = &nc;                     // &g_NetClient 0x8156A0
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

        // Mot de passe secondaire / PIN — RÉIFIÉ cette passe (offsets ABSOLUS recvBuf) :
        //   dword_16692A4 <- recvBuf+0x95 (=149), 4 o   (Crt_Memcpy EA 0x51BBE7)
        //   unk_16692A8   <- recvBuf+0x99 (=153), 5 o   (Crt_Memcpy EA 0x51BBFB)
        // Consommés par l'assistant PIN de CharSelect (hooks IsSecondaryPasswordRequired /
        // HasStoredSecondaryPassword). ⚠ Sans eux, un compte À PIN prend la MAUVAISE branche
        // à l'entrée en CharSelect (la branche PIN permute un pavé via ≥10 tirages Rng_Next),
        // ce qui DÉSYNCHRONISE le flux _holdrand PARTAGÉ (fond, rotation de spawn, job initial).
        // Voir Net/NetClient.h::g_SecondaryPwRequired/g_StoredSecondaryPw et [A12] CharSelectFlow.h.
        std::memcpy(&g_SecondaryPwRequired, nc.recvBuf + 0x95, 4); // dword_16692A4  EA 0x51BBE7
        std::memcpy(g_StoredSecondaryPw,    nc.recvBuf + 0x99, 5); // unk_16692A8    EA 0x51BBFB

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
        // Champs annexes du blob (unk_1669298.. dword_16692B8, offsets 137..365 — système
        // d'overlay compte-GM/mot-de-passe secondaire). Les DEUX champs pilotant l'assistant
        // PIN (dword_16692A4 @149 + unk_16692A8 @153) sont désormais réifiés ci-dessus ; le
        // RESTE de cette bande (autres offsets 137..365) demeure hors périmètre (non persisté).
        std::memcpy(g_CharRecords[0], nc.recvBuf + 367,   kCharRecordSize);
        std::memcpy(g_CharRecords[1], nc.recvBuf + 10456, kCharRecordSize);
        std::memcpy(g_CharRecords[2], nc.recvBuf + 20545, kCharRecordSize);

        // 3 deltas de solde ajoutés sur record[i]+16 (Net_LoginRequest 0x51B8E0) :
        //   dword_1669390[0] += v15  (EA 0x51bd15) — 0x1669390-0x1669380(record[0]) = 16
        //   dword_166BAF8    += v16  (EA 0x51bd26) — 0x166BAF8-0x166BAE8(record[1]) = 16
        //   dword_166E260    += v17  (EA 0x51bd38) — 0x166E260-0x166E250(record[2]) = 16
        // Sources v15/v16/v17 = recvBuf+30634/30638/30642 (EA 0x51bc9a/0x51bcb0/0x51bcc6,
        // MEMORY[0x81CE6A/6E/72] - recvBuf base 0x8156C0 = 0x77AA/0x77AE/0x77B2 = 30634/38/42).
        // Le binaire lit/écrit les globales record[i] directement ; ici les fiches sont
        // copiées dans g_CharRecords -> on applique le delta au même champ +16 (int32).
        // int32 SIGNÉ, addition simple : ni saturation ni borne (`add` nu, EA 0x51bd0f/
        // 0x51bd20/0x51bd32). Sémantique de record[i]+16 : compteur affiché en décimal
        // sur l'écran CharSelect (Scene_CharSelectRender EA 0x51da5d : `imul eax, 2768h`
        // puis `mov ecx, ds:dword_1669390[eax]`, formaté "%d" -> UI_DrawNumberValue).
        // Le blob se referme ici : dernier champ à +0x77BF..+0x77C2, total 0x77C3 = 30659
        // = la borne EXACTE de la boucle de réception (EA 0x51bad0). Les 5 octets de
        // bourrage NON LUS (+0x16E/+0x28D7/+0x5040/+0x77A9/+0x77B6) expliquent les
        // offsets « désalignés » — ce ne sont pas des erreurs de relevé.
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

        // Notice si AU MOINS UN delta > 0 — garde reproduite à l'instruction près
        // (Net_LoginRequest 0x51B8E0) :
        //   cmp [ebp+var_404], 0 ; jg loc_51BD59   EA 0x51bd3e/0x51bd45   (d0)
        //   cmp [ebp+var_400], 0 ; jg loc_51BD59   EA 0x51bd47/0x51bd4e   (d1)
        //   cmp [ebp+var_3FC], 0 ; jle loc_51BD7A  EA 0x51bd50/0x51bd57   (d2)
        // Comparaisons SIGNÉES (jg/jle) : un delta négatif n'ouvre PAS la notice.
        // ORDRE FIDÈLE : le binaire additionne les 3 deltas (EA 0x51bd0f-0x51bd38) PUIS
        // teste — d'où le test APRÈS les addAt16 ci-dessus.
        // Le binaire appelle ici UI_NoticeDlg_Open(byte_18225C8, 1, StrTable005_Get(
        // g_LangId, 1785), &String/*""*/) (EA 0x51bd68/0x51bd75) ; le port passe par
        // g_LoginNoticeHook (cf. Login.h), nul tant que UI/LoginScene.cpp ne l'a pas posé.
        if ((d0 > 0 || d1 > 0 || d2 > 0) && g_LoginNoticeHook)
            g_LoginNoticeHook(kLoginDeltaNoticeStrId); // push 6F9h = 1785, EA 0x51bd5e

        // NON PORTÉ — les 3 int32 de queue <- recvBuf+30647/30651/30655
        // (EA 0x51bcda/0x51bcee/0x51bd02), vérifié par xrefs_to sur chaque globale :
        //  - unk_1676170 (0x1676170) et unk_1676174 (0x1676174) : STOCKAGES MORTS — leur
        //    SEULE xref est l'écriture ci-dessus, jamais relus nulle part dans le binaire.
        //    Omission définitivement correcte. (L'ancienne justification « aucun += dans
        //    le binaire, simple persistance » était FAUSSE en droit : ce n'est pas
        //    l'absence de += qui les disqualifie, c'est qu'ils sont write-only.)
        //  - dword_167616C (0x167616C) : VIVANT — 5 xrefs, dont 4 HORS de cette fonction,
        //    et il est bel et bien +=-é ailleurs. Compteur d'opérations d'entrepôt en
        //    attente :
        //      · Pkt_DispatchStorageResponse 0x58A0F0 : `cmp [ebp+var_4C], 90Ah` (2314)
        //        EA 0x58bc63 -> `add edx, 1` EA 0x58bc72 / store EA 0x58bc75 ;
        //      · UI_NpcShop_OnRDown_Buy 0x5E5000 : `cmp ds:dword_167616C, 1 ; jl` EA
        //        0x5e55ba/0x5e55c1 -> si >= 1, ligne système StrTable005_Get(g_LangId,
        //        0x9F3 /*2547*/) puis `jmp loc_5E5671` EA 0x5e55e3 qui SAUTE toute la
        //        branche d'achat (loc_5E55E8) -> achat REFUSÉ ;
        //      · UI_MainInventory_OnLButtonUp 0x5B20B0 (EA 0x5bb3d8).
        //    La graine envoyée AU LOGIN a donc un effet gameplay réel (peut bloquer
        //    l'achat en boutique dès l'entrée en jeu).
        //    TODO [ancre 0x51bcda] : son foyer naturel est Net/NetClient.h (à côté de
        //    g_GmAuthLevel) et ses 3 consommateurs ne sont pas modélisés — fichiers NON
        //    possédés par ce front. Résidu remonté à l'orchestrateur.
    }
    return result;
}

// ---------------------------------------------------------------------------
// Net_ConnectGameServer 0x462A70  (handshake d'authentification serveur de jeu)
// ---------------------------------------------------------------------------
int ConnectGameServer(NetClient& nc, const char* host, uint16_t port, HWND notifyWnd) {
    // Idem ConnectLoginServer : publie l'objet actif comme singleton global
    // (&g_NetClient 0x8156A0). Garantit que GlobalNetClient() est valide avant tout
    // warp (qui ne survient qu'en jeu, post-handshake).
    g_NetClientPtr = &nc;                      // &g_NetClient 0x8156A0
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

    // Paquet d'auth de 141 octets (Net_ConnectGameServer 0x462A70) :
    //   [nonce1:4][nonce2_lo:3][sess:1@7][op=0x0B:1@8][jeton_compte:128@9]
    //   [element_local:4@137].
    //   +9   = byte_1669194 / g_AccountName (Crt_Memcpy EA 0x462d47) ;
    //   +137 = dword_1673194 / g_LocalElement (Crt_Memcpy &g_LocalElement EA 0x462d5d,
    //          v27 = buf+137, buf = ebp-3F0h -> 0x3F0-0x367 = 0x89 = 137).
    uint8_t pkt[141] = {};
    uint32_t nonce1, nonce2;
    MakeNonces(nonce1, nonce2);

    std::memcpy(pkt + 0, &nonce1, 4);          // [0..3]  nonce1
    std::memcpy(pkt + 4, &nonce2, 4);          // [4..7]  nonce2 (octet 7 écrasé)
    pkt[7] = sess;                             // [7]     session
    pkt[8] = 11;                               // [8]     opcode 0x0B
    std::memcpy(pkt + 9,   g_AccountName, 128);// [9..136]   byte_1669194 (EA 0x462d47)
    // [137..140] élément local (Net_ConnectGameServer 0x462A70, Crt_Memcpy
    // &g_LocalElement EA 0x462d5d ; v27 = buf+137). g_LocalElement = dword_1673194 =
    // SOURCE UNIQUE aliasée sur game::g_World.self.element (lue ICI telle quelle, sans
    // transformation). Dans le binaire, cette valeur est posée juste avant le handshake
    // par Scene_CharSelectUpdate 0x51BD90 (Crt_Memcpy g_SelfCharInvBlock 0x1673170 <-
    // fiche du slot sélectionné, EA 0x51c707) : g_LocalElement = g_SelfCharInvBlock+0x24
    // = charRecord[+36] du perso (champ nommé "job"@36 dans CharSelectFlow.h). La
    // POPULATION depuis la fiche du slot incombe au flux CharSelect (LoginScene, hors
    // périmètre de ce front) : sans elle, l'alias reste à 0 même si le dédoublement est
    // éliminé — cf. NOTE ci-dessous.
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

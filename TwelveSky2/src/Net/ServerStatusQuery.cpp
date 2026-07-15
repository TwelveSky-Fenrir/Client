// Net/ServerStatusQuery.cpp — implémentation fidèle de Net_QueryServerStatus 0x519CC0.
//
// Vérité = désassemblage (idaTs2). Décompilation confirmée de 0x519CC0 :
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
//           memcpy(&ret,        buf+13, 4);   // currentPopulation = retour
//           return ret; } }
//   closesocket(s); return -1;
//
// Le client N'ENVOIE RIEN : il se connecte et le serveur pousse 17 octets.
#include "Net/ServerStatusQuery.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

namespace ts2::net {

namespace {

// WSAStartup idempotent. Le cœur réseau (NetClient::NetStartup / Net_Init 0x462790)
// appelle déjà WSAStartup(2.2) globalement quand le client tourne. Mais cette requête
// de statut peut être émise indépendamment (écran de sélection de serveur) ; on garantit
// donc l'initialisation de Winsock au moins une fois, SANS jamais appeler WSACleanup
// (pour ne pas décrémenter le compteur global partagé avec NetClient).
void EnsureWinsock() {
    static bool started = false;
    if (!started) {
        WSADATA wsaData;
        std::memset(&wsaData, 0, sizeof(wsaData));
        if (WSAStartup(0x0202u, &wsaData) == 0) // MAKEWORD(2,2)
            started = true;
    }
}

// Attend que la socket `s` soit prête en lecture OU écriture (select), au plus
// `timeoutMs`. `forWrite` = true pour attendre la fin d'un connect non-bloquant,
// false pour attendre des données en réception. Retourne true si prête, false si
// timeout ou erreur.
bool WaitReady(SOCKET s, bool forWrite, uint32_t timeoutMs) {
    fd_set set;
    FD_ZERO(&set);
    FD_SET(s, &set);

    timeval tv;
    tv.tv_sec  = static_cast<long>(timeoutMs / 1000u);
    tv.tv_usec = static_cast<long>((timeoutMs % 1000u) * 1000u);

    // Le 1er argument est ignoré sous Winsock ; on passe 0.
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

    // sa.sa_family = 2 ; adresse résolue par gethostbyname ; port = ntohs(port).
    // NB : le binaire écrit *(u16*)sa_data = ntohs(port) — byteswap identique à htons ;
    // on reproduit l'appel exact (ntohs).
    // ÉCART DE FIDÉLITÉ (documenté) : gethostbyname est BLOQUANT et N'EST PAS borné par
    // `timeoutMs` (seuls connect/recv le sont, via select). C'est identique au binaire
    // (Net_QueryServerStatus 0x519CC0 fait aussi un gethostbyname bloquant). En pratique la
    // résolution est quasi instantanée (cache DNS après le 1er accès) et bornée par le
    // timeout DNS de l'OS (quelques secondes) — le join de LoginScene::Shutdown() ne peut
    // donc pas rester bloqué indéfiniment. Une résolution async (GetAddrInfoEx) serait
    // nécessaire pour un timeout strict sur le DNS, hors périmètre (non fidèle au binaire).
    sockaddr sa{};
    sa.sa_family = AF_INET;
    hostent* he = gethostbyname(host.c_str());
    if (!he || !he->h_addr_list || !he->h_addr_list[0]) {
        closesocket(s);
        return st; // return -1 (gethostbyname échoué)
    }
    std::memcpy(&sa.sa_data[2], he->h_addr_list[0], 4);
    *reinterpret_cast<uint16_t*>(sa.sa_data) = ntohs(port);

    // ÉCART DE FIDÉLITÉ (documenté, cf. en-tête) : connect NON-BLOQUANT + select pour
    // borner l'attente à `timeoutMs`. Le binaire fait un connect bloquant simple.
    u_long nb = 1;
    ioctlsocket(s, FIONBIO, &nb);

    if (connect(s, &sa, 16) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        // connect non-bloquant : WSAEWOULDBLOCK = en cours → attendre l'écriture.
        if (err != WSAEWOULDBLOCK || !WaitReady(s, /*forWrite=*/true, timeoutMs)) {
            closesocket(s);
            return st; // return -1 (connect échoué / timeout)
        }
        // Vérifier le résultat effectif du connect (SO_ERROR).
        int soErr = 0;
        int soLen = sizeof(soErr);
        if (getsockopt(s, SOL_SOCKET, SO_ERROR,
                       reinterpret_cast<char*>(&soErr), &soLen) != 0 || soErr != 0) {
            closesocket(s);
            return st; // return -1
        }
    }

    // Réception : boucle « recv jusqu'à avoir les 17 octets du record de statut ».
    // buf[0..4] = en-tête ignoré ; [5..8]=maxPop, [9..12]=loadStep, [13..16]=curPop.
    // ROBUSTESSE (équivalente-fidèle) : on plafonne chaque recv au RESTE nécessaire
    // (17 - total) au lieu de 1000 - total. Contre le serveur réel (qui envoie EXACTEMENT
    // 17 octets puis ferme, cf. RE/probe_server_status.py live 2026-07-15), le comportement
    // est IDENTIQUE au binaire (Net_QueryServerStatus 0x519CC0, test `v7 == 17`) ; mais si
    // un serveur coalesçait un payload plus large dans le même segment TCP, cette borne
    // garantit qu'on s'arrête pile à 17 (le binaire, lui, dépasserait 17 et échouerait en
    // ne retombant jamais sur `== 17`). Aucun octet au-delà de 17 n'est requis par le
    // protocole (le client ferme la socket juste après).
    unsigned char buf[17];
    int total = 0;
    while (total < 17) {
        // select avant chaque recv pour ne jamais bloquer indéfiniment.
        if (!WaitReady(s, /*forWrite=*/false, timeoutMs)) {
            closesocket(s);
            return st; // return -1 (timeout : jamais 17 octets)
        }
        int n = recv(s, reinterpret_cast<char*>(buf) + total, 17 - total, 0);
        if (n <= 0) {
            closesocket(s);
            return st; // return -1 (recv<=0 : déconnexion / erreur avant 17 octets)
        }
        total += n;
    }
    closesocket(s);
    std::memcpy(&st.maxPopulation,     buf + 5,  4); // [5..8]
    std::memcpy(&st.loadStep,          buf + 9,  4); // [9..12]
    std::memcpy(&st.currentPopulation, buf + 13, 4); // [13..16] = valeur de retour
    st.ok = true;
    return st;
}

} // namespace ts2::net

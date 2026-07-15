// Net/NetClient.h — objet client réseau du client TwelveSky2 (réécriture fidèle).
//
// Réplique de l'objet global `dword_8156A0` relevé dans le désassemblage
// (voir mémoire projet ts2-network-protocol). Seule la SÉMANTIQUE des champs est
// reproduite ; les offsets d'origine (build 32-bit) sont notés pour référence.
//
// Convention Winsock du projet : <winsock2.h> AVANT <windows.h>.
//
// Découpage du sous-système réseau (les ancrages du désassemblage sont répartis
// pour respecter la séparation des composants) :
//   Net_Init               0x462790  -> NetStartup            (ce fichier / .cpp)
//   Net_CloseSocket        0x463000  -> NetCloseSocket        (ce fichier)
//   boucle send builders             -> NetSend               (ce fichier / .cpp)
//   Net_ConnectLoginServer 0x462870  -> ConnectLoginServer    (Net/Login.*)
//   Net_ConnectGameServer  0x462A70  -> ConnectGameServer     (Net/Login.*)
//   Net_RecvDispatch       0x463040  -> PacketDispatcher      (Net/PacketDispatch.*)
//   chiffrement XOR (byte_8156A4/A5) -> Cipher                (Net/Cipher.*)
#pragma once
#include <winsock2.h>
#include <windows.h>
#include <cstdint>

#include "Net/Cipher.h"

#pragma comment(lib, "ws2_32.lib")

namespace ts2::net {

// Taille du buffer de réception (offset +32 de l'objet, 200000 o dans le binaire).
inline constexpr int kRecvBufSize = 200000;

// Objet client réseau — `dword_8156A0`.
//   +4   byte_8156A4  clé XOR mono-octet
//   +5   byte_8156A5  octet de séquence/session
//   +8                flag « handshake serveur login effectué »
//   +12  s            socket active
//   +16               sockaddr du dernier connect (16 o)
//   +32               buffer de réception (200000 o)
//   +200032           curseur d'écriture / longueur accumulée
struct NetClient {
    /* +0      */ uint32_t wsaReady    = 0;               // Net_Init met 1 (WSAStartup OK)
    /* +4      */ uint8_t  xorKey      = 0;               // byte_8156A4 — clé XOR
    /* +5      */ uint8_t  seq         = 0;               // byte_8156A5 — séquence/session
    /* +8      */ uint32_t loginReady  = 0;               // 1 = serveur login négocié
    /* +12     */ SOCKET   sock        = INVALID_SOCKET;  // s
    /* +16     */ sockaddr addr{};                        // sockaddr_in (16 o)
    /* +32     */ char     recvBuf[kRecvBufSize] = {};    // buffer de réception
    /* +200032 */ uint32_t recvCursor  = 0;               // index d'écriture / longueur

    // Vue Cipher (clé + séquence) — primitive XOR partagée avec les builders
    // sortants (Framing::PacketWriter). Instantané des octets +4/+5.
    Cipher MakeCipher() const { return Cipher(xorKey, seq); }
    void   StoreCipher(const Cipher& c) { xorKey = c.Key(); seq = c.Seq(); }
};

// Net_CloseSocket (0x463000) : si la session login est active, la ferme et la
// réinitialise. Reproduit à l'octet près le comportement du binaire :
//   if (loginReady) { loginReady = 0; closesocket(sock); sock = -1; }
inline void NetCloseSocket(NetClient& nc) {
    if (nc.loginReady) {
        nc.loginReady = 0;
        closesocket(nc.sock);
        nc.sock = INVALID_SOCKET;
    }
}

// Alias sémantique (« Disconnect » demandé par l'API réseau).
inline void NetDisconnect(NetClient& nc) { NetCloseSocket(nc); }

// Net_Init (0x462790) : WSAStartup(2.2) puis remise à l'état « prêt, non
// connecté » (wsaReady=1, loginReady=0, sock=INVALID_SOCKET). Le binaire
// memset(&WSAData,0,400) avant l'appel. Renvoie false si WSAStartup échoue
// (le binaire renvoie 0 sans toucher l'objet).
bool NetStartup(NetClient& nc);

// Pendant de Net_Init : WSACleanup + fermeture socket (App_Shutdown 0x462480).
void NetCleanup(NetClient& nc);

// Boucle d'envoi partiel commune aux 234 builders Net_Send* (C->S). Le buffer
// 'data' doit DÉJÀ être encadré (en-tête 9 o) et chiffré XOR par le builder ;
// NetSend ne fait que pousser les octets. Tolère WSAEWOULDBLOCK (10035) en
// réessayant le même fragment ; toute autre erreur ferme la socket
// (Net_CloseSocket) et renvoie false. Utilise nc.sock.
//
// NB : distinct du SendAll bloquant du handshake (Net/Login.cpp) — ici la
// socket est déjà asynchrone (WSAAsyncSelect posé par ConnectGameServer).
bool NetSend(NetClient& nc, const void* data, int len);

// --- État de compte partagé (renseigné par LoginRequest, relu par ConnectGameServer) ---
// byte_1669194 : ID saisi dans l'écran de login, puis ÉCRASÉ par le jeton de compte
//                (128 o) renvoyé par le serveur login ; ce jeton est ensuite renvoyé
//                tel quel au serveur de jeu dans le paquet d'authentification.
inline uint8_t  g_AccountName[128] = {};
// dword_1673194 : 4 octets « élément local » annexés au paquet d'auth game.
inline uint32_t g_LocalElement     = 0;
// dword_1669294 : niveau GM renvoyé par le serveur login.
inline uint32_t g_GmAuthLevel      = 0;

// --- Fiches personnage brutes (renvoyées par LoginRequest, persistées ici) ---
// Le blob de réponse de Net_LoginRequest (30659 o) contient, après le compte/niveau
// GM, 3 fiches personnage BRUTES de 10088 o (0x2768) chacune — `unk_1669380`,
// `unk_166BAE8`, `unk_166E250` dans le binaire (Crt_Memcpy 0x2768 o à chaque fois,
// EA 0x51bc56/0x51bc6d/0x51bc84 de Net_LoginRequest 0x51B8E0, RE-CONFIRMÉ par
// décompilation fraîche session 2026-07-14 ; recoupe Docs/TS2_LOGINSCENE_AUDIT.md
// §3.9). Le binaire réutilise ces globales directement ; ici on les persiste dans un
// tableau dédié (le buffer de réception nc.recvBuf, lui, EST écrasé par les appels
// réseau suivants — cf. Net/Login.cpp::LoginRequest). Layout interne détaillé
// (nom@20, job@36, faction@44, face@48, hairColor@52, power@56, lookPresetId@216,
// zoneId@5468, position locale x/y/z@5472/5476/5480) : cf. Net/CharSelectPackets.h.
inline constexpr int kCharRecordSize  = 10088; // 0x2768, une fiche
inline constexpr int kCharRecordCount = 3;     // 3 emplacements, cf. game::kMaxCharSlots
inline uint8_t g_CharRecords[kCharRecordCount][kCharRecordSize] = {};

} // namespace ts2::net

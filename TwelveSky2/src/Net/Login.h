// Net/Login.h — séquence de connexion et handshake du client TwelveSky2.
//
// Réécriture fidèle de :
//   - Net_ConnectLoginServer  0x462870  (connexion serveur login/version)
//   - Net_LoginRequest        0x51B8E0  (envoi id/pw, opcode 0x0B)
//   - Net_ConnectGameServer   0x462A70  (handshake d'authentification serveur de jeu)
//
// Chiffrement = XOR mono-octet, clé négociée au handshake.
// Cadre sortant : [nonce1:u32][nonce2_lo:3o][seq:u8@7][opcode:u8@8][payload] XOR clé.
#pragma once
#include "Net/NetClient.h"
#include <cstdint>

namespace ts2::net {

// --- Codes de retour (valeurs EXACTES écrites par le binaire) ---
// Communs aux connexions (ConnectLoginServer / ConnectGameServer) :
inline constexpr int kNetOk             = 0;
inline constexpr int kNetErrState       = 1;   // login : déjà connecté / game : login pas prêt
inline constexpr int kNetErrSocketSend  = 2;   // socket() échoué, ou send()/recv() game échoué
inline constexpr int kNetErrConnect     = 3;   // connect() échoué (erreur non-récupérable)
inline constexpr int kNetErrRecv        = 4;   // recv() de la bannière échoué
inline constexpr int kNetErrAuthRej1    = 5;   // game : réponse d'auth == 1
inline constexpr int kNetErrAsyncSelect = 6;   // game : WSAAsyncSelect échoué
inline constexpr int kNetErrAuthRejN    = 7;   // game : réponse d'auth != 0 et != 1
inline constexpr int kNetErrHost        = 12;  // gethostbyname() échoué
// Spécifiques à LoginRequest (sinon le code renvoyé est celui du serveur, 0..18) :
inline constexpr int kLoginErrSend      = 101; // send() échoué
inline constexpr int kLoginErrRecv      = 102; // recv() échoué

// Octet « extra » passé par l'écran de login (Scene_LoginUpdate 0x51AB20 : 106 = 0x6A).
inline constexpr uint32_t kLoginExtra = 106u;

// Hôtes du serveur login/version (Scene_ServerSelectUpdate 0x518D77 / 0x518E2F).
inline constexpr char kLoginHostCom[] = "12sky2-login.geniusorc.com"; // Hôte login .com fidèle (Scene_ServerSelectUpdate 0x518D77) ; reliquat de test "127.0.0.1" reverté
inline constexpr char kLoginHostOrg[] = "12sky2-login.geniusorc.org";

// Connexion au serveur login/version : ouvre la socket, connecte (retry sur
// CONNREFUSED/NETUNREACH/TIMEDOUT), lit 17 octets de bannière puis dérive la clé
// XOR (octet [1] de la bannière) et l'octet de session (clé + 127).
// (Net_ConnectLoginServer 0x462870). Retourne un code kNet* (0 = succès).
int ConnectLoginServer(NetClient& nc, const char* host, uint16_t port);

// Requête de login (id / mot de passe) sur la socket du serveur login, opcode 0x0B.
// `username` / `password` : chaînes (tampons logiques de 128 octets côté client).
// `extra` : 3e argument émis sur 4 octets LE (kLoginExtra dans le client).
// `outResult` reçoit le code serveur (0 = succès, 1..18 = refus) ou 101/102 sur
// erreur socket. (Net_LoginRequest 0x51B8E0). Retourne la même valeur que outResult.
int LoginRequest(NetClient& nc, const char* username, const char* password,
                 uint32_t extra, int& outResult);

// Connexion au serveur de jeu : ouvre la socket, connecte, lit 5 octets de bannière,
// dérive une clé XOR locale, construit et envoie le paquet d'authentification de
// 141 octets (2 nonces + session + opcode 0x0B + jeton de compte 128 o + élément
// local 4 o), lit la réponse de 5 octets, puis arme WSAAsyncSelect(WM_USER+1).
// `notifyWnd` : fenêtre destinataire des notifications socket asynchrones.
// (Net_ConnectGameServer 0x462A70). Requiert nc.loginReady != 0.
// Retourne un code kNet* (0 = succès).
int ConnectGameServer(NetClient& nc, const char* host, uint16_t port, HWND notifyWnd);

} // namespace ts2::net

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
#include <functional>   // g_LoginNoticeHook (notice post-login, cf. plus bas)

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

// 3e argument de Net_LoginRequest, émis sur 4 octets LE en [265..268] du paquet
// opcode 0x0B (Crt_Memcpy(v23, &a3, 4u) EA 0x51b9e6). Valeur LITTÉRALE de l'unique
// appelant Scene_LoginUpdate 0x51A8D0 : `push 1606Ah` EA 0x51ab0e = 0x1606A = 90218
// (ordre des pushes __stdcall/retn 10h : a4=&outResult EA 0x51ab0d, a3=1606Ah,
// a2=byte_1669214/mot de passe EA 0x51ab13, a1=g_AccountName EA 0x51ab18 ; les tampons
// 0x1669194/0x1669214 contigus de 128 o confirment l'appariement a1/a2).
// Vraisemblablement une estampille de version/build cliente — SÉMANTIQUE NON PROUVÉE,
// seule la valeur l'est. CORRECTION : valait 106 (= 0x6A) — l'ancien commentaire
// « 106 = 0x6A » trahit la faute d'origine, l'octet BAS de 1606Ah lu pour l'opérande
// entier. Aucun autre appelant (xrefs_to 0x51B8E0 -> 1 seul site, EA 0x51ab20).
inline constexpr uint32_t kLoginExtra = 90218u; // 0x1606A

// Hôtes du serveur login/version (Scene_ServerSelectUpdate 0x518D77 / 0x518E2F).
inline constexpr char kLoginHostCom[] = "12sky2-login.geniusorc.com"; // Hôte login .com fidèle (Scene_ServerSelectUpdate 0x518D77) ; reliquat de test "127.0.0.1" reverté
inline constexpr char kLoginHostOrg[] = "12sky2-login.geniusorc.org";

// --- Notice « deltas post-login » (Net_LoginRequest 0x51B8E0, EA 0x51bd3e-0x51bd75) ---
// Index 1-based dans StrTable005 (005.DAT) du texte affiché quand au moins un des 3
// deltas de fiche est > 0 : `push 6F9h` EA 0x51bd5e = 1785, consommé par
// StrTable005_Get(g_LangId, 1785) 0x4C1D10 (EA 0x51bd68).
inline constexpr int32_t kLoginDeltaNoticeStrId = 1785;

// Le binaire ouvre la notice DEPUIS la couche réseau :
//   UI_NoticeDlg_Open(byte_18225C8, /*type*/1, StrTable005_Get(g_LangId, 1785), /*""*/&String)
//   (0x5C0280, EA 0x51bd75 ; le 4e argument est la chaîne VIDE — read_cstring 0x7ec95f -> "").
// Le port sépare Net et UI : on reproduit l'appel via ce hook, même motif que
// Game/CharSelectFlow.h::CharSelectHost::ShowNotice (std::function<void(int32_t strId)>,
// câblé par UI/LoginScene.cpp sur OpenNotice(game::Str(strId))). game::Str(id) est
// l'équivalent fidèle de StrTable005_Get (1-based, "" hors bornes).
// ÉCARTS ASSUMÉS (identiques à ceux déjà documentés pour ShowNotice) : le paramètre
// `type`=1 et le 4e argument "" ne sont pas modélisés (ils ne changent pas le flux).
// Hook NON POSÉ = no-op sûr : la notice n'apparaît pas, les deltas s'appliquent quand
// même (le binaire additionne AVANT de tester, cf. EA 0x51bd15 < 0x51bd3e).
extern std::function<void(int32_t strId)> g_LoginNoticeHook;

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
// NB : le binaire renvoie dans eax soit la NOUVELLE valeur de record[0]+16
// (EA 0x51bd0f), soit le retour de UI_NoticeDlg_Open (EA 0x51bd75) — valeur MORTE :
// l'unique appelant Scene_LoginUpdate ignore eax et relit l'out-param (EA 0x51ab25
// `mov edx, [ebp+var_4]`). Écart délibéré et inobservable ; le contrat de ce port
// (retour == outResult) est plus utile et strictement équivalent à l'usage.
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

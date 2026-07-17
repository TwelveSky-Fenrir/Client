// Net/GameServerDomains.h — table des hôtes de serveur de jeu (Net_SelectServerDomain 0x53FE90).
//
// Reproduction FIDÈLE (transcription 1:1, désassemblage confirmé bit-identique) de la table
// de résolution `domainId -> hostname` du client TwelveSky2. La fonction d'origine est PURE
// (aucun socket) : deux `switch` imbriqués sur `g_ServerModeFlag 0x166918C` puis sur
// `domainId`, écrivant un littéral via `Crt_StringInit 0x75CAB0`. Hex-Rays échoue dessus
// (~30 branches convergentes) ; le layout est prouvé par disasm intégral (288 instr.,
// jump tables `jpt_53FEE4`/`jpt_54019F`). Cf. Docs/TS2_GAMESERVER_DOMAINS.md.
//
// Le `domainId` (1..20) et le `gamePort` sont fournis DYNAMIQUEMENT par le login/CharSelect
// server dans la réponse à l'opcode 22 (`Net_ReqEnterCharInfo 0x52B070`) ou le paquet
// `Pkt_GameServerConnectResult 0x469CF0` (opcode 0x18) ; le client ne fait que traduire
// l'entier reçu en hostname via cette table codée en dur, résolu ensuite par DNS
// (`gethostbyname`, cf. Net/Login.cpp::ConnectGameServer, Net_ConnectGameServer 0x462A70).
#pragma once
#include <string>

namespace ts2::net {

// Mode 0 (LIVE, `g_ServerModeFlag==0` = lancement /0/0/2/1024/768) : 20 sous-domaines
// `*.geniusorc.com` DISTINCTS, index 1..20 (cases jumptable 0x53FEEB..0x54007B ; littéraux
// contigus 0x7A9C18 descendant à 0x7A996C, pas 0x24). 32 caractères + NUL -> char[33].
inline constexpr char kGameServerHostsLive[20][33] = {
    "3A6k9d1G4b5j8H2f7C.geniusorc.com", //  1  0x7A9C18
    "7E4b1f9I3g2D6a5J8c.geniusorc.com", //  2  0x7A9BF4
    "5G1d4H6j3A7k2B9c8F.geniusorc.com", //  3  0x7A9BD0
    "2C9j8b1H5g7K4a6I3f.geniusorc.com", //  4  0x7A9BAC
    "8F2g6H9j5C1a7i4D3b.geniusorc.com", //  5  0x7A9B88
    "1J3f7K2g8B6h9A5d4i.geniusorc.com", //  6  0x7A9B64
    "6D5j3A9k2F4c7G1h8E.geniusorc.com", //  7  0x7A9B40
    "9I4j7F1b2d3G6c8E5H.geniusorc.com", //  8  0x7A9B1C
    "3H6i8A2j7G5D1c9K4b.geniusorc.com", //  9  0x7A9AF8
    "5C1i9k2A4H6j7G3b8D.geniusorc.com", // 10  0x7A9AD4
    "2G4a5D7i9f6H3k8B1c.geniusorc.com", // 11  0x7A9AB0
    "7B1h8d2F3j6A4G9k5C.geniusorc.com", // 12  0x7A9A8C
    "4F6i8K9c2H5g7D1b3J.geniusorc.com", // 13  0x7A9A68
    "8D3a1c6G9j7K5h4B2f.geniusorc.com", // 14  0x7A9A44
    "1J9d7C4k6f2G5h3B8i.geniusorc.com", // 15  0x7A9A20
    "8H6g4F1b7K3a2J9d5I.geniusorc.com", // 16  0x7A99FC
    "1E4h6J2b9G5D8i7C3f.geniusorc.com", // 17  0x7A99D8
    "6C2a4k7B5g9i1F3d8H.geniusorc.com", // 18  0x7A99B4
    "9F3j7H5c4A1b8E2i6K.geniusorc.com", // 19  0x7A9990
    "4K8b2c7I9j5G6h3D1f.geniusorc.com", // 20  0x7A996C
};

// Modes 1 & 2 (staging) : 2 hôtes `.geniusorc.org`, index 1..2 (0x5400D0/0x5400E3 pour le
// mode 1, 0x540138/0x54014B pour le mode 2 — MÊMES 2 littéraux 0x7A9948/0x7A9924).
inline constexpr char kGameServerHostsStaging[2][33] = {
    "9B1x8Y5k3C1d8da2dd.geniusorc.org", // 1  0x7A9948
    "6T2y4L1i5S9ddd8a9d.geniusorc.org", // 2  0x7A9924
};

// Mode dev (tout autre `g_ServerModeFlag`) : le MÊME hôte pour index 1..10 (0x7A9910).
inline constexpr char kGameServerHostDev[] = "test_ts2_zone.co.kr";

// Repli hors-plage (toutes branches) : littéral non routable "0.0.0.0" (0x7A9714).
inline constexpr char kGameServerHostInvalid[] = "0.0.0.0";

// Miroir de `g_ServerModeFlag 0x166918C` (= GameConfig::buildVariant, 1er jeton cmdline,
// écrit par WinMain @0x4609XX). Posé UNE FOIS par LoginScene::Init (avant tout connect
// game-server) — le login précède toujours l'entrée en monde dans le flux.
inline int g_ServerMode = 0;

// GARDE (décision utilisateur, session 2026-07-17 : « connexion prod réelle ») : true =
// résolution + connexion RÉELLE aux hôtes de production ci-dessus. C'est le comportement
// FIDÈLE du binaire (`Net_ConnectGameServer 0x462A70` résout puis connecte l'hôte de table).
// Autorisation explicite et informée (caveat « infra de production potentiellement active »).
inline constexpr bool kResolveRealGameHosts = true;

// Résolveur FIDÈLE de `Net_SelectServerDomain 0x53FE90` : donnée pure, aucun effet de bord.
// `serverMode` = g_ServerModeFlag ; `domainId` = index reçu du serveur (1..20).
std::string ResolveGameServerDomainRaw(int serverMode, int domainId);

// Résolveur à POLITIQUE utilisé par le câblage (unique point gardé). Garde ON (D1) -> hôte
// de table pour `domainId` ; sur index HORS-PLAGE ("0.0.0.0") retombe sur `loginFallbackHost`
// (dégradation gracieuse documentée — un serveur correct n'émet jamais d'index hors [1,20]).
// Garde OFF -> `loginFallbackHost` (repli login, aucune connexion prod).
std::string SelectGameServerHost(int domainId, const char* loginFallbackHost);

} // namespace ts2::net

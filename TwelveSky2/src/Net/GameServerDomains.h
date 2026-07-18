// Net/GameServerDomains.h — game server host table (Net_SelectServerDomain 0x53FE90).
//
// FAITHFUL reproduction (1:1 transcription, disassembly-confirmed bit-identical) of the
// TwelveSky2 client's `domainId -> hostname` resolution table. The original function is PURE
// (no socket): two nested `switch` on `g_ServerModeFlag 0x166918C` then on `domainId`,
// writing a literal via `Crt_StringInit 0x75CAB0`. Hex-Rays fails on it
// (~30 converging branches); the layout is proven by full disasm (288 instr.,
// jump tables `jpt_53FEE4`/`jpt_54019F`). See Docs/TS2_GAMESERVER_DOMAINS.md.
//
// `domainId` (1..20) and `gamePort` are provided DYNAMICALLY by the login/CharSelect
// server in the response to opcode 22 (`Net_ReqEnterCharInfo 0x52B070`) or the packet
// `Pkt_GameServerConnectResult 0x469CF0` (opcode 0x18); the client just translates
// the received integer into a hostname via this hardcoded table, resolved afterwards by DNS
// (`gethostbyname`, see Net/Login.cpp::ConnectGameServer, Net_ConnectGameServer 0x462A70).
#pragma once
#include <string>

namespace ts2::net {

// Mode 0 (LIVE, `g_ServerModeFlag==0` = launched with /0/0/2/1024/768): 20 DISTINCT
// `*.geniusorc.com` subdomains, index 1..20 (jumptable cases 0x53FEEB..0x54007B; contiguous
// literals 0x7A9C18 descending to 0x7A996C, stride 0x24). 32 chars + NUL -> char[33].
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

// Modes 1 & 2 (staging): 2 `.geniusorc.org` hosts, index 1..2 (0x5400D0/0x5400E3 for
// mode 1, 0x540138/0x54014B for mode 2 — SAME 2 literals 0x7A9948/0x7A9924).
inline constexpr char kGameServerHostsStaging[2][33] = {
    "9B1x8Y5k3C1d8da2dd.geniusorc.org", // 1  0x7A9948
    "6T2y4L1i5S9ddd8a9d.geniusorc.org", // 2  0x7A9924
};

// Dev mode (any other `g_ServerModeFlag`): the SAME host for index 1..10 (0x7A9910).
inline constexpr char kGameServerHostDev[] = "test_ts2_zone.co.kr";

// Out-of-range fallback (all branches): non-routable literal "0.0.0.0" (0x7A9714).
inline constexpr char kGameServerHostInvalid[] = "0.0.0.0";

// Mirror of `g_ServerModeFlag 0x166918C` (= GameConfig::buildVariant, 1st cmdline
// token, written by WinMain @0x4609XX). Set ONCE by LoginScene::Init (before any
// game-server connect) — login always precedes world entry in the flow.
inline int g_ServerMode = 0;

// GUARD (user decision, session 2026-07-17: "real prod connection"): true =
// REAL resolution + connection to the production hosts above. This is the FAITHFUL
// behavior of the binary (`Net_ConnectGameServer 0x462A70` resolves then connects the table
// host). Explicit and informed authorization (caveat: "potentially live production infra").
inline constexpr bool kResolveRealGameHosts = true;

// FAITHFUL resolver of `Net_SelectServerDomain 0x53FE90`: pure data, no side effect.
// `serverMode` = g_ServerModeFlag; `domainId` = index received from the server (1..20).
std::string ResolveGameServerDomainRaw(int serverMode, int domainId);

// POLICY resolver used by the wiring (single gated entry point). Guard ON (D1) -> table
// host for `domainId`; on OUT-OF-RANGE index ("0.0.0.0") falls back to `loginFallbackHost`
// (documented graceful degradation — a correct server never emits an index outside [1,20]).
// Guard OFF -> `loginFallbackHost` (login fallback, no prod connection).
std::string SelectGameServerHost(int domainId, const char* loginFallbackHost);

} // namespace ts2::net

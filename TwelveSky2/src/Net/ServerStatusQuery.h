// Net/ServerStatusQuery.h — "live" query of a server's population/status.
//
// Faithful rewrite of Net_QueryServerStatus (0x519CC0): the client opens a
// TCP socket to (host, port), SENDS NOTHING, and waits for the server to push a
// 17-byte status record. The first 5 bytes [0..4] are an
// ignored header; the payload is:
//   maxPopulation      = bytes [5..8]   (uint32 LE)
//   loadStep           = bytes [9..12]  (uint32 LE)   "load step" (gauge)
//   currentPopulation  = bytes [13..16] (uint32 LE)   = the binary's return value
// On any failure (socket / gethostbyname / connect / recv<=0 / disconnect before
// 17 bytes) the binary returns -1 and does NOT write outMaxPop/outLoadStep.
#pragma once
#include <cstdint>
#include <string>

namespace ts2::net {

// Result of a server status query (see Net_QueryServerStatus 0x519CC0).
struct LiveServerStatus {
    int32_t maxPopulation     = 0;   // bytes [5..8] of the status record
    int32_t loadStep          = 0;   // bytes [9..12]
    int32_t currentPopulation = -1;  // bytes [13..16]; -1 = failure/in progress
    bool    ok                = false; // false if connect/recv failed (curPop stays -1)
};

// Faithful to Net_QueryServerStatus 0x519CC0: TCP connect(host, port), recv EXACTLY
// 17 bytes, parse maxPop@5 / loadStep@9 / curPop@13. ok=false + currentPopulation=-1
// on any failure. No byte is sent to the server (the server pushes the record).
//
// FIDELITY GAP (documented): the binary is BLOCKING (connect + recv with no
// explicit timeout). To never freeze the caller (30 FPS UI loop), the wait is
// bounded via select() on connect and recv (`timeoutMs`). The parsing semantics
// and protocol (no send, exact 17 bytes, offsets) remain identical.
LiveServerStatus QueryServerStatusLive(const std::string& host, uint16_t port,
                                       uint32_t timeoutMs = 3000);

} // namespace ts2::net

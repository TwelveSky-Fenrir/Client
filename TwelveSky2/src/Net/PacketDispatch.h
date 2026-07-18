// Net/PacketDispatch.h — receives + dispatches incoming packets by opcode.
//
// Faithful to:
//   Net_RecvDispatch       0x463040  (recv -> append to buffer -> per-opcode drain loop)
//   Net_InitPacketHandlers 0x463270  (builds the size/handler tables)
//
// Original memory model (network client = dword_8156A0):
//   +32     : receive buffer (200000 bytes)      -> buffer_
//   +200032 : number of bytes filled             -> filled_
//   opcode  = buffer[0] (this+32); payload = buffer[1] (this+33, alias unk_8156C1)
//   dword_846408[op] = handler; dword_846808[op] = TOTAL size of the fixed frame
//
// The incoming stream is NOT decrypted here: Net_RecvDispatch reads the opcode in clear.
// (The single-byte XOR only applies on send; see Net_Send* builders.)
#pragma once
#include <winsock2.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>
#include "Net/Opcodes.h"

namespace ts2::net {

// Client receive buffer (this+32 .. this+200032).
inline constexpr std::size_t kRecvBufferSize = 200000;

// Only variable-length opcode: [op:u8][len:u32][payload] (Net_OnScriptTrigger 0x63).
inline constexpr std::uint8_t kVariableOpcode = 0x63;

// Variable frame header: opcode(1) + length(4).
inline constexpr std::uint32_t kVariableHeaderSize = 5;

// ---------------------------------------------------------------------------
// Per-opcode size table (dword_846808). Value = TOTAL size of the fixed frame
// on the wire = [opcode:u8] + payload; 0 = unknown opcode (no handler).
// All handled opcodes have a non-zero size, statically recovered from
// Net_InitPacketHandlers 0x463270 (the static IDB reads 0: these globals are
// initialized at runtime — so the disassembly is the source of truth).
// For the variable opcode 0x63 the table holds 5 (minimum header size).
// ---------------------------------------------------------------------------
constexpr std::array<std::uint32_t, 256> MakeSizeTable() {
    std::array<std::uint32_t, 256> t{}; // all zero
    t[0x0c] = 10377; t[0x0d] = 3781; t[0x0e] = 1005; t[0x0f] = 613;
    t[0x10] = 441;   t[0x11] = 25;   t[0x12] = 93;   t[0x13] = 97;
    t[0x14] = 62;    t[0x15] = 77;   t[0x16] = 9;    t[0x17] = 109;
    t[0x18] = 17;    t[0x19] = 13;   t[0x1a] = 17;   t[0x1b] = 13;
    t[0x1c] = 9;     t[0x1d] = 33;   t[0x1e] = 33;   t[0x1f] = 33;
    t[0x20] = 37;    t[0x21] = 29;   t[0x22] = 1237; t[0x23] = 5;
    t[0x24] = 1237;  t[0x25] = 78;   t[0x26] = 53;   t[0x27] = 21;
    t[0x28] = 5;     t[0x29] = 75;   t[0x2a] = 87;   t[0x2b] = 75;
    t[0x2c] = 9;     t[0x2d] = 41;   t[0x2e] = 18;   t[0x2f] = 1;
    t[0x30] = 5;     t[0x31] = 21;   t[0x32] = 5;    t[0x33] = 5;
    t[0x34] = 18;    t[0x35] = 1;    t[0x36] = 5;    t[0x37] = 233;
    t[0x38] = 233;   t[0x39] = 5;    t[0x3a] = 5;    t[0x3b] = 14;
    t[0x3c] = 1;     t[0x3d] = 5;    t[0x3e] = 18;   t[0x3f] = 9;
    t[0x40] = 5;     t[0x41] = 14;   t[0x42] = 1;    t[0x43] = 5;
    t[0x44] = 18;    t[0x45] = 1;    t[0x46] = 5;    t[0x47] = 14;
    t[0x48] = 1;     t[0x49] = 5;    t[0x4a] = 70;   t[0x4b] = 14;
    t[0x4c] = 75;    t[0x4d] = 14;   t[0x4e] = 14;   t[0x4f] = 18;
    t[0x50] = 14;    t[0x51] = 1;    t[0x52] = 5;    t[0x53] = 1397;
    t[0x54] = 75;    t[0x55] = 75;   t[0x56] = 5;    t[0x57] = 14;
    t[0x58] = 109;   t[0x59] = 79;   t[0x5a] = 75;   t[0x5b] = 213;
    t[0x5c] = 13;    t[0x5d] = 9;    t[0x5e] = 105;  t[0x5f] = 21;
    t[0x60] = 17;    t[0x61] = 105;  t[0x62] = 1;    t[0x63] = 5;
    t[0x64] = 17;    t[0x65] = 5;    t[0x66] = 9;    t[0x67] = 21;
    t[0x68] = 5;     t[0x69] = 29;   t[0x6a] = 33;   t[0x6b] = 9;
    t[0x6c] = 29;    t[0x6d] = 8969; t[0x6e] = 1;    t[0x6f] = 9;
    t[0x70] = 33;    t[0x71] = 5;    t[0x72] = 5;    t[0x73] = 13;
    t[0x74] = 13;    t[0x75] = 21;   t[0x76] = 17;   t[0x77] = 61;
    t[0x78] = 29;    t[0x79] = 22;   t[0x7a] = 25;   t[0x7b] = 13;
    t[0x7c] = 13;    t[0x7d] = 9;    t[0x7e] = 22;   t[0x7f] = 21;
    t[0x80] = 21;    t[0x81] = 21;   t[0x82] = 61;   t[0x83] = 365;
    t[0x84] = 9;     t[0x85] = 9;    t[0x86] = 65;   t[0x87] = 833;
    t[0x88] = 877;   t[0x89] = 13;   t[0x8a] = 21;   t[0x8b] = 79;
    t[0x8c] = 9;     t[0x8d] = 65;   t[0x8e] = 9;    t[0x8f] = 1;
    t[0x90] = 22;    t[0x91] = 29;   t[0x92] = 21;   t[0x93] = 10;
    t[0x94] = 685;   t[0x95] = 57;   t[0x96] = 685;  t[0x97] = 25;
    t[0x98] = 97;    t[0x99] = 14;   t[0x9a] = 9;    t[0x9b] = 17;
    t[0x9d] = 5;     t[0x9e] = 437;  t[0x9f] = 9;    t[0xa3] = 25;
    t[0xa4] = 29;    t[0xa5] = 13;   t[0xa6] = 9;    t[0xa8] = 25;
    t[0xa9] = 21;    t[0xaa] = 6;    t[0xab] = 37;   t[0xac] = 17;
    t[0xad] = 33;    t[0xae] = 29;   t[0xaf] = 13;   t[0xb0] = 25;
    t[0xb1] = 13;    t[0xb2] = 605;  t[0xb3] = 41;   t[0xb4] = 21;
    t[0xb6] = 25;
    return t;
}

// Frozen table, usable in constexpr.
inline constexpr std::array<std::uint32_t, 256> kPacketSize = MakeSizeTable();

// Accessor (reference to the frozen table).
inline const std::array<std::uint32_t, 256>& PacketSizeTable() { return kPacketSize; }

// Result of pumping a socket event (useful return of Net_RecvDispatch).
enum class RecvResult {
    Ok,     // data processed or nothing to read (WSAEWOULDBLOCK) — socket still open
    Closed, // FD_CLOSE, recv==0, or socket error -> caller must close
};

// ---------------------------------------------------------------------------
// PacketDispatcher: owns the receive buffer, the size table, and the
// registrable handler table; reproduces the original drain loop.
// ---------------------------------------------------------------------------
class PacketDispatcher {
public:
    // Handler for an incoming packet.
    //   opcode     : raw opcode (buffer[0]).
    //   payload    : first byte after the header (buffer+1 fixed, buffer+5 for 0x63).
    //   payloadLen : payload length (size[op]-1 fixed, len for 0x63).
    using Handler = std::function<void(std::uint8_t opcode,
                                       const std::uint8_t* payload,
                                       std::uint32_t payloadLen)>;

    PacketDispatcher();

    // Registers a handler (dword_846408 table). An empty handler = ignored opcode.
    void SetHandler(std::uint8_t opcode, Handler h);
    void SetHandler(Incoming opcode, Handler h) {
        SetHandler(static_cast<std::uint8_t>(opcode), std::move(h));
    }
    void ClearHandlers();

    // Overrides a size (dword_846808 table), in case an opcode needs to be
    // recalibrated from a runtime dump. All known opcodes are already populated.
    void SetSize(std::uint8_t opcode, std::uint32_t size) { sizes_[opcode] = size; }
    std::uint32_t SizeOf(std::uint8_t opcode) const { return sizes_[opcode]; }

    // Clears the receive buffer (partial reset, Net_CloseSocket-style).
    void Reset() { filled_ = 0; }
    std::uint32_t Filled() const { return filled_; }

    // Equivalent to Net_RecvDispatch(this, _, netEvent). netEvent = WSAGETSELECTEVENT(lParam)
    // of the socket message 0x401 (WM_USER+1). FD_READ -> one recv + drain; FD_CLOSE -> Closed.
    RecvResult OnSocketEvent(SOCKET s, std::uint16_t netEvent);

    // Drains all complete packets present in the buffer.
    void Drain();

    // Injects raw bytes into the buffer then drains (tests / offline replay).
    // Returns false if capacity (kRecvBufferSize) would be exceeded.
    bool PushBytes(const std::uint8_t* data, std::uint32_t n);

private:
    void Consume(std::uint32_t n); // memmove(buffer, buffer+n, filled-n); filled -= n

    std::array<std::uint32_t, 256>       sizes_;
    std::array<Handler, 256>             handlers_;
    std::array<std::uint8_t, kRecvBufferSize> buffer_;
    std::uint32_t                        filled_ = 0;
};

} // namespace ts2::net

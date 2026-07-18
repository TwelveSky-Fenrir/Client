// Net/SendPackets_Op.cpp — outbound builder definitions: Net_SendOp* family
// (split from SendPackets.cpp; see SendPackets.h for the shared declarations).
#include "Net/SendPackets.h"
#include <cstring>   // std::memcpy (Net_SendCmd_251, Net_SendUdpReport)

namespace ts2::net {

void Net_SendOp41(NetClient& nc, int8_t arg1) {
    // opcode 41 (0x29) : 1 byte field
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.Finalize(0x29, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp49(NetClient& nc, int8_t arg1) {
    // opcode 49 (0x31) : 1 byte field
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.Finalize(0x31, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp57(NetClient& nc, int8_t arg1) {
    // opcode 57 (0x39) : 1 byte field
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.Finalize(0x39, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp65(NetClient& nc, const void* name13) {
    // opcode 65 (0x41) : block/string of 13 bytes
    PacketWriter w;
    w.WriteBytes(name13, 13); // this+9
    w.Finalize(0x41, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp73(NetClient& nc) {
    // opcode 73 (0x49) : empty payload
    PacketWriter w;
    w.Finalize(0x49, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp81(NetClient& nc, const void* buf61) {
    // opcode 81 (0x51) : raw block of 61 bytes
    PacketWriter w;
    w.WriteBytes(buf61, 61); // this+9
    w.Finalize(0x51, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp89(NetClient& nc, int8_t arg1, int8_t arg2) {
    // opcode 89 (0x59) : 2 byte fields
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.WriteChar4LE(arg2); // this+13
    w.Finalize(0x59, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp98(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3) {
    // opcode 98 (0x62) : 3 byte fields
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.WriteChar4LE(arg2); // this+13
    w.WriteChar4LE(arg3); // this+17
    w.Finalize(0x62, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp105(NetClient& nc) {
    // opcode 105 (0x69) : empty payload
    PacketWriter w;
    w.Finalize(0x69, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp117(NetClient& nc) {
    // opcode 117 (0x75) : empty payload
    PacketWriter w;
    w.Finalize(0x75, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp128(NetClient& nc) {
    // opcode 128 (0x80) : empty payload
    PacketWriter w;
    w.Finalize(0x80, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp138(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7, int8_t arg8, int8_t arg9, int8_t arg10, int8_t arg11, int8_t arg12, int8_t arg13) {
    // opcode 138 (0x8A) : 13 byte fields (each sent as 4 bytes LE) = 52 bytes of payload
    PacketWriter w;
    w.WriteChar4LE(arg1);  // this+9
    w.WriteChar4LE(arg2);  // this+13
    w.WriteChar4LE(arg3);  // this+17
    w.WriteChar4LE(arg4);  // this+21
    w.WriteChar4LE(arg5);  // this+25
    w.WriteChar4LE(arg6);  // this+29
    w.WriteChar4LE(arg7);  // this+33
    w.WriteChar4LE(arg8);  // this+37
    w.WriteChar4LE(arg9);  // this+41
    w.WriteChar4LE(arg10); // this+45
    w.WriteChar4LE(arg11); // this+49
    w.WriteChar4LE(arg12); // this+53
    w.WriteChar4LE(arg13); // this+57
    w.Finalize(0x8A, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp42(NetClient& nc, int8_t a2, int8_t a3, int8_t a4, const void* buf24, int8_t a6) {
    // [net] opcode 0x2A : 3 bytes + 24-byte buffer + 1 byte
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteChar4LE(a3);            // this+13
    w.WriteChar4LE(a4);            // this+17
    w.WriteBytes(buf24, 24);       // this+21 : 24-byte buffer
    w.WriteChar4LE(a6);            // this+45
    w.Finalize(0x2A, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp50(NetClient& nc) {
    // [net] opcode 0x32 : empty payload
    PacketWriter w;
    w.Finalize(0x32, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp58(NetClient& nc, int8_t arg) {
    // [net] opcode 0x3A : one byte field
    PacketWriter w;
    w.WriteChar4LE(arg);           // this+9
    w.Finalize(0x3A, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp66(NetClient& nc) {
    // [net] opcode 0x42 : empty payload
    PacketWriter w;
    w.Finalize(0x42, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp74(NetClient& nc, int8_t arg) {
    // [net] opcode 0x4A : one byte field
    PacketWriter w;
    w.WriteChar4LE(arg);           // this+9
    w.Finalize(0x4A, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp82(NetClient& nc, int8_t a2, int8_t a3) {
    // [net] opcode 0x52 : two byte fields
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteChar4LE(a3);            // this+13
    w.Finalize(0x52, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp90(NetClient& nc, int8_t a2, int8_t a3) {
    // [net] opcode 0x5A : two byte fields
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteChar4LE(a3);            // this+13
    w.Finalize(0x5A, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp99(NetClient& nc, int8_t a2, const void* appearance68, const void* autoHunt44) {
    // [net] opcode 0x63 : 1 byte + 68-byte appearance blob (orig. unk_16755B0)
    //        + 44-byte auto-hunt config (orig. g_AutoHuntMode)
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteBytes(appearance68, 68);// this+13 : appearance 68-byte blob
    w.WriteBytes(autoHunt44, 44);  // this+81 : blob auto-hunt 44 bytes
    w.Finalize(0x63, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp106(NetClient& nc, int8_t a2, const void* buf12) {
    // [net] opcode 0x6A : 1 byte + 12-byte buffer.
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteBytes(buf12, 12);       // this+13 : 12-byte buffer
    w.Finalize(0x6A, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp116(NetClient& nc) {
    // [net] opcode 0x74 : empty payload
    PacketWriter w;
    w.Finalize(0x74, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp129(NetClient& nc, int8_t a2, int8_t a3) {
    // [net] opcode 0x81 (=129, encoded as -127 in the decompiler) : two byte fields
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteChar4LE(a3);            // this+13
    w.Finalize(0x81, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp139(NetClient& nc, int8_t arg) {
    // [net] opcode 0x8B (=139, encoded as -117 in the decompiler) : one byte field
    PacketWriter w;
    w.WriteChar4LE(arg);           // this+9
    w.Finalize(0x8B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp110(NetClient& nc, int8_t arg0, int8_t arg1) {
    // [net] opcode 0x6E : 2 byte fields (each promoted to 4 B LE). Length 17.
    PacketWriter w;
    w.WriteChar4LE(arg0);   // this+9
    w.WriteChar4LE(arg1);   // this+13
    w.Finalize(0x6E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp43(NetClient& nc, const void* payload13, int8_t flag) {
    // [net] opcode 0x2B : 13-byte block then 1 byte field (promoted to 4 bytes LE). Length 26.
    PacketWriter w;
    w.WriteBytes(payload13, 13);   // this+9 (memcpy 13 B)
    w.WriteChar4LE(flag);          // this+22
    w.Finalize(0x2B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp51(NetClient& nc) {
    // [net] opcode 0x33 : no payload (length 9, header only).
    PacketWriter w;
    w.Finalize(0x33, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp59(NetClient& nc, const void* payload13) {
    // [net] opcode 0x3B : 13-byte block. Length 22.
    PacketWriter w;
    w.WriteBytes(payload13, 13);   // this+9 (memcpy 13 B)
    w.Finalize(0x3B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp67(NetClient& nc, int8_t value) {
    // [net] opcode 0x43 : 1 byte field (promoted to 4 bytes LE). Length 13.
    PacketWriter w;
    w.WriteChar4LE(value);   // this+9
    w.Finalize(0x43, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp75(NetClient& nc, int8_t kind, const void* payload500) {
    // [net] opcode 0x4B : 1 byte field (kind, promoted to 4 bytes LE) + 500-byte block. Length 513.
    // Generic builder reused by Net_SendGuarded_5/13 (guild commands).
    PacketWriter w;
    w.WriteChar4LE(kind);          // this+9
    w.WriteBytes(payload500, 500); // this+13 (memcpy 500 B)
    w.Finalize(0x4B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp83(NetClient& nc, int8_t arg0, int8_t arg1) {
    // [net] opcode 0x53 : 2 byte fields (each promoted to 4 B LE). Length 17.
    PacketWriter w;
    w.WriteChar4LE(arg0);   // this+9
    w.WriteChar4LE(arg1);   // this+13
    w.Finalize(0x53, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp91(NetClient& nc) {
    // [net] opcode 0x5B : no payload (length 9, header only).
    PacketWriter w;
    w.Finalize(0x5B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp101(NetClient& nc) {
    // [net] opcode 0x65 : no payload (length 9, header only).
    PacketWriter w;
    w.Finalize(0x65, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp107(NetClient& nc, int8_t kind, const void* payload13) {
    // [net] opcode 0x6B : 1 byte field (kind, promoted to 4 bytes LE) + 13-byte block. Length 26.
    PacketWriter w;
    w.WriteChar4LE(kind);          // this+9
    w.WriteBytes(payload13, 13);   // this+13 (memcpy 13 B)
    w.Finalize(0x6B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp118(NetClient& nc) {
    // [net] opcode 0x76 : no payload (length 9, header only).
    PacketWriter w;
    w.Finalize(0x76, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp131(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7, int8_t arg8) {
    // [net] opcode 0x83 (131, written as -125 in a signed char) : 9 byte fields (each promoted to 4 B LE). Length 45.
    PacketWriter w;
    w.WriteChar4LE(arg0);   // this+9
    w.WriteChar4LE(arg1);   // this+13
    w.WriteChar4LE(arg2);   // this+17
    w.WriteChar4LE(arg3);   // this+21
    w.WriteChar4LE(arg4);   // this+25
    w.WriteChar4LE(arg5);   // this+29
    w.WriteChar4LE(arg6);   // this+33
    w.WriteChar4LE(arg7);   // this+37
    w.WriteChar4LE(arg8);   // this+41
    w.Finalize(0x83, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp140(NetClient& nc) {
    // [net] opcode 0x8C (140, written as -116 in a signed char) : no payload (length 9).
    PacketWriter w;
    w.Finalize(0x8C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp36(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5) {
    // [net] opcode 0x24 (36) — 5 byte fields (4 bytes LE each), total 29
    PacketWriter w;
    w.WriteChar4LE(arg1);                           // this+9
    w.WriteChar4LE(arg2);                           // this+13
    w.WriteChar4LE(arg3);                           // this+17
    w.WriteChar4LE(arg4);                           // this+21
    w.WriteChar4LE(arg5);                           // this+25
    w.Finalize(0x24, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp44(NetClient& nc) {
    // [net] opcode 0x2C (44) — no payload (total packet 9)
    PacketWriter w;
    w.Finalize(0x2C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp52(NetClient& nc) {
    // [net] opcode 0x34 (52) — no payload (total packet 9)
    PacketWriter w;
    w.Finalize(0x34, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp60(NetClient& nc) {
    // [net] opcode 0x3C (60) — no payload (total packet 9)
    PacketWriter w;
    w.Finalize(0x3C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp68(NetClient& nc, const void* data61) {
    // [net] opcode 0x44 (68) — raw payload of 61 bytes (total 70)
    PacketWriter w;
    w.WriteBytes(data61, 61);                       // this+9
    w.Finalize(0x44, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp76(NetClient& nc, const void* data61) {
    // [net] opcode 0x4C (76) — raw payload of 61 bytes (total 70)
    PacketWriter w;
    w.WriteBytes(data61, 61);                       // this+9
    w.Finalize(0x4C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp84(NetClient& nc, const void* data101, int8_t flag) {
    // [net] opcode 0x54 (84) — 101-byte block + 1 byte (4 LE), total 114
    PacketWriter w;
    w.WriteBytes(data101, 101);                     // this+9
    w.WriteChar4LE(flag);                           // this+110
    w.Finalize(0x54, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp92(NetClient& nc, int8_t value) {
    // [net] opcode 0x5C (92) — 1 byte field (4 bytes LE), total 13
    PacketWriter w;
    w.WriteChar4LE(value);                          // this+9
    w.Finalize(0x5C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp136(NetClient& nc) {
    // [net] opcode 0x88 (136) — no payload (total packet 9)
    PacketWriter w;
    w.Finalize(0x88, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp111(NetClient& nc, int8_t value) {
    // [net] opcode 0x6F (111) — 1 byte field (4 bytes LE), total 13
    PacketWriter w;
    w.WriteChar4LE(value);                          // this+9
    w.Finalize(0x6F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp119(NetClient& nc) {
    // [net] opcode 0x77 (119) — no payload (total packet 9)
    PacketWriter w;
    w.Finalize(0x77, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp132(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4) {
    // [net] opcode 0x84 (132) — 4 byte fields (4 bytes LE each), total 25
    PacketWriter w;
    w.WriteChar4LE(arg1);                           // this+9
    w.WriteChar4LE(arg2);                           // this+13
    w.WriteChar4LE(arg3);                           // this+17
    w.WriteChar4LE(arg4);                           // this+21
    w.Finalize(0x84, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp141(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, const void* data24) {
    // [net] opcode 0x8D (141) — 3 byte fields + 24-byte block, total 45
    PacketWriter w;
    w.WriteChar4LE(arg1);                           // this+9
    w.WriteChar4LE(arg2);                           // this+13
    w.WriteChar4LE(arg3);                           // this+17
    w.WriteBytes(data24, 24);                       // this+21
    w.Finalize(0x8D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp37(NetClient& nc) {
    // opcode 0x25 (37) — no payload (len 9)
    PacketWriter w;
    w.Finalize(0x25, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp45(NetClient& nc, int8_t a) {
    // opcode 0x2D (45) — 1 char field sent as 4 bytes LE (len 13)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.Finalize(0x2D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp53(NetClient& nc, const void* payload) {
    // opcode 0x35 (53) — raw payload of 13 bytes (len 22)
    PacketWriter w;
    w.WriteBytes(payload, 13);
    w.Finalize(0x35, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp61(NetClient& nc, int8_t a) {
    // opcode 0x3D (61) — 1 char field sent as 4 bytes LE (len 13)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.Finalize(0x3D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp69(NetClient& nc) {
    // opcode 0x45 (69) — no payload (len 9)
    PacketWriter w;
    w.Finalize(0x45, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp77(NetClient& nc, const void* payload) {
    // opcode 0x4D (77) — raw payload of 61 bytes (len 70)
    PacketWriter w;
    w.WriteBytes(payload, 61);
    w.Finalize(0x4D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp85(NetClient& nc, uint32_t length, const void* data) {
    // opcode 0x55 (85) — variable TLV: [u32 length][length bytes] (len = length+13)
    // original source: struct a2 -> length at a2+4, data at a2+8
    PacketWriter w;
    w.WriteU32(length);          // field length u32 (this+9)
    w.WriteBytes(data, length);  // payload variable (this+13)
    w.Finalize(0x55, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp94(NetClient& nc, const void* data) {
    // opcode 0x5E (94) — copies 64 bytes from a global buffer (dword_1674A60) (len 73)
    PacketWriter w;
    w.WriteBytes(data, 64);   // dword_1674A60, 64 B
    w.Finalize(0x5E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp100(NetClient& nc, int8_t a, int8_t b, const void* data) {
    // opcode 0x64 (100) — 2 char fields (4 LE each) + 13-byte buffer (len 30)
    PacketWriter w;
    w.WriteChar4LE(a);         // this+9
    w.WriteChar4LE(b);         // this+13
    w.WriteBytes(data, 13);    // this+17
    w.Finalize(0x64, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp112(NetClient& nc, const void* payload) {
    // opcode 0x70 (112) — raw payload of 61 bytes (len 70)
    PacketWriter w;
    w.WriteBytes(payload, 61);
    w.Finalize(0x70, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp120(NetClient& nc, int8_t a, int8_t b, int8_t c) {
    // opcode 0x78 (120) — 3 char fields, each sent as 4 bytes LE (len 21)
    PacketWriter w;
    w.WriteChar4LE(a);   // this+9
    w.WriteChar4LE(b);   // this+13
    w.WriteChar4LE(c);   // this+17
    w.Finalize(0x78, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp133(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d, int8_t e, int8_t f, int8_t g, int8_t h, int8_t i) {
    // opcode 0x85 (133, written as -123) — 9 char fields, each sent as 4 bytes LE (len 45)
    PacketWriter w;
    w.WriteChar4LE(a);   // this+9
    w.WriteChar4LE(b);   // this+13
    w.WriteChar4LE(c);   // this+17
    w.WriteChar4LE(d);   // this+21
    w.WriteChar4LE(e);   // this+25
    w.WriteChar4LE(f);   // this+29
    w.WriteChar4LE(g);   // this+33
    w.WriteChar4LE(h);   // this+37
    w.WriteChar4LE(i);   // this+41
    w.Finalize(0x85, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp142(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d) {
    // opcode 0x8E (142, written as -114) — 4 char fields, each sent as 4 bytes LE (len 25)
    PacketWriter w;
    w.WriteChar4LE(a);   // this+9
    w.WriteChar4LE(b);   // this+13
    w.WriteChar4LE(c);   // this+17
    w.WriteChar4LE(d);   // this+21
    w.Finalize(0x8E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp38(NetClient& nc, const void* payload61) {
    // [net] opcode 0x26 : party chat, 61-byte block
    PacketWriter w;
    w.WriteBytes(payload61, 61);
    w.Finalize(0x26, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp46(NetClient& nc) {
    // [net] opcode 0x2E : no argument (empty payload)
    PacketWriter w;
    w.Finalize(0x2E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp54(NetClient& nc) {
    // [net] opcode 0x36 : no argument (empty payload)
    PacketWriter w;
    w.Finalize(0x36, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp62(NetClient& nc) {
    // [net] opcode 0x3E : no argument (empty payload)
    PacketWriter w;
    w.Finalize(0x3E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp70(NetClient& nc, const void* payload13) {
    // [net] opcode 0x46 : 13-byte block
    PacketWriter w;
    w.WriteBytes(payload13, 13);
    w.Finalize(0x46, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp78(NetClient& nc, const void* payload13) {
    // [net] opcode 0x4E : 13-byte block
    PacketWriter w;
    w.WriteBytes(payload13, 13);
    w.Finalize(0x4E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp86(NetClient& nc, int8_t a, int8_t b) {
    // [net] opcode 0x56 : two byte fields (sent as 4 bytes LE)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.Finalize(0x56, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp95(NetClient& nc, const void* position12, int8_t kind) {
    // [net] opcode 0x5F : position float3 (12 B) then a byte
    // Original: position read from the self global (flt_1687330).
    PacketWriter w;
    w.WriteBytes(position12, 12);
    w.WriteChar4LE(kind);
    w.Finalize(0x5F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp102(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d) {
    // [net] opcode 0x66 : four byte fields (sent as 4 bytes LE)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.WriteChar4LE(c);
    w.WriteChar4LE(d);
    w.Finalize(0x66, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp113(NetClient& nc, int8_t a) {
    // [net] opcode 0x71 : one byte field (sent as 4 bytes LE)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.Finalize(0x71, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp121(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d) {
    // [net] opcode 0x79 : four byte fields (sent as 4 bytes LE)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.WriteChar4LE(c);
    w.WriteChar4LE(d);
    w.Finalize(0x79, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp134(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d) {
    // [net] opcode 0x86 (=134 ; -122 signed in the decompiler) : four byte fields
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.WriteChar4LE(c);
    w.WriteChar4LE(d);
    w.Finalize(0x86, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp143(NetClient& nc, int8_t a, int8_t b, int8_t c) {
    // [net] opcode 0x8F (=143 ; -113 signed in the decompiler) : three byte fields
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.WriteChar4LE(c);
    w.Finalize(0x8F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp39(NetClient& nc, const void* target13, const void* message61) {
    // [net] opcode 39 (0x27) : whisper -> target (13 B) + message (61 B)
    PacketWriter w;
    w.WriteBytes(target13, 13);    // +9
    w.WriteBytes(message61, 61);   // +22
    w.Finalize(0x27, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 13 + 61 = 83
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp47(NetClient& nc, const void* payload13) {
    // [net] opcode 47 (0x2F) : 13-byte block
    PacketWriter w;
    w.WriteBytes(payload13, 13);   // +9
    w.Finalize(0x2F, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 13 = 22
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp55(NetClient& nc, int8_t value) {
    // [net] opcode 55 (0x37) : 1 byte field
    PacketWriter w;
    w.WriteChar4LE(value);   // +9
    w.Finalize(0x37, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 4 = 13
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp63(NetClient& nc) {
    // [net] opcode 63 (0x3F) : no payload
    PacketWriter w;
    w.Finalize(0x3F, DefaultRng(), nc.xorKey, nc.seq);  // len = 9
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp71(NetClient& nc) {
    // [net] opcode 71 (0x47) : no payload
    PacketWriter w;
    w.Finalize(0x47, DefaultRng(), nc.xorKey, nc.seq);  // len = 9
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp79(NetClient& nc, int8_t subType, const void* payload100) {
    // [net] opcode 79 (0x4F): shared builder -> sub-type byte (+9) + 100-byte block (+13).
    // Used by Net_SendMenu_2 (sub-type 2). The caller provides a buffer >= 100 B.
    PacketWriter w;
    w.WriteChar4LE(subType);        // +9
    w.WriteBytes(payload100, 100);  // +13
    w.Finalize(0x4F, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 4 + 100 = 113
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp87(NetClient& nc, int8_t f0, int8_t f1) {
    // [net] opcode 87 (0x57) : 2 byte fields
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.Finalize(0x57, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 8 = 17
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp96(NetClient& nc, int8_t f0, int8_t f1, int8_t f2) {
    // [net] opcode 96 (0x60): 3 byte fields.
    // WARNING: the original fixes len=23 even though it only writes 3 fields (+9,+13,+17),
    // leaving 2 bytes UNINITIALIZED at +21/+22. We reproduce them as zeros to keep
    // the 23-byte length expected by the server.
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.WriteChar4LE(f2);   // +17
    w.WriteU16(0);        // +21 : padding (2 B)
    w.Finalize(0x60, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 12 + 2 = 23
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp103(NetClient& nc, int8_t f0, int8_t f1, int8_t f2) {
    // [net] opcode 103 (0x67) : 3 byte fields
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.WriteChar4LE(f2);   // +17
    w.Finalize(0x67, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 12 = 21
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp114(NetClient& nc, int8_t f0, int8_t f1, int8_t f2) {
    // [net] opcode 114 (0x72) : 3 byte fields
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.WriteChar4LE(f2);   // +17
    w.Finalize(0x72, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 12 = 21
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp126(NetClient& nc, int8_t value) {
    // [net] opcode 126 (0x7E) : 1 byte field
    PacketWriter w;
    w.WriteChar4LE(value);   // +9
    w.Finalize(0x7E, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 4 = 13
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp135(NetClient& nc, int8_t f0, int8_t f1, int8_t f2, int8_t f3, int8_t f4, int8_t f5, int8_t f6, int8_t f7, int8_t f8) {
    // [net] opcode 135 (0x87, -121 signed) : 9 consecutive byte fields
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.WriteChar4LE(f2);   // +17
    w.WriteChar4LE(f3);   // +21
    w.WriteChar4LE(f4);   // +25
    w.WriteChar4LE(f5);   // +29
    w.WriteChar4LE(f6);   // +33
    w.WriteChar4LE(f7);   // +37
    w.WriteChar4LE(f8);   // +41
    w.Finalize(0x87, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 36 = 45
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp40(NetClient& nc, const void* payload61) {
    // [net] opcode 0x28 : raw buffer of 61 bytes.
    PacketWriter w;
    w.WriteBytes(payload61, 61);  // this+9 : 61 bytes
    w.Finalize(0x28, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp48(NetClient& nc) {
    // [net] opcode 0x30 : no payload (wire length = 9).
    PacketWriter w;
    w.Finalize(0x30, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp56(NetClient& nc, int8_t arg0) {
    // [net] opcode 0x38 : one byte field (4 bytes LE).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.Finalize(0x38, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp64(NetClient& nc) {
    // [net] opcode 0x40 : no payload (wire length = 9).
    PacketWriter w;
    w.Finalize(0x40, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp72(NetClient& nc, const void* payload13) {
    // [net] opcode 0x48 : raw buffer of 13 bytes.
    PacketWriter w;
    w.WriteBytes(payload13, 13);  // this+9 : 13 bytes
    w.Finalize(0x48, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp80(NetClient& nc, const void* chatMsg61) {
    // [net] opcode 0x50 : normal/local chat, 61-byte buffer.
    PacketWriter w;
    w.WriteBytes(chatMsg61, 61);  // this+9 : 61 bytes (message)
    w.Finalize(0x50, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp88(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7, int8_t arg8) {
    // [net] opcode 0x58 : nine byte fields (4 bytes LE each).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.WriteChar4LE(arg1);  // this+13
    w.WriteChar4LE(arg2);  // this+17
    w.WriteChar4LE(arg3);  // this+21
    w.WriteChar4LE(arg4);  // this+25
    w.WriteChar4LE(arg5);  // this+29
    w.WriteChar4LE(arg6);  // this+33
    w.WriteChar4LE(arg7);  // this+37
    w.WriteChar4LE(arg8);  // this+41
    w.Finalize(0x58, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp97(NetClient& nc, int8_t arg0) {
    // [net] opcode 0x61 : one byte field (4 bytes LE).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.Finalize(0x61, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp104(NetClient& nc, int8_t arg0, int8_t arg1) {
    // [net] opcode 0x68 : two byte fields (4 bytes LE each).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.WriteChar4LE(arg1);  // this+13
    w.Finalize(0x68, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp115(NetClient& nc, int8_t arg0) {
    // [net] opcode 0x73 : one byte field (4 bytes LE).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.Finalize(0x73, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp127(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3) {
    // [net] opcode 0x7F : four byte fields (4 bytes LE each).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.WriteChar4LE(arg1);  // this+13
    w.WriteChar4LE(arg2);  // this+17
    w.WriteChar4LE(arg3);  // this+21
    w.Finalize(0x7F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp137(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3) {
    // [net] opcode 0x89 (137). The byte -119 is written at this+8 as 0x89 unsigned.
    // Four byte fields (4 bytes LE each).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.WriteChar4LE(arg1);  // this+13
    w.WriteChar4LE(arg2);  // this+17
    w.WriteChar4LE(arg3);  // this+21
    w.Finalize(0x89, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

} // namespace ts2::net

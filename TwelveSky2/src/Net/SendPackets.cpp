// Net/SendPackets.cpp — outbound builder definitions: Net_SendPacket_Op* family,
// Net_SendMenu_Var, Net_SendUdpReport, and the warp/auto-hunt business aliases.
// (Net_SendOp*/Net_SendVaultReq_*/Net_SendCmd_*/Net_SendGuarded_*/Net_SendMenu_N families
// live in SendPackets_Op.cpp / SendPackets_Vault.cpp / SendPackets_Cmd.cpp.)
#include "Net/SendPackets.h"
#include <cstring>   // std::memcpy (Net_SendCmd_251, Net_SendUdpReport)

namespace ts2::net {

bool Net_SendPacket_Op12(NetClient& nc, const void* head128, const void* name13, const void* tail72) {
    // Net_SendPacket_Op12 0x4B43C0 — opcode 12 (0x0C): 3 raw blocks (128 + 13 + 72).
    //   *(this+8)=12                  EA 0x4b444e
    //   Crt_Memcpy(this+9,   a2, 80h) EA 0x4b4462  -> 128-byte block
    //   Crt_Memcpy(this+137, a3, 0Dh) EA 0x4b447a  -> 13-byte block
    //   Crt_Memcpy(this+150, a4, 48h) EA 0x4b4492  -> 72-byte block
    //   *(this+15000)=222             EA 0x4b449d  -> wire length 9+213
    // RETURN: the binary returns 1 after a full send() (EA 0x4b4564) and 0 if send()
    // fails with an error != 10035/WSAEWOULDBLOCK, after Net_CloseSocket (EA 0x4b4531/
    // 0x4b4538); 10035 is NOT a failure (the binary loops back on the fragment,
    // EA 0x4b452a). NetSend() reproduces this semantics exactly -> we propagate it.
    // The caller Scene_EnterWorldUpdate 0x52BFF0 depends on this: `if (result)` EA 0x52c194
    // -> state 3 (+ g_GuardAuthTokenPending=0 EA 0x52c1ca); otherwise notice 67 (EA 0x52c1a2)
    // + state 4 (EA 0x52c1b7). Caller side = Scene/SceneManager.cpp (outside this front).
    PacketWriter w;
    w.WriteBytes(head128, 128); // this+9  : 128-byte block
    w.WriteBytes(name13, 13);   // this+137 : 13-byte block
    w.WriteBytes(tail72, 72);   // this+150 : 72-byte block
    w.Finalize(0x0C, DefaultRng(), nc.xorKey, nc.seq);
    return NetSend(nc, w.Data(), (int)w.Size()); // 1 = EA 0x4b4564 / 0 = EA 0x4b4538
}

void Net_SendPacket_Op20(NetClient& nc, int8_t arg1, int8_t arg2) {
    // opcode 20 (0x14) : 2 byte fields (each sent as 4 bytes LE)
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.WriteChar4LE(arg2); // this+13
    w.Finalize(0x14, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

// --- Business alias: warp / teleport --------------------------------------
void Net_SendWarpRequest(NetClient& nc, int32_t warpModeCode, int32_t targetZoneId) {
    // Net_SendPacket_Op20 0x4B5000 — byte-faithful i32 variant (zoneId zero-extended).
    // Byte-for-byte reproduction of the call Op20(&g_AutoPlayMgr, dword_1675A8C, v3[a1])
    // where v3[a1] (138/139/165/166) is pushed as 32 bits ZERO-extended (EA 0x5f5dd6).
    PacketWriter w;
    w.WriteI32(warpModeCode);  // this+9  : Crt_Memcpy(this+9,&a2,4)  EA 0x4b509f (a2=int)
    w.WriteI32(targetZoneId);  // this+13 : Crt_Memcpy(this+13,&a3,4) EA 0x4b50b4 (v3[a1] pushed as 32 bits)
    w.Finalize(0x14, DefaultRng(), nc.xorKey, nc.seq); // *(this+15000)=17 EA 0x4b50bf
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendAutoHuntSync(NetClient& nc, int8_t stateFlag,
                          const void* appearance68, const void* autoHunt44) {
    // Net_SendOp99 0x4BD140 (opcode 0x63) — semantic alias (68-byte appearance + 44-byte auto-hunt).
    Net_SendOp99(nc, stateFlag, appearance68, autoHunt44);
}

void Net_SendPacket_Op28(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4) {
    // opcode 28 (0x1C) : 4 byte fields (each sent as 4 bytes LE)
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.WriteChar4LE(arg2); // this+13
    w.WriteChar4LE(arg3); // this+17
    w.WriteChar4LE(arg4); // this+21
    w.Finalize(0x1C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op108(NetClient& nc, int8_t arg1, int8_t arg2, const void* name13) {
    // opcode 108 (0x6C) : 2 byte fields + string/13-byte block
    PacketWriter w;
    w.WriteChar4LE(arg1);      // this+9
    w.WriteChar4LE(arg2);      // this+13
    w.WriteBytes(name13, 13);  // this+17
    w.Finalize(0x6C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendMenu_Var(NetClient& nc, uint8_t opcode) {
    // opcode supplied by the caller (GM menu), empty payload
    // guard: blocked while a morph is in progress or if the anti-spam latch is already armed
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.Finalize(opcode, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendPacket_Op13(NetClient& nc, int8_t arg) {
    // [net] opcode 0x0D : a single byte field (this+9)
    PacketWriter w;
    w.WriteChar4LE(arg);            // this+9 : char sent as 4 bytes LE
    w.Finalize(0x0D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op21(NetClient& nc) {
    // [net] opcode 0x15 : empty payload (9 header bytes only)
    PacketWriter w;
    w.Finalize(0x15, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op29(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7, int8_t a8, int8_t a9) {
    // [net] opcode 0x1D : 9 consecutive byte fields (this+9,+13,...+41)
    PacketWriter w;
    w.WriteChar4LE(a1);            // this+9
    w.WriteChar4LE(a2);            // this+13
    w.WriteChar4LE(a3);            // this+17
    w.WriteChar4LE(a4);            // this+21
    w.WriteChar4LE(a5);            // this+25
    w.WriteChar4LE(a6);            // this+29
    w.WriteChar4LE(a7);            // this+33
    w.WriteChar4LE(a8);            // this+37
    w.WriteChar4LE(a9);            // this+41
    w.Finalize(0x1D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op109(NetClient& nc, int8_t flag, const void* name13, int8_t b1, int8_t b2, int8_t b3, int8_t b4, int8_t b5, int8_t b6, int8_t b7, const void* blob28) {
    // [net] opcode 0x6D: 13-byte name + 8 byte fields + 28-byte blob (order = offsets)
    // Write order by increasing offset: name13@9, b1..b7@22.., flag@50, blob28@54.
    PacketWriter w;
    w.WriteBytes(name13, 13);      // this+9  : name string 13 bytes
    w.WriteChar4LE(b1);            // this+22
    w.WriteChar4LE(b2);            // this+26
    w.WriteChar4LE(b3);            // this+30
    w.WriteChar4LE(b4);            // this+34
    w.WriteChar4LE(b5);            // this+38
    w.WriteChar4LE(b6);            // this+42
    w.WriteChar4LE(b7);            // this+46
    w.WriteChar4LE(flag);          // this+50 : 1st parameter (a2)
    w.WriteBytes(blob28, 28);      // this+54 : 28-byte blob
    w.Finalize(0x6D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

bool Net_SendUdpReport(uint8_t* self, const char* cp) {
    // [net] Sends a UDP report (datagram) to host 'cp'.
    // Separate path from the encrypted TCP protocol: dedicated UDP socket, no
    // XOR or PacketWriter header. Serves the anti-cheat/telemetry subsystem.
    WSADATA wsa;
    if (WSAStartup(0x0202, &wsa))
        return false;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);   // socket(2, 2, 0)
    if (sock == INVALID_SOCKET)
        return false;

    sockaddr to;
    memset(&to, 0, sizeof(to));
    to.sa_family = AF_INET;
    // Net_SendUdpReport 0x6D8173: port = byte_1860E1C ? ntohs(Crt_Atoi(&byte_1860E1C))
    //   : ntohs(0x3A98) [15000]. ASSUMED GAP: the ASCII config byte_1860E1C is NOT
    //   re-read here (hardcoded constant 15000) — function of the structural anticheat
    //   band [0x6D7234, 0x6FD04C), excluded per project policy (CLAUDE.md).
    u_short port = ntohs((u_short)0x3A98);
    *reinterpret_cast<uint16_t*>(to.sa_data) = port;
    *reinterpret_cast<uint32_t*>(&to.sa_data[2]) = inet_addr(cp);
    if (*reinterpret_cast<uint32_t*>(&to.sa_data[2]) == INADDR_NONE) {
        // 'cp' is not a literal IP: DNS resolution.
        hostent* he = gethostbyname(cp);
        if (!he) { closesocket(sock); return false; }
        memcpy(&to.sa_data[2], he->h_addr_list[0], he->h_length);
    }

    // TODO (anti-cheat/telemetry): the report serializer sub_6E323E (0x6E323E)
    // and the state (self+6340) are not reimplemented — path outside the game protocol.
    (void)self; (void)cp;
    bool ok = false;
    closesocket(sock);
    return ok;
}

void Net_SendPacket_Op14(NetClient& nc, int8_t value) {
    // [net] opcode 0x0E : 1 byte field (promoted to 4 bytes LE). Total length 13.
    PacketWriter w;
    w.WriteChar4LE(value);   // this+9
    w.Finalize(0x0E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op22(NetClient& nc, int8_t arg0, int8_t arg1) {
    // [net] opcode 0x16 : 2 byte fields (each promoted to 4 B LE). Length 17.
    PacketWriter w;
    w.WriteChar4LE(arg0);   // this+9
    w.WriteChar4LE(arg1);   // this+13
    w.Finalize(0x16, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op30(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7, int8_t arg8) {
    // [net] opcode 0x1E : 9 byte fields (each promoted to 4 B LE). Length 45.
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
    w.Finalize(0x1E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op15(NetClient& nc, const void* data72) {
    // [net] opcode 0x0F (15) — raw payload of 72 bytes (total packet 81)
    PacketWriter w;
    w.WriteBytes(data72, 72);                       // block copied to this+9
    w.Finalize(0x0F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op23(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3) {
    // [net] opcode 0x17 (23) — 3 byte fields (each promoted to 4 bytes LE), total 21
    PacketWriter w;
    w.WriteChar4LE(arg1);                           // this+9
    w.WriteChar4LE(arg2);                           // this+13
    w.WriteChar4LE(arg3);                           // this+17
    w.Finalize(0x17, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op31(NetClient& nc, int8_t kind, const void* data1232) {
    // [net] opcode 0x1F (31) — 1 byte + 1232-byte block, total 1245
    PacketWriter w;
    w.WriteChar4LE(kind);                           // this+9
    w.WriteBytes(data1232, 1232);                   // this+13
    w.Finalize(0x1F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op16(NetClient& nc, const void* payload) {
    // opcode 0x10 (16) — raw payload of 72 bytes (this+9, total len 81)
    PacketWriter w;
    w.WriteBytes(payload, 72);   // 72-byte buffer, copied
    w.Finalize(0x10, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op24(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d) {
    // opcode 0x18 (24) — 4 char fields, each sent as 4 bytes LE (len 25)
    PacketWriter w;
    w.WriteChar4LE(a);   // this+9
    w.WriteChar4LE(b);   // this+13
    w.WriteChar4LE(c);   // this+17
    w.WriteChar4LE(d);   // this+21
    w.Finalize(0x18, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op32(NetClient& nc, int8_t a) {
    // opcode 0x20 (32) — 1 char field sent as 4 bytes LE (len 13)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.Finalize(0x20, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op17(NetClient& nc, const void* payload61) {
    // [net] opcode 0x11 : 61-byte block copied as-is to this+9
    PacketWriter w;
    w.WriteBytes(payload61, 61);
    w.Finalize(0x11, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op25(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d) {
    // [net] opcode 0x19 : four byte fields (each sent as 4 bytes LE)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.WriteChar4LE(c);
    w.WriteChar4LE(d);
    w.Finalize(0x19, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op33(NetClient& nc, const void* text13) {
    // [net] opcode 0x21 : string/13-byte block
    PacketWriter w;
    w.WriteBytes(text13, 13);
    w.Finalize(0x21, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op18(NetClient& nc, const void* payload76) {
    // [net] opcode 18 (0x12) : opaque block of 76 bytes
    PacketWriter w;
    w.WriteBytes(payload76, 76);                        // this+9 .. +84
    w.Finalize(0x12, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 76 = 85
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op26(NetClient& nc, int32_t f0, int32_t f1, int32_t f2, int32_t f3, int32_t f4) {
    // Net_SendPacket_Op26 0x4B59C0: opcode 26 (0x1A), 5 int32 LE fields, length 29.
    //   *(this+8)=26 (EA 0x4b5a4e); Crt_Memcpy(this+9,&a2,4u)  EA 0x4b5a5f
    //   Crt_Memcpy(this+13,&a3,4u) EA 0x4b5a74; Crt_Memcpy(this+17,&a4,4u) EA 0x4b5a89
    //   Crt_Memcpy(this+21,&a5,4u) EA 0x4b5a9e; Crt_Memcpy(this+25,&a6,4u) EA 0x4b5ab3
    //   *(this+15000)=29 (EA 0x4b5abe).
    // The 5 slots are memcpy'd as 4 BYTES: they are int32, not char.
    // The Hex-Rays prototype's `char a6` is a sizing artifact of the LAST
    // __stdcall argument (nothing bounds its stack slot size): the callers
    // push full DWORDs there — UI_MsgBox_OnLButtonUp 0x5C0A90 @0x5c25d0
    // passes dword_1839288/dword_1822EF4, UI_Enchant_Click 0x5FBA20 @0x5fbf46 passes
    // *(this+22768)/*(this+11). Hence int32_t + WriteI32 for the 5 fields.
    PacketWriter w;
    w.WriteI32(f0);   // +9
    w.WriteI32(f1);   // +13
    w.WriteI32(f2);   // +17
    w.WriteI32(f3);   // +21
    w.WriteI32(f4);   // +25
    w.Finalize(0x1A, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 20 = 29
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op34(NetClient& nc, int8_t f0, int8_t f1) {
    // [net] opcode 34 (0x22) : 2 byte fields
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.Finalize(0x22, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 8 = 17
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op19(NetClient& nc, uint32_t subCmd, const void* payload) {
    // Net_SendPacket_Op19 0x4B4E70 — sole builder of channel 0x13 (vault/command).
    // IDA signature: int __thiscall Net_SendPacket_Op19(unsigned int this, int a2, _BYTE *a3).
    //   *(this+8) = 19               EA 0x4b4efe  -> wire opcode 0x13
    //   Crt_Memcpy(this+9, &a2, 4u)  EA 0x4b4f0f  -> sub-code INT32 LE @+9 (a2 = int)
    //   Crt_Memcpy(this+13, a3, 64h) EA 0x4b4f24  -> 100-byte block @+13
    //   *(this+15000) = 113          EA 0x4b4f2f  -> wire length 9+4+100
    // subCmd is a uint32_t, NOT a uint8_t: the 88 calling wrappers cover
    // 201..256 AND 501..531/534 (verified via xrefs_to 0x4B4E70, 1 site each, all with a
    // constant immediate `mov dword ptr [ebp+var_74], imm32` ZERO-extended). A uint8_t
    // would truncate mod 256 (501->245, 534->22, 256->0) — this is the historical origin of
    // the misleading IDA naming: the suffix of the Net_SendCmd_*/Net_SendVaultReq_* names is
    // (sub-code & 0xFF), NOT the sub-code. NEVER infer the sub-code from a name.
    PacketWriter w;
    w.WriteU32(subCmd);          // this+9  : sub-code (u32 LE, zero-extended) EA 0x4b4f0f
    w.WriteBytes(payload, 100);  // this+13 : 100-byte block                   EA 0x4b4f24
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq); // opcode 0x13, length 113
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op27(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3) {
    // [net] opcode 0x1B : four byte fields (each sent as 4 bytes LE).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.WriteChar4LE(arg1);  // this+13
    w.WriteChar4LE(arg2);  // this+17
    w.WriteChar4LE(arg3);  // this+21
    w.Finalize(0x1B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op35(NetClient& nc, int8_t flag, const void* name13, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // [net] opcode 0x23 : byte + string/13-byte buffer + 7 byte fields (4 bytes LE each).
    PacketWriter w;
    w.WriteChar4LE(flag);      // this+9
    w.WriteBytes(name13, 13);  // this+13 : 13 bytes
    w.WriteChar4LE(arg0);      // this+26
    w.WriteChar4LE(arg1);      // this+30
    w.WriteChar4LE(arg2);      // this+34
    w.WriteChar4LE(arg3);      // this+38
    w.WriteChar4LE(arg4);      // this+42
    w.WriteChar4LE(arg5);      // this+46
    w.WriteChar4LE(arg6);      // this+50
    w.Finalize(0x23, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

} // namespace ts2::net

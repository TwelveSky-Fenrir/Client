// Net/SendPackets_Cmd.cpp — outbound builder definitions: Net_SendCmd_*, Net_SendGuarded_*
// and the guarded Net_SendMenu_1/2/3 wrappers (same anti-spam-latch family as
// Net_SendGuarded_*; split from SendPackets.cpp; see SendPackets.h for the shared declarations).
#include "Net/SendPackets.h"
#include <cstring>   // std::memcpy (Net_SendCmd_251, Net_SendUdpReport)

namespace ts2::net {

void Net_SendCmd_247(NetClient& nc, int32_t arg1, int32_t arg2) {
    // Net_SendCmd_247 0x592720 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x59276b): wire opcode 0x13,
    //   sub-code 503 = mov dword ptr [ebp+var_74], 1F7h (EA 0x592733) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (503 & 0xFF = 247) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { arg1, arg2 };
    std::memcpy(block, f, sizeof(f));  // block+0..7: Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 503, block);   // EA 0x59276b -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_255(NetClient& nc) {
    // Net_SendCmd_255 0x5929A0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5929c7): wire opcode 0x13,
    //   sub-code 511 = mov dword ptr [ebp+var_74], 1FFh (EA 0x5929b3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (511 & 0xFF = 255) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    // 100-byte block NOT initialized on the binary side (stack) -> zeroed here: we do not
    //   reproduce the stack leak.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 511, block);   // EA 0x5929c7 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_7(NetClient& nc, const void* name13) {
    // Net_SendCmd_7 0x592C00 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592c39): wire opcode 0x13,
    //   sub-code 519 = mov dword ptr [ebp+var_74], 207h (EA 0x592c13) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (519 & 0xFF = 7) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, name13, 13);      // block+0..12: Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 519, block);   // EA 0x592c39 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_15(NetClient& nc, int32_t arg1) {
    // Net_SendCmd_15 0x592EA0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592ed9): wire opcode 0x13,
    //   sub-code 527 = mov dword ptr [ebp+var_74], 20Fh (EA 0x592eb3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (527 & 0xFF = 15) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 527, block);   // EA 0x592ed9 -> frame 9+4+100 = 113 bytes
}

void Net_SendGuarded_3(NetClient& nc) {
    // opcode 3 (0x03): guarded GM request, empty payload
    // guard: blocked while a morph is in progress or if the anti-spam latch is already armed
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.Finalize(0x03, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendGuarded_11(NetClient& nc) {
    // opcode 11 (0x0B): guarded GM request, empty payload
    // guard: blocked while a morph is in progress or if the anti-spam latch is already armed
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.Finalize(0x0B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendCmd_248(NetClient& nc, int32_t a1) {
    // Net_SendCmd_248 0x592780 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5927b9): wire opcode 0x13,
    //   sub-code 504 = mov dword ptr [ebp+var_74], 1F8h (EA 0x592793) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (504 & 0xFF = 248) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { a1 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 504, block);   // EA 0x5927b9 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_0(NetClient& nc) {
    // Net_SendCmd_0 0x5929E0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592a07): wire opcode 0x13,
    //   sub-code 512 = mov dword ptr [ebp+var_74], 200h (EA 0x5929f3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (512 & 0xFF = 0) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    // 100-byte block NOT initialized on the binary side (stack) -> zeroed here: we do not
    //   reproduce the stack leak.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 512, block);   // EA 0x592a07 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_8(NetClient& nc, int32_t a1, int32_t a2) {
    // Net_SendCmd_8 0x592C50 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592c9b): wire opcode 0x13,
    //   sub-code 520 = mov dword ptr [ebp+var_74], 208h (EA 0x592c63) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (520 & 0xFF = 8) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { a1, a2 };
    std::memcpy(block, f, sizeof(f));  // block+0..7: Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 520, block);   // EA 0x592c9b -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_16(NetClient& nc, const void* buf12) {
    // Net_SendCmd_16 0x592EF0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592f29): wire opcode 0x13,
    //   sub-code 528 = mov dword ptr [ebp+var_74], 210h (EA 0x592f03) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (528 & 0xFF = 16) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, buf12, 12);       // block+0..11: Crt_Memcpy(v2,a1,0xCu)
    Net_SendPacket_Op19(nc, 528, block);   // EA 0x592f29 -> frame 9+4+100 = 113 bytes
}

void Net_SendGuarded_4(NetClient& nc) {
    // [net] wraps Net_SendOp75 -> opcode 0x4B, sub-command 4.
    // Guard: nothing is sent if a morph is in progress or the GM cooldown
    // latch is already armed. Otherwise: send + arm the latch + timestamp.
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.WriteChar4LE((int8_t)4);     // this+9 : sub-command
    uint8_t block[500] = {0};      // fixed 500-byte block (no field)
    w.WriteBytes(block, sizeof(block));
    w.Finalize(0x4B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;   // timestamp of the last guarded send
}

void Net_SendGuarded_12(NetClient& nc) {
    // [net] wraps Net_SendOp75 -> opcode 0x4B, sub-command 12.
    // Same guard as Net_SendGuarded_4 (morph in progress / GM cooldown latch).
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.WriteChar4LE((int8_t)12);    // this+9 : sub-command
    uint8_t block[500] = {0};      // fixed 500-byte block (no field)
    w.WriteBytes(block, sizeof(block));
    w.Finalize(0x4B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;   // timestamp of the last guarded send
}

void Net_SendCmd_249(NetClient& nc, int32_t arg0) {
    // Net_SendCmd_249 0x5927D0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592809): wire opcode 0x13,
    //   sub-code 505 = mov dword ptr [ebp+var_74], 1F9h (EA 0x5927e3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (505 & 0xFF = 249) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg0 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 505, block);   // EA 0x592809 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_1(NetClient& nc, const void* payload13) {
    // Net_SendCmd_1 0x592A20 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592a59): wire opcode 0x13,
    //   sub-code 513 = mov dword ptr [ebp+var_74], 201h (EA 0x592a33) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (513 & 0xFF = 1) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, payload13, 13);   // block+0..12: Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 513, block);   // EA 0x592a59 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_9(NetClient& nc, int32_t arg0) {
    // Net_SendCmd_9 0x592CB0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592ce9): wire opcode 0x13,
    //   sub-code 521 = mov dword ptr [ebp+var_74], 209h (EA 0x592cc3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (521 & 0xFF = 9) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg0 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 521, block);   // EA 0x592ce9 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_17(NetClient& nc, int32_t arg0) {
    // Net_SendCmd_17 0x592F40 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592f79): wire opcode 0x13,
    //   sub-code 529 = mov dword ptr [ebp+var_74], 211h (EA 0x592f53) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (529 & 0xFF = 17) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg0 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 529, block);   // EA 0x592f79 -> frame 9+4+100 = 113 bytes
}

void Net_SendGuarded_5(NetClient& nc, const void* payload204) {
    // [SHELL] Guild command sub-op 5 via Net_SendOp75 (wire opcode 0x4B).
    // Guard: blocked while a morph is in progress or if the GM cooldown is already armed.
    // External game state (to be rewired to the real members):
    extern int   g_MorphInProgress;
    extern int   g_GmCmdCooldownLatch;
    extern float flt_1675B0C;      // GM cooldown timestamp
    extern float g_GameTimeSec;    // current game time (s)
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    uint8_t buf[500] = {0};              // Op75 payload block (500 B)
    std::memcpy(buf, payload204, 204);   // 204 useful bytes copied at the front
    Net_SendOp75(nc, 5, buf);            // kind=5, wire opcode 0x4B
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendGuarded_13(NetClient& nc) {
    // [SHELL] Guild command sub-op 13 via Net_SendOp75 (wire opcode 0x4B), no argument.
    // Guard: blocked while a morph is in progress or if the GM cooldown is already armed.
    // External game state (to be rewired to the real members):
    extern int   g_MorphInProgress;
    extern int   g_GmCmdCooldownLatch;
    extern float flt_1675B0C;      // GM cooldown timestamp
    extern float g_GameTimeSec;    // current game time (s)
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    uint8_t buf[500] = {0};        // block not initialized in the original -> null here
    Net_SendOp75(nc, 13, buf);     // kind=13, wire opcode 0x4B
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendCmd_250(NetClient& nc, int32_t arg1) {
    // Net_SendCmd_250 0x592820 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592859): wire opcode 0x13,
    //   sub-code 506 = mov dword ptr [ebp+var_74], 1FAh (EA 0x592833) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (506 & 0xFF = 250) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 506, block);   // EA 0x592859 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_2(NetClient& nc, const void* data13) {
    // Net_SendCmd_2 0x592A70 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592aa9): wire opcode 0x13,
    //   sub-code 514 = mov dword ptr [ebp+var_74], 202h (EA 0x592a83) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (514 & 0xFF = 2) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, data13, 13);      // block+0..12: Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 514, block);   // EA 0x592aa9 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_10(NetClient& nc, int32_t arg1, int32_t arg2) {
    // Net_SendCmd_10 0x592D00 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592d4b): wire opcode 0x13,
    //   sub-code 522 = mov dword ptr [ebp+var_74], 20Ah (EA 0x592d13) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (522 & 0xFF = 10) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { arg1, arg2 };
    std::memcpy(block, f, sizeof(f));  // block+0..7: Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 522, block);   // EA 0x592d4b -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_18(NetClient& nc, int32_t arg1) {
    // Net_SendCmd_18 0x592F90 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592fc9): wire opcode 0x13,
    //   sub-code 530 = mov dword ptr [ebp+var_74], 212h (EA 0x592fa3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (530 & 0xFF = 18) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 530, block);   // EA 0x592fc9 -> frame 9+4+100 = 113 bytes
}

void Net_SendGuarded_6(NetClient& nc) {
    // [net] guarded command: opcode 0x4B (75) via Net_SendOp75, sub-op 6; no arguments.
    // Op75 emits [sub-op:4][fixed block 500] -> block zero-padded.
    // Anti-spam guard: ignored if a morph is in progress or the cooldown latch is armed.
    if (g_MorphInProgress != 1 && !g_GmCmdCooldownLatch) {
        PacketWriter w;
        w.WriteU32(6);                              // sub-opcode
        for (int k = 0; k < 500; ++k) w.WriteU8(0); // 500-byte block
        w.Finalize(0x4B, DefaultRng(), nc.xorKey, nc.seq);
        NetSend(nc, w.Data(), (int)w.Size());
        g_GmCmdCooldownLatch = 1;                   // arm the latch
        flt_1675B0C = g_GameTimeSec;                // cooldown timestamp
    }
}

void Net_SendGuarded_14(NetClient& nc, int8_t arg1) {
    // [net] guarded guild command: opcode 0x4B (75) via Net_SendOp75, sub-op 14; 1 byte field.
    // Anti-spam guard identical to Net_SendGuarded_6.
    if (g_MorphInProgress != 1 && !g_GmCmdCooldownLatch) {
        PacketWriter w;
        w.WriteU32(14);                             // sub-opcode
        w.WriteChar4LE(arg1);                       // block[0..3]
        for (int k = 0; k < 496; ++k) w.WriteU8(0); // padding block 500
        w.Finalize(0x4B, DefaultRng(), nc.xorKey, nc.seq);
        NetSend(nc, w.Data(), (int)w.Size());
        g_GmCmdCooldownLatch = 1;                   // arm the latch
        flt_1675B0C = g_GameTimeSec;                // cooldown timestamp
    }
}

void Net_SendCmd_251(NetClient& nc, const void* data) {
    // Net_SendCmd_251 0x592870 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5928a9): wire opcode 0x13,
    //   sub-code 507 = mov dword ptr [ebp+var_74], 1FBh (EA 0x592883) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (507 & 0xFF = 251) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, data, 12);        // block+0..11: Crt_Memcpy(v2,a1,0xCu)
    Net_SendPacket_Op19(nc, 507, block);   // EA 0x5928a9 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_3(NetClient& nc, const void* data) {
    // Net_SendCmd_3 0x592AC0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592af9): wire opcode 0x13,
    //   sub-code 515 = mov dword ptr [ebp+var_74], 203h (EA 0x592ad3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (515 & 0xFF = 3) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, data, 13);        // block+0..12: Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 515, block);   // EA 0x592af9 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_11(NetClient& nc, int32_t a, int32_t b) {
    // Net_SendCmd_11 0x592D60 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592dab): wire opcode 0x13,
    //   sub-code 523 = mov dword ptr [ebp+var_74], 20Bh (EA 0x592d73) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (523 & 0xFF = 11) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { a, b };
    std::memcpy(block, f, sizeof(f));  // block+0..7: Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 523, block);   // EA 0x592dab -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_19(NetClient& nc, int32_t a) {
    // Net_SendCmd_19 0x592FE0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x593019): wire opcode 0x13,
    //   sub-code 531 = mov dword ptr [ebp+var_74], 213h (EA 0x592ff3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (531 & 0xFF = 19) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { a };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 531, block);   // EA 0x593019 -> frame 9+4+100 = 113 bytes
}

void Net_SendGuarded_7(NetClient& nc) {
    // opcode 0x07 (7) — guarded guild rank-up request (anti-spam during morph)
    // game globals (external): g_MorphInProgress, g_GmCmdCooldownLatch, g_GameTimeSec, flt_1675B0C
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;   // empty payload (Op75 send variant in the original)
    w.Finalize(0x07, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendGuarded_17(NetClient& nc, const void* data1, const void* data2, int8_t flag) {
    // opcode 0x11 (17) — guarded: 13 bytes + 13 bytes + 1 char field (4 LE)
    // game globals (external): g_MorphInProgress, g_GmCmdCooldownLatch, g_GameTimeSec, flt_1675B0C
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.WriteBytes(data1, 13);   // v4
    w.WriteBytes(data2, 13);   // v5
    w.WriteChar4LE(flag);      // v6
    w.Finalize(0x11, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendCmd_252(NetClient& nc, int32_t a1) {
    // Net_SendCmd_252 0x5928C0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5928f9): wire opcode 0x13,
    //   sub-code 508 = mov dword ptr [ebp+var_74], 1FCh (EA 0x5928d3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (508 & 0xFF = 252) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { a1 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 508, block);   // EA 0x5928f9 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_4(NetClient& nc, const void* payload13) {
    // Net_SendCmd_4 0x592B10 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592b49): wire opcode 0x13,
    //   sub-code 516 = mov dword ptr [ebp+var_74], 204h (EA 0x592b23) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (516 & 0xFF = 4) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, payload13, 13);   // block+0..12: Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 516, block);   // EA 0x592b49 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_12(NetClient& nc, int32_t a1) {
    // Net_SendCmd_12 0x592DC0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592df9): wire opcode 0x13,
    //   sub-code 524 = mov dword ptr [ebp+var_74], 20Ch (EA 0x592dd3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (524 & 0xFF = 12) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { a1 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 524, block);   // EA 0x592df9 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_22(NetClient& nc, int32_t a1) {
    // Net_SendCmd_22 0x593030 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x593069): wire opcode 0x13,
    //   sub-code 534 = mov dword ptr [ebp+var_74], 216h (EA 0x593043) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (534 & 0xFF = 22) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { a1 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 534, block);   // EA 0x593069 -> frame 9+4+100 = 113 bytes
}

void Net_SendGuarded_8(NetClient& nc, const void* payload13) {
    // [net] network opcode 0x4B (Net_SendOp75); sub-command 8; fixed 500-byte block.
    // GM-command anti-spam gating: blocked while a morph is in progress or the cooldown latch is active.
    // g_MorphInProgress / g_GmCmdCooldownLatch / g_GameTimeSec / flt_1675B0C = game state (external).
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.WriteU32(8);
    w.WriteBytes(payload13, 13);
    for (int i = 0; i < 487; ++i) w.WriteU8(0);   // 500 - 13
    w.Finalize(0x4B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendMenu_1(NetClient& nc) {
    // [net] network opcode 0x4F (Net_SendOp79); sub-command 1; fixed 100-byte block (not initialized in the original -> 0).
    // Same GM-command anti-spam gating as Net_SendGuarded_8.
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.WriteU32(1);
    for (int i = 0; i < 100; ++i) w.WriteU8(0);   // empty 100-byte block
    w.Finalize(0x4F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendCmd_253(NetClient& nc) {
    // Net_SendCmd_253 0x592910 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592937): wire opcode 0x13,
    //   sub-code 509 = mov dword ptr [ebp+var_74], 1FDh (EA 0x592923) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (509 & 0xFF = 253) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    // 100-byte block NOT initialized on the binary side (stack) -> zeroed here: we do not
    //   reproduce the stack leak.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 509, block);   // EA 0x592937 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_5(NetClient& nc, const void* payload13) {
    // Net_SendCmd_5 0x592B60 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592b99): wire opcode 0x13,
    //   sub-code 517 = mov dword ptr [ebp+var_74], 205h (EA 0x592b73) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (517 & 0xFF = 5) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, payload13, 13);   // block+0..12: Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 517, block);   // EA 0x592b99 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_13(NetClient& nc, int32_t value) {
    // Net_SendCmd_13 0x592E10 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592e49): wire opcode 0x13,
    //   sub-code 525 = mov dword ptr [ebp+var_74], 20Dh (EA 0x592e23) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (525 & 0xFF = 13) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { value };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 525, block);   // EA 0x592e49 -> frame 9+4+100 = 113 bytes
}

void Net_SendGuarded_1(NetClient& nc, const void* payload13) {
    // Guarded wrapper: wire opcode 75 (0x4B), sub-type 1. Op75 copies a 500-byte block.
    // Payload = 13-byte block. GM-command anti-spam latch.
    uint8_t payload[508] = {};
    std::memcpy(payload, payload13, 13);
    // sends nothing if a morph is in progress or the GM cooldown is already armed
    if (g_MorphInProgress != 1 && !g_GmCmdCooldownLatch) {
        Net_SendOp75(nc, 1, payload);
        g_GmCmdCooldownLatch = 1;
        flt_1675B0C = g_GameTimeSec;
    }
}

void Net_SendGuarded_9(NetClient& nc, const void* payload13, int8_t value) {
    // Guarded wrapper: wire opcode 75 (0x4B), sub-type 9. Op75 copies a 500-byte block.
    // Payload = 13-byte block (+0) followed by a byte field (+13, NOT aligned, matching the original).
    uint8_t payload[508] = {};
    std::memcpy(payload, payload13, 13);                              // +0..12
    reinterpret_cast<int32_t*>(payload + 13)[0] = value;             // +13 (char promoted to 4 bytes LE)
    if (g_MorphInProgress != 1 && !g_GmCmdCooldownLatch) {
        Net_SendOp75(nc, 9, payload);
        g_GmCmdCooldownLatch = 1;
        flt_1675B0C = g_GameTimeSec;
    }
}

void Net_SendMenu_2(NetClient& nc, const void* payload13) {
    // Guarded wrapper: wire opcode 79 (0x4F), sub-type 2. Op79 copies a 100-byte block.
    // Payload = 13-byte block. Same anti-spam latch as the guarded commands.
    uint8_t payload[108] = {};
    std::memcpy(payload, payload13, 13);
    if (g_MorphInProgress != 1 && !g_GmCmdCooldownLatch) {
        Net_SendOp79(nc, 2, payload);
        g_GmCmdCooldownLatch = 1;
        flt_1675B0C = g_GameTimeSec;
    }
}

void Net_SendCmd_246(NetClient& nc) {
    // Net_SendCmd_246 0x5926E0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592707): wire opcode 0x13,
    //   sub-code 502 = mov dword ptr [ebp+var_74], 1F6h (EA 0x5926f3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (502 & 0xFF = 246) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    // 100-byte block NOT initialized on the binary side (stack) -> zeroed here: we do not
    //   reproduce the stack leak.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 502, block);   // EA 0x592707 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_254(NetClient& nc, int32_t arg0) {
    // Net_SendCmd_254 0x592950 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592989): wire opcode 0x13,
    //   sub-code 510 = mov dword ptr [ebp+var_74], 1FEh (EA 0x592963) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (510 & 0xFF = 254) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg0 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 510, block);   // EA 0x592989 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_6(NetClient& nc, const void* data13) {
    // Net_SendCmd_6 0x592BB0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592be9): wire opcode 0x13,
    //   sub-code 518 = mov dword ptr [ebp+var_74], 206h (EA 0x592bc3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (518 & 0xFF = 6) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, data13, 13);      // block+0..12: Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 518, block);   // EA 0x592be9 -> frame 9+4+100 = 113 bytes
}

void Net_SendCmd_14(NetClient& nc) {
    // Net_SendCmd_14 0x592E60 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592e87): wire opcode 0x13,
    //   sub-code 526 = mov dword ptr [ebp+var_74], 20Eh (EA 0x592e73) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (526 & 0xFF = 14) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    // 100-byte block NOT initialized on the binary side (stack) -> zeroed here: we do not
    //   reproduce the stack leak.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 526, block);   // EA 0x592e87 -> frame 9+4+100 = 113 bytes
}

void Net_SendGuarded_2(NetClient& nc, int32_t ctxId) {
    // [net] Guarded command sub-command 2 on the Op75 builder (channel 0x4B).
    // Op75/Op79 and the guard globals are declared elsewhere (shared game state):
    //   extern void Net_SendOp75(NetClient&, uint8_t subCmd, const void* payload);
    //   extern int g_MorphInProgress, g_GmCmdCooldownLatch, dword_1675B10;
    //   extern float flt_1675B0C, g_GameTimeSec;
    // Anti-spam guard: nothing sent if a morph is in progress or the GM cooldown is active.
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    uint8_t buf[508] = {0};        // no field written: payload zeroed
    Net_SendOp75(nc, 2, buf);      // sub-command 2
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;   // cooldown timestamp
    dword_1675B10 = ctxId;         // arg stored for later use (NOT transmitted)
}

void Net_SendGuarded_10(NetClient& nc, const void* data13, const void* data5) {
    // [net] Guarded command sub-command 10 on Op75 (channel 0x4B).
    // Payload = 13 bytes (data13) followed by 5 bytes (data5).
    // Faithful to the original: the buffers are built BEFORE the guard.
    uint8_t buf[508] = {0};
    std::memcpy(buf + 0, data13, 13);   // 13 bytes
    std::memcpy(buf + 13, data5, 5);    // next 5 bytes
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    Net_SendOp75(nc, 10, buf);          // sub-command 10
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;        // cooldown timestamp
}

void Net_SendMenu_3(NetClient& nc, const void* data13) {
    // [net] Guarded menu command sub-command 3 on Op79 (channel 0x4F).
    // Payload = 13 bytes. Faithful to the original: the buffer is built BEFORE the guard.
    //   extern void Net_SendOp79(NetClient&, uint8_t subCmd, const void* payload);
    uint8_t buf[108] = {0};
    std::memcpy(buf, data13, 13);       // 13 bytes
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    Net_SendOp79(nc, 3, buf);           // sub-command 3
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;        // cooldown timestamp
}

} // namespace ts2::net

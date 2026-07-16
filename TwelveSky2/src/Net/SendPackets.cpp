// Net/SendPackets.cpp — définitions des builders sortants (généré).
#include "Net/SendPackets.h"
#include <cstring>   // std::memcpy (Net_SendCmd_251, Net_SendUdpReport)

namespace ts2::net {

void Net_SendPacket_Op12(NetClient& nc, const void* head128, const void* name13, const void* tail72) {
    // opcode 12 (0x0C) : 3 blocs bruts concatenes (128 + 13 + 72 = 213 octets de payload)
    PacketWriter w;
    w.WriteBytes(head128, 128); // this+9  : bloc de 128 octets
    w.WriteBytes(name13, 13);   // this+137 : bloc de 13 octets
    w.WriteBytes(tail72, 72);   // this+150 : bloc de 72 octets
    w.Finalize(0x0C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op20(NetClient& nc, int8_t arg1, int8_t arg2) {
    // opcode 20 (0x14) : 2 champs octet (chacun emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.WriteChar4LE(arg2); // this+13
    w.Finalize(0x14, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

// --- Alias metier warp/teleportation -------------------------------------
void Net_SendWarpRequest(NetClient& nc, int32_t warpModeCode, int32_t targetZoneId) {
    // Net_SendPacket_Op20 0x4B5000 — variante fidele i32 (zoneId zero-etendu).
    // Reproduit a l'octet pres l'appel Op20(&g_AutoPlayMgr, dword_1675A8C, v3[a1])
    // ou v3[a1] (138/139/165/166) est pousse sur 32 bits ZERO-etendu (EA 0x5f5dd6).
    PacketWriter w;
    w.WriteI32(warpModeCode);  // this+9  : Crt_Memcpy(this+9,&a2,4)  EA 0x4b509f (a2=int)
    w.WriteI32(targetZoneId);  // this+13 : Crt_Memcpy(this+13,&a3,4) EA 0x4b50b4 (v3[a1] pousse 32b)
    w.Finalize(0x14, DefaultRng(), nc.xorKey, nc.seq); // *(this+15000)=17 EA 0x4b50bf
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendAutoHuntSync(NetClient& nc, int8_t stateFlag,
                          const void* appearance68, const void* autoHunt44) {
    // Net_SendOp99 0x4BD140 (opcode 0x63) — alias semantique (apparence 68 o + auto-hunt 44 o).
    Net_SendOp99(nc, stateFlag, appearance68, autoHunt44);
}

void Net_SendPacket_Op28(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4) {
    // opcode 28 (0x1C) : 4 champs octet (chacun emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.WriteChar4LE(arg2); // this+13
    w.WriteChar4LE(arg3); // this+17
    w.WriteChar4LE(arg4); // this+21
    w.Finalize(0x1C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op108(NetClient& nc, int8_t arg1, int8_t arg2, const void* name13) {
    // opcode 108 (0x6C) : 2 champs octet + chaine/bloc de 13 octets
    PacketWriter w;
    w.WriteChar4LE(arg1);      // this+9
    w.WriteChar4LE(arg2);      // this+13
    w.WriteBytes(name13, 13);  // this+17
    w.Finalize(0x6C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp41(NetClient& nc, int8_t arg1) {
    // opcode 41 (0x29) : 1 champ octet
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.Finalize(0x29, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp49(NetClient& nc, int8_t arg1) {
    // opcode 49 (0x31) : 1 champ octet
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.Finalize(0x31, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp57(NetClient& nc, int8_t arg1) {
    // opcode 57 (0x39) : 1 champ octet
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.Finalize(0x39, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp65(NetClient& nc, const void* name13) {
    // opcode 65 (0x41) : bloc/chaine de 13 octets
    PacketWriter w;
    w.WriteBytes(name13, 13); // this+9
    w.Finalize(0x41, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp73(NetClient& nc) {
    // opcode 73 (0x49) : payload vide
    PacketWriter w;
    w.Finalize(0x49, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp81(NetClient& nc, const void* buf61) {
    // opcode 81 (0x51) : bloc brut de 61 octets
    PacketWriter w;
    w.WriteBytes(buf61, 61); // this+9
    w.Finalize(0x51, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp89(NetClient& nc, int8_t arg1, int8_t arg2) {
    // opcode 89 (0x59) : 2 champs octet
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.WriteChar4LE(arg2); // this+13
    w.Finalize(0x59, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp98(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3) {
    // opcode 98 (0x62) : 3 champs octet
    PacketWriter w;
    w.WriteChar4LE(arg1); // this+9
    w.WriteChar4LE(arg2); // this+13
    w.WriteChar4LE(arg3); // this+17
    w.Finalize(0x62, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp105(NetClient& nc) {
    // opcode 105 (0x69) : payload vide
    PacketWriter w;
    w.Finalize(0x69, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp117(NetClient& nc) {
    // opcode 117 (0x75) : payload vide
    PacketWriter w;
    w.Finalize(0x75, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp128(NetClient& nc) {
    // opcode 128 (0x80) : payload vide
    PacketWriter w;
    w.Finalize(0x80, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp138(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7, int8_t arg8, int8_t arg9, int8_t arg10, int8_t arg11, int8_t arg12, int8_t arg13) {
    // opcode 138 (0x8A) : 13 champs octet (chacun emis sur 4 octets LE) = 52 octets de payload
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

void Net_SendVaultReq_203(NetClient& nc, int8_t arg1) {
    // opcode 203 (0xCB) : requete coffre, 1 champ octet
    PacketWriter w;
    w.WriteChar4LE(arg1);
    w.Finalize(0xCB, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_211(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7) {
    // opcode 211 (0xD3) : requete coffre, 7 champs octet (chacun emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(arg1);
    w.WriteChar4LE(arg2);
    w.WriteChar4LE(arg3);
    w.WriteChar4LE(arg4);
    w.WriteChar4LE(arg5);
    w.WriteChar4LE(arg6);
    w.WriteChar4LE(arg7);
    w.Finalize(0xD3, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_219(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7) {
    // opcode 219 (0xDB) : requete coffre, 7 champs octet (chacun emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(arg1);
    w.WriteChar4LE(arg2);
    w.WriteChar4LE(arg3);
    w.WriteChar4LE(arg4);
    w.WriteChar4LE(arg5);
    w.WriteChar4LE(arg6);
    w.WriteChar4LE(arg7);
    w.Finalize(0xDB, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_227(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7) {
    // opcode 227 (0xE3) : requete coffre, 7 champs octet (chacun emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(arg1);
    w.WriteChar4LE(arg2);
    w.WriteChar4LE(arg3);
    w.WriteChar4LE(arg4);
    w.WriteChar4LE(arg5);
    w.WriteChar4LE(arg6);
    w.WriteChar4LE(arg7);
    w.Finalize(0xE3, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_235(NetClient& nc, int8_t arg1) {
    // opcode 235 (0xEB) : requete coffre (gain points/monnaie), 1 champ octet
    PacketWriter w;
    w.WriteChar4LE(arg1);
    w.Finalize(0xEB, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_243(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7) {
    // opcode 243 (0xF3) : requete coffre, 7 champs octet (chacun emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(arg1);
    w.WriteChar4LE(arg2);
    w.WriteChar4LE(arg3);
    w.WriteChar4LE(arg4);
    w.WriteChar4LE(arg5);
    w.WriteChar4LE(arg6);
    w.WriteChar4LE(arg7);
    w.Finalize(0xF3, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_251(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7) {
    // opcode 251 (0xFB) : requete coffre, 7 champs octet (chacun emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(arg1);
    w.WriteChar4LE(arg2);
    w.WriteChar4LE(arg3);
    w.WriteChar4LE(arg4);
    w.WriteChar4LE(arg5);
    w.WriteChar4LE(arg6);
    w.WriteChar4LE(arg7);
    w.Finalize(0xFB, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_247(NetClient& nc, int8_t arg1, int8_t arg2) {
    // opcode 247 (0xF7) : requete action, 2 champs octet
    PacketWriter w;
    w.WriteChar4LE(arg1);
    w.WriteChar4LE(arg2);
    w.Finalize(0xF7, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_255(NetClient& nc) {
    // opcode 255 (0xFF) : requete action, payload vide (aucun champ initialise dans l'original)
    PacketWriter w;
    w.Finalize(0xFF, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_7(NetClient& nc, const void* name13) {
    // opcode 7 (0x07) : requete action, bloc/chaine de 13 octets
    PacketWriter w;
    w.WriteBytes(name13, 13);
    w.Finalize(0x07, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_15(NetClient& nc, int8_t arg1) {
    // opcode 15 (0x0F) : requete action, 1 champ octet
    PacketWriter w;
    w.WriteChar4LE(arg1);
    w.Finalize(0x0F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendGuarded_3(NetClient& nc) {
    // opcode 3 (0x03) : requete GM gardee, payload vide
    // garde : bloquee pendant une morph ou si le latch anti-spam est deja arme
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.Finalize(0x03, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendGuarded_11(NetClient& nc) {
    // opcode 11 (0x0B) : requete GM gardee, payload vide
    // garde : bloquee pendant une morph ou si le latch anti-spam est deja arme
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.Finalize(0x0B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendMenu_Var(NetClient& nc, uint8_t opcode) {
    // opcode fourni par l'appelant (menu GM), payload vide
    // garde : bloquee pendant une morph ou si le latch anti-spam est deja arme
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.Finalize(opcode, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendPacket_Op13(NetClient& nc, int8_t arg) {
    // [net] opcode 0x0D : un seul champ octet (this+9)
    PacketWriter w;
    w.WriteChar4LE(arg);            // this+9 : char emis sur 4 octets LE
    w.Finalize(0x0D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op21(NetClient& nc) {
    // [net] opcode 0x15 : payload vide (9 octets d'entete seuls)
    PacketWriter w;
    w.Finalize(0x15, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op29(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7, int8_t a8, int8_t a9) {
    // [net] opcode 0x1D : 9 champs octet consecutifs (this+9,+13,...+41)
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
    // [net] opcode 0x6D : nom 13 o. + 8 champs octet + blob 28 o. (ordre = offsets)
    // Ordre d'ecriture par offset croissant : name13@9, b1..b7@22.., flag@50, blob28@54.
    PacketWriter w;
    w.WriteBytes(name13, 13);      // this+9  : chaine/nom 13 octets
    w.WriteChar4LE(b1);            // this+22
    w.WriteChar4LE(b2);            // this+26
    w.WriteChar4LE(b3);            // this+30
    w.WriteChar4LE(b4);            // this+34
    w.WriteChar4LE(b5);            // this+38
    w.WriteChar4LE(b6);            // this+42
    w.WriteChar4LE(b7);            // this+46
    w.WriteChar4LE(flag);          // this+50 : 1er parametre (a2)
    w.WriteBytes(blob28, 28);      // this+54 : blob 28 octets
    w.Finalize(0x6D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp42(NetClient& nc, int8_t a2, int8_t a3, int8_t a4, const void* buf24, int8_t a6) {
    // [net] opcode 0x2A : 3 octets + tampon 24 o. + 1 octet
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteChar4LE(a3);            // this+13
    w.WriteChar4LE(a4);            // this+17
    w.WriteBytes(buf24, 24);       // this+21 : tampon 24 octets
    w.WriteChar4LE(a6);            // this+45
    w.Finalize(0x2A, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp50(NetClient& nc) {
    // [net] opcode 0x32 : payload vide
    PacketWriter w;
    w.Finalize(0x32, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp58(NetClient& nc, int8_t arg) {
    // [net] opcode 0x3A : un champ octet
    PacketWriter w;
    w.WriteChar4LE(arg);           // this+9
    w.Finalize(0x3A, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp66(NetClient& nc) {
    // [net] opcode 0x42 : payload vide
    PacketWriter w;
    w.Finalize(0x42, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp74(NetClient& nc, int8_t arg) {
    // [net] opcode 0x4A : un champ octet
    PacketWriter w;
    w.WriteChar4LE(arg);           // this+9
    w.Finalize(0x4A, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp82(NetClient& nc, int8_t a2, int8_t a3) {
    // [net] opcode 0x52 : deux champs octet
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteChar4LE(a3);            // this+13
    w.Finalize(0x52, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp90(NetClient& nc, int8_t a2, int8_t a3) {
    // [net] opcode 0x5A : deux champs octet
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteChar4LE(a3);            // this+13
    w.Finalize(0x5A, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp99(NetClient& nc, int8_t a2, const void* appearance68, const void* autoHunt44) {
    // [net] opcode 0x63 : 1 octet + blob apparence 68 o. (orig. unk_16755B0)
    //        + config auto-hunt 44 o. (orig. g_AutoHuntMode)
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteBytes(appearance68, 68);// this+13 : blob apparence 68 octets
    w.WriteBytes(autoHunt44, 44);  // this+81 : blob auto-hunt 44 octets
    w.Finalize(0x63, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp106(NetClient& nc, int8_t a2, const void* buf12) {
    // [net] opcode 0x6A : 1 octet + tampon 12 o.
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteBytes(buf12, 12);       // this+13 : tampon 12 octets
    w.Finalize(0x6A, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp116(NetClient& nc) {
    // [net] opcode 0x74 : payload vide
    PacketWriter w;
    w.Finalize(0x74, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp129(NetClient& nc, int8_t a2, int8_t a3) {
    // [net] opcode 0x81 (=129, encode -127 dans la decomp.) : deux champs octet
    PacketWriter w;
    w.WriteChar4LE(a2);            // this+9
    w.WriteChar4LE(a3);            // this+13
    w.Finalize(0x81, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp139(NetClient& nc, int8_t arg) {
    // [net] opcode 0x8B (=139, encode -117 dans la decomp.) : un champ octet
    PacketWriter w;
    w.WriteChar4LE(arg);           // this+9
    w.Finalize(0x8B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_204(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5) {
    // [net] enveloppe Net_SendPacket_Op19 -> opcode 0x13.
    // Payload = [sous-commande 204 (char4LE)] + bloc fixe de 100 octets.
    // Le bloc porte les champs en tete (chaque octet sur 4 o. LE), reste a zero.
    PacketWriter w;
    w.WriteChar4LE((int8_t)204);   // this+9  : sous-commande 0xCC
    w.WriteChar4LE(a1);            // bloc+0
    w.WriteChar4LE(a2);            // bloc+4
    w.WriteChar4LE(a3);            // bloc+8
    w.WriteChar4LE(a4);            // bloc+12
    w.WriteChar4LE(a5);            // bloc+16
    uint8_t pad[80] = {0};         // complement du bloc a 100 octets
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_212(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // [net] enveloppe Net_SendPacket_Op19 -> opcode 0x13, sous-commande 212 (0xD4).
    // Payload = sous-cmd (char4LE) + bloc fixe de 100 octets (7 champs en tete).
    PacketWriter w;
    w.WriteChar4LE((int8_t)212);   // this+9 : sous-commande
    w.WriteChar4LE(a1);            // bloc+0
    w.WriteChar4LE(a2);            // bloc+4
    w.WriteChar4LE(a3);            // bloc+8
    w.WriteChar4LE(a4);            // bloc+12
    w.WriteChar4LE(a5);            // bloc+16
    w.WriteChar4LE(a6);            // bloc+20
    w.WriteChar4LE(a7);            // bloc+24
    uint8_t pad[72] = {0};         // complement du bloc a 100 octets
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_220(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // [net] enveloppe Net_SendPacket_Op19 -> opcode 0x13, sous-commande 220 (0xDC).
    PacketWriter w;
    w.WriteChar4LE((int8_t)220);   // this+9 : sous-commande
    w.WriteChar4LE(a1);            // bloc+0
    w.WriteChar4LE(a2);            // bloc+4
    w.WriteChar4LE(a3);            // bloc+8
    w.WriteChar4LE(a4);            // bloc+12
    w.WriteChar4LE(a5);            // bloc+16
    w.WriteChar4LE(a6);            // bloc+20
    w.WriteChar4LE(a7);            // bloc+24
    uint8_t pad[72] = {0};         // complement du bloc a 100 octets
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_228(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // [net] enveloppe Net_SendPacket_Op19 -> opcode 0x13, sous-commande 228 (0xE4).
    PacketWriter w;
    w.WriteChar4LE((int8_t)228);   // this+9 : sous-commande
    w.WriteChar4LE(a1);            // bloc+0
    w.WriteChar4LE(a2);            // bloc+4
    w.WriteChar4LE(a3);            // bloc+8
    w.WriteChar4LE(a4);            // bloc+12
    w.WriteChar4LE(a5);            // bloc+16
    w.WriteChar4LE(a6);            // bloc+20
    w.WriteChar4LE(a7);            // bloc+24
    uint8_t pad[72] = {0};         // complement du bloc a 100 octets
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_236(NetClient& nc) {
    // [net] enveloppe Net_SendPacket_Op19 -> opcode 0x13, sous-commande 236 (0xEC).
    // Aucun champ : bloc de 100 octets entierement a zero (orig. : pile non init.).
    PacketWriter w;
    w.WriteChar4LE((int8_t)236);   // this+9 : sous-commande
    uint8_t block[100] = {0};      // bloc fixe de 100 octets
    w.WriteBytes(block, sizeof(block));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_244(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // [net] enveloppe Net_SendPacket_Op19 -> opcode 0x13, sous-commande 244 (0xF4).
    PacketWriter w;
    w.WriteChar4LE((int8_t)244);   // this+9 : sous-commande
    w.WriteChar4LE(a1);            // bloc+0
    w.WriteChar4LE(a2);            // bloc+4
    w.WriteChar4LE(a3);            // bloc+8
    w.WriteChar4LE(a4);            // bloc+12
    w.WriteChar4LE(a5);            // bloc+16
    w.WriteChar4LE(a6);            // bloc+20
    w.WriteChar4LE(a7);            // bloc+24
    uint8_t pad[72] = {0};         // complement du bloc a 100 octets
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_252(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // [net] enveloppe Net_SendPacket_Op19 -> opcode 0x13, sous-commande 252 (0xFC).
    PacketWriter w;
    w.WriteChar4LE((int8_t)252);   // this+9 : sous-commande
    w.WriteChar4LE(a1);            // bloc+0
    w.WriteChar4LE(a2);            // bloc+4
    w.WriteChar4LE(a3);            // bloc+8
    w.WriteChar4LE(a4);            // bloc+12
    w.WriteChar4LE(a5);            // bloc+16
    w.WriteChar4LE(a6);            // bloc+20
    w.WriteChar4LE(a7);            // bloc+24
    uint8_t pad[72] = {0};         // complement du bloc a 100 octets
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_248(NetClient& nc, int8_t a1) {
    // [net] enveloppe Net_SendPacket_Op19 -> opcode 0x13, sous-commande 248 (0xF8).
    // Un seul champ octet en tete du bloc de 100 o.
    PacketWriter w;
    w.WriteChar4LE((int8_t)248);   // this+9 : sous-commande
    w.WriteChar4LE(a1);            // bloc+0
    uint8_t pad[96] = {0};         // complement du bloc a 100 octets
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_0(NetClient& nc) {
    // [net] enveloppe Net_SendPacket_Op19 -> opcode 0x13, sous-commande 0.
    // Aucun champ : bloc de 100 octets a zero.
    PacketWriter w;
    w.WriteChar4LE((int8_t)0);     // this+9 : sous-commande
    uint8_t block[100] = {0};      // bloc fixe de 100 octets
    w.WriteBytes(block, sizeof(block));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_8(NetClient& nc, int8_t a1, int8_t a2) {
    // [net] enveloppe Net_SendPacket_Op19 -> opcode 0x13, sous-commande 8.
    // Deux champs octet en tete du bloc de 100 o. (v3@0, v4@4).
    PacketWriter w;
    w.WriteChar4LE((int8_t)8);     // this+9 : sous-commande
    w.WriteChar4LE(a1);            // bloc+0
    w.WriteChar4LE(a2);            // bloc+4
    uint8_t pad[92] = {0};         // complement du bloc a 100 octets
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_16(NetClient& nc, const void* buf12) {
    // [net] enveloppe Net_SendPacket_Op19 -> opcode 0x13, sous-commande 16.
    // Tampon de 12 octets copie en tete du bloc de 100 o.
    PacketWriter w;
    w.WriteChar4LE((int8_t)16);    // this+9 : sous-commande
    w.WriteBytes(buf12, 12);       // bloc+0 : tampon 12 octets
    uint8_t pad[88] = {0};         // complement du bloc a 100 octets
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendGuarded_4(NetClient& nc) {
    // [net] enveloppe Net_SendOp75 -> opcode 0x4B, sous-commande 4.
    // Garde : rien n'est envoye si un morph est en cours ou si le verrou
    // de cooldown GM est deja arme. Sinon : envoi + arme le verrou + horodate.
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.WriteChar4LE((int8_t)4);     // this+9 : sous-commande
    uint8_t block[500] = {0};      // bloc fixe de 500 octets (aucun champ)
    w.WriteBytes(block, sizeof(block));
    w.Finalize(0x4B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;   // horodatage du dernier envoi garde
}

void Net_SendGuarded_12(NetClient& nc) {
    // [net] enveloppe Net_SendOp75 -> opcode 0x4B, sous-commande 12.
    // Meme garde que Net_SendGuarded_4 (morph en cours / verrou cooldown GM).
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.WriteChar4LE((int8_t)12);    // this+9 : sous-commande
    uint8_t block[500] = {0};      // bloc fixe de 500 octets (aucun champ)
    w.WriteBytes(block, sizeof(block));
    w.Finalize(0x4B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;   // horodatage du dernier envoi garde
}

bool Net_SendUdpReport(uint8_t* self, const char* cp) {
    // [net] Envoi d'un rapport UDP (datagramme) a un hote 'cp'.
    // Chemin distinct du protocole TCP chiffre : socket UDP dediee, pas de
    // XOR ni d'entete PacketWriter. Sert le sous-systeme anti-cheat/telemetrie.
    WSADATA wsa;
    if (WSAStartup(0x0202, &wsa))
        return false;
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);   // socket(2, 2, 0)
    if (sock == INVALID_SOCKET)
        return false;

    sockaddr to;
    memset(&to, 0, sizeof(to));
    to.sa_family = AF_INET;
    // Port : depuis la config ASCII byte_1860E1C si presente, sinon 15000 (0x3A98).
    // Port : 15000 (0x3A98) par défaut (config ASCII byte_1860E1C non réimplémentée).
    u_short port = ntohs((u_short)0x3A98);
    *reinterpret_cast<uint16_t*>(to.sa_data) = port;
    *reinterpret_cast<uint32_t*>(&to.sa_data[2]) = inet_addr(cp);
    if (*reinterpret_cast<uint32_t*>(&to.sa_data[2]) == INADDR_NONE) {
        // 'cp' n'est pas une IP litterale : resolution DNS.
        hostent* he = gethostbyname(cp);
        if (!he) { closesocket(sock); return false; }
        memcpy(&to.sa_data[2], he->h_addr_list[0], he->h_length);
    }

    // TODO (anti-cheat/télémétrie) : le sérialiseur de rapport sub_6E323E (0x6E323E)
    // et l'état (self+6340) ne sont pas réimplémentés — chemin hors protocole de jeu.
    (void)self; (void)cp;
    bool ok = false;
    closesocket(sock);
    return ok;
}

void Net_SendPacket_Op14(NetClient& nc, int8_t value) {
    // [net] opcode 0x0E : 1 champ octet (promu 4 o LE). Longueur totale 13.
    PacketWriter w;
    w.WriteChar4LE(value);   // this+9
    w.Finalize(0x0E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op22(NetClient& nc, int8_t arg0, int8_t arg1) {
    // [net] opcode 0x16 : 2 champs octet (chacun promu 4 o LE). Longueur 17.
    PacketWriter w;
    w.WriteChar4LE(arg0);   // this+9
    w.WriteChar4LE(arg1);   // this+13
    w.Finalize(0x16, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op30(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7, int8_t arg8) {
    // [net] opcode 0x1E : 9 champs octet (chacun promu 4 o LE). Longueur 45.
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

void Net_SendOp110(NetClient& nc, int8_t arg0, int8_t arg1) {
    // [net] opcode 0x6E : 2 champs octet (chacun promu 4 o LE). Longueur 17.
    PacketWriter w;
    w.WriteChar4LE(arg0);   // this+9
    w.WriteChar4LE(arg1);   // this+13
    w.Finalize(0x6E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp43(NetClient& nc, const void* payload13, int8_t flag) {
    // [net] opcode 0x2B : bloc de 13 octets puis 1 champ octet (promu 4 o LE). Longueur 26.
    PacketWriter w;
    w.WriteBytes(payload13, 13);   // this+9 (memcpy 13 o)
    w.WriteChar4LE(flag);          // this+22
    w.Finalize(0x2B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp51(NetClient& nc) {
    // [net] opcode 0x33 : aucun payload (longueur 9, header seul).
    PacketWriter w;
    w.Finalize(0x33, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp59(NetClient& nc, const void* payload13) {
    // [net] opcode 0x3B : bloc de 13 octets. Longueur 22.
    PacketWriter w;
    w.WriteBytes(payload13, 13);   // this+9 (memcpy 13 o)
    w.Finalize(0x3B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp67(NetClient& nc, int8_t value) {
    // [net] opcode 0x43 : 1 champ octet (promu 4 o LE). Longueur 13.
    PacketWriter w;
    w.WriteChar4LE(value);   // this+9
    w.Finalize(0x43, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp75(NetClient& nc, int8_t kind, const void* payload500) {
    // [net] opcode 0x4B : 1 champ octet (kind, promu 4 o LE) + bloc de 500 octets. Longueur 513.
    // Builder generique reutilise par Net_SendGuarded_5/13 (commandes guilde).
    PacketWriter w;
    w.WriteChar4LE(kind);          // this+9
    w.WriteBytes(payload500, 500); // this+13 (memcpy 500 o)
    w.Finalize(0x4B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp83(NetClient& nc, int8_t arg0, int8_t arg1) {
    // [net] opcode 0x53 : 2 champs octet (chacun promu 4 o LE). Longueur 17.
    PacketWriter w;
    w.WriteChar4LE(arg0);   // this+9
    w.WriteChar4LE(arg1);   // this+13
    w.Finalize(0x53, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp91(NetClient& nc) {
    // [net] opcode 0x5B : aucun payload (longueur 9, header seul).
    PacketWriter w;
    w.Finalize(0x5B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp101(NetClient& nc) {
    // [net] opcode 0x65 : aucun payload (longueur 9, header seul).
    PacketWriter w;
    w.Finalize(0x65, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp107(NetClient& nc, int8_t kind, const void* payload13) {
    // [net] opcode 0x6B : 1 champ octet (kind, promu 4 o LE) + bloc de 13 octets. Longueur 26.
    PacketWriter w;
    w.WriteChar4LE(kind);          // this+9
    w.WriteBytes(payload13, 13);   // this+13 (memcpy 13 o)
    w.Finalize(0x6B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp118(NetClient& nc) {
    // [net] opcode 0x76 : aucun payload (longueur 9, header seul).
    PacketWriter w;
    w.Finalize(0x76, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp131(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7, int8_t arg8) {
    // [net] opcode 0x83 (131, ecrit -125 en char signe) : 9 champs octet (chacun promu 4 o LE). Longueur 45.
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
    // [net] opcode 0x8C (140, ecrit -116 en char signe) : aucun payload (longueur 9).
    PacketWriter w;
    w.Finalize(0x8C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_205(NetClient& nc, int8_t arg0, int8_t arg1) {
    // Wrapper vault : opcode fil 0x13 (builder Op19), sous-commande 205.
    // Op19 emet [sous-cmd:4 LE][bloc payload de 100 o]. Les args occupent la tete du bloc.
    PacketWriter w;
    w.WriteChar4LE((int8_t)205);   // sous-commande vault (this+9)
    w.WriteChar4LE(arg0);          // bloc +0
    w.WriteChar4LE(arg1);          // bloc +4
    uint8_t pad[92] = {0};         // reste du bloc de 100 o (8 utilises)
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_213(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // Wrapper vault : opcode fil 0x13 (builder Op19), sous-commande 213.
    // Op19 emet [sous-cmd:4 LE][bloc payload de 100 o]. 7 champs octet en tete du bloc.
    PacketWriter w;
    w.WriteChar4LE((int8_t)213);   // sous-commande vault
    w.WriteChar4LE(arg0);          // bloc +0
    w.WriteChar4LE(arg1);          // bloc +4
    w.WriteChar4LE(arg2);          // bloc +8
    w.WriteChar4LE(arg3);          // bloc +12
    w.WriteChar4LE(arg4);          // bloc +16
    w.WriteChar4LE(arg5);          // bloc +20
    w.WriteChar4LE(arg6);          // bloc +24
    uint8_t pad[72] = {0};         // reste du bloc de 100 o (28 utilises)
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_221(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // Wrapper vault : opcode fil 0x13 (builder Op19), sous-commande 221.
    // Op19 emet [sous-cmd:4 LE][bloc payload de 100 o]. 7 champs octet en tete du bloc.
    PacketWriter w;
    w.WriteChar4LE((int8_t)221);   // sous-commande vault
    w.WriteChar4LE(arg0);          // bloc +0
    w.WriteChar4LE(arg1);          // bloc +4
    w.WriteChar4LE(arg2);          // bloc +8
    w.WriteChar4LE(arg3);          // bloc +12
    w.WriteChar4LE(arg4);          // bloc +16
    w.WriteChar4LE(arg5);          // bloc +20
    w.WriteChar4LE(arg6);          // bloc +24
    uint8_t pad[72] = {0};         // reste du bloc de 100 o (28 utilises)
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_229(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // Wrapper vault : opcode fil 0x13 (builder Op19), sous-commande 229.
    // Op19 emet [sous-cmd:4 LE][bloc payload de 100 o]. 7 champs octet en tete du bloc.
    PacketWriter w;
    w.WriteChar4LE((int8_t)229);   // sous-commande vault
    w.WriteChar4LE(arg0);          // bloc +0
    w.WriteChar4LE(arg1);          // bloc +4
    w.WriteChar4LE(arg2);          // bloc +8
    w.WriteChar4LE(arg3);          // bloc +12
    w.WriteChar4LE(arg4);          // bloc +16
    w.WriteChar4LE(arg5);          // bloc +20
    w.WriteChar4LE(arg6);          // bloc +24
    uint8_t pad[72] = {0};         // reste du bloc de 100 o (28 utilises)
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_237(NetClient& nc) {
    // Wrapper vault : opcode fil 0x13 (builder Op19), sous-commande 237 (convertir junk en argent).
    // Aucun argument : le bloc de 100 o est entierement nul.
    PacketWriter w;
    w.WriteChar4LE((int8_t)237);   // sous-commande vault
    uint8_t pad[100] = {0};        // bloc payload de 100 o
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_245(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // Wrapper vault : opcode fil 0x13 (builder Op19), sous-commande 245.
    // Op19 emet [sous-cmd:4 LE][bloc payload de 100 o]. 7 champs octet en tete du bloc.
    PacketWriter w;
    w.WriteChar4LE((int8_t)245);   // sous-commande vault
    w.WriteChar4LE(arg0);          // bloc +0
    w.WriteChar4LE(arg1);          // bloc +4
    w.WriteChar4LE(arg2);          // bloc +8
    w.WriteChar4LE(arg3);          // bloc +12
    w.WriteChar4LE(arg4);          // bloc +16
    w.WriteChar4LE(arg5);          // bloc +20
    w.WriteChar4LE(arg6);          // bloc +24
    uint8_t pad[72] = {0};         // reste du bloc de 100 o (28 utilises)
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_253(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // Wrapper vault : opcode fil 0x13 (builder Op19), sous-commande 253.
    // Op19 emet [sous-cmd:4 LE][bloc payload de 100 o]. 7 champs octet en tete du bloc.
    PacketWriter w;
    w.WriteChar4LE((int8_t)253);   // sous-commande vault
    w.WriteChar4LE(arg0);          // bloc +0
    w.WriteChar4LE(arg1);          // bloc +4
    w.WriteChar4LE(arg2);          // bloc +8
    w.WriteChar4LE(arg3);          // bloc +12
    w.WriteChar4LE(arg4);          // bloc +16
    w.WriteChar4LE(arg5);          // bloc +20
    w.WriteChar4LE(arg6);          // bloc +24
    uint8_t pad[72] = {0};         // reste du bloc de 100 o (28 utilises)
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_249(NetClient& nc, int8_t arg0) {
    // Wrapper action : opcode fil 0x13 (builder Op19), sous-commande 249.
    PacketWriter w;
    w.WriteChar4LE((int8_t)249);   // sous-commande
    w.WriteChar4LE(arg0);          // bloc +0
    uint8_t pad[96] = {0};         // reste du bloc de 100 o (4 utilises)
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_1(NetClient& nc, const void* payload13) {
    // Wrapper action : opcode fil 0x13 (builder Op19), sous-commande 1 (payload de 13 octets).
    PacketWriter w;
    w.WriteChar4LE((int8_t)1);     // sous-commande
    w.WriteBytes(payload13, 13);   // bloc +0 (memcpy 13 o)
    uint8_t pad[87] = {0};         // reste du bloc de 100 o (13 utilises)
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_9(NetClient& nc, int8_t arg0) {
    // Wrapper action : opcode fil 0x13 (builder Op19), sous-commande 9.
    PacketWriter w;
    w.WriteChar4LE((int8_t)9);     // sous-commande
    w.WriteChar4LE(arg0);          // bloc +0
    uint8_t pad[96] = {0};         // reste du bloc de 100 o (4 utilises)
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_17(NetClient& nc, int8_t arg0) {
    // Wrapper action : opcode fil 0x13 (builder Op19), sous-commande 17.
    PacketWriter w;
    w.WriteChar4LE((int8_t)17);    // sous-commande
    w.WriteChar4LE(arg0);          // bloc +0
    uint8_t pad[96] = {0};         // reste du bloc de 100 o (4 utilises)
    w.WriteBytes(pad, sizeof(pad));
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendGuarded_5(NetClient& nc, const void* payload204) {
    // [SHELL] Commande guilde sous-op 5 via Net_SendOp75 (opcode fil 0x4B).
    // Garde : bloquee pendant un morph en cours ou si le cooldown GM est deja arme.
    // Etat de jeu externe (a rebrancher sur les vrais membres) :
    extern int   g_MorphInProgress;
    extern int   g_GmCmdCooldownLatch;
    extern float flt_1675B0C;      // horodatage du cooldown GM
    extern float g_GameTimeSec;    // temps de jeu courant (s)
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    uint8_t buf[500] = {0};              // bloc payload de Op75 (500 o)
    std::memcpy(buf, payload204, 204);   // 204 octets utiles copies en tete
    Net_SendOp75(nc, 5, buf);            // kind=5, opcode fil 0x4B
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendGuarded_13(NetClient& nc) {
    // [SHELL] Commande guilde sous-op 13 via Net_SendOp75 (opcode fil 0x4B), sans argument.
    // Garde : bloquee pendant un morph en cours ou si le cooldown GM est deja arme.
    // Etat de jeu externe (a rebrancher sur les vrais membres) :
    extern int   g_MorphInProgress;
    extern int   g_GmCmdCooldownLatch;
    extern float flt_1675B0C;      // horodatage du cooldown GM
    extern float g_GameTimeSec;    // temps de jeu courant (s)
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    uint8_t buf[500] = {0};        // bloc non initialise dans l'original -> nul ici
    Net_SendOp75(nc, 13, buf);     // kind=13, opcode fil 0x4B
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendPacket_Op15(NetClient& nc, const void* data72) {
    // [net] opcode 0x0F (15) — payload brut de 72 octets (paquet total 81)
    PacketWriter w;
    w.WriteBytes(data72, 72);                       // bloc copie a this+9
    w.Finalize(0x0F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op23(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3) {
    // [net] opcode 0x17 (23) — 3 champs octet (chacun promu 4 octets LE), total 21
    PacketWriter w;
    w.WriteChar4LE(arg1);                           // this+9
    w.WriteChar4LE(arg2);                           // this+13
    w.WriteChar4LE(arg3);                           // this+17
    w.Finalize(0x17, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op31(NetClient& nc, int8_t kind, const void* data1232) {
    // [net] opcode 0x1F (31) — 1 octet + bloc de 1232 octets, total 1245
    PacketWriter w;
    w.WriteChar4LE(kind);                           // this+9
    w.WriteBytes(data1232, 1232);                   // this+13
    w.Finalize(0x1F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp36(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5) {
    // [net] opcode 0x24 (36) — 5 champs octet (4 octets LE chacun), total 29
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
    // [net] opcode 0x2C (44) — sans payload (paquet total 9)
    PacketWriter w;
    w.Finalize(0x2C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp52(NetClient& nc) {
    // [net] opcode 0x34 (52) — sans payload (paquet total 9)
    PacketWriter w;
    w.Finalize(0x34, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp60(NetClient& nc) {
    // [net] opcode 0x3C (60) — sans payload (paquet total 9)
    PacketWriter w;
    w.Finalize(0x3C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp68(NetClient& nc, const void* data61) {
    // [net] opcode 0x44 (68) — payload brut de 61 octets (total 70)
    PacketWriter w;
    w.WriteBytes(data61, 61);                       // this+9
    w.Finalize(0x44, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp76(NetClient& nc, const void* data61) {
    // [net] opcode 0x4C (76) — payload brut de 61 octets (total 70)
    PacketWriter w;
    w.WriteBytes(data61, 61);                       // this+9
    w.Finalize(0x4C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp84(NetClient& nc, const void* data101, int8_t flag) {
    // [net] opcode 0x54 (84) — bloc 101 octets + 1 octet (4 LE), total 114
    PacketWriter w;
    w.WriteBytes(data101, 101);                     // this+9
    w.WriteChar4LE(flag);                           // this+110
    w.Finalize(0x54, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp92(NetClient& nc, int8_t value) {
    // [net] opcode 0x5C (92) — 1 champ octet (4 octets LE), total 13
    PacketWriter w;
    w.WriteChar4LE(value);                          // this+9
    w.Finalize(0x5C, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp136(NetClient& nc) {
    // [net] opcode 0x88 (136) — sans payload (paquet total 9)
    PacketWriter w;
    w.Finalize(0x88, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp111(NetClient& nc, int8_t value) {
    // [net] opcode 0x6F (111) — 1 champ octet (4 octets LE), total 13
    PacketWriter w;
    w.WriteChar4LE(value);                          // this+9
    w.Finalize(0x6F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp119(NetClient& nc) {
    // [net] opcode 0x77 (119) — sans payload (paquet total 9)
    PacketWriter w;
    w.Finalize(0x77, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp132(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4) {
    // [net] opcode 0x84 (132) — 4 champs octet (4 octets LE chacun), total 25
    PacketWriter w;
    w.WriteChar4LE(arg1);                           // this+9
    w.WriteChar4LE(arg2);                           // this+13
    w.WriteChar4LE(arg3);                           // this+17
    w.WriteChar4LE(arg4);                           // this+21
    w.Finalize(0x84, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp141(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, const void* data24) {
    // [net] opcode 0x8D (141) — 3 champs octet + bloc 24 octets, total 45
    PacketWriter w;
    w.WriteChar4LE(arg1);                           // this+9
    w.WriteChar4LE(arg2);                           // this+13
    w.WriteChar4LE(arg3);                           // this+17
    w.WriteBytes(data24, 24);                       // this+21
    w.Finalize(0x8D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_206(NetClient& nc, int8_t arg1) {
    // [net] requete coffre : opcode 0x13 (19) via Net_SendPacket_Op19, sous-op 206.
    // Op19 emet toujours [sous-op:4][bloc fixe 100] -> on complete a zero le bloc.
    PacketWriter w;
    w.WriteU32(206);                                // sous-opcode (int32, this+9)
    w.WriteChar4LE(arg1);                           // bloc[0..3]
    for (int k = 0; k < 96; ++k) w.WriteU8(0);      // reste du bloc de 100 octets
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_214(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7) {
    // [net] requete coffre : opcode 0x13 (19) via Op19, sous-op 214 ; 7 champs octet.
    PacketWriter w;
    w.WriteU32(214);                                // sous-opcode
    w.WriteChar4LE(arg1);
    w.WriteChar4LE(arg2);
    w.WriteChar4LE(arg3);
    w.WriteChar4LE(arg4);
    w.WriteChar4LE(arg5);
    w.WriteChar4LE(arg6);
    w.WriteChar4LE(arg7);                           // 28 octets ecrits
    for (int k = 0; k < 72; ++k) w.WriteU8(0);      // padding bloc 100
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_222(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7) {
    // [net] requete coffre : opcode 0x13 (19) via Op19, sous-op 222 ; 7 champs octet.
    PacketWriter w;
    w.WriteU32(222);                                // sous-opcode
    w.WriteChar4LE(arg1);
    w.WriteChar4LE(arg2);
    w.WriteChar4LE(arg3);
    w.WriteChar4LE(arg4);
    w.WriteChar4LE(arg5);
    w.WriteChar4LE(arg6);
    w.WriteChar4LE(arg7);                           // 28 octets ecrits
    for (int k = 0; k < 72; ++k) w.WriteU8(0);      // padding bloc 100
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_230(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7) {
    // [net] requete coffre : opcode 0x13 (19) via Op19, sous-op 230 ; 7 champs octet.
    PacketWriter w;
    w.WriteU32(230);                                // sous-opcode
    w.WriteChar4LE(arg1);
    w.WriteChar4LE(arg2);
    w.WriteChar4LE(arg3);
    w.WriteChar4LE(arg4);
    w.WriteChar4LE(arg5);
    w.WriteChar4LE(arg6);
    w.WriteChar4LE(arg7);                           // 28 octets ecrits
    for (int k = 0; k < 72; ++k) w.WriteU8(0);      // padding bloc 100
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_238(NetClient& nc) {
    // [net] requete coffre : opcode 0x13 (19) via Op19, sous-op 238 ; sans arguments.
    // Le bloc de 100 octets est envoye entierement a zero.
    PacketWriter w;
    w.WriteU32(238);                                // sous-opcode
    for (int k = 0; k < 100; ++k) w.WriteU8(0);     // bloc 100 octets
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_246(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7) {
    // [net] requete coffre : opcode 0x13 (19) via Op19, sous-op 246 ; 7 champs octet.
    PacketWriter w;
    w.WriteU32(246);                                // sous-opcode
    w.WriteChar4LE(arg1);
    w.WriteChar4LE(arg2);
    w.WriteChar4LE(arg3);
    w.WriteChar4LE(arg4);
    w.WriteChar4LE(arg5);
    w.WriteChar4LE(arg6);
    w.WriteChar4LE(arg7);                           // 28 octets ecrits
    for (int k = 0; k < 72; ++k) w.WriteU8(0);      // padding bloc 100
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_254(NetClient& nc, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // [net] requete coffre : opcode 0x13 (19) via Op19, sous-op 254 ; 6 champs octet.
    PacketWriter w;
    w.WriteU32(254);                                // sous-opcode
    w.WriteChar4LE(arg1);
    w.WriteChar4LE(arg2);
    w.WriteChar4LE(arg3);
    w.WriteChar4LE(arg4);
    w.WriteChar4LE(arg5);
    w.WriteChar4LE(arg6);                           // 24 octets ecrits
    for (int k = 0; k < 76; ++k) w.WriteU8(0);      // padding bloc 100
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_250(NetClient& nc, int8_t arg1) {
    // [net] requete action : opcode 0x13 (19) via Op19, sous-op 250 ; 1 champ octet.
    PacketWriter w;
    w.WriteU32(250);                                // sous-opcode
    w.WriteChar4LE(arg1);                           // bloc[0..3]
    for (int k = 0; k < 96; ++k) w.WriteU8(0);      // padding bloc 100
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_2(NetClient& nc, const void* data13) {
    // [net] requete action : opcode 0x13 (19) via Op19, sous-op 2 ; bloc source de 13 octets.
    PacketWriter w;
    w.WriteU32(2);                                  // sous-opcode
    w.WriteBytes(data13, 13);                       // bloc[0..12]
    for (int k = 0; k < 87; ++k) w.WriteU8(0);      // padding bloc 100
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_10(NetClient& nc, int8_t arg1, int8_t arg2) {
    // [net] requete action : opcode 0x13 (19) via Op19, sous-op 10 ; 2 champs octet.
    PacketWriter w;
    w.WriteU32(10);                                 // sous-opcode
    w.WriteChar4LE(arg1);                           // bloc[0..3]
    w.WriteChar4LE(arg2);                           // bloc[4..7]
    for (int k = 0; k < 92; ++k) w.WriteU8(0);      // padding bloc 100
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_18(NetClient& nc, int8_t arg1) {
    // [net] requete action : opcode 0x13 (19) via Op19, sous-op 18 ; 1 champ octet.
    PacketWriter w;
    w.WriteU32(18);                                 // sous-opcode
    w.WriteChar4LE(arg1);                           // bloc[0..3]
    for (int k = 0; k < 96; ++k) w.WriteU8(0);      // padding bloc 100
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendGuarded_6(NetClient& nc) {
    // [net] commande gardee : opcode 0x4B (75) via Net_SendOp75, sous-op 6 ; sans arguments.
    // Op75 emet [sous-op:4][bloc fixe 500] -> bloc complete a zero.
    // Garde anti-spam : ignoree si un morph est en cours ou si le latch de cooldown est arme.
    if (g_MorphInProgress != 1 && !g_GmCmdCooldownLatch) {
        PacketWriter w;
        w.WriteU32(6);                              // sous-opcode
        for (int k = 0; k < 500; ++k) w.WriteU8(0); // bloc 500 octets
        w.Finalize(0x4B, DefaultRng(), nc.xorKey, nc.seq);
        NetSend(nc, w.Data(), (int)w.Size());
        g_GmCmdCooldownLatch = 1;                   // arme le latch
        flt_1675B0C = g_GameTimeSec;                // horodatage cooldown
    }
}

void Net_SendGuarded_14(NetClient& nc, int8_t arg1) {
    // [net] commande guilde gardee : opcode 0x4B (75) via Net_SendOp75, sous-op 14 ; 1 champ octet.
    // Garde anti-spam identique a Net_SendGuarded_6.
    if (g_MorphInProgress != 1 && !g_GmCmdCooldownLatch) {
        PacketWriter w;
        w.WriteU32(14);                             // sous-opcode
        w.WriteChar4LE(arg1);                       // bloc[0..3]
        for (int k = 0; k < 496; ++k) w.WriteU8(0); // padding bloc 500
        w.Finalize(0x4B, DefaultRng(), nc.xorKey, nc.seq);
        NetSend(nc, w.Data(), (int)w.Size());
        g_GmCmdCooldownLatch = 1;                   // arme le latch
        flt_1675B0C = g_GameTimeSec;                // horodatage cooldown
    }
}

void Net_SendPacket_Op16(NetClient& nc, const void* payload) {
    // opcode 0x10 (16) — charge utile brute de 72 octets (this+9, len totale 81)
    PacketWriter w;
    w.WriteBytes(payload, 72);   // tampon copie de 72 o
    w.Finalize(0x10, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op24(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d) {
    // opcode 0x18 (24) — 4 champs char, chacun emis sur 4 octets LE (len 25)
    PacketWriter w;
    w.WriteChar4LE(a);   // this+9
    w.WriteChar4LE(b);   // this+13
    w.WriteChar4LE(c);   // this+17
    w.WriteChar4LE(d);   // this+21
    w.Finalize(0x18, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op32(NetClient& nc, int8_t a) {
    // opcode 0x20 (32) — 1 champ char emis sur 4 octets LE (len 13)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.Finalize(0x20, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp37(NetClient& nc) {
    // opcode 0x25 (37) — aucune charge utile (len 9)
    PacketWriter w;
    w.Finalize(0x25, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp45(NetClient& nc, int8_t a) {
    // opcode 0x2D (45) — 1 champ char emis sur 4 octets LE (len 13)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.Finalize(0x2D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp53(NetClient& nc, const void* payload) {
    // opcode 0x35 (53) — charge utile brute de 13 octets (len 22)
    PacketWriter w;
    w.WriteBytes(payload, 13);
    w.Finalize(0x35, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp61(NetClient& nc, int8_t a) {
    // opcode 0x3D (61) — 1 champ char emis sur 4 octets LE (len 13)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.Finalize(0x3D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp69(NetClient& nc) {
    // opcode 0x45 (69) — aucune charge utile (len 9)
    PacketWriter w;
    w.Finalize(0x45, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp77(NetClient& nc, const void* payload) {
    // opcode 0x4D (77) — charge utile brute de 61 octets (len 70)
    PacketWriter w;
    w.WriteBytes(payload, 61);
    w.Finalize(0x4D, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp85(NetClient& nc, uint32_t length, const void* data) {
    // opcode 0x55 (85) — TLV variable : [u32 length][length octets] (len = length+13)
    // source d'origine : struct a2 -> length en a2+4, data en a2+8
    PacketWriter w;
    w.WriteU32(length);          // champ longueur u32 (this+9)
    w.WriteBytes(data, length);  // charge utile variable (this+13)
    w.Finalize(0x55, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp94(NetClient& nc, const void* data) {
    // opcode 0x5E (94) — copie 64 octets d'un tampon global (dword_1674A60) (len 73)
    PacketWriter w;
    w.WriteBytes(data, 64);   // dword_1674A60, 64 o
    w.Finalize(0x5E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp100(NetClient& nc, int8_t a, int8_t b, const void* data) {
    // opcode 0x64 (100) — 2 champs char (4 LE chacun) + tampon 13 octets (len 30)
    PacketWriter w;
    w.WriteChar4LE(a);         // this+9
    w.WriteChar4LE(b);         // this+13
    w.WriteBytes(data, 13);    // this+17
    w.Finalize(0x64, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp112(NetClient& nc, const void* payload) {
    // opcode 0x70 (112) — charge utile brute de 61 octets (len 70)
    PacketWriter w;
    w.WriteBytes(payload, 61);
    w.Finalize(0x70, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp120(NetClient& nc, int8_t a, int8_t b, int8_t c) {
    // opcode 0x78 (120) — 3 champs char, chacun emis sur 4 octets LE (len 21)
    PacketWriter w;
    w.WriteChar4LE(a);   // this+9
    w.WriteChar4LE(b);   // this+13
    w.WriteChar4LE(c);   // this+17
    w.Finalize(0x78, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp133(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d, int8_t e, int8_t f, int8_t g, int8_t h, int8_t i) {
    // opcode 0x85 (133, ecrit -123) — 9 champs char, chacun emis sur 4 octets LE (len 45)
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
    // opcode 0x8E (142, ecrit -114) — 4 champs char, chacun emis sur 4 octets LE (len 25)
    PacketWriter w;
    w.WriteChar4LE(a);   // this+9
    w.WriteChar4LE(b);   // this+13
    w.WriteChar4LE(c);   // this+17
    w.WriteChar4LE(d);   // this+21
    w.Finalize(0x8E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_207(NetClient& nc, int8_t amount) {
    // opcode 0xCF (207) — requete coffre : retrait/soustraction d'argent (1 champ char, 4 LE)
    PacketWriter w;
    w.WriteChar4LE(amount);
    w.Finalize(0xCF, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_215(NetClient& nc, int8_t npcId, int8_t itemId, int8_t qty, int8_t page, int8_t slot, int8_t col, int8_t row) {
    // opcode 0xD7 (215) — achat boutique PNJ : 7 champs char (npcId,itemId,qty,page,slot,col,row), 4 LE chacun
    PacketWriter w;
    w.WriteChar4LE(npcId);
    w.WriteChar4LE(itemId);
    w.WriteChar4LE(qty);
    w.WriteChar4LE(page);
    w.WriteChar4LE(slot);
    w.WriteChar4LE(col);
    w.WriteChar4LE(row);
    w.Finalize(0xD7, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_223(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // opcode 0xDF (223) — requete coffre : 7 champs char, 4 LE chacun
    PacketWriter w;
    w.WriteChar4LE(a1);
    w.WriteChar4LE(a2);
    w.WriteChar4LE(a3);
    w.WriteChar4LE(a4);
    w.WriteChar4LE(a5);
    w.WriteChar4LE(a6);
    w.WriteChar4LE(a7);
    w.Finalize(0xDF, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_231(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // opcode 0xE7 (231) — requete coffre : 7 champs char, 4 LE chacun
    PacketWriter w;
    w.WriteChar4LE(a1);
    w.WriteChar4LE(a2);
    w.WriteChar4LE(a3);
    w.WriteChar4LE(a4);
    w.WriteChar4LE(a5);
    w.WriteChar4LE(a6);
    w.WriteChar4LE(a7);
    w.Finalize(0xE7, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_239(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // opcode 0xEF (239) — requete coffre : 7 champs char, 4 LE chacun
    PacketWriter w;
    w.WriteChar4LE(a1);
    w.WriteChar4LE(a2);
    w.WriteChar4LE(a3);
    w.WriteChar4LE(a4);
    w.WriteChar4LE(a5);
    w.WriteChar4LE(a6);
    w.WriteChar4LE(a7);
    w.Finalize(0xEF, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_247(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // opcode 0xF7 (247) — requete coffre : 7 champs char, 4 LE chacun
    PacketWriter w;
    w.WriteChar4LE(a1);
    w.WriteChar4LE(a2);
    w.WriteChar4LE(a3);
    w.WriteChar4LE(a4);
    w.WriteChar4LE(a5);
    w.WriteChar4LE(a6);
    w.WriteChar4LE(a7);
    w.Finalize(0xF7, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_255(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6) {
    // opcode 0xFF (255) — requete coffre : 6 champs char, 4 LE chacun
    PacketWriter w;
    w.WriteChar4LE(a1);
    w.WriteChar4LE(a2);
    w.WriteChar4LE(a3);
    w.WriteChar4LE(a4);
    w.WriteChar4LE(a5);
    w.WriteChar4LE(a6);
    w.Finalize(0xFF, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_251(NetClient& nc, const void* data) {
    // Net_SendCmd_251 0x592870 -> Net_SendPacket_Op19 0x4B4E70 (opcode 0x13), PAS un
    // opcode 251 à plat. Binaire : _BYTE v2[108]; Crt_Memcpy(v2, data, 12) (EA 0x592894) ;
    // Net_SendPacket_Op19(&g_AutoPlayMgr, 507, v2) (EA 0x5928ae).
    // Op19 (0x4B4E70) : opcode 0x13 @+8 ; sous-code 507=0x1FB int32 LE @+9 (Crt_Memcpy(this+9,
    //   &a2,4) EA 0x4b4f0f) ; charge 100 o @+13 (Crt_Memcpy(this+13,a3,0x64) EA 0x4b4f24) ;
    //   *(this+15000)=113 (longueur fil 9+4+100) ; XOR clé complet ; ++seq.
    // Les 12 premiers octets du bloc de 100 = data ; les 88 restants = pile v2 NON initialisée
    //   côté binaire (fidèlement remplacés par des zéros ici, aucune fuite de pile reproduite).
    PacketWriter w;
    w.WriteI32(507);                 // this+9 : sous-code 0x1FB (i32, PAS char4LE ni u8 -> 507 intact)
    uint8_t block[100] = {0};        // bloc payload 100 o (v2, 12 utiles + 88 à 0)
    std::memcpy(block, data, 12);    // Crt_Memcpy(v2, data, 12) EA 0x592894
    w.WriteBytes(block, 100);        // this+13 : 100 octets
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq); // opcode 19, longueur totale 113
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_3(NetClient& nc, const void* data) {
    // opcode 0x03 (3) — requete d'action : charge utile brute de 13 octets
    PacketWriter w;
    w.WriteBytes(data, 13);
    w.Finalize(0x03, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_11(NetClient& nc, int8_t a, int8_t b) {
    // opcode 0x0B (11) — requete d'action : 2 champs char, 4 LE chacun
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.Finalize(0x0B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_19(NetClient& nc, int8_t a) {
    // opcode 0x13 (19) — requete d'action : 1 champ char, 4 LE
    PacketWriter w;
    w.WriteChar4LE(a);
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendGuarded_7(NetClient& nc) {
    // opcode 0x07 (7) — requete montee de grade guilde, gardee (anti-spam pendant morph)
    // globals de jeu (externes) : g_MorphInProgress, g_GmCmdCooldownLatch, g_GameTimeSec, flt_1675B0C
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;   // charge utile vide (variante d'envoi Op75 dans l'original)
    w.Finalize(0x07, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendGuarded_17(NetClient& nc, const void* data1, const void* data2, int8_t flag) {
    // opcode 0x11 (17) — gardee : 13 octets + 13 octets + 1 champ char (4 LE)
    // globals de jeu (externes) : g_MorphInProgress, g_GmCmdCooldownLatch, g_GameTimeSec, flt_1675B0C
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

void Net_SendPacket_Op17(NetClient& nc, const void* payload61) {
    // [net] opcode 0x11 : bloc de 61 octets copie tel quel a this+9
    PacketWriter w;
    w.WriteBytes(payload61, 61);
    w.Finalize(0x11, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op25(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d) {
    // [net] opcode 0x19 : quatre champs octet (chacun emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.WriteChar4LE(c);
    w.WriteChar4LE(d);
    w.Finalize(0x19, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op33(NetClient& nc, const void* text13) {
    // [net] opcode 0x21 : chaine/bloc de 13 octets
    PacketWriter w;
    w.WriteBytes(text13, 13);
    w.Finalize(0x21, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp38(NetClient& nc, const void* payload61) {
    // [net] opcode 0x26 : chat de groupe, bloc de 61 octets
    PacketWriter w;
    w.WriteBytes(payload61, 61);
    w.Finalize(0x26, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp46(NetClient& nc) {
    // [net] opcode 0x2E : sans argument (payload vide)
    PacketWriter w;
    w.Finalize(0x2E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp54(NetClient& nc) {
    // [net] opcode 0x36 : sans argument (payload vide)
    PacketWriter w;
    w.Finalize(0x36, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp62(NetClient& nc) {
    // [net] opcode 0x3E : sans argument (payload vide)
    PacketWriter w;
    w.Finalize(0x3E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp70(NetClient& nc, const void* payload13) {
    // [net] opcode 0x46 : bloc de 13 octets
    PacketWriter w;
    w.WriteBytes(payload13, 13);
    w.Finalize(0x46, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp78(NetClient& nc, const void* payload13) {
    // [net] opcode 0x4E : bloc de 13 octets
    PacketWriter w;
    w.WriteBytes(payload13, 13);
    w.Finalize(0x4E, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp86(NetClient& nc, int8_t a, int8_t b) {
    // [net] opcode 0x56 : deux champs octet (emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.Finalize(0x56, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp95(NetClient& nc, const void* position12, int8_t kind) {
    // [net] opcode 0x5F : position float3 (12 o) puis un octet
    // Original : position lue depuis le global self (flt_1687330).
    PacketWriter w;
    w.WriteBytes(position12, 12);
    w.WriteChar4LE(kind);
    w.Finalize(0x5F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp102(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d) {
    // [net] opcode 0x66 : quatre champs octet (emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.WriteChar4LE(c);
    w.WriteChar4LE(d);
    w.Finalize(0x66, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp113(NetClient& nc, int8_t a) {
    // [net] opcode 0x71 : un champ octet (emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.Finalize(0x71, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp121(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d) {
    // [net] opcode 0x79 : quatre champs octet (emis sur 4 octets LE)
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.WriteChar4LE(c);
    w.WriteChar4LE(d);
    w.Finalize(0x79, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp134(NetClient& nc, int8_t a, int8_t b, int8_t c, int8_t d) {
    // [net] opcode 0x86 (=134 ; -122 signe dans la decomp) : quatre champs octet
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.WriteChar4LE(c);
    w.WriteChar4LE(d);
    w.Finalize(0x86, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp143(NetClient& nc, int8_t a, int8_t b, int8_t c) {
    // [net] opcode 0x8F (=143 ; -113 signe dans la decomp) : trois champs octet
    PacketWriter w;
    w.WriteChar4LE(a);
    w.WriteChar4LE(b);
    w.WriteChar4LE(c);
    w.Finalize(0x8F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_208(NetClient& nc, int8_t p1, int8_t p2, int8_t p3, int8_t p4, int8_t p5, int8_t p6, int8_t p7) {
    // [net] opcode reseau 0x13 (Net_SendPacket_Op19) ; sous-commande coffre = 208 (0xD0)
    // Payload = [sous-cmd u32][bloc fixe 100 o : 7 champs char (4 o LE) + bourrage].
    PacketWriter w;
    w.WriteU32(208);            // sous-commande poussee en 32 bits (D0 00 00 00)
    w.WriteChar4LE(p1);
    w.WriteChar4LE(p2);
    w.WriteChar4LE(p3);
    w.WriteChar4LE(p4);
    w.WriteChar4LE(p5);
    w.WriteChar4LE(p6);
    w.WriteChar4LE(p7);
    for (int i = 0; i < 72; ++i) w.WriteU8(0);   // 100 - 7*4 : complete le bloc fixe
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_216(NetClient& nc, int8_t p1, int8_t p2, int8_t p3, int8_t p4, int8_t p5, int8_t p6, int8_t p7) {
    // [net] opcode reseau 0x13 (Net_SendPacket_Op19) ; sous-commande coffre = 216 (0xD8)
    PacketWriter w;
    w.WriteU32(216);
    w.WriteChar4LE(p1);
    w.WriteChar4LE(p2);
    w.WriteChar4LE(p3);
    w.WriteChar4LE(p4);
    w.WriteChar4LE(p5);
    w.WriteChar4LE(p6);
    w.WriteChar4LE(p7);
    for (int i = 0; i < 72; ++i) w.WriteU8(0);
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_224(NetClient& nc, int8_t p1, int8_t p2, int8_t p3, int8_t p4, int8_t p5, int8_t p6, int8_t p7) {
    // [net] opcode reseau 0x13 (Net_SendPacket_Op19) ; sous-commande coffre = 224 (0xE0)
    PacketWriter w;
    w.WriteU32(224);
    w.WriteChar4LE(p1);
    w.WriteChar4LE(p2);
    w.WriteChar4LE(p3);
    w.WriteChar4LE(p4);
    w.WriteChar4LE(p5);
    w.WriteChar4LE(p6);
    w.WriteChar4LE(p7);
    for (int i = 0; i < 72; ++i) w.WriteU8(0);
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_232(NetClient& nc, int8_t p1, int8_t p2, int8_t p3, int8_t p4, int8_t p5, int8_t p6, int8_t p7) {
    // [net] opcode reseau 0x13 (Net_SendPacket_Op19) ; sous-commande coffre = 232 (0xE8)
    PacketWriter w;
    w.WriteU32(232);
    w.WriteChar4LE(p1);
    w.WriteChar4LE(p2);
    w.WriteChar4LE(p3);
    w.WriteChar4LE(p4);
    w.WriteChar4LE(p5);
    w.WriteChar4LE(p6);
    w.WriteChar4LE(p7);
    for (int i = 0; i < 72; ++i) w.WriteU8(0);
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_240(NetClient& nc, int8_t p1, int8_t p2, int8_t p3, int8_t p4, int8_t p5, int8_t p6, int8_t p7) {
    // [net] opcode reseau 0x13 (Net_SendPacket_Op19) ; sous-commande coffre = 240 (0xF0)
    PacketWriter w;
    w.WriteU32(240);
    w.WriteChar4LE(p1);
    w.WriteChar4LE(p2);
    w.WriteChar4LE(p3);
    w.WriteChar4LE(p4);
    w.WriteChar4LE(p5);
    w.WriteChar4LE(p6);
    w.WriteChar4LE(p7);
    for (int i = 0; i < 72; ++i) w.WriteU8(0);
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_248(NetClient& nc, int8_t p1, int8_t p2, int8_t p3, int8_t p4, int8_t p5, int8_t p6, int8_t p7) {
    // [net] opcode reseau 0x13 (Net_SendPacket_Op19) ; sous-commande coffre = 248 (0xF8)
    PacketWriter w;
    w.WriteU32(248);
    w.WriteChar4LE(p1);
    w.WriteChar4LE(p2);
    w.WriteChar4LE(p3);
    w.WriteChar4LE(p4);
    w.WriteChar4LE(p5);
    w.WriteChar4LE(p6);
    w.WriteChar4LE(p7);
    for (int i = 0; i < 72; ++i) w.WriteU8(0);
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_0(NetClient& nc, int8_t p1, int8_t p2, int8_t p3, int8_t p4, int8_t p5, int8_t p6) {
    // [net] opcode reseau 0x13 (Net_SendPacket_Op19) ; sous-commande coffre = 0
    // Payload = [sous-cmd u32][bloc fixe 100 o : 6 champs char (4 o LE) + bourrage].
    PacketWriter w;
    w.WriteU32(0);
    w.WriteChar4LE(p1);
    w.WriteChar4LE(p2);
    w.WriteChar4LE(p3);
    w.WriteChar4LE(p4);
    w.WriteChar4LE(p5);
    w.WriteChar4LE(p6);
    for (int i = 0; i < 76; ++i) w.WriteU8(0);   // 100 - 6*4
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_252(NetClient& nc, int8_t a1) {
    // [net] opcode reseau 0x13 (Net_SendPacket_Op19) ; sous-commande 252 (0xFC)
    // Payload = [sous-cmd u32][bloc fixe 100 o : 1 champ char + bourrage].
    PacketWriter w;
    w.WriteU32(252);
    w.WriteChar4LE(a1);
    for (int i = 0; i < 96; ++i) w.WriteU8(0);   // 100 - 4
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_4(NetClient& nc, const void* payload13) {
    // [net] opcode reseau 0x13 (Net_SendPacket_Op19) ; sous-commande 4
    // Payload = [sous-cmd u32][bloc fixe 100 o : 13 octets copies + bourrage].
    PacketWriter w;
    w.WriteU32(4);
    w.WriteBytes(payload13, 13);
    for (int i = 0; i < 87; ++i) w.WriteU8(0);   // 100 - 13
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_12(NetClient& nc, int8_t a1) {
    // [net] opcode reseau 0x13 (Net_SendPacket_Op19) ; sous-commande 12
    PacketWriter w;
    w.WriteU32(12);
    w.WriteChar4LE(a1);
    for (int i = 0; i < 96; ++i) w.WriteU8(0);   // 100 - 4
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendCmd_22(NetClient& nc, int8_t a1) {
    // [net] opcode reseau 0x13 (Net_SendPacket_Op19) ; sous-commande 22
    PacketWriter w;
    w.WriteU32(22);
    w.WriteChar4LE(a1);
    for (int i = 0; i < 96; ++i) w.WriteU8(0);   // 100 - 4
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendGuarded_8(NetClient& nc, const void* payload13) {
    // [net] opcode reseau 0x4B (Net_SendOp75) ; sous-commande 8 ; bloc fixe de 500 octets.
    // Gating anti-spam commande MJ : bloque pendant une morph ou si le verrou de cooldown est actif.
    // g_MorphInProgress / g_GmCmdCooldownLatch / g_GameTimeSec / flt_1675B0C = etat de jeu (externes).
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
    // [net] opcode reseau 0x4F (Net_SendOp79) ; sous-commande 1 ; bloc fixe de 100 octets (non initialise dans l'original -> 0).
    // Meme gating anti-spam commande MJ que Net_SendGuarded_8.
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    PacketWriter w;
    w.WriteU32(1);
    for (int i = 0; i < 100; ++i) w.WriteU8(0);   // bloc de 100 o vide
    w.Finalize(0x4F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;
}

void Net_SendPacket_Op18(NetClient& nc, const void* payload76) {
    // [net] opcode 18 (0x12) : bloc opaque de 76 octets
    PacketWriter w;
    w.WriteBytes(payload76, 76);                        // this+9 .. +84
    w.Finalize(0x12, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 76 = 85
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op26(NetClient& nc, int8_t f0, int8_t f1, int8_t f2, int8_t f3, int8_t f4) {
    // [net] opcode 26 (0x1A) : 5 champs octet (chacun promu sur 4 o LE)
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.WriteChar4LE(f2);   // +17
    w.WriteChar4LE(f3);   // +21
    w.WriteChar4LE(f4);   // +25
    w.Finalize(0x1A, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 20 = 29
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op34(NetClient& nc, int8_t f0, int8_t f1) {
    // [net] opcode 34 (0x22) : 2 champs octet
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.Finalize(0x22, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 8 = 17
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp39(NetClient& nc, const void* target13, const void* message61) {
    // [net] opcode 39 (0x27) : chuchotement -> cible (13 o) + message (61 o)
    PacketWriter w;
    w.WriteBytes(target13, 13);    // +9
    w.WriteBytes(message61, 61);   // +22
    w.Finalize(0x27, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 13 + 61 = 83
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp47(NetClient& nc, const void* payload13) {
    // [net] opcode 47 (0x2F) : bloc de 13 octets
    PacketWriter w;
    w.WriteBytes(payload13, 13);   // +9
    w.Finalize(0x2F, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 13 = 22
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp55(NetClient& nc, int8_t value) {
    // [net] opcode 55 (0x37) : 1 champ octet
    PacketWriter w;
    w.WriteChar4LE(value);   // +9
    w.Finalize(0x37, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 4 = 13
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp63(NetClient& nc) {
    // [net] opcode 63 (0x3F) : sans payload
    PacketWriter w;
    w.Finalize(0x3F, DefaultRng(), nc.xorKey, nc.seq);  // len = 9
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp71(NetClient& nc) {
    // [net] opcode 71 (0x47) : sans payload
    PacketWriter w;
    w.Finalize(0x47, DefaultRng(), nc.xorKey, nc.seq);  // len = 9
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp79(NetClient& nc, int8_t subType, const void* payload100) {
    // [net] opcode 79 (0x4F) : builder partage -> sous-type octet (+9) + bloc 100 o (+13).
    // Utilise par Net_SendMenu_2 (sous-type 2). L'appelant fournit un tampon >= 100 o.
    PacketWriter w;
    w.WriteChar4LE(subType);        // +9
    w.WriteBytes(payload100, 100);  // +13
    w.Finalize(0x4F, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 4 + 100 = 113
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp87(NetClient& nc, int8_t f0, int8_t f1) {
    // [net] opcode 87 (0x57) : 2 champs octet
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.Finalize(0x57, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 8 = 17
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp96(NetClient& nc, int8_t f0, int8_t f1, int8_t f2) {
    // [net] opcode 96 (0x60) : 3 champs octet.
    // ATTENTION : l'original fixe len=23 alors qu'il n'ecrit que 3 champs (+9,+13,+17),
    // laissant 2 octets NON INITIALISES en +21/+22. On les reproduit en zeros pour
    // conserver la longueur de 23 octets attendue par le serveur.
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.WriteChar4LE(f2);   // +17
    w.WriteU16(0);        // +21 : bourrage (2 o)
    w.Finalize(0x60, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 12 + 2 = 23
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp103(NetClient& nc, int8_t f0, int8_t f1, int8_t f2) {
    // [net] opcode 103 (0x67) : 3 champs octet
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.WriteChar4LE(f2);   // +17
    w.Finalize(0x67, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 12 = 21
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp114(NetClient& nc, int8_t f0, int8_t f1, int8_t f2) {
    // [net] opcode 114 (0x72) : 3 champs octet
    PacketWriter w;
    w.WriteChar4LE(f0);   // +9
    w.WriteChar4LE(f1);   // +13
    w.WriteChar4LE(f2);   // +17
    w.Finalize(0x72, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 12 = 21
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp126(NetClient& nc, int8_t value) {
    // [net] opcode 126 (0x7E) : 1 champ octet
    PacketWriter w;
    w.WriteChar4LE(value);   // +9
    w.Finalize(0x7E, DefaultRng(), nc.xorKey, nc.seq);  // len = 9 + 4 = 13
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp135(NetClient& nc, int8_t f0, int8_t f1, int8_t f2, int8_t f3, int8_t f4, int8_t f5, int8_t f6, int8_t f7, int8_t f8) {
    // [net] opcode 135 (0x87, -121 signe) : 9 champs octet consecutifs
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

void Net_SendVaultReq_201(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // Wrapper : opcode FIL 19 (0x13), sous-type 201 (ramassage loot / depot entrepot).
    // Net_SendPacket_Op19 (defini dans un autre lot) ecrit le sous-type (+9) puis copie
    // 100 octets du bloc a +13. Les 7 champs occupent les 28 premiers octets du bloc.
    uint8_t payload[108] = {};                       // tampon partage (Op19 en copie 100)
    reinterpret_cast<int32_t*>(payload)[0] = a1;     // +0  (char promu sur 4 o LE)
    reinterpret_cast<int32_t*>(payload)[1] = a2;     // +4
    reinterpret_cast<int32_t*>(payload)[2] = a3;     // +8
    reinterpret_cast<int32_t*>(payload)[3] = a4;     // +12
    reinterpret_cast<int32_t*>(payload)[4] = a5;     // +16
    reinterpret_cast<int32_t*>(payload)[5] = a6;     // +20
    reinterpret_cast<int32_t*>(payload)[6] = a7;     // +24
    Net_SendPacket_Op19(nc, 201, payload);
}

void Net_SendVaultReq_209(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // Wrapper : opcode FIL 19 (0x13), sous-type 209 (requete entrepot). 7 champs octet.
    uint8_t payload[108] = {};
    reinterpret_cast<int32_t*>(payload)[0] = a1;
    reinterpret_cast<int32_t*>(payload)[1] = a2;
    reinterpret_cast<int32_t*>(payload)[2] = a3;
    reinterpret_cast<int32_t*>(payload)[3] = a4;
    reinterpret_cast<int32_t*>(payload)[4] = a5;
    reinterpret_cast<int32_t*>(payload)[5] = a6;
    reinterpret_cast<int32_t*>(payload)[6] = a7;
    Net_SendPacket_Op19(nc, 209, payload);
}

void Net_SendVaultReq_217(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // Wrapper : opcode FIL 19 (0x13), sous-type 217 (requete entrepot). 7 champs octet.
    uint8_t payload[108] = {};
    reinterpret_cast<int32_t*>(payload)[0] = a1;
    reinterpret_cast<int32_t*>(payload)[1] = a2;
    reinterpret_cast<int32_t*>(payload)[2] = a3;
    reinterpret_cast<int32_t*>(payload)[3] = a4;
    reinterpret_cast<int32_t*>(payload)[4] = a5;
    reinterpret_cast<int32_t*>(payload)[5] = a6;
    reinterpret_cast<int32_t*>(payload)[6] = a7;
    Net_SendPacket_Op19(nc, 217, payload);
}

void Net_SendVaultReq_225(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // Wrapper : opcode FIL 19 (0x13), sous-type 225 (requete entrepot). 7 champs octet.
    uint8_t payload[108] = {};
    reinterpret_cast<int32_t*>(payload)[0] = a1;
    reinterpret_cast<int32_t*>(payload)[1] = a2;
    reinterpret_cast<int32_t*>(payload)[2] = a3;
    reinterpret_cast<int32_t*>(payload)[3] = a4;
    reinterpret_cast<int32_t*>(payload)[4] = a5;
    reinterpret_cast<int32_t*>(payload)[5] = a6;
    reinterpret_cast<int32_t*>(payload)[6] = a7;
    Net_SendPacket_Op19(nc, 225, payload);
}

void Net_SendVaultReq_233(NetClient& nc, int8_t a1, int8_t a2) {
    // Wrapper : opcode FIL 19 (0x13), sous-type 233 (creation page entrepot). 2 champs octet.
    uint8_t payload[108] = {};
    reinterpret_cast<int32_t*>(payload)[0] = a1;   // +0
    reinterpret_cast<int32_t*>(payload)[1] = a2;   // +4 (tampon contigu dans l'original)
    Net_SendPacket_Op19(nc, 233, payload);
}

void Net_SendVaultReq_241(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // Wrapper : opcode FIL 19 (0x13), sous-type 241 (requete entrepot). 7 champs octet.
    uint8_t payload[108] = {};
    reinterpret_cast<int32_t*>(payload)[0] = a1;
    reinterpret_cast<int32_t*>(payload)[1] = a2;
    reinterpret_cast<int32_t*>(payload)[2] = a3;
    reinterpret_cast<int32_t*>(payload)[3] = a4;
    reinterpret_cast<int32_t*>(payload)[4] = a5;
    reinterpret_cast<int32_t*>(payload)[5] = a6;
    reinterpret_cast<int32_t*>(payload)[6] = a7;
    Net_SendPacket_Op19(nc, 241, payload);
}

void Net_SendVaultReq_249(NetClient& nc, int8_t a1, int8_t a2, int8_t a3, int8_t a4, int8_t a5, int8_t a6, int8_t a7) {
    // Wrapper : opcode FIL 19 (0x13), sous-type 249 (requete entrepot). 7 champs octet.
    uint8_t payload[108] = {};
    reinterpret_cast<int32_t*>(payload)[0] = a1;
    reinterpret_cast<int32_t*>(payload)[1] = a2;
    reinterpret_cast<int32_t*>(payload)[2] = a3;
    reinterpret_cast<int32_t*>(payload)[3] = a4;
    reinterpret_cast<int32_t*>(payload)[4] = a5;
    reinterpret_cast<int32_t*>(payload)[5] = a6;
    reinterpret_cast<int32_t*>(payload)[6] = a7;
    Net_SendPacket_Op19(nc, 249, payload);
}

void Net_SendVaultReq_245b(NetClient& nc) {
    // Wrapper : opcode FIL 19 (0x13), sous-type 245 (variante sans argument).
    // Le bloc de 100 o copie par Op19 n'est pas initialise dans l'original -> zeros ici.
    uint8_t payload[108] = {};
    Net_SendPacket_Op19(nc, 245, payload);
}

void Net_SendCmd_253(NetClient& nc) {
    // Wrapper : opcode FIL 19 (0x13), sous-type 253 (requete d'action sans argument).
    uint8_t payload[108] = {};
    Net_SendPacket_Op19(nc, 253, payload);
}

void Net_SendCmd_5(NetClient& nc, const void* payload13) {
    // Wrapper : opcode FIL 19 (0x13), sous-type 5. Payload = bloc de 13 octets.
    uint8_t payload[108] = {};
    std::memcpy(payload, payload13, 13);
    Net_SendPacket_Op19(nc, 5, payload);
}

void Net_SendCmd_13(NetClient& nc, int8_t value) {
    // Wrapper : opcode FIL 19 (0x13), sous-type 13. Payload = 1 champ octet.
    uint8_t payload[108] = {};
    reinterpret_cast<int32_t*>(payload)[0] = value;   // +0 (char promu sur 4 o LE)
    Net_SendPacket_Op19(nc, 13, payload);
}

void Net_SendGuarded_1(NetClient& nc, const void* payload13) {
    // Wrapper garde : opcode FIL 75 (0x4B), sous-type 1. Op75 copie un bloc de 500 o.
    // Payload = bloc de 13 octets. Latch anti-spam de commande GM.
    uint8_t payload[508] = {};
    std::memcpy(payload, payload13, 13);
    // n'envoie rien si un morph est en cours ou si le cooldown GM est deja arme
    if (g_MorphInProgress != 1 && !g_GmCmdCooldownLatch) {
        Net_SendOp75(nc, 1, payload);
        g_GmCmdCooldownLatch = 1;
        flt_1675B0C = g_GameTimeSec;
    }
}

void Net_SendGuarded_9(NetClient& nc, const void* payload13, int8_t value) {
    // Wrapper garde : opcode FIL 75 (0x4B), sous-type 9. Op75 copie un bloc de 500 o.
    // Payload = bloc de 13 octets (+0) suivi d'un champ octet (+13, NON aligne comme l'original).
    uint8_t payload[508] = {};
    std::memcpy(payload, payload13, 13);                              // +0..12
    reinterpret_cast<int32_t*>(payload + 13)[0] = value;             // +13 (char promu sur 4 o LE)
    if (g_MorphInProgress != 1 && !g_GmCmdCooldownLatch) {
        Net_SendOp75(nc, 9, payload);
        g_GmCmdCooldownLatch = 1;
        flt_1675B0C = g_GameTimeSec;
    }
}

void Net_SendMenu_2(NetClient& nc, const void* payload13) {
    // Wrapper garde : opcode FIL 79 (0x4F), sous-type 2. Op79 copie un bloc de 100 o.
    // Payload = bloc de 13 octets. Meme latch anti-spam que les commandes gardees.
    uint8_t payload[108] = {};
    std::memcpy(payload, payload13, 13);
    if (g_MorphInProgress != 1 && !g_GmCmdCooldownLatch) {
        Net_SendOp79(nc, 2, payload);
        g_GmCmdCooldownLatch = 1;
        flt_1675B0C = g_GameTimeSec;
    }
}

void Net_SendPacket_Op19(NetClient& nc, uint8_t subCmd, const void* payload) {
    // [net] Builder generique du canal 0x13 (coffre/commande).
    // Le 1er champ payload (u32) porte la sous-commande (202, 210, 6, 14, ...),
    // pousee en litteral entier -> etendue en zero sur 4 octets LE (PAS un char4LE).
    // Suivi d'un payload fixe de 100 octets. Longueur fil totale = 113 octets.
    PacketWriter w;
    w.WriteU32(subCmd);          // this+9 : sous-commande (u32 LE)
    w.WriteBytes(payload, 100);  // this+13 : 100 octets de payload
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op27(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3) {
    // [net] opcode 0x1B : quatre champs octet (chacun emis sur 4 octets LE).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.WriteChar4LE(arg1);  // this+13
    w.WriteChar4LE(arg2);  // this+17
    w.WriteChar4LE(arg3);  // this+21
    w.Finalize(0x1B, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendPacket_Op35(NetClient& nc, int8_t flag, const void* name13, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // [net] opcode 0x23 : octet + chaine/tampon 13 octets + 7 champs octet (4 octets LE chacun).
    PacketWriter w;
    w.WriteChar4LE(flag);      // this+9
    w.WriteBytes(name13, 13);  // this+13 : 13 octets
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

void Net_SendOp40(NetClient& nc, const void* payload61) {
    // [net] opcode 0x28 : tampon brut de 61 octets.
    PacketWriter w;
    w.WriteBytes(payload61, 61);  // this+9 : 61 octets
    w.Finalize(0x28, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp48(NetClient& nc) {
    // [net] opcode 0x30 : aucun payload (longueur fil = 9).
    PacketWriter w;
    w.Finalize(0x30, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp56(NetClient& nc, int8_t arg0) {
    // [net] opcode 0x38 : un champ octet (4 octets LE).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.Finalize(0x38, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp64(NetClient& nc) {
    // [net] opcode 0x40 : aucun payload (longueur fil = 9).
    PacketWriter w;
    w.Finalize(0x40, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp72(NetClient& nc, const void* payload13) {
    // [net] opcode 0x48 : tampon brut de 13 octets.
    PacketWriter w;
    w.WriteBytes(payload13, 13);  // this+9 : 13 octets
    w.Finalize(0x48, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp80(NetClient& nc, const void* chatMsg61) {
    // [net] opcode 0x50 : chat normal/local, tampon de 61 octets.
    PacketWriter w;
    w.WriteBytes(chatMsg61, 61);  // this+9 : 61 octets (message)
    w.Finalize(0x50, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp88(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6, int8_t arg7, int8_t arg8) {
    // [net] opcode 0x58 : neuf champs octet (4 octets LE chacun).
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
    // [net] opcode 0x61 : un champ octet (4 octets LE).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.Finalize(0x61, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp104(NetClient& nc, int8_t arg0, int8_t arg1) {
    // [net] opcode 0x68 : deux champs octet (4 octets LE chacun).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.WriteChar4LE(arg1);  // this+13
    w.Finalize(0x68, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp115(NetClient& nc, int8_t arg0) {
    // [net] opcode 0x73 : un champ octet (4 octets LE).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.Finalize(0x73, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp127(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3) {
    // [net] opcode 0x7F : quatre champs octet (4 octets LE chacun).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.WriteChar4LE(arg1);  // this+13
    w.WriteChar4LE(arg2);  // this+17
    w.WriteChar4LE(arg3);  // this+21
    w.Finalize(0x7F, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendOp137(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3) {
    // [net] opcode 0x89 (137). L'octet -119 ecrit a this+8 = 0x89 non signe.
    // Quatre champs octet (4 octets LE chacun).
    PacketWriter w;
    w.WriteChar4LE(arg0);  // this+9
    w.WriteChar4LE(arg1);  // this+13
    w.WriteChar4LE(arg2);  // this+17
    w.WriteChar4LE(arg3);  // this+21
    w.Finalize(0x89, DefaultRng(), nc.xorKey, nc.seq);
    NetSend(nc, w.Data(), (int)w.Size());
}

void Net_SendVaultReq_202(NetClient& nc, int8_t arg0, int8_t arg1) {
    // [net] Requete coffre sous-commande 202 sur le canal 0x13.
    // Construit un payload local de 100 octets (deux champs char4LE) puis le confie a Op19.
    uint8_t buf[100] = {0};
    int32_t f0 = arg0, f1 = arg1;    // char promu en int32 LE (extension de signe)
    std::memcpy(buf + 0, &f0, 4);    // champ 0
    std::memcpy(buf + 4, &f1, 4);    // champ 1
    Net_SendPacket_Op19(nc, 202, buf);
}

void Net_SendVaultReq_210(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // [net] Requete coffre sous-commande 210 : sept champs char4LE consecutifs.
    uint8_t buf[100] = {0};
    const int8_t vals[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    for (int i = 0; i < 7; ++i) {
        int32_t f = vals[i];               // char promu en int32 LE
        std::memcpy(buf + i * 4, &f, 4);   // champ i (offset 0,4,8,...,24)
    }
    Net_SendPacket_Op19(nc, 210, buf);
}

void Net_SendVaultReq_218(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // [net] Requete coffre sous-commande 218 : sept champs char4LE consecutifs.
    uint8_t buf[100] = {0};
    const int8_t vals[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    for (int i = 0; i < 7; ++i) {
        int32_t f = vals[i];
        std::memcpy(buf + i * 4, &f, 4);
    }
    Net_SendPacket_Op19(nc, 218, buf);
}

void Net_SendVaultReq_226(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // [net] Requete coffre sous-commande 226 : sept champs char4LE consecutifs.
    uint8_t buf[100] = {0};
    const int8_t vals[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    for (int i = 0; i < 7; ++i) {
        int32_t f = vals[i];
        std::memcpy(buf + i * 4, &f, 4);
    }
    Net_SendPacket_Op19(nc, 226, buf);
}

void Net_SendVaultReq_234(NetClient& nc, int8_t arg0) {
    // [net] Requete coffre sous-commande 234 : un seul champ char4LE.
    uint8_t buf[100] = {0};
    int32_t f = arg0;               // char promu en int32 LE
    std::memcpy(buf, &f, 4);        // champ 0
    Net_SendPacket_Op19(nc, 234, buf);
}

void Net_SendVaultReq_242(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // [net] Requete coffre sous-commande 242 : sept champs char4LE consecutifs.
    uint8_t buf[100] = {0};
    const int8_t vals[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    for (int i = 0; i < 7; ++i) {
        int32_t f = vals[i];
        std::memcpy(buf + i * 4, &f, 4);
    }
    Net_SendPacket_Op19(nc, 242, buf);
}

void Net_SendVaultReq_250(NetClient& nc, int8_t arg0, int8_t arg1, int8_t arg2, int8_t arg3, int8_t arg4, int8_t arg5, int8_t arg6) {
    // [net] Requete coffre sous-commande 250 : sept champs char4LE consecutifs.
    uint8_t buf[100] = {0};
    const int8_t vals[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    for (int i = 0; i < 7; ++i) {
        int32_t f = vals[i];
        std::memcpy(buf + i * 4, &f, 4);
    }
    Net_SendPacket_Op19(nc, 250, buf);
}

void Net_SendCmd_246(NetClient& nc) {
    // [net] Requete d'action sous-commande 246, sans champ (payload de 100 octets a zero).
    uint8_t buf[100] = {0};
    Net_SendPacket_Op19(nc, 246, buf);
}

void Net_SendCmd_254(NetClient& nc, int8_t arg0) {
    // [net] Requete d'action sous-commande 254 : un champ char4LE.
    uint8_t buf[100] = {0};
    int32_t f = arg0;               // char promu en int32 LE
    std::memcpy(buf, &f, 4);        // champ 0
    Net_SendPacket_Op19(nc, 254, buf);
}

void Net_SendCmd_6(NetClient& nc, const void* data13) {
    // [net] Requete d'action sous-commande 6 : 13 octets copies en tete du payload.
    uint8_t buf[100] = {0};
    std::memcpy(buf, data13, 13);   // 13 octets bruts
    Net_SendPacket_Op19(nc, 6, buf);
}

void Net_SendCmd_14(NetClient& nc) {
    // [net] Requete d'action sous-commande 14, sans champ (payload de 100 octets a zero).
    uint8_t buf[100] = {0};
    Net_SendPacket_Op19(nc, 14, buf);
}

void Net_SendGuarded_2(NetClient& nc, int32_t ctxId) {
    // [net] Commande guardee sous-commande 2 sur le builder Op75 (canal 0x4B).
    // Op75/Op79 et les globaux de garde sont declares ailleurs (etat jeu partage) :
    //   extern void Net_SendOp75(NetClient&, uint8_t subCmd, const void* payload);
    //   extern int g_MorphInProgress, g_GmCmdCooldownLatch, dword_1675B10;
    //   extern float flt_1675B0C, g_GameTimeSec;
    // Garde anti-spam : rien si morph en cours ou cooldown GM actif.
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    uint8_t buf[508] = {0};        // aucun champ ecrit : payload a zero
    Net_SendOp75(nc, 2, buf);      // sous-commande 2
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;   // horodatage du cooldown
    dword_1675B10 = ctxId;         // arg memorise pour usage ulterieur (NON transmis)
}

void Net_SendGuarded_10(NetClient& nc, const void* data13, const void* data5) {
    // [net] Commande guardee sous-commande 10 sur Op75 (canal 0x4B).
    // Payload = 13 octets (data13) suivis de 5 octets (data5).
    // Fidele a l'origine : les tampons sont construits AVANT la garde.
    uint8_t buf[508] = {0};
    std::memcpy(buf + 0, data13, 13);   // 13 octets
    std::memcpy(buf + 13, data5, 5);    // 5 octets suivants
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    Net_SendOp75(nc, 10, buf);          // sous-commande 10
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;        // horodatage du cooldown
}

void Net_SendMenu_3(NetClient& nc, const void* data13) {
    // [net] Commande menu guardee sous-commande 3 sur Op79 (canal 0x4F).
    // Payload = 13 octets. Fidele a l'origine : le tampon est construit AVANT la garde.
    //   extern void Net_SendOp79(NetClient&, uint8_t subCmd, const void* payload);
    uint8_t buf[108] = {0};
    std::memcpy(buf, data13, 13);       // 13 octets
    if (g_MorphInProgress == 1 || g_GmCmdCooldownLatch)
        return;
    Net_SendOp79(nc, 3, buf);           // sous-commande 3
    g_GmCmdCooldownLatch = 1;
    flt_1675B0C = g_GameTimeSec;        // horodatage du cooldown
}

} // namespace ts2::net

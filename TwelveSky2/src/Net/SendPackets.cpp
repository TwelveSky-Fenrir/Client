// Net/SendPackets.cpp — définitions des builders sortants (généré).
#include "Net/SendPackets.h"
#include <cstring>   // std::memcpy (Net_SendCmd_251, Net_SendUdpReport)

namespace ts2::net {

bool Net_SendPacket_Op12(NetClient& nc, const void* head128, const void* name13, const void* tail72) {
    // Net_SendPacket_Op12 0x4B43C0 — opcode 12 (0x0C) : 3 blocs bruts (128 + 13 + 72).
    //   *(this+8)=12                  EA 0x4b444e
    //   Crt_Memcpy(this+9,   a2, 80h) EA 0x4b4462  -> bloc de 128 octets
    //   Crt_Memcpy(this+137, a3, 0Dh) EA 0x4b447a  -> bloc de 13 octets
    //   Crt_Memcpy(this+150, a4, 48h) EA 0x4b4492  -> bloc de 72 octets
    //   *(this+15000)=222             EA 0x4b449d  -> longueur fil 9+213
    // RETOUR : le binaire renvoie 1 apres un send() integral (EA 0x4b4564) et 0 si send()
    // echoue avec une erreur != 10035/WSAEWOULDBLOCK, apres Net_CloseSocket (EA 0x4b4531/
    // 0x4b4538) ; 10035 n'est PAS un echec (le binaire re-boucle sur le fragment,
    // EA 0x4b452a). NetSend() reproduit exactement cette semantique -> on la propage.
    // L'appelant Scene_EnterWorldUpdate 0x52BFF0 en depend : `if (result)` EA 0x52c194
    // -> etat 3 (+ g_GuardAuthTokenPending=0 EA 0x52c1ca) ; sinon notice 67 (EA 0x52c1a2)
    // + etat 4 (EA 0x52c1b7). Volet appelant = Scene/SceneManager.cpp (hors de ce front).
    PacketWriter w;
    w.WriteBytes(head128, 128); // this+9  : bloc de 128 octets
    w.WriteBytes(name13, 13);   // this+137 : bloc de 13 octets
    w.WriteBytes(tail72, 72);   // this+150 : bloc de 72 octets
    w.Finalize(0x0C, DefaultRng(), nc.xorKey, nc.seq);
    return NetSend(nc, w.Data(), (int)w.Size()); // 1 = EA 0x4b4564 / 0 = EA 0x4b4538
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

void Net_SendVaultReq_203(NetClient& nc, int32_t arg1) {
    // Net_SendVaultReq_203 0x5902E0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590319) : opcode FIL 0x13,
    //   sous-code 203 = mov dword ptr [ebp+var_74], 0CBh (EA 0x5902f3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (203 & 0xFF = 203) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 203, block);   // EA 0x590319 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_211(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_211 0x590710 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5907b5) : opcode FIL 0x13,
    //   sous-code 211 = mov dword ptr [ebp+var_74], 0D3h (EA 0x590723) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (211 & 0xFF = 211) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 211, block);   // EA 0x5907b5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_219(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_219 0x590D10 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590db5) : opcode FIL 0x13,
    //   sous-code 219 = mov dword ptr [ebp+var_74], 0DBh (EA 0x590d23) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (219 & 0xFF = 219) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 219, block);   // EA 0x590db5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_227(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_227 0x591310 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5913b5) : opcode FIL 0x13,
    //   sous-code 227 = mov dword ptr [ebp+var_74], 0E3h (EA 0x591323) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (227 & 0xFF = 227) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 227, block);   // EA 0x5913b5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_235(NetClient& nc, int32_t arg1) {
    // Net_SendVaultReq_235 0x591840 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591879) : opcode FIL 0x13,
    //   sous-code 235 = mov dword ptr [ebp+var_74], 0EBh (EA 0x591853) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (235 & 0xFF = 235) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 235, block);   // EA 0x591879 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_243(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_243 0x591C50 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591cf5) : opcode FIL 0x13,
    //   sous-code 243 = mov dword ptr [ebp+var_74], 0F3h (EA 0x591c63) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (243 & 0xFF = 243) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 243, block);   // EA 0x591cf5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_251(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_251 0x592250 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5922f5) : opcode FIL 0x13,
    //   sous-code 251 = mov dword ptr [ebp+var_74], 0FBh (EA 0x592263) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (251 & 0xFF = 251) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 251, block);   // EA 0x5922f5 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_247(NetClient& nc, int32_t arg1, int32_t arg2) {
    // Net_SendCmd_247 0x592720 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x59276b) : opcode FIL 0x13,
    //   sous-code 503 = mov dword ptr [ebp+var_74], 1F7h (EA 0x592733) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (503 & 0xFF = 247) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { arg1, arg2 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..7 : Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 503, block);   // EA 0x59276b -> trame 9+4+100 = 113 o
}

void Net_SendCmd_255(NetClient& nc) {
    // Net_SendCmd_255 0x5929A0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5929c7) : opcode FIL 0x13,
    //   sous-code 511 = mov dword ptr [ebp+var_74], 1FFh (EA 0x5929b3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (511 & 0xFF = 255) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    // Bloc de 100 o NON initialise cote binaire (pile) -> zeros ici : on ne
    //   reproduit pas la fuite de pile.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 511, block);   // EA 0x5929c7 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_7(NetClient& nc, const void* name13) {
    // Net_SendCmd_7 0x592C00 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592c39) : opcode FIL 0x13,
    //   sous-code 519 = mov dword ptr [ebp+var_74], 207h (EA 0x592c13) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (519 & 0xFF = 7) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, name13, 13);      // bloc+0..12 : Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 519, block);   // EA 0x592c39 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_15(NetClient& nc, int32_t arg1) {
    // Net_SendCmd_15 0x592EA0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592ed9) : opcode FIL 0x13,
    //   sous-code 527 = mov dword ptr [ebp+var_74], 20Fh (EA 0x592eb3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (527 & 0xFF = 15) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 527, block);   // EA 0x592ed9 -> trame 9+4+100 = 113 o
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

void Net_SendVaultReq_204(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5) {
    // Net_SendVaultReq_204 0x590330 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5903b1) : opcode FIL 0x13,
    //   sous-code 204 = mov dword ptr [ebp+var_74], 0CCh (EA 0x590343) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (204 & 0xFF = 204) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[5] = { a1, a2, a3, a4, a5 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..19 : Crt_Memcpy(vN,&aN,4u) x5 (a1 a2 a3 a4 a5)
    Net_SendPacket_Op19(nc, 204, block);   // EA 0x5903b1 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_212(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_212 0x5907D0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590875) : opcode FIL 0x13,
    //   sous-code 212 = mov dword ptr [ebp+var_74], 0D4h (EA 0x5907e3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (212 & 0xFF = 212) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 212, block);   // EA 0x590875 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_220(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_220 0x590DD0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590e75) : opcode FIL 0x13,
    //   sous-code 220 = mov dword ptr [ebp+var_74], 0DCh (EA 0x590de3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (220 & 0xFF = 220) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 220, block);   // EA 0x590e75 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_228(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_228 0x5913D0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591475) : opcode FIL 0x13,
    //   sous-code 228 = mov dword ptr [ebp+var_74], 0E4h (EA 0x5913e3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (228 & 0xFF = 228) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 228, block);   // EA 0x591475 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_236(NetClient& nc) {
    // Net_SendVaultReq_236 0x591890 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5918b7) : opcode FIL 0x13,
    //   sous-code 236 = mov dword ptr [ebp+var_74], 0ECh (EA 0x5918a3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (236 & 0xFF = 236) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    // Bloc de 100 o NON initialise cote binaire (pile) -> zeros ici : on ne
    //   reproduit pas la fuite de pile.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 236, block);   // EA 0x5918b7 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_244(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_244 0x591D10 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591db5) : opcode FIL 0x13,
    //   sous-code 244 = mov dword ptr [ebp+var_74], 0F4h (EA 0x591d23) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (244 & 0xFF = 244) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 244, block);   // EA 0x591db5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_252(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_252 0x592310 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5923b5) : opcode FIL 0x13,
    //   sous-code 252 = mov dword ptr [ebp+var_74], 0FCh (EA 0x592323) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (252 & 0xFF = 252) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 252, block);   // EA 0x5923b5 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_248(NetClient& nc, int32_t a1) {
    // Net_SendCmd_248 0x592780 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5927b9) : opcode FIL 0x13,
    //   sous-code 504 = mov dword ptr [ebp+var_74], 1F8h (EA 0x592793) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (504 & 0xFF = 248) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { a1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 504, block);   // EA 0x5927b9 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_0(NetClient& nc) {
    // Net_SendCmd_0 0x5929E0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592a07) : opcode FIL 0x13,
    //   sous-code 512 = mov dword ptr [ebp+var_74], 200h (EA 0x5929f3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (512 & 0xFF = 0) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    // Bloc de 100 o NON initialise cote binaire (pile) -> zeros ici : on ne
    //   reproduit pas la fuite de pile.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 512, block);   // EA 0x592a07 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_8(NetClient& nc, int32_t a1, int32_t a2) {
    // Net_SendCmd_8 0x592C50 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592c9b) : opcode FIL 0x13,
    //   sous-code 520 = mov dword ptr [ebp+var_74], 208h (EA 0x592c63) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (520 & 0xFF = 8) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { a1, a2 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..7 : Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 520, block);   // EA 0x592c9b -> trame 9+4+100 = 113 o
}

void Net_SendCmd_16(NetClient& nc, const void* buf12) {
    // Net_SendCmd_16 0x592EF0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592f29) : opcode FIL 0x13,
    //   sous-code 528 = mov dword ptr [ebp+var_74], 210h (EA 0x592f03) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (528 & 0xFF = 16) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, buf12, 12);       // bloc+0..11 : Crt_Memcpy(v2,a1,0xCu)
    Net_SendPacket_Op19(nc, 528, block);   // EA 0x592f29 -> trame 9+4+100 = 113 o
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
    // Net_SendUdpReport 0x6D8173 : port = byte_1860E1C ? ntohs(Crt_Atoi(&byte_1860E1C))
    //   : ntohs(0x3A98) [15000]. ÉCART ASSUMÉ : la config ASCII byte_1860E1C n'est PAS
    //   relue ici (constante 15000 en dur) — fonction de la bande anticheat structurelle
    //   [0x6D7234, 0x6FD04C), exclue par consigne de projet (CLAUDE.md).
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

void Net_SendVaultReq_205(NetClient& nc, int32_t arg0, int32_t arg1) {
    // Net_SendVaultReq_205 0x5903D0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x59041b) : opcode FIL 0x13,
    //   sous-code 205 = mov dword ptr [ebp+var_74], 0CDh (EA 0x5903e3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (205 & 0xFF = 205) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { arg0, arg1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..7 : Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 205, block);   // EA 0x59041b -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_213(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_213 0x590890 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590935) : opcode FIL 0x13,
    //   sous-code 213 = mov dword ptr [ebp+var_74], 0D5h (EA 0x5908a3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (213 & 0xFF = 213) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 213, block);   // EA 0x590935 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_221(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_221 0x590E90 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590f35) : opcode FIL 0x13,
    //   sous-code 221 = mov dword ptr [ebp+var_74], 0DDh (EA 0x590ea3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (221 & 0xFF = 221) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 221, block);   // EA 0x590f35 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_229(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_229 0x591490 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591535) : opcode FIL 0x13,
    //   sous-code 229 = mov dword ptr [ebp+var_74], 0E5h (EA 0x5914a3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (229 & 0xFF = 229) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 229, block);   // EA 0x591535 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_237(NetClient& nc) {
    // Net_SendVaultReq_237 0x5918D0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5918f7) : opcode FIL 0x13,
    //   sous-code 237 = mov dword ptr [ebp+var_74], 0EDh (EA 0x5918e3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (237 & 0xFF = 237) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    // Bloc de 100 o NON initialise cote binaire (pile) -> zeros ici : on ne
    //   reproduit pas la fuite de pile.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 237, block);   // EA 0x5918f7 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_245(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_245 0x591DD0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591e75) : opcode FIL 0x13,
    //   sous-code 245 = mov dword ptr [ebp+var_74], 0F5h (EA 0x591de3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (245 & 0xFF = 245) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 245, block);   // EA 0x591e75 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_253(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_253 0x5923D0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592475) : opcode FIL 0x13,
    //   sous-code 253 = mov dword ptr [ebp+var_74], 0FDh (EA 0x5923e3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (253 & 0xFF = 253) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 253, block);   // EA 0x592475 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_249(NetClient& nc, int32_t arg0) {
    // Net_SendCmd_249 0x5927D0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592809) : opcode FIL 0x13,
    //   sous-code 505 = mov dword ptr [ebp+var_74], 1F9h (EA 0x5927e3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (505 & 0xFF = 249) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg0 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 505, block);   // EA 0x592809 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_1(NetClient& nc, const void* payload13) {
    // Net_SendCmd_1 0x592A20 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592a59) : opcode FIL 0x13,
    //   sous-code 513 = mov dword ptr [ebp+var_74], 201h (EA 0x592a33) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (513 & 0xFF = 1) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, payload13, 13);   // bloc+0..12 : Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 513, block);   // EA 0x592a59 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_9(NetClient& nc, int32_t arg0) {
    // Net_SendCmd_9 0x592CB0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592ce9) : opcode FIL 0x13,
    //   sous-code 521 = mov dword ptr [ebp+var_74], 209h (EA 0x592cc3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (521 & 0xFF = 9) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg0 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 521, block);   // EA 0x592ce9 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_17(NetClient& nc, int32_t arg0) {
    // Net_SendCmd_17 0x592F40 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592f79) : opcode FIL 0x13,
    //   sous-code 529 = mov dword ptr [ebp+var_74], 211h (EA 0x592f53) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (529 & 0xFF = 17) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg0 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 529, block);   // EA 0x592f79 -> trame 9+4+100 = 113 o
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

void Net_SendVaultReq_206(NetClient& nc, int32_t arg1) {
    // Net_SendVaultReq_206 0x590430 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590469) : opcode FIL 0x13,
    //   sous-code 206 = mov dword ptr [ebp+var_74], 0CEh (EA 0x590443) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (206 & 0xFF = 206) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 206, block);   // EA 0x590469 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_214(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_214 0x590950 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5909f5) : opcode FIL 0x13,
    //   sous-code 214 = mov dword ptr [ebp+var_74], 0D6h (EA 0x590963) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (214 & 0xFF = 214) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 214, block);   // EA 0x5909f5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_222(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_222 0x590F50 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590ff5) : opcode FIL 0x13,
    //   sous-code 222 = mov dword ptr [ebp+var_74], 0DEh (EA 0x590f63) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (222 & 0xFF = 222) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 222, block);   // EA 0x590ff5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_230(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_230 0x591550 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5915f5) : opcode FIL 0x13,
    //   sous-code 230 = mov dword ptr [ebp+var_74], 0E6h (EA 0x591563) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (230 & 0xFF = 230) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 230, block);   // EA 0x5915f5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_238(NetClient& nc) {
    // Net_SendVaultReq_238 0x591910 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591937) : opcode FIL 0x13,
    //   sous-code 238 = mov dword ptr [ebp+var_74], 0EEh (EA 0x591923) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (238 & 0xFF = 238) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    // Bloc de 100 o NON initialise cote binaire (pile) -> zeros ici : on ne
    //   reproduit pas la fuite de pile.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 238, block);   // EA 0x591937 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_246(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_246 0x591E90 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591f35) : opcode FIL 0x13,
    //   sous-code 246 = mov dword ptr [ebp+var_74], 0F6h (EA 0x591ea3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (246 & 0xFF = 246) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 246, block);   // EA 0x591f35 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_254(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_254 0x592490 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592523) : opcode FIL 0x13,
    //   sous-code 254 = mov dword ptr [ebp+var_74], 0FEh (EA 0x5924a3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (254 & 0xFF = 254) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[6] = { arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..23 : Crt_Memcpy(vN,&aN,4u) x6 (a1 a2 a3 a4 a5 a6)
    Net_SendPacket_Op19(nc, 254, block);   // EA 0x592523 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_250(NetClient& nc, int32_t arg1) {
    // Net_SendCmd_250 0x592820 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592859) : opcode FIL 0x13,
    //   sous-code 506 = mov dword ptr [ebp+var_74], 1FAh (EA 0x592833) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (506 & 0xFF = 250) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 506, block);   // EA 0x592859 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_2(NetClient& nc, const void* data13) {
    // Net_SendCmd_2 0x592A70 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592aa9) : opcode FIL 0x13,
    //   sous-code 514 = mov dword ptr [ebp+var_74], 202h (EA 0x592a83) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (514 & 0xFF = 2) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, data13, 13);      // bloc+0..12 : Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 514, block);   // EA 0x592aa9 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_10(NetClient& nc, int32_t arg1, int32_t arg2) {
    // Net_SendCmd_10 0x592D00 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592d4b) : opcode FIL 0x13,
    //   sous-code 522 = mov dword ptr [ebp+var_74], 20Ah (EA 0x592d13) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (522 & 0xFF = 10) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { arg1, arg2 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..7 : Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 522, block);   // EA 0x592d4b -> trame 9+4+100 = 113 o
}

void Net_SendCmd_18(NetClient& nc, int32_t arg1) {
    // Net_SendCmd_18 0x592F90 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592fc9) : opcode FIL 0x13,
    //   sous-code 530 = mov dword ptr [ebp+var_74], 212h (EA 0x592fa3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (530 & 0xFF = 18) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 530, block);   // EA 0x592fc9 -> trame 9+4+100 = 113 o
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

void Net_SendVaultReq_207(NetClient& nc, int32_t amount) {
    // Net_SendVaultReq_207 0x590480 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5904b9) : opcode FIL 0x13,
    //   sous-code 207 = mov dword ptr [ebp+var_74], 0CFh (EA 0x590493) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (207 & 0xFF = 207) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { amount };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 207, block);   // EA 0x5904b9 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_215(NetClient& nc, int32_t npcId, int32_t itemId, int32_t qty, int32_t page, int32_t slot, int32_t col, int32_t row) {
    // Net_SendVaultReq_215 0x590A10 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590ab5) : opcode FIL 0x13,
    //   sous-code 215 = mov dword ptr [ebp+var_74], 0D7h (EA 0x590a23) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (215 & 0xFF = 215) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { npcId, itemId, qty, page, slot, col, row };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 215, block);   // EA 0x590ab5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_223(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_223 0x591010 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5910b5) : opcode FIL 0x13,
    //   sous-code 223 = mov dword ptr [ebp+var_74], 0DFh (EA 0x591023) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (223 & 0xFF = 223) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 223, block);   // EA 0x5910b5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_231(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_231 0x591610 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5916b5) : opcode FIL 0x13,
    //   sous-code 231 = mov dword ptr [ebp+var_74], 0E7h (EA 0x591623) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (231 & 0xFF = 231) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 231, block);   // EA 0x5916b5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_239(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_239 0x591950 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5919f5) : opcode FIL 0x13,
    //   sous-code 239 = mov dword ptr [ebp+var_74], 0EFh (EA 0x591963) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (239 & 0xFF = 239) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 239, block);   // EA 0x5919f5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_247(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_247 0x591F50 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591ff5) : opcode FIL 0x13,
    //   sous-code 247 = mov dword ptr [ebp+var_74], 0F7h (EA 0x591f63) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (247 & 0xFF = 247) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 247, block);   // EA 0x591ff5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_255(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6) {
    // Net_SendVaultReq_255 0x592540 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5925d3) : opcode FIL 0x13,
    //   sous-code 255 = mov dword ptr [ebp+var_74], 0FFh (EA 0x592553) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (255 & 0xFF = 255) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[6] = { a1, a2, a3, a4, a5, a6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..23 : Crt_Memcpy(vN,&aN,4u) x6 (a1 a2 a3 a4 a5 a6)
    Net_SendPacket_Op19(nc, 255, block);   // EA 0x5925d3 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_251(NetClient& nc, const void* data) {
    // Net_SendCmd_251 0x592870 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5928a9) : opcode FIL 0x13,
    //   sous-code 507 = mov dword ptr [ebp+var_74], 1FBh (EA 0x592883) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (507 & 0xFF = 251) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, data, 12);        // bloc+0..11 : Crt_Memcpy(v2,a1,0xCu)
    Net_SendPacket_Op19(nc, 507, block);   // EA 0x5928a9 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_3(NetClient& nc, const void* data) {
    // Net_SendCmd_3 0x592AC0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592af9) : opcode FIL 0x13,
    //   sous-code 515 = mov dword ptr [ebp+var_74], 203h (EA 0x592ad3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (515 & 0xFF = 3) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, data, 13);        // bloc+0..12 : Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 515, block);   // EA 0x592af9 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_11(NetClient& nc, int32_t a, int32_t b) {
    // Net_SendCmd_11 0x592D60 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592dab) : opcode FIL 0x13,
    //   sous-code 523 = mov dword ptr [ebp+var_74], 20Bh (EA 0x592d73) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (523 & 0xFF = 11) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { a, b };
    std::memcpy(block, f, sizeof(f));  // bloc+0..7 : Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 523, block);   // EA 0x592dab -> trame 9+4+100 = 113 o
}

void Net_SendCmd_19(NetClient& nc, int32_t a) {
    // Net_SendCmd_19 0x592FE0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x593019) : opcode FIL 0x13,
    //   sous-code 531 = mov dword ptr [ebp+var_74], 213h (EA 0x592ff3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (531 & 0xFF = 19) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { a };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 531, block);   // EA 0x593019 -> trame 9+4+100 = 113 o
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

void Net_SendVaultReq_208(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_208 0x5904D0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590575) : opcode FIL 0x13,
    //   sous-code 208 = mov dword ptr [ebp+var_74], 0D0h (EA 0x5904e3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (208 & 0xFF = 208) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 208, block);   // EA 0x590575 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_216(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_216 0x590AD0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590b75) : opcode FIL 0x13,
    //   sous-code 216 = mov dword ptr [ebp+var_74], 0D8h (EA 0x590ae3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (216 & 0xFF = 216) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 216, block);   // EA 0x590b75 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_224(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_224 0x5910D0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591175) : opcode FIL 0x13,
    //   sous-code 224 = mov dword ptr [ebp+var_74], 0E0h (EA 0x5910e3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (224 & 0xFF = 224) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 224, block);   // EA 0x591175 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_232(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_232 0x5916D0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591775) : opcode FIL 0x13,
    //   sous-code 232 = mov dword ptr [ebp+var_74], 0E8h (EA 0x5916e3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (232 & 0xFF = 232) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 232, block);   // EA 0x591775 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_240(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_240 0x591A10 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591ab5) : opcode FIL 0x13,
    //   sous-code 240 = mov dword ptr [ebp+var_74], 0F0h (EA 0x591a23) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (240 & 0xFF = 240) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 240, block);   // EA 0x591ab5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_248(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_248 0x592010 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5920b5) : opcode FIL 0x13,
    //   sous-code 248 = mov dword ptr [ebp+var_74], 0F8h (EA 0x592023) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (248 & 0xFF = 248) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 248, block);   // EA 0x5920b5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_0(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6) {
    // Net_SendVaultReq_0 0x5925F0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592683) : opcode FIL 0x13,
    //   sous-code 256 = mov dword ptr [ebp+var_74], 100h (EA 0x592603) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (256 & 0xFF = 0) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[6] = { p1, p2, p3, p4, p5, p6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..23 : Crt_Memcpy(vN,&aN,4u) x6 (a1 a2 a3 a4 a5 a6)
    Net_SendPacket_Op19(nc, 256, block);   // EA 0x592683 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_252(NetClient& nc, int32_t a1) {
    // Net_SendCmd_252 0x5928C0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5928f9) : opcode FIL 0x13,
    //   sous-code 508 = mov dword ptr [ebp+var_74], 1FCh (EA 0x5928d3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (508 & 0xFF = 252) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { a1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 508, block);   // EA 0x5928f9 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_4(NetClient& nc, const void* payload13) {
    // Net_SendCmd_4 0x592B10 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592b49) : opcode FIL 0x13,
    //   sous-code 516 = mov dword ptr [ebp+var_74], 204h (EA 0x592b23) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (516 & 0xFF = 4) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, payload13, 13);   // bloc+0..12 : Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 516, block);   // EA 0x592b49 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_12(NetClient& nc, int32_t a1) {
    // Net_SendCmd_12 0x592DC0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592df9) : opcode FIL 0x13,
    //   sous-code 524 = mov dword ptr [ebp+var_74], 20Ch (EA 0x592dd3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (524 & 0xFF = 12) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { a1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 524, block);   // EA 0x592df9 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_22(NetClient& nc, int32_t a1) {
    // Net_SendCmd_22 0x593030 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x593069) : opcode FIL 0x13,
    //   sous-code 534 = mov dword ptr [ebp+var_74], 216h (EA 0x593043) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (534 & 0xFF = 22) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { a1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 534, block);   // EA 0x593069 -> trame 9+4+100 = 113 o
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

void Net_SendPacket_Op26(NetClient& nc, int32_t f0, int32_t f1, int32_t f2, int32_t f3, int32_t f4) {
    // Net_SendPacket_Op26 0x4B59C0 : opcode 26 (0x1A), 5 champs int32 LE, longueur 29.
    //   *(this+8)=26 (EA 0x4b5a4e) ; Crt_Memcpy(this+9,&a2,4u)  EA 0x4b5a5f
    //   Crt_Memcpy(this+13,&a3,4u) EA 0x4b5a74 ; Crt_Memcpy(this+17,&a4,4u) EA 0x4b5a89
    //   Crt_Memcpy(this+21,&a5,4u) EA 0x4b5a9e ; Crt_Memcpy(this+25,&a6,4u) EA 0x4b5ab3
    //   *(this+15000)=29 (EA 0x4b5abe).
    // Les 5 emplacements sont memcpy'es sur 4 OCTETS : ce sont des int32, pas des char.
    // Le `char a6` du prototype Hex-Rays est un artefact de dimensionnement du DERNIER
    // argument __stdcall (rien ne borne la taille de son emplacement de pile) : les
    // appelants y poussent des DWORD pleins — UI_MsgBox_OnLButtonUp 0x5C0A90 @0x5c25d0
    // passe dword_1839288/dword_1822EF4, UI_Enchant_Click 0x5FBA20 @0x5fbf46 passe
    // *(this+22768)/*(this+11). D'ou int32_t + WriteI32 sur les 5 champs.
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

void Net_SendVaultReq_201(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_201 0x5901C0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590265) : opcode FIL 0x13,
    //   sous-code 201 = mov dword ptr [ebp+var_74], 0C9h (EA 0x5901d3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (201 & 0xFF = 201) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 201, block);   // EA 0x590265 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_209(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_209 0x590590 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590635) : opcode FIL 0x13,
    //   sous-code 209 = mov dword ptr [ebp+var_74], 0D1h (EA 0x5905a3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (209 & 0xFF = 209) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 209, block);   // EA 0x590635 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_217(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_217 0x590B90 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590c35) : opcode FIL 0x13,
    //   sous-code 217 = mov dword ptr [ebp+var_74], 0D9h (EA 0x590ba3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (217 & 0xFF = 217) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 217, block);   // EA 0x590c35 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_225(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_225 0x591190 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591235) : opcode FIL 0x13,
    //   sous-code 225 = mov dword ptr [ebp+var_74], 0E1h (EA 0x5911a3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (225 & 0xFF = 225) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 225, block);   // EA 0x591235 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_233(NetClient& nc, int32_t a1, int32_t a2) {
    // Net_SendVaultReq_233 0x591790 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5917db) : opcode FIL 0x13,
    //   sous-code 233 = mov dword ptr [ebp+var_74], 0E9h (EA 0x5917a3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (233 & 0xFF = 233) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { a1, a2 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..7 : Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 233, block);   // EA 0x5917db -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_241(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_241 0x591AD0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591b75) : opcode FIL 0x13,
    //   sous-code 241 = mov dword ptr [ebp+var_74], 0F1h (EA 0x591ae3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (241 & 0xFF = 241) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 241, block);   // EA 0x591b75 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_249(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_249 0x5920D0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592175) : opcode FIL 0x13,
    //   sous-code 249 = mov dword ptr [ebp+var_74], 0F9h (EA 0x5920e3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (249 & 0xFF = 249) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 249, block);   // EA 0x592175 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_245b(NetClient& nc) {
    // Net_SendVaultReq_245b 0x5926A0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5926c7) : opcode FIL 0x13,
    //   sous-code 501 = mov dword ptr [ebp+var_74], 1F5h (EA 0x5926b3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (501 & 0xFF = 245) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    // Bloc de 100 o NON initialise cote binaire (pile) -> zeros ici : on ne
    //   reproduit pas la fuite de pile.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 501, block);   // EA 0x5926c7 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_253(NetClient& nc) {
    // Net_SendCmd_253 0x592910 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592937) : opcode FIL 0x13,
    //   sous-code 509 = mov dword ptr [ebp+var_74], 1FDh (EA 0x592923) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (509 & 0xFF = 253) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    // Bloc de 100 o NON initialise cote binaire (pile) -> zeros ici : on ne
    //   reproduit pas la fuite de pile.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 509, block);   // EA 0x592937 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_5(NetClient& nc, const void* payload13) {
    // Net_SendCmd_5 0x592B60 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592b99) : opcode FIL 0x13,
    //   sous-code 517 = mov dword ptr [ebp+var_74], 205h (EA 0x592b73) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (517 & 0xFF = 5) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, payload13, 13);   // bloc+0..12 : Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 517, block);   // EA 0x592b99 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_13(NetClient& nc, int32_t value) {
    // Net_SendCmd_13 0x592E10 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592e49) : opcode FIL 0x13,
    //   sous-code 525 = mov dword ptr [ebp+var_74], 20Dh (EA 0x592e23) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (525 & 0xFF = 13) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { value };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 525, block);   // EA 0x592e49 -> trame 9+4+100 = 113 o
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

void Net_SendPacket_Op19(NetClient& nc, uint32_t subCmd, const void* payload) {
    // Net_SendPacket_Op19 0x4B4E70 — builder UNIQUE du canal 0x13 (coffre/commande).
    // Signature IDA : int __thiscall Net_SendPacket_Op19(unsigned int this, int a2, _BYTE *a3).
    //   *(this+8) = 19               EA 0x4b4efe  -> opcode fil 0x13
    //   Crt_Memcpy(this+9, &a2, 4u)  EA 0x4b4f0f  -> sous-code INT32 LE @+9 (a2 = int)
    //   Crt_Memcpy(this+13, a3, 64h) EA 0x4b4f24  -> bloc de 100 octets @+13
    //   *(this+15000) = 113          EA 0x4b4f2f  -> longueur fil 9+4+100
    // subCmd est un uint32_t et NON un uint8_t : les 88 enveloppes appelantes couvrent
    // 201..256 ET 501..531/534 (verifie par xrefs_to 0x4B4E70, 1 site chacune, toutes a
    // immediat constant `mov dword ptr [ebp+var_74], imm32` ZERO-etendu). Un uint8_t
    // tronquerait mod 256 (501->245, 534->22, 256->0) — c'est l'origine historique du
    // mauvais nommage IDA : le suffixe des noms Net_SendCmd_*/Net_SendVaultReq_* vaut
    // (sous-code & 0xFF), PAS le sous-code. Ne JAMAIS deduire le sous-code d'un nom.
    PacketWriter w;
    w.WriteU32(subCmd);          // this+9  : sous-code (u32 LE, zero-etendu) EA 0x4b4f0f
    w.WriteBytes(payload, 100);  // this+13 : bloc de 100 octets              EA 0x4b4f24
    w.Finalize(0x13, DefaultRng(), nc.xorKey, nc.seq); // opcode 0x13, longueur 113
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

void Net_SendVaultReq_202(NetClient& nc, int32_t arg0, int32_t arg1) {
    // Net_SendVaultReq_202 0x590280 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5902cb) : opcode FIL 0x13,
    //   sous-code 202 = mov dword ptr [ebp+var_74], 0CAh (EA 0x590293) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (202 & 0xFF = 202) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { arg0, arg1 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..7 : Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 202, block);   // EA 0x5902cb -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_210(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_210 0x590650 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5906f5) : opcode FIL 0x13,
    //   sous-code 210 = mov dword ptr [ebp+var_74], 0D2h (EA 0x590663) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (210 & 0xFF = 210) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 210, block);   // EA 0x5906f5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_218(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_218 0x590C50 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x590cf5) : opcode FIL 0x13,
    //   sous-code 218 = mov dword ptr [ebp+var_74], 0DAh (EA 0x590c63) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (218 & 0xFF = 218) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 218, block);   // EA 0x590cf5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_226(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_226 0x591250 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x5912f5) : opcode FIL 0x13,
    //   sous-code 226 = mov dword ptr [ebp+var_74], 0E2h (EA 0x591263) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (226 & 0xFF = 226) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 226, block);   // EA 0x5912f5 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_234(NetClient& nc, int32_t arg0) {
    // Net_SendVaultReq_234 0x5917F0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591829) : opcode FIL 0x13,
    //   sous-code 234 = mov dword ptr [ebp+var_74], 0EAh (EA 0x591803) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (234 & 0xFF = 234) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg0 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 234, block);   // EA 0x591829 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_242(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_242 0x591B90 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x591c35) : opcode FIL 0x13,
    //   sous-code 242 = mov dword ptr [ebp+var_74], 0F2h (EA 0x591ba3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (242 & 0xFF = 242) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 242, block);   // EA 0x591c35 -> trame 9+4+100 = 113 o
}

void Net_SendVaultReq_250(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_250 0x592190 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592235) : opcode FIL 0x13,
    //   sous-code 250 = mov dword ptr [ebp+var_74], 0FAh (EA 0x5921a3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (250 & 0xFF = 250) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..27 : Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 250, block);   // EA 0x592235 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_246(NetClient& nc) {
    // Net_SendCmd_246 0x5926E0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592707) : opcode FIL 0x13,
    //   sous-code 502 = mov dword ptr [ebp+var_74], 1F6h (EA 0x5926f3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (502 & 0xFF = 246) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    // Bloc de 100 o NON initialise cote binaire (pile) -> zeros ici : on ne
    //   reproduit pas la fuite de pile.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 502, block);   // EA 0x592707 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_254(NetClient& nc, int32_t arg0) {
    // Net_SendCmd_254 0x592950 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592989) : opcode FIL 0x13,
    //   sous-code 510 = mov dword ptr [ebp+var_74], 1FEh (EA 0x592963) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (510 & 0xFF = 254) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg0 };
    std::memcpy(block, f, sizeof(f));  // bloc+0..3 : Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 510, block);   // EA 0x592989 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_6(NetClient& nc, const void* data13) {
    // Net_SendCmd_6 0x592BB0 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592be9) : opcode FIL 0x13,
    //   sous-code 518 = mov dword ptr [ebp+var_74], 206h (EA 0x592bc3) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (518 & 0xFF = 6) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    std::memcpy(block, data13, 13);      // bloc+0..12 : Crt_Memcpy(v2,a1,0xDu)
    Net_SendPacket_Op19(nc, 518, block);   // EA 0x592be9 -> trame 9+4+100 = 113 o
}

void Net_SendCmd_14(NetClient& nc) {
    // Net_SendCmd_14 0x592E60 -> Net_SendPacket_Op19 0x4B4E70 (appel EA 0x592e87) : opcode FIL 0x13,
    //   sous-code 526 = mov dword ptr [ebp+var_74], 20Eh (EA 0x592e73) -> int32 ZERO-etendu.
    // PIEGE : le suffixe du nom vaut sous-code & 0xFF (526 & 0xFF = 14) — ne JAMAIS
    //   deduire le sous-code du nom, il vient du `mov` ci-dessus.
    // Bloc de 100 o NON initialise cote binaire (pile) -> zeros ici : on ne
    //   reproduit pas la fuite de pile.
    uint8_t block[100] = {0};   // bloc copie par Op19 a +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 526, block);   // EA 0x592e87 -> trame 9+4+100 = 113 o
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

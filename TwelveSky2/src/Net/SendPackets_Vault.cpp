// Net/SendPackets_Vault.cpp — outbound builder definitions: Net_SendVaultReq_* family
// (split from SendPackets.cpp; see SendPackets.h for the shared declarations).
#include "Net/SendPackets.h"
#include <cstring>   // std::memcpy (Net_SendCmd_251, Net_SendUdpReport)

namespace ts2::net {

void Net_SendVaultReq_203(NetClient& nc, int32_t arg1) {
    // Net_SendVaultReq_203 0x5902E0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590319): wire opcode 0x13,
    //   sub-code 203 = mov dword ptr [ebp+var_74], 0CBh (EA 0x5902f3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (203 & 0xFF = 203) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 203, block);   // EA 0x590319 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_211(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_211 0x590710 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5907b5): wire opcode 0x13,
    //   sub-code 211 = mov dword ptr [ebp+var_74], 0D3h (EA 0x590723) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (211 & 0xFF = 211) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 211, block);   // EA 0x5907b5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_219(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_219 0x590D10 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590db5): wire opcode 0x13,
    //   sub-code 219 = mov dword ptr [ebp+var_74], 0DBh (EA 0x590d23) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (219 & 0xFF = 219) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 219, block);   // EA 0x590db5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_227(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_227 0x591310 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5913b5): wire opcode 0x13,
    //   sub-code 227 = mov dword ptr [ebp+var_74], 0E3h (EA 0x591323) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (227 & 0xFF = 227) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 227, block);   // EA 0x5913b5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_235(NetClient& nc, int32_t arg1) {
    // Net_SendVaultReq_235 0x591840 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591879): wire opcode 0x13,
    //   sub-code 235 = mov dword ptr [ebp+var_74], 0EBh (EA 0x591853) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (235 & 0xFF = 235) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 235, block);   // EA 0x591879 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_243(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_243 0x591C50 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591cf5): wire opcode 0x13,
    //   sub-code 243 = mov dword ptr [ebp+var_74], 0F3h (EA 0x591c63) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (243 & 0xFF = 243) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 243, block);   // EA 0x591cf5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_251(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_251 0x592250 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5922f5): wire opcode 0x13,
    //   sub-code 251 = mov dword ptr [ebp+var_74], 0FBh (EA 0x592263) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (251 & 0xFF = 251) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 251, block);   // EA 0x5922f5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_204(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5) {
    // Net_SendVaultReq_204 0x590330 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5903b1): wire opcode 0x13,
    //   sub-code 204 = mov dword ptr [ebp+var_74], 0CCh (EA 0x590343) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (204 & 0xFF = 204) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[5] = { a1, a2, a3, a4, a5 };
    std::memcpy(block, f, sizeof(f));  // block+0..19: Crt_Memcpy(vN,&aN,4u) x5 (a1 a2 a3 a4 a5)
    Net_SendPacket_Op19(nc, 204, block);   // EA 0x5903b1 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_212(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_212 0x5907D0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590875): wire opcode 0x13,
    //   sub-code 212 = mov dword ptr [ebp+var_74], 0D4h (EA 0x5907e3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (212 & 0xFF = 212) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 212, block);   // EA 0x590875 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_220(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_220 0x590DD0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590e75): wire opcode 0x13,
    //   sub-code 220 = mov dword ptr [ebp+var_74], 0DCh (EA 0x590de3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (220 & 0xFF = 220) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 220, block);   // EA 0x590e75 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_228(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_228 0x5913D0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591475): wire opcode 0x13,
    //   sub-code 228 = mov dword ptr [ebp+var_74], 0E4h (EA 0x5913e3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (228 & 0xFF = 228) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 228, block);   // EA 0x591475 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_236(NetClient& nc) {
    // Net_SendVaultReq_236 0x591890 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5918b7): wire opcode 0x13,
    //   sub-code 236 = mov dword ptr [ebp+var_74], 0ECh (EA 0x5918a3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (236 & 0xFF = 236) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    // 100-byte block NOT initialized on the binary side (stack) -> zeroed here: we do not
    //   reproduce the stack leak.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 236, block);   // EA 0x5918b7 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_244(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_244 0x591D10 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591db5): wire opcode 0x13,
    //   sub-code 244 = mov dword ptr [ebp+var_74], 0F4h (EA 0x591d23) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (244 & 0xFF = 244) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 244, block);   // EA 0x591db5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_252(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_252 0x592310 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5923b5): wire opcode 0x13,
    //   sub-code 252 = mov dword ptr [ebp+var_74], 0FCh (EA 0x592323) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (252 & 0xFF = 252) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 252, block);   // EA 0x5923b5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_205(NetClient& nc, int32_t arg0, int32_t arg1) {
    // Net_SendVaultReq_205 0x5903D0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x59041b): wire opcode 0x13,
    //   sub-code 205 = mov dword ptr [ebp+var_74], 0CDh (EA 0x5903e3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (205 & 0xFF = 205) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { arg0, arg1 };
    std::memcpy(block, f, sizeof(f));  // block+0..7: Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 205, block);   // EA 0x59041b -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_213(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_213 0x590890 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590935): wire opcode 0x13,
    //   sub-code 213 = mov dword ptr [ebp+var_74], 0D5h (EA 0x5908a3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (213 & 0xFF = 213) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 213, block);   // EA 0x590935 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_221(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_221 0x590E90 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590f35): wire opcode 0x13,
    //   sub-code 221 = mov dword ptr [ebp+var_74], 0DDh (EA 0x590ea3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (221 & 0xFF = 221) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 221, block);   // EA 0x590f35 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_229(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_229 0x591490 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591535): wire opcode 0x13,
    //   sub-code 229 = mov dword ptr [ebp+var_74], 0E5h (EA 0x5914a3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (229 & 0xFF = 229) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 229, block);   // EA 0x591535 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_237(NetClient& nc) {
    // Net_SendVaultReq_237 0x5918D0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5918f7): wire opcode 0x13,
    //   sub-code 237 = mov dword ptr [ebp+var_74], 0EDh (EA 0x5918e3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (237 & 0xFF = 237) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    // 100-byte block NOT initialized on the binary side (stack) -> zeroed here: we do not
    //   reproduce the stack leak.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 237, block);   // EA 0x5918f7 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_245(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_245 0x591DD0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591e75): wire opcode 0x13,
    //   sub-code 245 = mov dword ptr [ebp+var_74], 0F5h (EA 0x591de3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (245 & 0xFF = 245) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 245, block);   // EA 0x591e75 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_253(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_253 0x5923D0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592475): wire opcode 0x13,
    //   sub-code 253 = mov dword ptr [ebp+var_74], 0FDh (EA 0x5923e3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (253 & 0xFF = 253) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 253, block);   // EA 0x592475 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_206(NetClient& nc, int32_t arg1) {
    // Net_SendVaultReq_206 0x590430 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590469): wire opcode 0x13,
    //   sub-code 206 = mov dword ptr [ebp+var_74], 0CEh (EA 0x590443) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (206 & 0xFF = 206) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg1 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 206, block);   // EA 0x590469 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_214(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_214 0x590950 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5909f5): wire opcode 0x13,
    //   sub-code 214 = mov dword ptr [ebp+var_74], 0D6h (EA 0x590963) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (214 & 0xFF = 214) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 214, block);   // EA 0x5909f5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_222(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_222 0x590F50 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590ff5): wire opcode 0x13,
    //   sub-code 222 = mov dword ptr [ebp+var_74], 0DEh (EA 0x590f63) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (222 & 0xFF = 222) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 222, block);   // EA 0x590ff5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_230(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_230 0x591550 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5915f5): wire opcode 0x13,
    //   sub-code 230 = mov dword ptr [ebp+var_74], 0E6h (EA 0x591563) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (230 & 0xFF = 230) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 230, block);   // EA 0x5915f5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_238(NetClient& nc) {
    // Net_SendVaultReq_238 0x591910 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591937): wire opcode 0x13,
    //   sub-code 238 = mov dword ptr [ebp+var_74], 0EEh (EA 0x591923) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (238 & 0xFF = 238) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    // 100-byte block NOT initialized on the binary side (stack) -> zeroed here: we do not
    //   reproduce the stack leak.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 238, block);   // EA 0x591937 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_246(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6, int32_t arg7) {
    // Net_SendVaultReq_246 0x591E90 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591f35): wire opcode 0x13,
    //   sub-code 246 = mov dword ptr [ebp+var_74], 0F6h (EA 0x591ea3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (246 & 0xFF = 246) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg1, arg2, arg3, arg4, arg5, arg6, arg7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 246, block);   // EA 0x591f35 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_254(NetClient& nc, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_254 0x592490 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592523): wire opcode 0x13,
    //   sub-code 254 = mov dword ptr [ebp+var_74], 0FEh (EA 0x5924a3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (254 & 0xFF = 254) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[6] = { arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // block+0..23: Crt_Memcpy(vN,&aN,4u) x6 (a1 a2 a3 a4 a5 a6)
    Net_SendPacket_Op19(nc, 254, block);   // EA 0x592523 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_207(NetClient& nc, int32_t amount) {
    // Net_SendVaultReq_207 0x590480 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5904b9): wire opcode 0x13,
    //   sub-code 207 = mov dword ptr [ebp+var_74], 0CFh (EA 0x590493) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (207 & 0xFF = 207) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { amount };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 207, block);   // EA 0x5904b9 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_215(NetClient& nc, int32_t npcId, int32_t itemId, int32_t qty, int32_t page, int32_t slot, int32_t col, int32_t row) {
    // Net_SendVaultReq_215 0x590A10 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590ab5): wire opcode 0x13,
    //   sub-code 215 = mov dword ptr [ebp+var_74], 0D7h (EA 0x590a23) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (215 & 0xFF = 215) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { npcId, itemId, qty, page, slot, col, row };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 215, block);   // EA 0x590ab5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_223(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_223 0x591010 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5910b5): wire opcode 0x13,
    //   sub-code 223 = mov dword ptr [ebp+var_74], 0DFh (EA 0x591023) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (223 & 0xFF = 223) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 223, block);   // EA 0x5910b5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_231(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_231 0x591610 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5916b5): wire opcode 0x13,
    //   sub-code 231 = mov dword ptr [ebp+var_74], 0E7h (EA 0x591623) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (231 & 0xFF = 231) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 231, block);   // EA 0x5916b5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_239(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_239 0x591950 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5919f5): wire opcode 0x13,
    //   sub-code 239 = mov dword ptr [ebp+var_74], 0EFh (EA 0x591963) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (239 & 0xFF = 239) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 239, block);   // EA 0x5919f5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_247(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_247 0x591F50 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591ff5): wire opcode 0x13,
    //   sub-code 247 = mov dword ptr [ebp+var_74], 0F7h (EA 0x591f63) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (247 & 0xFF = 247) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 247, block);   // EA 0x591ff5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_255(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6) {
    // Net_SendVaultReq_255 0x592540 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5925d3): wire opcode 0x13,
    //   sub-code 255 = mov dword ptr [ebp+var_74], 0FFh (EA 0x592553) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (255 & 0xFF = 255) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[6] = { a1, a2, a3, a4, a5, a6 };
    std::memcpy(block, f, sizeof(f));  // block+0..23: Crt_Memcpy(vN,&aN,4u) x6 (a1 a2 a3 a4 a5 a6)
    Net_SendPacket_Op19(nc, 255, block);   // EA 0x5925d3 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_208(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_208 0x5904D0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590575): wire opcode 0x13,
    //   sub-code 208 = mov dword ptr [ebp+var_74], 0D0h (EA 0x5904e3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (208 & 0xFF = 208) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 208, block);   // EA 0x590575 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_216(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_216 0x590AD0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590b75): wire opcode 0x13,
    //   sub-code 216 = mov dword ptr [ebp+var_74], 0D8h (EA 0x590ae3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (216 & 0xFF = 216) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 216, block);   // EA 0x590b75 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_224(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_224 0x5910D0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591175): wire opcode 0x13,
    //   sub-code 224 = mov dword ptr [ebp+var_74], 0E0h (EA 0x5910e3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (224 & 0xFF = 224) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 224, block);   // EA 0x591175 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_232(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_232 0x5916D0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591775): wire opcode 0x13,
    //   sub-code 232 = mov dword ptr [ebp+var_74], 0E8h (EA 0x5916e3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (232 & 0xFF = 232) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 232, block);   // EA 0x591775 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_240(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_240 0x591A10 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591ab5): wire opcode 0x13,
    //   sub-code 240 = mov dword ptr [ebp+var_74], 0F0h (EA 0x591a23) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (240 & 0xFF = 240) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 240, block);   // EA 0x591ab5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_248(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6, int32_t p7) {
    // Net_SendVaultReq_248 0x592010 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5920b5): wire opcode 0x13,
    //   sub-code 248 = mov dword ptr [ebp+var_74], 0F8h (EA 0x592023) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (248 & 0xFF = 248) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { p1, p2, p3, p4, p5, p6, p7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 248, block);   // EA 0x5920b5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_0(NetClient& nc, int32_t p1, int32_t p2, int32_t p3, int32_t p4, int32_t p5, int32_t p6) {
    // Net_SendVaultReq_0 0x5925F0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592683): wire opcode 0x13,
    //   sub-code 256 = mov dword ptr [ebp+var_74], 100h (EA 0x592603) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (256 & 0xFF = 0) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[6] = { p1, p2, p3, p4, p5, p6 };
    std::memcpy(block, f, sizeof(f));  // block+0..23: Crt_Memcpy(vN,&aN,4u) x6 (a1 a2 a3 a4 a5 a6)
    Net_SendPacket_Op19(nc, 256, block);   // EA 0x592683 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_201(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_201 0x5901C0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590265): wire opcode 0x13,
    //   sub-code 201 = mov dword ptr [ebp+var_74], 0C9h (EA 0x5901d3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (201 & 0xFF = 201) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 201, block);   // EA 0x590265 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_209(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_209 0x590590 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590635): wire opcode 0x13,
    //   sub-code 209 = mov dword ptr [ebp+var_74], 0D1h (EA 0x5905a3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (209 & 0xFF = 209) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 209, block);   // EA 0x590635 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_217(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_217 0x590B90 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590c35): wire opcode 0x13,
    //   sub-code 217 = mov dword ptr [ebp+var_74], 0D9h (EA 0x590ba3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (217 & 0xFF = 217) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 217, block);   // EA 0x590c35 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_225(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_225 0x591190 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591235): wire opcode 0x13,
    //   sub-code 225 = mov dword ptr [ebp+var_74], 0E1h (EA 0x5911a3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (225 & 0xFF = 225) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 225, block);   // EA 0x591235 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_233(NetClient& nc, int32_t a1, int32_t a2) {
    // Net_SendVaultReq_233 0x591790 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5917db): wire opcode 0x13,
    //   sub-code 233 = mov dword ptr [ebp+var_74], 0E9h (EA 0x5917a3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (233 & 0xFF = 233) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { a1, a2 };
    std::memcpy(block, f, sizeof(f));  // block+0..7: Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 233, block);   // EA 0x5917db -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_241(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_241 0x591AD0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591b75): wire opcode 0x13,
    //   sub-code 241 = mov dword ptr [ebp+var_74], 0F1h (EA 0x591ae3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (241 & 0xFF = 241) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 241, block);   // EA 0x591b75 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_249(NetClient& nc, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7) {
    // Net_SendVaultReq_249 0x5920D0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592175): wire opcode 0x13,
    //   sub-code 249 = mov dword ptr [ebp+var_74], 0F9h (EA 0x5920e3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (249 & 0xFF = 249) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { a1, a2, a3, a4, a5, a6, a7 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 249, block);   // EA 0x592175 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_245b(NetClient& nc) {
    // Net_SendVaultReq_245b 0x5926A0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5926c7): wire opcode 0x13,
    //   sub-code 501 = mov dword ptr [ebp+var_74], 1F5h (EA 0x5926b3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (501 & 0xFF = 245) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    // 100-byte block NOT initialized on the binary side (stack) -> zeroed here: we do not
    //   reproduce the stack leak.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    Net_SendPacket_Op19(nc, 501, block);   // EA 0x5926c7 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_202(NetClient& nc, int32_t arg0, int32_t arg1) {
    // Net_SendVaultReq_202 0x590280 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5902cb): wire opcode 0x13,
    //   sub-code 202 = mov dword ptr [ebp+var_74], 0CAh (EA 0x590293) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (202 & 0xFF = 202) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[2] = { arg0, arg1 };
    std::memcpy(block, f, sizeof(f));  // block+0..7: Crt_Memcpy(vN,&aN,4u) x2 (a1 a2)
    Net_SendPacket_Op19(nc, 202, block);   // EA 0x5902cb -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_210(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_210 0x590650 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5906f5): wire opcode 0x13,
    //   sub-code 210 = mov dword ptr [ebp+var_74], 0D2h (EA 0x590663) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (210 & 0xFF = 210) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 210, block);   // EA 0x5906f5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_218(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_218 0x590C50 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x590cf5): wire opcode 0x13,
    //   sub-code 218 = mov dword ptr [ebp+var_74], 0DAh (EA 0x590c63) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (218 & 0xFF = 218) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 218, block);   // EA 0x590cf5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_226(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_226 0x591250 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x5912f5): wire opcode 0x13,
    //   sub-code 226 = mov dword ptr [ebp+var_74], 0E2h (EA 0x591263) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (226 & 0xFF = 226) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 226, block);   // EA 0x5912f5 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_234(NetClient& nc, int32_t arg0) {
    // Net_SendVaultReq_234 0x5917F0 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591829): wire opcode 0x13,
    //   sub-code 234 = mov dword ptr [ebp+var_74], 0EAh (EA 0x591803) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (234 & 0xFF = 234) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[1] = { arg0 };
    std::memcpy(block, f, sizeof(f));  // block+0..3: Crt_Memcpy(vN,&aN,4u) x1 (a1)
    Net_SendPacket_Op19(nc, 234, block);   // EA 0x591829 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_242(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_242 0x591B90 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x591c35): wire opcode 0x13,
    //   sub-code 242 = mov dword ptr [ebp+var_74], 0F2h (EA 0x591ba3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (242 & 0xFF = 242) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 242, block);   // EA 0x591c35 -> frame 9+4+100 = 113 bytes
}

void Net_SendVaultReq_250(NetClient& nc, int32_t arg0, int32_t arg1, int32_t arg2, int32_t arg3, int32_t arg4, int32_t arg5, int32_t arg6) {
    // Net_SendVaultReq_250 0x592190 -> Net_SendPacket_Op19 0x4B4E70 (call EA 0x592235): wire opcode 0x13,
    //   sub-code 250 = mov dword ptr [ebp+var_74], 0FAh (EA 0x5921a3) -> zero-extended int32.
    // TRAP: the name suffix equals sub-code & 0xFF (250 & 0xFF = 250) — NEVER
    //   infer the sub-code from the name; it comes from the `mov` above.
    uint8_t block[100] = {0};   // block copied by Op19 at +13 (Crt_Memcpy(this+13,a3,0x64u))
    const int32_t f[7] = { arg0, arg1, arg2, arg3, arg4, arg5, arg6 };
    std::memcpy(block, f, sizeof(f));  // block+0..27: Crt_Memcpy(vN,&aN,4u) x7 (a1 a2 a3 a4 a5 a6 a7)
    Net_SendPacket_Op19(nc, 250, block);   // EA 0x592235 -> frame 9+4+100 = 113 bytes
}

} // namespace ts2::net

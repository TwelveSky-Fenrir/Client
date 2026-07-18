// Net/CharSelectPackets.cpp — implementation. See CharSelectPackets.h for the
// RE confidence level (RECONFIRMED by fresh decompilation via idaTs2, 2026-07-14).
//
// NOTE: the 5 original functions (0x5298F0/0x52A4A0/0x52A740/0x52B070/0x52B310)
// EACH build their own inline frame in the binary (nonces + 9B header +
// XOR + send, pattern duplicated 5 times byte-for-byte). We faithfully factor this
// common pattern into SendFrame() below rather than duplicating it 5 times in C++ —
// the bytes produced on the wire are identical, only the source code organization
// differs from the binary (which has no common function here for lack of inlining
// by the original compiler).
#include "Net/CharSelectPackets.h"
#include "Net/Rng.h"           // DefaultRng() — SINGLE _holdrand stream (Rng_Next 0x7603FD)
#include "Game/ClientRuntime.h" // game::g_Client.Var — globals escape hatch (dword_1675898)
#include <cstring>
#include <vector>
#include <algorithm>

namespace ts2::net {

namespace {

// Blocking receive of exactly `need` bytes into nc.recvBuf (starting at
// offset 0). Same pattern as Net/Login.cpp::RecvExact (separate file, not
// exported) — the socket is still SYNCHRONOUS here (WSAAsyncSelect not yet set,
// cf. Net_ConnectGameServer, which hasn't been called yet at this point in the flow).
bool RecvExact(NetClient& nc, int need) {
    for (int i = 0; i != need; ) {
        int n = recv(nc.sock, nc.recvBuf + i, kRecvBufSize - i, 0);
        if (n <= 0) return false;
        i += n;
    }
    return true;
}

// Copies a string into a fixed-size `n`-byte field, zero-filled (truncates if too
// long). Reproduces the CopyField128 pattern from Net/Login.cpp for a shorter
// field (character name, 13 bytes at offset 20 of the record — cf. header).
void CopyFieldN(uint8_t* dst, size_t n, const std::string& src) {
    std::memset(dst, 0, n);
    const size_t c = std::min(n, src.size());
    std::memcpy(dst, src.data(), c);
}

// Reads an i32 result code at recvBuf[1] (unencrypted), same convention as
// Net_LoginRequest/Net_ConnectGameServer (Net/Login.cpp): 1 header byte (echo
// of the low-level opcode/status, unused here) then the code as 4 bytes LE.
int32_t ReadResultCode(NetClient& nc) {
    int32_t code = 0;
    std::memcpy(&code, nc.recvBuf + 1, 4);
    return code;
}

// Rng_Next() % 10000 x2 for each nonce — same algorithm/same consumption order
// as Net/Login.cpp::MakeNonces (duplicated here, a local function not exported
// there), confirmed identical across all 5 CharSelect functions by fresh
// decompilation.
//
// Rng_Next 0x7603FD = MSVC CRT's rand(): _holdrand = Crt_GetPtd()[5] (0x76D464);
// s = 214013*s + 2531011 (EA 0x76040b); returns (s >> 16) & 0x7FFF (EA 0x76041e).
// SINGLE SHARED STREAM with every other draw in the client (the binary has only
// ONE _holdrand, seeded by srand(time(NULL)) in App_Init 0x461C20 EA 0x461C3E):
// we hit net::DefaultRng() (Net/Rng.h) and NOT std::rand(), which here would be a
// SECOND independent stream (order/value drift vs. the binary).
inline int RngNext() { return DefaultRng().Next(); }
inline void MakeNonces(uint32_t& nonce1, uint32_t& nonce2) {
    int a = RngNext() % 10000;
    int b = RngNext() % 10000;
    nonce1 = static_cast<uint32_t>(a * b);
    int c = RngNext() % 10000;
    int d = RngNext() % 10000;
    nonce2 = static_cast<uint32_t>(c * d);
}

// Full blocking send. Same pattern as Net/Login.cpp::SendAll.
bool SendAllBlocking(SOCKET s, const uint8_t* buf, int len) {
    int off = 0;
    while (len > 0) {
        int n = send(s, reinterpret_cast<const char*>(buf) + off, len, 0);
        if (n == SOCKET_ERROR) return false;
        len -= n;
        off += n;
    }
    return true;
}

// Builds the 9B header [nonce1:4][nonce2_lo:3][seq:1@7][opcode:1@8] + `payload`,
// XOR-encrypts the whole thing with nc.xorKey, sends it blocking. On success:
// increments nc.seq (the ++byte_8156A5 pattern from the 5 original functions) and
// returns true. On failure: closes the socket (Net_CloseSocket) and returns false —
// the caller maps this to kCharSelectErrSend (101), like the binary.
//
// WRITE ORDER RE-VERIFIED BYTE-BY-BYTE (decompilation of 0x52A4A0 + 0x52BD80, this
// session) — identical across all 6 builders: `Crt_Memcpy(buf, &nonce1, 4u)` [0..3];
// `Crt_Memcpy(v14/*@4*/, &nonce2, 4u)` [4..7]; `Crt_Memcpy(v15/*@7*/, seq, 1u)` [7,
// OVERWRITES the 4th byte of nonce2); `v15[1] = opcode` [8]. Reproduced as-is below
// (4B memcpy of nonce2 at +4, THEN pkt[7] = seq which overwrites it).
//
// `extraResidualBytes`: number of bytes sent BEYOND 9+payloadLen, present in the
// emitted length but NEVER written by the binary (residual stack). Used ONLY by
// op27 (len=0Eh @0x52BE46 while the memcpy stops at byte 12) — they are XORed and
// sent like the rest. See CharSelectPackets.h::AccountReq_op27 (anomaly 1) and the
// TODO [0x52BE46] about their value. 0 for all others.
bool SendFrame(NetClient& nc, uint8_t opcode, const void* payload, size_t payloadLen,
               size_t extraResidualBytes = 0) {
    std::vector<uint8_t> pkt(9 + payloadLen + extraResidualBytes);
    uint32_t nonce1, nonce2;
    MakeNonces(nonce1, nonce2);
    std::memcpy(pkt.data() + 0, &nonce1, 4);
    std::memcpy(pkt.data() + 4, &nonce2, 4); // byte [7] overwritten right after by seq
    pkt[7] = nc.seq;
    pkt[8] = opcode;
    if (payloadLen)
        std::memcpy(pkt.data() + 9, payload, payloadLen);
    // `extraResidualBytes` stay at 0 (vector's default value): these are the bytes the
    // binary leaves on the stack — non-deterministic statically, cf. TODO [0x52BE46].
    for (auto& b : pkt)
        b ^= nc.xorKey;

    if (!SendAllBlocking(nc.sock, pkt.data(), static_cast<int>(pkt.size()))) {
        NetCloseSocket(nc);
        return false;
    }
    ++nc.seq;
    return true;
}

} // namespace

void ParseCharRecord(const uint8_t* rec, game::CharSlotInfo& out) {
    out = game::CharSlotInfo{};

    // Name: C-string, max 13 B (including NUL) — cf. kCharRecFieldName. Copied into a
    // local NUL-terminated buffer rather than following a memchr directly on `rec`,
    // so we never read past the 13 B even if the field isn't NUL-terminated in a
    // corrupted/partial record.
    char nameBuf[14] = {}; // 13 B field + 1 guard NUL
    std::memcpy(nameBuf, rec + kCharRecFieldName, 13);
    nameBuf[13] = '\0';
    out.name = nameBuf;

    // Occupied: Crt_Strcmp(name, "") != 0, EA 0x51c2f7 — reproduced by non-empty name
    // (std::string already stops at the first internal NUL, like Crt_Strcmp).
    out.occupied = !out.name.empty();
    if (!out.occupied) return; // faithful: the binary never uses any other field of an empty record

    std::memcpy(&out.job,        rec + kCharRecFieldJob,     4);
    // +40 = LIST's EFFECTIVE RACE. Field filled by the SERVER only (opcode 17
    // echo): data_refs(0x16709E0) = 0 refs — the creation form NEVER writes it
    // (re-verified this session: `xrefs_to(0x16709E0)` -> no references).
    // TWO consumers read it in the binary, both ported to C++:
    //   - motion resolution: `imul eax, 2768h` @0x51C528 then
    //     `mov ecx, ds:dword_16693A8[eax]` @0x51C52E (= &unk_1669380 + 0x2768*slot + 0x28)
    //     -> `call PcModel_ResolveSlotAndApply` (0x4E5A00) @0x51C53A.
    //     WARNING the EA 0x51C555 sometimes cited for this read is WRONG: it's a
    //     `jnz short loc_51C5C1` float comparison on +0xF59C (animTime).
    //   - "self" publication: `Crt_Memcpy(g_SelfCharInvBlock 0x1673170,
    //     &unk_1669380 + 0x2768*slot, 0x2768)` @0x51C707 -> block+0x28 IS
    //     g_LocalElementSecondary 0x1673198 (0x1673198 - 0x1673170 = 0x28 = 40), a global
    //     with 50 readers in the game (AutoPlay_IsLocalElementMatch 0x45C590,
    //     Char_BuildEquipSnapshot 0x4CC1C0, cGameHud_Render 0x64A900...).
    // Without this copy, game::CharSlotInfo::race stayed at 0 and BOTH live C++ paths
    // started from race=0 (Game/CharSelectFlow.cpp::ListMotionFrameCount and
    // UI/LoginScene.cpp::PublishSelfFromSlot -> g_World.self.elementSecondary).
    // Also cf. Char_RenderModel LIST branch: `mov eax,[edx+28h]` @0x527536.
    std::memcpy(&out.race,       rec + kCharRecFieldRace,    4);
    std::memcpy(&out.faction,    rec + kCharRecFieldFaction, 4);
    std::memcpy(&out.face,       rec + kCharRecFieldFace,    4);
    std::memcpy(&out.hairColor, rec + kCharRecFieldHair,    4);
    std::memcpy(&out.power,      rec + kCharRecFieldPower,   4);
    std::memcpy(&out.localZoneId, rec + kCharRecFieldZoneId, 4);

    // Local position: stored as int32 in the binary, cast to float AT USE TIME
    // (flt_1675AAC = (float)dword_166A8E0[...], EA 0x51c79e and following) — reproduced
    // as-is here (int32 read then conversion), not a direct float read.
    int32_t rawX = 0, rawY = 0, rawZ = 0;
    std::memcpy(&rawX, rec + kCharRecFieldPosX, 4);
    std::memcpy(&rawY, rec + kCharRecFieldPosY, 4);
    std::memcpy(&rawZ, rec + kCharRecFieldPosZ, 4);
    out.localPosX = static_cast<float>(rawX);
    out.localPosY = static_cast<float>(rawY);
    out.localPosZ = static_cast<float>(rawZ);
}

CharRecordListFields ReadCharRecordListFields(const uint8_t* rec) {
    CharRecordListFields out{};
    // Fields read by the LIST branch of Char_RenderModel 0x527020 (arg_4 == 0), in the
    // binary's order:
    //   `mov eax, [edx+28h]` @0x527536 -> a2 of PcModel_ResolveEquipSlot = +40 (RACE).
    //     WARNING the CREATION branch reads +36 for the same role (`mov edx,[ecx+24h]` @0x527051) —
    //     these are truly TWO distinct fields, cf. the kCharRecFieldRace anchor (.h).
    //   `mov ecx, [eax+2Ch]` @0x52752F -> a3 = +44 (GENDER 0..1).
    //   `cmp dword ptr [ecx+24h], 3` @0x52754A -> sentinel on +36.
    //   `mov edx, [ecx+0D8h]` @0x527497 + MobDb_GetEntry(mITEM) @0x5274A3 -> +216
    //     (starting weapon's item ID).
    std::memcpy(&out.race,   rec + kCharRecFieldRace,    4);
    std::memcpy(&out.gender, rec + kCharRecFieldFaction, 4); // +44, "faction" = gender
    std::memcpy(&out.job,    rec + kCharRecFieldJob,     4);
    std::memcpy(&out.startingWeaponItemId, rec + kCharRecFieldStartingWeaponItemId, 4);
    out.jobSentinelIs3 = (out.job == kCharRecJobSentinelValue);
    return out;
}

bool ReadCharRecordListFields(int32_t slot, CharRecordListFields& out) {
    out = CharRecordListFields{};
    // PORT guard: the binary indexes `&unk_1669380 + 10088*slot` without bounds. Real
    // callers already bound slot to [0..2] (this[15715], -1 = none); we refuse cleanly
    // instead of reading past the 3 records.
    if (slot < 0 || slot >= kCharRecordCount) return false;
    out = ReadCharRecordListFields(g_CharRecords[static_cast<size_t>(slot)]);
    return true;
}

void LoadCharacterSlotsFromRecords(std::array<game::CharSlotInfo, game::kMaxCharSlots>& slots) {
    static_assert(game::kMaxCharSlots == kCharRecordCount,
                  "kMaxCharSlots must stay in sync with kCharRecordCount (NetClient.h)");
    for (int i = 0; i < game::kMaxCharSlots; ++i)
        ParseCharRecord(g_CharRecords[static_cast<size_t>(i)], slots[static_cast<size_t>(i)]);
}

int32_t AccountKeepAlive(NetClient& nc) {
    // Opcode 12, NO payload (9 B, header only) — confirmed by fresh decompilation
    // of 0x5298F0. NO response wait (confirmed fire-and-forget heartbeat): the
    // binary sets *a1=0 immediately after a successful send, without recv().
    if (!SendFrame(nc, 12, nullptr, 0)) return kCharSelectErrSend;
    return 0;
}

int32_t CreateCharacter(NetClient& nc, int32_t slot, const game::CharCreateForm& form,
                        int32_t startingWeaponItemId) {
    // Opcode 17. REAL payload 10092 B = 4 B slot + 10088 B character record (confirmed
    // by fresh decompilation of 0x52A4A0 + of the caller Scene_CharSelectOnMouseUp
    // EA 0x526634-0x5267E4; total frame len=10101 @0x52A582 = 9 + 4 + 10088).
    // EXHAUSTIVE INVENTORY of fields written by the form (search_text "_16709|_1670A"
    // over [0x51B000, 0x527000) = 64 hits: the binary NEVER writes any other byte of the
    // creation record) — everything else is ZERO:
    //   [20..32] name (13 B, GetWindowTextA @0x526583/0x52658F) - [36] job (0x52537C
    //   random / 0x5260B2 minus / 0x526158 plus) - [44] gender, called "faction" (0x525382 /
    //   0x5261F8 / 0x526280) - [48] face (0x52538C / 0x526305 / 0x52636B) - [52] hairColor
    //   (0x525396 / 0x5263D1 / 0x52643A) - [104] equipment, 208 B set to 0
    //   (Crt_Memset(...,0,0xD0) @0x526634 — already covered by the vector's zero-init) -
    //   [216] startingWeaponItemId (0x52669A..0x52675B).
    // WARNING [40] (race) is NEVER written by the client: data_refs(0x16709E0) = 0 refs.
    // It's the SERVER that fills it, and it comes back via the echo (copied below).
    // WARNING [56] is never written by the form either.
    // WARNING `variant` is NOT in the record: it's this[15716] (+0xF590), a SCENE
    // field; it only reaches the network via startingWeaponItemId (+216).
    std::vector<uint8_t> payload(4 + 10088, 0);
    std::memcpy(payload.data(), &slot, 4);
    uint8_t* rec = payload.data() + 4;
    CopyFieldN(rec + kCharRecFieldName, 13, form.name);
    std::memcpy(rec + kCharRecFieldJob,     &form.job,       4);
    std::memcpy(rec + kCharRecFieldFaction, &form.faction,   4); // +44 = gender (historical name)
    std::memcpy(rec + kCharRecFieldFace,    &form.face,      4);
    std::memcpy(rec + kCharRecFieldHair,    &form.hairColor, 4);
    std::memcpy(rec + kCharRecFieldStartingWeaponItemId, &startingWeaponItemId, 4);

    if (!SendFrame(nc, 17, payload.data(), payload.size())) return kCharSelectErrSend;

    // REAL response 10093 B: [1][code:4][record-echo:10088] — the server sends back the
    // created record. We fully drain the stream so later reads don't get desynced.
    if (!RecvExact(nc, 10093)) return kCharSelectErrRecv;

    // CHARACTER-LIST MIRROR — Net_CreateCharacter 0x52A4A0, EA 0x52a71e:
    //   if (!v18) Crt_Memcpy((unsigned int)&unk_1669380 + 10088 * a1,
    //                        &MEMORY[0x8156C5], 0x2768u);
    // Guard `if (!v18)` at EA 0x52a700: the copy only happens on code 0. The source
    // &MEMORY[0x8156C5] = recvBuf(0x8156C0) + 5, so the echoed record starts at
    // recvBuf+5 (right after [1][code:4]) and is 0x2768 = 10088 B = kCharRecordSize.
    // The destination unk_1669380 + 10088*slot is EXACTLY the mirror that
    // Net_LoginRequest 0x51B8E0 fills at login (EA 0x51bc56/0x51bc6d/0x51bc84) —
    // ported here by net::g_CharRecords (NetClient.h).
    // WITHOUT this copy, the created character DISAPPEARS: LoadCharacterSlotsFromRecords
    // re-reads g_CharRecords[slot], still zeroed, on the next pass through the Init
    // sub-state (Game/CharSelectFlow.cpp), and ParseCharRecord goes back to
    // occupied=false (the `!out.name.empty()` criterion).
    const int32_t code = ReadResultCode(nc);
    if (code == 0 && slot >= 0 && slot < kCharRecordCount) {
        // Bounds: PORT safety guard (the binary has none — it indexes a 3-record
        // array embedded in .data). `slot` always comes from FindFirstFreeSlot()
        // (0..2): no observable behavior divergence.
        std::memcpy(g_CharRecords[static_cast<size_t>(slot)], nc.recvBuf + 5, kCharRecordSize);
    }
    return code;
}

int32_t VerifyCharName(NetClient& nc, int32_t slotEnc, const std::string& name) {
    // Net_ReqVerifyCharName 0x52B4C0 (opcode 24) — character deletion confirmed by
    // typing the name. 62 B frame (len=62, EA 0x52b59b) = 9 B header + 53 B payload.
    // Offsets proven by stack positions (buf@esp+0x24 -> offset 0):
    //   [nonce1:4@0] (EA 0x52b534) - [nonce2:3@4] (EA 0x52b54c) - [seq:1@7] (EA
    //   0x52b562) - [opcode=24:1@8] (v15[1]=24, EA 0x52b56a) - [slotEnc:4@9]
    //   (Crt_Memcpy(v16,&a1,4), v16@esp+0x2D, EA 0x52b57e) - [name:49@13]
    //   (Crt_Memcpy(v17,a2,0x31u), v17@esp+0x31, EA 0x52b593).
    // -> payload = [slotEnc:i32@0][name:49@4], 4+49 = 53; 9+53 = 62. OK
    // The name field is zero-filled (the binary memcpys 0x31=49 B from a local
    // `String` buffer filled by GetWindowTextA(dword_166900C, String, 49), EA 0x529273).
    uint8_t payload[4 + 49];
    std::memcpy(payload, &slotEnc, 4);
    CopyFieldN(payload + 4, 49, name);

    if (!SendFrame(nc, 24, payload, sizeof(payload))) return kCharSelectErrSend;
    // Blocking 5 B response = [1][code:4] (loop `j != 5` EA 0x52b67a; code read at
    // recvBuf+1 = &MEMORY[0x8156C1], EA 0x52b702). XOR key + ++seq after a successful
    // send (EA 0x52b5eb / 0x52b675): both handled by SendFrame.
    if (!RecvExact(nc, 5)) return kCharSelectErrRecv;
    return ReadResultCode(nc);
}

int32_t CharSlotAction(NetClient& nc, int32_t slot, int32_t action, int32_t arg) {
    // Opcode 18. REAL payload 12 B = 3 fields of 4B (slot/action/arg) at 0/4/8 — confirmed
    // by fresh decompilation of 0x52A740.
    uint8_t payload[12];
    std::memcpy(payload + 0, &slot,   4);
    std::memcpy(payload + 4, &action, 4);
    std::memcpy(payload + 8, &arg,    4);
    if (!SendFrame(nc, 18, payload, sizeof(payload))) return kCharSelectErrSend;
    if (!RecvExact(nc, 5)) return kCharSelectErrRecv; // [1][code:4], confirmed unchanged
    return ReadResultCode(nc);
}

game::EnterCharInfoResult ReqEnterCharInfo(NetClient& nc, int32_t slot) {
    game::EnterCharInfoResult out{};
    // Opcode 22. REAL payload 4 B = ONLY the slot — confirmed by fresh decompilation
    // of 0x52B070 (the old code sent a redundant 2nd field, misaligning the frame by
    // 4 B against a real server).
    int32_t slotField = slot;
    if (!SendFrame(nc, 22, &slotField, sizeof(slotField))) {
        out.resultCode = kCharSelectErrSend;
        return out;
    }
    // Response [1][code:4][domainId:4][gamePort:4][zoneId:4] (17 B) — CONFIRMED
    // unchanged by fresh decompilation (EA 0x52B217-0x52B2FE).
    if (!RecvExact(nc, 17)) { out.resultCode = kCharSelectErrRecv; return out; }
    int32_t code = 0, domainId = 0, gamePort = 0, zoneId = 0;
    std::memcpy(&code,     nc.recvBuf + 1,  4);
    std::memcpy(&domainId, nc.recvBuf + 5,  4);
    std::memcpy(&gamePort, nc.recvBuf + 9,  4);
    std::memcpy(&zoneId,   nc.recvBuf + 13, 4);
    out.resultCode = code;
    out.domainId   = domainId;
    out.gamePort   = gamePort;
    out.zoneId     = zoneId;
    return out;
}

int32_t ReqCancelEnter(NetClient& nc) {
    // REAL opcode 23 (0x17), NOT 21 (0x15) — CRITICAL discrepancy found by fresh
    // decompilation of 0x52B310 (IDA name "Net_ReqCancelEnter" confirmed via
    // lookup_funcs, but the opcode byte it actually emits on the wire is 23; the
    // generic opcode 21/Net_SendPacket_Op21 is an UNRELATED mechanism, used
    // elsewhere). NO payload (9 B). NO response wait (confirmed fire-and-forget,
    // like AccountKeepAlive): the binary sets *a1=0 immediately after a successful
    // send, without recv().
    if (!SendFrame(nc, 23, nullptr, 0)) return kCharSelectErrSend;
    return 0;
}

// --- PIN / secondary password helper (op13/14/15) — cf. header ---

int32_t SecondaryPasswordSet(NetClient& nc, const uint8_t pin5[5]) { // Net_AccountReq_op13 0x529AA0
    if (!SendFrame(nc, 13, pin5, 5)) return kCharSelectErrSend;      // opcode 13, PIN[5]@9 (len 14)
    if (!RecvExact(nc, 10)) { NetCloseSocket(nc); return kCharSelectErrRecv; } // recv 10
    const int32_t code = ReadResultCode(nc);                        // [1..4]
    if (code == 0) {                                                // @0x529CEC
        g_SecondaryPwRequired = 0;                                  // dword_16692A4 = 0
        std::memcpy(g_StoredSecondaryPw, nc.recvBuf + 5, 5);        // unk_16692A8 <- PIN echo (recv+5)
    }
    return code;
}

int32_t SecondaryPasswordChange(NetClient& nc, const uint8_t oldPin5[5], const uint8_t newPin5[5]) { // Net_AccountReq_op14 0x529D20
    uint8_t payload[10];
    std::memcpy(payload + 0, oldPin5, 5);                           // old PIN [9..13]
    std::memcpy(payload + 5, newPin5, 5);                           // new PIN [14..18]
    if (!SendFrame(nc, 14, payload, 10)) return kCharSelectErrSend; // opcode 14 (len 19)
    if (!RecvExact(nc, 10)) { NetCloseSocket(nc); return kCharSelectErrRecv; } // recv 10
    const int32_t code = ReadResultCode(nc);
    if (code == 0) {                                                // @0x529F7E
        g_SecondaryPwRequired = 0;                                  // dword_16692A4 = 0
        std::memcpy(g_StoredSecondaryPw, nc.recvBuf + 5, 5);        // unk_16692A8 <- new PIN echo
    }
    return code;
}

int32_t SecondaryPasswordVerify(NetClient& nc, const uint8_t pin5[5]) { // Net_AccountReq_op15 0x529FB0
    if (!SendFrame(nc, 15, pin5, 5)) return kCharSelectErrSend;      // opcode 15, PIN[5]@9 (len 14)
    if (!RecvExact(nc, 5)) { NetCloseSocket(nc); return kCharSelectErrRecv; } // recv 5
    const int32_t code = ReadResultCode(nc);
    if (code == 0)                                                  // @0x52A1FC
        g_SecondaryPwRequired = 0;                                  // dword_16692A4 = 0 ONLY (no PIN update)
    return code;
}

int32_t AccountReq_op27(NetClient& nc, int32_t arg) {
    // Net_AccountReq_op27 0x52BD80 (opcode 27), emitted from Scene_CharSelectOnMouseUp
    // @0x523E07. See CharSelectPackets.h::AccountReq_op27 for the 2 faithful anomalies.
    //
    // ANOMALY 1 — 14 B frame whose 14th byte is UNINITIALIZED: `len = 0Eh` @0x52BE46
    // while `Crt_Memcpy(v15 /*offset 9*/, &a1, 4u)` @0x52BE3E only writes bytes
    // 9..12. Byte 13 is residual stack, XORed (loop `i < len` -> 0..13) and sent.
    // Reproduced here via extraResidualBytes=1 (emitted as 0).
    // TODO [0x52BE46]: real value of byte 13 not determinable statically
    // (uninitialized stack) — runtime x32dbg dump required to know it.
    if (!SendFrame(nc, 27, &arg, sizeof(arg), /*extraResidualBytes=*/1))
        return kCharSelectErrSend;

    // 9 B response = [1][code:4][value:4] (loop `j != 9` @0x52BF27).
    if (!RecvExact(nc, 9)) return kCharSelectErrRecv;

    // ANOMALY 2 — `Crt_Memcpy(&dword_1675898, &MEMORY[0x8156C5], 4u)` @0x52BFC4 is
    // UNCONDITIONAL and precedes `*a2 = v16` @0x52BFD2: NO `if (!code)` guard,
    // unlike op17 (`if (!v18)` @0x52A700). The field is therefore overwritten even on
    // server error — reproduced as-is (do not add a guard).
    // dword_1675898: long tail of globals, same convention as the other writers of
    // this address (Net/GameHandlers_Misc.cpp, Net/GameVarDispatch.cpp).
    int32_t value = 0;
    std::memcpy(&value, nc.recvBuf + 5, 4);
    game::g_Client.Var(0x1675898) = value;

    // Result code read at rx+1 (`Crt_Memcpy(&v16, &MEMORY[0x8156C1], 4u)` @0x52BFB0).
    return ReadResultCode(nc);
}

void BuildEnterWorldTail72(float posX, float posY, float posZ, float rotationDeg,
                            uint8_t out[72]) {
    // Reproduces EXACTLY Crt_Memset(&dword_1675AA0,0,0x48) + the 5 field writes from
    // Scene_CharSelectUpdate (EA 0x51c765-0x51c7f9), cf. the kTail72Off* constants
    // declared in CharSelectPackets.h for the full decompiled justification (double
    // confirmation via Map_BeginWarpToFactionTown 0x55C510).
    std::memset(out, 0, 72);
    std::memcpy(out + kTail72OffPosX, &posX, sizeof(float));
    std::memcpy(out + kTail72OffPosY, &posY, sizeof(float));
    std::memcpy(out + kTail72OffPosZ, &posZ, sizeof(float));
    std::memcpy(out + kTail72OffRotA, &rotationDeg, sizeof(float));
    std::memcpy(out + kTail72OffRotB, &rotationDeg, sizeof(float));
    // kTail72OffMode/Flag/Pad8 stay at 0 (memset): the 2 decompiled sites ONLY write 0
    // at these offsets for an EnterWorld flow (mode=0, variant=0).
}

} // namespace ts2::net

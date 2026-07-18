// Net/RecvPackets_Misc.h — Misc incoming packet structs (S->C), split from RecvPackets.h.
// Framing: [opcode:u8][payload]. Each struct decodes the payload (LE, byte-exact).
// State-update logic (globals/entity arrays) lives in the matching Net/GameHandlers_Misc.cpp.
#pragma once
#include <cstdint>
#include "Asset/ByteReader.h"

namespace ts2::net {

// Net_OnPetSlotDispatch (opcode 0x66) — MEGA-DISPATCHER pet/mount (8 sub-opcodes), manages g_TalismanSlot & stats.
struct PetSlotDispatch {
    uint32_t subop; // payload+0 (1..8 : action)
    uint32_t value; // payload+4 (value/attribute depending on the sub-opcode)
    static PetSlotDispatch Parse(const uint8_t* payload, size_t len);
};

inline PetSlotDispatch PetSlotDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PetSlotDispatch p{};
    p.subop = r.U32();
    p.value = r.U32();
    return p;
}

// Net_OnMinigameStateLoad (opcode 0x76) — loads 4 dwords of minigame state.
struct MinigameStateLoad {
    uint32_t a; // payload+0  -> dword_1675D20
    uint32_t b; // payload+4  -> dword_1675D24
    uint32_t c; // payload+8  -> dword_1675D28
    uint32_t d; // payload+12 -> dword_1675D2C
    static MinigameStateLoad Parse(const uint8_t* payload, size_t len);
};

inline MinigameStateLoad MinigameStateLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    MinigameStateLoad p{};
    p.a = r.U32();
    p.b = r.U32();
    p.c = r.U32();
    p.d = r.U32();
    return p;
}

// Net_OnSummonSpawn (opcode 0x84 / 132) — ea=0x4AA810 — spawns a pet/summon for slot<4.
struct SummonSpawn {
    uint32_t status;  // payload+0 (init -1 ; 0 = success)
    uint32_t slot;    // payload+4 (init -1 ; summon slot, must be < 4)
    static SummonSpawn Parse(const uint8_t* payload, size_t len);
};

inline SummonSpawn SummonSpawn::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SummonSpawn p{};
    p.status = r.U32();
    p.slot   = r.U32();
    return p;
}

// Pkt_SystemMessageBox 0x464650 (opcode 0x0e / 14 dec) — server IMAGE load
// (Billboard_ValidateImageViaTempFile 0x5AEA60 -> TEMP.IMG -> Tex_LoadCompressedDDS ; misleading
//  legacy name "SystemMessageBox").
// Payload : [byteLen:u32][image:1000 binary bytes] = 1004 bytes.
// PROVEN SEMANTICS (Billboard_ValidateImageViaTempFile 0x5AEA60): the handler @0x464650 reads
//   param(u32)@0x8156C1 then 1000 bytes@0x8156C5, then calls the function that does
//   WriteFile(hFile, lpBuffer=image, nNumberOfBytesToWrite=param, ..) @0x5AEACD. SO payload+0
//   = the image LENGTH IN BYTES (<=1000) and payload+4 = the raw binary DDS BLOB — this is NOT
//   a path/C string (a NUL byte can appear as early as index 0).
struct ServerBillboardImage {
    uint32_t param;        // payload+0 : LENGTH in bytes to write to TEMP.IMG (nNumberOfBytesToWrite)
    char     image[1000];  // payload+4 : raw binary DDS BLOB (NOT a C string ; NUL possible at byte 0)
    // TODO(type) [anchor 0x5AEA60]: retyping param->byteLen and image->uint8_t[1000] would be more
    //   faithful, BUT the consumer Net/GameHandlers_Misc.cpp (op 0x0e) does strnlen(p.image,..)
    //   -> a retype would break that TU (outside this front's scope, not compilable here). The
    //   retype AND its consumer must be fixed TOGETHER by the owner of
    //   GameHandlers_Misc.cpp: write the first `param` bytes of image[] via
    //   Billboard_ValidateImageViaTempFile (CreateFileA "TEMP.IMG" -> WriteFile param bytes ->
    //   Tex_LoadCompressedDDS -> DeleteFileA, then *(obj+40)=1 / *(obj+44)=g_GameTimeSec on
    //   dword_1822350). The current strnlen TRUNCATES the binary blob at the first NUL byte
    //   (inverted semantics: this field is not a file name).
    static ServerBillboardImage Parse(const uint8_t* payload, size_t len);
};

inline ServerBillboardImage ServerBillboardImage::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ServerBillboardImage p{};
    p.param = r.U32();
    r.Read(p.image, sizeof(p.image));
    return p;
}

// Net_OnCultivationDispatch (opcode 0x58 / 88 dec) — MEGA-DISPATCHER cultivation/attributes (20 sub-ops).
// Payload : [value:u32][subOpcode:u32][body:u8[100]] = 108 bytes.
struct CultivationDispatch {
    uint32_t value;      // payload+0 : value/result code (v72), semantics depend on the sub-op
    uint32_t subOpcode;  // payload+4 : sub-op selector (v67), switch 1..20
    uint8_t  body[100];  // payload+8 : raw body, interpreted as u32[] depending on the sub-op
    // STATE: PARTIALLY implemented — Net/GameHandlers_Misc.cpp (0x58) covers cases
    //   1 (message str601, NOT YET the exact g_SelfBaseAttr292/296/300/304 rework),
    //   6 (g_GrowthIndex), 7 (cost 100 gold + 1M weight), 12/13 (toggle dword_16747D4/D8),
    //   19/20 (11 u32 of attribute buffs from body[0..43] + cost 1000 for 20). The fine-grained
    //   detail per sub-op (exact point allocation, AR min/max recompute) remains TODO(state) in this
    //   handler — struct only ~4.2 KB, not fully decompiled at this stage (20 sub-cases).
    static CultivationDispatch Parse(const uint8_t* payload, size_t len);
};

inline CultivationDispatch CultivationDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    CultivationDispatch p{};
    p.value = r.U32();
    p.subOpcode = r.U32();
    r.Read(p.body, sizeof(p.body));
    return p;
}

// Net_OnSkillCooldownSet (opcode 0x6f / 111 dec) — sets a skill's cooldown.
// Payload : [skillId:u32][value:u32] = 8 bytes.
struct SkillCooldownSet {
    uint32_t skillId;  // payload+0 : skill index (valid if 1..351)
    uint32_t value;    // payload+4 : cooldown value to store
    // STATE: implemented — Net/GameHandlers_Misc.cpp (0x6f): dword_18217D0[skillId]=value if
    //   1<=skillId<=351.
    static SkillCooldownSet Parse(const uint8_t* payload, size_t len);
};

inline SkillCooldownSet SkillCooldownSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SkillCooldownSet p{};
    p.skillId = r.U32();
    p.value = r.U32();
    return p;
}

// Net_OnSkillAuraSync (opcode 0x7d / 125 dec) — skill/aura toggle state sync (mini-dispatcher).
// Payload : [subOpcode:u32][value:u32] = 8 bytes.
struct SkillAuraSync {
    uint32_t subOpcode;  // payload+0 : selector (0,1,2,5,6,7,8,9)
    int32_t  value;      // payload+4 : value (signed ; base-10 packed in case 0)
    // STATE: implemented — Net/GameHandlers_Misc.cpp (0x7d): decodes base-10 (case 0, but WITHOUT
    //   loading/unloading the zone model — remaining TODO(state), .IMG asset outside the network
    //   scope), indexed toggle (case 2), messages (5/6), faction warp (7/9/8 via
    //   BeginWarpToFactionTown, Game/MapWarp.h).
    static SkillAuraSync Parse(const uint8_t* payload, size_t len);
};

inline SkillAuraSync SkillAuraSync::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SkillAuraSync p{};
    p.subOpcode = r.U32();
    p.value = r.I32();
    return p;
}

// Net_OnSystemNotice (opcode 0x85 / 133 dec) — system notice messages.
// Payload : [subOpcode:u32][value:u32] = 8 bytes.
struct SystemNotice {
    uint32_t subOpcode;  // payload+0 : 0,1,2
    uint32_t value;      // payload+4 : displayed value (case 0/1) or sub-selector (case 2 : 0..7)
    // STATE: implemented — Net/GameHandlers_Misc.cpp (0x85): the 3 cases (0/1 -> "[value]str" ;
    //   2 -> floating message + system line depending on value 0..7 -> str1996..2003).
    static SystemNotice Parse(const uint8_t* payload, size_t len);
};

inline SystemNotice SystemNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SystemNotice p{};
    p.subOpcode = r.U32();
    p.value = r.U32();
    return p;
}

// Pkt_ToggleObserver (opcode 0x28) — toggles observer/faction-3 mode.
struct ToggleObserver {
    uint32_t resultCode;  // payload+0   0=toggle OK, 1=refused
    static ToggleObserver Parse(const uint8_t* payload, size_t len);
};

inline ToggleObserver ToggleObserver::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ToggleObserver p{};
    p.resultCode = r.U32();
    return p;
}

// Net_OnServerNameNotice (opcode 0x61) — sub-op 1 = message by id, sub-op 2 = 3 position floats.
struct ServerNameNotice {
    uint32_t subop;     // payload+0   1=table message, 2=coordinates
    uint8_t  data[100]; // payload+4   sub-op1: data[0..3]=string id (StrTable003) ; sub-op2: data[0..11]=3 floats
    static ServerNameNotice Parse(const uint8_t* payload, size_t len);
};

inline ServerNameNotice ServerNameNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ServerNameNotice p{};
    p.subop = r.U32();
    r.Read(p.data, sizeof(p.data));
    return p;
}

// Net_OnScriptTrigger (opcode 0x63 / 99) — VARIABLE PACKET ([op][len:u32][data]). Script/quest trigger checked by the anticheat.
// AUDIT (2026-07-14): unlike this file's generic "payload = buffer+1" convention, this lone
// variable opcode already has its len:u32 field stripped by the dispatcher before the handler
// runs (cf. Net/PacketDispatch.cpp::Drain() case kVariableOpcode, Net/Framing.cpp::PacketReader::TryParse,
// same offsets): `payload` already points at the data bytes (buffer+5) and `len` is already the
// data byte count. Do NOT re-read payload+0 as a second length field (former bug: re-read the
// first 4 data bytes as a length, offset `data` by 4 extra bytes, and threw ts2::asset::AssetError
// -- uncaught anywhere in the OnPacket<T>/PacketDispatcher::Drain()/WndProc chain -- for any 0x63
// frame with len<4 bytes, a crash the original binary does NOT have: Net_OnScriptTrigger 0x4A55F0
// itself reads the length directly from unk_8156C1 (buffer+1, BEFORE stripping) and passes
// (unk_8156C5, v1) as-is to Ac_GuardClient_MakeVerifyData, with no re-read or possible exception).
// Fixed: p.length = len (already known), p.data = payload (already positioned).
struct ScriptTrigger {
    uint32_t       length;   // = len (Parse parameter, already read/validated by the dispatcher)
    const uint8_t* data;     // = payload (already positioned on the data bytes by the dispatcher)
    static ScriptTrigger Parse(const uint8_t* payload, size_t len);
};

inline ScriptTrigger ScriptTrigger::Parse(const uint8_t* payload, size_t len) {
    // AUDIT (2026-07-14): `payload`/`len` are ALREADY the data field + its size (the wire's
    // len:u32 field was read and stripped by the dispatcher, cf. the ScriptTrigger header
    // comment) -- no re-read or ByteReader needed, hence no possible exception here, even for
    // a 0x63 frame with empty data (len==0).
    ScriptTrigger p{};
    p.length = static_cast<uint32_t>(len);
    p.data = payload;
    return p;
}

// Net_OnRevivePrompt (opcode 0x72 / 114) — sets the 'revive available' flag.
// Payload read: flag(+0). Total 4 bytes.
struct RevivePrompt {
    uint32_t flag;   // payload+0 : 0 = dead / revive available
    static RevivePrompt Parse(const uint8_t* payload, size_t len);
};

inline RevivePrompt RevivePrompt::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RevivePrompt p{};
    p.flag = r.U32();
    return p;
}

// Net_OnBuffEffectDispatch (opcode 0xae / 174) — buff/stat messages + inventory-cell update (sub-opcode dispatcher -1..5).
// Payload read: subOpcode(+0) param1(+4) param2(+8) param3(+12) param4(+16) param5(+20) param6(+24). Total 28 bytes.
struct BuffEffectDispatch {
    int32_t  subOpcode;  // payload+0  : selector (-1..5)
    int32_t  param1;     // payload+4  : itemId / effect code (v35)
    uint32_t param2;     // payload+8  : inventory page (v33)
    uint32_t param3;     // payload+12 : inventory slot (v36)
    uint32_t param4;     // payload+16 : grid position (v32)
    int32_t  param5;     // payload+20 : value A (v34)
    int32_t  param6;     // payload+24 : value B (v38)
    static BuffEffectDispatch Parse(const uint8_t* payload, size_t len);
};

inline BuffEffectDispatch BuffEffectDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BuffEffectDispatch p{};
    p.subOpcode = r.I32();
    p.param1 = r.I32();
    p.param2 = r.U32();
    p.param3 = r.U32();
    p.param4 = r.U32();
    p.param5 = r.I32();
    p.param6 = r.U32();
    return p;
}

// Net_OnQuickslotSync (opcode 0x5b / 91) — loads 50 quickslot ids + a gold value. Payload 212 bytes.
struct QuickslotSync {
    uint32_t flag;         // payload+0   (v6) — 0 => apply
    uint32_t mode;         // payload+4   (v3) — 1 = quickslots, 2 = gold/weight
    uint32_t quickslot[50];// payload+8   (v7) — 50 shortcut ids
    uint32_t money;        // payload+208 (v5) — value -> g_InvWeight (mode 2)
    static QuickslotSync Parse(const uint8_t* payload, size_t len);
};

inline QuickslotSync QuickslotSync::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    QuickslotSync p{};
    p.flag = r.U32(); p.mode = r.U32();
    r.Read(p.quickslot, sizeof(p.quickslot));
    p.money = r.U32();
    return p;
}

// Net_OnCountdownTimerStart (opcode 0x73 / 115) — starts a countdown timer (based on timeGetTime). Payload 12 bytes.
struct CountdownTimerStart {
    uint32_t mode; // payload+0 (v2) -> dword_183914C (-1/1 => sound)
    uint32_t f1;   // payload+4 (v1) -> dword_1839150
    uint32_t f2;   // payload+8 (v3) -> dword_1839154
    static CountdownTimerStart Parse(const uint8_t* payload, size_t len);
};

inline CountdownTimerStart CountdownTimerStart::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    CountdownTimerStart p{};
    p.mode = r.U32(); p.f1 = r.U32(); p.f2 = r.U32();
    return p;
}

// Pkt_GameServerConnectResult (opcode 0x18) — server-selection result, triggers the connection.
// Payload = 12 bytes.
struct GameServerConnectResult {
    uint32_t resultCode;   // payload+0 : global code (0 = proceed with connection, 1..12 = error messages)
    uint32_t serverId;     // payload+4 : server index (-> Net_SelectServerDomain, under resultCode==0)
    uint32_t port;         // payload+8 : game server port (-> Net_ConnectGameServer)
    static GameServerConnectResult Parse(const uint8_t* payload, size_t len);
};

inline GameServerConnectResult GameServerConnectResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GameServerConnectResult p{};
    p.resultCode = r.U32();
    p.serverId   = r.U32();
    p.port       = r.U32();
    return p;
}

// Net_OnPvpTallyUpdate (opcode 0x39) — increments win/loss counters based on the code.
// Payload = 4 bytes.
struct PvpTallyUpdate {
    uint32_t code;   // payload+0 : 0 = loss (++dword_182292C), 1 = win (++dword_1822930), 2 = message
    static PvpTallyUpdate Parse(const uint8_t* payload, size_t len);
};

inline PvpTallyUpdate PvpTallyUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PvpTallyUpdate p{};
    p.code = r.U32();
    return p;
}

// sub_4AAB60 (opcode 0x82 / 130, size 61) — ea=0x4AAB60 — raw copy of 60 bytes
// (15 floats) into the global block flt_1676130. No parsing/dispatch in the binary.
struct RawFloatBlob15 {
    float values[15]; // payload+0..59 -> flt_1676130 (verbatim memcpy, 15 floats)
    static RawFloatBlob15 Parse(const uint8_t* payload, size_t len);
};

inline RawFloatBlob15 RawFloatBlob15::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RawFloatBlob15 p{};
    for (float& v : p.values) v = r.F32();
    return p;
}

// sub_4B33C0 (opcode 0xb1 / 177, size 5) — ea=0x4B33C0 — trivial setter of a global u32.
struct RawU32Setter {
    uint32_t value; // payload+0 -> dword_16874A0 (copied verbatim) ; also clears dword_1675B08 (busy flag)
    static RawU32Setter Parse(const uint8_t* payload, size_t len);
};

inline RawU32Setter RawU32Setter::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RawU32Setter p{};
    p.value = r.U32();
    return p;
}

} // namespace ts2::net

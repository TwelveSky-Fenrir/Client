// Net/RecvPackets_PartyGuild.h — PartyGuild incoming packet structs (S->C), split from RecvPackets.h.
// Framing: [opcode:u8][payload]. Each struct decodes the payload (LE, byte-exact).
// State-update logic (globals/entity arrays) lives in the matching Net/GameHandlers_PartyGuild.cpp.
#pragma once
#include <cstdint>
#include "Asset/ByteReader.h"

namespace ts2::net {

// Net_OnGuildRosterUpdate (opcode 0x4f) — alliance dissolution/departure, roster maintenance.
struct GuildRosterUpdate {
    uint32_t code;    // payload+0 (1/2/3 : different departure/dissolution cases)
    char     name[13];// payload+4 (name concerned, 13 bytes)
    static GuildRosterUpdate Parse(const uint8_t* payload, size_t len);
};

inline GuildRosterUpdate GuildRosterUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildRosterUpdate p{};
    p.code = r.U32();
    r.Read(p.name, sizeof(p.name)); // 13 bytes
    return p;
}

// Net_OnTeamSlotAssign (opcode 0x56) — writes a value into the current team slot.
struct TeamSlotAssign {
    uint32_t value; // payload+0 -> dword_1839EDC[dword_183A020]
    static TeamSlotAssign Parse(const uint8_t* payload, size_t len);
};

inline TeamSlotAssign TeamSlotAssign::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TeamSlotAssign p{};
    p.value = r.U32();
    return p;
}

// Net_OnPartyInviteResult (opcode 0x5d) — party invitation result (5 message cases).
struct PartyInviteResult {
    uint32_t code;  // payload+0 (1..5 : result case)
    uint32_t param; // payload+4 (id/value injected into some messages "[%d]%s")
    static PartyInviteResult Parse(const uint8_t* payload, size_t len);
};

inline PartyInviteResult PartyInviteResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyInviteResult p{};
    p.code  = r.U32();
    p.param = r.U32();
    return p;
}

// Net_OnPartyMemberTargetSet (opcode 0x7b) — sets a party member's target (resolved by network id).
struct PartyMemberTargetSet {
    uint32_t idHi;      // payload+0 (member's network id, high half -> dword_1687238)
    uint32_t idLo;      // payload+4 (network id, low half -> dword_168723C)
    uint32_t targetVal; // payload+8 (target value, stored as u16 in word_1687454)
    static PartyMemberTargetSet Parse(const uint8_t* payload, size_t len);
};

inline PartyMemberTargetSet PartyMemberTargetSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberTargetSet p{};
    p.idHi      = r.U32();
    p.idLo      = r.U32();
    p.targetVal = r.U32();
    return p;
}

// Pkt_AllyInvitePrompt (opcode 0x34 / 52) — ea=0x48FFB0 — guild/ally invitation.
struct AllyInvitePrompt {
    char     name[13];  // payload+0  (inviter's name, fixed 13-byte string)
    uint32_t inviterId; // payload+13 (inviter's id ; dword_1822838 = inviterId)
    static AllyInvitePrompt Parse(const uint8_t* payload, size_t len);
};

inline AllyInvitePrompt AllyInvitePrompt::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    AllyInvitePrompt p{};
    r.Read(p.name, sizeof(p.name));
    p.inviterId = r.U32();  // read at unk_8156CE = payload+13
    return p;
}

// Pkt_PartyInvitePrompt (opcode 0x2e / 46 dec) — party invitation (yes/no prompt).
// Payload : [inviterName:char[13]][flag:u32] = 17 bytes.
struct PartyInvitePrompt {
    char     inviterName[13];  // payload+0 : inviter's name (C string, fixed 13 bytes)
    uint32_t flag;             // payload+13 : 1 = str305, otherwise str426
    // STATE: implemented — Net/GameHandlers_PartyGuild.cpp (0x2e): g_Options.FilterPartyInvite
    //   -> g_Client.prompt.Open(8, ...) ; otherwise Net_SendOp45(2) + message str304.
    static PartyInvitePrompt Parse(const uint8_t* payload, size_t len);
};

inline PartyInvitePrompt PartyInvitePrompt::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyInvitePrompt p{};
    r.Read(p.inviterName, sizeof(p.inviterName));
    p.flag = r.U32();
    return p;
}

// Pkt_AllyInviteDecline (opcode 0x35 / 53 dec) — guild/alliance invitation declined. No payload.
struct AllyInviteDecline {
    // STATE: implemented — Net/GameHandlers_PartyGuild.cpp (0x35): g_Client.prompt.CloseIf(9) +
    //   message str327.
    static AllyInviteDecline Parse(const uint8_t* payload, size_t len);
};

inline AllyInviteDecline AllyInviteDecline::Parse(const uint8_t* /*payload*/, size_t /*len*/) {
    return {};
}

// Net_OnGuildRosterReset (opcode 0x4a / 74 dec) — resets the alliance/guild roster (5 names).
// Payload : [mode:u32][name1..name5:char[13] each] = 69 bytes.
struct GuildRosterReset {
    uint32_t mode;         // payload+0 : 1 -> str349, 2 -> str881
    char     name1[13];    // payload+4
    char     name2[13];    // payload+17
    char     name3[13];    // payload+30
    char     name4[13];    // payload+43
    char     name5[13];    // payload+56
    // STATE: implemented — Net/GameHandlers_PartyGuild.cpp (0x4a): g_World.allianceRoster.Reset()
    //   unconditionally (the 5 names read above are NOT reused, faithful to the original) ;
    //   mode 1 -> str349, mode 2 -> str881.
    static GuildRosterReset Parse(const uint8_t* payload, size_t len);
};

inline GuildRosterReset GuildRosterReset::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildRosterReset p{};
    p.mode = r.U32();
    r.Read(p.name1, sizeof(p.name1));
    r.Read(p.name2, sizeof(p.name2));
    r.Read(p.name3, sizeof(p.name3));
    r.Read(p.name4, sizeof(p.name4));
    r.Read(p.name5, sizeof(p.name5));
    return p;
}

// Pkt_PartyInviteDecline (opcode 0x2f) — party invitation declined (size table = 1, no field).
struct PartyInviteDecline {
    // No field: the packet reads nothing from the payload (opcode only).
    static PartyInviteDecline Parse(const uint8_t* payload, size_t len);
};

inline PartyInviteDecline PartyInviteDecline::Parse(const uint8_t* payload, size_t len) {
    (void)payload; (void)len;
    return PartyInviteDecline{};
}

// Pkt_AllyJoinResult (opcode 0x36) — guild/alliance join result code (0..6).
struct AllyJoinResult {
    uint32_t resultCode;  // payload+0   0=success, 1..6=errors
    static AllyJoinResult Parse(const uint8_t* payload, size_t len);
};

inline AllyJoinResult AllyJoinResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    AllyJoinResult p{};
    p.resultCode = r.U32();
    return p;
}

// Net_OnPartyResultDialog (opcode 0x3d) — party action result code (0..5).
struct PartyResultDialog {
    uint32_t resultCode;  // payload+0   0=success, 1..5=errors
    static PartyResultDialog Parse(const uint8_t* payload, size_t len);
};

inline PartyResultDialog PartyResultDialog::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyResultDialog p{};
    p.resultCode = r.U32();
    return p;
}

// Net_OnGuildMemberJoin (opcode 0x4b) — adds a member to the first free slot of the alliance roster.
struct GuildMemberJoin {
    char name[13];  // payload+0   new member's name (13 bytes, NUL-terminated)
    static GuildMemberJoin Parse(const uint8_t* payload, size_t len);
};

inline GuildMemberJoin GuildMemberJoin::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildMemberJoin p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

// Pkt_PartyJoinResult (opcode 0x30 / 48) — party join result code.
// Payload read: resultCode(+0). Total 4 bytes.
struct PartyJoinResult {
    uint32_t resultCode;   // payload+0 : 0..5
    static PartyJoinResult Parse(const uint8_t* payload, size_t len);
};

inline PartyJoinResult PartyJoinResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyJoinResult p{};
    p.resultCode = r.U32();
    return p;
}

// Pkt_GuildMemberInfo (opcode 0x37 / 55) — guild member/list info block.
// Payload read: field0(+0) blockA[128](+4) blockB[96](+132) field228(+228). Total 232 bytes.
struct GuildMemberInfo {
    uint32_t field0;       // payload+0   (1st arg of UI_ItemListWin_Open: count/type)
    uint8_t  blockA[128];  // payload+4   (main block)
    uint8_t  blockB[96];   // payload+132 (secondary block)
    uint32_t field228;     // payload+228 (4th arg)
    static GuildMemberInfo Parse(const uint8_t* payload, size_t len);
};

inline GuildMemberInfo GuildMemberInfo::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildMemberInfo p{};
    p.field0 = r.U32();
    r.Read(p.blockA, sizeof(p.blockA));
    r.Read(p.blockB, sizeof(p.blockB));
    p.field228 = r.U32();
    return p;
}

// Net_OnPartyMemberNameSet (opcode 0x3e / 62, size table 18) — sets a member's name into a roster slot.
// Payload read: slotIndex(+0) name[13](+4). Total 17 bytes.
struct PartyMemberNameSet {
    uint32_t slotIndex;   // payload+0 : roster slot index
    char     name[13];    // payload+4 : member's name
    static PartyMemberNameSet Parse(const uint8_t* payload, size_t len);
};

inline PartyMemberNameSet PartyMemberNameSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberNameSet p{};
    p.slotIndex = r.U32();
    r.Read(p.name, sizeof(p.name));
    return p;
}

// Net_OnGuildChatMessage (opcode 0x4c / 76) — guild chat message.
// Payload read: senderName[13](+0) message[61](+13). Total 74 bytes.
struct GuildChatMessage {
    char senderName[13];   // payload+0
    char message[61];      // payload+13
    static GuildChatMessage Parse(const uint8_t* payload, size_t len);
};

inline GuildChatMessage GuildChatMessage::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildChatMessage p{};
    r.Read(p.senderName, sizeof(p.senderName));
    r.Read(p.message, sizeof(p.message));
    return p;
}

// Net_OnTeamFormationDispatch (opcode 0x53 / 83, size table 1397) — guild mega-dispatcher (17 sub-opcodes).
// Payload read: statusCode(+0) subOpcode(+4) guildBlob[1388](+8). Total 1396 bytes.
struct TeamFormationDispatch {
    uint32_t statusCode;      // payload+0 : result/status code (v88 ; 0 = success, >0 = error)
    uint32_t subOpcode;       // payload+4 : sub-opcode selector (v86 : 1..17)
    uint8_t  guildBlob[1388]; // payload+8 : roster/guild block (copied to unk_1839970 depending on the case)
    static TeamFormationDispatch Parse(const uint8_t* payload, size_t len);
};

inline TeamFormationDispatch TeamFormationDispatch::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TeamFormationDispatch p{};
    p.statusCode = r.U32();
    p.subOpcode = r.U32();
    r.Read(p.guildBlob, sizeof(p.guildBlob));
    return p;
}

// Net_OnPartyMemberHpSet (opcode 0x7f / 127, size table 21) — sets a party member's current+max HP/MP.
// Payload read: entityIdHi(+0) entityIdLo(+4) kind(+8) curValue(+12) maxValue(+16). Total 20 bytes.
struct PartyMemberHpSet {
    uint32_t entityIdHi;  // payload+0
    uint32_t entityIdLo;  // payload+4
    uint32_t kind;        // payload+8  : 1 or 2 (HP vs MP / display variant)
    int32_t  curValue;    // payload+12 : current value (v10)
    int32_t  maxValue;    // payload+16 : max value (v7)
    static PartyMemberHpSet Parse(const uint8_t* payload, size_t len);
};

inline PartyMemberHpSet PartyMemberHpSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberHpSet p{};
    p.entityIdHi = r.U32();
    p.entityIdLo = r.U32();
    p.kind = r.U32();
    p.curValue = r.I32();
    p.maxValue = r.I32();
    return p;
}

// Net_OnPartyMemberValueSet (opcode 0x3f / 63) — sets a member value and re-scans the roster. Payload 8 bytes.
struct PartyMemberValueSet {
    uint32_t index; // payload+0 (v1) — index into dword_184BE50
    uint32_t value; // payload+4 (v2) — value to write
    static PartyMemberValueSet Parse(const uint8_t* payload, size_t len);
};

inline PartyMemberValueSet PartyMemberValueSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberValueSet p{};
    p.index = r.U32(); p.value = r.U32();
    return p;
}

// Net_OnGuildMemberLeave (opcode 0x4d / 77) — removes a member from the alliance roster and shifts it. Payload 13 bytes (size_table = 14).
struct GuildMemberLeave {
    char name[13]; // payload+0 (v5) — departing member's name (16-byte buffer, 13 read)
    static GuildMemberLeave Parse(const uint8_t* payload, size_t len);
};

inline GuildMemberLeave GuildMemberLeave::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildMemberLeave p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

// Net_OnGuildNoticeChat (opcode 0x54 / 84) — posts a faction/guild chat message. Payload 74 bytes.
struct GuildNoticeChat {
    char name[13];    // payload+0  (v3) — sender's name
    char message[61]; // payload+13 (v2) — text
    static GuildNoticeChat Parse(const uint8_t* payload, size_t len);
};

inline GuildNoticeChat GuildNoticeChat::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildNoticeChat p{};
    r.Read(p.name, sizeof(p.name));
    r.Read(p.message, sizeof(p.message));
    return p;
}

// Net_OnPartyMemberUpdate (opcode 0x80 / 128) — updates a party member field + handles departure. Payload 20 bytes.
struct PartyMemberUpdate {
    uint32_t idHi;     // payload+0  (v4) — entity identity (high), matched against dword_1687238
    uint32_t idLo;     // payload+4  (v5) — entity identity (low),  matched against dword_168723C
    uint32_t selector; // payload+8  (v10) — 1..4 (which field)
    uint32_t value1;   // payload+12 (v9) -> dword_1687458
    uint32_t value2;   // payload+16 (v6) -> dword_168745C
    static PartyMemberUpdate Parse(const uint8_t* payload, size_t len);
};

inline PartyMemberUpdate PartyMemberUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberUpdate p{};
    p.idHi = r.U32(); p.idLo = r.U32(); p.selector = r.U32();
    p.value1 = r.U32(); p.value2 = r.U32();
    return p;
}

// Net_OnPartyMemberClear (opcode 0x40) — clears a party roster slot by index.
// Payload = 4 bytes. (The original fastcall a1 argument is immediately overwritten by the payload memcpy.)
struct PartyMemberClear {
    uint32_t slotIndex;   // payload+0 : roster slot index to reset (g_PartyRosterNames, stride 13 bytes)
    static PartyMemberClear Parse(const uint8_t* payload, size_t len);
};

inline PartyMemberClear PartyMemberClear::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberClear p{};
    p.slotIndex = r.U32();
    return p;
}

// Net_OnGuildMemberKick (opcode 0x4e) — expels the matching name from the alliance roster.
// Payload = 13 bytes.
struct GuildMemberKick {
    char name[13];   // payload+0 : name of the expelled member
    static GuildMemberKick Parse(const uint8_t* payload, size_t len);
};

inline GuildMemberKick GuildMemberKick::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildMemberKick p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

// Net_OnGuildActionResult (opcode 0x5c, size 13 = 1 opcode + 12 payload) — 3-case guild action result.
// Payload = 12 bytes.
struct GuildActionResult {
    uint32_t flag;    // payload+0 : result flag (0 = success depending on the action)
    uint32_t action;  // payload+4 : action selector (1, 2, 3)
    uint32_t extra;   // payload+8 : read but unused in the observed handler
    static GuildActionResult Parse(const uint8_t* payload, size_t len);
};

inline GuildActionResult GuildActionResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildActionResult p{};
    p.flag   = r.U32();
    p.action = r.U32();
    p.extra  = r.U32();
    return p;
}

// Net_OnPartyItemResult (opcode 0x81) — sets an inventory cell / leaves the party.
// Payload = 20 bytes.
struct PartyItemResult {
    uint32_t status;   // payload+0  : 1 = place item, 2 = leave party/return to town
    uint32_t itemId;   // payload+4  : item id placed into the cell
    uint32_t invRow;   // payload+8  : inventory row (384*invRow)
    uint32_t invCol;   // payload+12 : inventory column (6*invCol)
    uint32_t gridPos;  // payload+16 : linear position (gridX = %8, gridY = /8)
    static PartyItemResult Parse(const uint8_t* payload, size_t len);
};

inline PartyItemResult PartyItemResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyItemResult p{};
    p.status  = r.U32();
    p.itemId  = r.U32();
    p.invRow  = r.U32();
    p.invCol  = r.U32();
    p.gridPos = r.U32();
    return p;
}

} // namespace ts2::net

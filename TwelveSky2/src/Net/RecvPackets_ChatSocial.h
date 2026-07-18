// Net/RecvPackets_ChatSocial.h — ChatSocial incoming packet structs (S->C), split from RecvPackets.h.
// Framing: [opcode:u8][payload]. Each struct decodes the payload (LE, byte-exact).
// State-update logic (globals/entity arrays) lives in the matching Net/GameHandlers_ChatSocial.cpp.
#pragma once
#include <cstdint>
#include "Asset/ByteReader.h"

namespace ts2::net {

// Net_OnFactionBoardSync (opcode 0x3a) — faction-quiver resync (status code only).
struct FactionBoardSync {
    uint32_t code; // payload+0 (0 = apply the quiver staging ; 1 = failure, closes the UI)
    static FactionBoardSync Parse(const uint8_t* payload, size_t len);
};

inline FactionBoardSync FactionBoardSync::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    FactionBoardSync p{};
    p.code = r.U32();
    return p;
}

// Net_OnConfirmPromptOpen_Dlg20 (opcode 0x41) — opens confirmation box 20 (with a 13-byte name/text).
struct ConfirmPromptOpen_Dlg20 {
    char nameText[13]; // payload+0 (name string, injected into "[%s]%s")
    static ConfirmPromptOpen_Dlg20 Parse(const uint8_t* payload, size_t len);
};

inline ConfirmPromptOpen_Dlg20 ConfirmPromptOpen_Dlg20::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ConfirmPromptOpen_Dlg20 p{};
    r.Read(p.nameText, sizeof(p.nameText)); // 13 bytes
    return p;
}

// Net_OnConfirmPromptClose_Dlg10 (opcode 0x48) — closes dialog box 10 if active. NO payload field.
struct ConfirmPromptClose_Dlg10 {
    // The handler reads nothing from the payload (no sub_75C740/Crt_Memcpy on unk_8156C1).
    static ConfirmPromptClose_Dlg10 Parse(const uint8_t* payload, size_t len);
};

inline ConfirmPromptClose_Dlg10 ConfirmPromptClose_Dlg10::Parse(const uint8_t* payload, size_t len) {
    (void)payload; (void)len;
    return ConfirmPromptClose_Dlg10{};
}

// Net_OnFriendListEvent (opcode 0x90) — friend add/remove notice (4 cases), with name & class.
struct FriendListEvent {
    uint32_t code;    // payload+0 (0..3 : notice type)
    uint32_t param;   // payload+4 (class/counter depending on the case ; Str_GetClassLabel(param))
    char     name[13];// payload+8 (friend's name, 13 bytes)
    static FriendListEvent Parse(const uint8_t* payload, size_t len);
};

inline FriendListEvent FriendListEvent::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    FriendListEvent p{};
    p.code  = r.U32();
    p.param = r.U32();
    r.Read(p.name, sizeof(p.name)); // 13 bytes
    return p;
}

// Net_OnConfirmPromptOpen_Dlg19 (opcode 0x3b / 59, size 14) — ea=0x4906F0 — opens confirmation dialog 19.
struct ConfirmPromptOpenDlg19 {
    char name[13];  // payload+0 (name shown in the prompt, fixed 13-byte string)
    static ConfirmPromptOpenDlg19 Parse(const uint8_t* payload, size_t len);
};

inline ConfirmPromptOpenDlg19 ConfirmPromptOpenDlg19::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ConfirmPromptOpenDlg19 p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

// Net_OnConfirmPromptClose_Dlg20 (opcode 0x42 / 66) — ea=0x490BB0 — closes dialog 20 if active.
// NO field read from the payload (the handler only touches globals).
struct ConfirmPromptCloseDlg20 {
    static ConfirmPromptCloseDlg20 Parse(const uint8_t* payload, size_t len);
};

inline ConfirmPromptCloseDlg20 ConfirmPromptCloseDlg20::Parse(const uint8_t* payload, size_t len) {
    (void)payload; (void)len;
    return ConfirmPromptCloseDlg20{};
}

// Net_OnResultDialog340 (opcode 0x49 / 73) — ea=0x491090 — 6-case result messages (strings 340..345).
struct ResultDialog340 {
    uint32_t status;  // payload+0 (0..5 -> string 340..345)
    static ResultDialog340 Parse(const uint8_t* payload, size_t len);
};

inline ResultDialog340 ResultDialog340::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ResultDialog340 p{};
    p.status = r.U32();
    return p;
}

// Net_OnConfirmPromptOpen_Dlg14 (opcode 0x50 / 80, size 14) — ea=0x491C10 — opens confirmation dialog 14.
struct ConfirmPromptOpenDlg14 {
    char name[13];  // payload+0 (name shown, fixed 13-byte string)
    static ConfirmPromptOpenDlg14 Parse(const uint8_t* payload, size_t len);
};

inline ConfirmPromptOpenDlg14 ConfirmPromptOpenDlg14::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ConfirmPromptOpenDlg14 p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

// Net_OnSelfFactionChat (opcode 0x57 / 87) — ea=0x4930D0 — posts a faction chat line for a given name.
struct SelfFactionChat {
    char name[13];  // payload+0 (sender's name, fixed 13-byte string)
    static SelfFactionChat Parse(const uint8_t* payload, size_t len);
};

inline SelfFactionChat SelfFactionChat::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SelfFactionChat p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

// Pkt_ChatNotice (opcode 0x14 / 20 dec) — system notice chat line.
// Payload : [text:char[61]] = 61 bytes.
struct ChatNotice {
    char text[61];  // payload+0 : notice text (C string, fixed 61 bytes)
    // STATE: implemented — Net/GameHandlers_ChatSocial.cpp (0x14): g_Client.msg.Floating(0,0,text)
    //   + g_Client.msg.System(text).
    static ChatNotice Parse(const uint8_t* payload, size_t len);
};

inline ChatNotice ChatNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ChatNotice p{};
    r.Read(p.text, sizeof(p.text));
    return p;
}

// Net_OnConfirmPromptClose_Dlg19 (opcode 0x3c / 60 dec) — closes confirmation dialog 19. No payload.
struct ConfirmPromptClose_Dlg19 {
    // STATE: implemented — Net/GameHandlers_ChatSocial.cpp (0x3c): g_Client.prompt.CloseIf(19) +
    //   message str501.
    static ConfirmPromptClose_Dlg19 Parse(const uint8_t* payload, size_t len);
};

inline ConfirmPromptClose_Dlg19 ConfirmPromptClose_Dlg19::Parse(const uint8_t* /*payload*/, size_t /*len*/) {
    return {};
}

// Net_OnTradeResultDialog (opcode 0x43 / 67 dec) — trade result messages (str 511-518).
// Payload : [resultCode:u32] = 4 bytes.
struct TradeResultDialog {
    uint32_t resultCode;  // payload+0 : code 0..7 -> message str511..518
    // STATE: implemented — Net/GameHandlers_ChatSocial.cpp (0x43): CloseNoticeIf(9) then
    //   message str(511+resultCode) ; resultCode==0 also fires Net_SendOp62.
    static TradeResultDialog Parse(const uint8_t* payload, size_t len);
};

inline TradeResultDialog TradeResultDialog::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeResultDialog p{};
    p.resultCode = r.U32();
    return p;
}

// Net_OnConfirmPromptClose_Dlg14 (opcode 0x51 / 81 dec) — closes confirmation dialog 14. No payload.
struct ConfirmPromptClose_Dlg14 {
    // STATE: implemented — Net/GameHandlers_ChatSocial.cpp (0x51): g_Client.prompt.CloseIf(14) +
    //   message str410.
    static ConfirmPromptClose_Dlg14 Parse(const uint8_t* payload, size_t len);
};

inline ConfirmPromptClose_Dlg14 ConfirmPromptClose_Dlg14::Parse(const uint8_t* /*payload*/, size_t /*len*/) {
    return {};
}

// Net_OnNpcDialogEvent (opcode 0x9f / 159 dec) — NPC dialog result messages.
// Payload : [subOpcode:u32][nameStringId:u32] = 8 bytes.
struct NpcDialogEvent {
    uint32_t subOpcode;     // payload+0 : 0..5, selects str2340..2346
    uint32_t nameStringId;  // payload+4 : index into StrTable003 (NPC/object name)
    // STATE: implemented — Net/GameHandlers_ChatSocial.cpp (0x9f): "<name> <str2340..2346 per
    //   subOpcode>" as a floating message + system line ; 2nd line str2341 for subOpcode 0/2.
    static NpcDialogEvent Parse(const uint8_t* payload, size_t len);
};

inline NpcDialogEvent NpcDialogEvent::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    NpcDialogEvent p{};
    p.subOpcode = r.U32();
    p.nameStringId = r.U32();
    return p;
}

// Net_OnRequestTargetNameSet (opcode 0x44) — stores the target name of a request (size table = 18).
struct RequestTargetNameSet {
    uint32_t subop;    // payload+0   1 or 2 (request type)
    char     name[13]; // payload+4   target name (13 bytes, NUL-terminated)
    static RequestTargetNameSet Parse(const uint8_t* payload, size_t len);
};

inline RequestTargetNameSet RequestTargetNameSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RequestTargetNameSet p{};
    p.subop = r.U32();
    r.Read(p.name, sizeof(p.name));
    return p;
}

// Net_OnResultDialog399 (opcode 0x52) — generic result code (0..5, strings 399-404).
struct ResultDialog399 {
    uint32_t resultCode;  // payload+0   0=success, 1..5=errors
    static ResultDialog399 Parse(const uint8_t* payload, size_t len);
};

inline ResultDialog399 ResultDialog399::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ResultDialog399 p{};
    p.resultCode = r.U32();
    return p;
}

// Net_OnWhisperMessage (opcode 0x59) — whisper message, size table = 79.
struct WhisperMessage {
    uint32_t subop;      // payload+0    1=received, 2=sent (echo)
    char     sender[13]; // payload+4    interlocutor's name (13 bytes)
    char     msg[61];    // payload+17   message text (61 bytes)
    static WhisperMessage Parse(const uint8_t* payload, size_t len);
};

inline WhisperMessage WhisperMessage::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WhisperMessage p{};
    p.subop = r.U32();
    r.Read(p.sender, sizeof(p.sender));
    r.Read(p.msg, sizeof(p.msg));
    return p;
}

// Net_OnFriendStatusNotice (opcode 0x7e) — friend online/offline notification.
struct FriendStatusNotice {
    uint32_t subop;    // payload+0    1=online, 2=offline
    char     name[13]; // payload+4    friend's name (13 bytes)
    uint32_t classId;  // payload+17   class/faction id (used as StrTable005_Get(classId+75))
    static FriendStatusNotice Parse(const uint8_t* payload, size_t len);
};

inline FriendStatusNotice FriendStatusNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    FriendStatusNotice p{};
    p.subop = r.U32();
    r.Read(p.name, sizeof(p.name));
    p.classId = r.U32();
    return p;
}

// Pkt_WhisperReceive (opcode 0x29 / 41, size table 75) — incoming whisper/PM line.
// Payload read: senderName[13](+0) message[61](+13). Total 74 bytes.
struct WhisperReceive {
    char senderName[13];   // payload+0
    char message[61];      // payload+13
    static WhisperReceive Parse(const uint8_t* payload, size_t len);
};

inline WhisperReceive WhisperReceive::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WhisperReceive p{};
    r.Read(p.senderName, sizeof(p.senderName));
    r.Read(p.message, sizeof(p.message));
    return p;
}

// Net_OnRequestCancelClear (opcode 0x45 / 69) — request cancellation: clears target names.
// No field read from the payload (payload empty / ignored).
struct RequestCancelClear {
    // no field: the handler reads nothing from the payload
    static RequestCancelClear Parse(const uint8_t* payload, size_t len);
};

inline RequestCancelClear RequestCancelClear::Parse(const uint8_t* payload, size_t len) {
    (void)payload; (void)len;
    return RequestCancelClear{};
}

// Net_OnTradeChatMessage (opcode 0x5a / 90) — trade/commerce chat message.
// Payload read: senderName[13](+0) message[61](+13). Total 74 bytes.
struct TradeChatMessage {
    char senderName[13];   // payload+0
    char message[61];      // payload+13
    static TradeChatMessage Parse(const uint8_t* payload, size_t len);
};

inline TradeChatMessage TradeChatMessage::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeChatMessage p{};
    r.Read(p.senderName, sizeof(p.senderName));
    r.Read(p.message, sizeof(p.message));
    return p;
}

// Pkt_PartyChatOrInvite (opcode 0x2a / 42) — party chat / invitation notification. Payload 86 bytes.
struct PartyChatOrInvite {
    uint32_t selector;    // payload+0  (v13) — 0=joined, 1/2=notices, 3=chat message
    uint32_t flags;       // payload+4  (v10) — auxiliary 4-byte field
    char     name[13];    // payload+8  (v11) — sender/target name (16-byte buffer, 13 read)
    char     message[61]; // payload+21 (v9)  — message text
    uint32_t filterFlag;  // payload+82 (v8)  — case 3: if <1 and the filter is active, message is dropped
    static PartyChatOrInvite Parse(const uint8_t* payload, size_t len);
};

inline PartyChatOrInvite PartyChatOrInvite::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyChatOrInvite p{};
    p.selector = r.U32(); p.flags = r.U32();
    r.Read(p.name, sizeof(p.name));
    r.Read(p.message, sizeof(p.message));
    p.filterFlag = r.U32();
    return p;
}

// Net_OnRequestStateSet (opcode 0x46 / 70) — sets the UI state of a request. Payload 4 bytes.
struct RequestStateSet {
    uint32_t state; // payload+0 (v1) — 0 => dword_1675B14=1 ; 1 => dword_1675B14=2
    static RequestStateSet Parse(const uint8_t* payload, size_t len);
};

inline RequestStateSet RequestStateSet::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RequestStateSet p{};
    p.state = r.U32();
    return p;
}

// Net_OnTradeChatMsg_Ch24 (opcode 0x8b / 139) — posts a message on chat channel 24. Payload 78 bytes.
struct TradeChatMsg {
    uint32_t f0;      // payload+0  (v4) — 4-byte field (unused for display)
    char     name[13];// payload+4  (v2) — sender's name
    char     message[61]; // payload+17 (v1) — text
    static TradeChatMsg Parse(const uint8_t* payload, size_t len);
};

inline TradeChatMsg TradeChatMsg::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    TradeChatMsg p{};
    p.f0 = r.U32();
    r.Read(p.name, sizeof(p.name));
    r.Read(p.message, sizeof(p.message));
    return p;
}

// Pkt_ShoutMessage (opcode 0x2b) — broadcast message (shout / GM).
// Payload = 74 bytes. Fixed-size strings (not necessarily NUL-terminated -> treat as buffers).
struct ShoutMessage {
    char senderName[13];   // payload+0  : sender's name (prefix "[GM]" -> GM color)
    char message[61];      // payload+13 : shout text
    static ShoutMessage Parse(const uint8_t* payload, size_t len);
};

inline ShoutMessage ShoutMessage::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ShoutMessage p{};
    r.Read(p.senderName, sizeof(p.senderName));
    r.Read(p.message, sizeof(p.message));
    return p;
}

// Net_OnConfirmPromptOpen_Dlg10 (opcode 0x47, size 14 = 1 opcode + 13 payload) — opens confirmation dialog 10.
// Payload = 13 bytes.
struct ConfirmPromptOpen_Dlg10 {
    char name[13];   // payload+0 : name shown in the prompt ("[%s]%s")
    static ConfirmPromptOpen_Dlg10 Parse(const uint8_t* payload, size_t len);
};

inline ConfirmPromptOpen_Dlg10 ConfirmPromptOpen_Dlg10::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ConfirmPromptOpen_Dlg10 p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

// Net_OnFactionChatMessage (opcode 0x55) — faction chat message.
// Payload = 74 bytes.
struct FactionChatMessage {
    char senderName[13];   // payload+0  : sender's name
    char message[61];      // payload+13 : text
    static FactionChatMessage Parse(const uint8_t* payload, size_t len);
};

inline FactionChatMessage FactionChatMessage::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    FactionChatMessage p{};
    r.Read(p.senderName, sizeof(p.senderName));
    r.Read(p.message, sizeof(p.message));
    return p;
}

// Net_OnSocialListRemove (opcode 0x79) — removes a name from the friend/block lists (sub-ops 297/298/299).
// Payload = 21 bytes.
struct SocialListRemove {
    uint32_t listOp;    // payload+0 : sub-opcode (297 = list A, 298/299 = lists B/C)
    uint32_t category;  // payload+4 : category/tab index to match (compared against i)
    char     name[13];  // payload+8 : name to remove
    static SocialListRemove Parse(const uint8_t* payload, size_t len);
};

inline SocialListRemove SocialListRemove::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SocialListRemove p{};
    p.listOp   = r.U32();
    p.category = r.U32();
    r.Read(p.name, sizeof(p.name));
    return p;
}

} // namespace ts2::net

// Net/RecvPackets_Core.h — Core incoming packet structs (S->C), split from RecvPackets.h.
// Framing: [opcode:u8][payload]. Each struct decodes the payload (LE, byte-exact).
// State-update logic (globals/entity arrays) lives in the matching Net/GameHandlers_Core.cpp.
#pragma once
#include <cstdint>
#include "Asset/ByteReader.h"

namespace ts2::net {

// Pkt_EnterWorld (opcode 0x0c) — zone/world load, re-inits the entity arrays.
// Payload = 2 contiguous blocks copied wholesale: the char/inventory block (10088 bytes) then the zone-state block (288 bytes).
struct EnterWorld {
    uint8_t selfCharInvBlock[10088]; // payload+0     -> g_SelfCharInvBlock (local character + inventory)
    uint8_t zoneStateBlock[288];     // payload+10088 -> dword_16758D8 (zone state)
    static EnterWorld Parse(const uint8_t* payload, size_t len);
};

inline EnterWorld EnterWorld::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    EnterWorld p{};
    r.Read(p.selfCharInvBlock, sizeof(p.selfCharInvBlock)); // 10088 bytes
    r.Read(p.zoneStateBlock,   sizeof(p.zoneStateBlock));   // 288 bytes
    return p;
}

// Pkt_SmithUpgradeResult (opcode 0x27 / 39 dec) — QUEST INTERACTION RESULT (misnamed).
// Payload : [resultCode:u32][invRow:u32][invSlot:u32][gridX:u32][gridY:u32] = 20 bytes.
struct QuestInteractResult {
    uint32_t resultCode;  // payload+0 : code 1..9, drives step advance / reward / failure
    uint32_t invRow;      // payload+4 : target inventory row/page (-1 = none) (v34)
    uint32_t invSlot;     // payload+8 : target inventory column/slot (v32)
    uint32_t gridX;       // payload+12 : X position in the grid (v36)
    uint32_t gridY;       // payload+16 : Y position in the grid (v33)
    // STATE: implemented — high-level messages in Net/GameHandlers_Misc.cpp (0x27) ; inventory
    //   write [invRow][invSlot] + quest counters (QuestProgressState) in
    //   game::ApplyQuestInteractResultState (Game/QuestSystem.h/.cpp), wired as an override by
    //   Net/GameHandlers_Core.cpp (registered last, complements the handler above without
    //   duplicating it).
    static QuestInteractResult Parse(const uint8_t* payload, size_t len);
};

inline QuestInteractResult QuestInteractResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    QuestInteractResult p{};
    p.resultCode = r.U32();
    p.invRow = r.U32();
    p.invSlot = r.U32();
    p.gridX = r.U32();
    p.gridY = r.U32();
    return p;
}

// Pkt_OnCombatResult (opcode 0x15) — combat result, opaque 76-byte block.
struct OnCombatResult {
    uint8_t block[76]; // payload+0  combat result block (internal structure not decoded here)
    static OnCombatResult Parse(const uint8_t* payload, size_t len);
};

inline OnCombatResult OnCombatResult::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    OnCombatResult p{};
    r.Read(p.block, sizeof(p.block));
    return p;
}

// Net_OnAchievementDataLoad (opcode 0x98) — loads achievement flags (96 bytes).
struct AchievementDataLoad {
    uint8_t flags[96]; // payload+0   flag block, copied into dword_184C218
    static AchievementDataLoad Parse(const uint8_t* payload, size_t len);
};

inline AchievementDataLoad AchievementDataLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    AchievementDataLoad p{};
    r.Read(p.flags, sizeof(p.flags));
    return p;
}

// Pkt_SetGameVar (opcode 0x16 / 22) — 'set game variable' mega-dispatcher (158 cases).
// Payload read: varId(+0) value(+4). Total 8 bytes.
struct SetGameVar {
    uint32_t varId;   // payload+0 : switch selector (1..158)
    int32_t  value;   // payload+4 : value to apply
    static SetGameVar Parse(const uint8_t* payload, size_t len);
};

inline SetGameVar SetGameVar::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SetGameVar p{};
    p.varId = r.U32();
    p.value = r.I32();
    return p;
}

// Net_OnAchievementNotice (opcode 0x99 / 153) — exploit/achievement notification (dword_184C218 table).
// Payload read: name[13](+0). Total 13 bytes.
struct AchievementNotice {
    char name[13];   // payload+0 : name (player/exploit)
    static AchievementNotice Parse(const uint8_t* payload, size_t len);
};

inline AchievementNotice AchievementNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    AchievementNotice p{};
    r.Read(p.name, sizeof(p.name));
    return p;
}

// Pkt_CharStatDelta (opcode 0x11 / 17) — stats/exp/hp/mp delta mega-dispatcher (36 sub-cases).
// Fixed 24-byte payload (size_table = 25 = 24 + opcode byte).
struct CharStatDelta {
    uint32_t idHi;   // payload+0  — entity identity (high), matched against dword_1687238[227*i]
    uint32_t idLo;   // payload+4  — entity identity (low),  matched against dword_168723C[227*i]
    uint32_t subOp;  // payload+8  — sub-opcode (switch 1..36) selecting the field to modify
    uint32_t valA;   // payload+12 — primary delta (v36)
    uint32_t valB;   // payload+16 — secondary value (v39)
    uint32_t valC;   // payload+20 — tertiary value (v43)
    static CharStatDelta Parse(const uint8_t* payload, size_t len);
};

inline CharStatDelta CharStatDelta::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    CharStatDelta p{};
    p.idHi = r.U32(); p.idLo = r.U32(); p.subOp = r.U32();
    p.valA = r.U32(); p.valB = r.U32(); p.valC = r.U32();
    return p;
}

// Pkt_GuildInfoUpdate (opcode 0x38 / 56) — guild info + roster update. Payload 232 bytes (size_table = 233).
struct GuildInfoUpdate {
    uint32_t header;        // payload+0   (v2) — header/counter -> dword_1822848
    uint8_t  block128[128]; // payload+4   (v5) — 128-byte block (guild name/notice) -> dword_182284C
    uint8_t  members[96];   // payload+132 (v3) — 8 members x 12 bytes (3 dwords: id, val1, val2)
    uint32_t footer;        // payload+228 (v7) — final field -> dword_1822934
    static GuildInfoUpdate Parse(const uint8_t* payload, size_t len);
};

inline GuildInfoUpdate GuildInfoUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GuildInfoUpdate p{};
    p.header = r.U32();
    r.Read(p.block128, sizeof(p.block128));
    r.Read(p.members, sizeof(p.members));
    p.footer = r.U32();
    return p;
}

// Net_OnWorldEntityDispatch (opcode 0x5e / 94, on-wire size 105 -> payload 104) —
// ea=0x494870 — MEGA-DISPATCHER (62337 bytes of original code, ~300 sub-cases, state machine
// for local-player combo/posture skill activation). Payload = a u32 sub-opcode (main switch,
// v736 in the disasm) followed by a raw 100-byte block (v702, reinterpreted differently per
// sub-case: u32 idx, secondary u32 idx, 13-byte tag, floats...). Like 0x1a (ItemActionDispatch),
// this packet is a mega-dispatcher outside the generic struct+OnPacket<T> pattern: this struct
// serves as documentation/coverage marker, but Net/WorldEntityDispatch.h (namespace ts2::game)
// re-parses the raw payload directly (same reasons as ItemActionDispatch.h). See that file for
// the detailed map of covered sub-opcodes (1..18, combo families 1 and 2) and the ones
// documented as TODO (the rest, ~284 sub-cases, cf. audit Docs/TS2_PROTOCOL_COMPLETENESS_AUDIT.md §2.4).
struct WorldEntityDispatchHeader {
    uint32_t subOpcode; // payload+0  -> v736 (main switch)
    uint8_t  raw[100];  // payload+4  -> v702[0..99] (raw parameters, sub-opcode dependent)
    static WorldEntityDispatchHeader Parse(const uint8_t* payload, size_t len);
};

inline WorldEntityDispatchHeader WorldEntityDispatchHeader::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    WorldEntityDispatchHeader p{};
    p.subOpcode = r.U32();
    r.Read(p.raw, sizeof(p.raw));
    return p;
}

} // namespace ts2::net

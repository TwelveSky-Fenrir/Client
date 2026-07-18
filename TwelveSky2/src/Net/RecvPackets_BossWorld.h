// Net/RecvPackets_BossWorld.h — BossWorld incoming packet structs (S->C), split from RecvPackets.h.
// Framing: [opcode:u8][payload]. Each struct decodes the payload (LE, byte-exact).
// State-update logic (globals/entity arrays) lives in the matching Net/GameHandlers_BossWorld.cpp.
#pragma once
#include <cstdint>
#include "Asset/ByteReader.h"

namespace ts2::net {

// Net_OnBossHpBarUpdate (opcode 0x9d) — updates the boss HP bar (percentage = value/2).
struct BossHpBarUpdate {
    uint32_t hpRaw; // payload+0 (stored /2 -> dword_1675E9C, as a percentage)
    static BossHpBarUpdate Parse(const uint8_t* payload, size_t len);
};

inline BossHpBarUpdate BossHpBarUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossHpBarUpdate p{};
    p.hpRaw = r.U32();
    return p;
}

// Net_OnRankBoardLoad (opcode 0xb2) — loads the leaderboard table (header + 600-byte body).
struct RankBoardLoad {
    uint32_t header;    // payload+0  -> dword_18260C8 (total count ; pages = header/10)
    uint8_t  body[600]; // payload+4  -> dword_1825E70 (leaderboard body)
    static RankBoardLoad Parse(const uint8_t* payload, size_t len);
};

inline RankBoardLoad RankBoardLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    RankBoardLoad p{};
    p.header = r.U32();
    r.Read(p.body, sizeof(p.body)); // 600 bytes
    return p;
}

// Pkt_ZoneChangeInfo (opcode 0x0d) — ea=0x464500 — zone-change info blob.
// Two contiguous blocks in the payload (contiguous recv buffer: unk_815BED == payload+1324).
struct ZoneChangeInfo {
    uint8_t block1[1324];  // payload+0    (copied into dword_1685E08)
    uint8_t block2[2456];  // payload+1324 (copied into byte_1686334)
    static ZoneChangeInfo Parse(const uint8_t* payload, size_t len);
};

inline ZoneChangeInfo ZoneChangeInfo::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ZoneChangeInfo p{};
    r.Read(p.block1, sizeof(p.block1));
    r.Read(p.block2, sizeof(p.block2));
    return p;
}

// Pkt_SpawnZoneObject (opcode 0x86 / 134) — ea=0x4680F0 — creation/update/removal of a zone object (portal/door).
struct SpawnZoneObject {
    uint32_t idHi;      // payload+0   (object key, high word)
    uint32_t idLo;      // payload+4   (object key, low word)
    uint8_t  body[52];  // payload+8   (object body, copied verbatim)
    uint32_t action;    // payload+60  (2 = create/update, 3 = remove)
    static SpawnZoneObject Parse(const uint8_t* payload, size_t len);
};

inline SpawnZoneObject SpawnZoneObject::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SpawnZoneObject p{};
    p.idHi = r.U32();
    p.idLo = r.U32();
    r.Read(p.body, sizeof(p.body));
    p.action = r.U32();  // read at unk_8156FD = payload+60 (after the 52-byte body)
    return p;
}

// Net_OnBossHpInit (opcode 0x5f / 95, size 21) — ea=0x4A51D0 — initializes boss #1's HP bar (dword_1675BB4).
struct BossHpInit {
    uint32_t a;   // payload+0  (-> dword_1675BBC)
    uint32_t b;   // payload+4  (-> dword_1675BC0 ; returned value)
    uint32_t c;   // payload+8  (-> dword_1675BC4)
    uint32_t d;   // payload+12 (-> dword_1675BC8)
    uint32_t hp;  // payload+16 (max HP ; dword_1675BB4 = hp/2)
    static BossHpInit Parse(const uint8_t* payload, size_t len);
};

inline BossHpInit BossHpInit::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossHpInit p{};
    p.a  = r.U32();  // originally the first 4 u32 are read via a single 16-byte memcpy
    p.b  = r.U32();
    p.c  = r.U32();
    p.d  = r.U32();
    p.hp = r.U32();  // read at unk_8156D1 = payload+16
    return p;
}

// Net_OnBossHpInit2 (opcode 0x67 / 103) — ea=0x4A5C20 — initializes boss #2's HP bar (dword_1675CB8).
// Layout identical to BossHpInit.
struct BossHpInit2 {
    uint32_t a;   // payload+0  (-> dword_1675CC0)
    uint32_t b;   // payload+4  (-> dword_1675CC4 ; returned value)
    uint32_t c;   // payload+8  (-> dword_1675CC8)
    uint32_t d;   // payload+12 (-> dword_1675CCC)
    uint32_t hp;  // payload+16 (max HP ; dword_1675CB8 = hp/2)
    static BossHpInit2 Parse(const uint8_t* payload, size_t len);
};

inline BossHpInit2 BossHpInit2::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossHpInit2 p{};
    p.a  = r.U32();
    p.b  = r.U32();
    p.c  = r.U32();
    p.d  = r.U32();
    p.hp = r.U32();  // read at unk_8156D1 = payload+16
    return p;
}

// Net_OnDataTableLoad_1686CCC (opcode 0x96 / 150) — ea=0x4AC580 — loads a 680-byte UI table (leaderboard).
struct DataTableLoad1686CCC {
    uint32_t status;      // payload+0 (0 = apply the table)
    uint8_t  table[680];  // payload+4 (copied into byte_1686CCC if status==0)
    static DataTableLoad1686CCC Parse(const uint8_t* payload, size_t len);
};

inline DataTableLoad1686CCC DataTableLoad1686CCC::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    DataTableLoad1686CCC p{};
    p.status = r.U32();
    r.Read(p.table, sizeof(p.table));
    return p;
}

// Net_OnBossPanelLoad (opcode 0x9e / 158, size 437) — ea=0x4AD2A0 — loads the boss panel header + body.
struct BossPanelLoad {
    uint32_t header[4];  // payload+0  (16 bytes: -> dword_1675EA0/EA4/EA8/EAC)
    uint8_t  body[420];  // payload+16 (-> dword_1675EB0)
    static BossPanelLoad Parse(const uint8_t* payload, size_t len);
};

inline BossPanelLoad BossPanelLoad::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossPanelLoad p{};
    r.Read(p.header, sizeof(p.header));  // originally a 16-byte memcpy
    r.Read(p.body, sizeof(p.body));      // 420-byte memcpy at unk_8156D1 = payload+16
    return p;
}

// Net_OnZoneBuffStatus (opcode 0x60 / 96 dec) — on/off state of zone buffs (per faction).
// Payload : [flags:u32[4]] = 16 bytes.
struct ZoneBuffStatus {
    uint32_t flags[4];  // payload+0 : 4 on(1)/off flags, indexed by faction (str 75..78)
    // STATE: implemented — Net/GameHandlers_BossWorld.cpp (0x60): aggregated line "[str75..78] ON/OFF"
    //   (`== 1` test @0x4A52E4/0x4A5367/0x4A53EA ; 4th block "[%s] %s" gated by g_SelfMorphNpcId>153
    //   @0x4A5470 — faithfully reproduced) ; if flags[g_LocalElement]==0 -> BeginWarpToFactionTown
    //   (Game/MapWarp.h). (The old note "guard >153 not reproduced" is STALE.)
    static ZoneBuffStatus Parse(const uint8_t* payload, size_t len);
};

inline ZoneBuffStatus ZoneBuffStatus::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    ZoneBuffStatus p{};
    for (int i = 0; i < 4; ++i) p.flags[i] = r.U32();
    return p;
}

// Net_OnBossHpPercent (opcode 0x68 / 104 dec) — boss HP percentage.
// Payload : [hp:u32] = 4 bytes.
struct BossHpPercent {
    uint32_t hp;  // payload+0 : raw value ; the displayed percentage is hp/2
    // STATE: implemented — Net/GameHandlers_BossWorld.cpp (0x68): message "[hp/2]str843".
    static BossHpPercent Parse(const uint8_t* payload, size_t len);
};

inline BossHpPercent BossHpPercent::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossHpPercent p{};
    p.hp = r.U32();
    return p;
}

// Net_OnInstanceEnter (opcode 0xa3) — instance/event entry or result (6 u32).
struct InstanceEnter {
    uint32_t subop; // payload+0    1=entry, 2=result
    uint32_t code;  // payload+4    sub-code (0=ok, 1..3 depending on subop)
    uint32_t p0;    // payload+8    -> dword_1675790
    uint32_t p1;    // payload+12   -> dword_1675794
    uint32_t p2;    // payload+16   -> dword_1675798
    uint32_t p3;    // payload+20   -> dword_167579C
    static InstanceEnter Parse(const uint8_t* payload, size_t len);
};

inline InstanceEnter InstanceEnter::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    InstanceEnter p{};
    p.subop = r.U32();
    p.code  = r.U32();
    p.p0    = r.U32();
    p.p1    = r.U32();
    p.p2    = r.U32();
    p.p3    = r.U32();
    return p;
}

// Net_OnBattlefieldStatus (opcode 0x93 / 147) — zone/war state ; map exit gated by level.
// Payload read (unaligned): subState(u8,+0) warState(u32,+1) param(u32,+5). Total 9 bytes.
struct BattlefieldStatus {
    uint8_t  subState;   // payload+0 : sub-state (v5)
    int32_t  warState;   // payload+1 : war state -> dword_16692A0 (v6)
    int32_t  param;      // payload+5 : parameter (required level / timer) (v8)
    static BattlefieldStatus Parse(const uint8_t* payload, size_t len);
};

inline BattlefieldStatus BattlefieldStatus::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BattlefieldStatus p{};
    p.subState = r.U8();
    p.warState = r.I32();
    p.param = r.I32();
    return p;
}

// Pkt_MapObjectUpdate (opcode 0x17 / 23) — map-object/storage data transfer.
// Fixed 108-byte payload (size_table = 109). Simple relay to Pkt_DispatchStorageResponse(a, b, body).
struct MapObjectUpdate {
    uint32_t a;         // payload+0  (v2) — 1st argument passed to the storage dispatcher
    uint32_t b;         // payload+4  (v1) — 2nd argument
    uint8_t  body[100]; // payload+8  (v3) — opaque data block (100 bytes)
    static MapObjectUpdate Parse(const uint8_t* payload, size_t len);
};

inline MapObjectUpdate MapObjectUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    MapObjectUpdate p{};
    p.a = r.U32(); p.b = r.U32(); r.Read(p.body, sizeof(p.body));
    return p;
}

// Net_OnBossHpDecrement (opcode 0x64 / 100) — decrements the boss counter and updates its HP. Payload 16 bytes.
struct BossHpDecrement {
    uint32_t f0; // payload+0  (v4) -> dword_1675C90
    uint32_t f1; // payload+4  (v5) -> dword_1675C94 (returned value)
    uint32_t f2; // payload+8  (v6) -> dword_1675C98
    uint32_t f3; // payload+12 (v7) -> dword_1675C9C
    static BossHpDecrement Parse(const uint8_t* payload, size_t len);
};

inline BossHpDecrement BossHpDecrement::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossHpDecrement p{};
    p.f0 = r.U32(); p.f1 = r.U32(); p.f2 = r.U32(); p.f3 = r.U32();
    return p;
}

// Net_OnBattlefieldStateChange (opcode 0xaa / 170) — battlefield state change. Payload 5 bytes.
// NOTE: state is 1 byte (no promotion on the inbound side).
struct BattlefieldStateChange {
    uint8_t  state; // payload+0 (v5) — 1 BYTE: 0=opening, 1=closing
    uint32_t value; // payload+1 (v6[0]) -> dword_16692A0 (BG state)
    static BattlefieldStateChange Parse(const uint8_t* payload, size_t len);
};

inline BattlefieldStateChange BattlefieldStateChange::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BattlefieldStateChange p{};
    p.state = r.U8(); p.value = r.U32();
    return p;
}

// Net_OnMountTicketPrompt (opcode 0x9a / 154) — NPC prompt for a mount ticket (items 783..789). Payload 8 bytes (size_table = 9).
struct MountTicketPrompt {
    uint32_t itemId;   // payload+0 (v6) — item id (lookup ItemDefTbl_GetRecord)
    uint32_t strIndex; // payload+4 (v5) — string index (StrTable003)
    static MountTicketPrompt Parse(const uint8_t* payload, size_t len);
};

inline MountTicketPrompt MountTicketPrompt::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    MountTicketPrompt p{};
    p.itemId = r.U32(); p.strIndex = r.U32();
    return p;
}

// Net_OnBossSpawnNotice (opcode 0x65, size 5 = 1 opcode + 4 payload) — boss spawn notification.
// Payload = 4 bytes.
struct BossSpawnNotice {
    uint32_t value;   // payload+0 : value displayed in the message (boss id/index)
    static BossSpawnNotice Parse(const uint8_t* payload, size_t len);
};

inline BossSpawnNotice BossSpawnNotice::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    BossSpawnNotice p{};
    p.value = r.U32();
    return p;
}

// Net_OnDataTableLoad_1686F74 (opcode 0x94, size 685 = 1 opcode + 684 payload) — loads a 680-byte table.
// Payload = 684 bytes.
struct DataTableLoad_1686F74 {
    uint32_t flag;        // payload+0 : if 0 -> copy the table into byte_1686F74
    uint8_t  table[680];  // payload+4 : table contents
    static DataTableLoad_1686F74 Parse(const uint8_t* payload, size_t len);
};

inline DataTableLoad_1686F74 DataTableLoad_1686F74::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    DataTableLoad_1686F74 p{};
    p.flag = r.U32();
    r.Read(p.table, sizeof(p.table));
    return p;
}

// Net_OnHonorRankEvent (opcode 0xa6) — honor rank/PK events.
// Payload = 8 bytes.
struct HonorRankEvent {
    uint32_t category;   // payload+0 : event category (0, 1, 2, 3)
    uint32_t value;      // payload+4 : rank/value (interpreted per category ; -> dword_16760F4 in cat 3)
    static HonorRankEvent Parse(const uint8_t* payload, size_t len);
};

inline HonorRankEvent HonorRankEvent::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    HonorRankEvent p{};
    p.category = r.U32();
    p.value    = r.U32();
    return p;
}

} // namespace ts2::net

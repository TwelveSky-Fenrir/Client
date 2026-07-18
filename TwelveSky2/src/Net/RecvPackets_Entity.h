// Net/RecvPackets_Entity.h — Entity incoming packet structs (S->C), split from RecvPackets.h.
// Framing: [opcode:u8][payload]. Each struct decodes the payload (LE, byte-exact).
// State-update logic (globals/entity arrays) lives in the matching Net/GameHandlers_Entity.cpp.
#pragma once
#include <cstdint>
#include "Asset/ByteReader.h"

namespace ts2::net {

// Pkt_SpawnNpc (opcode 0x13) — creation/update/removal of an NPC.
struct SpawnNpc {
    uint32_t idHi;      // payload+0  -> dword_17AB538 (network id, high half)
    uint32_t idLo;      // payload+4  -> dword_17AB53C (network id, low half)
    uint8_t  body[84];  // payload+8  -> dword_17AB544 (NPC body ; body[0..3] = mob-db model id)
    uint32_t action;    // payload+92 (== 3 : removal/despawn ; otherwise creation/refresh)
    static SpawnNpc Parse(const uint8_t* payload, size_t len);
};

inline SpawnNpc SpawnNpc::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SpawnNpc p{};
    p.idHi = r.U32();
    p.idLo = r.U32();
    r.Read(p.body, sizeof(p.body)); // 84 bytes
    p.action = r.U32();
    return p;
}

// Pkt_GroundItemRemove (opcode 0x19) — decrements/removes a ground item stack (container/slot).
struct GroundItemRemove {
    uint32_t status;         // payload+0 (0 = actual removal ; 1 = simple latch release)
    uint32_t containerIndex; // payload+4 (container index, base *42 into dword_1674400)
    uint32_t slotIndex;      // payload+8 (slot index, base *3)
    static GroundItemRemove Parse(const uint8_t* payload, size_t len);
};

inline GroundItemRemove GroundItemRemove::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    GroundItemRemove p{};
    p.status         = r.U32();
    p.containerIndex = r.U32();
    p.slotIndex      = r.U32();
    return p;
}

// Net_OnPartyMemberPosition (opcode 0x91 / 145 dec) — world position of a party member.
// Payload : [idHi:u32][idLo:u32][gridX:u32][gridY:u32][pos:f32[3]] = 28 bytes.
struct PartyMemberPosition {
    uint32_t idHi;    // payload+0 : network identity, high half (compared to dword_1687238[227*i])
    uint32_t idLo;    // payload+4 : network identity, low half (compared to dword_168723C[227*i])
    uint32_t gridX;   // payload+8 : grid cell X (-> dword_1687478)
    uint32_t gridY;   // payload+12 : grid cell Y (-> dword_168747C)
    float    pos[3];  // payload+16 : world position X,Y,Z (-> unk_1687480/84/88)
    // STATE: implemented — Game/EntityManager.cpp::EntityManager::OnPartyMemberPosition, wired
    //   to opcode 0x91 by Net/GameHandlers_Entity.cpp. Finds the entity via FindPlayer(idHi,
    //   idLo) and writes gridX/gridY/pos into PlayerEntity::body (+ x/y/z mirror).
    static PartyMemberPosition Parse(const uint8_t* payload, size_t len);
};

inline PartyMemberPosition PartyMemberPosition::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    PartyMemberPosition p{};
    p.idHi = r.U32();
    p.idLo = r.U32();
    p.gridX = r.U32();
    p.gridY = r.U32();
    p.pos[0] = r.F32();
    p.pos[1] = r.F32();
    p.pos[2] = r.F32();
    return p;
}

// Pkt_SpawnCharacter (opcode 0x0f) — creation/update of a character record (908-byte record).
// Identifies the entity by the (idHi,idLo) pair then copies a 600-byte body into the slot.
struct SpawnCharacter {
    uint32_t idHi;      // payload+0   network ID, high half (dword_1687238)
    uint32_t idLo;      // payload+4   network ID, low half (dword_168723C)
    uint8_t  body[600]; // payload+8   character body, copied verbatim into dword_168724C
                        //   body+48  : character name (char[], NUL-terminated, <=16 bytes) --
                        //   confirmed via Char_DrawNameplate 0x56EF40 (this+72 == body+48),
                        //   extracted by EntityManager::ReadPlayerName (cf. Game/GameState.h::PlayerEntity::name)
                        //   body+216 : 72-byte move-state block (g_SelfMoveStateBlock)
                        //   body+220 : action-state ; body+272 : anim/stun id (visual buff)
    uint32_t mode;      // payload+608 1=full update, 2=movement, 3=local spawn
    static SpawnCharacter Parse(const uint8_t* payload, size_t len);
};

inline SpawnCharacter SpawnCharacter::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SpawnCharacter p{};
    p.idHi = r.U32();
    p.idLo = r.U32();
    r.Read(p.body, sizeof(p.body));
    p.mode = r.U32();
    return p;
}

// Pkt_CharStateUpdate (opcode 0x10 / 16) — update of a character's state bitfields.
// Payload read: idHi(+0) idLo(+4) stateValues[72](+8, 288 bytes) stateFlags[36](+296, 144 bytes). Total 440 bytes.
struct CharStateUpdate {
    uint32_t entityIdHi;        // payload+0  (compared to dword_1687238[227*i])
    uint32_t entityIdLo;        // payload+4  (compared to dword_168723C[227*i])
    uint32_t stateValues[72];   // payload+8  : 36 pairs [value, extra], indexed v3[2*i]/v3[2*i+1]
    uint32_t stateFlags[36];    // payload+296: 1 = set state i, 2 = clear state i
    static CharStateUpdate Parse(const uint8_t* payload, size_t len);
};

inline CharStateUpdate CharStateUpdate::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    CharStateUpdate p{};
    p.entityIdHi = r.U32();
    p.entityIdLo = r.U32();
    r.Read(p.stateValues, sizeof(p.stateValues));
    r.Read(p.stateFlags, sizeof(p.stateFlags));
    return p;
}

// Pkt_SpawnMonster (opcode 0x12) — creation/update of a monster record.
// Payload = 92 bytes. body[80] is the record copied into dword_1766F84 (stride 70 dwords/280 bytes).
struct SpawnMonster {
    uint32_t idHi;         // payload+0  : network identity key, high half (dword_1766F78)
    uint32_t idLo;         // payload+4  : network identity key, low half (dword_1766F7C)
    uint8_t  body[80];     // payload+8  : monster body (model id up front -> ItemDefTbl_GetRecord)
    uint32_t updateFlag;   // payload+88 : if ==1, reapplies movement state onto an existing monster
    static SpawnMonster Parse(const uint8_t* payload, size_t len);
};

inline SpawnMonster SpawnMonster::Parse(const uint8_t* payload, size_t len) {
    ts2::asset::ByteReader r(payload, len);
    SpawnMonster p{};
    p.idHi = r.U32();
    p.idLo = r.U32();
    r.Read(p.body, sizeof(p.body));
    p.updateFlag = r.U32();
    return p;
}

} // namespace ts2::net

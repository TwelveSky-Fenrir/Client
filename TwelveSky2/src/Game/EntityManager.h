// Game/EntityManager.h — world entity manager (spawn / despawn / update).
//
// Populates and refreshes g_World's entity arrays (players / monsters / NPCs)
// from INCOMING packets decoded in Net/RecvPackets.h. One method per packet
// family. Clean C++ rewrite (not byte-exact) of the original handlers:
//   Pkt_EnterWorld           0x464160  (op 0x0c) -> OnEnterWorld
//   Pkt_SpawnCharacter       0x4646c0  (op 0x0f) -> OnSpawnCharacter
//   Pkt_CharStateUpdate      0x464c10  (op 0x10) -> OnCharStateUpdate
//   Pkt_CharStatDelta        0x465d90  (op 0x11) -> OnCharStatDelta
//   Pkt_SpawnMonster         0x467b00  (op 0x12) -> OnSpawnMonster
//   Pkt_SpawnNpc             0x467ec0  (op 0x13) -> OnSpawnNpc
//   Pkt_GroundItemRemove     0x46a200  (op 0x19) -> OnGroundItemRemove
//   Net_OnPartyMemberPosition 0x4ab9f0 (op 0x91) -> OnPartyMemberPosition
//
// Shared state lives in Game/GameState.h (g_World); only types PROPER to this
// subsystem (pickup grid) are declared here. See RE/net_handler_notes.md and
// Docs/TS2_PROTOCOL_SPEC.md for the original semantics.
#pragma once
#include <cstddef>   // size_t (kPMoveState/kPMoveStateLen below)
#include <cstdint>
#include <vector>
#include "Game/GameState.h"
#include "Net/RecvPackets.h"

namespace ts2::game {

// Offsets of the PLAYER "move-state" block, relative to the slot's body (600 bytes).
// EXPOSED (used to be file-local in EntityManager.cpp) for the intent layer
// Game/PlayerCmdController.*, which reads/writes this same block: in the binary
// this is the global g_SelfMoveStateBlock 0x1687324, and g_EntityArray 0x1687234
// + 0x18 (body) + 216 = 0x1687324 — so this IS ALREADY that same block, not a
// separate state. Cross-checked by body+228 = 0x1687330 = flt_1687330 (local
// player's world position).
// Layout: {animSlot@+0, actionState@+4, animFrame@+8, pos@+12, dest@+24, facing@+36,
//           targetFacing@+40, targetKind@+44, ...} — see TS2_MoveStateBlock (IDB) and
//           Game/PlayerCmdController.h::MoveCmdBlock.
inline constexpr size_t kPMoveState    = 216;  // move-state block (72 bytes) -> g_SelfMoveStateBlock
inline constexpr size_t kPMoveStateLen = 72;

// Ground pickup grid (dword_1674400 / g_Container5_ItemId in the original).
// WARNING: this is NOT the GameWorld::groundItems world-object array
// (dword_1764D14) — it's the small "radar" pickup grid addressed by
// (container, slot), the sole target of the Pkt_GroundItemRemove handler. Each
// slot = triplet {itemId, count, aux}. Container = 14 slots (original stride
// 42 dwords), slot = 3 dwords. Modeled here on its own since absent from GameState.
struct GroundPickupSlot {
    uint32_t itemId = 0;
    uint32_t count  = 0;
    uint32_t aux    = 0;
    bool empty() const { return itemId == 0 && count == 0; }
};

// EntityManager: wires incoming packets to g_World's entity arrays.
class EntityManager {
public:
    // op 0x0c — resets ALL entity arrays then copies the received self/zone
    // blocks (character+inventory 10088 bytes, zone state 288 bytes).
    void OnEnterWorld(const net::EnterWorld& p);

    // op 0x0f — creates/updates a character (index 0 = local player).
    // Returns the created/refreshed slot.
    PlayerEntity* OnSpawnCharacter(const net::SpawnCharacter& p);

    // op 0x12 — creates/updates a monster. Returns the slot, or nullptr if the
    // definition id is invalid while the MONSTER_INFO base is loaded.
    MonsterEntity* OnSpawnMonster(const net::SpawnMonster& p);

    // op 0x13 — creates / refreshes / despawns (action==3) an NPC.
    // Returns the live slot, or nullptr on despawn / rejection. Also fills
    // in x/y/z (body+16/20/24, see Game/EntityManager.cpp).
    NpcEntity* OnSpawnNpc(const net::SpawnNpc& p);

    // op 0x10 — updates a character's state bitfields (36 states).
    void OnCharStateUpdate(const net::CharStateUpdate& p);

    // op 0x11 — stat/HP/MP/level deltas (36-case dispatcher; entity subset).
    void OnCharStatDelta(const net::CharStatDelta& p);

    // op 0x91 — world position of a party member (resolved by network identity).
    void OnPartyMemberPosition(const net::PartyMemberPosition& p);

    // op 0x19 — decrement/removal of a pickup-grid stack.
    void OnGroundItemRemove(const net::GroundItemRemove& p);

    // Bounds-checked access to the pickup grid (state owned by the manager).
    // Returns nullptr if (container,slot) is out of bounds.
    GroundPickupSlot* PickupSlot(uint32_t containerIndex, uint32_t slotIndex);

    // True if the entity is the local player (index 0 of the players array).
    bool IsSelf(const PlayerEntity* e) const;

    // Raw pickup grid (read-only, for UI/tests).
    const std::vector<GroundPickupSlot>& GroundPickup() const { return groundPickup_; }

    // Pickup grid bounds.
    static constexpr uint32_t kSlotsPerContainer = 14;
    static constexpr uint32_t kMaxContainers     = 256;

private:
    std::vector<GroundPickupSlot> groundPickup_;
};

// Single global instance (mirroring the original handlers operating on globals).
inline EntityManager g_EntityManager;

} // namespace ts2::game

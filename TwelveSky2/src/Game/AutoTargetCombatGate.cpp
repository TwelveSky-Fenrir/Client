// Game/AutoTargetCombatGate.cpp — see AutoTargetCombatGate.h for the EA -> function table,
// fidelity notes (addresses dword_1675B24/28/2C, pools 4/7), and the reuse policy
// (Combat_IsElementAllowedOnMap already ported by Game/ComboPickupTick.h).
#include "Game/AutoTargetCombatGate.h"
#include <cmath>

namespace ts2::game {

namespace {

// 3D Euclidean distance (Math_Dist3D 0x53FAA0: sqrt(dx^2+dy^2+dz^2)), same formula as
// Game/ComboPickupTick.cpp / Game/ItemPickupSystem.cpp / Game/NpcInteraction.cpp
// (redeclared locally, not exported elsewhere — same established convention).
inline float Dist3D(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

// AutoTarget_DefaultRangeLookup — see the .h banner for the offset justification and the
// address proof. mode 4 == g_World.groundItems (exact storage of the g_NpcRenderArray/
// dword_1764D14 pool, RE-CONFIRMED this mission by fresh decompilation of
// Item_PickupTarget 0x539EC0 + World_PickEntityAtCursor 0x538AB0 -- THIS POOL IS ACTUALLY AN
// NPC ARRAY, NOT GROUND OBJECTS, despite the field name inherited from GameState.h; see the
// detailed semantic fix in the .h), mode 7 == g_World.zoneObjects. NEITHER tests slot
// activity in the binary (EA 0x52cd91/0x52ce6b) -- only the bounds-check (needed for a
// lazily-growing std::vector on the ClientSource side) is applied.
bool AutoTarget_DefaultRangeLookup(const GameWorld& world, int mode, int index, float outPos[3]) {
    if (mode == 4) { // EA 0x52cd91: unk_1764D28 == g_World.groundItems[index].x (exact storage
                      // of the NPC pool g_NpcRenderArray -- see the semantic fix in the .h)
        if (index < 0 || static_cast<std::size_t>(index) >= world.npcRenderEntries.size()) return false;
        const NpcRenderEntry& gi = world.npcRenderEntries[static_cast<std::size_t>(index)];
        outPos[0] = gi.x; outPos[1] = gi.y; outPos[2] = gi.z;
        return true;
    }
    if (mode == 7) { // EA 0x52ce6b
        if (index < 0 || static_cast<std::size_t>(index) >= world.zoneObjects.size()) return false;
        const ZoneObjectEntity& z = world.zoneObjects[static_cast<std::size_t>(index)];
        std::memcpy(outPos, z.body.data(), sizeof(float) * 3);
        return true;
    }
    return false;
}

// ValidateAutoTarget — EA 0x52CCA7..0x52CE77 (switch dword_1675B24) of Scene_InGameUpdate.
void ValidateAutoTarget(GameWorld& world, const AutoTargetRangeLookup& rangedLookupIn) {
    const int32_t mode = g_Client.VarGet(kAutoTargetModeAddr);

    // Effective oracle: the one supplied by the caller, otherwise the default fallback
    // (modes 4 AND 7, see the .h banner) wired to THIS `world`.
    auto rangedLookup = [&](int m, int idx, float outPos[3]) -> bool {
        if (rangedLookupIn) return rangedLookupIn(m, idx, outPos);
        return AutoTarget_DefaultRangeLookup(world, m, idx, outPos);
    };

    switch (mode) {
    case 1: // EA 0x52cca7 — 3 values treated strictly identically in the binary
    case 2:
    case 3: {
        const int32_t wantHi = g_Client.VarGet(kAutoTargetIdHiAddr);
        const int32_t wantLo = g_Client.VarGet(kAutoTargetIdLoAddr);
        bool found = false;
        // kk = 1: index 0 (self) is skipped, faithful to `for (kk=1; kk<g_EntityCount; ...)`.
        for (std::size_t i = 1; i < world.players.size(); ++i) {
            const PlayerEntity& p = world.players[i];
            if (!p.active) continue;
            if (!AutoTarget_PlayerRecordPopulated(p)) continue;
            if (!Combat_IsTargetablePlayerState(p.anim.state)) continue;
            if (static_cast<int32_t>(p.id.hi) != wantHi) continue;
            if (static_cast<int32_t>(p.id.lo) != wantLo) continue;
            found = true;
            break;
        }
        if (!found) g_Client.Var(kAutoTargetModeAddr) = 0; // EA 0x52cd53
        break;
    }
    case 5: { // EA 0x52cda7
        const int32_t wantHi = g_Client.VarGet(kAutoTargetIdHiAddr);
        const int32_t wantLo = g_Client.VarGet(kAutoTargetIdLoAddr);
        bool found = false;
        for (const MonsterEntity& m : world.monsters) {
            if (!m.active) continue;
            if (!Combat_IsTargetableMonsterState(AutoTarget_MonsterActionState(m))) continue;
            if (static_cast<int32_t>(m.id.hi) != wantHi) continue;
            if (static_cast<int32_t>(m.id.lo) != wantLo) continue;
            found = true;
            break;
        }
        if (!found) g_Client.Var(kAutoTargetModeAddr) = 0; // EA 0x52ce35
        break;
    }
    case 4:   // EA 0x52cd91 — raw index into g_World.groundItems (storage of the real NPC
              // pool, see the semantic fix in the .h banner)
    case 7: { // EA 0x52ce6b — raw index into ZoneObjectEntity (g_World.zoneObjects)
        const int32_t index = g_Client.VarGet(kAutoTargetIdHiAddr);
        float pos[3] = {0.0f, 0.0f, 0.0f};
        const bool ok = rangedLookup(mode, index, pos);
        const PlayerEntity& self = world.Self();
        if (!ok || Dist3D(pos[0], pos[1], pos[2], self.x, self.y, self.z) > kAutoTargetRangeLimit)
            g_Client.Var(kAutoTargetModeAddr) = 0;
        break;
    }
    default:
        // default switch EA (0/6/other): no action, faithful.
        break;
    }
}

// IsCombatAllowedOnMapForSelf — see the .h banner.
bool IsCombatAllowedOnMapForSelf(const GameWorld& world, const ElementPairTable& pairs) {
    const int mapElement     = world.self.element;             // g_LocalElement 0x1673194
    const int selfMorphNpcId = g_Client.VarGet(0x1675A98u);     // g_SelfMorphNpcId
    return IsCombatAllowedOnMap(mapElement, selfMorphNpcId, pairs);
}

} // namespace ts2::game

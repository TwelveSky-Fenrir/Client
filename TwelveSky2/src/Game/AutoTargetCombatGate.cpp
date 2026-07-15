// Game/AutoTargetCombatGate.cpp — voir AutoTargetCombatGate.h pour la table EA -> fonction,
// les notes de fidélité (adresses dword_1675B24/28/2C, pools 4/7) et la politique de
// réutilisation (Combat_IsElementAllowedOnMap déjà porté par Game/ComboPickupTick.h).
#include "Game/AutoTargetCombatGate.h"
#include <cmath>

namespace ts2::game {

namespace {

// Distance euclidienne 3D (Math_Dist3D 0x53FAA0 : sqrt(dx^2+dy^2+dz^2)), même formule que
// Game/ComboPickupTick.cpp / Game/ItemPickupSystem.cpp / Game/NpcInteraction.cpp
// (redéclarée localement, non exportée ailleurs — même convention établie).
inline float Dist3D(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

// ===========================================================================
// AutoTarget_DefaultRangeLookup — cf. bandeau .h pour la justification des offsets et la
// preuve d'adresse. mode 4 == g_World.groundItems (storage exact du pool g_NpcRenderArray/
// dword_1764D14, RE-CONFIRMÉ cette mission par décompilation fraîche de Item_PickupTarget
// 0x539EC0 + World_PickEntityAtCursor 0x538AB0 -- CE POOL EST EN RÉALITÉ UN TABLEAU DE NPCs,
// PAS D'OBJETS AU SOL, malgré le nom du champ hérité de GameState.h ; cf. correctif
// sémantique détaillé dans le .h), mode 7 == g_World.zoneObjects. NI L'UN NI L'AUTRE ne teste
// l'activité du slot dans le binaire (EA 0x52cd91/0x52ce6b) -- seul le bounds-check
// (nécessaire pour un std::vector à croissance paresseuse côté ClientSource) est appliqué.
// ===========================================================================
bool AutoTarget_DefaultRangeLookup(const GameWorld& world, int mode, int index, float outPos[3]) {
    if (mode == 4) { // EA 0x52cd91 : unk_1764D28 == g_World.groundItems[index].x (storage exact
                      // du pool NPC g_NpcRenderArray -- cf. correctif sémantique du .h)
        if (index < 0 || static_cast<std::size_t>(index) >= world.groundItems.size()) return false;
        const GroundItem& gi = world.groundItems[static_cast<std::size_t>(index)];
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

// ===========================================================================
// ValidateAutoTarget — EA 0x52CCA7..0x52CE77 (switch dword_1675B24) de Scene_InGameUpdate.
// ===========================================================================
void ValidateAutoTarget(GameWorld& world, const AutoTargetRangeLookup& rangedLookupIn) {
    const int32_t mode = g_Client.VarGet(kAutoTargetModeAddr);

    // Oracle effectif : celui fourni par l'appelant, sinon le repli par défaut (modes 4 ET 7,
    // cf. bandeau .h) branché sur CE `world`.
    auto rangedLookup = [&](int m, int idx, float outPos[3]) -> bool {
        if (rangedLookupIn) return rangedLookupIn(m, idx, outPos);
        return AutoTarget_DefaultRangeLookup(world, m, idx, outPos);
    };

    switch (mode) {
    case 1: // EA 0x52cca7 — 3 valeurs traitées de façon strictement identique dans le binaire
    case 2:
    case 3: {
        const int32_t wantHi = g_Client.VarGet(kAutoTargetIdHiAddr);
        const int32_t wantLo = g_Client.VarGet(kAutoTargetIdLoAddr);
        bool found = false;
        // kk = 1 : l'index 0 (self) est ignoré, fidèle à `for (kk=1; kk<g_EntityCount; ...)`.
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
    case 4:   // EA 0x52cd91 — index brut dans g_World.groundItems (storage du pool NPC réel,
              // cf. correctif sémantique du bandeau .h)
    case 7: { // EA 0x52ce6b — index brut dans ZoneObjectEntity (g_World.zoneObjects)
        const int32_t index = g_Client.VarGet(kAutoTargetIdHiAddr);
        float pos[3] = {0.0f, 0.0f, 0.0f};
        const bool ok = rangedLookup(mode, index, pos);
        const PlayerEntity& self = world.Self();
        if (!ok || Dist3D(pos[0], pos[1], pos[2], self.x, self.y, self.z) > kAutoTargetRangeLimit)
            g_Client.Var(kAutoTargetModeAddr) = 0;
        break;
    }
    default:
        // EA défaut du switch (0/6/autre) : aucune action, fidèle.
        break;
    }
}

// ===========================================================================
// IsCombatAllowedOnMapForSelf — cf. bandeau .h.
// ===========================================================================
bool IsCombatAllowedOnMapForSelf(const GameWorld& world, const ElementPairTable& pairs) {
    const int mapElement     = world.self.element;             // g_LocalElement 0x1673194
    const int selfMorphNpcId = g_Client.VarGet(0x1675A98u);     // g_SelfMorphNpcId
    return IsCombatAllowedOnMap(mapElement, selfMorphNpcId, pairs);
}

} // namespace ts2::game

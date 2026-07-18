// Game/GameState.cpp — entity slot lookup/allocation (linear scan + first free slot).
#include "Game/GameState.h"

namespace ts2::game {

template <class Vec>
static typename Vec::value_type* FindOrAdd(Vec& v, EntityId id) {
    for (auto& e : v)
        if (e.active && e.id == id) return &e;      // existing active slot
    for (auto& e : v)
        if (!e.active) {                            // reuse 1st free slot
            e = {};
            e.active = true;
            e.id = id;
            return &e;
        }
    v.emplace_back();                               // otherwise grow the vector
    auto& e = v.back();
    e.active = true;
    e.id = id;
    return &e;
}

PlayerEntity*  GameWorld::FindOrAddPlayer(EntityId id)  { return FindOrAdd(players, id); }
MonsterEntity* GameWorld::FindOrAddMonster(EntityId id) { return FindOrAdd(monsters, id); }
NpcEntity*     GameWorld::FindOrAddNpc(EntityId id)     { return FindOrAdd(npcs, id); }

} // namespace ts2::game

// Game/GameState.cpp — recherche/allocation de slots d'entités (scan linéaire + 1er libre).
#include "Game/GameState.h"

namespace ts2::game {

template <class Vec>
static typename Vec::value_type* FindOrAdd(Vec& v, EntityId id) {
    for (auto& e : v)
        if (e.active && e.id == id) return &e;      // slot existant actif
    for (auto& e : v)
        if (!e.active) {                            // 1er slot libre réutilisé
            e = {};
            e.active = true;
            e.id = id;
            return &e;
        }
    v.emplace_back();                               // sinon on agrandit
    auto& e = v.back();
    e.active = true;
    e.id = id;
    return &e;
}

PlayerEntity*  GameWorld::FindOrAddPlayer(EntityId id)  { return FindOrAdd(players, id); }
MonsterEntity* GameWorld::FindOrAddMonster(EntityId id) { return FindOrAdd(monsters, id); }
NpcEntity*     GameWorld::FindOrAddNpc(EntityId id)     { return FindOrAdd(npcs, id); }

} // namespace ts2::game

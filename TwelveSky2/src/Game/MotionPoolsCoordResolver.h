// Game/MotionPoolsCoordResolver.h — implémentation concrète de
// game::IFactionTownCoordResolver (Game/MapWarp.h, module leaf) au-dessus de la
// table de coordonnées chargée par Game/MotionPools.h (LoadGInfo003Bin, mZONEMOVEINFO).
// Fichier d'intégration séparé (pas dans MapWarp.h) pour que ce dernier reste un
// module feuille sans dépendance vers MotionPools.
#pragma once
#include "Game/MapWarp.h"
#include "Game/MotionPools.h"

namespace ts2::game {

class MotionPoolsCoordResolver : public IFactionTownCoordResolver {
public:
    // Fidèle à l'appel d'origine (Motion_GetComboOffsetTable puis fallback
    // GInfo2_GetVec3) : ici on n'a que le fallback câblé (GetVec3 sur la table
    // 003.BIN) — `element` n'est pas consommé par GetVec3 (déjà documenté dans
    // MotionPools.h : la table est indexée par motion/NPC id, pas par élément).
    bool ResolveTownCoords(int32_t /*element*/, int32_t townNpcId,
                           float& x, float& y, float& z) const override {
        return GetVec3(townNpcId, x, y, z);
    }
};

// Instance globale unique, prête à l'emploi partout où BeginWarpToFactionTown
// est appelée sans résolveur explicite.
inline MotionPoolsCoordResolver g_CoordResolver;

} // namespace ts2::game

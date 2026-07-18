// Game/MotionPoolsCoordResolver.h — concrete implementation of
// game::IFactionTownCoordResolver (Game/MapWarp.h, leaf module) on top of the
// coordinate table loaded by Game/MotionPools.h (LoadGInfo003Bin, mZONEMOVEINFO).
// Separate integration file (not in MapWarp.h) so the latter stays a
// leaf module with no dependency on MotionPools.
#pragma once
#include "Game/MapWarp.h"
#include "Game/MotionPools.h"

namespace ts2::game {

class MotionPoolsCoordResolver : public IFactionTownCoordResolver {
public:
    // Faithful to the original call (Motion_GetComboOffsetTable then fallback
    // GInfo2_GetVec3): only the fallback is wired up here (GetVec3 on the
    // 003.BIN table) — `element` is not consumed by GetVec3 (already documented in
    // MotionPools.h: the table is indexed by motion/NPC id, not by element).
    bool ResolveTownCoords(int32_t /*element*/, int32_t townNpcId,
                           float& x, float& y, float& z) const override {
        return GetVec3(townNpcId, x, y, z);
    }
};

// Single global instance, ready to use anywhere BeginWarpToFactionTown
// is called without an explicit resolver.
inline MotionPoolsCoordResolver g_CoordResolver;

} // namespace ts2::game

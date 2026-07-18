// Game/AnimationTick_Internal.h — shared file-local helpers for the Game/AnimationTick.*
// split family (AnimationTick.cpp / AnimationTick_World.cpp / AnimationTick_Entities.cpp).
// Included only by that family; not part of the public Game/AnimationTick.h API. Everything
// below is inline/constexpr inside an anonymous namespace (internal linkage per TU, so no ODR
// risk from being included by multiple .cpp files) — same convention as
// Game/StatFormulas_Internal.h.
#pragma once
#include <cstdint>
#include "Game/AnimationTick.h"   // IMorphModelOracle, LocalAnimTickHost
#include "Game/ClientRuntime.h"  // g_Client.Var/VarF (long-tail globals escape hatch)
#include "Game/MapWarp.h"        // BeginWarpToFactionTown, WarpAddr::SelfMorphNpcId

namespace ts2::game {
namespace {

// oracle==nullptr -> "infinite" duration: the timer advances but never completes (clean
// degradation, cf. Game/AnimationTick.h).
inline float MorphDuration(const IMorphModelOracle* oracle, uint32_t tableAddr) {
    return oracle ? static_cast<float>(oracle->GetSubObjectCount(tableAddr)) : 1.0e9f;
}

// 75-row "generic" table of Player_UpdateLocalAnim (0x5321D0), mechanically
// extracted from the decompilation (see mission: extraction script, verified line by
// line). 3 additional rows indexed/tabled by g_SelfMorphNpcId (blocks
// 0x1675BA4/BDC/BE4) and 1 row indexed by g_LocalElement (block 0x1675D98/DA8) are OUTSIDE
// this table (handled separately in Player_UpdateLocalAnim, see further down) + 3 "pulse"
// blocks (cadence this+8 %6, independent of ModelObj_GetSubObjectCount) also outside
// the table.
struct MorphTimerRow {
    uint32_t flagAddr;
    uint32_t frameAddr;
    uint32_t tableAddr;
    bool     grow;            // true = grows toward `duration`; false = decays toward 0
    bool     clampOnComplete; // grow only: frame = duration-1 once complete
    int32_t  loadArg;         // -1 = no World_LoadCurrentZoneModel call
    bool     warpCheck;       // shrink only: + World_IsPointOnGround/BeginWarp
};

constexpr MorphTimerRow kMorphRows[] = {
    {0x1675BEC, 0x1675BF0, 0xB6201C, true,  false, -1, false},
    {0x1675BF4, 0x1675BF8, 0xB620B0, true,  false, -1, false},
    {0x1675BFC, 0x1675C00, 0xB65830, true,  false, -1, false},
    {0x1675C04, 0x1675C08, 0xB663C0, true,  false, -1, false},
    {0x1675C0C, 0x1675C5C, 0xB668F4, true,  true,   2, false},
    {0x1675C34, 0x1675C5C, 0xB668F4, false, true,  -1, false},
    {0x1675C10, 0x1675C60, 0xB66988, true,  true,  -1, false},
    {0x1675C38, 0x1675C60, 0xB66988, false, true,   3, true },
    {0x1675C14, 0x1675C64, 0xB66988, true,  true,   4, false},
    {0x1675C3C, 0x1675C64, 0xB66988, false, true,  -1, false},
    {0x1675C18, 0x1675C68, 0xB66A1C, true,  true,  -1, false},
    {0x1675C40, 0x1675C68, 0xB66A1C, false, true,   5, true },
    {0x1675C1C, 0x1675C6C, 0xB66A1C, true,  true,   6, false},
    {0x1675C44, 0x1675C6C, 0xB66A1C, false, true,  -1, false},
    {0x1675C20, 0x1675C70, 0xB66AB0, true,  true,  -1, false},
    {0x1675C48, 0x1675C70, 0xB66AB0, false, true,   7, true },
    {0x1675C24, 0x1675C74, 0xB66AB0, true,  true,   8, false},
    {0x1675C4C, 0x1675C74, 0xB66AB0, false, true,  -1, false},
    {0x1675C28, 0x1675C78, 0xB66B44, true,  true,  -1, false},
    {0x1675C50, 0x1675C78, 0xB66B44, false, true,   9, true },
    {0x1675C2C, 0x1675C7C, 0xB66B44, true,  true,  10, false},
    {0x1675C54, 0x1675C7C, 0xB66B44, false, true,  -1, false},
    {0x1675C30, 0x1675C80, 0xB66BD8, true,  true,  -1, false},
    {0x1675C58, 0x1675C80, 0xB66BD8, false, true,  11, true },
    {0x1675C84, 0x1675C88, 0xB66C6C, true,  true,   2, false},
    {0x1675CA0, 0x1675CA4, 0xB6201C, true,  false, -1, false},
    {0x1675CA8, 0x1675CAC, 0xB620B0, true,  false, -1, false},
    {0x1675CB0, 0x1675CB4, 0xB67C08, true,  true,   2, false},
    {0x1675CD0, 0x1675CD4, 0xB6882C, true,  true,   2, false},
    {0x1675CDC, 0x1675CFC, 0xB68BA4, true,  true,   2, false},
    {0x1675CE0, 0x1675D00, 0xB68BA4, true,  true,   2, false},
    {0x1675CE4, 0x1675D04, 0xB68BA4, true,  true,   2, false},
    {0x1675CE8, 0x1675D08, 0xB68BA4, true,  true,   2, false},
    {0x1675CEC, 0x1675D0C, 0xB68BA4, true,  true,   3, false},
    {0x1675CF0, 0x1675D10, 0xB68BA4, true,  true,   3, false},
    {0x1675CF4, 0x1675D14, 0xB68BA4, true,  true,   3, false},
    {0x1675CF8, 0x1675D18, 0xB68BA4, true,  true,   3, false},
    {0x1675DC8, 0x1675DD0, 0xB65F20, true,  true,  -1, false},
    {0x1675DCC, 0x1675DD4, 0xB65FB4, true,  true,  -1, false},
    {0x1675DD8, 0x1675DE8, 0xB68DF4, true,  true,   2, false},
    {0x1675DDC, 0x1675DEC, 0xB68DF4, true,  true,   3, false},
    {0x1675DE0, 0x1675DF0, 0xB68DF4, true,  true,   4, false},
    {0x1675DE4, 0x1675DF4, 0xB68DF4, true,  true,   5, false},
    {0x1675D30, 0x1675D50, 0xB68BA4, true,  true,   2, false},
    {0x1675D34, 0x1675D54, 0xB68BA4, true,  true,   2, false},
    {0x1675D38, 0x1675D58, 0xB68BA4, true,  true,   2, false},
    {0x1675D3C, 0x1675D5C, 0xB68BA4, true,  true,   2, false},
    {0x1675D40, 0x1675D60, 0xB68BA4, true,  true,   3, false},
    {0x1675D44, 0x1675D64, 0xB68BA4, true,  true,   3, false},
    {0x1675D48, 0x1675D68, 0xB68BA4, true,  true,   3, false},
    {0x1675D4C, 0x1675D6C, 0xB68BA4, true,  true,   3, false},
    {0x1675DF8, 0x1675E48, 0xB668F4, true,  true,   2, false},
    {0x1675E20, 0x1675E48, 0xB668F4, false, true,  -1, false},
    {0x1675DFC, 0x1675E4C, 0xB66988, true,  true,  -1, false},
    {0x1675E24, 0x1675E4C, 0xB66988, false, true,   3, true },
    {0x1675E00, 0x1675E50, 0xB66988, true,  true,   4, false},
    {0x1675E28, 0x1675E50, 0xB66988, false, true,  -1, false},
    {0x1675E04, 0x1675E54, 0xB66A1C, true,  true,  -1, false},
    {0x1675E2C, 0x1675E54, 0xB66A1C, false, true,   5, true },
    {0x1675E08, 0x1675E58, 0xB66A1C, true,  true,   6, false},
    {0x1675E30, 0x1675E58, 0xB66A1C, false, true,  -1, false},
    {0x1675E0C, 0x1675E5C, 0xB66AB0, true,  true,  -1, false},
    {0x1675E34, 0x1675E5C, 0xB66AB0, false, true,   7, true },
    {0x1675E10, 0x1675E60, 0xB66AB0, true,  true,   8, false},
    {0x1675E38, 0x1675E60, 0xB66AB0, false, true,  -1, false},
    {0x1675E14, 0x1675E64, 0xB66B44, true,  true,  -1, false},
    {0x1675E3C, 0x1675E64, 0xB66B44, false, true,   9, true },
    {0x1675E18, 0x1675E68, 0xB66B44, true,  true,  10, false},
    {0x1675E40, 0x1675E68, 0xB66B44, false, true,  -1, false},
    {0x1675E1C, 0x1675E6C, 0xB66BD8, true,  true,  -1, false},
    {0x1675E44, 0x1675E6C, 0xB66BD8, false, true,  11, true },
    {0x1675E70, 0x1675E80, 0xB69200, true,  true,   2, false},
    {0x1675E74, 0x1675E84, 0xB69200, true,  true,   2, false},
    {0x1675E78, 0x1675E88, 0xB69200, true,  true,   2, false},
    {0x1675E7C, 0x1675E8C, 0xB69200, true,  true,   2, false},
};

// Applies one row of `kMorphRows`: reads/writes game::g_Client.Var/VarF AT THE
// ORIGINAL ADDRESSES (same keys as the binary), advances at dt*30 frames/s, and triggers
// World_LoadCurrentZoneModel / World_IsPointOnGround+Map_BeginWarpToFactionTown at the end
// of the run depending on the row. `selfElement`/`selfPos` = game::g_World.self.element /
// Self().xyz.
inline void TickMorphRow(const MorphTimerRow& row, float dt, const IMorphModelOracle* oracle,
                   const LocalAnimTickHost& host, int32_t selfElement,
                   float selfX, float selfY, float selfZ) {
    int32_t& flag = g_Client.Var(row.flagAddr);
    if (!flag) return;

    float& frame = g_Client.VarF(row.frameAddr);
    const float duration = MorphDuration(oracle, row.tableAddr);

    if (row.grow) {
        frame += dt * 30.0f;
        if (frame < duration) return;
        flag = 0;
        if (row.clampOnComplete) frame = duration - 1.0f;
        if (row.loadArg >= 0 && host.LoadCurrentZoneModel) host.LoadCurrentZoneModel(row.loadArg);
        // No "grow" row has warpCheck==true in the original table (reserved for shrink).
    } else {
        frame -= dt * 30.0f;
        if (frame >= 0.0f) return;
        flag = 0;
        frame = 0.0f;
        if (row.loadArg >= 0 && host.LoadCurrentZoneModel) host.LoadCurrentZoneModel(row.loadArg);
        if (row.warpCheck) {
            const bool onGround = host.IsPointOnGround && host.IsPointOnGround(selfX, selfY, selfZ);
            if (!onGround) BeginWarpToFactionTown(selfElement);
        }
    }
}

} // namespace
} // namespace ts2::game

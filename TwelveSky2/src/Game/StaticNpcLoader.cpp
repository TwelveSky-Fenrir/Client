// Game/StaticNpcLoader.cpp — voir StaticNpcLoader.h pour le contexte complet et
// Docs/TS2_NPC_ZONE_LOADER_TRIGGER.md pour la preuve de decompilation.
#include "Game/StaticNpcLoader.h"
#include "Game/MotionPools.h"
#include "Core/Log.h"

namespace ts2::game {

namespace {
std::vector<StaticNpcSlot> g_zoneNpcs;
int g_currentZoneId = 0;
} // namespace

bool LoadZoneNpcs(int zoneId1Based) {
    g_zoneNpcs.clear();
    g_currentZoneId = 0;

    const int count = ZoneNpcCount(zoneId1Based);
    if (count <= 0) {
        // Zone hors bornes [1,350], table mZONENPCINFO non chargee, ou zone sans PNJ
        // statique -- pas une erreur (fidele : cGameData_LoadZoneNpcInfo ne fait
        // simplement rien de plus si mZONENPCINFO[zoneId].count == 0).
        g_currentZoneId = zoneId1Based;
        return true;
    }

    g_zoneNpcs.reserve(static_cast<size_t>(count));
    int skipped = 0;
    for (int i = 0; i < count; ++i) {
        const uint32_t kindId = ZoneNpcKindId(zoneId1Based, i);
        const NpcDefRecord* record = GetNpcDefRecord(kindId);
        if (!record) {
            // Fidele a cGameData_LoadZoneNpcInfo : SkillDefTbl_GetRecord(mNPC, kindId)
            // a echoue -> flag_occupe reste a 0, le slot n'est jamais active dans
            // g_NpcRenderArray. On n'ajoute donc rien (pas de slot "vide" invente).
            ++skipped;
            continue;
        }

        StaticNpcSlot slot;
        slot.kindId = kindId;
        slot.def    = record;
        ZoneNpcPosition(zoneId1Based, i, slot.x, slot.y, slot.z);
        slot.angle  = ZoneNpcAngle(zoneId1Based, i);
        g_zoneNpcs.push_back(slot);
    }

    g_currentZoneId = zoneId1Based;
    TS2_LOG("StaticNpcLoader : zone %d -> %zu PNJ statiques charges (%d ignores, kindId "
            "sans NpcDefRecord)", zoneId1Based, g_zoneNpcs.size(), skipped);
    return true;
}

const std::vector<StaticNpcSlot>& ZoneNpcs() {
    return g_zoneNpcs;
}

int CurrentZoneNpcZoneId() {
    return g_currentZoneId;
}

} // namespace ts2::game

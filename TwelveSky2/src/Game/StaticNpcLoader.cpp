// Game/StaticNpcLoader.cpp — see StaticNpcLoader.h for full context (including the 2
// fidelity gaps fixed by wave W7) and Docs/TS2_NPC_ZONE_LOADER_TRIGGER.md for the
// decompilation proof.
#include "Game/StaticNpcLoader.h"
#include "Game/MotionPools.h"
#include "Core/Log.h"
#include <cstddef> // std::size_t (pool indexing)
#include <cstdint> // std::uint32_t (kindId)

namespace ts2::game {

namespace {
int g_currentZoneId = 0;
} // namespace

// LoadZoneNpcs — cGameData_LoadZoneNpcInfo 0x5578E0 (SOLE WRITER of g_NpcRenderArray
// 0x1764D14). Original decompilation (this = g_LocalPlayerSheet 0x1685748, type float*):
//
//   for (i = 0; i < mZONENPCINFO[501 * g_SelfMorphNpcId - 501]; ++i) {            /*0x5578EA*/
//     *((_DWORD*)this + 22*i + 228723) = SkillDefTbl_GetRecord(mNPC, kindId[i]);  /*0x557946*/
//     if (*((_DWORD*)this + 22*i + 228723)) {                                     /*0x557956*/
//       *((_DWORD*)this + 22*i + 228724) = 1;                                     /*0x55796B*/
//       *(this + 22*i + 228726) = 0.0;                                            /*0x55797F*/
//       *(this + 22*i + 228727) = 0.0;                                            /*0x557995*/
//       *(this + 22*i + 228728/9/30) = pos.x/y/z;               /*0x5579C1/ED/0x557A19*/
//       *(this + 22*i + 228734) = angle;                                          /*0x557A42*/
//       *(this + 22*i + 228743) = *(this + 22*i + 228734);                        /*0x557A62*/
//     }
//   }
//
// Offset mapping (this typed float*, so dword index): 228723 dw = 914892 bytes = 0xDF5CC,
// and 0x1685748 + 0xDF5CC = 0x1764D14 == g_NpcRenderArray EXACTLY. The following indices
// (228724/6/7/8/9/30/34/43) give intra-slot offsets +4/+12/+16/+20/+24/+28/+44/+80 for a
// stride of 22 dw (88 bytes). This is why this site does NOT APPEAR in xrefs to 0x1764D14
// (addressing via `this + immediate offset`): the proof is an offset calculation, not an xref.
//
// NOTE: the binary has NO bounds guard (it would write past the 100th slot if count demanded
// it) — structurally impossible, since mZONENPCINFO only carries 100 entries per zone.
bool LoadZoneNpcs(int zoneId1Based) {
    g_currentZoneId = 0;

    // Single pool (ex-`g_zoneNpcs` private + ex-`g_World.groundItems`, merged by W7).
    // NO clear() here: the binary doesn't clear anything in this loader (no `else` on the
    // @0x557956 guard, no zeroing of slots >= count). Cleanup belongs EXCLUSIVELY to
    // Pkt_EnterWorld (loop `for i<g_NpcCount: dtor(slot)` @0x464237, dtor 0x57FE70 which
    // only resets +4=0, preserving def/pos/angle). Original sequence: EnterWorld (disables
    // the 100 slots) -> SpawnCharacter(self) @0x4648E6 -> this loader.
    std::vector<NpcRenderEntry>& pool = g_World.npcRenderEntries;

    // The pool has a FIXED capacity, owned SOLELY by GameData_InitPools (== cGameData_InitPools
    // 0x5575D0, `*((_DWORD*)this + 1718) = 100` @0x5575E9 -> g_NpcCount 0x1687220). This module
    // does NOT size it itself: that would recreate the representation duplication this wave
    // eliminates. If it's empty, that's an upstream wiring defect.
    if (pool.empty()) {
        TS2_LOG("StaticNpcLoader : pool g_NpcRenderArray VIDE (GameData_InitPools 0x5575D0 pas "
                "encore appele ?) -- zone %d ignoree, aucun PNJ de decor ne sera rendu.",
                zoneId1Based);
        return false;
    }

    const int count = ZoneNpcCount(zoneId1Based);
    if (count <= 0) {
        // Zone out of bounds [1,350], mZONENPCINFO table not loaded, or zone without static
        // NPCs -- not an error (faithful: the @0x5578EA loop simply doesn't iterate if
        // mZONENPCINFO[zoneId].count == 0, and doesn't clear anything either).
        g_currentZoneId = zoneId1Based;
        return true;
    }

    // C++-only bound (the binary has none, cf. NOTE above): mZONENPCINFO structurally carries
    // 100 entries max per zone (row = 501 dw = 4 count + 100 kindId + 300 pos + 100 angle =
    // 2004 bytes) and the pool has 100 -> this clamp is a no-op in practice. It avoids UB
    // (out-of-vector write) without changing observable behavior.
    const int maxSlots = static_cast<int>(pool.size());
    int activated = 0;
    int holes     = 0;
    for (int i = 0; i < count && i < maxSlots; ++i) {
        NpcRenderEntry& slot = pool[static_cast<std::size_t>(i)];

        // @0x557946: slot.def = SkillDefTbl_GetRecord(mNPC, kindId) -- written
        // UNCONDITIONALLY, including as nullptr (the following guard decides).
        const std::uint32_t kindId = ZoneNpcKindId(zoneId1Based, i);
        const NpcDefRecord* record = GetNpcDefRecord(kindId);
        slot.def = record;

        if (!record) {
            // @0x557956: `if (record)` guard WITHOUT `else` -> the +4 flag stays AS-IS and
            // slot i isn't activated. It's also not removed: index i stays aligned with
            // mZONENPCINFO[i] (the binary NEVER compacts). This index goes over the network
            // (Net_QueueRunTo(..., 4, index, ...) @0x539E78, index resolved by
            // World_PickEntityAtCursor `*a4 = j` @0x538E8F): compacting would point the
            // server at a DIFFERENT target (cf. fidelity gap #1 in the .h).
            ++holes;
            continue;
        }

        slot.active   = true;  // @0x55796B — +4 = 1 (occupied flag)
        slot.mode     = 0;     // @0x55797F — +12 = 0 (tick mode: 0 -> Npc_RenderSlotTick_Loop)
        slot.frameAcc = 0.0f;  // @0x557995 — +16 = 0.0 (frame accumulator)
        // @0x5579C1 / @0x5579ED / @0x557A19 — +20/+24/+28 = XYZ position (mZONENPCINFO+0x194)
        ZoneNpcPosition(zoneId1Based, i, slot.x, slot.y, slot.z);
        // @0x557A42 — +44 = displayed angle (mZONENPCINFO+0x644)
        slot.angle = ZoneNpcAngle(zoneId1Based, i);
        // @0x557A62 — +80 = *(+44): baseline reread by Npc_RenderSlotTick_Loop @0x58048E
        // (`*(this+11) = *(this+20)`) once distance to the local player exceeds 400.
        slot.angleBase = slot.angle;
        ++activated;
    }

    g_currentZoneId = zoneId1Based;
    TS2_LOG("StaticNpcLoader : zone %d -> %d PNJ statiques actives sur %d entrees "
            "mZONENPCINFO (%d trous : kindId sans NpcDefRecord, slots laisses inactifs a "
            "index stable)", zoneId1Based, activated, count, holes);
    return true;
}

// Thin accessor over the single pool -- cf. contract (100 fixed slots, test `active`) in the .h.
const std::vector<NpcRenderEntry>& ZoneNpcs() {
    return g_World.npcRenderEntries;
}

int CurrentZoneNpcZoneId() {
    return g_currentZoneId;
}

} // namespace ts2::game

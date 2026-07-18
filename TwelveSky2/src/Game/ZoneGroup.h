// Game/ZoneGroup.h — zoneId -> regional/faction group table.
//
// CLEAN C++ rewrite (byte-exact on the table) of:
//   World_ZoneIdToGroup   EA 0x4DC260 (int __cdecl/__stdcall(int zoneId) -> int group, -1 if unknown)
//
// No static caller was found in the IDB (mcp__idaTs2__xrefs_to on 0x4DC260 and
// on the symbol "World_ZoneIdToGroup" both return 0 xrefs). The function is named and
// commented in the IDB ("[asset] map zone/map id to group/category code") by a renaming
// pass predating this project — name and comment kept as-is. The lack of a static
// reference suggests either a caller via an unresolved function pointer/table not caught by
// Hex-Rays, or dead code kept for build compatibility; to confirm dynamically if needed
// (breakpoint on 0x4DC260 via x32dbg).
//
// OBSERVED ROLE OF THE GROUP (indirect inference, to be falsified if a caller is found):
// The faction town NPC ids returned by Game/MapWarp.h::FactionTownNpcId(element)
// each fall into a DISTINCT group of this table:
//   element 0 (faction A) -> town NPC   1 -> ZoneIdToGroup(1)   == group 1 (zones 1..4)
//   element 1 (faction B) -> town NPC   6 -> ZoneIdToGroup(6)   == group 2 (zones 6..9)
//   element 2 (faction C) -> town NPC  11 -> ZoneIdToGroup(11)  == group 3 (zones 11..14)
//   element 3 (observer) -> town NPC 140 -> ZoneIdToGroup(140) == group 6 (zone 140 only)
// This coincidence (each of the 4 faction "capitals" opens a separate group, and each
// capital's satellite zones share its group) is consistent with a REGIONAL GROUPING BY
// STARTING CONTINENT/FACTION usage (zones 1-4/6-9/11-14 = 3 home continents + their adjacent
// zones). The other groups (4,5,7,8,9) gather zones without an associated faction capital
// (zones "shared"/dungeons/event zones — e.g. group 5 = a very large number of zones 49..174,
// plausibly "shared open world"; group 7 = zones 200/201/297..299, plausibly guild/nation war
// zones; group 9 = 319..323, plausibly an instanced dungeon pack). Precise usage (PvP
// restriction? regional chat channel? warp resolution? GameGuard anti-cheat filtering, out of
// scope?) NOT confirmed for lack of a caller — TODO(reverse) if an xref appears after re-analysis.
//
// SCOPE: pure data table (LOGIC, no rendering) — no divergence from the binary.
#pragma once
#include <cstdint>

namespace ts2::game {

// World_ZoneIdToGroup 0x4DC260 — frozen table (faithful switch/case, original order not
// significant since it is a simple lookup). Returns -1 (the original's `default` behavior) for
// any zoneId not listed in the table.
int32_t ZoneIdToGroup(int32_t zoneId);

} // namespace ts2::game

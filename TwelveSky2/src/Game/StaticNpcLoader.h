// Game/StaticNpcLoader.h — loader for DECOR static NPCs (merchants, guards,
// quest givers...) placed by the map, client-source EQUIVALENT of
// `cGameData_LoadZoneNpcInfo` 0x5578E0 (renamed in the IDB, ex-`cGameData_LoadMapEffects`).
//
// Full context, proof, and decompilation: Docs/TS2_NPC_ZONE_LOADER_TRIGGER.md.
// Summary:
//   - The original client fills a `g_NpcRenderArray` array (0x1764D14, stride 88 bytes,
//     100 slots) FROM A PER-ZONE STATIC TABLE `mZONENPCINFO` loaded ONCE at startup
//     (G02_GINFO\002.BIN, already wired client-side, cf. MotionPools.h §3
//     "mZONENPCINFO" + ZoneNpcCount/ZoneNpcKindId/ZoneNpcPosition/ZoneNpcAngle).
//   - This array is FULLY REPOPULATED (never merged/patched) EVERY TIME the
//     `Pkt_SpawnCharacter` packet (opcode 0x0F) creates entry i==0 (= local player) of
//     the entity array — i.e. on EVERY EnterWorld+SpawnCharacter(self) cycle, so at
//     initial login AND on every zone (re)load/warp/teleport (the server systematically
//     sends an EnterWorld+SpawnCharacter(self) on every zone change, cf.
//     Docs/TS2_PROTOCOL_SPEC.md). Proof: full decompilation of Pkt_SpawnCharacter 0x4646C0
//     (cf. doc), `if (!i)` guard right after slot creation, BEFORE any logic specific to
//     already-existing entities.
//   - This is an ENTIRELY LOCAL mechanism (no network packet dedicated to decor NPCs:
//     the only network NPC opcode, 0x13 Pkt_SpawnNpc, feeds a DISJOINT GAMEPLAY array
//     `dword_17AB534`/`game::NpcEntity`/`g_World.npcs`, used ONLY for interaction/
//     targeting — never for mesh rendering, cf. Docs/TS2_NPC_MESH_DRAW.md §"gameplay vs
//     render arrays"). The two arrays conceptually overlap (same NPCs) but are NOT
//     synchronized with each other on the original binary side: NPC mesh rendering
//     (Npc_DrawMesh 0x57FF00) reads ONLY `g_NpcRenderArray`, never `dword_17AB534`.
//
// Wired trigger: Game/EntityManager.cpp::OnSpawnCharacter, "new slot" branch, when
// `IsSelf(e)` is true (exact equivalent of the original `if (!i)` guard, @0x4648E6) —
// calls `LoadZoneNpcs(g_World.zoneId)`. This path is ACTUALLY REACHED at runtime
// (verified W7: EntityManager.cpp:318), which is this module's anti-dead-code guarantee.
//
// VERIFIED 2026-07-14 ("decor NPC position fidelity" dedicated mission): the mapping
// zoneId1Based -> mZONENPCINFO row (row = zoneId1Based - 1, MotionPools::AttachTableRow)
// is DIRECT, WITH NO INTERMEDIATE LOOKUP TABLE on the original binary side — confirmed
// by decompiling cGameData_LoadZoneNpcInfo 0x5578E0 (`mZONENPCINFO[501 *
// g_SelfMorphNpcId - 501]`, pure affine formula). The only subtle point is the SOURCE
// of the original zoneId: the binary deliberately reuses the global g_SelfMorphNpcId
// (0x1675A98, "current form/morph id") as a temporary carrier for the target zoneId,
// reset on EVERY pass through Scene_EnterWorldUpdate (login AND every warp/teleport/
// respawn — 41 write sites found on dword_1675A9C, cf.
// Docs/TS2_NPC_RENDER_ARRAY_WRITER.md §7 for the full disassembly proof). This confirms
// and resolves a previously open point (§7, "hypothesis 2" adopted) and validates that
// the indexing formula already implemented here (zoneId1Based - 1, no LUT) is faithful
// to the original.
//
// MISE A JOUR — Pass 4 / wave W7, front "npc-array-unify": merged the TWO duplicate
// representations of pool g_NpcRenderArray 0x1764D14 (this module's private
// `std::vector<StaticNpcSlot> g_zoneNpcs` vs. Game/GameState.h's unused
// `GameWorld::groundItems`) into a SINGLE `GameWorld::npcRenderEntries` (NpcRenderEntry,
// proven layout, 100 FIXED slots). This module is its SOLE WRITER (equivalent to
// cGameData_LoadZoneNpcInfo 0x5578E0); `ZoneNpcs()` is now a thin accessor kept for
// existing readers.
// Two fidelity gaps fixed by W7 (the old private vector had both):
//  1. INDEX LOSS (network-visible): the old `push_back` COMPACTED the list, but the binary
//     keeps slot `i` aligned to `mZONENPCINFO[i]` (leaves an inactive HOLE) — this index is
//     sent over the network (`Net_QueueRunTo(..., 4, a1, ...)` @0x539E78, a1 resolved by
//     World_PickEntityAtCursor `*a4 = j` @0x538E8F); compacting would target the wrong
//     entity server-side.
//  2. SPURIOUS CLEAR: the binary clears NOTHING in this loader (no `else` on the @0x557956
//     guard, no zeroing of slots >= count) — cleanup belongs SOLELY to Pkt_EnterWorld
//     (`for i<g_NpcCount: dtor(slot)` @0x464237, dtor 0x57FE70 resets only +4=0); the old
//     `g_zoneNpcs.clear()` was therefore unfaithful.
#pragma once
#include "Game/ExtraDatabases.h"
#include "Game/GameState.h" // NpcRenderEntry / GameWorld::npcRenderEntries / kNpcRenderPoolCapacity
#include <cstdint>
#include <vector>

namespace ts2::game {

// Alias KEPT from this module's historical name: the "static NPC slot" IS an entry of the
// single g_NpcRenderArray pool (Game/GameState.h::NpcRenderEntry — proven layout: def(+0),
// active(+4), mode(+12), frameAcc(+16), x/y/z(+20/24/28), angle(+44), angleBase(+80)).
// Kept so existing readers (Scene/WorldRenderer.cpp, UI/MinimapWidget.cpp) keep compiling
// without a type change.
//
// NOTE: the `kindId` field of the old StaticNpcSlot has DISAPPEARED — it doesn't exist in
// the binary's 88 bytes (the loader consumes kindId on the fly to resolve `def` and never
// stores it; readers re-derive the kind via `def`: Npc_RenderSlotTick_Loop @0x580429 reads
// def+1324, UI_NpcWin_Open 0x5DB530 @0x5dc03a reads `*(*a2)` == def+0 == NpcDefRecord::id).
// W7 CORRECTION (re-verified by grep): contrary to the first draft, ONE C++ reader STILL
// references it — Game/AnimationTick.cpp:923 (ZoneNpc_OnDialogueOpen, port of 0x5DB530)
// does `slot.def ? slot.def->id : slot.kindId` as a fallback. This fallback is DEAD on the
// binary side (0x5DB530 reads def+0 on an ACTIVE slot, so def is nonzero) but breaks
// COMPILATION since the merge (NpcRenderEntry has no kindId member). kindId stays OUT of
// the layout (fidelity to the proven 88 bytes): it's AnimationTick.cpp:923 that the
// orchestrator must adapt (`: slot.kindId` -> `: 0`, file out of scope for this front).
// Only occurrence of a `.kindId` member in src/.
using StaticNpcSlot = NpcRenderEntry;

// Repopulates the g_NpcRenderArray pool for `zoneId1Based` (1..350) — client-source
// equivalent of cGameData_LoadZoneNpcInfo 0x5578E0. Requires that
// MotionPools::LoadGInfo002Bin() AND ExtraDatabases::LoadExtraDatabases() have already
// been called (App_Init), AND that GameData_InitPools() has sized the pool (==
// cGameData_InitPools 0x5575D0, SOLE owner of the capacity); otherwise returns false
// without writing anything.
//
// CLEARS NOTHING (cf. gap #2 in the banner above): writes slots [0, count) IN PLACE, index
// by index. For each i: `def` = GetNpcDefRecord(ZoneNpcKindId(...)) is written
// UNCONDITIONALLY (@0x557946); if `def == nullptr`, the @0x557956 guard (no `else`) leaves
// `active` AS-IS and slot i remains a HOLE — index i is never reassigned (cf. gap #1).
// Slots >= count keep their state: the original sequence is EnterWorld (disables the 100
// slots @0x464237) -> SpawnCharacter(self) -> this loader.
bool LoadZoneNpcs(int zoneId1Based);

// Thin accessor over the single `g_World.npcRenderEntries` pool.
//
// /!\ CONTRACT CHANGED BY W7: now returns the pool's 100 FIXED SLOTS, NOT a compacted
// list of occupied slots. Every reader MUST test `n.active` before use, or risk handling
// empty slots (def == nullptr, position 0,0,0).
const std::vector<NpcRenderEntry>& ZoneNpcs();

// zoneId1Based used by the last successful LoadZoneNpcs() (0 if none).
int CurrentZoneNpcZoneId();

} // namespace ts2::game

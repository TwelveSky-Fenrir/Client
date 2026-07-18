// Game/AutoTargetCombatGate.h — AUTO-TARGET SYSTEM / COMBAT GATE: faithful C++ rewrite of
// two blocks decompiled via idaTs2 (Hex-Rays). Self-contained Game/*.h/.cpp module
// (dedicated mission, 2026-07-14): does NOT edit Scene/SceneManager.*, App/App.*, or any
// other "ALREADY WRITTEN" file — the consolidation agent wires the functions below onto
// InGameTickFlowHost::ValidateAutoTarget / ::IsCombatAllowedOnMap (Game/InGameTickFlow.h,
// already declared there as opaque std::function hooks, not wired on the SceneManager.cpp
// side to date — see its EA comment 0x52cca7/0x52cf8e).
//
// Source of truth = the TwelveSky2.exe disassembly (imagebase 0x400000), decompiled
// directly via MCP idaTs2 for this mission (Docs/TS2_GROUNDFX_AUTOTARGET.md and
// Docs/TS2_COMBAT_ELEMENT_GATING.md did NOT exist yet at the time of this work — direct
// decompilation was done instead, see the EA details below).
//
// 1) ValidateAutoTarget — EA block 0x52CCA7..0x52CE77 of Scene_InGameUpdate 0x52C600
//    (switch(dword_1675B24), called ONLY when step 12's gating gate of the InGame tick
//    lets it through — see Game/InGameTickFlow.h, out of scope here).
//    + Char_IsTargetablePlayerState 0x558AE0, Char_IsTargetableMonsterState 0x558B10
//      (trivial predicates, directly decompiled: return a1 != 12 / a1 != 12 && a1 != 19).
//
// 2) IsCombatAllowedOnMap — MISSION NAME given to Combat_IsElementAllowedOnMap 0x55CBF0.
//    IMPORTANT (gap between the mission's naming and the real binary, found via direct
//    decompilation): despite its IDA name "...OnMap", this function consults NO map/zone
//    identifier and does NOT implement a "PVP/safe zone" notion — it is a compatibility
//    matrix of CURRENT ELEMENT (mapElement=g_LocalElement) x ALLIANCE PAIR
//    (Char_GetPairedElement) x ACTIVE MORPH (g_SelfMorphNpcId), which in reality gates
//    automatic pickup of elemental combo markers (flt_1676130, 5 slots) — NOT PVP combat.
//    No "map -> PVP/safe" table exists in the binary at this call site (sole confirmed
//    caller: EA 0x52cf7a, see xrefs_to). This function is ALREADY fully ported by
//    Game/ComboPickupTick.h/.cpp (Combat_IsElementAllowedOnMap, same EA, same logic
//    confirmed by independent decompilation below): this file does NOT REIMPLEMENT it, it
//    exposes it under the name requested by the mission (direct wrapper, see
//    IsCombatAllowedOnMap below) to satisfy the name of the
//    InGameTickFlowHost::IsCombatAllowedOnMap hook.
//
// FLAGGED AMBIGUITY (same policy as GroupIdentity in Game/GameState.h):
// dword_1675B24 (address of the auto-target "mode" below) is REUSED as-is by
// Net/GameHandlers_VendorTrade.cpp / UI/PlayerTradeWindow.h as "player-to-player trade
// state" — SAME memory address, DIFFERENT semantics. No real conflict on the binary side:
// the two systems never run simultaneously (modal trade window driven by packets 0x31/0x33
// vs. the InGame tick's auto-targeting). Reproduced here via the SAME
// game::g_Client.Var (Game/ClientRuntime.h) escape hatch already used for THESE THREE
// specific addresses by Game/AutoPlaySystem.cpp (see UpdateTargeting, EA 0x45D080) — NO
// storage duplication introduced by this file.
//
// REUSE (mission rule — do NOT duplicate an already-written system):
//   - Combat_IsElementAllowedOnMap + ElementPairTable are already ported by
//     Game/SkillCombat.h / Game/ComboPickupTick.h — REUSED as-is (wrapper, see above), no
//     reimplementation of the element/morph matrix here.
//   - g_Client.Var/VarF (Game/ClientRuntime.h) — the globals escape hatch already standard
//     throughout ClientSource, reused for dword_1675B24/28/2C AND g_SelfMorphNpcId
//     (0x1675A98, SAME convention as Net/GameVarDispatch.cpp / Net/WorldEntityDispatch.cpp
//     / Game/AnimationTick.cpp — no global CombatMorphState instance exists in ClientSource
//     to date, read here read-only just like everywhere else).
//   - g_World.self.element (SelfState::element, Game/GameState.h, SAME address
//     g_LocalElement 0x1673194) — same read convention as
//     Net/GameHandlers_InvDispatch.cpp line ~214.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <functional>
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/ComboPickupTick.h" // Combat_IsElementAllowedOnMap + Game/SkillCombat.h::ElementPairTable

namespace ts2::game {

// 1) ValidateAutoTarget — original state/addresses.

// Original addresses of the 3 globals driving the locked auto-target.
inline constexpr uint32_t kAutoTargetModeAddr   = 0x1675B24u; // dword_1675B24: mode (0/1/2/3/4/5/7)
inline constexpr uint32_t kAutoTargetIdHiAddr   = 0x1675B28u; // dword_1675B28: id.hi (modes 1/2/3/5) OR raw index (modes 4/7)
inline constexpr uint32_t kAutoTargetIdLoAddr   = 0x1675B2Cu; // dword_1675B2C: id.lo (modes 1/2/3/5 only)
inline constexpr float    kAutoTargetRangeLimit = 500.0f;     // EA 0x52cd91/0x52ce6b (invalid if distance > 500.0, strict)

// Char_IsTargetablePlayerState 0x558AE0 — EA confirmed by direct decompilation:
//   BOOL __stdcall Char_IsTargetablePlayerState(int a1) { return a1 != 12; }
// (TODO unresolved elsewhere in ClientSource to date — see Game/ActionStateMachine.cpp,
// EA comment 0x571B8F, which still approximates it as "active && id matches"; this file
// provides the exact implementation for ITS OWN use, without editing that other file).
inline bool Combat_IsTargetablePlayerState(int actionState) { return actionState != 12; }

// Char_IsTargetableMonsterState 0x558B10 — same: return a1 != 12 && a1 != 19.
inline bool Combat_IsTargetableMonsterState(int actionState) {
    return actionState != 12 && actionState != 19;
}

// dword_168724C[227*kk] — reads the 1st dword of the player spawn payload, =
// PlayerEntity::body[0..3]. EXACT address confirmed by arithmetic (dword_1687234=active@+0
// -> +4/+8=id -> +0xC=timestamp -> +0x18(24)=body[0], and dword_168724C-dword_1687234=0x18):
// dword_168724C IS body[0..3] reinterpreted as int32. Fine-grained semantics undetermined
// (probably a class/appearance id, same family as NpcEntity/MonsterEntity::body[0] = mob
// id); tested for simple non-zero-ness ("record populated"), faithful to the binary, which
// only tests this dword's truthiness (no value comparison).
inline bool AutoTarget_PlayerRecordPopulated(const PlayerEntity& p) {
    int32_t v = 0;
    std::memcpy(&v, p.body.data(), sizeof(v));
    return v != 0;
}

// dword_1766F8C[70*mm] — monster's "raw" action state read DIRECTLY from the body, NOT
// MonsterEntity::anim.state. EXACT address confirmed by arithmetic (dword_1766F74=
// active@+0 -> +4/+8=id -> +0xC=timestamp -> +0x10(16)=body start, consistent with
// GameState.h "def +0x60" = 16+80 -> dword_1766F8C-dword_1766F74=0x18(24) = body[24-16=8..11]).
// This field is NOT guaranteed identical to MonsterEntity::anim.state (extracted by ANOTHER
// function, Char_Update 0x581E10, with its own offsets not confirmed to align with this
// one): read here from the raw body to stay faithful to THIS exact binary read, without
// relying on an unproven assumption about anim.state.
inline int32_t AutoTarget_MonsterActionState(const MonsterEntity& m) {
    int32_t v = 0;
    std::memcpy(&v, m.body.data() + 8, sizeof(v));
    return v;
}

// Position oracle for the "in-range object" modes (4 = SAME 88-byte pool exposed on the
// ClientSource side as g_World.groundItems (dword_1764D14/g_NpcRenderArray); 7 =
// ZoneObjectEntity/g_World.zoneObjects). `mode` receives the raw value of dword_1675B24 (4
// or 7) so the caller can distinguish the two pools. Returns false -> target considered out
// of range (safe fallback, SAME consequences as an empty pool/invalid index on the binary
// side).
//
// *** SEMANTIC FIX (mission "AutoTargetRangeLookup not wired", 2026-07-14, fresh independent
// decompilation of Item_PickupTarget 0x539EC0 and World_PickEntityAtCursor 0x538AB0) ***:
// dword_1764D14/g_NpcRenderArray is NOT ground-loot storage despite its ClientSource field
// name g_World.groundItems — World_PickEntityAtCursor treats this exact array (loop `j`,
// stride 22 dw, bound g_NpcCount) as click category **4 = NPC** via `Scene_RayHitNpcBox`, and
// Item_PickupTarget 0x539EC0 (reading the same array/offset unk_1764D28 for mode 4's range
// guard below) never picks anything up — its only observable effect is
// `UI_NpcWin_Open(&g_NpcRenderArray[...])`, an NPC dialog window. The REAL ground-loot pool is
// a DIFFERENT array, dword_17AB534 (stride 38 dw/152 bytes, click category **6**, raycast
// `Scene_RayHitItemModel`) — the very one GameState.h mismodels as `NpcEntity`/`g_World.npcs`.
// Same diagnosis independently reached by Game/GroundAuraWorldObjectTick.h (§ "audit steps
// 5-8 update"), now confirmed by a 4th cross-trace. `g_World.groundItems` is therefore a
// MISNAMED field inherited from GameState.h (shared foundation file, OUT OF SCOPE for this
// mission) — it actually carries an NPC array, not ground loot. This changes NOTHING about
// the wiring below: mode 4 of g_PendingOrderKind (walk-to/target NPC, consistent with the
// World_PickEntityAtCursor sequence 1-3=player, 4=npc, 5=monster, 7=zone object) always needs
// to read this exact array (address+stride+offset), and `g_World.groundItems` structurally
// IS that array (same derived address, same 88-byte stride, same x/y/z offsets) — the only
// correct ClientSource container to use here, despite its name. No other pool fits:
// `g_World.npcs` (dword_17AB534) has a totally different stride/offset (152 bytes) and is
// actually the model-object array (category 6), not the one read by this switch. Renaming
// GameState.h::groundItems -> npcRenderEntries (and vice versa for npcs) is a deeper fix
// outside this mission's Game/AutoTargetCombatGate.*.h scope (shared foundation file) —
// flagged here for a future consistency pass, NOT applied.
using AutoTargetRangeLookup = std::function<bool(int mode, int index, float outPos[3])>;

// Default oracle: mode==4 -> x/y/z read from g_World.groundItems (storage REUSED as-is —
// see the semantic fix above: this pool is actually the NPC array g_NpcRenderArray/
// dword_1764D14, NOT pickable objects, but it is indeed the EXACT array this mode must
// read); mode==7 -> x/y/z = the first 3 floats of ZoneObjectEntity::body.
//
// mode==4, EA 0x52cd91: `Math_Dist3D((char*)&unk_1764D28 + 88*g_PendingOrderGridX,
// flt_1687330)`. unk_1764D28 - g_NpcRenderArray(dword_1764D14) == 0x14 (20 bytes), stride 88
// bytes, raw index = g_PendingOrderGridX -- address/stride/offset RE-CONFIRMED by 2
// additional independent decompilations this mission (Item_PickupTarget 0x539EC0, SAME
// expression `unk_1764D28 + 22*index`; World_PickEntityAtCursor 0x538AB0, loop `j` on the
// SAME array/stride/bound g_NpcCount). NO activity test (`active`) in the binary for this
// mode (contrary to what one might expect) — faithfully reproduced: only a defensive
// bounds-check is added below (needed because g_World.groundItems is a lazily-growing
// std::vector on the ClientSource side, NOT a fixed-capacity C array guaranteed like the
// original).
//
// mode==7, EA 0x52ce6b: address fidelity confirmed (unk_180EF0C - g_ZoneObjectArray == 0x18
// == ZoneObjectEntity::body offset 0, see GameState.h) AND RE-CONFIRMED by
// World_PickEntityAtCursor (loop `n`, category 7 = g_ZoneObjectArray, SAME array). The binary
// ALSO does NOT test z.active for this mode -- ONLY the defensive bounds-check (same reason
// as mode 4) is applied here, without filtering on slot activity.
bool AutoTarget_DefaultRangeLookup(const GameWorld& world, int mode, int index, float outPos[3]);

// ValidateAutoTarget — EA block 0x52CCA7..0x52CE77 of Scene_InGameUpdate (switch on
// dword_1675B24). Validates the currently locked target and resets the mode
// (g_Client.Var(kAutoTargetModeAddr)=0) if it's no longer valid — otherwise touches
// NOTHING (faithful: the binary never modifies dword_1675B28/2C here, only the mode can
// fall back to 0).
//   mode 1/2/3: target = remote player (world.players[1..], index 0 = self is skipped,
//                faithful to the `for (kk=1; kk<g_EntityCount; ...)` loop) — found via
//                (active && AutoTarget_PlayerRecordPopulated && Combat_IsTargetablePlayerState
//                (anim.state) && id.hi==dword_1675B28 && id.lo==dword_1675B2C). Reset if NO
//                player matches (faithful: exhaustive search, NO distance test for these 3
//                modes).
//   mode 5     : target = monster (world.monsters[0..]) — same pattern, via
//                Combat_IsTargetableMonsterState(AutoTarget_MonsterActionState(m)).
//   mode 4     : target = "NPC in range" (NOT a ground object despite the ClientSource field
//                name g_World.groundItems carrying this pool -- see the semantic fix in the
//                AutoTarget_DefaultRangeLookup banner) — RAW index = dword_1675B28 (NOT a
//                network id, faithful: the binary directly indexes the g_NpcRenderArray
//                pool, no existence scan, AND NO slot-activity test -- RE-VERIFIED, see
//                AutoTarget_DefaultRangeLookup).
//   mode 7     : target = "zone object in range" (ZoneObjectEntity/g_World.zoneObjects) —
//                same raw-indexing pattern as mode 4.
//                Reset (modes 4 and 7) if `rangedLookup` returns false OR distance > 500.0 from self.
//   default    : no action (faithful: the binary silently ignores any other mode, including
//                0, already null, and any value never observed in this switch, e.g. 6 —
//                "pickable object" category/dword_17AB534 in World_PickEntityAtCursor,
//                absent from THIS particular switch).
// Default `rangedLookup` (null) => AutoTarget_DefaultRangeLookup(world, ...) (modes 4 AND 7,
// see banner above -- mode 4 resolved via world.groundItems, storage reused as-is).
void ValidateAutoTarget(GameWorld& world,
                         const AutoTargetRangeLookup& rangedLookup = nullptr);

// 2) IsCombatAllowedOnMap — EXACT wrapper of Combat_IsElementAllowedOnMap (already ported).

// Direct wrapper of Game::Combat_IsElementAllowedOnMap (Game/ComboPickupTick.h, EA
// 0x55CBF0) — REUSED as-is, NO reimplementation of the element/morph matrix. Exposed
// under the name requested by the mission AND by the
// InGameTickFlowHost::IsCombatAllowedOnMap hook (Game/InGameTickFlow.h). `pairs` = the
// character's element pair table (g_LocalPlayerSheet+455..458) — NO global instance exists
// in ClientSource to date (see Scene/SceneManager.cpp, EA comment 0x52cf8e); {} (4x -1, "no
// pair registered") is the closest default fallback for a character that never received
// this network data — NOT an "always false" shortcut: the result ALWAYS genuinely depends
// on `selfMorphNpcId` (see Combat_IsElementAllowedOnMap, the -1/"self" branch of each
// internal switch, EA 0x55cc38 onward).
inline bool IsCombatAllowedOnMap(int mapElement, int selfMorphNpcId,
                                  const ElementPairTable& pairs = {}) {
    return Combat_IsElementAllowedOnMap(mapElement, selfMorphNpcId, pairs);
}

// "Zero-argument" variant directly wireable to InGameTickFlowHost::IsCombatAllowedOnMap
// (std::function<bool()>, EA 0x52cf6e-0x52cf94):
//   mov ecx, g_LocalPlayerSheet ; push g_LocalElement ; call Combat_IsElementAllowedOnMap
// mapElement = world.self.element (g_LocalElement 0x1673194, same read convention as
// Net/GameHandlers_InvDispatch.cpp); selfMorphNpcId = g_Client.VarGet(0x1675A98)
// (g_SelfMorphNpcId escape hatch, same convention as Net/GameVarDispatch.cpp /
// Net/WorldEntityDispatch.cpp / Game/AnimationTick.cpp). NOTE: the binary combines this
// result with `&& !g_GmAuthLevel` AT THE CALL SITE (0x52cf8e) — Game/InGameTickFlow.cpp
// already applies this combination via a SEPARATE IsGm hook (step 12d); this function
// returns ONLY the Combat_IsElementAllowedOnMap result, faithful to the established split.
bool IsCombatAllowedOnMapForSelf(const GameWorld& world, const ElementPairTable& pairs = {});

} // namespace ts2::game

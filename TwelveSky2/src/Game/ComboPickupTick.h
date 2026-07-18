// Game/ComboPickupTick.h — Combo/pickup/quest, support system for the "step 12" block
// of the InGame tick (Game/InGameTickFlow.h). Faithful C++ rewrite (a real translation
// of the disassembly, not invention) of the following small cluster. Source of truth =
// TwelveSky2.exe disassembly (imagebase 0x400000) via MCP idaTs2.
//
// Original functions translated (EA -> function):
//   Combo_FindNearbyFollowup     0x501270 -> Combo_FindNearbyFollowup()
//   BeginComboMorph              0x52CF69 -> BeginComboMorph()            (inline block of
//                                             Scene_InGameUpdate, NOT a separate function
//                                             in the binary — same source as the rest of
//                                             this file, decompiled with full context)
//   Combat_IsElementAllowedOnMap 0x55CBF0 -> Combat_IsElementAllowedOnMap()
//   [5-slot pickup]               0x52CF94..0x52D067 -> TickNearbyPickupSlots() (inline
//                                             block of Scene_InGameUpdate, NOT a separate
//                                             function — the 5-slot loop + Net_SendOp106)
//   Quest_UpdateMarkerTimer      0x510D90 -> Quest_UpdateMarkerTimer()
//   Tips002_RotateUpdate         0x4C1840 -> Tips_RotateUpdate()          (wrapper: the
//                                             timer/INDEX rotation is ALREADY ported
//                                             faithfully by TipsTable::Advance, see
//                                             Game/StringTables.h — this file only adds
//                                             the missing Msg_AppendChatLine call)
//   Npc_AutoInteractForPet       0x53B5F0 -> ALREADY FULLY PORTED by
//                                             NpcInteractionSystem::AutoInteractForPet()
//                                             (Game/NpcInteraction.h/.cpp) — REUSED as-is,
//                                             no duplication here (see note below).
//
// All these functions come from the SAME source block (the "step 12" switch/case of
// Scene_InGameUpdate 0x52C600, see Game/InGameTickFlow.h/.cpp for the full tick
// orchestration). They were decompiled together (a single Hex-Rays pass over
// Scene_InGameUpdate) then split here into clean, reusable functions/API.
//
// ---------------------------------------------------------------------------------------
// REUSE (mission rule — do NOT duplicate an already-written system):
//   - Npc_AutoInteractForPet (0x53B5F0) is already fully ported by
//     NpcInteractionSystem::AutoInteractForPet() (Game/NpcInteraction.h). This file does
//     NOT reimplement it; the integration point InGameTickFlowHost::AutoInteractNpcForPet
//     must call THIS method directly (wiring left for the consolidation step, see the
//     mission's CRITICAL COORDINATION RULE — SceneManager.cpp is not edited here).
//   - Ground pickup via "mouse click" (Item_PickupTarget/Item_InteractGround) is already
//     ported by Game/ItemPickupSystem.h — DIFFERENT from the AUTOMATIC pickup of the 5
//     nearby slots below (flt_1676130, fed by server packet 0x82, see
//     Net/GameHandlers_Misc.cpp), which has no equivalent in ItemPickupSystem.h: this
//     file adds it (TickNearbyPickupSlots), without touching ItemPickupSystem.h/.cpp.
//   - Quest_CheckObjectiveState / QuestProgressState / QuestStepRecord / LookupQuestStep are
//     already ported by Game/QuestSystem.h — Quest_UpdateMarkerTimer below REUSES them
//     as-is (does NOT operate on a new quest data model).
//   - ElementPairTable / CombatMorphState (g_SelfMorphNpcId) are already ported by
//     Game/SkillCombat.h — Combat_IsElementAllowedOnMap and BeginComboMorph REUSE them.
//   - net::Rng / net::DefaultRng() (Rng_Next 0x7603FD, identical CRT LCG) are already
//     ported by Net/Rng.h — BeginComboMorph's random rotation REUSES it (same generation
//     as all network builders, faithful: it's the SAME rand() on the binary side).
//   - Net_SendPacket_Op20 / Net_SendOp106 are already declared in Net/SendPackets.h.
//   - TipsTable (Game/StringTables.h) already carries the timer/index (600 s):
//     Tips_RotateUpdate below just adds the missing side effect (append to the chat log).
//
// ---------------------------------------------------------------------------------------
// GINFO2 (flt_1555D08, "motion/combo" table, ~350 entries x 805 dwords): ASSET table not
// modeled elsewhere in ClientSource (no loader identified for this .IMG file within the
// scope of this mission — same situation as the NPC mQUEST table in Game/QuestSystem.h,
// see its "(B) OUT OF SCOPE" banner). Combo_FindNearbyFollowup and BeginComboMorph query
// THIS table to, respectively, enumerate nearby combo-followup candidates for a position,
// and resolve a combo's origin position by key. BOTH algorithms (bounds, loop, distance
// comparison, test order) are reproduced faithfully below; the table itself is injected
// by the caller via ComboCandidateLookup / ComboMotionOriginLookup (same pattern as
// QuestStepLookup: null callback -> "no candidate"/"null position" result, safe by default).
//
// Combo_CheckTransition (0x4FD650, direct callee of Combo_FindNearbyFollowup) is a
// 900+ line combo/level/element compatibility table (belongs to SkillCombat, OUT OF
// SCOPE for this specific mission — only Combo_FindNearbyFollowup 0x501270 is assigned).
// Injected via ComboTransitionCheck, same pattern.
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <vector>
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/QuestSystem.h"
#include "Game/SkillCombat.h"   // ElementPairTable (Char_GetPairedElement), CombatMorphState
#include "Game/StringTables.h" // TipsTable (Tips_RotateUpdate)
#include "Net/NetClient.h"
#include "Net/SendPackets.h"

namespace ts2::game {

// ===========================================================================
// Combat_IsElementAllowedOnMap 0x55CBF0 — element-vs-map matrix.
// ===========================================================================
//
// Original call (confirmed by disassembly, EA 0x52CF75-0x52CF7A):
//   mov ecx, offset g_LocalPlayerSheet ; this = local player sheet (Char_GetPairedElement)
//   push g_LocalElement               ; a2   = tested element (= mapElement here)
//   call Combat_IsElementAllowedOnMap
// "this" is ONLY used to resolve Char_GetPairedElement(this, a2): reproduced here via
// `pairs` (Game/SkillCombat.h::ElementPairTable, same data g_LocalPlayerSheet+455..458).
//
// Faithful structure: switch(mapElement){0,1,2,3} -> switch(paired){-1/self,0,1,2,3} ->
// membership test of `selfMorphNpcId` (g_SelfMorphNpcId 0x1675A98, see
// Game/SkillCombat.h::CombatMorphState::currentActionId) against a fixed set of 8 or 12 ids.
// NOTE: in the binary, the internal "default" cases (goto LABEL_47/92/137/182) are dead
// code — Char_GetPairedElement only ever returns {-1,0,1,2,3}, values ALL covered
// explicitly by each inner switch. Reproduced here as `return false` (never reached in
// practice, same input constants).
bool Combat_IsElementAllowedOnMap(int mapElement, int selfMorphNpcId, const ElementPairTable& pairs);

// ===========================================================================
// Combo_FindNearbyFollowup 0x501270 — nearby combo-followup target.
// ===========================================================================
//
// int __thiscall Combo_FindNearbyFollowup(_DWORD *this, int a2, int a3)
// {
//   if ( a2 < 1 || a2 > 350 ) return -1;
//   for ( i = 0; i < *(this + 805*a2 - 802); ++i )
//     if ( Math_Dist3D(this + 805*a2 + 3*i - 801, a3) < 30.0
//       && Combo_CheckTransition(a2, *(this + 805*a2 + i - 501)) == 1 )
//       return *(this + 805*a2 + i - 501);
//   return -1;
// }
// this=flt_1555D08 (GINFO2), a2=current motionId (g_SelfMorphNpcId), a3=&flt_1687330 (self).
// See the GINFO2 banner at the top of this file for table injection.

// A followup candidate: motion id + associated 3D position (extracted from GINFO2).
struct ComboMotionCandidate {
    int32_t id = -1;
    float   x = 0.0f, y = 0.0f, z = 0.0f;
};

// Enumerates the followup candidates linked to `motionId` (GINFO2, not modeled here —
// see banner). nullptr/not wired -> no candidate (Combo_FindNearbyFollowup then returns
// -1, safe).
using ComboCandidateLookup = std::function<std::vector<ComboMotionCandidate>(int motionId)>;

// Combo_CheckTransition 0x4FD650 (SkillCombat, OUT OF SCOPE for this mission): returns
// 1 (valid transition at current level), 2 (known transition but insufficient level) or
// 0 (unknown/out-of-bounds transition). Only code 1 validates a candidate here (faithful:
// exact `== 1` in the binary). nullptr/not wired -> constant 0 (no valid candidate).
using ComboTransitionCheck = std::function<int(int fromMotionId, int toMotionId)>;

// Returns the motion id of the FIRST candidate found (table order) at 3D distance < 30.0
// from (selfX,selfY,selfZ) AND whose transition (motionId -> candidate.id) is exactly 1,
// or -1 if none (or motionId out of [1,350], faithful guard EA 0x501286).
int Combo_FindNearbyFollowup(int motionId, float selfX, float selfY, float selfZ,
                              const ComboCandidateLookup& candidates,
                              const ComboTransitionCheck& transitionCheck);

// ===========================================================================
// BeginComboMorph EA 0x52CF69 — combo morph start (inline block of
// Scene_InGameUpdate, see Game/InGameTickFlow.h step 12c for the call context: called
// ONLY when Combo_FindNearbyFollowup found a candidate AND no morph is already in
// progress — guards reproduced on the INGameTickFlow_Update side, NOT here).
// ex-VeryOldClient: EFFECT_OBJECT type 11 (transform aura attached to a bone) +
// SetSantaEffect (type 14) — PLAUSIBLE (Docs/TS2_FX_ROSETTA.md §1 + §4 render gap). This
// block only carries the morph STATE (g_SelfMorphNpcId 0x1675A98); the aura's VISUAL =
// Fx_DrawZoneAura 0x583F90 = RENDER GAP (Gfx layer, FRONT 2 NOT OWNED) — not implemented here.
// ===========================================================================
//
//   g_MorphInProgress = 1;                       // 0x1675A88
//   dword_1675A8C = 4;                            // phase
//   dword_1675A90 = 0;                            // (never read again elsewhere in this function)
//   dword_1675A9C = v24;                          // followupMotionId
//   Crt_Memset(&dword_1675AA0, 0, 72);             // reset the warp block (72 bytes)
//   dword_1675AA0 = 0;                             // +0  (redundant with the memset)
//   dword_1675AA4 = 1;                             // +4  (OVERWRITES the memset -> "armed")
//   flt_1675AA8 = 0.0;                             // +8  (redundant)
//   GInfo2_FindVec3ByKey(flt_1555D08, v24, g_SelfMorphNpcId, &flt_1675AAC); // +12/+16/+20
//   flt_1675AC4 = (float)(Rng_Next() % 360);       // +36 current rotation
//   flt_1675AC8 = flt_1675AC4;                     // +40 target rotation (== current at init)
//   Net_SendPacket_Op20(client, dword_1675A8C, v24);

// 72-byte warp block (dword_1675AA0..dword_1675AA0+72 = 0x1675AA0..0x1675AE8), layout
// faithful to the binary's named offsets; regions with no identified consumer in
// Scene_InGameUpdate remain opaque bytes (memset to zero only, never read here).
#pragma pack(push, 1)
struct ComboMorphWarpBlock {
    int32_t flag0            = 0;    // +0  dword_1675AA0 (reset, rewritten to 0)
    int32_t flag1            = 0;    // +4  dword_1675AA4 (rewritten to 1 after the memset -> "armed")
    float   timer             = 0.0f; // +8  flt_1675AA8 (reset)
    float   targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f; // +12/+16/+20 (GInfo2_FindVec3ByKey)
    uint8_t _unknown24[12]   = {};    // +24..+35: zeroed by the memset, no identified
                                       // consumer in Scene_InGameUpdate.
    float   rotationCurrent   = 0.0f; // +36 flt_1675AC4 (Rng_Next() % 360)
    float   rotationTarget    = 0.0f; // +40 flt_1675AC8 (== rotationCurrent at init)
    uint8_t _unknown44[28]   = {};    // +44..+71: zeroed by the memset, no identified
                                       // consumer in Scene_InGameUpdate.
};
#pragma pack(pop)
static_assert(sizeof(ComboMorphWarpBlock) == 72,
              "ComboMorphWarpBlock must be 72 bytes (see Crt_Memset(&dword_1675AA0, 0, 72))");

// Combo morph runtime state (mirrors g_MorphInProgress/dword_1675A8C/90/9C + warp block).
struct ComboMorphState {
    bool                 inProgress       = false; // g_MorphInProgress 0x1675A88
    int32_t              phase            = 0;     // dword_1675A8C (4 once started)
    int32_t              unk90            = 0;     // dword_1675A90 (always 0 here, never read again)
    int32_t              followupMotionId = 0;     // dword_1675A9C
    ComboMorphWarpBlock  warp;
};

// GInfo2_FindVec3ByKey 0x4FD540 (same GINFO2 table, see banner): resolves the origin
// position associated with key `originKey` (= CURRENT motionId, g_SelfMorphNpcId) in the
// followup list for combo `followupMotionId`. `outPos` MUST be written (x,y,z) — {0,0,0}
// if absent, faithful to the binary which zeroes the vec3 before the lookup and leaves it
// as-is if not found. nullptr/not wired -> constant {0,0,0}.
using ComboMotionOriginLookup = std::function<void(int followupMotionId, int originKey, float outPos[3])>;

// Starts the combo morph toward `followupMotionId` (NO guard here: "followup != -1" and
// "!morphInProgress" are the caller's responsibility, see banner above and
// Game/InGameTickFlow.cpp). `currentMotionId` = g_SelfMorphNpcId at call time (origin key
// passed to GInfo2_FindVec3ByKey). Emits Net_SendPacket_Op20(phase, followupMotionId) via
// `netClient` (Net/SendPackets.h, already ported). Random rotation via net::DefaultRng()
// (Net/Rng.h — SAME generator as the original Rng_Next(), reused as-is).
void BeginComboMorph(ComboMorphState& state, int followupMotionId, int currentMotionId,
                      net::NetClient& netClient, const ComboMotionOriginLookup& originLookup);

// ===========================================================================
// Automatic pickup of the 5 nearby slots — inline block of
// Scene_InGameUpdate EA 0x52CF94..0x52D067 (NOT a separate function in the binary).
// ===========================================================================
//
//   for ( nn = 0; nn < 5; ++nn )
//     if ( (flt_1676130[3*nn] || flt_1676130[3*nn+1] || flt_1676130[3*nn+2])
//       && Math_Dist3D(&flt_1676130[3*nn], flt_1687330) < 100.0 )
//     {
//       flt_1676130[3*nn] = flt_1676130[3*nn+1] = flt_1676130[3*nn+2] = 0.0;
//       Net_SendOp106(client, nn, flt_1687330);   // payload = PLAYER position (not the slot's)
//     }
//
// flt_1676130 (15 floats = 5 x vec3) is ALREADY fed by the server packet 0x82 handler
// (Net/GameHandlers_Misc.cpp, via g_Client.VarF(0x1676130 + i*4)) — this file READS/WRITES
// these SAME locations via g_Client.VarF, without duplicating the storage.
inline constexpr int      kNearbyPickupSlotCount   = 5;
inline constexpr float    kNearbyPickupRadius       = 100.0f; // EA 0x52d023 (< 100.0 strict)
inline constexpr uint32_t kNearbyPickupSlotBaseAddr = 0x1676130u; // flt_1676130 (g_Client.VarF)

// Clears + Net_SendOp106 for each non-null slot within 3D distance < 100.0 of
// (selfX,selfY,selfZ). Network payload = LOCAL PLAYER position (faithful: NOT the slot's
// position, see Net_SendOp106(client, nn, flt_1687330) in the binary).
void TickNearbyPickupSlots(float selfX, float selfY, float selfZ, net::NetClient& netClient);

// ===========================================================================
// Quest_UpdateMarkerTimer 0x510D90 — quest marker display timer (30 s/600 s).
// ===========================================================================
//
// Operates on the SAME g_PlayerCmdController (0x1669170) struct as Game/QuestSystem.h ::
// QuestProgressState (zoneId +10249*4=40996, npcQuestId +11553*4=46212 — REUSED as-is,
// NOT a new model) + 5 fields specific to THIS function (+51576..+51592, out of scope for
// QuestSystem.h -> QuestMarkerState below, same pattern as NpcInteraction.h::
// NpcInteractionExt: a parallel struct, not a memory overlay).
struct QuestMarkerState {
    bool     active            = false; // +51576 dword_this+51576 (marker displayed)
    float    lastTimerSec       = 0.0f;  // +51580 (reused for BOTH the 30s/600s timers)
    int32_t  lastObjectiveState = 0;    // +51584 (cached result of Quest_CheckObjectiveState)
    uint32_t targetNpcQuestKey  = 0;    // +51588 (the "+92" field == QuestStepRecord::field92
                                         //         of the resolved mQUEST NPC record, see QuestSystem.h)
    int32_t  markerVariant      = 0;    // +51592 (0 for the "objective complete" case v2==1;
                                         //         Rng_Next()%3+1 for the "in progress" case)
};

// g_WarehouseWindowOpen && dword_1822ED0 && *dword_1822ED0 == targetNpcQuestKey — warehouse
// window/quest-open state (UI, OUT OF SCOPE for this file). Called TWICE with DIFFERENT
// keys in the binary (once against the already-armed marker, once against the NEW resolved
// candidate): a callback rather than a fixed bool, to stay faithful. nullptr/not wired ->
// constant false (no window ever "consumes" the marker).
using WarehouseTargetMatch = std::function<bool(uint32_t targetNpcQuestKey)>;

// Snd3D_PlayScaledVolume 0x4DA380 (audio, OUT OF SCOPE): played ONLY in the v2==1 branch
// ("objective complete", see binary EA 0x510e80). nullptr/not wired -> silent.
using QuestMarkerSoundCallback = std::function<void()>;

// `gameTimeSec` = g_World.gameTimeSec. `isArenaZone` = Map_IsArenaZone(&unk_1685740) 0x54B690
// (global guard at function head, OUT OF SCOPE — not modeled elsewhere in ClientSource;
// true -> the function does NOTHING, faithful).
void Quest_UpdateMarkerTimer(QuestMarkerState& marker, const QuestProgressState& progress,
                              float gameTimeSec, bool isArenaZone,
                              const WarehouseTargetMatch& warehouseTargetMatches,
                              const QuestMarkerSoundCallback& playMarkerSound = nullptr);

// ===========================================================================
// Tips002_RotateUpdate 0x4C1840 — tips rotation (mGAMENOTICE announcement banner).
// ===========================================================================
//
// The timer/index (600 s, wraps to 0) is ALREADY ported faithfully by TipsTable::Advance
// (Game/StringTables.h) — THIS file only adds the missing side effect:
//   Msg_AppendChatLine((char*)this + 101*currentIndex + 4, 3, &String); // 0x4c18c6
// i.e. appending the new tip text to the chat log. The "3" is a mFONTCOLOR palette
// INDEX (ColorTable_InitPalette 0x4C1D60) — NOT an ARGB: the binary stores it raw and
// resolves it at draw time via ColorTable_GetColor 0x4C1FE0. The resolution is now done
// at the producer (g_Strings.colors.Get(3) = 0xFFFFFF00, opaque yellow), see
// Tips_RotateUpdate in the .cpp — the old note "same limitation as g_SysMsgColor" is
// LIFTED (the real accessor 0x4C1FE0 was found and wired).
void Tips_RotateUpdate(TipsTable& tips, float gameTimeSec);

} // namespace ts2::game

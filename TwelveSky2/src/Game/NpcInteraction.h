// Game/NpcInteraction.h — NPC interaction system of TwelveSky2 (ts2::game).
//
// Faithful C++ rewrite (real translation of the disassembly, no invention) of the small
// "Npc_*" cluster that handles: selecting the nearest interactable NPC, auto-interaction
// (range/cooldown), and NPC classification (special / quest target / nameplate color).
// Feeds the currently stubbed NPC hooks of Game/AutoPlaySystem.h (host.InteractNpc ->
// Npc_Interact, host.ShouldRefreshNpc -> equivalent of maybe_Npc_ShouldRefreshTarget) and the
// quest-target resolution of Game/QuestSystem.h (IsQuestTarget can feed the definition of a
// "go talk to/kill an entity of category X" objective).
//
// Original functions translated (EA -> function/method):
//   Npc_Interact             0x53A660 -> NpcInteractionSystem::Interact()
//   Npc_AutoInteract         0x53A980 -> NpcInteractionSystem::AutoInteractCurrentTarget()
//   Npc_AutoSelectNearest    0x53ABC0 -> NpcInteractionSystem::AutoSelectNearestInteractable()
//   Npc_AutoInteractForPet   0x53B5F0 -> NpcInteractionSystem::AutoInteractForPet()
//   Npc_IsQuestTarget        0x540340 -> Npc_IsQuestTarget()          (pure function)
//   Npc_GetNameplateColor    0x540790 -> Npc_GetNameplateColor()      (pure function)
//   Npc_IsSpecialType        0x54EE60 -> Npc_IsSpecialType()          (pure function)
// Callees required for fidelity, translated internally (no external dependency):
//   Math_Dist2D_XZ           0x53FA40 -> DistanceXZ()   (NPC<->player distance in the XZ plane)
//   Math_Dist3D              0x53FAA0 -> Distance3D()   (NPC<->player 3D distance)
//   Level_ToAggroValue       0x53F700 -> Npc_LevelToAggroValue() (full table 100..157)
//
// ---------------------------------------------------------------------------------------
// NPC FIELD PROVENANCE (important): the 4 action functions read a RUNTIME array (base
// dword_17AB534, not in the IDB as-is — stride 38 dwords/152 bytes), DIFFERENT from the raw
// network payload modeled by NpcEntity::body (84 bytes, cf. Game/GameState.h). Fields
// identified by address arithmetic between the symbols neighboring this array:
//   +0   active           (dword_17AB534[38*i])
//   +4   EntityId.hi      (dword_17AB538[38*i])
//   +8   EntityId.lo      (dword_17AB53C[38*i])
//   +16  "offer" itemId   (dword_17AB544[38*i])  — item handled/given by the NPC
//   +20  "offer" weight   (dword_17AB548[38*i])
//   +100 def pointer      (dword_17AB598[38*i])  — fields +184/+188 read by Npc_Interact and
//        friends; SAME convention as Game/AutoPlaySystem.cpp (kDefOffFaction=184,
//        kDefOffNpcKind=188 on NpcEntity::def) -> so we reuse NpcEntity::def for this
//        pointer, consistent with the rest of the already-written code.
//   +128 position (x,y,z) (flt_17AB5B4[38*i])
// These last 3 groups (offer itemId/weight, position) are ABSENT from NpcEntity (neither
// the wire body nor def carries them) -> modeled here via NpcInteractionExt (array parallel
// to g_World.npcs, same index, mirroring AutoPlaySystem::MonsterAutoplayExt). FIDELITY NOTE:
// this differs from the (unproven, cf. "unk_17AB554"/"deduced" comments) choice made in
// AutoPlaySystem.cpp to read position from NpcEntity::body+16 — the direct address
// arithmetic on Npc_Interact above is the most reliable source available here; to be
// reconciled during a future port of the real NPC spawn (TODO integration).
//
// Npc_IsQuestTarget/Npc_GetNameplateColor read an "a1+96" pointer with fields at
// +232/+236 (category selection) and, for color, +252/+260/+352 (model height, aggro/level
// threshold). IMPORTANT FIDELITY NOTE: offset +96 corresponds EXACTLY to MonsterEntity::def
// (documented "+0x60" = 96 in Game/GameState.h), whereas NpcEntity::def is theoretically at
// +100 (cf. above) — these 2 functions therefore very probably operate on the original
// MONSTER array despite their "Npc_*" IDB name (a precedent already encountered in
// QuestSystem.h: "Pkt_SmithUpgradeResult... misnamed"). Per the mission scope ("Operates on
// game::g_World.npcs"), they are exposed here GENERICALLY on a record pointer (compatible
// with both NpcEntity::def AND MonsterEntity::def, both resolved as "const void*") rather
// than locking in an entity type — the caller picks the source.
//
// RULE: this file does not edit any existing header. Includes Game/GameState.h (NpcEntity,
// EntityId, GameWorld), Game/ClientRuntime.h (g_Client.msg/inv, Str()) and Game/QuestSystem.h
// (Quest_SumExceeds2Billion, reused as-is — same formula as Util_SumExceeds2Billion
// 0x53F660 used by ALL functions in this file).
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <array>
#include <vector>
#include <functional>

#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/QuestSystem.h"

namespace ts2::game {

// ---------------------------------------------------------------------------
// Faithful constants (binary literals).
// ---------------------------------------------------------------------------
constexpr float kNpcInteractRange = 50.0f; // <= threshold used by the 4 action functions
                                            // (Math_Dist2D_XZ/Math_Dist3D <= 50.0)

// Offsets within the record pointed to by NpcEntity::def (cf. banner above). Reuses EXACTLY
// the names/values already established in Game/AutoPlaySystem.cpp (kDefOffFaction/kDefOffNpcKind)
// to stay consistent with the rest of the port ; +252/+260/+352 are specific to this file
// (Npc_GetNameplateColor / Char_DrawOverheadName 0x581440).
constexpr std::size_t kNpcDefOffFaction     = 184; // dword_17AB598[i]+184 (== AutoPlaySystem kDefOffFaction)
constexpr std::size_t kNpcDefOffKind        = 188; // dword_17AB598[i]+188 (== AutoPlaySystem kDefOffNpcKind ; ==1 -> "vendor"/direct vault)
constexpr std::size_t kNpcDefOffQuestCatA   = 232; // *(def+232) — objective category (Npc_IsQuestTarget/GetNameplateColor)
constexpr std::size_t kNpcDefOffQuestCatB   = 236; // *(def+236) — subcategory (category==1 branch)
constexpr std::size_t kNpcDefOffAggroLevel  = 352; // *(def+352) — compared against Level_ToAggroValue(local level)

// ---------------------------------------------------------------------------
// Raw LE dword read from an opaque record (same convention as the anonymous helpers in
// AutoPlaySystem.cpp — redeclared here locally, not exported there).
// ---------------------------------------------------------------------------
inline uint32_t NpcDefReadU32(const void* def, std::size_t offset) {
    if (!def) return 0;
    uint32_t v = 0;
    std::memcpy(&v, static_cast<const uint8_t*>(def) + offset, sizeof(v));
    return v;
}
inline int32_t NpcDefReadI32(const void* def, std::size_t offset) {
    return static_cast<int32_t>(NpcDefReadU32(def, offset));
}

// ---------------------------------------------------------------------------
// Runtime fields absent from NpcEntity (cf. banner above) — array parallel to
// g_World.npcs, same index (mirroring AutoPlaySystem::MonsterAutoplayExt). To be populated
// by the (future) port of NPC spawn/update; safe defaults (0) until something feeds them.
// ---------------------------------------------------------------------------
struct NpcInteractionExt {
    float    x = 0.0f, y = 0.0f, z = 0.0f; // flt_17AB5B4[38*i] (+128 original) : world position
    uint32_t offerItemId = 0;              // dword_17AB544[38*i] (+16) : item handled/given by the NPC
    uint32_t offerWeight = 0;              // dword_17AB548[38*i] (+20) : associated weight/quantity
};

// ---------------------------------------------------------------------------
// "Local element" context needed by Npc_IsQuestTarget/Npc_GetNameplateColor (mirrors blocks
// of g_LocalPlayerSheet 0x1685748 not covered by SelfState, cf. Game/GameState.h:
// g_ElementLoadout 0x1685E14..+0x1C = loadout[0..3], and Char_GetPairedElement 0x557C00 which
// searches for `element` in the same g_LocalPlayerSheet's alliance[2] pairs, +0x71C/+0x728).
// Pure data (no hidden behavior): the caller populates it from the future port of
// g_LocalPlayerSheet; defaults (everything at 0, pairedElement absent -> -1) = "no element",
// faithful to the original fallback.
// ---------------------------------------------------------------------------
struct NpcQuestContext {
    int localElement = 0;                 // = g_World.self.element (g_LocalElement 0x1673194)
    std::array<int, 4> elementLoadout{};   // g_ElementLoadout..+0x1C (loadout[0..3] ; loadout[4] never read by these 2 functions)
    int factionFlag = 0;                   // dword_1687320[0] (faction/camp indicator, exact meaning not proven here)

    // Char_GetPairedElement 0x557C00: "paired" element (other member of an alliance pair in
    // the loadout), -1 if absent. Pure injectable function (depends on g_LocalPlayerSheet,
    // out of scope for this mission's shared headers); nullptr -> constant -1 (faithful to
    // the original's "return -1" fallback at the end of the function).
    std::function<int(int element)> pairedElement;
    int GetPaired(int element) const { return pairedElement ? pairedElement(element) : -1; }
};

// ---------------------------------------------------------------------------
// Faithful distances (Math_Dist2D_XZ 0x53FA40 / Math_Dist3D 0x53FAA0 — sqrt(dx²+dz²) and
// sqrt(dx²+dy²+dz²), no approximation).
// ---------------------------------------------------------------------------
inline float Npc_DistanceXZ(float x1, float z1, float x2, float z2) {
    const float dx = x1 - x2, dz = z1 - z2;
    return std::sqrt(dx * dx + dz * dz);
}
inline float Npc_Distance3D(float x1, float y1, float z1, float x2, float y2, float z2) {
    const float dx = x1 - x2, dy = y1 - y2, dz = z1 - z2;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// ---------------------------------------------------------------------------
// Level_ToAggroValue 0x53F700 — full table (identity below 100, hardcoded curve 100..157,
// 1 by default beyond 157). Used by Npc_GetNameplateColor (default branch: comparing player
// "power" vs the NPC's/monster's threshold).
// ---------------------------------------------------------------------------
int Npc_LevelToAggroValue(int level);

// ===========================================================================
// Pure classification (no state, no network) — Npc_IsSpecialType 0x54EE60.
// ===========================================================================
// True for the fixed list {19,20,21,34,49,120,154,175,176,177,190,191,192,193} ("special"
// type/code table reused in several places in the binary — cf. xrefs Char_CalcAttackRating*,
// Char_BuildEquipSnapshot, UI_MainInventory: NOT NPC-specific despite the IDB name).
bool Npc_IsSpecialType(int typeOrCode);

// ===========================================================================
// Npc_IsQuestTarget 0x540340 / Npc_GetNameplateColor 0x540790 — PURE functions operating on
// a generic record pointer "def" (NpcEntity::def OR MonsterEntity::def, cf. banner above).
// `def` may be nullptr ("not found"/default color result, safe).
// ===========================================================================

// True if the entity matches the local player's active quest/element/faction objective
// (faithful branches: category 1 -> subcategory 232/236 ; categories 6/7/8/9/0xE/0xF ->
// direct element/faction; everything else -> false).
bool Npc_IsQuestTarget(const void* def, const NpcQuestContext& ctx);

// Nameplate color code (10 = ally/matched element, 2 = hostile/mismatch, 22/33 = default
// branch depending on the "power" gap Level_ToAggroValue(local level) vs *(def+352)).
// `selfLevel`/`selfLevelBonus` = g_World.self.level / g_World.self.levelBonus.
int Npc_GetNameplateColor(const void* def, const NpcQuestContext& ctx, int selfLevel, int selfLevelBonus);

// ---------------------------------------------------------------------------
// Out-of-scope integration points (network/UI/item/stat not modeled in this mission's
// shared headers). Optional callbacks; default behavior documented at the call site. Original
// EAs cited for the real wiring — NO direct network send is done by this module outside of
// SendVaultReq201.
// ---------------------------------------------------------------------------
struct NpcInteractionHost {
    // maybe_Npc_ShouldRefreshTarget 0x583E20: refresh/lock eligibility for the NPC
    // (alliance/chat name comparisons + timers — out of scope, depends on social UI).
    // Signature aligned with AutoPlaySystem::AutoPlayHost::ShouldRefreshNpc so the SAME
    // callback can be wired on both sides. Default (unwired): always eligible.
    std::function<bool(const NpcEntity&)> ShouldRefreshTarget;

    // cGameHud_PlaceItemIntoBag 0x650470: attempts to place (itemId, weight) into the bag.
    // outSlot=-1 => failure. Order of the other 3 outputs faithful to the original call
    // (&v9,&v11,&v10,&v14 — v9=outSlot ; v11/v10/v14 reordered as-is, semantics not proven,
    // re-injected as-is into Net_SendVaultReq_201).
    std::function<void(uint32_t itemId, uint32_t weight, int& outSlot, int& outB, int& outC, int& outD)> TryPlaceItemIntoBag;

    // Net_SendVaultReq_201 0x5901C0: network send of the NPC interaction/reward request.
    // Only network send point of this module ; PRECISE TODO: wire to the real Net_Send*
    // builder once the outbound layer is available.
    std::function<void(int idHi, int idLo, int p0, int outSlot, int outB, int outC, int outD)> SendVaultReq201;

    // Npc_Interact's "out of range" branch (0x53a73a) — approach on foot. Chains in the
    // binary: World_IsPointBlocked 0x540DA0 (tests the PLAYER's position, not the NPC's —
    // faithful, not an error on our part) -> Char_CalcAttackSpeed 0x4CCAB0 (speed) ->
    // Skill_TraceProjectilePath 0x5419F0 (computes an approach point) -> Net_QueueRunTo
    // 0x511B00 (enqueues the movement). Out of scope (collision + pathing + network):
    // PRECISE TODO, cf. EAs above. Called with the target NPC's position ; no-op by default.
    std::function<void(float npcX, float npcY, float npcZ)> ApproachNpc;

    // Item_GetEquipCategory 0x54C940: equip category of the selected item (1/2 in
    // AutoInteractForPet). Out of scope (Item/Stat, headers not included here). -1 by
    // default (never matches 1 or 2), faithful to the "no match" case.
    std::function<int(uint32_t itemId)> GetEquipCategory;

    // Full gate of Npc_AutoInteractForPet (0x53b600..0x53b661): MobDb_GetEntry(&mITEM,
    // selectedItemId) 0x4C3C00 must succeed AND (typeCode!=22 OR (Item_NormalizeStatByType
    // 0x4C8FF0 >= 100 AND associated counter >= 1)). Out of scope (Item/Stat). false by
    // default (function inert until wired — conservative: pet availability cannot be
    // proven without the real Item system).
    std::function<bool(uint32_t selectedItemId)> IsPetCommandItemReady;
};

// ---------------------------------------------------------------------------
// NpcInteractionSystem — carries the state shared between the 4 action functions
// ("request in flight" lock dword_1675B08/flt_1675B0C — SAME globals as
// AutoPlaySystem::pendingItemUseLatch_/pendingItemUseTimeSec_ documented in
// AutoPlaySystem.h: in the original binary this is a SINGLE lock shared between scroll use
// AND NPC interaction. This module carries ITS OWN copy; integration TODO: unify with
// AutoPlaySystem if both run concurrently in the game loop) + the out-of-scope hooks (host)
// + the per-NPC extension (ext_).
// ---------------------------------------------------------------------------
class NpcInteractionSystem {
public:
    NpcInteractionHost host;

    // g_MorphInProgress 0x1675A88: blocks emitting the NPC request (all action functions).
    // Driven by the caller (same global as AutoPlayExternalState::morphInProgress).
    bool morphInProgress = false;

    // Per-NPC extension (offer position + item/weight) — accessible for population by the
    // future NPC spawn port. Automatically resizes against g_World.npcs.
    NpcInteractionExt& Ext(std::size_t npcIndex);
    const NpcInteractionExt* TryExt(std::size_t npcIndex) const;

    // Npc_Interact 0x53A660 — finds the active NPC with identity `targetId`, approaches or
    // interacts depending on range (50.0). `gameTimeSec` = g_World.gameTimeSec (lock
    // timestamp, faithful to flt_1675B0C = g_GameTimeSec).
    void Interact(EntityId targetId, float gameTimeSec);

    // Npc_AutoInteract 0x53A980 — interacts with the current attack-order target
    // (`currentAttackOrderTarget` = g_SelfAttackOrder_GridX/Y 0x1687354/58, NO movement
    // if out of range — faithful, unlike Interact()). Returns the original return code
    // (0 = failure/absent, 1 = success or "out of range but not an error").
    int AutoInteractCurrentTarget(EntityId currentAttackOrderTarget, float gameTimeSec);

    // Npc_AutoSelectNearest 0x53ABC0 — scans g_World.npcs in 6 decreasing-priority passes
    // (direct vendor, then category {5,6}, {4}, {3}, {2}, {1}) and interacts with the first
    // exploitable NPC found ; failure message (weight/bag/no NPC) otherwise.
    void AutoSelectNearestInteractable(float gameTimeSec);

    // Npc_AutoInteractForPet 0x53B5F0 — auto command tied to the selected item
    // (`selectedItemId` = g_SelectedInvItemId 0x1673258). Gated via host.IsPetCommandItemReady.
    void AutoInteractForPet(uint32_t selectedItemId, float gameTimeSec);

private:
    std::vector<NpcInteractionExt> ext_;

    bool  pendingLatch_ = false;        // dword_1675B08 (g_GmCmdCooldownLatch)
    float pendingLatchTimeSec_ = 0.0f;  // flt_1675B0C

    // Common result for the 4 action functions (Net_SendVaultReq_201 0x5901C0 args).
    struct RewardArgs {
        int idHi = 0, idLo = 0, p0 = 0, outSlot = 0, outB = 0, outC = 0, outD = 0;
        bool ok = false;        // false => failure (do not send)
        bool blockedWeight = false; // true => blocked by Util_SumExceeds2Billion (weight)
        bool blockedBag    = false; // true => blocked by PlaceItemIntoBag failure
    };

    // Shared body (typeCode188==1 -> direct vault + weight guard ; otherwise PlaceItemIntoBag).
    RewardArgs BuildRewardArgs(const NpcEntity& npc, const NpcInteractionExt& ext) const;
    // Sends the request if neither morph nor lock is in progress (faithful: otherwise silent, NO error).
    void SendReward(const RewardArgs& args, float gameTimeSec);

    bool ShouldRefresh(const NpcEntity& npc) const;
    int  FindNpcIndexById(EntityId id) const;
};

} // namespace ts2::game

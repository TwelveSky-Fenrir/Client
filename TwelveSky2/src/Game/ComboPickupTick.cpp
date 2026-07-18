// Game/ComboPickupTick.cpp — see ComboPickupTick.h for the EA -> function table and the
// fidelity notes (GINFO2 not modeled, reuse of NpcInteraction/QuestSystem/
// SkillCombat/StringTables/Rng).
#include "Game/ComboPickupTick.h"
#include <cmath>
#include <initializer_list>

namespace ts2::game {

namespace {

// 3D Euclidean distance (Math_Dist3D 0x53FAA0: sqrt(dx^2+dy^2+dz^2)), same formula as
// Game/ItemPickupSystem.cpp / Game/NpcInteraction.cpp (redeclared locally, not exported
// elsewhere).
inline float Dist3D(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

} // namespace

// ===========================================================================
// Combat_IsElementAllowedOnMap 0x55CBF0.
// ===========================================================================
bool Combat_IsElementAllowedOnMap(int mapElement, int selfMorphNpcId, const ElementPairTable& pairs) {
    auto inSet = [selfMorphNpcId](std::initializer_list<int> ids) {
        for (int id : ids) if (selfMorphNpcId == id) return true;
        return false;
    };
    const int paired = pairs.Paired(mapElement);

    // Char_GetPairedElement is bounded to {-1,0,1,2,3}; each inner switch below covers
    // these 5 values exhaustively -> the "default" cases (binary's goto LABEL_47/92/137/182)
    // are dead code. Reproduced as `false`, never reached in practice.
    switch (mapElement) {
    case 0:
        switch (paired) {
        case -1: case 0: return inSet({6, 7, 8, 9, 11, 12, 13, 14, 140, 141, 142, 143});
        case 1:           return inSet({11, 12, 13, 14, 140, 141, 142, 143});
        case 2:           return inSet({6, 7, 8, 9, 140, 141, 142, 143});
        case 3:           return inSet({6, 7, 8, 9, 11, 12, 13, 14});
        default:          return false;
        }
    case 1:
        switch (paired) {
        case -1: case 1: return inSet({1, 2, 3, 4, 11, 12, 13, 14, 140, 141, 142, 143});
        case 0:           return inSet({11, 12, 13, 14, 140, 141, 142, 143});
        case 2:           return inSet({1, 2, 3, 4, 140, 141, 142, 143});
        case 3:           return inSet({1, 2, 3, 4, 11, 12, 13, 14});
        default:          return false;
        }
    case 2:
        switch (paired) {
        case -1: case 2: return inSet({1, 2, 3, 4, 6, 7, 8, 9, 140, 141, 142, 143});
        case 0:           return inSet({6, 7, 8, 9, 140, 141, 142, 143});
        case 1:           return inSet({1, 2, 3, 4, 140, 141, 142, 143});
        case 3:           return inSet({1, 2, 3, 4, 6, 7, 8, 9});
        default:          return false;
        }
    case 3:
        switch (paired) {
        case -1: case 3: return inSet({1, 2, 3, 4, 6, 7, 8, 9, 11, 12, 13, 14});
        case 0:           return inSet({6, 7, 8, 9, 11, 12, 13, 14});
        case 1:           return inSet({1, 2, 3, 4, 11, 12, 13, 14});
        case 2:           return inSet({1, 2, 3, 4, 6, 7, 8, 9});
        default:          return false;
        }
    default:
        return false; // EA 0x55d343 (LABEL_182)
    }
}

// ===========================================================================
// Combo_FindNearbyFollowup 0x501270.
// ===========================================================================
int Combo_FindNearbyFollowup(int motionId, float selfX, float selfY, float selfZ,
                              const ComboCandidateLookup& candidates,
                              const ComboTransitionCheck& transitionCheck) {
    if (motionId < 1 || motionId > 350) return -1; // EA 0x501286

    if (!candidates) return -1;
    const std::vector<ComboMotionCandidate> list = candidates(motionId);

    for (const ComboMotionCandidate& cand : list) {
        if (Dist3D(cand.x, cand.y, cand.z, selfX, selfY, selfZ) >= 30.0f) continue; // EA 0x50131c (< 30.0 strict)
        const int transition = transitionCheck ? transitionCheck(motionId, cand.id) : 0;
        if (transition == 1) return cand.id; // EA 0x501337
    }
    return -1; // EA 0x501341
}

// ===========================================================================
// BeginComboMorph EA 0x52CF69.
// ex-VeryOldClient: EFFECT_OBJECT type 11 (transform aura) + SetSantaEffect (type 14) —
//   PLAUSIBLE (Docs/TS2_FX_ROSETTA.md §1 "Fx_DrawZoneAura" + §4 render gap). This block only
//   carries the morph STATE (g_SelfMorphNpcId 0x1675A98, dword_1675A88..); the aura's VISUAL =
//   Fx_DrawZoneAura 0x583F90 (looping aura driven by g_GameTimeSec, gated on
//   g_SelfMorphNpcId) = RENDER GAP (Gfx layer, FRONT 2 NOT OWNED) — NOT implemented
//   here (build-safe). The aura consumes the SAME g_SelfMorphNpcId as this state.
// ===========================================================================
void BeginComboMorph(ComboMorphState& state, int followupMotionId, int currentMotionId,
                      net::NetClient& netClient, const ComboMotionOriginLookup& originLookup) {
    state.inProgress       = true;              // g_MorphInProgress = 1 (EA 0x52cec8)
    state.phase            = 4;                 // dword_1675A8C = 4    (EA 0x52ced2)
    state.unk90            = 0;                 // dword_1675A90 = 0    (EA 0x52cedc)
    state.followupMotionId = followupMotionId;  // dword_1675A9C = v24  (EA 0x52cee9)

    // Crt_Memset(&dword_1675AA0, 0, 72) -> full reset of the warp block.
    state.warp = ComboMorphWarpBlock{};
    state.warp.flag0 = 0;   // EA 0x52ceff (redundant with the memset)
    state.warp.flag1 = 1;   // EA 0x52cf09 (OVERWRITES the memset -> "armed")
    state.warp.timer = 0.0f; // EA 0x52cf15 (redundant)

    float originPos[3] = {0.0f, 0.0f, 0.0f};
    if (originLookup) originLookup(followupMotionId, currentMotionId, originPos); // EA 0x52cf30
    state.warp.targetX = originPos[0];
    state.warp.targetY = originPos[1];
    state.warp.targetZ = originPos[2];

    const float rotation = static_cast<float>(net::DefaultRng().NextMod(360)); // EA 0x52cf48
    state.warp.rotationCurrent = rotation; // EA 0x52cf48
    state.warp.rotationTarget  = rotation; // EA 0x52cf54 (== rotationCurrent at init)

    net::Net_SendPacket_Op20(netClient, static_cast<int8_t>(state.phase),
                              static_cast<int8_t>(followupMotionId)); // EA 0x52cf69
}

// ===========================================================================
// Automatic pickup of the 5 nearby slots (EA 0x52CF94..0x52D067).
// ===========================================================================
void TickNearbyPickupSlots(float selfX, float selfY, float selfZ, net::NetClient& netClient) {
    for (int slot = 0; slot < kNearbyPickupSlotCount; ++slot) {
        const uint32_t base = kNearbyPickupSlotBaseAddr + static_cast<uint32_t>(slot) * 12u;
        float& sx = g_Client.VarF(base);
        float& sy = g_Client.VarF(base + 4u);
        float& sz = g_Client.VarF(base + 8u);

        if (sx == 0.0f && sy == 0.0f && sz == 0.0f) continue; // EA 0x52d023 (all 3 == 0.0)
        if (Dist3D(sx, sy, sz, selfX, selfY, selfZ) >= kNearbyPickupRadius) continue; // < 100.0 strict

        sx = 0.0f; sy = 0.0f; sz = 0.0f; // EA 0x52d02d/0x52d03b/0x52d049

        const float selfPos[3] = {selfX, selfY, selfZ}; // payload = PLAYER position (faithful)
        net::Net_SendOp106(netClient, static_cast<int8_t>(slot), selfPos); // EA 0x52d05d
    }
}

// ===========================================================================
// Quest_UpdateMarkerTimer 0x510D90.
// ===========================================================================
void Quest_UpdateMarkerTimer(QuestMarkerState& marker, const QuestProgressState& progress,
                              float gameTimeSec, bool isArenaZone,
                              const WarehouseTargetMatch& warehouseTargetMatches,
                              const QuestMarkerSoundCallback& playMarkerSound) {
    if (isArenaZone) return; // EA 0x510d9e (Map_IsArenaZone)

    auto matches = [&](uint32_t key) { return warehouseTargetMatches && warehouseTargetMatches(key); };

    if (marker.active) { // EA 0x510daf (dword_this+51576 != 0)
        if (matches(marker.targetNpcQuestKey)) {
            marker.active = false; // EA 0x510f67
        } else if (gameTimeSec - marker.lastTimerSec >= 30.0f) { // EA 0x510f8d
            marker.lastTimerSec = gameTimeSec; // EA 0x510f9a
            marker.active = false;              // EA 0x510fa3
        }
        return;
    }

    if (gameTimeSec - marker.lastTimerSec < 600.0f) return; // EA 0x510dd6
    marker.lastTimerSec = gameTimeSec; // EA 0x510de6

    marker.lastObjectiveState = Quest_CheckObjectiveState(progress); // EA 0x510df7 (REUSES QuestSystem.h)
    const int32_t v2 = marker.lastObjectiveState;
    if (v2 == 0) return; // EA 0x510e0d

    if (v2 == 1) {
        // EA 0x510e40: NpcTbl_FindByTypeAndId(mQUEST, zoneId, npcQuestId+1) -> REUSES
        // QuestSystem.h::LookupQuestStep (same signature/table).
        const QuestStepRecord* rec = LookupQuestStep(progress.zoneId, progress.npcQuestId + 1);
        if (rec && !matches(rec->field92)) { // EA 0x510e6e
            if (playMarkerSound) playMarkerSound(); // EA 0x510e80
            marker.active           = true;         // EA 0x510e88
            marker.targetNpcQuestKey = rec->field92; // EA 0x510e9b
            marker.markerVariant     = 0;             // EA 0x510ea4
        }
    } else {
        // EA 0x510ecc: NpcTbl_FindByTypeAndId(mQUEST, zoneId, npcQuestId) without +1.
        const QuestStepRecord* rec = LookupQuestStep(progress.zoneId, progress.npcQuestId);
        if (rec && !matches(rec->field92)) { // EA 0x510eff
            marker.active           = true;                                   // EA 0x510f09
            marker.targetNpcQuestKey = rec->field92;                          // EA 0x510f1c
            marker.markerVariant     = net::DefaultRng().NextMod(3) + 1;      // EA 0x510f35
        }
    }
}

// ===========================================================================
// Tips002_RotateUpdate 0x4C1840.
// ===========================================================================
void Tips_RotateUpdate(TipsTable& tips, float gameTimeSec) {
    if (tips.Advance(gameTimeSec)) { // timer/index already faithful (Game/StringTables.h)
        // EA 0x4c18c6: Msg_AppendChatLine(g_ChatManager, text, 3, &String). The "3" is a
        // mFONTCOLOR palette INDEX (ColorTable_InitPalette 0x4C1D60), NOT an ARGB: the
        // binary stores it raw and resolves it at DRAW time via ColorTable_GetColor
        // 0x4C1FE0 (colors[3] = -256 = 0xFFFFFF00, opaque yellow). MessageLog::Chat takes
        // an ARGB (pre-existing architecture, ~500 call sites out of scope): resolved here,
        // at the producer (documented deviation of the resolution point, same pixel).
        // Without this fix, passing 3 as an ARGB gave 0x00000003 (alpha 0) -> invisible banner.
        g_Client.msg.Chat(tips.Current(), g_Strings.colors.Get(3)); // 0x4C1FE0: Get(3) -> 0xFFFFFF00
    }
}

} // namespace ts2::game

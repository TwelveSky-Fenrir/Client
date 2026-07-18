// Game/InGameTickFlow.cpp — see InGameTickFlow.h for the discovered flow, the exact tick
// order, and the original EAs.
#include "Game/InGameTickFlow.h"

namespace ts2::game {

namespace {
constexpr int   kSpawnTimeoutFrames   = 5000; // 0x1388 @0x52C6A5
constexpr int   kKeepAliveEveryFrames = 300;  // 0x12C  @0x52C891 (and 0x52C850, anticheat ignored)
constexpr int   kGatingEveryFrames    = 30;   // 0x1E   @0x52CC58 / @0x52CE8E
constexpr float kStaleEntitySeconds   = 7.5f; // @0x52C9CB / 0x52CAA4 / 0x52CB31
constexpr int   kBusyState1 = 11, kBusyState2 = 12, kBusyState3 = 33, kBusyState4 = 34,
                kBusyState5 = 35, kBusyState6 = 36, kBusyState7 = 37; // @0x52CC58..

inline bool IsBusyActionState(int s) {
    return s == kBusyState1 || s == kBusyState2 || s == kBusyState3 || s == kBusyState4 ||
           s == kBusyState5 || s == kBusyState6 || s == kBusyState7;
}

void RunMainTick(InGameTickFlowState& s, const InGameTickFlowHost& h, float dt) {
    ++s.frameCounter; // @0x52C850 (counter incremented by the original GameGuard poll,
                       // ignored here, but the increment itself is faithfully kept)

    // 1. Keepalive every 300 frames + poll pending clan/faction request. @0x52C891
    if (s.frameCounter % kKeepAliveEveryFrames == 0) {
        bool ok = h.SendKeepAlive && h.SendKeepAlive();
        if (!ok && h.AppendKeepAliveFailedMessage) h.AppendKeepAliveFailedMessage(); // @0x52C8C4
        if (h.HasPendingTargetRequest && h.HasPendingTargetRequest() && h.SendPendingTargetPoll) {
            h.SendPendingTargetPoll(); // @0x52C8FA
        }
    }

    // 2. 10 s timeout on the "warp suppressed" flag. @0x52C91F
    if (h.TickWarpSuppressionTimeout) h.TickWarpSuppressionTimeout(g_World.gameTimeSec);

    // 3-5. Unconditional anim/collision every frame. @0x52C930..0x52C975
    if (h.AutoUsePotion) h.AutoUsePotion(dt);                          // @0x52C930
    if (h.UpdateMapObjectAnim) h.UpdateMapObjectAnim(dt);              // @0x52C94B (constant 15.0 on the original side)
    if (h.UpdateLocalPlayerAnim) h.UpdateLocalPlayerAnim(dt);          // @0x52C95A
    if (h.UpdateEntityAnimFrame) h.UpdateEntityAnimFrame(0, dt);       // @0x52C96D (idx 0 = self)
    if (h.UpdateCameraCollision) h.UpdateCameraCollision();            // @0x52C975

    // 6. Remote players (indices 1..N-1), 7.5 s staleness. @0x52C97A
    for (size_t j = 1; j < g_World.players.size(); ++j) {
        const PlayerEntity& p = g_World.players[j];
        if (!p.active) continue;
        if (g_World.gameTimeSec - p.timestamp <= kStaleEntitySeconds) {
            if (h.UpdateEntityAnimFrame) h.UpdateEntityAnimFrame(static_cast<int>(j), dt); // @0x52C9FD
        } else if (h.DespawnStalePlayer) {
            h.DespawnStalePlayer(static_cast<int>(j), dt); // @0x52C9DC
        }
    }

    // 7. 88-byte array (GroundItem per GameState.h — cf. the naming mismatch documented
    //    in the header). NO staleness check: unconditional tick if active. @0x52CA07
    for (size_t k = 0; k < g_World.npcRenderEntries.size(); ++k) {
        if (g_World.npcRenderEntries[k].active && h.TickGroundItemEffect) {
            h.TickGroundItemEffect(static_cast<int>(k), dt); // @0x52CA4C
        }
    }

    // 8. Monsters, 7.5 s staleness (otherwise respawn after knockback, NOT despawn). @0x52CA53
    for (size_t m = 0; m < g_World.monsters.size(); ++m) {
        const MonsterEntity& mo = g_World.monsters[m];
        if (!mo.active) continue;
        if (g_World.gameTimeSec - mo.timestamp <= kStaleEntitySeconds) {
            if (h.UpdateMonster) h.UpdateMonster(static_cast<int>(m), dt); // @0x52CAD6
        } else if (h.RespawnMonsterAfterKnockback) {
            h.RespawnMonsterAfterKnockback(static_cast<int>(m)); // @0x52CAB5
        }
    }

    // 9. 152-byte array (NpcEntity per GameState.h — cf. the naming mismatch documented
    //    in the header). 7.5 s staleness (otherwise cleanup). @0x52CAE0
    for (size_t n = 0; n < g_World.npcs.size(); ++n) {
        const NpcEntity& npc = g_World.npcs[n];
        if (!npc.active) continue;
        if (g_World.gameTimeSec - npc.timestamp <= kStaleEntitySeconds) {
            if (h.TickNpcEffect) h.TickNpcEffect(static_cast<int>(n), dt); // @0x52CB63
        } else if (h.CleanupStaleNpcEffect) {
            h.CleanupStaleNpcEffect(static_cast<int>(n)); // @0x52CB42
        }
    }

    // 10. Attack projectile pool (g_FxAuraCount/dword_17D06F4, already catalogued in
    //     Docs/TS2_FX_CATALOG.md — NOT a buff/debuff aura pool despite the hook names;
    //     intentionally NOT modeled in GameState.h), no staleness check. @0x52CB6D
    if (h.GetFxAuraCount) {
        int count = h.GetFxAuraCount();
        for (int ii = 0; ii < count; ++ii) {
            if (h.IsFxAuraActive && h.IsFxAuraActive(ii) && h.UpdateHomingProjectile) {
                h.UpdateHomingProjectile(ii, dt); // @0x52CBB2
            }
        }
    }

    // 11. Zone objects/resource nodes (dword_1687230/dword_180EEF4, pool DISTINCT from
    //     the previous one — now modeled in GameState.h via ZoneObjectEntity/
    //     g_World.zoneObjects, hooks not wired here), no staleness check, NO index passed
    //     to the hook (faithful to the original, cf. header). @0x52CBB9
    if (h.GetWorldObjectCount) {
        int count = h.GetWorldObjectCount();
        for (int jj = 0; jj < count; ++jj) {
            if (h.IsWorldObjectActive && h.IsWorldObjectActive(jj) && h.TickWorldObject) {
                h.TickWorldObject(dt); // @0x52CBFA
            }
        }
    }

    // 12. Targeting/pickup/combo gating gate — see header for the exact semantics
    //     (faithfully reproduced, including the "block entirely skipped" case). @0x52CC58
    int selfState = h.GetSelfActionState ? h.GetSelfActionState() : 0;
    bool busy = IsBusyActionState(selfState);
    bool exchangeOpen = h.IsExchangeWindowOpen && h.IsExchangeWindowOpen();
    bool onGateTick = (s.frameCounter % kGatingEveryFrames == 0);

    bool runBlock = false;
    if (!onGateTick || busy || exchangeOpen) {
        // Direct bypass to the block (original LABEL_71), without NPC auto-interaction.
        runBlock = true;
    } else {
        bool canPetInteract = h.CanAutoInteractNpc && h.CanAutoInteractNpc();
        bool invDirty       = h.IsInventoryDirty && h.IsInventoryDirty();
        if (canPetInteract && !invDirty) {
            if (h.AutoInteractNpcForPet) h.AutoInteractNpcForPet(); // @0x52CC83
            runBlock = true;
        }
        // else: block entirely skipped this frame (faithful to the original).
    }

    if (runBlock) {
        // 12a. Auto-target validation (mode + target array owned elsewhere). @0x52CCA7
        if (h.ValidateAutoTarget) h.ValidateAutoTarget();

        // 12b. Quest marker timer. @0x52CE77
        if (h.UpdateQuestMarkerTimer) h.UpdateQuestMarkerTimer();

        // 12c. Follow-up combo, re-tested independently every 30 frames. @0x52CE8E
        if (s.frameCounter % kGatingEveryFrames == 0) {
            int followup = h.FindComboFollowupTarget ? h.FindComboFollowupTarget() : -1;
            bool morphing = h.IsMorphInProgress && h.IsMorphInProgress();
            if (followup != -1 && !morphing && h.BeginComboMorph) {
                h.BeginComboMorph(followup); // @0x52CF69
            }
        }

        // 12d. Nearby pickup, if combat is allowed on the map and the player is not GM. @0x52CF8E
        bool combatAllowed = h.IsCombatAllowedOnMap && h.IsCombatAllowedOnMap();
        bool isGm          = h.IsGm && h.IsGm();
        if (combatAllowed && !isGm && h.TickNearbyPickupSlots) {
            h.TickNearbyPickupSlots(); // loop over the 5 slots @0x52CF94..0x52D05D
        }

        // 12e. Tip text rotation. @0x52D06C
        if (h.RotateTipText) h.RotateTipText();
    }
}
} // namespace

void InGameTickFlow_Update(InGameTickFlowState& s, const InGameTickFlowHost& h, float dt) {
    switch (s.state) {

    case InGameTickState::Setup: // case 0 @0x52C61F (one-shot, no wait)
        if (h.ResetUiAndScratch) h.ResetUiAndScratch();
        s.state         = InGameTickState::WaitFirstSpawn; // @0x52C676
        s.frameCounter  = 0;
        return;

    case InGameTickState::WaitFirstSpawn: // case 1 @0x52C69B
        // FIDELITY FIX (audit 2026-07-14): the "spawn received" short-circuit documented
        // below was wired NOWHERE in ClientSource (EntityManager::
        // OnSpawnCharacter explicitly returns this "out of entity scope" point — cf. its
        // comment — and no other caller ever sets
        // InGameTickState::InitCamera): without this check, the state machine stayed stuck
        // here until the full timeout (5000 frames, ~166 s), then moved to Failed and
        // NEVER executed MainTick again, regardless of the actual world state. Faithful to
        // Pkt_SpawnCharacter (EA 0x4646C0, inbound network opcode 0x0f), which directly writes
        // g_SceneSubState=3 as soon as the entity at INDEX 0 (self) is created: this module is
        // already coupled to Game/GameState.h (cf. rest of MainTick), so it reads directly
        // g_World.players[0].active (set to true by GameState.cpp::FindOrAdd at the exact
        // moment of the spawn, BEFORE this Update() is called again the same frame, if the
        // network is processed ahead of the game loop — same ordering assumption as the
        // original binary): NO new host hook, NO SceneManager coupling required.
        if (!g_World.players.empty() && g_World.players[0].active) {
            s.state        = InGameTickState::InitCamera; // @0x464901 (Pkt_SpawnCharacter, i==0)
            s.frameCounter = 0;                            // @0x46490b
            return;
        }
        ++s.frameCounter;
        if (s.frameCounter >= kSpawnTimeoutFrames) {
            if (h.ShowSpawnTimeoutNotice) h.ShowSpawnTimeoutNotice(); // @0x52C6C5
            s.state        = InGameTickState::Failed; // @0x52C6CD
            s.frameCounter = 0;
        }
        return;

    case InGameTickState::Failed: // case 2 @0x52C6DE (terminal)
        return;

    case InGameTickState::InitCamera: { // case 3 @0x52C6EF (one-shot)
        const PlayerEntity& self = g_World.players.empty() ? PlayerEntity{} : g_World.players[0];
        if (h.InitCamera) h.InitCamera(self.x, self.y, self.z); // @0x52C759/0x52C7CF/0x52C7E8
        s.state        = InGameTickState::MainTick; // @0x52C7F1
        s.frameCounter = 0;
        return;
    }

    case InGameTickState::MainTick: // default @0x52C81C — never exited
    default:
        RunMainTick(s, h, dt);
        return;
    }
}

} // namespace ts2::game

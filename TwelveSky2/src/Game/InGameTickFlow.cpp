// Game/InGameTickFlow.cpp — voir InGameTickFlow.h pour le flux découvert, l'ordre exact
// du tick et les EAs d'origine.
#include "Game/InGameTickFlow.h"

namespace ts2::game {

namespace {
constexpr int   kSpawnTimeoutFrames   = 5000; // 0x1388 @0x52C6A5
constexpr int   kKeepAliveEveryFrames = 300;  // 0x12C  @0x52C891 (et 0x52C850, anticheat ignoré)
constexpr int   kGatingEveryFrames    = 30;   // 0x1E   @0x52CC58 / @0x52CE8E
constexpr float kStaleEntitySeconds   = 7.5f; // @0x52C9CB / 0x52CAA4 / 0x52CB31
constexpr int   kBusyState1 = 11, kBusyState2 = 12, kBusyState3 = 33, kBusyState4 = 34,
                kBusyState5 = 35, kBusyState6 = 36, kBusyState7 = 37; // @0x52CC58..

inline bool IsBusyActionState(int s) {
    return s == kBusyState1 || s == kBusyState2 || s == kBusyState3 || s == kBusyState4 ||
           s == kBusyState5 || s == kBusyState6 || s == kBusyState7;
}

void RunMainTick(InGameTickFlowState& s, const InGameTickFlowHost& h, float dt) {
    ++s.frameCounter; // @0x52C850 (compteur incrémenté par le poll GameGuard d'origine,
                       // ignoré ici, mais l'incrément lui-même est fidèlement conservé)

    // 1. Keepalive /300 frames + poll requête clan/faction en attente. @0x52C891
    if (s.frameCounter % kKeepAliveEveryFrames == 0) {
        bool ok = h.SendKeepAlive && h.SendKeepAlive();
        if (!ok && h.AppendKeepAliveFailedMessage) h.AppendKeepAliveFailedMessage(); // @0x52C8C4
        if (h.HasPendingTargetRequest && h.HasPendingTargetRequest() && h.SendPendingTargetPoll) {
            h.SendPendingTargetPoll(); // @0x52C8FA
        }
    }

    // 2. Timeout 10 s du flag "warp suppressé". @0x52C91F
    if (h.TickWarpSuppressionTimeout) h.TickWarpSuppressionTimeout(g_World.gameTimeSec);

    // 3-5. Anim/collision inconditionnelles chaque frame. @0x52C930..0x52C975
    if (h.AutoUsePotion) h.AutoUsePotion(dt);                          // @0x52C930
    if (h.UpdateMapObjectAnim) h.UpdateMapObjectAnim(dt);              // @0x52C94B (constante 15.0 côté original)
    if (h.UpdateLocalPlayerAnim) h.UpdateLocalPlayerAnim(dt);          // @0x52C95A
    if (h.UpdateEntityAnimFrame) h.UpdateEntityAnimFrame(0, dt);       // @0x52C96D (idx 0 = soi)
    if (h.UpdateCameraCollision) h.UpdateCameraCollision();            // @0x52C975

    // 6. Joueurs distants (indices 1..N-1), péremption 7,5 s. @0x52C97A
    for (size_t j = 1; j < g_World.players.size(); ++j) {
        const PlayerEntity& p = g_World.players[j];
        if (!p.active) continue;
        if (g_World.gameTimeSec - p.timestamp <= kStaleEntitySeconds) {
            if (h.UpdateEntityAnimFrame) h.UpdateEntityAnimFrame(static_cast<int>(j), dt); // @0x52C9FD
        } else if (h.DespawnStalePlayer) {
            h.DespawnStalePlayer(static_cast<int>(j), dt); // @0x52C9DC
        }
    }

    // 7. Tableau 88 o (GroundItem au sens GameState.h — cf. écart de nommage documenté
    //    dans le header). AUCUNE péremption : tick inconditionnel si actif. @0x52CA07
    for (size_t k = 0; k < g_World.groundItems.size(); ++k) {
        if (g_World.groundItems[k].active && h.TickGroundItemEffect) {
            h.TickGroundItemEffect(static_cast<int>(k), dt); // @0x52CA4C
        }
    }

    // 8. Monstres, péremption 7,5 s (sinon respawn après knockback, PAS despawn). @0x52CA53
    for (size_t m = 0; m < g_World.monsters.size(); ++m) {
        const MonsterEntity& mo = g_World.monsters[m];
        if (!mo.active) continue;
        if (g_World.gameTimeSec - mo.timestamp <= kStaleEntitySeconds) {
            if (h.UpdateMonster) h.UpdateMonster(static_cast<int>(m), dt); // @0x52CAD6
        } else if (h.RespawnMonsterAfterKnockback) {
            h.RespawnMonsterAfterKnockback(static_cast<int>(m)); // @0x52CAB5
        }
    }

    // 9. Tableau 152 o (NpcEntity au sens GameState.h — cf. écart de nommage documenté
    //    dans le header). Péremption 7,5 s (sinon cleanup). @0x52CAE0
    for (size_t n = 0; n < g_World.npcs.size(); ++n) {
        const NpcEntity& npc = g_World.npcs[n];
        if (!npc.active) continue;
        if (g_World.gameTimeSec - npc.timestamp <= kStaleEntitySeconds) {
            if (h.TickNpcEffect) h.TickNpcEffect(static_cast<int>(n), dt); // @0x52CB63
        } else if (h.CleanupStaleNpcEffect) {
            h.CleanupStaleNpcEffect(static_cast<int>(n)); // @0x52CB42
        }
    }

    // 10. Pool de projectiles d'attaque (g_FxAuraCount/dword_17D06F4, déjà catalogué dans
    //     Docs/TS2_FX_CATALOG.md — PAS un pool d'auras buff/debuff malgré le nom des hooks ;
    //     intentionnellement PAS modélisé dans GameState.h), aucune péremption. @0x52CB6D
    if (h.GetFxAuraCount) {
        int count = h.GetFxAuraCount();
        for (int ii = 0; ii < count; ++ii) {
            if (h.IsFxAuraActive && h.IsFxAuraActive(ii) && h.UpdateHomingProjectile) {
                h.UpdateHomingProjectile(ii, dt); // @0x52CBB2
            }
        }
    }

    // 11. Objets de zone/nœuds de ressource (dword_1687230/dword_180EEF4, pool DISTINCT du
    //     précédent — désormais modélisé dans GameState.h via ZoneObjectEntity/
    //     g_World.zoneObjects, hooks non branchés ici), aucune péremption, PAS d'indice passé
    //     au hook (fidèle à l'original, cf. header). @0x52CBB9
    if (h.GetWorldObjectCount) {
        int count = h.GetWorldObjectCount();
        for (int jj = 0; jj < count; ++jj) {
            if (h.IsWorldObjectActive && h.IsWorldObjectActive(jj) && h.TickWorldObject) {
                h.TickWorldObject(dt); // @0x52CBFA
            }
        }
    }

    // 12. Porte de gating ciblage/pickup/combo — cf. header pour la sémantique exacte
    //     (reproduite fidèlement y compris son cas "bloc entièrement sauté"). @0x52CC58
    int selfState = h.GetSelfActionState ? h.GetSelfActionState() : 0;
    bool busy = IsBusyActionState(selfState);
    bool exchangeOpen = h.IsExchangeWindowOpen && h.IsExchangeWindowOpen();
    bool onGateTick = (s.frameCounter % kGatingEveryFrames == 0);

    bool runBlock = false;
    if (!onGateTick || busy || exchangeOpen) {
        // Bypass direct vers le bloc (LABEL_71 d'origine), sans auto-interaction PNJ.
        runBlock = true;
    } else {
        bool canPetInteract = h.CanAutoInteractNpc && h.CanAutoInteractNpc();
        bool invDirty       = h.IsInventoryDirty && h.IsInventoryDirty();
        if (canPetInteract && !invDirty) {
            if (h.AutoInteractNpcForPet) h.AutoInteractNpcForPet(); // @0x52CC83
            runBlock = true;
        }
        // sinon : bloc entièrement sauté cette frame (fidèle à l'original).
    }

    if (runBlock) {
        // 12a. Validation de la cible auto (mode + tableau cible détenus ailleurs). @0x52CCA7
        if (h.ValidateAutoTarget) h.ValidateAutoTarget();

        // 12b. Timer des marqueurs de quête. @0x52CE77
        if (h.UpdateQuestMarkerTimer) h.UpdateQuestMarkerTimer();

        // 12c. Combo de suivi, retesté indépendamment toutes les 30 frames. @0x52CE8E
        if (s.frameCounter % kGatingEveryFrames == 0) {
            int followup = h.FindComboFollowupTarget ? h.FindComboFollowupTarget() : -1;
            bool morphing = h.IsMorphInProgress && h.IsMorphInProgress();
            if (followup != -1 && !morphing && h.BeginComboMorph) {
                h.BeginComboMorph(followup); // @0x52CF69
            }
        }

        // 12d. Pickup à proximité, si combat autorisé sur la carte et joueur non-GM. @0x52CF8E
        bool combatAllowed = h.IsCombatAllowedOnMap && h.IsCombatAllowedOnMap();
        bool isGm          = h.IsGm && h.IsGm();
        if (combatAllowed && !isGm && h.TickNearbyPickupSlots) {
            h.TickNearbyPickupSlots(); // boucle des 5 emplacements @0x52CF94..0x52D05D
        }

        // 12e. Rotation du texte d'astuce. @0x52D06C
        if (h.RotateTipText) h.RotateTipText();
    }
}
} // namespace

void InGameTickFlow_Update(InGameTickFlowState& s, const InGameTickFlowHost& h, float dt) {
    switch (s.state) {

    case InGameTickState::Setup: // case 0 @0x52C61F (one-shot, pas d'attente)
        if (h.ResetUiAndScratch) h.ResetUiAndScratch();
        s.state         = InGameTickState::WaitFirstSpawn; // @0x52C676
        s.frameCounter  = 0;
        return;

    case InGameTickState::WaitFirstSpawn: // case 1 @0x52C69B
        // CORRECTIF FIDÉLITÉ (audit 2026-07-14) : le court-circuit "spawn reçu" documenté
        // ci-dessous n'était câblé NULLE PART dans ClientSource (EntityManager::
        // OnSpawnCharacter renvoie explicitement ce point "hors périmètre entité" — cf. son
        // commentaire — et aucun autre appelant ne pose jamais
        // InGameTickState::InitCamera) : sans ce test, l'automate restait bloqué ici
        // jusqu'au timeout complet (5000 frames, ~166 s) puis passait en Failed et
        // n'exécutait plus JAMAIS MainTick, quel que soit le vrai état du monde. Fidèle à
        // Pkt_SpawnCharacter (EA 0x4646C0, opcode réseau 0x0f) qui écrit directement
        // g_SceneSubState=3 dès que l'entité d'INDICE 0 (soi-même) est créée : ce module est
        // déjà couplé à Game/GameState.h (cf. reste de MainTick) donc lit directement
        // g_World.players[0].active (posé à true par GameState.cpp::FindOrAdd au moment
        // exact du spawn, AVANT que ce Update() ne soit rappelé la même frame si le réseau
        // est traité en amont de la boucle de jeu — même hypothèse d'ordonnancement que le
        // binaire d'origine) : AUCUN nouveau hook host, AUCUN couplage SceneManager requis.
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

    case InGameTickState::MainTick: // default @0x52C81C — jamais quittée
    default:
        RunMainTick(s, h, dt);
        return;
    }
}

} // namespace ts2::game

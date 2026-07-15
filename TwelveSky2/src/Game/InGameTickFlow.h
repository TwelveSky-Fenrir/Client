// Game/InGameTickFlow.h — Machine d'état + ordre de tick de la scène EN JEU (scène InGame = 6).
//
// Réécriture C++ fidèle de Scene_InGameUpdate 0x52C600 (dispatché par cSceneMgr_Update
// 0x517BF0 quand g_SceneMgr.sceneId == Scene::InGame ; cSceneMgr_Update appelle ENSUITE
// AutoPlay_Update(g_AutoPlayBot) — CONFIRMÉ dans le désassemblage, PAS réimplémenté ici,
// cf. note d'intégration en bas de fichier). Source unique de vérité : décompilation
// Hex-Rays via idaTs2.
//
// FLUX RÉEL DÉCOUVERT (5 sous-états, dont un boucle indéfiniment) :
//
//   0 Setup          : 1 frame. Reset scratch/UI/audio résiduels -> WaitFirstSpawn.
//   1 WaitFirstSpawn : attente avec timeout 5000 frames (0x1388, ~166 s). Le client N'ENTRE
//                      PAS lui-même en jeu : c'est le paquet serveur de spawn du personnage
//                      LOCAL (Pkt_SpawnCharacter, opcode 15 entrant, EA 0x4646C0, RÉSEAU —
//                      hors périmètre) qui, quand il crée l'entité d'INDICE 0 (= soi-même,
//                      cf. Docs mémoire ts2-entity-model), écrit directement subState=3
//                      (InitCamera) + remet le compteur à 0, court-circuitant ce timeout.
//                      Si rien n'arrive avant 5000 frames : notice (StrTable005 id 71) ->
//                      état 2 (Failed). CORRECTIF FIDÉLITÉ (audit 2026-07-14) : ce module
//                      détecte désormais lui-même ce court-circuit en lisant directement
//                      g_World.players[0].active (déjà un couplage existant à
//                      Game/GameState.h, cf. MainTick) — AVANT ce correctif, RIEN dans
//                      ClientSource ne posait jamais InGameTickState::InitCamera
//                      (EntityManager::OnSpawnCharacter renvoyait explicitement ce point
//                      "hors périmètre entité") : l'automate restait bloqué ici jusqu'au
//                      timeout puis passait en Failed pour de bon, MainTick n'étant alors
//                      JAMAIS exécuté. Cf. Game/InGameTickFlow.cpp pour le détail.
//   2 Failed         : terminal, ne fait rien (comportement fidèle du case 2 d'origine,
//                      qui est un simple `return`).
//   3 InitCamera     : 1 frame, one-shot. Cadre la caméra 3e personne sur le joueur local
//                      (eye = self + (50,60,50), look = self + (0,10,0), cf. .cpp) puis
//                      -> MainTick avec compteur remis à 0.
//   4+ MainTick      : boucle principale, JAMAIS quittée (le switch d'origine n'a pas de
//                      case au-delà de 3 ; tout sous-état >=4 tombe dans le `default` qui
//                      ne change plus jamais subState). Exécutée à CHAQUE frame tant que
//                      la scène reste InGame. Détail de l'ordre exact ci-dessous.
//
// ORDRE EXACT DU TICK PRINCIPAL (MainTick, un seul passage de Scene_InGameUpdate) :
//   1. Toutes les 300 frames (0x12C) : keepalive Net_SendPacket_Op13 (message système si
//      échec) + éventuel poll de requête clan/faction en attente (Net_SendOp64) si les
//      deux noms de requête sont renseignés.
//   2. Timeout de 10 s sur le flag "warp suppressé" (dword_1675B00 / AutoPlayExternalState
//      ::warpSuppressed) : auto-clear si g_GameTimeSec dépasse le seuil.
//   3. Auto-utilisation de potion (Game_AutoUsePotion, CHAQUE frame).
//   4. Anim des objets de collision de map (MapColl_UpdateObjectAnim, CHAQUE frame).
//   5. Anim locale du joueur (Player_UpdateLocalAnim) puis anim de l'entité 0=soi
//      (Char_UpdateAnimationFrame) puis collision caméra (Camera_UpdateCollision).
//   6. Boucle JOUEURS distants (indices 1..N-1, g_World.players) : anim si vu il y a
//      <=7,5 s, sinon despawn (entité jugée périmée).
//   7. Boucle "objets au sol" / tableau à 88 o (Fx_MeleeSwingTick — nom d'origine
//      surprenant pour un tick de rendu de ramassage, cf. écart ci-dessous) : tick
//      inconditionnel si actif.
//   8. Boucle MONSTRES (g_World.monsters) : update si vu il y a <=7,5 s, sinon respawn
//      après knockback (entité jugée périmée -> réanimation, PAS despawn).
//   9. Boucle "NPC" / tableau à 152 o (Fx_GibUpdate — nom d'origine surprenant, cf. écart
//      ci-dessous) : update si vu il y a <=7,5 s, sinon cleanup.
//  10. Boucle "auras"/projectiles homing (tableau à 64 o, PAS modélisé dans GameState.h) :
//      tick inconditionnel si actif, AUCUNE vérification de péremption 7,5 s.
//  11. Boucle "objets de monde" (tableau à 76 o, PAS modélisé dans GameState.h) : tick
//      inconditionnel si actif. PARTICULARITÉ FIDÈLE : l'appel d'origine (sub_584170) ne
//      reçoit PAS l'indice de boucle, seulement dt — reproduit tel quel (host sans index).
//  12. Bloc ciblage/pickup/combo automatique — cf. section "PORTE DE GATING" plus bas.
//
// ÉCART CONNU (à signaler, PAS à corriger ici) : GameState.h modélise le tableau à 88 o
// (dword_1764D14, ex-"g_NpcRenderArray") comme GroundItem et celui à 152 o (dword_17AB534)
// comme NpcEntity, alors que les fonctions de tick d'origine sur ces tableaux s'appellent
// Fx_MeleeSwingTick (88 o) et Fx_GibUpdate (152 o) — des noms plus évocateurs d'effets
// visuels que de logique "objet au sol"/"PNJ". Le désassemblage confirme les tailles/
// compteurs (g_NpcCount pour le premier, dword_1687228 pour le second) mais PAS la
// sémantique définitive de ces deux tableaux. Ce module suit la classification de
// GameState.h par cohérence avec le socle partagé ; l'orchestrateur devra trancher.
//
// PORTE DE GATING du bloc ciblage/pickup/combo (fidèle, y compris sa bizarrerie) :
//   Soit A = (frameCounter % 30 == 0) ET état d'action != {11,12,33,34,35,36,37} ET
//            fenêtre d'échange fermée.
//   - si !A : le bloc s'exécute SANS appeler l'auto-interaction PNJ (pour animaux/pets).
//   - si A ET (host peut auto-interagir PNJ ET inventaire non "dirty") : appelle
//     l'auto-interaction PNJ PUIS exécute le bloc.
//   - si A ET (host ne peut pas / inventaire dirty) : le bloc est ENTIÈREMENT SAUTÉ cette
//     frame-là (aucune validation de cible, aucun combo, aucun pickup, aucune rotation de
//     tip). C'est exactement le comportement du binaire (if/goto imbriqué sans else) —
//     reproduit tel quel, pas une simplification de notre part.
//   Contenu du bloc quand il s'exécute :
//     a. Validation de la cible auto (mode + cible encore existante/à portée) -> host.
//     b. Timer des marqueurs de quête (Quest_UpdateMarkerTimer) -> host.
//     c. Toutes les 30 frames (re-testé INDÉPENDAMMENT du gate ci-dessus, sur la même
//        valeur de compteur) : recherche d'un combo de suivi à proximité ; si trouvé et
//        qu'aucun morph n'est déjà en cours -> déclenche le morph de combo -> host.
//     d. Si l'élément local est autorisé sur la carte et que le joueur n'est pas GM :
//        tick des 5 emplacements de pickup à proximité (<100 unités) -> host.
//     e. Rotation du texte d'astuce (Tips002_RotateUpdate) -> host.
//
// GAMEGUARD/ANTICHEAT — IGNORÉ PAR CONSIGNE DE PROJET : l'original poll
// Ac_GameGuard_Heartbeat toutes les 300 frames (quitte l'appli si != 1877) et relaie un
// jeton d'auth en attente (g_GuardAuthTokenPending -> Net_SendOp85) EN TÊTE du `default`
// MainTick. DÉLIBÉRÉMENT NON reproduit ici (CLAUDE.md : "ignore totalement l'anticheat").
//
// Rendu 3D détaillé (skinning, particules, shaders des effets ci-dessus) : TODO précis,
// hors périmètre — chaque hook du Host ci-dessous est le point d'intégration exact où le
// futur code de rendu/anim/gameplay doit être branché (EA d'origine documentée par hook).
//
// Autonomie : n'inclut que la STL + Game/GameState.h (compteurs/tableaux d'entités déjà
// modélisés par le socle partagé, cf. mission) pour éviter de dupliquer une 2e définition
// des tableaux joueurs/monstres/PNJ. Aucun couplage à Scene/SceneManager.h.
#pragma once
#include <functional>
#include "Game/GameState.h"

namespace ts2::game {

enum class InGameTickState : int {
    Setup          = 0, // case 0 @0x52C61F
    WaitFirstSpawn = 1, // case 1 @0x52C69B
    Failed         = 2, // case 2 @0x52C6DE (terminal)
    InitCamera     = 3, // case 3 @0x52C6EF (one-shot)
    MainTick       = 4, // default @0x52C81C (boucle principale, jamais quittée)
};

struct InGameTickFlowState {
    InGameTickState state = InGameTickState::Setup;
    int frameCounter = 0; // g_SceneMgr.frameCounter (dword_1676188 d'origine)
};

// Points d'intégration (effets de bord hors périmètre : réseau, UI, rendu, gameplay fin).
// Un hook nul = no-op (retourne false/0/-1 selon le type). EA d'origine en commentaire.
struct InGameTickFlowHost {
    // --- Setup (case 0) ---------------------------------------------------------------
    // sub_53F630(&unk_1685740) + sub_4C1110(0) 0x4C1110 (reset tooltip) +
    // UI_FocusEditBox(&g_UIEditBoxMgr,0) 0x50F4A0 + reset du scratch 150 dw de la scène.
    std::function<void()> ResetUiAndScratch;

    // --- WaitFirstSpawn (case 1) --------------------------------------------------------
    // UI_NoticeDlg_Open(2, StrTable005_Get(g_LangId,71), "") 0x5C0280 : timeout d'attente
    // du spawn du perso local.
    std::function<void()> ShowSpawnTimeoutNotice;

    // --- InitCamera (case 3) ------------------------------------------------------------
    // Cam_SetLookAt 0x69CCD0 + Camera_SetEyeTarget 0x403420 (g_GxdRenderer) +
    // g_CamFollowDist = Math_Dist3D(g_CameraPos, ...) 0x53FAA0, PUIS dword_1837E64=1 /
    // dword_1837E68=0 (flags de transition caméra, sémantique exacte au-delà de {1,0}
    // non déterminée). Position du joueur local fournie par g_World.Self() (x,y,z) —
    // l'appelant peut aussi la lire lui-même : passée ici en confort d'API.
    std::function<void(float selfX, float selfY, float selfZ)> InitCamera;

    // --- MainTick : réseau/latence (étape 1) --------------------------------------------
    // Net_SendPacket_Op13(client, g_LocalElement) 0x4B4570 : keepalive /300 frames.
    // Retour = succès d'émission (déclenche le message système StrTable005 id 70 si faux).
    std::function<bool()> SendKeepAlive;
    std::function<void()> AppendKeepAliveFailedMessage;
    // Crt_Strcmp sur 2 champs de requête en attente (g_PendingReqTargetName_Sub2/_Sub1)
    // non vides -> true.
    std::function<bool()> HasPendingTargetRequest;
    // Net_SendOp64 0x4B9B20 : poll de requête clan/faction en attente.
    std::function<void()> SendPendingTargetPoll;

    // --- MainTick : étape 2 (warp suppressé) ---------------------------------------------
    // dword_1675B00 (AutoPlayExternalState::warpSuppressed) : auto-clear si actif depuis
    // >10 s (g_GameTimeSec - flt_1675B04). Le host possède le timestamp de pose du latch ;
    // ce hook reçoit gameTimeSec et fait le test+clear en interne.
    std::function<void(float gameTimeSec)> TickWarpSuppressionTimeout;

    // --- MainTick : étapes 3-5 (chaque frame, inconditionnel) -----------------------------
    std::function<void(float dt)> AutoUsePotion;              // Game_AutoUsePotion 0x5C4800
    std::function<void(float dt)> UpdateMapObjectAnim;         // MapColl_UpdateObjectAnim(15.0,dt) 0x694A00
    std::function<void(float dt)> UpdateLocalPlayerAnim;       // Player_UpdateLocalAnim 0x5321D0
    std::function<void(int entityIndex, float dt)> UpdateEntityAnimFrame; // Char_UpdateAnimationFrame 0x571880 (idx=0 ici)
    std::function<void()> UpdateCameraCollision;                // Camera_UpdateCollision 0x538580

    // --- MainTick : étape 6 (joueurs distants, péremption 7,5 s) --------------------------
    std::function<void(int playerIndex, float dt)> DespawnStalePlayer; // sub_55D720 0x55D720

    // --- MainTick : étape 7 (tableau 88o, cf. écart de nommage documenté ci-dessus) -------
    std::function<void(int index, float dt)> TickGroundItemEffect; // Fx_MeleeSwingTick 0x5803A0

    // --- MainTick : étape 8 (monstres, péremption 7,5 s) ----------------------------------
    std::function<void(int monsterIndex, float dt)> UpdateMonster;             // Char_Update 0x581E10
    std::function<void(int monsterIndex)> RespawnMonsterAfterKnockback;        // 0x580550

    // --- MainTick : étape 9 (tableau 152o, cf. écart de nommage documenté ci-dessus) ------
    std::function<void(int npcIndex, float dt)> TickNpcEffect;   // Fx_GibUpdate 0x583CD0
    std::function<void(int npcIndex)> CleanupStaleNpcEffect;     // sub_583390 0x583390

    // --- MainTick : étape 10 (pool de projectiles d'attaque, PAS dans GameState.h) --------
    // Identification résolue (mission "aura/objets-de-monde", 2026-07-14) : malgré son nom,
    // ce N'EST PAS un pool d'auras buff/debuff — g_FxAuraCount (0x168722C) est le compteur
    // du pool SoA de PROJECTILES D'ATTAQUE dword_17D06F4 (stride 64 dw), alloué par
    // Fx_SpawnAttackProjectile(Alt) 0x582530/0x582A10, déjà catalogué en détail dans
    // Docs/TS2_FX_CATALOG.md. Volontairement non modélisé dans GameState.h (déjà documenté
    // ailleurs) ; câblage réel de ces hooks = mission séparée.
    std::function<int()> GetFxAuraCount;                          // g_FxAuraCount 0x168722C
    std::function<bool(int index)> IsFxAuraActive;
    std::function<void(int index, float dt)> UpdateHomingProjectile; // Fx_HomingProjectileUpdate 0x5862D0

    // --- MainTick : étape 11 (objets de zone/nœuds de ressource) ---------------------------
    // Identification résolue (mission "aura/objets-de-monde", 2026-07-14) : dword_1687230
    // est le compteur d'un pool DISTINCT du précédent (objets de zone : mines, portails, ...),
    // peuplé par Pkt_SpawnZoneObject (opcode 0x86). Désormais modélisé dans GameState.h via
    // ZoneObjectEntity / g_World.zoneObjects (N=500) ; ces hooks restent néanmoins non
    // branchés ici (câblage runtime = mission séparée, cf. Game/GameState.h et
    // Game/MiscManagers.cpp pour le détail).
    std::function<int()> GetWorldObjectCount;                     // dword_1687230
    std::function<bool(int index)> IsWorldObjectActive;
    // Fidèle : PAS d'indice passé (sub_584170 0x584170 ne reçoit que dt dans l'original).
    std::function<void(float dt)> TickWorldObject;

    // --- MainTick : étape 12, porte de gating -----------------------------------------
    // Renvoie l'état d'action du joueur local (g_SelfActionState[0]) pour tester
    // l'appartenance à {11,12,33,34,35,36,37} (états "occupé" : dialogue/coffre/etc.).
    std::function<int()> GetSelfActionState;
    std::function<bool()> IsExchangeWindowOpen;                  // UI_IsExchangeWindowOpen 0x5AC6E0
    // sub_53B9E0(this) 0x53B9E0 : éligibilité globale (cf. AutoPlayExternalState::
    // sceneTransitionBlocking — VALEUR INVERSÉE ici : ce hook doit renvoyer true quand
    // sub_53B9E0 renvoie VRAI, PAS le sens "blocking" d'AutoPlaySystem).
    std::function<bool()> CanAutoInteractNpc;
    std::function<bool()> IsInventoryDirty;                      // g_InvDirtyEnable == 1
    std::function<void()> AutoInteractNpcForPet;                 // Npc_AutoInteractForPet 0x53B5F0

    // --- MainTick : étape 12a (validation de cible auto) ----------------------------------
    // Valide/efface en interne dword_1675B24 et la cible verrouillée (dword_1675B28/2C)
    // selon son mode (1/2/3=joueur, 4=NPC à portée, 5=monstre, 7=objet à portée). Ce
    // module ne connaît PAS le mode ni les tableaux cibles (propriété d'un autre système,
    // ex. ItemPickupSystem/SkillCombat) : un seul hook opaque, appelé uniquement quand la
    // porte de gating l'autorise.
    std::function<void()> ValidateAutoTarget;

    // --- MainTick : étape 12b -------------------------------------------------------------
    std::function<void()> UpdateQuestMarkerTimer;                 // Quest_UpdateMarkerTimer 0x510D90

    // --- MainTick : étape 12c (toutes les 30 frames, indépendant du gate) -----------------
    // Combo_FindNearbyFollowup 0x501270 : renvoie l'id de cible de suivi ou -1.
    std::function<int()> FindComboFollowupTarget;
    std::function<bool()> IsMorphInProgress;                      // g_MorphInProgress 0x1675A88
    // Déclenche le morph de combo (dword_1675A8C=4, dword_1675A9C=followupId, reset des
    // champs de morph, rotation aléatoire, Net_SendPacket_Op20). Un seul hook opaque.
    std::function<void(int followupTargetId)> BeginComboMorph;

    // --- MainTick : étape 12d --------------------------------------------------------------
    std::function<bool()> IsCombatAllowedOnMap;                   // Combat_IsElementAllowedOnMap 0x55CBF0
    std::function<bool()> IsGm;                                   // g_GmAuthLevel != 0
    // Tick des 5 emplacements de pickup à proximité (<100 u) : efface + Net_SendOp106 pour
    // ceux en portée. Un seul hook opaque (propriété d'ItemPickupSystem).
    std::function<void()> TickNearbyPickupSlots;

    // --- MainTick : étape 12e ---------------------------------------------------------------
    std::function<void()> RotateTipText;                          // Tips002_RotateUpdate 0x4C1840
};

// Scene_InGameUpdate 0x52C600. Appeler 1x/frame (30 FPS, dt = 1/30 s côté original) tant
// que la scène active est InGame. IMPORTANT (confirmé dans le désassemblage) :
// cSceneMgr_Update appelle AutoPlay_Update(g_AutoPlayBot) JUSTE APRÈS Scene_InGameUpdate,
// à CHAQUE frame InGame, indépendamment du sous-état ci-dessus (même pendant Setup/
// WaitFirstSpawn/Failed/InitCamera). Ce fichier NE réimplémente PAS AutoPlay_Update
// (Game/AutoPlaySystem.h le fait déjà) : l'appelant doit conserver l'ordre
//   InGameTickFlow_Update(...); AutoPlaySystem::Update(dt);
// pour rester fidèle.
void InGameTickFlow_Update(InGameTickFlowState& state, const InGameTickFlowHost& host, float dt);

} // namespace ts2::game

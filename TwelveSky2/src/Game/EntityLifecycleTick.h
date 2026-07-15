// Game/EntityLifecycleTick.h — Système de cycle de vie des entités : despawn joueur
// distant périmé, tick monstre (aura/hit-window/knockback), respawn monstre après
// knockback, tick + péremption des effets NPC (gravité/collision sol), cleanup NPC périmé.
//
// Réécriture C++ des 5 fonctions suivantes (source unique de vérité = décompilation
// Hex-Rays via idaTs2, imagebase 0x400000) :
//   sub_55D720   (nommée PlayerArray_SlotDestruct dans l'IDB) — étape 6 de
//                Game/InGameTickFlow.h (host.DespawnStalePlayer).
//   Char_Update              0x581E10 — étape 8 (host.UpdateMonster).
//   sub_580550   (nommée Char_RespawnAfterKnockback dans l'IDB) — étape 8, branche
//                "périmé" (host.RespawnMonsterAfterKnockback).
//   Fx_GibUpdate              0x583CD0 — étape 9 (host.TickNpcEffect).
//   sub_583390   — étape 9, branche "périmé" (host.CleanupStaleNpcEffect).
//
// Tous les 5 sont appelés depuis Scene_InGameUpdate 0x52C600 (default/MainTick), pour
// CHAQUE entité active dont g_GameTimeSec - timestamp <= 7.5 (tick) ou > 7.5 (fonction
// "périmé"). Cf. Game/InGameTickFlow.h pour l'orchestration complète et Game/GameState.h
// pour PlayerEntity/MonsterEntity/NpcEntity (structures consommées ici, PAS modifiées).
//
// === ÉCART DE NOMMAGE CONFIRMÉ PAR DÉCOMPILATION (à signaler) ===
// sub_55D720, sub_580550 et sub_583390 décompilent TOUS LES TROIS en une seule ligne
// identique : `*(_DWORD*)this = 0; return this;` — c'est-à-dire qu'ils désactivent
// (mettent active=false) le slot d'entité qu'on leur passe, RIEN D'AUTRE. En particulier,
// sub_580550 (renommé "Char_RespawnAfterKnockback" dans l'IDB, commentaire de tête
// "[GXD] Réinitialise le personnage après expiration (~3 s) du knockback") a EXACTEMENT
// le même comportement observable que les deux fonctions "despawn/cleanup" — malgré son
// nom qui suggère une réanimation. Le commentaire de Game/InGameTickFlow.h ("entité jugée
// périmée -> réanimation, PAS despawn") reflète l'intention du nommage IDA, pas le
// comportement réel de la fonction : au niveau de CE code, les trois fonctions sont
// interchangeables (désactivation de slot). Reproduit ici fidèlement : les trois wrappers
// ci-dessous appellent la même primitive interne DeactivateSlot(). Un futur système de
// respawn monstre "réel" (relance d'un timer de spawn, notification serveur, etc.) est
// HORS PÉRIMÈTRE de cette mission — non trouvé dans le corps de sub_580550 lui-même
// (peut-être géré ailleurs, p.ex. côté serveur ou dans le spawn handler réseau).
//
// === Char_Update / Fx_GibUpdate — champs additionnels hors GameState.h ===
// MonsterEntity/NpcEntity (Game/GameState.h) sont des modèles PROPRES volontairement
// allégés (pas byte-exact) qui ne portent PAS les champs de tick par-frame utilisés par
// ces deux fonctions (timers d'aura, état de fenêtre de coup, vitesse/offset de chute
// physique...). À l'image d'ActionStateMachine.h (ActionFsm, en sus de GameState) et
// EntityManager.h (GroundPickupSlot, "absente de GameState"), ce module porte ces champs
// dans des structures d'EXTENSION parallèles indexées comme g_World.monsters/g_World.npcs
// (MonsterTickExt / NpcTickExt, cf. ci-dessous) plutôt que d'étendre GameState.h (fichier
// socle partagé, hors périmètre d'édition de cette mission). ATTENTION INTÉGRATION : ces
// vecteurs d'extension doivent être réinitialisés (Reset*TickExt) quand un slot change
// d'identité réseau (spawn sur un slot recyclé) — ce module ne le fait PAS lui-même
// (il n'a pas visibilité sur les points de spawn, cf. Game/EntityManager.h). CÂBLÉ (audit
// 2026-07-14) : Game/EntityManager.cpp::OnSpawnMonster/OnSpawnNpc appellent désormais
// ResetMonsterTickExt/ResetNpcTickExt sur la branche "nouveau" (slot fraîchement pris via
// FindOrAdd — potentiellement recyclé), PAS sur la branche "rafraîchissement" (même
// entité, l'extension doit survivre).
//
// === Sous-systèmes hors périmètre, exposés en callbacks opaques (EntityLifecycleTickHost) ===
// Comme Game/ActionStateMachine.h (IAnimFrameOracle) et Game/InGameTickFlow.h
// (InGameTickFlowHost), les dépendances suivantes du binaire d'origine sortent du
// périmètre "cycle de vie d'entité" de cette mission et sont exposées en hooks :
//   - ModelObj_GetSubObjectCount 0x4D7080 (durée d'anim de swap de modèle, donnée asset)
//   - Anim_IsFrameInHitListA/B   0x559F80 / 0x55A000 (table figée frame-de-contact, asset)
//   - Combat_SendMeleeHit1/2     0x5823E0 / 0x582480 (émission réseau côté "monstre attaque")
//   - Fx_SpawnAttackProjectile(Alt) 0x582530 / 0x582A10 (FX de projectile)
//   - Char_MotionTick_* (0x582D40..0x5832E0, 9 handlers), dispatchés par le switch terminal
//     de Char_Update — FSM d'animation monstre, HORS PÉRIMÈTRE (rendu/anim 3D), regroupée
//     en UN SEUL hook opaque DispatchMotionTick (même traitement que BeginComboMorph/
//     ValidateAutoTarget dans InGameTickFlowHost).
//   - le bypass *(_DWORD*)(*((_DWORD*)this+24)+244)==113 || ...+236)==18 (0x571FF2 iso
//     dans Char_Update ~0x581FF2->0x582060) : la nature exacte du pointeur this+24 est
//     AMBIGUË dans le désassemblage (utilisé À LA FOIS comme int animId direct pour
//     Anim_IsFrameInHitListA/B ET comme pointeur déréférencé pour ce test) — non résolue
//     par cette mission, exposée en callback IsAttackTargetBypassActive() (défaut : false,
//     c.-à-d. on fait toujours le scan de cible).
//   - MapColl_GetGroundHeight 0x697130 (collision terrain)
//   - Snd3D_PlayPositional 0x4DA450 (son d'impact au premier tick de retombée)
//
// Namespace ts2::game. Fonctions libres opérant sur game::g_World (pas de singleton
// implicite : le monde et l'extension sont passés/accessibles explicitement).
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "Game/GameState.h"

namespace ts2::game {

// ===========================================================================
// Extension "monstre" — champs de tick par-frame absents de MonsterEntity.
// Offsets d'origine (Char_Update 0x581E10, index dword *(this+N) => octet N*4,
// stride réel dword_1766F74 = 0x118 = 280 o = 70 dwords) donnés en commentaire.
// ===========================================================================
struct MonsterTickExt {
    // --- Dispatch FSM (this+6, octet 24) : lu par Char_Update, écrit par les handlers
    // Char_MotionTick_* eux-mêmes (hors périmètre) via host.DispatchMotionTick. Ce
    // module ne fait QUE le lire pour décider quoi appeler ensuite (aucune transition
    // d'état gérée ici). Valeurs observées dans le switch d'origine (0x5822D3) :
    // 0=ToIdle,1=Loop,3=MoveA,4=MoveB,5=AttackA,7=AttackB,8=Hit,0xC=Knockback,0x13=Death.
    int32_t motionState = 0;

    // --- Timers de swap de modèle (3 emplacements indépendants, motif identique) :
    // this+64/65 (256/260), this+66/67 (264/268), this+68/69 (272/276). active=bool,
    // frame s'incrémente de dt*30 jusqu'à la durée (asset, host.GetAuraSwapDuration)
    // puis active repasse à false.
    bool  auraActive[3] = {false, false, false};
    float auraFrame[3]  = {0.0f, 0.0f, 0.0f};

    // --- Bloc de détection de fenêtre de coup (états 5=AttackA / 7=AttackB uniquement,
    // 0x581F34..0x582145). Champs consultés :
    int32_t attackWindupMode = 0;  // this+27 (108) : ==1 -> fenêtre de coup potentiellement active
    bool    hitLatched        = false; // this+30 (120) : latch "déjà dans la fenêtre" (anti double-trigger)
    int32_t hitActionKind     = 0; // this+28 (112) : 1=coup mêlée (dispatch this+29), 2=projectile
    int32_t hitActionSub      = 0; // this+29 (116) : valide si hitActionKind==1 : 1=SendMeleeHit1, 2=SendMeleeHit2
    float   animFrame         = 0.0f; // this+7  (28)  : frame courante, passée aux oracles de hit-list
    int32_t weaponOrSkillAnimId = 0;  // this+24 (96)  : clé passée à Anim_IsFrameInHitListA/B (cf. ambiguïté ci-dessus)
    EntityId attackTargetId;          // this+17/18 (68/72) : id réseau de la cible courante (peuplé par
                                       // l'IA de combat, HORS PÉRIMÈTRE — ce module se contente de le lire
                                       // pour revalider l'existence de la cible avant d'émettre le coup)

    // --- Chute/knockback (0x582145..0x5822B6), active tant que fallActive, plafonnée à
    // fallLandCounter < fallLandThreshold. Vélocité + offset de retombée SÉPARÉS de la
    // position monde réelle du monstre : la vraie MonsterEntity.x/y/z du binaire d'origine
    // vit à record+32/36/40 (confirmé par Fx_SpawnAttackProjectile @0x5825b5-0x582601, qui
    // lit *(this+32)/*(this+36)/*(this+40) comme origine du projectile), PAS à record+240.
    // this+240..251 (this+60/61/62 ci-dessous) est un champ ENTIÈREMENT DISTINCT, jamais
    // lu ni écrit par Fx_SpawnAttackProjectile ni par le rendu -- CORRECTION d'un écart de
    // documentation d'une vague précédente qui affirmait par erreur "this+240 ==
    // MonsterEntity.x/y/z". En réalité fallOffX/Y/Z se comporte comme une position DE VOL
    // ABSOLUE (probablement amorcée à mon.x/y/z par Char_MotionTick_Knockback au début du
    // knockback, HORS PÉRIMÈTRE, jamais observé ici), PAS un petit delta relatif : la sonde
    // de sol et le son d'atterrissage utilisent fallOffX/Y/Z tels quels comme coordonnées
    // ABSOLUES (confirmé par désassemblage brut 0x582206..0x58223e, PAS seulement le
    // pseudocode Hex-Rays) -- cf. bug corrigé dans EntityLifecycleTick.cpp::UpdateMonster
    // (audit étapes 5-8, 2026-07-14) : la version précédente de ce fichier passait par
    // erreur mon.x/mon.z (jamais mis à jour ici) à la sonde au lieu de fallOffX/fallOffZ.
    bool    fallActive        = false; // this+53 (212)
    int32_t fallLandCounter   = 0;     // this+58 (232) : incrémenté à chaque atterrissage détecté
    int32_t fallLandThreshold = 1;     // this+57 (228) : condition d'arrêt (this+58 < this+57)
    float   fallVelX = 0.0f, fallVelY = 0.0f, fallVelZ = 0.0f; // this+54/55/56 (216/220/224) — fallVelY accumule -300*dt (gravité)
    float   fallOffX = 0.0f, fallOffY = 0.0f, fallOffZ = 0.0f; // this+60/61/62 (240/244/248) — offset intégré
    float   fallGroundClearance = 0.0f; // this+59 (236) : offset soustrait à la hauteur de sol au clamp
};

// ===========================================================================
// Extension "NPC/effet" — champs de tick physique absents de NpcEntity.
// Offsets d'origine (Fx_GibUpdate 0x583CD0, stride réel dword_17AB534 = 0x98 = 152 o
// = 38 dwords) donnés en commentaire.
// ===========================================================================
struct NpcTickExt {
    bool  gibActive = false;             // this+21 (84) : arme la physique de chute
    float velX = 0.0f, velY = 0.0f, velZ = 0.0f; // this+28/29/30 (112/116/120) — velY accumule -300*dt
    float posX = 0.0f, posY = 0.0f, posZ = 0.0f; // this+32/33/34 (128/132/136) — position intégrée (PAS NpcEntity.*, ce dernier n'a pas de x/y/z)
    float extraRate  = 0.0f;             // this+31 (124) : taux additionnel (rotation/scale probable, non identifié)
    float extraValue = 0.0f;             // this+36 (144) : accumulateur intégré par extraRate*dt
};

// Stockage d'extension, indexé comme g_World.monsters / g_World.npcs. Grandit paresseusement
// (EnsureMonsterExtCapacity/EnsureNpcExtCapacity, appelés en tête de UpdateMonster/TickNpcEffect).
inline std::vector<MonsterTickExt> g_MonsterTickExt;
inline std::vector<NpcTickExt>     g_NpcTickExt;

// Réinitialise l'extension d'un slot (à appeler par l'agent de consolidation depuis
// EntityManager::OnSpawnMonster/OnSpawnNpc quand un nouveau monstre/NPC prend possession
// d'un index de tableau — évite qu'un vieux fallOffset/vélocité ne "fuite" sur la nouvelle
// entité). No-op si l'index est hors bornes (agrandit d'abord si besoin).
void ResetMonsterTickExt(int monsterIndex);
void ResetNpcTickExt(int npcIndex);

// ===========================================================================
// Callbacks opaques vers les sous-systèmes hors périmètre (cf. commentaire de tête).
// Un hook nul = no-op / valeur par défaut conservatrice (voir chaque site d'appel dans
// le .cpp). Même convention que Game/InGameTickFlow.h::InGameTickFlowHost.
// ===========================================================================
struct EntityLifecycleTickHost {
    // --- UpdateMonster : timers de swap de modèle -----------------------------------
    // ModelObj_GetSubObjectCount 0x4D7080. auraSlot in {0,1,2} <-> {unk_B63174,
    // unk_B63208, unk_B5A180} (3 modèles distincts dans le binaire d'origine).
    std::function<float(int auraSlot)> GetAuraSwapDuration;

    // --- UpdateMonster : fenêtre de coup ----------------------------------------------
    std::function<bool(int32_t animOrWeaponId, float frame)> IsFrameInHitListA; // 0x559F80 (état 5=AttackA)
    std::function<bool(int32_t animOrWeaponId, float frame)> IsFrameInHitListB; // 0x55A000 (état 7=AttackB)
    // Cf. écart documenté en tête de fichier (nature ambiguë de this+24).
    std::function<bool()> IsAttackTargetBypassActive;
    std::function<void(int monsterIndex)> SendMeleeHit1;            // Combat_SendMeleeHit1 0x5823E0
    std::function<void(int monsterIndex)> SendMeleeHit2;            // Combat_SendMeleeHit2 0x582480
    std::function<void(int monsterIndex)> SpawnAttackProjectile;    // Fx_SpawnAttackProjectile 0x582530 (état 5, hitActionKind==2)
    std::function<void(int monsterIndex)> SpawnAttackProjectileAlt; // Fx_SpawnAttackProjectileAlt 0x582A10 (état 7)

    // --- UpdateMonster : dispatch FSM finale (switch this+6) -------------------------
    std::function<void(int monsterIndex, float dt)> DispatchMotionTick; // Char_MotionTick_* 0x582D40..0x5832E0

    // --- UpdateMonster + TickNpcEffect : terrain/son --------------------------------
    // MapColl_GetGroundHeight 0x697130. Retourne true si une hauteur de sol a été
    // trouvée sous (x,z) (outGroundY rempli) ; probeY = hauteur de sonde (pos.y+20
    // côté original, cf. corps des deux fonctions). IMPORTANT (re-vérifié par
    // désassemblage brut, pas seulement le pseudocode, audit étapes 5-8 2026-07-14) :
    // x/z proviennent des champs de tick de l'EXTENSION (ext.fallOffX/Z côté
    // UpdateMonster, ext.posX/Z côté TickNpcEffect), PAS de MonsterEntity.x/z /
    // NpcEntity.x/z ; probeY utilise la valeur PRÉ-update de ce tick (avant intégration
    // gravité/vélocité), PAS la valeur déjà intégrée -- cf. corrections appliquées dans
    // EntityLifecycleTick.cpp.
    std::function<bool(float x, float z, float probeY, float& outGroundY)> GetGroundHeight;
    // Snd3D_PlayPositional 0x4DA450 : joué uniquement au 1er atterrissage détecté
    // (fallLandCounter passant de 0 à 1) côté UpdateMonster. Fx_GibUpdate n'appelle
    // AUCUN son (vérifié dans le pseudocode — asymétrie fidèle à l'original).
    std::function<void(float x, float y, float z)> PlayLandingSound;
};

// ===========================================================================
// Les 5 fonctions de la mission. Signatures alignées sur les hooks correspondants de
// Game/InGameTickFlow.h::InGameTickFlowHost (DespawnStalePlayer, UpdateMonster,
// RespawnMonsterAfterKnockback, TickNpcEffect, CleanupStaleNpcEffect) pour un branchement
// direct par l'agent de consolidation, ex. :
//   host.DespawnStalePlayer = [](int idx, float) { DespawnStalePlayer(g_World, idx); };
// ===========================================================================

// sub_55D720 (PlayerArray_SlotDestruct). Désactive g_World.players[playerIndex] (active
// = false). Ne touche à AUCUN autre champ (id/timestamp/body conservés tels quels,
// fidèle à `*this = 0` qui ne clear QUE le premier dword) — un futur FindOrAddPlayer
// écrasera le slot proprement à la prochaine réutilisation (cf. GameState.cpp::FindOrAdd).
// No-op si playerIndex hors bornes.
void DespawnStalePlayer(GameWorld& world, int playerIndex);

// sub_580550 (Char_RespawnAfterKnockback). Cf. écart de nommage en tête de fichier :
// comportement réel identique à DespawnStalePlayer/CleanupStaleNpcEntity (désactive le
// slot). No-op si monsterIndex hors bornes.
void RespawnMonsterAfterKnockback(GameWorld& world, int monsterIndex);

// sub_583390. Désactive g_World.npcs[npcIndex]. No-op si npcIndex hors bornes.
void CleanupStaleNpcEntity(GameWorld& world, int npcIndex);

// Char_Update 0x581E10 (tick monstre, appelé quand g_GameTimeSec - timestamp <= 7.5s).
// Opère sur world.monsters[monsterIndex] + g_MonsterTickExt[monsterIndex] (agrandi au
// besoin). No-op si !world.monsters[monsterIndex].active ou index hors bornes (fidèle au
// garde `if (*(_DWORD*)this)` de tête de fonction, doublé par le filtre actif déjà fait
// par l'appelant InGameTickFlow_Update — revérifié ici par robustesse défensive).
void UpdateMonster(GameWorld& world, int monsterIndex, float dt, const EntityLifecycleTickHost& host);

// Fx_GibUpdate 0x583CD0 (tick NPC/effet, appelé quand g_GameTimeSec - timestamp <= 7.5s).
// Opère sur world.npcs[npcIndex] + g_NpcTickExt[npcIndex] (agrandi au besoin). Physique de
// chute (gravité -300 u/s², intégration semi-implicite, snap au sol) — strictement no-op
// tant que ext.gibActive==false (fidèle au double garde `this[0]` + `this+21` d'origine).
void TickNpcEffect(GameWorld& world, int npcIndex, float dt, const EntityLifecycleTickHost& host);

} // namespace ts2::game

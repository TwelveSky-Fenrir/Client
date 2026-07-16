// Game/PlayerAnimCursorTick.h — avance du CURSEUR d'animation d'UN joueur (entity+248 =
// CharAnimState::animFrame), portage fidèle de l'idiome UNIVERSEL des ~81 handlers du switch
// terminal de Char_UpdateAnimationFrame 0x571880 (@0x5727BF). Front F_PLAYERANIM (2026-07-17).
//
// POURQUOI CE FICHIER. Les joueurs (soi + distants) étaient dessinés avec animState=0 figé
// (idle) + une horloge GLOBALE (Gfx/MotionCache.h::SampleByGameTime) : tous en phase, jamais
// d'autre clip qu'idle. Cause racine : le curseur réel entity+248 n'était JAMAIS avancé côté
// C++. Le switch terminal 0x5727BF (qui contient l'avance) n'est pas dispatché
// (Scene/SceneManager.cpp:1140, 10e argument `stateHandler` = nullptr), et le routeur PARTIEL
// Game/AnimationTick.h §7 (Char_DispatchStateTick) ne couvre que 6/81 cas (cast/guard) — PAS
// idle(0)/move(1), les états DOMINANTS. Ce module fournit l'AVANCE UNIVERSELLE du curseur
// (idle/move et tout état à clip bouclé), à appeler 1x/frame par joueur en phase UPDATE (miroir
// de Char_UpdateAnimationFrame, appelé par Scene_InGameUpdate 0x52C600 @0x52c96d soi /
// @0x52c9fd distants).
//
// VÉRITÉ TERRAIN (IDA idaTs2, re-décompilée cette session) — l'idiome est IDENTIQUE dans tous
// les handlers du switch, prouvé sur les DEUX états dominants :
//   Char_AnimTick_5746E0 0x5746E0 (case 0 = idle) @0x5747a8 : *(float*)(this+248) = a3*30.0 + *(float*)(this+248)
//   Char_TickMoveState   0x574830 (case 1 = move) @0x5748f8 : *(float*)(this+248) = a3*30.0 + *(float*)(this+248)
//                                                @0x574911/@0x574922 : if((this+248) >= (double)v8) (this+248) -= (double)v8
//   v8 = PcModel_ResolveSlotAndApply 0x4E5A00  (= PcModel_ResolveEquipSlot 0x4E46A0 puis
//        Motion_GetFrameCount 0x4D7830)  = NOMBRE DE FRAMES du clip courant.
// -> avance = frame += dt*30 ; wrap = SOUSTRACTION (JAMAIS un modulo, cf. Gfx/MotionCache.h
//    SampleByCursor : l'état Mort gèle le curseur à frameCount-1, un modulo le ressusciterait).
//
// SÉPARATION D'AVEC LA FSM (fidèle). Ce module n'écrit QUE le curseur (animFrame). Il ne touche
// PAS l'état (anim.state = entity+244), écrit ailleurs : réseau (Game/EntityManager.cpp:390 =
// body+220, Pkt_SpawnCharacter 0x4646C0 -> Char_SetActionAnimParams 0x570E70), input
// (~90 Player_Queue*/Net_Queue* -> g_SelfActionState = 1) et transitions FSM. La transition
// idle->move interne (Char_AnimTick_5746E0 @0x5747db : fin d'idle -> state=1, cursor=0) est une
// ÉCRITURE d'ÉTAT, hors du périmètre de ce curseur pur : reproduite par la FSM, pas ici. Le
// clip suit l'état (PcModel_ResolveEquipSlot 0x4E46A0, base + 156*état) ; ce module fait juste
// tourner le curseur DANS le clip courant, et le clip change quand l'état change (réseau/input).
#pragma once
#include "Game/GameState.h" // CharAnimState (state = entity+244, animFrame = entity+248)

namespace ts2::game {

// Avance le curseur d'animation d'un joueur d'UNE frame. `dt` = a3 d'origine (1/30 s @30 FPS).
// `frameCount` = nombre de frames du clip COURANT (fourni par l'appelant via l'oracle de motion
// du joueur — ts2::WorldPlayerMotionFrameCount = Motion_GetFrameCount 0x4D7830, MÊME source que
// la palette dessinée : sinon wrap et échantillonnage divergeraient). Ne modifie QUE
// anim.animFrame (entity+248) — jamais anim.state (cf. bandeau : séparation FSM).
//
// DÉGRADATION (aucune invention). frameCount <= 0 (slot non résolu / fichier absent) -> le
// curseur AVANCE mais ne reboucle jamais (durée inconnue : on ne fabrique aucune borne), MÊME
// politique que game::Monster_DispatchMotionTick (Game/AnimationTick.h §5). Cette garde
// frameCount>0 protège aussi contre une boucle infinie du wrap.
void Player_AdvanceAnimCursor(CharAnimState& anim, float dt, int frameCount);

// GARDE DE NON-RÉGRESSION DE CÂBLAGE (PAS un comportement du binaire) — pendant EXACT de
// game::Monster_MotionTickIsWired() / ZoneNpc_AnimTickIsWired() / Char_StateTickIsWired().
// Vrai dès qu'un Player_AdvanceAnimCursor a réellement tourné au moins une fois. Permet à
// Gfx/PlayerPaperdoll (via Scene/WorldRenderer, drapeau DrawableEntity::hasAnimCursor) de ne
// consommer le curseur par-entité (MotionCache::SampleByCursor) QUE s'il est réellement
// alimenté ; sinon on conserve le repli horloge globale (SampleByGameTime, clip correct mais en
// phase), au lieu de FIGER TOUS LES JOUEURS À LA FRAME 0 — le blocker recon de ce front. À
// retirer le jour où le câblage UPDATE (Scene/SceneManager.cpp) est verrouillé par un test.
bool Player_AnimCursorTickIsWired();

} // namespace ts2::game

// Gfx/CameraThirdPersonBridge.h — pont « caméra 3e personne » entre le tick InGame et les
// deux fonctions gameplay DÉJÀ ÉCRITES mais jamais appelées faute de gfx::Camera MUTABLE dans
// SceneManager::Update(double dt) (seul Render() en reçoit une const, cf. tête de
// Scene/SceneManager.cpp) :
//   - Game/CameraWarpTick.h::InGame_InitCamera   <- Scene_InGameUpdate case 3, EA 0x52C6EF
//     (cadrage caméra ONE-SHOT à l'entrée en InGame : Cam_SetLookAt(eye=self+(50,60,50),
//     target=self+(0,10,0)) + armement de CameraFollowState).
//   - Game/AnimationTick.h::Camera_UpdateCollision <- EA 0x538580 (suivi de cible + collision
//     terrain/objets, CHAQUE FRAME InGame : reprojette le bras oeil-cible de la frame
//     précédente autour de la nouvelle position du joueur local, puis corrige par sweep de
//     collision si un oracle est fourni).
//
// Module FEUILLE côté rendu/gameplay : ne fait qu'ORCHESTRER ces deux fonctions déjà écrites
// et fonctionnelles (AUCUNE logique nouvelle inventée ici, cf. leurs commentaires respectifs
// pour la fidélité EA complète). N'inclut PAS Scene/SceneManager.h : règle de coordination de
// la mission ("PONT CAMÉRA 3e PERSONNE") — ce fichier n'édite ni ne dépend de
// Scene/SceneManager.*/App/App.*, un agent de consolidation dédié câble l'appel ci-dessous
// depuis SceneManager::Update APRÈS extension de sa signature.
//
// ============================================================================================
// SIGNATURE POUR L'AGENT DE CONSOLIDATION (câblage réel dans SceneManager, HORS PÉRIMÈTRE de
// cette mission) :
//
//   void ts2::gfx::TickThirdPersonCamera(gfx::Camera& camera, const game::GameWorld& world,
//                                         float dt, bool justEnteredInGame);
//
// Étapes de câblage attendues côté SceneManager (état confirmé au 2026-07-14, cf. commentaire
// existant au site `host.UpdateCameraCollision`/`host.InitCamera` dans SceneManager.cpp,
// TS2_INGAME_TODO_ONCE "Camera_UpdateCollision non branche") :
//   1. La caméra existe déjà : `App::camera_` (App/App.h/.cpp), possédée par App, passée à
//      SceneManager::Render() en gfx::Camera CONST. SceneManager::Update(double dt), lui, NE
//      REÇOIT AUCUNE gfx::Camera — ni const ni mutable. Étendre SceneManager::Update (et son
//      appelant dans App.cpp) pour lui faire recevoir `App::camera_` en MUTABLE (gfx::Camera&),
//      la MÊME instance que celle passée à Render() (sinon le suivi de cible calculé ici n'a
//      aucun effet visible côté rendu).
//   2. Édition requise : SceneManager.h (signature Update), SceneManager.cpp (case
//      Scene::InGame), App.h/App.cpp (site d'appel Update) — TOUS hors périmètre de cette
//      mission (règle « ce module N'ÉDITE PAS Scene/SceneManager.*/App/App.* »).
//   3. Détecter la transition d'entrée en InGame avec le MÊME flag que celui qui gate déjà les
//      hooks `TS2_INGAME_TODO_ONCE` (cf. SceneManager.cpp, case Scene::InGame) : passer
//      `justEnteredInGame=true` UNIQUEMENT sur cette frame-là, `false` toutes les suivantes.
//   4. Appeler, APRÈS la mise à jour de la position du joueur local pour cette frame (le
//      suivi de cible lit `game::g_World.Self()`/`players[0]` — doit déjà être à jour pour
//      cette frame) :
//        ts2::gfx::TickThirdPersonCamera(camera /* App::camera_, reçue en mutable */,
//                                         game::g_World, static_cast<float>(dt),
//                                         justEnteredInGame);
// ============================================================================================
//
// Free-look / oracle de collision : AUCUN câblage réel disponible côté ClientSource à ce jour
// (pas de système UI de bascule free-look, pas de World/WorldMap de collision de caméra) —
// TickThirdPersonCamera appelle donc Camera_UpdateCollision avec freeLookActive=false,
// camMode=0, collision=nullptr (dégradation propre déjà documentée dans Game/AnimationTick.h :
// la caméra suit alors le bras précédent SANS correction de collision). Si de futurs systèmes
// apparaissent, étendre TickThirdPersonCamera avec des paramètres optionnels plutôt que de
// dupliquer cette fonction.
#pragma once

#include "Gfx/Camera.h"
#include "Game/GameState.h"

namespace ts2::gfx {

// Tick caméra 3e personne complet pour UNE frame InGame. `world` = game::g_World (lecture
// seule ; `world.Self()`/`world.players[0]` fournit la position du joueur local — si
// `world.players` est vide, InGame_InitCamera et Camera_UpdateCollision cadrent tous deux sur
// l'origine (0,0,0), dégradation propre fidèle au cas "self non encore spawné").
//
//   - Si `justEnteredInGame` est vrai : appelle D'ABORD game::InGame_InitCamera(camera, ...)
//     avec la position du joueur local — cadrage d'entrée, EXACTEMENT une fois par entrée en
//     InGame (fidèle à Scene_InGameUpdate case 3). L'état de cadrage (game::CameraFollowState,
//     dword_1837E64/68 d'origine) est possédé PAR CE MODULE (une seule caméra 3e personne
//     active à la fois côté client — pas de paramètre supplémentaire dans la signature
//     imposée par la mission).
//   - Dans TOUS les cas : appelle ENSUITE game::Camera_UpdateCollision(camera, world, ...)
//     pour le suivi de cible + collision de la frame courante (0x538580).
//
// `dt` est réservé pour un futur branchement d'orbite/zoom pilotés par vélocité
// (gfx::Camera::Update(dt), mouvement continu souris/clavier maintenu) : NI InGame_InitCamera
// NI Camera_UpdateCollision ne consomment `dt` dans le binaire d'origine (les deux opèrent sur
// des positions instantanées, pas des intégrations temporelles) — non invoqué ici pour rester
// strictement fidèle au périmètre de cette mission (orchestrer les deux fonctions existantes,
// sans inventer de logique supplémentaire). Un futur système d'input caméra (mission séparée)
// pourra appeler `camera.Update(dt)` explicitement AVANT TickThirdPersonCamera s'il pose des
// vélocités d'orbite continues via Camera::SetOrbitVelocity/SetZoomVelocity.
void TickThirdPersonCamera(Camera& camera, const game::GameWorld& world,
                            float dt, bool justEnteredInGame);

} // namespace ts2::gfx

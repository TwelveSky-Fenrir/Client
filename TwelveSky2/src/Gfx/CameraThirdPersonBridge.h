// Gfx/CameraThirdPersonBridge.h — pont « caméra 3e personne » entre le tick InGame et les
// deux fonctions gameplay DÉJÀ ÉCRITES :
//   - Game/CameraWarpTick.h::InGame_InitCamera   <- Scene_InGameUpdate case 3, EA 0x52C6EF
//     (cadrage caméra ONE-SHOT à l'entrée en InGame : Cam_SetLookAt(eye=self+(50,60,50),
//     target=self+(0,10,0)) + armement de CameraFollowState).
//   - Game/AnimationTick.h::Camera_UpdateCollision <- EA 0x538580 (suivi de cible + collision
//     terrain/objets, CHAQUE FRAME InGame : reprojette le bras oeil-cible de la frame
//     précédente autour de la nouvelle position du joueur local, puis corrige par sweep de
//     collision quand un oracle est fourni).
//
// Module FEUILLE côté rendu/gameplay : n'invente AUCUNE logique — il orchestre ces deux
// fonctions et, depuis WG-02 (Passe 4/W7), construit l'ORACLE DE COLLISION RÉEL à partir de
// world::WorldAssets (World/WorldIntegration.h) au lieu de passer nullptr.
//
// CÂBLAGE (état 2026-07-16) : l'appel EST déjà branché dans Scene/SceneManager.cpp (au tick
// InGame, cf. `gfx::TickThirdPersonCamera(...)`), la caméra passée est bien App::camera_ reçue
// en mutable, et `justEnteredInGame` est déjà détecté par SceneManager. Le SEUL ajout de
// câblage requis par WG-02 est le nouveau 5e argument `worldAssets` (cf. la signature ci-
// dessous et le rapport de front) — sans quoi l'oracle reste nul et la caméra traverse le décor.
#pragma once

#include "Gfx/Camera.h"
#include "Game/GameState.h"

// Fournisseur des requêtes de collision de zone (sweep terrain .WG, point bloqué Main/WJ, sol
// Main). Défini dans World/WorldIntegration.h ; forward-declaré ici (précédent :
// Gfx/WorldGeometryRenderer.h:251) pour garder cet en-tête léger.
namespace ts2::world { class WorldAssets; }

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
//     active à la fois côté client).
//   - Dans TOUS les cas : appelle ENSUITE game::Camera_UpdateCollision(camera, world, ...)
//     pour le suivi de cible + collision de la frame courante (0x538580).
//
// `worldAssets` (WG-02) : si non nul ET la zone a chargé sa collision, un oracle réel
// (WorldCameraCollision : game::ICameraCollisionQueries) branche les 4 requêtes du binaire
// (Terrain_SweepSphereSegment 0x69a1f0 sur le .WG ; World_IsPointBlocked 0x540da0 sur Main+WJ ;
// MapColl_GetGroundHeight @0x5388f4 sur Main ; MapColl_LineOfSightObjects 0x696fc0 -> false,
// non porté). `worldAssets==nullptr` -> oracle nul = comportement fidèle « zone non chargée »
// (la caméra suit le bras précédent SANS correction de collision).
//
// `dt` est réservé pour un futur branchement d'orbite/zoom pilotés par vélocité : NI
// InGame_InitCamera NI Camera_UpdateCollision ne consomment `dt` dans le binaire d'origine —
// non invoqué ici pour rester strictement fidèle.
//
// CÂBLAGE ORCHESTRATEUR (WG-02, hors de mes fichiers) : Scene/SceneManager.cpp:1254 doit passer
// le 5e argument `worldAssets_.get()` (membre déjà présent de SceneManager, cf. SceneManager.cpp
// :268) — un seul argument à ajouter. Sans lui, la compilation échoue (paramètre requis, pas de
// défaut : garantie anti-code-mort).
void TickThirdPersonCamera(Camera& camera, const game::GameWorld& world,
                            float dt, bool justEnteredInGame,
                            const world::WorldAssets* worldAssets);

} // namespace ts2::gfx

// Gfx/CameraThirdPersonBridge.h — "third-person camera" bridge between the InGame tick and the
// two gameplay functions ALREADY WRITTEN:
//   - Game/CameraWarpTick.h::InGame_InitCamera   <- Scene_InGameUpdate case 3, EA 0x52C6EF
//     (ONE-SHOT camera framing on InGame entry: Cam_SetLookAt(eye=self+(50,60,50),
//     target=self+(0,10,0)) + arms CameraFollowState).
//   - Game/AnimationTick.h::Camera_UpdateCollision <- EA 0x538580 (target follow + terrain/object
//     collision, EVERY InGame FRAME: reprojects the previous frame's eye-target arm around the
//     new local player position, then corrects via collision sweep when an oracle is provided).
//
// LEAF module on the render/gameplay side: invents NO logic — it orchestrates these two
// functions and, since WG-02 (Pass 4/W7), builds the REAL COLLISION ORACLE from
// world::WorldAssets (World/WorldIntegration.h) instead of passing nullptr.
//
// WIRING (state 2026-07-16): the call IS already wired in Scene/SceneManager.cpp (in the
// InGame tick, cf. `gfx::TickThirdPersonCamera(...)`), the camera passed is indeed App::camera_
// received as mutable, and `justEnteredInGame` is already detected by SceneManager. The ONLY
// wiring left for WG-02 is the new 5th argument `worldAssets` (cf. the signature below and the
// front report) — without it the oracle stays null and the camera clips through the scenery.
#pragma once

#include "Gfx/Camera.h"
#include "Game/GameState.h"

// Provider of zone collision queries (terrain .WG sweep, blocked point Main/WJ, ground
// Main). Defined in World/WorldIntegration.h; forward-declared here (precedent:
// Gfx/WorldGeometryRenderer.h:251) to keep this header light.
namespace ts2::world { class WorldAssets; }

namespace ts2::gfx {

// Complete third-person camera tick for ONE InGame frame. `world` = game::g_World (read-only;
// `world.Self()`/`world.players[0]` provides the local player position — if `world.players` is
// empty, both InGame_InitCamera and Camera_UpdateCollision frame on the origin (0,0,0), a clean
// fallback faithful to the "self not yet spawned" case).
//
//   - If `justEnteredInGame` is true: FIRST calls game::InGame_InitCamera(camera, ...)
//     with the local player position — entry framing, EXACTLY once per InGame entry
//     (faithful to Scene_InGameUpdate case 3). The framing state (game::CameraFollowState,
//     originally dword_1837E64/68) is OWNED BY THIS MODULE (only one third-person camera
//     active at a time client-side).
//   - In ALL cases: THEN calls game::Camera_UpdateCollision(camera, world, ...)
//     for target follow + collision of the current frame (0x538580).
//
// `worldAssets` (WG-02): if non-null AND the zone has loaded its collision, a real oracle
// (WorldCameraCollision: game::ICameraCollisionQueries) wires the 4 binary queries
// (Terrain_SweepSphereSegment 0x69a1f0 on the .WG; World_IsPointBlocked 0x540da0 on Main+WJ;
// MapColl_GetGroundHeight @0x5388f4 on Main; MapColl_LineOfSightObjects 0x696fc0 -> false,
// not ported). `worldAssets==nullptr` -> null oracle = faithful "zone not loaded" behavior
// (the camera follows the previous arm WITHOUT collision correction).
//
// `dt` is reserved for a future velocity-driven orbit/zoom hookup: NEITHER InGame_InitCamera
// NOR Camera_UpdateCollision consume `dt` in the original binary — not invoked here to stay
// strictly faithful.
//
// ORCHESTRATOR WIRING (WG-02, outside my files): Scene/SceneManager.cpp:1254 must pass the
// 5th argument `worldAssets_.get()` (a member already present on SceneManager, cf.
// SceneManager.cpp:268) — a single argument to add. Without it, compilation fails (required
// parameter, no default: anti-dead-code guarantee).
void TickThirdPersonCamera(Camera& camera, const game::GameWorld& world,
                            float dt, bool justEnteredInGame,
                            const world::WorldAssets* worldAssets);

} // namespace ts2::gfx

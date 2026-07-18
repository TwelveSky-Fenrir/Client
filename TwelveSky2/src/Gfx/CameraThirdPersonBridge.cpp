// Gfx/CameraThirdPersonBridge.cpp — implementation. See CameraThirdPersonBridge.h for the
// full doc (scope, wiring signature expected on the SceneManager side).
#include "Gfx/CameraThirdPersonBridge.h"
#include "Game/CameraWarpTick.h"       // InGame_InitCamera, CameraFollowState (already written)
#include "Game/AnimationTick.h"        // Camera_UpdateCollision, ICameraCollisionQueries, CameraCollisionHost
#include "World/WorldIntegration.h"    // world::WorldAssets — collision-query provider (WG-02)

namespace ts2::gfx {

namespace {

// Camera framing state (original dword_1837E64/dword_1837E68/g_CamFollowDist, see
// Game/CameraWarpTick.h::CameraFollowState): only ONE 3rd-person camera active at a time
// client-side -> state owned by this module.
game::CameraFollowState g_FollowState{};

// Free-look host (Camera_UpdateCollision): no free-look toggle UI system on the
// ClientSource side -> all std::function members stay null (faithful clean degradation).
const game::CameraCollisionHost g_NoFreeLookHost{};

// REAL collision oracle (WG-02, Camera_UpdateCollision 0x538580): translates the 4 queries
// of the game::ICameraCollisionQueries interface to world::WorldAssets. Each method targets the
// slot PROVEN by the call-site disassembly (report §1.3): the sweep -> .WG (slot 0 =
// g_GameWorld @0x5387b9); the blocked point -> Main+WJ (0x540da0); the ground -> Main (@0x5388f4).
class WorldCameraCollision final : public game::ICameraCollisionQueries {
public:
    explicit WorldCameraCollision(const world::WorldAssets& assets) : assets_(assets) {}

    // Terrain_SweepSphereSegment 0x69a1f0 (radius 2.5) against the TERRAIN .WG.
    bool SweepSphereSegment(const D3DXVECTOR3& from, const D3DXVECTOR3& to, float radius,
                            D3DXVECTOR3& outHit) const override {
        const float f[3] = { from.x, from.y, from.z };
        const float t[3] = { to.x, to.y, to.z };
        float hit[3];
        if (!assets_.SweepCameraSegment(f, t, radius, hit)) return false;
        outHit.x = hit[0]; outHit.y = hit[1]; outHit.z = hit[2];
        return true;
    }
    // World_IsPointBlocked 0x540da0 (Main + WJ).
    bool IsPointBlocked(const D3DXVECTOR3& p) const override {
        const float pt[3] = { p.x, p.y, p.z };
        return assets_.IsPointBlocked(pt);
    }
    // MapColl_LineOfSightObjects 0x696fc0 (not ported -> false, see WorldIntegration.cpp).
    bool LineOfSightBlockedByObjects(const D3DXVECTOR3& from, const D3DXVECTOR3& to) const override {
        const float f[3] = { from.x, from.y, from.z };
        const float t[3] = { to.x, to.y, to.z };
        return assets_.LineOfSightBlockedByObjects(f, t);
    }
    // MapColl_GetGroundHeight(&dword_14A88E4, x, z, &out, 0, 0.0, 0, 1) @0x5388f4 = HasGroundAt.
    bool IsGroundBlocked(float x, float z) const override {
        return assets_.HasGroundAt(x, z);
    }

private:
    const world::WorldAssets& assets_;
};

} // namespace

void TickThirdPersonCamera(Camera& camera, const game::GameWorld& world,
                            float dt, bool justEnteredInGame,
                            const world::WorldAssets* worldAssets) {
    // `dt` unused here: NEITHER InGame_InitCamera NOR Camera_UpdateCollision integrate
    // time in the original binary (see CameraThirdPersonBridge.h header).
    (void)dt;

    // Local player position. GameWorld::Self() is not const (may insert a default slot
    // if `players` is empty) -> read-only access via a local non-const reference.
    game::GameWorld& mutableWorld = const_cast<game::GameWorld&>(world);
    const game::PlayerEntity& self = mutableWorld.Self();

    if (justEnteredInGame) {
        // InGame entry framing (Scene_InGameUpdate case 3, EA 0x52C6EF) — already written
        // (Game/CameraWarpTick.h), reused as-is: NO duplication.
        game::InGame_InitCamera(camera, g_FollowState, self.x, self.y, self.z);
    }

    // Target follow + terrain/object collision (Camera_UpdateCollision 0x538580). WG-02:
    // REAL oracle when the zone is loaded (worldAssets non-null) -> the camera no longer
    // clips through scenery; else nullptr = faithful "zone not loaded" behavior (previous
    // arm, unfixed). Free-look disabled (no toggle UI system on the ClientSource side).
    if (worldAssets) {
        const WorldCameraCollision collision(*worldAssets);
        game::Camera_UpdateCollision(camera, world, /*freeLookActive=*/false, /*camMode=*/0,
                                      &collision, g_NoFreeLookHost);
    } else {
        game::Camera_UpdateCollision(camera, world, /*freeLookActive=*/false, /*camMode=*/0,
                                      /*collision=*/nullptr, g_NoFreeLookHost);
    }
}

} // namespace ts2::gfx

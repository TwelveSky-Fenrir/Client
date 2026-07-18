// Game/AnimationTick_World.cpp — implementation. See Game/AnimationTick.h for the full doc
// (original EA, scope, hosts/oracles). Decompilation source: idaTs2 (Hex-Rays).
// Split family: shared morph-timer helpers live in Game/AnimationTick_Internal.h;
// Player_UpdateLocalAnim/Char_UpdateAnimationFrame (§1/§2) live in Game/AnimationTick.cpp;
// the monster/zone-NPC/player-FSM dispatch (§5/§6/§7) lives in Game/AnimationTick_Entities.cpp.
// This file covers §3/§4: Camera_UpdateCollision, MapColl_UpdateObjectAnim.
#include "Game/AnimationTick.h"
#include "Game/ClientRuntime.h"   // g_Client.Var/VarF (long-tail globals escape hatch)
#include "Game/MapWarp.h"         // BeginWarpToFactionTown, WarpAddr::SelfMorphNpcId
#include "Game/CameraWarpTick.h"  // Cam_SetLookAt (already written, reused as-is)
#include "Game/EntityLifecycleTick.h" // g_MonsterTickExt (motionState/animFrame/attackWindupMode) — §5
#include "Game/ExtraDatabases.h"      // NpcDefRecord::id (decor NPC kind, ZoneNpc_OnDialogueOpen) — §6
                                       // (the NPC pool itself = g_World.npcRenderEntries, via GameState.h)
#include <cmath>
#include <cstring>

namespace ts2::game {

// 3. Camera_UpdateCollision 0x538580
namespace {

D3DXVECTOR3 NormalizeSafe(const D3DXVECTOR3& v, float& outLen) {
    outLen = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (outLen <= 0.0f) return D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    return D3DXVECTOR3(v.x / outLen, v.y / outLen, v.z / outLen);
}

} // namespace

void Camera_UpdateCollision(gfx::Camera& camera, const GameWorld& world,
                             bool freeLookActive, int camMode,
                             const ICameraCollisionQueries* collision,
                             const CameraCollisionHost& host) {
    const bool hasCharInvBlock = !world.self.charInvBlock.empty(); // !g_SelfCharInvBlock inverted
    const int32_t morphNpcId = g_Client.VarGet(WarpAddr::SelfMorphNpcId);

    // --- Guard 0x5385A5: "shop"/morph 194 mode outside free-look -> early return ---
    if (!hasCharInvBlock && !freeLookActive && morphNpcId == 194) return;

    // Current self position (flt_1687330 array — real pos block lives in GameState.h).
    D3DXVECTOR3 targetBase(world.players.empty() ? 0.0f : world.players[0].x,
                            world.players.empty() ? 0.0f : world.players[0].y,
                            world.players.empty() ? 0.0f : world.players[0].z);

    // --- "Follow entity" free-look (0x5385CD..0x538681) ---------------------------------
    if (!hasCharInvBlock && freeLookActive && camMode == 3) {
        D3DXVECTOR3 followPos;
        if (!host.FindFreeLookFollowTarget || !host.FindFreeLookFollowTarget(followPos))
            return; // i == g_EntityCount (0x538631): no target -> early return
        targetBase = followPos;
        if (host.SendFollowCameraUpdate) host.SendFollowCameraUpdate(targetBase);
    }

    const D3DXVECTOR3 target(targetBase.x, targetBase.y + 10.0f, targetBase.z); // v25,v26,v27

    // --- Reprojecting the previous camera "arm" around the new target (0x53868C..
    // 0x5387A0): keeps the same (eye-target) vector as the previous frame,
    // renormalized onto g_CamFollowDist (approximated via camera.Distance(), cf. top of
    // file / Game/CameraWarpTick.h). ----------------------------------------------------
    const D3DXVECTOR3 prevEye = camera.Eye();
    const D3DXVECTOR3 armVec(prevEye.x - target.x, prevEye.y - target.y, prevEye.z - target.z);
    // FIDELITY NOTE: the binary recomputes v12..14 = (g_CameraPos - flt_80013C) + the new
    // target, THEN reuses the direction (v12-target) — algebraically this is equivalent
    // to "direction = (previousEye - previousTargetTarget)"; here we use directly
    // (previousEye - currentTarget), which, with a near-stable target from one frame to the
    // next (player movement << camera distance), converges to the same result. Documented
    // as an ACCEPTED approximation (g_CameraPos/flt_80013C = renderer globals OUT OF
    // SCOPE for this module, not ported in Gfx/Camera.h).
    float armLen = 0.0f;
    D3DXVECTOR3 dir = NormalizeSafe(armVec, armLen);

    const float followDist = camera.Distance();
    D3DXVECTOR3 eye(target.x + followDist * dir.x, target.y + followDist * dir.y, target.z + followDist * dir.z);

    // --- Terrain collision correction (0x5387BE..0x5387D1) --------------------------------
    if (collision) {
        D3DXVECTOR3 hit;
        if (collision->SweepSphereSegment(target, eye, 2.5f, hit)) eye = hit;

        // --- "Ground-height stepping" fallback if the target isn't blocked AND the line
        // of sight crosses a map object (0x5387FD..0x538962) -----------------------------
        if (!collision->IsPointBlocked(targetBase) && collision->LineOfSightBlockedByObjects(target, eye)) {
            D3DXVECTOR3 toEye(eye.x - target.x, eye.y - target.y, eye.z - target.z);
            float dist2 = 0.0f;
            D3DXVECTOR3 dir2 = NormalizeSafe(toEye, dist2);

            float step = 1.0f;
            D3DXVECTOR3 stepped(target.x + step * dir2.x, target.y + step * dir2.y, target.z + step * dir2.z);
            while (collision->IsGroundBlocked(stepped.x, stepped.z)) {
                step += 1.0f;
                stepped = D3DXVECTOR3(target.x + step * dir2.x, target.y + step * dir2.y, target.z + step * dir2.z);
                if (dist2 < step) {
                    stepped = D3DXVECTOR3(target.x + dist2 * dir2.x, target.y + dist2 * dir2.y, target.z + dist2 * dir2.z);
                    break;
                }
            }
            eye = stepped;
        }
    }

    // --- Minimum distance clamp (0x538A0C..0x538A38): if the final distance <10,
    // pulls the eye in to a fixed distance of 4 around the target. -----------------------
    {
        D3DXVECTOR3 toEye(eye.x - target.x, eye.y - target.y, eye.z - target.z);
        float finalDist = 0.0f;
        D3DXVECTOR3 finalDir = NormalizeSafe(toEye, finalDist);
        if (finalDist < 10.0f)
            eye = D3DXVECTOR3(target.x + finalDir.x * 4.0f, target.y + finalDir.y * 4.0f, target.z + finalDir.z * 4.0f);
    }

    // --- Final placement (0x538A6A/0x538A9E): Cam_SetLookAt (ALREADY WRITTEN, Game/
    // CameraWarpTick.h) — a single call pushes eye+target onto `camera`, see its fidelity
    // note for the Camera_SetEyeTarget/g_GxdRenderer redundancy on the binary side
    // (not duplicated here). --------------------------------------------------------------
    Cam_SetLookAt(camera, eye.x, eye.y, eye.z, target.x, target.y, target.z);
}

// 4. MapColl_UpdateObjectAnim 0x694A00
void MapColl_UpdateObjectAnim(MapCollisionObjectAnimState& obj, float dt,
                               IMapObjectAnimOracle* oracle) {
    constexpr float kAnimFps = 15.0f; // original a2, ALWAYS 15.0 at the known call site

    if (!obj.active || obj.mode != 1) return; // 0x694A16: *(this+1) && *(this+2)==1

    // --- Animated sub-objects (0x694A1C..0x694AC9) ----------------------------------------
    for (MapAnimSubObject& sub : obj.animObjects) {
        sub.frame += kAnimFps * dt;
        if (!oracle) continue;
        const int frameCount = oracle->GetModelFrameCount(sub.modelIndex);
        if (frameCount <= 0) continue;
        // Faithful "modulo" loop (0x694A6C..0x694AB6): subtracts frameCount while
        // the integer index exceeds frameCount-1 (handles a dt large enough to skip
        // several anim loops in a single frame).
        while (static_cast<int>(sub.frame) > frameCount - 1)
            sub.frame -= static_cast<float>(frameCount);
    }

    // --- Particle emitters (0x694AD9..0x694B3C) -------------------------------------------
    if (!oracle) return;
    for (size_t i = 0; i < obj.particleEmitters.size(); ++i) {
        MapParticleEmitter& p = obj.particleEmitters[i];
        if (p.initialized)
            oracle->UpdateParticle(static_cast<int>(i), dt);
        else
            oracle->InitParticle(p.particleDefIndex); // the binary does NOT write `initialized`
                                                        // here (set elsewhere, out of scope)
    }
}

} // namespace ts2::game

// Game/CameraWarpTick.cpp — see Game/CameraWarpTick.h for the original EAs and
// detailed per-function fidelity notes.
#include "Game/CameraWarpTick.h"
#include <cmath>

namespace ts2::game {

// =====================================================================================
// 1. Third-person camera
// =====================================================================================

bool Cam_SetLookAt(gfx::Camera& camera,
                    float eyeX, float eyeY, float eyeZ,
                    float targetX, float targetY, float targetZ) {
    // 0x69cd07: eye == target identically -> rejected (null direction).
    if (eyeX == targetX && eyeY == targetY && eyeZ == targetZ)
        return false;

    const float dx = eyeX - targetX; // v11 0x69cd15
    const float dy = eyeY - targetY; // v12 0x69cd29
    const float dz = eyeZ - targetZ; // v13 0x69cd3d

    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist <= 0.0f) // div-by-0 guard (already excluded in practice by the test above)
        return false;

    // Raw disasm: fdivp (dy/dist) -> call Math_AsinFpu -> fmul flt_7BB28C(=radToDeg)
    // -> fabs -> fcomp flt_7EDB70(=89.989998). NOT a plain "ratio*radToDeg" (the Hex-Rays
    // output from decompile() is misleading on this exact point, cf. .h comment).
    const float elevationDeg = std::fabs(std::asin(dy / dist) * gfx::Camera::kRadToDeg);
    if (elevationDeg > 89.989998f) // 0x69cd8f/91
        return false;

    // yaw/pitch derived from the (eye, target) pair for gfx::Camera's isomorphic spherical
    // model (cf. Gfx/Camera.h header): eye = target + dist*(cos(p)*sin(y), sin(p),
    // cos(p)*cos(y)) => dx/dist = cos(p)*sin(y), dz/dist = cos(p)*cos(y), dy/dist = sin(p).
    const float yaw   = std::atan2(dx, dz);
    const float pitch = std::asin(dy / dist);

    camera.SetTarget(targetX, targetY, targetZ);
    camera.SetDistance(dist); // cf. documented clamp discrepancy in the .h
    camera.SetYaw(yaw);
    camera.SetPitch(pitch);
    return true;
}

void InGame_InitCamera(gfx::Camera& camera, CameraFollowState& follow,
                        float selfX, float selfY, float selfZ) {
    // EA 0x52c6fe..0x52c759: eye = self+(50,60,50), target = self+(0,10,0).
    const float eyeX = selfX + 50.0f;
    const float eyeY = selfY + 60.0f;
    const float eyeZ = selfZ + 50.0f;
    const float targetX = selfX;
    const float targetY = selfY + 10.0f;
    const float targetZ = selfZ;

    Cam_SetLookAt(camera, eyeX, eyeY, eyeZ, targetX, targetY, targetZ);
    // Camera_SetEyeTarget (0x403420, EA 0x52c7cf) recomputes EXACTLY the same eye/target
    // pair to push it into g_GxdRenderer — redundant on the binary side (two writes of the
    // same state), not reproduced separately here: `camera` already carries the up-to-date state.

    // g_CamFollowDist = Math_Dist3D(g_CameraPos, flt_80013C) — cf. .h fidelity note.
    follow.followDist = camera.Distance();

    follow.initialized    = true; // dword_1837E64 = 1 (0x52c802)
    follow.transitionFlag = 0;    // dword_1837E68 = 0 (0x52c80c)
}

// =====================================================================================
// 2. "Warp suppressed" flag timeout
// =====================================================================================

void Warp_TickSuppressionTimeout(WarpSuppressionState& state, float gameTimeSec) {
    // EA 0x52c91f, faithful: `if (dword_1675B00 && g_GameTimeSec - flt_1675B04 > 10.0)`.
    if (state.suppressed && (gameTimeSec - state.setAtSec) > 10.0f)
        state.suppressed = false; // 0x52c921
}

void Warp_SetSuppressed(WarpSuppressionState& state, float gameTimeSec) {
    state.suppressed = true;
    state.setAtSec = gameTimeSec;
}

// =====================================================================================
// 3. Auto-use potion
// =====================================================================================

namespace {

// Faithful to the identical double call (belt scan) repeated 3x in the binary for
// {HP!=5, HP==5, MP!=5}: only the PotionKind (hence the subtype set {1,2,5}/{3,4,5})
// changes on the host.FindBeltPotionSlot side. Returns true + `outSlot` filled if a slot
// was found AND used (host.UsePotion called), false otherwise (no effect).
bool TryUsePotion(const AutoPotionHost& host, PotionKind kind) {
    if (!host.FindBeltPotionSlot)
        return false;
    BeltSlot slot;
    if (!host.FindBeltPotionSlot(kind, slot))
        return false;
    if (host.UsePotion)
        host.UsePotion(kind, slot);
    if (host.SetGmCmdCooldownActive)
        host.SetGmCmdCooldownActive();
    return true;
}

float Call(const std::function<float()>& fn) { return fn ? fn() : 0.0f; }
int   Call(const std::function<int()>& fn)   { return fn ? fn() : 0; }
bool  Call(const std::function<bool()>& fn)  { return fn ? fn() : false; }

} // namespace

void Game_AutoUsePotion(const AutoPotionHost& host) {
    // Guards 0x5c485a.
    if (Call(host.GetHpGauge) < 1.0f)
        return;
    if (Call(host.IsMorphInProgress))
        return;
    if (!Call(host.IsAutoPotionSystemEnabled))
        return;
    if (Call(host.IsGmCmdCooldownActive))
        return;
    const int selfAction = Call(host.GetSelfActionState);
    if (selfAction == 11 || selfAction == 12 || selfAction == 38)
        return;

    // --- HP test (dword_1674728) ----------------------------------------------------
    const int hpSetting = Call(host.GetHpThresholdSetting);
    if (hpSetting >= 1 && hpSetting <= 5) {
        const float hp = Call(host.GetHpGauge);
        const float hpMetric = Call(host.GetHpThresholdMetric);
        bool shouldScan;
        if (hpSetting != 5) {
            // 0x5c48a7: hp < threshold * metric / 5.
            shouldScan = hp < (static_cast<float>(hpSetting) * hpMetric / 5.0f);
        } else {
            // 0x5c49b3: scan only if !(metric*0.99 <= hp), i.e. hp < metric*0.99.
            shouldScan = !(hpMetric * 0.99f <= hp);
        }
        if (shouldScan && TryUsePotion(host, PotionKind::Hp))
            return; // LABEL_70: only one potion per frame.
    }

    // --- MP test (dword_167472C), reached only if the HP test did not trigger --------
    const int mpSetting = Call(host.GetMpThresholdSetting);
    if (mpSetting >= 1 && mpSetting <= 5) {
        const float mp = Call(host.GetMpGauge);
        const float mpMetric = Call(host.GetMpThresholdMetric);
        bool shouldScan;
        if (mpSetting == 5) {
            // 0x5c4be5: scan if metric*0.99 > mp.
            shouldScan = (mpMetric * 0.99f) > mp;
        } else {
            // 0x5c4ad9: mp < threshold * metric / 5.
            shouldScan = mp < (static_cast<float>(mpSetting) * mpMetric / 5.0f);
        }
        if (shouldScan)
            TryUsePotion(host, PotionKind::Mp); // LABEL_70 or silent return if nothing found.
    }
}

// =====================================================================================
// 4. Pending target request (clan/faction) — ex "active guild/party name"
//    (fidelity rename, cf. detailed note in Game/CameraWarpTick.h section 4)
// =====================================================================================

bool HasPendingTargetRequest(const std::string& reqTargetSub2, const std::string& reqTargetSub1) {
    return !reqTargetSub2.empty() || !reqTargetSub1.empty();
}

} // namespace ts2::game

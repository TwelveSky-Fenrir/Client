// Gfx/Camera.cpp — implementation of the GXD engine's orbiting camera.
// See Camera.h for the table of reversed functions.
#include "Gfx/Camera.h"

#include <cmath>

// Link Direct3D9 / D3DX9 libs (DirectX SDK June 2010, x86).
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

Camera::Camera() = default;

// Elevation clamp.
// Faithful to Cam_OrbitPitch (0x69CF90) / Camera_RotateEyePitch (0x403650):
//   if ( fabs(newPitchDeg) <= 89.9 ) apply, else ignore.
// Here we clamp (instead of ignoring): the visual result is identical — the eye
// never crosses the pole, which protects LookAtLH from gimbal lock (up fixed at (0,1,0)).
float Camera::ClampPitch(float rad)
{
    const float limit = kPitchLimitDeg * kDegToRad;
    if (rad > limit)  return limit;
    if (rad < -limit) return -limit;
    return rad;
}

// Clamps the distance to [min,max].
// Cam_ClampDistance (0x69CE00) only clamps the maximum; the 25..150 bounds of
// Camera_Init also enforce the minimum via Camera_MouseWheelZoom. We apply
// both bounds consistently here.
void Camera::ClampDistanceInternal()
{
    if (distance_ < minDist_) distance_ = minDist_;
    if (distance_ > maxDist_) distance_ = maxDist_;
}

void Camera::SetDistance(float d)
{
    distance_ = d;
    ClampDistanceInternal();
}

void Camera::SetDistanceLimits(float mn, float mx)
{
    if (mn > mx) { const float t = mn; mn = mx; mx = t; }
    minDist_ = mn;
    maxDist_ = mx;
    ClampDistanceInternal();
}

// Incremental orbit.
// yaw accumulates freely (Cam_OrbitYaw 0x69CEE0 is purely incremental);
// pitch is clamped (Cam_OrbitPitch 0x69CF90).
void Camera::Orbit(float dYawRad, float dPitchRad)
{
    yaw_  += dYawRad;
    pitch_ = ClampPitch(pitch_ + dPitchRad);
}

// Mouse-drag orbit.
// Camera_MouseDragRotate (0x50AFD0):
//   yawDeg   = (mx - lastMx) * 0.2   (this+60)
//   pitchDeg = (my - lastMy) * 0.3   (this+64)
// then Cam_OrbitYaw(yawDeg) / Cam_OrbitPitch(pitchDeg) (degrees -> radians internally).
//
// 30deg/80deg DRAG BOUNDS (gap INPUT-10, filled in Passe 4 / W9) — REVERT, NOT CLAMP.
// Original sequence, re-read:
//   1. save eye (0x800130/34/38) + target (0x80013C/40/44)          @0x50B075..0x50B0A2
//   2. Cam_OrbitYaw then Cam_OrbitPitch
//   3. v22 = Math_Dist3D(&eye, &target)                             @0x50B10B
//      if v22 > 0 : v32 = asin(fabs(eye.y - target.y) / v22) * 57.2957763671875  @0x50B12A..0x50B16D
//      else        v32 = 0                                         @0x50B174
//   4. if (target.y >= eye.y && 30.0 < |v32|) @0x50B26A  OR  (target.y < eye.y && 80.0 < |v32|)
//      @0x50B1BA  ->  Cam_SetLookAt(saved state) @0x50B29B / @0x50B1EB
//                     + Camera_SetEyeTarget @0x50B2CF  AND RETURN.
//
// FIDELITY NOTE: the original restores the FULL EYE, so YAW gets reverted too —
//   not just pitch. We restore both here (the drag is rejected wholesale, it is
//   not "trimmed" on just the offending axis).
//
// The symmetric 89.9deg clamp of Orbit()/ClampPitch (Cam_OrbitPitch 0x69CF90) still applies
// UPSTREAM via Orbit(): it's a separate engine-level guard, and both stack as in the
// binary (Camera_MouseDragRotate calls Cam_OrbitPitch, which already clamps, THEN tests).
//
// OUT OF SCOPE (controller, cf. Gfx/CameraThirdPersonBridge, not owned here): the scene
// guards at 0x50AFD0 (`g_SceneMgr == 6 && g_SceneSubState == 4`, button `a4 == 2`) decide
// WHETHER the drag happens at all; they don't change what the drag does.
void Camera::OrbitByMouse(int dxPixels, int dyPixels)
{
    // 1) Save state BEFORE orbiting (equivalent to the saved eye/target).
    const float savedYaw   = yaw_;
    const float savedPitch = pitch_;

    // 2) Apply the orbit (yaw free, pitch clamped to 89.9deg by ClampPitch).
    const float yawDeg   = static_cast<float>(dxPixels) * kMouseYawSensDeg;
    const float pitchDeg = static_cast<float>(dyPixels) * kMousePitchSensDeg;
    Orbit(yawDeg * kDegToRad, pitchDeg * kDegToRad);

    // 3) Resulting elevation in degrees. Our pitch IS the angle the original recomputes
    //    via asin(|eye.y - target.y| / dist): with eye = target + dist*(cp*sy, sp, cp*cy),
    //    we get |eye.y - target.y| / dist = |sin(pitch)|, so asin(...) = |pitch|.
    //    We therefore use |pitch| directly instead of redoing the trig round trip.
    const float elevDeg = std::fabs(pitch_) * kRadToDeg;

    // 4) Test the two asymmetric bounds then REVERT (yaw AND pitch) + return.
    //    pitch <= 0 <=> target.y >= eye.y (camera below target) -> 30deg bound  @0x50B26A
    //    pitch >  0 <=> target.y <  eye.y (camera above target) -> 80deg bound  @0x50B1BA
    const float limitDeg = (pitch_ <= 0.0f) ? kDragPitchLimitBelowDeg
                                             : kDragPitchLimitAboveDeg;
    if (limitDeg < elevDeg) {
        yaw_   = savedYaw;   // @0x50B29B / @0x50B1EB : restore then hard return
        pitch_ = savedPitch;
        return;
    }
}

// Dolly (zoom). delta>0 moves the eye closer to the target; re-clamped to bounds.
void Camera::Zoom(float delta)
{
    distance_ -= delta;
    ClampDistanceInternal();
}

// Wheel dolly.
// Camera_MouseWheelZoom (0x50B460) applies a step proportional to the wheel
// delta (this+19 = 0.1) and clamps the distance to [25,150]. WM_MOUSEWHEEL
// reports multiples of WHEEL_DELTA (120); a forward notch => zoom in.
void Camera::ZoomByWheel(int wheelDelta)
{
    const float steps = static_cast<float>(wheelDelta) / 120.0f; // notches
    Zoom(steps * kWheelZoomStep);
}

// Update: integrates orbit/zoom velocities over dt then resynchronizes the state.
// Zero velocities by default => Update is just a per-frame hook point
// (the original controller, Camera_UpdateFromInput 0x50B7D0, is event-driven:
// each key press applies a discrete step, no integration).
void Camera::Update(float dt)
{
    if (yawVel_ != 0.0f || pitchVel_ != 0.0f)
        Orbit(yawVel_ * dt, pitchVel_ * dt);
    if (zoomVel_ != 0.0f)
        Zoom(zoomVel_ * dt);
}

// Eye derived from the spherical state.
// Isomorphic to the original's incremental rotations (Cam_OrbitYaw / Cam_OrbitPitch),
// with the engine's convention (left-handed LookAtLH, up=(0,1,0)):
//   yaw = PI  => eye on -Z, i.e. eye = target + (0,0,-dist), like the init in
//   Gfx_InitDevice (eye (0,0,-10), target (0,0,0)).
D3DXVECTOR3 Camera::Eye() const
{
    const float cp = std::cos(pitch_);
    const float sp = std::sin(pitch_);
    const float sy = std::sin(yaw_);
    const float cy = std::cos(yaw_);
    return D3DXVECTOR3(
        target_.x + distance_ * cp * sy,
        target_.y + distance_ * sp,
        target_.z + distance_ * cp * cy);
}

D3DXVECTOR3 Camera::Forward() const
{
    D3DXVECTOR3 fwd = target_ - Eye();
    D3DXVec3Normalize(&fwd, &fwd);
    return fwd;
}

// View matrix: LookAtLH(eye, target, up) — cf. GXD_BeginScene (0x4046E3),
// where up is fixed to (0,1,0). Result = g_GxdRenderer+748. ex-VeryOldClient: mViewMatrix.
void Camera::BuildViewMatrix(D3DXMATRIX& out) const
{
    const D3DXVECTOR3 eye = Eye();
    D3DXMatrixLookAtLH(&out, &eye, &target_, &up_);
}

// Projection matrix: PerspectiveFovLH(fovY, aspect, near, far) — cf.
// Gfx_InitDevice (0x69BFC6), vertical FOV = 45deg converted to radians.
// Result = g_GxdRenderer+648. ex-VeryOldClient: mPerspectiveMatrix.
void Camera::BuildProjMatrix(D3DXMATRIX& out, float aspect) const
{
    D3DXMatrixPerspectiveFovLH(&out, fovY_, aspect, nearZ_, farZ_);
}

} // namespace ts2::gfx

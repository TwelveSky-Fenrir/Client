// Gfx/Camera.h — orbiting camera of the GXD engine ("look at a target" model).
//
// LEAF module: depends on no other project header. It cleanly reifies the
// camera data model reversed from TwelveSky2.exe, which is scattered between the
// input controller and the two renderer singletons (g_GfxRenderer @ 0x7FFE18, g_GxdRenderer
// @ 0x18C4EF8). See Docs/TS2_GXD_ENGINE.md.
//
// The original camera is NOT parameterized by (yaw,pitch,dist): it stores two
// points (eye, target) and applies incremental rotations around the target.
// Functions reversed as the source of the model below:
//   Cam_SetLookAt            0x69CCD0  eye/target + pitch guard 89.99deg
//   Cam_OrbitYaw             0x69CEE0  rotates the eye around the target (Y axis)
//   Cam_OrbitPitch           0x69CF90  elevation rotation, clamp 89.9deg
//   Cam_ClampDistance        0x69CE00  clamps the eye->target distance
//   Camera_SetEyeTarget      0x403420  pushes eye/target into g_GxdRenderer (+712/+724)
//   Camera_Init              0x50ABC0  control constants (sensitivities, zoom bounds)
//   Camera_MouseDragRotate   0x50AFD0  orbit on mouse drag (yaw 0.2, pitch 0.3 deg/px)
//   Camera_MouseWheelZoom    0x50B460  wheel dolly (step 0.1, bounds 25..150)
//   Camera_UpdateFromInput   0x50B7D0  keyboard/keybind controller (orbit step 6deg)
//   Gfx_InitDevice           0x69B9B0  proj = PerspectiveFovLH(45deg, w/h, near, far)
//   GXD_BeginScene           0x4046E3  view = LookAtLH(eye, target, up=(0,1,0)) every frame
//
// Here, the same behavior is exposed via an equivalent spherical state
// (target + distance + yaw + pitch + fov), strictly isomorphic to the original's
// incremental rotations, and the view/projection matrices are rebuilt with d3dx9
// exactly like the engine (left-handed LookAtLH + left-handed PerspectiveFovLH).
#pragma once

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

namespace ts2::gfx {

class Camera {
public:
    // --- Constants faithful to the disassembly -------------------------------------
    // Vertical FOV: g_GfxRenderer+128 = 0x42340000 = 45.0f ; g_GxdRenderer+56 = 45.0f.
    // ex-VeryOldClient: mFovY (v1 Object A @+128 AND v2 Object B @+56, note 45.0). CONFIRMED §1.2.
    static constexpr float kFovDegDefault = 45.0f;
    // Conversion factors: exact literals used by the binary.
    static constexpr float kDegToRad = 0.017453292f;      // pi/180 (0x3C8EFA35)
    static constexpr float kRadToDeg = 57.2957763671875f; // 180/pi (0x42652EE1)
    // Elevation guard: Cam_OrbitPitch/Camera_RotateEyePitch clamp at 89.9deg
    // (Cam_SetLookAt rejects beyond 89.99deg). We keep the strict 89.9 bound.
    // ENGINE LEVEL — not to be confused with the two CONTROLLER bounds below, nor
    // with the 89.99 of Camera_SetEyeTarget 0x403420 (@0x40350E, cf. Gfx/GxdRenderer.h):
    // all THREE coexist in the binary, at three different layers.
    static constexpr float kPitchLimitDeg = 89.9f;

    // --- MOUSE DRAG elevation bounds (Camera_MouseDragRotate 0x50AFD0) -------------
    // Two ASYMMETRIC bounds, set by Camera_Init 0x50ABC0 (bytes read from the image):
    //   +68 (0x44) <- flt_7EDA24 = 0x41F00000 = 30.0f  (`fstp dword ptr [ecx+44h]` @0x50AC37)
    //   +72 (0x48) <- flt_7A95B0 = 0x42A00000 = 80.0f  (`fstp dword ptr [edx+48h]` @0x50AC43)
    //
    // REVERT semantics (not clamp) — cf. Camera::OrbitByMouse:
    //   target ABOVE eye (`flt_800140 >= flt_800134`) and 30.0 < |elevDeg| @0x50B26A
    //   eye ABOVE target (`flt_800140 <  flt_800134`) and 80.0 < |elevDeg| @0x50B1BA
    // -> the original RESTORES the eye AND target saved before the orbit
    //    (Cam_SetLookAt @0x50B29B / @0x50B1EB + Camera_SetEyeTarget @0x50B2CF) then RETURNS.
    //
    // Sign convention resolved against `target.y >= eye.y`: the derived eye is
    // eye.y = target.y + dist*sin(pitch) (cf. Camera::Eye()), hence
    //   target.y >= eye.y  <=>  sin(pitch) <= 0  <=>  pitch <= 0  -> 30deg bound
    //   target.y <  eye.y  <=>  pitch > 0                          -> 80deg bound
    static constexpr float kDragPitchLimitBelowDeg = 30.0f; // pitch <= 0 (camera below target)
    static constexpr float kDragPitchLimitAboveDeg = 80.0f; // pitch >  0 (camera above target)
    // Zoom bounds: Camera_Init writes +84 = 25.0 (min) and +88 = 150.0 (max).
    static constexpr float kMinDistDefault = 25.0f;
    static constexpr float kMaxDistDefault = 150.0f;
    // Mouse sensitivities: Camera_Init +60 = 0.2 (yaw), +64 = 0.3 (pitch), in deg/pixel.
    static constexpr float kMouseYawSensDeg   = 0.2f;
    static constexpr float kMousePitchSensDeg = 0.3f;
    // Wheel step: Camera_Init +76 = 0.1 (distance unit per notch).
    static constexpr float kWheelZoomStep = 0.1f;
    // Keyboard orbit step: Cam_OrbitYaw(+/-6.0) in Camera_UpdateFromInput.
    static constexpr float kKeyOrbitStepDeg = 6.0f;
    // Default eye: Gfx_InitDevice initializes eye=(0,0,-10), target=(0,0,0).
    static constexpr float kDefaultDistance = 10.0f;

    Camera();

    // --- Target (look-at point) -----------------------------------------------------
    void SetTarget(const D3DXVECTOR3& t) { target_ = t; }
    void SetTarget(float x, float y, float z) { target_.x = x; target_.y = y; target_.z = z; }
    const D3DXVECTOR3& Target() const { return target_; }

    // --- Eye->target distance (zoom) ----------------------------------------------
    // Clamped to [minDist_, maxDist_] (faithful to Cam_ClampDistance + Init bounds).
    void  SetDistance(float d);
    float Distance() const { return distance_; }
    void  SetDistanceLimits(float mn, float mx);
    float MinDistance() const { return minDist_; }
    float MaxDistance() const { return maxDist_; }

    // --- Orbit angles (radians) -------------------------------------------------
    // yaw: rotation around the Y axis (Cam_OrbitYaw). pitch: elevation (Cam_OrbitPitch),
    // clamped to +/-kPitchLimitDeg.
    void  SetYaw(float rad)   { yaw_ = rad; }
    void  SetPitch(float rad) { pitch_ = ClampPitch(rad); }
    float Yaw() const   { return yaw_; }
    float Pitch() const { return pitch_; }

    // --- Projection ----------------------------------------------------------------
    void  SetFovDeg(float deg) { fovY_ = deg * kDegToRad; }
    void  SetFovRad(float rad) { fovY_ = rad; }
    float FovY() const { return fovY_; }            // radians
    float FovDeg() const { return fovY_ * kRadToDeg; }
    // near/far: supplied by the renderer (Gfx_InitDevice params a7/a8); default
    // values are indicative, adjustable.
    void  SetClipPlanes(float nearZ, float farZ) { nearZ_ = nearZ; farZ_ = farZ; }
    float NearZ() const { return nearZ_; }
    float FarZ()  const { return farZ_; }

    // --- Up vector (Camera_SetUpVector 0x71CE40; the engine hardcodes (0,1,0)) ------
    void SetUp(const D3DXVECTOR3& up) { up_ = up; }
    const D3DXVECTOR3& Up() const { return up_; }

    // --- Orbit mutators --------------------------------------------------------
    // Incremental orbit (radians). Equivalent to Cam_OrbitYaw(dyaw)+Cam_OrbitPitch(dpitch)
    // with the same elevation clamp.
    void Orbit(float dYawRad, float dPitchRad);
    // Mouse-drag orbit, in pixels: applies the 0.2/0.3 deg/px sensitivities of
    // Camera_MouseDragRotate (0x50AFD0).
    void OrbitByMouse(int dxPixels, int dyPixels);
    // Dolly: moves the eye closer (delta>0) or farther, distance re-clamped to bounds.
    void Zoom(float delta);
    // Wheel dolly: `wheelDelta` = sum of WM_MOUSEWHEEL notches (multiples of 120).
    // A forward notch zooms in (Camera_MouseWheelZoom, 0x50B460).
    void ZoomByWheel(int wheelDelta);

    // --- Optional velocities (continuous orbit while a key is held) ------------------
    // Zero by default: Update(dt) then just resynchronizes the derived eye.
    void SetOrbitVelocity(float yawRadPerSec, float pitchRadPerSec) { yawVel_ = yawRadPerSec; pitchVel_ = pitchRadPerSec; }
    void SetZoomVelocity(float unitsPerSec) { zoomVel_ = unitsPerSec; }

    // Integrates orbit/zoom velocities over `dt` (seconds) and recomputes the eye.
    void Update(float dt);

    // --- Derived positions --------------------------------------------------------
    // Eye reconstructed from the spherical state (isomorphic to the original's rotations):
    //   eye = target + dist * (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))
    // Pushed into g_GxdRenderer+712 (Camera_SetEyeTarget 0x403420). ex-VeryOldClient: mCameraEye.
    D3DXVECTOR3 Eye() const;
    // Normalized forward direction = normalize(target - eye) (cf. g_GxdRenderer+736). ex-VeryOldClient: mCameraForward.
    D3DXVECTOR3 Forward() const;
    // Retrieves the (eye,target) pair to push to the renderer (Camera_SetEyeTarget).
    void GetEyeTarget(D3DXVECTOR3& eye, D3DXVECTOR3& at) const { eye = Eye(); at = target_; }

    // --- Matrices ------------------------------------------------------------------
    // Left-handed view: D3DXMatrixLookAtLH(eye, target, up) — like GXD_BeginScene (0x4046E3).
    void BuildViewMatrix(D3DXMATRIX& out) const;
    // Left-handed projection: D3DXMatrixPerspectiveFovLH(fovY, aspect, near, far) —
    // like Gfx_InitDevice (0x69BFC6). `aspect` = back-buffer width/height.
    void BuildProjMatrix(D3DXMATRIX& out, float aspect) const;

private:
    static float ClampPitch(float rad);
    void         ClampDistanceInternal();

    D3DXVECTOR3 target_{0.0f, 0.0f, 0.0f}; // look-at target (g_GxdRenderer+724) — ex-VeryOldClient: mCameraLook
    D3DXVECTOR3 up_{0.0f, 1.0f, 0.0f};     // up fixed to (0,1,0) in the engine
    float distance_ = kDefaultDistance;    // eye->target distance
    float yaw_   = D3DX_PI;                 // default yaw => eye on -Z (0,0,-dist)
    float pitch_ = 0.0f;                    // elevation
    float fovY_  = kFovDegDefault * kDegToRad; // vertical FOV (radians) — ex-VeryOldClient: mFovY (g_GxdRenderer+56)
    float nearZ_ = 1.0f;                    // near plane (renderer, g_GxdRenderer+60) — ex-VeryOldClient: mNearPlane
    float farZ_  = 1000000.0f;              // far plane — App_Init @0x461d5e/0x461dd2 passes far=1e6 (flt_7EDB80=0x49742400) to Gfx_InitDevice/GXD_DeviceReinit ; ex-VeryOldClient: mFarPlane
    float minDist_ = kMinDistDefault;       // min zoom bound (Camera_Init+84)
    float maxDist_ = kMaxDistDefault;       // max zoom bound (Camera_Init+88)
    float yawVel_   = 0.0f;                  // rad/s (optional continuous orbit)
    float pitchVel_ = 0.0f;                  // rad/s
    float zoomVel_  = 0.0f;                  // units/s
};

} // namespace ts2::gfx

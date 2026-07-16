// Gfx/Camera.h — caméra en orbite du moteur GXD (modèle « regarde une cible »).
//
// Module FEUILLE : ne dépend d'aucun autre header du projet. Il réifie proprement le
// modèle de données caméra reversé dans TwelveSky2.exe, dispersé entre le contrôleur
// d'entrée et les deux singletons renderer (g_GfxRenderer @ 0x7FFE18, g_GxdRenderer
// @ 0x18C4EF8). Voir Docs/TS2_GXD_ENGINE.md.
//
// La caméra d'origine n'est PAS paramétrée par (yaw,pitch,dist) : elle stocke deux
// points (oeil, cible) et applique des rotations incrémentales autour de la cible.
// Fonctions reversées à l'origine du modèle ci-dessous :
//   Cam_SetLookAt            0x69CCD0  oeil/cible + garde-fou pitch 89.99deg
//   Cam_OrbitYaw             0x69CEE0  rotation de l'oeil autour de la cible (axe Y)
//   Cam_OrbitPitch           0x69CF90  rotation d'élévation, clamp 89.9deg
//   Cam_ClampDistance        0x69CE00  bride la distance oeil->cible
//   Camera_SetEyeTarget      0x403420  pousse oeil/cible dans g_GxdRenderer (+712/+724)
//   Camera_Init              0x50ABC0  constantes de contrôle (sensibilités, bornes zoom)
//   Camera_MouseDragRotate   0x50AFD0  orbite au drag souris (yaw 0.2, pitch 0.3 deg/px)
//   Camera_MouseWheelZoom    0x50B460  dolly molette (pas 0.1, bornes 25..150)
//   Camera_UpdateFromInput   0x50B7D0  contrôleur clavier/keybind (pas orbite 6deg)
//   Gfx_InitDevice           0x69B9B0  proj = PerspectiveFovLH(45deg, w/h, near, far)
//   GXD_BeginScene           0x4046E3  vue  = LookAtLH(oeil, cible, up=(0,1,0)) chaque frame
//
// Ici, on expose le même comportement via un état sphérique équivalent
// (cible + distance + yaw + pitch + fov), strictement isomorphe aux rotations
// incrémentales d'origine, et on reconstruit les matrices vue/projection avec d3dx9
// exactement comme le moteur (LookAtLH gauche + PerspectiveFovLH gauche).
#pragma once

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>

namespace ts2::gfx {

class Camera {
public:
    // --- Constantes fidèles au désassemblage -------------------------------------
    // FOV vertical : g_GfxRenderer+128 = 0x42340000 = 45.0f ; g_GxdRenderer+56 = 45.0f.
    // ex-VeryOldClient: mFovY (v1 Object A @+128 ET v2 Object B @+56, note 45.0). CONFIRMED §1.2.
    static constexpr float kFovDegDefault = 45.0f;
    // Facteurs de conversion : littéraux exacts employés par le binaire.
    static constexpr float kDegToRad = 0.017453292f;      // pi/180 (0x3C8EFA35)
    static constexpr float kRadToDeg = 57.2957763671875f; // 180/pi (0x42652EE1)
    // Garde-fou d'élévation : Cam_OrbitPitch/Camera_RotateEyePitch clampent à 89.9deg
    // (Cam_SetLookAt rejette au-delà de 89.99deg). On retient la borne stricte 89.9.
    // NIVEAU MOTEUR — à ne pas confondre avec les deux bornes de CONTRÔLEUR ci-dessous ni
    // avec le 89.99 de Camera_SetEyeTarget 0x403420 (@0x40350E, cf. Gfx/GxdRenderer.h) :
    // les TROIS coexistent dans le binaire, à trois étages différents.
    static constexpr float kPitchLimitDeg = 89.9f;

    // --- Bornes d'élévation du DRAG SOURIS (Camera_MouseDragRotate 0x50AFD0) -------------
    // Deux bornes ASYMÉTRIQUES, posées par Camera_Init 0x50ABC0 (octets lus dans l'image) :
    //   +68 (0x44) <- flt_7EDA24 = 0x41F00000 = 30.0f  (`fstp dword ptr [ecx+44h]` @0x50AC37)
    //   +72 (0x48) <- flt_7A95B0 = 0x42A00000 = 80.0f  (`fstp dword ptr [edx+48h]` @0x50AC43)
    //
    // Sémantique de REVERT (et non de clamp) — cf. Camera::OrbitByMouse :
    //   cible AU-DESSUS de l'oeil (`flt_800140 >= flt_800134`) et 30.0 < |elevDeg| @0x50B26A
    //   oeil AU-DESSUS de la cible (`flt_800140 <  flt_800134`) et 80.0 < |elevDeg| @0x50B1BA
    // -> l'original RESTAURE l'oeil ET la cible sauvegardés avant l'orbite
    //    (Cam_SetLookAt @0x50B29B / @0x50B1EB + Camera_SetEyeTarget @0x50B2CF) puis SORT.
    //
    // Convention de signe résolue contre `target.y >= eye.y` : l'oeil dérivé vaut
    // eye.y = target.y + dist*sin(pitch) (cf. Camera::Eye()), donc
    //   target.y >= eye.y  <=>  sin(pitch) <= 0  <=>  pitch <= 0  -> borne 30deg
    //   target.y <  eye.y  <=>  pitch > 0                          -> borne 80deg
    static constexpr float kDragPitchLimitBelowDeg = 30.0f; // pitch <= 0 (caméra sous la cible)
    static constexpr float kDragPitchLimitAboveDeg = 80.0f; // pitch >  0 (caméra au-dessus)
    // Bornes de zoom : Camera_Init écrit +84 = 25.0 (min) et +88 = 150.0 (max).
    static constexpr float kMinDistDefault = 25.0f;
    static constexpr float kMaxDistDefault = 150.0f;
    // Sensibilités souris : Camera_Init +60 = 0.2 (yaw), +64 = 0.3 (pitch), en deg/pixel.
    static constexpr float kMouseYawSensDeg   = 0.2f;
    static constexpr float kMousePitchSensDeg = 0.3f;
    // Pas molette : Camera_Init +76 = 0.1 (unité de distance par cran).
    static constexpr float kWheelZoomStep = 0.1f;
    // Pas d'orbite au clavier : Cam_OrbitYaw(+/-6.0) dans Camera_UpdateFromInput.
    static constexpr float kKeyOrbitStepDeg = 6.0f;
    // Oeil par défaut : Gfx_InitDevice initialise oeil=(0,0,-10), cible=(0,0,0).
    static constexpr float kDefaultDistance = 10.0f;

    Camera();

    // --- Cible (point regardé) -----------------------------------------------------
    void SetTarget(const D3DXVECTOR3& t) { m_target = t; }
    void SetTarget(float x, float y, float z) { m_target.x = x; m_target.y = y; m_target.z = z; }
    const D3DXVECTOR3& Target() const { return m_target; }

    // --- Distance oeil->cible (zoom) ----------------------------------------------
    // Clampée dans [m_minDist, m_maxDist] (fidèle à Cam_ClampDistance + bornes Init).
    void  SetDistance(float d);
    float Distance() const { return m_distance; }
    void  SetDistanceLimits(float mn, float mx);
    float MinDistance() const { return m_minDist; }
    float MaxDistance() const { return m_maxDist; }

    // --- Angles d'orbite (radians) -------------------------------------------------
    // yaw : rotation autour de l'axe Y (Cam_OrbitYaw). pitch : élévation (Cam_OrbitPitch),
    // clampée à +/-kPitchLimitDeg.
    void  SetYaw(float rad)   { m_yaw = rad; }
    void  SetPitch(float rad) { m_pitch = ClampPitch(rad); }
    float Yaw() const   { return m_yaw; }
    float Pitch() const { return m_pitch; }

    // --- Projection ----------------------------------------------------------------
    void  SetFovDeg(float deg) { m_fovY = deg * kDegToRad; }
    void  SetFovRad(float rad) { m_fovY = rad; }
    float FovY() const { return m_fovY; }            // radians
    float FovDeg() const { return m_fovY * kRadToDeg; }
    // near/far : fournis par le renderer (Gfx_InitDevice params a7/a8) ; valeurs par
    // défaut indicatives, ajustables.
    void  SetClipPlanes(float nearZ, float farZ) { m_nearZ = nearZ; m_farZ = farZ; }
    float NearZ() const { return m_nearZ; }
    float FarZ()  const { return m_farZ; }

    // --- Vecteur « up » (Camera_SetUpVector 0x71CE40 ; le moteur code (0,1,0)) ------
    void SetUp(const D3DXVECTOR3& up) { m_up = up; }
    const D3DXVECTOR3& Up() const { return m_up; }

    // --- Mutateurs d'orbite --------------------------------------------------------
    // Orbite incrémentale (radians). Équivaut à Cam_OrbitYaw(dyaw)+Cam_OrbitPitch(dpitch)
    // avec le même clamp d'élévation.
    void Orbit(float dYawRad, float dPitchRad);
    // Orbite au drag souris, en pixels : applique les sensibilités 0.2/0.3 deg/px de
    // Camera_MouseDragRotate (0x50AFD0).
    void OrbitByMouse(int dxPixels, int dyPixels);
    // Dolly : rapproche (delta>0) ou éloigne l'oeil, distance re-clampée aux bornes.
    void Zoom(float delta);
    // Dolly molette : `wheelDelta` = somme des crans WM_MOUSEWHEEL (multiples de 120).
    // Un cran vers l'avant rapproche (Camera_MouseWheelZoom, 0x50B460).
    void ZoomByWheel(int wheelDelta);

    // --- Vélocités optionnelles (orbite continue touche maintenue) ------------------
    // Par défaut nulles : Update(dt) ne fait alors que resynchroniser l'oeil dérivé.
    void SetOrbitVelocity(float yawRadPerSec, float pitchRadPerSec) { m_yawVel = yawRadPerSec; m_pitchVel = pitchRadPerSec; }
    void SetZoomVelocity(float unitsPerSec) { m_zoomVel = unitsPerSec; }

    // Intègre les vélocités d'orbite/zoom sur `dt` (secondes) et recalcule l'oeil.
    void Update(float dt);

    // --- Positions dérivées --------------------------------------------------------
    // Oeil reconstruit depuis l'état sphérique (isomorphe aux rotations d'origine) :
    //   oeil = cible + dist * (cos(pitch)*sin(yaw), sin(pitch), cos(pitch)*cos(yaw))
    // Poussé dans g_GxdRenderer+712 (Camera_SetEyeTarget 0x403420). ex-VeryOldClient: mCameraEye.
    D3DXVECTOR3 Eye() const;
    // Direction avant normalisée = normalize(cible - oeil) (cf. g_GxdRenderer+736). ex-VeryOldClient: mCameraForward.
    D3DXVECTOR3 Forward() const;
    // Récupère le couple (oeil,cible) à pousser vers le renderer (Camera_SetEyeTarget).
    void GetEyeTarget(D3DXVECTOR3& eye, D3DXVECTOR3& at) const { eye = Eye(); at = m_target; }

    // --- Matrices ------------------------------------------------------------------
    // Vue gauche : D3DXMatrixLookAtLH(oeil, cible, up) — comme GXD_BeginScene (0x4046E3).
    void BuildViewMatrix(D3DXMATRIX& out) const;
    // Projection gauche : D3DXMatrixPerspectiveFovLH(fovY, aspect, near, far) —
    // comme Gfx_InitDevice (0x69BFC6). `aspect` = largeur/hauteur du back-buffer.
    void BuildProjMatrix(D3DXMATRIX& out, float aspect) const;

private:
    static float ClampPitch(float rad);
    void         ClampDistanceInternal();

    D3DXVECTOR3 m_target{0.0f, 0.0f, 0.0f}; // cible regardée (g_GxdRenderer+724) — ex-VeryOldClient: mCameraLook
    D3DXVECTOR3 m_up{0.0f, 1.0f, 0.0f};     // up figé (0,1,0) dans le moteur
    float m_distance = kDefaultDistance;    // distance oeil->cible
    float m_yaw   = D3DX_PI;                 // yaw par défaut => oeil sur -Z (0,0,-dist)
    float m_pitch = 0.0f;                    // élévation
    float m_fovY  = kFovDegDefault * kDegToRad; // FOV vertical (radians) — ex-VeryOldClient: mFovY (g_GxdRenderer+56)
    float m_nearZ = 1.0f;                    // plan proche (renderer, g_GxdRenderer+60) — ex-VeryOldClient: mNearPlane
    float m_farZ  = 15000.0f;                // plan lointain (renderer, g_GxdRenderer+64) — ex-VeryOldClient: mFarPlane
    float m_minDist = kMinDistDefault;       // borne zoom min (Camera_Init+84)
    float m_maxDist = kMaxDistDefault;       // borne zoom max (Camera_Init+88)
    float m_yawVel   = 0.0f;                  // rad/s (orbite continue optionnelle)
    float m_pitchVel = 0.0f;                  // rad/s
    float m_zoomVel  = 0.0f;                  // unités/s
};

} // namespace ts2::gfx

// Gfx/Camera.cpp — implémentation de la caméra en orbite du moteur GXD.
// Voir Camera.h pour la table des fonctions reversées.
#include "Gfx/Camera.h"

#include <cmath>

// Liaison des libs Direct3D9 / D3DX9 (DirectX SDK June 2010, x86).
#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

Camera::Camera() = default;

// -----------------------------------------------------------------------------
// Clamp d'élévation.
// Fidèle à Cam_OrbitPitch (0x69CF90) / Camera_RotateEyePitch (0x403650) :
//   if ( fabs(newPitchDeg) <= 89.9 ) appliquer, sinon ignorer.
// Ici on borne (au lieu d'ignorer) : le résultat visuel est identique — l'oeil
// ne franchit jamais le pôle, ce qui protège LookAtLH du gimbal (up figé (0,1,0)).
// -----------------------------------------------------------------------------
float Camera::ClampPitch(float rad)
{
    const float limit = kPitchLimitDeg * kDegToRad;
    if (rad > limit)  return limit;
    if (rad < -limit) return -limit;
    return rad;
}

// -----------------------------------------------------------------------------
// Bride la distance dans [min,max].
// Cam_ClampDistance (0x69CE00) ne borne que le maximum ; les bornes 25..150 de
// Camera_Init encadrent aussi le minimum via Camera_MouseWheelZoom. On applique
// les deux bornes de façon cohérente.
// -----------------------------------------------------------------------------
void Camera::ClampDistanceInternal()
{
    if (m_distance < m_minDist) m_distance = m_minDist;
    if (m_distance > m_maxDist) m_distance = m_maxDist;
}

void Camera::SetDistance(float d)
{
    m_distance = d;
    ClampDistanceInternal();
}

void Camera::SetDistanceLimits(float mn, float mx)
{
    if (mn > mx) { const float t = mn; mn = mx; mx = t; }
    m_minDist = mn;
    m_maxDist = mx;
    ClampDistanceInternal();
}

// -----------------------------------------------------------------------------
// Orbite incrémentale.
// yaw accumulé librement (Cam_OrbitYaw 0x69CEE0 est purement incrémental) ;
// pitch clampé (Cam_OrbitPitch 0x69CF90).
// -----------------------------------------------------------------------------
void Camera::Orbit(float dYawRad, float dPitchRad)
{
    m_yaw  += dYawRad;
    m_pitch = ClampPitch(m_pitch + dPitchRad);
}

// -----------------------------------------------------------------------------
// Orbite au drag souris.
// Camera_MouseDragRotate (0x50AFD0) :
//   yawDeg   = (mx - lastMx) * 0.2   (this+60)
//   pitchDeg = (my - lastMy) * 0.3   (this+64)
// puis Cam_OrbitYaw(yawDeg) / Cam_OrbitPitch(pitchDeg) (degrés -> radians en interne).
//
// BORNES 30deg/80deg DU DRAG (gap INPUT-10, comblé Passe 4 / W9) — REVERT, PAS CLAMP.
// Séquence d'origine relue :
//   1. sauvegarde oeil (0x800130/34/38) + cible (0x80013C/40/44)   @0x50B075..0x50B0A2
//   2. Cam_OrbitYaw puis Cam_OrbitPitch
//   3. v22 = Math_Dist3D(&oeil, &cible)                            @0x50B10B
//      si v22 > 0 : v32 = asin(fabs(eye.y - target.y) / v22) * 57.2957763671875  @0x50B12A..0x50B16D
//      sinon       v32 = 0                                         @0x50B174
//   4. si (target.y >= eye.y && 30.0 < |v32|) @0x50B26A  OU  (target.y < eye.y && 80.0 < |v32|)
//      @0x50B1BA  ->  Cam_SetLookAt(sauvegarde) @0x50B29B / @0x50B1EB
//                     + Camera_SetEyeTarget @0x50B2CF  ET SORTIE.
//
// ⚠ POINT DE FIDÉLITÉ : l'original restaure l'OEIL COMPLET, donc le YAW est annulé lui
//   aussi — pas seulement le pitch. On restaure bien les deux (le drag est intégralement
//   rejeté, il n'est pas « rogné » sur le seul axe fautif).
//
// Le clamp symétrique 89.9deg de Orbit()/ClampPitch (Cam_OrbitPitch 0x69CF90) reste appliqué
// EN AMONT par Orbit() : c'est un garde-fou moteur distinct, les deux se cumulent comme dans
// le binaire (Camera_MouseDragRotate appelle Cam_OrbitPitch, qui clampe déjà, PUIS teste).
//
// HORS PÉRIMÈTRE (contrôleur, cf. Gfx/CameraThirdPersonBridge non possédé) : les gardes de
// scène de 0x50AFD0 (`g_SceneMgr == 6 && g_SceneSubState == 4`, bouton `a4 == 2`) décident
// SI le drag a lieu ; elles ne changent pas ce que fait le drag.
// -----------------------------------------------------------------------------
void Camera::OrbitByMouse(int dxPixels, int dyPixels)
{
    // 1) Sauvegarde de l'état AVANT orbite (équivalent de l'oeil/cible sauvegardés).
    const float savedYaw   = m_yaw;
    const float savedPitch = m_pitch;

    // 2) Application de l'orbite (yaw libre, pitch clampé à 89.9deg par ClampPitch).
    const float yawDeg   = static_cast<float>(dxPixels) * kMouseYawSensDeg;
    const float pitchDeg = static_cast<float>(dyPixels) * kMousePitchSensDeg;
    Orbit(yawDeg * kDegToRad, pitchDeg * kDegToRad);

    // 3) Élévation résultante en degrés. Notre pitch EST l'angle que l'original recalcule
    //    par asin(|eye.y - target.y| / dist) : avec eye = target + dist*(cp*sy, sp, cp*cy),
    //    on a |eye.y - target.y| / dist = |sin(pitch)|, donc asin(...) = |pitch|.
    //    On utilise donc directement |pitch| plutôt que de refaire le trajet trigonométrique.
    const float elevDeg = std::fabs(m_pitch) * kRadToDeg;

    // 4) Test des deux bornes asymétriques puis REVERT (yaw ET pitch) + sortie.
    //    pitch <= 0 <=> target.y >= eye.y (caméra sous la cible) -> borne 30deg  @0x50B26A
    //    pitch >  0 <=> target.y <  eye.y (caméra au-dessus)     -> borne 80deg  @0x50B1BA
    const float limitDeg = (m_pitch <= 0.0f) ? kDragPitchLimitBelowDeg
                                             : kDragPitchLimitAboveDeg;
    if (limitDeg < elevDeg) {
        m_yaw   = savedYaw;   // @0x50B29B / @0x50B1EB : restauration puis sortie sèche
        m_pitch = savedPitch;
        return;
    }
}

// -----------------------------------------------------------------------------
// Dolly (zoom). delta>0 rapproche l'oeil de la cible ; re-clamp aux bornes.
// -----------------------------------------------------------------------------
void Camera::Zoom(float delta)
{
    m_distance -= delta;
    ClampDistanceInternal();
}

// -----------------------------------------------------------------------------
// Dolly molette.
// Camera_MouseWheelZoom (0x50B460) applique un pas proportionnel au delta de
// molette (this+19 = 0.1) et borne la distance dans [25,150]. WM_MOUSEWHEEL
// remonte des multiples de WHEEL_DELTA (120) ; un cran avant => zoom avant.
// -----------------------------------------------------------------------------
void Camera::ZoomByWheel(int wheelDelta)
{
    const float steps = static_cast<float>(wheelDelta) / 120.0f; // crans
    Zoom(steps * kWheelZoomStep);
}

// -----------------------------------------------------------------------------
// Update : intègre les vélocités d'orbite/zoom sur dt puis resynchronise l'état.
// Vélocités nulles par défaut => Update est un simple point d'accroche par frame
// (le contrôleur d'origine, Camera_UpdateFromInput 0x50B7D0, est piloté par
// événements : chaque touche applique un pas discret, pas d'intégration).
// -----------------------------------------------------------------------------
void Camera::Update(float dt)
{
    if (m_yawVel != 0.0f || m_pitchVel != 0.0f)
        Orbit(m_yawVel * dt, m_pitchVel * dt);
    if (m_zoomVel != 0.0f)
        Zoom(m_zoomVel * dt);
}

// -----------------------------------------------------------------------------
// Oeil dérivé de l'état sphérique.
// Isomorphe aux rotations incrémentales d'origine (Cam_OrbitYaw / Cam_OrbitPitch),
// avec la convention du moteur (LookAtLH gauche, up=(0,1,0)) :
//   yaw = PI  => oeil sur -Z, soit oeil = cible + (0,0,-dist), comme l'init
//   Gfx_InitDevice (oeil (0,0,-10), cible (0,0,0)).
// -----------------------------------------------------------------------------
D3DXVECTOR3 Camera::Eye() const
{
    const float cp = std::cos(m_pitch);
    const float sp = std::sin(m_pitch);
    const float sy = std::sin(m_yaw);
    const float cy = std::cos(m_yaw);
    return D3DXVECTOR3(
        m_target.x + m_distance * cp * sy,
        m_target.y + m_distance * sp,
        m_target.z + m_distance * cp * cy);
}

D3DXVECTOR3 Camera::Forward() const
{
    D3DXVECTOR3 fwd = m_target - Eye();
    D3DXVec3Normalize(&fwd, &fwd);
    return fwd;
}

// -----------------------------------------------------------------------------
// Matrice de vue : LookAtLH(oeil, cible, up) — cf. GXD_BeginScene (0x4046E3),
// où l'up est figé à (0,1,0). Résultat = g_GxdRenderer+748. ex-VeryOldClient: mViewMatrix.
// -----------------------------------------------------------------------------
void Camera::BuildViewMatrix(D3DXMATRIX& out) const
{
    const D3DXVECTOR3 eye = Eye();
    D3DXMatrixLookAtLH(&out, &eye, &m_target, &m_up);
}

// -----------------------------------------------------------------------------
// Matrice de projection : PerspectiveFovLH(fovY, aspect, near, far) — cf.
// Gfx_InitDevice (0x69BFC6), FOV vertical = 45deg converti en radians.
// Résultat = g_GxdRenderer+648. ex-VeryOldClient: mPerspectiveMatrix.
// -----------------------------------------------------------------------------
void Camera::BuildProjMatrix(D3DXMATRIX& out, float aspect) const
{
    D3DXMatrixPerspectiveFovLH(&out, m_fovY, aspect, m_nearZ, m_farZ);
}

} // namespace ts2::gfx

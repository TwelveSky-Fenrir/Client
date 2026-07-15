// Game/CameraWarpTick.cpp — voir Game/CameraWarpTick.h pour les EA d'origine et les notes
// de fidélité détaillées de chaque fonction.
#include "Game/CameraWarpTick.h"
#include <cmath>

namespace ts2::game {

// =====================================================================================
// 1. Caméra 3e personne
// =====================================================================================

bool Cam_SetLookAt(gfx::Camera& camera,
                    float eyeX, float eyeY, float eyeZ,
                    float targetX, float targetY, float targetZ) {
    // 0x69cd07 : oeil == cible à l'identique -> rejeté (direction nulle).
    if (eyeX == targetX && eyeY == targetY && eyeZ == targetZ)
        return false;

    const float dx = eyeX - targetX; // v11 0x69cd15
    const float dy = eyeY - targetY; // v12 0x69cd29
    const float dz = eyeZ - targetZ; // v13 0x69cd3d

    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist <= 0.0f) // garde-fou div0 (déjà exclu en pratique par le test ci-dessus)
        return false;

    // Disasm brut : fdivp (dy/dist) -> call Math_AsinFpu -> fmul flt_7BB28C(=radToDeg)
    // -> fabs -> fcomp flt_7EDB70(=89.989998). PAS un simple "ratio*radToDeg" (le Hex-Rays
    // affiché dans decompile() est trompeur sur ce point précis, cf. commentaire .h).
    const float elevationDeg = std::fabs(std::asin(dy / dist) * gfx::Camera::kRadToDeg);
    if (elevationDeg > 89.989998f) // 0x69cd8f/91
        return false;

    // yaw/pitch dérivés du couple (oeil,cible) pour le modèle sphérique isomorphe de
    // gfx::Camera (cf. tête de Gfx/Camera.h) : eye = target + dist*(cos(p)*sin(y), sin(p),
    // cos(p)*cos(y)) => dx/dist = cos(p)*sin(y), dz/dist = cos(p)*cos(y), dy/dist = sin(p).
    const float yaw   = std::atan2(dx, dz);
    const float pitch = std::asin(dy / dist);

    camera.SetTarget(targetX, targetY, targetZ);
    camera.SetDistance(dist); // cf. écart de clamp documenté dans le .h
    camera.SetYaw(yaw);
    camera.SetPitch(pitch);
    return true;
}

void InGame_InitCamera(gfx::Camera& camera, CameraFollowState& follow,
                        float selfX, float selfY, float selfZ) {
    // EA 0x52c6fe..0x52c759 : eye = self+(50,60,50), target = self+(0,10,0).
    const float eyeX = selfX + 50.0f;
    const float eyeY = selfY + 60.0f;
    const float eyeZ = selfZ + 50.0f;
    const float targetX = selfX;
    const float targetY = selfY + 10.0f;
    const float targetZ = selfZ;

    Cam_SetLookAt(camera, eyeX, eyeY, eyeZ, targetX, targetY, targetZ);
    // Camera_SetEyeTarget (0x403420, EA 0x52c7cf) recalcule EXACTEMENT le même couple
    // oeil/cible pour le pousser dans g_GxdRenderer — redondant côté binaire (deux writes du
    // même état), non reproduit séparément ici : `camera` porte déjà l'état à jour.

    // g_CamFollowDist = Math_Dist3D(g_CameraPos, flt_80013C) — cf. note fidélité du .h.
    follow.followDist = camera.Distance();

    follow.initialized    = true; // dword_1837E64 = 1 (0x52c802)
    follow.transitionFlag = 0;    // dword_1837E68 = 0 (0x52c80c)
}

// =====================================================================================
// 2. Timeout du flag "warp supprimé"
// =====================================================================================

void Warp_TickSuppressionTimeout(WarpSuppressionState& state, float gameTimeSec) {
    // EA 0x52c91f, fidèle : `if (dword_1675B00 && g_GameTimeSec - flt_1675B04 > 10.0)`.
    if (state.suppressed && (gameTimeSec - state.setAtSec) > 10.0f)
        state.suppressed = false; // 0x52c921
}

void Warp_SetSuppressed(WarpSuppressionState& state, float gameTimeSec) {
    state.suppressed = true;
    state.setAtSec = gameTimeSec;
}

// =====================================================================================
// 3. Auto-utilisation de potion
// =====================================================================================

namespace {

// Fidèle au double appel identique (belt scan) répété 3x dans le binaire pour {HP!=5,
// HP==5, MP!=5} : seul le PotionKind (donc l'ensemble de sous-types {1,2,5}/{3,4,5})
// change côté host.FindBeltPotionSlot. Renvoie true + `outSlot` rempli si un emplacement a
// été trouvé ET utilisé (host.UsePotion appelé), false sinon (aucun effet).
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
    // Garde-fous 0x5c485a.
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

    // --- Test HP (dword_1674728) -------------------------------------------------------
    const int hpSetting = Call(host.GetHpThresholdSetting);
    if (hpSetting >= 1 && hpSetting <= 5) {
        const float hp = Call(host.GetHpGauge);
        const float hpMetric = Call(host.GetHpThresholdMetric);
        bool shouldScan;
        if (hpSetting != 5) {
            // 0x5c48a7 : hp < seuil * metric / 5.
            shouldScan = hp < (static_cast<float>(hpSetting) * hpMetric / 5.0f);
        } else {
            // 0x5c49b3 : scan seulement si !(metric*0.99 <= hp), i.e. hp < metric*0.99.
            shouldScan = !(hpMetric * 0.99f <= hp);
        }
        if (shouldScan && TryUsePotion(host, PotionKind::Hp))
            return; // LABEL_70 : une seule potion par frame.
    }

    // --- Test MP (dword_167472C), atteint seulement si le test HP n'a pas déclenché ----
    const int mpSetting = Call(host.GetMpThresholdSetting);
    if (mpSetting >= 1 && mpSetting <= 5) {
        const float mp = Call(host.GetMpGauge);
        const float mpMetric = Call(host.GetMpThresholdMetric);
        bool shouldScan;
        if (mpSetting == 5) {
            // 0x5c4be5 : scan si metric*0.99 > mp.
            shouldScan = (mpMetric * 0.99f) > mp;
        } else {
            // 0x5c4ad9 : mp < seuil * metric / 5.
            shouldScan = mp < (static_cast<float>(mpSetting) * mpMetric / 5.0f);
        }
        if (shouldScan)
            TryUsePotion(host, PotionKind::Mp); // LABEL_70 ou retour silencieux si rien trouvé.
    }
}

// =====================================================================================
// 4. Cible de requête en attente (clan/faction) — ex "nom de guilde/groupe actif"
//    (renommage de fidélité, cf. note détaillée dans Game/CameraWarpTick.h section 4)
// =====================================================================================

bool HasPendingTargetRequest(const std::string& reqTargetSub2, const std::string& reqTargetSub1) {
    return !reqTargetSub2.empty() || !reqTargetSub1.empty();
}

} // namespace ts2::game

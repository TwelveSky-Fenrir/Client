// Gfx/CameraThirdPersonBridge.cpp — implémentation. Voir CameraThirdPersonBridge.h pour la
// doc complète (périmètre, signature de câblage attendue côté SceneManager).
#include "Gfx/CameraThirdPersonBridge.h"
#include "Game/CameraWarpTick.h"       // InGame_InitCamera, CameraFollowState (déjà écrits)
#include "Game/AnimationTick.h"        // Camera_UpdateCollision, ICameraCollisionQueries, CameraCollisionHost
#include "World/WorldIntegration.h"    // world::WorldAssets — fournisseur des requêtes de collision (WG-02)

namespace ts2::gfx {

namespace {

// État de cadrage caméra (dword_1837E64/dword_1837E68/g_CamFollowDist d'origine, cf.
// Game/CameraWarpTick.h::CameraFollowState) : une SEULE caméra 3e personne active à la fois
// côté client -> état possédé par ce module.
game::CameraFollowState g_FollowState{};

// Host free-look (Camera_UpdateCollision) : aucun système UI de bascule free-look côté
// ClientSource -> tous les std::function restent nuls (dégradation propre fidèle).
const game::CameraCollisionHost g_NoFreeLookHost{};

// Oracle de collision RÉEL (WG-02, Camera_UpdateCollision 0x538580) : traduit les 4 requêtes
// de l'interface game::ICameraCollisionQueries vers world::WorldAssets. Chaque méthode vise le
// slot PROUVÉ par le désassemblage du site d'appel (report §1.3) : le sweep -> .WG (slot 0 =
// g_GameWorld @0x5387b9) ; le point bloqué -> Main+WJ (0x540da0) ; le sol -> Main (@0x5388f4).
class WorldCameraCollision final : public game::ICameraCollisionQueries {
public:
    explicit WorldCameraCollision(const world::WorldAssets& assets) : assets_(assets) {}

    // Terrain_SweepSphereSegment 0x69a1f0 (radius 2.5) contre le TERRAIN .WG.
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
    // MapColl_LineOfSightObjects 0x696fc0 (non porté -> false, cf. WorldIntegration.cpp).
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
    // `dt` non consommé ici : NI InGame_InitCamera NI Camera_UpdateCollision n'intègrent de
    // temps dans le binaire d'origine (cf. tête de CameraThirdPersonBridge.h).
    (void)dt;

    // Position du joueur local. GameWorld::Self() n'est pas const (peut insérer un slot par
    // défaut si `players` est vide) -> accès en lecture seule via une référence non-const locale.
    game::GameWorld& mutableWorld = const_cast<game::GameWorld&>(world);
    const game::PlayerEntity& self = mutableWorld.Self();

    if (justEnteredInGame) {
        // Cadrage d'entrée en InGame (Scene_InGameUpdate case 3, EA 0x52C6EF) — déjà écrit
        // (Game/CameraWarpTick.h), réutilisé tel quel : AUCUNE duplication.
        game::InGame_InitCamera(camera, g_FollowState, self.x, self.y, self.z);
    }

    // Suivi de cible + collision terrain/objets (Camera_UpdateCollision 0x538580). WG-02 :
    // oracle RÉEL quand la zone est chargée (worldAssets non nul) -> la caméra ne traverse plus
    // le décor ; sinon nullptr = comportement fidèle « zone non chargée » (bras précédent, sans
    // correction). Free-look désactivé (aucun système UI de bascule côté ClientSource).
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

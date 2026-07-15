// Gfx/CameraThirdPersonBridge.cpp — implémentation. Voir CameraThirdPersonBridge.h pour la
// doc complète (périmètre, signature de câblage attendue côté SceneManager).
#include "Gfx/CameraThirdPersonBridge.h"
#include "Game/CameraWarpTick.h" // InGame_InitCamera, CameraFollowState (déjà écrits)
#include "Game/AnimationTick.h"  // Camera_UpdateCollision, CameraCollisionHost (déjà écrits)

namespace ts2::gfx {

namespace {

// État de cadrage caméra (dword_1837E64/dword_1837E68/g_CamFollowDist d'origine, cf.
// Game/CameraWarpTick.h::CameraFollowState) : une SEULE caméra 3e personne active à la fois
// côté client (pas de multi-instance) -> état possédé par ce module plutôt qu'un paramètre
// supplémentaire dans la signature de TickThirdPersonCamera imposée par la mission.
game::CameraFollowState g_FollowState{};

// Host vide pour Camera_UpdateCollision : aucun câblage réel disponible côté ClientSource à
// ce jour (pas de système de free-look UI) — tous les std::function restent null, dégradation
// propre fidèle (cf. Game/AnimationTick.h::CameraCollisionHost).
const game::CameraCollisionHost g_NoFreeLookHost{};

} // namespace

void TickThirdPersonCamera(Camera& camera, const game::GameWorld& world,
                            float dt, bool justEnteredInGame) {
    // `dt` non consommé ici : NI InGame_InitCamera NI Camera_UpdateCollision n'intègrent de
    // temps dans le binaire d'origine (cf. tête de CameraThirdPersonBridge.h). Réservé pour un
    // futur branchement Camera::Update(dt) (orbite/zoom pilotés par vélocité souris/clavier),
    // hors périmètre de cette mission.
    (void)dt;

    // Position du joueur local. GameWorld::Self() n'est pas const (peut insérer un slot par
    // défaut si `players` est vide) -> accès en lecture seule via une référence non-const
    // locale, même pattern que Game/ItemPickupSystem.cpp::IsWithinPickupRange.
    game::GameWorld& mutableWorld = const_cast<game::GameWorld&>(world);
    const game::PlayerEntity& self = mutableWorld.Self();

    if (justEnteredInGame) {
        // Cadrage d'entrée en InGame (Scene_InGameUpdate case 3, EA 0x52C6EF) — déjà écrit et
        // fonctionnel (Game/CameraWarpTick.h), réutilisé tel quel : AUCUNE duplication de
        // cette logique ici.
        game::InGame_InitCamera(camera, g_FollowState, self.x, self.y, self.z);
    }

    // Suivi de cible + collision terrain/objets de la frame courante (Camera_UpdateCollision
    // 0x538580) — déjà écrit et fonctionnel (Game/AnimationTick.h), réutilisé tel quel.
    // Free-look désactivé et aucun oracle de collision câblés par défaut (cf. tête de fichier
    // pour la justification et le point d'extension futur).
    game::Camera_UpdateCollision(camera, world, /*freeLookActive=*/false, /*camMode=*/0,
                                  /*collision=*/nullptr, g_NoFreeLookHost);
}

} // namespace ts2::gfx

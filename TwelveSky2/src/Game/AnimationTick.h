// Game/AnimationTick.h — SYSTEME ANIMATION/COLLISION : réécriture C++ fidèle de 4
// fonctions décompilées via idaTs2 (Hex-Rays), câblées sur game::g_World (Game/GameState.h)
// et Game/ActionStateMachine.h. Module Game/*.h/.cpp AUTONOME (mission dédiée) : n'édite PAS
// Scene/SceneManager.*, l'agent de consolidation câble les hooks ci-dessous sur l'existant.
//
// Fonctions couvertes (EA d'origine, imagebase 0x400000) :
//   - Player_UpdateLocalAnim        0x5321D0 — anim/FX du joueur LOCAL uniquement (this =
//     g_LocalPlayerSheet 0x1685748). RECOMPTÉ EXHAUSTIVEMENT bloc-par-bloc contre une
//     ré-décompilation fraîche (audit 2026-07-14) : 85 blocs conditionnels au total =
//     75 lignes génériques (table kMorphRows, vérifiée 1:1 par flagAddr avec le binaire)
//     + 3 blocs "pulse" cadencés this+8%6 (0x1675BAC/BB0, BCC/BD0, BD4/BD8)
//     + 3 blocs spéciaux indexés par g_SelfMorphNpcId (0x1675BA4/BDC/BE4)
//     + 1 bloc indexé par g_LocalElement (0x1675D98/DA8)
//     + 1 bloc pulse final (0x1675E90/E98)
//     = 83 blocs "timer" au sens strict (le chiffre "~83" d'un rapport antérieur est donc
//     CONFIRMÉ EXACT, pas sous-estimé), auxquels s'ajoutent
//     + 1 bloc ambiance/BGM (replay 900s, non-timer)
//     + 1 vérification finale one-shot (g_SelfMorphNpcId==196 && dword_1685E10==1 -> warp)
//     = 85 blocs au total, TOUS présents et vérifiés dans Player_UpdateLocalAnim ci-dessous
//     (aucun bloc manquant détecté). Entièrement DATA-DRIVEN : reproduits via une table
//     statique (Player_UpdateLocalAnim dans AnimationTick.cpp), chaque timer stocké aux
//     ADRESSES D'ORIGINE via game::g_Client.Var/VarF (Game/ClientRuntime.h, même convention
//     que Game/MapWarp.cpp).
//   - Char_UpdateAnimationFrame     0x571880 — fait avancer la FSM d'anim/action d'UNE
//     entité (joueur distant OU monstre). Le cœur (détection de contact, interruption de
//     cast, primitives de tick générique) est DÉJÀ écrit dans Game/ActionStateMachine.h/.cpp
//     (ActionFsm) : cette fonction construit un ActionFsm TRANSITOIRE depuis
//     game::CharAnimState (persisté dans PlayerEntity::anim/MonsterEntity::anim, cf.
//     GameState.h — champ AJOUTÉ par cette mission), l'utilise, puis recopie le résultat.
//     Complète l'orchestration avec les blocs NON couverts par ActionFsm : timers FX
//     secondaires (data-driven, même table-engine que Player_UpdateLocalAnim), rotation
//     faciale lissée (540°/s, byte-exact, AUCUNE dépendance asset), aura spéciale, marque de
//     guilde, requête d'arrêt AutoPlay. Le SWITCH terminal (0x5727BF, 55 handlers Char_*/
//     Combat_TickAttackState, chacun piloté par une durée d'anim ASSET, hors périmètre) est
//     exposé via UN SEUL hook opaque `TickStateHandler` — même politique que
//     Game/ActionStateMachine.h::IAnimFrameOracle (cf. tête de ce fichier pour la
//     justification : rendu 3D/motion = hors périmètre gameplay).
//   - Camera_UpdateCollision        0x538580 — caméra 3e personne : recalcule l'œil en
//     maintenant le même bras (œil-cible) qu'à la frame précédente autour de la nouvelle
//     cible (joueur local, y+10), puis corrige par collision terrain (Terrain_SweepSphere
//     Segment)/objets (MapColl_LineOfSightObjects + MapColl_GetGroundHeight, stepping au
//     sol). Opère sur gfx::Camera (Gfx/Camera.h) et réutilise Cam_SetLookAt(déjà écrit dans
//     Game/CameraWarpTick.h) pour le calage final — PAS de duplication de cette fonction.
//   - MapColl_UpdateObjectAnim      0x694A00 — anim des objets de collision de carte
//     (sous-objets animés à 15 fps + émetteurs de particules attachés). Le "this" d'origine
//     (objet de collision de zone) N'A PAS d'équivalent dans GameState.h (propriété du futur
//     World/WorldMap) : modélisé ici en état AUTONOME (MapCollisionObjectAnimState), fourni
//     par l'appelant (pas dans g_World).
//
// HORS PÉRIMÈTRE (données d'assets 3D/motion, réseau, rendu FX — cf. même politique que
// Game/ActionStateMachine.h et Game/InGameTickFlow.h) : exposé via interfaces "oracle"
// (IMorphModelOracle, ICameraCollisionQueries) et hosts `std::function` opaques, EA
// d'origine documentée à chaque point d'usage. Un hook/oracle nul dégrade proprement
// (timer ne complète jamais / pas de correction de collision / no-op), ne bloque JAMAIS.
//
// Autonomie : dépend de Game/GameState.h (CharAnimState, GameWorld — champ `anim` AJOUTÉ par
// cette mission), Game/ActionStateMachine.h (ActionFsm, réutilisé tel quel), Game/MapWarp.h
// (BeginWarpToFactionTown, réutilisé tel quel), Game/ClientRuntime.h (g_Client.Var/VarF,
// échappatoire globals "longue traîne"), Game/CameraWarpTick.h + Gfx/Camera.h (Cam_SetLookAt,
// réutilisé tel quel). N'inclut PAS Scene/SceneManager.h ni Net/*.
#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include "Game/GameState.h"
#include "Game/ActionStateMachine.h"
#include "Gfx/Camera.h"

namespace ts2::game {

// =====================================================================================
// 1. Player_UpdateLocalAnim 0x5321D0
// =====================================================================================

// Fidèle à ModelObj_GetSubObjectCount(tableAddr, 0) : nombre de sous-objets/frames d'un
// modèle de morph identifié par son ADRESSE D'ORIGINE (unk_Bxxxxxx, éventuellement décalée
// par 150368*modelIndex + 75184*modelVariant pour les tables paramétrées — cf. usage dans
// Char_UpdateAnimationFrame). Donnée d'asset 3D, HORS PÉRIMÈTRE (cf. tête de fichier) :
// l'implémentation réelle vit dans la couche rendu/assets. oracle==nullptr -> les timers
// avancent mais ne complètent jamais (dégradation propre, cf. Duration() dans le .cpp).
class IMorphModelOracle {
public:
    virtual ~IMorphModelOracle() = default;
    virtual int GetSubObjectCount(uint32_t tableAddr) const = 0;
};

struct LocalAnimTickHost {
    // World_LoadCurrentZoneModel((char*)dword_14A883C, reason) 0x4dd6e0 — recharge le
    // calque de modèle de zone (mode ville/pvp/monture/etc.) quand un timer de morph se
    // termine. Cible réelle : world::WorldMap::LoadCurrentZoneModel(int) déjà écrit
    // (World/WorldMap.h), instance possédée par SceneManager — câblage laissé à l'agent de
    // consolidation (host pattern, comme InGameTickFlow.h).
    std::function<void(int reason)> LoadCurrentZoneModel;
    // World_IsPointOnGround(pos) — hors périmètre (collision/hauteur de terrain).
    std::function<bool(float x, float y, float z)> IsPointOnGround;
    // g_BgmEnabled == 1 (option utilisateur musique) + Snd_Play3D(0,100,0) si vrai — relance
    // l'ambiance toutes les 900s (15 min). IsBgmEnabled nul -> false (pas de replay).
    std::function<bool()> IsBgmEnabled;
    std::function<void()> PlayAmbientBgm;
};

// Player_UpdateLocalAnim 0x5321D0. `dt` = a3 d'origine (delta-temps de la frame, 1/30s
// @30 FPS). Opère UNIQUEMENT sur le joueur LOCAL (world.Self(), world.self.element pour la
// résolution de faction) — fidèle à l'original qui n'a pas de paramètre d'entité (this =
// singleton g_LocalPlayerSheet). Les 83 timers eux-mêmes vivent dans game::g_Client.Var/
// VarF, PAS dans GameWorld (ce sont des globals d'origine, pas des champs par-entité) :
// aucune modification de GameState.h nécessaire pour cette fonction.
void Player_UpdateLocalAnim(GameWorld& world, float dt,
                             const IMorphModelOracle* oracle, const LocalAnimTickHost& host);

// =====================================================================================
// 2. Char_UpdateAnimationFrame 0x571880
// =====================================================================================

struct CharAnimTickHost {
    // GuildMark_RegisterName(this+40) — hors périmètre (registre de rendu de marque de
    // guilde flottante). Déclenché quand anim.hasPendingGuildMark==true (entity+68==1).
    std::function<void()> RegisterGuildMark;

    // Aura spéciale (entity+180==2160 -> tente d'attacher, cf. g_FxAuraCount/dword_17D06F4
    // hors périmètre, propriété du futur FxAuraSystem — même tableau que
    // Game/InGameTickFlow.h étape 10). HasFreeAuraSlot nul -> false (jamais attachée).
    std::function<bool()> HasFreeAuraSlot;
    std::function<void()> AttachSpecialAura; // Fx_AttachSpecialAura(this) 0x?

    // g_PendingStopRequest==1 && isLocalSimulation && state==Move(1) -> clear global +
    // Net_SendOp95(&g_AutoPlayMgr, 2). GetPendingStopRequest/ClearPendingStopRequest sont
    // des globals PARTAGÉS (pas par-entité) : l'appelant les branche sur son propre état
    // AutoPlay (cf. Game/AutoPlaySystem.h). Nuls -> jamais déclenché.
    std::function<bool()> GetPendingStopRequest;
    std::function<void()> ClearPendingStopRequest;
    std::function<void()> SendAutoPlayStopAck;
};

// Résultat exposé pour le SEUL bloc "détection de contact" (délégué à
// ActionFsm::UpdateContactDetection, déjà écrit) — voir Game/ActionStateMachine.h pour la
// sémantique complète de chaque champ.
struct CharAnimTickResult {
    bool                contactFiredThisTick = false;
    CombatActionRequest lastAction{};
    bool                pendingAoECast    = false;
    bool                pendingProjectile = false;
};

// Char_UpdateAnimationFrame 0x571880. `anim` = état persistant de l'entité (PlayerEntity::
// anim / MonsterEntity::anim, GameState.h). `actor`/`hitOracle` = contexte combat + table
// de frames d'événement, transmis TELS QUELS à ActionFsm::UpdateContactDetection (cf.
// Game/ActionStateMachine.h — hitOracle peut être nullptr, dégrade proprement).
// `isLocalSimulation` = !a2 d'origine (true = entité pilotée localement, déclenche
// réellement les envois réseau via lastAction) ; `isSelf` = comparaison *(this+4)==
// dword_1687238[0] d'origine (true seulement pour l'entité 0 = soi-même) ;
// `pendingCastInterrupt` = condition externe déjà évaluée par l'appelant (g_InvDirtyEnable
// ==1 && (g_AutoHuntFuelA>0||g_AutoHuntFuelB>0), globals hors périmètre de ce module — cf.
// Game/ActionStateMachine.h::ActionFsm::pendingCastInterrupt). `modelOracle` = même oracle
// que Player_UpdateLocalAnim (tables de timers FX secondaires). `stateHandler`, s'il est
// fourni, est appelé APRÈS l'interruption de cast avec l'état COURANT (post-interruption)
// pour faire avancer le switch terminal (0x5727BF, 55 handlers asset-driven, hors
// périmètre) — nul -> aucune progression d'anim au-delà de ce que ce module couvre déjà
// (contact/interrupt/FX/rotation), la FSM reste "gelée" sur son état courant.
void Char_UpdateAnimationFrame(CharAnimState& anim, const CombatActorState& actor,
                                const GameWorld& world, const IAnimFrameOracle* hitOracle,
                                bool isLocalSimulation, bool isSelf, bool pendingCastInterrupt,
                                float dt, const IMorphModelOracle* modelOracle,
                                const std::function<void(CharActionState state, float dt)>& stateHandler,
                                const CharAnimTickHost& host, CharAnimTickResult& outResult);

// =====================================================================================
// 3. Camera_UpdateCollision 0x538580
// =====================================================================================

// Interfaces de collision terrain/objets — HORS PÉRIMÈTRE (géométrie de monde, propriété
// de World/WorldMap.*/Gfx/WorldGeometryRenderer.h, pas de Game/GameState.h). Un oracle nul
// désactive TOUTE correction de collision (la caméra suit alors le bras précédent sans
// jamais se rapprocher du mur/décor) — dégrade proprement, ne bloque jamais.
class ICameraCollisionQueries {
public:
    virtual ~ICameraCollisionQueries() = default;
    // Terrain_SweepSphereSegment(&from,&to,2.5,outHit) — vrai si le segment croise le
    // terrain avant `to` ; `outHit` = point d'impact (remplace `to`).
    virtual bool SweepSphereSegment(const D3DXVECTOR3& from, const D3DXVECTOR3& to,
                                     float radius, D3DXVECTOR3& outHit) const = 0;
    // World_IsPointBlocked(p) — vrai si p est à l'intérieur d'un solide de collision.
    virtual bool IsPointBlocked(const D3DXVECTOR3& p) const = 0;
    // MapColl_LineOfSightObjects(&from,&to) — vrai si la ligne from->to nécessite le repli
    // "ground-height stepping" ci-dessous (mobilier/mur intercepté par un objet de map).
    virtual bool LineOfSightBlockedByObjects(const D3DXVECTOR3& from, const D3DXVECTOR3& to) const = 0;
    // MapColl_GetGroundHeight(x,z,&out,0,0.0,0,true) — les 4 derniers arguments d'origine
    // (index de calque/seuil de pente/flags) sont FIXES sur cet unique site d'appel, donc
    // non paramétrés ici. Vrai si (x,,z) est obstrué à cette hauteur.
    virtual bool IsGroundBlocked(float x, float z) const = 0;
};

struct CameraCollisionHost {
    // Mode free-look (g_CamFreeLook && g_CamMode==3) : cherche l'entité dont le nom ==
    // g_CamFollowName (Crt_Strcmp sur unk_168727C+908*i, tableau parallèle indexé par
    // g_EntityArray — HORS PÉRIMÈTRE, propriété du futur EntityManager). Nul -> traité
    // comme "aucune cible trouvée" (comportement fidèle : `return` anticipé du binaire).
    std::function<bool(D3DXVECTOR3& outPos)> FindFreeLookFollowTarget;
    // Net_SendCmd_251(pos) — notifie le serveur du nouveau point de suivi caméra.
    std::function<void(const D3DXVECTOR3& pos)> SendFollowCameraUpdate;
};

// Camera_UpdateCollision 0x538580. Lit/écrit `camera` (œil/cible caméra 3e personne, cf.
// Gfx/Camera.h) : `camera.Eye()`/`camera.Target()` = flt_1687330-dérivé/g_CameraPos-dérivé
// de la frame PRÉCÉDENTE ; `camera.Distance()` = approximation de g_CamFollowDist (MÊME
// convention d'approximation que Game/CameraWarpTick.h::InGame_InitCamera, cf. sa note de
// fidélité — g_CamFollowDist n'est lu nulle part d'autre dans ClientSource à ce jour).
// `freeLookActive`/`camMode` = g_CamFreeLook/g_CamMode (état caméra UI, PAS dans GameWorld).
// Ne modifie PAS `camera` si le binaire aurait fait un `return` anticipé (gardes fidèles :
// morph "boutique" 194 hors free-look ; cible free-look introuvable).
void Camera_UpdateCollision(gfx::Camera& camera, const GameWorld& world,
                             bool freeLookActive, int camMode,
                             const ICameraCollisionQueries* collision,
                             const CameraCollisionHost& host);

// =====================================================================================
// 4. MapColl_UpdateObjectAnim 0x694A00
// =====================================================================================

// Sous-objet animé (record 36o d'origine, *(this+27) array, compte *(this+26)).
struct MapAnimSubObject {
    int32_t modelIndex = 0; // record+0 — clé de résolution du modèle animé (hors périmètre)
    float   frame       = 0.0f; // record+28 — position dans l'anim (frames @ 15 fps)
};

// Émetteur de particules (record 76o d'origine, *(this+32) array, compte *(this+31)).
struct MapParticleEmitter {
    int32_t particleDefIndex = 0; // record+0 — index dans la table de défs (hors périmètre)
    bool    initialized      = false; // record+28 — PROPRIÉTÉ EXTERNE : jamais écrit par
                                       // cette fonction dans le binaire (positionné ailleurs,
                                       // au spawn de l'émetteur) ; lu seulement ici.
};

// État autonome de l'objet de collision de carte (le "this" d'origine, PAS dans
// game::GameWorld — propriété du futur World/WorldMap, cf. tête de fichier).
struct MapCollisionObjectAnimState {
    bool active = false; // *(this+1) (idx1) — validité de l'enregistrement
    int  mode   = 0;     // *(this+2) (idx2) — doit valoir 1 pour que le tick s'exécute
    std::vector<MapAnimSubObject>   animObjects;
    std::vector<MapParticleEmitter> particleEmitters;
};

class IMapObjectAnimOracle {
public:
    virtual ~IMapObjectAnimOracle() = default;
    // ModelObj frame count pour un objet de collision animé — table de modèles hors
    // périmètre (assets), résolue via *(this+24)[modelIndex]+8 -> ...+252 dans le binaire.
    virtual int GetModelFrameCount(int modelIndex) const = 0;
    // Particle_Init(defTable + 232*defIndex) — démarre l'émetteur (record pas encore
    // `initialized`). Mutable (démarre réellement l'émetteur côté FX).
    virtual void InitParticle(int particleDefIndex) = 0;
    // Particle_UpdateEmit(dt, paramsA, paramsB) — hors périmètre (FX), un hook opaque par
    // émetteur déjà initialisé (params bruts entity+4/+16 non modélisés ici).
    virtual void UpdateParticle(int index, float dt) = 0;
};

// MapColl_UpdateObjectAnim 0x694A00. `dt` = a3 d'origine. Le paramètre a2 d'origine vaut
// TOUJOURS 15.0 au seul site d'appel connu (InGameTickFlow.h, MainTick étape 4,
// `MapColl_UpdateObjectAnim(15.0,dt)`) — figé ici en constante (kAnimFps) plutôt qu'en
// paramètre pour coller à l'usage réel. `oracle` peut être nullptr : les sous-objets
// avancent mais ne rebouclent jamais (frame croît sans borne) et les particules ne sont ni
// initialisées ni mises à jour — dégradation propre.
// IDENTITÉ DU "this" D'ORIGINE (précision apportée par audit 2026-07-14, disasm brut du site
// d'appel 0x52c946) : `mov ecx, offset dword_14A883C` — le "this" est le MÊME global fixe
// dword_14A883C que celui passé en premier argument à World_LoadCurrentZoneModel partout
// dans Player_UpdateLocalAnim (0x5321D0, cf. plus haut). C'est donc un SINGLETON unique
// (probablement le "modèle/objet de zone courante"), PAS une instance par sous-objet de
// carte — utile si un futur World/WorldMap veut exposer une seule instance globale de
// MapCollisionObjectAnimState plutôt qu'un tableau.
void MapColl_UpdateObjectAnim(MapCollisionObjectAnimState& obj, float dt,
                               IMapObjectAnimOracle* oracle);

} // namespace ts2::game

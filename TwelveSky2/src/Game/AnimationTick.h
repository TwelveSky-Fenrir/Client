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
//     entité du tableau JOUEURS (g_EntityArray, stride 908) — soi-même OU un joueur
//     distant.
//
//     ///// CORRECTION FACTUELLE — Passe 4 / vague W7, front motion-anim (2026-07-16) /////
//     La rédaction précédente disait « (joueur distant OU MONSTRE) ». C'est FAUX : AUCUN
//     monstre ne passe jamais par 0x571880. Preuve, désassemblage de l'unique appelant
//     Scene_InGameUpdate 0x52C600 (relu instruction par instruction cette session) — QUATRE
//     familles d'entités, DISJOINTES, chacune avec sa propre fonction de tick :
//       @0x52c96d  Char_UpdateAnimationFrame(g_EntityArray, 0, dt)           self      (stride 908)
//       @0x52c9fd  Char_UpdateAnimationFrame(&g_EntityArray[908*j], j, dt)   distants  (stride 908)
//       @0x52ca4c  Npc_RenderSlotTick(&g_NpcRenderArray[88*k], k, dt)        PNJ décor (stride  88)
//       @0x52cad6  Char_Update(&dword_1766F74[280*m], m, dt)                 MONSTRES  (stride 280)
//     La FSM d'animation des MONSTRES est donc Char_Update 0x581E10 (switch @0x5822D3, 9
//     handlers Char_MotionTick_*), portée par Monster_DispatchMotionTick ci-dessous (§5) ;
//     celle des PNJ de décor est Npc_RenderSlotTick 0x5803A0, portée par ZoneNpc_TickAnim
//     (§6). Le `TODO [ancre 0x571880]` qui figurait dans Scene/WorldRenderer.cpp pour
//     l'animType des monstres pointait, pour la même raison, la mauvaise fonction.
//
//     Le cœur (détection de contact, interruption de
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

// =====================================================================================
// 5. FSM d'animation MONSTRE — les 9 Char_MotionTick_* + leur dispatch @0x5822D3
//    (Passe 4 / vague W7, front motion-anim — gaps as-motion-01 / as-motion-02)
// =====================================================================================
//
// POURQUOI CE BLOC EXISTE. Les monstres étaient dessinés avec `animType` figé à 0 (idle) et
// une horloge GLOBALE partagée : toutes les entités animées en phase, aucune ne changeant
// jamais d'animation. La cause racine n'était PAS le renderer mais un hook jamais assigné :
// `EntityLifecycleTickHost::DispatchMotionTick` (Game/EntityLifecycleTick.h:187) est
// déclaré, appelé par UpdateMonster (EntityLifecycleTick.cpp:153) ... et personne ne
// l'implémentait — les 9 handlers étaient classés « HORS PÉRIMÈTRE ». Résultat :
// `MonsterTickExt::motionState` / `::animFrame` ne bougeaient JAMAIS.
// => Conséquence à connaître avant de câbler : brancher le renderer sur
// g_MonsterTickExt[i].animFrame SANS ce portage donnerait un curseur constamment 0, donc des
// monstres TOTALEMENT FIGÉS — strictement pire que l'horloge globale. Les deux vont ensemble.
//
// VÉRITÉ TERRAIN (IDA, re-prouvée bloc par bloc cette session) :
//   Char_Draw 0x5805C0 @0x580770 : animType = *((_DWORD*)this + 6)  = slot monstre +24
//   Char_Draw 0x5805C0 @0x580828 : curseur  = *((float*)this + 7)   = slot monstre +28
//   Char_Update 0x581E10 @0x5822D3 : switch sur CE MÊME slot+24, 9 cas — et ces 9 valeurs
//     {0,1,3,4,5,7,8,0xC,0x13} sont EXACTEMENT le set valide de Model_GetNpcMotionSlot
//     0x4E5960 @0x4e59a4 (preuve croisée : slot+24 EST bien l'index d'animation du dessin).
//   Tous les handlers : `frame += dt*30`, frameCount = Model_GetMotionFrameCount 0x4E5A70
//     (MÊME slot que le dessin -> count == palette.frameCount, cf. Gfx/MotionCache.h).
//     Le wrap est une SOUSTRACTION UNIQUE (jamais un modulo).
//
// Correspondance état -> sémantique de fin (EA du handler, EA de la transition) :
//   0  ToIdle    0x582D40 : frame>=count -> state=1, frame=0            @0x582d99/@0x582da5
//   1  Loop      0x582DB0 : frame>=count -> frame -= count              @0x582e10
//   3  MoveA     0x582E20 : wrap @0x582e80 + StepTowardTarget(vit. +384) -> arrivée/échec
//                           => state=1, frame=0                         @0x582ebd/@0x582ed7
//   4  MoveB     0x582EF0 : idem MoveA, vitesse def+388                 @0x582f8d/@0x582fa7
//   5  AttackA   0x582FC0 : frame>=count -> state=1, frame=0, +108=0    @0x583019..@0x58302b
//   7  AttackB   0x583040 : idem AttackA                                @0x583099..@0x5830ab
//   8  Hit       0x5830C0 : frame>=count -> state=1, frame=0            @0x583119/@0x583125
//   0xC Knockback 0x583130 : frame>=count -> frame = count-1 (gel)      @0x583284
//   0x13 Death   0x5832E0 : frame>=count -> frame = count-1 (gel)       @0x583345
//
// L'état porteur est `game::g_MonsterTickExt[monsterIndex]` (Game/EntityLifecycleTick.h) :
// `.motionState` = slot+24, `.animFrame` = slot+28, `.attackWindupMode` = slot+108. Ces trois
// champs EXISTENT DÉJÀ et portent les bons offsets — rien à ajouter à MonsterTickExt (fichier
// non possédé par ce front). `MonsterEntity::anim` (GameState.h:225) n'est PAS utilisé ici :
// c'est un CharAnimState calqué sur des offsets JOUEUR (entity+244/+248), mort pour les
// monstres (cf. avertissement Game/AutoTargetCombatGate.h:106-112).

// Nombre de frames d'une anim, par famille d'entité. Miroir de Model_GetMotionFrameCount
// 0x4E5A70 (monstre) / Model_GetWeaponEffectFrameCount 0x4E5A40 (PNJ décor) : tous deux
// résolvent le MÊME slot que le dessin, donc le count est celui de la palette de dessin.
// Donnée d'asset (motion) => HORS PÉRIMÈTRE de Game/*, exposée en oracle — même politique que
// IMorphModelOracle/IAnimFrameOracle. Implémenté par Scene/WorldRenderer.cpp (seul détenteur
// du MotionCache), exposé via ts2::WorldMotionFrameCountOracle() (Scene/WorldRenderer.h).
// Renvoie 0 si le slot ne résout pas (fichier absent/borne dépassée) — traité comme "durée
// inconnue" par les handlers (cf. dégradation ci-dessous).
class IMotionFrameCountOracle {
public:
    virtual ~IMotionFrameCountOracle() = default;
    // Model_GetMotionFrameCount 0x4E5A70(g_ModelMotionArray, MONSTER_INFO.kindIndexP1-1, animType).
    // monsterDefId = MonsterEntity::body[0] (id brut, sans -1 — même convention que
    // Game/EntityManager.cpp::ResolveMobDef ; le -1 sur kindIndexP1 est fait en interne).
    virtual int GetMonsterMotionFrameCount(uint32_t monsterDefId, int animType) const = 0;
    // Model_GetWeaponEffectFrameCount 0x4E5A40(g_ModelMotionArray, NpcDefRecord::fieldE-1, animType),
    // indexé comme game::ZoneNpcs() (Game/StaticNpcLoader.h).
    virtual int GetZoneNpcMotionFrameCount(int zoneNpcIndex, int animType) const = 0;
};

// Dépendances des états Move (3/4) sortant du périmètre "animation" : déplacement réel de
// l'entité contre la collision de carte. Hook nul -> cf. note de dégradation sur
// Monster_DispatchMotionTick.
struct MonsterMotionTickHost {
    // MapColl_StepTowardTarget 0x6974C0(&dword_14A88E4, this+32 /*pos*/, this+44 /*cible*/,
    // speed, dt, &outArrived) — déplace le monstre vers sa cible. Renvoie false si le pas a
    // ÉCHOUÉ (bloqué) ; outArrived=true quand la cible est atteinte. Les DEUX cas repassent
    // l'état à Loop(1) dans le binaire (@0x582ebd échec, @0x582ed7 arrivée).
    // speed = MONSTER_INFO+384 (MoveA) / +388 (MoveB) — lu par l'implémenteur du hook, qui
    // seul a accès au record de def et à la géométrie de collision.
    std::function<bool(int monsterIndex, bool moveB, float dt, bool& outArrived)> StepTowardTarget;
};

// Char_Update 0x581E10, switch terminal @0x5822D3 UNIQUEMENT (les blocs amont — timers d'aura,
// fenêtre de coup, physique de chute — sont déjà portés par Game/EntityLifecycleTick.cpp::
// UpdateMonster, qui appelle ce dispatch en dernier via host.DispatchMotionTick).
//
// DÉGRADATION (aucun hook/oracle n'est obligatoire, rien ne bloque jamais) :
//   - oracle nul OU frameCount<=0 : le curseur AVANCE (frame += dt*30) mais aucune borne n'est
//     connue -> aucun wrap, aucune transition d'état émise. Sûr : on ne fabrique pas une durée.
//   - StepTowardTarget nul (états 3/4) : on saute le déplacement ET la transition "arrivé". On
//     NE traite SURTOUT PAS "hook absent" comme "result==0 -> state=1", ce qui ferait sortir de
//     Move instantanément à chaque frame (le monstre ne marcherait jamais). Le wrap de frame,
//     lui, reste appliqué (il ne dépend pas du déplacement).
// No-op si l'index est hors bornes, si le monstre est inactif, ou si motionState n'est pas
// l'un des 9 cas du switch (`default: return` du binaire @0x5822d3 — fidèle).
void Monster_DispatchMotionTick(GameWorld& world, int monsterIndex, float dt,
                                 const IMotionFrameCountOracle* oracle,
                                 const MonsterMotionTickHost& host);

// Vrai dès qu'un Monster_DispatchMotionTick a réellement été exécuté au moins une fois depuis
// le lancement. GARDE DE NON-RÉGRESSION DE CÂBLAGE, PAS un comportement du binaire : elle
// permet à Scene/WorldRenderer.cpp de ne consommer le curseur par-entité QUE s'il est
// réellement alimenté, et de conserver sinon l'ancien repli par horloge globale (animé mais en
// phase) au lieu de figer tous les monstres à la frame 0 — le scénario de régression décrit en
// tête de ce bloc, qui surviendrait si le câblage de DispatchMotionTick était oublié. À retirer
// le jour où le câblage est verrouillé par un test.
bool Monster_MotionTickIsWired();

// =====================================================================================
// 6. Animation des PNJ DE DÉCOR — Npc_RenderSlotTick 0x5803A0 (+ _Loop/_Once)
//    (Passe 4 / vague W7, front motion-anim — gaps as-motion-01 / as-motion-02)
// =====================================================================================
//
// VÉRITÉ TERRAIN (IDA, re-prouvée cette session). Slot d'origine = &g_NpcRenderArray[88*i]
// (0x1764D14, stride 88), layout concordant sur 3 fonctions (cGameData_LoadZoneNpcInfo
// 0x5578E0, Npc_DrawMesh 0x57FF00, Npc_RenderSlotTick 0x5803A0) :
//     +0  ptr def (*(def+1324) = kindIndex+1)   +4  actif
//     +12 animType/mode (0=Loop, 1=Once)        +16 curseur
//     +20/24/28 pos                             +44 angle affiché   +80 angle baseline
//   Npc_DrawMesh @0x57ffa0 : Model_GetNpcMeshSlot(..., a3 = *((_DWORD*)this + 3)) -> +12
//   Npc_DrawMesh @0x57fff1 : SObject_DrawEx(..., animTime = *(this + 4), ...)     -> +16
//   Npc_RenderSlotTick_Loop 0x580400 : +16 += dt*30 @0x58043e ; wrap par soustraction @0x58045f ;
//     si Math_Dist3D(pos, flt_1687330 /*joueur local*/) > 400 -> +44 = +80 @0x58048e
//   Npc_RenderSlotTick_Once 0x5804A0 : frame>=count -> +12 = 0, +16 = 0 @0x5804f8/@0x580504
//
// QUI ÉCRIT animType=1 (trouvé — le système est bien VIVANT) : UI_NpcWin_Open 0x5DB530, à
// l'ouverture de la fenêtre de dialogue d'un PNJ, @0x5dc019..0x5dc0a8 :
//     if (slot+12 != 1) { slot+12 = 1; slot+16 = 0.0;                      @0x5dc026/@0x5dc032
//         if (kind not in {63,113,213,313,7})
//             slot+44 = Math_AngleBetween2D(slot+20, slot+28, joueur.x, joueur.z);  @0x5dc0a2
//         Fx_MeleeSwingUpdate(slot); }                                     @0x5dc0a8
// Soit : animType ∈ {0,1} — 0 = idle bouclé, 1 = animation « salut » jouée UNE FOIS quand on
// lui parle (+ le PNJ se tourne vers le joueur, sauf 5 kinds), puis _Once le remet à 0. Le
// reset baseline au-delà de 400 u ferme la boucle (il reprend son cap d'origine si on s'éloigne).
//
// PIÈGE ÉVITÉ (à ne pas refaire) : `xrefs_to(0x1764D20)` (= slot+12) renvoie 0 xref. En
// conclure « personne n'écrit animType » serait FAUX — les écritures sont relatives à un
// registre (`mov [eax+0Ch], 1`), donc INVISIBLES en xref absolue. Même piège que celui déjà
// documenté pour flt_18C53C0 (Scene/WorldRenderer.h:269).
//
// OÙ VIT L'ÉTAT — PLUS DE VECTEUR PARALLÈLE (adaptation W7 « npc-array-unify »). La rédaction
// initiale de ce front portait un `ZoneNpcAnimExt` parallèle « parce que StaticNpcSlot n'avait NI
// animType NI curseur ». Cette prémisse est CADUQUE : la vague W7 a fusionné les DEUX modélisations
// du pool d'origine dans `GameWorld::npcRenderEntries` (Game/GameState.h, struct NpcRenderEntry)
// dont le layout PROUVÉ porte DÉJÀ, aux offsets d'origine du slot g_NpcRenderArray, tous les
// champs de ce tick :
//     +12 mode (animType, 0=Loop / 1=Once)      +16 frameAcc (curseur)
//     +44 angle (affiché, muté par le tick)     +80 angleBase (baseline)
// On tick donc DIRECTEMENT ces champs natifs (fidèle à Npc_RenderSlotTick 0x5803A0 qui opère
// EXACTEMENT sur eux), au lieu de dupliquer l'état — c'est précisément l'anti-doublon que W7 a
// établi (`StaticNpcSlot` est désormais un alias de `NpcRenderEntry`, `kindId` supprimé). Le
// chargeur (cGameData_LoadZoneNpcInfo 0x5578E0) initialise déjà mode=0 @0x55797f, frameAcc=0
// @0x557995, angle=angleBase @0x557a42/@0x557a62 : aucun état d'init à porter ici (l'ancien
// ZoneNpc_ResetAnimExt disparaît avec le vecteur).
//
// DOUBLON DE TICK RESTANT (à signaler, hors périmètre — non tranché). Ce même pool a un SECOND
// portage de 0x5803A0 déjà câblé : Game/GroundAuraWorldObjectTick.h::TickGroundItemEffect
// (InGameTickFlow étape 7, Scene/SceneManager.cpp). Mais il écrit une EXTENSION PARALLÈLE MORTE
// (`g_GroundItemTickExt`, oracle nul) que PERSONNE ne lit pour le rendu -> il n'anime rien à
// l'écran. `ZoneNpc_TickAnim` ci-dessous écrit les champs NATIFS que le renderer lit : c'est LUI
// qui ferme le gap. Les deux écrivent des champs DISJOINTS (natifs vs ext morte) -> pas de
// double-avance du même curseur ; le doublon reste néanmoins une dette de fidélité (le binaire
// tick 0x5803A0 une seule fois par slot/frame). Ce front ne retire pas TickGroundItemEffect
// (GroundAuraWorldObjectTick / SceneManager non possédés) — cf. rapport.

// Npc_RenderSlotTick 0x5803A0 pour TOUS les slots ACTIFS de g_World.npcRenderEntries (pool unique
// W7), une fois par frame. Dispatch fidèle @0x5803ba : slot inactif (`active`==false, garde
// @0x5803ac) -> no-op ; mode==0 -> _Loop (0x580400) ; mode==1 -> _Once (0x5804A0) ; toute autre
// valeur -> no-op (le binaire ne teste que ces deux-là). oracle nul ou frameCount<=0 : le curseur
// (frameAcc) avance mais ne reboucle/ne complète jamais (dégradation propre, cohérente avec
// Monster_DispatchMotionTick).
// À CÂBLER (hors de mes fichiers, bloquant pour as-motion-01/02 côté PNJ) : appeler UNE FOIS PAR
// FRAME dans le tick InGame — Scene/SceneManager.cpp, juste après game::InGameTickFlow_Update
// (~ligne 1151) — avec l'oracle ts2::WorldMotionFrameCountOracle(). Sans ce câblage frameAcc reste
// 0 et le renderer garde le repli horloge globale (cf. ZoneNpc_AnimTickIsWired ci-dessous).
void ZoneNpc_TickAnim(float dt, const IMotionFrameCountOracle* oracle);

// Pendant exact de Monster_MotionTickIsWired() pour les PNJ de décor : vrai dès qu'un
// ZoneNpc_TickAnim a réellement tourné. MÊME garde de non-régression, MÊME justification —
// tant que Scene/SceneManager.cpp n'appelle pas ZoneNpc_TickAnim par frame, le curseur reste
// à 0 et consommer `cursor` figerait les PNJ de décor sur la 1re frame de leur idle, alors
// qu'ils étaient au moins animés (en phase) par l'ancien repli SampleByGameTime. PAS un
// comportement du binaire : à retirer quand le câblage est verrouillé par un test.
bool ZoneNpc_AnimTickIsWired();

// UI_NpcWin_Open 0x5DB530 @0x5dc019..0x5dc0a8 — à appeler à l'OUVERTURE de la fenêtre de dialogue
// d'un PNJ de décor. Mute DIRECTEMENT le slot natif g_World.npcRenderEntries[zoneNpcIndex] (index
// dans le pool unique = index de game::ZoneNpcs()). Idempotent : ne fait rien si mode vaut déjà 1
// (garde @0x5dc01d, fidèle).
// `playerX/playerZ` = flt_1687330/flt_1687338 (position du joueur local) — le PNJ se tourne vers
// le joueur (angle=+44) SAUF si son NpcDefRecord::id ∈ {63,113,213,313,7} (@0x5dc03a..0x5dc06b).
// NON CÂBLÉ à ce jour (l'UI NpcDialog appartient à une vague voisine) -> mode reste 0 : c'est
// FIDÈLE (un PNJ à qui personne ne parle boucle son idle), pas un bug.
// NOTE : Fx_MeleeSwingUpdate(slot) 0x57FE90 (@0x5dc0a8, son positionnel) n'est PAS reproduit ici —
// hors périmètre audio/FX de ce front. TODO [ancre 0x57FE90].
void ZoneNpc_OnDialogueOpen(int zoneNpcIndex, float playerX, float playerZ);

} // namespace ts2::game

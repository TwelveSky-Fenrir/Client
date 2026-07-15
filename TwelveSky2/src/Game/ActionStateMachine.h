// Game/ActionStateMachine.h — FSM d'action/animation du personnage (pivot du
// déclenchement combat), réécriture C++ PROPRE (pas byte-exact sur le rendu, byte-exact
// sur les seuils/offsets/valeurs numériques d'état et sur la formule de vitesse).
//
// Source de vérité = Char_UpdateAnimationFrame (0x571880, ~5.9 Ko de pseudocode,
// désassemblage TwelveSky2.exe imagebase 0x400000). Cette fonction est le tick par
// personnage (this = objet "fiche perso" au format de byte_1685748/g_LocalPlayerSheet ;
// a2 = indicateur "entité pilotée à distance / relecture réseau" -> !a2 = simulation
// locale réelle, déclenche réellement les envois réseau ; a3 = dt de la frame 30 FPS).
//
// PÉRIMÈTRE (imposé par la mission) : uniquement la MACHINE D'ÉTAT et le TIMING de
// l'action — quels états existent (valeurs numériques d'origine), quelles transitions,
// à quelle frame le contact (coup/compétence) part réellement, et la vitesse d'attaque
// (Char_CalcAttackSpeed 0x4CCAB0, déjà câblée dans Game/StatFormulas.h::CalcAttackSpeed).
// Le rendu 3D du squelette/mesh (PcModel_ResolveSlotAndApply 0x4E5A00, sélection de sous-
// objets ModelObj_GetSubObjectCount 0x4D7080, tables de frames d'animation par arme/
// compétence Anim_IsWeaponHitFrame 0x558D80 / Anim_LookupFrameEvent 0x558B40) est HORS
// PÉRIMÈTRE : ces données sont pilotées par les assets de motion (SOBJECT/MOTION), pas
// par du code. Ce module expose une interface `IAnimFrameOracle` que la couche
// rendu/anim doit implémenter pour brancher ces tables (TODO précis à chaque point
// d'usage, EA citée).
//
// Offsets d'origine relevés dans Char_UpdateAnimationFrame (indices dword *((_DWORD*)this
// + N) => octet N*4 ; les fonctions "sœurs" Combat_TickAttackState 0x574BD0,
// Char_AttackAnimTick_576890/576A20, Char_CastAnimTick_5762F0 etc. utilisent la même
// disposition en offsets OCTET directs `this + N` — les deux notations concordent) :
//   +0    bool   actif           validité de l'enregistrement (garde de tête de fonction)
//   +92   int    modelIndex      (idx23) modèle/apparence
//   +96   int    modelVariant    (idx24) variante modèle (genre/skin)
//   +108  int    (idx27) / +112 int (idx28)   paramètres additionnels PcModel_ResolveSlotAndApply
//   +116  arme   champ résolu par Weapon_ClassFromField56 (0x4CC930) -> classe d'arme
//   +144  bool   altWeaponSet    (idx144... voir plus bas, ATTENTION collision de notation)
// (cf. commentaires détaillés sur ActionFsm ci-dessous pour chaque champ retenu)
//
// CombatSystem.h (DÉJÀ ÉCRIT, NE PAS ÉDITER) documente déjà que entity+244 est À LA FOIS
// le sélecteur d'état de cette FSM (switch de Char_UpdateAnimationFrame, ~0x5727BF) ET le
// champ "facing" P[11] envoyé tel quel dans les paquets d'action (Combat_QueueMeleeAttack
// 0x573130 / Combat_QueueSkillAction 0x573200 lisent *(this+244) sans transformation) —
// vérifié à nouveau ici sur les deux builders : c'est bien une réutilisation volontaire du
// même mot mémoire, pas une erreur d'étiquetage.
#pragma once
#include <cstdint>
#include "Game/CombatSystem.h"
#include "Game/GameState.h"

namespace ts2::game {

// ---------------------------------------------------------------------------
// États d'action — valeurs numériques EXACTES du switch terminal de
// Char_UpdateAnimationFrame (0x5727BF..0x572F42). Chaque cas appelle un handler
// Char_*Tick*/Char_*AnimEnd*/Combat_TickAttackState dédié (EA en commentaire) qui fait
// avancer entity+248 (frame courante) de a3*30 (ou d'un multiple pondéré pour les états
// de cast, cf. CastSlot0/1/2) et transitionne quand la frame atteint la durée d'anim
// (donnée asset 3D, hors périmètre — cf. IAnimFrameOracle/TickTimedState). Valeurs NON
// listées dans le switch (8, 0x18..0x1D, 0x2F, 0x35, 0x3B, 0x4D..0x50, 0x54, ...) tombent
// dans le `default: break` d'origine -> aucune progression d'anim pour cette entité tant
// que l'état vaut une de ces valeurs (états probablement pilotés ailleurs, ou inutilisés).
enum class CharActionState : int32_t {
    Idle                    = 0x00, // Char_AnimTick_5746E0            0x5746E0 — boucle d'inactivité
    Move                    = 0x01, // Char_TickMoveState              0x574830 — déplacement + proximité ramassage/aura
    ApproachAndInteract     = 0x02, // Combat_TickAttackState          0x574BD0 — marche vers la cible puis auto-interact (combat/pickup/npc/gather) en portée
    RecoveryToMove          = 0x03, // Char_AnimEndToRecovery_576050   0x576050 — fin d'anim -> Move, recalcule *(this+240) via Weapon_ClassFromField56
    AnimEndToIdle_5761A0    = 0x04, // Char_AnimEndToIdle_5761A0       0x5761A0
    CastSlot0               = 0x05, // Char_CastAnimTick_5762F0        0x5762F0 — windup compétence, table de frame d'événement slot 0 (a4=état-5=0)
    CastSlot1               = 0x06, // Char_CastAnimTick_5764D0        0x5764D0 — idem, slot 1 (a4=1)
    CastSlot2               = 0x07, // Char_CastAnimTick_5766B0        0x5766B0 — idem, slot 2 (a4=2)
    // (0x08 absent du switch : default -> no-op)
    AttackWindupA           = 0x09, // Char_AttackAnimTick_576890      0x576890 — windup coup n°1, tire l'ordre d'attaque en fin d'anim
    AttackWindupB           = 0x0A, // Char_AttackAnimTick_576A20      0x576A20 — windup coup n°2 (variante), idem
    AnimHold                = 0x0B, // Char_AnimHold_576BB0            0x576BB0
    DeathRespawn            = 0x0C, // Char_TickDeathRespawn           0x576CB0 — anim de mort, décompte respawn (this+740), warp destination
    AnimEndToIdle_577D70    = 0x0D, // 0x577D70
    AnimLoop_577EC0         = 0x0E, // 0x577EC0
    AnimLoop_577FC0         = 0x0F, // 0x577FC0
    AnimEndOpenDialog       = 0x10, // Char_AnimEndOpenDialog_5780C0   0x5780C0
    AnimEndToIdle_578290    = 0x11, // 0x578290
    AnimEndToState19        = 0x12, // Char_AnimEndToState19_5783E0    0x5783E0
    AnimLoop_578510         = 0x13, // 0x578510
    AnimEndToIdle_578610    = 0x14, // 0x578610
    AnimEndToIdle_578760    = 0x15, // 0x578760
    AnimEndToIdle_5788B0    = 0x16, // 0x5788B0
    AnimEndToIdle_578A00    = 0x17, // 0x578A00
    // (0x18..0x1D absents du switch)
    AnimEndToState31        = 0x1E, // Char_AnimEndToState31_578B50    0x578B50
    AnimLoop_578C70         = 0x1F, // 0x578C70
    Run                     = 0x20, // Char_TickRunState               0x578D70
    AnimEndToState34        = 0x21, // Char_AnimEndToState34_579DD0    0x579DD0
    ArcMoveSeg1             = 0x22, // Char_TickArcMoveSeg1            0x579EE0 — saut/trajectoire arquée, segment 1
    ArcMoveSeg2             = 0x23, // Char_TickArcMoveSeg2            0x57A040 — segment 2
    ArcMoveSeg3             = 0x24, // Char_TickArcMoveSeg3            0x57A190 — segment 3
    AnimEndToIdle_57A2F0    = 0x25, // 0x57A2F0
    AnimEndToIdle_57A440    = 0x26, // 0x57A440
    AnimEndToIdle_57A5A0    = 0x27, // 0x57A5A0
    Channel                 = 0x28, // Char_TickChannelState           0x57A700 — canalisation de compétence (maintien)
    AnimEndToIdle_57A970    = 0x29, // 0x57A970
    CastAnimTick_57AAC0     = 0x2A, // 0x57AAC0
    CastAnimTick_57ACB0     = 0x2B, // 0x57ACB0
    CastAnimTick_57AEA0     = 0x2C, // 0x57AEA0
    CastAnimTick_57B040     = 0x2D, // 0x57B040
    CastAnimTick_57B230     = 0x2E, // 0x57B230
    // (0x2F absent)
    CastAnimTick_57B420     = 0x30, // 0x57B420
    CastAnimTick_57B610     = 0x31, // 0x57B610
    CastAnimTick_57B800     = 0x32, // 0x57B800
    CastAnimTick_57B9A0     = 0x33, // 0x57B9A0
    CastAnimTick_57BB90     = 0x34, // 0x57BB90
    // (0x35 absent)
    CastAnimTick_57BD80     = 0x36, // 0x57BD80
    CastAnimTick_57BF20     = 0x37, // 0x57BF20
    CastAnimTick_57C0C0     = 0x38, // 0x57C0C0
    CastAnimTick_57C260     = 0x39, // 0x57C260
    CastAnimTick_57C400     = 0x3A, // 0x57C400
    // (0x3B absent)
    AnimEndToIdle_57C5A0    = 0x3C, // 0x57C5A0
    AnimEndToIdle_57C6D0    = 0x3D, // 0x57C6D0
    AnimEndToIdle_57C800    = 0x3E, // 0x57C800
    AnimEndToState64        = 0x3F, // Char_AnimEndToState64_57C930    0x57C930
    LootPickup              = 0x40, // Char_TickLootPickupState        0x57CA50
    AnimEndSpawnSkillFx     = 0x41, // Char_AnimEndSpawnSkillFx_57CE40 0x57CE40 — déclenche un FX de compétence en fin d'anim (hors périmètre rendu)
    AnimEndToIdle_57D0E0    = 0x42, // 0x57D0E0
    AnimEndToIdle_57D230    = 0x43, // 0x57D230
    AnimEndToIdle_57D380    = 0x44, // 0x57D380
    CastAnimTick_57D4D0     = 0x45, // 0x57D4D0
    CastAnimTick_57D6C0     = 0x46, // 0x57D6C0
    CastAnimTick_57D8B0     = 0x47, // 0x57D8B0
    CastAnimTick_57DAA0     = 0x48, // 0x57DAA0
    CastAnimTick_57DC90     = 0x49, // 0x57DC90
    CastAnimTick_57DE30     = 0x4A, // 0x57DE30
    AnimEndToIdle_57DFD0    = 0x4B, // 0x57DFD0
    AnimEndToIdle_57E120    = 0x4C, // 0x57E120
    // (0x4D..0x50 absents)
    CastAnimTick_57E280     = 0x51, // 0x57E280
    CastAnimTick_57E420     = 0x52, // 0x57E420
    CastAnimTick_57E5C0     = 0x53, // 0x57E5C0
    // (0x54 absent)
    CastAnimTick_57E760     = 0x55, // 0x57E760
    CastAnimTick_57E950     = 0x56, // 0x57E950
    CastAnimTick_57EB40     = 0x57, // 0x57EB40
    CastAnimTick_57ED30     = 0x58, // 0x57ED30
    ActionToStand           = 0x59, // Char_ActionTick_ToStand         0x57EF20
    ActionToStand2          = 0x5A, // Char_ActionTick_ToStand2        0x57F0C0
    GuardBegin              = 0x5B, // Char_ActionTick_GuardBegin      0x57F260 — passe l'état à 92 (GuardLoop) puis 93 (GuardEnd) si pas de maintien
    GuardLoop               = 0x5C, // Char_ActionTick_GuardLoop       0x57F410 — maintien tant que this+548==1 (touche gardée)
    GuardEnd                = 0x5D, // Char_ActionTick_GuardEnd        0x57F600
    GuardHit                = 0x5E, // Char_ActionTick_GuardHit        0x57F800 — anim d'impact bloqué (cf. CombatPacket::resultType==4)
    GuardHitAlt             = 0x5F, // Char_ActionTick_GuardHitAlt     0x57F990
};

inline int32_t ToRaw(CharActionState s) { return static_cast<int32_t>(s); }

// ---------------------------------------------------------------------------
// Interface de branchement vers les tables d'animation figées dans le binaire mais
// indexées par des données d'assets (motion/skinning) — HORS PÉRIMÈTRE de ce module
// (rendu 3D). L'implémentation réelle vit dans la couche rendu/anim.
// ---------------------------------------------------------------------------
class IAnimFrameOracle {
public:
    virtual ~IAnimFrameOracle() = default;

    // Fidèle à Anim_IsWeaponHitFrame (0x558D80) : table figée {weaponAnimId -> 1..3 frames
    // de contact ±1} pour les coups d'arme "simples" (cette branche est empruntée quand
    // hitUsesSkillTable==false). weaponAnimId = entity+296 (== CombatActorState::skillId,
    // 0 pour un coup de base), frame = entity+248, weaponClass = classe résolue par
    // Weapon_ClassFromField56 (0x4CC930, hors périmètre : dépend de l'équipement 3D).
    virtual bool IsWeaponHitFrame(int32_t weaponAnimId, float frame, int32_t weaponClass) const = 0;

    // Fidèle à Anim_LookupFrameEvent (0x558B40) : table figée indexée par
    // [modelIndex][weaponClass][castSlot(=état-5, 0..2)][frame±1][altIndex] -> id
    // d'événement (skillId/effet) écrit dans outEventId. altIndex = 0 si altWeaponSet
    // (entity+576) sinon weaponAnimSlot (entity+220). Utilisée quand hitUsesSkillTable==true.
    virtual bool LookupSkillFrameEvent(int32_t modelIndex, int32_t weaponClass, int32_t castSlot,
                                        float frame, int32_t altIndex, int32_t& outEventId) const = 0;
};

// ---------------------------------------------------------------------------
// FSM d'action par entité — champs retenus (offsets d'origine en commentaire, "entity+N"
// = octet N du même enregistrement que CombatActorState). Les champs de position/cible/
// compétence sont directement ceux de CombatActorState (partagés avec les builders réseau
// Build{Melee,Skill}Attack) : `actor.facing` == entity+244 == `state` (miroir typé),
// `actor.meleeSubmode` == entity+284, `actor.targetId` == entity+288/292,
// `actor.skillId` == entity+296 (== a1 de Anim_IsWeaponHitFrame).
// ---------------------------------------------------------------------------
struct ActionFsm {
    // --- Contexte partagé avec les builders de paquet d'action (cf. CombatSystem.h) ---
    CombatActorState actor;

    // --- État / timing (entity+244, +248) ---
    CharActionState state     = CharActionState::Idle;  // entity+244 (miroir de actor.facing)
    float           animFrame = 0.0f;                   // entity+248 — position dans l'anim courante (unités = frames @ 30 FPS)

    // !a2 dans le binaire : true si cette entité est simulée localement (le joueur ou une
    // entité dont on rejoue le tick client) — seul ce cas envoie réellement des paquets
    // (Net_SendPacket_Op16/18) et déclenche le contact ; false = relecture d'un état déjà
    // reçu du réseau (autres joueurs/monstres), avance juste l'anim.
    bool isLocalSimulation = true;

    // --- Détection de la frame de contact (bloc en tête de Char_UpdateAnimationFrame,
    // 0x571926..0x571D2A — actif quel que soit `state` tant que hitCheckActive==true) ---
    bool hitCheckActive   = false; // entity+624 (idx156) — arme le test de contact ce tick
    bool hitFired         = false; // entity+640 (idx160) — latch "déjà tiré pour cette anim" (empêche double-déclenchement tant que la frame reste dans la fenêtre ±1)
    bool hitUsesSkillTable = false; // entity+628 (idx157) — true: Anim_LookupFrameEvent (table compétence, castSlot=état-5) ; false: Anim_IsWeaponHitFrame (table arme, clé=actor.skillId)
    bool altWeaponSet     = false; // entity+576 (idx144) — sélectionne l'entrée "arme alternative" dans les deux tables ci-dessus
    int32_t weaponAnimSlot = 0;    // entity+220 (idx55) — passé comme altIndex à LookupSkillFrameEvent quand altWeaponSet==false
    int32_t lastSkillEventId = 0;  // v68 dans le binaire — id d'événement écrit par LookupSkillFrameEvent au moment du premier tir ; réutilisé comme argument skillId de BuildMeleeAttack (cf. Combat_QueueMeleeAttack(v68) 0x571D7D)

    // --- Nature de l'action déclenchée au contact (entity+632, +636) ---
    // actionKind   : 1 = coup/compétence instantanée (dispatch vers Build{Melee,Skill}Attack),
    //                2 = compétence à projectile (Effect_SpawnSkillProjectile 0x573A90, FX -> hors périmètre)
    // actionSubKind (valide seulement si actionKind==1) : 1 = cible unique, 2 = zone
    //                (Combat_CastAoESkillOnTargets 0x573480 — énumération de cibles hors périmètre)
    int32_t actionKind    = 1; // entity+632 (idx158)
    int32_t actionSubKind = 1; // entity+636 (idx159)

    // --- Résolution de la table de contact (fournie par la couche rendu/anim, hors périmètre) ---
    int32_t modelIndex  = 0; // entity+92  (idx23)
    int32_t weaponClass = 0; // Weapon_ClassFromField56(0x4CC930) résolu en amont par l'appelant

    // --- Interruption de cast (0x57275A) : uniquement pour l'entité locale (soi-même,
    // comparaison *(this+4)==dword_1687238[0] dans le binaire). Sémantique exacte des
    // globals déclencheurs (g_InvDirtyEnable 0x16755AC / g_AutoHuntFuelA 0x16755A4 /
    // g_AutoHuntFuelB 0x16755A8, liés à l'auto-hunt) non modélisée ici : l'appelant
    // positionne ce drapeau quand la condition d'origine est vraie.
    bool isSelf               = false;
    bool pendingCastInterrupt = false;

    // --- Sorties du dernier Update() ---
    bool                 contactFiredThisTick = false; // un coup/compétence instantané a été validé ce tick
    CombatActionRequest  lastAction{};                  // requête construite par BuildMeleeAttack/BuildSkillAction (valide seulement si contactFiredThisTick)
    bool                 pendingAoECast       = false;  // actionSubKind==2 au contact -> Combat_CastAoESkillOnTargets (0x573480, hors périmètre : ce module ne calcule pas la liste de cibles)
    bool                 pendingProjectile    = false;  // actionKind==2 au contact -> Effect_SpawnSkillProjectile (0x573A90, hors périmètre FX)

    // Positionne l'état ET son miroir dans actor.facing (entity+244) — cf. en-tête de
    // fichier : les deux DOIVENT rester synchronisés, c'est le même mot mémoire d'origine.
    void SetState(CharActionState s) {
        state = s;
        actor.facing = ToRaw(s);
    }

    // ==========================================================================
    // Bloc de détection de contact — traduction fidèle de Char_UpdateAnimationFrame
    // 0x571926 (if (*(this+156)==1)) .. 0x571D2A (v66==1 -> dispatch). Appelé à chaque
    // Update() indépendamment de `state` (le binaire ne teste QUE hitCheckActive).
    // Retourne true si un coup/compétence instantané a été validé (contactFiredThisTick).
    // `world` = table d'entités pour la revalidation de cible (0x571BC9/0x571C89) ;
    // `oracle` peut être nullptr (aucun contact ne sera jamais détecté, dégrade proprement).
    // ==========================================================================
    bool UpdateContactDetection(const GameWorld& world, const IAnimFrameOracle* oracle);

    // ==========================================================================
    // Interruption de cast — 0x57275A : si pendingCastInterrupt && isSelf && state est
    // un des 3 emplacements de cast (CastSlot0/1/2), force le retour à Move avec frame
    // remise à 0 (comme le binaire : *(this+244)=1; *(this+248)=0.0;). Retourne true si la
    // transition a eu lieu (et remet pendingCastInterrupt à false).
    // ==========================================================================
    bool ApplyPendingCastInterrupt();

    // ==========================================================================
    // Tick générique "l'anim avance de dt*30 frames jusqu'à durationFrames puis
    // transitionne vers nextState" — motif QUASI UNIVERSEL des handlers Char_AnimEnd*/
    // Char_AnimLoop*/Char_AttackAnimTick_*/Char_TickDeathRespawn (ex. 0x574C98,
    // 0x576958, 0x576D78, 0x57F328...). `durationFrames` est une donnée d'anim 3D
    // (ModelObj_GetSubObjectCount 0x4D7080 ou similaire) — HORS PÉRIMÈTRE, fournie par
    // l'appelant (couche rendu). `loopInstead` reproduit la variante "boucle" (frame -=
    // duration au lieu de clamp, ex. Char_TickMoveState 0x574911/0x574922) plutôt que la
    // variante "fin d'anim -> transition" (ex. Char_AnimEndToRecovery 0x576131).
    // Retourne true si la transition (ou le rebouclage) a eu lieu ce tick.
    // ==========================================================================
    bool TickTimedState(float dt, float durationFrames, CharActionState nextState, bool loopInstead = false);

    // ==========================================================================
    // Variante pour les 3 états de cast (CastSlot0/1/2) — l'incrément de frame N'EST PAS
    // dt*30 mais dt*weaponRatePct*0.3 (0x576414, Char_CalcWeaponRatePct 0x4CD900 déjà
    // câblée dans StatFormulas.h), et n'a lieu QUE si weaponRatePct est dans les bornes
    // [Char_CalcAnimBoundMin99, Char_CalcAnimBoundMax121] (0x57FB30/0x57FBB0 — tables
    // d'anim, hors périmètre ; l'appelant fournit `withinBounds`). En fin d'anim, retour à
    // Move (1), frame=0, ET hitCheckActive=false (0x57644F, comportement spécifique à ce
    // groupe d'états — les autres handlers ne touchent pas hitCheckActive).
    // ==========================================================================
    bool TickCastState(float dt, double weaponRatePct, bool withinBounds, float durationFrames);

    // ==========================================================================
    // États de garde (0x5B..0x5F) — reproduction du sous-automate this+548 (bool "touche
    // de garde maintenue", fourni par l'appelant = input) / this+552 (sous-état garde
    // 2=maintien en cours, 3=relâché) observé dans Char_ActionTick_GuardBegin (0x57F260)
    // et Char_ActionTick_GuardLoop (0x57F410). Ne modélise pas l'envoi réseau
    // (Net_SendOp104 0x4BDAE0, hors périmètre net) ; expose seulement les transitions
    // d'état + le sous-état de garde via `guardSubstate` (membre ajouté ci-dessous).
    // ==========================================================================
    int32_t guardSubstate = 0; // entity+552 : 0=inactif, 2=maintien, 3=relâché/fin
    bool guardKeyHeld     = false; // entrée fournie par l'appelant, reflète entity+548
    bool TickGuardBegin(float dt, float durationFrames);
    bool TickGuardLoop(float dt, float durationFrames);
};

// ---------------------------------------------------------------------------
// Liste EXACTE des identifiants d'animation d'arme/compétence donnant le bonus de
// vitesse ×1.1 dans Combat_TickAttackState (0x575A39..0x575A44). Reproduite telle
// quelle (byte-exact) — NOTE : cette liste est un SUR-ENSEMBLE de celle utilisée par
// Char_CalcAttackSpeed (0x4CCD14, ~39 valeurs, déjà neutralisée dans StatFormulas.cpp
// faute de champ SelfState porteur du "special item" runtime) ; les deux listes
// DIVERGENT réellement dans le binaire (incohérence d'origine entre les deux call
// sites, conservée telle quelle plutôt que "corrigée").
bool IsFastAttackAnimId(int32_t animId);

} // namespace ts2::game

// Game/ActionStateMachine.cpp — implémentation de la FSM d'action (ts2::game).
// Fidèle à Char_UpdateAnimationFrame (0x571880) et aux handlers d'état cités en
// commentaire dans ActionStateMachine.h. Voir ce fichier pour la table complète des
// offsets d'origine et le périmètre (hors rendu 3D).
#include "Game/ActionStateMachine.h"

namespace ts2::game {

// ===========================================================================
// Détection de la frame de contact — 0x571926..0x571D2A.
// ===========================================================================
bool ActionFsm::UpdateContactDetection(const GameWorld& world, const IAnimFrameOracle* oracle) {
    contactFiredThisTick = false;
    pendingAoECast        = false;
    pendingProjectile     = false;

    if (!hitCheckActive)   // *(this+156) != 1 (0x571926) -> aucun test de contact ce tick
        return false;

    bool justFired = false; // v66

    if (hitFired) {
        // *(this+160) != 0 (0x571936) : un contact a déjà été tiré pour cette anim ; on ne
        // fait que vérifier si on est TOUJOURS dans la fenêtre ±1 pour, sinon, refermer le
        // latch (0x571A7B..0x571B7B / 0x571B4D..0x571B7B). Ne redéclenche PAS d'action.
        bool stillInWindow = false;
        if (oracle) {
            if (hitUsesSkillTable) {
                const int32_t castSlot = ToRaw(state) - 5;
                const int32_t altIndex = altWeaponSet ? 0 : weaponAnimSlot;
                int32_t dummyEvent = 0;
                stillInWindow = oracle->LookupSkillFrameEvent(modelIndex, weaponClass, castSlot,
                                                                animFrame, altIndex, dummyEvent);
            } else {
                stillInWindow = oracle->IsWeaponHitFrame(actor.skillId, animFrame, weaponClass);
            }
        }
        if (!stillInWindow)
            hitFired = false; // *(this+160) = 0
    } else if (hitUsesSkillTable) {
        // 0x57194D..0x571A18 : table compétence (Anim_LookupFrameEvent), castSlot = état-5
        // (valide uniquement pour CastSlot0/1/2 ; sinon castSlot sort de {0,1,2} et l'oracle
        // doit renvoyer false — c'est à l'implémentation de l'oracle de le garantir).
        const int32_t castSlot = ToRaw(state) - 5;
        const int32_t altIndex = altWeaponSet ? 0 : weaponAnimSlot;
        int32_t skillEventId = 0;
        if (oracle && oracle->LookupSkillFrameEvent(modelIndex, weaponClass, castSlot,
                                                      animFrame, altIndex, skillEventId)) {
            hitFired = true;          // *(this+160) = 1
            justFired = true;         // v66 = 1
            lastSkillEventId = skillEventId; // v68
        }
    } else {
        // 0x571A2D..0x571A65 : table arme simple (Anim_IsWeaponHitFrame), clé = actor.skillId
        // (== entity+296, 0 pour un coup de base).
        if (oracle && oracle->IsWeaponHitFrame(actor.skillId, animFrame, weaponClass)) {
            hitFired  = true;
            justFired = true;
        }
    }

    if (!justFired)
        return false;

    // 0x571B8F..0x571D2A : revalidation de la cible avant d'autoriser l'effet. Le binaire
    // rescanne le tableau d'entités correspondant en testant (active && netID==target &&
    // état-cible "ciblable"). L'état-cible exact (sub_558AE0/558B10, dword_168724C) n'est
    // PAS porté par PlayerEntity/MonsterEntity (Game/GameState.h, non modifiable ici) :
    // TODO 0x558AE0 (Char_IsTargetablePlayerState) / 0x558B10 (Char_IsTargetableMonsterState)
    // — approximé ici par (active && id correspond).
    bool targetValid = false;
    switch (actor.meleeSubmode) { // entity+284
    case 0:
        // 0x571BB6 case 0 : aucune classe de cible spécifique -> toujours validé (v67=1).
        targetValid = true;
        break;
    case 2:
    case 3:
        for (const PlayerEntity& p : world.players) {
            if (p.active && p.id == actor.targetId) { targetValid = true; break; }
        }
        break;
    case 5:
        for (const MonsterEntity& m : world.monsters) {
            if (m.active && m.id == actor.targetId) { targetValid = true; break; }
        }
        break;
    default:
        // {1,4,6,7} : hors combat (interactions joueur/objet/pnj/récolte) -> pas de contact
        // combat déclenché par ce bloc (0x571BB6 default: break, v67 reste 0).
        targetValid = false;
        break;
    }
    if (!targetValid)
        return false;

    // 0x571D2A..0x571DAF : dispatch selon actionKind (entity+632) / actionSubKind (entity+636).
    if (actionKind == 1) {
        if (actionSubKind == 1) {
            // 0x571D66 : if (!a2) — seule l'entité simulée localement envoie réellement le
            // paquet d'action (Net_SendPacket_Op18, dans Combat_QueueMeleeAttack/SkillAction).
            if (isLocalSimulation) {
                if (hitUsesSkillTable)
                    // 0x571D7D : Combat_QueueMeleeAttack(v68) — v68 = id d'événement remonté
                    // par la table compétence, réutilisé comme argument skillId du builder
                    // melee (comportement d'origine, en apparence inversé par rapport aux
                    // noms hitUsesSkillTable/BuildMeleeAttack — fidèlement conservé).
                    lastAction = BuildMeleeAttack(actor, lastSkillEventId);
                else
                    // 0x571D87 : Combat_QueueSkillAction(this).
                    lastAction = BuildSkillAction(actor);
                contactFiredThisTick = true;
            }
        } else if (actionSubKind == 2) {
            // 0x571D99 : Combat_CastAoESkillOnTargets(this) — énumération des cibles en zone,
            // TODO 0x573480 (hors périmètre : logique de sélection multi-cibles).
            if (isLocalSimulation)
                pendingAoECast = true;
        }
    } else if (actionKind == 2) {
        // 0x571DA7 : Effect_SpawnSkillProjectile(a2) — appelée SANS test !a2 dans le binaire
        // (contrairement aux deux branches ci-dessus) : un projectile est signalé même pour
        // une entité rejouée depuis le réseau (effet visuel côté spectateur).
        // TODO 0x573A90 (hors périmètre : FX/rendu).
        pendingProjectile = true;
    }

    return contactFiredThisTick;
}

// ===========================================================================
// Interruption de cast — 0x57275A..0x5727AC.
// ===========================================================================
bool ActionFsm::ApplyPendingCastInterrupt() {
    if (!pendingCastInterrupt || !isSelf)
        return false;

    if (state == CharActionState::CastSlot0 ||
        state == CharActionState::CastSlot1 ||
        state == CharActionState::CastSlot2) {
        SetState(CharActionState::Move); // *(this+244) = 1
        animFrame = 0.0f;                // *(this+248) = 0.0
        pendingCastInterrupt = false;
        return true;
    }
    return false;
}

// ===========================================================================
// Tick générique "anim end -> transition" / "anim loop".
//
// NATURE (précision W11) : c'est une PRIMITIVE GÉNÉRIQUE paramétrée (nextState/loopInstead),
// PAS le portage d'un handler particulier. Le motif `frame += dt*30 ; if (frame >= duree)
// { transition }` est partagé mot pour mot par la grande majorité des 81 cas du switch
// terminal (0x5727BF). Les EFFETS DE BORD spécifiques de chaque cas restent donc à la
// charge de l'appelant — ex. pour le cas 4 (Char_AnimEndToIdle_5761A0 0x5761A0, dont
// l'appel est prouvé @0x572834) le binaire fait EN PLUS, une fois l'anim terminée :
//     *(this+240) = 2 * Weapon_ClassFromField56(g_EquipSnapshotScratch, this+116); @0x57629B
//     if ( !a2 ) { *(this+296) = 0;                                                @0x5762C4
//                  Net_SendPacket_Op16(&g_AutoPlayMgr, this+240); }                @0x5762DC
// TODO [ancre 0x57629B] : +240 (= « animSlot », g_SelfMoveStateBlock 0x1687324 pour l'entité
// 0 : 0x1687324 - dword_1687234 = 0xF0 = 240) N'A PAS de champ porteur dans
// game::CharAnimState (Game/GameState.h, hors périmètre de ce front) ; Weapon_ClassFromField56
// 0x4CC930 n'est portée nulle part. Relation prouvée si un champ est ajouté un jour :
// +240 == 2 * CharAnimState::weaponClass (weaponClass étant DÉJÀ défini comme le résultat de
// Weapon_ClassFromField56, cf. ActionStateMachine.h).
// ===========================================================================
bool ActionFsm::TickTimedState(float dt, float durationFrames, CharActionState nextState, bool loopInstead) {
    animFrame += dt * 30.0f; // motif universel : *(this+248) = a3*30.0 + *(this+248)

    if (animFrame < durationFrames)
        return false;

    if (loopInstead) {
        // ex. Char_TickMoveState 0x574911/0x574922 : *(this+248) -= duree, état inchangé.
        animFrame -= durationFrames;
        return true;
    }

    SetState(nextState);
    animFrame = 0.0f;
    return true;
}

// ===========================================================================
// Tick des états de cast (CastSlot0/1/2) — 0x5763BE..0x57644F.
//
// BORNE HAUTE CORRIGÉE (Passe 4 / W11, gap CTF-02) : la rédaction précédente annonçait
// « 0x5763BE..0x576470 ». C'est FAUX — 0x576470 est le `Net_SendPacket_Op16` de la queue
// `if (!a2)`, qui n'a JAMAIS été portée. Le code ci-dessous s'arrête bien à 0x57644F
// (`*(this+624) = 0`). Voir le TODO de queue en fin de fonction.
// ===========================================================================
bool ActionFsm::TickCastState(float dt, double weaponRatePct, bool withinBounds, float durationFrames) {
    if (withinBounds)
        // 0x576414 : *(this+248) = a3 * v6 * 0.3 + *(this+248), v6 = Char_CalcWeaponRatePct.
        animFrame += static_cast<float>(dt * weaponRatePct * 0.300000011920929);
    // Si !withinBounds : le binaire n'avance PAS la frame ce tick (0x5763FA), fidèlement
    // reproduit en sautant l'incrément.

    if (animFrame < durationFrames)
        return false;

    // 0x576437..0x57644F : retour à Move, frame à 0, ET fermeture du latch de contact
    // (comportement SPÉCIFIQUE à ce groupe d'états — aucun autre handler ne touche
    // hitCheckActive).
    SetState(CharActionState::Move);
    animFrame = 0.0f;
    hitCheckActive = false;

    // TODO [ancre Char_CastAnimTick_5762F0 0x5762F0 @0x57645D..0x5764C2] : queue `if (!a2)`
    // NON portée — c'est la CHAÎNE DE RÉ-ATTAQUE AUTOMATIQUE en fin de cast :
    //     if ( !a2 ) {                                          @0x57645D
    //         Net_SendPacket_Op16(&g_AutoPlayMgr, this+240);    @0x576470  (builder PORTÉ,
    //                                                            Net/SendPackets.cpp:1266)
    //         if ( !Game_UseFirstReadySkill() ) {               @0x57647A  (0x538190, NON portée)
    //             v3 = *(this+284);                             @0x57648E  (== actor.meleeSubmode)
    //             if ( v3 == 2 || v3 == 3 ) Player_AttackTargetPlayer();  @0x5764AA (0x539B00, NON portée)
    //             else if ( v3 == 5 )       Player_AttackTargetMonster(); @0x5764C2 (0x53A3C0, NON portée)
    //         }
    //     }
    // Sans elle, un cast qui se termine ne ré-enchaîne PAS sur la cible courante. Blocage
    // réel : les 3 fonctions 0x538190/0x539B00/0x53A3C0 ne sont portées nulle part (grep
    // exhaustif de src/ : citées en commentaire uniquement) — cf. gap AP-04.
    return true;
}

// ===========================================================================
// États de garde — 0x57F260 (GuardBegin) / 0x57F410 (GuardLoop).
// ===========================================================================
bool ActionFsm::TickGuardBegin(float dt, float durationFrames) {
    animFrame += dt * 30.0f; // 0x57F328

    if (animFrame < durationFrames)
        return false;

    SetState(CharActionState::GuardLoop); // *(this+244) = 92 (0x57F34B)
    animFrame = 0.0f;                     // 0x57F35A
    guardSubstate = 2;                    // *(this+552) = 2 (0x57F363)

    // CORRECTIF DE FIDÉLITÉ (Passe 4 / vague W11, front w11-combat-fsm — gap CTF-02).
    // Le saut GuardLoop -> GuardEnd est IMBRIQUÉ dans `if (!a4)` @0x57F371 dans le binaire,
    // donc réservé à l'entité simulée LOCALEMENT ; la rédaction précédente l'appliquait
    // INCONDITIONNELLEMENT. Structure re-prouvée par décompilation de Char_ActionTick_GuardBegin
    // 0x57F260 cette session :
    //     *(this+552) = 2;                                   @0x57F363
    //     if ( !a4 ) {                                       @0x57F371   <-- la garde manquante
    //         Net_SendOp104(&g_AutoPlayMgr, 3, *(this+552)); @0x57F389
    //         Net_SendOp104(&g_AutoPlayMgr, 2, 0);           @0x57F397
    //         if ( !*(this+548) ) {                          @0x57F39F
    //             *(this+552) = 3;                           @0x57F3AB
    //             *(this+244) = 93;                          @0x57F3B8
    //             ...
    //     }
    // Conséquence du défaut corrigé : une entité REJOUÉE depuis le réseau (a4!=0, où +548
    // « touche de garde maintenue » n'a AUCUNE source d'input et vaut donc toujours 0)
    // sautait GuardLoop -> GuardEnd dès la 1re frame, alors que le binaire la laisse en
    // GuardLoop et attend l'état poussé par le serveur.
    if (isLocalSimulation && !guardKeyHeld) { // !a4 (0x57F371) && !*(this+548) (0x57F39F)
        guardSubstate = 3;                  // 0x57F3AB
        SetState(CharActionState::GuardEnd); // *(this+244) = 93 (0x57F3B8)
    }
    // TODO [ancre 0x57F260 @0x57F389/@0x57F397/@0x57F3D3/@0x57F3E7/@0x57F400] : la queue
    // `if (!a4)` émet aussi Net_SendOp104(3, guardSubstate), Net_SendOp104(2, 0), puis
    // Char_UpdateWeaponGlowState 0x55D740, *(this+296)=0 et Net_SendPacket_Op16(this+240).
    // Les DEUX builders SONT portés (Net/SendPackets.cpp:2280 et :1266) — le blocage n'est
    // pas « hors périmètre net » mais un choix de couche : Game/* n'inclut pas Net/* (cf.
    // bandeau Game/AnimationTick.h). À câbler via des hooks côté appelant.
    return true;
}

bool ActionFsm::TickGuardLoop(float dt, float durationFrames) {
    animFrame += dt * 30.0f; // 0x57F4D8

    if (guardKeyHeld) { // *(this+548) == 1 (0x57F4E8)
        if (guardSubstate == 2 && animFrame < durationFrames) // 0x57F509
            return false;
        if (guardSubstate == 2 && animFrame > durationFrames) { // 0x57F52F
            animFrame = 0.0f; // 0x57F536
            return false;
        }
        // guardSubstate != 2 (déjà en 3) : tombe dans la reconduction ci-dessous.
    } else {
        // 0x57F546..0x57F553 : touche relâchée -> passe/maintient GuardEnd.
        SetState(CharActionState::GuardEnd);
        guardSubstate = 3;
    }

    animFrame = 0.0f; // 0x57F562

    if (guardSubstate == 3 && guardKeyHeld) { // 0x57F57E (redondant avec le cas ci-dessus, fidèle au binaire)
        SetState(CharActionState::GuardEnd);
        guardSubstate = 3;
    }
    return true;
}

// ===========================================================================
// Liste des ids d'animation "rapides" (bonus ×1.1) — Combat_TickAttackState 0x575A39.
// ===========================================================================
bool IsFastAttackAnimId(int32_t id) {
    if (id >= 1301 && id <= 1305) return true;
    if (id == 2489) return true;
    if (id >= 1306 && id <= 1309) return true;
    if (id >= 1313 && id <= 1331) return true;
    if (id == 510 || id == 511) return true;
    if (id == 559) return true;
    if (id >= 814 && id <= 821) return true;
    if (id >= 2266 && id <= 2285) return true;
    if (id == 2316 || id == 2317) return true;
    if (id >= 2422 && id <= 2441) return true;
    if (id >= 1917 && id <= 1936) return true;
    if (id >= 19002 && id <= 19021) return true;
    if (id >= 19025 && id <= 19044) return true;
    if (id >= 19051 && id <= 19070) return true;
    if (id >= 19261 && id <= 19280) return true;
    return false;
}

} // namespace ts2::game

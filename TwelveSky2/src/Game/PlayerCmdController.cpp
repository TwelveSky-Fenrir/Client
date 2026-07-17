// Game/PlayerCmdController.cpp — implementation de la couche d'intention (op 0x0F).
//
// LE MOULE COMMUN aux builders (etabli par decompilation Hex-Rays MCP idaTs2 des 8
// fonctions portees — Net_QueueRespawnMove 0x5117A0, Net_QueueMoveResume 0x511870,
// Net_QueueMoveTo 0x5119B0, Net_QueueRunTo 0x511B00, Net_QueueAttack5/6/7 0x511EF0/
// 0x5120A0/0x512250, Player_QueueSkill_op32 0x5134D0) :
//
//   si (+0xC990 != 0) abandon                        // latch « commande en vol »
//   si (!Char_IsAttackAction(g_LocalPlayerSheet)) abandon
//   old = memcpy(g_SelfMoveStateBlock 0x1687324, 72)
//   new.animSlot    = old.animSlot ? old.animSlot : 2*Weapon_ClassFromField112(...)
//   new.actionState = <constante propre au builder>
//   new.animFrame   = 0.0 (sauf RunTo)
//   new.pos/dest/facing/targetFacing = <propre au builder>
//   new.{targetKind..levelBonus} = <propre au builder> ; new.combatFlag = 0
//   Net_SendPacket_Op15(&g_AutoPlayMgr, new)         // 0x4B4870, trame 81 o
//   +0xC994 = g_GameTimeSec
//   [+0xC990 = 1]                                    // SAUF RunTo / MoveTo / MoveResume
//   [memcpy(g_SelfMoveStateBlock, new, 72)]          // RunTo SEUL
//   [si actionState self ∈ {2,32} -> actionState=1, animFrame=0]  // SAUF RunTo / Respawn
//
// ASYMETRIES PROUVEES — NE PAS LISSER (chacune verifiee sur le decompile, pas deduite) :
//  - Net_QueueRunTo SEUL teste la connexion (MEMORY[0x8156A8] = g_NetClient+8 = loginReady)
//    @0x511B31, SEUL reecrit le bloc self @0x511C5C, SEUL reporte animFrame quand
//    old.actionState==2 @0x511B88, et SEUL transmet a3..a8 tels quels.
//  - RunTo pose facing = old.facing (+0x24) et targetFacing = angle ; les ATTAQUES et la
//    COMPETENCE posent l'angle DANS LES DEUX champs (@0x511FFE/@0x512004).
//  - MoveTo/MoveResume posent facing = targetFacing = old.targetFacing (+0x28, pas +0x24).
//  - MoveTo : pos = dest = a2. MoveResume : pos = old.pos, dest = {0,0,0}.
//    RespawnMove : pos = a2, dest = {0,0,0}.
//  - RunTo / MoveTo / MoveResume ne posent PAS le latch ; Attack5/6/7, Skill et
//    RespawnMove le posent.
//  - RespawnMove ne teste NI le latch NI Char_IsAttackAction : sa seule garde est
//    actionState self == 12 (mort) @0x5117B0 ; il ne lit pas le bloc self (construit de
//    zero) et ne fait PAS le fixup 2/32.
//  - Attack5/6/7 forcent animSlot = 2*classe+1 (impair) @0x511F8C — jamais old.animSlot —
//    et ajoutent une garde anti-repetition @0x511F5F.
//
// CORRESPONDANCE DES GLOBALS « self » (tous DEJA modelises, aucun nouveau global cree) :
//   g_EntityArray 0x1687234 -> g_World.players[0] ; body = entity+0x18 = 0x168724C
//   g_SelfMoveStateBlock      0x1687324 = body+216 (kPMoveState)
//   g_SelfActionState         0x1687328 = body+220 = move-state+4
//   g_SelfAnimFrame           0x168732C = body+224 = move-state+8
//   flt_1687330 (pos monde)   0x1687330 = body+228 = move-state+12
//   g_SelfAttackOrder_Action  0x1687350 = body+260 = move-state+44
//   g_SelfAttackOrder_GridX   0x1687354 = body+264 ; _GridY 0x1687358 = body+268
//   g_EquipMain               0x16731D8 = g_World.self.equip ; +112 = equip[7].itemId
//                             (recoupe independamment par Game/SkillCombat.cpp:681-682)
#include "Game/PlayerCmdController.h"

#include "Game/GameState.h"       // g_World (Self()/self.equip/gameTimeSec)
#include "Game/EntityManager.h"   // kPMoveState / kPMoveStateLen (bloc move-state du body)
#include "Game/GameDatabase.h"    // GetItemInfo / ItemInfo::typeCode (+188)
#include "Game/ClientRuntime.h"   // g_Client : stockage unique de dword_1675B00 / flt_1675B04
#include "Net/NetClient.h"        // GlobalNetClient / loginReady (g_NetClient 0x8156A0)
#include "Net/SendPackets.h"      // Net_SendPacket_Op15 (0x4B4870)
#include "Net/Rng.h"              // DefaultRng (Rng_Next 0x7603FD)

#include <cstring>
#include <cmath>

namespace ts2::game {

// Le bloc move-state doit tenir dans le body modelise (600 o) — garantit que les
// memcpy ci-dessous restent dans les bornes sans test runtime (le binaire adresse
// 0x1687324 sans borne).
static_assert(kPMoveState + kPMoveStateLen <= 600,
              "le bloc move-state doit tenir dans PlayerEntity::body (600 o)");
static_assert(kPMoveStateLen == sizeof(MoveCmdBlock),
              "MoveCmdBlock doit couvrir exactement le bloc move-state (72 o)");

namespace {

// --- offsets « ordre d'attaque » du self, relatifs au body (cf. bandeau de tete).
// Ecrits par le serveur (copie brute du body au spawn), lus par la garde anti-repetition
// de Net_QueueAttack5/6/7 @0x511F5F. Non modelises ailleurs -> constantes locales ancrees.
constexpr size_t kPAttackOrderAction = 260; // g_SelfAttackOrder_Action 0x1687350
constexpr size_t kPAttackOrderGridX  = 264; // g_SelfAttackOrder_GridX  0x1687354
constexpr size_t kPAttackOrderGridY  = 268; // g_SelfAttackOrder_GridY  0x1687358

inline int32_t RdI32(const uint8_t* b, size_t o) { int32_t v; std::memcpy(&v, b + o, 4); return v; }

// g_SelfActionState 0x1687328 = move-state+4 (LIVE, pas la copie locale).
int32_t SelfActionState() {
    return RdI32(g_World.Self().body.data(), kPMoveState + 4);
}

// Char_IsAttackAction 0x558A50 : `switch (*(this + 1784))` avec this = g_LocalPlayerSheet
// 0x1685748 -> 0x1685748 + 1784*4 = 0x1687328 = g_SelfActionState (= move-state+4).
// Ce n'est donc PAS une lecture de fiche : c'est le code d'action self.
bool IsAttackAction(int32_t actionState) {
    switch (actionState) {                              /*0x558a7e*/
    case 1: case 2: case 5: case 6: case 7:
    case 0xE: case 0xF: case 0x1F: case 0x20: case 0x28: case 0x40:
        return true;                                    /*0x558a85*/
    default:
        return false;                                   /*0x558a8c*/
    }
}

// Weapon_ClassFromField112 0x4CC870 : classe d'arme 1..3 (0 = aucune) depuis le type
// (+188) du record mITEM de l'arme equipee (`MobDb_GetEntry(&mITEM, *(a2 + 112))`
// @0x4CC88D, a2 = g_EquipMain -> +112 = 7e slot de 16 o = equip[7].itemId).
// ECART ASSUME : le binaire cache aussi le record dans g_EquipSnapshotScratch+28
// (`*(this + 7) = record` @0x4CC88D). Ce scratch (0x8E719C) n'est pas modelise et
// aucun consommateur ClientSource ne le lit — l'effet de bord est donc omis.
// TODO [ancre 0x4CC870] : porter le cache si un lecteur de 0x8E719C apparait.
int32_t WeaponClass() {
    const ItemInfo* rec = GetItemInfo(g_World.self.equip[7].itemId);
    if (!rec) return 0;                                 /*0x4cc893*/
    return WeaponClassFromTypeCode(rec->typeCode);      /*0x4cc8be — record+188 (helper partagé)*/
}

// Lecture du bloc self (Crt_Memcpy(local, g_SelfMoveStateBlock, 0x48u)).
MoveCmdBlock ReadSelfBlock() {
    MoveCmdBlock b{};
    std::memcpy(&b, g_World.Self().body.data() + kPMoveState, kPMoveStateLen);
    return b;
}

// Ecriture du bloc self (Crt_Memcpy(g_SelfMoveStateBlock, new, 0x48u)) — RunTo SEUL.
void WriteSelfBlock(const MoveCmdBlock& b) {
    std::memcpy(g_World.Self().body.data() + kPMoveState, &b, kPMoveStateLen);
}

// Fixup commun de fin @0x512083 (Attack5/6/7), @0x511ADC (MoveTo), @0x51198E
// (MoveResume), @0x513630 (Skill) : si l'action self est « course » (2) ou
// « competence » (32), on la ramene a « marche » (1) et on remet la frame a 0.
// Ecrit dans le bloc LIVE (g_SelfActionState / g_SelfAnimFrame), pas dans la copie.
void FixupSelfActionState() {
    uint8_t* b = g_World.Self().body.data();
    const int32_t s = RdI32(b, kPMoveState + 4);
    if (s == 2 || s == 32) {
        const int32_t one = 1;   std::memcpy(b + kPMoveState + 4, &one, 4);
        const float   zero = 0.0f; std::memcpy(b + kPMoveState + 8, &zero, 4);
    }
}

inline void CopyVec3(float dst[3], const float src[3]) {
    dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
}

} // namespace

// Math_AngleBetween2D 0x53FB20 — transcription identique a celle de
// Game/AnimationTick.cpp:802 / Game/GroundAuraWorldObjectTick.cpp:185 (verifiee ligne a
// ligne contre le decompile : memes ancres, meme ordre d'operations).
float AngleBetween2D(float a1, float a2, float a3, float a4) {
    if (a3 == a1 && a4 == a2) return 0.0f;                       // @0x53fb45
    float dx = a3 - a1, dz = a4 - a2;                            // v12,v13
    const float len = std::sqrt(dz * dz + dx * dx);              // @0x53fb82
    if (len > 0.0f) { dx /= len; dz /= len; }                    // @0x53fb99
    const float chordZ = dz - 1.0f;                              // v14
    const float chord  = std::sqrt(chordZ * chordZ + dx * dx);   // @0x53fbdb
    float half = chord * 0.5f;                                   // @0x53fbf8 (v8/2)
    if (half > 1.0f) half = 1.0f;                                // @0x53fbf8 (branche asin(1.0))
    float ang = std::asin(half) * 2.0f;                          // @0x53fc30/@0x53fc44 (rad)
    if (a3 < a1) ang = 6.283185482025146f - ang;                 // @0x53fc54
    const float deg = ang * 57.2957763671875f + 180.0f;          // @0x53fc71
    if (deg >= 360.0f) return deg - 360.0f;                      // @0x53fc82
    return deg;                                                  // @0x53fc93
}

// ---------------------------------------------------------------------------
// Etat — STOCKAGE UNIQUE dans la longue traine ClientRuntime (cf. bandeau du .h).
//   +51600 = 0x1669170+0xC990 = dword_1675B00 ; +51604 = 0x1669170+0xC994 = flt_1675B04.
// Var() et VarF() indexent le meme conteneur par ADRESSE : 0x1675B00 (int) et 0x1675B04
// (float) sont deux slots distincts, aucun aliasing entre eux.
// ---------------------------------------------------------------------------
int32_t& PlayerCmdController::Busy()        { return g_Client.Var(0x1675B00); }
float&   PlayerCmdController::LastCmdTime() { return g_Client.VarF(0x1675B04); }

// ---------------------------------------------------------------------------
// Envoi — Net_SendPacket_Op15 0x4B4870.
// Le binaire adresse g_NetClient 0x8156A0 en global et emet inconditionnellement ;
// cote C++ le pointeur global peut etre nul tant qu'aucune connexion n'est amorcee
// (cf. Net/NetClient.h:67) -> garde de survie, sans equivalent binaire.
// ---------------------------------------------------------------------------
void PlayerCmdController::Send(const MoveCmdBlock& blk) {
    net::NetClient* c = net::GlobalNetClient();
    if (!c) return;
    ts2::net::Net_SendPacket_Op15(*c, &blk);            /*0x511c37 (RunTo) etc.*/
}

// ---------------------------------------------------------------------------
// Player_ResetCombatState 0x50F6A0 — remise a zero du latch a l'ENTREE DANS LE MONDE
// (Pkt_SpawnCharacter @0x4648F2, gate self DANS la branche « nouveau slot »). L'ack par
// paquet du jeu courant est un chemin DISTINCT (@0x464BF0, mode 3 + self), cf. le .h.
// ---------------------------------------------------------------------------
void PlayerCmdController::ResetCombatState() {
    Busy() = 0;                                         /*0x50f6e7 : *(this+51600) = 0*/
    // TODO [ancre 0x50F6A0] : le binaire remet a zero TOUT le bloc combat/action
    // 51480->53184 (~150 champs : +51480 @0x50F6AC, +51576 @0x50F6B9, +51608 @0x50F6F4,
    // +51620 @0x50F701, quetes/morph/compteurs, jusqu'au Crt_Memset(this+53164,0,0x14)
    // @0x50FED9). Ces champs ne sont pas modelises ici et n'ont aucun consommateur
    // ClientSource -> non portes plutot qu'inventes. Effets de bord deja couverts
    // ailleurs : BGM gate g_BgmEnabled @0x50F76E (SceneManager::LoadZoneBgm) et
    // Net_SendOp64 @0x50F746 (InGameTickFlow::SendPendingTargetPoll).
}

// ---------------------------------------------------------------------------
// Net_QueueRunTo 0x511B00 — actionState 2 (course). Le builder le plus atypique.
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueRunTo(const float dest[3], int32_t targetKind,
                                     int32_t targetId1, int32_t targetId2,
                                     int32_t activeSkillId, int32_t level,
                                     int32_t levelBonus) {
    // @0x511b31 : MEMORY[0x8156A8] && !*(this+51600) && Char_IsAttackAction(...)
    // MEMORY[0x8156A8] = g_NetClient+8 = nc.loginReady. RunTo est le SEUL a le tester.
    net::NetClient* c = net::GlobalNetClient();
    if (!c || !c->loginReady) return false;
    if (Busy()) return false;
    if (!IsAttackAction(SelfActionState())) return false;

    const MoveCmdBlock old = ReadSelfBlock();           /*0x511b4a*/
    MoveCmdBlock blk{};
    blk.animSlot    = old.animSlot ? old.animSlot       /*0x511b74*/
                                   : 2 * WeaponClass(); /*0x511b69*/
    blk.actionState = 2;                                /*0x511b7a*/
    // RunTo SEUL reporte la frame quand la course etait deja en cours.
    blk.animFrame   = (old.actionState == 2) ? old.animFrame : 0.0f;  /*0x511b88/@0x511b97*/
    CopyVec3(blk.pos, old.pos);                         /*0x511ba0..0x511baf*/
    CopyVec3(blk.dest, dest);                           /*0x511bb7..0x511bc9*/
    blk.facing       = old.facing;                      /*0x511bcf (v30 = old+0x24)*/
    blk.targetFacing = AngleBetween2D(old.pos[0], old.pos[2], dest[0], dest[2]); /*0x511bfd*/
    blk.targetKind    = targetKind;                     /*0x511c03*/
    blk.targetId1     = targetId1;                      /*0x511c09*/
    blk.targetId2     = targetId2;                      /*0x511c0f*/
    blk.activeSkillId = activeSkillId;                  /*0x511c15*/
    blk.level         = level;                          /*0x511c1b*/
    blk.levelBonus    = levelBonus;                     /*0x511c21*/
    blk.combatFlag    = 0;                              /*0x511c24*/

    Send(blk);                                          /*0x511c37*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x511c48*/
    WriteSelfBlock(blk);                                /*0x511c5c — RunTo SEUL*/
    // NB : ni latch (+51600) ni fixup 2/32 ici — verifie sur le decompile complet.
    return true;
}

// ---------------------------------------------------------------------------
// Net_QueueMoveTo 0x5119B0 — actionState 1. pos ET dest = a2.
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueMoveTo(const float dest[3]) {
    if (Busy()) return false;                           /*0x5119c5*/
    if (!IsAttackAction(SelfActionState())) return false; /*0x5119d3*/

    const MoveCmdBlock old = ReadSelfBlock();           /*0x5119ec*/
    MoveCmdBlock blk{};
    blk.animSlot    = old.animSlot ? old.animSlot       /*0x511a16*/
                                   : 2 * WeaponClass(); /*0x511a0b*/
    blk.actionState = 1;                                /*0x511a1c*/
    blk.animFrame   = 0.0f;                             /*0x511a28*/
    CopyVec3(blk.pos, dest);                            /*0x511a33..0x511a48 — pos = a2*/
    CopyVec3(blk.dest, blk.pos);                        /*0x511a51..0x511a5d — dest = pos*/
    blk.facing       = old.targetFacing;                /*0x511a63 (v27 = old+0x28)*/
    blk.targetFacing = old.targetFacing;                /*0x511a69*/
    blk.targetKind   = 0;                               /*0x511a6c*/
    blk.targetId1    = -1;                              /*0x511a73*/
    // targetId2/activeSkillId/level/levelBonus/combatFlag = 0 (@0x511a7a..0x511a96)

    Send(blk);                                          /*0x511aa9*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x511aba*/
    FixupSelfActionState();                             /*0x511adc*/
    return true;
}

// ---------------------------------------------------------------------------
// Net_QueueMoveResume 0x511870 — actionState 1, reprise depuis le cap memorise.
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueMoveResume() {
    if (Busy()) return false;                           /*0x511885*/
    if (!IsAttackAction(SelfActionState())) return false; /*0x511893*/

    const MoveCmdBlock old = ReadSelfBlock();           /*0x5118ac*/
    MoveCmdBlock blk{};
    blk.animSlot    = old.animSlot ? old.animSlot       /*0x5118d6*/
                                   : 2 * WeaponClass(); /*0x5118cb*/
    blk.actionState = 1;                                /*0x5118dc*/
    blk.animFrame   = 0.0f;                             /*0x5118e8*/
    CopyVec3(blk.pos, old.pos);                         /*0x5118f1..0x511900 (v4[3..5])*/
    // dest = {0,0,0} @0x511905..0x51190f (deja nul par l'init de MoveCmdBlock)
    blk.facing       = old.targetFacing;                /*0x511915 (v5 = old+0x28)*/
    blk.targetFacing = old.targetFacing;                /*0x51191b*/
    blk.targetKind   = 0;                               /*0x51191e*/
    blk.targetId1    = -1;                              /*0x511925*/
    // memset(&v3[13], 0, 20) @0x51192c : targetId2..combatFlag = 0 (deja nuls)

    Send(blk);                                          /*0x51195b*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x51196c*/
    FixupSelfActionState();                             /*0x51198e*/
    return true;
}

// ---------------------------------------------------------------------------
// Net_QueueRespawnMove 0x5117A0 — actionState 0. Garde : self mort (actionState 12).
// Construit le bloc DE ZERO (aucune lecture du bloc self).
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueRespawnMove(const float pos[3]) {
    if (SelfActionState() != 12) return false;          /*0x5117b0*/

    MoveCmdBlock blk{};
    blk.animSlot    = 0;                                /*0x5117b7*/
    blk.actionState = 0;                                /*0x5117be*/
    blk.animFrame   = 0.0f;                             /*0x5117c7*/
    CopyVec3(blk.pos, pos);                             /*0x5117cf..0x5117e1 — pos = a2*/
    // dest = {0,0,0} @0x5117e6..0x5117f0 (deja nul)
    // Cap tire au hasard : (float)(Rng_Next() % 360), MEME valeur dans les deux champs.
    const float ang = static_cast<float>(net::DefaultRng().NextMod(360)); /*0x511806*/
    blk.facing       = ang;
    blk.targetFacing = ang;                             /*0x51180c*/
    blk.targetKind   = 0;                               /*0x51180f*/
    blk.targetId1    = -1;                              /*0x511816*/
    // targetId2..combatFlag = 0 (@0x51181d..0x511839)

    Send(blk);                                          /*0x511849*/
    Busy() = 1;                                         /*0x511851*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x511864*/
    // NB : pas de fixup 2/32 ici — verifie sur le decompile complet.
    return true;
}

// ---------------------------------------------------------------------------
// Net_QueueAttack5/6/7 0x511EF0 / 0x5120A0 / 0x512250.
// Les trois decompiles sont IDENTIQUES a la constante actionState pres (5/6/7) :
// impl. partagee, parametree, strictement fidele.
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueAttackImpl(int32_t actionState, const float dest[3],
                                          int32_t targetKind, int32_t gridX, int32_t gridY) {
    if (Busy()) return false;                           /*0x511f05*/
    if (!IsAttackAction(SelfActionState())) return false; /*0x511f13*/

    // Garde anti-repetition @0x511F5F : si on est deja en train d'attaquer (action 5..7)
    // EXACTEMENT le meme ordre (meme kind + meme case de grille), on abandonne en silence.
    const uint8_t* b = g_World.Self().body.data();
    const int32_t s = SelfActionState();
    if (s >= 5 && s <= 7
        && RdI32(b, kPAttackOrderAction) == targetKind
        && RdI32(b, kPAttackOrderGridX)  == gridX
        && RdI32(b, kPAttackOrderGridY)  == gridY) {
        return false;
    }

    const MoveCmdBlock old = ReadSelfBlock();           /*0x511f71*/
    MoveCmdBlock blk{};
    // FORCE a 2*classe+1 (impair) : contrairement aux autres builders, l'ancien
    // animSlot n'est JAMAIS reporte ici.
    blk.animSlot    = 2 * WeaponClass() + 1;            /*0x511f8c*/
    blk.actionState = actionState;                      /*0x511f92 (5) / 6 / 7*/
    blk.animFrame   = 0.0f;                             /*0x511f9e*/
    CopyVec3(blk.pos, old.pos);                         /*0x511fa7..0x511fb6*/
    CopyVec3(blk.dest, dest);                           /*0x511fbe..0x511fd0*/
    // L'angle va dans LES DEUX champs de cap (contrairement a RunTo).
    const float ang = AngleBetween2D(old.pos[0], old.pos[2], dest[0], dest[2]); /*0x511ffe*/
    blk.facing       = ang;
    blk.targetFacing = ang;                             /*0x512004*/
    blk.targetKind = targetKind;                        /*0x51200a*/
    blk.targetId1  = gridX;                             /*0x512010*/
    blk.targetId2  = gridY;                             /*0x512016*/
    // activeSkillId/level/levelBonus/combatFlag = 0 en dur (@0x512019..0x51202e) :
    // le binaire IGNORE ses arguments a6..a8 sur ce builder.

    Send(blk);                                          /*0x512041*/
    Busy() = 1;                                         /*0x51204c*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x512062*/
    FixupSelfActionState();                             /*0x512083*/
    return true;
}

bool PlayerCmdController::QueueAttack5(const float dest[3], int32_t targetKind,
                                       int32_t gridX, int32_t gridY) {
    return QueueAttackImpl(5, dest, targetKind, gridX, gridY); /*0x511EF0*/
}

bool PlayerCmdController::QueueAttack6(const float dest[3], int32_t targetKind,
                                       int32_t gridX, int32_t gridY) {
    return QueueAttackImpl(6, dest, targetKind, gridX, gridY); /*0x5120A0*/
}

bool PlayerCmdController::QueueAttack7(const float dest[3], int32_t targetKind,
                                       int32_t gridX, int32_t gridY) {
    return QueueAttackImpl(7, dest, targetKind, gridX, gridY); /*0x512250*/
}

// ---------------------------------------------------------------------------
// Player_QueueSkill_op32 0x5134D0 — actionState 32 (lancement de competence).
// ---------------------------------------------------------------------------
bool PlayerCmdController::QueueSkill32(const float dest[3], int32_t skillId,
                                       int32_t level, int32_t elemResist) {
    // @0x5134f3 : if (*(this+51600) || !Char_IsAttackAction(...)) return 0;
    if (Busy() || !IsAttackAction(SelfActionState())) return false;

    const MoveCmdBlock old = ReadSelfBlock();           /*0x51350e*/
    MoveCmdBlock blk{};
    blk.animSlot    = old.animSlot ? old.animSlot       /*0x513538*/
                                   : 2 * WeaponClass(); /*0x51352d*/
    blk.actionState = 32;                               /*0x51353e*/
    blk.animFrame   = 0.0f;                             /*0x51354a*/
    CopyVec3(blk.pos, old.pos);                         /*0x513553..0x513562*/
    CopyVec3(blk.dest, dest);                           /*0x51356a..0x51357c*/
    const float ang = AngleBetween2D(old.pos[0], old.pos[2], dest[0], dest[2]); /*0x5135aa*/
    blk.facing       = ang;
    blk.targetFacing = ang;                             /*0x5135b0*/
    blk.targetKind = 0;                                 /*0x5135b3*/
    blk.targetId1  = -1;                                /*0x5135ba*/
    blk.targetId2  = 0;                                 /*0x5135c1*/
    blk.activeSkillId = skillId;                        /*0x5135cb (a6)*/
    blk.level         = level;                          /*0x5135d1 (a7)*/
    blk.levelBonus    = elemResist;                     /*0x5135d7 (a8)*/
    blk.combatFlag    = 0;                              /*0x5135da*/

    Send(blk);                                          /*0x5135ed*/
    Busy() = 1;                                         /*0x5135f8*/
    LastCmdTime() = g_World.gameTimeSec;                  /*0x51360e*/
    FixupSelfActionState();                             /*0x513630*/
    return true;
}

} // namespace ts2::game

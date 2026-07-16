// Game/PlayerCmdController.h — couche d'INTENTION du joueur (opcode reseau 0x0F).
//
// Porte le module `g_PlayerCmdController` (dword_1669170) du binaire : les builders
// Net_Queue*/Player_QueueSkill_* qui traduisent une intention joueur (clic-au-sol,
// attaque, competence, reprise de marche, respawn) en un bloc de commande de 72 octets
// envoye par Net_SendPacket_Op15 0x4B4870 (opcode fil 15 = 0x0F, trame totale 81 o).
//
// C'est LE chemin par lequel le personnage agit cote serveur : sans lui, l'op 0x0F
// n'est jamais emis et le joueur ne peut ni se deplacer, ni attaquer, ni lancer un sort.
// Les 74 appelants du binaire partagent tous le meme moule (cf. .cpp) :
//   Net_QueueRespawnMove  0x5117A0   Net_QueueMoveResume   0x511870
//   Net_QueueMoveTo       0x5119B0   Net_QueueRunTo        0x511B00
//   Net_QueueAttack5      0x511EF0   Net_QueueAttack6      0x5120A0
//   Net_QueueAttack7      0x512250   Player_QueueSkill_op32 0x5134D0
//
// Sous-ensemble porte ici = les 8 builders ci-dessus (les seuls re-decompiles et prouves
// pour cette vague). Les ~66 autres (Net_QueueAction3/4/9/10/13/15..18, Player_Queue*_opNN)
// suivent le meme moule mais ne sont PAS portes : leurs constantes n'ont pas ete verifiees.
// TODO [ancre 0x511C70..0x517A80] : porter le reste apres re-decompilation individuelle
// (chaque builder a ses PROPRES asymetries — cf. le bandeau du .cpp, ne pas extrapoler).
//
// L'etat « self » lu/ecrit par ces builders n'est PAS un global de plus : le bloc
// g_SelfMoveStateBlock 0x1687324 est DEJA modelise, c'est g_World.Self().body[216..288)
// (preuve : g_EntityArray 0x1687234 + 0x18 (body) + 216 = 0x1687324 ; et body+228 =
// 0x1687330 = flt_1687330 = position monde du joueur local, cf. Game/EntityManager.cpp).
#pragma once
#include <cstdint>

namespace ts2::game {

// Bloc de commande de 72 octets — miroir 1:1 du type `TS2_MoveStateBlock` de l'IDB
// (size 72), soit le payload brut copie a this+9 par Net_SendPacket_Op15 0x4B4870
// (`Crt_Memcpy(this + 9, a2, 0x48u)` @0x4B490F).
//
// NB FIDELITE : la regle « tout char sortant est emis sur 4 octets LE » NE S'APPLIQUE PAS
// a l'op 0x0F — le bloc part en memcpy brut de 72 o et ne contient aucun champ `char`.
// Les offsets ci-dessous sont ceux du type IDB, revalides champ par champ contre les
// cadres de pile des builders (cf. .cpp).
struct MoveCmdBlock {
    int32_t animSlot      = 0;    // +0x00 slot d'animation (2*classe_arme, +1 pour les attaques)
    int32_t actionState   = 0;    // +0x04 code action/motion interne (le « opNN » des noms IDB)
    float   animFrame     = 0.0f; // +0x08 frame d'animation courante
    float   pos[3]        = {};   // +0x0C position source
    float   dest[3]       = {};   // +0x18 position cible
    float   facing        = 0.0f; // +0x24 cap courant (deg)
    float   targetFacing  = 0.0f; // +0x28 cap vise (deg) — Math_AngleBetween2D 0x53FB20
    int32_t targetKind    = 0;    // +0x2C categorie de cible
    int32_t targetId1     = 0;    // +0x30 id/grille X de la cible
    int32_t targetId2     = 0;    // +0x34 id/grille Y de la cible
    int32_t activeSkillId = 0;    // +0x38 id de competence active
    int32_t level         = 0;    // +0x3C niveau de competence
    int32_t levelBonus    = 0;    // +0x40 bonus/resistance elementaire
    int32_t combatFlag    = 0;    // +0x44 toujours 0 dans les 8 builders portes
};
static_assert(sizeof(MoveCmdBlock) == 72, "MoveCmdBlock doit faire exactement 72 o (payload op 0x0F)");

// Math_AngleBetween2D 0x53FB20 : cap (deg 0..360) du point (a1,a2) vers (a3,a4).
// Expose ici pour la couche d'intention (les 36 builders de commande l'appellent).
// DEDUPLICATION A FAIRE (hors perimetre de ce front) : deux copies fichier-local
// IDENTIQUES existent deja, Game/AnimationTick.cpp:802 et
// Game/GroundAuraWorldObjectTick.cpp:185 (au service des FX). Elles devraient pointer
// ici — cf. rapport de vague, non bloquant (les 3 transcriptions sont equivalentes).
float AngleBetween2D(float a1, float a2, float a3, float a4);

// PlayerCmdController — g_PlayerCmdController 0x1669170.
//
// Le binaire adresse un enorme struct (bloc combat/action 51480->53184, ~150 champs).
// Seuls les DEUX champs effectivement lus/ecrits par les builders portes sont modelises
// ici ; le reste n'est pas invente (cf. TODO de ResetCombatState).
//
// LE CYCLE INTENTION -> ACQUITTEMENT (etabli par data_refs sur dword_1675B00 = le latch) :
// les builders d'attaque/competence/respawn posent Busy() en emettant, et le latch est
// relache par TROIS chemins distincts — tous prouves, aucun n'est optionnel :
//   1. Pkt_SpawnCharacter @0x464BF0 — mode 3 + self : L'ACQUITTEMENT PAR PAQUET, le
//      chemin nominal. DEJA porte : Game/EntityManager.cpp:492 (et son jumeau opcode 0x15
//      Net/GameHandlers_Entity.cpp:48). C'est lui qui debloque le joueur en jeu.
//   2. Player_ResetCombatState @0x50F6E7 — nouveau slot self (entree dans le monde).
//      Porte ci-dessous, cable depuis Game/EntityManager.cpp (branche nouveau slot).
//   3. Scene_InGameUpdate @0x52C8FF-@0x52C921 — filet de securite par TIMEOUT, teste
//      CHAQUE FRAME : `if (Busy() && g_GameTimeSec - LastCmdTime() > 10.0) Busy() = 0;`
//      (seuil dbl_7EDB88 = 0x4024000000000000 = 10.0, verifie sur les octets).
//      NON PORTE — Scene/SceneManager.cpp est hors du perimetre de ce front ; signale a
//      l'orchestrateur. Sans lui, un paquet d'ack perdu bloque le joueur indefiniment
//      au lieu de 10 s.
class PlayerCmdController {
public:
    // --- Cycle intention -> acquittement ------------------------------------------
    // Player_ResetCombatState 0x50F6A0. UNIQUE appelant prouve (xrefs_to = 1) :
    // Pkt_SpawnCharacter 0x4646C0 @0x4648F2, gate `var_2B0 == 0` (self) DANS la branche
    // « nouveau slot » — donc a l'entree dans le monde, PAS a chaque paquet (l'ack par
    // paquet, c'est le chemin 1 ci-dessus).
    void ResetCombatState();

    // --- Builders d'intention (chacun = un Net_Queue* du binaire) -------------------
    // Tous renvoient true si le paquet a bien ete emis, false si une garde a coupe.

    // Net_QueueRunTo 0x511B00 (actionState=2) — ordre de course vers `dest`.
    // SEUL builder a (a) tester la connexion (MEMORY[0x8156A8] @0x511B31), (b) reecrire
    // le bloc self (@0x511C5C), (c) NE PAS poser `busy`, (d) reporter animFrame.
    bool QueueRunTo(const float dest[3], int32_t targetKind, int32_t targetId1,
                    int32_t targetId2, int32_t activeSkillId, int32_t level,
                    int32_t levelBonus);

    // Net_QueueMoveTo 0x5119B0 (actionState=1) — deplacement vers une position explicite.
    // Le binaire IGNORE ses arguments a3..a8 (targetKind=0 / targetId1=-1 en dur
    // @0x511A6C/@0x511A73) : la signature ne les expose donc pas.
    bool QueueMoveTo(const float dest[3]);

    // Net_QueueMoveResume 0x511870 (actionState=1) — reprise de marche depuis le cap
    // memorise. Le binaire ne prend QUE `this` (aucune destination).
    bool QueueMoveResume();

    // Net_QueueRespawnMove 0x5117A0 (actionState=0) — reapparition. Garde unique :
    // actionState self == 12 (mort). Cap tire au hasard (Rng_Next()%360 @0x511806).
    // a3..a8 ignores par le binaire (targetKind=0 / targetId1=-1 en dur).
    bool QueueRespawnMove(const float pos[3]);

    // Net_QueueAttack5/6/7 0x511EF0 / 0x5120A0 / 0x512250 — identiques a la constante
    // actionState pres (verifie par decompilation des trois). a6..a8 ignores (0 en dur).
    bool QueueAttack5(const float dest[3], int32_t targetKind, int32_t gridX, int32_t gridY);
    bool QueueAttack6(const float dest[3], int32_t targetKind, int32_t gridX, int32_t gridY);
    bool QueueAttack7(const float dest[3], int32_t targetKind, int32_t gridX, int32_t gridY);

    // Player_QueueSkill_op32 0x5134D0 (actionState=32) — lancement de competence.
    // a3..a5 ignores (targetKind=0 / targetId1=-1 / targetId2=0 en dur) ; a6..a8 =
    // skillId / niveau / resistance elementaire.
    bool QueueSkill32(const float dest[3], int32_t skillId, int32_t level, int32_t elemResist);

    // --- Etat (champs prouves du struct 0x1669170) ---------------------------------
    // STOCKAGE UNIQUE — NE PAS DEDOUBLER EN MEMBRES. Le binaire adresse ces deux champs
    // AUSSI en absolu (le gros bloc 0x1669170 recouvre des « globals » nommes par l'IDB) :
    //   g_PlayerCmdController+51600 = 0x1669170+0xC990 = dword_1675B00 (latch)
    //   g_PlayerCmdController+51604 = 0x1669170+0xC994 = flt_1675B04   (horodatage)
    // Recoupe independamment : *(this+40996) de Player_ResetCombatState = 0x1669170+0xA024
    // = 0x1673194 = g_LocalElement, un « global » lui aussi. Le binaire n'a qu'UN stockage.
    // dword_1675B00 est DEJA modelise par la longue traine ClientRuntime — le dedoubler en
    // membre est l'erreur classique du projet (cf. Net/NetClient.h:110, g_LocalElement
    // dedouble -> partait a 0 au handshake) et ici le latch ne retomberait JAMAIS, car les
    // acquittements ecrivent le Var, pas le membre.
    //
    // Ces accesseurs pointent donc le MEME slot que g_Client.Var(0x1675B00) /
    // g_Client.VarF(0x1675B04). Effet de bord voulu : les DEUX acquittements deja ecrits
    // mais jusqu'ici SANS AUCUN LECTEUR (Game/EntityManager.cpp:492 @0x464BF0, mode 3 self,
    // et Net/GameHandlers_Entity.cpp:48, opcode 0x15) deviennent enfin VIVANTS.
    int32_t& Busy();          // +51600 == dword_1675B00
    float&   LastCmdTime();   // +51604 == flt_1675B04

private:
    // Coeur commun : envoie `blk` via Net_SendPacket_Op15 sur net::GlobalNetClient().
    void Send(const MoveCmdBlock& blk);

    // Impl. partagee de Net_QueueAttack5/6/7 (decompiles IDENTIQUES a la constante
    // actionState pres — 5 @0x511F92, 6 @0x512142, 7 @0x5122F2).
    bool QueueAttackImpl(int32_t actionState, const float dest[3], int32_t targetKind,
                         int32_t gridX, int32_t gridY);
};

// Instance unique — g_PlayerCmdController 0x1669170 (le binaire n'en a qu'une, adressee
// en global par les 74 builders via `mov ecx, offset g_PlayerCmdController`).
inline PlayerCmdController g_PlayerCmd;

} // namespace ts2::game

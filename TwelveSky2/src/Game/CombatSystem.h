// Game/CombatSystem.h — Résolution de combat côté client TwelveSky2.
// Réécriture C++ PROPRE (pas byte-exact) de :
//   - cGameData_ApplyCombatResult (0x55A380) : applicateur PUR du paquet de résultat
//     de combat entrant (opcode 21 = 0x15, 76 o) -> délta HP sur la bonne entité.
//   - Combat_QueueMeleeAttack (0x573130) / Combat_QueueSkillAction (0x573200) :
//     builders du paquet d'action SORTANT (opcode 18 = 0x12, payload 76 o).
// Vérité = Docs/TS2_GAMEPLAY_LOGIC.md §5 + désassemblage (IDB RE/TwelveSky2.exe.i64)
// + RE/net_handler_notes.md (Pkt_OnCombatResult 0x468340).
//
// Le client est autoritaire-serveur : ce module N'INVENTE aucun dégât. Il ne fait
// qu'APPLIQUER un résultat calculé par le serveur (délta HP, drapeau de mort) et
// CONSTRUIRE les demandes d'action. Les effets visuels/sonores/texte (Fx_*, Snd3D_*,
// Msg_AppendSystemLine) du binaire d'origine relèvent des couches render/audio/UI et
// sont volontairement hors périmètre de GameState — documentés mais non reproduits ici.
#pragma once
#include <cstdint>
#include "Game/GameState.h"

namespace ts2::game {

// ---------------------------------------------------------------------------
// Paquet de RÉSULTAT de combat — opcode ENTRANT 21 (0x15), 76 o = 19 int32.
// Layout EXACT relevé dans cGameData_ApplyCombatResult (indices a2[0..18]) et
// Docs/TS2_GAMEPLAY_LOGIC.md §5.3/§5.6 (« CombatResult »). Le serveur calcule tout ;
// le client applique appliedDmg1 (a2[16]) et appliedDmg2 (a2[18]).
// ---------------------------------------------------------------------------
struct CombatPacket {
    // a2[0] : type de résultat = branche d'application.
    //   1,2 = victime JOUEUR (2 composantes) ; 3 = victime MONSTRE (comp 1 seule) ;
    //   4   = garde/blocage joueur (comp 1 seule).
    int32_t  resultType  = 0;
    EntityId attacker;            // a2[1..2]  netID hi/lo de l'attaquant
    EntityId victim;              // a2[3..4]  netID hi/lo de la victime
    float    impactX = 0.0f;      // a2[5]     point d'impact monde X
    float    impactY = 0.0f;      // a2[6]                        Y
    float    impactZ = 0.0f;      // a2[7]                        Z
    int32_t  kind        = 0;     // a2[8]     1=melee, 2=skill
    int32_t  skillId     = 0;     // a2[9]     (==78 -> pas de son côté client)
    int32_t  reserved10  = 0;     // a2[10] (+40) réservé
    int32_t  reserved11  = 0;     // a2[11] (+44) réservé
    int32_t  weaponHitId = 0;     // a2[12] (+48) 0 = RATÉ (aucun HP touché)
    int32_t  critMask    = 0;     // a2[13] (+52) bit0=crit HP, bit1=crit 2e barre ; ==1 -> FX garde
    int32_t  absorbed    = 0;     // a2[14] (+56) absorbé (bouclier) — affichage seulement
    int32_t  grossDmg1   = 0;     // a2[15] (+60) brut composante 1 (net affiché = gross1-absorb)
    int32_t  appliedDmg1 = 0;     // a2[16] (+64) délta HP RÉEL, composante 1
    int32_t  grossDmg2   = 0;     // a2[17] (+68) brut composante 2 (2e barre / élément)
    int32_t  appliedDmg2 = 0;     // a2[18] (+72) délta HP RÉEL, composante 2 (joueur uniquement)

    // pkt[12]==0 => coup raté : aucune application HP (le binaire n'affiche qu'un texte).
    bool isMiss() const { return weaponHitId == 0; }

    // Décode un payload brut de 76 o (little-endian) tel que memcpy'é par
    // Pkt_OnCombatResult (0x468340) depuis unk_8156C1.
    static CombatPacket FromRaw(const void* payload76);
};

// ---------------------------------------------------------------------------
// Drapeau de mort du joueur LOCAL (dword_16760D0 = g_SelfDeadFlag). Positionné par
// ApplyCombatResult quand les HP self tomberaient sous 1. Remis à zéro à la renaissance
// (hors périmètre de ce module).
// ---------------------------------------------------------------------------
inline bool g_SelfDead = false;

// ---------------------------------------------------------------------------
// APPLICATION du résultat de combat (cGameData_ApplyCombatResult 0x55A380).
// Opère sur g_World : décrémente les HP de la victime (PlayerEntity.hp / MonsterEntity.hp)
// et arme g_SelfDead si le joueur local meurt. C'est un applicateur PUR (aucun calcul de
// dégât). Cf. Docs/TS2_GAMEPLAY_LOGIC.md §5.4.
// ---------------------------------------------------------------------------
void ApplyCombatResult(const CombatPacket& pkt);

// Variante pratique : décode un payload brut de 76 o puis applique (chemin du handler
// Pkt_OnCombatResult 0x468340).
void ApplyCombatResultRaw(const void* payload76);

// ---------------------------------------------------------------------------
// Contexte d'action de l'entité locale — champs du record entité (908 o) lus par les
// builders d'action (offsets d'origine en commentaire). Rempli par la FSM d'action
// (Char_UpdateAnimationFrame 0x571880) à la frame de contact de l'animation.
// ---------------------------------------------------------------------------
struct CombatActorState {
    EntityId selfId;              // entity+4 / +8   netID du joueur local
    EntityId targetId;           // entity+288 / +292  netID de la cible
    float    x = 0.0f;           // entity+252  position self X
    float    y = 0.0f;           // entity+256                 Y
    float    z = 0.0f;           // entity+260                 Z
    int32_t  facing       = 0;   // entity+244  cap/facing (P[11]) — aussi sélecteur d'état d'action
    int32_t  meleeSubmode = 0;   // entity+284  sous-mode melee : {2,3,5} -> attackSubtype {1,2,3}
    int32_t  skillId      = 0;   // entity+296  compétence courante (0 = melee pur)
    int32_t  skillLevelA  = 0;   // entity+300  (skillLevel = A + B)
    int32_t  skillLevelB  = 0;   // entity+304
};

// ---------------------------------------------------------------------------
// Demande d'action de combat — opcode SORTANT 18 (0x12), payload 76 o = 19 int32.
// Layout : Docs/TS2_GAMEPLAY_LOGIC.md §5.2/§5.6 (« CombatActionRequest »). Les champs
// P[12..18] sont laissés à 0 (le serveur les remplit dans la réponse op21).
// ---------------------------------------------------------------------------
struct CombatActionRequest {
    int32_t  attackSubtype = 0;  // P[0]  1=vs joueur A, 2=vs joueur B, 3=vs monstre, 5=skill-dash, 6=skill-AoE
    EntityId self;               // P[1..2]  netID self
    EntityId target;             // P[3..4]  netID cible
    float    x = 0.0f;           // P[5]  position self X
    float    y = 0.0f;           // P[6]                 Y
    float    z = 0.0f;           // P[7]                 Z
    int32_t  kind       = 0;     // P[8]  1=melee, 2=skill
    int32_t  skillId    = 0;     // P[9]  (0 = melee pur)
    int32_t  skillLevel = 0;     // P[10]
    int32_t  facing     = 0;     // P[11]

    // Sérialise le payload op18 (76 o = 19 int32 LE). P[12..18] = 0.
    void Serialize(int32_t out[19]) const;
    void Serialize(uint8_t out[76]) const;
};

// Builder d'attaque au corps-à-corps (Combat_QueueMeleeAttack 0x573130).
// skillId = compétence associée (0 pour un coup de base). kind forcé à 1 (melee),
// skillLevel forcé à 0.
CombatActionRequest BuildMeleeAttack(const CombatActorState& actor, int32_t skillId);

// Builder d'action de compétence (Combat_QueueSkillAction 0x573200).
// attackSubtype dérivé de la compétence : {4,23,42} -> 5 (dash), {5,24,43} -> 6 (AoE),
// sinon du sous-mode melee. kind forcé à 2 (skill), skillLevel = A+B.
CombatActionRequest BuildSkillAction(const CombatActorState& actor);

} // namespace ts2::game

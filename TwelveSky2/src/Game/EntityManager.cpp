// Game/EntityManager.cpp — implementation du gestionnaire d'entites.
//
// Offsets confirmes par decompilation Hex-Rays (MCP idaTs2). Tous les offsets
// ci-dessous sont RELATIFS au debut du corps (body) copie dans chaque slot, pas
// au debut du record. Rappels de mappage :
//   JOUEUR   record 908 o (g_EntityArray 0x1687234) : active@+0, idHi@+4, idLo@+8,
//            timestamp@+0xC, body@+0x18. body+216 = bloc move-state (72 o) =
//            {moveVal@+0, actionState@+4, animFrame@+8, posX@+12, posY@+16,
//             posZ@+20, ... heading@+36}. => position monde = body+228/232/236
//            (flt_1687330/34/38, lu par le rendu Fx_HomingProjectileUpdate 0x5862d0).
//            body+252 = heading (cap horizontal, degrés) — mission ROTATION/ORIENTATION
//            2026-07-14, CONFIRMÉ par convergence CharAnimState::facingCurrentDeg
//            (entity+276 = body+252, Char_UpdateAnimationFrame 0x571880) ET Char_Draw
//            0x5805C0 (voir le pendant monstre ci-dessous et GameState.h::
//            PlayerEntity::heading pour le détail complet) ; cf. Docs/
//            TS2_ENTITY_ARRAY_DUALITY_CHECK.md.
//            body+48 = nom du personnage (char[], NUL-terminé, mission NAMEPLATES
//            2026-07-14) — confirmé par Char_DrawNameplate 0x56EF40 (this+72 = body+48,
//            `Crt_Vsnprintf(v115, "%s", this+72)`) ; cf. GameState.h::PlayerEntity::name.
//   MONSTRE  record 280 o (dword_1766F74 0x1766F74) : active@+0, idHi@+4, idLo@+8,
//            timestamp@+0xC, body@+0x10. body+4 = move-state (72 o) ; position monde
//            = body+16/20/24 (unk_1766F94/98/9C) ; def@record+0x60 ; rayon@record+0x64 ;
//            body+40 = heading (move-state+36, degrés) — CONFIRMÉ DIRECTEMENT par
//            décompilation de Char_Draw 0x5805C0 : `this` = &dword_1766F74[i] (aucun
//            tableau de rendu intermédiaire, cf. Docs/TS2_ENTITY_ARRAY_DUALITY_CHECK.md
//            §1), `*((float*)this+14)` (= record+56 = body+40) injecté comme composante
//            Y du vecteur de rotation {0,heading,0} passé à SObject_DrawEx ->
//            Model_Render 0x40EBB0 (« compose la matrice monde S*Rz*Ry*Rx*T », rôle IDB) ;
//            le vecteur d'échelle correspondant est câblé en DUR à {1,1,1} dans
//            SObject_DrawEx — donc CE champ est une rotation, jamais une échelle.
//   NPC      record 152 o (dword_17AB534 0x17AB534) : body@+0x10 (84 o) ; def@record+0x64.
//            position monde = body+16/20/24 — confirmé par décompilation Hex-Rays
//            de Char_SelectAuraEffect 0x5835B0 (appelée en fin de Pkt_SpawnNpc
//            0x467EC0, juste après la copie du body) : this+8/9/10 (this = base
//            record &dword_17AB534[38*i]) == record+32/36/40 == body+16/20/24,
//            même convention que le monstre (body+16/20/24). Valeur déjà présente
//            dans le payload réseau copié tel quel dans NpcEntity::body ; aucune
//            rotation confirmée pour ce record (pas ajoutée, pour ne pas inventer).
#include "Game/EntityManager.h"
#include "Game/GameDatabase.h"        // GetMonsterInfo / MonsterInfo (resolution 1-based)
#include "Game/StaticNpcLoader.h"
#include "Game/EntityLifecycleTick.h" // ResetMonsterTickExt/ResetNpcTickExt (cf. TODO ci-dessous)
#include "Game/PlayerCmdController.h" // g_PlayerCmd : Player_ResetCombatState 0x50F6A0 (@0x4648f2)
#include "Game/ClientRuntime.h"       // g_Client : miroirs self (dword_1675884/1675B00) + longue traine Var/VarF
#include "Net/SendPackets.h"          // Net_SendVaultReq_207/Op23 + GlobalNetClient (Pkt_EnterWorld @0x4643d1..)
#include "Gfx/FxSetters.h"            // FxPool_* (pool dword_17D06F4) + Fx_AttachDashTrail (Char_SetupAuraFlags 0x5814F0)

#include <cstring>
#include <cmath>

namespace ts2::game {
namespace {

// ---- helpers de lecture/ecriture bornee sur un buffer d'octets (LE, sans UB d'aliasing).
inline uint32_t RdU32(const uint8_t* b, size_t o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; }
inline int32_t  RdI32(const uint8_t* b, size_t o) { int32_t  v; std::memcpy(&v, b + o, 4); return v; }
inline float    RdF32(const uint8_t* b, size_t o) { float    v; std::memcpy(&v, b + o, 4); return v; }
inline void     WrU32(uint8_t* b, size_t o, uint32_t v) { std::memcpy(b + o, &v, 4); }
inline void     WrI32(uint8_t* b, size_t o, int32_t  v) { std::memcpy(b + o, &v, 4); }
inline void     WrF32(uint8_t* b, size_t o, float    v) { std::memcpy(b + o, &v, 4); }

// ---- offsets JOUEUR (relatifs au body de 600 o).
// NB : kPMoveState / kPMoveStateLen ont DEMENAGE dans Game/EntityManager.h (ils sont
// desormais partages avec Game/PlayerCmdController.*, qui lit/ecrit le meme bloc =
// g_SelfMoveStateBlock 0x1687324). Ne pas les redeclarer ici : le nom du header serait
// masque et les deux definitions deviendraient ambigues.
constexpr size_t kPActionState = 220;  // move-state+4 — ex-VeryOldClient: aType (ACTION_INFO ; aType/aSort permutable, Rosetta §7)
constexpr size_t kPAnimFrame   = 224;  // move-state+8 — ex-VeryOldClient: aFrame
constexpr size_t kPPosX        = 228;  // move-state+12 -> flt_1687330 — ex-VeryOldClient: aLocation[0] (Y/Z suivent)
constexpr size_t kPPosY        = 232;  // flt_1687334
constexpr size_t kPPosZ        = 236;  // flt_1687338
constexpr size_t kPHeading     = 252;  // move-state+36 -> cap horizontal (degrés), cf.
                                        // GameState.h::PlayerEntity::heading pour la
                                        // preuve de décompilation complète (mission
                                        // ROTATION/ORIENTATION, 2026-07-14). MÊME champ
                                        // que CharAnimState::facingCurrentDeg
                                        // (entity+276 = body+252).
                                        // ex-VeryOldClient: aFront (ACTION_INFO) [CONFIRMED, Rosetta §2]
constexpr size_t kPLevelCtr    = 84;   // dword_16872A0 (compteur de niveau par entite)
constexpr size_t kPHp          = 292;  // dword_1687370 (barre de combat AR-min courant)
constexpr size_t kPMp          = 300;  // dword_1687378 (barre de combat AR-max courant)
constexpr size_t kPAnimId      = 272;  // id d'anim/stun issu du spawn
constexpr size_t kPStateArr    = 304;  // dword_168737C : 36 ints d'etat/statut
constexpr size_t kPStateCount  = 36;
constexpr size_t kPPartyGridX  = 556;  // dword_1687478 (op 0x91)
constexpr size_t kPPartyGridY  = 560;  // dword_168747C
constexpr size_t kPPartyPos    = 564;  // unk_1687480 : 3 floats (op 0x91)
constexpr size_t kPPartyPosPad = 576;  // unk_168748C = 0.0f
constexpr size_t kPStunDur     = 592;  // dword_168749C
constexpr size_t kPName        = 48;   // nom du personnage (char[], NUL-terminé) — cf.
                                        // GameState.h::PlayerEntity::name pour la preuve
                                        // de décompilation (Char_DrawNameplate 0x56EF40,
                                        // this+72 = body+48) et la note sur la longueur.
                                        // ex-VeryOldClient: aName [CONFLICT §7 C1 résolu, name@+72].
constexpr size_t kPNameBufLen  = 16;   // borne de lecture (cf. commentaire ci-dessus)

// ---- offsets MONSTRE (relatifs au body de 80 o).
constexpr size_t kMMoveState   = 4;    // move-state (72 o)
constexpr size_t kMMoveStateLen = 72;
constexpr size_t kMPosX        = 16;   // unk_1766F94 — ex-VeryOldClient: mAction.aLocation[0]
constexpr size_t kMPosY        = 20;   // unk_1766F98
constexpr size_t kMPosZ        = 24;   // unk_1766F9C
constexpr size_t kMHeading     = 40;   // move-state[4]+36 -> cap horizontal (degrés),
                                        // cf. GameState.h::MonsterEntity::heading pour
                                        // la preuve de décompilation directe (Char_Draw
                                        // 0x5805C0, mission ROTATION/ORIENTATION,
                                        // 2026-07-14).
                                        // ex-VeryOldClient: mAction.aFront [CONFIRMED, Rosetta §4]

// ---- offsets NPC (relatifs au body de 84 o) — cf. bandeau de tête de fichier.
constexpr size_t kNPosX        = 16;
constexpr size_t kNPosY        = 20;
constexpr size_t kNPosZ        = 24;

// ---- offsets dans un record de definition MONSTER_INFO (pour le rayon de collision).
// Desormais portes par MonsterInfo::collDim[0]/[2] (Game/GameDatabase.h) ; conserves ici
// comme ancre documentaire (Pkt_SpawnMonster 0x467B00 : def[+248]=collDim[0], def[+256]=collDim[2]).
[[maybe_unused]] constexpr size_t kDefDimA = 248;
[[maybe_unused]] constexpr size_t kDefDimB = 256;

// Recherche SEULE (sans allocation) — les handlers d'etat n'agissent que si l'entite
// existe deja (contrairement aux handlers de spawn qui allouent via FindOrAdd*).
PlayerEntity* FindPlayer(EntityId id) {
    for (auto& e : g_World.players)
        if (e.active && e.id == id) return &e;
    return nullptr;
}
MonsterEntity* FindMonster(EntityId id) {
    for (auto& e : g_World.monsters)
        if (e.active && e.id == id) return &e;
    return nullptr;
}
NpcEntity* FindNpc(EntityId id) {
    for (auto& e : g_World.npcs)
        if (e.active && e.id == id) return &e;
    return nullptr;
}

// Resolution d'une definition de MONSTRE via ItemDefTbl_GetRecord (nom IDB trompeur :
// charge MONSTER_INFO, cf. Gfx/ModelCache.h) -> g_World.db.monster. Renvoie nullptr si la
// base est chargee mais l'id hors plage (=> rejet du spawn, comme l'original) ; nullptr
// silencieux si base non chargee. RESERVE aux monstres (OnSpawnMonster) : les PNJ reseau
// utilisent ResolveNpcDef ci-dessous (table DIFFERENTE), cf. Pkt_SpawnNpc 0x467EC0.
const uint8_t* ResolveMobDef(uint32_t id, bool& tableLoaded) {
    const DataTable& t = g_World.db.monster;
    tableLoaded = (t.count != 0);
    if (!tableLoaded) return nullptr;
    // CORRECTION off-by-one : le getter MONSTER 0x4C6570 est STRICTEMENT 1-based
    // (base+944*(id-1), rejet id<1||id>count, garde 1er dword!=0). Pkt_SpawnMonster 0x467B00
    // passe l'id reseau BRUT (1-based, body[0]) -> GetMonsterInfo applique le -1. Table chargee
    // + id invalide => nullptr => OnSpawnMonster rejette le spawn (fidele a `Record==0` de 0x467B00).
    return reinterpret_cast<const uint8_t*>(GetMonsterInfo(id));
}

// Resolution d'une definition de PNJ RESEAU (dword_17AB534) -- transcription EXACTE de
// Pkt_SpawnNpc 0x467EC0 : `MobDb_GetEntry(mITEM, body[0])` -> table ITEM_INFO
// (g_World.db.item), PAS MONSTER_INFO. MobDb_GetEntry 0x4C3C00 est 1-based (record(id-1),
// rejet si id<1 || id>count || slot vide itemId==0) -- MEME semantique que GetItemInfo.
// C'est bien un record ITEM_INFO que lisent tous les consommateurs de NpcEntity::def
// (Game/NpcInteraction.cpp : +184 faction, +188 kind, +232/+236 quete, +352 aggro ;
// Game/AutoPlaySystem.cpp : +0 nom via +4, +184/+188) -- offsets tous DANS le record
// ITEM_INFO de 436 o et conformes a son layout (Game/GameDatabase.h::ItemInfo).
const uint8_t* ResolveNpcDef(uint32_t id, bool& tableLoaded) {
    const DataTable& t = g_World.db.item;
    tableLoaded = (t.count != 0);
    if (!tableLoaded || id < 1 || id > t.count) return nullptr;
    const uint8_t* rec = t.record(id - 1); // 1-based (cf. MobDb_GetEntry 0x4C3C00)
    if (rec) {
        uint32_t itemId = 0;
        std::memcpy(&itemId, rec, 4);
        if (itemId == 0) return nullptr; // slot vide, comme MobDb_GetEntry
    }
    return rec;
}

void ReadPlayerPos(PlayerEntity& e) {
    const uint8_t* b = e.body.data();
    e.x = RdF32(b, kPPosX);
    e.y = RdF32(b, kPPosY);
    e.z = RdF32(b, kPPosZ);
    e.heading = RdF32(b, kPHeading); // cf. PlayerEntity::heading (move-state+36)
}
// Nom du personnage (body+48, cf. GameState.h::PlayerEntity::name) : lecture C-string
// bornee a kPNameBufLen octets (aucun octet garanti hors bornes si le serveur envoie
// un buffer non NUL-termine dans les kPNameBufLen o -> pas d'overrun, juste un nom
// tronque a la longueur max plutot qu'invente).
void ReadPlayerName(PlayerEntity& e) {
    const uint8_t* b = e.body.data() + kPName;
    size_t len = 0;
    while (len < kPNameBufLen && b[len] != 0) ++len;
    e.name.assign(reinterpret_cast<const char*>(b), len);
}
void ReadMonsterPos(MonsterEntity& m) {
    const uint8_t* b = m.body.data();
    m.x = RdF32(b, kMPosX);
    m.y = RdF32(b, kMPosY);
    m.z = RdF32(b, kMPosZ);
    m.heading = RdF32(b, kMHeading); // cf. MonsterEntity::heading (move-state+36)
}
void ReadNpcPos(NpcEntity& e) {
    const uint8_t* b = e.body.data();
    e.x = RdF32(b, kNPosX);
    e.y = RdF32(b, kNPosY);
    e.z = RdF32(b, kNPosZ);
}

// Char_SetActionAnimParams 0x570E70 (switch @0x570ED5 sur a1[61] = entity+244 = anim.state) :
// pose hitCheckActive/hitUsesSkillTable/actionKind/actionSubKind/hitFired (idx156-160).
// SOUS-ENSEMBLE MODELISE de la fonction d'origine (le reste = Fx_Attach*/muzzle idx183-184/
// bloc UI if(!a3) 0x571635 non modelises, cf. commentaire au site d'appel). Table transcrite
// bit-a-bit du binaire (decompilation idaTs2 verifiee). // 0x570E70
//
// NOTE FIDELITE idx157 (hitUsesSkillTable, bool) : le binaire ecrit a1[157]=1 (case 5/6/7)
// ou a1[157]=2 (cas competence) ; le SEUL lecteur (Char_UpdateAnimationFrame 0x571880 ->
// Game/ActionStateMachine.cpp) le teste en booleen (`if (hitUsesSkillTable)`, 0x571936/57194D),
// donc 1 et 2 sont tous deux "true" a la lecture. hitUsesSkillTable=true dans TOUTES les
// branches non-default (perte 1-vs-2 sans consequence observable).
void Char_ApplyActionAnimParams(CharAnimState& a) {
    // a1[156]=0 (@0x570E95) — pose inconditionnelle avant le switch.
    a.hitCheckActive = false;
    switch (a.state) {
    case 5: case 6: case 7:                                     // @0x570EDF (157=1)
    case 0x26: case 0x47: case 0x48: case 0x57: case 0x58:      // @0x571196 (157=2)
    case 0x27: case 0x2D: case 0x2E: case 0x33: case 0x34:      // @0x571380 (157=2)
    case 0x2A: case 0x2B: case 0x30: case 0x31:                 // @0x571498 (157=2)
    case 0x45: case 0x46: case 0x55: case 0x56:
        // 156=1, 157=1|2, 158=1, 159=1, 160=0
        a.hitCheckActive = true; a.hitUsesSkillTable = true;
        a.actionKind = 1; a.actionSubKind = 1; a.hitFired = false; break;
    case 0x2C: case 0x32: case 0x38: case 0x52:                 // @0x5715B0
    case 0x51:                                                  // @0x57156A
    case 0x53:                                                  // @0x5715F3
        // 156=1, 157=2, 158=1, 159=2, 160=0
        a.hitCheckActive = true; a.hitUsesSkillTable = true;
        a.actionKind = 1; a.actionSubKind = 2; a.hitFired = false; break;
    case 0x36: case 0x37:                                       // @0x5713C6
    case 0x39: case 0x3A: case 0x49: case 0x4A: case 0x59: case 0x5A: // @0x571452
        // 156=1, 157=2, 158=2, 159=1, 160=0
        a.hitCheckActive = true; a.hitUsesSkillTable = true;
        a.actionKind = 2; a.actionSubKind = 1; a.hitFired = false; break;
    default:                                                    // @0x570ED5 default : 156=0, reste inchange
        break;
    }
}

// ---- helpers op 0x10 (Pkt_CharStateUpdate 0x464c10) : miroirs SELF + resets de combo.
//
// Le bloc dword_16758D8 (288 o) est modelise par g_World.self.zoneState. Le binaire l'adresse
// toujours (tableau BSS de 288 o) ; on garantit donc la taille avant ecriture (EnterWorld le
// remplit normalement d'abord). dword_16758D8[2*i] = zoneState[8*i], dword_16758DC[2*i] =
// zoneState[8*i+4]. // 0x46530c / 0x465326
inline void WrZoneU32(size_t off, uint32_t v) {
    auto& zs = g_World.self.zoneState;
    if (zs.size() < off + 4) zs.resize(288, 0); // dword_16758D8 = 0x120 o dans le binaire
    std::memcpy(zs.data() + off, &v, 4);
}

// Slots « compteurs de combo » = indices 9 et 29..34 du tableau d'etat dword_168737C
// (dword_16873A0 = 168737C+4*9 ; dword_16873F0..1687404 = 168737C+4*29..+4*34), cf. la
// decouverte transverse : ce ne sont PAS des globales distinctes mais des slots du meme tableau.
constexpr int kComboSlots[7] = { 9, 29, 30, 31, 32, 33, 34 };

// NON-SELF : remet a zero le seul slot d'etat (dword_168737C[227*v7+j]).
inline void ZeroStateSlot(uint8_t* b, int j) {
    WrU32(b, kPStateArr + 4 * j, 0);
}
// SELF (branche v7==0) : zeroe AUSSI la paire miroir dword_16758D8[2*j]/DC[2*j] (zoneState
// 8*j / 8*j+4) et l'horodatage flt_16759F8[j] (VarF 0x16759F8+4*j), cf. branche @0x465469..
inline void ZeroSelfSlot(uint8_t* b, int j) {
    WrZoneU32(8 * j, 0);                       // dword_16758D8[2*j]=0
    WrZoneU32(8 * j + 4, 0);                   // dword_16758DC[2*j]=0
    g_Client.VarF(0x16759F8 + 4 * j) = 0.0f;   // flt_16759F8[j]=0.0
    WrU32(b, kPStateArr + 4 * j, 0);           // dword_168737C[227*v7+j]=0
}
// Reinitialise tous les slots de kComboSlots SAUF skipA/skipB (cas 9/29..34). self=true
// reproduit la branche self (@0x465469..) avec miroir+horodatage, sinon l'etat seul (@0x464eb0..).
inline void ResetComboSlots(uint8_t* b, bool self, int skipA, int skipB) {
    for (int j : kComboSlots) {
        if (j == skipA || j == skipB) continue;
        if (self) ZeroSelfSlot(b, j);
        else      ZeroStateSlot(b, j);
    }
}

// ---- Traînée de dash de MONSTRE — Char_SetupAuraFlags 0x5814F0 (sous-chemin dash-trail SEUL).
// Unique appelant : Pkt_SpawnMonster 0x467B00 @0x467DA6, sur le NOUVEAU slot uniquement (apres
// resolution de la def + rayon). Le binaire teste la CLASSE de modele (def+244 =
// MonsterInfo::kindIndexP1) via un switch, puis, si l'etat de vitesse (def+236 =
// MonsterInfo::field236) est dans [2,6], attache une trainee PARTICULAIRE (type 5) au 1er slot
// libre du pool dword_17D06F4 (< g_FxAuraCount). Deux groupes de classes :
//   {42,44,46,59,61,64,65,67,74,75,76,85,89} -> side 1 (def 18)   @0x5815e1
//   {48,53,62,66,72,81}                       -> side 2 (def 19)   @0x581652
// PRODUCTEUR SEUL : le tick + le rendu des slots type 5 existent deja (SceneManager / FxRenderer,
// Vague D) ; on ne touche donc ni l'un ni l'autre. Cf. Gfx/FxSetters.cpp::Fx_AttachDashTrail et
// Docs/TS2_SWEEP_ENTITY_FX.md §4.
//
// NB fidelite : Char_SetupAuraFlags remet aussi a 0 des champs d'aura du record monstre
// (this+27/53/64/66/68 = record+108/212/256/264/272), TOUS au-dela du body de 80 o modelise
// (def@+96, rayon@+100) et sans consommateur dans ClientSource -> NON reproduits (regle
// « non prouve / non lu = absent »).

// FxEntitySource depuis un monstre : seuls idHi/idLo sont lus par le chemin particule
// (a3[1]/a3[2]) ; l'ancre modele (a3[24]+244) n'est PAS lue (cf. Fx_AttachDashTrail, d[30]=0).
// Equivalent local du SourceFromMonster de Net/CombatResultApply.cpp (namespace anonyme non partage).
gfx::FxEntitySource FxSourceFromMonster(const MonsterEntity& m) {
    gfx::FxEntitySource s;
    s.idHi = m.id.hi;   // a3[1]  record+4
    s.idLo = m.id.lo;   // a3[2]  record+8
    return s;
}

// Cote de trainee (1/2) pour une classe de modele, ou 0 si aucune (switch @0x581570 sur def+244).
int MonsterDashTrailSide(int32_t modelClass) {
    switch (modelClass) {
        case 42: case 44: case 46: case 59: case 61: case 64: case 65:
        case 67: case 74: case 75: case 76: case 85: case 89:
            return 1;                                          // def 18 (@0x5815e1)
        case 48: case 53: case 62: case 66: case 72: case 81:
            return 2;                                          // def 19 (@0x581652)
        default:
            return 0;                                          // default : aucune trainee (@0x581657)
    }
}

// Char_SetupAuraFlags 0x5814F0 (sous-chemin dash-trail) : attache la trainee au monstre `m`
// fraichement spawne, si sa classe le prevoit ET si l'etat de vitesse est dans [2,6].
void AttachMonsterDashTrail(const MonsterEntity& m) {
    if (!m.def) return;                                        // gate `*(this+24)` (def resolu)
    const auto* mi = reinterpret_cast<const MonsterInfo*>(m.def);
    const int side = MonsterDashTrailSide(mi->kindIndexP1);    // *(def+244) — switch de classe @0x58154a
    if (side == 0) return;
    const int32_t speed = mi->field236;                        // *(def+236) — etat de vitesse
    if (speed < 2 || speed > 6) return;                        // gate @0x581590 (grp1) / @0x581601 (grp2)
    const int j = gfx::FxPool_FindFreeSlot();                  // for i<g_FxAuraCount && dword_17D06F4[64*i]
    if (j < 0) return;                                         // pool plein (i==g_FxAuraCount) -> aucun attach
    gfx::Fx_AttachDashTrail(&gfx::FxPool_Slots()[j], FxSourceFromMonster(m), side); // @0x5815e1 / @0x581652
}

} // namespace (anonyme)

// ---------------------------------------------------------------------------
// op 0x0c — Pkt_EnterWorld : reset des tableaux d'entites + copie des blocs.
// ---------------------------------------------------------------------------
void EntityManager::OnEnterWorld(const net::EnterWorld& p) {
    // RESET de tous les tableaux d'entites (l'original despawn slot par slot ;
    // vider les vecteurs est l'equivalent propre — index 0 = self sera repeuple
    // par le prochain Pkt_SpawnCharacter).
    g_World.players.clear();
    g_World.monsters.clear();
    g_World.npcs.clear();
    g_World.groundItems.clear();
    groundPickup_.clear();

    // Copie des deux blocs memoire portes par le payload :
    //   selfCharInvBlock (10088 o) -> g_SelfCharInvBlock (perso local + inventaire)
    //   zoneStateBlock   (288 o)   -> dword_16758D8 (etat de zone)
    g_World.self.charInvBlock.assign(p.selfCharInvBlock, p.selfCharInvBlock + sizeof(p.selfCharInvBlock));
    g_World.self.zoneState.assign(p.zoneStateBlock, p.zoneStateBlock + sizeof(p.zoneStateBlock));

    // Arme la bascule de scene EnterWorld -> InGame : fidele a dword_1676180=6 ecrit
    // DIRECTEMENT par Pkt_EnterWorld dans le binaire (PAS une transition normale de la
    // machine d'etat EnterWorldFlow, qui ne detecte qu'un timeout de secours). Consomme
    // par SceneManager::Update() (case Scene::EnterWorld), cf. GameState.h::GameWorld::
    // sceneEnterWorldPending et Docs/TS2_ENTERWORLD_WIRING_TODO.md.
    g_World.sceneEnterWorldPending = true;

    // TODO(scene) ancre : Pkt_EnterWorld pose aussi g_SceneSubState=0 (@0x46430e) et
    // dword_1676188=0 (@0x464318). g_SceneSubState est prive dans SceneManager (Scene/*, non
    // possede par ce front) -> laisse au proprietaire (cf. App/PlayerInputController.cpp:21). // 0x464160

    // Palier de croissance dword_1675D90 = f(g_GrowthIndex) @0x464329-0x464394. L'ENTREE
    // g_GrowthIndex 0x1674774 est modelisee par g_World.self.growthIndex (GameState.h:396 ;
    // la cle Var 0x1674774 est MORTE, cf. Net/GameHandlers_Misc.cpp:422). Le palier derive a
    // des consommateurs reels (UI_WishA_Open 0x600059, Pkt_ItemActionDispatch 0x477abc). // 0x464160
    {
        const int gi = g_World.self.growthIndex;
        int32_t tier;
        if      (gi < 1)   tier = 0; // @0x46432b
        else if (gi < 100) tier = 1; // @0x464340
        else if (gi < 200) tier = 2; // @0x464358
        else if (gi < 300) tier = 3; // @0x464370
        else if (gi < 400) tier = 4; // @0x464388
        else               tier = 5; // @0x464394
        g_Client.Var(0x1675D90) = tier;
    }

    // Resets @0x46439e/0x4643a8/0x4643b8 (consommateurs reels : Pkt_SetGameVar,
    // Char_CalcExternalAttack, famille UI_ShareBox_*).
    g_Client.Var(0x16760D8) = 0;                          // dword_16760D8 @0x46439e
    g_Client.Var(0x16760DC) = 0;                          // dword_16760DC @0x4643a8
    g_Client.Var(0x16760E0) = g_Client.VarGet(0x1675800); // dword_16760E0 = dword_1675800 @0x4643b8

    // Requetes de suivi emises par Pkt_EnterWorld selon dword_1675A8C (@0x4643d1/0x4643da/
    // 0x4644ad). Le binaire adresse g_NetClient en GLOBAL -> emission via le singleton, meme
    // motif que MapWarp/ArmFullWarp. Builders (code mort jusqu'ici) : Net_SendVaultReq_207
    // 0x590480, Net_SendPacket_Op23 0x4b5490. Verrou anti-spam g_GmCmdCooldownLatch 0x1675B08.
    if (auto* c = ts2::net::GlobalNetClient()) {
        const int32_t warpMode = g_Client.VarGet(0x1675A8C);        // dword_1675A8C
        if (warpMode == 5) {                                        // @0x4643d1
            const int32_t tgtZone = g_Client.VarGet(0x1675A9C);     // g_TargetZoneId
            const int32_t a0      = g_Client.VarGet(0x16692A0);     // dword_16692A0
            const int32_t b0      = g_Client.VarGet(0x167588C);     // dword_167588C
            if ((tgtZone == 84 || (a0 != 2 && b0 <= 0 && a0 != 3))
                && g_Client.VarGet(0x1675B08) == 0) {               // @0x464436
                ts2::net::Net_SendVaultReq_207(*c, g_Client.VarGet(0x1675A90)); // dword_1675A90 @0x464448
                g_Client.Var(0x1675B08) = 1;                        // @0x46444d
                g_Client.VarF(0x1675B0C) = g_World.gameTimeSec;     // flt_1675B0C @0x46445d
            }
        } else if (warpMode == 8 || warpMode == 9) {               // @0x4643da / @0x4644ad
            if (g_Client.VarGet(0x1675B08) == 0) {
                ts2::net::Net_SendPacket_Op23(*c,
                    static_cast<int8_t>(g_Client.VarGet(0x1675D88)), // dword_1675D88
                    static_cast<int8_t>(g_Client.VarGet(0x1675D8C)), // dword_1675D8C
                    static_cast<int8_t>(warpMode));                  // 8 @0x464489 / 9 @0x4644c5
                g_Client.Var(0x1675B08) = 1;
                g_Client.VarF(0x1675B0C) = g_World.gameTimeSec;
            }
        }
    }

    // TODO(state) ancre : le binaire finit par Player_CheckStateDigit(&g_PlayerCmdController)
    // @0x4644ea (valeur de retour du handler). g_PlayerCmdController (module PlayerCmdController)
    // n'est ni modelise ni possede par ce front -> non porte. // 0x464160
}

// ---------------------------------------------------------------------------
// op 0x0f — Pkt_SpawnCharacter : creation/mise a jour d'un personnage.
// ---------------------------------------------------------------------------
PlayerEntity* EntityManager::OnSpawnCharacter(const net::SpawnCharacter& p) {
    const EntityId id{ p.idHi, p.idLo };
    PlayerEntity* existing = FindPlayer(id);

    if (!existing) {
        // --- NOUVEAU slot : init complete (le corps porte deja position + etats).
        PlayerEntity* e = g_World.FindOrAddPlayer(id);
        e->timestamp = g_World.gameTimeSec;
        std::memcpy(e->body.data(), p.body, e->body.size());
        ReadPlayerPos(*e);
        ReadPlayerName(*e);
        // Sequence spawn-anim de Pkt_SpawnCharacter 0x4646C0 (tout nouveau slot) :
        //   Char_RefreshStatusEffectVisuals 0x570890 (@0x4648bc) puis Char_SetActionAnimParams
        //   0x570E70 (@0x4648da). Sous-ensemble MODELISE sur CharAnimState (idx156-160/221) ;
        //   Fx_Attach*/muzzle/UI restent TODO ancre (pool g_FxPool/dword_17D06F4 non attache,
        //   aucune aura active a ce spawn -> boucles Fx_Attach* = no-op fidele).
        e->anim.state = RdI32(e->body.data(), kPActionState); // a1[61]=entity+244=body+220 (0x570ED5)
        // (Vague G — traîne d'arme) Gate maître de Char_DrawWeaponEffectVariantB 0x56BF90 (@0x56c01b :
        // weaponAnimSlot(this+55=entity+220=body+196) != 0 && !altWeaponSet(this+144=entity+576=body+552)).
        // Alimentés au spawn comme anim.state ci-dessus -> sinon resolveWeaponTrail échoue au gate =
        // aucune traîne (dégradation propre). TODO(ancre) : recopier depuis les paquets d'action/skill
        // qui changent l'anim active (Pkt_ItemActionDispatch/skill) pour refléter les casts en cours de jeu.
        e->anim.weaponAnimSlot = RdI32(e->body.data(), 196);      // entity+220 = body+196 (idx55)
        e->anim.altWeaponSet   = RdI32(e->body.data(), 552) != 0; // entity+576 = body+552 (idx144)
        e->anim.fxAuraAttachedLatch = false; // a1[221]=0 (Char_RefreshStatusEffectVisuals 0x570936)
        Char_ApplyActionAnimParams(e->anim); // switch 0x570ED5 (pose 156=0 puis idx156-160)
        // TODO ancre : Fx_Attach* (0x570890/0x570E70), Char_UpdateWeaponGlowState 0x55D740,
        //   muzzle idx183/184 (case 0xC @0x570F25), bloc UI if(!a3) (0x571635) — non modelises.
        // Si index 0 (self) : l'original relance le combat local + les effets de map
        // et met g_SceneSubState=3 ; effets de scene hors perimetre entite.
        if (IsSelf(e)) {
            // Player_ResetCombatState 0x50F6A0 (@0x4648f2, self uniquement) : reset du bloc
            // combat/action de g_PlayerCmdController. Ses effets VISIBLES restent couverts
            // ailleurs (play BGM gate g_BgmEnabled 0x50F76E -> SceneManager::LoadZoneBgm ;
            // Net_SendOp64 poll 0x50F746 -> host.SendPendingTargetPoll InGameTickFlow).
            //
            // Le latch « commande en vol » (+51600 = dword_1675B00) A DESORMAIS un
            // consommateur : la couche d'intention Game/PlayerCmdController.* le pose en
            // emettant l'op 0x0F. L'ancien commentaire « le reset interne n'a aucun
            // consommateur ClientSource » est donc PERIME. Ce reset-ci couvre l'ENTREE
            // DANS LE MONDE (gate `var_2B0 == 0` @0x4648df, branche nouveau slot) ;
            // l'acquittement PAR PAQUET du jeu courant, lui, est le `mode 3 + self` plus
            // bas dans cette meme fonction (@0x464BF0, ligne ~492) — les deux ecrivent le
            // MEME slot (g_Client.Var(0x1675B00)), conformement au binaire qui n'a qu'un
            // stockage. xrefs_to(0x50F6A0) = 1 SEUL appelant, exactement ici (@0x4648f2).
            g_PlayerCmd.ResetCombatState();
            // EQUIVALENT de l'appel cGameData_LoadZoneNpcInfo(g_LocalPlayerSheet) @0x4648fc
            // (garde `if (!i)` de Pkt_SpawnCharacter 0x4646C0) : repeuple les PNJ de decor
            // statiques de la zone courante. Cf. Game/StaticNpcLoader.h pour le detail ; le
            // suivi de g_World.zoneId au warp est desormais assure par SceneManager::ReloadZone
            // (Passe 3 W2-F1), donc LoadZoneNpcs voit la bonne zone au (re)spawn self.
            LoadZoneNpcs(g_World.zoneId);
        }
        return e;
    }

    // --- slot EXISTANT : rafraichissement + logique de mode.
    PlayerEntity* e = existing;
    e->timestamp = g_World.gameTimeSec;
    uint8_t* b = e->body.data();

    // Sauvegarde de l'ancien move-state AVANT ecrasement (par defaut on le conserve).
    uint8_t oldMove[kPMoveStateLen];
    std::memcpy(oldMove, b + kPMoveState, kPMoveStateLen);
    const int oldActionState = RdI32(oldMove, 4); // action-state = move-state+4

    // Copie du nouveau corps complet, puis restauration de l'ancien move-state.
    std::memcpy(b, p.body, e->body.size());
    std::memcpy(b + kPMoveState, oldMove, kPMoveStateLen);

    const int newActionState = RdI32(p.body, kPActionState);
    const int newAnimId      = RdI32(p.body, kPAnimId);

    if (p.mode == 1) {
        // Garde : ne pas interrompre certains etats d'action en cours.
        bool skip = false;
        if (oldActionState == 11) {
            if (newActionState != 1 && newActionState != 12) skip = true;
        } else if (oldActionState == 12 && newActionState != 0) {
            skip = true;
        }
        if (!skip) {
            // Applique le nouveau move-state (donc nouvelle position + anim).
            std::memcpy(b + kPMoveState, p.body + kPMoveState, kPMoveStateLen);

            // Cas action-state==2 : preserve la frame d'anim si le mouvement n'a pas change.
            if (newActionState == 2) {
                if (!IsSelf(e)) {
                    const int newMove0 = RdI32(p.body, kPMoveState);
                    const int oldMove0 = RdI32(oldMove, 0);
                    const int oldMove1 = RdI32(oldMove, 4);
                    if (newMove0 == oldMove0 && newActionState == oldMove1) {
                        float oldFrame; std::memcpy(&oldFrame, oldMove + 8, 4);
                        std::memcpy(b + kPAnimFrame, &oldFrame, 4);
                    }
                } else {
                    // self : restaure entierement l'ancien move-state.
                    std::memcpy(b + kPMoveState, oldMove, kPMoveStateLen);
                }
            }

            // Char_SetActionAnimParams 0x570E70 REJOUE sur le slot EXISTANT (mode==1) — @0x464b15,
            // apres l'application du nouveau move-state et du bloc actionState==2, juste avant le
            // switch de stun. Lit l'action-state COURANT du body (a1[61]=body+220) : pour
            // self+actionState==2 le move-state a ete restaure a l'ancien ci-dessus, donc l'ancien
            // action-state — fidele au binaire. Miroir de la sequence du slot neuf (lignes 299-301,
            // sans Char_RefreshStatusEffectVisuals qui n'est PAS appele sur cette branche).
            e->anim.state = RdI32(b, kPActionState);
            Char_ApplyActionAnimParams(e->anim);

            // Duree de stun selon l'id d'anim (body+272), DOUBLEE du miroir self dword_1675884
            // (@0x464b69/0x464ba0/0x464bd7) — lu par UI_GameHud_Render 0x67a3c0 (3 sites).
            if ((newAnimId >= 139 && newAnimId <= 145) || (newAnimId >= 147 && newAnimId <= 149)) {
                WrI32(b, kPStunDur, 90);
                if (IsSelf(e)) g_Client.Var(0x1675884) = 90; // dword_1675884 @0x464b69
            } else if (newAnimId == 146) {
                WrI32(b, kPStunDur, 60);
                if (IsSelf(e)) g_Client.Var(0x1675884) = 60; // @0x464ba0
            } else if (newAnimId == 150) {
                WrI32(b, kPStunDur, 30);
                if (IsSelf(e)) g_Client.Var(0x1675884) = 30; // @0x464bd7
            }
        }
    } else if (p.mode == 3 && IsSelf(e)) {
        // self, spawn local : dword_1675B00=0 @0x464bf0 (latch consomme par ~20 sites —
        // Scene_InGameUpdate/UI_GameHud_ProcNet/Player_CastSkill…). Meme motif que
        // Net/GameHandlers_Entity.cpp:48 (opcode 0x15).
        //
        // C'EST L'ACQUITTEMENT PAR PAQUET DE L'OP 0x0F (chemin nominal) : dword_1675B00
        // == g_PlayerCmdController+51600 (0x1669170+0xC990) == le latch pose par les
        // builders d'intention de Game/PlayerCmdController.* — c.-a-d. le MEME slot que
        // g_PlayerCmd.Busy(), pas un miroir. Cette ecriture etait jusqu'ici SANS LECTEUR ;
        // elle est ce qui debloque le joueur apres chaque attaque/competence.
        g_Client.Var(0x1675B00) = 0;
    }
    // mode==2 : aucun effet sur le slot existant (fidele au handler d'origine).

    ReadPlayerPos(*e);  // rafraichit x/y/z depuis le move-state courant.
    ReadPlayerName(*e); // le nom fait partie du body recopie en entier ci-dessus (ligne 175).
    return e;
}

// ---------------------------------------------------------------------------
// op 0x12 — Pkt_SpawnMonster : creation/mise a jour d'un monstre.
// ---------------------------------------------------------------------------
MonsterEntity* EntityManager::OnSpawnMonster(const net::SpawnMonster& p) {
    const EntityId id{ p.idHi, p.idLo };
    MonsterEntity* existing = FindMonster(id);

    if (existing) {
        // Rafraichissement : conserve l'ancien move-state sauf si updateFlag==1.
        MonsterEntity* m = existing;
        m->timestamp = g_World.gameTimeSec;
        uint8_t* b = m->body.data();
        uint8_t oldMove[kMMoveStateLen];
        std::memcpy(oldMove, b + kMMoveState, kMMoveStateLen);
        std::memcpy(b, p.body, m->body.size());
        std::memcpy(b + kMMoveState, oldMove, kMMoveStateLen);
        if (p.updateFlag == 1)
            std::memcpy(b + kMMoveState, p.body + kMMoveState, kMMoveStateLen);
        ReadMonsterPos(*m);
        return m;
    }

    // Nouveau : resolution de la definition AVANT allocation (rejet si id invalide).
    const uint32_t defId = RdU32(p.body, 0);
    bool tableLoaded = false;
    const uint8_t* def = ResolveMobDef(defId, tableLoaded);
    if (tableLoaded && !def)
        return nullptr; // id de modele inconnu -> pas de spawn (fidele a l'original).

    MonsterEntity* m = g_World.FindOrAddMonster(id);
    m->timestamp = g_World.gameTimeSec;
    std::memcpy(m->body.data(), p.body, m->body.size());
    m->def = def;

    // Le slot d'index (m - monsters.data()) peut avoir ete recycle depuis un AUTRE
    // monstre (slot libre reutilise par FindOrAdd, cf. GameState.cpp) : on purge son
    // extension de tick (Game/EntityLifecycleTick.h) pour eviter qu'un vieux
    // fallOffset/etat d'aura ne "fuite" sur cette nouvelle entite -- ferme le TODO
    // precis laisse en tete d'EntityLifecycleTick.h ("a accrocher dans
    // EntityManager::OnSpawnMonster/OnSpawnNpc").
    ResetMonsterTickExt(static_cast<int>(m - g_World.monsters.data()));

    // Rayon de collision : Pkt_SpawnMonster 0x467B00 calcule
    // sqrt(def[+256]^2 + def[+248]^2)*0.5 (= collDim[2], collDim[0]). `def` est desormais
    // garanti = MonsterInfo* (resolu par GetMonsterInfo) -> acces type.
    if (def) {
        const auto* mi = reinterpret_cast<const MonsterInfo*>(def);
        const double s = static_cast<double>(mi->collDim[0]) * mi->collDim[0]
                       + static_cast<double>(mi->collDim[2]) * mi->collDim[2];
        m->radius = static_cast<float>(std::sqrt(s) * 0.5);
    }

    // Char_SetupAuraFlags 0x5814F0 (@0x467da6, apres le rayon de collision) : trainee de dash
    // particulaire selon la classe de modele (def+244) + l'etat de vitesse (def+236 in [2,6]).
    // PRODUCTEUR SEUL — le tick/rendu des slots type 5 sont deja cables (SceneManager/FxRenderer).
    // (Char_UpdateMotionState 0x5816a0, appele juste apres @0x467dc4, releve du move-state : non
    // porte ici — non touche par ce front.)
    AttachMonsterDashTrail(*m);

    ReadMonsterPos(*m);
    return m;
}

// ---------------------------------------------------------------------------
// op 0x13 — Pkt_SpawnNpc : creation / rafraichissement / despawn (action==3).
// ---------------------------------------------------------------------------
NpcEntity* EntityManager::OnSpawnNpc(const net::SpawnNpc& p) {
    const EntityId id{ p.idHi, p.idLo };
    NpcEntity* existing = FindNpc(id);

    if (existing) {
        if (p.action == 3) {
            // Despawn : libere le slot (sub_583390).
            *existing = NpcEntity{};
            return nullptr;
        }
        // Rafraichissement.
        existing->timestamp = g_World.gameTimeSec;
        std::memcpy(existing->body.data(), p.body, existing->body.size());
        bool loaded = false;
        existing->def = ResolveNpcDef(RdU32(p.body, 0), loaded);
        existing->action = p.action;
        ReadNpcPos(*existing);
        return existing;
    }

    if (p.action == 3)
        return nullptr; // despawn d'un NPC inconnu -> rien.

    // Nouveau : resolution de la definition avant allocation (rejet si id invalide).
    // ITEM_INFO (ResolveNpcDef = MobDb_GetEntry(mITEM), cf. Pkt_SpawnNpc 0x467EC0), PAS
    // la table monstre.
    bool tableLoaded = false;
    const uint8_t* def = ResolveNpcDef(RdU32(p.body, 0), tableLoaded);
    if (tableLoaded && !def)
        return nullptr;

    NpcEntity* e = g_World.FindOrAddNpc(id);
    e->timestamp = g_World.gameTimeSec;
    std::memcpy(e->body.data(), p.body, e->body.size());
    e->def = def;
    e->action = p.action;
    ReadNpcPos(*e);

    // Meme purge que OnSpawnMonster ci-dessus (slot potentiellement recycle) --
    // ferme le TODO precis d'EntityLifecycleTick.h pour la branche NPC.
    ResetNpcTickExt(static_cast<int>(e - g_World.npcs.data()));

    return e;
}

// ---------------------------------------------------------------------------
// op 0x10 — Pkt_CharStateUpdate : pose/efface 36 bitfields d'etat d'un personnage.
// ---------------------------------------------------------------------------
void EntityManager::OnCharStateUpdate(const net::CharStateUpdate& p) {
    // Pkt_CharStateUpdate 0x464c10. Layout: [op][idHi@8156C1][idLo@8156C5]
    //   [stateValues 288o = 36 paires @8156C9][stateFlags 144o = 36 @8157E9] = 441 o.
    PlayerEntity* e = FindPlayer({ p.entityIdHi, p.entityIdLo });
    if (!e) return; // v7 == -1 : le binaire ne fait rien si l'entite n'existe pas.

    uint8_t* b = e->body.data();
    const bool self = IsSelf(e); // branchement if(v7)/else @0x464d1a : v7==0 => SELF.

    for (int i = 0; i < static_cast<int>(kPStateCount); ++i) {
        const uint32_t flag  = p.stateFlags[i];          // v8[i]
        const uint32_t val   = p.stateValues[2 * i];     // v3[2*i]
        const uint32_t extra = p.stateValues[2 * i + 1]; // v3[2*i+1]

        if (flag == 1) {
            if (self) {
                // Branche SELF @0x46529e : miroir dword_16758D8/DC + horodatage AVANT le switch.
                WrZoneU32(8 * i, val);                                   // dword_16758D8[2*i] @0x46530c
                WrZoneU32(8 * i + 4, extra);                             // dword_16758DC[2*i] @0x465326
                g_Client.VarF(0x16759F8 + 4 * i) = g_World.gameTimeSec;  // flt_16759F8[i]     @0x465339
                WrU32(b, kPStateArr + 4 * i, val);                       // dword_168737C[i]   @0x46535f
                switch (i) {
                    // Cas 15/16/17/18 : le binaire joue un son (Snd3D_PlayScaledVolume, dernier
                    //   arg=1 en self @0x46537a/0x4653c2/0x46540a/0x465452) et pose des drapeaux/
                    //   timers d'effet (dword_1687598 @0x46538b, unk_168759C @0x4653a3,
                    //   dword_16875AC @0x4653e1, unk_16875B0 @0x4653f9, dword_16875B4 @0x465429,
                    //   flt_16875B8 @0x465441). Ces drapeaux sont ECRITURE SEULE (xrefs_to = 2 refs,
                    //   toutes dans Pkt_CharStateUpdate ; unk_168759C = 0 ref) et vivent a
                    //   record+868..900, HORS du body de 600 o modelise -> non portables sans effet
                    //   observable ; audio non branche ici. TODO ancre (0x464c10).
                    case 15: case 16: case 17: case 18: break;
                    case 9:  ResetComboSlots(b, true, 9, -1);  break; // @0x465469 (skip slot 9)
                    case 29: ResetComboSlots(b, true, 29, -1); break; // @0x4655a7
                    case 30: ResetComboSlots(b, true, 30, -1); break; // @0x4656e5
                    case 31: ResetComboSlots(b, true, 31, -1); break; // @0x465823
                    case 32: ResetComboSlots(b, true, 32, -1); break; // @0x465961
                    case 33: ResetComboSlots(b, true, 33, -1); break; // @0x465a9f (self: saute 33 seul, 6 resets)
                    case 34: ResetComboSlots(b, true, 34, -1); break; // @0x465bdd (self: saute 34 seul, 6 resets)
                    case 35: ZeroSelfSlot(b, 35);              break; // @0x465d14 (auto-annulant : ecrit puis efface)
                    default: break;
                }
            } else {
                // Branche NON-SELF @0x464d1a.
                WrU32(b, kPStateArr + 4 * i, val); // dword_168737C[227*v7+i] @0x464d9a
                switch (i) {
                    // Sons @0x464db5/0x464dfd/0x464e1b/0x464e63 (arg=0) + memes drapeaux morts —
                    //   voir la branche SELF ci-dessus (non portes, TODO ancre 0x464c10).
                    case 15: case 16: case 17: case 18: break;
                    case 9:  ResetComboSlots(b, false, 9, -1);  break; // @0x464eb0 (skip slot 9)
                    case 29: ResetComboSlots(b, false, 29, -1); break; // @0x464f46
                    case 30: ResetComboSlots(b, false, 30, -1); break; // @0x464fdc
                    case 31: ResetComboSlots(b, false, 31, -1); break; // @0x465072
                    case 32: ResetComboSlots(b, false, 32, -1); break; // @0x465108
                    // ASYMETRIE PROUVEE (bug d'origine reproduit) : en NON-SELF les cas 33 et 34
                    //   sautent A LA FOIS 33 ET 34 (5 resets @0x46519a / @0x465213), alors que la
                    //   branche SELF saute seulement le slot propre (6 resets). Ne PAS corriger.
                    case 33: ResetComboSlots(b, false, 33, 34); break; // @0x46519a
                    case 34: ResetComboSlots(b, false, 33, 34); break; // @0x465213
                    default: break; // pas de cas 35 en non-self.
                }
            }
        } else if (flag == 2) {
            if (self) {
                WrZoneU32(8 * i, 0);             // dword_16758D8[2*i]=0 @0x465d4e
                WrZoneU32(8 * i + 4, 0);         // dword_16758DC[2*i]=0 @0x465d5f
                WrU32(b, kPStateArr + 4 * i, 0); // dword_168737C[227*v7+i]=0 @0x465d7c
            } else {
                WrU32(b, kPStateArr + 4 * i, 0); // @0x465289
            }
        }
    }
}

// ---------------------------------------------------------------------------
// op 0x11 — Pkt_CharStatDelta : deltas PV/PM/niveau (sous-ensemble entite du 36-cas).
// ---------------------------------------------------------------------------
void EntityManager::OnCharStatDelta(const net::CharStatDelta& p) {
    PlayerEntity* e = FindPlayer({ p.idHi, p.idLo });
    if (!e) return;

    uint8_t* b = e->body.data();
    const bool self = IsSelf(e);

    switch (p.subOp) {
        case 1: // gain de niveau : compteur d'entite + niveau self.
            WrI32(b, kPLevelCtr, RdI32(b, kPLevelCtr) + static_cast<int32_t>(p.valA));
            if (self) g_World.self.level += static_cast<int>(p.valA);
            break;
        case 4: { // degats PV : AR-min courant -= (valA+valB), plancher 0.
            int32_t hp = RdI32(b, kPHp) - static_cast<int32_t>(p.valA) - static_cast<int32_t>(p.valB);
            if (hp < 1) hp = 0;
            WrI32(b, kPHp, hp);
            break;
        }
        case 8: // soin PV.
            WrI32(b, kPHp, RdI32(b, kPHp) + static_cast<int32_t>(p.valA));
            break;
        case 9: // soin PM (AR-max courant).
            WrI32(b, kPMp, RdI32(b, kPMp) + static_cast<int32_t>(p.valA));
            break;
        case 23: // PM = 0.
            WrI32(b, kPMp, 0);
            break;
        case 24: // fixe PV.
            WrI32(b, kPHp, static_cast<int32_t>(p.valA));
            break;
        case 25: // fixe PM.
            WrI32(b, kPMp, static_cast<int32_t>(p.valA));
            break;
        default:
            // Autres cas (attributs, argent, buffs, compteurs de combo, montures/skills,
            // reset multi-champ du cas 22...) : relevent du StatEngine / systemes dedies.
            break;
    }

    // Reflet des barres de combat dans les champs clairs de l'entite.
    e->hp = RdI32(b, kPHp);
    e->mp = RdI32(b, kPMp);
}

// ---------------------------------------------------------------------------
// op 0x91 — Net_OnPartyMemberPosition : position monde d'un membre de groupe.
// ---------------------------------------------------------------------------
void EntityManager::OnPartyMemberPosition(const net::PartyMemberPosition& p) {
    PlayerEntity* e = FindPlayer({ p.idHi, p.idLo });
    if (!e) return;

    uint8_t* b = e->body.data();
    // Snapshot party (dword_1687478/47C + unk_1687480/84/88, unk_168748C=0).
    WrU32(b, kPPartyGridX, p.gridX);
    WrU32(b, kPPartyGridY, p.gridY);
    WrF32(b, kPPartyPos + 0, p.pos[0]);
    WrF32(b, kPPartyPos + 4, p.pos[1]);
    WrF32(b, kPPartyPos + 8, p.pos[2]);
    WrF32(b, kPPartyPosPad, 0.0f);

    // Reflet dans les champs de position clairs (position monde a jour du membre).
    e->x = p.pos[0];
    e->y = p.pos[1];
    e->z = p.pos[2];
}

// ---------------------------------------------------------------------------
// op 0x19 — Pkt_GroundItemRemove : decrement/retrait d'une pile de ramassage.
// ---------------------------------------------------------------------------
void EntityManager::OnGroundItemRemove(const net::GroundItemRemove& p) {
    // Pkt_GroundItemRemove 0x46a200. Le verrou anti-spam GM g_GmCmdCooldownLatch 0x1675B08 est
    // libere pour status 0 (@0x46a25a) ET status 1 (@0x46a30c) ; status>=2 = no-op total.
    if (p.status == 0 || p.status == 1)
        g_Client.Var(0x1675B08) = 0;

    if (p.status != 0)
        return; // status>=1 : pas de retrait de grille (le latch a deja ete traite ci-dessus).

    // status==0 : le binaire joue aussi un son (Snd3D_PlayScaledVolume flt_14891BC @0x46a26f,
    // audio non branche ici -> TODO ancre), puis decremente/purge la pile.
    GroundPickupSlot* s = PickupSlot(p.containerIndex, p.slotIndex);
    if (!s) return;

    // --dword_1674400[...] @0x46a29c : la garde count>0 est OBLIGATOIRE (count unsigned cote
    // C++, int signe cote binaire -> meme etat final 0 sans underflow, via le clamp <1 ci-dessous).
    if (s->count > 0) --s->count;
    if (s->count < 1) { // pile epuisee -> vide la cellule (@0x46a2cb/e5/ff).
        s->itemId = 0;
        s->count  = 0;
        s->aux    = 0;
    }
}

// ---------------------------------------------------------------------------
GroundPickupSlot* EntityManager::PickupSlot(uint32_t containerIndex, uint32_t slotIndex) {
    if (containerIndex >= kMaxContainers || slotIndex >= kSlotsPerContainer)
        return nullptr;
    const size_t idx = static_cast<size_t>(containerIndex) * kSlotsPerContainer + slotIndex;
    if (groundPickup_.size() <= idx)
        groundPickup_.resize(idx + 1);
    return &groundPickup_[idx];
}

bool EntityManager::IsSelf(const PlayerEntity* e) const {
    return !g_World.players.empty() && e == &g_World.players[0];
}

} // namespace ts2::game

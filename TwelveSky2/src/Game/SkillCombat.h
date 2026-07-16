// Game/SkillCombat.h — Intégration combat des compétences (ts2::game).
//
// Complément de SkillSystem.h (non édité) : résolution de stance active, sélection de
// motion d'animation par compétence/arme, hotkeys et tentative de cast en combat. Vérité
// = le désassemblage de TwelveSky2.exe (imagebase 0x400000) via MCP idaTs2.
//
// Fonctions d'origine reproduites ici :
//   Skill_GetActiveStance       0x4FB210  -> Skill_GetActiveStance
//   Skill_GetActiveStance2      0x4FCD40  -> Skill_GetActiveStance2
//   Skill_IsCurrentStanceSet    0x4FB0F0  -> Skill_IsCurrentStanceSet
//   Skill_GetComboMotionId      0x4FAD00  -> Skill_GetComboMotionId
//   Skill_GetMotionId2          0x4FC160  -> Skill_GetMotionId2
//   Skill_GetSpecialMotionId    0x4FC6D0  -> Skill_GetSpecialMotionId
//   Skill_IsSpecialUsable       0x4FC730  -> Skill_IsSpecialUsable
//   Skill_IsCurrentSpecial      0x4FC800  -> Skill_IsCurrentSpecial
//   Skill_GetBuffMotionId       0x4FC840  -> Skill_GetBuffMotionId
//   Skill_CheckBuffState        0x4FC950  -> Skill_CheckBuffState
//   Skill_GetBuffLevel          0x4FCB70  -> Skill_GetBuffLevel
//   Skill_IsCurrentBuff         0x4FCBC0  -> Skill_IsCurrentBuff
//   Skill_RemapByWeapon         0x501350  -> Skill_RemapByWeapon (+ sub_4FAB60 0x4FAB60)
//   Skill_IsHotkeyPressed       0x511340  -> Skill_IsHotkeyPressed
//   Skill_CanCastAtCursor       0x540E60  -> Skill_CanCastAtCursor
//   Skill_CastStoredAtTarget    0x53E740  -> Skill_CastStoredAtTarget
//   Skill_IsUsableOnCurrentMap  0x55D3B0  -> Skill_IsUsableOnCurrentMap (+ Char_CompareSkillLoadout 0x557B00)
//   Skill_HitTestSlot           0x662980  -> Skill_HitTestSlot
//   Skill_IsCurrentAttackSet    0x4FABC0  -> Skill_IsCurrentAttackSet
//   Skill_IsCurrentComboSet     0x4FC5D0  -> Skill_IsCurrentComboSet
//   Skill_IsCurrentSet138       0x4FCC00  -> Skill_IsCurrentSet138
//   Skill_IsCurrentSet5         0x4FCC70  -> Skill_IsCurrentSet5
//
// Globals d'origine non modélisés dans GameState.h/SkillSystem.h et introduits ici
// (structures locales, ne modifient AUCUN fichier existant) :
//   g_SelfMorphNpcId   0x1675A98 (id d'action/posture/« morph » courant du joueur local)
//     ⚠ NOM IDA TROMPEUR (établi vague W10, non corrigé ici pour ne pas rippler) : ce
//     global est en réalité l'ID DE ZONE/CARTE COURANT, pas un morph. Trois preuves
//     indépendantes : (a) World_LoadCurrentZoneModel 0x4DD6E0 le lit pour choisir le
//     fichier Z%03d.WM à charger — cf. World/WorldMap.h:159 `SetCurrentZoneId(...)
//     // g_SelfMorphNpcId 0x1675a98` et Scene/SceneManager.cpp:374 qui lui passe le
//     zoneId ; (b) Combat_CanTargetOnMap 0x558740 @0x558759 fait
//     `Map_GetPvpMode(g_MotionFrameRangeTable, g_SelfMorphNpcId)` puis branche sur
//     291/138/139/165/166/324/342/270-274/54 = des CARTES (291 a d'ailleurs ses deux
//     variantes Z291_1.WM/Z291_2.WM, cf. WorldMap::flagZ291Variant) ; (c) le préfixe
//     `Map_` de la fonction qui le consomme. NE PAS propager le mot « morph » dans du
//     code neuf : World/TerrainPicker.cpp lit `g_World.zoneId` pour cette valeur.
//   dword_16747BC      (compteur de palier de renaissance : 0 normal, 4..6 renaissance
//                        simple, >=7 renaissance élevée)
//   g_MotionFrameRangeTable 0x14A9350 (table SoA 350 entrées, réutilisée par
//                        SkillLevelTable ; le bloc {comboGroup,flag} à +700 dwords est
//                        modélisé ici via SkillBranchTable, cf. sub_4FAB60 0x4FAB60)
//   g_LocalPlayerSheet 0x1685748 (+455..458 paires d'éléments -> ElementPairTable ;
//                        +4052/+4104 étiquettes de loadout -> SkillLoadoutTable)
//   byte_1673184       (étiquette de branche d'arme courante, 13 o, « currentTag »)
//   g_Container5 (dword_1674400/04/16743FC, indexé [dword_1675B1C][dword_1675B20]) ->
//                        SelectedCastSlot (slot de barre de sorts sélectionné)
//   flt_1687330        (position monde du joueur local -> paramètre `selfPos`)
//   dword_168735C/60/64 (compétence/niveau/paramètre en attente de cast) -> PendingSkillCast
//
// HORS PÉRIMÈTRE (gardes/tables reproduites fidèlement ; l'ACTION externe est déléguée à
// l'appelant via une interface) :
//   Terrain_PickRayScreen 0x699A80 / World_IsPointBlocked 0x540DA0 / MapColl_GetGroundHeight
//   0x697130 (picking écran + collision terrain -> rendu 3D) : cf. ITerrainPicker.
//   Player_QueueSkill_opNN 0x5137D0..0x517320 (~30 builders réseau opcode sortant, cf.
//   Docs/TS2_PROTOCOL_SPEC.md) : cf. ISkillCastSink. Le mapping skillId -> groupe d'opcode
//   EST reproduit fidèlement (Skill_ResolveCastOpGroup).
#pragma once
#include <cstdint>
#include <cstring>
#include <array>
#include <vector>
#include "Game/GameState.h"
#include "Game/GameDatabase.h"
#include "Game/SkillSystem.h"
#include "Game/StatFormulas.h"
#include "Game/ClientRuntime.h"
// world::CollisionSlot (ITerrainPicker::PickRayScreen, cf. G-PICK-03 ci-dessous). AUCUN
// cycle : World/WorldMap.h est un module LEAF (n'inclut que <array>/<cstdint>/<string>/
// <vector> + des déclarations anticipées d'asset), il n'inclut aucun header Game/.
#include "World/WorldMap.h"

namespace ts2::game {

// ---------------------------------------------------------------------------
// État runtime « morph »/renaissance — absent de SelfState (cf. en-tête de fichier).
// ---------------------------------------------------------------------------
struct CombatMorphState {
    int currentActionId = 0; // g_SelfMorphNpcId 0x1675A98
    int rebirthTier      = 0; // dword_16747BC
};

// ---------------------------------------------------------------------------
// Bloc {comboGroup,flag} de g_MotionFrameRangeTable (0x14A9350), à +700 dwords du bloc
// {min,max} déjà modélisé par SkillLevelTable. Mirroir de sub_4FAB60 0x4FAB60 : renvoie
// le champ comboGroup (branche d'arme associée à la compétence), -1 si id hors [1,350].
// ---------------------------------------------------------------------------
struct SkillBranchTable {
    DataTable table; // stride attendu = 8 (comboGroup @+0)
    int Get(int skillId) const {
        if (skillId < 1 || skillId > 350) return -1;
        const uint8_t* rec = table.record(static_cast<uint32_t>(skillId - 1));
        return rec ? Skill_ReadI32(rec, 0) : -1;
    }
};

// ---------------------------------------------------------------------------
// Table de paires d'éléments du personnage (mirroir g_LocalPlayerSheet+455..458 dwords,
// Char_GetPairedElement 0x557C00) : 2 paires bidirectionnelles {a<->b, c<->d}.
// ---------------------------------------------------------------------------
struct ElementPairTable {
    int a = -1, b = -1, c = -1, d = -1;
    int Paired(int element) const {
        if (a == element) return b;
        if (b == element) return a;
        if (c == element) return d;
        if (d == element) return c;
        return -1;
    }
};

// ---------------------------------------------------------------------------
// EXPOSITION GLOBALE (mission 2026-07-14, Docs/TS2_COMBAT_ELEMENT_GATING.md §3/§4) —
// débloque les gates SkillLevelTable/ElementPairTable pour Net/WorldEntityDispatch.cpp
// (et tout futur appelant) sans dupliquer d'algorithme déjà porté ci-dessus/dans
// SkillSystem.h.
//
// 1) g_AlliancePairTable (== g_LocalPlayerSheet+0x71C..0x728, EXACTEMENT le bloc
//    mirroré par ElementPairTable::a/b/c/d) N'A PAS d'instance C++ persistante : sa
//    vraie source, côté binaire, est l'échappatoire mémoire elle-même — DÉSORMAIS
//    ALIMENTÉE (mission "câblage ElementPairTable", 2026-07-14, cf. addendum
//    Docs/TS2_COMBAT_ELEMENT_GATING.md) par Net/WorldEntityDispatch.cpp ::
//    ApplyAlliancePairFamily, wiring de Net_OnWorldEntityDispatch sous-opcode 46
//    (établit une paire) / 47 (l'efface), EA 0x497ce4/0x497d76. Combat_ReadLocalElementPairs()
//    construit un ElementPairTable INSTANTANÉ en relisant g_Client.VarGet() aux 4
//    adresses d'origine, MÊME convention que g_SelfMorphNpcId (0x1675A98) partout
//    ailleurs dans ClientSource — ces 4 adresses sont maintenant réellement écrites
//    par un handler réseau (avant cette passe, aucun écrivain n'existait : la table
//    restait figée à {0,0,0,0}, valeur BSS initiale). Note de fidélité conservée : cet
//    état initial {0,0,0,0} se comporte EXACTEMENT comme le repli {-1,-1,-1,-1} du
//    constructeur par défaut d'ElementPairTable (pour tout K dans [0,3], Paired(K)
//    renvoie soit -1 soit K lui-même selon que a/b/c/d valent 0 ou non, et
//    Combat_IsElementAllowedOnMap traite "P==-1" et "P==K" de façon IDENTIQUE, cf.
//    Docs/TS2_COMBAT_ELEMENT_GATING.md §2) — donc AUCUN repli explicite à −1 n'est
//    nécessaire, lire tel quel reste correct avant la première paire établie.
inline constexpr uint32_t kElementPairAAddr = 0x1685E64u; // g_LocalPlayerSheet+0x71C (a) == g_AlliancePairTable[0]
inline constexpr uint32_t kElementPairBAddr = 0x1685E68u; // +0x720 (b) == dword_1685E68[0]
inline constexpr uint32_t kElementPairCAddr = 0x1685E6Cu; // +0x724 (c) == g_AlliancePairTable[2]
inline constexpr uint32_t kElementPairDAddr = 0x1685E70u; // +0x728 (d) == dword_1685E68[2]

// Instantané de g_AlliancePairTable, prêt pour ElementPairTable::Paired(...). Lecture
// seule ici (l'écriture est faite par Net/WorldEntityDispatch.cpp ::
// ApplyAlliancePairFamily, sous-opcodes 46/47, cf. bandeau ci-dessus).
ElementPairTable Combat_ReadLocalElementPairs();

// 2) SkillLevelTable (Game/SkillSystem.h) — AUCUN chargeur .IMG n'existe pour cette
//    table (contrairement à g_World.db.skill/level/item/monster/socketT, cf.
//    Game/GameDatabase.h) : Motion_InitFrameTable 0x4F1380 la construit à
//    App_Init(EA 0x46227c) via un switch(i) de 350 cas ENTIÈREMENT CODÉS EN DUR dans le
//    binaire (i = skillId-1 ; mêmes {min@+0,max@+4} lus par
//    SkillLevelTable_GetMin/Max 0x4FAB00/0x4FAB30) — AUCUNE dérivation possible depuis
//    g_World.db.skill (SKILL_INFO, table .IMG DISTINCTE, cf. skillinfo::kOffStatMin/Max
//    dans Game/SkillSystem.h, un tout autre système). GetSkillLevelTable() transcrit
//    fidèlement ce switch (vérifié cas par cas contre le désassemblage complet de
//    Motion_InitFrameTable, EA 0x4F1380..0x4F69E7) et construit l'instance UNIQUE une
//    seule fois (static locale, cf. .cpp). UI/GameWindows.cpp (audit SkillTreeWindow,
//    2026-07-14) lie désormais directement GetSkillLevelTable() à SkillTreeWindow::Bind
//    au lieu de l'ancien membre local vide "faute d'avoir identifié cette source" —
//    les niveaux requis affichés par nœud sont donc réels, plus jamais 0..0.
const SkillLevelTable& GetSkillLevelTable();

// ---------------------------------------------------------------------------
// Snapshot clavier DirectInput (bit7 d'un octet d'état = touche enfoncée, testé « <0 »
// en int8 signé dans le binaire). 14 emplacements (1..9, 0, F1..F4) par jeu de touches :
//   modeA = g_MorphUiMode==1 : byte_80140F..801418 (10) + byte_8013D6..8013D9 (4)
//   modeB = sinon            : byte_8013D6..8013DF (10) + byte_8013E4..8013E7 (4)
// Cf. Skill_IsHotkeyPressed 0x511340 / Game_UseFirstReadySkill 0x538190.
// ---------------------------------------------------------------------------
struct HotkeySnapshot {
    std::array<int8_t, 14> modeA{};
    std::array<int8_t, 14> modeB{};
    bool Pressed(int slot, int morphUiMode) const {
        if (slot < 0 || slot >= 14) return false;
        const auto& arr = (morphUiMode == 1) ? modeA : modeB;
        return arr[static_cast<size_t>(slot)] < 0;
    }
};

// ---------------------------------------------------------------------------
// Table de liaison touche<->slot de barre de raccourcis, copie EMBARQUÉE dans l'objet UI
// d'origine (dwords +11427 = opcode/skillId lié, +11429 = drapeau « lié »==1 ; stride 3
// dwords/slot, 42 dwords/page = 14 slots). Mirroir de l'accès
// this[42*page+3*slot+11427/11429] dans Skill_IsHotkeyPressed 0x511340.
// ---------------------------------------------------------------------------
struct HotkeyBindTable {
    struct Bind {
        int32_t opcode = 0; // +11427 : identifiant comparé à l'appelant (a2 d'origine)
        int32_t bound  = 0; // +11429 : ==1 si le slot est lié à une touche
    };
    std::vector<std::array<Bind, 14>> pages;
};

// ---------------------------------------------------------------------------
// Grilles d'étiquettes de buffs appris (mirroir unk_16869C0/unk_1686AC4/unk_1686BC8) :
// 4 lignes (élément) x 5 colonnes, étiquette de branche 13 o par cellule. Comparées à
// l'étiquette de branche courante (byte_1673184) dans Skill_CheckBuffState 0x4FC950.
// ---------------------------------------------------------------------------
struct BuffLearnedGrid {
    std::array<std::array<std::array<char, 13>, 5>, 4> cells{};
};
struct BuffLearnedGrids {
    BuffLearnedGrid g297; // unk_16869C0 (skill 297)
    BuffLearnedGrid g298; // unk_1686AC4 (skill 298)
    BuffLearnedGrid g299; // unk_1686BC8 (skill 299)
};

// Niveaux de buff courants (mirroir dword_1686064[0]/dword_1686068/dword_168606C).
struct BuffLevels {
    int32_t v297 = 0, v298 = 0, v299 = 0;
};

// ---------------------------------------------------------------------------
// Table de compatibilité « loadout de compétence » du personnage local (mirroir de
// g_LocalPlayerSheet+4052.. et +4104.., Char_CompareSkillLoadout 0x557B00) : pour chaque
// branche d'arme (0..3), une étiquette « principale » (13 o) et 12 étiquettes
// « alternatives » (13 o), comparées à l'étiquette de branche courante.
// ---------------------------------------------------------------------------
struct SkillLoadoutTable {
    std::array<std::array<char, 13>, 4>                 primary{}; // this+4052+13*branch
    std::array<std::array<std::array<char, 13>, 12>, 4> alt{};     // this+4104+156*branch+13*i

    // 0 = aucune correspondance, 1 = étiquette principale, 2 = étiquette alternative.
    int Compare(const char currentTag[13], int branch) const;
};

// ---------------------------------------------------------------------------
// Slot de barre de sorts actuellement sélectionné pour un cast « ciblé au curseur »
// (mirroir de dword_1674404[i] (drapeau lié), dword_16743FC[i] (code de type),
// dword_1674400[i] (valeur de niveau), avec i=42*dword_1675B1C+3*dword_1675B20, et
// selected = (dword_1675B20 != -1)). Cf. Skill_CanCastAtCursor 0x540E60.
// ---------------------------------------------------------------------------
struct SelectedCastSlot {
    bool    selected = false;
    int32_t bound    = 0; // dword_1674404[...] : ==1 -> slot actif
    int32_t typeCode = 0; // dword_16743FC[...] : 3/22/41 -> sort à cible verrouillée
    int32_t level    = 0; // dword_1674400[...] : terme additif de portée
};

// ---------------------------------------------------------------------------
// Point de branchement pour le picking écran/collision terrain (rendu 3D, HORS
// PÉRIMÈTRE gameplay pur) :
//   IsPointBlocked  <- World_IsPointBlocked 0x540DA0 (+ MapColl_GetGroundHeight 0x697130)
//   PickRayScreen   <- Terrain_PickRayScreen 0x699A80
// IMPLÉMENTEUR RÉEL : ts2::world::TerrainPicker (World/TerrainPicker.h) — branché sur
// WorldAssets (mailles .WM/.WJ réellement décodées). Avant la vague W10, cette interface
// n'avait AUCUN implémenteur (G-PICK-06).
//
// ⚠ DEUX CORRECTIONS DE FIDÉLITÉ (vague W10, re-prouvées au désassemblage) :
//
// 1) `slot` (G-PICK-03) — le binaire N'INTERROGE PAS toujours la même maille de collision.
//    Terrain_PickRayScreen est une méthode __thiscall dont le `this` EST la MapColl visée :
//      Skill_CanCastAtCursor @0x540F83 : `mov ecx, offset dword_14A88E4` -> .WM  (Main)
//                            @0x540FC4 : `mov ecx, offset dword_14A898C` -> .WJ  (WJ)
//                            @0x54105F : `mov ecx, offset dword_14A88E4` -> .WM  (Main)
//    Identité des deux mailles PROUVÉE par arithmétique d'offsets : 0x14A898C - 0x14A88E4
//    = 0xA8, et World/WorldMap.h:90-94 fixe Main = base+0xA8 / WJ = base+0x150 (écart 0xA8
//    lui aussi) -> base g_GameWorld = 0x14A883C, donc dword_14A88E4 == Main (.WM) et
//    dword_14A898C == WJ (.WJ). Corroboré par xrefs_to : 24 refs sur 0x14A88E4 (maille
//    principale de tout le jeu) contre 3 sur 0x14A898C.
//
// 2) `twoSide` (ex-`wantEntityHit`, MISNOMER corrigé) — le 6e argument de
//    Terrain_PickRayScreen 0x699A80 n'a rien d'un « veut toucher une entité » : il est
//    transmis TEL QUEL et UNIQUEMENT à MapColl_RaycastNearest 0x6960C0 @0x699BA9 comme
//    dernier paramètre, dont World/WorldMap.h:319-321 établit le rôle = `twoSide`
//    (accepter les faces orientées des DEUX côtés). Le renommer ici supprime un faux sens
//    qui aurait égaré tout futur implémenteur.
// ---------------------------------------------------------------------------
struct ITerrainPicker {
    virtual ~ITerrainPicker() = default;
    virtual bool IsPointBlocked(const float pos[3]) = 0;
    // `slot`    : maille de collision interrogée (Main = .WM, WJ = .WJ) — cf. §1 ci-dessus.
    // `twoSide` : 6e arg d'origine de Terrain_PickRayScreen (0 ou 1) — cf. §2 ci-dessus.
    // outPos reçoit le point 3D touché. Retourne false si rien n'est touché.
    virtual bool PickRayScreen(int screenX, int screenY, world::CollisionSlot slot,
                                bool twoSide, float outPos[3]) = 0;
};

// ---------------------------------------------------------------------------
// Constructeurs réseau Player_QueueSkill_opNN (0x5137D0..0x517320, ~30 builders,
// opcode sortant) : HORS PÉRIMÈTRE (cf. Docs/TS2_PROTOCOL_SPEC.md). Le mapping
// skillId -> groupe d'opcode EST reproduit fidèlement (Skill_ResolveCastOpGroup) ;
// l'envoi réel est délégué à l'appelant via cette interface.
// ---------------------------------------------------------------------------
struct ISkillCastSink {
    virtual ~ISkillCastSink() = default;
    // Retourne true si le paquet a été construit/envoyé avec succès (mirroir de la
    // valeur de retour non nulle des Player_QueueSkill_opNN d'origine).
    virtual bool QueueSkillCast(int opGroup, int skillId, int level, int param,
                                 const float pos[3], int32_t targetHi, int32_t targetLo,
                                 int32_t targetKind) = 0;
};

// Compétence en attente de cast (mirroir dword_168735C/1687360/1687364).
struct PendingSkillCast {
    int32_t skillId = 0; // dword_168735C
    int32_t level   = 0; // dword_1687360
    int32_t param   = 0; // dword_1687364
};

// Motifs d'échec d'une tentative de cast (Skill_CastStoredAtTarget 0x53E740 et
// Skill_CheckCastPrereqs / Player_CastSkill 0x53BC40).
enum class SkillCastFailReason {
    None = 0,
    NotEnoughMp     = 147,  // StrTable005 id 147
    IncompatibleForm = 1920, // StrTable005 id 1920 (morph 88/54 + compétence de posture)
    StanceRequired  = 1146, // StrTable005 id 1146 (posture/spécial/niveau>=70 requis)
    MorphBlocked    = 1212, // StrTable005 id 1212 (morph transformé 234..240)
    // --- Gardes propres à Player_CastSkill 0x53BC40 (cf. Skill_CheckCastPrereqs) ---
    ElementMismatch = 145,  // StrTable005 id 145 (0x91) [Player_CastSkill @0x53BDAE]
    WeaponMismatch  = 146,  // StrTable005 id 146 (0x92) [Player_CastSkill @0x53BE20]
    UnknownSkill    = -1,   // record introuvable ou groupe d'opcode non mappé
    SinkRejected    = -2,   // ISkillCastSink::QueueSkillCast a renvoyé faux
    // Arme équipée sans record ITEM_INFO : le binaire sort en SILENCE (aucun message),
    // à NE PAS confondre avec WeaponMismatch/msg 146 [Player_CastSkill @0x53BDEF-0x53BDF7].
    WeaponRecordMissing = -3,
};

struct SkillCastAttemptResult {
    bool                ok = false;
    int                 mpCost = 0;
    SkillCastFailReason reason = SkillCastFailReason::None;
};

// ========================= API du systeme ==================================

// Sélectionne la compétence de posture/stance active (49/120/154/295/296) selon le
// niveau effectif courant. Le passage 295->296 est gaté par rebirthTier (>=7 -> 296,
// [4,6] ou <4 -> 295, cf. §. 0 si aucune tranche ne correspond.
int Skill_GetActiveStance(const SelfState& self, const CombatMorphState& morph,
                           const SkillLevelTable& lvlTbl);

// Variante seconde vague (319-323) : rebirthTier>=7 -> 323 sinon 322 pour la tranche
// terminale ; 0 si aucune tranche ne correspond.
int Skill_GetActiveStance2(const SelfState& self, const CombatMorphState& morph,
                            const SkillLevelTable& lvlTbl);

// L'id d'action courant est-il une posture/garde (49/51/53/120-122/146-164/295/296/
// 319-323) ?
bool Skill_IsCurrentStanceSet(int currentActionId);

// Table (typeArme 1..4, index) -> id de motion de combo. -1 si hors table.
int Skill_GetComboMotionId(int weaponType, int index);

// Seconde table (branche 0..3, index 0..7) -> id de motion. -1 si hors table.
int Skill_GetMotionId2(int branch, int index);

// Table (index 0..3) -> id de motion spéciale (267/268/269/250). -1 si hors table.
int Skill_GetSpecialMotionId(int index);

// La compétence spéciale (250 ou 267..269) est-elle utilisable au niveau effectif
// courant ? Gardes supplémentaires : 250 requiert rebirthTier>0 OU levelBonus==12 ;
// 269 requiert rebirthTier==0 ET levelBonus<12.
bool Skill_IsSpecialUsable(int specialId, const SelfState& self, const CombatMorphState& morph,
                            const SkillLevelTable& lvlTbl);

// L'id d'action courant est-il une compétence spéciale (250 ou 267..269) ?
bool Skill_IsCurrentSpecial(int currentActionId);

// Table (index 0..19) -> id de motion de buff (241-249/292-294/311-312/325-330).
// -1 si hors table.
int Skill_GetBuffMotionId(int index);

// Vérifie l'étiquette de branche apprise pour un buff (297/298/299) contre l'étiquette
// de branche courante : 0 = même élément, 1 = élément différent, 2 = non trouvé/inconnu.
int Skill_CheckBuffState(int buffSkillId, const BuffLearnedGrids& grids,
                          const char currentTag[13], int localElement);

// Niveau courant d'un buff (297/298/299). 0 si id inconnu.
int Skill_GetBuffLevel(int buffSkillId, const BuffLevels& levels);

// L'id d'action courant est-il un buff (297..299) ?
bool Skill_IsCurrentBuff(int currentActionId);

// Remappe une compétence d'arme (skillId) vers son équivalent pour weaponType si le
// type d'arme courant de la compétence diverge (ni identique, ni pairé). Reproduit
// fidèlement les 4 tables de repli (weaponType 0..3) ; skillId inchangé si non mappable
// ou déjà compatible.
int Skill_RemapByWeapon(int weaponType, int skillId, const SkillBranchTable& branch,
                         const ElementPairTable& pairs);

// La touche liée au slot [page][slot] de la barre de raccourcis est-elle actuellement
// enfoncée et bien liée à `opcode` ? Faux si aucun slot sélectionné (slot==-1), si le
// slot n'est pas lié, ou si l'opcode lié diverge.
bool Skill_IsHotkeyPressed(const HotkeyBindTable& binds, int page, int slot,
                            const HotkeySnapshot& keys, int morphUiMode, int opcode);

// Un cast au curseur écran (screenX,screenY) est-il valide ? Deux branches :
//  - slot de barre sélectionné et à cible verrouillée (typeCode 3/22/41) : exige
//    Skill_IsCurrentAttackSet, calcule la portée via Skill_InterpStat (stat#6) + la
//    résistance élémentaire (CalcElementResist), compare à la distance 3D du point
//    ciblé (picking verrouillé), avec un minimum de 10.0.
//  - sinon : picking libre, refuse si le point cliqué est bloqué (hors sol/dans un mur).
bool Skill_CanCastAtCursor(const float selfPos[3], const SelfState& self, const GameDatabases& db,
                            const CombatMorphState& morph, const SelectedCastSlot& slot,
                            int screenX, int screenY, ITerrainPicker& picker);

// Groupe d'opcode réseau (Player_QueueSkill_opNN) associé à un skillId de cast, ou -1
// si non mappé. Transcription 1:1 du gros switch de Skill_CastStoredAtTarget 0x53E740.
int Skill_ResolveCastOpGroup(int skillId);

// Tentative de cast de la compétence en attente (`pending`) sur la position/cible
// fournie. Vérifie le coût MP (Skill_CalcRealMpCost / Skill_CalcRegenPct existants),
// le garde-fou de forme incompatible (morph 88/54 + compétence de posture), le
// garde-fou de posture requise pour le groupe 38 (skillId 4/23/42), et le blocage
// « morph transformé » (234..240) avant de déléguer à `sink`. Le débit MP réel, à la
// réussite, DÉLÈGUE à Skill_TryConsumeMp (SkillSystem.h) — cf. commentaire d'implém.
SkillCastAttemptResult Skill_CastStoredAtTarget(SelfState& self, const GameDatabases& db,
                                                 const CombatMorphState& morph,
                                                 const PendingSkillCast& pending,
                                                 const float pos[3], int32_t targetHi,
                                                 int32_t targetLo, int32_t targetKind,
                                                 ISkillCastSink& sink);

// ---------------------------------------------------------------------------
// Gardes de prérequis du chemin de cast PRINCIPAL — Player_CastSkill 0x53BC40.
//
// ⚠ ÉTAT DE CONSOMMATION (honnêteté requise, ne pas effacer sans câbler) : cette
// fonction est écrite mais N'EST APPELÉE PAR PERSONNE aujourd'hui. Player_CastSkill
// 0x53BC40 n'est pas porté, et AUCUN de ses 8 sites d'appel / 4 fonctions d'entrée
// joueur ne l'est non plus (vérifié par xrefs_to 0x53BC40 + grep exhaustif de src/) :
//   Game_OnWorldLeftClick   0x536690 (@0x536863, @0x536EFA)
//   Game_OnHotkey           0x537330 (@0x5377F7)
//   Game_UseFirstReadySkill 0x538190 (@0x53855F)
//   AutoPlay_Update         0x45E770 (@0x45E953, @0x45EA46, @0x45EBD0, @0x45ED3D)
// DETTE OUVERTE : à appeler depuis le front qui portera l'une de ces entrées, en TÊTE
// du cast (toute la chaîne de prérequis précède tout envoi réseau dans le binaire) ;
// un résultat `ok == false` doit interrompre le cast sans rien émettre.
//
// ⚠ NE PAS l'appeler depuis Skill_CastStoredAtTarget (0x53E740) : cette variante-là
// est le chemin AUTOPLAY (appelants exclusifs Player_AutoInteractPlayer 0x5396F0 et
// Player_AutoInteractMonster 0x53A170) et NE PORTE PAS ces gardes — le désassemblage
// 0x53E740..0x53E7F9 va directement au coût MP. Les y ajouter serait une INFIDÉLITÉ.
//
// Ordre EXACT reproduit — chaîne COMPLÈTE de prérequis, EA 0x53BD12..0x53BEA0 :
//   0. RECORD @0x53BD12 : SkillGrowthTbl_GetRecord ; NUL -> `xor eax,eax` @0x53BD20,
//      échec SILENCIEUX (aucun message) -> UnknownSkill.
//   1. FORME @0x53BD27 : g_SelfMorphNpcId (0x1675A98) == 0x58 (88) OU 0x36 (54), ET
//      (category(+0x220) == 4 || == 5 || rec[0] == 0x4E (78)) -> msg 1920 @0x53BD60, échec.
//      ⚠ CE message-là n'est PAS gaté par arg_C (aucun `cmp [ebp+arg_C],0` avant
//      0x53BD59) — contrairement aux trois suivants. Fidèlement reproduit : émis quel
//      que soit `showErr`.
//   2. ÉLÉMENT @0x53BD84 : reqElement(+0x228) != 1 && (reqElement - 2) != self
//      .elementSecondary (g_LocalElementSecondary 0x1673198) -> msg 145 @0x53BDAE, échec.
//   3. ARME @0x53BDD2 : reqWeaponType(+0x22C) != 1 -> record ITEM_INFO de l'arme équipée
//      (dword_1673248 == self.equip[7].itemId) ; record NUL -> échec SILENCIEUX
//      @0x53BDEF-0x53BDF7 ; sinon reqWeaponType != (ITEM_INFO+188 - 0x0B) -> msg 146
//      @0x53BE20, échec.
//   4. COÛT MP @0x53BE41 : ftol(InterpStat(#1, rec[0], level)) réduit du % régén
//      (division ENTIÈRE @0x53BE72-0x53BE86) ; self.mp < coût -> msg 147, échec. DEUX
//      sites d'émission du msg 147 sur ce chemin d'échec : @0x53BEA0 (gaté par arg_C /
//      showErr) PUIS @0x53BECA (gaté par g_InvDirtyEnable(0x16755AC)==1, le drapeau maître
//      d'auto-hunt — PAS par showErr) ; les deux peuvent se déclencher.
// `showErr` (arg_C d'origine) gate les messages 145/146 et la PREMIÈRE émission du 147
// (@0x53BDA1, @0x53BE13, @0x53BE93) ; la SECONDE émission du 147 (@0x53BEBA) est gatée par
// le drapeau d'auto-hunt, pas par showErr. Les échecs se produisent que showErr soit vrai
// ou faux, seul le message est conditionné. NE DÉBITE PAS le MP (le binaire ne débite
// qu'après succès du builder réseau, bien plus loin dans la fonction).
//
// NOTE : la garde 1 (FORME) est la MÊME que celle déjà portée dans
// Skill_CastStoredAtTarget (0x53E740) — les deux fonctions la portent chacune de leur
// côté dans le binaire ; ce n'est donc pas une duplication introduite ici.
SkillCastAttemptResult Skill_CheckCastPrereqs(const SelfState& self, const GameDatabases& db,
                                               const CombatMorphState& morph,
                                               int skillId, int level, bool showErr);

// La branche d'arme `mapZoneIndex` (0..3) est-elle utilisable sur la carte courante ?
// Exige currentActionId dans le quadruplet associé (cf. implém.) ET une correspondance
// de loadout (SkillLoadoutTable::Compare != 0).
bool Skill_IsUsableOnCurrentMap(int mapZoneIndex, int currentActionId,
                                 const SkillLoadoutTable& loadout, const char currentTag[13]);

// Teste la position (cursorX,cursorY) contre la grille 2x5 de la barre de compétences
// ancrée en (anchorX,anchorY) (cf. UI_ProjectSpriteToScreen 0x50F5D0, calculée par
// l'appelant — HORS PÉRIMÈTRE ici). Renvoie l'index de slot LIBRE trouvé (destination
// valide pour un drag&drop), ou -1 (aucun slot sous le curseur, slot déjà occupé, ou
// classOffset hors table). panelVisible==false -> -1 immédiat (mirroir this[2]==0).
int Skill_HitTestSlot(bool panelVisible, int anchorX, int anchorY, int cursorX, int cursorY,
                       int classOffset, const SkillBar& bar);

// L'id d'action courant appartient-il à l'ensemble d'attaque/compétence « standard » ?
bool Skill_IsCurrentAttackSet(int currentActionId);
// ... à l'ensemble combo (19-36/175-193) ?
bool Skill_IsCurrentComboSet(int currentActionId);
// ... à l'ensemble {138,139,165,166} ?
bool Skill_IsCurrentSet138(int currentActionId);
// ... à l'ensemble {5,10,15,123} ?
bool Skill_IsCurrentSet5(int currentActionId);

} // namespace ts2::game

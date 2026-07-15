// Game/SkillSystem.h — Systeme de competences du client TwelveSky2 (ts2::game).
//
// Reecriture PROPRE (pas byte-exact) de la logique de competences relevee dans
// le desassemblage de TwelveSky2.exe (imagebase 0x400000). Verite = le DESASM
// (MCP idaTs2) + Docs/TS2_GAMEPLAY_LOGIC.md §6. Toutes les fonctions operent sur
// g_World.db.skill / g_World.db.item (tables .IMG) et SelfState (bloc « self »).
//
// Fonctions d'origine reproduites ici :
//   Skill_CostById              0x4CD0E0  -> Skill_CostById
//   Skill_ResolveLevelSlot      0x4FB370  -> Skill_ResolveLevelSlot
//   Skill_IsLearned             0x4FBCB0  -> SkillLearnFlags::IsLearned
//   Skill_GetValueByClassA      0x54E620  -> Skill_GetValueByClassA
//   Skill_GetValueByClassB      0x54E980  -> Skill_GetValueByClassB
//   Skill_UnpackTreeNodes       0x54C090  -> Skill_UnpackTreeNodes
//   Skill_CountTreeNodes        0x54BF70  -> Skill_CountTreeNodes
//   SkillGrowthTbl_GetRecord    0x4C4E90  -> Skill_GetRecord
//   SkillGrowthTbl_InterpStat   0x4C4EE0  -> Skill_InterpStat
//   Skill_IsAvailableByLevel    0x4FAF40  -> Skill_IsAvailableByLevel
//   Skill_IsAvailableByBranch   0x4FC390  -> Skill_IsAvailableByBranch
//   Skill_GetUpgradeCostTier    0x54F4D0  -> Skill_GetUpgradeCostTier
//   Char_CalcRegen              0x4D67F0  -> Skill_CalcRegenPct
//   (cast MP) Skill_CastStored  0x53E740  -> Skill_CalcRealMpCost / Skill_TryConsumeMp
//   (learn)   Pkt_ItemAction G0 0x46A456  -> Skill_Learn (barre g_LearnedSkills 0x16742BC)
//
// ---------------------------------------------------------------------------
// DISPOSITION DE L'ARBRE DE COMPETENCES — CONFIRME_FIDELE (2026-07-14, re-
// verifie par nouvelle passe de decompilation idaTs2 le meme jour, y compris
// xrefs_to sur SkillGrowthTbl_GetRecord 0x4C4E90 : les SEULS lecteurs de cette
// table cote UI sont UI_SkillLearn_OnLDown/Draw/OnMove — aucune autre fonction
// du binaire ne dessine de widget « arbre » alternatif). Verdict : le jeu
// d'origine N'A PAS de disposition d'arbre a positions custom par noeud ni de
// lignes de connexion parent-enfant — la « vraie » disposition EST une grille
// simple, tel que documente ci-dessous. Ce n'est plus une limite de recherche,
// c'est un fait confirme du binaire.
//
// UI_SkillLearn_Enter/OnLDown/OnLUp/Draw/OnMove (0x5E1BA0..0x5E2450)
// forment une grille FIXE 3 colonnes (branche d'arme, i=0..2) x 8 lignes
// (palier, j=0..7), position pixel exacte (formule, pas une table de
// coordonnees par noeud) :
//   x = x0 + 76*i + 35   y = y0 + 54*j + 71     (case ~50x50 px)
//   x0 = nWidth/2 - largeurFond/2, y0 = nHeight/2 - hauteurFond/2 (panneau centre)
// Le skillId de chaque case vient de *(this+2)+2076+32*i+4*j, ou *(this+2) est
// le NPC formateur actuellement ouvert (confirme par UI_SkillLearn_Enter 0x5E1BC5 :
// *(this+2)+1312 compare a g_LocalElement, un champ faction/element de PNJ) : LA
// GRILLE 3x8 EST PROPRE A CHAQUE PNJ FORMATEUR, ce n'est PAS un arbre global
// unique parcourable a volonte. UI_SkillLearn_Draw (0x5E2200) ne dessine AUCUNE
// ligne de connexion parent-enfant (verifie deux fois : le corps ne contient
// que fond + icones de case + texte de compteur SP) — absence structurelle
// CONFIRMEE, pas une limite de recherche. Le filtrage de prerequis (case 1..4
// dans UI_SkillLearn_OnLDown 0x5E1E89, gate sur les 4 plages de
// g_LearnedSkills) correspond exactement a la logique deja portee ici dans
// Skill_Learn (switch sur kOffSection).
//
// Point SEPARE et toujours reel (pas couvert par le CONFIRME_FIDELE ci-dessus) :
// SkillTreeWindow.cpp (UI/) ne cable PAS la grille 3x8 telle quelle, non pas
// parce que la disposition serait inconnue (elle est CONFIRME_FIDELE), mais
// parce que la donnee « quel skillId dans quelle case » est propre a chaque
// PNJ et n'a pas d'equivalent dans GameDatabases/DataTable cote client reecrit
// (aucune table NPC->competences enseignees n'a ete portee), et que ouvrir
// cette fenetre est, dans l'original, un evenement d'interaction PNJ — pas un
// raccourci clavier global ('K') comme le fait GameWindows.h actuellement.
// C'est un choix d'architecture documente (backend manquant + contrat
// d'ouverture different), PAS une disposition non retrouvee. Cf. commentaire
// detaille dans UI/SkillTreeWindow.cpp/.h pour le choix retenu.
#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <unordered_map>
#include "Game/GameState.h"

namespace ts2::game {

// ---------------------------------------------------------------------------
// Troncature Crt_ftol (0x760810) : cast (int), troncature VERS ZERO (= floor si
// positif). Toute multiplication flottante du gameplay passe par la.
// ---------------------------------------------------------------------------
inline int Skill_Ftol(double x) { return static_cast<int>(x); }

inline int32_t Skill_ReadI32(const uint8_t* rec, std::size_t off) {
    int32_t v;
    std::memcpy(&v, rec + off, sizeof(v));
    return v;
}

// ---------------------------------------------------------------------------
// Layout du record SKILL_INFO (776 o, indexe 1-based). Offsets = 4*idx dword.
// ---------------------------------------------------------------------------
namespace skillinfo {
inline constexpr std::size_t kRecordSize     = 776;
inline constexpr std::size_t kOffSkillId     = 0x000; // dword0  : id (sentinelle, 0 = slot vide)
inline constexpr std::size_t kOffName        = 0x004; // idx1    : nom C-string embarque dans le record.
                                                        // Confirme par UI_SkillLearn_OnLDown 0x5E20A5 :
                                                        // Crt_Vsnprintf(v15, "[%s]%s", v14 + 4, ...) ou v14
                                                        // est le pointeur record brut (v14+4 lu DIRECTEMENT
                                                        // comme const char*, pas via StrTable). Longueur du
                                                        // champ non confirmee (uniquement le NUL terminal
                                                        // garantit la fin) -> Skill_GetName() ne fait AUCUNE
                                                        // hypothese de largeur fixe, cf. .cpp.
inline constexpr std::size_t kOffIconIndex   = 0x224; // idx137  : index d'icone REEL (atlas interne du
                                                        // client d'origine), DISTINCT du skillId. Confirme
                                                        // par UI_SkillLearn_Draw 0x5E2328 : Sprite2D_Draw
                                                        // ((int)&unk_A1BD60 + 148*v16, ...) avec
                                                        // v16 = v12[137] - 1 (+1 supplementaire si le noeud
                                                        // est le noeud survole/en attente). L'ancienne
                                                        // hypothese « iconIdx == skillId » de
                                                        // SkillTreeWindow.cpp (ResolveSkillIconPath) est
                                                        // donc FAUSSE ; corrigee pour lire ce champ.
inline constexpr std::size_t kOffSection     = 0x21C; // idx135  : section/element -> plage de barre
inline constexpr std::size_t kOffCategory    = 0x220; // idx136  : categorie (4/5 = posture)
inline constexpr std::size_t kOffReqWeapon   = 0x228; // idx138  : type d'arme requis
inline constexpr std::size_t kOffReqBranch   = 0x22C; // idx139  : classe/branche requise
inline constexpr std::size_t kOffSpCost      = 0x230; // idx140  : cout en points de competence
inline constexpr std::size_t kOffLevelNorm   = 0x234; // idx141  : denominateur d'interpolation
inline constexpr std::size_t kOffStatMin     = 0x240; // idx144  : stat_min[0..24] (stat#1 = cout MP)
inline constexpr std::size_t kOffStatMax     = 0x2A4; // idx169  : stat_max[0..24]
} // namespace skillinfo

// ITEM_INFO : champs consultes par le systeme de competences.
namespace iteminfo {
inline constexpr std::size_t kOffTypeCode    = 188;   // idx47 : 0xD..0x15 = classe d'arme
inline constexpr std::size_t kOffTaughtSkill = 0x15C; // idx87 : id de la competence enseignee
inline constexpr std::size_t kOffRegen       = 360;   // reduction % cout MP (somme sur 13 slots)
} // namespace iteminfo

// ---------------------------------------------------------------------------
// Contexte runtime rare : mirroir de dword_16851B8. Actif dans Skill_InterpStat
// pour la stat#7 (portee distance) des competences 112..120 : penalite ×0.7 si
// != 3. Reglable par le code appelant (ex : contexte fenetre/mode).
// ---------------------------------------------------------------------------
inline int g_Skill112RangeMode = 0;

// ---------------------------------------------------------------------------
// Table min/max du niveau d'acces par competence (SkillLevelTable_GetMin/Max
// 0x4FAB00/0x4FAB30). Tableau plat de dwords : record(id-1) = {min:i32, max:i32}
// (stride 8), skillId 1..350. Non present dans GameDatabases -> table injectee.
// ---------------------------------------------------------------------------
struct SkillLevelTable {
    DataTable table; // stride attendu = 8 (min @+0, max @+4)
    int Min(int skillId) const;
    int Max(int skillId) const;
};

// ---------------------------------------------------------------------------
// Barre de competences apprises (g_LearnedSkills 0x16742BC) : 40 slots, stride 8
// = {skillId @+0, spCost @+4}. Un slot est libre quand skillId == 0.
// ---------------------------------------------------------------------------
struct SkillBarSlot {
    uint32_t skillId = 0;
    int32_t  spCost  = 0;
};
struct SkillBar {
    std::array<SkillBarSlot, 40> slots{};
    // Premier slot libre dans [begin, end) ; -1 si aucun (fidele : test skillId<1).
    int FindFree(int begin, int end) const;
    void Clear() { slots.fill(SkillBarSlot{}); }
};

// ---------------------------------------------------------------------------
// Drapeaux « competence apprise » epars (Skill_IsLearned 0x4FBCB0). Seul un jeu
// fige d'ids est suivi (mastery/postures/tribu) ; tout autre id -> non appris.
// ---------------------------------------------------------------------------
struct SkillLearnFlags {
    std::unordered_map<int, int> flags; // skillId -> valeur brute (appris ssi ==1)
    // L'id figure-t-il dans l'ensemble suivi par Skill_IsLearned ?
    static bool IsTrackable(int skillId);
    void Set(int skillId, int value) { flags[skillId] = value; }
    bool IsLearned(int skillId) const; // IsTrackable(id) && flags[id]==1
};

// ---------------------------------------------------------------------------
// Resultat d'une tentative de consommation de MP au cast.
// ---------------------------------------------------------------------------
struct SkillCastResult {
    bool ok   = false; // true = MP suffisant, debite
    int  cost = 0;     // cout MP reel calcule (apres regen)
};

// ========================= API du systeme ==================================

// Acces 1-based au record SKILL_INFO (nul si id hors bornes ou record vide).
const uint8_t* Skill_GetRecord(const DataTable& skillTbl, int skillId);

// Nom de la competence (skillinfo::kOffName, C-string embarque dans le record -
// PAS une entree StrTable). Chaine vide si rec == nullptr. Le buffer pointe DANS
// rec (duree de vie liee a la DataTable, ne pas conserver le pointeur au-dela).
const char* Skill_GetName(const uint8_t* rec);

// Index d'icone reel (skillinfo::kOffIconIndex), distinct du skillId. 0 si rec
// == nullptr. Cf. commentaire kOffIconIndex : l'atlas d'origine (unk_A1BD60,
// pas de source .IMG identifiee) n'est pas reproductible tel quel cote client
// reecrit ; cette valeur sert de meilleur candidat d'index pour toute resolution
// d'icone par fichier individuel (cf. NoteSkillIcon, UI/SkillTreeWindow.cpp).
int Skill_GetIconIndex(const uint8_t* rec);

// Formule pivot : interpole une stat de competence entre min et max selon le
// niveau. statIndex 1..25 (1 = cout MP, 6 = portee/vitesse, 7 = portee dist).
// Retour double (l'appelant applique Skill_Ftol). Cf. §6.1.
double Skill_InterpStat(const DataTable& skillTbl, int skillId, int level, int statIndex);

// Cout MP NOMINAL affiche (tooltip/UI). Table codee en dur 1..138 ; hors table :
// classe de l'arme equipee (self.equip[7]). Cf. §6.2 « nominal ».
int Skill_CostById(int skillId, const SelfState& self, const DataTable& itemTbl);

// Somme de la reduction % cout MP = Σ ITEM_INFO+360 sur les 13 equipements.
int Skill_CalcRegenPct(const SelfState& self, const DataTable& itemTbl);

// Cout MP REEL debite : ftol(InterpStat #1) reduit du % regen. Cf. §6.2 « reel ».
int Skill_CalcRealMpCost(const DataTable& skillTbl, int skillId, int level, int regenPct);

// Verifie et debite le MP (mirroir Skill_CastStoredAtTarget) : si self.mp >= cout
// alors self.mp -= cout et ok=true ; sinon ok=false (message 147 cote client).
SkillCastResult Skill_TryConsumeMp(SelfState& self, const DataTable& skillTbl,
                                   const DataTable& itemTbl, int skillId, int level);

// Disponibilite par niveau : lvlEff=level+levelBonus dans [min,max] du skill, avec
// gate renaissance (rebirth>=7) pour 295/296/322/323. Cf. §6.3.
bool Skill_IsAvailableByLevel(const SkillLevelTable& lvlTbl, int skillId,
                              int level, int levelBonus, int rebirth);

// Disponibilite par branche d'arme : + niveau dans [min,max] ET element attendu.
bool Skill_IsAvailableByBranch(const SkillLevelTable& lvlTbl, int skillId,
                               int level, int levelBonus, int element);

// Decode le niveau courant en un couple (row, col). (-1,-1) si hors de toute
// tranche. Cf. Skill_ResolveLevelSlot 0x4FB370.
// CORRECTIF DOC (verifie par xrefs_to sur l'IDB) : l'UNIQUE appelant de cette
// fonction dans le binaire d'origine est UI_FactionInfoWnd_Render 0x672010 (via
// call 0x673160) — PAS une fenetre « arbre de competences ». (row,col) repere en
// realite le rang/palier courant sur la fenetre « Info de faction »/renaissance
// (d'ou le parametre rebirth). A ne PAS brancher sur SkillTreeWindow : le vrai
// widget d'apprentissage de competences (UI_SkillLearn_Draw 0x5E2200 et suite)
// n'utilise PAS cette fonction — cf. bloc de commentaires en tete de fichier.
void Skill_ResolveLevelSlot(const SkillLevelTable& lvlTbl, int level, int levelBonus,
                            int rebirth, int& outRow, int& outCol);

// Valeurs par classe / palier (tier 1..12). A : classes 1/2/3/5. B : classes 3/7.
int Skill_GetValueByClassA(int classId, int tier);
int Skill_GetValueByClassB(int classId, int tier);

// Cout d'amelioration par niveau 0..12 (0,3500,4500,... ,11000).
int Skill_GetUpgradeCostTier(int level);

// Arbre de talents : 3 dwords (12 octets) -> 5 valeurs de noeud (v = b_lo + 1000*b_hi).
// Retour = octet sentinelle (b[1]) ; 0 => aucun noeud (out mis a zero).
int Skill_UnpackTreeNodes(uint32_t w0, uint32_t w1, uint32_t w2, int out[5]);
// Nombre de paires de noeuds non-nulles consecutives (0..5).
int Skill_CountTreeNodes(uint32_t w0, uint32_t w1, uint32_t w2);

// Apprentissage : place la competence enseignee dans la barre selon sa section,
// debite self.skillPoints du cout SP. Retourne l'index de slot, ou -1 si echec
// (record introuvable, section inconnue, barre pleine). Cf. §8.1 groupe G0.
int Skill_Learn(SkillBar& bar, SelfState& self, const DataTable& skillTbl, uint32_t taughtSkillId);

// Id de competence enseignee par un item (ITEM_INFO+0x15C).
uint32_t Skill_TaughtSkillIdFromItem(const uint8_t* itemRec);

} // namespace ts2::game

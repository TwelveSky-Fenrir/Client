// UI/SkillTreeWindow.h — fenêtre « Arbre de compétences » du client TwelveSky2.
//
// Vue interactive sur game::SkillBar (Game/SkillSystem.h, déjà écrit) : grille
// 8 colonnes × 5 lignes = 40 emplacements, calquée EXACTEMENT sur
// SkillBar::slots (40 slots {skillId, spCost}, cf. commentaire g_LearnedSkills
// 0x16742BC). À droite, une VRAIE grille de nœuds (4x3, paginée) balayant TOUT
// l'arbre de compétences connu (skillId 1..kMaxSkillId présent dans skillTbl,
// cf. game::SkillLevelTable) — pas seulement les compétences immédiatement
// apprenables : chaque nœud affiche son icône (.IMG best-effort, repli sur
// pastille colorée, cf. NoteSkillIcon au .cpp), son niveau requis et son état
// (Locked = niveau/branche non atteint, Available = apprenable maintenant,
// Learned = déjà dans la barre), cf. NodeState/NodeStateOf. Interaction :
//   1. clic sur un nœud Available de la grille « Disponibles »  -> sélectionne
//      la compétence candidate (surlignée) ; clic sur Locked/Learned -> statut
//      informatif seulement (pas de sélection) ;
//   2. clic sur un emplacement VIDE de la grille avec une compétence
//      sélectionnée -> tentative d'apprentissage (AttemptLearn) : vérifie
//      self.skillPoints >= coût SP (SKILL_INFO +0x230, skillinfo::kOffSpCost,
//      garde réelle UI_SkillLearn_OnLDown 0x5E1DC4) puis... N'APPLIQUE RIEN.
//      Corrigé Passe 4 / W6 : cette étape débitait self.skillPoints et plaçait
//      la compétence dans la barre via game::Skill_Learn — un effet local
//      OPTIMISTE que le binaire ne fait PAS (la confirmation réelle,
//      UI_MsgBox_OnLButtonUp case 3 @0x5C0C23, se contente d'ÉMETTRE et
//      d'attendre le serveur), et de surcroît emprunté à un autre flux
//      (Skill_Learn est ancrée sur le handler ENTRANT Pkt_ItemAction G0
//      0x46A456 = apprentissage par LIVRE). L'émission qui devrait la remplacer
//      (opcode 0x13 / sous-code 202) est BLOQUÉE : elle exige l'id du PNJ
//      formateur, que cette fenêtre ne possède pas — cf. le TODO [ancre
//      0x5C0C5E] détaillé au-dessus de AttemptLearn() dans le .cpp.
// Survol d'un emplacement APPRIS : tooltip avec le coût SP réellement débité
// à l'apprentissage (mémorisé dans le slot) ET le coût MP nominal de cast
// (Skill_CostById 0x4CD0E0). Bandeau d'en-tête : points de compétence non
// dépensés (self.skillPoints) + posture/stance active courante
// (Skill_GetActiveStance 0x4FB210, Game/SkillCombat.h).
//
// Aucune action de cette fenêtre n'envoie de paquet réseau — état AUDITÉ (Passe 4 /
// W6), et pour deux raisons de nature DIFFÉRENTE, à ne pas confondre :
//   - Sélection d'un nœud : NE DOIT RIEN ÉMETTRE. Prouvé : UI_SkillLearn_OnLDown
//     0x5E1C40 n'émet rien, il valide (SP / déjà apprise / place libre) puis ouvre une
//     boîte de confirmation (UI_MsgBox_Open kind 3, 0x5E20C0). Le silence réseau au
//     clic est donc FIDÈLE, ce n'est pas un manque.
//   - Confirmation de l'apprentissage : DEVRAIT émettre (opcode 0x13, sous-code 202,
//     [npcId:i32][skillId:i32]) mais reste BLOQUÉE faute d'un npcId prouvé. MISE À JOUR
//     (Passe 4 / W6, vérif IDA) : le builder Net_SendVaultReq_202 a depuis été corrigé
//     en (NetClient&, int32_t npcId, int32_t skillId) (Net/SendPackets.h:242) — il ne
//     tronque plus rien et est conforme au fil. Le SEUL blocage restant est donc le
//     npcId, que cette fenêtre (arbre générique, hotkey 'K') ne possède pas. Détail
//     complet + layout dans le TODO [ancre 0x5C0C5E] au-dessus de AttemptLearn() (.cpp).
// L'apprentissage n'est plus appliqué à l'état local non plus (le binaire ne le fait
// pas) : la fenêtre reste donc une VUE de l'arbre tant que le flux PNJ n'est pas porté.
//
// DISPOSITION — CONFIRME_FIDELE (2026-07-14, décompilation idaTs2, re-vérifiée
// le même jour) : le binaire d'origine n'a pas de disposition d'arbre à
// positions custom par nœud ni de lignes de connexion parent-enfant — sa
// « vraie » disposition (UI_SkillLearn_Draw 0x5E2200, grille 3 colonnes x 8
// lignes par PNJ-formateur, formule de pixel fixe) EST une grille simple ;
// ce fait est confirmé, pas une limite de recherche. Cette fenêtre-ci affiche
// une grille GÉNÉRIQUE paginée différente (pas la grille 3x8 par PNJ) par
// choix d'architecture délibéré (backend NPC->compétences non porté +
// contrat d'ouverture différent), documenté précisément dans le bloc de note
// en tête du .cpp et le bloc « DISPOSITION DE L'ARBRE DE COMPETENCES —
// CONFIRME_FIDELE » en tête de Game/SkillSystem.h. En revanche le nom,
// l'index d'icône et les prérequis affichés par nœud sont désormais lus
// depuis les VRAIS champs SKILL_INFO (Skill_GetName, Skill_GetIconIndex,
// kOffReqWeapon/kOffReqBranch/kOffSection).
//
// Règle du projet : ce fichier n'édite AUCUN header existant ; il inclut
// UI/UIManager.h, Game/SkillSystem.h et Game/SkillCombat.h en lecture seule.
#pragma once
#include "UI/UIManager.h"
#include "Game/SkillSystem.h"
#include "Game/SkillCombat.h"
#include "Gfx/GpuTexture.h"

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>

namespace ts2::ui {

// SkillTreeWindow — Dialog modal (tant qu'ouverte) affichant/pilotant la barre
// de compétences apprises. Hérite de ts2::ui::Dialog (contrat UIManager, non
// édité). Nécessite un Bind() avant tout rendu utile (aucun crash si non liée
// : la fenêtre s'affiche vide et refuse silencieusement les actions).
class SkillTreeWindow : public Dialog {
public:
    SkillTreeWindow();

    // Lie la fenêtre aux données runtime nécessaires : table SKILL_INFO (pour
    // Skill_GetRecord/coût SP), table ITEM_INFO (pour Skill_CostById), table de
    // bornes de niveau (Skill_IsAvailableByLevel), la barre de compétences
    // apprises et l'état du joueur local (skillPoints/niveau/renaissance, LUS
    // seulement). `morph` est optionnel (posture/renaissance) — cf.
    // Game/SkillCombat.h.
    // NB (Passe 4 / W6) : `bar` et `self` ne sont plus JAMAIS écrits par cette
    // fenêtre — l'apprentissage local (game::Skill_Learn) a été retiré, le binaire
    // attendant le serveur (cf. AttemptLearn dans le .cpp). Les références restent
    // non-const car le contrat de Bind() est partagé/appelé par UI/GameWindows.cpp
    // (fichier non possédé par ce front) : le durcir en const dépasse ce périmètre.
    //
    // AUCUN npcId : c'est précisément ce qui bloque l'émission du sous-code 202
    // (cf. TODO [ancre 0x5C0C5E] au-dessus de AttemptLearn dans le .cpp). Le vrai
    // widget d'apprentissage est ouvert PAR un PNJ formateur et tient son
    // enregistrement (*(this+2) == dword_1822ED0) ; cette fenêtre-ci est ouverte au
    // raccourci 'K' et n'en a aucun.
    void Bind(const game::DataTable& skillTbl, const game::DataTable& itemTbl,
              const game::SkillLevelTable& lvlTbl, game::SkillBar& bar,
              game::SelfState& self, const game::CombatMorphState& morph = {});

    void Open() override;  // Dialog::Open() + réinitialise sélection/page/statut
    void Close() override { Dialog::Close(); }

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x, y, w, h; };

    // Cible actuellement « armée » (bouton/emplacement enfoncé, en attente du
    // relâchement dessus) — même pattern que OptionsWindow/WarehouseWindow.
    enum class Target { None, Close, PrevPage, NextPage, GridSlot, CandidateRow };

    // État d'un nœud de l'arbre de compétences (liste « Disponibles », désormais
    // une VRAIE grille de nœuds — pas seulement les compétences immédiatement
    // apprenables) : Locked = niveau/branche non atteint (Skill_IsAvailableByLevel
    // false), Available = apprenable maintenant (sélectionnable), Learned = déjà
    // présente dans bar_ (affichée pour vue d'ensemble de l'arbre, non sélectionnable).
    enum class NodeState { Locked, Available, Learned };
    NodeState NodeStateOf(int skillId) const;

    // Géométrie recalculée à chaque frame à partir des dimensions écran
    // (centrage) ; le hit-test (routé entre deux frames) s'appuie sur
    // lastScreenW_/lastScreenH_ mémorisées au dernier Render.
    void Layout(int screenW, int screenH, Rect& panel, Rect& close, Rect& grid,
                Rect& candPanel, Rect& prevBtn, Rect& nextBtn) const;

    Rect SlotRect(const Rect& grid, int slotIndex) const;
    // Cellule de la grille de nœuds « Disponibles », indexée à plat (row-major,
    // kCandCols colonnes) — remplace l'ancienne liste de lignes texte.
    Rect CandidateCellRect(const Rect& candPanel, int cellIndex) const;

    // Icône d'un nœud (paresseuse, mise en cache) : voir NoteSkillIcon dans le
    // .cpp pour la méthodologie (repli automatique sur pastille colorée).
    gfx::GpuTexture* GetIconTex(IDirect3DDevice9* dev, uint32_t skillId);

    // Reconstruit la liste des compétences apprenables (candidates_) à partir
    // de skillTbl_/lvlTbl_/bar_/self_. Appelée en tête de Render (les deux
    // phases lisent le même résultat, calculé une fois par frame).
    void RecomputeCandidates();

    void ActivateIfHit(const Rect& close, const Rect& grid, const Rect& candPanel,
                        const Rect& prevBtn, const Rect& nextBtn, int x, int y);
    void HandleGridSlotClick(int slotIndex);
    void HandleCandidateClick(int rowOnPage);
    // Confirmation d'apprentissage — miroir de UI_MsgBox_OnLButtonUp case 3 (0x5C0C23).
    // N'émet PAS (npcId indisponible) et n'applique AUCUN effet local, comme le binaire :
    // cf. le TODO [ancre 0x5C0C5E] détaillé au-dessus de la définition dans le .cpp.
    void AttemptLearn();

    static bool In(const Rect& r, int x, int y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }

    // --- Liaison données (non possédée) ---
    const game::DataTable*       skillTbl_ = nullptr;
    const game::DataTable*       itemTbl_  = nullptr;
    const game::SkillLevelTable* lvlTbl_   = nullptr;
    game::SkillBar*              bar_      = nullptr;
    game::SelfState*             self_     = nullptr;
    game::CombatMorphState       morph_{};
    bool                         bound_    = false;

    // --- État interaction ---
    Target      armedTarget_    = Target::None;
    int         armedGridSlot_  = -1;
    int         armedCandRow_   = -1;
    uint32_t    selectedSkillId_ = 0; // compétence candidate sélectionnée (liste de droite)
    int         page_           = 0; // page courante de la liste « Disponibles »
    std::vector<int> candidates_;    // skillIds apprenables, recalculée chaque frame

    std::string statusText_;         // dernier résultat d'action, affiché en pied de fenêtre
    bool        statusIsError_ = false;

    // Icônes de nœud, paresseuses + mises en cache (même pattern que
    // InventoryWindow::iconCache_ / WarehouseWindow::iconCache_). Vidées à
    // Shutdown implicite (durée de vie = process, comme les autres fenêtres).
    std::unordered_map<uint32_t, gfx::GpuTexture> iconCache_;

    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // --- Géométrie (coordonnées panneau, référence 1024x768) ---
    static constexpr int kCols        = 8;
    static constexpr int kRows        = 5;   // 8x5 = 40 == SkillBar::slots.size()
    static constexpr int kCellSize    = 40;
    static constexpr int kCellGap     = 4;
    static constexpr int kCellPitch   = kCellSize + kCellGap;
    static constexpr int kGridW       = kCols * kCellPitch - kCellGap;
    static constexpr int kGridH       = kRows * kCellPitch - kCellGap;

    static constexpr int kGridPad     = 14;
    static constexpr int kTitleH      = 26;
    static constexpr int kHeaderInfoH = 20;
    static constexpr int kInfoBarH    = 32; // tooltip survol (2 lignes) entre grille et pied
    static constexpr int kFooterH     = 40;
    static constexpr int kCloseBtn    = 18;

    static constexpr int kCandPanelW  = 200;
    static constexpr int kCandHeaderH = 16;
    static constexpr int kCandBtnH    = 16;
    // Grille de nœuds « Disponibles » : mêmes kCellSize/kCellGap/kCellPitch que
    // la grille SkillBar (icônes de taille homogène dans toute la fenêtre).
    static constexpr int kCandCols     = 4;
    static constexpr int kCandRows     = 3;
    static constexpr int kItemsPerPage = kCandCols * kCandRows; // 12 nœuds/page

    static constexpr int kPanelW = kGridPad * 2 + kGridW + kGridPad + kCandPanelW;
    static constexpr int kPanelH = kTitleH + kHeaderInfoH + kGridH + kInfoBarH + kFooterH;

    // Borne haute des ids de compétence connus (SkillLevelTable, cf. SkillSystem.h : skillId 1..350).
    // Sert aussi de borne pour la résolution d'icône (dossier 003 de l'atlas UI, 755
    // fichiers CONTIGUS 1..755 — largement au-delà, cf. NoteSkillIcon au .cpp).
    static constexpr int kMaxSkillId = 350;

    // --- Palette (D3DCOLOR = 0xAARRGGBB, cf. contrat UI) ---
    static constexpr D3DCOLOR kColBg            = 0xE0202028u; // fond panneau
    static constexpr D3DCOLOR kColBorder        = 0xFF808080u; // cadre
    static constexpr D3DCOLOR kColTitleBg       = 0xFF2C2C3Cu; // bandeau titre
    static constexpr D3DCOLOR kColTitle         = 0xFFFFDD66u; // titre
    static constexpr D3DCOLOR kColText          = 0xFFFFFFFFu; // texte
    static constexpr D3DCOLOR kColTextDim       = 0xFFAAAAAAu; // texte atténué
    static constexpr D3DCOLOR kColSelect        = 0xFF4060A0u; // survol/sélection
    static constexpr D3DCOLOR kColError         = 0xFFFF6060u; // erreur
    static constexpr D3DCOLOR kColSuccess       = 0xFF60FF60u; // succès
    static constexpr D3DCOLOR kColClose         = 0xFFB03A3Au;
    static constexpr D3DCOLOR kColBtnHover      = 0xFF4060A0u;
    static constexpr D3DCOLOR kColBtn           = 0xFF38384Au;
    static constexpr D3DCOLOR kColBtnOff        = 0xFF26262Au;
    static constexpr D3DCOLOR kColCellLearned      = 0xFF34503Eu; // emplacement appris (vert sourd)
    static constexpr D3DCOLOR kColCellLearnedHover = 0xFF3C6048u;
    static constexpr D3DCOLOR kColCellEmpty        = 0xFF1A1A20u; // emplacement vide
    static constexpr D3DCOLOR kColCellEmptyHover   = 0xFF2A2A34u;
    static constexpr D3DCOLOR kColRowBg         = 0xFF262630u; // fond cellule liste « Disponibles »
    static constexpr D3DCOLOR kColRowHover      = 0xFF32324Au;

    // Pastilles de repli par état de nœud (icône .IMG indisponible/non chargée) —
    // cf. NodeState / NoteSkillIcon (.cpp).
    static constexpr D3DCOLOR kColPastilleLocked    = 0xFF4A4A50u; // gris terne : verrouillé
    static constexpr D3DCOLOR kColPastilleAvailable = 0xFFC9A227u; // or : apprenable
    static constexpr D3DCOLOR kColPastilleLearned   = 0xFF3FAE55u; // vert : déjà appris
    static constexpr D3DCOLOR kColNodeBorderLocked    = 0xFF3A3A3Eu;
    static constexpr D3DCOLOR kColNodeBorderAvailable = 0xFFDDBB55u;
    static constexpr D3DCOLOR kColNodeBorderLearned   = 0xFF60FF60u;
    static constexpr D3DCOLOR kColNodeDimOverlay      = 0x90101014u; // assombrit icône verrouillée
};

} // namespace ts2::ui

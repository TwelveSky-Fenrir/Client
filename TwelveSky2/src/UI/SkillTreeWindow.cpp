// UI/SkillTreeWindow.cpp — implémentation de la fenêtre Arbre de compétences.
// Voir SkillTreeWindow.h pour le contrat d'interaction complet.
//
// DISPOSITION — CONFIRME_FIDELE (2026-07-14, décompilation idaTs2, re-vérifiée
// le même jour via une seconde passe + xrefs_to sur SkillGrowthTbl_GetRecord
// 0x4C4E90 : seuls UI_SkillLearn_OnLDown/Draw/OnMove lisent cette table côté
// UI, aucun autre widget « arbre » n'existe dans le binaire). Voir le bloc de
// note détaillé en tête de Game/SkillSystem.h pour les adresses exactes et le
// détail de la ré-vérification. Résumé du verdict :
//   - CONFIRME_FIDELE : le binaire d'origine n'a PAS de disposition d'arbre à
//     positions custom par nœud ni de lignes de connexion parent-enfant. Le
//     vrai widget d'apprentissage (UI_SkillLearn_Draw 0x5E2200 et les
//     fonctions sœurs 0x5E1BA0..0x5E2450) est une grille FIXE simple, par
//     FORMULE de pixel (x = x0+76*i+35, y = y0+54*j+71), 3 colonnes (branche
//     d'arme) x 8 lignes (palier), PROPRE À CHAQUE PNJ formateur ouvert (le
//     skillId de chaque case vient du NPC lui-même, pas d'une table globale)
//     — et il ne dessine AUCUNE ligne de connexion parent-enfant (vérifié en
//     lisant le corps complet de la fonction, deux fois). Ce n'est plus une
//     limite de recherche : la disposition « grille simple » EST la vraie
//     disposition du jeu d'origine, confirmée par la décompilation.
//   - Ce qui reste un choix d'architecture réel (distinct du point ci-dessus,
//     PAS une disposition non retrouvée) : cette fenêtre-ci (SkillTreeWindow)
//     reste volontairement une grille GÉNÉRIQUE paginée (kCandCols x
//     kCandRows, cf. .h) plutôt que de reproduire la grille 3x8 par PNJ telle
//     quelle, pour deux raisons précises :
//       1) Backend manquant : la table « quel skillId dans quelle case, pour
//          quel PNJ » n'a pas d'équivalent dans GameDatabases/DataTable côté
//          client réécrit (aucune table NPC->compétences enseignées portée) ;
//          sans elle, une grille 3x8 « à la vraie disposition » afficherait
//          des cases vides ou des données inventées, moins fidèle que la liste
//          paginée actuelle qui balaie la VRAIE table SKILL_INFO en entier.
//       2) Contrat d'ouverture différent : dans l'original, cette fenêtre
//          s'ouvre en interagissant avec un PNJ formateur (mode Enter/Open,
//          gate faction/élément) ; ici elle s'ouvre au raccourci global 'K'
//          (GameWindows.h) — rearchitecturer ce contrat est hors périmètre de
//          cette passe (ne touche pas Scene/SceneManager).
//   - Ce qui EST maintenant fidèle (ex-approximatif) : le nom affiché par
//     nœud vient du champ réel du record (game::Skill_GetName, skillinfo::
//     kOffName) et non plus d'un simple « #id » ; l'index d'icône sondé vient
//     du champ réel skillinfo::kOffIconIndex (idx137) et non plus d'une
//     hypothèse « index == skillId » (qui était FAUSSE, cf. NoteSkillIcon) ;
//     le panneau de détail affiche aussi les prérequis réels (arme/branche/
//     section, champs SKILL_INFO) en plus du niveau et du coût SP.
#include "UI/SkillTreeWindow.h"
#include "UI/PanelSkin.h"
#include "Asset/ImgFile.h"

#include <cstdio> // snprintf

namespace ts2::ui {

namespace {
// Fond de panneau réel (best effort) : gabarit large (702,488) du dossier atlas
// UI G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, choisi par
// proximité de ratio avec le panneau Arbre de compétences (~590x334, panneau
// large ; cf. méthodologie détaillée dans UI/PanelSkin.h). Repli automatique
// sur kColBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01672.IMG");

// NoteSkillIcon — chemin .IMG de l'icône d'un nœud de compétence.
//
// SOURCE DE L'INDEX (mise à jour 2026-07-14, IDA de nouveau accessible) :
// CONFIRMÉ par décompilation directe que l'icône d'un nœud n'est PAS indexée par
// skillId mais par un champ dédié du record SKILL_INFO — game::skillinfo::
// kOffIconIndex (idx137, +0x224), lu via Skill_GetIconIndex(rec). Cf.
// UI_SkillLearn_Draw 0x5E2200 (corps : Sprite2D_Draw((int)&unk_A1BD60 + 148*v16,
// ...) avec v16 = v12[137]-1) : l'ancienne hypothèse « index = skillId » ci-
// dessous était donc une approximation, corrigée.
//
// DESTINATION (dossier .IMG) : en revanche, l'atlas d'origine (unk_A1BD60, une
// table de 148 o/entrée pointant vers des Sprite2D pré-chargés depuis un fichier
// non identifié dans cette session — pas de .npk/.IMG résolu par xref direct)
// n'a PAS d'équivalent « un fichier par icône » retrouvé côté client réécrit.
// On garde donc la MÊME MÉTHODOLOGIE de repli que ResolveItemIconPath
// (UI/InventoryWindow.cpp) : sondage direct des .IMG du dossier
// G03_GDATA/D01_GIMAGE2D/003 (755 fichiers, index CONTIGU 1..755, 25/50 px),
// mais désormais adressé par l'index d'icône RÉEL (kOffIconIndex) au lieu du
// skillId — NON CONFIRMÉ que ce dossier soit la bonne destination (aucune xref
// directe entre kOffIconIndex et un chemin G03_GDATA), mais l'INDEX employé pour
// sonder ce dossier est désormais la vraie donnée du binaire plutôt qu'une
// supposition. Repli AUTOMATIQUE et SILENCIEUX sur pastille colorée (cf.
// NodeState) dans TOUS les cas d'échec : fichier absent, device D3D9
// indisponible, décodage/texture GPU en échec.
std::string ResolveSkillIconPath(uint32_t iconIndex) {
    if (iconIndex == 0 || iconIndex > 755) return {};
    char buf[64];
    std::snprintf(buf, sizeof(buf), "G03_GDATA\\D01_GIMAGE2D\\003\\003_%05u.IMG", iconIndex);
    return std::string(buf);
}

// Libellé affiché pour un nœud : nom réel (game::Skill_GetName, cf. skillinfo::
// kOffName) si le record existe, sinon repli "#id". La largeur EXACTE du champ
// nom dans le record n'est pas confirmée (seul le NUL terminal la borne) : on
// tronque défensivement via la précision de format ("%.*s", maxChars) pour ne
// jamais laisser un champ mal terminé déborder dans le reste de l'UI ; maxChars
// est ajusté par l'appelant selon la largeur disponible (case 40px vs tooltip).
void FormatSkillLabel(char* out, size_t outSize, const uint8_t* rec, uint32_t skillId, int maxChars) {
    const char* name = rec ? game::Skill_GetName(rec) : "";
    if (name && name[0] != '\0')
        std::snprintf(out, outSize, "%.*s", maxChars, name);
    else
        std::snprintf(out, outSize, "#%u", skillId);
}
} // namespace

SkillTreeWindow::SkillTreeWindow() {
    x_ = 0; y_ = 0; bOpen_ = false;
}

void SkillTreeWindow::Bind(const game::DataTable& skillTbl, const game::DataTable& itemTbl,
                            const game::SkillLevelTable& lvlTbl, game::SkillBar& bar,
                            game::SelfState& self, const game::CombatMorphState& morph) {
    skillTbl_ = &skillTbl;
    itemTbl_  = &itemTbl;
    lvlTbl_   = &lvlTbl;
    bar_      = &bar;
    self_     = &self;
    morph_    = morph;
    bound_    = true;
}

void SkillTreeWindow::Open() {
    Dialog::Open();
    page_ = 0;
    selectedSkillId_ = 0;
    statusText_.clear();
    statusIsError_ = false;
    armedTarget_ = Target::None;
    armedGridSlot_ = -1;
    armedCandRow_ = -1;
}

void SkillTreeWindow::Layout(int screenW, int screenH, Rect& panel, Rect& close, Rect& grid,
                              Rect& candPanel, Rect& prevBtn, Rect& nextBtn) const {
    panel.x = screenW / 2 - kPanelW / 2;
    panel.y = screenH / 2 - kPanelH / 2;
    panel.w = kPanelW;
    panel.h = kPanelH;

    // Bouton fermeture (croix) en haut à droite du panneau.
    close = { panel.x + panel.w - kCloseBtn - 6, panel.y + 4, kCloseBtn, kCloseBtn };

    // Grille 8x5 (= SkillBar::slots), sous le bandeau titre + ligne d'en-tête.
    grid.x = panel.x + kGridPad;
    grid.y = panel.y + kTitleH + kHeaderInfoH;
    grid.w = kGridW;
    grid.h = kGridH;

    // Liste des compétences apprenables, à droite de la grille, même hauteur.
    candPanel.x = grid.x + grid.w + kGridPad;
    candPanel.y = grid.y;
    candPanel.w = kCandPanelW;
    candPanel.h = kGridH;

    // Boutons de pagination, ancrés en bas du panneau candidats.
    const int bw = (kCandPanelW - 6) / 2;
    prevBtn = { candPanel.x,          candPanel.y + candPanel.h - kCandBtnH - 2, bw, kCandBtnH };
    nextBtn = { candPanel.x + bw + 6, candPanel.y + candPanel.h - kCandBtnH - 2, bw, kCandBtnH };
}

SkillTreeWindow::Rect SkillTreeWindow::SlotRect(const Rect& grid, int slotIndex) const {
    const int row = slotIndex / kCols;
    const int col = slotIndex % kCols;
    return { grid.x + col * kCellPitch, grid.y + row * kCellPitch, kCellSize, kCellSize };
}

SkillTreeWindow::Rect SkillTreeWindow::CandidateCellRect(const Rect& candPanel, int cellIndex) const {
    const int row = cellIndex / kCandCols;
    const int col = cellIndex % kCandCols;
    // Grille centrée horizontalement dans candPanel (mêmes pas que la grille SkillBar).
    const int gridW = kCandCols * kCellPitch - kCellGap;
    const int originX = candPanel.x + (candPanel.w - gridW) / 2;
    const int originY = candPanel.y + kCandHeaderH;
    return { originX + col * kCellPitch, originY + row * kCellPitch, kCellSize, kCellSize };
}

SkillTreeWindow::NodeState SkillTreeWindow::NodeStateOf(int skillId) const {
    if (!bound_) return NodeState::Locked;
    for (const auto& s : bar_->slots) {
        if (s.skillId == static_cast<uint32_t>(skillId)) return NodeState::Learned;
    }
    if (game::Skill_IsAvailableByLevel(*lvlTbl_, skillId, self_->level, self_->levelBonus,
                                        morph_.rebirthTier)) {
        return NodeState::Available;
    }
    return NodeState::Locked;
}

gfx::GpuTexture* SkillTreeWindow::GetIconTex(IDirect3DDevice9* dev, uint32_t skillId) {
    // Cache toujours clé par skillId (stable, 1:1 avec le nœud affiché) même si la
    // résolution de fichier utilise désormais l'index d'icône réel du record
    // (skillinfo::kOffIconIndex) — cf. NoteSkillIcon plus haut.
    auto it = iconCache_.find(skillId);
    if (it != iconCache_.end())
        return it->second.Valid() ? &it->second : nullptr;

    gfx::GpuTexture tex;
    if (dev && bound_) {
        const uint8_t* rec = game::Skill_GetRecord(*skillTbl_, static_cast<int>(skillId));
        const int iconIndex = rec ? game::Skill_GetIconIndex(rec) : 0;
        const std::string path = ResolveSkillIconPath(static_cast<uint32_t>(iconIndex));
        if (!path.empty()) {
            asset::ImgFile img;
            if (img.Load(path))
                tex.CreateFromImgFile(dev, img);
        }
    }
    // Cache aussi l'échec (texture invalide) pour ne pas retenter le chargement chaque
    // frame (même pattern que InventoryWindow::GetIconTex / WarehouseWindow::GetIconTex).
    auto res = iconCache_.emplace(skillId, std::move(tex));
    return res.first->second.Valid() ? &res.first->second : nullptr;
}

// Reconstruit la liste des nœuds de l'arbre : TOUS les skillId dotés d'un record
// SKILL_INFO valide (pas seulement les apprenables maintenant), afin que la grille
// « Disponibles » montre la VUE D'ENSEMBLE de l'arbre (verrouillé/apprenable/appris).
// L'état par nœud est recalculé à la volée par NodeStateOf (pas mémorisé ici) : la
// liste elle-même ne change que si skillTbl_/kMaxSkillId changent, mais l'état
// dépend de bar_/self_/morph_ qui bougent à chaque frame.
void SkillTreeWindow::RecomputeCandidates() {
    candidates_.clear();
    if (!bound_) { page_ = 0; return; }

    for (int id = 1; id <= kMaxSkillId; ++id) {
        if (!game::Skill_GetRecord(*skillTbl_, id)) continue; // record absent/vide
        candidates_.push_back(id);
    }

    const int maxPage = candidates_.empty() ? 0
        : (static_cast<int>(candidates_.size()) - 1) / kItemsPerPage;
    if (page_ > maxPage) page_ = maxPage;
    if (page_ < 0) page_ = 0;
}

bool SkillTreeWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    Rect panel, close, grid, candPanel, prevBtn, nextBtn;
    Layout(lastScreenW_, lastScreenH_, panel, close, grid, candPanel, prevBtn, nextBtn);

    armedTarget_ = Target::None;
    armedGridSlot_ = -1;
    armedCandRow_ = -1;

    if (!In(panel, x, y)) return false; // clic hors fenêtre : ne consomme pas

    if (In(close, x, y)) {
        armedTarget_ = Target::Close;
    } else if (In(prevBtn, x, y)) {
        armedTarget_ = Target::PrevPage;
    } else if (In(nextBtn, x, y)) {
        armedTarget_ = Target::NextPage;
    } else if (In(grid, x, y)) {
        for (int i = 0; i < kCols * kRows; ++i) {
            if (In(SlotRect(grid, i), x, y)) { armedTarget_ = Target::GridSlot; armedGridSlot_ = i; break; }
        }
    } else if (In(candPanel, x, y)) {
        for (int i = 0; i < kItemsPerPage; ++i) {
            if (In(CandidateCellRect(candPanel, i), x, y)) {
                armedTarget_ = Target::CandidateRow; armedCandRow_ = i; break;
            }
        }
    }
    return true; // panneau modal de fait tant qu'ouverte : toute la surface consomme le clic
}

void SkillTreeWindow::ActivateIfHit(const Rect& close, const Rect& grid, const Rect& candPanel,
                                     const Rect& prevBtn, const Rect& nextBtn, int x, int y) {
    switch (armedTarget_) {
        case Target::Close:
            if (In(close, x, y)) Close();
            break;
        case Target::PrevPage:
            if (In(prevBtn, x, y) && page_ > 0) --page_;
            break;
        case Target::NextPage: {
            const int maxPage = candidates_.empty() ? 0
                : (static_cast<int>(candidates_.size()) - 1) / kItemsPerPage;
            if (In(nextBtn, x, y) && page_ < maxPage) ++page_;
            break;
        }
        case Target::GridSlot: {
            if (armedGridSlot_ < 0 || armedGridSlot_ >= kCols * kRows) break;
            if (!In(SlotRect(grid, armedGridSlot_), x, y)) break;
            HandleGridSlotClick(armedGridSlot_);
            break;
        }
        case Target::CandidateRow: {
            if (armedCandRow_ < 0 || armedCandRow_ >= kItemsPerPage) break;
            if (!In(CandidateCellRect(candPanel, armedCandRow_), x, y)) break;
            HandleCandidateClick(armedCandRow_);
            break;
        }
        default: break;
    }
    armedTarget_ = Target::None;
    armedGridSlot_ = -1;
    armedCandRow_ = -1;
}

void SkillTreeWindow::HandleGridSlotClick(int slotIndex) {
    if (!bound_) return;
    if (slotIndex < 0 || static_cast<size_t>(slotIndex) >= bar_->slots.size()) return;
    if (bar_->slots[static_cast<size_t>(slotIndex)].skillId != 0) {
        // Emplacement déjà occupé : pas de retrait/réorganisation dans cette fenêtre.
        return;
    }
    if (selectedSkillId_ == 0) {
        statusText_ = "Sélectionnez d'abord une compétence dans la liste « Disponibles ».";
        statusIsError_ = true;
        return;
    }
    AttemptLearn();
}

void SkillTreeWindow::HandleCandidateClick(int rowOnPage) {
    const size_t idx = static_cast<size_t>(page_) * kItemsPerPage + static_cast<size_t>(rowOnPage);
    if (idx >= candidates_.size()) return;
    const int id = candidates_[idx];

    switch (NodeStateOf(id)) {
        case NodeState::Learned:
            statusText_ = "Compétence déjà apprise.";
            statusIsError_ = false;
            return; // pas de sélection : rien à apprendre
        case NodeState::Locked: {
            char buf[96];
            const int minLvl = lvlTbl_ ? lvlTbl_->Min(id) : 0;
            std::snprintf(buf, sizeof(buf), "Verrouillé : niveau %d requis.", minLvl);
            statusText_ = buf;
            statusIsError_ = true;
            return; // pas de sélection : nœud non apprenable
        }
        case NodeState::Available:
            break; // sélectionnable, cf. ci-dessous
    }

    selectedSkillId_ = static_cast<uint32_t>(id);
    statusText_ = "Compétence sélectionnée : cliquez un emplacement libre de la grille.";
    statusIsError_ = false;
}

void SkillTreeWindow::AttemptLearn() {
    if (!bound_) return;

    const uint8_t* rec = game::Skill_GetRecord(*skillTbl_, static_cast<int>(selectedSkillId_));
    if (!rec) {
        statusText_ = "Compétence introuvable dans la table.";
        statusIsError_ = true;
        selectedSkillId_ = 0;
        return;
    }

    const int spCost = game::Skill_ReadI32(rec, game::skillinfo::kOffSpCost);
    if (self_->skillPoints < spCost) {
        statusText_ = "Points de compétence insuffisants.";
        statusIsError_ = true;
        return; // on garde la sélection : l'utilisateur peut réessayer après avoir gagné des points
    }

    // Skill_Learn débite self.skillPoints et place la compétence dans la barre selon sa
    // section (indépendant de l'emplacement effectivement cliqué, fidèle à l'original).
    const int slot = game::Skill_Learn(*bar_, *self_, *skillTbl_, selectedSkillId_);
    if (slot >= 0) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "Compétence #%u apprise (emplacement %d).",
                      selectedSkillId_, slot);
        statusText_ = buf;
        statusIsError_ = false;
        selectedSkillId_ = 0;
        // TODO(send): notifier le serveur de l'apprentissage — builder non identifié avec
        // certitude dans Net/SendPackets.h à la date d'écriture (candidat probable : groupe
        // d'opcode « item action » G0, cf. Pkt_ItemAction 0x46A456 côté handler entrant ;
        // aucun Net_SendItemActionG0(...) confirmé ici).
        // PISTE (ré-audit W4-F3, NON PROUVÉE) : cGameHud_OnMouseDown 0x62B080 route
        // l'apprentissage via SkillTrain_RowCount 0x5F1930 / SkillTrain_SkillIdAt
        // 0x5F1B70 / Game_MapSkillRowToId 0x6121A0 puis appelle Net_SendGuarded_12
        // (0x593670) / Net_SendGuarded_13 (0x5936F0) = Net_SendOp75(container, 12|13,
        // payload) -> opcode sortant 0x4B, sous-op 12/13, gardés cooldown/morph. Payload
        // vide observé -> rôle « apprendre skill » NON confirmé : NE PAS câbler (et pas
        // de net_ joignable ici de toute façon). Ne PAS inventer l'appel : l'état local
        // (bar_/self_->skillPoints) est déjà à jour pour l'UI.
    } else {
        statusText_ = "Échec de l'apprentissage (section incompatible ou barre pleine).";
        statusIsError_ = true;
    }
}

bool SkillTreeWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    Rect panel, close, grid, candPanel, prevBtn, nextBtn;
    Layout(lastScreenW_, lastScreenH_, panel, close, grid, candPanel, prevBtn, nextBtn);

    const bool inside = In(panel, x, y);
    if (inside) {
        ActivateIfHit(close, grid, candPanel, prevBtn, nextBtn, x, y);
    } else {
        armedTarget_ = Target::None;
        armedGridSlot_ = -1;
        armedCandRow_ = -1;
    }
    return inside; // ne consomme que si le relâchement tombe dans la fenêtre
}

bool SkillTreeWindow::OnKey(int vk) {
    if (!bOpen_) return false;
    if (vk == VK_ESCAPE) { Close(); return true; }
    return true; // fenêtre modale de fait tant qu'ouverte : avale les autres touches
}

void SkillTreeWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;

    Rect panel, close, grid, candPanel, prevBtn, nextBtn;
    Layout(ctx.screenW, ctx.screenH, panel, close, grid, candPanel, prevBtn, nextBtn);
    RecomputeCandidates();

    const int maxPage = candidates_.empty() ? 0
        : (static_cast<int>(candidates_.size()) - 1) / kItemsPerPage;

    char buf[160];

    if (ctx.phase == UiPhase::Panels) {
        // Fond + cadre du panneau + bandeau titre.
        kPanelBg.Draw(ctx, panel.x, panel.y, panel.w, panel.h, kColBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, panel.h, kColBorder, 2);
        ctx.FillRect(panel.x, panel.y, panel.w, kTitleH, kColTitleBg);
        ctx.DrawFrame(panel.x, panel.y, panel.w, kTitleH, kColBorder, 1);

        // Bouton fermeture.
        const bool closeHover = In(close, cursorX, cursorY);
        ctx.FillRect(close.x, close.y, close.w, close.h, closeHover ? kColBtnHover : kColClose);
        ctx.DrawFrame(close.x, close.y, close.w, close.h, kColBorder, 1);

        // Device D3D9 pour la résolution d'icônes (nul tant que device pas prêt -> repli
        // pastille automatique dans les deux grilles ci-dessous).
        IDirect3DDevice9* dev = ctx.renderer ? ctx.renderer->Device() : nullptr;

        // Grille 8x5 des emplacements de la barre de compétences (icône réelle si
        // résolue, sinon pastille pleine colorée — même esprit que WarehouseWindow).
        for (int i = 0; i < kCols * kRows; ++i) {
            const Rect r = SlotRect(grid, i);
            const uint32_t slotSkillId = bound_ ? bar_->slots[static_cast<size_t>(i)].skillId : 0;
            const bool occupied = slotSkillId != 0;
            const bool hover = In(r, cursorX, cursorY);

            gfx::GpuTexture* icon = occupied ? GetIconTex(dev, slotSkillId) : nullptr;
            if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 && ctx.sprites) {
                ctx.FillRect(r.x, r.y, r.w, r.h, kColCellLearned); // fond neutre sous l'icône
                const float sx = static_cast<float>(r.w) / static_cast<float>(icon->Width());
                const float sy = static_cast<float>(r.h) / static_cast<float>(icon->Height());
                ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, r.x, r.y, sx, sy,
                                              gfx::kSpriteWhite, /*compensatePos=*/true);
            } else {
                D3DCOLOR bg;
                if (occupied) {
                    bg = hover ? kColCellLearnedHover : kColCellLearned; // pastille de repli
                } else if (hover && selectedSkillId_ != 0) {
                    bg = kColSelect; // cible valide pour la sélection en attente
                } else {
                    bg = hover ? kColCellEmptyHover : kColCellEmpty;
                }
                ctx.FillRect(r.x, r.y, r.w, r.h, bg);
            }
            const D3DCOLOR frame = (occupied && hover) ? kColSelect : kColBorder;
            ctx.DrawFrame(r.x, r.y, r.w, r.h, frame, (occupied && hover) ? 2 : 1);
        }

        // Grille de nœuds « Disponibles » (arbre complet, paginé) : icône réelle si
        // résolue, sinon pastille colorée par état (Locked/Available/Learned).
        ctx.FillRect(candPanel.x, candPanel.y, candPanel.w, kCandHeaderH, kColTitleBg);
        for (int i = 0; i < kItemsPerPage; ++i) {
            const size_t idx = static_cast<size_t>(page_) * kItemsPerPage + static_cast<size_t>(i);
            if (idx >= candidates_.size()) break;
            const int id = candidates_[idx];
            const NodeState state = NodeStateOf(id);
            const Rect r = CandidateCellRect(candPanel, i);
            const bool selected = (state == NodeState::Available &&
                                    static_cast<uint32_t>(id) == selectedSkillId_);
            const bool hover = In(r, cursorX, cursorY);

            D3DCOLOR pastille, border;
            switch (state) {
                case NodeState::Learned:   pastille = kColPastilleLearned;   border = kColNodeBorderLearned;   break;
                case NodeState::Available: pastille = kColPastilleAvailable; border = kColNodeBorderAvailable; break;
                default:                   pastille = kColPastilleLocked;    border = kColNodeBorderLocked;    break;
            }

            gfx::GpuTexture* icon = GetIconTex(dev, static_cast<uint32_t>(id));
            if (icon && icon->Handle() && icon->Width() > 0 && icon->Height() > 0 && ctx.sprites) {
                ctx.FillRect(r.x, r.y, r.w, r.h, kColRowBg); // fond neutre sous l'icône
                const float sx = static_cast<float>(r.w) / static_cast<float>(icon->Width());
                const float sy = static_cast<float>(r.h) / static_cast<float>(icon->Height());
                ctx.sprites->DrawSpriteScaled(icon->Handle(), nullptr, r.x, r.y, sx, sy,
                                              gfx::kSpriteWhite, /*compensatePos=*/true);
                if (state == NodeState::Locked) // assombrit l'icône pour les nœuds verrouillés
                    ctx.FillRect(r.x, r.y, r.w, r.h, kColNodeDimOverlay);
            } else {
                ctx.FillRect(r.x, r.y, r.w, r.h, hover ? kColRowHover : pastille); // pastille de repli
            }
            ctx.DrawFrame(r.x, r.y, r.w, r.h, selected ? kColSelect : border, selected ? 2 : 1);
        }

        // Boutons de pagination.
        const bool prevHover = In(prevBtn, cursorX, cursorY);
        ctx.FillRect(prevBtn.x, prevBtn.y, prevBtn.w, prevBtn.h,
                     (page_ > 0) ? (prevHover ? kColBtnHover : kColBtn) : kColBtnOff);
        ctx.DrawFrame(prevBtn.x, prevBtn.y, prevBtn.w, prevBtn.h, kColBorder, 1);
        const bool nextHover = In(nextBtn, cursorX, cursorY);
        ctx.FillRect(nextBtn.x, nextBtn.y, nextBtn.w, nextBtn.h,
                     (page_ < maxPage) ? (nextHover ? kColBtnHover : kColBtn) : kColBtnOff);
        ctx.DrawFrame(nextBtn.x, nextBtn.y, nextBtn.w, nextBtn.h, kColBorder, 1);
        return;
    }

    // --- Phase texte ---
    ctx.Text("Compétences", panel.x + 10, panel.y + 6, kColTitle);
    ctx.Text("X", close.x + 5, close.y + 2, kColText);

    // Ligne d'en-tête : points de compétence non dépensés + posture active.
    std::snprintf(buf, sizeof(buf), "Points de compétence disponibles : %d",
                  bound_ ? self_->skillPoints : 0);
    ctx.Text(buf, panel.x + 10, panel.y + kTitleH + 3, kColText);
    if (bound_) {
        const int stance = game::Skill_GetActiveStance(*self_, morph_, *lvlTbl_);
        if (stance > 0) std::snprintf(buf, sizeof(buf), "Posture active : #%d", stance);
        else            std::snprintf(buf, sizeof(buf), "Posture active : aucune");
        ctx.Text(buf, panel.x + 300, panel.y + kTitleH + 3, kColTextDim);
    }

    // Contenu de la grille : id de la compétence apprise + tooltip au survol.
    for (int i = 0; i < kCols * kRows; ++i) {
        const Rect r = SlotRect(grid, i);
        if (!bound_) continue;
        const auto& slot = bar_->slots[static_cast<size_t>(i)];
        if (slot.skillId != 0) {
            const uint8_t* rec = game::Skill_GetRecord(*skillTbl_, static_cast<int>(slot.skillId));
            FormatSkillLabel(buf, sizeof(buf), rec, slot.skillId, /*maxChars=*/6); // case 40px
            ctx.Text(buf, r.x + 3, r.y + 3, kColText);
            if (In(r, cursorX, cursorY)) {
                std::snprintf(buf, sizeof(buf), "Coût d'apprentissage : %d SP", slot.spCost);
                ctx.Text(buf, grid.x, grid.y + grid.h + 4, kColTextDim);
                const int mpCost = itemTbl_
                    ? game::Skill_CostById(static_cast<int>(slot.skillId), *self_, *itemTbl_) : 0;
                std::snprintf(buf, sizeof(buf), "Coût MP nominal (cast) : %d", mpCost);
                ctx.Text(buf, grid.x, grid.y + grid.h + 18, kColTextDim);
            }
        } else if (selectedSkillId_ != 0 && In(r, cursorX, cursorY)) {
            ctx.Text("+", r.x + r.w / 2 - 3, r.y + r.h / 2 - 6, kColSuccess);
        }
    }

    // Grille de nœuds « Disponibles » : arbre complet, chaque nœud affiche son id +
    // niveau requis à même la cellule ; un survol détaille l'état/coût sous la grille.
    ctx.Text("Disponibles", candPanel.x + 4, candPanel.y + 2, kColTitle);
    int hoveredNodeId = 0;
    for (int i = 0; i < kItemsPerPage; ++i) {
        const size_t idx = static_cast<size_t>(page_) * kItemsPerPage + static_cast<size_t>(i);
        if (idx >= candidates_.size()) break;
        const Rect r = CandidateCellRect(candPanel, i);
        const int id = candidates_[idx];
        const int minLvl = lvlTbl_ ? lvlTbl_->Min(id) : 0;
        const NodeState state = NodeStateOf(id);
        const D3DCOLOR idColor = (state == NodeState::Locked) ? kColTextDim : kColText;

        const uint8_t* rec = game::Skill_GetRecord(*skillTbl_, id);
        FormatSkillLabel(buf, sizeof(buf), rec, static_cast<uint32_t>(id), /*maxChars=*/8); // case 40px
        ctx.Text(buf, r.x + 2, r.y + 1, idColor);
        std::snprintf(buf, sizeof(buf), "Lv%d", minLvl);
        ctx.Text(buf, r.x + 2, r.y + r.h - 11, idColor);
        if (state == NodeState::Learned)
            ctx.Text("OK", r.x + r.w - 16, r.y + r.h - 11, kColSuccess);

        if (In(r, cursorX, cursorY)) hoveredNodeId = id;
    }
    if (candidates_.empty()) {
        ctx.Text("Aucune compétence connue dans la table SKILL_INFO.",
                  candPanel.x + 4, candPanel.y + kCandHeaderH + 4, kColTextDim);
    }

    // Détail du nœud survolé (nom/id/niveau/coût/prérequis/état), sous la grille.
    if (hoveredNodeId != 0) {
        const NodeState state = NodeStateOf(hoveredNodeId);
        const uint8_t* rec = game::Skill_GetRecord(*skillTbl_, hoveredNodeId);
        const int spCost = rec ? game::Skill_ReadI32(rec, game::skillinfo::kOffSpCost) : 0;
        const int minLvl = lvlTbl_ ? lvlTbl_->Min(hoveredNodeId) : 0;
        const int maxLvl = lvlTbl_ ? lvlTbl_->Max(hoveredNodeId) : 0;
        // Nom réel + id (donnée SKILL_INFO, cf. skillinfo::kOffName).
        FormatSkillLabel(buf, sizeof(buf), rec, static_cast<uint32_t>(hoveredNodeId), /*maxChars=*/40);
        char nameBuf[80];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s (#%d)", buf, hoveredNodeId);
        const int detailY = candPanel.y + kCandHeaderH + kCandRows * kCellPitch - kCellGap;
        ctx.Text(nameBuf, candPanel.x + 4, detailY + 4, kColTitle);
        // Niveau requis + coût SP.
        std::snprintf(buf, sizeof(buf), "Niveau %d..%d - %d SP", minLvl, maxLvl, spCost);
        ctx.Text(buf, candPanel.x + 4, detailY + 18, kColTextDim);
        // Prérequis (arme/branche/section) — champs SKILL_INFO réels, 0 = aucun.
        const int reqWeapon = rec ? game::Skill_ReadI32(rec, game::skillinfo::kOffReqWeapon) : 0;
        const int reqBranch = rec ? game::Skill_ReadI32(rec, game::skillinfo::kOffReqBranch) : 0;
        const int section    = rec ? game::Skill_ReadI32(rec, game::skillinfo::kOffSection)   : 0;
        std::snprintf(buf, sizeof(buf), "Prérequis : arme %d, branche %d, section %d",
                      reqWeapon, reqBranch, section);
        ctx.Text(buf, candPanel.x + 4, detailY + 32, kColTextDim);
        const char* stateTxt = (state == NodeState::Learned) ? "Deja apprise"
                              : (state == NodeState::Available) ? "Apprenable maintenant"
                              : "Verrouille (niveau insuffisant)";
        const D3DCOLOR stateCol = (state == NodeState::Learned) ? kColSuccess
                                 : (state == NodeState::Available) ? kColNodeBorderAvailable
                                 : kColError;
        ctx.Text(stateTxt, candPanel.x + 4, detailY + 46, stateCol);
    }

    std::snprintf(buf, sizeof(buf), "Page %d / %d", page_ + 1, maxPage + 1);
    ctx.Text(buf, candPanel.x + 4, candPanel.y + candPanel.h - kCandBtnH - 14, kColTextDim);
    ctx.Text("< Préc", prevBtn.x + 6, prevBtn.y + 2, kColText);
    ctx.Text("Suiv >", nextBtn.x + 6, nextBtn.y + 2, kColText);

    // Pied de fenêtre : statut de la dernière action, ou instruction par défaut.
    if (!statusText_.empty()) {
        ctx.Text(statusText_.c_str(), panel.x + 10, panel.y + panel.h - kFooterH + 6,
                 statusIsError_ ? kColError : kColSuccess);
    } else {
        ctx.Text("Sélectionnez une compétence disponible puis cliquez un emplacement libre.",
                 panel.x + 10, panel.y + panel.h - kFooterH + 6, kColTextDim);
    }
    if (selectedSkillId_ != 0) {
        std::snprintf(buf, sizeof(buf), "Compétence en attente d'apprentissage : #%u", selectedSkillId_);
        ctx.Text(buf, panel.x + 10, panel.y + panel.h - kFooterH + 20, kColTextDim);
    }
}

} // namespace ts2::ui

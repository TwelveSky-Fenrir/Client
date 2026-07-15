// UI/SocialWindow.cpp — implémentation de la fenêtre Social (amis/liste noire + succès).
#include "UI/SocialWindow.h"
#include "UI/PanelSkin.h"
#include "Game/GameState.h" // game::g_World.allianceRoster.guildName (contexte guilde réel)

#include <cstdio>

namespace ts2::ui {

namespace {
// Fond de panneau réel (best effort) : gabarit large (702,488) du dossier atlas
// UI G03_GDATA/D01_GIMAGE2D/001 — candidat NON CONFIRMÉ par IDA, choisi par
// proximité de ratio avec le panneau Social (560x400, ratio quasi identique ;
// cf. méthodologie détaillée dans UI/PanelSkin.h). Indice distinct de celui
// utilisé par SkillTreeWindow (même cluster de tailles, fichiers différents).
// Repli automatique sur kColPanelBg si absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01491.IMG");
} // namespace

SocialWindow::SocialWindow() {
    statusText_ = "";
}

void SocialWindow::Open() {
    Dialog::Open();
    // AutoPlay_LoadFriendList/EnemyList (EA 0x45d730/0x45daf0) : rechargement disque
    // à l'ouverture, fidèle au comportement AutoPlay (fichier absent -> liste vidée).
    social_.LoadAll();
    hasSelection_ = false;
    selectedName_.clear();
    nameInput_.clear();
    statusText_.clear();
    armClose_ = armTabFriends_ = armTabAch_ = false;
    armAddFriend_ = armAddBlacklist_ = armRemove_ = false;
}

void SocialWindow::Close() {
    Dialog::Close();
    armClose_ = armTabFriends_ = armTabAch_ = false;
    armAddFriend_ = armAddBlacklist_ = armRemove_ = false;
}

// ---------------------------------------------------------------------------
// Géométrie
// ---------------------------------------------------------------------------
SocialWindow::Layout SocialWindow::ComputeLayout(int screenW, int screenH) const {
    Layout lo;
    lo.panel.x = (screenW - kPanelW) / 2;
    lo.panel.y = (screenH - kPanelH) / 2;
    lo.panel.w = kPanelW;
    lo.panel.h = kPanelH;

    lo.closeBtn = { lo.panel.x + lo.panel.w - 28, lo.panel.y + 8, 20, 20 };

    const int tabsY = lo.panel.y + 34;
    lo.tabFriends      = { lo.panel.x + 16, tabsY, 130, 24 };
    lo.tabAchievements = { lo.panel.x + 16 + 130 + 8, tabsY, 130, 24 };

    const int listY = tabsY + 24 + 12;
    lo.friendList   = { lo.panel.x + 16, listY, kListW, kListH };
    lo.blacklistList= { lo.panel.x + 16 + kListW + 20, listY, kListW, kListH };

    const int inputY = listY + kListH + 14;
    lo.nameInputBox   = { lo.panel.x + 16, inputY, 180, 22 };
    lo.btnAddFriend   = { lo.nameInputBox.x + lo.nameInputBox.w + 8, inputY, 90, 22 };
    lo.btnAddBlacklist= { lo.btnAddFriend.x + lo.btnAddFriend.w + 8, inputY, 112, 22 };
    lo.btnRemove      = { lo.btnAddBlacklist.x + lo.btnAddBlacklist.w + 8, inputY, 90, 22 };

    // Grille succès : même zone verticale que les deux listes (listY, hauteur kListH
    // + place pour la ligne de saisie non utilisée dans cet onglet), largeur pleine.
    lo.achGrid = { lo.panel.x + 16, listY, kPanelW - 32, kListH + 14 + 22 };

    return lo;
}

SocialWindow::Rect SocialWindow::RowRect(const Rect& listArea, int index) {
    return Rect{ listArea.x + 2, listArea.y + kListHeaderH + index * kRowH,
                 listArea.w - 4, kRowH - 1 };
}

SocialWindow::Rect SocialWindow::AchCellRect(const Rect& gridArea, int index) {
    constexpr int kCols = 4;
    constexpr int kGap  = 6;
    const int cellW = (gridArea.w - (kCols - 1) * kGap) / kCols;
    const int cellH = 34;
    const int col = index % kCols;
    const int row = index / kCols;
    return Rect{ gridArea.x + col * (cellW + kGap),
                 gridArea.y + kListHeaderH + row * (cellH + kGap),
                 cellW, cellH };
}

// ---------------------------------------------------------------------------
// Logique amis / liste noire
// ---------------------------------------------------------------------------
game::SocialNameList* SocialWindow::FindListContaining(const std::string& name, bool& outIsBlacklist) {
    if (social_.friends.Contains(name)) { outIsBlacklist = false; return &social_.friends; }
    if (social_.blacklist.Contains(name)) { outIsBlacklist = true; return &social_.blacklist; }
    return nullptr;
}

void SocialWindow::SetStatus(const std::string& s, D3DCOLOR c) {
    statusText_ = s;
    statusColor_ = c;
}

void SocialWindow::TryAdd(bool toBlacklist) {
    if (nameInput_.empty()) {
        SetStatus("Entrez un nom avant d'ajouter.", kColError);
        return;
    }
    // AutoPlay_OnMouseUpNameList (EA 0x45b000) : capacité pleine testée AVANT
    // présence dans l'autre liste — reproduit tel quel par AddFriend/AddToBlacklist.
    const game::SocialListOp op = toBlacklist ? social_.AddToBlacklist(nameInput_)
                                               : social_.AddFriend(nameInput_);
    switch (op) {
        case game::SocialListOp::Added:
            SetStatus("« " + nameInput_ + " » ajouté à " +
                       (toBlacklist ? std::string("la liste noire.") : std::string("la liste d'amis.")),
                       kColSuccess);
            nameInput_.clear();
            break;
        case game::SocialListOp::AlreadyListed:
            SetStatus("« " + nameInput_ + " » figure déjà dans une des deux listes.", kColError);
            break;
        case game::SocialListOp::ListFull:
            SetStatus("Liste pleine (48 noms max).", kColError);
            break;
    }
}

void SocialWindow::TryRemoveSelected() {
    if (!hasSelection_) {
        SetStatus("Sélectionnez d'abord un nom dans une des deux listes.", kColError);
        return;
    }
    // Revalide l'appartenance courante (au cas où la liste aurait changé entre la
    // sélection et le clic sur « Retirer ») plutôt que de se fier aveuglément au
    // drapeau selectedIsBlacklist_ mémorisé au moment du clic.
    bool isBlacklist = selectedIsBlacklist_;
    if (!FindListContaining(selectedName_, isBlacklist)) {
        SetStatus("Retrait impossible (nom introuvable).", kColError);
        hasSelection_ = false;
        return;
    }
    const bool ok = isBlacklist ? social_.RemoveFromBlacklist(selectedName_)
                                 : social_.RemoveFriend(selectedName_);
    if (ok) {
        SetStatus("« " + selectedName_ + " » retiré.", kColSuccess);
        hasSelection_ = false;
        selectedName_.clear();
    } else {
        SetStatus("Retrait impossible (nom introuvable).", kColError);
    }
}

// ---------------------------------------------------------------------------
// Souris
// ---------------------------------------------------------------------------
bool SocialWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    const Layout lo = ComputeLayout(lastScreenW_, lastScreenH_);

    if (!lo.panel.Contains(x, y))
        return false; // hors du panneau : laisse passer (pas de blocage du monde 3D)

    armClose_ = lo.closeBtn.Contains(x, y);
    armTabFriends_ = lo.tabFriends.Contains(x, y);
    armTabAch_ = lo.tabAchievements.Contains(x, y);

    if (tab_ == Tab::Friends) {
        armAddFriend_ = lo.btnAddFriend.Contains(x, y);
        armAddBlacklist_ = lo.btnAddBlacklist.Contains(x, y);
        armRemove_ = lo.btnRemove.Contains(x, y);

        // Sélection immédiate d'une rangée (clic-enfoncé), comme les cellules
        // d'inventaire : pas besoin d'attendre le relâchement pour une simple
        // sélection en surbrillance. Bornée aux rangées réellement DESSINÉES
        // (maxRowsF/B) pour ne pas créer de zone cliquable fantôme sous la liste.
        const int maxRowsF = (lo.friendList.h - kListHeaderH) / kRowH;
        const int maxRowsB = (lo.blacklistList.h - kListHeaderH) / kRowH;
        for (int i = 0; i < static_cast<int>(social_.friends.names.size()) && i < maxRowsF; ++i) {
            if (RowRect(lo.friendList, i).Contains(x, y)) {
                selectedName_ = social_.friends.names[static_cast<size_t>(i)];
                selectedIsBlacklist_ = false;
                hasSelection_ = true;
                break;
            }
        }
        for (int i = 0; i < static_cast<int>(social_.blacklist.names.size()) && i < maxRowsB; ++i) {
            if (RowRect(lo.blacklistList, i).Contains(x, y)) {
                selectedName_ = social_.blacklist.names[static_cast<size_t>(i)];
                selectedIsBlacklist_ = true;
                hasSelection_ = true;
                break;
            }
        }
    } else {
        armAddFriend_ = armAddBlacklist_ = armRemove_ = false;
    }

    return true; // clic dans le panneau : toujours consommé (modal)
}

bool SocialWindow::OnClick(int x, int y) {
    if (!bOpen_) return false;
    const Layout lo = ComputeLayout(lastScreenW_, lastScreenH_);
    const bool inPanel = lo.panel.Contains(x, y);

    if (armClose_ && lo.closeBtn.Contains(x, y)) { Close(); armClose_ = false; return true; }
    if (armTabFriends_ && lo.tabFriends.Contains(x, y)) tab_ = Tab::Friends;
    if (armTabAch_ && lo.tabAchievements.Contains(x, y)) tab_ = Tab::Achievements;

    if (tab_ == Tab::Friends) {
        if (armAddFriend_ && lo.btnAddFriend.Contains(x, y)) TryAdd(false);
        if (armAddBlacklist_ && lo.btnAddBlacklist.Contains(x, y)) TryAdd(true);
        if (armRemove_ && lo.btnRemove.Contains(x, y)) TryRemoveSelected();
    }

    armClose_ = armTabFriends_ = armTabAch_ = false;
    armAddFriend_ = armAddBlacklist_ = armRemove_ = false;
    return inPanel;
}

// ---------------------------------------------------------------------------
// Clavier — saisie du champ « nom » (VK_0..VK_9 / VK_A..VK_Z / VK_SPACE / VK_BACK).
// ---------------------------------------------------------------------------
bool SocialWindow::OnKey(int vk) {
    if (!bOpen_) return false;

    if (tab_ == Tab::Friends) {
        if (vk == VK_BACK) {
            if (!nameInput_.empty()) nameInput_.pop_back();
            return true;
        }
        if (vk == VK_RETURN) {
            TryAdd(false); // Entrée = ajout rapide côté « amis »
            return true;
        }
        if (vk == VK_TAB) {
            tab_ = Tab::Achievements;
            return true;
        }
        if (vk == VK_SPACE) {
            if (nameInput_.size() < kNameMaxLen) nameInput_.push_back(' ');
            return true;
        }
        if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) {
            if (nameInput_.size() < kNameMaxLen) nameInput_.push_back(static_cast<char>(vk));
            return true;
        }
    } else {
        if (vk == VK_TAB) {
            tab_ = Tab::Friends;
            return true;
        }
    }
    return true; // fenêtre modale ouverte : absorbe le reste du clavier
}

// ---------------------------------------------------------------------------
// Rendu
// ---------------------------------------------------------------------------
void SocialWindow::Render(const UiContext& ctx, int cursorX, int cursorY) {
    if (!bOpen_) return;

    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    const Layout lo = ComputeLayout(ctx.screenW, ctx.screenH);

    RenderChrome(ctx, lo);
    if (tab_ == Tab::Friends)
        RenderFriendsTab(ctx, lo);
    else
        RenderAchievementsTab(ctx, lo);

    (void)cursorX; (void)cursorY;
}

void SocialWindow::RenderChrome(const UiContext& ctx, const Layout& lo) {
    if (ctx.phase == UiPhase::Panels) {
        kPanelBg.Draw(ctx, lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h, kColPanelBg);
        ctx.DrawFrame(lo.panel.x, lo.panel.y, lo.panel.w, lo.panel.h, kColFrame, 1);

        ctx.FillRect(lo.closeBtn.x, lo.closeBtn.y, lo.closeBtn.w, lo.closeBtn.h,
                     armClose_ ? kColHover : kColTabIdle);
        ctx.DrawFrame(lo.closeBtn.x, lo.closeBtn.y, lo.closeBtn.w, lo.closeBtn.h, kColFrame, 1);

        ctx.FillRect(lo.tabFriends.x, lo.tabFriends.y, lo.tabFriends.w, lo.tabFriends.h,
                     tab_ == Tab::Friends ? kColTabActive : kColTabIdle);
        ctx.DrawFrame(lo.tabFriends.x, lo.tabFriends.y, lo.tabFriends.w, lo.tabFriends.h, kColFrame, 1);

        ctx.FillRect(lo.tabAchievements.x, lo.tabAchievements.y, lo.tabAchievements.w, lo.tabAchievements.h,
                     tab_ == Tab::Achievements ? kColTabActive : kColTabIdle);
        ctx.DrawFrame(lo.tabAchievements.x, lo.tabAchievements.y, lo.tabAchievements.w, lo.tabAchievements.h,
                     kColFrame, 1);
    } else { // UiPhase::Text
        ctx.Text("Social", lo.panel.x + 16, lo.panel.y + 10, kColTitle);
        ctx.Text("X", lo.closeBtn.x + 6, lo.closeBtn.y + 3, kColText);
        ctx.Text("Amis", lo.tabFriends.x + (lo.tabFriends.w - ctx.MeasureText("Amis")) / 2,
                 lo.tabFriends.y + 5, kColText);
        ctx.Text("Succes", lo.tabAchievements.x + (lo.tabAchievements.w - ctx.MeasureText("Succes")) / 2,
                 lo.tabAchievements.y + 5, kColText);
    }
}

void SocialWindow::RenderFriendsTab(const UiContext& ctx, const Layout& lo) {
    if (ctx.phase == UiPhase::Panels) {
        ctx.FillRect(lo.friendList.x, lo.friendList.y, lo.friendList.w, lo.friendList.h, 0xFF181820u);
        ctx.DrawFrame(lo.friendList.x, lo.friendList.y, lo.friendList.w, lo.friendList.h, kColFrameDim, 1);
        ctx.FillRect(lo.blacklistList.x, lo.blacklistList.y, lo.blacklistList.w, lo.blacklistList.h, 0xFF181820u);
        ctx.DrawFrame(lo.blacklistList.x, lo.blacklistList.y, lo.blacklistList.w, lo.blacklistList.h, kColFrameDim, 1);

        // Rangées amis
        const int maxRowsF = (lo.friendList.h - kListHeaderH) / kRowH;
        for (int i = 0; i < static_cast<int>(social_.friends.names.size()) && i < maxRowsF; ++i) {
            const Rect r = RowRect(lo.friendList, i);
            const bool sel = hasSelection_ && !selectedIsBlacklist_ && social_.friends.names[static_cast<size_t>(i)] == selectedName_;
            ctx.FillRect(r.x, r.y, r.w, r.h, sel ? kColHover : kColRowFriend);
        }
        // Rangées liste noire
        const int maxRowsB = (lo.blacklistList.h - kListHeaderH) / kRowH;
        for (int i = 0; i < static_cast<int>(social_.blacklist.names.size()) && i < maxRowsB; ++i) {
            const Rect r = RowRect(lo.blacklistList, i);
            const bool sel = hasSelection_ && selectedIsBlacklist_ && social_.blacklist.names[static_cast<size_t>(i)] == selectedName_;
            ctx.FillRect(r.x, r.y, r.w, r.h, sel ? kColHover : kColRowBlack);
        }

        ctx.FillRect(lo.nameInputBox.x, lo.nameInputBox.y, lo.nameInputBox.w, lo.nameInputBox.h, 0xFF10101Au);
        ctx.DrawFrame(lo.nameInputBox.x, lo.nameInputBox.y, lo.nameInputBox.w, lo.nameInputBox.h, kColFrame, 1);

        ctx.FillRect(lo.btnAddFriend.x, lo.btnAddFriend.y, lo.btnAddFriend.w, lo.btnAddFriend.h,
                     armAddFriend_ ? kColHover : kColTabIdle);
        ctx.DrawFrame(lo.btnAddFriend.x, lo.btnAddFriend.y, lo.btnAddFriend.w, lo.btnAddFriend.h, kColFrame, 1);

        ctx.FillRect(lo.btnAddBlacklist.x, lo.btnAddBlacklist.y, lo.btnAddBlacklist.w, lo.btnAddBlacklist.h,
                     armAddBlacklist_ ? kColHover : kColTabIdle);
        ctx.DrawFrame(lo.btnAddBlacklist.x, lo.btnAddBlacklist.y, lo.btnAddBlacklist.w, lo.btnAddBlacklist.h,
                     kColFrame, 1);

        ctx.FillRect(lo.btnRemove.x, lo.btnRemove.y, lo.btnRemove.w, lo.btnRemove.h,
                     armRemove_ ? kColHover : kColTabIdle);
        ctx.DrawFrame(lo.btnRemove.x, lo.btnRemove.y, lo.btnRemove.w, lo.btnRemove.h, kColFrame, 1);
    } else { // UiPhase::Text
        char hdr[64];
        const int maxRowsF = (lo.friendList.h - kListHeaderH) / kRowH;
        const int maxRowsB = (lo.blacklistList.h - kListHeaderH) / kRowH;

        std::snprintf(hdr, sizeof(hdr), "Amis (%zu/%zu)%s",
                      social_.friends.names.size(), game::SocialNameList::kCapacity,
                      static_cast<int>(social_.friends.names.size()) > maxRowsF ? " *" : "");
        ctx.Text(hdr, lo.friendList.x + 4, lo.friendList.y + 2, kColTitle);

        std::snprintf(hdr, sizeof(hdr), "Liste noire (%zu/%zu)%s",
                      social_.blacklist.names.size(), game::SocialNameList::kCapacity,
                      static_cast<int>(social_.blacklist.names.size()) > maxRowsB ? " *" : "");
        ctx.Text(hdr, lo.blacklistList.x + 4, lo.blacklistList.y + 2, kColTitle);

        for (int i = 0; i < static_cast<int>(social_.friends.names.size()) && i < maxRowsF; ++i) {
            const Rect r = RowRect(lo.friendList, i);
            ctx.Text(social_.friends.names[static_cast<size_t>(i)].c_str(), r.x + 3, r.y + 1, kColText);
        }
        for (int i = 0; i < static_cast<int>(social_.blacklist.names.size()) && i < maxRowsB; ++i) {
            const Rect r = RowRect(lo.blacklistList, i);
            ctx.Text(social_.blacklist.names[static_cast<size_t>(i)].c_str(), r.x + 3, r.y + 1, kColText);
        }

        // Champ de saisie + caret simple (toujours visible : pas de gestion de focus
        // multi-champ dans cette fenêtre, un seul champ éditable).
        std::string shown = nameInput_ + "_";
        ctx.Text(shown.c_str(), lo.nameInputBox.x + 4, lo.nameInputBox.y + 4, kColText);

        ctx.Text("+ Ami", lo.btnAddFriend.x + (lo.btnAddFriend.w - ctx.MeasureText("+ Ami")) / 2,
                 lo.btnAddFriend.y + 5, kColText);
        ctx.Text("+ Liste noire", lo.btnAddBlacklist.x + (lo.btnAddBlacklist.w - ctx.MeasureText("+ Liste noire")) / 2,
                 lo.btnAddBlacklist.y + 5, kColText);
        ctx.Text("Retirer", lo.btnRemove.x + (lo.btnRemove.w - ctx.MeasureText("Retirer")) / 2,
                 lo.btnRemove.y + 5, kColText);

        if (!statusText_.empty())
            ctx.Text(statusText_.c_str(), lo.panel.x + 16, lo.btnRemove.y + 30, statusColor_);

        // Note d'honnêteté : cette liste est le fichier local AutoPlay (ciblage bot de
        // farm), PAS un roster « ami en ligne » côté serveur — aucun champ de présence
        // n'existe dans la structure d'origine (cf. Game/SocialSystem.h, en-tête §2).
        ctx.Text("Liste locale (AutoPlay) - aucun statut « en ligne » : non prouve par le desassemblage.",
                 lo.panel.x + 16, lo.panel.y + lo.panel.h - 20, kColTextDim);
    }
}

void SocialWindow::RenderAchievementsTab(const UiContext& ctx, const Layout& lo) {
    if (ctx.phase == UiPhase::Panels) {
        ctx.FillRect(lo.achGrid.x, lo.achGrid.y, lo.achGrid.w, lo.achGrid.h, 0xFF181820u);
        ctx.DrawFrame(lo.achGrid.x, lo.achGrid.y, lo.achGrid.w, lo.achGrid.h, kColFrameDim, 1);

        for (int i = 0; i < static_cast<int>(game::AchievementState::kFlagCount); ++i) {
            const Rect c = AchCellRect(lo.achGrid, i);
            const bool unlocked = game::g_Achievements.flags[static_cast<size_t>(i)] != 0;
            ctx.FillRect(c.x, c.y, c.w, c.h, unlocked ? kColUnlockBg : kColLockedBg);
            ctx.DrawFrame(c.x, c.y, c.w, c.h, kColFrameDim, 1);
        }
    } else { // UiPhase::Text
        // Contexte guilde RÉEL (game::g_World.allianceRoster.guildName == g_LocalGuildName
        // 0x168740C, cf. Game/GameState.h::AllianceRoster et Docs/
        // TS2_ALLIANCE_PARTY_ROSTER.md §3) — les "succès de tribu" n'ont de sens que pour
        // une guilde active ; affiché honnêtement vide (pas d'invention) tant qu'aucun
        // handler n'a peuplé le nom (hors guilde, ou avant réception du roster).
        const std::string& liveGuildName = game::g_World.allianceRoster.guildName;
        const std::string achHeader = "Succes de tribu (dword_184C218, 24 emplacements) - Guilde : " +
                                       (liveGuildName.empty() ? std::string("(aucune)") : liveGuildName);
        ctx.Text(achHeader.c_str(), lo.achGrid.x + 2, lo.achGrid.y + 2, kColTitle);

        for (int i = 0; i < static_cast<int>(game::AchievementState::kFlagCount); ++i) {
            const Rect c = AchCellRect(lo.achGrid, i);
            const bool unlocked = game::g_Achievements.flags[static_cast<size_t>(i)] != 0;

            char line1[32];
            std::snprintf(line1, sizeof(line1), "Succes #%d", i);
            ctx.Text(line1, c.x + 4, c.y + 4, kColText);

            const char* line2 = unlocked ? "Debloque" : "Verrouille";
            ctx.Text(line2, c.x + 4, c.y + 18, unlocked ? kColSuccess : kColTextDim);
        }

        ctx.Text("Non alimente : Net_OnAchievementDataLoad (opcode 0x98) n'est pas encore "
                 "branche a cette fenetre (TODO(state) dans Net/GameHandlers_BossWorld.cpp).",
                 lo.panel.x + 16, lo.panel.y + lo.panel.h - 20, kColTextDim);
    }
}

} // namespace ts2::ui

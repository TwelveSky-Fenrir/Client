// UI/SocialWindow.cpp — implementation of the Social window (friends/blacklist + achievements).
#include "UI/SocialWindow.h"
#include "UI/PanelSkin.h"
#include "Game/GameState.h" // game::g_World.allianceRoster.guildName (real guild context)

#include <cstdio>

namespace ts2::ui {

namespace {
// Real panel background (best effort): wide template (702,488) from UI atlas
// folder G03_GDATA/D01_GIMAGE2D/001 — candidate NOT CONFIRMED by IDA, chosen by
// ratio proximity to the Social panel (560x400, nearly identical ratio;
// see detailed methodology in UI/PanelSkin.h). Different index than the one
// used by SkillTreeWindow (same size cluster, different files).
// Automatic fallback to kColPanelBg if absent.
const PanelSkin kPanelBg("G03_GDATA\\D01_GIMAGE2D\\001\\001_01491.IMG");
} // namespace

SocialWindow::SocialWindow() {
    statusText_ = "";
}

void SocialWindow::Open() {
    Dialog::Open();
    // AutoPlay_LoadFriendList/EnemyList (EA 0x45d730/0x45daf0): reload from disk
    // on open, faithful to AutoPlay behavior (file absent -> list cleared).
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
// Geometry
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

    // Achievements grid: same vertical zone as the two lists (listY, height kListH
    // + room for the input line unused in this tab), full width.
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
// Friends / blacklist logic
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
    // AutoPlay_OnMouseUpNameList (EA 0x45b000): full-capacity checked BEFORE
    // presence in the other list — reproduced as-is by AddFriend/AddToBlacklist.
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
    // Re-validates current membership (in case the list changed between
    // selection and the "Remove" click) rather than blindly trusting the
    // selectedIsBlacklist_ flag memoized at click time.
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
// Mouse
// ---------------------------------------------------------------------------
bool SocialWindow::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    const Layout lo = ComputeLayout(lastScreenW_, lastScreenH_);

    if (!lo.panel.Contains(x, y))
        return false; // outside the panel: let it pass through (no 3D world blocking)

    armClose_ = lo.closeBtn.Contains(x, y);
    armTabFriends_ = lo.tabFriends.Contains(x, y);
    armTabAch_ = lo.tabAchievements.Contains(x, y);

    if (tab_ == Tab::Friends) {
        armAddFriend_ = lo.btnAddFriend.Contains(x, y);
        armAddBlacklist_ = lo.btnAddBlacklist.Contains(x, y);
        armRemove_ = lo.btnRemove.Contains(x, y);

        // Immediate row selection (mouse-down), like inventory
        // cells: no need to wait for release for a simple highlight
        // selection. Bounded to the rows actually DRAWN
        // (maxRowsF/B) to avoid a phantom clickable area below the list.
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

    return true; // click inside the panel: always consumed (modal)
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
// Keyboard — "name" field input (VK_0..VK_9 / VK_A..VK_Z / VK_SPACE / VK_BACK).
// ---------------------------------------------------------------------------
bool SocialWindow::OnKey(int vk) {
    if (!bOpen_) return false;

    if (tab_ == Tab::Friends) {
        if (vk == VK_BACK) {
            if (!nameInput_.empty()) nameInput_.pop_back();
            return true;
        }
        if (vk == VK_RETURN) {
            TryAdd(false); // Enter = quick-add on the "friends" side
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
    return true; // modal window open: absorbs the rest of the keyboard
}

// ---------------------------------------------------------------------------
// Render
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

        // Friend rows
        const int maxRowsF = (lo.friendList.h - kListHeaderH) / kRowH;
        for (int i = 0; i < static_cast<int>(social_.friends.names.size()) && i < maxRowsF; ++i) {
            const Rect r = RowRect(lo.friendList, i);
            const bool sel = hasSelection_ && !selectedIsBlacklist_ && social_.friends.names[static_cast<size_t>(i)] == selectedName_;
            ctx.FillRect(r.x, r.y, r.w, r.h, sel ? kColHover : kColRowFriend);
        }
        // Blacklist rows
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

        // Input field + simple caret (always visible: no multi-field focus
        // handling in this window, only one editable field).
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

        // Honesty note: this list is the local AutoPlay file (farm bot
        // targeting), NOT a server-side "online friend" roster — no presence field
        // exists in the original structure (see Game/SocialSystem.h, header §2).
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
        // REAL guild context (game::g_World.allianceRoster.guildName == g_LocalGuildName
        // 0x168740C, see Game/GameState.h::AllianceRoster and Docs/
        // TS2_ALLIANCE_PARTY_ROSTER.md §3) — "guild achievements" are only meaningful for
        // an active guild; shown honestly empty (not invented) until a
        // handler has populated the name (no guild, or before the roster is received).
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

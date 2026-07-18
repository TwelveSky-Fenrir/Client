// UI/SocialWindow.h — "Social" window: friends/blacklist (AutoPlay) + guild achievements.
//
// ===========================================================================
// NETWORK AUDIT (Pass 4, wave W6, 2026-07-16) — VERDICT: ZERO EMISSION = FAITHFUL.
// DO NOT "FIX" THIS ABSENCE IN A FUTURE WAVE.
// ===========================================================================
// This window emits NO packet, and that MATCHES the binary (verified by
// full decompilation of AutoPlay_OnMouseUpNameList 0x45B000, the real
// friends/blacklist code — a tab of the AutoPlay window, selector *((_WORD*)this+146):
// 0 = friends, 1 = enemies/blacklist):
//   - "Add" button (x+74, y+303), sprite unk_9664E0, latch checked at EA 0x45B5E2:
//       GetWindowTextA(hWnd, ·, 25) empty          -> message 1949, RETURN
//       !MobDb_FindByName(mITEM, ·)                -> message 1948, RETURN (EA 0x45B63F)
//       AutoPlay_IsNameListed(·)                   -> message 1947, RETURN (EA 0x45B679)
//       capacity >= 0x30 (48)                      -> message 1980 (EA 0x45B80F/0x45B6F1)
//       else List_PushBackNode + AutoPlay_SaveFriendList (EA 0x45B7EC)
//                                / AutoPlay_SaveEnemyList  (EA 0x45B90A)
//   - "Remove" button (x+144, y+303), sprite unk_966608, EA 0x45B974:
//       list walk, List_EraseNode, then Save*List (EA 0x45BC1C / 0x45BE2F).
// NO `Net_Send*` on these paths: list persistence is a DISK WRITE
// (G02_GINFO\011.BIN / 012.BIN), NOT a server sync. The only
// Net_SendOp99 calls in the function (EA 0x45B1A5 arg=1, EA 0x45B272 arg=0) are
// starting/stopping the AutoPlay BOT — unrelated to social features.
// The protocol's "friends" packets (0x7e Net_OnFriendStatusNotice, 0x90
// Net_OnFriendListEvent, 0x79 Net_OnSocialListRemove) are INBOUND ONLY:
// no emitting social UI exists in this client (xrefs on unk_16869C0 /
// 0x1686AC4 / 0x1686BC8: no emitter).
//
// KNOWN RESIDUAL (not addressed by this wave — outside the "network emission"
// mandate, and semantics too ambiguous to guess): the validation `MobDb_FindByName(mITEM, name)`
// (message 1948, anchors 0x45B63F for add and 0x45B9D8 for remove) is NOT
// ported in SocialWindow::TryAdd/TryRemoveSelected. The binary tests a PLAYER
// name against the `mITEM` database (0x8E71EC) — the exact intent of this test remains to
// be clarified before porting. Capacity (48) and "already listed" are, however, already
// modeled (Game/SocialSystem.h: SocialListOp::ListFull / AlreadyListed).
//
// POSITION AUDIT (RE-VERIFIED VIA FRESH DECOMPILATION, 2026-07-14): the screen
// CENTERING below (`(screenW-kPanelW)/2, (screenH-kPanelH)/2`) follows the
// same EA-proven pattern as UI_ClanWin_Draw 0x5DA210/UI_MemberSelectWnd_Render
// 0x667860 (`nWidth/2 - w/2`, CURRENT resolution, no fixed scale factor) —
// NO coordinate/scale bug found in the centering formula itself.
// However, decompiling AutoPlay_OnMouseUpNameList 0x45B000 (the REAL
// friends/blacklist add/remove code) shows this UI is NOT an independent
// dialog in the binary: it is a TAB embedded in the AutoPlay window
// (farm bot, UI/AutoPlayWindow.h) at FIXED offsets relative to THIS
// window's anchor (`*((_DWORD*)this+7)`=x, `*((_DWORD*)this+6)`=y, not a
// separately-centered "Social" window): friend editing at (x+74,y+303), blacklist
// editing at (x+144,y+303), tabs at (x+26,y+103)/(x+153,y+103). SocialWindow (this
// file) is therefore a 560x400 window independently centered on screen — a
// pragmatic, internally-coherent REINVENTION, but WITHOUT a 1:1 anchor position
// matching the binary (which has no standalone "Social" window). "Achievements" tab
// (AchievementState): no render UI identified in the binary (only
// Net_OnAchievementDataLoad 0x4AC920 exists on the network side) — same caveats
// already documented below. Fix applied: none (the centering is already
// EA-faithful); only this audit note is new.
//
// Modal dialog (ts2::ui::Dialog, see UI/UIManager.h) wired to Game/SocialSystem.h.
// Two clickable tabs at the top of the panel:
//   - "Friends": the TWO local lists proven by disassembly
//                (AutoPlaySocialLists::friends / blacklist, EA 0x45d730-0x45e1a3),
//                with add/remove via the AddFriend/AddToBlacklist/Remove* API
//                already written in SocialSystem.h. NO "online" field exists
//                in this structure (see the honesty warning at the top of
//                SocialSystem.h): not invented, explicitly indicated
//                on screen instead.
//   - "Achievements": AchievementState::flags (24 int32 slots, dword_184C218),
//                read directly from game::g_Achievements (Game/SocialSystem.h),
//                populated by Net_OnAchievementDataLoad (opcode 0x98,
//                Net/GameHandlers_BossWorld.cpp) — a single shared state, no more
//                local copy. As long as no data has been received from the server,
//                all achievements show "locked", an honest state rather than
//                an invented one.
//
// Name input WITHOUT a Win32 EDIT nor Widgets::EditBox (the Dialog contract only exposes
// OnKey(int vk), no OnChar): alphanumeric virtual keys are typed directly
// (VK_0..VK_9 = '0'..'9', VK_A..VK_Z = 'A'..'Z' in Win32, VK_SPACE),
// mirroring a raw QWERTY keyboard — no lowercase or accents, an accepted
// limitation of this UI rewrite (the original binary uses a real native EDIT control).
#pragma once
#include "UI/UIManager.h"
#include "Game/SocialSystem.h"

#include <string>

namespace ts2::ui {

class SocialWindow : public Dialog {
public:
    SocialWindow();

    // Open: reloads the friends/blacklist lists from disk
    // (AutoPlaySocialLists::LoadAll -> G02_GINFO\011.BIN / 012.BIN), faithful to
    // the reload performed by AutoPlay before displaying its name list.
    void Open() override;
    void Close() override;

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;

    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // Access to shared state (game::g_Achievements, populated by the network handler
    // opcode 0x98) — no more local copy, see file header comment.
    game::AchievementState&       Achievements()       { return game::g_Achievements; }
    const game::AchievementState& Achievements() const { return game::g_Achievements; }
    game::AutoPlaySocialLists&    Social()             { return social_; }

private:
    enum class Tab { Friends = 0, Achievements = 1 };

    struct Rect {
        int x = 0, y = 0, w = 0, h = 0;
        bool Contains(int px, int py) const {
            return px >= x && px < x + w && py >= y && py < y + h;
        }
    };

    // Geometry recomputed on every Render (variable screen dimensions) AND cached
    // (lastScreenW_/H_) so OnMouseDown/OnClick — routed outside Render,
    // with only screen (x,y) as argument — get the same layout back. Same
    // idiom as MsgBoxDialog::Layout in UI/UIManager.cpp.
    struct Layout {
        Rect panel;
        Rect closeBtn;
        Rect tabFriends, tabAchievements;
        Rect friendList, blacklistList;   // list body below the header (excludes title)
        Rect nameInputBox;
        Rect btnAddFriend, btnAddBlacklist, btnRemove;
        Rect achGrid;                      // achievements grid area (cells computed separately)
    };
    Layout ComputeLayout(int screenW, int screenH) const;

    // Row n (0-based) of a name list (below the column header).
    static Rect RowRect(const Rect& listArea, int index);
    // Cell n (0-based) of the achievements grid (4 columns).
    static Rect AchCellRect(const Rect& gridArea, int index);

    void RenderChrome(const UiContext& ctx, const Layout& lo);
    void RenderFriendsTab(const UiContext& ctx, const Layout& lo);
    void RenderAchievementsTab(const UiContext& ctx, const Layout& lo);

    // Returns a pointer to the list (friends or blacklist) containing `name`,
    // nullptr if absent from both. Used by the generic "Remove" button.
    game::SocialNameList* FindListContaining(const std::string& name, bool& outIsBlacklist);

    void SetStatus(const std::string& s, D3DCOLOR c);
    void TryAdd(bool toBlacklist);
    void TryRemoveSelected();

    // --- Data ---
    game::AutoPlaySocialLists social_;       // friends/blacklist lists (loaded at Open())
    // (no more local achievements copy: Achievements() reads shared game::g_Achievements)

    Tab  tab_ = Tab::Friends;

    std::string selectedName_;          // selected name (click on a row)
    bool        selectedIsBlacklist_ = false;
    bool        hasSelection_ = false;

    std::string nameInput_;             // "name to add" input field
    static constexpr size_t kNameMaxLen = 24; // 25 bytes/slot - 1 (SocialNameList::kSlotBytes)

    std::string statusText_;
    D3DCOLOR    statusColor_ = 0xFFC8C8C8u;

    // Mouse latches (armed on press, validated on release WITHIN the same element —
    // btnPressed[] pattern from the original dialogs, see UI_MsgBox_OnLButtonDown/Up).
    bool armClose_ = false, armTabFriends_ = false, armTabAch_ = false;
    bool armAddFriend_ = false, armAddBlacklist_ = false, armRemove_ = false;

    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // --- Layout constants ---
    static constexpr int kPanelW = 560;
    static constexpr int kPanelH = 400;
    static constexpr int kListW  = 250;
    static constexpr int kListH  = 200;
    static constexpr int kListHeaderH = 18;
    static constexpr int kRowH        = 16;

    // --- Palette (D3DCOLOR = 0xAARRGGBB) ---
    static constexpr D3DCOLOR kColPanelBg   = 0xE0202028u;
    static constexpr D3DCOLOR kColFrame     = 0xFF808080u;
    static constexpr D3DCOLOR kColFrameDim  = 0xFF505050u;
    static constexpr D3DCOLOR kColText      = 0xFFFFFFFFu;
    static constexpr D3DCOLOR kColTextDim   = 0xFFAAAAAAu;
    static constexpr D3DCOLOR kColTitle     = 0xFFFFDD66u;
    static constexpr D3DCOLOR kColHover     = 0xFF4060A0u;
    static constexpr D3DCOLOR kColTabActive = 0xFF4060A0u;
    static constexpr D3DCOLOR kColTabIdle   = 0xFF303038u;
    static constexpr D3DCOLOR kColError     = 0xFFFF6060u;
    static constexpr D3DCOLOR kColSuccess   = 0xFF60FF60u;
    static constexpr D3DCOLOR kColRowFriend = 0xFF283828u; // friends list row background
    static constexpr D3DCOLOR kColRowBlack  = 0xFF382828u; // blacklist row background
    static constexpr D3DCOLOR kColLockedBg  = 0xFF303038u;
    static constexpr D3DCOLOR kColUnlockBg  = 0xFF2E4A2Eu;
};

} // namespace ts2::ui

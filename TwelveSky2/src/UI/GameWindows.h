// UI/GameWindows.h — integration: owns and registers the game windows
// (Warehouse/Guild/Quest/Skills/Options/Social/AutoPlay/Vendor/Party/
// Trade/Character/Inventory/NPC Dialog) with the UIManager, and routes
// keyboard shortcuts.
//
// Written during integration (not by a generation agent): wires the 14
// ts2::ui::*Window classes (already written by the ts2-ui-giga-wave/2 workflow) onto
// the UIManager framework (already written, Docs/TS2_CLIENT_SHELL.md §2.2) and onto
// the Game/* systems that feed them.
//
// InventoryWindow (8x8 bag + equipment, cf. UI/InventoryWindow.h) does NOT inherit
// from Dialog: it's a standalone class (its own Init(renderer,font), argument-less
// Render(), its own SpriteBatch) written independently of the UIManager
// framework. `InventoryDialogAdapter` below plugs it into the UIManager registry
// (mouse routing "first consumer wins" + reverse-order rendering, like the
// 13 others) WITHOUT modifying its public interface.
//
// NpcDialogWindow (cf. UI/NpcDialogWindow.h) was written but was NOT REGISTERED
// ANYWHERE (no instance, no UIManager::Register, no Open() call anywhere
// in the repo) before the "PanelSkin device chain" audit (2026-07-14): its render
// chain was therefore broken at the very first link (Render() never invoked,
// even with a valid device). Fixed here by owning/registering it EXACTLY
// like the other 10 PanelSkin windows, so it receives a valid ctx.renderer
// as soon as a future caller invokes npcDialog_.Open(...). The real call site
// (click on an NPC -> Npc_Interact 0x53A660 -> opens THIS window, cf.
// banner of UI/NpcDialogWindow.cpp::HandleAcceptClick) lives on the 3D mouse
// routing side, out of scope for this mission (Scene/SceneManager.cpp, deliberately
// not modified): remains an explicit integration TODO, not invented logic.
#pragma once
#include "UI/UIManager.h"
#include "UI/WarehouseWindow.h"
#include "UI/GuildWindow.h"
#include "UI/QuestTrackerWindow.h"
#include "UI/NpcDialogWindow.h"
#include "UI/SkillTreeWindow.h"
#include "UI/OptionsWindow.h"
#include "UI/SocialWindow.h"
#include "UI/AutoPlayWindow.h"
#include "UI/VendorShopWindow.h"
#include "UI/PartyWindow.h"
#include "UI/PlayerTradeWindow.h"
#include "UI/CharacterStatsWindow.h"
#include "UI/EnchantWindow.h"
#include "UI/InventoryWindow.h"
#include "UI/ClanContextMenu.h"

#include "Game/AutoPlaySystem.h"
#include "Game/SkillSystem.h"
#include "Game/QuestSystem.h"   // game::g_QuestProgress (synced every frame)

#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Gfx/IconTextureCache.h"

namespace ts2::ui {

// Keyboard shortcuts (Win32 VK) of the manually-driven windows. The "always
// visible" windows (Party, Quest Tracker) open once at Init() and hide
// themselves (logic internal to their Render()) — no dedicated key.
namespace hotkeys {
inline constexpr int kInventory      = 'I'; // "Inventory" (8x8 bag + equipment) — free, checked against
                                             // Scene/SceneManager.cpp (C/K/G/O/N/H/V/T/Y/E already taken)
inline constexpr int kCharacterStats = 'C';
inline constexpr int kSkillTree      = 'K';
inline constexpr int kGuild          = 'G';
inline constexpr int kOptions        = 'O';
inline constexpr int kSocial         = 'N'; // "social network" (F is taken by DIK movement)
inline constexpr int kWarehouse      = 'H'; // "Hangar"/warehouse (normal opening = server packet 0x22)
inline constexpr int kVendor         = 'V'; // (normal opening = server packet 0x87)
inline constexpr int kPlayerTrade    = 'T'; // (normal opening = server packet 0x31)
inline constexpr int kAutoPlay       = 'Y';
inline constexpr int kEnchant        = 'E';
} // namespace hotkeys

// InventoryDialogAdapter — Dialog -> InventoryWindow bridge (cf. file header
// comment). No OnKey: InventoryWindow has no internal shortcut (Escape-type);
// opening/closing goes through GameWindows::HandleHotkey (kInventory),
// like the other 12 windows.
class InventoryDialogAdapter : public Dialog {
public:
    explicit InventoryDialogAdapter(InventoryWindow& w) : win_(w) {}

    void Open()  override { win_.Open();  Dialog::Open();  }
    void Close() override { win_.Close(); Dialog::Close(); }

    bool OnMouseDown(int x, int y) override { return win_.OnMouseDown(x, y); } // cGameHud_OnMouseDown 0x62B080
    bool OnClick(int x, int y)     override { return win_.OnMouseUp(x, y); }   // cGameHud_OnMouseUp   0x62DFA0

    void Render(const UiContext& ctx, int cursorX, int cursorY) override {
        // InventoryWindow::Render() draws panels AND text in a single call
        // (own SpriteBatch + Font, separate from the shared UIManager batch) — only
        // triggered on the Panels sub-pass to avoid double-rendering
        // (UIManager::Render() calls Dialog::Render twice per frame: once in
        // the Panels phase, once in the Text phase).
        if (ctx.phase != UiPhase::Panels) return;
        win_.SetCursorPos(cursorX, cursorY);
        win_.Render();
    }

private:
    InventoryWindow& win_;
};

// GameWindows — owns the 14 windows + their dedicated font/sprite, registers
// them with UIManager (routing/rendering), and converts the keys above
// into Open()/Close() calls. Only relevant in the InGame scene (created/destroyed
// with the HUD).
class GameWindows {
public:
    GameWindows();

    // Current instance (set by Init, cleared by Shutdown; nullptr outside the InGame scene).
    // Mirrors UIManager::Instance() — the binary manipulates global singletons
    // (g_MemberSelectWnd 0x184BE38, g_ClanWin dword_1822938, …), so the global accessor is
    // FAITHFUL, not just a convenience.
    //
    // RATIONALE: the binary's network handlers call the windows DIRECTLY
    // (e.g. Net_OnPartyMemberNameSet 0x4909A0 -> UI_MemberSelectWnd_Open @0x4909F8, with
    // no guard). On the C++ side, Net/ previously included NO UI/ at all: this access point
    // is the FIRST Net -> UI edge in the repo, introduced to wire this precise anchor
    // (cf. Net/GameHandlers_PartyGuild.cpp, handler 0x3e). Without it, PartyWindow's
    // Op57/Op58 emissions stay dead code (cf. UI/PartyWindow.h:71-83).
    static GameWindows* Instance();

    // Creates dedicated sprite/font + initializes UIManager (renderer device) and
    // registers all windows. `notifyHwnd` = window for ScreenToClient.
    bool Init(gfx::Renderer& renderer, void* notifyHwnd, int screenW, int screenH);
    void Shutdown();

    void OnDeviceLost();
    void OnDeviceReset();
    void SetScreenSize(int w, int h);

    // Rendering (delegates to UIManager::Instance().Render(), which iterates
    // registered dialogs in reverse order — popups on top).
    void Render();

    // cSceneMgr_Update 0x517BF0 (case 6) calls AutoPlay_Update(g_AutoPlayBot)
    // RIGHT AFTER Scene_InGameUpdate on EVERY InGame frame — confirmed by
    // direct decompilation (Game/InGameTickFlow.h). To be called from
    // SceneManager::Update, case InGame, after the world tick.
    void UpdateAutoPlay(float dt) { autoPlaySystem_.Update(dt); }

    // Routes a keyboard shortcut to the concerned window's Open()/Close().
    // Returns true if consumed (to be called BEFORE the caller's UIManager::RouteKey,
    // so global keys don't leak into an open dialog).
    bool HandleHotkey(int vk);

    // Direct access (to wire server packets: contextual opening
    // of Warehouse/Vendor/Trade on receipt of a network packet — TODO
    // for later integration; these windows remain accessible via keyboard
    // in the meantime).
    WarehouseWindow&       Warehouse()    { return warehouse_; }
    GuildWindow&            Guild()        { return guild_; }
    QuestTrackerWindow&     QuestTracker() { return questTracker_; }
    SkillTreeWindow&        SkillTree()    { return skillTree_; }
    OptionsWindow&           Options()      { return options_; }
    SocialWindow&            Social()       { return social_; }
    AutoPlayWindow&          AutoPlay()     { return autoPlayWindow_; }
    game::AutoPlaySystem&    AutoPlaySys()  { return autoPlaySystem_; }
    VendorShopWindow&        Vendor()       { return vendor_; }
    PartyWindow&             Party()        { return party_; }
    PlayerTradeWindow&       PlayerTrade()  { return trade_; }
    CharacterStatsWindow&    CharStats()    { return charStats_; }
    EnchantWindow&           Enchant()      { return enchant_; }
    InventoryWindow&         Inventory()    { return inventory_; }
    // Cf. header banner: registered in UIManager (sound device chain) but
    // no caller yet triggers Open(npc, questCtx, questProgress, interaction)
    // in this port — accessible here for the future NPC-click wiring.
    NpcDialogWindow&         NpcDialog()    { return npcDialog_; }
    // Player context menu (UI_ClanWin dword_1822938, gap SGP-1). Registered in
    // UIManager below => rendered AND routed as soon as a caller invokes OpenForPlayer().
    // The binary's 2 openers depend on entity picking (G-PICK-05, absent):
    // cf. "WIRING" banner of UI/ClanContextMenu.h.
    ClanContextMenu&         ClanMenu()     { return clanMenu_; }

private:
    // --- PromptState -> MsgBoxDialog bridge (gaps SCN-01 + SCN-02) ---
    // Called at the top of Render(). game::g_Client.prompt had 12 WRITERS and ZERO READERS:
    // the in-game modal notice was never displayed (SCN-01), and since the yes/no
    // box was never drawn or clickable, Op45/49/55/61/67/74 were only ever emitted by the
    // handlers' automatic-refusal branch (value 2) — the player could refuse a
    // party/guild invite but never accept one (SCN-02). This bridge is the missing READER.
    void SyncPrompt();

    // Called by the box's callback the moment the user decides (OK/Cancel),
    // to disarm the mirror state WITHIN the same turn. Without it, a new prompt of the SAME
    // type arriving before the next Render would be swallowed (the rising edge only
    // triggers on `!promptShown_ || promptType_ != type`).
    void OnPromptDismissed() { promptShown_ = false; promptType_ = 0; }

    bool promptShown_ = false; // is a box open for the current prompt?
    int  promptType_  = 0;     // dword_1822450 (type of the reflected prompt)
    gfx::SpriteBatch sprite_;
    gfx::Font        font_;
    bool             inited_ = false;

    // GPU icon cache SHARED between Inventory/Warehouse/Enchant/Vendor ("texture
    // cache memory audit" mission, 2026-07-14, cf. Gfx/IconTextureCache.h):
    // avoids decoding/uploading the same .IMG icon to VRAM once per window that
    // displays it. Injected into the 4 windows via SetIconCache() at Init() below.
    gfx::IconTextureCache sharedIconCache_;

    // --- Game/* systems owned here (no natural home elsewhere) ---
    game::AutoPlaySystem  autoPlaySystem_;
    game::SkillBar        skillBar_;
    // Per-skill level-threshold table: NO local member here — bound
    // directly to game::GetSkillLevelTable() (Game/SkillCombat.h) at Bind(),
    // which is the faithful and complete transcription (350 cases) of
    // Motion_InitFrameTable 0x4F1380, now available (2026-07-14). An
    // empty local member was used previously for lack of having identified this
    // source; fixed so SkillTreeWindow no longer shows required levels
    // as zero.

    // --- Windows ---
    WarehouseWindow       warehouse_;
    GuildWindow            guild_;
    QuestTrackerWindow     questTracker_;
    SkillTreeWindow        skillTree_;
    OptionsWindow           options_;
    SocialWindow            social_;
    AutoPlayWindow           autoPlayWindow_{autoPlaySystem_};
    VendorShopWindow         vendor_;
    PartyWindow               party_;
    PlayerTradeWindow          trade_;
    CharacterStatsWindow        charStats_;
    EnchantWindow                 enchant_;
    InventoryWindow                 inventory_;
    InventoryDialogAdapter            inventoryAdapter_{inventory_};
    NpcDialogWindow                     npcDialog_;
    ClanContextMenu                      clanMenu_;   // UI_ClanWin dword_1822938 (SGP-1)
};

} // namespace ts2::ui

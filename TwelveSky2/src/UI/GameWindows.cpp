// UI/GameWindows.cpp — implementation of the game windows integration.
//
// Include order: Net/ FIRST (Net/NetClient.h pulls <winsock2.h> before <windows.h>,
// which UI/GameWindows.h pulls transitively via UIManager.h -> <d3d9.h>) — same
// convention as UI/PartyWindow.cpp:6-10 / UI/ClanContextMenu.cpp.
#include "Net/SendPackets.h"    // Net_SendOp45/49/55/61/67/74 (prompt replies)
#include "Net/NetClient.h"      // net::GlobalNetClient()
#include "UI/GameWindows.h"
#include "Game/GameState.h"
#include "Game/ClientRuntime.h" // game::g_Client.prompt (PromptState)
#include "Core/Log.h"
#include <windows.h>

namespace ts2::ui {

namespace {
// Current instance (cf. GameWindows::Instance() in GameWindows.h). Only one
// GameWindows lives at a time (created/destroyed with the InGame scene).
GameWindows* g_GameWindowsInstance = nullptr;
} // namespace

GameWindows* GameWindows::Instance() { return g_GameWindowsInstance; }

GameWindows::GameWindows() = default;

bool GameWindows::Init(gfx::Renderer& renderer, void* notifyHwnd, int screenW, int screenH) {
    IDirect3DDevice9* dev = renderer.Device();
    if (!dev) { TS2_WARN("GameWindows::Init : device nul."); return false; }

    if (!sprite_.Create(dev))
        TS2_WARN("GameWindows : SpriteBatch::Create a echoue.");
    if (!font_.Init(dev, screenW, screenH))
        TS2_WARN("GameWindows : Font::Init a echoue.");

    if (!UIManager::Instance().Init(&renderer, &sprite_, &font_,
                                    static_cast<HWND>(notifyHwnd), screenW, screenH)) {
        TS2_ERR("GameWindows : UIManager::Init a echoue.");
        return false;
    }

    // InventoryWindow manages its own D3D9 device (dedicated SpriteBatch, cf.
    // header comment of UI/GameWindows.h): Init() kept separate from UIManager::Init()
    // above, non-fatal on failure (the window just stays invisible/non-
    // functional, like the other Create/Init failures in this function).
    if (!inventory_.Init(renderer, &font_))
        TS2_WARN("GameWindows : InventoryWindow::Init a echoue.");

    // SHARED icon cache (cf. header banner of GameWindows.h + Gfx/IconTextureCache.h):
    // a single instance injected into the 4 windows that display item icons,
    // so each .IMG icon is loaded/uploaded to VRAM only ONCE per session.
    inventory_.SetIconCache(&sharedIconCache_);
    warehouse_.SetIconCache(&sharedIconCache_);
    enchant_.SetIconCache(&sharedIconCache_);
    vendor_.SetIconCache(&sharedIconCache_);

    // Registration (order = routing priority; the most "on-top" modal popups
    // must come first). MsgBox (internal to UIManager) is already first. Contextual
    // popups are placed before the background panels.
    auto& mgr = UIManager::Instance();
    // Player context menu (UI_ClanWin dword_1822938, gap SGP-1): contextual popup
    // registered FIRST among GameWindows' windows (right after UIManager's internal
    // MsgBox) — the binary opens it via UI_CloseAllDialogs(dword_1821D4C, 1) @0x5D8E50,
    // i.e. on top of everything else. This registration is enough to render AND
    // route it; only its OPENER is missing (entity picking, G-PICK-05 — cf. ClanContextMenu.h).
    mgr.Register(&clanMenu_);
    mgr.Register(&inventoryAdapter_);
    mgr.Register(&charStats_);
    mgr.Register(&skillTree_);
    mgr.Register(&guild_);
    mgr.Register(&options_);
    mgr.Register(&social_);
    mgr.Register(&warehouse_);
    mgr.Register(&vendor_);
    mgr.Register(&trade_);
    mgr.Register(&enchant_);
    mgr.Register(&autoPlayWindow_);
    // "Always visible" panels (auto-hidden by their own internal logic)
    // at the end of the list = rendered first (background), routed last.
    mgr.Register(&questTracker_);
    mgr.Register(&party_);
    // NpcDialogWindow (cf. GameWindows.h banner): contextual popup like the
    // other dialogs above (closed by default, bOpen_=false until something
    // calls npcDialog_.Open()) — placed with the modal popups, not the
    // "always visible" panels.
    mgr.Register(&npcDialog_);

    // Bind the skill window to the available runtime data (SKILL_INFO table
    // loaded by LoadGameDatabases, likewise ITEM_INFO; level-threshold table =
    // game::GetSkillLevelTable(), a faithful and complete transcription of the 350
    // cases of Motion_InitFrameTable 0x4F1380 (Game/SkillCombat.h) — replaces the
    // former empty local table that made every node show up "Locked" with
    // required level 0..0).
    skillTree_.Bind(game::g_World.db.skill, game::g_World.db.item,
                    game::GetSkillLevelTable(), skillBar_, game::g_World.self);

    // Always-visible panels: opened once, hide themselves (Render() checks its
    // own display condition — non-empty group / active quest).
    questTracker_.Open();
    party_.Open();

    inited_ = true;
    g_GameWindowsInstance = this; // publishes the global accessor (cf. GameWindows::Instance())
    TS2_LOG("GameWindows initialise (%dx%d, 15 fenetres enregistrees).", screenW, screenH);
    return true;
}

void GameWindows::Shutdown() {
    if (!inited_) return;
    if (g_GameWindowsInstance == this) g_GameWindowsInstance = nullptr;
    inventory_.Shutdown(); // device/SpriteBatch owned by InventoryWindow (cf. Init above)
    UIManager::Instance().Shutdown();
    font_.Shutdown();
    sprite_.Destroy();
    sharedIconCache_.Clear(); // explicitly releases resident icon GPU textures
    inited_ = false;
}

void GameWindows::OnDeviceLost() {
    sprite_.OnLostDevice();
    font_.OnDeviceLost();
    inventory_.OnDeviceLost(); // ID3DXSprite owned by InventoryWindow, outside the shared UIManager batch
}

void GameWindows::OnDeviceReset() {
    sprite_.OnResetDevice();
    font_.OnDeviceReset();
    inventory_.OnDeviceReset();
}

void GameWindows::SetScreenSize(int w, int h) {
    UIManager::Instance().SetScreenSize(w, h);
    font_.SetClipRect(w, h);
    inventory_.SetScreenSize(w, h);
}

// PromptState -> MsgBoxDialog bridge — gaps SCN-01 (no reader) + SCN-02 (never accepted)
namespace {

// Prompt types that expect a NETWORK REPLY, and their builder.
// Table PROVEN by the 2 jump tables of UI_MsgBox_OnLButtonUp 0x5C0A90 (identical cases
// on both sides): 8 -> Op45, 9 -> Op49, 10 -> Op67, 14 -> Op74, 19 -> Op55, 20 -> Op61.
// Corresponding openers: 8 = Pkt_PartyInvitePrompt 0x48FA70, 9 = Pkt_AllyInvitePrompt
// 0x48FFB0, 19 = Dlg19 0x4906F0, 20 = Dlg20 0x490AF0, 10 = Dlg10 0x490EE0, 14 = Dlg14 0x491C10.
bool IsNetConfirmType(int type) {
    return type == 8 || type == 9 || type == 10 || type == 14 || type == 19 || type == 20;
}

// Emits the reply to prompt `type` with value `value`.
void SendPromptReply(int type, int8_t value) {
    net::NetClient* nc = net::GlobalNetClient();
    if (!nc) return; // no session: nothing sent (the binary doesn't test this, but
                     // g_NetClient is always alive there; here the pointer can be null).
    switch (type) {
        case 8:  net::Net_SendOp45(*nc, value); break; // 0x5C1080 (OK) / 0x5C2DDE (Cancel)
        case 9:  net::Net_SendOp49(*nc, value); break; // 0x5C1096 / 0x5C2DF4
        case 10: net::Net_SendOp67(*nc, value); break; // 0x5C10AC / 0x5C2E0A
        case 14: net::Net_SendOp74(*nc, value); break; // 0x5C110C / 0x5C2E1D
        case 19: net::Net_SendOp55(*nc, value); break; // 0x5C11BD / 0x5C2E30
        case 20: net::Net_SendOp61(*nc, value); break; // 0x5C11D3 / 0x5C2E43
        default: break;                                // non-network type (2 = NoticeDlg)
    }
}

} // namespace

// SCN-01 + SCN-02. Reflects game::g_Client.prompt into the shared MsgBoxDialog
// (UIManager::MsgBox(), registered FIRST by UIManager::Init -> modal and routed first).
//
// VALUE POLARITY — THE GAPS DOSSIER HAD IT BACKWARD; REFUTED BY DISASSEMBLY
// The dossier (and Net/Opcodes.h:232/236, wrongly marked "CONFIRMED") claim
// "1 = accept / 2 = refuse". This is FALSE. Ground truth (UI_MsgBox_OnLButtonUp 0x5C0A90):
//   - btn1 latch [+0Ch] -> sprite unk_8E8FF0 @ (x+0xA5=165, y+0x5A=90) -> jpt_5C0BE5 -> push 0
//   - btn2 latch [+10h] -> sprite unk_8E91AC @ (x+0xF1=241, y+90)      -> jpt_5C2DC3 -> push 1
// btn1 = OK, proven by: UI_NoticeDlg_OnLButtonUp 0x5C03F0 — a SINGLE-button dialog (OK only) —
// hit-tests the SAME sprite unk_8E8FF0 @0x5C048A, at (x+0xCB=203, y+90) = exactly the CENTER
// of the MsgBox's two slots ((165+241)/2 = 203), in the same frame unk_8E8F5C.
// Corroboration: Net_OnConfirmPromptOpen_Dlg10 0x490EE0 contains an auto-ACCEPT (auto-hunt
// bot) that forces dword_1822444 = 1 (= btn1 latch) @0x490FFA then synthesizes a click
// @0x49101F -> it results in Op67(0). Therefore:
//   0 = ACCEPT (OK click) · 1 = REFUSE (Cancel click) · 2 = AUTOMATIC refusal (filter off).
// => The `Net_SendOpXX(2)` in handlers (filter-disabled branch) is FAITHFUL: do not touch it.
void GameWindows::SyncPrompt() {
    auto& p = game::g_Client.prompt;

    // Rising edge: prompt active and not yet reflected (or type changed).
    if (p.active && (!promptShown_ || promptType_ != p.type)) {
        const int  type   = p.type;
        const bool netAsk = IsNetConfirmType(type);
        // Mapping of the TWO text lines. The binary draws line1 @y+34 (0x5C31F5) and
        // line2 @y+50 (0x5C3266); PromptState::Open(t, body, title) is called by the
        // handlers with body = line1 and title = line2 (e.g. Net/GameHandlers_PartyGuild.cpp
        // :102 -> Open(8, "[name]"+Str(305), Str(306))). MsgBoxDialog::Render, however,
        // draws title_ ABOVE body_ (UI/UIManager.cpp:186-191). We therefore deliberately
        // cross (p.body -> title_, p.title -> body_) to preserve the original VISUAL
        // ORDER; MsgBoxDialog's field names remain misleading (flagged in the report).
        // `withCancel`: the 6 network types have 2 buttons (OK/Cancel); the others (type 2 =
        // UI_NoticeDlg) have only ONE (OK), cf. UI_NoticeDlg_Render 0x5C0630.
        UIManager::Instance().MsgBox().Open(
            p.body, p.title,
            [type](int button) {
                if (IsNetConfirmType(type)) {
                    // kBtnOk -> 0 (accept) ; kBtnCancel -> 1 (refuse). Cf. banner above.
                    SendPromptReply(type,
                        button == MsgBoxDialog::kBtnOk ? static_cast<int8_t>(0)
                                                       : static_cast<int8_t>(1));
                }
                // TODO [anchor 0x5C04DF]: for type 2 (UI_NoticeDlg), the original OK
                //   runs Net_CloseSocket(&g_NetClient) + g_SceneMgr=2 + g_SceneSubState=0
                //   (back to server select). That's the scope of gaps SCN-03/04:
                //   Scene/SceneManager.* is NOT owned by this front -> we just
                //   close the prompt (no fake action, no invention).
                game::g_Client.prompt.Close();
                // Disarms the mirror state IMMEDIATELY (cf. OnPromptDismissed): a new
                // prompt of the same type could arrive before the next Render. `this` is NOT
                // captured (the lambda outlives it, stored in msgBox_.onResult_, owned by the
                // UIManager singleton, which can outlive GameWindows) — go back through
                // Instance(), null after Shutdown.
                if (GameWindows* gw = GameWindows::Instance()) gw->OnPromptDismissed();
            },
            /*withCancel=*/netAsk);
        promptShown_ = true;
        promptType_  = type;
        return;
    }

    // Prompt active, already reflected, but the box is no longer open AND our callback did
    // NOT run (it would have reset promptShown_ to false): it was therefore closed by a THIRD
    // PARTY — UIManager::CloseAll/ResetAll, i.e. UI_CloseAllDialogs 0x5AC590 / UI_ResetAllDialogs
    // 0x5AC3F0. In the binary there is only ONE storage location (dword_1822440 IS both
    // the "box visible" flag and the "prompt active" flag): closing the box therefore TURNS OFF the
    // prompt. We align the mirror state on this semantics rather than leaving an orphaned
    // prompt (which would be reopened every frame in a loop).
    if (p.active && promptShown_ && !UIManager::Instance().MsgBox().IsOpen()) {
        p.Close();
        promptShown_ = false;
        promptType_  = 0;
        return;
    }

    // Falling edge: the prompt was closed by the network (PromptState::CloseIf from
    // handlers 0x2f/0x35/0x3c/0x42/0x48/0x51) -> close the box.
    if (!p.active && promptShown_) {
        UIManager::Instance().MsgBox().Close();
        promptShown_ = false;
        promptType_  = 0;
    }
}

void GameWindows::Render() {
    if (!inited_) return;
    // Reflects network prompt state into the shared MsgBox BEFORE rendering (SCN-01/SCN-02):
    // this is what finally gives game::g_Client.prompt a READER.
    SyncPrompt();
    // Syncs the quest-progress snapshot consumed by QuestTrackerWindow
    // with the state actually written by the network (game::g_QuestProgress, Game/QuestSystem.h).
    questTracker_.SetProgressState(game::g_QuestProgress);
    UIManager::Instance().Render();
}

bool GameWindows::HandleHotkey(int vk) {
    if (!inited_) return false;
    // Independent toggle per window (no CloseAll: these panels don't overlap
    // enough to justify the original's mutual exclusivity).
    auto toggle = [](Dialog& d) { if (d.IsOpen()) d.Close(); else d.Open(); };
    switch (vk) {
    case hotkeys::kInventory:      toggle(inventoryAdapter_); return true;
    case hotkeys::kCharacterStats: toggle(charStats_);      return true;
    case hotkeys::kSkillTree:      toggle(skillTree_);      return true;
    case hotkeys::kGuild:          toggle(guild_);          return true;
    case hotkeys::kOptions:        toggle(options_);        return true;
    case hotkeys::kSocial:         toggle(social_);         return true;
    case hotkeys::kWarehouse:      toggle(warehouse_);      return true;
    case hotkeys::kVendor:         toggle(vendor_);         return true;
    case hotkeys::kPlayerTrade:    toggle(trade_);          return true;
    case hotkeys::kEnchant:        toggle(enchant_);        return true;
    case hotkeys::kAutoPlay:       toggle(autoPlayWindow_); return true;
    default: return false;
    }
}

} // namespace ts2::ui

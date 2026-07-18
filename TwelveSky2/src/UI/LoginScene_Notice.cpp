// UI/LoginScene_Notice.cpp — modal notice dialog (UI_NoticeDlg_Open 0x5C0280 /
// UI_NoticeDlg_OnLButtonUp 0x5C03F0 simplified) and the shared modal MsgBox (UI_MsgBox_Render
// 0x5C3100). Mechanical split of LoginScene.cpp (see LoginScene.cpp for the class lifecycle/
// dispatch and LoginScene.h for the full class declaration).
#include "UI/LoginScene.h"
#include "UI/LoginScene_Internal.h" // kColText (shared by the split family)
#include "UI/UiProjection.h"       // ui::ProjectDesignAnchor (UI_ProjectSpriteToScreen 0x50F5D0)
#include "Config/GameOptions.h"    // ts2::config::Cfg_SaveLastServer (G02_GINFO\010.BIN, write-only)
#include "Net/Login.h"             // ConnectLoginServer / LoginRequest / ConnectGameServer
#include "Net/CharSelectPackets.h" // AccountKeepAlive/CreateCharacter/CharSlotAction/ReqEnterCharInfo/ReqCancelEnter
#include "Net/Rng.h"                // DefaultRng() — Rng_Next() % 360 for spawnRotationDeg (see GameState.h)
#include "Net/GameServerDomains.h"  // SelectGameServerHost / g_ServerMode (Net_SelectServerDomain 0x53FE90)
#include "Game/GameState.h"        // game::g_World.zoneId (consumed by EnterWorldFlow)
#include "Game/StringTables.h"     // game::g_Strings.bannedWords (001.DAT, 1432 banned words — creation filter)
#include "Game/ClientRuntime.h"    // game::Str(id) — real StrTable005 text for CharSelect notices
#include "Game/GameDatabase.h"     // game::GetItemInfo / WeaponClassFromTypeCode (entry motion 0x4CC870)
#include "Game/MiscManagers.h"     // game::Cursors() / kCursorDefault (scene-entry cursor reset, 0x4C1110)
#include "Asset/ImgFile.h"         // asset::ImgFile (.IMG loader, real ServerSelect/Login background)
#include "Gfx/Camera.h"            // gfx::Camera — application projection (Gfx_InitDevice 0x69BFC6)
#include "Core/Log.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

namespace ts2::ui {

// Draws the shared modal MsgBox (msgBox_ = dword_1822438) LAST for the active scene
// (UI_RenderAllDialogs 0x5AE2D0). REPLACES the old DeleteConfirmRender/ExitConfirmRender with
// their INVENTED geometry (modal veil + 2 panel/border fills + 64x24 Button, no real anchor):
// the binary draws NO fill at all, only atlas sprites via UI_MsgBox_Render 0x5C3100 (panel
// slot 7 centered on its native size; OK 8/9/10 @ +165,+90; Cancel 11/12/13 @ +241,+90; title
// centered +234,+42, empty body). Hover cursor = PHYSICAL position (CharSelectCursorClient =
// GetPhysicalCursorPos+ScreenToClient, like the dialog pipeline @0x5AE2DD). No-op if msgBox_
// is closed -> callable at the tail of ANY scene.
void LoginScene::RenderMsgBox() {
    if (!msgBox_.IsOpen()) return;
    const POINT cur = CharSelectCursorClient(); // physical cursor (dialog pipeline 0x5AE2DD)
    msgBox_.Render([this](int s) { return GetAtlasSprite(s); },
                   sprites_, font_, screenW_, screenH_, cur, kColText);
}

// Notice modal (UI_NoticeDlg_Open 0x5C0280 / UI_NoticeDlg_OnLButtonUp 0x5C03F0 simplified)
void LoginScene::OpenNotice(const std::string& text, std::function<void()> onClose) {
    noticeOpen_    = true;
    noticeText_    = text;
    noticeOnClose_ = std::move(onClose);
}

// Closes the notice and executes its "kind" action (cf. OpenNotice). Single entry point for
// the OK click (OnMouseUp) and Enter/Escape (OnKeyDown) — faithful: the binary also closes the
// notice then executes the action BEFORE returning control (UI_NoticeDlg_Close is called
// before the switch(this[4]) in UI_NoticeDlg_OnLButtonUp, EA 0x5C04A5).
void LoginScene::CloseNotice() {
    noticeOpen_ = false;
    if (noticeOnClose_) {
        std::function<void()> cb = std::move(noticeOnClose_);
        noticeOnClose_ = nullptr;
        cb();
    }
}

// No open/close animation: confirmed by decompiling UI_NoticeDlg_Render 0x5C0630 (drawn at a
// fixed position, full opacity, as long as the active flag is true) and
// UI_NoticeDlg_OnLButtonUp 0x5C03F0 (closes + executes the action on the same frame, no
// delay). See Game/ClientRuntime.h::PromptState for details. The static trace below is
// therefore faithful as is — do not add a fade.
void LoginScene::RenderNotice() {
    // Real notice dialog (UI_NoticeDlg_Render 0x5C0630): panel unk_8E8F5C = slot 7 ->
    // 001_00008.IMG, centered on ITS real size; OK button unk_8E8FF0/unk_8E9084/unk_8E9118 =
    // slots 8/9/10 (idle/hover/pressed) at (panel+203, +90); text centered on panel+234
    // at panel+42 (1 line) via UI_DrawNumberValue (outline mode). ZERO fill, ZERO modal veil
    // (the binary draws none): without the real texture, NOTHING (faithful to Sprite2D_Draw).
    gfx::GpuTexture* panel = GetAtlasSprite(7);
    if (!panel || !panel->Valid() || panel->Width() == 0 || panel->Height() == 0) return;
    const int panelW = static_cast<int>(panel->Width());
    const int panelH = static_cast<int>(panel->Height());
    const int px = screenW_ / 2 - panelW / 2;
    const int py = screenH_ / 2 - panelH / 2;
    const int okX = px + 203, okY = py + 90;

    // OK button hover (Sprite2D_HitTest on the native size of idle sprite slot 8).
    const POINT mp = CursorClient();
    gfx::GpuTexture* okIdle = GetAtlasSprite(8);
    bool okHover = false;
    if (okIdle && okIdle->Valid()) {
        okHover = mp.x >= okX && mp.x < okX + static_cast<int>(okIdle->Width()) &&
                  mp.y >= okY && mp.y < okY + static_cast<int>(okIdle->Height());
    }

    if (sprites_.Ready()) {
        sprites_.Begin();
        sprites_.DrawSprite(panel->Handle(), nullptr, px, py, gfx::kSpriteWhite);
        // OK: slot 8 (idle) or 9 (hover). Clicking its zone closes the notice (cf. OnMouseUp).
        if (gfx::GpuTexture* okT = GetAtlasSprite(okHover ? 9 : 8))
            sprites_.DrawSprite(okT->Handle(), nullptr, okX, okY, gfx::kSpriteWhite);
        sprites_.End();
    }
    if (font_.Ready() && !noticeText_.empty()) {
        font_.BeginBatch();
        const int tw = font_.MeasureText(noticeText_.c_str());
        font_.DrawTextStyled(noticeText_.c_str(), px + 234 - tw / 2, py + 42, kColText, gfx::kStyleOutline);
        font_.EndBatch();
    }
}

} // namespace ts2::ui

// UI/ConfirmMsgBox.h — shared Yes/No modal dialog (dword_1822438).
//
// FAITHFUL port of the binary's shared MsgBox: UI_MsgBox_Open 0x5C08C0 / _OnLButtonDown 0x5C0980
// / _OnLButtonUp 0x5C0A90 / _Render 0x5C3100, painted at the END of the scene by UI_RenderAllDialogs
// 0x5AE2D0 (@0x520EAF for CharSelect; the exit flow goes through the same object on ServerSelect).
// Replaces the INVENTED FillRect geometry of DeleteConfirmRender/ExitConfirmRender.
//
// Sprites (UI atlas g_AssetMgr_UiAtlasSlots 0x8E8B50, proven by UI_MsgBox_Render 0x5C3100):
//   panel slot 7 ; OK slots 8/9/10 (idle/hover/pressed) ; Cancel slots 11/12/13.
// Geometry: panel centered on ITS native size (0x5C313B/0x5C3160) ; OK @ (panelX+165,
// panelY+90) (0x5C35F5) ; Cancel @ (panelX+241, panelY+90) (0x5C368D) ; title centered at
// x=panelX+234, y=panelY+42 when the body is empty (0x5C31AC/0x5C31B2 — delete/exit case).
//
// Note: the NoticeDlg's OK (1 button) sits at panelX+203 (0x5C0830); the MsgBox's OK is at
// panelX+165 (it's pushed left to make room for Cancel). This module is the MsgBox (2 buttons) —
// the NoticeDlg remains ported separately (LoginScene::RenderNotice, already faithful).
//
// DECOUPLED module: knows neither the atlas nor the device — the caller supplies a
// SpriteProvider (slot -> gfx::GpuTexture*) plus its own SpriteBatch/Font. The panel is
// RECENTERED on EVERY Render AND on every click (like the binary): otherwise the hit-test
// drifts on resize.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <windows.h>   // POINT
#include <d3d9.h>      // D3DCOLOR

#include "Gfx/GpuTexture.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"

namespace ts2::ui {

class ConfirmMsgBox {
public:
    using OkFn = std::function<void()>;                          // switch(type) action on OK
    using SpriteProvider = std::function<gfx::GpuTexture*(int slot)>;

    // UI_MsgBox_Open 0x5C08C0: opens with a localized title + an `actionType` (the binary's
    // switch(type): 2=delete, 1=exit) + the OK callback. Body is always "" (delete/exit).
    void Open(std::string title, int32_t actionType, OkFn onOk);
    void Close();                                                // UI_ConfirmPrompt_Close 0x5C0960
    bool IsOpen() const { return open_; }
    int32_t Type() const { return type_; }

    // UI_MsgBox_Render 0x5C3100: panel 7 + OK (8/9/10) + Cancel (11/12/13) + title. Recenters.
    // Runs its OWN batches (sprite then font), as DeleteConfirmRender used to.
    void Render(const SpriteProvider& sprite, gfx::SpriteBatch& sb, gfx::Font& font,
                int screenW, int screenH, POINT cursor, D3DCOLOR textColor);

    // UI_MsgBox_OnLButtonDown 0x5C0980: arms btnPressed_ (OK/Cancel). Always modal (true).
    bool OnMouseDown(const SpriteProvider& sprite, int x, int y, int screenW, int screenH);
    // UI_MsgBox_OnLButtonUp 0x5C0A90: on OK -> Close then onOk_() ; on Cancel -> Close. Modal.
    bool OnMouseUp(const SpriteProvider& sprite, int x, int y, int screenW, int screenH);

private:
    void Recenter(const SpriteProvider& sprite, int screenW, int screenH);
    static bool HitSprite(gfx::GpuTexture* t, int x, int y, int mx, int my);

    bool        open_ = false;
    std::string title_;
    int32_t     type_ = 0;
    OkFn        onOk_;
    bool        btnPressed_[2] = {false, false}; // [0]=OK, [1]=Cancel
    int         panelX_ = 0, panelY_ = 0;        // recentered on every Render/click
};

} // namespace ts2::ui

// UI/OptionsWindow.h — "Options / Settings" window of the TwelveSky2 client.
//
// Clean C++ transposition of the options dialog from the disassembly:
//   UI_OptionsWnd_OnClick     0x66D140  checkboxes + -/+ buttons with clamping
//   UI_OptionsWnd_OnMouseDown 0x66C180  button latching (~47 hit-tests, volumes as float)
//   UI_OptionsWnd_Render      0x66EAC0  draws state + READS THE REAL TEXT (StrTable005_Get,
//                                       0x4C1D20) displayed to the right of each row
//   Options_SetDefaults       0x4C2020  \
//   Options_LoadBin           0x4C2140   > config::GameOptions (Config/GameOptions.h)
//   Options_SaveBin           0x4C2280  /
//
// The options block is 23 x int32 (see Config/GameOptions.h for the byte-exact layout
// and per-field documented bounds). This window exposes the 21 fields ACTUALLY driven
// by a control in UI_OptionsWnd_Render/OnClick/OnMouseDown:
//   - bounds [0,1]                -> checkbox (toggled on click)
//   - wider bounds                -> "-" / "+" buttons clamping at the bound
// idx3 (Reserved3) and idx8 (Reserved8) are DELIBERATELY ABSENT from kFields: a full
// read of the 3 original UI functions (0x66C180..0x66D833 OnMouseDown, 0x66D140..0x66E82A
// OnClick, 0x66EAC0..0x67048B Render) confirms that NO hit-test, NO Sprite2D_Draw and NO
// StrTable005_Get references these two fields — they are loaded/saved (92 bytes intact)
// but never had a row in the original panel. Giving them a made-up checkbox would be a
// placeholder, not fidelity; GameOptions.h already documents them as "reserved, no xref".
//
// DISPLAYED VALUES: the text to the right of each row (e.g. "ON"/"OFF", "LOW"/"MEDUIM"/
// "HIGH", "Basic"/"Advanced", "Phase 1".."Phase 9", "[F1]-[F10]"/"[1]-[0]") is the REAL
// StrTable005 text (005.DAT, mMESSAGE) read by UI_OptionsWnd_Render via StrTable005_Get
// (this+106*i-102) — reconstructed here via game::Str(id) (Game/ClientRuntime.cpp), which
// reads the same 005.DAT table loaded by Game/StringTables.h. valueStrBase = StrTable005
// id for value==minV; subsequent values are CONTIGUOUS (+1 per step), confirmed for the
// 19 relevant fields by disassembling Render. The 2 volume sliders (idx10/idx11) have NO
// text in the original (just a slider handle Sprite2D_Draw with no StrTable005_Get):
// valueStrBase=-1 -> numeric "%d" fallback (faithful: it's genuinely the only text info
// missing from the original panel).
//
// PERSISTENCE (OPT-01): the binary has NO "Save" button. EVERY option mutation (-/+
// click, checkbox, end of volume drag) calls Options_SaveBin 0x4C2280 — single choke
// point LABEL_270 @0x66E819 of UI_OptionsWnd_OnClick 0x66D140 for -/+, and
// @0x66DC9C/@0x66DCB9 for the 2 sliders. The 92-byte block is fully rewritten on every
// click. Persistence is therefore IMPLICIT; Options_Save_STUB 0x4C2130 (called by
// App_Shutdown) has an empty body -> the click is the ONLY save path.
//
// FOOTER (OPT-02/03): instead of a Save/Cancel pair (a porting invention), the binary
// carries TWO footer buttons (y+596), tested right after Close:
//   - "Logout" (this+5, x+148 = LEFT, sprite unk_901470, @0x66D2AC):
//       Net_CloseSocket(&g_NetClient) + g_SceneMgr=2 + g_SceneSubState=0 + dword_1676188=0
//       + Close (@0x66D2C4..@0x66D2EA). Ported via ::ts2::Notice_DispatchOkAction(2) (same
//       quadruplet @0x5C04DF, DEFERRED return to ServerSelect — no live scene change,
//       ClientSource architecture constraint).
//   - "Quit game" (this+4, x+202 = RIGHT, sprite unk_8F4C68, @0x66D236):
//       Log_WriteLine "[ABNORMAL_END] ( 5 )" + g_QuitFlag=1 + Close (@0x66D253..@0x66D265).
//       g_QuitFlag=1 (0x815590) modeled via PostQuitMessage(0) (established project idiom,
//       cf. UI/LoginScene.cpp:232-233, App/App.cpp WM_DESTROY).
// None of these 3 buttons (Close/Quit/Logout) save: only option mutations do.
//
// None of these 23 values travel over the network (they are local client preferences,
// cf. comment in GameOptions.h): so there is no TODO(send) here.
#pragma once
#include <cstdint>

#include "UI/UIManager.h"
#include "Config/GameOptions.h"

namespace ts2::ui {

// Description of an options-block field — member pointer into config::GameOptions
// so the 21 rows are driven from a single data table (no duplicated code).
struct OptionsFieldDef {
    const char* label;                          // displayed label (FR)
    int32_t config::GameOptions::* field;        // member pointer (idx0..idx22, cf. skip idx3/idx8)
    bool    checkbox;                            // true = bound [0,1] -> checkbox
    int32_t minV;                                // documented lower bound
    int32_t maxV;                                // documented upper bound
    int32_t step;                                // -/+ button step (ignored if checkbox)
    int32_t valueStrBase;                        // StrTable005 id for value==minV, -1 = no text (numeric slider)
};

// Options window: scrollable vertical list of the 23 config::g_Options fields, plus
// the binary's 3 buttons (Close / Logout / Quit game). Implicit persistence on every
// mutation (OPT-01). Inherits ts2::ui::Dialog (UIManager contract).
class OptionsWindow : public Dialog {
public:
    OptionsWindow();

    void Open() override;                          // Dialog::Open() + reload displayed state
    void Close() override { Dialog::Close(); }

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

private:
    struct Rect { int x, y, w, h; };

    // Currently "armed" target (button pressed, waiting for release on top of it).
    // ExitGame/Logout = footer buttons (OPT-02); Slider = volume drag (OPT-04).
    enum class Target {
        None, Close, ExitGame, Logout, ScrollUp, ScrollDown,
        RowCheckbox, RowMinus, RowPlus, Slider,
    };

    // Geometry recomputed every frame from screen dimensions (centering), exactly like
    // MsgBoxDialog::Layout — the hit-test (routed across two frames) relies on
    // lastScreenW_/lastScreenH_ memorized at the last Render.
    void Layout(int screenW, int screenH, Rect& panel, Rect& close,
                Rect& logout, Rect& exitGame, Rect& list, Rect& scrollUp, Rect& scrollDown) const;
    // Rectangle of row `row` (index into kFields) in `list`'s coordinate space, accounting
    // for current scroll. Returns false if outside the visible area.
    bool RowRect(const Rect& list, int row, Rect& out) const;
    // Interactive sub-rects of a row (checkbox OR minus/plus+value).
    Rect RowCheckboxRect(const Rect& row) const;
    Rect RowMinusRect(const Rect& row) const;
    Rect RowPlusRect(const Rect& row) const;
    // Horizontal track of a volume slider (OPT-04). Width 94 px, mirrors the binary
    // (dbl_7BAF20=94.0; UI_OptionsWnd_Render 0x66EAC0, clamp [winX+0x80, winX+0xDE] @0x66FED0).
    Rect RowSliderTrackRect(const Rect& row) const;

    int  VisibleRowCount(const Rect& list) const;
    int  MaxScroll(const Rect& list) const;
    void ClampScroll(const Rect& list);

    // Applies the armed target's action if the release lands on it.
    void ActivateIfHit(const Rect& panel, const Rect& close, const Rect& logout,
                        const Rect& exitGame, const Rect& list,
                        const Rect& scrollUp, const Rect& scrollDown, int x, int y);

    static bool In(const Rect& r, int x, int y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }

    Target armedTarget_ = Target::None;
    int    armedRow_    = -1; // affected row if RowCheckbox/RowMinus/RowPlus

    int    scroll_ = 0; // first visible row (scrolling)

    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;

    // --- Geometry constants (panel + list) ---
    static constexpr int kPanelW   = 380;
    static constexpr int kPanelH   = 440;
    static constexpr int kTitleH   = 26;
    static constexpr int kFooterH  = 34;
    static constexpr int kRowH     = 20;
    static constexpr int kCloseBtn = 18;
    static constexpr int kScrollBtnH = 18;
    // Width of the volume slider track (OPT-04) — reuses the binary's 94.0 constant
    // (dbl_7BAF20, UI_OptionsWnd_Render 0x66EAC0).
    static constexpr int kTrackW = 94;

    // --- Palette (ARGB, cf. UI contract) ---
    static constexpr D3DCOLOR kColBg       = Argb(224, 32, 32, 40);
    static constexpr D3DCOLOR kColBorder   = Argb(255, 128, 128, 128);
    static constexpr D3DCOLOR kColTitleBg  = Argb(255, 44, 44, 60);
    static constexpr D3DCOLOR kColTitle    = Argb(255, 255, 221, 102);
    static constexpr D3DCOLOR kColText     = Argb(255, 255, 255, 255);
    static constexpr D3DCOLOR kColRowAlt   = Argb(60, 255, 255, 255);
    static constexpr D3DCOLOR kColBtn      = Argb(255, 56, 64, 88);
    static constexpr D3DCOLOR kColBtnHover = Argb(255, 64, 96, 160);
    static constexpr D3DCOLOR kColBtnDown  = Argb(255, 150, 120, 70);
    static constexpr D3DCOLOR kColCheck    = Argb(255, 96, 255, 96);
    static constexpr D3DCOLOR kColClose    = Argb(255, 255, 96, 96);
};

} // namespace ts2::ui

// UI/UIManager.h — UI framework / dialog manager of the TwelveSky2 client.
//
// FAITHFUL rewrite of the UI framework found in the disassembly (idaTs2), see
// Docs/TS2_CLIENT_SHELL.md §2.2. The original framework is NOT a polymorphic object
// system: it's a static registry of about 38 singleton dialogs whose "methods" are
// free __thiscall functions hand-wired into parallel dispatch chains (one per
// lifecycle phase / event type). This gives a clean, PRAGMATIC C++ transposition
// (a `Dialog` base class, a `UIManager` registry) that preserves the TWO key
// invariants:
//
//   1. "First consumer wins" (UI_RouteLButtonDown 0x5AC740,
//      UI_RouteLButtonUp 0x5AD0F0, UI_RouteKeyInput 0x5ADF50): events are pushed
//      to each dialog in sequence; as soon as a handler returns 1 the chain
//      stops and the 3D world does not receive the event.
//   2. Rendered in REVERSE order of routing (UI_RenderAllDialogs 0x5AE2D0):
//      background first, modal popups last (= drawn on top). There is NO
//      separate UI logic tick: the logic lives in _Render, the Pkt_* network
//      handlers, and keyboard routing.
//
// Builds on the Gfx bricks: ts2::gfx::Renderer (device), ts2::gfx::SpriteBatch
// (2D ID3DXSprite batch) and ts2::gfx::Font (ID3DXFont text). None of these
// bricks are redefined here.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <vector>
#include <functional>

#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Core/Types.h"

namespace ts2::ui {

// Compact ARGB color (local helper; D3DCOLOR = 0xAARRGGBB).
inline constexpr D3DCOLOR Argb(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
    return (static_cast<D3DCOLOR>(a) << 24) | (static_cast<D3DCOLOR>(r) << 16)
         | (static_cast<D3DCOLOR>(g) << 8)  |  static_cast<D3DCOLOR>(b);
}

// Render phase: the UI pass is split into two independent sub-passes because the
// sprite batch (panels) and the font batch (text) are two distinct ID3DXSprite
// objects. FillRect/DrawFrame only draw in the Panels phase; Text only in the Text
// phase. Each Dialog::Render is thus called twice per frame, but written naturally
// (panels + text in a single method body).
//
// TODO [anchors 0x69E620 / 0x69E650 / 0x6A3080 / 0x69E750] — PROVEN DIVERGENCE (gap GX2D-01),
// fix OUT OF SCOPE for this front (it lives in Gfx/Font.{h,cpp} + Gfx/SpriteBatch.{h,cpp}).
// The binary has only ONE ID3DXSprite for the entire 2D pass, and blits and text are
// INTERLEAVED in a SINGLE batch:
//   - Gfx_Begin2D 0x69E620: SetRenderState(ZENABLE=7, FALSE) @0x69E630 then
//     Sprite(+608)->Begin(D3DXSPRITE_ALPHABLEND=16) @0x69E644  — ONE SINGLE Begin per frame.
//   - Gfx_End2D   0x69E650: Sprite(+608)->End() @0x69E65C then SetRenderState(7, TRUE).
//   - UI_DrawSprite 0x6A3080 (blit): Draw via `dword_800078` @0x6A30FC = g_GfxRenderer+608.
//   - UI_DrawText   0x69E750 (text): Font(+612)->DrawTextA(Sprite(+608), ...) @0x69E800.
//     => both consumers hit the SAME ID3DXSprite (0x800078).
//   - Interleaving proven in Scene_LoginRender 0x51B020, between Gfx_Begin2D @0x51B189 and
//     Gfx_End2D @0x51B5A7: blit @0x51B207, blit @0x51B28F, TEXT @0x51B316, blit (caret)
//     @0x51B34F, TEXT @0x51B40C, blits @0x51B445/0x51B4C7/0x51B54A.
// ZENABLE=FALSE + no sorting => SUBMISSION ORDER is occlusion order. Here, the two
// sub-passes below (panels THEN text, on two distinct ID3DXSprite objects —
// SpriteBatch::sprite_ and Font::sprite_) push ALL text above ALL panels: the order
// diverges as soon as two overlapping dialogs are registered. LATENT as of today:
// UIManager only registers one dialog (msgBox_, cf. Init), and with a single dialog
// both orders coincide. Fix: share a single ID3DXSprite between Font and SpriteBatch
// (e.g. Font::SetExternalSprite), then collapse UIManager::Render() into one pass and
// drop UiPhase. Also affects ChatWindow/BuffStatusPanel/GameHud/GameWindows/
// WorldRenderer, which use the same two-pass idiom.
enum class UiPhase { Panels, Text };

// ---------------------------------------------------------------------------
// UiContext — shared graphics resources passed to each Dialog::Render.
// Provides the minimal 2D primitives (filled rect, frame, text) on top of
// SpriteBatch/Font. The filled rect is a blit of a 1x1 white texture scaled and
// modulated by the color (the engine's only real modulation, cf.
// SpriteBatch::DrawSpriteScaled), so it doesn't depend on a .IMG asset.
struct UiContext {
    gfx::Renderer*     renderer = nullptr; // D3D9 device (ts2::gfx::Renderer)
    gfx::SpriteBatch*  sprites  = nullptr; // shared 2D batch (panels)
    gfx::Font*         font     = nullptr; // UI font (text)
    IDirect3DTexture9* whiteTex = nullptr; // 1x1 white texture (created by UIManager)
    int                screenW  = ts2::kRefWidth;  // nWidth  (0x1669184)
    int                screenH  = ts2::kRefHeight; // nHeight (0x1669188)
    float              gameTimeSec = 0.0f;         // g_GameTimeSec (auto-close, animations)
    UiPhase            phase    = UiPhase::Panels;  // current sub-pass

    // Filled colored rect (Panels phase only).
    void FillRect(int x, int y, int w, int h, D3DCOLOR color) const;
    // 1 px frame (Panels phase only).
    void DrawFrame(int x, int y, int w, int h, D3DCOLOR color, int thickness = 1) const;
    // Normal-mode text via the UI font (Text phase only).
    void Text(const char* s, int x, int y, D3DCOLOR color) const;
    // Pixel width of a text (for centering); 0 if font unavailable.
    int  MeasureText(const char* s) const;
};

// ---------------------------------------------------------------------------
// Dialog — base class of a UI dialog. Transposes the disassembly's UIDialogBase
// struct: +0 x, +4 y, +8 bOpen, +0xC btnPressed[N]. Handlers return true if the
// event is CONSUMED ("first consumer wins" rule).
class Dialog {
public:
    virtual ~Dialog() = default;

    // Opens / closes the dialog (toggles the "visible"/bOpen flag).
    virtual void Open();   // *_Open pattern: bOpen=1, resets button latches
    virtual void Close();  // bOpen=0

    // Left-click pressed (UI_RouteLButtonDown 0x5AC740 chain).
    virtual bool OnMouseDown(int x, int y) { (void)x; (void)y; return false; }
    // Left-click released = confirmed "click" (UI_RouteLButtonUp 0x5AD0F0 chain).
    virtual bool OnClick(int x, int y)     { (void)x; (void)y; return false; }
    // Keyboard input / key (UI_RouteKeyInput 0x5ADF50 chain). `vk` = virtual-key.
    virtual bool OnKey(int vk)             { (void)vk; return false; }

    // --- RIGHT click (gap UIFW-01) ------------------------------------------
    // UI_RouteRButtonDown 0x5AD5D0 chain (1st slot called @0x5AD5E4) / UI_RouteRButtonUp
    // 0x5ADA90 (1st slot @0x5ADAA4): 38 "first consumer wins" slots, strictly the
    // same `test eax,eax / jz / mov eax,1 / jmp` pattern as the left-click chains.
    // WARNING: in the IDB, the slots of chain 0x5AD5D0 carry the INHERITED and
    // MISLEADING label `UI_Dlg_OnLButtonDblClk_*`: their real role is OnRButtonDown (cf.
    // IDA head comment of 0x5AD5D0). Real handlers (non-stubs): cGameHud_OnRButtonDown
    // 0x6318E0 (@0x5AD7E4), cQuickSlotWin_OnRButtonDown 0x6608D0 (@0x5AD804),
    // UI_NpcWin_OnRDown_Dispatch 0x5DDC50 (@0x5AD7A4), UI_OptionsWnd_OnRButtonDown 0x66A170,
    // UI_CharListWnd_OnRButtonDown 0x66E840, UI_RankWnd_OnRButtonDown 0x6747E0, etc.
    virtual bool OnRButtonDown(int x, int y) { (void)x; (void)y; return false; }
    virtual bool OnRButtonUp(int x, int y)   { (void)x; (void)y; return false; }

    // Per-frame render (called in REVERSE order of routing). Receives the client
    // cursor position for hover, like UI_RenderAllDialogs which pushes (x,y) to
    // each draw.
    virtual void Render(const UiContext& ctx, int cursorX, int cursorY) {
        (void)ctx; (void)cursorX; (void)cursorY;
    }

    // --- HOVER / tooltip pass (gap UIFW-03) -------------------------
    // Mirrors UI_RouteRButtonExamine 0x5AE5E0. WARNING: despite its IDA name, this
    // function has NOTHING to do with the right button: `xrefs_to 0x5AE5E0` returns
    // EXACTLY 1 xref, @0x5AE5C9 — the LAST useful instruction of UI_RenderAllDialogs
    // 0x5AE2D0 (followed by `mov esp,ebp / pop ebp / retn` @0x5AE5CE). It is therefore a
    // per-frame HOVER pass, executed AFTER the ~39 draws, with the SAME (x,y) client
    // cursor as the draws (var_C/var_10, from GetPhysicalCursorPos @0x5AE2DD).
    // Two structural properties to preserve:
    //   (1) "first consumer wins" chain -> ONE SINGLE tooltip per frame;
    //   (2) executed after all Render calls -> the tooltip is ALWAYS on top.
    // Real consumers: UI_Shop_ShowItemTooltip 0x5C9360 (@0x5AE6B1),
    // UI_Warehouse_ShowItemTooltip 0x5CB4A0 (@0x5AE702), UI_ItemListWin_OnMove 0x5D2510
    // (@0x5AE71D), UI_StorageWin_OnMove 0x5D7D20 (@0x5AE738), UI_NpcWin_OnMove_Dispatch
    // 0x5DE8C0 (@0x5AE76E), cGameHud_DrawTooltipDispatch 0x64EA30 (@0x5AE7A4),
    // cQuickSlotWin_DrawTooltip 0x6620E0 (@0x5AE7BF), UI_QuickBar_Handle 0x6869E0
    // (@0x5AE8CD), UI_ConsumableBar_OnRightClick 0x68E940 (@0x5AE8E8).
    // Returns true if THIS dialog drew its tooltip (= consumes the pass).
    virtual bool OnHover(const UiContext& ctx, int cursorX, int cursorY) {
        (void)ctx; (void)cursorX; (void)cursorY; return false;
    }

    // Opt-out of UIManager::CloseAll (public/documented default of the binary: closed).
    // UI_CloseAllDialogs 0x5AC590 does NOT act on the entire UI: its list is FIXED (~27
    // targets) and DELIBERATELY leaves MsgBox (dword_1822438), NoticeDlg, TextInput,
    // ItemListWin, StorageWin, ClanWin, NpcWin, AutoPlay open — whereas
    // UI_ResetAllDialogs 0x5AC3F0 (~42 targets) DOES reset them, MsgBox included (call
    // UI_Dlg_OnReset_ClearFlag8_5C08A0(dword_1822438) @0x5AC430, with no counterpart in
    // 0x5AC590). A dialog absent from the 0x5AC590 list must return false here.
    virtual bool ClosedByCloseAll() const { return true; }

    bool IsOpen() const { return bOpen_; }  // *(this+8)
    int  X() const { return x_; }           // *(this+0)
    int  Y() const { return y_; }           // *(this+4)

protected:
    static bool PointInRect(int px, int py, int rx, int ry, int rw, int rh) {
        return px >= rx && px < rx + rw && py >= ry && py < ry + rh;
    }

    int  x_     = 0;      // +0x00 screen position (recentered every frame)
    int  y_     = 0;      // +0x04
    bool bOpen_ = false;  // +0x08 visible flag
};

// ---------------------------------------------------------------------------
// MsgBoxDialog — shared OK/Cancel modal dialog (unk_1822438 in the original).
// Faithful transposition of the MsgBox cycle from §2.2:
//   UI_MsgBox_Open        0x5C08C0  (bOpen=1, openTime, reset latches, context)
//   UI_MsgBox_OnLButtonDown 0x5C0980 (OK/Cancel hit-test, latch, modal -> return 1)
//   UI_MsgBox_OnLButtonUp 0x5C0A90  (confirms per the pressed button, then closes)
//   UI_MsgBox_Render      0x5C3100  (screen center, frame, text, buttons + auto-close)
// Pragmatic simplifications: text is supplied in plain form (instead of a switch on
// contextType -> StrTable005) and the background is a flat panel (instead of the
// .IMG sprite).
class MsgBoxDialog : public Dialog {
public:
    static constexpr int kBtnOk     = 0;
    static constexpr int kBtnCancel = 1;

    // Result callback: receives kBtnOk / kBtnCancel. Optional.
    using ResultFn = std::function<void(int button)>;

    // Opens the box with a title + body; `onResult` fires on confirmation.
    // `withCancel=false` -> 1-button box (simplified NoticeDlg behavior).
    void Open(const std::string& title, const std::string& body,
              ResultFn onResult = {}, bool withCancel = true);
    void Open() override { Dialog::Open(); } // bare open (default state)

    bool OnMouseDown(int x, int y) override;
    bool OnClick(int x, int y) override;
    bool OnKey(int vk) override;
    void Render(const UiContext& ctx, int cursorX, int cursorY) override;

    // UI_CloseAllDialogs 0x5AC590 does NOT touch dword_1822438 (the shared MsgBox): the
    // list @0x5AC5A2-0x5AC6D1 never cites it. Only UI_ResetAllDialogs 0x5AC3F0 resets it
    // (@0x5AC430). Opening a window must therefore NOT swallow the current modal box —
    // hence the opt-out.
    bool ClosedByCloseAll() const override { return false; }

private:
    struct Rect { int x, y, w, h; };
    // Geometry recomputed every frame from screen dimensions (centering).
    void Layout(int screenW, int screenH, Rect& box, Rect& ok, Rect& cancel) const;
    void Finish(int button); // invokes the callback + closes

    std::string title_;
    std::string body_;
    ResultFn    onResult_;
    bool        withCancel_ = true;
    bool        btnPressed_[2] = {false, false}; // +0x0C latches (armed on down)
    float       openTime_ = 0.0f;                // +0x14 openTime (type-4 auto-close)
    // Screen dims stored at the last Render: hit-testing (OnMouseDown/OnClick, routed
    // between two frames) must align with the actually drawn geometry, regardless of
    // resolution. Faithful to the original UI which recenters every frame.
    mutable int lastScreenW_ = ts2::kRefWidth;
    mutable int lastScreenH_ = ts2::kRefHeight;
};

// ---------------------------------------------------------------------------
// UIManager — dialog registry + event routers + render pass. Equivalent to the
// binary's UI_* free-function block + static registry. Singleton (like
// g_GxdRenderer::Instance()), since the original manipulates global singletons.
class UIManager {
public:
    static UIManager& Instance();

    // Init: stores the Gfx bricks, creates the 1x1 white texture, registers the
    // built-in dialogs (MsgBox). `hwnd` is used to convert the screen cursor
    // position -> client (like UI_RenderAllDialogs: GetPhysicalCursorPos + ScreenToClient).
    bool Init(gfx::Renderer* renderer, gfx::SpriteBatch* sprites, gfx::Font* font,
              HWND hwnd, int screenW, int screenH);
    void Shutdown(); // UI_DestroyAllDialogs 0x5AC270 (teardown, App_Shutdown)

    // Registers a dialog (NOT owned). Order = ROUTING priority: index 0 = first
    // consumer / topmost popup; rendering iterates in reverse.
    void Register(Dialog* dlg);

    // --- Event routers (first consumption wins) ---
    bool RouteMouseDown(int x, int y); // UI_RouteLButtonDown 0x5AC740
    bool RouteMouseUp(int x, int y);   // UI_RouteLButtonUp   0x5AD0F0
    bool RouteKey(int vk);             // UI_RouteKeyInput    0x5ADF50
    // RIGHT click (gap UIFW-01). Names expected AS-IS by the App front, which already
    // assigned its hooks and documents them: App/App.cpp:633 and :651 cite
    // "ts2::ui::UIManager::RouteRButtonDown/Up". DO NOT rename without re-wiring App.
    bool RouteRButtonDown(int x, int y); // UI_RouteRButtonDown 0x5AD5D0
    bool RouteRButtonUp(int x, int y);   // UI_RouteRButtonUp   0x5ADA90

    // Per-frame render pass (reverse order of routing). Call from Scene_*Render,
    // once per frame. UI_RenderAllDialogs 0x5AE2D0.
    void Render();

    // --- Global lifecycle ---
    // WARNING: 0x5AC3F0 and 0x5AC590 are TWO DISTINCT functions with different lists —
    // do not conflate them (they were, here, until wave W9, cf. gap N-1).
    void ResetAll(); // UI_ResetAllDialogs 0x5AC3F0 (scene transitions): ~42 targets,
                     // EVERYTHING is reset, MsgBox included (@0x5AC430), EDIT focus
                     // released UNCONDITIONALLY (UI_FocusEditBox(mgr,0) @0x5AC3FE).
    void CloseAll(); // UI_CloseAllDialogs 0x5AC590 (opening a window closes the others):
                     // FIXED list of ~27 targets, which spares MsgBox & co. Dialogs
                     // outside the list opt out via Dialog::ClosedByCloseAll() -> false.

    // Access to the built-in MsgBox dialog (shared, like unk_1822438).
    MsgBoxDialog& MsgBox() { return msgBox_; }

    // Screen dimensions (updated on resize).
    void SetScreenSize(int w, int h) { ctx_.screenW = w; ctx_.screenH = h; }

private:
    UIManager() = default;
    ~UIManager() { Shutdown(); }
    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;

    bool CreateWhiteTexture(IDirect3DDevice9* dev);
    // "First consumer wins" hover pass (UI_RouteRButtonExamine 0x5AE5E0, called
    // @0x5AE5C9 at the end of UI_RenderAllDialogs). Executed at the end of EACH
    // sub-pass of Render() — see the detailed justification on the definition.
    void RunHoverChain(int cx, int cy);

    UiContext             ctx_;
    HWND                  hwnd_ = nullptr;
    std::vector<Dialog*>  dialogs_;   // registry in routing order (0 = top)
    MsgBoxDialog          msgBox_;    // built-in dialog (owned)
    bool                  inited_ = false;
};

} // namespace ts2::ui

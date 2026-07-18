// UI/UIManager.cpp — UI framework implementation / dialog manager.
// See UIManager.h and Docs/TS2_CLIENT_SHELL.md §2.2.
#include "UI/UIManager.h"
#include "Core/Log.h"
#include <utility> // std::move

namespace ts2::ui {

// ===========================================================================
// UiContext — 2D primitives
// ===========================================================================
namespace {
// Unit source rect for the 1x1 white texture (scaled by FillRect).
const RECT kUnitSrc = {0, 0, 1, 1};
} // namespace

void UiContext::FillRect(int x, int y, int w, int h, D3DCOLOR color) const {
    if (phase != UiPhase::Panels) return;          // sprite draw outside Panels phase
    if (!sprites || !sprites->Ready() || !whiteTex) return;
    if (w <= 0 || h <= 0) return;
    // Blit of a 1x1 white texture scaled to (w,h) and modulated by `color`.
    // compensatePos=true: final on-screen position stays (x,y) despite the scale
    // (cf. SpriteBatch::DrawSpriteScaled / UI_DrawSpriteScaledAlpha 0x457D70).
    sprites->DrawSpriteScaled(whiteTex, &kUnitSrc, x, y,
                              static_cast<float>(w), static_cast<float>(h),
                              color, /*compensatePos=*/true);
}

void UiContext::DrawFrame(int x, int y, int w, int h, D3DCOLOR color, int t) const {
    if (phase != UiPhase::Panels) return;
    if (w <= 0 || h <= 0 || t <= 0) return;
    FillRect(x,         y,         w, t,         color); // top
    FillRect(x,         y + h - t, w, t,         color); // bottom
    FillRect(x,         y,         t, h,         color); // left
    FillRect(x + w - t, y,         t, h,         color); // right
}

void UiContext::Text(const char* s, int x, int y, D3DCOLOR color) const {
    if (phase != UiPhase::Text) return;            // text draw outside Text phase
    if (!s || !font || !font->Ready()) return;
    // Shadowed style (kStyleShadow): readability over panels, matching the original
    // UI which pushes a black outline/shadow (Font_DrawTextStyled 0x405DC0).
    font->DrawTextStyled(s, x, y, color, gfx::kStyleShadow);
}

int UiContext::MeasureText(const char* s) const {
    if (!s || !font || !font->Ready()) return 0;
    return font->MeasureText(s);
}

// ===========================================================================
// Dialog — base
// ===========================================================================
void Dialog::Open()  { bOpen_ = true; }
void Dialog::Close() { bOpen_ = false; }

// ===========================================================================
// MsgBoxDialog — OK/Cancel modal (unk_1822438)
// ===========================================================================
namespace {
// MsgBox frame dimensions (approximation of the original background sprite).
constexpr int kBoxW = 320;
constexpr int kBoxH = 160;
constexpr int kBtnW = 96;
constexpr int kBtnH = 28;

// Palette (flat colors, in lieu of the .IMG sprite).
const D3DCOLOR kColBg       = Argb(230,  24,  28,  40);
const D3DCOLOR kColBorder   = Argb(255, 180, 150,  90);
const D3DCOLOR kColBtn      = Argb(255,  56,  64,  88);
const D3DCOLOR kColBtnHover = Argb(255,  84,  96, 128);
const D3DCOLOR kColBtnDown  = Argb(255, 150, 120,  70);
const D3DCOLOR kColText     = Argb(255, 240, 240, 240);
const D3DCOLOR kColTitle    = Argb(255, 255, 214, 140);
} // namespace

void MsgBoxDialog::Layout(int screenW, int screenH, Rect& box, Rect& ok, Rect& cancel) const {
    // Screen centering (like UI_MsgBox_Render: centers (nWidth/2, nHeight/2)).
    box.x = screenW / 2 - kBoxW / 2;
    box.y = screenH / 2 - kBoxH / 2;
    box.w = kBoxW;
    box.h = kBoxH;
    const int by = box.y + kBoxH - kBtnH - 16;
    if (withCancel_) {
        // Two buttons: OK on the left, Cancel on the right.
        ok     = { box.x + 40,                 by, kBtnW, kBtnH };
        cancel = { box.x + kBoxW - 40 - kBtnW, by, kBtnW, kBtnH };
    } else {
        // Single centered button.
        ok     = { box.x + kBoxW / 2 - kBtnW / 2, by, kBtnW, kBtnH };
        cancel = { 0, 0, 0, 0 };
    }
}

void MsgBoxDialog::Open(const std::string& title, const std::string& body,
                        ResultFn onResult, bool withCancel) {
    // UI_MsgBox_Open 0x5C08C0: cuts input, stores openTime, arms bOpen,
    // resets button latches, sets title/body.
    title_        = title;
    body_         = body;
    onResult_     = std::move(onResult);
    withCancel_   = withCancel;
    btnPressed_[0] = btnPressed_[1] = false;
    openTime_     = gfx::g_GameTimeSec;
    bOpen_        = true;
    TS2_LOG("MsgBox ouverte : \"%s\"", title_.c_str());
}

void MsgBoxDialog::Finish(int button) {
    bOpen_ = false;                    // close (bOpen=0)
    ResultFn fn = std::move(onResult_);
    onResult_ = {};
    btnPressed_[0] = btnPressed_[1] = false;
    if (fn) fn(button);
}

bool MsgBoxDialog::OnMouseDown(int x, int y) {
    if (!bOpen_) return false;
    // UI_MsgBox_OnLButtonDown 0x5C0980: button hit-test -> latch btnPressed[i].
    // Uses the screen dims stored at the last Render (actually drawn geometry).
    Rect box, ok, cancel;
    Layout(lastScreenW_, lastScreenH_, box, ok, cancel);
    if (PointInRect(x, y, ok.x, ok.y, ok.w, ok.h)) {
        btnPressed_[kBtnOk] = true;
    } else if (withCancel_ && PointInRect(x, y, cancel.x, cancel.y, cancel.w, cancel.h)) {
        btnPressed_[kBtnCancel] = true;
    }
    return true; // de facto modal: consumes every click while the box is open
}

bool MsgBoxDialog::OnClick(int x, int y) {
    if (!bOpen_) return false;
    // UI_MsgBox_OnLButtonUp 0x5C0A90: confirms if released on the armed button, else cancels.
    Rect box, ok, cancel;
    Layout(lastScreenW_, lastScreenH_, box, ok, cancel);
    if (btnPressed_[kBtnOk] && PointInRect(x, y, ok.x, ok.y, ok.w, ok.h)) {
        Finish(kBtnOk);
    } else if (withCancel_ && btnPressed_[kBtnCancel] &&
               PointInRect(x, y, cancel.x, cancel.y, cancel.w, cancel.h)) {
        Finish(kBtnCancel);
    } else {
        btnPressed_[0] = btnPressed_[1] = false; // released outside button: disarm
    }
    return true; // modal
}

bool MsgBoxDialog::OnKey(int vk) {
    if (!bOpen_) return false;
    // Modal shortcuts: Enter -> OK, Escape -> Cancel (closes the box).
    if (vk == VK_RETURN) { Finish(kBtnOk); return true; }
    if (vk == VK_ESCAPE) { Finish(withCancel_ ? kBtnCancel : kBtnOk); return true; }
    return true; // modal: swallows every key while open
}

void MsgBoxDialog::Render(const UiContext& ctx, int cursorX, int cursorY) {
    // Stores the current screen dims so hit-testing (routed between two frames)
    // stays aligned with the drawn geometry. Done in both sub-passes.
    lastScreenW_ = ctx.screenW;
    lastScreenH_ = ctx.screenH;
    if (!bOpen_) return;
    Rect box, ok, cancel;
    Layout(ctx.screenW, ctx.screenH, box, ok, cancel);

    // --- Panels phase: background, frame, buttons ---
    if (ctx.phase == UiPhase::Panels) {
        ctx.FillRect(box.x, box.y, box.w, box.h, kColBg);
        ctx.DrawFrame(box.x, box.y, box.w, box.h, kColBorder, 2);

        const bool okHover = PointInRect(cursorX, cursorY, ok.x, ok.y, ok.w, ok.h);
        D3DCOLOR okCol = btnPressed_[kBtnOk] ? kColBtnDown : (okHover ? kColBtnHover : kColBtn);
        ctx.FillRect(ok.x, ok.y, ok.w, ok.h, okCol);
        ctx.DrawFrame(ok.x, ok.y, ok.w, ok.h, kColBorder, 1);

        if (withCancel_) {
            const bool caHover = PointInRect(cursorX, cursorY, cancel.x, cancel.y, cancel.w, cancel.h);
            D3DCOLOR caCol = btnPressed_[kBtnCancel] ? kColBtnDown : (caHover ? kColBtnHover : kColBtn);
            ctx.FillRect(cancel.x, cancel.y, cancel.w, cancel.h, caCol);
            ctx.DrawFrame(cancel.x, cancel.y, cancel.w, cancel.h, kColBorder, 1);
        }
        return;
    }

    // --- Text phase: title, body, button labels (centered) ---
    // Title centered at the top of the frame.
    const int titleW = ctx.MeasureText(title_.c_str());
    ctx.Text(title_.c_str(), box.x + (box.w - titleW) / 2, box.y + 14, kColTitle);
    // Body (single line, centered) — the skeleton does not handle line wrapping.
    const int bodyW = ctx.MeasureText(body_.c_str());
    ctx.Text(body_.c_str(), box.x + (box.w - bodyW) / 2, box.y + 56, kColText);
    // Button labels.
    const char* okLbl = "OK";
    const int okLblW = ctx.MeasureText(okLbl);
    ctx.Text(okLbl, ok.x + (ok.w - okLblW) / 2, ok.y + 6, kColText);
    if (withCancel_) {
        const char* caLbl = "Annuler";
        const int caLblW = ctx.MeasureText(caLbl);
        ctx.Text(caLbl, cancel.x + (cancel.w - caLblW) / 2, cancel.y + 6, kColText);
    }
}

// ===========================================================================
// UIManager
// ===========================================================================
UIManager& UIManager::Instance() {
    static UIManager s_instance;
    return s_instance;
}

bool UIManager::CreateWhiteTexture(IDirect3DDevice9* dev) {
    if (!dev) return false;
    IDirect3DTexture9* tex = nullptr;
    // MANAGED pool: automatically restored after a device Reset (no OnLost*).
    HRESULT hr = dev->CreateTexture(1, 1, 1, 0, D3DFMT_A8R8G8B8,
                                    D3DPOOL_MANAGED, &tex, nullptr);
    if (FAILED(hr) || !tex) {
        TS2_ERR("UIManager : CreateTexture(1x1 blanche) a echoue (0x%08lX)", hr);
        return false;
    }
    D3DLOCKED_RECT lr{};
    if (SUCCEEDED(tex->LockRect(0, &lr, nullptr, 0))) {
        *reinterpret_cast<uint32_t*>(lr.pBits) = 0xFFFFFFFFu; // opaque white ARGB
        tex->UnlockRect(0);
    }
    ctx_.whiteTex = tex;
    return true;
}

bool UIManager::Init(gfx::Renderer* renderer, gfx::SpriteBatch* sprites,
                     gfx::Font* font, HWND hwnd, int screenW, int screenH) {
    ctx_.renderer = renderer;
    ctx_.sprites  = sprites;
    ctx_.font     = font;
    ctx_.screenW  = screenW;
    ctx_.screenH  = screenH;
    hwnd_         = hwnd;

    if (renderer && renderer->Device())
        CreateWhiteTexture(renderer->Device());

    // Registers the built-in dialogs. MsgBox is a modal popup: it sits at the
    // HEAD of the registry (first consumer / rendered on top, like unk_1822438).
    dialogs_.clear();
    Register(&msgBox_);

    inited_ = true;
    TS2_LOG("UIManager initialise (%d dialogues, %dx%d)",
            static_cast<int>(dialogs_.size()), screenW, screenH);
    return true;
}

void UIManager::Shutdown() {
    // UI_DestroyAllDialogs 0x5AC270: teardown on shutdown.
    dialogs_.clear();
    if (ctx_.whiteTex) { ctx_.whiteTex->Release(); ctx_.whiteTex = nullptr; }
    inited_ = false;
}

void UIManager::Register(Dialog* dlg) {
    if (dlg) dialogs_.push_back(dlg);
}

bool UIManager::RouteMouseDown(int x, int y) {
    // UI_RouteLButtonDown 0x5AC740: "first consumer wins" chain. (The binary
    // gates certain special zones — chat tabs — behind the state guard
    // g_SceneMgr==6 && g_SceneSubState==4; the dialog chain itself always
    // runs, each dialog testing its own bOpen.)
    for (Dialog* d : dialogs_)
        if (d && d->OnMouseDown(x, y)) return true;
    return false;
}

bool UIManager::RouteMouseUp(int x, int y) {
    // UI_RouteLButtonUp 0x5AD0F0.
    for (Dialog* d : dialogs_)
        if (d && d->OnClick(x, y)) return true;
    return false;
}

bool UIManager::RouteKey(int vk) {
    // UI_RouteKeyInput 0x5ADF50.
    for (Dialog* d : dialogs_)
        if (d && d->OnKey(vk)) return true;
    return false;
}

// --- RIGHT click (gap UIFW-01) ----------------------------------------------
// UI_RouteRButtonDown 0x5AD5D0: 38 chained slots, first one returning 1 consumes
// (`test eax,eax / jz / mov eax,1 / jmp` — e.g. @0x5AD5E4 -> @0x5AD5ED for slot 0,
// down to the last one's `return ...(a1,a2) != 0` @0x5ADA8A). No head block (unlike
// UI_RouteLButtonDown 0x5AC740, whose chat block 0x5AC75B-0x5ACC01 precedes the
// chain): the router hits the dialogs directly, hence the bare loop below.
//
// WARNING: the STATE GUARD is NOT here — it lives in Input_OnRButtonDown 0x50ADB0 @0x50AE17
// (g_SceneMgr==6 && g_SceneSubState==4 && g_SelfActionState[0] in {11,12,33..37} => the right
// click is ENTIRELY swallowed, this router isn't even called). It is already
// reproduced, at its rightful layer, by App::RButtonGateOpen (App/App.cpp:1167).
bool UIManager::RouteRButtonDown(int x, int y) {
    for (Dialog* d : dialogs_)
        if (d && d->OnRButtonDown(x, y)) return true;
    return false;
}

bool UIManager::RouteRButtonUp(int x, int y) {
    // UI_RouteRButtonUp 0x5ADA90: symmetric chain (1st slot @0x5ADAA4, pattern
    // `test eax,eax / jz loc_5ADAB7 / mov eax,1 / jmp loc_5ADF4A`).
    for (Dialog* d : dialogs_)
        if (d && d->OnRButtonUp(x, y)) return true;
    return false;
}

void UIManager::Render() {
    if (!inited_) return;
    ctx_.gameTimeSec = gfx::g_GameTimeSec;

    // Cursor position -> client coordinates (UI_RenderAllDialogs 0x5AE2D0:
    // GetPhysicalCursorPos @0x5AE2DD then ScreenToClient(hWndParent 0x815184) @0x5AE2EE).
    //
    // TT-10 — GetPhysicalCursorPos, NOT GetCursorPos (the comment already cited the right
    // API @0x5AE2DD while the actual call was GetCursorPos: real divergence). The two only
    // coincide without DPI virtualization; on a non-DPI-aware 32-bit client under a scaled
    // screen, GetCursorPos returns LOGICAL coordinates and shifts all UI hit-testing +
    // tooltip anchoring. The binary is physical end-to-end.
    POINT p{};
    GetPhysicalCursorPos(&p);                                   // 0x5AE2DD
    if (hwnd_) ScreenToClient(hwnd_, &p);                       // 0x5AE2EE
    const int cx = p.x, cy = p.y;

    // Rendered in REVERSE order of the registry: background first, modal popups last
    // (= on top). Two independent sub-passes because the sprite batch (panels) and the
    // font batch (text) are two distinct ID3DXSprite objects.
    //
    // TODO [anchors 0x69E620 / 0x69E650 / 0x6A3080 / 0x69E750]: the binary does ONE SINGLE
    // Begin/End (Gfx_Begin2D/Gfx_End2D) on ONE SINGLE ID3DXSprite (g_GfxRenderer+608 =
    // dword_800078), blits and text INTERLEAVED in submission order — cf. the evidence
    // block above `enum class UiPhase` in UIManager.h (gap GX2D-01). The two sub-passes
    // below diverge from that order, but the fix requires merging Font::sprite_ and
    // SpriteBatch::sprite_, which live in Gfx/Font.{h,cpp} and Gfx/SpriteBatch.{h,cpp} —
    // out of scope for this front. NOT FIXED HERE, deliberately: LATENT divergence as
    // long as only one dialog is registered (cf. Init).

    // Pass 1: panels (sprite batch).
    if (ctx_.sprites && ctx_.sprites->Ready()) {
        ctx_.phase = UiPhase::Panels;
        if (SUCCEEDED(ctx_.sprites->Begin(D3DXSPRITE_ALPHABLEND))) {
            for (auto it = dialogs_.rbegin(); it != dialogs_.rend(); ++it)
                if (*it) (*it)->Render(ctx_, cx, cy);
            RunHoverChain(cx, cy);   // 0x5AE5C9 — after ALL draws of the sub-pass
            ctx_.sprites->End();
        }
    }

    // Pass 2: text (font batch).
    if (ctx_.font && ctx_.font->Ready() && ctx_.font->BeginBatch(D3DXSPRITE_ALPHABLEND)) {
        ctx_.phase = UiPhase::Text;
        for (auto it = dialogs_.rbegin(); it != dialogs_.rend(); ++it)
            if (*it) (*it)->Render(ctx_, cx, cy);
        RunHoverChain(cx, cy);       // 0x5AE5C9 — same, text side of the tooltip
        ctx_.font->EndBatch();
    }
}

// UIFW-03 — HOVER pass, mirrors UI_RouteRButtonExamine 0x5AE5E0 (called
// @0x5AE5C9, LAST call of UI_RenderAllDialogs 0x5AE2D0, right before `mov esp,ebp /
// pop ebp / retn` @0x5AE5CE). "First consumer wins" chain: the 1st dialog that
// draws its tooltip stops the pass (one tooltip per frame max, @0x5AE6B1 Shop,
// @0x5AE702 Warehouse, @0x5AE71D ItemListWin, @0x5AE738 StorageWin, @0x5AE76E NpcWin,
// @0x5AE7A4 cGameHud, @0x5AE7BF QuickSlot, @0x5AE8CD QuickBar, @0x5AE8E8 ConsumableBar).
//
// ORDER: the original chain follows the canonical routing order; dialogs_ is thus
// iterated FORWARD (like RouteMouseDown), NOT reversed like the draws.
//
// PLACEMENT — called at the end of EACH sub-pass, not a 3rd time after both
// (as the gap notes suggested): UiContext::FillRect only draws in the Panels
// phase (UIManager.cpp:18) and UiContext::Text only in the Text phase
// (UIManager.cpp:39), so a pass placed after both would draw NOTHING. The
// binary itself has only ONE 2D batch (Gfx_Begin2D @0x51B189 ... UI_RenderAllDialogs
// @0x51B59D ... Gfx_End2D @0x51B5A7 in Scene_LoginRender 0x51B020), where the
// tooltip is simply submitted last. Since cursor and state are identical in both
// sub-passes, the winner is deterministically the SAME dialog: the tooltip is
// therefore submitted last in each batch, hence on top. This is the most faithful
// transposition possible while the two-pass architecture holds (cf. TODO GX2D-01
// above `enum class UiPhase`).
void UIManager::RunHoverChain(int cx, int cy) {
    for (Dialog* d : dialogs_)
        if (d && d->OnHover(ctx_, cx, cy)) return;   // first consumer wins
}

void UIManager::ResetAll() {
    // UI_ResetAllDialogs 0x5AC3F0: neutral state on scene transitions. ~42 targets
    // (@0x5AC408-0x5AC589), WITHOUT exception — MsgBox included (@0x5AC430) — plus an
    // UNCONDITIONAL UI_FocusEditBox(&g_UIEditBoxMgr, 0) at the top (@0x5AC3FE). Closing
    // every registered dialog is therefore the faithful mirror here.
    for (Dialog* d : dialogs_) if (d) d->Close();
}

void UIManager::CloseAll() {
    // N-1 — UI_CloseAllDialogs 0x5AC590 (opening a window closes the others). This is NOT
    // ResetAll: its list is FIXED (~27 targets, @0x5AC5A2-0x5AC6D1) and deliberately leaves
    // MsgBox (dword_1822438), NoticeDlg, TextInput, ItemListWin, StorageWin, ClanWin,
    // NpcWin, and AutoPlay untouched — all absent from the list. Hence the
    // ClosedByCloseAll() filter: without it, opening a window would swallow the current
    // modal box (a real bug — the 3 current callers, ClanContextMenu.cpp:238,
    // StoragePwWindow.cpp:156, PartyWindow.cpp:135, all go through here).
    for (Dialog* d : dialogs_)
        if (d && d->ClosedByCloseAll()) d->Close();

    // EDIT focus is only released CONDITIONALLY (@0x5AC669):
    //   if (g_UIEditBoxMgr == 20) UI_FocusEditBox(&g_UIEditBoxMgr, 0);   // @0x5AC672
    // — unlike ResetAll's unconditional release (@0x5AC3FE). With no native EDIT focus
    // state modeled in UIManager (the 21 native EDIT controls of UI_CreateEditBoxes
    // 0x50E460 aren't wired up, cf. UI/Win32EditBox.h), there's nothing to do here.
    // TODO [anchor 0x5AC669]: port this once/if native EDIT focus gets wired up.

    // TODO [anchor 0x5AC59B]: the 2nd parameter of UI_CloseAllDialogs(this, a2) gates
    //   `cDrawWin_Close(dword_1839290)` @0x5AC5A2 + `cGameHud_Hide(dword_1839568)` @0x5AC5AC.
    // The 3 real callsites in the binary ALL pass a2=1 (UI_ClanWin_Open @0x5D8E50,
    // UI_StoragePwWnd_ProcNet @0x666F44, UI_MemberSelectWnd_ProcNet @0x6677C4): the HUD
    // SHOULD therefore be hidden here. NOT MODELABLE in UIManager, which owns neither the
    // HUD nor the DrawWin (they live in SceneManager: hud_). Deliberately NO
    // `alsoHudAndDrawWin` parameter added: nobody could honor it, it would just be a dead
    // button. To be handled on the Scene front (cf. front report, wiringTodo).
}

} // namespace ts2::ui

// UI/ServerSelectRender.h — VISUAL RENDERING of the ServerSelect screen (ts2::ui).
//
// Faithful rewrite of the real GEOMETRY (positions/dimensions) of:
//   Scene_ServerSelectRender      0x519250 (~1.3 KB)  — main body
//   ServerSelect_GetButtonX       0x519F40             — X column of a server button
//   ServerSelect_GetButtonY       0x51A0A0             — Y row of a server button
//   ServerSelect_GetButtonImageId 0x51A220             — sprite id (atlas unk_8E8B50) of a button
//   ServerSelect_DrawLoadBar      0x51A440             — load bar + "full" badge
//   UI_ProjectSpriteToScreen      0x50F5D0             — resolution-independent anchor of the back button
// decompiled via idaTs2 (HTTP JSON-RPC server http://127.0.0.1:13337/mcp, method
// "decompile", the `idaTs2` MCP not being exposed as a deferred tool in this session —
// SAME IDB as the MCP, no invented data).
//
// REAL ASSET WIRING (2026-07-14, cf. Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md, EA-by-EA
// proof) — this module now ACTUALLY loads the `.IMG` sprites from the shared atlas
// `g_AssetMgr_UiAtlasSlots` (same files `G03_GDATA\D01_GIMAGE2D\001\
// 001_%05d.IMG` as Sprite2D_BuildPath 0x4D68E0, confirmed +1 slot->file OFFSET) via
// GetSprite() (lazy cache asset::ImgFile::Load + gfx::GpuTexture::CreateFromImgFile,
// SAME pattern as UI/InventoryWindow.cpp::GetIconTex / UI/PanelSkin.h):
//   - full-screen background (random 2380/2381, this[168])         : doc §1.2
//   - central panel (slot 1785 -> 001_01786.IMG, unk_929344)       : §1.3
//   - load bar, 8 tiers + "unknown" badge (slots 1899..1907 -> 001_01900..
//     001_01908.IMG, unk_92D52C..unk_92D9CC)                       : §1.4/1.5
//   - "server full" badge (slot 2599 -> 001_02600.IMG, unk_9469DC) : §1.4/1.5
//   - bottom-right action button, 3 states (slots 4/5/6 -> 001_00005/6/7.IMG,
//     unk_8E8DA0/unk_8E8E34/unk_8E8EC8)                            : §1.5
// NO visual fallback: if a `.IMG` file can't be loaded at runtime (D3D9 device not
// ready or the file is genuinely missing), the corresponding sprite is simply
// NOT drawn — FAITHFUL behavior of Sprite2D_Draw/Sprite2D_DrawScaled (which display
// nothing if EnsureLoaded fails). The binary draws ONLY real `.IMG` sprites: no
// colored FillRect, no frame, no invented title/label text. All the files above
// are confirmed present on disk
// (ClientSource/TwelveSky2/GameData/G03_GDATA/D01_GIMAGE2D/001/). GetSprite() needs a
// valid D3D9 device: the caller MUST call SetDevice() once the device is created
// (LoginScene::Init, same pattern as InventoryWindow::Init(gfx::Renderer&)).
//
// ACTUALLY ACTIVE MODE (2026-07-14, full re-audit of the doc above, §2): with the
// documented launch command (`/0/0/2/1024/768`), g_ServerModeFlag is 0 —
// Scene_ServerSelectUpdate then builds ONLY ONE server, host
// "12sky2-login.geniusorc.com", port 8088 (0x1F98) — AND Scene_ServerSelectRender takes
// the `else` branch (EA 0x5194DD, singleServerMode=false below, NOT the "big number"
// UI_DrawNumberValue branch, which belongs to g_ServerModeFlag!=0). This `else` branch
// is a loop over server buttons, but with ONLY ONE entry built by Update, it only ever
// draws ONE button — this is therefore NO LONGER a multi-channel grid: the caller
// (UI/LoginScene.cpp::BuildServerList) now builds exactly THIS single entry (instead of
// the former 6 `MultiChannel` channels), which makes the loop below a FAITHFUL
// reproduction (no longer an "accepted compromise") of the path actually taken by the
// binary for this launch command. The `MultiChannel` mode (6 channels,
// `g_ServerModeFlag` != 0/1/2) remains documented and modeled in
// Game/ServerSelectFlow.h::ServerListMode/BuildServerList() as a future reference/
// option — it's no longer LoginScene's default path.
//
// === REAL GEOMETRY EXTRACTED (0x519250) ===
//
// Reference resolution = ts2::kRefWidth/kRefHeight (flt_1669178/flt_166917C =
// 1024x768). The full-screen background (this[168] = ServerSelectState::backgroundImageId,
// indexes the shared atlas unk_8E8B50, no 148-byte stride/entry — SAME atlas as the
// Intro logos and server buttons) is drawn AT SCALE (0,0) with:
//   scaleX = nWidth  / kRefWidth   (v13, EA 0x519435)
//   scaleY = nHeight / kRefHeight  (v14, EA 0x519419)
//
// The central panel (sprite unk_929344, slot 1785 -> 001_01786.IMG, ACTUALLY loaded
// via GetSprite(); texture's REAL dimensions used for centering when available —
// EXACTLY Sprite2D_GetWidth/Height(unk_929344) of the original —, falls back to the
// known real dimensions kPanelW/kPanelH (737x755, IMG header) for CENTERING only if
// loading fails — the panel is then not drawn, never replaced by a fallback fill)
// is CENTERED on screen:
//   baseX = nWidth/2  - panelW/2   (v24, EA 0x519486)
//   baseY = nHeight/2 - panelH/2   (v17, EA 0x5194A9)
// This is the origin (baseX, baseY) to which ALL offsets below are added
// (server buttons AND load bars share this same origin).
//
// Server buttons (ServerSelect_GetButtonX/Y 0x519F40/0x51A0A0): EXACT offsets
// captured in the disassembly's switch(id), id = index in the loop
// [selectedGroupBtnLo..selectedGroupBtnHi] (this[15372]/this[15373], EXACTLY the
// bounds of ServerSelectState::selectedGroupBtnLo/Hi). The ids observed in the 3
// layout functions are sparse: 0..9 (rows of a group, up to 10 servers, SINGLE
// column X=+291) and 60 (special button, column X=+539); 40 and 50 have NO case in
// GetButtonX (default -> offset 0, faithful to the original — edge-case behavior
// reproduced as-is). Ids 6..9 reuse the SAME Y rows as 0..3 (196/278ish/
// 378/469 vs 196/287/378/469 — slight 278 vs 287 divergence at case 7, EXACT per the
// disassembly, not a typo): these are likely buttons of ANOTHER group occupying the
// SAME screen space (only one group visible at a time, cf.
// ServerSelectFlow.h::selectedGroup); 40/50/60 are special slots (exact semantics
// UNCONFIRMED TODO — cf. kSpecialSlotXxx below) not modeled in
// ServerSelectState (no dedicated field), exposed here for completeness/documentation
// but NOT drawn by the main loop (which only iterates over
// ServerSelectState::servers indices, 0-based, firmly within the 0..9 range in
// practice — 6 entries in MultiChannel mode, 1 in SingleServer, cf. Game::BuildServerList).
//
// Load bars (ServerSelect_DrawLoadBar 0x51A440): 2 sprites per server, at the
// SAME origin (baseX,baseY) —
//   load bar (level badge, 8 stacked sprites unk_92D52C..unk_92D938, no
//     148-byte stride) at (baseX+barOffX, baseY+barOffY);
//   "full" badge (unk_9469DC, drawn ONLY if population >= maxPopulation)
//     at (baseX+fullOffX, baseY+fullOffY).
// Bar level: 8 tiers, threshold = loadStep * k (k=1..7), the loadStep value
// coming from this[id+13371] in the binary — FAITHFULLY modeled by the
// ServerEntry::loadStep field (added 2026-07-15), fed like maxPopulation by the
// server's status record (Net_QueryServerStatus 0x519CC0, bytes 9-12). LoadLevel()
// reproduces EXACTLY the pop >= k*loadStep comparison chain (pending population, <0,
// shows the dedicated unk_92D9CC badge).
//
// Back button (bottom of the screen, UI_ProjectSpriteToScreen 0x50F5D0, called with
// (imgId=4, refX=891, refY=701)): RESOLUTION-INDEPENDENT anchor distinct from the panel
// centering, reusing the same scale factor as the background (nWidth/kRefWidth,
// nHeight/kRefHeight) but WITHOUT screen centering — it's a corner/HUD anchor:
//   outX = Crt_ftol(scaleX * (891 + w/2)) - w/2
//   outY = Crt_ftol(scaleY * (701 + h/2)) - h/2
// where w/h = dimensions of atlas sprite[4] (NOT the sprite actually drawn afterward —
// unk_8E8DA0/unk_8E8E34/unk_8E8EC8, normal/hovered/pressed — faithful to the binary,
// which computes the position on one sprite and blits ANOTHER sprite at the resulting
// position, implicitly assuming they're the same size). CONFIRMED (Docs/TS2_LOGIN_BUTTON_
// ASSETS.md §"Noted exception"): slot 4/5/6 -> 001_00005/6/7.IMG, ACTUALLY loaded here
// (normal/hovered/pressed), nothing drawn if missing. State: this[3] of the
// SceneMgr, NOT modeled in ServerSelectState (the ServerSelect scene reuses this[3] for
// the back button latch, while Intro uses this[3..152] for logoFade — same raw
// memory, different interpretation per scene): latch handled HERE locally
// (cf. ServerSelectRender::OnActionButtonMouseDown/Up): the this[3] latch is armed on
// down and VALIDATED on up (OnActionButtonMouseUp returns true if the cursor is still
// on the sprite), which triggers the modal exit confirmation in LoginScene
// (Scene_ServerSelectOnMouseUp 0x519AC0 -> UI_MsgBox_Open dword_1822438 action_id=1 ->
// g_QuitFlag=1).
//
// Display mode (g_ServerModeFlag == dword_166918C, EXACTLY the same global as
// ServerSelectFlow.h::ServerListMode, address 0x166918C): if non-zero -> "single big
// number" panel (UI_DrawNumberValue, EA 0x53FCC0, on server 0 only); if
// zero -> button loop (singleServerMode=false below).
//
// RE-AUDIT (2026-07-14, full re-decompilation of Scene_ServerSelectRender 0x519250,
// EA 0x5194CB `if (g_ServerModeFlag)`, then full re-verification via
// Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md): the real test is a simple `if
// (g_ServerModeFlag)` in C. For THE LAUNCH COMMAND DOCUMENTED BY THIS PROJECT
// (`/0/0/2/1024/768` -> dword_166918C = 0, cf. WinMain EA 0x4609F1/0x460BAE):
//   - dword_166918C == 0 (ONLY ACTIVE CASE HERE) -> Update builds 1 SINGLE server (host
//     "12sky2-login.geniusorc.com", port 8088); Render takes the `else` branch
//     (EA 0x5194DD, singleServerMode=false) -> draws exactly THIS single button, its
//     load bar (9 real sprites) and the action button (3 real states). The binary NEVER
//     produces a multi-button grid for this command.
//   - dword_166918C == 1 or 2 -> 1 single server (port 8088, "EUTest" GameGuard variant);
//     Render takes the big-number branch (EA 0x519664, singleServerMode=true) —
//     kept in this module for the fidelity of the other path, but is NOT the path
//     taken by the documented launch command.
//   - dword_166918C == other (!=0,1,2) -> Update builds 6 channels (EA 0x518F6E) but
//     Render ALSO takes the big-number branch (same test) -> a single number, server 0;
//     the other 5 channels are NEVER drawn as buttons. This `MultiChannel` mode
//     remains modeled in Game/ServerSelectFlow.h::ServerListMode as a future
//     reference/option; it's NO LONGER what UI/LoginScene.cpp builds by default.
// CURRENT WIRING (UI/LoginScene.cpp::BuildServerList): now builds EXACTLY
// the single SingleServer entry above (host/port confirmed, cf. Net/Login.h::
// kLoginHostCom) and calls Render(..., singleServerMode=false) with this single entry
// bounded to 0..0 — the `else` loop below is therefore no longer a "compromise" but a
// FAITHFUL reproduction of the path actually active in the binary for the launch
// command documented by this project.
#pragma once
#include "UI/UIManager.h"          // ts2::ui::UiContext
#include "Game/ServerSelectFlow.h" // ts2::game::ServerSelectState
#include "Gfx/GpuTexture.h"        // gfx::GpuTexture (real background/buttons, atlas unk_8E8B50)
#include <cstdint>
#include <unordered_map>

namespace ts2::ui {

// Layout constants/tables — EXACT values extracted from the disassembly's
// switch(id) (see header comment). Grouped in a namespace so they can be
// reused/tested independently of the render class.
namespace serverselect_layout {

// EXACT sprite dimensions consulted via Sprite2D_GetWidth/Height in the IDB:
// - panel unk_929344: GetWidth 0x519486 / GetHeight 0x5194A9 -> 001_01786.IMG = 737x755.
// - server buttons: Sprite2D_HitTest 0x51958C/0x5199E6 on ButtonImageId(i) -> 153x23.
// - back button atlas[4]: UI_ProjectSpriteToScreen 0x5196D1/0x519A79/0x519AED -> 96x31.
// Used for fallback positioning/hit-testing when the texture hasn't loaded yet;
// Render() prefers the real runtime dimensions once GetSprite() succeeds.
constexpr int kPanelW    = 737;
constexpr int kPanelH    = 755;
constexpr int kButtonW   = 153;
constexpr int kButtonH   = 23;
constexpr int kBackBtnW  = 96;
constexpr int kBackBtnH  = 31;

// Atlas slots CONFIRMED by disassembly (Docs/TS2_SERVERSELECT_REAL_ASSET_IP.md +
// Docs/TS2_LOGIN_BUTTON_ASSETS.md, session 2026-07-14). 0-based index passed to
// ServerSelectRender::GetSprite() (which applies the +1 offset to the real
// file name `001_%05d.IMG` itself, cf. Sprite2D_BuildPath 0x4D68E0). All the
// corresponding files are confirmed present on disk
// (ClientSource/TwelveSky2/GameData/G03_GDATA/D01_GIMAGE2D/001/).

// Central panel (unk_929344): slot 1785 -> file 001_01786.IMG.
constexpr int kPanelImgSlot = 1785;

// Load bar (ServerSelect_DrawLoadBar 0x51A440): 8 increasing tiers
// (unk_92D52C..unk_92D938) -> files 001_01900.IMG..001_01907.IMG, THEN the
// "unknown population" badge (unk_92D9CC, pop < 0) -> file 001_01908.IMG.
constexpr int kLoadBarStepSlot[8] = {1899, 1900, 1901, 1902, 1903, 1904, 1905, 1906};
constexpr int kLoadBarPendingSlot = 1907; // pop < 0 (query in progress)

// "Server full" badge (unk_9469DC, pop >= maxPop) -> file 001_02600.IMG.
constexpr int kLoadBarFullSlot = 2599;

// Bottom-right action button (unk_8E8DA0/unk_8E8E34/unk_8E8EC8), 3 states ->
// files 001_00005.IMG / 001_00006.IMG / 001_00007.IMG (Docs/TS2_LOGIN_BUTTON_ASSETS.md
// §"Noted exception: ServerSelect does NOT use" the Login's OK/Quit pair).
constexpr int kActionBtnNormalSlot   = 4;
constexpr int kActionBtnHoverSlot    = 5;
constexpr int kActionBtnPressedSlot  = 6;

// "Special" ids observed in the 3 layout tables but absent from
// ServerSelectState (no dedicated field, semantics unconfirmed) — exposed for
// documentation/completeness, NOT drawn by ServerSelectRender::Render.
constexpr int kSpecialSlotA = 40; // GetButtonImageId -> 3452, GetButtonX -> default (0)
constexpr int kSpecialSlotB = 50; // GetButtonImageId -> 3099, GetButtonX -> default (0)
constexpr int kSpecialSlotC = 60; // GetButtonImageId -> 2630, own X column (+539)

// ServerSelect_GetButtonX 0x519F40: X offset (relative to baseX) of a server button by
// id. Default = 0, faithful to the binary (no explicit default case -> genuinely
// uninitialized result, but the function only declares `result` uninitialized for
// ids outside the table; we document 0 as a safe reproduction of the "default" path,
// which does exist explicitly in GetButtonX/Y/ImageId — DrawLoadBar has NO such
// default, cf. GetLoadBarOffsets below).
int ButtonOffsetX(int id);

// ServerSelect_GetButtonY 0x51A0A0: Y offset (relative to baseY) of a server button.
int ButtonOffsetY(int id);

// ServerSelect_GetButtonImageId 0x51A220: sprite id (atlas unk_8E8B50, no 148-byte
// stride) of the "released" button; the "hovered/active" button is the NEXT entry
// (+148 bytes = +1 index) in the atlas, cf. the Sprite2D_Draw(...+ v23 + 148, ...)
// call at EA 0x5195F6.
int ButtonImageId(int id);

// Offsets of the 2 badges drawn by ServerSelect_DrawLoadBar 0x51A440 (relative to
// baseX/baseY). `valid=false` for an id outside the explicitly handled 0..9/60 cases
// (the binary then reads UNINITIALIZED local variables — behavior that can't be
// faithfully reproduced; we draw nothing, safer than inventing a position).
struct LoadBarOffsets {
    int  barOffX = 0, barOffY = 0;   // level badge (unk_92D52C..unk_92D938 / pending unk_92D9CC)
    int  fullOffX = 0, fullOffY = 0; // "full" badge (unk_9469DC)
    bool valid = false;
};
LoadBarOffsets GetLoadBarOffsets(int id);

} // namespace serverselect_layout

// ServerSelectRender — draws the ServerSelect screen from a read-only
// game::ServerSelectState. NEVER modifies the state passed in. Holds a single
// PURELY visual state bit (back button latch, this[3] in the binary for this
// scene) — the real click logic stays in Game::ServerSelectFlow.h
// (OnServerClicked/OnGroupClicked/OnActionButtonReleased).
class ServerSelectRender {
public:
    // Must be called ONCE after the D3D9 device is created (UI/LoginScene.cpp::Init,
    // same pattern as InventoryWindow::Init(gfx::Renderer&)): without a device, GetSprite()
    // can't load any real texture and Render() will draw NOTHING (no sprite —
    // and never a fallback fill). `ctx.renderer` (UI/UIManager.h) is
    // deliberately NOT used here: LoginScene only owns a raw IDirect3DDevice9*
    // (no gfx::Renderer), exactly like UI/InventoryWindow.cpp/UI/PanelSkin.cpp.
    void SetDevice(IDirect3DDevice9* device) { device_ = device; }

    // Full rendering (background + panel + buttons/load bars + back button), ALL
    // drawn with the atlas's REAL sprites once SetDevice() has been called and the
    // files are present (cf. slot constants above); a sprite whose `.IMG` file can't
    // be loaded is simply NOT drawn (no colored fallback, no frame, no invented
    // text — faithful to Sprite2D_Draw).
    // `cursorX/cursorY`: CLIENT cursor position (like Dialog::Render — the binary
    // itself does GetPhysicalCursorPos+ScreenToClient at EA 0x5193E7/0x5193F8; here we
    // follow the UIManager::Render pattern which computes the cursor ONCE for all
    // elements and passes it down as a parameter, cf. UI/UIManager.h::Dialog::Render).
    // `singleServerMode`: reflects g_ServerModeFlag (see the file header note); false
    // by default = loop branch (EA 0x5194DD), the case ACTUALLY ACTIVE for the launch
    // command documented by this project, now bounded by the caller to the single
    // SingleServer entry (no more multi-channel grid).
    // Called twice per frame by the scene driver (once per UiPhase, like
    // Dialog::Render / MsgBoxDialog::Render); sprite drawing is internally filtered
    // to the Panels phase.
    void Render(const UiContext& ctx, const game::ServerSelectState& state,
                int cursorX, int cursorY, bool singleServerMode = false);

    // Action/exit button latch (mirrors this[3] for THIS scene only,
    // Scene_ServerSelectOnMouseDown/Up 0x519780/0x519AC0). To be called from the
    // ServerSelect scene's mouse routers. OnActionButtonMouseDown arms the latch if the
    // cursor is on the sprite (EA 0x519AAF: this[3]=1). OnActionButtonMouseUp disarms the
    // latch (EA 0x519AFE: this[3]=0) and returns true if the click is CONFIRMED (latch
    // armed AND cursor still on the sprite, Sprite2D_HitTest EA 0x519B1A) — the caller
    // then opens the exit confirmation (UI_MsgBox_Open, EA 0x519B3E).
    void OnActionButtonMouseDown(int cursorX, int cursorY, const UiContext& ctx);
    bool OnActionButtonMouseUp(int cursorX, int cursorY, const UiContext& ctx);

private:
    // Resolves an atlas slot from unk_8E8B50 (AssetMgr_InitAllSlots 0x4deb50, category 1 ->
    // "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG") to its GPU texture, lazy cache
    // (SAME pattern as UI/InventoryWindow.cpp::GetIconTex). +1 OFFSET CONFIRMED by
    // direct decompilation (Sprite2D_BuildPath 0x4d68e0 formats the file with
    // `slot+1`): the real file for slot `id` is 001_<id+1>.IMG, NOT 001_<id>.IMG —
    // verified by real content (ButtonImageId(i)+1 = a consistent 153x23 DXT3 button,
    // while ButtonImageId(i) itself points to an unrelated asset). Requires
    // device_ (cf. SetDevice above); returns nullptr (nothing drawn on the
    // caller side, never a fallback fill) if the device isn't ready or the file is
    // missing/unreadable.
    gfx::GpuTexture* GetSprite(int slotIndex);

    IDirect3DDevice9* device_ = nullptr; // cf. SetDevice() — required to load real .IMG files
    bool actionButtonPressed_ = false;
    std::unordered_map<int, gfx::GpuTexture> spriteCache_; // slot -> texture (lazy, category 1)
};

} // namespace ts2::ui

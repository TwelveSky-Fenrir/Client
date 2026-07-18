// UI/LoginScene.h — TwelveSky2 client login shell scenes (ts2::ui).
//
// Groups the three network-bootstrap scenes, wired onto the same state machine
// as the binary (cSceneMgr 0x1676180):
//   - ServerSelect (id 2) : Scene_ServerSelect* (0x518B30 / 0x519250 / 0x519780)
//   - Login        (id 3) : Scene_Login*        (0x51A8D0 / 0x51B020 / 0x51B5D0 / 0x51B780)
//   - CharSelect   (id 4) : Scene_CharSelect*   (0x51BD90 / 0x51CED0 / 0x520F40 / 0x522E50)
//
// Connection flow (Docs/TS2_CLIENT_SHELL.md §4):
//   ServerSelect --click--> Login --OK--> ConnectLoginServer + LoginRequest(op0x0B,
//   ver 106) --success--> CharSelect --Enter--> ConnectGameServer(op0x0B,
//   WSAAsyncSelect 0x401) --code 0--> EnterWorld.
//
// PRAGMATIC rewrite: a functional skeleton that compiles and DRAWS (colored fills via
// SpriteBatch + labels via Font) and drives the real network flow (Net/Login.h) on top of
// net::NetSystem, without reimplementing the 573 UI functions or the .IMG sprite loading
// (UI atlas unk_8E8B50) — real assets plug in via gfx::SetSpriteTextureLoader.
//
// Fields/buttons are ts2::ui widgets (EditBox/Button) manipulated EXCLUSIVELY through their
// public interface (setters/getters + events).
//
// Project Winsock convention: Net/NetSystem.h -> Net/NetClient.h puts <winsock2.h> BEFORE
// <windows.h>; included FIRST here to guarantee the order.
#pragma once
#include "Net/NetSystem.h"      // net::NetSystem / net::NetClient (winsock2 before windows)
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <functional>
#include <mutex>                 // std::mutex — protects the server state shared with the status thread
#include <string>
#include <thread>                // std::thread — server-status worker (Net_ServerStatusThread 0x518AB0)
#include <vector>

#include "UI/Widgets.h"         // ts2::ui::EditBox / ts2::ui::Button
#include "UI/ConfirmMsgBox.h"   // ts2::ui::ConfirmMsgBox (shared MsgBox dword_1822438, UI_MsgBox_* 0x5C08C0)
#include "UI/ServerSelectRender.h" // ts2::ui::ServerSelectRender (real ServerSelect geometry render)
#include "UI/IntroRender.h"        // ts2::ui::IntroRender (real Intro geometry render)
#include "UI/EnterWorldRender.h"   // ts2::ui::EnterWorldRender (real EnterWorld geometry render)
#include "Game/IntroFlow.h"        // ts2::game::IntroState (RenderIntro parameter)
#include "Game/EnterWorldFlow.h"   // ts2::game::EnterWorldFlowState (RenderEnterWorld parameter)
#include "Game/CharSelectFlow.h"   // ts2::game::CharSelectState/Host — faithful CharSelect flow
#include "Game/ServerSelectFlow.h" // ts2::game::ServerSelectState/Host — faithful ServerSelect flow (REAL list + status)
#include "Net/ServerStatusQuery.h" // ts2::net::QueryServerStatusLive — live population polling (ss-netconnect contract)
#include "Audio/AudioSystem.h"     // audio::SoundBuffer — front-end BGM Z000.BGM (Scene_ServerSelectUpdate 0x518BF7)
#include "Gfx/SpriteBatch.h"    // gfx::SpriteBatch, gfx::SetActiveSprite
#include "Gfx/Font.h"           // gfx::Font
#include "Gfx/GpuTexture.h"     // gfx::GpuTexture (real ServerSelect/Login background, atlas unk_8E8B50)
#include "Gfx/Renderer.h"       // gfx::Renderer — required by gfx::MeshRenderer::Init (CharSelect 3D preview)
#include "Gfx/CharPreview3D.h"  // gfx::CharPreview3D + MeshRenderer/ModelCache/MotionCache (Char_RenderModel 0x527020)
#include "Scene/SceneManager.h" // ts2::Scene
#include <memory>               // std::unique_ptr — ModelCache/MotionCache (non-copyable, argument ctor)
#include <unordered_map>

namespace ts2::ui {

// Login scene sub-states (this[1] of cSceneMgr — Scene_LoginUpdate 0x51A8D0).
enum class LoginSub {
    Init       = 0, // reset fields + focus ID
    Idle       = 1, // idle + GameGuard heartbeat (/30 frames)
    Trigger    = 2, // OK clicked -> switch to DoLogin
    DoLogin    = 3, // reads ID/PW, ConnectLoginServer + LoginRequest
    // NoticeWait = 4: NO case 4 in the real switch (falls into the
    // `default: return result;` of Scene_LoginUpdate — no-op every frame).
    // The real exit does NOT come from this function: it is driven by
    // UI_NoticeDlg_OnLButtonUp 0x5C03F0 (OK click on the notice), which for
    // ALL notices opened by Scene_LoginUpdate (this[4]=kind=2, confirmed by
    // disassembly — `push 2` before EVERY UI_NoticeDlg_Open call on the
    // singleton &byte_18225C8, without exception, including the empty
    // ID/PW notices) unconditionally executes, on the OK click:
    // Net_CloseSocket(&g_NetClient) + g_SceneMgr=2 (return to ServerSelect)
    // + g_SceneSubState=0 (EA 0x5C04DF-0x5C0502). In other words: closing
    // ANY notice shown from the Login screen ALWAYS returns to ServerSelect
    // and closes the socket — even for a simple empty field.
    // Faithfully reproduced via OpenNotice()'s close callback (cf.
    // DoLogin()) rather than an auto-return to Idle (old invented
    // behavior, cf. Docs/TS2_LOGINSCENE_AUDIT.md §3.6 — gap closed).
    NoticeWait = 4, // no-op every frame; exit driven by the notice's OK click (cf. above)
};

// The server list and each entry's status/population are now carried by the faithful module
// Game/ServerSelectFlow.h (game::ServerSelectState/ServerEntry): source of truth for the FLOW
// (BuildServerList/PollServerStatuses/UpdateServerSelect/OnServerClicked, mirroring
// Scene_ServerSelect* 0x518B30/0x519250/0x519780 + Net_ServerStatusThread 0x518AB0).
// LoginScene no longer defines a local ServerEntry (the old ts2::ui struct duplicated these
// fields) — cf. member serverState_ below.

// LoginScene — state machine for scenes 2/3/4. Wire into SceneManager: on
// Update/Render/OnLButton*, delegate to the methods below then apply
// PendingScene() (Change + ClearPending).
class LoginScene {
public:
    LoginScene() = default;
    ~LoginScene();
    LoginScene(const LoginScene&) = delete;
    LoginScene& operator=(const LoginScene&) = delete;

    // Creates the font + sprite batch from the device, builds the server list and resets the
    // login screen. `net` = shared network system (socket dword_8156A0 + dispatcher);
    // `notifyWnd` = window receiving async socket notifications (WM_USER+1).
    //
    // `serverModeFlag` = dword_166918C (g_ServerModeFlag) = GameConfig::buildVariant (1st
    // `/N/...` token of the command line, parsed by WinMain EA 0x4609F1/0x460BAE). Drives the
    // server table construction (BuildServerList, keyed on this flag, faithful to
    // Scene_ServerSelectUpdate 0x518B30). DEFAULT 0 = SingleServer mode, the ONLY mode active
    // for the documented command `/0/0/2/1024/768` — SceneManager can pass cfg.buildVariant to
    // cover non-zero builds (cf. "Points to watch" of the session doc).
    //
    // `renderer` (OPTIONAL, default nullptr) = the gfx::Renderer owned by SceneManager.
    // Required ONLY by the CharSelect 3D preview: gfx::MeshRenderer::Init() takes a
    // `gfx::Renderer&` (of which it only reads Device(), cf. Gfx/MeshRenderer.cpp), and there
    // is no way to build a Renderer from an IDirect3DDevice9* (Renderer::Init creates its OWN
    // device). Defaulted parameter so as not to break the single existing caller
    // (Scene/SceneManager.cpp:337), which is not a file of this front.
    // ⚠ AS LONG AS SceneManager DOES NOT PASS `&renderer` HERE, charPreviewReady_ stays false
    //   and the 4 Char_RenderModel calls (0x51D361/0x51D3CC/0x51D429/0x51D480) draw NOTHING.
    //   -> wiring TODO CHARSELECT_3D (cf. front report cs-render-2d).
    bool Init(IDirect3DDevice9* device, net::NetSystem* net, HWND notifyWnd,
              int screenW, int screenH, int serverModeFlag = 0,
              gfx::Renderer* renderer = nullptr);
    void Shutdown();

    void SetScreenSize(int w, int h) { screenW_ = w; screenH_ = h; }
    void OnDeviceLost();   // around IDirect3DDevice9::Reset
    void OnDeviceReset();

    // --- Per-scene dispatch (wired into SceneManager cases 2/3/4) ---
    void Update(ts2::Scene scene);
    void Render(ts2::Scene scene);

    // Renders the Intro screen (Scene::Intro), driven by SceneManager (ts2::game::IntroState
    // and its flow remain owned by SceneManager, cf. Game/IntroFlow.h — LoginScene drives NO
    // state here). Simply reuses the GPU resources already created by Init() (sprite batch +
    // font + white texture): LoginScene is initialized before the Intro in the bootstrap
    // (SceneManager::Init creates+Init()s login_ first), so these resources are already ready
    // when the Intro plays.
    void RenderIntro(const game::IntroState& state);

    // Renders the EnterWorld transition screen (Scene::EnterWorld), driven by SceneManager
    // (ts2::game::EnterWorldFlowState remains owned by SceneManager, cf.
    // Game/EnterWorldFlow.h — same decoupling as RenderIntro above). `zoneId` = the same value
    // passed to game::EnterWorldFlow_Update (originally dword_1675A9C). Reuses the GPU
    // resources already created by Init() (sprite batch + font + white texture) — see
    // UI/EnterWorldRender.h for the reproduced real geometry (Scene_EnterWorldRender 0x52C260).
    void RenderEnterWorld(const game::EnterWorldFlowState& state, int zoneId);

    void OnMouseDown(ts2::Scene scene, int x, int y);
    void OnMouseUp(ts2::Scene scene, int x, int y);

    // Keyboard input (relayed from App_WndProc: WM_CHAR -> OnChar, WM_KEYDOWN -> OnKeyDown).
    // Only meaningful in the Login scene.
    void OnChar(char c);
    void OnKeyDown(int vk);

    // Transition requested by the state machine (None = stay). SceneManager must consume it
    // after Update/OnMouse*: Change(PendingScene()) then ClearPending().
    ts2::Scene PendingScene() const { return pending_; }
    void       ClearPending()       { pending_ = ts2::Scene::None; }

private:
    // --- ServerSelect (scene 2) — faithful flow: Game/ServerSelectFlow.h drives the REAL
    // LIST (BuildServerList) and the population polling (PollServerStatuses via a worker
    // thread, cf. LaunchServerStatusThread); LoginScene routes input and draws.
    void ServerSelectUpdate();
    void ServerSelectRender();
    void ServerSelectOnMouseDown(int x, int y);
    void ServerSelectOnMouseUp(int x, int y);   // Scene_ServerSelectOnMouseUp 0x519AC0 (confirms exit)
    RECT ServerRowRect(int i) const;
    // Launches the server-status worker (faithful to CreateThread(Net_ServerStatusThread
    // 0x518AB0) on the Init->Idle sub-state transition): polls over bounded blocking TCP
    // (ts2::net::QueryServerStatusLive) OUTSIDE the render loop, publishes populations under
    // lock (serverMutex_). The render reads these int32 values "as they arrive" (curPop==-1 =
    // still polling).
    void LaunchServerStatusThread();

    // --- Login (scene 3) ---
    void LoginUpdate();          // Scene_LoginUpdate 0x51A8D0
    void LoginRender();          // Scene_LoginRender 0x51B020
    void LoginOnMouseDown(int x, int y); // Scene_LoginOnMouseDown 0x51B5D0
    void LoginOnMouseUp(int x, int y);   // Scene_LoginOnMouseUp   0x51B780
    void DoLogin();              // sub-state 3
    // Action kind=2 of UI_NoticeDlg_OnLButtonUp 0x5C03F0 (EA 0x5C04DF-0x5C0502): closes the
    // socket and returns to ServerSelect. Passed as `onClose` to OpenNotice() by DoLogin() for
    // the 4 notices it opens (cf. LoginSub::NoticeWait).
    void AbortLoginToServerSelect();
    void LayoutLogin(int px, int py);
    void DrawFieldValue(const EditBox& box, int tx, int ty);
    // Real input-field caret (Sprite2D_Draw(unk_8EA42C = slot 43 of the UI atlas) at
    // panel+textWidth+127, y — EA 0x51B34F (ID) / 0x51B445 (PW)). Drawn in the SPRITE batch
    // when the field is focused; falls back to a text caret "|" (Font batch, DrawFieldValue)
    // only if sprite slot 43 is unavailable (cf. CaretSpriteReady).
    void DrawFieldCaretSprite(const EditBox& box, int tx, int ty);
    bool CaretSpriteReady();     // true if the real caret sprite (slot 43) is loadable
    void ResetLoginFields();
    void SetFocus(int field);    // 0=none, 1=ID, 2=PW (dword_1668FC0)

    // --- CharSelect (scene 4) — faithful flow: Game/CharSelectFlow.h::CharSelectState/Host
    // drive the logic (sub-states/creation/deletion/entry); LoginScene now only routes input
    // and draws the current state (cf. Docs/TS2_CHARSELECT_AUDIT.md).
    void CharSelectUpdate();
    void CharSelectRender();
    void CharSelectOnMouseDown(int x, int y);
    void CharSelectOnMouseUp(int x, int y);
    void BuildCharSelectHost();  // builds charHost_ (called once from Init())
    void LayoutCharSelect();     // List screen (this[15714]==1)
    void LayoutCreateForm();     // create-form screen (this[15714]==2)
    void CharListRender();
    void CreateFormRender();
    // Draws the shared modal MsgBox (msgBox_) LAST for the active scene (UI_RenderAllDialogs
    // 0x5AE2D0). Hover cursor = PHYSICAL position (CharSelectCursorClient, like the binary
    // @0x5AE2DD). Called after all the rest of both ServerSelect's AND CharSelect's rendering.
    void RenderMsgBox();

    // --- CharSelect: EXACT paint order (Scene_CharSelectRender 0x51CED0) ---
    // background (Begin2D..End2D) -> 3D PASS -> 2D UI (Begin2D..End2D). The three helpers
    // below split the original function in THIS order; CharSelectRender() chains them.
    // Stacking background+UI into a single Begin2D and drawing the 3D elsewhere BREAKS the
    // order.
    void CharSelectRenderBg();       // Sprite2D_DrawScaled(atlas+148*this[15713],0,0,nW/1024,nH/768) @0x51D2AB
    void CharSelectRenderPreview3D();// 4x Char_RenderModel between End2D 0x51D2B5 and Begin2D 0x51D48A
    void CharSelectRenderUi2D();     // 2D screen dispatch (0x51D4CB): List / Form

    // LEFT detail panel of the List screen, ABSOLUTE origin (15,19).
    // GUARD: `cmp [ecx+0F58Ch], -1 ; jz loc_51DF0D` @0x51D7CA -> NOTHING if no slot is
    // selected (guard MISSING from the consolidated spec §8.3, re-proven by disassembly).
    void CharDetailPanelRender();
    // Column of 10 buttons (origin (nWidth-140, nHeight-301), pitch 37; #8/#9 ABSOLUTE).
    void CharButtonColumnRender();

    // Canonical 3-STATE button pattern (CONSECUTIVE slots n/n+1/n+2), reference EA
    // 0x51DF32-0x51DF9A (ENTER) — identical for the 10 buttons, without exception:
    //   if (latch)                          Draw(base+2 /*pressed*/, x, y);
    //   else if (HitTest(base /*normal*/))  Draw(base+1 /*hover*/, x, y);
    //   else                                Draw(base   /*normal*/, x, y);
    // ⚠ The hit-test ALWAYS targets the NORMAL sprite, even when another state is painted.
    void DrawTriStateSprite(int slotNormal, int x, int y, bool latched, int mouseX, int mouseY);
    // Sprite2D_HitTest 0x4D6C50 on an atlas slot: bounds >= left/top and < right/bottom.
    // Returns false if the sprite is not loadable (unknown dimensions -> no rect).
    bool AtlasHitTest(int slotIndex, int x, int y, int mouseX, int mouseY);

    // Reads an int32 from the RAW 10088-byte sheet of slot `slot` (net::g_CharRecords[slot]
    // == &unk_1669380 + 0x2768*slot). Needed because game::CharSlotInfo
    // (Game/CharSelectFlow.h, OUTSIDE this front) does NOT expose the fields the render reads:
    // +60 (dword_16693BC, level bonus) and +5708 (dword_166A9CC, 2nd rebirth tier) drive BOTH
    // 4-tier cascades of the List screen (@0x51D55C/0x51D572) and of the detail panel
    // (@0x51D7FA/0x51D815), and the 11 detail-panel fields
    // (+16/+88/+92/+100/+5432/+5484/+5488/+5568/+5572/+9408) are not exposed either. This is
    // EXACTLY the binary's source, which indexes these same globals flatly.
    // Returns 0 if `slot` is outside [0, net::kCharRecordCount).
    static int32_t CharRecI32(int32_t slot, int byteOffset);

    // UI_MeasureNumberText 0x53FCA0 + UI_DrawNumberValue 0x53FCC0 (bitmap font
    // unk_1685740): `x = cx - width/2` (idiom `movzx/cdq/sub/sar 1` @0x51D73F-0x51D747).
    void DrawNumberCentered(const char* text, int centerX, int y);

    // GetPhysicalCursorPos(&pt) @0x51D493 then ScreenToClient(hWndParent 0x815184, &pt)
    // @0x51D4A4 — DISTINCT from CursorClient() (GetCursorPos), used by the other scenes.
    // CharSelect hover is recomputed PER FRAME in the render from this live physical
    // position; NO hover index is cached in Update.
    POINT CharSelectCursorClient() const;

    // --- Modal notice (simplified NoticeDlg 0x5C0280/0x5C03F0) ---
    // `onClose` reproduces the action triggered by UI_NoticeDlg_OnLButtonUp on the OK click,
    // indexed by this[4] ("kind"). Notices opened by Scene_LoginUpdate ALL have kind=2 (cf.
    // LoginSub::NoticeWait comment) -> onClose must close the socket + return to ServerSelect;
    // nullptr (default) = plain close with no side effect (behavior of CharSelect notices,
    // kind not verified here, out of scope).
    void OpenNotice(const std::string& text, std::function<void()> onClose = nullptr);
    void CloseNotice(); // closes the notice and executes onClose (OK click / Enter / Escape)
    void RenderNotice();

    // --- Render / input helpers ---
    void  FillRect(int x, int y, int w, int h, D3DCOLOR color); // via the 1x1 white texture
    POINT CursorClient() const;
    void  CreateWhiteTexture();
    // Builds the REAL server table via game::BuildServerList(serverState_, mode) (mode derived
    // from serverModeFlag_). Replaces the old local construction + AddServer().
    void  BuildServerList();
    static std::string ConnectErrText(int code);   // kNet* codes -> real StrTable005 (game::Str)
    static std::string LoginErrText(int result);   // server code -> real StrTable005 (game::Str)

    // --- Real ServerSelect/Login background (atlas unk_8E8B50, cf. LoginScene.cpp) ---
    // Scene_ServerSelectUpdate 0x518B30 (EA 0x518C29-0x518C40): this[168] = 2380 or 2381
    // (Rng_Next()%2, 50/50) drawn ONCE per entry into ServerSelect; Scene_LoginRender 0x51B020
    // (EA 0x51B207) RE-READS THE SAME this[168] (shared scene memory, not reset between
    // ServerSelect and Login) -> Login ALWAYS shows the same background as the preceding
    // ServerSelect draw. Reproduced here via bgAtlasSlot_ (drawn once in Init(),
    // simplification: the original re-draws it on every (re)entry into ServerSelect, not
    // modeled by this skeleton, which has no distinct ServerSelect "Init" sub-state).
    gfx::GpuTexture* GetAtlasSprite(int slotIndex); // lazy-load + cache (nullptr on failure)
    void DrawFullscreenBg(int slotIndex); // real fullscreen texture (nothing if not loaded — zero fallback)

    // Wires the generic "Confirm"/"Cancel" sprite pair (slots 9/10 and 12/13 of the shared
    // pool unk_8E8B50 — Docs/TS2_LOGIN_BUTTON_ASSETS.md §4) onto a button pair, falling back to
    // the existing colored rect (SetFallbackColors/SetFallbackTexture) if the .IMG files are
    // genuinely missing/unreadable at runtime. Reused for Login (OK/Quit), CharSelect list
    // (Enter/Quit) and the delete confirmation (Yes/No) — the same sprite pair the binary
    // reuses in Scene_CharSelectRender and the modal dialogs (doc §5, confirmed xrefs).
    void ApplyConfirmCancelSkin(Button& confirmBtn, Button& cancelBtn);

    // --- Dependencies ---
    IDirect3DDevice9* device_    = nullptr;
    net::NetSystem*   net_       = nullptr;         // shared socket + dispatcher
    HWND              notifyWnd_ = nullptr;
    int               screenW_   = 1024;
    int               screenH_   = 768;

    gfx::SpriteBatch  sprites_;                 // 2D batch (colored fills + sprites)
    gfx::Font         font_;                    // GXD font (text)
    IDirect3DTexture9* whiteTex_ = nullptr;     // 1x1 white (modulated -> fills)
    ts2::ui::ServerSelectRender serverSelectRender_; // real ServerSelect render (binary positions/dimensions, cf. UI/ServerSelectRender.h)
    ts2::ui::IntroRender        introRender_;        // real Intro render (binary positions/dimensions, cf. UI/IntroRender.h)
    ts2::ui::EnterWorldRender   enterWorldRender_;   // real EnterWorld render (binary positions/dimensions, cf. UI/EnterWorldRender.h)

    // --- Login state ---
    LoginSub loginSub_ = LoginSub::Init;
    int      frame_    = 0;                     // this[2] (sub-state counter)
    int      focusField_ = 0;                   // dword_1668FC0
    EditBox  idBox_, pwBox_;                    // dword_1668FC4 / dword_1668FC8
    Button   okBtn_, exitBtn_, optBtn_;         // this[3] / this[4] / this[5]
    bool     shadowsEnabled_ = false;           // g_Opt_GfxDetailShadows 0x84DEF8
    std::string loggedUser_;                    // validated ID (shown in CharSelect)

    // --- Servers (faithful flow, cf. Game/ServerSelectFlow.h) ---
    // serverState_ = server table + populations + selection (mirrors the ServerSelect portion
    // of g_SceneMgr 0x1676180). serverHost_ = network/persistence callbacks: QueryServerStatus
    // wired to ts2::net::QueryServerStatusLive (ss-netconnect contract,
    // Net_QueryServerStatus 0x519CC0), SaveLastServer to config::Cfg_SaveLastServer.
    game::ServerSelectState serverState_;
    game::ServerSelectHost  serverHost_;
    int  serverModeFlag_ = 0;                   // dword_166918C (g_ServerModeFlag) = buildVariant:
                                                // drives BuildServerList (0 = SingleServer .com active;
                                                // 1/2 = SingleServer other host; else MultiChannel)

    // Server-status worker (faithful: CreateThread(Net_ServerStatusThread 0x518AB0)). Polls
    // populations over bounded blocking TCP OUTSIDE the render loop. serverMutex_ protects
    // concurrent access to the (int32) population fields of serverState_.servers.
    std::thread statusThread_;
    std::mutex  serverMutex_;
    bool        statusThreadLaunched_ = false;  // launched once (on the Init->Idle transition)

    // Front-end BGM (Scene_ServerSelectUpdate 0x518BF7: Snd_LoadOggToBuffers(
    // "G03_GDATA\D10_WORLDBGM\Z000.BGM", loop)). Loaded+played once, on the same Init->Idle
    // transition as the status worker; continuous loop covering both ServerSelect AND Login
    // (shared front-end). Ogg->PCM decoding via the callback wired in AudioSystem::Init
    // (Audio/OggVorbisDecoder).
    audio::SoundBuffer bgm_;

    // --- Real ServerSelect/Login background (atlas unk_8E8B50) ---
    int bgAtlasSlot_ = 2380;                     // this[168] (2380/2381, cf. GetAtlasSprite)
    // [A4] CharSelect background: the old `charBgSlot_` member, drawn ONCE in Init(), was
    // REMOVED. The `Rng_Next()%3` -> 2383/2384/2385 draw is INSIDE the Init block of
    // Scene_CharSelectUpdate (EA 0x51C23A `call Rng_Next`; 0x51C261/70/7F the 3 writes;
    // immediately followed by this[15714]=1 @0x51C28C and this[15715]=-1 @0x51C299): it is
    // therefore RE-DRAWN on EVERY scene entry, not once at boot. This draw is now carried by
    // game::CharSelectState::backgroundSlot (RunInitBlock); LoginScene READS it (wiring TODO
    // CHARBG_SLOT of Game/CharSelectFlow.h:480 — CLOSED here).
    // Drawing a 2nd time here would ALSO shift the shared PRNG stream (net::DefaultRng), on
    // which network nonces depend: a double fidelity defect.
    std::unordered_map<int, gfx::GpuTexture> atlasCache_; // slot -> texture (lazy)

    // --- CharSelect 3D preview (Char_RenderModel 0x527020, 4 call sites) ---
    // These members are PERSISTENT (caches + device objects): this is precisely what earlier
    // passes' 3D-preview TODOs called "blocked, requires owned members". LoginScene.h
    // belonging to this front, the blocker is lifted.
    gfx::Renderer*   gfxRenderer_ = nullptr;   // supplied by Init() (cf. wiring TODO CHARSELECT_3D)
    gfx::MeshRenderer            charMesh_;    // 76-byte decl + skinned shaders
    std::unique_ptr<gfx::ModelCache> charModels_;  // C%03d%03d%03d.SOBJECT (ctor: MeshRenderer&)
    // ⚠ SAME MotionCache for the DRAWN PALETTE and for the FRAME COUNT returned to
    // host.GetMotionFrameCount: in the binary it's the SAME g_ModelMotionArray 0x8E8B30 used
    // for both (PcModel_ResolveEquipSlot @0x52705F/0x527544 and PcModel_ResolveSlotAndApply
    // @0x51c555). Two caches would diverge on missing motions. Same discipline as
    // Scene/WorldRenderer.cpp::Motions().
    std::unique_ptr<gfx::MotionCache> charMotions_;
    bool charPreviewReady_ = false;            // charMesh_.Init() succeeded AND a renderer was supplied

    // --- CharSelect (faithful flow, cf. Game/CharSelectFlow.h) ---
    game::CharSelectState charState_;  // sub-state/screen/slots/form/preview
    game::CharSelectHost  charHost_;   // network/UI callbacks (built by BuildCharSelectHost)

    // List screen: slots (click = selection), Create/Delete/Enter/Quit.
    Button enterBtn_, backBtn_;        // enterBtn_ = ENTER (16/17/18); backBtn_ = BACK (963/964/965)
    Button createBtn_, deleteBtn_;     // CREATE (19/20/21); DELETE (22/23/24)
    Button restoreBtn_;                // slots 3086/3087/3088 @ (x0, y0-37), EA 0x51E34F
    // QUIT (slots 25/26/27 @ (x0, y0+222), EA 0x51E2AA). Used to be hit-tested "by hand" on
    // the sprite rect in CharSelectOnMouseUp, without a latch: the binary, for its part,
    // latches it like the others (`cmp dword ptr [ecx+24h], 0` = this[9] @0x51E288) and thus
    // does paint its PRESSED state (slot 27). Dedicated widget -> same 3-state pattern as the
    // other 9.
    Button quitBtn_;

    // Create-form screen: 5 +/- pairs (job/faction/face/hair color/variant), typed name
    // (EditBox reused from Widgets.h), Confirm/Cancel.
    Button  jobMinusBtn_, jobPlusBtn_, factionMinusBtn_, factionPlusBtn_;
    Button  faceMinusBtn_, facePlusBtn_, hairMinusBtn_, hairPlusBtn_;
    Button  variantMinusBtn_, variantPlusBtn_;
    Button  createConfirmBtn_, createCancelBtn_;
    EditBox createNameBox_;

    // Create-preview 3D ROTATION buttons (slots 44/45 and 46/47, projected by
    // UI_ProjectSpriteToScreen 0x50F5D0 at world coords (390,628)/(557,628)). Latches this[15]
    // (+0x3C) / this[16] (+0x40): STICKY — never cleared during the Active state (no clear in
    // Update/OnMouseDown/OnMouseUp), ONLY by the scene's Init (150-latch loop @0x51BE83) =
    // charHost_.ClearAllButtonLatches. A left click -> continuous rotation +3°/frame
    // (@0x51CDE8) until scene re-entry; a right click adds -3° (@0x51CE09) -> +3-3=0 = a hard
    // stop. Do NOT model via Button (armed_ self-clears).
    bool rotLeftLatched_  = false; // this[15]
    bool rotRightLatched_ = false; // this[16]

    // Shared modal MsgBox (dword_1822438): DELETE confirmation (CharSelect,
    // host.ShowDeleteConfirm, type 2) AND EXIT confirmation (ServerSelect, action button, type
    // 1). A single object reused by the binary — a single member here (UI/ConfirmMsgBox.h).
    // Replaces the old deleteConfirmOpen_/exitConfirmOpen_ members + their 4 invented Yes/No
    // buttons.
    ui::ConfirmMsgBox msgBox_;

    // --- Notice ---
    bool        noticeOpen_ = false;
    std::string noticeText_;
    std::function<void()> noticeOnClose_; // kind=2 action (cf. OpenNotice); empty = no-op
    // [A3] TYPE of the CharSelect notice (2nd argument of UI_NoticeDlg_Open 0x5C0280). THIS is
    // what decides whether the Locked sub-state is a dead end (mode 1 = Close: the scene stays
    // where it is) or an EXIT to ServerSelect (mode 2 = Disconnect: UI_NoticeDlg_OnLButtonUp
    // 0x5C03F0 case 2 -> Net_CloseSocket 0x5C04DF, g_SceneMgr=2 0x5C04E4, g_SceneSubState=0
    // 0x5C04EE, dword_1676188=0 0x5C04F8).
    // Without this field, Locked was a PERMANENT DEAD END (the scene's 4 handlers are gated
    // `==1` and never see this click — it arrives via UI_RouteLButtonUp 0x5AD0F0, sole xref EA
    // 0x5AD164). Populated by charHost_.ShowNoticeTyped.
    game::NoticeType noticeType_ = game::NoticeType::Close;

    // --- Requested transition ---
    ts2::Scene pending_ = ts2::Scene::None;
};

} // namespace ts2::ui

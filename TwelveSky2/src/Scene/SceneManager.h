// Scene/SceneManager.h — client scene state machine = cSceneMgr (object dword_1676180).
// Dispatches cSceneMgr_Update 0x517BF0 / _Render 0x517CB0 on the scene id.
// See Docs/TS2_CLIENT_SHELL.md §2.1.
//
// Owns the shell scenes (LoginScene = ServerSelect/Login/CharSelect) and the
// in-game HUD (GameHud). Lightweight header: concrete scenes are held by pointer
// (unique_ptr) to avoid pulling winsock2/d3dx9 into every includer.
#pragma once
#include <memory>
#include <string>
#include "Game/IntroFlow.h"      // lightweight (STL only): IntroState held by value
#include "Game/EnterWorldFlow.h" // same: EnterWorldFlowState held by value
#include "Game/InGameTickFlow.h" // same: InGameTickFlowState held by value (Scene_InGameUpdate)

struct IDirect3DDevice9; // COM (global) — avoids including d3d9.h here

namespace ts2 {

// WorldRenderer (Scene/WorldRenderer.h) pulls in d3d9/d3dx9 (MeshRenderer/Font): held
// by pointer and forward-declared here, like login_/hud_/windows_, to avoid
// weighing down includers of this lightweight header.
class WorldRenderer;

namespace gfx { class Renderer; class Camera; class WorldGeometryRenderer; class ModelObjectRenderer; }
namespace net { class NetSystem; }
namespace ui  { class LoginScene; class GameHud; class GameWindows; }
namespace world { class WorldAssets; class WorldMap; }
// Scene BGM slot (Audio/AudioSystem.h): forward-declared + held by pointer to
// avoid pulling dsound.h into this lightweight header (like login_/hud_/world_).
namespace audio { class BgmChannel; }

enum class Scene {
    None         = 0,
    Intro        = 1,  // Scene_IntroUpdate 0x517FE0: INTRO.AVI splash + logo fades
    ServerSelect = 2,  // 0x518B30: server list (hardcoded or via status thread)
    Login        = 3,  // Scene_LoginUpdate 0x51A8D0: credential entry + handshake
    CharSelect   = 4,  // character selection (UI_CharListWnd)
    EnterWorld   = 5,  // entering-world transition
    InGame       = 6,  // in game; also calls AutoPlay_Update
};

// Mirrors the +4 field of the cSceneMgr 0x1676180 object, i.e. g_SceneSubState 0x1676184 =
// current scene sub-state. Exposed as a GLOBAL — not a private member — because
// that's how the binary consumes it: Camera_UpdateFromInput 0x50B7D0
// DIRECTLY reads both globals in its entry guard @0x50B7EC
//   if ( g_SceneMgr != 6 || g_SceneSubState != 4 ) return;
// never receiving the sub-state as a parameter (see App/PlayerInputController.cpp).
//
// WARNING (verified pitfall): SceneManager::subState_ (private, below) does NOT carry this
// value in the InGame scene — it is only ever written to 0. The real InGame sub-state lives
// in inGameTickState_ (game::InGameTickState, Game/InGameTickFlow.h); the EnterWorld one lives in
// enterWorldState_. THIS is the global that SceneManager::Update keeps in sync from these two
// state machines (sync points commented in the .cpp). // 0x1676184
extern int g_SceneSubState;

// ---------------------------------------------------------------------------
// SCN-01 — OK button action of the notice dialog (byte_18225C8).
// Reproduces `switch (*(this+4))` @0x5C04C9 from UI_NoticeDlg_OnLButtonUp 0x5C03F0, executed
// right after UI_NoticeDlg_Close (@0x5C04A5):
//   1 -> nothing · 2 -> Net_CloseSocket + return to ServerSelect (@0x5C04DF/@0x5C04E4)
//   3 -> "[ABNORMAL_END] ( 3 )" + g_QuitFlag=1 (@0x5C0516/@0x5C051B)
//   4..9 -> Net_SendOp44/48/54/66/73/60 (@0x5C0531..@0x5C0586)
// Lives HERE (not in the UI front-end) because its two structuring actions are
// SCENE STATE changes, owned by this module: the return to scene 2 is queued
// and consumed by SceneManager::Update, like sceneEnterWorldPending.
//
// The Yes/No types of the MsgBox registry (8/9/10/14/19/20) are IGNORED here: they are
// already emitted by UI/GameWindows.cpp::SyncPrompt (Net_SendOp45/49/67/74/55/61). The two
// tables overlap in VALUE but not in SEMANTICS — see the detailed banner in the .cpp.
//
// EXPECTED CALLER (wiring to be added by the orchestrator, file NOT owned by this front-end):
// UI/GameWindows.cpp:211-216, in the OK callback of MsgBox().Open, which currently carries the
// TODO [ancre 0x5C04DF] and just does `game::g_Client.prompt.Close();`. Add there:
//     ts2::Notice_DispatchOkAction(type);   // 0x5C04C9 (before the existing prompt.Close())
// (`type` is already captured by the lambda there; include "Scene/SceneManager.h").
void Notice_DispatchOkAction(int type); // 0x5C03F0 / switch @0x5C04C9

// Header of the cSceneMgr object: [+0 id][+4 sub-state][+8 frame counter][+12 150-dword buffer][+612 BGM slot].
class SceneManager {
public:
    SceneManager();
    ~SceneManager();
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    // cSceneMgr_Init 0x517AF0: creates LoginScene + HUD from the device/network.
    // renderer/net must already be initialized (device created, WSAStartup done).
    // `gameDataDir`: GameData root (App::gameDataDir_, see App::ResolveGameDataDir) —
    // needed by world::WorldAssets to load Z%03d.WO (static geometry, see
    // Gfx/WorldGeometryRenderer.h).
    // `serverModeFlag` = GameConfig::buildVariant (1st cmdline token, g_ServerModeFlag
    // dword_166918C): drives ServerSelect mode (0 = SingleServer 8088). Passed to
    // LoginScene::Init (conditional BuildServerList). Defaults to 0 (documented launch).
    void  Init(gfx::Renderer& renderer, net::NetSystem& net, void* notifyHwnd,
               int screenW, int screenH, const std::string& gameDataDir,
               int serverModeFlag = 0);
    void  Shutdown();

    void  StartIntro();              // cSceneMgr_StartIntro 0x517B80
    // cSceneMgr_Update 0x517BF0. `camera`: App's 3rd-person camera (gfx::Camera), received
    // HERE as MUTABLE (unlike Render() which receives it as const) — needed for the
    // REAL wiring of InGame_InitCamera/Camera_UpdateCollision (Scene::InGame case, see
    // Gfx/CameraThirdPersonBridge.h): the SAME instance passed to Render(),
    // otherwise the target tracking computed here would have no visible effect on rendering.
    // MINIMAL signature addition (same policy already justified for Render() below).
    void  Update(double dt, gfx::Camera& camera);
    // cSceneMgr_Render 0x517CB0. `camera`: App's 3rd-person camera (gfx::Camera),
    // needed for world rendering (InGame case) for the view/projection matrices —
    // absent from the original cSceneMgr_Render (App did not yet carry a separate
    // camera); MINIMAL signature addition for WorldRenderer wiring.
    void  Render(IDirect3DDevice9* device, const gfx::Camera& camera);
    void  OnLButtonDown(int x, int y);
    void  OnLButtonUp(int x, int y);
    void  OnChar(char c);            // WM_CHAR (login/chat input)
    void  OnKeyDown(int vk);         // WM_KEYDOWN
    void  Change(Scene s);
    Scene Current() const { return scene_; }

    // --- In-game text input (GAP-APPLIFE-02) ---------------------------------
    // UI_Chat_FocusInput 0x68B200: gives focus to the chat input box.
    // Reproduces the entry guard @0x68B217 `if (g_SceneMgr == 6 && g_SceneSubState == 4)`
    // — outside this state, the original does NOTHING (no focus). Returns true if
    // focus was actually taken.
    // Its ONE original caller is App_WndProc 0x461930 @0x461B5E (`if (a3 == 13)`,
    // the WndProc's only keyboard handling): so it's App/App.cpp's job to call it — see
    // RouteTextInputKey below, which packages this call with focus arbitration.
    bool  FocusChatInput();          // 0x68B200
    // WM_KEYDOWN keyboard entry point for text input, to be called from
    // App::HandleMessage BEFORE the `scene_.Current() != Scene::InGame` restriction
    // (App/App.cpp) that reserves OnKeyDown for the DIK path. Returns true if the key was
    // CONSUMED by an input field (the caller must then not propagate it).
    //
    // Reproduces the binary's REAL arbitration, which is NOT "chat first":
    //   - UI_EditBoxWndProc 0x50E070: when a native EDIT has focus, IT receives
    //     WM_KEYDOWN and eats the key (case 4: `wParam==13` -> UI_Chat_SubmitInput
    //     0x68B330 @0x50E1D6) — the main window then sees nothing;
    //   - otherwise the main window receives WM_KEYDOWN and VK_RETURN opens the chat
    //     (App_WndProc @0x461B5E -> UI_Chat_FocusInput 0x68B200).
    // ClientSource has no live native EDIT (in-game input is the
    // custom-drawn widget ui::ChatWindow): this routing IS the faithful equivalent of 0x50E070.
    bool  RouteTextInputKey(int vk); // 0x50E070 / 0x461B5E

    // D3D9 device loss/restoration (around a Reset()).
    void  OnDeviceLost();
    void  OnDeviceReset();

private:
    // Applies the transition requested by LoginScene (PendingScene) and handles
    // entering the game (init HUD the 1st time).
    void  ConsumePending();

    // UIFW-08 — mouse-input action-state gate, reproduced from the 4 entry points
    // Input_OnLButtonDown 0x50AC90 (@0x50ACF7) / Input_OnLButtonUp 0x50AD20 (@0x50AD87) /
    // Input_OnRButtonDown 0x50ADB0 (@0x50AE17) / Input_OnRButtonUp 0x50AE40, all guarded
    // by the SAME test:
    //   if ( (g_SceneMgr != 6 || g_SceneSubState != 4
    //      || g_SelfActionState[0] != 11 && != 12 && != 33 && != 34 && != 35 && != 36 && != 37)
    //     && !UI_RouteLButtonDown(...) )  cSceneMgr_OnLButtonDown(...);
    // In other words: in game (scene 6 / sub-state 4) AND g_SelfActionState[0] 0x1687328 in
    // {11,12,33,34,35,36,37}, the click is TOTALLY swallowed — neither UI nor world. Returns true
    // in that case. // 0x50ACF7 / 0x50AD87
    bool  InputSwallowedByActionState() const;

    // --- Scene BGM slot (cSceneMgr +612: cSceneMgr_ReinitBgm 0x517A80 /
    //     SceneMgr_ReleaseSoundBuffers 0x517B60). ---
    // Loads+plays the zone's .BGM (World_LoadZoneResource 0x4DCB60 case 12,
    //   "G03_GDATA\D10_WORLDBGM\Z%03d.BGM") into the slot, looping if the
    //   g_BgmEnabled (0x84DEF0) option is active. Guarded if fileId unknown / file missing.
    void  LoadZoneBgm(int zoneId);
    // Snd_ReleaseBuffers 0x6A80D0 (SceneMgr_ReleaseSoundBuffers 0x517B60) on the slot.
    void  ReleaseBgm();

    // RE-ENTRANT zone reload (warp / op 0x18 Pkt_GameServerConnectResult 0x469CF0
    // -> g_SceneMgr=5). Replays the Scene_EnterWorldUpdate 0x52BFF0 case 1 cycle
    // (World_LoadZoneResource 0x4DCB60 idx 1..12) + rebuilds .WO geometry + BGM on a
    // NEW zone, WITHOUT re-initializing the renderer (lifts the one-shot worldGeomReady_/
    // LoadZoneBgm guards). Consumes sceneReloadPending/pendingWarpZoneId (GameState.h). // 0x52BFF0
    void  ReloadZone(int zoneId);

    Scene scene_      = Scene::None; // +0
    int   subState_   = 0;           // +4
    int   frameCount_ = 0;           // +8
    game::IntroState       introState_;      // faithful state machine Scene_IntroUpdate (279 frames)
    game::EnterWorldFlowState enterWorldState_; // faithful state machine Scene_EnterWorldUpdate
    game::InGameTickFlowState inGameTickState_; // faithful state machine Scene_InGameUpdate 0x52C600 (InGame scene)

    std::unique_ptr<ui::LoginScene>   login_;       // scenes 2/3/4
    std::unique_ptr<ui::GameHud>      hud_;          // HUD scene 6
    std::unique_ptr<ui::GameWindows>  windows_;      // in-game windows scene 6 (Warehouse/Guild/...)
    std::unique_ptr<WorldRenderer>    world_;        // 3D rendering of entities scene 6 (Scene/WorldRenderer.h)
    // Model-object mesh renderer (Wave F, Gfx/ModelObjectRenderer.h): draws combat FX
    // meshes (block/parry/deflect, MiscC bank) via the s_meshDraw hook. ModelObj_Draw 0x4D71B0.
    std::unique_ptr<gfx::ModelObjectRenderer> modelObjRenderer_;
    // Static world geometry (.WO chunk, see Gfx/WorldGeometryRenderer.h) — DISTINCT from
    // `world_` (WorldRenderer = player/monster entities). worldAssets_/worldMap_ load
    // Z%03d.WO ONLY (not WM/WJ/WG/atmosphere/sound, out of scope for this wiring) via the
    // World/WorldIntegration.h hooks; worldGeom_ uploads+draws its GPU content.
    std::unique_ptr<world::WorldAssets>       worldAssets_;
    std::unique_ptr<world::WorldMap>          worldMap_;
    std::unique_ptr<gfx::WorldGeometryRenderer> worldGeom_;
    // Scene BGM slot = SoundObj sub-object at cSceneMgr +612 in the original
    //   (cSceneMgr_ReinitBgm 0x517A80). Held by pointer here (lightweight header).
    std::unique_ptr<audio::BgmChannel>        bgm_;
    gfx::Renderer* renderer_ = nullptr;
    net::NetSystem* net_     = nullptr;
    void* notifyHwnd_ = nullptr;
    int   screenW_ = 1024, screenH_ = 768;
    std::string gameDataDir_;
    bool  hudReady_ = false;
    bool  windowsReady_ = false;
    bool  worldReady_ = false;
    bool  worldGeomReady_ = false;
};

} // namespace ts2

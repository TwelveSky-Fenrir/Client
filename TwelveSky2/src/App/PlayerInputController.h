// App/PlayerInputController.h — per-frame in-game keyboard controller.
//
// Faithful mirror of Camera_UpdateFromInput 0x50B7D0 (object g_CameraCtrl 0x1668F60,
// initialized by mINPUT Camera_Init 0x50ABC0). Ground truth = TwelveSky2.exe disassembly.
// Called 1x/frame (App_FrameTick 0x462619), right after the keyboard poll
// (Input_AcquireKeyboard 0x46260F) and BEFORE the cSceneMgr_Update fixed-step loop.
// PRECISION (disassembly 0x4625D0, corrected in W5b): the call IS gated by the upstream
// ACCUMULATOR guard @0x4625FD (`delta < flt_815188` = 0.0333 -> jmp loc_4626DC, skip
// everything), but it is OUTSIDE the FIXED-STEP loop, whose head is loc_462623 @0x462623
// (`mov ecx,1` / `test ecx,ecx` / `jz loc_46267D`) and which contains cSceneMgr_Update
// @0x46263B looping back via @0x46267B. Consequence: gated 1x per FRAME, never 1x per
// fixed step -- unlike cSceneMgr_Update, which can run multiple times.
//
// Role: reads immediate keyboard state (InputSystem::KeyState) + buffered
// events (FirstKeyDownDik), derives player WASD movement (sent to the
// server via the EXISTING builder Net_SendCmd_251 0x592870 / opcode 251),
// camera/quickslot toggles, F12 screenshot, and routes leftover keys
// to cSceneMgr_OnKeyDown 0x517F80.
//
// g_CameraCtrl 0x1668F60 layout (offsets proven — Camera_Init 0x50ABC0 +
// Input_ResetMouseState 0x50E000):
//   +0  [0] int   homeToggle  = 1     (case 199)          Camera_Init 0x50ABC0
//   +4  [1] int   pgupCycle   = 1     (case 201)          Camera_Init 0x50ABC0
//   +8  [2] int   mouseLook   = 0     (movement gate)     Input_ResetMouseState 0x50E000
//   +12 [3] int   mode        = 0     (camera mode 1/2/3) Input_ResetMouseState 0x50E000
//   +16 [4] char[] lockName   = ""    (case 38, target lock)
//   +32 [8] int   zoomIndex   = 0     (cases 44/45/46)    Input_ResetMouseState 0x50E000
//   +36 [9]  float speed[0]   = 20.0  (forward/back W/S)  Input_ResetMouseState 0x50E000
//   +40 [10] float speed[1]   = 5.0   (yaw step A/D mode1) Input_ResetMouseState 0x50E000
//   +44 [11] float speed[2]   = 5.0   (strafe step Q/E)   Input_ResetMouseState 0x50E000
//   +48 [12] float speed[3]   = 5.0   (vertical step R/F) Input_ResetMouseState 0x50E000
//   +52..+88: mouse/zoom state -- ALREADY reified in gfx::Camera (Camera.h), not duplicated here.
#pragma once

#include "Input/InputSystem.h"   // input::InputSystem (KeyState/IsKeyDown/FirstKeyDownDik)
#include "Gfx/Camera.h"          // gfx::Camera (Forward/Orbit/kDegToRad)
#include "Scene/SceneManager.h"  // enum class ts2::Scene (gate InGame)
#include <functional>
#include <cstdint>

namespace ts2 {

// Forward-decl (keeps the .h light: the full NetClient definition is pulled in
// by the .cpp via Net/SendPackets.h -> Net/NetClient.h).
namespace net { struct NetClient; }

// Control state mirroring g_CameraCtrl 0x1668F60 (CONTROL fields only;
// mouse/zoom fields +52..+88 live in gfx::Camera). Defaults = Camera_Init
// 0x50ABC0 + Input_ResetMouseState 0x50E000.
struct CameraCtrlState {
    int32_t homeToggle = 1;                     // +0  [0]  Camera_Init 0x50ABC0
    int32_t pgupCycle  = 1;                     // +4  [1]  Camera_Init 0x50ABC0
    int32_t mouseLook  = 0;                     // +8  [2]  Input_ResetMouseState 0x50E000
    int32_t mode       = 0;                     // +12 [3]  Input_ResetMouseState 0x50E000
    int32_t zoomIndex  = 0;                     // +32 [8]  Input_ResetMouseState 0x50E000
    float   speed[4]   = { 20.0f, 5.0f, 5.0f, 5.0f }; // +36..+48 [9..12] Input_ResetMouseState 0x50E000
    // +16 [4] lockName (case 38): target lock compared against String 0x7EC95F -- not modeled
    // here (Crt_Strcmp/Crt_StringInit + global String out of front scope). TODO [ancre 0x7EC95F].
};

// In-game keyboard controller. Single instance (the original has one unique g_CameraCtrl).
class PlayerInputController {
public:
    PlayerInputController() = default;

    // Mirrors Camera_UpdateFromInput 0x50B7D0. Called 1x/gated frame (App_FrameTick 0x462619).
    //  - in    : keyboard state (immediate state[] + event buffer).
    //  - cam   : camera (gfx::Camera) -- orbit/look (Cam_OrbitYaw 0x69CEE0, etc.).
    //  - nc    : socket (net::NetClient) -- emits movement (Net_SendCmd_251 0x592870).
    //  - scene : current scene = mirrors g_SceneMgr 0x1676180. The entry guard
    //    @0x50B7EC tests TWO globals: `g_SceneMgr != 6 || g_SceneSubState != 4` -> the
    //    sub-state (ts2::g_SceneSubState 0x1676184, Scene/SceneManager.h; 4 = MainTick,
    //    set @0x52C7F1) is read directly as a global by Update(), just like in the binary,
    //    and is therefore NOT a parameter here. Nothing executes before the world tick
    //    is fully started.
    void Update(const input::InputSystem& in, gfx::Camera& cam,
                net::NetClient& nc, Scene scene);

    // --- Hooks to cross-front state/functions not owned here (rule #6) ---
    // g_UIEditBoxMgr 0x1668FC0: text input active -> skip the movement block (0x50B7FA).
    void SetTextInputActivePredicate(std::function<bool()> p) { textInputActive_ = std::move(p); }
    // g_SelfCharInvBlock[0] 0x1673170: player blocked (inventory/menu) -> orbit instead of movement.
    void SetSelfBlockedPredicate(std::function<bool()> p) { selfBlocked_ = std::move(p); }
    // g_MorphInProgress 0x1675A88: morph in progress (==1) -> cancels WASD movement.
    void SetMorphInProgressPredicate(std::function<bool()> p) { morphInProgress_ = std::move(p); }
    // Screenshot_SaveNext 0x5481A0 (Gfx/file function not owned here): called on F12 (case 88).
    void SetScreenshotHook(std::function<void()> h) { screenshotHook_ = std::move(h); }
    // cSceneMgr_OnKeyDown 0x517F80 (LABEL_240 0x50DDE4): routes leftover keys.
    void SetSceneKeyDownHook(std::function<void(int)> h) { sceneKeyDown_ = std::move(h); }

    // --- Self position modeled locally (flt_1687330/34/38 = self+252, Game-owned) ---
    void SetSelfPosition(float x, float y, float z) { selfPos_[0] = x; selfPos_[1] = y; selfPos_[2] = z; }
    const float* SelfPosition() const { return selfPos_; }

    const CameraCtrlState& State() const { return st_; }

private:
    // Movement block (mode 1 = player, sends 251; fallback orbit +/-6 if mouse-look off).
    // Returns true if the original would have made a global `return` (skipping LABEL_73),
    // false if it would have done `goto LABEL_73` (cf. body 0x50B810..0x50C721).
    bool UpdateWasd(const input::InputSystem& in, gfx::Camera& cam, net::NetClient& nc);
    // LABEL_73 0x50C726: first buffered KEY-DOWN event -> discrete scancode switch.
    void ProcessDiscreteKeys(const input::InputSystem& in, net::NetClient& nc);
    // LABEL_240 0x50DDE4: routes the key to cSceneMgr_OnKeyDown (under guards).
    void RouteSceneKey(int dik);

    CameraCtrlState st_;
    // flt_1687330/34/38 0x1687330 (self entity +252, Game-owned) -- modeled locally.
    // TODO [ancre 0x1687234+252]: sync from the self entity once Game exposes it
    // (otherwise server desync; acceptable in dev/offline, the W1 target = wire the builder).
    float selfPos_[3] = { 0.0f, 0.0f, 0.0f };

    std::function<bool()>    textInputActive_;   // g_UIEditBoxMgr 0x1668FC0
    std::function<bool()>    selfBlocked_;        // g_SelfCharInvBlock 0x1673170
    std::function<bool()>    morphInProgress_;    // g_MorphInProgress 0x1675A88
    std::function<void()>    screenshotHook_;     // Screenshot_SaveNext 0x5481A0
    std::function<void(int)> sceneKeyDown_;       // cSceneMgr_OnKeyDown 0x517F80
};

} // namespace ts2

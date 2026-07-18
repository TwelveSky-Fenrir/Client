// App/App_WndProc.cpp — application and main loop implementation.
#include "App/App.h"
#include "App/PlayerInputController.h"  // W1-F2 : Camera_UpdateFromInput 0x50B7D0 (g_CameraCtrl 0x1668F60)
#include "Core/Types.h"
#include "Core/Log.h"
#include "Asset/AssetSelfTest.h"
#include "UI/UIManager.h"         // ui::UIManager: right-click routers (UI_RouteRButtonDown 0x5AD5D0 / Up 0x5ADA90)
#include "Gfx/SpriteBatch.h"      // gfx::g_GameTimeSec (sprite blit clock)
#include "Gfx/Font.h"             // mFONTDATA : Font::AddTtfResource
#include "Game/GameDatabase.h"    // game::LoadGameDatabases (tables .IMG 005_*)
#include "Game/QuestSystem.h"     // mHELP : game::LoadQuestTable (005_00007.IMG)
#include "Game/StringTables.h"    // mBADWORD/mGAMENOTICE/mZONENAME/mMESSAGE/mFONTCOLOR
#include "Game/ExtraDatabases.h"  // mNPC/mQUEST : 2 additional .IMG tables
#include "Game/MotionPools.h"     // mGDATA/mZONEMAININFO/mZONENPCINFO/mZONEMOVEINFO
#include "Game/MiscManagers.h"    // mTRANSFER/mPOINTER/mUTIL/mMYINFO/mPLAY
#include "Config/GameOptions.h"   // mGAMEOPTION : g_Options (23 fields, 001.BIN)
#include "Audio/AudioSystem.h"    // DirectSound8 (device init, master volume)
#include "Gfx/GxdRenderer.h"      // g_GxdRenderer: shares the D3D9 device of g_GfxRenderer
#include "Net/Rng.h"              // net::DefaultRng() — seeded here, see App_Init 0x461C20 EA 0x461C3E
#include "Net/ClientState.h"      // net::g_MorphInProgress = g_MorphInProgress 0x1675A88 (guard @0x50B857)
#include "Game/MapWarp.h"         // game::kSelfActionStateOffset -> g_SelfActionState[0] 0x1687328 (@0x50AE17)
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>                  // std::time — srand(time(NULL)) of App_Init (EA 0x461C35)
#include <string>
// Intro AVI (App::Run, faithful to WinMain 0x4609C0 -> PlayShow_PlayVideoFile 0x6D70A0):
// WIN32_LEAN_AND_MEAN (defined at project level) excludes ole2.h/objbase.h from <windows.h> ->
// explicitly required for CoInitializeEx/CoCreateInstance/CoUninitialize. <dshow.h> provides
// IGraphBuilder/IMediaControl/IVideoWindow/IMediaEventEx (lib: strmiids.lib, see .vcxproj).
#include <objbase.h>
#include <dshow.h>
#include <imm.h>               // ImmGetDefaultIMEWnd — IME deactivation (WinMain 0x4615a5)
#pragma comment(lib, "imm32.lib") // imm32 not listed in the .vcxproj (not editable here)

namespace ts2 {

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (self)
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MOUSEWHEEL:
        input_.ProcessMessage(msg, wParam, lParam);
        break;
    default:
        break;
    }

    switch (msg) {
    case WM_ACTIVATEAPP:            // 0x1C : g_WindowActive = wParam (0x4619D0)
        windowActive_ = (wParam != FALSE);
        return 0;

    case WM_MOUSEMOVE: {            // 0x200 -> Camera_MouseDragRotate 0x50AFD0 (@0x461B22)
        // INPUT-01. The original updates this+52/+56 (last mouse position) on ALL its exit
        // paths (@0x50AFF1, @0x50B00E, @0x50B05E, @0x50B2DA, @0x50B2F1): the delta is
        // therefore ALWAYS consumed, even when it doesn't orbit. The accumulator is therefore
        // drained UNCONDITIONALLY — otherwise a drag following a free movement would replay
        // the entire accumulated path at once (orbit jump).
        int dx = 0, dy = 0;
        input_.GetMouseDelta(dx, dy);          // <- this+52/+56 (0x50AFF1/0x50B00E/0x50B2F1)
        // Original guards, in order:
        //  1. @0x50AFE9: g_SceneMgr != 6 || g_SceneSubState != 4 -> no orbit.
        //  2. @0x50B006: `if (a4 != 2)` where a4 = wParam -> STRICT EQUALITY to MK_RBUTTON (=2):
        //     the right button ALONE orbits; Shift+right-click (MK_SHIFT|MK_RBUTTON = 6) or
        //     left+right (MK_LBUTTON|MK_RBUTTON = 3) do NOT orbit. `==`, not `&`.
        //  3. @0x50B056: `(!g_SelfCharInvBlock[0] && !*(this+8) && g_SelfMorphNpcId == 194)
        //                || (!g_SelfCharInvBlock[0] &&  *(this+8) && g_CamMode != 1)` -> no
        //     orbit (this+8 = mouseLook of g_CameraCtrl 0x1668F60, see
        //     PlayerInputController.h:24). NOT MODELED here: g_SelfMorphNpcId 0x1675A98
        //     and g_CamMode 0x1668F6C
        //     are outside this front. With the binary's default values (blocked=0,
        //     mouseLook=0, morphNpcId != 194) BOTH clauses are FALSE -> orbit proceeds,
        //     which is exactly the original's behavior in the same state.
        //     TODO [ancre 0x50B056] : wire the 3rd guard once g_SelfMorphNpcId/g_CamMode
        //     are modeled (Gfx/Game front).
        if (scene_.Current() == Scene::InGame && g_SceneSubState == 4 && wParam == MK_RBUTTON) {
            // v13 = (mx - *(this+52)) * *(this+60)(=0.2) -> Cam_OrbitYaw   @0x50B0BA/@0x50B0C9
            // v12 = (my - *(this+56)) * *(this+64)(=0.3) -> Cam_OrbitPitch @0x50B0E3/@0x50B0F2
            // (offsets in BYTES; sensitivities set by Camera_Init @0x50AC1F/@0x50AC2B;
            //  Camera::OrbitByMouse literally applies these two constants, Camera.cpp:74-79.)
            camera_.OrbitByMouse(dx, dy);
            // NOT PORTED (Gfx front): the elevation clamp *(this+68) = 30.0 /
            // *(this+72) = 80.0 (@0x50B26A / @0x50B1BA) which CANCELS out-of-bounds orbit by
            // re-applying Cam_SetLookAt on the stored eye, and the distance renormalization to
            // *(this+80) (@0x50B3C7). TODO [ancre 0x50B26A] : port these bounds into gfx::Camera.
        }
        return 0;                              // 0x461B29
    }

    case WM_LBUTTONDOWN:            // 0x201 : SetCapture + Input_OnLButtonDown 0x50AC90
        scene_.OnLButtonDown(static_cast<short>(LOWORD(lParam)),
                             static_cast<short>(HIWORD(lParam)));
        return 0;
    case WM_LBUTTONUP:              // 0x202 : ReleaseCapture + Input_OnLButtonUp 0x50AD20
        scene_.OnLButtonUp(static_cast<short>(LOWORD(lParam)),
                           static_cast<short>(HIWORD(lParam)));
        return 0;

    // 0x204/0x205/0x207/0x20A: the work is done by the InputSystem hooks set up in
    // App::Init (already triggered by input_.ProcessMessage in the switch above, just like the
    // original WndProc calls Input_OnRButtonDown/Camera_ResetView/Camera_MouseWheelZoom).
    // These cases exist only to return 0 like the original (@0x461A94/@0x461AC8/@0x461AF5/
    // @0x461B44) instead of falling through to DefWindowProc.
    case WM_RBUTTONDOWN:            // 0x204 -> Input_OnRButtonDown 0x50ADB0 (@0x461A8F)
    case WM_RBUTTONUP:              // 0x205 -> Input_OnRButtonUp   0x50AE40 (@0x461AC3)
    case WM_MBUTTONDOWN:            // 0x207 -> Camera_ResetView    0x50AED0 (@0x461AF0)
    case WM_MOUSEWHEEL:             // 0x20A -> Camera_MouseWheelZoom 0x50B460 (@0x461B3F)
        return 0;

    case kWM_Socket: // 0x401: asynchronous socket notification (WSAAsyncSelect) -> 0x4619E9
        net_.OnSocketMessage(wParam, lParam);
        return 0;

    case WM_CHAR:
        // ABSENT from the binary (App_WndProc has NEITHER WM_CHAR NOR a generic WM_KEYDOWN)
        // — ASSUMED and necessary COMPENSATORY DEVIATION: the original client delegates text
        // input to mEDITBOX's 21 subclassed native Win32 EDIT controls (UI_CreateEditBoxes
        // 0x50E460), which consume WM_CHAR themselves; ClientSource replaces them with
        // standalone ts2::ui::EditBox (see mEDITBOX above), which therefore need to be fed here.
        // Kept.
        scene_.OnChar(static_cast<char>(wParam));
        return 0;

    case WM_KEYDOWN:
        // GAP-APPLIFE-01 / INPUT-08 — the `if (wParam == VK_ESCAPE) PostQuitMessage(0);`
        // branch was REMOVED: it was an invention. The binary's case 256 (@0x46197E) handles
        // ONLY ONE key — `if (a3 == 13) { UI_Chat_FocusInput(); return 0; }` @0x461B55 —
        // and everything else falls to DefWindowProc, for which VK_ESCAPE is a TOTAL NO-OP.
        // The original client NEVER quits via keyboard: its only two exits are g_QuitFlag
        // 0x815590 (UI Quit buttons, read by WinMain @0x46162B; mirrored by quit_) and
        // WM_DESTROY -> PostQuitMessage (@0x461BC3). Esc must therefore reach the dialog
        // router (closing the focused dialog), not close the game.
        //
        // INPUT-04 / GAP-APPLIFE-04 — DELIBERATELY NOT APPLIED HERE (ATOMIC cross-front
        // work item, see report). The finding is accurate: this path injects VKs while
        // App::Init's DirectInput hook (SetSceneKeyDownHook above) injects DIKs into the SAME
        // SceneManager::OnKeyDown, and the binary's in-game keyboard is 100% DIK
        // (Game_OnHotkey 0x537330 re-reads the DirectInput buffer and compares raw scancodes,
        // e.g. `cmp g_UiCmdQueueRecords[eax], 15h` @0x5373A0 = DIK_Y). Real collisions:
        // DIK_EQUALS=0x0D read as VK_RETURN, DIK_7=0x08 as VK_BACK, DIK_Y=0x15 as VK_CAPITAL.
        // BUT restricting this case to only the Login/CharSelect scenes (the proposed fix)
        // CANNOT be done by this front ALONE: all in-game keyboard consumers are VK-indexed
        // (UIManager::RouteKey <- Dialog::OnKey: `vk == VK_ESCAPE` in
        // GuildWindow.cpp:631/648, OptionsWindow.cpp:265, SkillTreeWindow.cpp:453,
        // VendorShopWindow.cpp:335, NpcDialogWindow.cpp:320, PlayerTradeWindow.cpp:349... ;
        // GameWindows::HandleHotkey compares 'I'/'C'/'O'). Cutting the VK source before the UI
        // front has converted these tables to DIK would leave these consumers with NO feed AT
        // ALL: Esc would no longer close any dialog and hotkeys would die — a clear
        // REGRESSION, and the exact opposite of what GAP-APPLIFE-01 above requires ("Esc must
        // reach the dialog router"). Both halves of the fix must therefore land TOGETHER
        // (App + Scene + UI). The VK feed is kept as-is in the meantime.
        // TODO [ancre 0x537330] : atomic DIK migration, see front report.
        // @0x461B5E: Enter (VK_RETURN=13) -> UI_Chat_FocusInput 0x68B200 — the ONLY keyboard
        // handling in the original WndProc, and it PRECEDES scene routing. RouteTextInputKey
        // (SceneManager.h:141, mirrors 0x50E070 / @0x461B5E) packages the focus arbitration:
        // if it consumes the key (chat input active), the WndProc stops there.
        // (The earlier TODO said this wiring was impossible — SceneManager exposed neither
        // RouteTextInputKey nor FocusChatInput at the time; the scene-wiring front has since
        // added them.)
        if (scene_.RouteTextInputKey(static_cast<int>(wParam)))
            return 0;
        scene_.OnKeyDown(static_cast<int>(wParam));
        return 0;

    case WM_SYSCOMMAND: {
        // INPUT-07 / GAP-APPLIFE-03 — filter @0x461B70-0x461BB9, absent until now (everything
        // fell to DefWindowProc). `if (dword_1669180 != 2) return 0;` @0x461B70: outside mode 2,
        // EVERY system command is swallowed. dword_1669180 is PROVEN == GameConfig::windowMode
        // (data_refs 0x1669180: written @0x460CE2 in WinMain's cmdline parse, then compared via
        // `cmp ds:dword_1669180, 2` at three points — @0x461314 window-style choice,
        // @0x461B69 here, @0x461D17 App_Init); `== 2` == windowed == cfg_.Windowed()
        // (GameConfig.h:16). In FULLSCREEN, therefore, everything is blocked.
        if (!cfg_.Windowed())
            return 0;                          // 0x461BB9
        const WPARAM sc = wParam & 0xFFF0;     // 0x461B7B
        // Only 4 commands are relayed to DefWindowProc: SC_MOVE 0xF010 / SC_MINIMIZE
        // 0xF020 / SC_MAXIMIZE 0xF030 (@0x461BA0) and SC_RESTORE 0xF120 (@0x461BAB).
        if (sc == SC_MOVE || sc == SC_MINIMIZE || sc == SC_MAXIMIZE || sc == SC_RESTORE)
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        // Everything else -> 0 (@0x461BB3): notably blocks SC_CLOSE (0xF060), SC_SIZE
        // (0xF000), SC_KEYMENU (0xF100, Alt/Alt+Space = loop freeze), SC_SCREENSAVE
        // (0xF140) and SC_MONITORPOWER (0xF170) — the screensaver and monitor sleep therefore
        // cannot trigger mid-session.
        return 0;
    }

    case WM_CLOSE:
        // 0x10 -> `xor eax,eax` / `return 0` @0x461BBD-0x461BBF, WITHOUT calling
        // DefWindowProc: the window is NEVER destroyed via the close button nor Alt+F4.
        // Without this case, the message fell to DefWindowProc, which translates it to
        // DestroyWindow -> WM_DESTROY -> PostQuitMessage: the client would close where the
        // original refuses to. The only legitimate destruction path remains App::Shutdown
        // (explicit DestroyWindow).
        return 0;

    case WM_DESTROY:                // 0x02 : only emitter of PostQuitMessage (@0x461BC3)
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);   // 0x461BDD
    }
}

} // namespace ts2

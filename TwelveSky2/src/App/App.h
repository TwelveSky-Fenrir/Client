// App/App.h — application and main loop of the client (WinMain/App_Init/App_FrameTick/App_WndProc).
// See Docs/TS2_CLIENT_SHELL.md §2.1.
#pragma once
#include "Net/NetSystem.h"   // pulls winsock2.h before windows.h (required order)
#include <windows.h>
#include "App/GameConfig.h"
#include "Scene/SceneManager.h"
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"          // mINPUT (App_Init) : Camera_Init — NOT DirectInput8, see App.cpp
#include "Input/InputSystem.h"  // DirectInput8 keyboard (queue of Gfx_InitDevice, polled per frame)
#include "Game/MiscManagers.h"  // mPOINTER : game::CursorSet
#include <string>

namespace ts2 {

class App {
public:
    // Body of WinMain 0x4609C0: parses cmdline, creates the window, App_Init, PeekMessage loop, App_Shutdown.
    int Run(HINSTANCE hInstance, const char* cmdLine);

private:
    bool Init();                     // App_Init 0x461C20 : REAL sequence of the 32 managers
    void ResolveGameDataDir();       // probes GameData + switches CWD onto it (faithful relative paths)
    void LoadDatabases();            // loads the .IMG tables (g_World.db) — non-blocking
    void FrameTick();                // App_FrameTick 0x4625D0 : fixed-step 30 FPS loop
    void Shutdown();                 // App_Shutdown 0x462480

    bool RegisterWindowClass();
    bool CreateGameWindow();
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM); // App_WndProc 0x461930

    // Right-click state gate, shared by Input_OnRButtonDown 0x50ADB0 (test @0x50AE17)
    // and Input_OnRButtonUp 0x50AE40 (test @0x50AEA7) — both carry EXACTLY the same
    // condition. Returns false when the right click must be FULLY swallowed (neither UI
    // nor scene), i.e. when g_SceneMgr==6 AND g_SceneSubState==4 AND g_SelfActionState[0]
    // (0x1687328) is one of {11,12,33,34,35,36,37}.
    bool RButtonGateOpen() const;

    double NowSeconds() const;       // high-resolution clock (QueryPerformanceCounter)

    HINSTANCE     hInst_ = nullptr;
    HWND          hwnd_  = nullptr;
    GameConfig          cfg_;
    gfx::Renderer       renderer_;
    gfx::Camera         camera_;      // mINPUT (App_Init 0x462384) : Camera_Init 0x50ABC0
    net::NetSystem      net_;
    input::InputSystem  input_;       // DirectInput8 clavier (queue Gfx_InitDevice)
    SceneManager        scene_;
    // mPOINTER (dword_8E714C): the set of 9 cursors is the UNIQUE singleton game::Cursors()
    // (mirror of the binary's global), NOT a member — otherwise two CursorSet instances would
    // diverge (scenes/UI would set the index on the singleton while App ticked the member). App
    // owns its life cycle (LoadResources/AnimateTick/DestroyAll on game::Cursors()).
    std::string         gameDataDir_; // GameData directory resolved by ResolveGameDataDir()

    bool   windowActive_  = true;    // dword_81558C (WM_ACTIVATEAPP)
    bool   quit_          = false;   // dword_815590 (g_QuitFlag)
    bool   cursorsReady_  = false;   // mPOINTER loaded -> allows Cursor_AnimateTick per frame (0x46163b)
    double gameClockSec_  = 0.0;     // g_GameTimeSec
    double frameAccumSec_ = 0.0;     // g_FrameAccumSec 0x815580
    double lastPurgeSec_  = 0.0;     // 0x81518C

    long long perfBase_ = 0;         // QPC base (set at startup)
    long long perfFreq_ = 1;         // QPC frequency
};

} // namespace ts2

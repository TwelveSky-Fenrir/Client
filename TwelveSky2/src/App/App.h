// App/App.h — application et boucle principale du client (WinMain/App_Init/App_FrameTick/App_WndProc).
// Voir Docs/TS2_CLIENT_SHELL.md §2.1.
#pragma once
#include "Net/NetSystem.h"   // tire winsock2.h avant windows.h (ordre requis)
#include <windows.h>
#include "App/GameConfig.h"
#include "Scene/SceneManager.h"
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"          // mINPUT (App_Init) : Camera_Init — PAS DirectInput8, cf. App.cpp
#include "Input/InputSystem.h"  // DirectInput8 clavier (queue de Gfx_InitDevice, poll par frame)
#include "Game/MiscManagers.h"  // mPOINTER : game::CursorSet
#include <string>

namespace ts2 {

class App {
public:
    // Corps de WinMain 0x4609C0 : parse cmdline, crée la fenêtre, App_Init, boucle PeekMessage, App_Shutdown.
    int Run(HINSTANCE hInstance, const char* cmdLine);

private:
    bool Init();                     // App_Init 0x461C20 : séquence RÉELLE des 32 managers
    void ResolveGameDataDir();       // sonde GameData + bascule le CWD dessus (chemins relatifs fidèles)
    void LoadDatabases();            // charge les tables .IMG (g_World.db) — non bloquant
    void FrameTick();                // App_FrameTick 0x4625D0 : boucle à pas fixe 30 FPS
    void Shutdown();                 // App_Shutdown 0x462480

    bool RegisterWindowClass();
    bool CreateGameWindow();
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT HandleMessage(HWND, UINT, WPARAM, LPARAM); // App_WndProc 0x461930

    // Garde d'état du clic DROIT, commune à Input_OnRButtonDown 0x50ADB0 (test @0x50AE17)
    // et Input_OnRButtonUp 0x50AE40 (test @0x50AEA7) — les deux portent EXACTEMENT la même
    // condition. Renvoie false quand le clic droit doit être INTÉGRALEMENT avalé (ni UI ni
    // scène), c'est-à-dire quand g_SceneMgr==6 ET g_SceneSubState==4 ET g_SelfActionState[0]
    // (0x1687328) appartient à {11,12,33,34,35,36,37}.
    bool RButtonGateOpen() const;

    double NowSeconds() const;       // horloge haute résolution (QueryPerformanceCounter)

    HINSTANCE     hInst_ = nullptr;
    HWND          hwnd_  = nullptr;
    GameConfig          cfg_;
    gfx::Renderer       renderer_;
    gfx::Camera         camera_;      // mINPUT (App_Init 0x462384) : Camera_Init 0x50ABC0
    net::NetSystem      net_;
    input::InputSystem  input_;       // DirectInput8 clavier (queue Gfx_InitDevice)
    SceneManager        scene_;
    // mPOINTER (dword_8E714C) : le jeu de 9 curseurs est le singleton UNIQUE game::Cursors()
    // (miroir du global du binaire), PAS un membre — sinon deux CursorSet divergeraient (les
    // scènes/UI poseraient l'index dans le singleton pendant qu'App tickerait le membre). App
    // en détient le cycle de vie (LoadResources/AnimateTick/DestroyAll sur game::Cursors()).
    std::string         gameDataDir_; // dossier GameData retenu par ResolveGameDataDir()

    bool   windowActive_  = true;    // dword_81558C (WM_ACTIVATEAPP)
    bool   quit_          = false;   // dword_815590 (g_QuitFlag)
    bool   cursorsReady_  = false;   // mPOINTER chargé -> autorise Cursor_AnimateTick par frame (0x46163b)
    double gameClockSec_  = 0.0;     // g_GameTimeSec
    double frameAccumSec_ = 0.0;     // g_FrameAccumSec 0x815580
    double lastPurgeSec_  = 0.0;     // 0x81518C

    long long perfBase_ = 0;         // base QPC (posée au démarrage)
    long long perfFreq_ = 1;         // fréquence QPC
};

} // namespace ts2

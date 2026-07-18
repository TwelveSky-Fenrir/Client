// Scene/SceneAudit.cpp — implementation of the temporary audit harness (see
// Scene/SceneAudit.h for full context and the removal reminder).
// Note: actual verification of the "UIManager::Init -> GameWindows ->
// SceneManager chain" mission (2026-07-14) ended up reusing
// Tools/UiWindowSelfTest.h (already present, extended with which="options"), so
// this file is no longer called from main.cpp — kept functional to avoid
// leaving a dangling vcxproj entry.
#include "Scene/SceneAudit.h"
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"
#include "Net/NetSystem.h"
#include "Scene/SceneManager.h"
#include "Core/Log.h"
#include <windows.h>

namespace ts2 {

// Minimal GameData resolution, independent of App::ResolveGameDataDir()
// (private, in a file out of scope for this audit): same candidates.
static std::string ResolveGameDataDirStandalone() {
    static const char* const kCandidates[] = {
        "GameData",
        "TwelveSky2/GameData",
        "ClientSource/TwelveSky2/GameData",
        "../../../TwelveSky2/GameData",
    };
    for (const char* dir : kCandidates) {
        std::string probe = std::string(dir) + "/G03_GDATA/D01_GIMAGE2D/005/005_00001.IMG";
        if (GetFileAttributesA(probe.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        char absPath[MAX_PATH] = {};
        std::string result = (GetFullPathNameA(dir, MAX_PATH, absPath, nullptr) != 0)
                                  ? std::string(absPath) : std::string(dir);
        if (!SetCurrentDirectoryA(result.c_str()))
            TS2_WARN("[SceneAudit] GameData resolu \"%s\" mais SetCurrentDirectory a echoue.",
                     result.c_str());
        return result;
    }
    return "";
}

int RunSceneAudit(const std::string& gameDataDirIn, int holdSeconds) {
    TS2_LOG("[SceneAudit] === Debut audit chaine UIManager::Init -> GameWindows -> SceneManager ===");

    std::string gameDataDir = gameDataDirIn.empty() ? ResolveGameDataDirStandalone() : gameDataDirIn;
    if (gameDataDir.empty()) {
        TS2_ERR("[SceneAudit] GameData introuvable — abandon.");
        return 1;
    }
    TS2_LOG("[SceneAudit] GameData = \"%s\"", gameDataDir.c_str());

    const int w = 1024, h = 768;

    WNDCLASSA wc = {};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "TS2_SceneAudit_Wnd";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "TwelveSky2 - SceneAudit",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 CW_USEDEFAULT, CW_USEDEFAULT, w, h,
                                 nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) { TS2_ERR("[SceneAudit] CreateWindow a echoue."); return 1; }

    gfx::Renderer renderer;
    if (!renderer.Init(hwnd, w, h, /*windowed=*/true)) {
        TS2_ERR("[SceneAudit] gfx::Renderer::Init a echoue (device D3D9).");
        DestroyWindow(hwnd);
        return 1;
    }
    TS2_LOG("[SceneAudit] Device D3D9 cree : dev=%p", (void*)renderer.Device());

    net::NetSystem net;
    net.Init(); // WSAStartup; no network connection required for this audit

    SceneManager mgr;
    mgr.Init(renderer, net, hwnd, w, h, gameDataDir);

    // REAL path exercised here (same code as main.cpp/App.cpp in normal play):
    // SceneManager::Change(InGame) -> windows_->Init(*renderer_,...) ->
    // UIManager::Instance().Init(&renderer,...) -> ctx_.renderer = renderer.
    TS2_LOG("[SceneAudit] Appel SceneManager::Change(Scene::InGame) (force, sans login/reseau)...");
    mgr.Change(Scene::InGame);
    TS2_LOG("[SceneAudit] Scene courante = %d (6=InGame attendu)", (int)mgr.Current());

    // Opens the Options window via the SAME path as the real 'O' key
    // (SceneManager::OnKeyDown -> GameWindows::HandleHotkey -> OptionsWindow::Open).
    mgr.OnKeyDown('O');
    TS2_LOG("[SceneAudit] OnKeyDown('O') envoye (ouverture OptionsWindow attendue).");

    gfx::Camera camera;

    // Render loop + message pump for `holdSeconds` (keeps the window
    // visible for an external screenshot).
    DWORD startTick = GetTickCount();
    DWORD endTick = startTick + static_cast<DWORD>(holdSeconds) * 1000u;
    int frames = 0;
    while (GetTickCount() < endTick) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        mgr.Update(0.033333, camera);
        if (renderer.BeginFrame()) {
            mgr.Render(renderer.Device(), camera);
            renderer.EndFrame();
            ++frames;
        }
        Sleep(16);
    }
    TS2_LOG("[SceneAudit] %d frames rendues sur %d s.", frames, holdSeconds);

    mgr.Shutdown();
    net.Shutdown();
    renderer.Shutdown();
    DestroyWindow(hwnd);

    TS2_LOG("[SceneAudit] === Fin audit ===");
    return 0;
}

} // namespace ts2

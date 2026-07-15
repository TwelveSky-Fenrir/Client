// Scene/SceneAudit.cpp — implementation du harnais d'audit temporaire (cf.
// Scene/SceneAudit.h pour le contexte complet et le rappel de suppression).
// Note : la verification effective de la mission "chaine UIManager::Init ->
// GameWindows -> SceneManager" (2026-07-14) a finalement reutilise
// Tools/UiWindowSelfTest.h (deja present, etendu avec which="options"), donc ce
// fichier n'est plus appele depuis main.cpp — conserve fonctionnel pour ne pas
// laisser d'entree vcxproj pendante.
#include "Scene/SceneAudit.h"
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"
#include "Net/NetSystem.h"
#include "Scene/SceneManager.h"
#include "Core/Log.h"
#include <windows.h>

namespace ts2 {

// Resolution GameData minimale, independante de App::ResolveGameDataDir()
// (prive, dans un fichier hors perimetre de cet audit) : memes candidats.
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
    net.Init(); // WSAStartup ; aucune connexion reseau requise pour cet audit

    SceneManager mgr;
    mgr.Init(renderer, net, hwnd, w, h, gameDataDir);

    // Chemin REEL exerce ici (meme code que main.cpp/App.cpp en jeu normal) :
    // SceneManager::Change(InGame) -> windows_->Init(*renderer_,...) ->
    // UIManager::Instance().Init(&renderer,...) -> ctx_.renderer = renderer.
    TS2_LOG("[SceneAudit] Appel SceneManager::Change(Scene::InGame) (force, sans login/reseau)...");
    mgr.Change(Scene::InGame);
    TS2_LOG("[SceneAudit] Scene courante = %d (6=InGame attendu)", (int)mgr.Current());

    // Ouvre la fenetre Options via le MEME chemin que la vraie touche 'O'
    // (SceneManager::OnKeyDown -> GameWindows::HandleHotkey -> OptionsWindow::Open).
    mgr.OnKeyDown('O');
    TS2_LOG("[SceneAudit] OnKeyDown('O') envoye (ouverture OptionsWindow attendue).");

    gfx::Camera camera;

    // Boucle de rendu + pompe messages pendant `holdSeconds` (laisse la fenetre
    // visible pour une capture d'ecran externe).
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

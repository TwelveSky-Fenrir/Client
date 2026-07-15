// Tools/WorldReflectionSelfTest.cpp — voir WorldReflectionSelfTest.h. Outil TEMPORAIRE,
// a supprimer apres verification (mission "EXTENSION OMBRE/REFLET" 2026-07-14).
//
// Meme squelette que Tools/UiWindowSelfTest.cpp (fenetre Win32 + gfx::Renderer reel),
// mais rendu direct via Scene/WorldRenderer.h (pas de SceneManager/UI ici : la mission
// ne porte que sur le rendu d'entites, pas sur le flux de scenes).
#include "Tools/WorldReflectionSelfTest.h"
#include "Scene/WorldRenderer.h"
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"
#include "Game/GameDatabase.h"
#include "Game/GameState.h"
#include "Core/Types.h"
#include "Core/Log.h"

#include <windows.h>
#include <string>
#include <cstring>

namespace ts2::tools {

namespace {

// Duplique EXACTEMENT la logique de sondage de App::ResolveGameDataDir (App/App.cpp,
// NON MODIFIE) — meme convention que Tools/UiWindowSelfTest.cpp.
std::string ResolveGameDataDirLocal() {
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
        std::string out = dir;
        if (GetFullPathNameA(dir, MAX_PATH, absPath, nullptr) != 0) out = absPath;
        SetCurrentDirectoryA(out.c_str());
        return out;
    }
    return {};
}

LRESULT CALLBACK WndProcTest(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, w, l);
}

} // namespace

int RunWorldReflectionSelfTest(int seconds, int width, int height) {
    const std::string gd = ResolveGameDataDirLocal();
    TS2_LOG("WorldReflectionSelfTest : GameData = \"%s\"", gd.c_str());
    if (!game::LoadGameDatabases(gd))
        TS2_WARN("WorldReflectionSelfTest : LoadGameDatabases incomplet (corps/armes en repli cube).");

    WNDCLASSA wc{};
    wc.lpfnWndProc   = WndProcTest;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.lpszClassName = "TS2_WorldReflectionSelfTest";
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        TS2_ERR("WorldReflectionSelfTest : RegisterClass a echoue (%lu)", GetLastError());
        return 1;
    }

    const int w = (width  > 0) ? width  : ts2::kRefWidth;
    const int h = (height > 0) ? height : ts2::kRefHeight;
    const DWORD style = WS_VISIBLE | WS_CAPTION | WS_SYSMENU;
    RECT r{0, 0, w, h};
    AdjustWindowRect(&r, style, FALSE);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "TS2 WorldReflectionSelfTest", style,
                                50, 50, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        TS2_ERR("WorldReflectionSelfTest : CreateWindowEx a echoue (%lu)", GetLastError());
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    gfx::Renderer renderer;
    if (!renderer.Init(hwnd, w, h, /*windowed=*/true)) {
        TS2_ERR("WorldReflectionSelfTest : Renderer::Init a echoue.");
        return 1;
    }

    WorldRenderer world;
    if (!world.Init(renderer, w, h)) {
        TS2_ERR("WorldReflectionSelfTest : WorldRenderer::Init a echoue.");
        return 1;
    }

    // Camera : cible a l'origine, distance min par defaut (25, cf. Camera::kMinDistDefault)
    // -> largement au-dessus du near-cull cameras (10 u, IsBeyondCameraNearCull) pour les 3
    // entites placees ci-dessous.
    gfx::Camera camera;
    camera.SetTarget(0.0f, 0.0f, 0.0f);
    camera.SetPitch(20.0f * gfx::Camera::kDegToRad);

    // Peuple game::g_World DIRECTEMENT (meme mecanisme que EntityManager en production,
    // juste sans passer par Net_RecvDispatch) : joueur local + joueur distant + monstre,
    // tous actifs et proches les uns des autres, pour observer en UNE capture la
    // difference de comportement reflectionEligible (monstre=oui, joueurs=non).
    game::g_World.players.clear();
    game::g_World.monsters.clear();
    game::g_World.npcs.clear();

    game::PlayerEntity self{};
    self.active = true;
    self.x = -6.0f; self.y = 0.0f; self.z = 0.0f;
    self.hp = 100; self.mp = 100;
    self.name = "Self";
    game::g_World.players.push_back(self);

    game::PlayerEntity distant{};
    distant.active = true;
    distant.x = 6.0f; distant.y = 0.0f; distant.z = 0.0f;
    distant.hp = 100; distant.mp = 100;
    distant.name = "Distant";
    game::g_World.players.push_back(distant);

    game::MonsterEntity mob{};
    mob.active = true;
    mob.x = 0.0f; mob.y = 0.0f; mob.z = 0.0f;
    mob.hp = 50;
    game::g_World.monsters.push_back(mob);

    TS2_LOG("WorldReflectionSelfTest : scene peuplee (self=-6,0,0 ; distant=6,0,0 ; "
            "monstre=0,0,0). reflectionEligible attendu : monstre=OUI, joueurs=NON.");

    LARGE_INTEGER freq, t0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    auto elapsedSec = [&]() -> double {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - t0.QuadPart) / static_cast<double>(freq.QuadPart);
    };

    auto pumpFrame = [&]() {
        MSG msg{};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        camera.Update(1.0f / 30.0f);
        if (renderer.Ready() && renderer.BeginFrame()) {
            world.Render(camera);
            renderer.EndFrame();
        }
    };

    TS2_LOG("WorldReflectionSelfTest : fenetre ouverte, maintien %d s pour capture externe.",
            seconds > 0 ? seconds : 8);
    const double holdSec = seconds > 0 ? static_cast<double>(seconds) : 8.0;
    while (elapsedSec() < holdSec) { pumpFrame(); Sleep(16); }

    world.Shutdown();
    renderer.Shutdown();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return 0;
}

} // namespace ts2::tools

// Tools/UiWindowSelfTest.cpp — voir UiWindowSelfTest.h. Outil TEMPORAIRE, a supprimer
// apres verification (mission audit device reel Vendor/Warehouse/Inventory 2026-07-14).
//
// Ordre d'inclusion : Net/ EN PREMIER (NetSystem.h tire winsock2 avant windows.h),
// meme convention que UI/WarehouseWindow.cpp / UI/InventoryWindow.cpp.
#include "Net/NetSystem.h"
#include "Tools/UiWindowSelfTest.h"
#include "Scene/SceneManager.h"
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"
#include "Game/GameDatabase.h"
#include "Game/GameState.h"
#include "Game/ClientRuntime.h"
#include "Game/WarehouseSystem.h"
#include "Core/Types.h"
#include "Core/Log.h"

#include <windows.h>
#include <string>
#include <cstring>

namespace ts2::tools {

namespace {

// Duplique EXACTEMENT la logique de sondage de App::ResolveGameDataDir (App/App.cpp,
// NON MODIFIE) — petite copie locale a cet outil de verification, pas une edition du
// fichier interdit.
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

int RunUiWindowSelfTest(const char* which, int seconds, int width, int height) {
    const std::string gd = ResolveGameDataDirLocal();
    TS2_LOG("UiWindowSelfTest : GameData = \"%s\"", gd.c_str());
    if (!game::LoadGameDatabases(gd))
        TS2_WARN("UiWindowSelfTest : LoadGameDatabases incomplet (icones repli possible).");

    WNDCLASSA wc{};
    wc.lpfnWndProc   = WndProcTest;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.lpszClassName = "TS2_UiWindowSelfTest";
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        TS2_ERR("UiWindowSelfTest : RegisterClass a echoue (%lu)", GetLastError());
        return 1;
    }

    const int w = (width  > 0) ? width  : ts2::kRefWidth;
    const int h = (height > 0) ? height : ts2::kRefHeight;
    const DWORD style = WS_VISIBLE | WS_CAPTION | WS_SYSMENU;
    RECT r{0, 0, w, h};
    AdjustWindowRect(&r, style, FALSE);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "TS2 UiWindowSelfTest", style,
                                50, 50, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        TS2_ERR("UiWindowSelfTest : CreateWindowEx a echoue (%lu)", GetLastError());
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    gfx::Renderer renderer;
    if (!renderer.Init(hwnd, w, h, /*windowed=*/true)) {
        TS2_ERR("UiWindowSelfTest : Renderer::Init a echoue.");
        return 1;
    }

    net::NetSystem net;
    net.Init();

    gfx::Camera camera;
    SceneManager scene;
    scene.Init(renderer, net, hwnd, w, h, gd);
    // Force Scene::InGame DIRECTEMENT, sans passer par le handshake reseau. C'est le
    // MEME etat que le repli deja present en production dans SceneManager::Update
    // (case Scene::EnterWorld -> "flux en echec -> bascule InGame de secours") : donc
    // pas un artifice isole, un etat atteignable tel quel par le binaire reel.
    scene.Change(ts2::Scene::InGame);

    // Peuple un objet de test avec une icone REELLE (premier itemId dont ITEM_INFO
    // expose un iconId != 0), pour verifier le chemin de resolution GPU complet
    // (ITEM_INFO -> 002_%05u.IMG -> GpuTexture -> sprite), pas juste le repli rect.
    uint32_t testItemId = 0;
    for (uint32_t id = 1; id <= 2000 && !testItemId; ++id) {
        if (const game::ItemInfo* info = game::GetItemInfo(id); info && info->iconId != 0)
            testItemId = id;
    }
    TS2_LOG("UiWindowSelfTest : itemId de test = %u", testItemId);

    if (testItemId) {
        // Inventaire (sac page 0, 2 cases) + equipement (slot 0).
        game::g_Client.inv.Set(/*row*/0, /*col*/0, testItemId, /*gridX*/0, /*gridY*/0,
                               /*count*/1, /*durability*/0, /*serial*/0);
        game::g_Client.inv.Set(/*row*/0, /*col*/1, testItemId, /*gridX*/2, /*gridY*/0,
                               /*count*/3, /*durability*/0, /*serial*/0);
        game::g_World.self.equip[0].itemId = testItemId;

        // Entrepot : 2 cellules garnies.
        game::g_Warehouse.grid.cells[0][0].itemId = static_cast<int32_t>(testItemId);
        game::g_Warehouse.grid.cells[0][0].count  = 1;
        game::g_Warehouse.grid.cells[1][2].itemId = static_cast<int32_t>(testItemId);
        game::g_Warehouse.grid.cells[1][2].count  = 5;

        // Marchand : catalogue 2 entrees (adresses d'origine, cf. VendorShopWindow.h).
        game::g_Client.Var(0x1826134) = 2;                              // EntryCount
        game::g_Client.Var(0x1826128) = 1;                              // PageCount
        game::g_Client.Var(0x1826130) = -1;                             // Selection
        game::g_Client.Var(0x182613C)     = static_cast<int32_t>(testItemId); // idx0 itemId
        game::g_Client.Var(0x182613C + 4) = static_cast<int32_t>(testItemId); // idx1 itemId
        game::g_Client.Var(0x1834F84)     = 100;                        // prix idx0 comp0
    }

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
        scene.Update(1.0 / 30.0, camera);
        if (renderer.Ready() && renderer.BeginFrame()) {
            scene.Render(renderer.Device(), camera);
            renderer.EndFrame();
        }
    };

    // Quelques frames pour laisser windowsReady_ s'initialiser (GameWindows::Init).
    for (int i = 0; i < 10; ++i) { pumpFrame(); Sleep(16); }

    int vk = 0;
    if (which) {
        if (std::strcmp(which, "vendor") == 0)    vk = 'V';
        else if (std::strcmp(which, "warehouse") == 0) vk = 'H';
        else if (std::strcmp(which, "inventory") == 0) vk = 'I';
        // Ajout (audit chaine UIManager::Init -> GameWindows -> SceneManager,
        // 2026-07-14) : OptionsWindow, meme hotkey reel que GameWindows::hotkeys::kOptions.
        else if (std::strcmp(which, "options") == 0)   vk = 'O';
        // Ajout (audit coordonnees Inventaire/Entrepot/Enchantement, 2026-07-14) :
        // EnchantWindow, meme hotkey reel que GameWindows::hotkeys::kEnchant.
        else if (std::strcmp(which, "enchant") == 0)   vk = 'E';
    }
    if (vk) scene.OnKeyDown(vk);
    for (int i = 0; i < 15; ++i) { pumpFrame(); Sleep(16); }

    TS2_LOG("UiWindowSelfTest : fenetre '%s' ouverte, maintien %d s pour capture externe.",
            which ? which : "(aucune)", seconds);
    const double holdSec = seconds > 0 ? static_cast<double>(seconds) : 5.0;
    while (elapsedSec() < holdSec) { pumpFrame(); Sleep(16); }

    scene.Shutdown();
    renderer.Shutdown();
    net.Shutdown();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return 0;
}

} // namespace ts2::tools

// Tools/UiWindowSelfTest.cpp — see UiWindowSelfTest.h. TEMPORARY tool, to remove
// after verification (real-device audit mission Vendor/Warehouse/Inventory 2026-07-14).
//
// Include order: Net/ FIRST (NetSystem.h pulls winsock2 before windows.h),
// same convention as UI/WarehouseWindow.cpp / UI/InventoryWindow.cpp.
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

// Duplicates EXACTLY the probing logic of App::ResolveGameDataDir (App/App.cpp,
// NOT MODIFIED) — a small local copy for this verification tool, not an edit of
// the forbidden file.
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
    // Forces Scene::InGame DIRECTLY, without going through the network handshake. This
    // is the SAME state as the fallback already present in production in
    // SceneManager::Update (case Scene::EnterWorld -> "flow failed -> fall back to
    // InGame"): so not an isolated artifice, a state reachable as-is by the real binary.
    scene.Change(ts2::Scene::InGame);

    // Populates a test object with a REAL icon (first itemId whose ITEM_INFO exposes
    // a non-zero iconId), to verify the full GPU resolution path (ITEM_INFO ->
    // 002_%05u.IMG -> GpuTexture -> sprite), not just the fallback rect.
    uint32_t testItemId = 0;
    for (uint32_t id = 1; id <= 2000 && !testItemId; ++id) {
        if (const game::ItemInfo* info = game::GetItemInfo(id); info && info->iconId != 0)
            testItemId = id;
    }
    TS2_LOG("UiWindowSelfTest : itemId de test = %u", testItemId);

    if (testItemId) {
        // Inventory (bag page 0, 2 slots) + equipment (slot 0).
        game::g_Client.inv.Set(/*row*/0, /*col*/0, testItemId, /*gridX*/0, /*gridY*/0,
                               /*count*/1, /*durability*/0, /*serial*/0);
        game::g_Client.inv.Set(/*row*/0, /*col*/1, testItemId, /*gridX*/2, /*gridY*/0,
                               /*count*/3, /*durability*/0, /*serial*/0);
        game::g_World.self.equip[0].itemId = testItemId;

        // Warehouse: 2 filled cells.
        game::g_Warehouse.grid.cells[0][0].itemId = static_cast<int32_t>(testItemId);
        game::g_Warehouse.grid.cells[0][0].count  = 1;
        game::g_Warehouse.grid.cells[1][2].itemId = static_cast<int32_t>(testItemId);
        game::g_Warehouse.grid.cells[1][2].count  = 5;

        // Vendor: 2-entry catalog (original addresses, cf. VendorShopWindow.h).
        game::g_Client.Var(0x1826134) = 2;                              // EntryCount
        game::g_Client.Var(0x1826128) = 1;                              // PageCount
        game::g_Client.Var(0x1826130) = -1;                             // Selection
        game::g_Client.Var(0x182613C)     = static_cast<int32_t>(testItemId); // idx0 itemId
        game::g_Client.Var(0x182613C + 4) = static_cast<int32_t>(testItemId); // idx1 itemId
        game::g_Client.Var(0x1834F84)     = 100;                        // price idx0 comp0
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

    // A few frames to let windowsReady_ initialize (GameWindows::Init).
    for (int i = 0; i < 10; ++i) { pumpFrame(); Sleep(16); }

    int vk = 0;
    if (which) {
        if (std::strcmp(which, "vendor") == 0)    vk = 'V';
        else if (std::strcmp(which, "warehouse") == 0) vk = 'H';
        else if (std::strcmp(which, "inventory") == 0) vk = 'I';
        // Added (UIManager::Init -> GameWindows -> SceneManager chain audit,
        // 2026-07-14): OptionsWindow, same real hotkey as GameWindows::hotkeys::kOptions.
        else if (std::strcmp(which, "options") == 0)   vk = 'O';
        // Added (Inventory/Warehouse/Enchant coordinate audit, 2026-07-14):
        // EnchantWindow, same real hotkey as GameWindows::hotkeys::kEnchant.
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

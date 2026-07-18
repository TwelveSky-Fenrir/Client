// Tools/CharSelectSelfTest.cpp — see header. CharSelect/CreateChar preview harness.
// Include order: Net/ FIRST (NetSystem.h pulls winsock2 before windows.h).
#include "Net/NetSystem.h"
#include "Tools/CharSelectSelfTest.h"
#include "Net/NetClient.h"        // net::g_CharRecords / kCharRecordSize / kCharRecordCount
#include "Scene/SceneManager.h"
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"
#include "Game/GameDatabase.h"    // game::LoadGameDatabases (mITEM etc. — appearance resolution)
#include "Game/StringTables.h"    // game::LoadStringTables / g_Strings.messages (StrTable005: text for Str(id))
#include "Core/Types.h"
#include "Core/Log.h"

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>   // D3DXSaveSurfaceToFileA (capture back buffer -> PNG for visual verification)
#include <string>
#include <cstring>
#include <algorithm> // std::min (name truncation to 12 bytes)

namespace ts2::tools {

namespace {

// Local copy of App::ResolveGameDataDir logic (file not modified), + "." candidate
// (the exe is built inside GameData). Switches CWD to the GameData root.
std::string ResolveGameDataDirLocal() {
    static const char* const kCandidates[] = {
        ".", "GameData", "TwelveSky2/GameData",
        "ClientSource/TwelveSky2/GameData", "../../../TwelveSky2/GameData",
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

// Current SceneManager, to route input from WndProc (same as App/App.cpp does
// for the real window). Null outside RunCharSelectSelfTest.
SceneManager* g_testScene = nullptr;

LRESULT CALLBACK WndProcTest(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        // Route mouse/keyboard to the scene (CRÉER click, +/- arrows, name entry, etc.).
        case WM_LBUTTONDOWN:
            if (g_testScene) g_testScene->OnLButtonDown(static_cast<short>(LOWORD(l)),
                                                        static_cast<short>(HIWORD(l)));
            return 0;
        case WM_LBUTTONUP:
            if (g_testScene) g_testScene->OnLButtonUp(static_cast<short>(LOWORD(l)),
                                                      static_cast<short>(HIWORD(l)));
            return 0;
        case WM_CHAR:
            if (g_testScene) g_testScene->OnChar(static_cast<char>(w));
            return 0;
        case WM_KEYDOWN:
            if (g_testScene) g_testScene->OnKeyDown(static_cast<int>(w));
            return 0;
    }
    return DefWindowProcA(hwnd, msg, w, l);
}

// Writes a default character record of kCharRecordSize (10088 bytes) into net::g_CharRecords[slot].
// Offsets = those read by the LIST rendering (cf. Net/CharSelectPackets.h kCharRecField*):
//   name@20 (13-byte C-string, occupied = non-empty name), job@36, race@40, gender@44, face@48,
//   hair@52, power(level)@56. The list's 3D preview (CharPreview3D::BuildFromRecord) reads
//   race@40 / gender@44 / face@48 / hair@52 -> all must be within valid bounds
//   (race 0..2, gender 0..1, face 0..6, hair 0..2).
void InjectDefaultChar(int slot, const char* name, int32_t race, int32_t gender,
                       int32_t face, int32_t hair, int32_t job, int32_t level,
                       int32_t equipAItemId = 0, int32_t equipBItemId = 0,
                       int32_t weaponItemId = 0) {
    if (slot < 0 || slot >= net::kCharRecordCount) return;
    uint8_t* rec = net::g_CharRecords[slot];
    std::memset(rec, 0, net::kCharRecordSize);
    // name@20: 12 usable chars + NUL (keeps the field's 13 bytes).
    const size_t n = std::min<size_t>(std::strlen(name), 12);
    std::memcpy(rec + 20, name, n); // rec[20+n] stays 0 (memset) -> null-terminated C-string
    auto put = [&](int off, int32_t v) { std::memcpy(rec + off, &v, sizeof(v)); };
    put(36, job);    // job/class
    put(40, race);   // effective RACE for list rendering
    put(44, gender); // gender
    put(48, face);   // face
    put(52, hair);   // hair
    put(56, level);  // power = level (default selection = highest occupied level)
    // Equipment: item IDs read by CharPreview3D::BuildFromRecord (kRecOffItemEquipA=136,
    // kRecOffItemEquipB=184) -> GetItemInfo(id)->field196 = armor mesh index.
    put(136, equipAItemId); // "torso" armor (EquipA)
    put(184, equipBItemId); // "legs" armor (EquipB)
    put(216, weaponItemId); // WEAPON (v31; kRecOffItemWeapon) -> BuildFromRecord step 6
}

// Searches mITEM for the first item whose field196 == targetEntry (EquipA/B mesh index). Used to
// EQUIP the list record: entry 35 = the "showcase" armor used by the creation screen
// (kCreateBodyEntryIndex). Returns 0 if none found (record stays base-body, faithful).
int32_t FindEquipItemWithEntry(uint32_t targetEntry) {
    for (int32_t id = 1; id <= 99999; ++id) {
        const game::ItemInfo* it = game::GetItemInfo(static_cast<uint32_t>(id));
        if (it && it->field196 == targetEntry) return id;
    }
    return 0;
}

// Searches for a WEAPON item (typeCode 13..21 -> WeaponClassFromTypeCode != 0). BuildFromRecord step 6
// draws entry field196-1. Targets field196==34 (entry 33 = the "creation" weapon,
// kCreateWeaponEntryIndex, known mesh); failing that, the first weapon item with field196 in [1,57].
int32_t FindWeaponItem() {
    for (int32_t id = 1; id <= 99999; ++id) {
        const game::ItemInfo* it = game::GetItemInfo(static_cast<uint32_t>(id));
        if (it && game::WeaponClassFromTypeCode(it->typeCode) != 0 && it->field196 == 34u) return id;
    }
    for (int32_t id = 1; id <= 99999; ++id) {
        const game::ItemInfo* it = game::GetItemInfo(static_cast<uint32_t>(id));
        if (it && game::WeaponClassFromTypeCode(it->typeCode) != 0 &&
            it->field196 >= 1u && it->field196 <= 57u) return id;
    }
    return 0;
}

} // namespace

int RunCharSelectSelfTest(int seconds, int width, int height) {
    const std::string gd = ResolveGameDataDirLocal();
    TS2_LOG("CharSelectSelfTest : GameData = \"%s\"", gd.c_str());
    if (gd.empty())
        TS2_WARN("CharSelectSelfTest : GameData introuvable — assets illisibles, ecran vide probable.");
    if (!game::LoadGameDatabases(gd))
        TS2_WARN("CharSelectSelfTest : LoadGameDatabases incomplet (DB items pour l'apercu).");
    // StrTable005 (005.DAT -> mMESSAGE): without it, game::Str(id) returns "#id" (create labels,
    // notices...). App::Init loads it (App.cpp:438); the harness must do the same for REAL
    // labels (Clan/Gender/Head/Face/Weapon, notices) instead of "#24/#26/...".
    if (!game::LoadStringTables(game::g_Strings, gd, 0.0f, /*trVariant=*/false))
        TS2_WARN("CharSelectSelfTest : LoadStringTables partiel (labels 'Str(id)' -> #id).");
    TS2_LOG("CharSelectSelfTest : StrTable005 = %u messages.", game::g_Strings.messages.Count());

    WNDCLASSA wc{};
    wc.lpfnWndProc   = WndProcTest;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.lpszClassName = "TS2_CharSelectSelfTest";
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        TS2_ERR("CharSelectSelfTest : RegisterClass a echoue (%lu)", GetLastError());
        return 1;
    }

    const int w = (width  > 0) ? width  : ts2::kRefWidth;
    const int h = (height > 0) ? height : ts2::kRefHeight;
    const DWORD style = WS_VISIBLE | WS_CAPTION | WS_SYSMENU;
    RECT r{0, 0, w, h};
    AdjustWindowRect(&r, style, FALSE);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "TS2 CharSelect Preview (-charselecttest)",
                                style, 50, 50, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        TS2_ERR("CharSelectSelfTest : CreateWindowEx a echoue (%lu)", GetLastError());
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    gfx::Renderer renderer;
    if (!renderer.Init(hwnd, w, h, /*windowed=*/true)) {
        TS2_ERR("CharSelectSelfTest : Renderer::Init a echoue.");
        return 1;
    }

    net::NetSystem net;
    net.Init();

    gfx::Camera camera;
    SceneManager scene;
    scene.Init(renderer, net, hwnd, w, h, gd);

    // Injects 2 default character records BEFORE pumping: the CharSelect scene init
    // (RunInitBlock, ~30 frames) calls host.LoadCharacterSlots -> LoadCharacterSlotsFromRecords
    // which reads net::g_CharRecords. They appear in the list with their 3D preview.
    const int32_t armorItem  = FindEquipItemWithEntry(35); // 35 = "creation" armor (kCreateBodyEntryIndex)
    const int32_t weaponItem = FindWeaponItem();           // weapon (typeCode 13..21)
    TS2_LOG("CharSelectSelfTest : armure id=%d, arme id=%d (0 = aucun).", armorItem, weaponItem);
    // Slot 0: realistic EQUIPPED character (armor + weapon); slot 1: base body (comparison).
    InjectDefaultChar(0, "Guerrier", /*race*/0, /*gender*/0, /*face*/0, /*hair*/0, /*job*/0, /*level*/50,
                      /*equipA*/armorItem, /*equipB*/armorItem, /*weapon*/weaponItem);
    InjectDefaultChar(1, "Sorciere", /*race*/1, /*gender*/1, /*face*/2, /*hair*/1, /*job*/1, /*level*/30);
    TS2_LOG("CharSelectSelfTest : 2 persos injectes. Clic gauche = interaction (CRÉER, fleches, "
            "selection). Ferme la fenetre pour quitter.");

    g_testScene = &scene;
    scene.Change(ts2::Scene::CharSelect); // forces scene 4 (otherwise unreachable without a server)

    LARGE_INTEGER freq, t0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    auto elapsedSec = [&]() -> double {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - t0.QuadPart) / static_cast<double>(freq.QuadPart);
    };

    // Saves the current back buffer to a PNG (visual verification without screen access).
    auto capturePng = [&](const char* file) {
        IDirect3DSurface9* bb = nullptr;
        if (SUCCEEDED(renderer.Device()->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
            const HRESULT hr = D3DXSaveSurfaceToFileA(file, D3DXIFF_PNG, bb, nullptr, nullptr);
            TS2_LOG("CharSelectSelfTest : capture -> %s (hr=0x%08lX)", file, static_cast<unsigned long>(hr));
            bb->Release();
        }
    };

    bool quit = false;
    int  frameNo = 0;
    while (!quit && (seconds <= 0 || elapsedSec() < static_cast<double>(seconds))) {
        MSG msg{};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { quit = true; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        // Scripted sequence (SAFE window [models ~30 .. keep-alive scene-frame 60[: beyond that the
        // "session expired" id 20 notice would hide the character, for lack of a real server). "+"
        // arrows of the form at panelX+196=885 (center ~893), rows panelY+{78,102,126,150,174}={151,175,
        // 199,223,247}. In CREATION: "Clan" = RACE index (changes the whole body), "Weapon"
        // = variant (weapon class). SetCreateJob (Clan) resets gender/face/hair/variant.
        //   35 LIST · 38 Create click (930,518) · 41 capture default race1 · 43 Clan+ ->race2 · 47 capture
        //   race2 · 49 Weapon+ ->next weapon class · 53 capture race2+weapon2.
        ++frameNo;
        if (frameNo == 38) { scene.OnLButtonDown(930, 518); scene.OnLButtonUp(930, 518); } // Create
        // ROTATE LEFT button (slot 44, world (390,628) -> rotLeftLatched_ @0x522DE7). STICKY
        // latch: a single click -> the preview keeps rotating continuously (yaw previewRot[1] +/- 3 deg/frame).
        if (frameNo == 42) { scene.OnLButtonDown(398, 636); scene.OnLButtonUp(398, 636); }   // rotate left
        scene.Update(1.0 / 30.0, camera);
        if (renderer.Ready() && renderer.BeginFrame()) {
            scene.Render(renderer.Device(), camera);
            if (frameNo == 35) capturePng("preview_capture.png");
            if (frameNo == 41) capturePng("preview_create.png");        // face (before rotation)
            if (frameNo == 50) capturePng("preview_create_rotA.png");   // ~24 deg (8 frames)
            if (frameNo == 58) capturePng("preview_create_rotB.png");   // ~48 deg (16 frames)
            renderer.EndFrame();
        }
        Sleep(16);
    }

    g_testScene = nullptr;
    scene.Shutdown();
    renderer.Shutdown();
    net.Shutdown();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return 0;
}

} // namespace ts2::tools

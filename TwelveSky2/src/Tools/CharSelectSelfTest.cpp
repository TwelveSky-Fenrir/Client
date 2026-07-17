// Tools/CharSelectSelfTest.cpp — voir le header. Harnais de preview CharSelect/CreateChar.
// Ordre d'inclusion : Net/ EN PREMIER (NetSystem.h tire winsock2 avant windows.h).
#include "Net/NetSystem.h"
#include "Tools/CharSelectSelfTest.h"
#include "Net/NetClient.h"        // net::g_CharRecords / kCharRecordSize / kCharRecordCount
#include "Scene/SceneManager.h"
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"
#include "Game/GameDatabase.h"    // game::LoadGameDatabases (mITEM etc. — resolution d'apparence)
#include "Game/StringTables.h"    // game::LoadStringTables / g_Strings.messages (StrTable005 : texte de Str(id))
#include "Core/Types.h"
#include "Core/Log.h"

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>   // D3DXSaveSurfaceToFileA (capture back buffer -> PNG pour verification visuelle)
#include <string>
#include <cstring>
#include <algorithm> // std::min (troncature du nom a 12 o)

namespace ts2::tools {

namespace {

// Copie locale de la logique de App::ResolveGameDataDir (fichier non modifie), + candidat "."
// (l'exe est bati dans GameData). Bascule le CWD sur la racine GameData.
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

// SceneManager courant, pour router les entrees depuis WndProc (comme App/App.cpp le fait
// pour la fenetre reelle). Nul hors de RunCharSelectSelfTest.
SceneManager* g_testScene = nullptr;

LRESULT CALLBACK WndProcTest(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
        case WM_DESTROY: PostQuitMessage(0); return 0;
        // Route souris/clavier vers la scene (clic CRÉER, fleches +/-, saisie du nom, etc.).
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

// Ecrit une fiche perso par defaut de kCharRecordSize (10088 o) dans net::g_CharRecords[slot].
// Offsets = ceux lus par le rendu de la LISTE (cf. Net/CharSelectPackets.h kCharRecField*) :
//   name@20 (13 o C-string, occupation = nom non vide), job@36, race@40, gender@44, face@48,
//   hair@52, power(niveau)@56. Le rendu 3D de la liste (CharPreview3D::BuildFromRecord) lit
//   race@40 / gender@44 / face@48 / hair@52 -> tous doivent etre dans les bornes valides
//   (race 0..2, gender 0..1, face 0..6, hair 0..2).
void InjectDefaultChar(int slot, const char* name, int32_t race, int32_t gender,
                       int32_t face, int32_t hair, int32_t job, int32_t level) {
    if (slot < 0 || slot >= net::kCharRecordCount) return;
    uint8_t* rec = net::g_CharRecords[slot];
    std::memset(rec, 0, net::kCharRecordSize);
    // name@20 : 12 caracteres utiles + NUL (garde les 13 o du champ).
    const size_t n = std::min<size_t>(std::strlen(name), 12);
    std::memcpy(rec + 20, name, n); // rec[20+n] reste 0 (memset) -> C-string terminee
    auto put = [&](int off, int32_t v) { std::memcpy(rec + off, &v, sizeof(v)); };
    put(36, job);    // job/classe
    put(40, race);   // RACE effective du rendu de la liste
    put(44, gender); // genre
    put(48, face);   // visage
    put(52, hair);   // cheveux
    put(56, level);  // power = niveau (selection par defaut = plus haut niveau occupe)
}

} // namespace

int RunCharSelectSelfTest(int seconds, int width, int height) {
    const std::string gd = ResolveGameDataDirLocal();
    TS2_LOG("CharSelectSelfTest : GameData = \"%s\"", gd.c_str());
    if (gd.empty())
        TS2_WARN("CharSelectSelfTest : GameData introuvable — assets illisibles, ecran vide probable.");
    if (!game::LoadGameDatabases(gd))
        TS2_WARN("CharSelectSelfTest : LoadGameDatabases incomplet (DB items pour l'apercu).");
    // StrTable005 (005.DAT -> mMESSAGE) : sans elle, game::Str(id) renvoie "#id" (labels create,
    // notices...). App::Init la charge (App.cpp:438) ; le harnais doit faire pareil pour des
    // labels REELS (Clan/Gender/Head/Face/Weapon, notices) au lieu de "#24/#26/...".
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

    // Injecte 2 fiches perso par defaut AVANT de pomper : l'init de scene CharSelect
    // (RunInitBlock, ~30 frames) appelle host.LoadCharacterSlots -> LoadCharacterSlotsFromRecords
    // qui lit net::g_CharRecords. Elles apparaissent dans la liste avec leur apercu 3D.
    InjectDefaultChar(0, "Guerrier", /*race*/0, /*gender*/0, /*face*/0, /*hair*/0, /*job*/0, /*level*/50);
    InjectDefaultChar(1, "Sorciere", /*race*/1, /*gender*/1, /*face*/2, /*hair*/1, /*job*/1, /*level*/30);
    TS2_LOG("CharSelectSelfTest : 2 persos injectes. Clic gauche = interaction (CRÉER, fleches, "
            "selection). Ferme la fenetre pour quitter.");

    g_testScene = &scene;
    scene.Change(ts2::Scene::CharSelect); // force la scene 4 (sinon inatteignable hors serveur)

    LARGE_INTEGER freq, t0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    auto elapsedSec = [&]() -> double {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - t0.QuadPart) / static_cast<double>(freq.QuadPart);
    };

    // Sauve le back-buffer courant dans un PNG (verification visuelle sans acces ecran).
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
        // Sequence scriptee (fenetre SURE [modeles ~30 .. keep-alive scene-frame 60[ : au-dela la
        // notice « session expiree » id 20 masquerait le perso, faute de vrai serveur). Fleches « + »
        // du formulaire a panelX+196=885 (centre ~893), rangees panelY+{78,102,126,150,174}={151,175,
        // 199,223,247}. Dans la CREATION : « Clan » = index de RACE (change tout le corps), « Weapon »
        // = variant (classe d'arme). SetCreateJob (Clan) RAZ genre/visage/cheveux/variant.
        //   35 LISTE · 38 clic Creer (930,518) · 41 capture race1 defaut · 43 Clan+ ->race2 · 47 capture
        //   race2 · 49 Weapon+ ->classe d'arme suivante · 53 capture race2+arme2.
        ++frameNo;
        if (frameNo == 38) { scene.OnLButtonDown(930, 518); scene.OnLButtonUp(930, 518); } // Creer
        if (frameNo == 43) { scene.OnLButtonDown(893, 159); scene.OnLButtonUp(893, 159); } // Clan + (race)
        if (frameNo == 49) { scene.OnLButtonDown(893, 255); scene.OnLButtonUp(893, 255); } // Weapon + (variant)
        scene.Update(1.0 / 30.0, camera);
        if (renderer.Ready() && renderer.BeginFrame()) {
            scene.Render(renderer.Device(), camera);
            if (frameNo == 35) capturePng("preview_capture.png");
            if (frameNo == 41) capturePng("preview_create.png");           // race 1 (defaut)
            if (frameNo == 47) capturePng("preview_create_clan2.png");     // autre clan/race
            if (frameNo == 53) capturePng("preview_create_weapon2.png");   // + classe d'arme suivante
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

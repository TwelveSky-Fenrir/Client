// Tools/WorldSelfTest.cpp — voir le header. Harnais `-worldtest` (voir le perso dans le monde 3D).
// Ordre d'inclusion : Net/ EN PREMIER (NetSystem.h tire winsock2 avant windows.h).
#include "Net/NetSystem.h"
#include "Tools/WorldSelfTest.h"
#include "Net/NetClient.h"
#include "Scene/SceneManager.h"        // SceneManager, ts2::Scene, ts2::g_SceneSubState
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"
#include "Game/GameDatabase.h"         // game::LoadGameDatabases
#include "Game/StringTables.h"         // game::LoadStringTables / g_Strings
#include "Game/GameState.h"            // game::g_World / PlayerEntity
#include "Core/Types.h"
#include "Core/Log.h"

#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>                     // D3DXSaveSurfaceToFileA (capture back buffer -> PNG)
#include <string>
#include <cstring>
#include <cstdint>

namespace ts2::tools {

namespace {

// Copie locale de la logique de App::ResolveGameDataDir (cf. CharSelectSelfTest.cpp).
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

LRESULT CALLBACK WndProcTest(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, w, l);
}

// Ecrit un u32 LE a l'offset `off` du body 600 o de PlayerEntity (apparence lue par
// Scene/WorldRenderer : body+68 race, +72 gender, +76 costumeSlot0, +80 costumeSlot1, +148 arme).
void PutBodyU32(std::array<uint8_t, 600>& body, int off, uint32_t v) {
    if (off < 0 || off + 4 > static_cast<int>(body.size())) return;
    std::memcpy(body.data() + off, &v, sizeof(v));
}

// Cherche dans mITEM le 1er item dont field196 == targetEntry (= variant de mesh d'armure de corps ;
// C%03d{token}%03d avec suffixe field196+1). Sert a EQUIPER le self : field196=34 -> suffixe 035 =
// l'armure "showcase" que l'ecran de creation utilise. 0 si aucun -> corps de base.
int32_t FindEquipItemWithField196(uint32_t targetEntry) {
    for (int32_t id = 1; id <= 99999; ++id) {
        const game::ItemInfo* it = game::GetItemInfo(static_cast<uint32_t>(id));
        if (it && it->field196 == targetEntry) return id;
    }
    return 0;
}

} // namespace

int RunWorldSelfTest(int seconds, int zoneId, float selfX, float selfY, float selfZ,
                     int width, int height) {
    const std::string gd = ResolveGameDataDirLocal();
    TS2_LOG("WorldSelfTest : GameData = \"%s\"", gd.c_str());
    if (gd.empty())
        TS2_WARN("WorldSelfTest : GameData introuvable — zone illisible, ecran vide probable.");
    if (!game::LoadGameDatabases(gd))
        TS2_WARN("WorldSelfTest : LoadGameDatabases incomplet.");
    if (!game::LoadStringTables(game::g_Strings, gd, 0.0f, /*trVariant=*/false))
        TS2_WARN("WorldSelfTest : LoadStringTables partiel (labels HUD -> #id).");

    WNDCLASSA wc{};
    wc.lpfnWndProc   = WndProcTest;
    wc.hInstance     = GetModuleHandle(nullptr);
    wc.lpszClassName = "TS2_WorldSelfTest";
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        TS2_ERR("WorldSelfTest : RegisterClass a echoue (%lu)", GetLastError());
        return 1;
    }

    const int w = (width  > 0) ? width  : ts2::kRefWidth;
    const int h = (height > 0) ? height : ts2::kRefHeight;
    const DWORD style = WS_VISIBLE | WS_CAPTION | WS_SYSMENU;
    RECT r{0, 0, w, h};
    AdjustWindowRect(&r, style, FALSE);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "TS2 World Preview (-worldtest)", style,
                                50, 50, r.right - r.left, r.bottom - r.top,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        TS2_ERR("WorldSelfTest : CreateWindowEx a echoue (%lu)", GetLastError());
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);

    gfx::Renderer renderer;
    if (!renderer.Init(hwnd, w, h, /*windowed=*/true)) {
        TS2_ERR("WorldSelfTest : Renderer::Init a echoue.");
        return 1;
    }

    net::NetSystem net;
    net.Init(); // arme les handlers ; AUCUNE connexion reelle (pas de socket ouvert)

    gfx::Camera camera;
    SceneManager scene;
    scene.Init(renderer, net, hwnd, w, h, gd);

    // --- Etat monde force (ce que le serveur fournit normalement via op22/op0x0c/op0x0f) ---
    game::g_World.zoneId = zoneId;              // = g_TargetZoneId / g_SelfMorphNpcId (0x1675A98)
    game::g_World.players.clear();
    game::g_World.monsters.clear();
    game::g_World.npcs.clear();

    game::PlayerEntity self{};
    self.active = true;                         // = Pkt_SpawnCharacter self (op0x0f) -> players[0]
    self.name   = "Aventurier";
    self.x = selfX; self.y = selfY; self.z = selfZ;
    self.hp = 100; self.mp = 100;
    // Apparence (mêmes bornes que -charselecttest, résout C001001001/C001002001/... sur disque).
    PutBodyU32(self.body, 68, 0);  // race
    PutBodyU32(self.body, 72, 0);  // gender
    PutBodyU32(self.body, 76, 0);  // costume slot0
    PutBodyU32(self.body, 80, 0);  // costume slot1
    // ARMURE (G3, DEEP IDA #5) : equip[2]=body+108 (torse token 003), equip[5]=body+132 (jambes token
    // 004). Le CODE G3 (PlayerPaperdoll) resout le variant via ITEM_INFO+196 -> C%03d003{f196+1}. ⚠ Le
    // rendu in-world de l'armure "showcase" (field196=34 -> C003035, 6 meshes) presente un ECART (pieces
    // detachees + sombre) alors que la MEME armure rend PROPREMENT en CharSelect (meme fichier) -> ecart
    // de PALETTE/rendu PlayerPaperdoll vs CharPreview3D, A INVESTIGUER. On laisse le self en corps de base
    // (equip 0 -> variantEff 0 -> C%03d003001, l'entree AssetMgr catalogue[0]) pour une demo propre.
    // Pour tester l'armure : decommenter (FindEquipItemWithField196 + PutBodyU32 108/132).
    const int32_t armorItem = 0; // FindEquipItemWithField196(34);
    TS2_LOG("WorldSelfTest : armure equipee id=%d (0 = corps de base).", armorItem);
    PutBodyU32(self.body, 108, static_cast<uint32_t>(armorItem)); // equip[2] = torse
    PutBodyU32(self.body, 132, static_cast<uint32_t>(armorItem)); // equip[5] = jambes
    game::g_World.players.push_back(self);

    // Joueur DISTANT (race 1, genre 1 = femme) a cote : montre le rendu MULTI-JOUEURS + la variete
    // d'apparence in-world (MEME chemin PlayerPaperdoll que le self, idx>0). Position dans le champ
    // de la camera 3e personne (self + offset).
    game::PlayerEntity distant{};
    distant.active  = true;
    distant.name    = "Compagnon";
    distant.x = selfX + 22.0f; distant.y = selfY; distant.z = selfZ + 6.0f;
    distant.hp = 100; distant.mp = 100;
    distant.heading = 200.0f;      // orientation differente du self
    PutBodyU32(distant.body, 68, 1);  // race 1
    PutBodyU32(distant.body, 72, 1);  // gender 1 (femme)
    PutBodyU32(distant.body, 76, 0);
    PutBodyU32(distant.body, 80, 0);
    game::g_World.players.push_back(distant);
    TS2_LOG("WorldSelfTest : zone=%d self=(%.1f,%.1f,%.1f) injecte a players[0].",
            zoneId, selfX, selfY, selfZ);

    // Sauve le back-buffer courant dans un PNG (verification visuelle sans acces ecran).
    auto capturePng = [&](const char* file) {
        IDirect3DSurface9* bb = nullptr;
        if (SUCCEEDED(renderer.Device()->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) && bb) {
            const HRESULT hr = D3DXSaveSurfaceToFileA(file, D3DXIFF_PNG, bb, nullptr, nullptr);
            TS2_LOG("WorldSelfTest : capture -> %s (hr=0x%08lX)", file, static_cast<unsigned long>(hr));
            bb->Release();
        }
    };

    // Etape 1 : flux EnterWorld -> charge les 12 ressources de zone (surtout .WG terrain) dans
    // worldAssets_ (World_LoadZoneResource 0x4DCB60). L'envoi op12 echoue (pas de socket) mais
    // le chargement de zone a lieu AVANT (etat LoadZoneResources), donc la zone est prete.
    scene.Change(ts2::Scene::EnterWorld);

    // Bornes de frames (le chargement du .WG ~45 Mo peut bloquer une frame : on borne en FRAMES,
    // pas en temps, pour garantir la capture).
    const int kForceInGameFrame = 260; // apres WaitBeforeUnload(30)+LoadZoneResources(~200)
    const int kCaptureFrame     = 330; // ~70 f apres InGame : Build terrain + cadrage camera fait
    const int kEndFrame         = kCaptureFrame + 120; // maintien apres capture
    const int maxSeconds        = (seconds > 0) ? seconds : 60;

    LARGE_INTEGER freq, t0;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    auto elapsedSec = [&]() -> double {
        LARGE_INTEGER now; QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - t0.QuadPart) / static_cast<double>(freq.QuadPart);
    };

    bool quit = false, forcedInGame = false, captured = false;
    int  frameNo = 0;
    while (!quit && frameNo < kEndFrame && elapsedSec() < static_cast<double>(maxSeconds)) {
        MSG msg{};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { quit = true; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        ++frameNo;
        // Simule l'ACK serveur (Pkt_EnterWorld 0x464160) : bascule InGame -> worldGeom_->Build.
        if (frameNo == kForceInGameFrame && !forcedInGame) {
            forcedInGame = true;
            scene.Change(ts2::Scene::InGame);
            TS2_LOG("WorldSelfTest : Change(InGame) force (frame %d).", frameNo);
        }
        scene.Update(1.0 / 30.0, camera);
        if (renderer.Ready() && renderer.BeginFrame()) {
            scene.Render(renderer.Device(), camera);
            if (frameNo == kCaptureFrame) { captured = true; capturePng("preview_world.png"); }
            if (frameNo == kCaptureFrame + 45) capturePng("preview_world_anim2.png"); // ~1.5s + tard : DETECTE l'anim
            renderer.EndFrame();
        }
        if (frameNo % 30 == 0)
            TS2_LOG("WorldSelfTest : frame=%d subState=%d players=%zu monsters=%zu",
                    frameNo, ts2::g_SceneSubState, game::g_World.players.size(),
                    game::g_World.monsters.size());
        Sleep(16);
    }

    scene.Shutdown();
    renderer.Shutdown();
    net.Shutdown();
    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return 0;
}

} // namespace ts2::tools

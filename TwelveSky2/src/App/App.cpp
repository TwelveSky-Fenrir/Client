// App/App.cpp — implémentation de l'application et de la boucle principale.
#include "App/App.h"
#include "App/PlayerInputController.h"  // W1-F2 : Camera_UpdateFromInput 0x50B7D0 (g_CameraCtrl 0x1668F60)
#include "Core/Types.h"
#include "Core/Log.h"
#include "Asset/AssetSelfTest.h"
#include "Gfx/SpriteBatch.h"      // gfx::g_GameTimeSec (horloge des blits sprite)
#include "Gfx/Font.h"             // mFONTDATA : Font::AddTtfResource
#include "Game/GameDatabase.h"    // game::LoadGameDatabases (tables .IMG 005_*)
#include "Game/QuestSystem.h"     // mHELP : game::LoadQuestTable (005_00007.IMG)
#include "Game/StringTables.h"    // mBADWORD/mGAMENOTICE/mZONENAME/mMESSAGE/mFONTCOLOR
#include "Game/ExtraDatabases.h"  // mNPC/mQUEST : 2 tables .IMG supplementaires
#include "Game/MotionPools.h"     // mGDATA/mZONEMAININFO/mZONENPCINFO/mZONEMOVEINFO
#include "Game/MiscManagers.h"    // mTRANSFER/mPOINTER/mUTIL/mMYINFO/mPLAY
#include "Config/GameOptions.h"   // mGAMEOPTION : g_Options (23 champs, 001.BIN)
#include "Audio/AudioSystem.h"    // DirectSound8 (init device, volume maître)
#include "Gfx/GxdRenderer.h"      // g_GxdRenderer : partage le device D3D9 de g_GfxRenderer
#include "Net/Rng.h"              // net::DefaultRng() — semé ici, cf. App_Init 0x461C20 EA 0x461C3E
#include "Net/ClientState.h"      // net::g_MorphInProgress = g_MorphInProgress 0x1675A88 (garde @0x50B857)
#include "Game/MapWarp.h"         // game::kSelfActionStateOffset -> g_SelfActionState[0] 0x1687328 (@0x50AE17)
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>                  // std::time — srand(time(NULL)) d'App_Init (EA 0x461C35)
#include <string>
// Intro AVI (App::Run, fidèle WinMain 0x4609C0 -> PlayShow_PlayVideoFile 0x6D70A0) :
// WIN32_LEAN_AND_MEAN (défini au projet) exclut ole2.h/objbase.h de <windows.h> -> requis
// explicitement pour CoInitializeEx/CoCreateInstance/CoUninitialize. <dshow.h> fournit
// IGraphBuilder/IMediaControl/IVideoWindow/IMediaEventEx (lib: strmiids.lib, cf. .vcxproj).
#include <objbase.h>
#include <dshow.h>
#include <imm.h>               // ImmGetDefaultIMEWnd — désactivation IME (WinMain 0x4615a5)
#pragma comment(lib, "imm32.lib") // imm32 non listée dans le .vcxproj (non modifiable ici)

namespace ts2 {

// W1-F2 : contrôleur clavier in-game (Camera_UpdateFromInput 0x50B7D0). Dans l'original,
// son état vit dans l'objet global g_CameraCtrl 0x1668F60 (distinct des singletons
// renderer), initialisé par mINPUT Camera_Init 0x50ABC0. App.h n'étant pas éditable par
// ce front (fichier non possédé), l'instance vit ici en portée fichier — le client n'a
// qu'une seule App, donc une seule instance, fidèle au singleton d'origine.
namespace { PlayerInputController g_playerInput; }

// ---- GameConfig::Parse ------------------------------------------------------
// Fidèle à WinMain 0x4609C0 (zone 0x460A13-0x460F4B, re-vérifiée par décompilation
// fraîche 2026-07-15 ; cf. Docs/TS2_CMDLINE_STRICT_VALIDATION.md) : ligne de commande
// '/'-délimitée, champs [build, TR, windowMode, width, height], avec VALIDATION STRICTE
// DE LONGUEUR par champ — c'est le sens de « cmdline '/'-strict » :
//   champ 1 (build/mode serveur) : EXACTEMENT 1 caractère (sinon [Error::PARAMETER])
//   champ 2 (variante TR)        : EXACTEMENT 1 caractère (sinon [Error::PARAMETER2])
//   champ 3 (mode fenêtre)       : EXACTEMENT 1 caractère (sinon [Error::PARAMETER5])
//   champ 4 (largeur)            : 1 à 4 caractères       (sinon [Error::PARAMETER6])
//   champ 5 (hauteur)            : 1 à 4 caractères       (sinon [Error::PARAMETER7])
//   6e champ (trop de '/')       : rejeté                 ([Error::PARAMETER8])
// DÉVIATIONS dev ASSUMÉES (documentées) vs l'original : (a) un champ mal formé rend la
// config invalide (valid=false) au lieu d'un MessageBox "[Error::PARAMETERn]" + return 0
// bloquant — App::Run journalise puis retombe sur /0/0/2/1024/768 ; (b) on exige >=3
// champs (l'original tolère silencieusement le sous-effectif en laissant les champs à 0) ;
// (c) garde-fou width/height<=0 -> défaut de référence (l'original accepterait 0 -> 0x0).
GameConfig GameConfig::Parse(const char* cmdLine) {
    GameConfig c;
    if (!cmdLine || cmdLine[0] != '/')
        return c; // invalide (l'original affiche « [Error::PARAMETER2] » et sort, 0x460A13)

    int fields[5] = {0, 0, 0, 0, 0};
    int n = 0;
    const char* p = cmdLine;                 // pointe sur le '/' initial (validé ci-dessus)
    while (*p == '/' && n < 5) {
        ++p;                                 // saute le '/'
        const char* start = p;
        while (*p && *p != '/') ++p;         // avance jusqu'au prochain '/' ou fin de chaîne
        const int len    = static_cast<int>(p - start);
        const int maxLen = (n < 3) ? 1 : 4;  // champs 1-3 = 1 car ; champs 4-5 = 1..4 car
        if (len < 1 || len > maxLen)
            return c;                        // champ mal formé -> config invalide
        fields[n++] = std::atoi(start);      // Crt_Atoi : 1 caractère non numérique -> 0 (fidèle)
    }
    if (*p == '/')
        return c; // 6e champ (trop de '/') -> invalide (fidèle [Error::PARAMETER8], 0x460B17 default)

    if (n >= 3) {
        c.buildVariant = fields[0];
        c.useTRVariant = fields[1];
        c.windowMode   = fields[2];
        if (n >= 4) c.width  = fields[3];
        if (n >= 5) c.height = fields[4];
        if (c.width  <= 0) c.width  = kRefWidth;
        if (c.height <= 0) c.height = kRefHeight;
        c.valid = true;
    }
    return c;
}

// ---- App --------------------------------------------------------------------
int App::Run(HINSTANCE hInstance, const char* cmdLine) {
    hInst_ = hInstance;

    // Mode auto-test de la couche Asset : « -assettest <cheminGameData> ».
    if (cmdLine && std::strncmp(cmdLine, "-assettest", 10) == 0) {
        const char* dir = cmdLine + 10;
        while (*dir == ' ') ++dir;
        return asset::RunSelfTest(*dir ? std::string(dir) : std::string("GameData"));
    }

    LARGE_INTEGER f, c0;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c0);
    perfFreq_ = f.QuadPart ? f.QuadPart : 1;
    perfBase_ = c0.QuadPart;

    cfg_ = GameConfig::Parse(cmdLine);
    if (!cfg_.valid) {
        // Fidèle : l'original refuse une cmdline mal formée. En dev, on tolère
        // les défauts pour pouvoir lancer sans argument (/0/0/2/1024/768).
        TS2_WARN("Ligne de commande invalide/absente -> defauts /0/0/2/%d/%d.", kRefWidth, kRefHeight);
        cfg_.valid = true;
    }
    TS2_LOG("Config : build=%d TR=%d mode=%d (%s) %dx%d",
            cfg_.buildVariant, cfg_.useTRVariant, cfg_.windowMode,
            cfg_.Windowed() ? "fenetre" : "plein-ecran", cfg_.width, cfg_.height);

    // Instance unique (fidèle : FindWindowA("TwelveSky2")).
    if (FindWindowA(kWindowClassName, nullptr)) {
        MessageBoxA(nullptr, "TwelveSky2 est deja lance.", kWindowTitle, MB_OK | MB_ICONWARNING);
        return 0;
    }

    if (!RegisterWindowClass()) return 1;
    if (!CreateGameWindow())    return 1;

    // ---- Intro : lecture BLOQUANTE de GameData/INTRO.AVI (DirectShow) -----------------
    // Fidèle à WinMain 0x4609C0 (EA 0x4614e6-0x4614eb, confirmé par 2 décompilations
    // indépendantes) : PlayShow_PlayVideoFile("INTRO.AVI", hInstance, hWnd) 0x6D70A0 est
    // appelée juste après la création de la fenêtre principale et AVANT ShowWindow/
    // UpdateWindow/App_Init — donc ICI, pas plus tard. Mécanisme confirmé par décompilation
    // de PlayShow_PlayVideoFile + des fonctions citées (PlayShow_CreateWindow 0x6D6B30,
    // PlayShow_InitFilterGraph 0x6D6C20, PlayShow_RenderFileAndRun 0x6D6E40,
    // PlayShow_SetFullScreen 0x6D6CF0, PlayShow_ReleaseInterfaces 0x6D67C0, PlayShow_WndProc
    // 0x6D69B0) : DirectShow "GraphBuilder minimal" — CoInitializeEx(APARTMENTTHREADED),
    // CoCreateInstance(CLSID_FilterGraph) -> IGraphBuilder -> QueryInterface IMediaControl/
    // IVideoWindow/IMediaEventEx, IGraphBuilder::RenderFile(chemin large), IVideoWindow::
    // put_Owner(fenêtre dédiée "Play Show", 0x0 à (0,0) — le rendu passe par l'overlay plein
    // écran DirectShow, pas par une fenêtre visible), put_FullScreenMode(OATRUE) + message
    // drain vers cette même fenêtre, IMediaControl::Pause() PUIS Run() (ordre exact de
    // l'original), pompe messages + IMediaEventEx::GetEvent jusqu'à EC_COMPLETE/EC_USERABORT.
    // SIMPLIFICATION ASSUMÉE (documentée) : l'original route Echap/Entrée/Espace via un
    // PlayShow_WndProc dédié qui appelle IMediaControl::Stop() puis poste WM_CLOSE ; ici,
    // ces 3 touches sont détectées DIRECTEMENT dans la pompe de messages ci-dessous (même
    // résultat — sortie anticipée du visionnage — sans WndProc séparé, DefWindowProcA
    // suffisant puisqu'aucune fenêtre visible n'est dessinée). Échec propre à chaque étape
    // (fichier absent, DirectShow/codec AVI indisponible) : log + on continue sans planter,
    // exactement comme l'original (PlayShow_PlayVideoFile ne fait jamais avorter WinMain).
    {
        const char* const kIntroClassName = "TS2IntroPlayShow";
        WNDCLASSA wcIntro   = {};
        wcIntro.lpfnWndProc = DefWindowProcA;
        wcIntro.hInstance   = hInstance;
        wcIntro.lpszClassName = kIntroClassName;
        const bool introClassOk = RegisterClassA(&wcIntro) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;

        // Résolution du chemin de INTRO.AVI : ResolveGameDataDir() (App_Init) n'a pas encore
        // tourné à ce stade (fidèle : l'intro joue AVANT App_Init) -> sonde les mêmes
        // candidats en dur pour rester utilisable en dev (exe loin de la racine GameData).
        std::string introPath;
        {
            static const char* const kIntroDirs[] = {
                "", "GameData", "TwelveSky2/GameData",
                "ClientSource/TwelveSky2/GameData", "../../../TwelveSky2/GameData",
            };
            for (const char* dir : kIntroDirs) {
                std::string p = (*dir ? std::string(dir) + "/INTRO.AVI" : std::string("INTRO.AVI"));
                if (GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES) { introPath = p; break; }
            }
        }

        if (!introClassOk) {
            TS2_WARN("[Intro] RegisterClass(\"%s\") a echoue (%lu) — INTRO.AVI ignoree.",
                     kIntroClassName, GetLastError());
        } else if (introPath.empty()) {
            TS2_WARN("[Intro] INTRO.AVI introuvable (candidats epuises) — lecture ignoree.");
        } else {
            HWND hIntroWnd = CreateWindowExA(0, kIntroClassName, "TwelveSky2: INTRO.AVI", 0,
                                              0, 0, 0, 0, nullptr, nullptr, hInstance, nullptr);
            if (!hIntroWnd) {
                TS2_WARN("[Intro] CreateWindowEx (fenetre dediee) a echoue (%lu) — INTRO.AVI ignoree.",
                         GetLastError());
            } else {
                // Correctif (verification runtime 2026-07-14) : une fenetre 0x0 creee sans
                // WS_VISIBLE ne recoit JAMAIS le focus clavier automatiquement -> le pump
                // PeekMessageA(hIntroWnd,...) ci-dessous ne voit alors jamais de WM_KEYDOWN,
                // rendant Echap/Entree/Espace inoperants (lecture bloquee pour la duree
                // integrale du fichier, sans issue manuelle). Force le focus explicitement.
                SetForegroundWindow(hIntroWnd);
                SetFocus(hIntroWnd);
                TS2_LOG("[Intro] Lecture bloquante de \"%s\" (DirectShow).", introPath.c_str());

                HRESULT hrCo = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
                const bool comOwned = SUCCEEDED(hrCo);
                if (!comOwned && hrCo != RPC_E_CHANGED_MODE) {
                    TS2_WARN("[Intro] CoInitializeEx a echoue (0x%08lX) — INTRO.AVI ignoree.",
                             static_cast<unsigned long>(hrCo));
                } else {
                    IGraphBuilder* pGraph   = nullptr;
                    IMediaControl* pControl = nullptr;
                    IMediaEventEx* pEvent   = nullptr;
                    IVideoWindow*  pVidWin  = nullptr;

                    HRESULT hr = CoCreateInstance(CLSID_FilterGraph, nullptr, CLSCTX_INPROC_SERVER,
                                                   IID_IGraphBuilder, reinterpret_cast<void**>(&pGraph));
                    if (FAILED(hr)) {
                        TS2_WARN("[Intro] CoCreateInstance(CLSID_FilterGraph) a echoue (0x%08lX, "
                                 "DirectShow/quartz.dll indisponible) — INTRO.AVI ignoree.",
                                 static_cast<unsigned long>(hr));
                    } else if (FAILED(pGraph->QueryInterface(IID_IMediaControl, reinterpret_cast<void**>(&pControl))) ||
                               FAILED(pGraph->QueryInterface(IID_IVideoWindow,  reinterpret_cast<void**>(&pVidWin)))  ||
                               FAILED(pGraph->QueryInterface(IID_IMediaEventEx, reinterpret_cast<void**>(&pEvent)))) {
                        TS2_WARN("[Intro] QueryInterface IMediaControl/IVideoWindow/IMediaEventEx "
                                 "a echoue — INTRO.AVI ignoree.");
                    } else {
                        wchar_t wPath[MAX_PATH] = {};
                        MultiByteToWideChar(CP_ACP, 0, introPath.c_str(), -1, wPath, MAX_PATH);
                        hr = pGraph->RenderFile(wPath, nullptr);
                        if (FAILED(hr)) {
                            TS2_WARN("[Intro] RenderFile(\"%s\") a echoue (0x%08lX, codec AVI "
                                     "manquant ?) — lecture ignoree.", introPath.c_str(),
                                     static_cast<unsigned long>(hr));
                        } else {
                            // Fidèle PlayShow_RenderFileAndRun/SetFullScreen : Owner puis, si pas
                            // déjà plein écran, message drain + FullScreenMode(OATRUE).
                            pVidWin->put_Owner(reinterpret_cast<OAHWND>(hIntroWnd));
                            long fsMode = 0;
                            pVidWin->get_FullScreenMode(&fsMode);
                            if (!fsMode) {
                                OAHWND hDrainUnused = 0;
                                pVidWin->get_MessageDrain(&hDrainUnused);
                                pVidWin->put_MessageDrain(reinterpret_cast<OAHWND>(hIntroWnd));
                                pVidWin->put_FullScreenMode(OATRUE);
                            }
                            HRESULT hrPause = pControl->Pause(); // ordre exact de l'original (Pause avant Run)
                            HRESULT hrRun   = pControl->Run();
                            TS2_LOG("[Intro] Pause=0x%08lX Run=0x%08lX fullscreen(demande)=%d",
                                     static_cast<unsigned long>(hrPause), static_cast<unsigned long>(hrRun), !fsMode);

                            bool playing = true;
                            MSG imsg{};
                            while (playing) {
                                long evCode = 0; LONG_PTR ep1 = 0, ep2 = 0;
                                while (pEvent->GetEvent(&evCode, &ep1, &ep2, 0) == S_OK) {
                                    pEvent->FreeEventParams(evCode, ep1, ep2);
                                    if (evCode == EC_COMPLETE || evCode == EC_USERABORT) playing = false;
                                }
                                if (!playing) break;
                                if (PeekMessageA(&imsg, hIntroWnd, 0, 0, PM_REMOVE)) {
                                    if (imsg.message == WM_KEYDOWN &&
                                        (imsg.wParam == VK_ESCAPE || imsg.wParam == VK_RETURN ||
                                         imsg.wParam == VK_SPACE)) {
                                        playing = false; // saut manuel (fidèle PlayShow_WndProc 0x6D69B0)
                                    } else {
                                        TranslateMessage(&imsg);
                                        DispatchMessageA(&imsg);
                                    }
                                } else {
                                    Sleep(50);
                                }
                            }
                            pControl->Stop();
                            pVidWin->put_Visible(OAFALSE);
                            pVidWin->put_Owner(0);
                        }
                    }

                    if (pVidWin)  pVidWin->Release();
                    if (pEvent)   pEvent->Release();
                    if (pControl) pControl->Release();
                    if (pGraph)   pGraph->Release();
                }
                if (comOwned) CoUninitialize();
                DestroyWindow(hIntroWnd);
            }
        }
        if (introClassOk) UnregisterClassA(kIntroClassName, hInstance);
    }

    // Fidèle WinMain 0x4609C0 (ordre VÉRIFIÉ par désassemblage frais 0x4614fb-0x461513) :
    // ShowWindow PUIS UpdateWindow SONT APPELÉS AVANT App_Init. La fenêtre — créée avec
    // WS_VISIBLE — s'affiche donc en noir pendant tout le chargement des 32 managers.
    // (L'ancien ordre C++ Init->ShowWindow était inversé.)
    ShowWindow(hwnd_, SW_SHOW);   // 0x4614fb (l'original passe nCmdShow ; SW_SHOW ici — main.cpp non modifiable, cf. compte rendu)
    UpdateWindow(hwnd_);          // 0x461505

    if (!Init()) {                // App_Init 0x461513
        // Fidèle 0x46151f-0x461536 puis 0x461591 : MessageBox "[Error::ApplicationInit()]",
        // App_Shutdown, puis return 0 (xor eax,eax). Chaque manager a déjà affiché son
        // propre "[Error::mXXX.Init()]" ; ceci est le second MessageBox générique.
        MessageBoxA(hwnd_, "[Error::ApplicationInit()]", kWindowTitle, MB_OK | MB_ICONERROR);
        Shutdown();
        return 0;
    }

    // Désactivation de l'IME (WinMain 0x461598-0x4615ab, chemin succès d'App_Init) :
    // SendMessageA(ImmGetDefaultIMEWnd(hWnd), WM_IME_CONTROL(0x283), IMC_CLOSESTATUSWINDOW(0x21), 0).
    if (HWND hIme = ImmGetDefaultIMEWnd(hwnd_))
        SendMessageA(hIme, WM_IME_CONTROL, IMC_CLOSESTATUSWINDOW, 0);

    // Boucle principale fidèle (WinMain 0x4615e9-0x461640) : PeekMessage non bloquant ;
    // Cursor_AnimateTick à CHAQUE itération (branche message ET branche frame).
    MSG msg = {};
    for (;;) {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);                        // 0x46160c
            DispatchMessage(&msg);                         // 0x461616
            if (msg.message == WM_QUIT) break;             // 0x46161c cmp Msg.message,12h -> shutdown
        } else {
            FrameTick();                                   // App_FrameTick 0x4625d0 (0x461626)
            if (quit_) break;                              // 0x46162b cmp g_QuitFlag,0 -> shutdown
        }
        // Cursor_AnimateTick(&dword_8E714C) 0x4c1140 (0x46163b) : réaffirme SetCursor(slot
        // actif) chaque tour. Gardé sur le succès du chargement des curseurs (mPOINTER) :
        // sans les ressources RT_GROUP_CURSOR (.rc absent) les slots sont nuls, et un
        // SetCursor(NULL) par frame masquerait le curseur — déviation dev documentée.
        if (cursorsReady_)
            cursors_.AnimateTick();
    }

    Shutdown();                                            // App_Shutdown 0x461642
    return static_cast<int>(msg.wParam);                   // return Msg.wParam
}

// Séquence RÉELLE de App_Init 0x461C20 (décompilation directe idaTs2, 2026-07-14) —
// 32 managers en chaîne stricte (mFONTDATA..mPLAY ci-dessous, chacun cité par son
// tag et son ancre EA d'origine), chacun affichant "[Error::mXXX.Init()]" et
// avortant au premier échec. Ordre et fonctions EXACTS ci-dessous (remplace
// l'ancien squelette générique à 27 stubs qui ne reflétait ni l'ordre ni les
// vraies fonctions -- l'ancien compte de 27 était un simple gabarit générique,
// pas une mesure du vrai App_Init). Chaque étape cite son ancre EA d'origine ; les 32
// managers sont désormais tous câblés (ordre revérifié par désassemblage frais, Vague 1
// 2026-07-15) — mEDITBOX/mUI = déviations UI assumées, mPAT = stub fidèle par construction.
bool App::Init() {
    // srand(time(NULL)) — TOUTE PREMIÈRE opération d'App_Init 0x461C20, avant même la
    // lecture de hInstance (EA 0x461C46) et le 1er manager mFONTDATA (EA 0x461C5C) ;
    // seul le prologue /GS la précède. Désassemblage :
    //   461c33  push 0
    //   461c35  call Crt_Time            ; time(NULL)
    //   461c3d  push eax
    //   461c3e  call Crt_Srand           ; srand(...) -> _tiddata->_holdrand
    // Sans cette graine, net::DefaultRng() démarrait à state_=1 (défaut CRT pré-srand),
    // soit la MÊME séquence à chaque lancement. Rng_Next 0x7603FD est le LCG MSVC exact
    // (imul 343FDh / add 269EC3h, cf. Net/Rng.h) : semer ici rend le flux fidèle.
    net::DefaultRng().Seed(static_cast<uint32_t>(std::time(nullptr)));

    // Résolution du répertoire GameData + changement du répertoire de travail du
    // process. L'ORIGINAL suppose CWD == racine d'installation (contenant
    // G01_GFONT/G02_GINFO/G03_GDATA/... directement) — tous les chemins relatifs
    // du binaire (TTF, .IMG, .BIN) sont écrits SANS préfixe "GameData\". Fait AVANT
    // mFONTDATA (le tout premier manager) pour que ce chemin relatif fonctionne
    // quel que soit le répertoire depuis lequel l'exe est lancé (utile en dev : le
    // binaire compilé vit sous build\Win32\Debug\, loin de GameData\).
    ResolveGameDataDir();

    // mFONTDATA (0x461c5c) : Font_AddTtfResource 0x4C0E70 — enregistre la police TTF
    // embarquée (G01_GFONT). Non bloquant ici (l'original avorte ; on tolère l'échec
    // pour rester utilisable sans les assets de police).
    if (!gfx::Font::AddTtfResource(cfg_.useTRVariant != 0))
        TS2_WARN("[mFONTDATA] Font::AddTtfResource a echoue — police de secours.");

    // mGXD (0x461d1e..) : Gfx_InitDevice 0x69B9B0 + GXD_DeviceReinit 0x4023F0.
    if (!renderer_.Init(hwnd_, cfg_.width, cfg_.height, cfg_.Windowed())) {
        MessageBoxA(hwnd_, "[Error::mGXD.Init()]", kWindowTitle, MB_OK | MB_ICONERROR);
        return false;
    }
    {
        int gxdError = 0;
        if (!gfx::GxdRenderer::Instance().DeviceReinit(renderer_.D3D(), renderer_.Device(),
                                                       cfg_.width, cfg_.height,
                                                       1.0f, 1000000.0f, &gxdError)) {
            char text[64];
            std::snprintf(text, sizeof(text), "[Error::TW2AddIn::mGXD.Init(%d)]", gxdError);
            MessageBoxA(hwnd_, text, kWindowTitle, MB_OK | MB_ICONERROR);
            return false;
        }
    }
    // GX-DEV-01 — Gfx_HandleDeviceLostReset 0x69DD40 : observateurs de perte/restauration du
    // device D3D9. Dans le binaire, g_GfxRenderer 0x7FFE18 POSSÈDE directement l'ID3DXEffect
    // (+620), l'ID3DXFont (+612) et l'ID3DXSprite (+608) et appelle lui-même leurs OnLostDevice
    // (@0x69DE3E) puis, après le Reset (@0x69DE55), leurs OnResetDevice (@0x69DE9B). Ici ces
    // objets D3DX appartiennent aux couches hautes (SceneManager -> LoginScene/GameHud/
    // GameWindows/WorldRenderer -> Font/SpriteBatch), d'où l'indirection par observateur.
    // Point d'attache prescrit par Gfx/Renderer.h:42-52. SANS cet enregistrement, onLost_/
    // onReset_ restent nullptr et TOUTE la chaîne SceneManager::OnDeviceLost/OnDeviceReset
    // (SceneManager.cpp:1356/1364) est du CODE MORT : après une perte de device (Alt+Tab en
    // plein écran, changement de résolution) les ID3DXSprite/ID3DXFont ne sont jamais notifiés
    // et la restauration est impossible. HandleDeviceLost() est appelée inconditionnellement en
    // tête de chaque frame par Renderer::BeginFrame (Renderer.cpp:238), miroir des 8 xrefs de
    // Gfx_HandleDeviceLostReset en tête des Scene_*Render.
    renderer_.SetDeviceCallbacks(
        [](void* u) { static_cast<SceneManager*>(u)->OnDeviceLost();  },   // 0x69DE3E (avant Reset)
        [](void* u) { static_cast<SceneManager*>(u)->OnDeviceReset(); },   // 0x69DE9B (après Reset)
        &scene_);

    // Audio DirectSound8 (Gfx_ZeroInitRenderer 0x69B980, appelé depuis Gfx_InitDevice).
    // Non bloquant : sur échec, le jeu tourne muet (comme l'original, flag dispo=0).
    if (audio::Audio().Init(hwnd_))
        TS2_LOG("Audio DirectSound8 initialise.");
    else
        TS2_WARN("Audio indisponible (DirectSound8) — demarrage muet.");
    // DirectInput8 clavier (queue de Gfx_InitDevice, 0x69C7F2..0x69C8C5). Souris=Win32.
    if (input_.Init(hInst_, hwnd_))
        TS2_LOG("Input DirectInput8 (clavier) initialise.");
    else
        TS2_WARN("Input indisponible (DirectInput8) — clavier materiel inactif.");

    // mNETWORK (0x461f0c) + mWORKER (0x461f38) : Net_Init 0x462790 (WSAStartup) +
    // Net_InitPacketHandlers 0x463270 (tables de dispatch, DÉJÀ dans NetSystem::Init).
    // Non bloquant : le client peut démarrer hors-ligne.
    if (!net_.Init())
        TS2_WARN("[mNETWORK/mWORKER] Reseau indisponible (WSAStartup) — demarrage hors-ligne.");

    // mTRANSFER (0x461f64) : sub_4B43A0 — NO-OP CONFIRMÉ dans le binaire (Hex-Rays
    // l'a réduit à `return 1;`, `this` jamais touché). Reproduit tel quel.
    game::Transfer_InitNoOp();

    // mPOINTER (0x461f90) : CursorSet_LoadResources 0x4C0FA0 — 9 curseurs Win32
    // EMBARQUÉS dans les ressources .exe (RT_GROUP_CURSOR). Échouera tant que
    // ClientSource n'embarque pas les mêmes ids dans son .rc — comportement honnête
    // documenté (Game/MiscManagers.h), non bloquant ici.
    cursorsReady_ = cursors_.LoadResources(hInst_);
    if (cursorsReady_)
        TS2_LOG("[mPOINTER] 9 curseurs charges.");
    else
        TS2_WARN("[mPOINTER] CursorSet::LoadResources incomplet (ressources .rc absentes) "
                 "— Cursor_AnimateTick par frame neutralise (evite un curseur masque).");

    // mBADWORD/mGAMENOTICE/mZONENAME/mMESSAGE/mFONTCOLOR (0x461fbc..0x46206c) :
    // Dict001_Load/Tips002_Load/StrTable003_Load/StrTable005_Load/ColorTable_InitPalette.
    // mMESSAGE (game::g_Strings.messages) est la table CRITIQUE consultée par TOUS
    // les handlers réseau via game::Str(id) — désormais du texte réel, plus "#id".
    if (game::LoadStringTables(game::g_Strings, gameDataDir_,
                               static_cast<float>(NowSeconds()), cfg_.useTRVariant != 0))
        TS2_LOG("[mBADWORD/mGAMENOTICE/mZONENAME/mMESSAGE/mFONTCOLOR] Tables chargees "
                "(%u mots bannis, %u astuces, %u zones, %u messages).",
                game::g_Strings.bannedWords.Count(), game::g_Strings.notices.Count(),
                game::g_Strings.zoneNames.Count(), game::g_Strings.messages.Count());
    else
        TS2_WARN("[mBADWORD/mGAMENOTICE/mZONENAME/mMESSAGE/mFONTCOLOR] Chargement partiel/echoue.");

    // mGAMEOPTION (0x462098) : Options_LoadAndNormalize 0x4C2110 — charge G02_GINFO\001.BIN
    // (23 champs) puis le RÉÉCRIT sur disque s'il manquait (matérialisation des défauts,
    // fidèle à l'original). Non bloquant.
    config::g_Options.LoadAndNormalize();
    TS2_LOG("[mGAMEOPTION] Options : son=%d musique=%d ombres=%d.",
            config::g_Options.SoundVolume, config::g_Options.MusicVolume,
            config::g_Options.GfxDetailShadows);
    audio::Audio().SetMasterVolume(config::g_Options.SoundVolume);

    // mLEVEL/mITEM/mSKILL/mMONSTER/mSOCKET (0x4620c4..0x462224, sauf mNPC/mQUEST/mPAT
    // ci-dessous) : LevelTable_LoadImg/MobDb_LoadImg/SkillGrowthTbl_LoadImg/
    // ItemDefTbl_LoadImg/AnchorTbl_LoadImg — les 5 tables .IMG 005_* (DÉJÀ écrites,
    // Game/GameDatabase.h). Non bloquant : si absentes, spawns rejetés proprement.
    LoadDatabases();

    // mNPC (0x462148) : SkillDefTbl_LoadImg 0x4C6BD0 — CONFIRMÉ table PNJ réelle
    // (005_00005.IMG, rec[0]="Blacksmith Wu", nom IDA trompeur comme MobDb/ItemDefTbl).
    // mQUEST (0x462174) : NpcTbl_LoadImg 0x4C8090 — CONFIRMÉ table quête réelle
    // (005_00006.IMG, rec[0]="[Intro] Banker Bai & Beggar Xiao", 10 blocs de dialogue).
    if (game::LoadExtraDatabases(gameDataDir_))
        TS2_LOG("[mNPC/mQUEST] Tables PNJ (005_00005.IMG) + Quete (005_00006.IMG) chargees.");
    else
        TS2_WARN("[mNPC/mQUEST] Tables supplementaires illisibles.");

    // mHELP (0x4621a0) : QuestTbl_LoadImg 0x4C8630 — DÉJÀ chargée (Game/QuestSystem.h::
    // LoadQuestTable, 005_00007.IMG, 999×84o). Attribution au manager "mHELP" (pas
    // "mQUEST" — confirmé par ce désassemblage frais) : le rôle réel de cette table
    // reste probablement "aide/tutoriel" plutôt que progression de quête à proprement
    // parler ; conservé tel quel (aucune régression, juste une note de fidélité).
    if (game::LoadQuestTable(gameDataDir_))
        TS2_LOG("[mHELP] QuestTbl (005_00007.IMG) chargee.");

    // mPAT (0x4621cc) : PatTbl_LoadImg_STUB 0x4C8DA0 — un STUB dans le binaire
    // D'ORIGINE (suffixe _STUB posé par IDA) : la table de plafonds d'arme
    // (dword_8E717C) n'est JAMAIS réellement chargée par le client d'origine non
    // plus. Rien à faire ici — fidèle par construction (déjà documenté dans
    // Game/StatFormulas.h comme terme neutralisé).

    // mGDATA (0x462250) : AssetMgr_InitAllSlots 0x4DEB50 — init du pool modèle/motion
    // (pas un fichier, une géométrie de tableaux en mémoire).
    if (game::InitModelMotionPool())
        TS2_LOG("[mGDATA] Pool modele/motion initialise.");
    // mZONEMAININFO (0x46227c) : Motion_InitFrameTable 0x4F1380 — table 350 lignes,
    // pure donnée codée en dur (350/350 cas reproduits).
    if (game::InitFrameTable())
        TS2_LOG("[mZONEMAININFO] Table de frames (350 lignes) initialisee.");
    // mZONENPCINFO (0x4622a8) : Motion_LoadGInfo002Bin 0x4FCFD0 -> G02_GINFO\002.BIN
    // (701400 o, 350x501 dwords, table d'ancrage).
    if (game::LoadGInfo002Bin(gameDataDir_))
        TS2_LOG("[mZONENPCINFO] G02_GINFO\\002.BIN charge (350x501 dwords).");
    else
        TS2_WARN("[mZONENPCINFO] G02_GINFO\\002.BIN illisible.");
    // mZONEMOVEINFO (0x4622d4) : Motion_LoadGInfo003Bin 0x4FD420 -> G02_GINFO\003.BIN
    // (1127000 o, 350x805 floats). Peuple la table déjà consultée par
    // Game/MapWarp.h (via game::g_CoordResolver, câblé dans les handlers réseau) —
    // jusqu'ici vérifiée vide en mémoire faute de ce chargeur.
    if (game::LoadGInfo003Bin(gameDataDir_))
        TS2_LOG("[mZONEMOVEINFO] G02_GINFO\\003.BIN charge (350x805 floats).");
    else
        TS2_WARN("[mZONEMOVEINFO] G02_GINFO\\003.BIN illisible.");

    // mMYINFO (0x462300) : Player_ResetAnimState 0x50F520 — réinit état d'animation
    // du joueur local. Opère sur un bloc "contrôleur de commandes joueur" pas encore
    // porté (g_PlayerCmdController) : appelé avec un tampon local dédié en attendant.
    // Game/MiscManagers.cpp écrit jusqu'à l'index 13314 -> AU MOINS 13315 floats requis
    // (documenté dans ce fichier) ; marge portée à 16384 pour rester au-delà en sécurité.
    {
        static float s_playerCmdController[16384] = {};
        game::Player_ResetAnimState(s_playerCmdController, static_cast<float>(NowSeconds()));
    }
    TS2_LOG("[mMYINFO] Etat d'animation joueur reinitialise.");

    // mMAIN (0x46232c) : cSceneMgr_Init 0x517AF0 — DÉJÀ écrit (SceneManager::Init).
    // gameDataDir_ : requis par world::WorldAssets (géométrie .WO, Gfx/WorldGeometryRenderer.h).
    // 7e arg cfg_.buildVariant = champ 0 de la cmdline = g_ServerModeFlag (0x166918C, « mode
    // serveur / variante de build ») -> transmis a SceneManager comme serverModeFlag, qui le
    // relaie a login_->Init (SceneManager.h/.cpp cable en parallele, HORS de ce fichier).
    scene_.Init(renderer_, net_, hwnd_, cfg_.width, cfg_.height, gameDataDir_, cfg_.buildVariant);

    // mUTIL (0x462358) : sub_53F2B0 — NO-OP CONFIRMÉ (même constat que mTRANSFER :
    // Hex-Rays l'a réduit à `return 1;`, `this` jamais touché). Reproduit tel quel.
    game::Util_InitNoOp();

    // mINPUT (0x462384) : Camera_Init 0x50ABC0 — PAS DirectInput8 (déjà fait plus
    // haut, dans le prolongement de Gfx_InitDevice) : ce manager initialise en fait
    // la CAMÉRA (sensibilités souris, bornes de zoom). Gfx/Camera.h est déjà écrit ;
    // ses constantes par défaut REPRODUISENT DÉJÀ celles de Camera_Init (0.2/0.3
    // deg/px, bornes 25..150) — l'instanciation par défaut suffit, rien à charger.
    camera_ = gfx::Camera{};
    TS2_LOG("[mINPUT] Camera initialisee (bornes zoom %.0f..%.0f).",
            camera_.MinDistance(), camera_.MaxDistance());

    // W1-F2 : câblage du contrôleur clavier in-game (Camera_UpdateFromInput 0x50B7D0).
    // g_CameraCtrl 0x1668F60 est initialisé par ce même manager mINPUT (Camera_Init 0x50ABC0) :
    // ses défauts (mouseLook/mode/speed[]) sont déjà posés par la construction de
    // CameraCtrlState. On y branche les hooks vers les états/fonctions inter-front non possédés.
    g_playerInput.SetScreenshotHook([]{
        // Screenshot_SaveNext 0x5481A0 = Gfx_SaveScreenshot 0x69EA50 ("G04_GSHOT\\Sxxxxx.JPG") —
        // fonction Gfx/file non possédée par ce front.
        // TODO [ancre 0x5481A0] : câbler renderer_.SaveScreenshot quand la fonction sera exposée.
    });
    g_playerInput.SetSceneKeyDownHook([this](int dik){
        scene_.OnKeyDown(dik);   // cSceneMgr_OnKeyDown 0x517F80 (LABEL_240 0x50DDE4)
    });
    // INPUT-09 — g_MorphInProgress 0x1675A88 : les 6 chemins de mouvement de
    // Camera_UpdateFromInput testent `if (g_MorphInProgress == 1) return;` (@0x50B857,
    // @0x50B8D0, @0x50B9B2, @0x50BA6E, @0x50BB23, @0x50BB63) — comparaison LITTÉRALE à 1,
    // reproduite telle quelle. net::g_MorphInProgress (Net/ClientState.h:18) est le miroir
    // vivant de ce global : remis à 0 par les 12 cas de fin de morph de
    // Net/GameHandlers_Misc.cpp:259-270 et déjà lu par InventoryWindow/SkillTreeWindow/
    // CharacterStatsWindow. Sans ce prédicat, morphInProgress_ était un std::function vide
    // -> le WASD restait émis PENDANT un morph, contrairement à l'original.
    g_playerInput.SetMorphInProgressPredicate([]{
        return net::g_MorphInProgress == 1;   // 0x1675A88, cf. 0x50B857
    });
    // Prédicats NON câblés, faute de preuve ou de modèle (règle : ne pas deviner) :
    //  - textInputActive_ (g_UIEditBoxMgr 0x1668FC0, garde @0x50B7FA) : exige un
    //    SceneManager::IsTextInputFocused() reflétant « un EDIT in-game a le focus » (au
    //    minimum GuildWindow.nameEdit_). Gater sur ChatWindow.Focused() seul serait un no-op
    //    (le clavier chat n'est pas routé en InGame). TODO [ancre 0x1668FC0] — front Scene.
    //  - selfBlocked_ (g_SelfCharInvBlock 0x1673170) : SÉMANTIQUE NON LEVÉE. Camera_
    //    UpdateFromInput @0x50B810 exige g_SelfCharInvBlock[0]==0 pour le bloc WASD, alors que
    //    Game_OnHotkey @0x537347 exige l'INVERSE (`cmp ds:g_SelfCharInvBlock, 0 / jnz`) pour
    //    les hotkeys : les deux modes sont mutuellement exclusifs, ce qui suggère un drapeau
    //    de validité/mode plutôt qu'un « blocage ». Câbler sans lever la polarité inverserait
    //    silencieusement le comportement. TODO [ancre 0x1673170] : confronter les writers.

    // --- Câblage des hooks souris (App_WndProc 0x461930) ----------------------------------
    // Dans le binaire, le WndProc appelle DIRECTEMENT Camera_ResetView (@0x461AF0),
    // Camera_MouseWheelZoom (@0x461B3F) et Input_OnRButtonDown/Up (@0x461A8F/@0x461AC3).
    // Côté ClientSource, InputSystem expose ces points d'appel sous forme de hooks
    // (Input/InputSystem.h:211-217) alimentés par input_.ProcessMessage depuis HandleMessage ;
    // le SetCapture/ReleaseCapture du WndProc (@0x461A68/@0x461A9B) est déjà fait par
    // InputSystem::OnRButtonDown/Up. Sans les assignations ci-dessous, onMDown_/onWheel_/
    // onRDown_/onRUp_ restaient nullptr : bouton du milieu, molette et clic DROIT inertes.

    // WM_MBUTTONDOWN 0x207 -> Camera_ResetView 0x50AED0 (@0x461AF0). Le nom IDA est TROMPEUR :
    // la fonction ne « reset » pas la caméra, elle MIROITE l'œil à 180° autour de la cible.
    input_.SetMButtonDownCallback([this](int, int) {
        // Garde 1 @0x50AEE9 : g_SceneMgr == 6 && g_SceneSubState == 4.
        if (scene_.Current() != Scene::InGame || g_SceneSubState != 4)
            return;
        // Garde 2 @0x50AF09 : `g_SelfCharInvBlock[0] || !this[2] || g_CamMode == 1`. Ce n'est
        // PAS un bail-out : le miroir a lieu QUAND cette condition est VRAIE. g_SelfCharInvBlock
        // 0x1673170 (polarité non levée, cf. ci-dessus) et g_CamMode 0x1668F6C (== g_CameraCtrl+12)
        // ne sont pas modélisés ici ; mouseLook (g_CameraCtrl+8) l'est. Avec les valeurs par
        // défaut du binaire (blocked=0, camMode=0, mouseLook=0 — Input_ResetMouseState 0x50E000),
        // la condition se réduit à `!mouseLook`, ce qui est fidèle À CET ÉTAT.
        // TODO [ancre 0x50AF09] : ajouter g_SelfCharInvBlock/g_CamMode une fois modélisés.
        if (g_playerInput.State().mouseLook != 0)
            return;
        // Miroir @0x50AF3D/@0x50AF5B : œil' = (2*cible.x - œil.x, œil.y INCHANGÉ,
        // 2*cible.z - œil.z), cible inchangée (v7/v8/v9). Nier les composantes horizontales de
        // (œil - cible) en préservant la verticale équivaut EXACTEMENT, dans le modèle sphérique
        // de gfx::Camera (Eye() = cible + d*(cos p*sin y, sin p, cos p*cos y)), à yaw += PI avec
        // pitch ET distance inchangés. Suivi de Cam_SetLookAt @0x50AF8D + Camera_SetEyeTarget
        // @0x50AFC1, que la reconstruction de l'œil par Camera::Eye() couvre.
        camera_.SetYaw(camera_.Yaw() + D3DX_PI);
    });

    // WM_MOUSEWHEEL 0x20A -> Camera_MouseWheelZoom 0x50B460 (@0x461B3F, arg = SHIWORD(wParam)).
    input_.SetMouseWheelCallback([this](int delta) {
        // Vide l'accumulateur mouse_.wheel d'InputSystem (un seul WM_MOUSEWHEEL par callback :
        // la valeur consommée == `delta`). Sans lecteur, il croîtrait indéfiniment. On garde
        // `delta` car c'est l'argument EXACT passé par le WndProc @0x461B3F.
        input_.ConsumeWheel();
        // Garde @0x50B479 : g_SceneMgr == 6 && g_SceneSubState == 4 — AUCUNE autre garde
        // (ni blocked, ni mouse-look), contrairement aux autres chemins caméra.
        if (scene_.Current() != Scene::InGame || g_SceneSubState != 4)
            return;
        // @0x50B490 : v8 = (double)a2 * this[19], où this+76 = 0.1 (Camera_Init @0x50AC4F).
        // a2 est le delta BRUT de WM_MOUSEWHEEL (±120 par cran), PAS un nombre de crans :
        // 120 * 0.1 = 12 unités de distance par cran. Puis Cam_ClampDistance(g_GfxRenderer, v8)
        // @0x50B49F, dont l'effet net (relu à 0x69CE00 : si |œil-cible| > a2, alors
        // œil -= normalize(œil-cible) * a2) est exactement `distance -= a2` == Camera::Zoom(a2).
        // Le snap aux bornes 25/150 (this[21]/this[22], @0x50B641/@0x50B7C5) est couvert par le
        // ClampDistanceInternal de Camera::Zoom.
        // ⚠ On N'UTILISE PAS gfx::Camera::ZoomByWheel : elle divise d'abord par 120
        //   (Camera.cpp:98) puis multiplie par 0.1 -> 0.1 unité/cran, soit 120x TROP LENT vs
        //   @0x50B490. Défaut du front Gfx, signalé ; contourné ici sans y toucher.
        camera_.Zoom(static_cast<float>(delta) * gfx::Camera::kWheelZoomStep);
    });

    // WM_RBUTTONDOWN 0x204 / WM_RBUTTONUP 0x205 -> Input_OnRButtonDown 0x50ADB0 /
    // Input_OnRButtonUp 0x50AE40 (@0x461A8F / @0x461AC3).
    // ÉTAT : la garde d'état (RButtonGateOpen, miroir @0x50AE17/@0x50AEA7) est reproduite et
    // s'exécute, mais le DISPATCH TERMINAL est BLOQUÉ hors de ce front — ni
    // ts2::ui::UIManager::RouteRButtonDown/Up (UIManager.h:206-209 n'expose que RouteMouseDown/
    // RouteMouseUp/RouteKey, sans paramètre de bouton) ni ts2::SceneManager::OnRButtonDown/Up
    // (SceneManager.h:90-93 n'expose que OnLButtonDown/OnLButtonUp/OnChar/OnKeyDown) n'existent.
    // Les écrire ici ne compilerait pas et sort du périmètre de ce front (fichiers non possédés).
    // Les hooks SONT néanmoins assignés (ils s'exécutent à chaque clic droit) et la garde est
    // exacte : il ne reste qu'UNE ligne à substituer dans chacun des deux corps ci-dessous dès
    // que le front Scene aura ajouté ses méthodes (cf. rapport de front, wiringTodo).
    input_.SetRButtonDownCallback([this](int x, int y) {
        // 0x50AE17 : hors garde, le clic droit est INTÉGRALEMENT avalé (ni UI ni scène).
        if (!RButtonGateOpen())
            return;
        (void)x; (void)y;
        // Chaîne d'origine : `if (!UI_RouteRButtonDown(a1,a2)) cSceneMgr_OnRButtonDown(...)`
        // (@0x50AE2F) — premier consommateur gagne. Le corps scène est un no-op FIDÈLE
        // (cSceneMgr_OnRButtonDown 0x517EA0 n'appelle, en scène 6, que les vides 0x537310/
        // 0x537320) : toute la valeur comportementale est dans le routeur UI 0x5AD5D0.
        // TODO [ancre 0x50AE2F] : le front Scene doit exposer `void SceneManager::OnRButtonDown
        // (int x, int y)` (à côté de OnLButtonDown, SceneManager.h:90) qui, en interne, tente
        // d'abord UIManager::RouteRButtonDown (miroir UI_RouteRButtonDown 0x5AD5D0, chaîne
        // « premier consommateur gagne ») puis retombe sur le no-op fidèle 0x517EA0. La
        // décomposition UI-puis-scène appartient à SceneManager, qui SEUL possède le UIManager
        // (App n'y a aucun accès : SceneManager.h n'expose ni UI() ni Chat()).
        // Une fois la méthode ajoutée, remplacer les deux (void) ci-dessus par :
        //     scene_.OnRButtonDown(x, y);
    });
    input_.SetRButtonUpCallback([this](int x, int y) {
        if (!RButtonGateOpen())   // 0x50AEA7 : garde IDENTIQUE à celle du RButtonDown
            return;
        (void)x; (void)y;
        // TODO [ancre 0x50AEBF] : symétrique du RButtonDown ci-dessus — le front Scene doit
        // exposer `void SceneManager::OnRButtonUp(int x, int y)` (UI_RouteRButtonUp 0x5ADA90
        // puis no-op fidèle cSceneMgr_OnRButtonUp 0x517F10). Remplacer alors les (void) par :
        //     scene_.OnRButtonUp(x, y);
    });

    // mEDITBOX (0x4623b0) : UI_CreateEditBoxes 0x50E460 — crée 21 EDIT Win32 natifs
    // sous-classés. DÉVIATION ASSUMÉE (documentée dès Docs/TS2_CLIENT_SHELL.md) :
    // ce client utilise des ts2::ui::EditBox autonomes (UI/Widgets.h) à la place,
    // pas d'EDIT natifs. Rien à faire ici.

    // mUI (0x4623dc) : UI_InitAllDialogs 0x5ABF50 — enregistre ~38 dialogues au
    // démarrage. DÉVIATION ASSUMÉE : ts2::ui::UIManager (déjà écrit) + les 13
    // fenêtres de jeu (UI/GameWindows.h) ne sont construites qu'à l'entrée en scène
    // InGame (SceneManager::Change), pas ici — ces dialogues n'ont de sens qu'avec
    // un g_World peuplé. Comportement fonctionnellement équivalent, activation
    // différée plutôt qu'immédiate-mais-masquée.

    // mPLAY (0x462405) : cGameData_InitPools 0x5575D0 — capacités fixes des pools
    // d'entités (déjà modélisés en std::vector dynamique dans Game/GameState.h ;
    // toujours vrai, fidèle : le binaire ne peut pas échouer ici).
    game::GameData_InitPools();
    TS2_LOG("[mPLAY] Pools de donnees de jeu initialises.");

    // Finalisation (0x462429-0x46245d, step 33 — APRÈS mPLAY, AVANT StartIntro, ordre
    // vérifié par désassemblage frais) : Terrain_PushRenderState(&g_GfxRenderer) 0x69CB80
    // lit QueryPerformanceCounter et renvoie le temps écoulé, écrit dans g_GameTimeSec
    // (0x815180) ; g_FrameAccumSec (0x815580) et flt_81518C (0x81518C) reçoivent la même
    // valeur ; flt_815188 (0x815188) <- flt_7A6918 = 0x3D088889 = 0.033333335f = 1/30 s
    // (pas fixe 30 FPS = kFixedTimestep, constante à la compilation ici). On amorce donc
    // les trois horloges de frame à l'instant courant, à ce point précis de la séquence.
    gameClockSec_ = frameAccumSec_ = lastPurgeSec_ = NowSeconds();

    scene_.StartIntro();  // cSceneMgr_StartIntro 0x517B80 (fin de App_Init, 0x462462)
    TS2_LOG("App_Init termine (sequence fidele App_Init 0x461C20).");
    return true;
}

// Sonde les emplacements candidats de GameData (relatifs à l'exe puis au projet,
// pour tolérer un lancement depuis build\Win32\{Debug,Release}\ en dev) et, dès
// qu'un dossier plausible est trouvé (contient G03_GDATA\D01_GIMAGE2D\005\...),
// mémorise son chemin ABSOLU (gameDataDir_) ET bascule le CWD du process dessus.
// Après cet appel, tout chemin relatif SANS préfixe "GameData\" (comme les chaînes
// codées en dur du binaire d'origine : "G01_GFONT\...", "G02_GINFO\...") se résout
// correctement, exactement comme si l'exe avait été lancé depuis la racine
// d'installation (hypothèse implicite de App_Init 0x461C20).
void App::ResolveGameDataDir() {
    static const char* const kCandidates[] = {
        ".",   // CWD EST DÉJÀ la racine GameData (exe lancé depuis GameData, ou build déposé
               // dans GameData comme suggéré) : sans ce candidat, gameDataDir_ restait vide
               // (WorldMap/tables in-game dégradées) alors que les chemins directs "G03_GDATA\..."
               // fonctionnaient déjà. Testé en premier : cas le plus fidèle (App_Init 0x461C20
               // suppose CWD == racine d'installation).
        "GameData",
        "TwelveSky2/GameData",
        "ClientSource/TwelveSky2/GameData",
        "../../../TwelveSky2/GameData",
    };
    for (const char* dir : kCandidates) {
        std::string probe = std::string(dir) + "/G03_GDATA/D01_GIMAGE2D/005/005_00001.IMG";
        if (GetFileAttributesA(probe.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue; // dossier candidat absent -> essai suivant, sans bruit.

        char absPath[MAX_PATH] = {};
        if (GetFullPathNameA(dir, MAX_PATH, absPath, nullptr) == 0) {
            gameDataDir_ = dir; // repli : chemin relatif tel quel.
        } else {
            gameDataDir_ = absPath;
        }
        if (SetCurrentDirectoryA(gameDataDir_.c_str())) {
            TS2_LOG("GameData resolu : \"%s\" (CWD bascule).", gameDataDir_.c_str());
        } else {
            TS2_WARN("GameData resolu : \"%s\" mais SetCurrentDirectory a echoue (%lu).",
                     gameDataDir_.c_str(), GetLastError());
        }
        return;
    }
    TS2_WARN("GameData introuvable (candidats epuises) — chemins relatifs non fiables.");
}

// Charge les tables .IMG (g_World.db) + la table d'aide (QuestTbl). Non bloquant :
// si gameDataDir_ est vide/invalide, le client démarre quand même (spawns rejetés).
void App::LoadDatabases() {
    if (gameDataDir_.empty()) {
        TS2_WARN("Bases .IMG : GameData non resolu — tables statiques indisponibles.");
        return;
    }
    if (game::LoadGameDatabases(gameDataDir_))
        TS2_LOG("Bases .IMG chargées depuis \"%s\".", gameDataDir_.c_str());
    else
        TS2_WARN("Bases .IMG présentes mais illisibles sous \"%s\".", gameDataDir_.c_str());
}

void App::FrameTick() {
    // Pas fixe 30 FPS à accumulateur (App_FrameTick 0x4625D0). Structure EXACTE de l'original
    // (re-décompilée 2026-07-16) : Terrain_PushRenderState est appelé À CHAQUE frame (0x4625DE),
    // mais poll clavier + Camera_UpdateFromInput + boucle Update + Render + purge sont TOUS
    // À L'INTÉRIEUR de la garde d'accumulateur `if (1/30 <= now - accum)` (0x4625FD) — le rendu
    // est donc couplé au pas fixe 30 FPS (une seule passe par tranche écoulée).
    gameClockSec_ = NowSeconds();   // Terrain_PushRenderState(g_GfxRenderer) 0x4625DE (chaque frame)
    // Horloge de jeu partagée (flt_815180) : lue par les blits sprite (TTL textures)
    // et le clignotement du caret des champs de saisie.
    gfx::g_GameTimeSec = static_cast<float>(gameClockSec_);
    game::g_World.gameTimeSec = static_cast<float>(gameClockSec_);

    // GARDE unique (0x4625FD) : rien ci-dessous ne tourne tant qu'un pas fixe complet ne s'est
    // pas écoulé.
    if (gameClockSec_ - frameAccumSec_ >= kFixedTimestep) {
        // Poll clavier matériel DANS la garde (Input_AcquireKeyboard(g_WindowActive) 0x46260F).
        input_.Poll(windowActive_);
        // Camera_UpdateFromInput(&g_CameraCtrl) 0x462619 : 1×/frame gardée, AVANT la boucle
        // Update. Émet le déplacement WASD (Net_SendCmd_251), gère caméra/quickslots/F12 et
        // route le reliquat clavier vers scene_.OnKeyDown.
        //
        // M7 — Camera_UpdateFromInput 0x50B7D0 fait un read-modify-write IN SITU sur
        // flt_1687330/34/38 (self entity +252 = game::g_World.Self().x/y/z, cf. GameState.h:135) :
        // il LIT la position self, y accumule le delta WASD (@0x50b870..0x50b929) puis la
        // reinjecte et l'envoie via Net_SendCmd_251. selfPos_ de PlayerInputController est un
        // modele local (defaut {0,0,0}) ; sans cette synchro, WASD partirait toujours de
        // l'origine (position serveur ignoree). On seede AVANT Update et on reinjecte APRES,
        // exactement comme le binaire opere sur le global in situ. Gate scene==InGame : (a)
        // fidele au gate g_SceneMgr==6 de 0x50b7ec ; (b) evite que Self() (qui auto-cree
        // players[0] si vide, GameState.h:552) ne fabrique un self fantome pendant Intro/Login.
        // // 0x50B7D0 / flt_1687330 (self+252)
        const bool inGameForSelf = (scene_.Current() == Scene::InGame);
        if (inGameForSelf) {
            const game::PlayerEntity& self = game::g_World.Self();  // dword_1687234[0] (self)
            g_playerInput.SetSelfPosition(self.x, self.y, self.z);  // <- flt_1687330/34/38
        }
        g_playerInput.Update(input_, camera_, net_.Client(), scene_.Current());
        if (inGameForSelf) {
            const float* p = g_playerInput.SelfPosition();          // -> flt_1687330/34/38 mutes
            game::PlayerEntity& self = game::g_World.Self();
            self.x = p[0]; self.y = p[1]; self.z = p[2];            // reinjection in situ (0x50b870..)
        }

        // Boucle de rattrapage (do/while : on est déjà dans la garde) — 0x46263B..0x462677.
        do {
            // camera_ passée en MUTABLE (cf. Scene/SceneManager.h) : même instance que celle
            // lue en const par scene_.Render() ci-dessous, requis par le câblage caméra 3e
            // personne (case Scene::InGame, Gfx/CameraThirdPersonBridge.h).
            scene_.Update(kFixedTimestep, camera_);
            frameAccumSec_ += kFixedTimestep;
        } while (gameClockSec_ - frameAccumSec_ >= kFixedTimestep);

        // Rendu DANS la garde si la fenêtre est active (0x462684 : GXD_BeginScene ... Present).
        if (windowActive_ && renderer_.Ready()) {
            if (renderer_.BeginFrame()) {
                scene_.Render(renderer_.Device(), camera_);
                renderer_.EndFrame();
            }
        }

        // Purge des assets expirés toutes les 60 s (0x4626AE).
        if (gameClockSec_ - lastPurgeSec_ >= kAssetPurgeIntervalSec) {
            lastPurgeSec_ = gameClockSec_;
            // AssetMgr_UpdateUnloadExpired(g_ModelMotionArray 0x8E8B30, now, 300.0) 0x4626D7 :
            // balaye le pool géant Sprite2D/ModelObj/Motion/SObject/Snd3D (_UnloadIfStale(slot,
            // now, ttl=300)). Le pool n'est PAS possédé par ce front (front Asset).
            // TODO [ancre 0x4E2050] : AssetMgr::PurgeExpired(kAssetTtlSec) quand le pool sera modélisé.
        }
    }
}

// App_Shutdown 0x462480 — re-décompilation FRAÎCHE idaTs2 (2026-07-15) : 33 `call` de
// démontage en chaîne, SANS garde d'échec (tout exécuté inconditionnellement), dans l'ORDRE
// EXACT ci-dessous — miroir LIFO de App_Init SAUF mSOCKET (initialisé step 21 mais libéré ICI
// en POSITION 20, tardive : seul écart au LIFO strict). LES 33 ÉTAPES SONT TOUTES REPRÉSENTÉES
// À LEUR POSITION (aucune omission silencieuse) : soit un APPEL de l'équivalent C++ existant,
// soit une note explicite [no-op] (fonction d'origine vide dans le binaire) ou [RAII] (le
// teardown est assuré par le destructeur du global/membre std::vector à la fin du process —
// effet net identique au GlobalFree d'origine, léger différé de timing assumé). Noms IDA
// trompeurs : le TAG (argument mXXX) prime sur le nom de fonction (comme pour App_Init).
void App::Shutdown() {
    TS2_LOG("App_Shutdown.");

    //  1. cGameData_DestroyPools       0x557780 (mPLAY) — APPEL : vide les 5 pools d'entités de g_World.
    game::GameData_DestroyPools();
    //  2. UI_UpdateAllDialogs          0x5AC270 (mUI) — [couvert] seule vraie action =
    //     GuildMark_ClearTextures(g_Guild) 0x667CE0 (cache de 1000 textures de marques de
    //     guilde), sous-système HORS PÉRIMÈTRE (jamais alloué côté ClientSource) ; toutes les
    //     autres callees sont des stubs vides. UIManager::Shutdown est atteint transitivement
    //     via scene_.Shutdown() (étape 6) -> GameWindows::Shutdown -> UIManager::Shutdown.
    //  3. UI_DestroyEditBoxes          0x50F440 (mEDITBOX) — [déviation] ts2::ui::EditBox
    //     autonomes remplacent les 21 EDIT Win32 natifs (aucun HWND détenu par App à détruire).
    //  4. CameraCtrl_Destruct          0x50AC80 (mINPUT) — [no-op] binaire (camera_ = valeur).
    //  5. GameAux_Destruct             0x53F2C0 (mUTIL) — [no-op] binaire (`this` jamais touché).
    //  6. SceneMgr_ReleaseSoundBuffers 0x517B60 (mMAIN) — APPEL (portée élargie : toute la scène).
    scene_.Shutdown();
    //  7. PlayerCmdController_Destruct 0x50F5C0 (mMYINFO) — [no-op] binaire.
    //  8. MotionInfo003_Destruct       0x4FD4B0 (mZONEMOVEINFO) — [no-op] binaire (GInfo003.bin).
    //  9. MotionInfo002_Destruct       0x4FD060 (mZONENPCINFO) — [no-op] binaire (GInfo002.bin).
    // 10. Motion_FrameTableFree        0x4F6F50 (mZONEMAININFO) — [no-op] binaire (table 350 lignes).
    // 11. AssetMgr_DestroyAllSlots     0x4E07F0 (mGDATA) — [no-op effectif] : InitModelMotionPool
    //     n'alloue rien (chargement async hors périmètre), donc aucun slot à libérer.
    // 12. PatTbl_Free                  0x4C8DB0 (mPAT) — [no-op] binaire (mPAT = stub à l'Init).
    // 13. QuestTbl_Free   (mHELP)      0x4C8870 — [RAII] game::g_QuestTable (std::vector).
    // 14. NpcTbl_Free     (mQUEST)     0x4C8300 — [RAII] g_ExtraDb.quest.
    // 15. SkillDefTbl_Free(mNPC)       0x4C6E60 — [RAII] g_ExtraDb.npc (noms IDA inversés).
    // 16. ItemDefTbl_Free (mMONSTER)   0x4C6530 — [RAII] g_World.db.monster.
    // 17. SkillGrowthTbl_Free(mSKILL)  0x4C4E50 — [RAII] g_World.db.skill.
    // 18. MobDb_Free      (mITEM)      0x4C3BC0 — [RAII] g_World.db.item.
    // 19. LevelTable_Free (mLEVEL)     0x4C28B0 — [no-op binaire + RAII] g_World.db.level.
    // 20. AnchorTbl_Free  (mSOCKET)    0x4C75F0 — [RAII] g_World.db.socketT. mSOCKET LIBÉRÉ ICI
    //     (position 20 tardive) alors qu'il est initialisé au step 21 : SEUL écart au LIFO strict.
    // 21. Options_Save_STUB            0x4C2130 (mGAMEOPTION) — APPEL (stub vide fidèle du binaire).
    config::g_Options.SaveStub();
    // 22. ColorTable_Free              0x4C1FD0 (mFONTCOLOR) — [no-op] binaire.
    // 23. StrTable005_Free             0x4C1D00 (mMESSAGE) — [no-op binaire + RAII g_Strings.messages].
    // 24. StrTable003_Free             0x4C1AC0 (mZONENAME) — [no-op binaire + RAII g_Strings.zoneNames].
    // 25. Tips002_Free                 0x4C1830 (mGAMENOTICE) — [no-op binaire + RAII g_Strings.notices].
    // 26. Dict001_Free                 0x4C1400 (mBADWORD) — [no-op binaire + RAII g_Strings.bannedWords].
    // 27. CursorSet_DestroyAll         0x4C10B0 (mPOINTER) — APPEL (DestroyIcon sur les 9 curseurs).
    cursors_.DestroyAll();
    // 28. AutoPlay_ShutdownStub        0x4B43B0 (mTRANSFER) — [no-op] binaire.
    // 29. Net_PacketSizeTable_Dtor     0x464150 (mWORKER) — [no-op] binaire (table de tailles ; couverte par net RAII).
    // 30. Net_Shutdown                 0x462820 (mNETWORK) — APPEL (closesocket + WSACleanup).
    net_.Shutdown();
    // 31. GXD_DeviceRelease            0x402F70 (mGXD, 2e renderer skinné g_GxdRenderer 0x18C4EF8)
    //     — [fusion] ClientSource n'a qu'UN gfx::Renderer ; les 2 singletons d'origine partagent
    //     le même device D3D9 -> libéré une SEULE fois à l'étape 32 (surtout PAS de double-release).
    gfx::GxdRenderer::Instance().Shutdown();
    // 32. Gfx_ShutdownDevice           0x69C990 (mGXD) — APPELS : l'unique fonction d'origine libère
    //     DirectInput8 PUIS DirectSound8 PUIS le device D3D9 (+ ChangeDisplaySettings) ; ClientSource
    //     sépare en 3 classes (Input/Audio/Renderer), donc 3 appels couvrent cette étape :
    input_.Shutdown();
    audio::Audio().Shutdown();
    renderer_.Shutdown();
    // 33. Font_RemoveTtfResource       0x4C0F10 (mFONTDATA) — APPEL (TOUT DERNIER appel ; contrepartie
    //     exacte de Font::AddTtfResource, 1er manager d'App_Init).
    gfx::Font::RemoveTtfResource(cfg_.useTRVariant != 0);

    // Cleanup shell C++ hors des 33 étapes (l'original détruit la fenêtre via WM_DESTROY/
    // DefWindowProc) : libère la fenêtre principale puis déscelle la classe.
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    UnregisterClassA(kWindowClassName, hInst_);
}

bool App::RegisterWindowClass() {
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &App::WndProc;
    wc.hInstance     = hInst_;
    // Fidèle WinMain 0x4611f6 (WNDCLASSEXA) : hIcon = LoadIconA(hInstance, MAKEINTRESOURCE(101)).
    // On tente l'icône ressource 101 (0x65) embarquée ; repli IDI_APPLICATION tant que
    // TwelveSky2.rc n'embarque pas cet id (ressource .exe à extraire — cf. compte rendu).
    wc.hIcon         = LoadIconA(hInst_, MAKEINTRESOURCEA(101));
    if (!wc.hIcon)
        wc.hIcon     = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    // Fidele : original = GetStockObject(BLACK_BRUSH) (EA proche 0x461278), pas une brosse
    // systeme claire — flash noir (pas blanc) pendant creation/resize de fenetre.
    wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = kWindowClassName;
    if (!RegisterClassExA(&wc)) {
        TS2_ERR("RegisterClassEx a echoue (%lu)", GetLastError());
        return false;
    }
    return true;
}

bool App::CreateGameWindow() {
    DWORD style, exStyle = 0;
    int x, y, w = cfg_.width, h = cfg_.height;

    if (cfg_.Windowed()) {
        // Fidèle WinMain 0x461321 (VÉRIFIÉ par désassemblage frais : `mov [ebp+dwStyle],
        // 10CA0000h`) : style réel = 0x10CA0000 = WS_VISIBLE|WS_CAPTION|WS_SYSMENU|
        // WS_MINIMIZEBOX — fenêtre NON redimensionnable, AVEC bouton minimiser, SANS
        // maximiser. L'ancien WS_THICKFRAME (=0x10CC0000, style inverse) était une ERREUR
        // d'un audit antérieur (roadmap §5.3) : corrigé en Vague 1 (2026-07-15).
        style = WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT r = {0, 0, w, h};
        AdjustWindowRect(&r, style, FALSE);
        const int ww = r.right - r.left;
        const int wh = r.bottom - r.top;
        // CFG-02 — ORDRE des divisions entières, fidèle au désassemblage de WinMain 0x4609C0.
        // L'original fait DEUX divisions tronquées SÉPARÉES, pas une seule sur la différence.
        // Y (@0x4613e9) : GetSystemMetrics(SM_CYSCREEN=1) puis `cdq/sub eax,edx/sar ecx,1`
        //   (= SM_CY/2), puis rc.bottom-rc.top puis `cdq/sub eax,edx/sar eax,1` (= wh/2), puis
        //   `sub ecx,eax` -> Y = SM_CY/2 - wh/2.
        // X (@0x46140c) : idem avec SM_CXSCREEN=0 et rc.right-rc.left -> X = SM_CX/2 - ww/2.
        // (`cdq/sub eax,edx/sar 1` = idiome de division signée par 2 arrondie vers zéro,
        //  strictement équivalent au `/ 2` du C sur int.)
        // Non équivalent à (SM - ww)/2 en arithmétique entière quand la dimension AJUSTÉE est
        // impaire — or ww/wh le sont typiquement (AdjustWindowRect ajoute caption + bordures) :
        // ex. SM=1080, wh=793 -> binaire 540-396=144 ; ancien C++ (1080-793)/2=143 (écart 1 px).
        x = GetSystemMetrics(SM_CXSCREEN) / 2 - ww / 2;   // 0x46140c
        y = GetSystemMetrics(SM_CYSCREEN) / 2 - wh / 2;   // 0x4613e9
        w = ww; h = wh;
    } else {
        // Fidele (audit INIT 2026-07-14) : original cree la fenetre plein ecran a la
        // resolution DEMANDEE en cmdline (nWidth/nHeight, cfg_.width/height ici), PAS a la
        // resolution du bureau (GetSystemMetrics) — et pose WS_EX_APPWINDOW (absent avant).
        style = WS_POPUP;
        exStyle = WS_EX_APPWINDOW;
        x = 0; y = 0;
        w = cfg_.width; h = cfg_.height;
    }

    hwnd_ = CreateWindowExA(exStyle, kWindowClassName, kWindowTitle, style,
                            x, y, w, h, nullptr, nullptr, hInst_, this);
    if (!hwnd_) {
        TS2_ERR("CreateWindowEx a echoue (%lu)", GetLastError());
        return false;
    }
    return true;
}

LRESULT CALLBACK App::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    App* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        self = static_cast<App*>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    } else {
        self = reinterpret_cast<App*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }
    if (self)
        return self->HandleMessage(hwnd, msg, wParam, lParam);
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

LRESULT App::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MOUSEWHEEL:
        input_.ProcessMessage(msg, wParam, lParam);
        break;
    default:
        break;
    }

    switch (msg) {
    case WM_ACTIVATEAPP:            // 0x1C : g_WindowActive = wParam (0x4619D0)
        windowActive_ = (wParam != FALSE);
        return 0;

    case WM_MOUSEMOVE: {            // 0x200 -> Camera_MouseDragRotate 0x50AFD0 (@0x461B22)
        // INPUT-01. L'original met à jour this+52/+56 (dernière position souris) sur TOUS ses
        // chemins de sortie (@0x50AFF1, @0x50B00E, @0x50B05E, @0x50B2DA, @0x50B2F1) : le delta
        // est donc TOUJOURS consommé, y compris quand il n'orbite pas. On vide donc
        // l'accumulateur INCONDITIONNELLEMENT — sinon un drag faisant suite à un déplacement
        // libre rejouerait tout le trajet accumulé d'un coup (saut d'orbite).
        int dx = 0, dy = 0;
        input_.GetMouseDelta(dx, dy);          // <- this+52/+56 (0x50AFF1/0x50B00E/0x50B2F1)
        // Gardes de l'original, dans l'ordre :
        //  1. @0x50AFE9 : g_SceneMgr != 6 || g_SceneSubState != 4 -> pas d'orbite.
        //  2. @0x50B006 : `if (a4 != 2)` où a4 = wParam -> ÉGALITÉ STRICTE à MK_RBUTTON (=2) :
        //     le bouton droit SEUL orbite ; Maj+clic droit (MK_SHIFT|MK_RBUTTON = 6) ou
        //     gauche+droit (MK_LBUTTON|MK_RBUTTON = 3) n'orbitent PAS. `==` et non `&`.
        //  3. @0x50B056 : `(!g_SelfCharInvBlock[0] && !*(this+8) && g_SelfMorphNpcId == 194)
        //                || (!g_SelfCharInvBlock[0] &&  *(this+8) && g_CamMode != 1)` -> pas
        //     d'orbite (this+8 = mouseLook de g_CameraCtrl 0x1668F60, cf.
        //     PlayerInputController.h:24). NON MODÉLISÉE ici : g_SelfMorphNpcId 0x1675A98
        //     et g_CamMode 0x1668F6C
        //     sont hors de ce front. Avec les valeurs par défaut du binaire (blocked=0,
        //     mouseLook=0, morphNpcId != 194) les DEUX clauses sont FAUSSES -> l'orbite passe,
        //     ce qui est exactement le comportement de l'original dans le même état.
        //     TODO [ancre 0x50B056] : câbler la 3e garde quand g_SelfMorphNpcId/g_CamMode
        //     seront modélisés (front Gfx/Game).
        if (scene_.Current() == Scene::InGame && g_SceneSubState == 4 && wParam == MK_RBUTTON) {
            // v13 = (mx - *(this+52)) * *(this+60)(=0.2) -> Cam_OrbitYaw   @0x50B0BA/@0x50B0C9
            // v12 = (my - *(this+56)) * *(this+64)(=0.3) -> Cam_OrbitPitch @0x50B0E3/@0x50B0F2
            // (offsets en OCTETS ; sensibilités posées par Camera_Init @0x50AC1F/@0x50AC2B ;
            //  Camera::OrbitByMouse applique littéralement ces deux constantes, Camera.cpp:74-79.)
            camera_.OrbitByMouse(dx, dy);
            // NON TRANSPOSÉ (front Gfx) : le clamp d'élévation *(this+68) = 30.0 /
            // *(this+72) = 80.0 (@0x50B26A / @0x50B1BA) qui ANNULE l'orbite hors bornes en
            // réappliquant Cam_SetLookAt sur l'œil mémorisé, et la renormalisation de distance
            // à *(this+80) (@0x50B3C7). TODO [ancre 0x50B26A] : porter ces bornes dans gfx::Camera.
        }
        return 0;                              // 0x461B29
    }

    case WM_LBUTTONDOWN:            // 0x201 : SetCapture + Input_OnLButtonDown 0x50AC90
        scene_.OnLButtonDown(static_cast<short>(LOWORD(lParam)),
                             static_cast<short>(HIWORD(lParam)));
        return 0;
    case WM_LBUTTONUP:              // 0x202 : ReleaseCapture + Input_OnLButtonUp 0x50AD20
        scene_.OnLButtonUp(static_cast<short>(LOWORD(lParam)),
                           static_cast<short>(HIWORD(lParam)));
        return 0;

    // 0x204/0x205/0x207/0x20A : le travail est fait par les hooks InputSystem posés dans
    // App::Init (déjà déclenchés par input_.ProcessMessage dans le switch ci-dessus, comme le
    // WndProc d'origine appelle Input_OnRButtonDown/Camera_ResetView/Camera_MouseWheelZoom).
    // Ces cases n'existent que pour rendre 0 comme l'original (@0x461A94/@0x461AC8/@0x461AF5/
    // @0x461B44) au lieu de tomber dans DefWindowProc.
    case WM_RBUTTONDOWN:            // 0x204 -> Input_OnRButtonDown 0x50ADB0 (@0x461A8F)
    case WM_RBUTTONUP:              // 0x205 -> Input_OnRButtonUp   0x50AE40 (@0x461AC3)
    case WM_MBUTTONDOWN:            // 0x207 -> Camera_ResetView    0x50AED0 (@0x461AF0)
    case WM_MOUSEWHEEL:             // 0x20A -> Camera_MouseWheelZoom 0x50B460 (@0x461B3F)
        return 0;

    case kWM_Socket: // 0x401 : notification socket asynchrone (WSAAsyncSelect) -> 0x4619E9
        net_.OnSocketMessage(wParam, lParam);
        return 0;

    case WM_CHAR:
        // ABSENT du binaire (App_WndProc n'a NI WM_CHAR NI WM_KEYDOWN générique) — DÉVIATION
        // COMPENSATOIRE ASSUMÉE et nécessaire : le client d'origine confie la saisie texte aux
        // 21 EDIT Win32 natifs sous-classés de mEDITBOX (UI_CreateEditBoxes 0x50E460), qui
        // consomment WM_CHAR eux-mêmes ; ClientSource les remplace par des ts2::ui::EditBox
        // autonomes (cf. mEDITBOX ci-dessus), qui doivent donc être alimentés ici. Conservé.
        scene_.OnChar(static_cast<char>(wParam));
        return 0;

    case WM_KEYDOWN:
        // GAP-APPLIFE-01 / INPUT-08 — la branche `if (wParam == VK_ESCAPE) PostQuitMessage(0);`
        // a été SUPPRIMÉE : elle était une invention. Le case 256 du binaire (@0x46197E) ne
        // traite QU'UNE touche — `if (a3 == 13) { UI_Chat_FocusInput(); return 0; }` @0x461B55
        // — et tout le reste part à DefWindowProc, pour lequel VK_ESCAPE est un NO-OP TOTAL.
        // Le client d'origine ne quitte JAMAIS au clavier : ses deux seules sorties sont
        // g_QuitFlag 0x815590 (boutons Quitter de l'UI, lu par WinMain @0x46162B ; miroir
        // quit_) et WM_DESTROY -> PostQuitMessage (@0x461BC3). Échap doit donc atteindre le
        // routeur de dialogues (fermeture du dialogue focalisé), pas fermer le jeu.
        //
        // INPUT-04 / GAP-APPLIFE-04 — NON APPLIQUÉ ICI, DÉLIBÉRÉMENT (chantier inter-front
        // ATOMIQUE, cf. rapport). Le constat est exact : ce chemin injecte des VK alors que le
        // hook DirectInput d'App::Init (SetSceneKeyDownHook ci-dessus) injecte des DIK dans le
        // MÊME SceneManager::OnKeyDown, et le clavier in-game du binaire est 100% DIK
        // (Game_OnHotkey 0x537330 relit le tampon DirectInput et compare des scancodes bruts,
        // ex. `cmp g_UiCmdQueueRecords[eax], 15h` @0x5373A0 = DIK_Y). Collisions réelles :
        // DIK_EQUALS=0x0D lu comme VK_RETURN, DIK_7=0x08 comme VK_BACK, DIK_Y=0x15 comme
        // VK_CAPITAL.
        // MAIS restreindre ce case aux seules scènes Login/CharSelect (le correctif proposé)
        // ne peut PAS être fait par ce front SEUL : tous les consommateurs clavier in-game sont
        // indexés en VK (UIManager::RouteKey <- Dialog::OnKey : `vk == VK_ESCAPE` dans
        // GuildWindow.cpp:631/648, OptionsWindow.cpp:265, SkillTreeWindow.cpp:453,
        // VendorShopWindow.cpp:335, NpcDialogWindow.cpp:320, PlayerTradeWindow.cpp:349... ;
        // GameWindows::HandleHotkey compare 'I'/'C'/'O'). Couper la source VK avant que le
        // front UI n'ait converti ces tables en DIK laisserait ces consommateurs SANS AUCUNE
        // alimentation : Échap ne fermerait plus aucun dialogue et les hotkeys mourraient —
        // une RÉGRESSION nette, et l'inverse exact de ce qu'exige GAP-APPLIFE-01 ci-dessus
        // (« Échap doit atteindre le routeur de dialogues »). Les deux moitiés du correctif
        // doivent donc atterrir ENSEMBLE (App + Scene + UI). Le feed VK est conservé tel quel
        // en attendant. TODO [ancre 0x537330] : migration DIK atomique, cf. rapport de front.
        scene_.OnKeyDown(static_cast<int>(wParam));
        // TODO [ancre 0x461B5E] : Entrée (VK_RETURN=13) -> UI_Chat_FocusInput 0x68B200, seul
        // traitement clavier du WndProc d'origine. Non câblable depuis ce front : SceneManager
        // n'expose aucun accès au ChatWindow (ni UI() ni Chat()), et ChatWindow n'est de toute
        // façon pas enregistré dans UIManager en InGame (Focus() n'a aucun appelant) — cf.
        // rapport de front, chantier du front Scene/UI.
        return 0;

    case WM_SYSCOMMAND: {
        // INPUT-07 / GAP-APPLIFE-03 — filtre @0x461B70-0x461BB9, absent jusqu'ici (tout tombait
        // dans DefWindowProc). `if (dword_1669180 != 2) return 0;` @0x461B70 : hors mode 2,
        // TOUTE commande système est avalée. dword_1669180 est PROUVÉ == GameConfig::windowMode
        // (data_refs 0x1669180 : écrit @0x460CE2 dans le parse cmdline de WinMain, puis comparé
        // `cmp ds:dword_1669180, 2` en trois points — @0x461314 choix du style de fenêtre,
        // @0x461B69 ici, @0x461D17 App_Init) ; `== 2` == fenêtré == cfg_.Windowed()
        // (GameConfig.h:16). En PLEIN ÉCRAN, donc, tout est bloqué.
        if (!cfg_.Windowed())
            return 0;                          // 0x461BB9
        const WPARAM sc = wParam & 0xFFF0;     // 0x461B7B
        // Seules 4 commandes sont relayées à DefWindowProc : SC_MOVE 0xF010 / SC_MINIMIZE
        // 0xF020 / SC_MAXIMIZE 0xF030 (@0x461BA0) et SC_RESTORE 0xF120 (@0x461BAB).
        if (sc == SC_MOVE || sc == SC_MINIMIZE || sc == SC_MAXIMIZE || sc == SC_RESTORE)
            return DefWindowProcA(hwnd, msg, wParam, lParam);
        // Tout le reste -> 0 (@0x461BB3) : bloque notamment SC_CLOSE (0xF060), SC_SIZE
        // (0xF000), SC_KEYMENU (0xF100, Alt/Alt+Espace = gel de la boucle), SC_SCREENSAVE
        // (0xF140) et SC_MONITORPOWER (0xF170) — l'économiseur d'écran et la mise en veille du
        // moniteur ne peuvent donc pas se déclencher en pleine partie.
        return 0;
    }

    case WM_CLOSE:
        // 0x10 -> `xor eax,eax` / `return 0` @0x461BBD-0x461BBF, SANS appeler DefWindowProc :
        // la fenêtre n'est JAMAIS détruite par la croix ni par Alt+F4. Sans ce case, le message
        // tombait dans DefWindowProc, qui le traduit en DestroyWindow -> WM_DESTROY ->
        // PostQuitMessage : le client se fermait là où l'original refuse. Le seul chemin de
        // destruction légitime reste App::Shutdown (DestroyWindow explicite).
        return 0;

    case WM_DESTROY:                // 0x02 : seul émetteur de PostQuitMessage (@0x461BC3)
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);   // 0x461BDD
    }
}

// -----------------------------------------------------------------------------
// RButtonGateOpen — garde d'état commune à Input_OnRButtonDown 0x50ADB0 (@0x50AE17) et
// Input_OnRButtonUp 0x50AE40 (@0x50AEA7). Les deux fonctions portent une condition
// STRICTEMENT identique (vérifiée par décompilation des deux) :
//
//   if ( (g_SceneMgr != 6 || g_SceneSubState != 4 || g_SelfActionState[0] ∉ {11,12,33,34,35,36,37})
//        && !UI_RouteRButton*(x, y) )
//       cSceneMgr_OnRButton*(&g_SceneMgr, x, y);
//
// Autrement dit : quand on est en jeu (scène 6), en sous-état MainTick (4) ET que l'état
// d'action du joueur self appartient à cet ensemble de 7 valeurs, le clic droit est
// INTÉGRALEMENT avalé — il n'atteint ni l'UI ni la scène. Cette fonction renvoie donc
// l'inverse de cette condition d'avalement.
// -----------------------------------------------------------------------------
bool App::RButtonGateOpen() const {
    // 0x50AE17 : g_SceneMgr != 6 || g_SceneSubState != 4 -> porte ouverte d'office.
    if (scene_.Current() != Scene::InGame || g_SceneSubState != 4)
        return true;

    // g_SelfActionState[0] 0x1687328 == g_World.players[0].body @ kSelfActionStateOffset
    // (Game/MapWarp.h) — même dérivation que SceneManager.cpp:1081-1091 (host.GetSelfActionState).
    int32_t actionState = 0;
    if (!game::g_World.players.empty()) {
        const game::PlayerEntity& self0 = game::g_World.players[0];
        if (self0.body.size() >= game::kSelfActionStateOffset + sizeof(actionState))
            std::memcpy(&actionState, self0.body.data() + game::kSelfActionStateOffset,
                        sizeof(actionState));
    }

    // Les 7 valeurs littérales du test (@0x50AE17) : 11, 12, 33, 34, 35, 36, 37.
    switch (actionState) {
    case 11: case 12: case 33: case 34: case 35: case 36: case 37:
        return false;   // clic droit intégralement avalé
    default:
        return true;
    }
}

double App::NowSeconds() const {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart - perfBase_) / static_cast<double>(perfFreq_);
}

} // namespace ts2

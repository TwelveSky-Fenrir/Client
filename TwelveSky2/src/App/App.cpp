// App/App.cpp — implémentation de l'application et de la boucle principale.
#include "App/App.h"
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
#include <cstdlib>
#include <cstdio>
#include <cstring>
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
    // Pas fixe 30 FPS à accumulateur (App_FrameTick 0x4625D0).
    gameClockSec_ = NowSeconds();
    // Horloge de jeu partagée (flt_815180) : lue par les blits sprite (TTL textures)
    // et le clignotement du caret des champs de saisie.
    gfx::g_GameTimeSec = static_cast<float>(gameClockSec_);
    game::g_World.gameTimeSec = static_cast<float>(gameClockSec_);

    // Poll clavier matériel (Input_AcquireKeyboard(g_WindowActive) 0x6A2130).
    input_.Poll(windowActive_);
    while (gameClockSec_ - frameAccumSec_ >= kFixedTimestep) {
        // camera_ passée en MUTABLE (cf. Scene/SceneManager.h) : même instance que celle
        // lue en const par scene_.Render() ci-dessous, requis par le câblage caméra 3e
        // personne (case Scene::InGame, Gfx/CameraThirdPersonBridge.h).
        scene_.Update(kFixedTimestep, camera_);
        frameAccumSec_ += kFixedTimestep;
    }
    // Rendu 1×/frame si la fenêtre est active (GXD_BeginScene ... Present).
    if (windowActive_ && renderer_.Ready()) {
        if (renderer_.BeginFrame()) {
            scene_.Render(renderer_.Device(), camera_);
            renderer_.EndFrame();
        }
    }

    // Purge des assets expirés toutes les 60 s (TTL 300 s).
    if (gameClockSec_ - lastPurgeSec_ >= kAssetPurgeIntervalSec) {
        lastPurgeSec_ = gameClockSec_;
        // TODO (jalon Assets) : AssetMgr::PurgeExpired(kAssetTtlSec);
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
        x = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
        y = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
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
    case WM_ACTIVATEAPP:
        windowActive_ = (wParam != FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        scene_.OnLButtonDown(static_cast<short>(LOWORD(lParam)),
                             static_cast<short>(HIWORD(lParam)));
        return 0;
    case WM_LBUTTONUP:
        scene_.OnLButtonUp(static_cast<short>(LOWORD(lParam)),
                           static_cast<short>(HIWORD(lParam)));
        return 0;
    case kWM_Socket: // 0x401 : notification socket asynchrone (WSAAsyncSelect)
        net_.OnSocketMessage(wParam, lParam);
        return 0;
    case WM_CHAR: // saisie texte (login/chat) -> champ focalisé
        scene_.OnChar(static_cast<char>(wParam));
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { PostQuitMessage(0); }
        else scene_.OnKeyDown(static_cast<int>(wParam));
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wParam, lParam);
    }
}

double App::NowSeconds() const {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return static_cast<double>(c.QuadPart - perfBase_) / static_cast<double>(perfFreq_);
}

} // namespace ts2

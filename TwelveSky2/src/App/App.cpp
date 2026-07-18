// App/App.cpp — application and main loop implementation.
#include "App/App.h"
#include "App/App_Internal.h"     // shared g_playerInput (App.cpp/App_Init.cpp/App_WndProc.cpp split family)
#include "App/PlayerInputController.h"  // W1-F2 : Camera_UpdateFromInput 0x50B7D0 (g_CameraCtrl 0x1668F60)
#include "Core/Types.h"
#include "Core/Log.h"
#include "Asset/AssetSelfTest.h"
#include "UI/UIManager.h"         // ui::UIManager: right-click routers (UI_RouteRButtonDown 0x5AD5D0 / Up 0x5ADA90)
#include "Gfx/SpriteBatch.h"      // gfx::g_GameTimeSec (sprite blit clock)
#include "Gfx/Font.h"             // mFONTDATA : Font::AddTtfResource
#include "Game/GameDatabase.h"    // game::LoadGameDatabases (tables .IMG 005_*)
#include "Game/QuestSystem.h"     // mHELP : game::LoadQuestTable (005_00007.IMG)
#include "Game/StringTables.h"    // mBADWORD/mGAMENOTICE/mZONENAME/mMESSAGE/mFONTCOLOR
#include "Game/ExtraDatabases.h"  // mNPC/mQUEST : 2 additional .IMG tables
#include "Game/MotionPools.h"     // mGDATA/mZONEMAININFO/mZONENPCINFO/mZONEMOVEINFO
#include "Game/MiscManagers.h"    // mTRANSFER/mPOINTER/mUTIL/mMYINFO/mPLAY
#include "Config/GameOptions.h"   // mGAMEOPTION : g_Options (23 fields, 001.BIN)
#include "Audio/AudioSystem.h"    // DirectSound8 (device init, master volume)
#include "Gfx/GxdRenderer.h"      // g_GxdRenderer: shares the D3D9 device of g_GfxRenderer
#include "Net/Rng.h"              // net::DefaultRng() — seeded here, see App_Init 0x461C20 EA 0x461C3E
#include "Net/ClientState.h"      // net::g_MorphInProgress = g_MorphInProgress 0x1675A88 (guard @0x50B857)
#include "Game/MapWarp.h"         // game::kSelfActionStateOffset -> g_SelfActionState[0] 0x1687328 (@0x50AE17)
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>                  // std::time — srand(time(NULL)) of App_Init (EA 0x461C35)
#include <string>
// Intro AVI (App::Run, faithful to WinMain 0x4609C0 -> PlayShow_PlayVideoFile 0x6D70A0):
// WIN32_LEAN_AND_MEAN (defined at project level) excludes ole2.h/objbase.h from <windows.h> ->
// explicitly required for CoInitializeEx/CoCreateInstance/CoUninitialize. <dshow.h> provides
// IGraphBuilder/IMediaControl/IVideoWindow/IMediaEventEx (lib: strmiids.lib, see .vcxproj).
#include <objbase.h>
#include <dshow.h>
#include <imm.h>               // ImmGetDefaultIMEWnd — IME deactivation (WinMain 0x4615a5)
#pragma comment(lib, "imm32.lib") // imm32 not listed in the .vcxproj (not editable here)

namespace ts2 {

int App::Run(HINSTANCE hInstance, const char* cmdLine) {
    hInst_ = hInstance;

    // Asset-layer self-test mode: "-assettest <GameDataPath>".
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
        // Faithful: the original rejects a malformed cmdline. In dev, defaults are tolerated
        // so the client can launch without arguments (/0/0/2/1024/768).
        TS2_WARN("Ligne de commande invalide/absente -> defauts /0/0/2/%d/%d.", kRefWidth, kRefHeight);
        cfg_.valid = true;
    }
    TS2_LOG("Config : build=%d TR=%d mode=%d (%s) %dx%d",
            cfg_.buildVariant, cfg_.useTRVariant, cfg_.windowMode,
            cfg_.Windowed() ? "fenetre" : "plein-ecran", cfg_.width, cfg_.height);

    // Single instance (faithful: FindWindowA("TwelveSky2")).
    if (FindWindowA(kWindowClassName, nullptr)) {
        MessageBoxA(nullptr, "TwelveSky2 est deja lance.", kWindowTitle, MB_OK | MB_ICONWARNING);
        return 0;
    }

    if (!RegisterWindowClass()) return 1;
    if (!CreateGameWindow())    return 1;

    // Intro: BLOCKING playback of GameData/INTRO.AVI (DirectShow). Faithful to WinMain 0x4609C0
    // (EA 0x4614e6-0x4614eb, confirmed by 2 independent decompilations): PlayShow_PlayVideoFile
    // ("INTRO.AVI", hInstance, hWnd) 0x6D70A0 is called right after main-window creation and
    // BEFORE ShowWindow/UpdateWindow/App_Init — hence HERE, not later. Mechanism confirmed by
    // decompiling PlayShow_PlayVideoFile and its callees (PlayShow_CreateWindow 0x6D6B30,
    // PlayShow_InitFilterGraph 0x6D6C20, PlayShow_RenderFileAndRun 0x6D6E40,
    // PlayShow_SetFullScreen 0x6D6CF0, PlayShow_ReleaseInterfaces 0x6D67C0, PlayShow_WndProc
    // 0x6D69B0): a minimal DirectShow "GraphBuilder" — CoInitializeEx(APARTMENTTHREADED),
    // CoCreateInstance(CLSID_FilterGraph) -> IGraphBuilder -> QueryInterface IMediaControl/
    // IVideoWindow/IMediaEventEx, IGraphBuilder::RenderFile(wide path), IVideoWindow::
    // put_Owner(dedicated "Play Show" window, 0x0 at (0,0) — rendering goes through the
    // DirectShow fullscreen overlay, not a visible window), put_FullScreenMode(OATRUE) + message
    // drain to that same window, IMediaControl::Pause() THEN Run() (exact original order),
    // message pump + IMediaEventEx::GetEvent until EC_COMPLETE/EC_USERABORT.
    // ASSUMED SIMPLIFICATION (documented): the original routes Esc/Enter/Space through a
    // dedicated PlayShow_WndProc that calls IMediaControl::Stop() then posts WM_CLOSE; here
    // these 3 keys are detected DIRECTLY in the message pump below (same result — early exit
    // from playback — without a separate WndProc, DefWindowProcA suffices since no visible
    // window is drawn). Clean failure at each step (missing file, DirectShow/AVI codec
    // unavailable): log and continue without crashing, exactly like the original
    // (PlayShow_PlayVideoFile never aborts WinMain).
    {
        const char* const kIntroClassName = "TS2IntroPlayShow";
        WNDCLASSA wcIntro   = {};
        wcIntro.lpfnWndProc = DefWindowProcA;
        wcIntro.hInstance   = hInstance;
        wcIntro.lpszClassName = kIntroClassName;
        const bool introClassOk = RegisterClassA(&wcIntro) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;

        // Resolve the INTRO.AVI path: ResolveGameDataDir() (App_Init) has not run yet at this
        // point (faithful: intro plays BEFORE App_Init) -> probes the same hardcoded candidates
        // to stay usable in dev (exe far from the GameData root).
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
                // Fix (runtime check 2026-07-14): a 0x0 window created without WS_VISIBLE NEVER
                // automatically receives keyboard focus -> the PeekMessageA(hIntroWnd,...) pump
                // below then never sees a WM_KEYDOWN, making Esc/Enter/Space inoperative (playback
                // blocked for the file's full duration, no manual escape). Force focus explicitly.
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
                            // Faithful to PlayShow_RenderFileAndRun/SetFullScreen: Owner, then, if
                            // not already fullscreen, message drain + FullScreenMode(OATRUE).
                            pVidWin->put_Owner(reinterpret_cast<OAHWND>(hIntroWnd));
                            long fsMode = 0;
                            pVidWin->get_FullScreenMode(&fsMode);
                            if (!fsMode) {
                                OAHWND hDrainUnused = 0;
                                pVidWin->get_MessageDrain(&hDrainUnused);
                                pVidWin->put_MessageDrain(reinterpret_cast<OAHWND>(hIntroWnd));
                                pVidWin->put_FullScreenMode(OATRUE);
                            }
                            HRESULT hrPause = pControl->Pause(); // exact original order (Pause before Run)
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
                                        playing = false; // manual skip (faithful to PlayShow_WndProc 0x6D69B0)
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

    // Faithful to WinMain 0x4609C0 (order VERIFIED by fresh disassembly 0x4614fb-0x461513):
    // ShowWindow THEN UpdateWindow ARE CALLED BEFORE App_Init. The window — created with
    // WS_VISIBLE — therefore displays black while all 32 managers load.
    // (The previous C++ Init->ShowWindow order was reversed.)
    ShowWindow(hwnd_, SW_SHOW);   // 0x4614fb (original passes nCmdShow; SW_SHOW here — main.cpp not editable, see report)
    UpdateWindow(hwnd_);          // 0x461505

    if (!Init()) {                // App_Init 0x461513
        // Faithful to 0x46151f-0x461536 then 0x461591: MessageBox "[Error::ApplicationInit()]",
        // App_Shutdown, then return 0 (xor eax,eax). Each manager has already shown its own
        // "[Error::mXXX.Init()]"; this is the second, generic MessageBox.
        MessageBoxA(hwnd_, "[Error::ApplicationInit()]", kWindowTitle, MB_OK | MB_ICONERROR);
        Shutdown();
        return 0;
    }

    // IME deactivation (WinMain 0x461598-0x4615ab, App_Init success path):
    // SendMessageA(ImmGetDefaultIMEWnd(hWnd), WM_IME_CONTROL(0x283), IMC_CLOSESTATUSWINDOW(0x21), 0).
    if (HWND hIme = ImmGetDefaultIMEWnd(hwnd_))
        SendMessageA(hIme, WM_IME_CONTROL, IMC_CLOSESTATUSWINDOW, 0);

    // Faithful main loop (WinMain 0x4615e9-0x461640): non-blocking PeekMessage;
    // Cursor_AnimateTick on EVERY iteration (both the message branch AND the frame branch).
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
        // Cursor_AnimateTick(&dword_8E714C) 0x4c1140 (0x46163b): re-asserts SetCursor(active
        // slot) every iteration. Gated on successful cursor loading (mPOINTER): without the
        // RT_GROUP_CURSOR resources (.rc missing) the slots are null, and a SetCursor(NULL) per
        // frame would hide the cursor — documented dev deviation.
        if (cursorsReady_)
            game::Cursors().AnimateTick(); // UNIQUE singleton (mPOINTER 0x8E714C), see C-cursor
    }

    Shutdown();                                            // App_Shutdown 0x461642
    return static_cast<int>(msg.wParam);                   // return Msg.wParam
}

void App::FrameTick() {
    // Fixed-step 30 FPS with accumulator (App_FrameTick 0x4625D0). EXACT structure of the
    // original (re-decompiled 2026-07-16): Terrain_PushRenderState runs on EVERY frame (0x4625DE),
    // but keyboard poll + Camera_UpdateFromInput + Update loop + Render + purge are ALL INSIDE the
    // accumulator guard `if (1/30 <= now - accum)` (0x4625FD) — rendering is therefore coupled to
    // the fixed 30 FPS step (a single pass per elapsed slice).
    gameClockSec_ = NowSeconds();   // Terrain_PushRenderState(g_GfxRenderer) 0x4625DE (every frame)
    // Shared game clock (flt_815180): read by sprite blits (texture TTL)
    // and the caret blink of text-input fields.
    gfx::g_GameTimeSec = static_cast<float>(gameClockSec_);
    game::g_World.gameTimeSec = static_cast<float>(gameClockSec_);

    // Single GUARD (0x4625FD): nothing below runs until a full fixed step has elapsed.
    if (gameClockSec_ - frameAccumSec_ >= kFixedTimestep) {
        // Hardware keyboard poll INSIDE the guard (Input_AcquireKeyboard(g_WindowActive) 0x46260F).
        input_.Poll(windowActive_);
        // Camera_UpdateFromInput(&g_CameraCtrl) 0x462619: 1x/frame, gated, BEFORE the Update
        // loop. Emits WASD movement (Net_SendCmd_251), handles camera/quickslots/F12, and routes
        // leftover keyboard input to scene_.OnKeyDown.
        //
        // M7 — Camera_UpdateFromInput 0x50B7D0 does an IN-SITU read-modify-write on
        // flt_1687330/34/38 (self entity +252 = game::g_World.Self().x/y/z, see GameState.h:135):
        // it READS the self position, accumulates the WASD delta into it (@0x50b870..0x50b929),
        // then writes it back and sends it via Net_SendCmd_251. PlayerInputController's selfPos_
        // is a local model (default {0,0,0}); without this sync, WASD would always start from the
        // origin (server position ignored). Seeded BEFORE Update and written back AFTER, exactly
        // as the binary operates on the global in situ. Gate scene==InGame: (a) faithful to the
        // g_SceneMgr==6 gate at 0x50b7ec; (b) avoids Self() (which auto-creates players[0] when
        // empty, GameState.h:552) fabricating a ghost self during Intro/Login.
        // 0x50B7D0 / flt_1687330 (self+252)
        const bool inGameForSelf = (scene_.Current() == Scene::InGame);
        if (inGameForSelf) {
            const game::PlayerEntity& self = game::g_World.Self();  // dword_1687234[0] (self)
            g_playerInput.SetSelfPosition(self.x, self.y, self.z);  // <- flt_1687330/34/38
        }
        g_playerInput.Update(input_, camera_, net_.Client(), scene_.Current());
        if (inGameForSelf) {
            const float* p = g_playerInput.SelfPosition();          // -> flt_1687330/34/38 mutated
            game::PlayerEntity& self = game::g_World.Self();
            self.x = p[0]; self.y = p[1]; self.z = p[2];            // write-back in situ (0x50b870..)
        }

        // Catch-up loop (do/while: already inside the guard) — 0x46263B..0x462677.
        do {
            // camera_ passed as MUTABLE (see Scene/SceneManager.h): same instance read as const
            // by scene_.Render() below, required by the third-person camera wiring
            // (case Scene::InGame, Gfx/CameraThirdPersonBridge.h).
            scene_.Update(kFixedTimestep, camera_);
            frameAccumSec_ += kFixedTimestep;
        } while (gameClockSec_ - frameAccumSec_ >= kFixedTimestep);

        // Render INSIDE the guard when the window is active (0x462684: GXD_BeginScene ... Present).
        if (windowActive_ && renderer_.Ready()) {
            if (renderer_.BeginFrame()) {
                scene_.Render(renderer_.Device(), camera_);
                renderer_.EndFrame();
            }
        }

        // Purge of expired assets every 60 s (0x4626AE).
        if (gameClockSec_ - lastPurgeSec_ >= kAssetPurgeIntervalSec) {
            lastPurgeSec_ = gameClockSec_;
            // AssetMgr_UpdateUnloadExpired(g_ModelMotionArray 0x8E8B30, now, 300.0) 0x4626D7:
            // sweeps the giant Sprite2D/ModelObj/Motion/SObject/Snd3D pool (_UnloadIfStale(slot,
            // now, ttl=300)). The pool is NOT owned by this front (Asset front).
            // TODO [ancre 0x4E2050] : AssetMgr::PurgeExpired(kAssetTtlSec) once the pool is modeled.
        }
    }
}

// App_Shutdown 0x462480 — FRESH re-decompilation via idaTs2 (2026-07-15): 33 chained teardown
// `call`s, WITH NO failure guard (everything runs unconditionally), in the EXACT order below —
// LIFO mirror of App_Init EXCEPT mSOCKET (initialized at step 21 but freed HERE at late
// POSITION 20 — the only departure from strict LIFO). ALL 33 STEPS ARE REPRESENTED AT THEIR
// POSITION (no silent omission): either a CALL to the existing C++ equivalent, or an explicit
// [no-op] note (original function empty in the binary) or [RAII] note (teardown is handled by
// the global/member std::vector destructor at process end — net effect identical to the
// original GlobalFree, slight timing deferral assumed). Misleading IDA names: the TAG (mXXX
// argument) takes precedence over the function name (as for App_Init).
void App::Shutdown() {
    TS2_LOG("App_Shutdown.");

    //  1. cGameData_DestroyPools       0x557780 (mPLAY) — CALL: empties g_World's 5 entity pools.
    game::GameData_DestroyPools();
    //  2. UI_UpdateAllDialogs          0x5AC270 (mUI) — [covered] only real action =
    //     GuildMark_ClearTextures(g_Guild) 0x667CE0 (cache of 1000 guild-mark textures),
    //     subsystem OUT OF SCOPE (never allocated on the ClientSource side); all other callees
    //     are empty stubs. UIManager::Shutdown is reached transitively via scene_.Shutdown()
    //     (step 6) -> GameWindows::Shutdown -> UIManager::Shutdown.
    //  3. UI_DestroyEditBoxes          0x50F440 (mEDITBOX) — [deviation] standalone
    //     ts2::ui::EditBox replace the 21 native Win32 EDIT controls (no HWND owned by App to destroy).
    //  4. CameraCtrl_Destruct          0x50AC80 (mINPUT) — [no-op] in the binary (camera_ = value type).
    //  5. GameAux_Destruct             0x53F2C0 (mUTIL) — [no-op] in the binary (`this` never touched).
    //  6. SceneMgr_ReleaseSoundBuffers 0x517B60 (mMAIN) — CALL (widened scope: the whole scene).
    scene_.Shutdown();
    //  7. PlayerCmdController_Destruct 0x50F5C0 (mMYINFO) — [no-op] in the binary.
    //  8. MotionInfo003_Destruct       0x4FD4B0 (mZONEMOVEINFO) — [no-op] in the binary (GInfo003.bin).
    //  9. MotionInfo002_Destruct       0x4FD060 (mZONENPCINFO) — [no-op] in the binary (GInfo002.bin).
    // 10. Motion_FrameTableFree        0x4F6F50 (mZONEMAININFO) — [no-op] in the binary (350-row table).
    // 11. AssetMgr_DestroyAllSlots     0x4E07F0 (mGDATA) — [effectively no-op]: InitModelMotionPool
    //     allocates nothing (async loading out of scope), so there is no slot to free.
    // 12. PatTbl_Free                  0x4C8DB0 (mPAT) — [no-op] in the binary (mPAT = stub at Init).
    // 13. QuestTbl_Free   (mHELP)      0x4C8870 — [RAII] game::g_QuestTable (std::vector).
    // 14. NpcTbl_Free     (mQUEST)     0x4C8300 — [RAII] g_ExtraDb.quest.
    // 15. SkillDefTbl_Free(mNPC)       0x4C6E60 — [RAII] g_ExtraDb.npc (IDA names swapped).
    // 16. ItemDefTbl_Free (mMONSTER)   0x4C6530 — [RAII] g_World.db.monster.
    // 17. SkillGrowthTbl_Free(mSKILL)  0x4C4E50 — [RAII] g_World.db.skill.
    // 18. MobDb_Free      (mITEM)      0x4C3BC0 — [RAII] g_World.db.item.
    // 19. LevelTable_Free (mLEVEL)     0x4C28B0 — [binary no-op + RAII] g_World.db.level.
    // 20. AnchorTbl_Free  (mSOCKET)    0x4C75F0 — [RAII] g_World.db.socketT. mSOCKET FREED HERE
    //     (late position 20) even though it is initialized at step 21: the ONLY departure from strict LIFO.
    // 21. Options_Save_STUB            0x4C2130 (mGAMEOPTION) — CALL (faithful empty stub from the binary).
    config::g_Options.SaveStub();
    // 22. ColorTable_Free              0x4C1FD0 (mFONTCOLOR) — [no-op] in the binary.
    // 23. StrTable005_Free             0x4C1D00 (mMESSAGE) — [binary no-op + RAII g_Strings.messages].
    // 24. StrTable003_Free             0x4C1AC0 (mZONENAME) — [binary no-op + RAII g_Strings.zoneNames].
    // 25. Tips002_Free                 0x4C1830 (mGAMENOTICE) — [binary no-op + RAII g_Strings.notices].
    // 26. Dict001_Free                 0x4C1400 (mBADWORD) — [binary no-op + RAII g_Strings.bannedWords].
    // 27. CursorSet_DestroyAll         0x4C10B0 (mPOINTER) — CALL (DestroyIcon on the 9 cursors).
    game::Cursors().DestroyAll(); // UNIQUE singleton (mPOINTER), see C-cursor
    // 28. AutoPlay_ShutdownStub        0x4B43B0 (mTRANSFER) — [no-op] in the binary.
    // 29. Net_PacketSizeTable_Dtor     0x464150 (mWORKER) — [no-op] in the binary (size table; covered by net RAII).
    // 30. Net_Shutdown                 0x462820 (mNETWORK) — CALL (closesocket + WSACleanup).
    net_.Shutdown();
    // 31. GXD_DeviceRelease            0x402F70 (mGXD, 2nd skinned renderer g_GxdRenderer 0x18C4EF8)
    //     — [merged] ClientSource has only ONE gfx::Renderer; the 2 original singletons share
    //     the same D3D9 device -> freed a SINGLE time at step 32 (especially NO double-release).
    gfx::GxdRenderer::Instance().Shutdown();
    // 32. Gfx_ShutdownDevice           0x69C990 (mGXD) — CALLS: the single original function
    //     frees DirectInput8 THEN DirectSound8 THEN the D3D9 device (+ ChangeDisplaySettings);
    //     ClientSource splits this into 3 classes (Input/Audio/Renderer), so 3 calls cover this step:
    input_.Shutdown();
    audio::Audio().Shutdown();
    renderer_.Shutdown();
    // 33. Font_RemoveTtfResource       0x4C0F10 (mFONTDATA) — CALL (VERY LAST call; exact
    //     counterpart of Font::AddTtfResource, App_Init's 1st manager).
    gfx::Font::RemoveTtfResource(cfg_.useTRVariant != 0);

    // C++ shell cleanup outside the 33 steps (the original destroys the window via WM_DESTROY/
    // DefWindowProc): frees the main window then unregisters the class.
    if (hwnd_) { DestroyWindow(hwnd_); hwnd_ = nullptr; }
    UnregisterClassA(kWindowClassName, hInst_);
}

// RButtonGateOpen — state gate shared by Input_OnRButtonDown 0x50ADB0 (@0x50AE17) and
// Input_OnRButtonUp 0x50AE40 (@0x50AEA7). Both functions carry a STRICTLY identical condition
// (verified by decompiling both):
//
//   if ( (g_SceneMgr != 6 || g_SceneSubState != 4 || g_SelfActionState[0] ∉ {11,12,33,34,35,36,37})
//        && !UI_RouteRButton*(x, y) )
//       cSceneMgr_OnRButton*(&g_SceneMgr, x, y);
//
// In other words: when in-game (scene 6), in sub-state MainTick (4) AND the self player's
// action state belongs to this set of 7 values, the right click is FULLY swallowed — it
// reaches neither the UI nor the scene. This function therefore returns the inverse of that
// swallowing condition.
bool App::RButtonGateOpen() const {
    // 0x50AE17: g_SceneMgr != 6 || g_SceneSubState != 4 -> gate open by default.
    if (scene_.Current() != Scene::InGame || g_SceneSubState != 4)
        return true;

    // g_SelfActionState[0] 0x1687328 == g_World.players[0].body @ kSelfActionStateOffset
    // (Game/MapWarp.h) — same derivation as SceneManager.cpp:1081-1091 (host.GetSelfActionState).
    int32_t actionState = 0;
    if (!game::g_World.players.empty()) {
        const game::PlayerEntity& self0 = game::g_World.players[0];
        if (self0.body.size() >= game::kSelfActionStateOffset + sizeof(actionState))
            std::memcpy(&actionState, self0.body.data() + game::kSelfActionStateOffset,
                        sizeof(actionState));
    }

    // The 7 literal values of the test (@0x50AE17): 11, 12, 33, 34, 35, 36, 37.
    switch (actionState) {
    case 11: case 12: case 33: case 34: case 35: case 36: case 37:
        return false;   // right click fully swallowed
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

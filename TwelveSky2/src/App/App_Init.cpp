// App/App_Init.cpp — application and main loop implementation.
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

// GameConfig::Parse — faithful to WinMain 0x4609C0 (zone 0x460A13-0x460F4B, re-verified by
// fresh decompilation 2026-07-15; see Docs/TS2_CMDLINE_STRICT_VALIDATION.md): '/'-delimited
// command line, fields [build, TR, windowMode, width, height], with STRICT PER-FIELD LENGTH
// VALIDATION — that's what "cmdline '/'-strict" means:
//   field 1 (build/server mode) : EXACTLY 1 character   (else [Error::PARAMETER])
//   field 2 (TR variant)        : EXACTLY 1 character   (else [Error::PARAMETER2])
//   field 3 (window mode)       : EXACTLY 1 character   (else [Error::PARAMETER5])
//   field 4 (width)             : 1 to 4 characters     (else [Error::PARAMETER6])
//   field 5 (height)            : 1 to 4 characters     (else [Error::PARAMETER7])
//   6th field (too many '/')    : rejected               ([Error::PARAMETER8])
// ASSUMED dev DEVIATIONS (documented) vs the original: (a) a malformed field makes the
// config invalid (valid=false) instead of a blocking MessageBox "[Error::PARAMETERn]" +
// return 0 — App::Run logs it then falls back to /0/0/2/1024/768; (b) >=3 fields are
// required (the original silently tolerates an under-count, leaving fields at 0);
// (c) width/height<=0 guard -> reference default (the original would accept 0 -> 0x0).
GameConfig GameConfig::Parse(const char* cmdLine) {
    GameConfig c;
    if (!cmdLine || cmdLine[0] != '/')
        return c; // invalid (the original shows "[Error::PARAMETER2]" and exits, 0x460A13)

    int fields[5] = {0, 0, 0, 0, 0};
    int n = 0;
    const char* p = cmdLine;                 // points at the initial '/' (validated above)
    while (*p == '/' && n < 5) {
        ++p;                                 // skip the '/'
        const char* start = p;
        while (*p && *p != '/') ++p;         // advance to the next '/' or end of string
        const int len    = static_cast<int>(p - start);
        const int maxLen = (n < 3) ? 1 : 4;  // fields 1-3 = 1 char; fields 4-5 = 1..4 chars
        if (len < 1 || len > maxLen)
            return c;                        // malformed field -> invalid config
        fields[n++] = std::atoi(start);      // Crt_Atoi: 1 non-numeric character -> 0 (faithful)
    }
    if (*p == '/')
        return c; // 6th field (too many '/') -> invalid (faithful [Error::PARAMETER8], 0x460B17 default)

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

// REAL App_Init 0x461C20 sequence (direct idaTs2 decompilation, 2026-07-14) — 32 managers in
// a strict chain (mFONTDATA..mPLAY below, each cited by its tag and original EA anchor), each
// showing "[Error::mXXX.Init()]" and aborting on first failure. EXACT order and functions below
// (replaces the old generic 27-stub skeleton, which reflected neither the order nor the real
// functions — that old count of 27 was a plain generic template, not a measurement of the real
// App_Init). Every step cites its original EA anchor; all 32 managers are now wired (order
// re-verified by fresh disassembly, Wave 1 2026-07-15) — mEDITBOX/mUI = assumed UI deviations,
// mPAT = faithful stub by construction.
bool App::Init() {
    // srand(time(NULL)) — VERY FIRST operation of App_Init 0x461C20, even before reading
    // hInstance (EA 0x461C46) and the 1st manager mFONTDATA (EA 0x461C5C); only the /GS
    // prologue precedes it. Disassembly:
    //   461c33  push 0
    //   461c35  call Crt_Time            ; time(NULL)
    //   461c3d  push eax
    //   461c3e  call Crt_Srand           ; srand(...) -> _tiddata->_holdrand
    // Without this seed, net::DefaultRng() would start at state_=1 (pre-srand CRT default),
    // i.e. the SAME sequence on every launch. Rng_Next 0x7603FD is the exact MSVC LCG
    // (imul 343FDh / add 269EC3h, see Net/Rng.h): seeding here makes the stream faithful.
    net::DefaultRng().Seed(static_cast<uint32_t>(std::time(nullptr)));

    // Resolve the GameData directory + change the process's working directory. The ORIGINAL
    // assumes CWD == install root (containing G01_GFONT/G02_GINFO/G03_GDATA/... directly) —
    // all the binary's relative paths (TTF, .IMG, .BIN) are written WITHOUT a "GameData\"
    // prefix. Done BEFORE mFONTDATA (the very first manager) so this relative path works
    // regardless of the directory the exe is launched from (useful in dev: the compiled
    // binary lives under build\Win32\Debug\, far from GameData\).
    ResolveGameDataDir();

    // mFONTDATA (0x461c5c) : Font_AddTtfResource 0x4C0E70 — registers the embedded TTF
    // font (G01_GFONT). Non-blocking here (the original aborts; the failure is tolerated
    // here to stay usable without font assets).
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
    // GX-DEV-01 — Gfx_HandleDeviceLostReset 0x69DD40: D3D9 device lost/reset observers. In the
    // binary, g_GfxRenderer 0x7FFE18 directly OWNS the ID3DXEffect (+620), the ID3DXFont (+612)
    // and the ID3DXSprite (+608), and itself calls their OnLostDevice (@0x69DE3E) then, after the
    // Reset (@0x69DE55), their OnResetDevice (@0x69DE9B). Here these D3DX objects belong to the
    // upper layers (SceneManager -> LoginScene/GameHud/GameWindows/WorldRenderer -> Font/
    // SpriteBatch), hence the observer indirection. Attachment point prescribed by
    // Gfx/Renderer.h:42-52. WITHOUT this registration, onLost_/onReset_ stay nullptr and the
    // ENTIRE SceneManager::OnDeviceLost/OnDeviceReset chain (SceneManager.cpp:1356/1364) is DEAD
    // CODE: after a device loss (Alt+Tab in fullscreen, resolution change) the ID3DXSprite/
    // ID3DXFont are never notified and restoration is impossible. HandleDeviceLost() is called
    // unconditionally at the top of every frame by Renderer::BeginFrame (Renderer.cpp:238),
    // mirroring the 8 xrefs of Gfx_HandleDeviceLostReset at the top of the Scene_*Render functions.
    renderer_.SetDeviceCallbacks(
        [](void* u) { static_cast<SceneManager*>(u)->OnDeviceLost();  },   // 0x69DE3E (before Reset)
        [](void* u) { static_cast<SceneManager*>(u)->OnDeviceReset(); },   // 0x69DE9B (after Reset)
        &scene_);

    // DirectSound8 audio (Gfx_ZeroInitRenderer 0x69B980, called from Gfx_InitDevice).
    // Non-blocking: on failure, the game runs muted (like the original, dispo flag=0).
    if (audio::Audio().Init(hwnd_))
        TS2_LOG("Audio DirectSound8 initialise.");
    else
        TS2_WARN("Audio indisponible (DirectSound8) — demarrage muet.");
    // DirectInput8 keyboard (queue of Gfx_InitDevice, 0x69C7F2..0x69C8C5). Mouse=Win32.
    if (input_.Init(hInst_, hwnd_))
        TS2_LOG("Input DirectInput8 (clavier) initialise.");
    else
        TS2_WARN("Input indisponible (DirectInput8) — clavier materiel inactif.");

    // mNETWORK (0x461f0c) + mWORKER (0x461f38) : Net_Init 0x462790 (WSAStartup) +
    // Net_InitPacketHandlers 0x463270 (dispatch tables, ALREADY in NetSystem::Init).
    // Non-blocking: the client can start offline.
    if (!net_.Init())
        TS2_WARN("[mNETWORK/mWORKER] Reseau indisponible (WSAStartup) — demarrage hors-ligne.");

    // mTRANSFER (0x461f64) : sub_4B43A0 — CONFIRMED NO-OP in the binary (Hex-Rays
    // reduced it to `return 1;`, `this` never touched). Reproduced as-is.
    game::Transfer_InitNoOp();

    // mPOINTER (0x461f90) : CursorSet_LoadResources 0x4C0FA0 — 9 Win32 cursors
    // EMBEDDED in the .exe resources (RT_GROUP_CURSOR). Will fail until ClientSource embeds
    // the same ids in its .rc — honest documented behavior (Game/MiscManagers.h), non-blocking here.
    cursorsReady_ = game::Cursors().LoadResources(hInst_); // UNIQUE singleton (mPOINTER), see C-cursor
    if (cursorsReady_)
        TS2_LOG("[mPOINTER] 9 curseurs charges.");
    else
        TS2_WARN("[mPOINTER] CursorSet::LoadResources incomplet (ressources .rc absentes) "
                 "— Cursor_AnimateTick par frame neutralise (evite un curseur masque).");

    // mBADWORD/mGAMENOTICE/mZONENAME/mMESSAGE/mFONTCOLOR (0x461fbc..0x46206c) :
    // Dict001_Load/Tips002_Load/StrTable003_Load/StrTable005_Load/ColorTable_InitPalette.
    // mMESSAGE (game::g_Strings.messages) is the CRITICAL table consulted by ALL network
    // handlers via game::Str(id) — now real text, no longer "#id".
    if (game::LoadStringTables(game::g_Strings, gameDataDir_,
                               static_cast<float>(NowSeconds()), cfg_.useTRVariant != 0))
        TS2_LOG("[mBADWORD/mGAMENOTICE/mZONENAME/mMESSAGE/mFONTCOLOR] Tables chargees "
                "(%u mots bannis, %u astuces, %u zones, %u messages).",
                game::g_Strings.bannedWords.Count(), game::g_Strings.notices.Count(),
                game::g_Strings.zoneNames.Count(), game::g_Strings.messages.Count());
    else
        TS2_WARN("[mBADWORD/mGAMENOTICE/mZONENAME/mMESSAGE/mFONTCOLOR] Chargement partiel/echoue.");

    // mGAMEOPTION (0x462098) : Options_LoadAndNormalize 0x4C2110 — loads G02_GINFO\001.BIN
    // (23 fields) then REWRITES it to disk if missing (materializing defaults, faithful to the
    // original). Non-blocking.
    config::g_Options.LoadAndNormalize();
    TS2_LOG("[mGAMEOPTION] Options : son=%d musique=%d ombres=%d.",
            config::g_Options.SoundVolume, config::g_Options.MusicVolume,
            config::g_Options.GfxDetailShadows);
    audio::Audio().SetMasterVolume(config::g_Options.SoundVolume);

    // mLEVEL/mITEM/mSKILL/mMONSTER/mSOCKET (0x4620c4..0x462224, except mNPC/mQUEST/mPAT
    // below) : LevelTable_LoadImg/MobDb_LoadImg/SkillGrowthTbl_LoadImg/
    // ItemDefTbl_LoadImg/AnchorTbl_LoadImg — the 5 .IMG 005_* tables (ALREADY written,
    // Game/GameDatabase.h). Non-blocking: if missing, spawns are cleanly rejected.
    LoadDatabases();

    // mNPC (0x462148) : SkillDefTbl_LoadImg 0x4C6BD0 — CONFIRMED real NPC table
    // (005_00005.IMG, rec[0]="Blacksmith Wu", misleading IDA name like MobDb/ItemDefTbl).
    // mQUEST (0x462174) : NpcTbl_LoadImg 0x4C8090 — CONFIRMED real quest table
    // (005_00006.IMG, rec[0]="[Intro] Banker Bai & Beggar Xiao", 10 dialogue blocks).
    if (game::LoadExtraDatabases(gameDataDir_))
        TS2_LOG("[mNPC/mQUEST] Tables PNJ (005_00005.IMG) + Quete (005_00006.IMG) chargees.");
    else
        TS2_WARN("[mNPC/mQUEST] Tables supplementaires illisibles.");

    // mHELP (0x4621a0) : QuestTbl_LoadImg 0x4C8630 — ALREADY loaded (Game/QuestSystem.h::
    // LoadQuestTable, 005_00007.IMG, 999x84 bytes). Attributed to the "mHELP" manager (not
    // "mQUEST" — confirmed by this fresh disassembly): this table's real role is probably
    // "help/tutorial" rather than strict quest progress; kept as-is (no regression, just a
    // fidelity note).
    if (game::LoadQuestTable(gameDataDir_))
        TS2_LOG("[mHELP] QuestTbl (005_00007.IMG) chargee.");

    // mPAT (0x4621cc) : PatTbl_LoadImg_STUB 0x4C8DA0 — a STUB in the ORIGINAL binary
    // (_STUB suffix set by IDA): the weapon-cap table (dword_8E717C) is NEVER actually loaded
    // by the original client either. Nothing to do here — faithful by construction (already
    // documented in Game/StatFormulas.h as a neutralized term).

    // mGDATA (0x462250) : AssetMgr_InitAllSlots 0x4DEB50 — inits the model/motion pool
    // (not a file, an in-memory array layout).
    if (game::InitModelMotionPool())
        TS2_LOG("[mGDATA] Pool modele/motion initialise.");
    // mZONEMAININFO (0x46227c) : Motion_InitFrameTable 0x4F1380 — 350-row table, pure
    // hardcoded data (350/350 cases reproduced).
    if (game::InitFrameTable())
        TS2_LOG("[mZONEMAININFO] Table de frames (350 lignes) initialisee.");
    // mZONENPCINFO (0x4622a8) : Motion_LoadGInfo002Bin 0x4FCFD0 -> G02_GINFO\002.BIN
    // (701400 bytes, 350x501 dwords, anchor table).
    if (game::LoadGInfo002Bin(gameDataDir_))
        TS2_LOG("[mZONENPCINFO] G02_GINFO\\002.BIN charge (350x501 dwords).");
    else
        TS2_WARN("[mZONENPCINFO] G02_GINFO\\002.BIN illisible.");
    // mZONEMOVEINFO (0x4622d4) : Motion_LoadGInfo003Bin 0x4FD420 -> G02_GINFO\003.BIN
    // (1127000 bytes, 350x805 floats). Populates the table already consulted by
    // Game/MapWarp.h (via game::g_CoordResolver, wired into the network handlers) —
    // previously verified empty in memory for lack of this loader.
    if (game::LoadGInfo003Bin(gameDataDir_))
        TS2_LOG("[mZONEMOVEINFO] G02_GINFO\\003.BIN charge (350x805 floats).");
    else
        TS2_WARN("[mZONEMOVEINFO] G02_GINFO\\003.BIN illisible.");

    // mMYINFO (0x462300) : Player_ResetAnimState 0x50F520 — resets the local player's
    // animation state. Operates on a "player command controller" block not yet ported
    // (g_PlayerCmdController): called with a dedicated local buffer in the meantime.
    // Game/MiscManagers.cpp writes up to index 13314 -> AT LEAST 13315 floats required
    // (documented in that file); margin raised to 16384 to stay safely above that.
    {
        static float s_playerCmdController[16384] = {};
        game::Player_ResetAnimState(s_playerCmdController, static_cast<float>(NowSeconds()));
    }
    TS2_LOG("[mMYINFO] Etat d'animation joueur reinitialise.");

    // mMAIN (0x46232c) : cSceneMgr_Init 0x517AF0 — ALREADY written (SceneManager::Init).
    // gameDataDir_: required by world::WorldAssets (.WO geometry, Gfx/WorldGeometryRenderer.h).
    // 7th arg cfg_.buildVariant = cmdline field 0 = g_ServerModeFlag (0x166918C, "server
    // mode / build variant") -> passed to SceneManager as serverModeFlag, which relays it
    // to login_->Init (SceneManager.h/.cpp wired in parallel, OUTSIDE this file).
    scene_.Init(renderer_, net_, hwnd_, cfg_.width, cfg_.height, gameDataDir_, cfg_.buildVariant);

    // mUTIL (0x462358) : sub_53F2B0 — CONFIRMED NO-OP (same finding as mTRANSFER:
    // Hex-Rays reduced it to `return 1;`, `this` never touched). Reproduced as-is.
    game::Util_InitNoOp();

    // mINPUT (0x462384) : Camera_Init 0x50ABC0 — NOT DirectInput8 (already done above,
    // as part of Gfx_InitDevice): this manager actually initializes the CAMERA (mouse
    // sensitivities, zoom bounds). Gfx/Camera.h is already written; its default constants
    // ALREADY REPRODUCE those of Camera_Init (0.2/0.3 deg/px, bounds 25..150) — default
    // construction is enough, nothing to load.
    camera_ = gfx::Camera{};
    TS2_LOG("[mINPUT] Camera initialisee (bornes zoom %.0f..%.0f).",
            camera_.MinDistance(), camera_.MaxDistance());

    // W1-F2: wiring of the in-game keyboard controller (Camera_UpdateFromInput 0x50B7D0).
    // g_CameraCtrl 0x1668F60 is initialized by this same mINPUT manager (Camera_Init 0x50ABC0):
    // its defaults (mouseLook/mode/speed[]) are already set by CameraCtrlState's construction.
    // Hooks are wired here toward cross-front states/functions not owned by this front.
    g_playerInput.SetScreenshotHook([]{
        // Screenshot_SaveNext 0x5481A0 = Gfx_SaveScreenshot 0x69EA50 ("G04_GSHOT\\Sxxxxx.JPG") —
        // Gfx/file function not owned by this front.
        // TODO [ancre 0x5481A0] : wire renderer_.SaveScreenshot once the function is exposed.
    });
    g_playerInput.SetSceneKeyDownHook([this](int dik){
        scene_.OnKeyDown(dik);   // cSceneMgr_OnKeyDown 0x517F80 (LABEL_240 0x50DDE4)
    });
    // INPUT-09 — g_MorphInProgress 0x1675A88: the 6 movement paths of
    // Camera_UpdateFromInput test `if (g_MorphInProgress == 1) return;` (@0x50B857,
    // @0x50B8D0, @0x50B9B2, @0x50BA6E, @0x50BB23, @0x50BB63) — LITERAL comparison to 1,
    // reproduced as-is. net::g_MorphInProgress (Net/ClientState.h:18) is the live mirror
    // of this global: reset to 0 by the 12 morph-end cases of
    // Net/GameHandlers_Misc.cpp:259-270 and already read by InventoryWindow/SkillTreeWindow/
    // CharacterStatsWindow. Without this predicate, morphInProgress_ was an empty std::function
    // -> WASD kept being emitted DURING a morph, unlike the original.
    g_playerInput.SetMorphInProgressPredicate([]{
        return net::g_MorphInProgress == 1;   // 0x1675A88, see 0x50B857
    });
    // Predicates NOT wired, for lack of proof or a model (rule: never guess):
    //  - textInputActive_ (g_UIEditBoxMgr 0x1668FC0, guard @0x50B7FA): requires a
    //    SceneManager::IsTextInputFocused() reflecting "an in-game EDIT has focus" (at
    //    minimum GuildWindow.nameEdit_). Gating on ChatWindow.Focused() alone would be a no-op
    //    (chat keyboard input is not routed while InGame). TODO [ancre 0x1668FC0] — Scene front.
    //  - selfBlocked_ (g_SelfCharInvBlock 0x1673170): SEMANTICS NOT RESOLVED. Camera_
    //    UpdateFromInput @0x50B810 requires g_SelfCharInvBlock[0]==0 for the WASD block, while
    //    Game_OnHotkey @0x537347 requires the OPPOSITE (`cmp ds:g_SelfCharInvBlock, 0 / jnz`)
    //    for hotkeys: the two modes are mutually exclusive, which suggests a validity/mode
    //    flag rather than a "block". Wiring it without resolving the polarity would silently
    //    invert the behavior. TODO [ancre 0x1673170] : cross-check the writers.

    // Mouse-hook wiring (App_WndProc 0x461930). In the binary, the WndProc calls DIRECTLY
    // Camera_ResetView (@0x461AF0), Camera_MouseWheelZoom (@0x461B3F) and Input_OnRButtonDown/Up
    // (@0x461A8F/@0x461AC3). On the ClientSource side, InputSystem exposes these call sites as
    // hooks (Input/InputSystem.h:211-217) fed by input_.ProcessMessage from HandleMessage; the
    // WndProc's SetCapture/ReleaseCapture (@0x461A68/@0x461A9B) is already done by
    // InputSystem::OnRButtonDown/Up. Without the assignments below, onMDown_/onWheel_/
    // onRDown_/onRUp_ stayed nullptr: middle button, wheel and RIGHT click were inert.

    // WM_MBUTTONDOWN 0x207 -> Camera_ResetView 0x50AED0 (@0x461AF0). The IDA name is
    // MISLEADING: the function doesn't "reset" the camera, it MIRRORS the eye 180 degrees
    // around the target.
    input_.SetMButtonDownCallback([this](int, int) {
        // Guard 1 @0x50AEE9: g_SceneMgr == 6 && g_SceneSubState == 4.
        if (scene_.Current() != Scene::InGame || g_SceneSubState != 4)
            return;
        // Guard 2 @0x50AF09: `g_SelfCharInvBlock[0] || !this[2] || g_CamMode == 1`. This is
        // NOT a bail-out: the mirror happens WHEN this condition is TRUE. g_SelfCharInvBlock
        // 0x1673170 (polarity not resolved, see above) and g_CamMode 0x1668F6C (== g_CameraCtrl+12)
        // are not modeled here; mouseLook (g_CameraCtrl+8) is. With the binary's default values
        // (blocked=0, camMode=0, mouseLook=0 — Input_ResetMouseState 0x50E000), the condition
        // reduces to `!mouseLook`, which is faithful IN THIS STATE.
        // TODO [ancre 0x50AF09] : add g_SelfCharInvBlock/g_CamMode once modeled.
        if (g_playerInput.State().mouseLook != 0)
            return;
        // Mirror @0x50AF3D/@0x50AF5B: eye' = (2*target.x - eye.x, eye.y UNCHANGED,
        // 2*target.z - eye.z), target unchanged (v7/v8/v9). Negating the horizontal components
        // of (eye - target) while keeping the vertical one is EXACTLY equivalent, in gfx::Camera's
        // spherical model (Eye() = target + d*(cos p*sin y, sin p, cos p*cos y)), to yaw += PI with
        // pitch AND distance unchanged. Followed by Cam_SetLookAt @0x50AF8D + Camera_SetEyeTarget
        // @0x50AFC1, which the eye reconstruction by Camera::Eye() covers.
        camera_.SetYaw(camera_.Yaw() + D3DX_PI);
    });

    // WM_MOUSEWHEEL 0x20A -> Camera_MouseWheelZoom 0x50B460 (@0x461B3F, arg = SHIWORD(wParam)).
    input_.SetMouseWheelCallback([this](int delta) {
        // Drains InputSystem's mouse_.wheel accumulator (a single WM_MOUSEWHEEL per callback:
        // the consumed value == `delta`). Without a reader, it would grow unbounded. `delta` is
        // kept because it is the EXACT argument passed by the WndProc @0x461B3F.
        input_.ConsumeWheel();
        // Guard @0x50B479: g_SceneMgr == 6 && g_SceneSubState == 4 — NO other guard
        // (neither blocked nor mouse-look), unlike the other camera paths.
        if (scene_.Current() != Scene::InGame || g_SceneSubState != 4)
            return;
        // @0x50B490: v8 = (double)a2 * this[19], where this+76 = 0.1 (Camera_Init @0x50AC4F).
        // a2 is the RAW WM_MOUSEWHEEL delta (+/-120 per notch), NOT a notch count:
        // 120 * 0.1 = 12 distance units per notch. Then Cam_ClampDistance(g_GfxRenderer, v8)
        // @0x50B49F, whose net effect (re-read at 0x69CE00: if |eye-target| > a2, then
        // eye -= normalize(eye-target) * a2) is exactly `distance -= a2` == Camera::Zoom(a2).
        // The 25/150 bound snap (this[21]/this[22], @0x50B641/@0x50B7C5) is covered by
        // Camera::Zoom's ClampDistanceInternal.
        // WARNING: gfx::Camera::ZoomByWheel is NOT used: it first divides by 120
        //   (Camera.cpp:98) then multiplies by 0.1 -> 0.1 unit/notch, i.e. 120x TOO SLOW vs
        //   @0x50B490. A defect of the Gfx front, flagged; worked around here without touching it.
        camera_.Zoom(static_cast<float>(delta) * gfx::Camera::kWheelZoomStep);
    });

    // WM_RBUTTONDOWN 0x204 / WM_RBUTTONUP 0x205 -> Input_OnRButtonDown 0x50ADB0 /
    // Input_OnRButtonUp 0x50AE40 (@0x461A8F / @0x461AC3).
    // Original chain @0x50AE2F: `if (!UI_RouteRButtonDown(a1,a2)) cSceneMgr_OnRButtonDown(...)`
    // — "first consumer wins". The SCENE branch is a PROVEN no-op:
    // cSceneMgr_OnRButtonDown 0x517EA0 -> SceneMgr_RButtonDown_NoOp 0x537310 = EMPTY body
    // (same for 0x517F10 -> 0x537320 for the Up). All the behavioral value is therefore in the
    // UI router, and NO addition to SceneManager is required: terminal dispatch reduces to
    // calling the UI router. (An earlier banner claimed RouteRButtonDown/Up did not exist —
    // true when this front was written, no longer since: UIManager.h:262-263.)
    input_.SetRButtonDownCallback([this](int x, int y) {
        // 0x50AE17: outside the gate, the right click is FULLY swallowed (neither UI nor scene).
        if (!RButtonGateOpen())
            return;
        // UI_RouteRButtonDown 0x5AD5D0 (@0x50AE2F). The return value is ignored: the scene
        // branch it guards in the original is empty (0x537310), hence no observable effect.
        ui::UIManager::Instance().RouteRButtonDown(x, y);
    });
    input_.SetRButtonUpCallback([this](int x, int y) {
        if (!RButtonGateOpen())   // 0x50AEA7: gate IDENTICAL to RButtonDown's
            return;
        // UI_RouteRButtonUp 0x5ADA90 (@0x50AEBF), same reasoning as RButtonDown.
        ui::UIManager::Instance().RouteRButtonUp(x, y);
    });

    // mEDITBOX (0x4623b0) : UI_CreateEditBoxes 0x50E460 — creates 21 subclassed native
    // Win32 EDIT controls. ASSUMED DEVIATION (documented since Docs/TS2_CLIENT_SHELL.md):
    // this client uses standalone ts2::ui::EditBox (UI/Widgets.h) instead, no native EDIT
    // controls. Nothing to do here.

    // mUI (0x4623dc) : UI_InitAllDialogs 0x5ABF50 — registers ~38 dialogs at startup.
    // ASSUMED DEVIATION: ts2::ui::UIManager (already written) + the 13 game windows
    // (UI/GameWindows.h) are only built on entering the InGame scene (SceneManager::Change),
    // not here — these dialogs only make sense with a populated g_World. Functionally
    // equivalent behavior, deferred rather than immediate-but-hidden activation.

    // mPLAY (0x462405) : cGameData_InitPools 0x5575D0 — fixed entity pool capacities
    // (already modeled as dynamic std::vector in Game/GameState.h; always true, faithful:
    // the binary cannot fail here).
    game::GameData_InitPools();
    TS2_LOG("[mPLAY] Pools de donnees de jeu initialises.");

    // Finalization (0x462429-0x46245d, step 33 — AFTER mPLAY, BEFORE StartIntro, order
    // verified by fresh disassembly): Terrain_PushRenderState(&g_GfxRenderer) 0x69CB80 reads
    // QueryPerformanceCounter and returns the elapsed time, written to g_GameTimeSec
    // (0x815180); g_FrameAccumSec (0x815580) and flt_81518C (0x81518C) receive the same
    // value; flt_815188 (0x815188) <- flt_7A6918 = 0x3D088889 = 0.033333335f = 1/30 s
    // (fixed 30 FPS step = kFixedTimestep, a compile-time constant here). The three frame
    // clocks are therefore seeded to the current instant, at this exact point in the sequence.
    gameClockSec_ = frameAccumSec_ = lastPurgeSec_ = NowSeconds();

    scene_.StartIntro();  // cSceneMgr_StartIntro 0x517B80 (end of App_Init, 0x462462)
    TS2_LOG("App_Init termine (sequence fidele App_Init 0x461C20).");
    return true;
}

// Probes GameData candidate locations (relative to the exe, then to the project, to
// tolerate a dev launch from build\Win32\{Debug,Release}\) and, as soon as a plausible folder
// is found (contains G03_GDATA\D01_GIMAGE2D\005\...), stores its ABSOLUTE path
// (gameDataDir_) AND switches the process's CWD onto it. After this call, any relative path
// WITHOUT a "GameData\" prefix (like the original binary's hardcoded strings:
// "G01_GFONT\...", "G02_GINFO\...") resolves correctly, exactly as if the exe had been
// launched from the install root (implicit assumption of App_Init 0x461C20).
void App::ResolveGameDataDir() {
    static const char* const kCandidates[] = {
        ".",   // CWD is ALREADY the GameData root (exe launched from GameData, or a build
               // dropped into GameData as suggested): without this candidate, gameDataDir_
               // stayed empty (degraded WorldMap/in-game tables) even though direct paths
               // "G03_GDATA\..." already worked. Tested first: the most faithful case
               // (App_Init 0x461C20 assumes CWD == install root).
        "GameData",
        "TwelveSky2/GameData",
        "ClientSource/TwelveSky2/GameData",
        "../../../TwelveSky2/GameData",
    };
    for (const char* dir : kCandidates) {
        std::string probe = std::string(dir) + "/G03_GDATA/D01_GIMAGE2D/005/005_00001.IMG";
        if (GetFileAttributesA(probe.c_str()) == INVALID_FILE_ATTRIBUTES)
            continue; // candidate folder absent -> try next, silently.

        char absPath[MAX_PATH] = {};
        if (GetFullPathNameA(dir, MAX_PATH, absPath, nullptr) == 0) {
            gameDataDir_ = dir; // fallback: relative path as-is.
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

// Loads the .IMG tables (g_World.db) + the help table (QuestTbl). Non-blocking:
// if gameDataDir_ is empty/invalid, the client still starts (spawns rejected).
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

bool App::RegisterWindowClass() {
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &App::WndProc;
    wc.hInstance     = hInst_;
    // Faithful to WinMain 0x4611f6 (WNDCLASSEXA): hIcon = LoadIconA(hInstance, MAKEINTRESOURCE(101)).
    // Tries the embedded icon resource 101 (0x65); falls back to IDI_APPLICATION until
    // TwelveSky2.rc embeds that id (an .exe resource to extract — see report).
    wc.hIcon         = LoadIconA(hInst_, MAKEINTRESOURCEA(101));
    if (!wc.hIcon)
        wc.hIcon     = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    // Faithful: original = GetStockObject(BLACK_BRUSH) (EA near 0x461278), not a light system
    // brush — black flash (not white) during window creation/resize.
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
        // Faithful to WinMain 0x461321 (VERIFIED by fresh disassembly: `mov [ebp+dwStyle],
        // 10CA0000h`): real style = 0x10CA0000 = WS_VISIBLE|WS_CAPTION|WS_SYSMENU|
        // WS_MINIMIZEBOX — NON-resizable window, WITH a minimize button, WITHOUT
        // maximize. The old WS_THICKFRAME (=0x10CC0000, opposite style) was an ERROR
        // from an earlier audit (roadmap §5.3): fixed in Wave 1 (2026-07-15).
        style = WS_VISIBLE | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        RECT r = {0, 0, w, h};
        AdjustWindowRect(&r, style, FALSE);
        const int ww = r.right - r.left;
        const int wh = r.bottom - r.top;
        // CFG-02 — integer division ORDER, faithful to WinMain 0x4609C0's disassembly.
        // The original performs TWO SEPARATE truncated divisions, not a single one on the
        // difference.
        // Y (@0x4613e9): GetSystemMetrics(SM_CYSCREEN=1) then `cdq/sub eax,edx/sar ecx,1`
        //   (= SM_CY/2), then rc.bottom-rc.top then `cdq/sub eax,edx/sar eax,1` (= wh/2), then
        //   `sub ecx,eax` -> Y = SM_CY/2 - wh/2.
        // X (@0x46140c): same with SM_CXSCREEN=0 and rc.right-rc.left -> X = SM_CX/2 - ww/2.
        // (`cdq/sub eax,edx/sar 1` = signed division-by-2 idiom rounding toward zero, strictly
        //  equivalent to C's `/ 2` on int.)
        // NOT equivalent to (SM - ww)/2 in integer arithmetic when the ADJUSTED dimension is
        // odd — and ww/wh typically are (AdjustWindowRect adds caption + borders):
        // e.g. SM=1080, wh=793 -> binary 540-396=144; old C++ (1080-793)/2=143 (1 px off).
        x = GetSystemMetrics(SM_CXSCREEN) / 2 - ww / 2;   // 0x46140c
        y = GetSystemMetrics(SM_CYSCREEN) / 2 - wh / 2;   // 0x4613e9
        w = ww; h = wh;
    } else {
        // Faithful (INIT audit 2026-07-14): the original creates the fullscreen window at the
        // resolution REQUESTED on the cmdline (nWidth/nHeight, cfg_.width/height here), NOT at
        // the desktop resolution (GetSystemMetrics) — and sets WS_EX_APPWINDOW (absent before).
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

} // namespace ts2

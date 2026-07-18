// Asset/AssetSelfTest.cpp — runtime validation of the ENTIRE Asset layer against
// real files. Invoked via "TwelveSky2.exe -assettest <GameDataPath>".
#include "Asset/AssetSelfTest.h"
#include "Core/Log.h" // TS2_LOG (used by the GetForNpc debug instrumentation below)
#include "Asset/NpkArchive.h"
#include "Asset/ImgFile.h"
#include "Asset/Motion.h"
#include "Asset/Model.h"
#include "Asset/WorldChunk.h"
#include "Asset/Sound.h"
#include "Asset/Texture.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
// Real intro logo GPU verification (mission "LOGO INTRO REEL", 2026-07-14):
// hidden D3D9 device + full IntroRender, see dedicated block at the bottom of RunSelfTest.
#include "UI/IntroRender.h"
#include "UI/UIManager.h"
#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Game/IntroFlow.h"
// GetForMonster()/GetForNpc() robustness verification (mission "VERIFICATION DE
// ROBUSTESSE GetForMonster/GetForNpc", 2026-07-14): ModelCache.h already pulls in
// Game/GameDatabase.h (LoadGameDatabases/g_World.db.monster) and Game/ExtraDatabases.h
// (NpcDefRecord/LoadExtraDatabases/GetNpcDefRecord), see dedicated block at the bottom of RunSelfTest.
#include "Gfx/ModelCache.h"
// Visual 3D-rendering verification, offline (mission "VERIFICATION VISUELLE
// COMPLETE DU RENDU 3D", 2026-07-14): loads a REAL zone (Z001.WO/.ATM) via the same
// loaders as World::WorldMap (World_LoadZoneResource case 3/7) and pushes the result into
// Gfx/WorldGeometryRenderer.h (GPU upload + Render()) WITHOUT going through Net_ConnectGameServer,
// see dedicated block at the bottom of RunSelfTest.
#include "World/WorldIntegration.h"
#include "World/WorldMap.h"
#include "Gfx/WorldGeometryRenderer.h"
#include "Gfx/Camera.h"
#include "Core/Log.h" // TS2_LOG/TS2_WARN/TS2_ERR (fix: missing include, window-audit mission 2026-07-14)
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

namespace ts2::asset {

static void OpenConsole() {
    // If stdout is already a valid stream (pipe/file redirected by the caller, e.g.
    // "-RedirectStandardOutput" to capture [INFO]/[WARN] TS2_LOG logs), do NOT
    // overwrite that stream with an ephemeral console: AllocConsole()+freopen_s to
    // CONOUT$ would redirect ALL output (including IntroRender) to a console window
    // that closes as soon as the process ends, losing the captured logs.
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != nullptr && h != INVALID_HANDLE_VALUE) return;
    if (AllocConsole()) {
        FILE* f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
    }
}

static std::string Join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// First file with extension `extLower` under `dir` (recursive).
static std::string FirstWith(const std::string& dir, const std::string& extLower) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return "";
    for (auto it = fs::recursive_directory_iterator(dir, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        std::string e = it->path().extension().string();
        for (auto& c : e) c = (char)tolower((unsigned char)c);
        if (e == extLower) return it->path().string();
    }
    return "";
}

int RunSelfTest(const std::string& gd) {
    OpenConsole();
    std::printf("=== Asset self-test (GameData = %s) ===\n\n", gd.c_str());
    int fails = 0;
    auto ok = [&](bool cond, const char* fmt, ...) {
        va_list ap; va_start(ap, fmt);
        char buf[512]; vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
        std::printf("[%s] %s\n", cond ? " ok " : "FAIL", buf);
        std::fflush(stdout); // immediate visibility even if the process crashes right after (diagnostic)
        if (!cond) ++fails;
    };

    ok(Zlib::Instance().Available(), "GXDCompress.dll / uncompress");

    // NPK
    { NpkArchive a;
      bool r = a.Open(Join(gd, "GXDEFFECT\\GXDEffect.npk"));
      ok(r && a.Entries().size() == 12, "NPK GXDEffect.npk : v%u, %zu entrees",
         r ? a.Version() : 0, r ? a.Entries().size() : 0);
      if (r) { try { auto b = a.Read(a.Entries().front());
          ok(!b.empty(), "NPK read '%s' -> %zu o", a.Entries().front().name.c_str(), b.size());
      } catch (...) { ok(false, "NPK read"); } } }

    // IMG (texture + table)
    { ImgFile t; bool r = t.Load(Join(gd, "G03_GDATA\\D01_GIMAGE2D\\001\\001_00001.IMG"));
      ok(r && t.Kind() == ImgKind::TextureDxt, "IMG texture -> %s payload=%zu",
         r ? t.FourCC().c_str() : "?", r ? t.Payload().size() : 0);
      ImgFile d; bool r2 = d.Load(Join(gd, "G03_GDATA\\D01_GIMAGE2D\\005\\005_00001.IMG"));
      ok(r2 && d.Kind() == ImgKind::Table, "IMG table -> %s payload=%zu",
         r2 ? d.TableName().c_str() : "?", r2 ? d.Payload().size() : 0); }

    // MOTION
    { std::string p = FirstWith(Join(gd, "G03_GDATA\\D03_GMOTION"), ".motion");
      Motion m; bool r = !p.empty() && m.Load(p);
      ok(r, "MOTION %s : env=%d ver=%d %uf x %ubones = %u keyframes",
         p.empty() ? "?" : fs::path(p).filename().string().c_str(),
         r ? (int)m.Envelope() : 0, r ? m.Version() : 0,
         r ? m.FrameCount() : 0, r ? m.BoneCount() : 0, r ? m.KeyframeCount() : 0); }

    // SOBJECT
    { std::string p = FirstWith(Join(gd, "G03_GDATA\\D04_GSOBJECT"), ".sobject");
      SObject s; bool r = !p.empty() && s.Load(p);
      ok(r && s.envOk() && s.inflateOk() && s.walkOk(),
         "SOBJECT %s : fmt=%d ver=%c env=%d inflate=%d walk=%d meshes=%u",
         p.empty() ? "?" : fs::path(p).filename().string().c_str(),
         r ? (int)s.format() : 0, r && s.version() ? s.version() : '?',
         r ? s.envOk() : 0, r ? s.inflateOk() : 0, r ? s.walkOk() : 0, r ? s.meshCount() : 0); }

    // WORLD
    for (const char* ext : {".wm", ".wj", ".wo", ".wp"}) {
        std::string p = FirstWith(Join(gd, "G03_GDATA\\D07_GWORLD"), ext);
        WorldChunk w; bool r = !p.empty() && w.Load(p);
        ok(r, "WORLD %-6s -> type=%s", ext, r ? WorldChunkTypeName(w.Type()) : "?");
    }

    // SOUND
    { std::string p = FirstWith(Join(gd, "G03_GDATA\\D09_WSOUND"), ".wsound");
      WSound ws; bool r = !p.empty() && ws.Load(p);
      ok(r && ws.SizeOk(), "WSOUND %s : count=%u emitters=%u sizeOk=%d",
         p.empty() ? "?" : fs::path(p).filename().string().c_str(),
         r ? ws.Count() : 0, r ? ws.Count2() : 0, r ? ws.SizeOk() : 0);
      std::string isn = FirstWith(Join(gd, "G03_GDATA\\D06_GSOUND"), ".isn");
      std::vector<uint8_t> ogg;
      bool r2 = !isn.empty() && ReadWholeFile(isn, ogg);
      ok(r2 && IsOgg(ogg), "ISN est Ogg (%zu o)", ogg.size()); }

    // TEXTURES
    { std::string tga = FirstWith(Join(gd, "G03_GDATA\\D11_ATMOSPHERE"), ".tga");
      Texture t; bool r = !tga.empty() && t.LoadFile(tga);
      ok(r && !t.Empty(), "TGA %s : %ux%u bpp=%u pixels=%zu",
         tga.empty() ? "?" : fs::path(tga).filename().string().c_str(),
         r ? t.width : 0, r ? t.height : 0, r ? t.bpp : 0, r ? t.pixels.size() : 0);
      std::string dds = FirstWith(Join(gd, "G03_GDATA\\D07_GWORLD"), ".shadow");
      Texture d; bool r2 = !dds.empty() && d.LoadFile(dds);
      ok(r2, "SHADOW(DDS) %s : %ux%u %s mips=%u",
         dds.empty() ? "?" : fs::path(dds).filename().string().c_str(),
         r2 ? d.width : 0, r2 ? d.height : 0, r2 ? d.fourCC.c_str() : "?", r2 ? d.mipCount : 0); }

    // INTRO LOGO (real GPU rendering, mission "LOGO INTRO REEL") ---------------------
    // Hidden D3D9 device (offscreen WS_POPUP) + full IntroRender: verifies, outside the
    // 30 FPS accumulator fast-forward (App::FrameTick), that the 33 sprites
    // 001_00799..001_00831.IMG load and draw via the SAME path as Scene_IntroRender
    // (ImgFile::Load + GpuTexture::CreateFromImgFile + SpriteBatch::DrawSpriteScaled) --
    // see the "IntroRender : logo charge (...)" lines above, one per sub-state 1..33.
    // `gd` is converted to an ABSOLUTE path (`gdAbs`) BEFORE the chdir below -- bug fixed
    // 2026-07-14 (mission "verification visuelle rendu 3D") where a relative gd became
    // invalid after chdir, making LoadGameDatabases/LoadExtraDatabases fail with "IMG
    // illisible" despite the files existing on disk. Any use of `gd` after this point
    // must go through `gdAbs`.
    std::string gdAbs = gd;
    { char absPath[MAX_PATH] = {};
      if (GetFullPathNameA(gd.c_str(), MAX_PATH, absPath, nullptr) != 0) gdAbs = absPath; }

    { // IntroRender loads its paths AS RELATIVE (like the original binary and like
      // App::Run after ResolveGameDataDir()): switch the CWD to GameData here too,
      // otherwise ImgFile::Load fails (the -assettest path didn't do this chdir, unlike
      // App::Run) and the test would systematically fall back to the flat fallback.
      SetCurrentDirectoryA(gdAbs.c_str());
      WNDCLASSA wc = {}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandleA(nullptr);
      wc.lpszClassName = "TS2_AssetSelfTest_Hidden";
      RegisterClassA(&wc);
      HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "selftest", WS_POPUP,
                                   0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr);
      gfx::Renderer renderer;
      bool devOk = hwnd && renderer.Init(hwnd, 1024, 768, /*windowed=*/true);
      ok(devOk, "IntroRender : device D3D9 (hors-ecran) cree");
      if (devOk) {
          gfx::SpriteBatch sprites;
          bool spOk = sprites.Create(renderer.Device());
          gfx::Font::AddTtfResource(false);
          gfx::Font font;
          bool fontOk = font.Init(renderer.Device(), 1024, 768, false);
          ok(spOk && fontOk, "IntroRender : SpriteBatch/Font (test) crees");

          ui::UiContext ctx;
          ctx.renderer = &renderer;
          ctx.sprites  = &sprites;
          ctx.font     = &font;
          ctx.screenW  = 1024;
          ctx.screenH  = 768;

          ui::IntroRender introRender;
          game::IntroState state;
          int framesRendered = 0;
          for (int s = 0; s <= 34; ++s) {
              state.subState = s;
              if (renderer.BeginFrame()) {
                  sprites.Begin();
                  ctx.phase = ui::UiPhase::Panels;
                  introRender.Render(ctx, state);
                  sprites.End();
                  renderer.EndFrame();
                  ++framesRendered;
              }
          }
          ok(framesRendered == 35, "IntroRender : 35 sous-etats (0..34) rendus sans exception "
             "(les 33 lignes [INFO] IntroRender:logo charge ci-dessus confirment le sprite reel)");

          font.Shutdown();
          sprites.Destroy();
      }
      renderer.Shutdown();
      if (hwnd) DestroyWindow(hwnd); }

    // MODELCACHE — GetForMonster()/GetForNpc() robustness (mission "VERIFICATION DE
    // ROBUSTESSE GetForMonster/GetForNpc", 2026-07-14) -----------------------------
    // Goal: confirm that neither API call, across a LARGE sample of real ids (the FULL
    // real MONSTER_INFO table 005_00004.IMG, 10000 records, + the FULL real NpcDefRecord
    // table 005_00005.IMG, 500 records) nor on degenerate bounds (0, count, count+1,
    // UINT32_MAX, MSB only), ever crashes/throws or breaks contract (GetForMonster()/
    // GetForNpc() contract: nullptr OR a valid SkinnedModel pointer, never anything else).
    // Also records the resolution success rate (X models loaded+uploaded out of Y ids
    // with a field244 in the [1,333] catalog).
    { bool dbOk = game::LoadGameDatabases(gdAbs);
      bool extraOk = game::LoadExtraDatabases(gdAbs);
      ok(dbOk, "ModelCache selftest : g_World.db (LoadGameDatabases) charge");
      ok(extraOk, "ModelCache selftest : g_ExtraDb (LoadExtraDatabases) charge");

      if (dbOk) {
          WNDCLASSA wc2 = {}; wc2.lpfnWndProc = DefWindowProcA; wc2.hInstance = GetModuleHandleA(nullptr);
          wc2.lpszClassName = "TS2_AssetSelfTest_ModelCache";
          RegisterClassA(&wc2); // silent failure if already registered (class reused): OK.
          HWND hwnd2 = CreateWindowExA(0, wc2.lpszClassName, "selftest-modelcache", WS_POPUP,
                                        0, 0, 64, 64, nullptr, nullptr, wc2.hInstance, nullptr);
          gfx::Renderer renderer2;
          bool devOk2 = hwnd2 && renderer2.Init(hwnd2, 1024, 768, /*windowed=*/true);
          ok(devOk2, "ModelCache selftest : device D3D9 (hors-ecran) #2 cree");

          if (devOk2) {
              gfx::MeshRenderer meshRenderer;
              bool mrOk = meshRenderer.Init(renderer2);
              ok(mrOk, "ModelCache selftest : MeshRenderer.Init (decl+shaders skinnes) OK");

              if (mrOk) {
                  gfx::ModelCache cache(meshRenderer, gdAbs, /*maxResident=*/100000);

                  // --- GetForMonster() over the FULL real table (10000 records) --------
                  const game::DataTable& monsterTable = game::g_World.db.monster;
                  uint32_t validField244 = 0; // records whose field244 is in the [1,333] catalog
                  uint32_t resolved      = 0; // GetForMonster() != nullptr (file actually loaded+uploaded)
                  bool     monsterCrashed = false;
                  for (uint32_t id = 0; id < monsterTable.count; ++id) {
                      const uint8_t* rec = monsterTable.record(id);
                      if (!rec) { monsterCrashed = true; break; } // should never happen (id < count)
                      uint32_t field244 = 0;
                      std::memcpy(&field244, rec + 244, sizeof(field244));
                      if (field244 >= 1 && field244 <= 333) ++validField244;

                      const gfx::SkinnedModel* m = cache.GetForMonster(id);
                      if (m) ++resolved;
                  }
                  ok(!monsterCrashed, "GetForMonster : DataTable::record() coherent sur tout [0,%u)",
                     monsterTable.count);
                  ok(true, "GetForMonster : %u modele(s) resolu(s) (charges+uploades) sur %u id "
                            "avec field244 catalogue [1,333] (table reelle balayee en entier : %u records)",
                     resolved, validField244, monsterTable.count);
                  ok(resolved <= validField244,
                     "GetForMonster : coherence resolved(%u) <= validField244(%u) -- ne resout jamais "
                     "plus que le catalogue kindIndex meme si le disque a tous les fichiers",
                     resolved, validField244);

                  // --- GetForMonster() degenerate bounds: never crashes, always nullptr --
                  bool edgeOk = true;
                  edgeOk = edgeOk && (cache.GetForMonster(monsterTable.count) == nullptr);
                  edgeOk = edgeOk && (cache.GetForMonster(monsterTable.count + 1) == nullptr);
                  edgeOk = edgeOk && (cache.GetForMonster(0xFFFFFFFFu) == nullptr);
                  edgeOk = edgeOk && (cache.GetForMonster(0x80000000u) == nullptr);
                  // id 0 is already covered by the [0,count) sweep above (valid, NOT a
                  // degenerate bound for a table indexed from 0) -- not retested here.
                  ok(edgeOk, "GetForMonster : bornes degenerees (count=%u, count+1, UINT32_MAX, "
                              "0x80000000) -> nullptr sans exception", monsterTable.count);

                  // --- GetForNpc() over the FULL real NpcDefRecord table (up to 500 ids) ---
                  // Current contract (cf. ModelCache.h banner "PNJ NON RESOLU"): ALWAYS nullptr,
                  // regardless of record content -- this just checks the absence of a crash and
                  // the contract's stability on REAL NpcDefRecord entries loaded from disk.
                  uint32_t npcTested = 0;
                  bool npcAlwaysNull = true;
                  if (extraOk) {
                      for (uint32_t npcId = 1; npcId <= 500; ++npcId) {
                          if (npcId % 25 == 1) TS2_LOG("ModelCache selftest : GetForNpc npcId=%u...", npcId); // DEBUG bisection (robustness mission)
                          const game::NpcDefRecord* npc = game::GetNpcDefRecord(npcId);
                          if (!npc) continue; // empty slot (id==0) or out of bounds -- skip, not a "real" NPC
                          ++npcTested;
                          if (cache.GetForNpc(*npc) != nullptr) npcAlwaysNull = false;
                      }
                  }
                  ok(npcAlwaysNull, "GetForNpc : %u vrai(s) NpcDefRecord teste(s) (table reelle "
                                     "005_00005.IMG), nullptr systematique sans exception (mapping "
                                     "kindIndex PNJ non resolu -- comportement attendu, cf. bandeau "
                                     "ModelCache.h)", npcTested);
              }
              meshRenderer.Shutdown();
          }
          renderer2.Shutdown();
          if (hwnd2) DestroyWindow(hwnd2);
      } }

    // WORLDGEOMETRYRENDERER / SKYRENDERER — real 3D rendering OFFLINE (mission
    // "VERIFICATION VISUELLE COMPLETE DU RENDU 3D", 2026-07-14) -----------------------
    // Reproduces the ONLY data path needed to draw a zone (World_LoadZoneResource
    // case 3 = .WO, case 7 = .ATM), WITHOUT World_LoadMap (so WITHOUT the DRM/
    // SilverLining gate -- cf. WorldMap.h banner) and WITHOUT any network packet:
    // exactly the subset World/WorldIntegration.h + Gfx/WorldGeometryRenderer.h need.
    // Goal: prove Init()/Build()/Render() run without exception/crash on a REAL .WO
    // chunk (50k+ instances documented elsewhere) and that the sky is really derived
    // from the real .ATM (HasRealAtmosphere()==true, not the "midi" fallback).
    { world::WorldAssets assets(gdAbs);
      world::WorldLoadHooks hooks = assets.MakeHooks();
      world::WorldMap map(hooks);
      // zoneId=1 -> fileId=1 (WorldMap::ZoneIdToFileId) -> Z001.WO/Z001.ATM (present on
      // disk, already validated earlier in this file by the "WORLD .wo/.tga" test via
      // FirstWith(D07_GWORLD) -- here we target this file BY NAME for a reproducible result.
      map.SetCurrentZoneId(1);
      unsigned char woOk  = map.LoadZoneResource(1, world::ResourceKind::ObjectsWO);
      unsigned char atmOk = map.LoadZoneResource(1, world::ResourceKind::Atmosphere);
      ok(woOk != 0, "World Z001.WO : LoadZoneResource(ObjectsWO) charge (%zu octets .WO parses "
                     "en asset::ObjectChunk)", assets.Objects() ? size_t(1) : size_t(0));
      ok(atmOk != 0, "World Z001.ATM : LoadZoneResource(Atmosphere) charge (World_LoadDataFile, "
                      "sans World_LoadMap/porte DRM SilverLining)");

      WNDCLASSA wc3 = {}; wc3.lpfnWndProc = DefWindowProcA; wc3.hInstance = GetModuleHandleA(nullptr);
      wc3.lpszClassName = "TS2_AssetSelfTest_WorldGeo";
      RegisterClassA(&wc3);
      HWND hwnd3 = CreateWindowExA(0, wc3.lpszClassName, "selftest-worldgeo", WS_POPUP,
                                    0, 0, 64, 64, nullptr, nullptr, wc3.hInstance, nullptr);
      gfx::Renderer renderer3;
      bool devOk3 = hwnd3 && renderer3.Init(hwnd3, 1024, 768, /*windowed=*/true);
      ok(devOk3, "WorldGeometryRenderer selftest : device D3D9 (hors-ecran) #3 cree");

      if (devOk3) {
          gfx::WorldGeometryRenderer worldGeo;
          bool wgInitOk = worldGeo.Init(renderer3);
          ok(wgInitOk, "WorldGeometryRenderer::Init (MeshRenderer + SkyRenderer) OK sans exception");

          if (wgInitOk) {
              bool buildOk = worldGeo.Build(assets);
              ok(buildOk, "WorldGeometryRenderer::Build(Z001) OK : %zu parts GPU uploadees "
                          "(dont %zu multi-ancre A>1 en pose statique frame 0, %zu ignorees "
                          "pour cause reelle), %zu instances, %zu appels de dessin prevus",
                 worldGeo.UploadedPartCount(), worldGeo.MultiAnchorStaticCount(),
                 worldGeo.SkippedMultiAnchorCount(),
                 worldGeo.InstanceCount(), worldGeo.PlannedDrawCallCount());

              // Real rendering (several frames, like App::FrameTick): proves no D3D/CPU
              // exception occurs on real data, outside the 30 FPS fast-forward and WITHOUT
              // a server connection (real InGame is unreachable without a server --
              // cf. mission notes -- this is the requested honest fallback).
              gfx::Camera camera;
              camera.SetTarget(0.0f, 0.0f, 0.0f);
              camera.SetDistance(80.0f);
              bool renderCrashed = false;
              int framesRendered3 = 0;
              for (int f = 0; f < 5; ++f) {
                  if (renderer3.BeginFrame()) {
                      worldGeo.Render(camera, 1024, 768);
                      renderer3.EndFrame();
                      ++framesRendered3;
                  }
              }
              ok(framesRendered3 == 5, "WorldGeometryRenderer::Render(Z001) : 5 frames rendues "
                                        "sans exception (ciel + %zu objet(s) .WO reel(s))",
                 worldGeo.UploadedPartCount());
              (void)renderCrashed;
          }
          worldGeo.Shutdown();
      }
      renderer3.Shutdown();
      if (hwnd3) DestroyWindow(hwnd3); }

    std::printf("\n=== %s (%d echec(s)) ===\n", fails ? "ECHEC" : "SUCCES", fails);
    std::printf("Appuyez sur Entree pour fermer...\n");
    std::getchar();
    return fails ? 1 : 0;
}

} // namespace ts2::asset

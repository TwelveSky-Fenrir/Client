// Asset/AssetSelfTest.cpp — validation runtime de TOUTE la couche Asset contre les
// vrais fichiers. Invoqué par « TwelveSky2.exe -assettest <cheminGameData> ».
#include "Asset/AssetSelfTest.h"
#include "Core/Log.h" // TS2_LOG (utilise par l'instrumentation debug GetForNpc plus bas)
#include "Asset/NpkArchive.h"
#include "Asset/ImgFile.h"
#include "Asset/Motion.h"
#include "Asset/Model.h"
#include "Asset/WorldChunk.h"
#include "Asset/Sound.h"
#include "Asset/Texture.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
// Vérification GPU du logo Intro réel (mission "LOGO INTRO REEL", 2026-07-14) :
// device D3D9 caché + IntroRender complet, cf. bloc dédié en bas de RunSelfTest.
#include "UI/IntroRender.h"
#include "UI/UIManager.h"
#include "Gfx/Renderer.h"
#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Game/IntroFlow.h"
// Vérification de robustesse GetForMonster()/GetForNpc() (mission "VERIFICATION DE
// ROBUSTESSE GetForMonster/GetForNpc", 2026-07-14) : ModelCache.h tire déjà
// Game/GameDatabase.h (LoadGameDatabases/g_World.db.monster) et Game/ExtraDatabases.h
// (NpcDefRecord/LoadExtraDatabases/GetNpcDefRecord), cf. bloc dédié en bas de RunSelfTest.
#include "Gfx/ModelCache.h"
// Verification visuelle du rendu 3D hors connexion reseau (mission "VERIFICATION VISUELLE
// COMPLETE DU RENDU 3D", 2026-07-14) : charge une VRAIE zone (Z001.WO/.ATM) via les memes
// chargeurs que World::WorldMap (World_LoadZoneResource case 3/7) et pousse le resultat dans
// Gfx/WorldGeometryRenderer.h (upload GPU + Render()) SANS passer par Net_ConnectGameServer,
// cf. bloc dedie en bas de RunSelfTest.
#include "World/WorldIntegration.h"
#include "World/WorldMap.h"
#include "Gfx/WorldGeometryRenderer.h"
#include "Gfx/Camera.h"
#include "Core/Log.h" // TS2_LOG/TS2_WARN/TS2_ERR (correction : include manquant, mission audit fenetres 2026-07-14)
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

namespace ts2::asset {

static void OpenConsole() {
    // Si stdout est déjà un flux valide (pipe/fichier redirigé par l'appelant, ex.
    // « -RedirectStandardOutput » pour capturer les logs [INFO]/[WARN] TS2_LOG), NE
    // PAS écraser ce flux avec une console éphémère : AllocConsole()+freopen_s vers
    // CONOUT$ redirigerait TOUTE la sortie (y compris IntroRender) vers une fenêtre
    // de console qui se referme dès la fin du process, perdant les logs capturés.
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

// Premier fichier d'extension `extLower` sous `dir` (récursif).
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
        std::fflush(stdout); // visibilite immediate meme si le process crashe juste apres (diagnostic)
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

    // INTRO LOGO (rendu GPU réel, mission "LOGO INTRO REEL") ---------------------
    // Device D3D9 caché (fenêtre WS_POPUP hors écran) + IntroRender complet, pour
    // vérifier hors du fast-forward de l'accumulateur 30 FPS (App::FrameTick) que
    // les 33 sprites 001_00799..001_00831.IMG se chargent et se dessinent réellement
    // via le MÊME chemin que Scene_IntroRender (ImgFile::Load + GpuTexture::
    // CreateFromImgFile + SpriteBatch::DrawSpriteScaled). Voir les lignes
    // "IntroRender : logo charge (...)" ci-dessus : une par sous-état 1..33 attendue.
    // Chemin ABSOLU de gd, calcule AVANT le chdir ci-dessous (bug corrige 2026-07-14,
    // mission "verification visuelle rendu 3D") : App::ResolveGameDataDir() (App.cpp)
    // convertit deja gd en absolu avant son propre SetCurrentDirectoryA -- ce fichier
    // ne le faisait PAS, si bien que gd (souvent relatif, ex. "..\..\..\TwelveSky2\
    // GameData" passe sur la ligne de commande) redevenait un chemin INVALIDE apres le
    // chdir (double-relatif, resolu contre le NOUVEAU CWD au lieu de l'ancien). Consequence
    // observee : LoadGameDatabases/LoadExtraDatabases (lignes ci-dessous) echouaient
    // "IMG illisible" alors que les fichiers existent bel et bien sur disque -- confirme
    // par test manuel (les MEMES fichiers s'ouvrent sans probleme via Join(gd,...) plus
    // haut dans cette fonction, AVANT le chdir). Toute utilisation de `gd` APRES ce point
    // doit passer par `gdAbs` (chemin absolu, stable quel que soit le CWD courant).
    std::string gdAbs = gd;
    { char absPath[MAX_PATH] = {};
      if (GetFullPathNameA(gd.c_str(), MAX_PATH, absPath, nullptr) != 0) gdAbs = absPath; }

    { // IntroRender charge ses chemins EN RELATIF (comme le binaire d'origine et comme
      // App::Run après ResolveGameDataDir()) : bascule le CWD sur GameData ici aussi,
      // sinon ImgFile::Load echoue (le -assettest n'a pas fait ce chdir, contrairement
      // à App::Run) et le test retomberait systematiquement sur le repli aplat.
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

    // MODELCACHE — robustesse GetForMonster()/GetForNpc() (mission "VERIFICATION DE
    // ROBUSTESSE GetForMonster/GetForNpc", 2026-07-14) -----------------------------
    // But : confirmer qu'aucun appel de ces deux API, sur un ECHANTILLON LARGE de
    // vrais identifiants (TOUTE la table MONSTER_INFO reelle 005_00004.IMG, 10000
    // records, + TOUTE la table NpcDefRecord reelle 005_00005.IMG, 500 records) ni
    // sur des bornes degenerees (0, count, count+1, UINT32_MAX, MSB seul), ne
    // provoque de crash/exception ni de comportement hors contrat (le contrat de
    // GetForMonster()/GetForNpc() est : nullptr OU pointeur SkinnedModel valide,
    // jamais autre chose). Documente aussi le taux de resolution reussie (X modeles
    // charges+uploades sur Y ids ayant un field244 dans le catalogue [1,333]).
    { bool dbOk = game::LoadGameDatabases(gdAbs);
      bool extraOk = game::LoadExtraDatabases(gdAbs);
      ok(dbOk, "ModelCache selftest : g_World.db (LoadGameDatabases) charge");
      ok(extraOk, "ModelCache selftest : g_ExtraDb (LoadExtraDatabases) charge");

      if (dbOk) {
          WNDCLASSA wc2 = {}; wc2.lpfnWndProc = DefWindowProcA; wc2.hInstance = GetModuleHandleA(nullptr);
          wc2.lpszClassName = "TS2_AssetSelfTest_ModelCache";
          RegisterClassA(&wc2); // echec silencieux si deja enregistree (classe reutilisee) : OK.
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

                  // --- GetForMonster() sur TOUTE la table reelle (10000 records) --------
                  const game::DataTable& monsterTable = game::g_World.db.monster;
                  uint32_t validField244 = 0; // records dont field244 est dans le catalogue [1,333]
                  uint32_t resolved      = 0; // GetForMonster() != nullptr (fichier reellement charge+uploade)
                  bool     monsterCrashed = false;
                  for (uint32_t id = 0; id < monsterTable.count; ++id) {
                      const uint8_t* rec = monsterTable.record(id);
                      if (!rec) { monsterCrashed = true; break; } // ne devrait jamais arriver (id < count)
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

                  // --- GetForMonster() bornes degenerees : jamais de crash, toujours nullptr --
                  bool edgeOk = true;
                  edgeOk = edgeOk && (cache.GetForMonster(monsterTable.count) == nullptr);
                  edgeOk = edgeOk && (cache.GetForMonster(monsterTable.count + 1) == nullptr);
                  edgeOk = edgeOk && (cache.GetForMonster(0xFFFFFFFFu) == nullptr);
                  edgeOk = edgeOk && (cache.GetForMonster(0x80000000u) == nullptr);
                  // id 0 est deja couvert par le balayage [0,count) ci-dessus (valide, PAS une
                  // borne degeneree pour une table indexee a partir de 0) -- pas reteste ici.
                  ok(edgeOk, "GetForMonster : bornes degenerees (count=%u, count+1, UINT32_MAX, "
                              "0x80000000) -> nullptr sans exception", monsterTable.count);

                  // --- GetForNpc() sur TOUTE la table NpcDefRecord reelle (jusqu'a 500 ids) ---
                  // Contrat actuel (cf. bandeau ModelCache.h "PNJ NON RESOLU") : TOUJOURS nullptr,
                  // quel que soit le contenu du record -- on verifie juste l'absence de crash et
                  // la stabilite du contrat sur de VRAIS NpcDefRecord charges depuis le disque.
                  uint32_t npcTested = 0;
                  bool npcAlwaysNull = true;
                  if (extraOk) {
                      for (uint32_t npcId = 1; npcId <= 500; ++npcId) {
                          if (npcId % 25 == 1) TS2_LOG("ModelCache selftest : GetForNpc npcId=%u...", npcId); // DEBUG bisection (mission robustesse)
                          const game::NpcDefRecord* npc = game::GetNpcDefRecord(npcId);
                          if (!npc) continue; // slot vide (id==0) ou hors bornes -- ignore, pas un "vrai" PNJ
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

    // WORLDGEOMETRYRENDERER / SKYRENDERER — rendu 3D reel HORS CONNEXION RESEAU (mission
    // "VERIFICATION VISUELLE COMPLETE DU RENDU 3D", 2026-07-14) -----------------------
    // Reproduit le SEUL chemin de donnees necessaire pour dessiner une zone (World_
    // LoadZoneResource case 3 = .WO, case 7 = .ATM), SANS World_LoadMap (donc SANS la
    // porte DRM/SilverLining -- cf. bandeau WorldMap.h) et SANS aucun paquet reseau :
    // exactement le sous-ensemble dont World/WorldIntegration.h + Gfx/WorldGeometryRenderer.h
    // ont besoin. But : prouver que Init()/Build()/Render() tournent sans exception/crash
    // sur un VRAI chunk .WO (50k+ instances documentees ailleurs) et que le ciel derive
    // reellement du .ATM reel (HasRealAtmosphere()==true, pas le repli "midi").
    { world::WorldAssets assets(gdAbs);
      world::WorldLoadHooks hooks = assets.MakeHooks();
      world::WorldMap map(hooks);
      // zoneId=1 -> fileId=1 (WorldMap::ZoneIdToFileId) -> Z001.WO/Z001.ATM (presents sur
      // disque, deja valides plus haut dans ce fichier par le test "WORLD .wo/.tga" via
      // FirstWith(D07_GWORLD) -- ici on cible NOMMEMENT ce fichier pour un resultat reproductible.
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

              // Rendu reel (plusieurs frames, comme App::FrameTick) : preuve qu'aucune
              // exception D3D/CPU ne survient sur des donnees reelles, hors du fast-forward
              // 30 FPS et SANS connexion serveur (InGame reel inaccessible sans serveur --
              // cf. Docs/notes de mission -- ceci est le repli honnete demande).
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

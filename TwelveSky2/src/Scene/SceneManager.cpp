// Scene/SceneManager.cpp — machine de scènes : Intro -> ServerSelect/Login/CharSelect (LoginScene)
// -> EnterWorld -> InGame (GameHud). Dispatch fidèle à cSceneMgr_Update/_Render.
#include "UI/LoginScene.h"      // tire Net/NetSystem.h (winsock2) en premier
#include "UI/GameHud.h"
#include "UI/GameWindows.h"
#include "Gfx/Renderer.h"
#include "Gfx/Camera.h"
#include "Net/NetSystem.h"
#include "Scene/SceneManager.h"
#include "Scene/WorldRenderer.h"
#include "Gfx/WorldGeometryRenderer.h" // géométrie statique .WO (distinct de WorldRenderer=entités)
#include "World/WorldIntegration.h"    // world::WorldAssets (charge réellement Z%03d.WO)
#include "World/WorldMap.h"            // world::WorldMap::LoadZoneResource / ZoneIdToFileId
#include "Audio/AudioSystem.h"        // audio::BgmChannel (slot BGM de scène, cSceneMgr +612)
#include "Config/GameOptions.h"       // config::g_Options.BgmEnabled (g_BgmEnabled 0x84DEF0)
#include "Gfx/SpriteBatch.h"    // gfx::g_GameTimeSec
#include "Game/GameState.h"     // game::g_World (zoneId)
#include "Game/ClientRuntime.h" // game::Str (messages d'erreur EnterWorld)
#include "Game/MapWarp.h"       // game::kSelfActionStateOffset (porte de gating InGame étape 12)
#include "Net/SendPackets.h"    // Net_SendPacket_Op13 (keepalive) / Net_SendOp64 (poll requête clan/faction)
#include "Net/CharSelectPackets.h" // net::BuildEnterWorldTail72 (bloc tail72 confirmé)
// 4 systèmes d'appoint du tick InGame (mission de câblage 2026-07-14, cf. rapports des
// agents dédiés) : anim/collision, cycle de vie d'entité, caméra/warp/potion/guilde,
// combo/pickup/quête. Câblés ci-dessous dans case Scene::InGame (RunMainTick).
#include "Game/AnimationTick.h"
#include "Game/EntityLifecycleTick.h"
#include "Game/CameraWarpTick.h"
#include "Game/ComboPickupTick.h"
#include "Game/NpcInteraction.h" // NpcInteractionSystem::AutoInteractForPet (déjà porté, réutilisé)
// 3 systèmes supplémentaires câblés dans ce même bloc (mission de câblage 2026-07-14,
// suite des 4 ci-dessus) : effets au sol/auras/objets de zone, gate auto-cible/combat,
// pont caméra 3e personne.
#include "Game/GroundAuraWorldObjectTick.h"
#include "Game/AutoTargetCombatGate.h"
#include "Gfx/CameraThirdPersonBridge.h"
#include "Core/Log.h"
#include <windows.h>
#include <cstring>
#include <cstdio>   // std::snprintf (chemin BGM "Z%03d.BGM")

// TODO journalisé UNE SEULE FOIS par point d'intégration (évite le flood de logs pour les
// hooks appelés à 30 Hz depuis InGameTickFlow_Update — cf. case Scene::InGame ci-dessous).
#define TS2_INGAME_TODO_ONCE(...) do { \
        static bool s_ts2IngameTodoWarned = false; \
        if (!s_ts2IngameTodoWarned) { s_ts2IngameTodoWarned = true; TS2_LOG(__VA_ARGS__); } \
    } while (0)

namespace ts2 {

// Définition du miroir de g_SceneSubState 0x1676184 (champ +4 de cSceneMgr 0x1676180),
// déclaré dans SceneManager.h. Tenu à jour aux 4 points de synchro marqués « 0x1676184 »
// ci-dessous ; consommé par App/PlayerInputController.cpp (garde Camera_UpdateFromInput
// @0x50B7EC). Valeur initiale 0 = sous-état d'entrée de toute scène. // 0x1676184
int g_SceneSubState = 0;

SceneManager::SceneManager() = default;
SceneManager::~SceneManager() { Shutdown(); }

static const char* SceneName(Scene s) {
    switch (s) {
    case Scene::Intro:        return "Intro";
    case Scene::ServerSelect: return "ServerSelect";
    case Scene::Login:        return "Login";
    case Scene::CharSelect:   return "CharSelect";
    case Scene::EnterWorld:   return "EnterWorld";
    case Scene::InGame:       return "InGame";
    default:                  return "None";
    }
}

void SceneManager::Init(gfx::Renderer& renderer, net::NetSystem& net, void* notifyHwnd,
                        int screenW, int screenH, const std::string& gameDataDir,
                        int serverModeFlag) {
    renderer_    = &renderer;
    net_         = &net;
    notifyHwnd_  = notifyHwnd;
    screenW_     = screenW;
    screenH_     = screenH;
    gameDataDir_ = gameDataDir;
    scene_    = Scene::None;
    subState_ = 0;
    frameCount_ = 0;
    g_SceneSubState = 0;   // miroir du champ +4 de cSceneMgr (état neuf) // 0x1676184

    // Scènes shell de connexion (ServerSelect/Login/CharSelect).
    login_ = std::make_unique<ui::LoginScene>();
    if (!login_->Init(renderer.Device(), &net, static_cast<HWND>(notifyHwnd), screenW, screenH, serverModeFlag))
        TS2_WARN("LoginScene::Init a echoue (rendu login indisponible).");

    // HUD en jeu : construit à la volée en entrant en scène InGame.
    hud_ = std::make_unique<ui::GameHud>();
    hudReady_ = false;

    // Fenêtres de jeu (Entrepôt/Guilde/Quête/Compétences/Options/Social/AutoPlay/
    // Marchand/Groupe/Échange/Personnage) : même cycle de vie que le HUD.
    windows_ = std::make_unique<ui::GameWindows>();
    windowsReady_ = false;

    // Rendu 3D des entités (players/monsters) : même cycle de vie que le HUD.
    world_ = std::make_unique<WorldRenderer>();
    worldReady_ = false;

    // Géométrie de monde statique (.WO, cf. Gfx/WorldGeometryRenderer.h) : le rendu GPU
    // (worldGeom_->Init/Build) reste construit paresseusement à l'entrée en InGame (cf.
    // Change()), car il a besoin d'un device D3D "chaud" et de zoneId. En revanche
    // worldAssets_/worldMap_ (données de zone CPU, PAS le rendu) sont désormais construits
    // ICI, PAS à l'entrée InGame comme avant ce câblage : Scene::EnterWorld en a besoin
    // AVANT InGame pour son sous-état LoadZoneResources (cf. Update(), case EnterWorld,
    // host.LoadZoneResource) — voir Docs/TS2_ENTERWORLD_WIRING_TODO.md pour l'audit complet.
    worldGeom_ = std::make_unique<gfx::WorldGeometryRenderer>();
    worldGeomReady_ = false;

    // Slot BGM de scène (cSceneMgr +612) : ctor par défaut = zéro-init, équivalent de
    // cSceneMgr_ReinitBgm 0x517A80 -> SndMgr_InitBgmSlot 0x6A80A0 (SoundObj remis à 0).
    // Le device audio (DirectSoundCreate8) est créé ailleurs (AudioSystem::Init, cf. App) ;
    // ici on ne fait qu'allouer le slot. LoadZoneBgm charge le .BGM à l'entrée en jeu.
    bgm_ = std::make_unique<audio::BgmChannel>();

    if (!gameDataDir_.empty()) {
        worldAssets_ = std::make_unique<world::WorldAssets>(gameDataDir_);
        worldMap_    = std::make_unique<world::WorldMap>(worldAssets_->MakeHooks());
        worldMap_->SetDevice(renderer.Device());
    } else {
        TS2_WARN("SceneManager: gameDataDir vide - WorldMap indisponible "
                 "(EnterWorld/InGame degrades : chargement de zone impossible).");
    }

    TS2_LOG("SceneManager initialise (%dx%d).", screenW, screenH);
}

void SceneManager::Shutdown() {
    if (login_)   { login_->Shutdown();   login_.reset(); }
    if (windows_) { windows_->Shutdown(); windows_.reset(); }
    if (hud_)     { hud_->Shutdown();     hud_.reset(); }
    if (world_)   { world_->Shutdown();   world_.reset(); }
    if (worldGeom_) { worldGeom_->Shutdown(); worldGeom_.reset(); }
    // SceneMgr_ReleaseSoundBuffers 0x517B60 -> Snd_ReleaseBuffers(cSceneMgr+153) 0x6A80D0 :
    //   libère le slot BGM au destructeur de cSceneMgr (App_Shutdown 0x462480).
    if (bgm_) { bgm_->Release(); bgm_.reset(); }
    worldMap_.reset();
    worldAssets_.reset();
    renderer_ = nullptr;
    net_ = nullptr;
}

void SceneManager::StartIntro() { Change(Scene::Intro); }

void SceneManager::Change(Scene s) {
    const Scene prev = scene_;   // sert au release du slot BGM en QUITTANT InGame (voir bas)
    TS2_LOG("Scene : %s -> %s", SceneName(scene_), SceneName(s));
    scene_ = s;
    subState_ = 0;
    frameCount_ = 0;
    // Tout changement de scène repart du sous-état 0 dans le binaire : le champ +4 de
    // cSceneMgr est remis à 0 par les writers de g_SceneMgr (ex. op 0x18
    // Pkt_GameServerConnectResult 0x469CF0 : g_SceneMgr=5 @0x469d95 PUIS g_SceneSubState=0
    // @0x469d9f), et Scene_EnterWorldUpdate le repose lui-même à 1 après son case 0
    // (@0x52C0B0). Sans ce reset, la garde @0x50B7EC verrait un sous-état périmé de la
    // scène précédente. // 0x1676184
    g_SceneSubState = 0;

    // M6 — chaque ENTREE en scene EnterWorld repart de subState 0 dans le binaire : op 0x18
    // Pkt_GameServerConnectResult 0x469CF0 (@0x469d9f, g_SceneSubState=0) et le flux normal
    // posent g_SceneSubState 0x1676184 = 0, et Scene_EnterWorldUpdate 0x52BFF0 lit son etat
    // courant dans *(this+1)==g_SceneSubState. enterWorldState_ modelise ce champ pour cette
    // scene -> reset symetrique du reset deja present dans ReloadZone. Sans lui, une 2e entree
    // EnterWorld reprend d'un etat perime (WaitServerAck/Failed du cycle precedent -> machine
    // gelee). // 0x1676184 / 0x52BFF0
    if (s == Scene::EnterWorld)
        enterWorldState_ = game::EnterWorldFlowState{};

    // W5b — reset SYMÉTRIQUE pour la scène InGame, même raison que le bloc EnterWorld ci-dessus.
    // Pkt_EnterWorld 0x464160 est le SEUL writer de g_SceneMgr 0x1676180 = 6 — prouvé par balayage
    // d'octets sur l'image entière : find_bytes "C7 05 80 61 67 01 06 00 00 00" -> 1 SEULE occurrence
    // (@0x464304), et aucun store par registre (A3/89 05/89 0D/… sur 0x1676180 = 0 match). Or ce même
    // writer repose AUSSI, dans la foulée, g_SceneSubState 0x1676184 = 0 @0x46430E et
    // dword_1676188 = 0 @0x464318 : l'automate de Scene_InGameUpdate 0x52C600 repart donc de
    // case 0 (Setup) à CHAQUE entrée en scène 6, jamais d'un état hérité.
    // Le `this` de Scene_InGameUpdate EST bien cSceneMgr 0x1676180 : xrefs_to 0x52C600 -> 1 seul
    // appelant (cSceneMgr_Update 0x517BF0 @0x517c79), lui-même appelé avec
    // `mov ecx, offset g_SceneMgr` @0x462636 -> *(this+4) = g_SceneSubState (le switch @0x52C61F)
    // et *(this+8) = dword_1676188 (le compteur). game::InGameTickFlowState{} = {Setup=0,
    // frameCounter=0} en est le miroir 1:1.
    // Sans ce reset, un warp InGame->EnterWorld->InGame (op 0x18 -> op 0x0c) laisse inGameTickState_
    // à MainTick : le `g_SceneSubState = 0` posé ligne 160 est écrasé dès la frame suivante par
    // `g_SceneSubState = (int)inGameTickState_.state` = 4 (ligne ~945), Setup ne rejoue pas, et
    // surtout InitCamera (Cam_SetLookAt @0x52C759 / Camera_SetEyeTarget @0x52C7CF) ne recadre
    // JAMAIS la caméra sur la nouvelle zone. // 0x464304 / 0x46430E / 0x464318 / 0x52C600
    if (s == Scene::InGame)
        inGameTickState_ = game::InGameTickFlowState{};

    // Entrée en jeu : initialise le HUD et les fenêtres de jeu une seule fois (device stable).
    if (s == Scene::InGame && hud_ && !hudReady_ && renderer_) {
        hudReady_ = hud_->Init(*renderer_, screenW_, screenH_);
        if (!hudReady_) TS2_WARN("GameHud::Init a echoue (HUD indisponible).");
    }
    if (s == Scene::InGame && windows_ && !windowsReady_ && renderer_) {
        windowsReady_ = windows_->Init(*renderer_, notifyHwnd_, screenW_, screenH_);
        if (!windowsReady_) TS2_WARN("GameWindows::Init a echoue (fenetres indisponibles).");
    }
    if (s == Scene::InGame && world_ && !worldReady_ && renderer_) {
        worldReady_ = world_->Init(*renderer_, screenW_, screenH_);
        if (!worldReady_) TS2_WARN("WorldRenderer::Init a echoue (rendu monde indisponible).");
    }
    // Géométrie de monde statique (.WO) : chargement UNE SEULE FOIS à l'entrée en InGame,
    // pour le zoneId courant (game::g_World.zoneId). Pas de rechargement au changement de
    // zone en cours de partie (TODO futur : accrocher World_LoadZoneResource(ObjectsWO) au
    // flux de warp/MapWarp, cf. Game/MapWarp.h — hors périmètre de ce câblage initial, comme
    // host.LoadZoneResource déjà en TODO dans le case EnterWorld ci-dessous).
    if (s == Scene::InGame && worldGeom_ && !worldGeomReady_ && renderer_) {
        worldGeomReady_ = worldGeom_->Init(*renderer_);
        if (!worldGeomReady_) {
            TS2_WARN("WorldGeometryRenderer::Init a echoue (geometrie statique indisponible).");
        } else if (!worldMap_ || !worldAssets_) {
            // worldMap_/worldAssets_ sont désormais construits UNE FOIS dans Init() (cf.
            // Docs/TS2_ENTERWORLD_WIRING_TODO.md §2), pas ici, pour être déjà disponibles
            // pendant Scene::EnterWorld. nullptr ici => gameDataDir_ était vide à Init()
            // (déjà averti à ce moment-là).
            TS2_WARN("SceneManager: WorldMap indisponible (gameDataDir vide a Init) - "
                     "chargement .WO impossible.");
        } else {
            const int zoneId = game::g_World.zoneId;
            // Pose la cle de zone courante AVANT tout chargement de couche : SetCurrentZoneId
            // n'etait appele nulle part ailleurs (grep) -> World_LoadCurrentZoneModel 0x4DD6E0
            // (qui lit g_SelfMorphNpcId 0x1675A98) travaillait sur zone 0. // 0x4DD6E0
            if (worldMap_) worldMap_->SetCurrentZoneId(zoneId); // g_SelfMorphNpcId 0x1675A98
            // Redondant avec le chargement idx=3 (ResourceKind::ObjectsWO) déjà effectué
            // pendant Scene::EnterWorld (LoadZoneResources, cf. host.LoadZoneResource dans
            // Update()) - WorldMap::LoadZoneResource est idempotent (recharge le même
            // fichier). Gardé ici par sécurité pour les chemins qui forcent
            // Change(Scene::InGame) directement SANS passer par EnterWorld
            // (Scene/SceneAudit.cpp, Tools/UiWindowSelfTest.cpp).
            const unsigned char ok = worldMap_->LoadZoneResource(zoneId, world::ResourceKind::ObjectsWO);
            if (ok) {
                worldGeom_->Build(*worldAssets_);
                TS2_LOG("SceneManager: geometrie .WO zone %d chargee (%zu parts GPU, %zu ignorees A>1).",
                        zoneId, worldGeom_->UploadedPartCount(), worldGeom_->SkippedMultiAnchorCount());
            } else {
                TS2_WARN("SceneManager: World_LoadZoneResource(ObjectsWO, zone=%d) a echoue "
                         "(zoneId->fileId inconnu ou fichier Z%%03d.WO absent).", zoneId);
            }
        }
    }

    // --- Slot BGM de scène (cSceneMgr +612) : câblage enter-world / exit ---
    // Entrée en jeu = "enter-world" : charge+joue en boucle le .BGM de la zone courante,
    //   comme World_LoadZoneResource 0x4DCB60 case 12 (chemin "G03_GDATA\D10_WORLDBGM\Z%03d.BGM")
    //   puis le play gaté g_BgmEnabled (Player_ResetCombatState 0x50f761/0x50f76e ; MÊME cycle
    //   release->load->play que Scene_ServerSelectUpdate 0x518B30 sur le slot cSceneMgr +612).
    //   Hors des gardes du bloc géométrie ci-dessus : le BGM doit se charger même si le rendu
    //   .WO échoue. Filet de sécurité robuste pour les chemins qui forcent Change(InGame)
    //   directement ; le flux EnterWorld (host.LoadZoneResource idx=12) ne charge, lui, qu'un
    //   SoundBuffer throwaway côté WorldAssets. LoadZoneBgm fait Release AVANT reload (0x518bde).
    // TODO(zone-change en cours de partie) : un warp/MapWarp (Game/MapWarp.h) ne repasse PAS
    //   par Change(InGame) aujourd'hui (même limite que la géométrie .WO, cf. lignes ci-dessus).
    //   Quand le flux de warp sera câblé, il devra rappeler LoadZoneBgm(nouveauZoneId) pour
    //   recharger l'ambiance (World_LoadZoneResource case 12 est ré-appelée par zone dans le binaire).
    if (s == Scene::InGame) {
        LoadZoneBgm(game::g_World.zoneId);
    } else if (prev == Scene::InGame) {
        // Sortie du jeu (retour menu / déconnexion) : coupe l'ambiance de zone.
        //   SceneMgr_ReleaseSoundBuffers 0x517B60 -> Snd_ReleaseBuffers 0x6A80D0.
        ReleaseBgm();
    }
}

// --- Slot BGM de scène : chargement (enter-world/zone-change) + release (exit) ---
// Voir Audio/AudioSystem.h (BgmChannel) pour l'arbitrage complet des ancres IDA.
void SceneManager::LoadZoneBgm(int zoneId) {
    if (!bgm_) return;
    // World_LoadZoneResource 0x4DCB60 case 12 : Z = World_ZoneIdToFileId(zoneId) 0x4db0f0.
    //   fileId == -1 -> la zone n'a pas de BGM (le binaire saute le chargement, `if (v3 != -1)`
    //   @0x4dd406) : on coupe l'éventuel BGM précédent et on sort.
    const int fileId = world::WorldMap::ZoneIdToFileId(zoneId);
    if (fileId < 0) {
        TS2_LOG("SceneManager: zone %d sans fileId -> pas de BGM.", zoneId);
        ReleaseBgm();
        return;
    }
    // 0x4dd41d : chaîne .rdata "G03_GDATA\\D10_WORLDBGM\\Z%03d.BGM" (aG03GdataD10Wor_0 @0x7a7cc8).
    //   Le décodeur (OggVorbisLoadCallback via asset::ReadOggFile) attend un chemin résoluble
    //   -> on préfixe la racine GameData, comme World/WorldIntegration::LoadWorldBgm.
    char rel[64];
    std::snprintf(rel, sizeof(rel), "G03_GDATA\\D10_WORLDBGM\\Z%03d.BGM", fileId); // 0x4dd41d
    const std::string full = gameDataDir_.empty() ? std::string(rel)
                                                   : (gameDataDir_ + "\\" + rel);
    // g_BgmEnabled 0x84DEF0 (option f12) — gate du play (0x518c03 / 0x50f761). vol=100 en dur
    //   aux deux sites de play (0x518c14 / 0x50f76e) ; MusicVolume (option idx10) s'applique
    //   ailleurs (sons positionnels / UI), pas au play du slot BGM.
    const bool enabled = (config::g_Options.BgmEnabled == 1);
    if (bgm_->LoadAndPlay(full, enabled, 100)) {
        TS2_LOG("SceneManager: BGM zone %d (Z%03d.BGM) chargee%s.", zoneId, fileId,
                enabled ? " et jouee (boucle)" : " (option BGM off : silencieuse)");
    } else {
        // Guard exigée : .BGM absent / device audio indispo / décodeur Ogg absent -> muet,
        //   AUCUN crash (client silencieux pour cette zone, comme un DirectSound non dispo).
        TS2_WARN("SceneManager: BGM zone %d (Z%03d.BGM) indisponible "
                 "(fichier absent, audio non initialise ou decodeur Ogg absent).", zoneId, fileId);
    }
}

void SceneManager::ReleaseBgm() {
    // SceneMgr_ReleaseSoundBuffers 0x517B60 -> Snd_ReleaseBuffers(cSceneMgr+153) 0x6A80D0.
    if (bgm_) bgm_->Release();
}

// Rechargement RE-ENTRANT de zone (warp) — cf. declaration SceneManager.h. Rejoue la case 1
// de Scene_EnterWorldUpdate 0x52BFF0 (World_LoadZoneResource 0x4DCB60 idx 1..12) + rebuild
// GPU .WO + BGM, en LEVANT les gardes one-shot que Change() ne rejoue jamais en re-entree.
void SceneManager::ReloadZone(int zoneId) {
    // 1. Cle de zone courante : g_SelfMorphNpcId = g_TargetZoneId (Scene_EnterWorldUpdate
    //    0x52C173). Lue par World_LoadCurrentZoneModel 0x4DD6E0 (SetCurrentZoneId) ET
    //    cGameData_LoadZoneNpcInfo 0x5578E0 (via g_World.zoneId -> LoadZoneNpcs, spawn self).
    game::g_World.zoneId = zoneId;                        // g_SelfMorphNpcId 0x1675A98
    if (worldMap_) worldMap_->SetCurrentZoneId(zoneId);   // World_LoadCurrentZoneModel 0x4DD6E0

    // 2. Re-execute World_LoadZoneResource(zoneId, kind) pour kinds 1..12, fidele a la
    //    boucle case 1 (0x52C0F8 : idx 0..19, seuls 1..12 chargent, le reste no-op).
    //    Idempotent (WorldMap::LoadZoneResource recharge le meme fichier). // 0x4DCB60
    if (worldMap_) {
        for (int idx = 1; idx <= 12; ++idx)
            worldMap_->LoadZoneResource(zoneId, static_cast<world::ResourceKind>(idx)); // 0x4DCB60 case idx
    }

    // 3. Reconstruit la geometrie .WO GPU pour la nouvelle zone : LEVE la garde one-shot
    //    worldGeomReady_ (dans Change() le bloc rebuild est gate !worldGeomReady_ -> jamais
    //    rejoue en re-entree). ObjectsWO (kind 3) vient d'etre recharge dans la boucle ci-dessus
    //    -> worldAssets_ a jour. // 0x4DCB60 case 3 (ObjectsWO) + worldGeom_->Build
    if (worldGeomReady_ && worldGeom_ && worldAssets_) {
        worldGeom_->Build(*worldAssets_);
        TS2_LOG("SceneManager: rechargement zone %d -> geometrie .WO reconstruite "
                "(%zu parts GPU).", zoneId, worldGeom_->UploadedPartCount());
    }

    // 4. Recharge l'ambiance BGM de la nouvelle zone (LEVE la garde one-shot ; meme cycle
    //    release->load->play que World_LoadZoneResource 0x4DCB60 case 12). // 0x4DCB60 case 12
    LoadZoneBgm(zoneId);
}

void SceneManager::ConsumePending() {
    if (!login_) return;
    const Scene p = login_->PendingScene();
    if (p != Scene::None) {
        login_->ClearPending();
        Change(p);
    }
}

void SceneManager::Update(double dt, gfx::Camera& camera) {
    ++frameCount_;
    // H2 — op 0x18 Pkt_GameServerConnectResult 0x469CF0 pose g_SceneMgr=5 (@0x469d95, case 0 /
    // sous-resultat 0 ; confirme IDA : g_SceneSubState=0 @0x469d9f, dword_1676188=0 @0x469da9)
    // PENDANT InGame (reconnexion/relais serveur). Le handler reseau (Net/GameHandlers_Misc.cpp
    // op 0x18) arme game::g_World.sceneReloadPending, mais ce flag n'est lu QUE par la
    // case Scene::EnterWorld ci-dessous -> jamais atteint depuis InGame (reload mort). On
    // reproduit ICI le basculement scene 6->5 : le switch dispatchera alors sur case EnterWorld
    // LA MEME frame (comme le binaire ou g_SceneMgr==5 avant cSceneMgr_Update), laquelle
    // consommera le flag via sa branche sceneReloadPending existante (ReloadZone). NE PAS clear
    // le flag ici (la case EnterWorld le fera). // 0x469d95 / g_SceneMgr 0x1676180=5
    if (scene_ == Scene::InGame && game::g_World.sceneReloadPending) {
        Change(Scene::EnterWorld);
    }
    switch (scene_) {
    case Scene::Intro:
        // Automate fidèle Scene_IntroUpdate 0x517FE0 (Game/IntroFlow.h) : 90 + 33×3 + 90
        // = 279 frames (9,3 s @ 30 FPS), PAS 90 frames comme l'ancien placeholder.
        if (game::UpdateIntro(introState_, 0.0f)) Change(Scene::ServerSelect);
        break;
    case Scene::ServerSelect:
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) { login_->Update(scene_); ConsumePending(); }
        break;
    case Scene::EnterWorld: {
        // (W2-F1) RECHARGEMENT RE-ENTRANT (warp / op 0x18 Pkt_GameServerConnectResult 0x469CF0,
        // seul writer de g_SceneMgr=5 @0x469d95 ; il a deja fait Change(Scene::EnterWorld) et
        // arme ces 2 champs). Teste EN PREMIER. g_TargetZoneId 0x1675A9C = pendingWarpZoneId.
        if (game::g_World.sceneReloadPending) {
            game::g_World.sceneReloadPending = false;
            const int warpZone = (game::g_World.pendingWarpZoneId >= 0)
                                     ? game::g_World.pendingWarpZoneId
                                     : game::g_World.zoneId;
            game::g_World.pendingWarpZoneId = -1;
            // Re-arme la machine d'etat visuelle (case 0..3) pour la NOUVELLE zone : sans ce
            // reset, enterWorldState_ resterait bloque sur WaitServerAck/Failed du cycle
            // precedent. Scene_EnterWorldUpdate repart de subState 0 (0x52C00F). // 0x52BFF0
            enterWorldState_ = game::EnterWorldFlowState{};
            subState_ = 0; frameCount_ = 0;
            g_SceneSubState = 0;   // miroir du champ +4 : rechargement = sous-état 0 // 0x1676184
            // Ecrit g_World.zoneId + SetCurrentZoneId (equivalent g_SelfMorphNpcId=g_TargetZoneId
            // @0x52C173) et rejoue LoadZoneResource(1..12) + rebuild geo/BGM. // 0x52BFF0 / 0x4DCB60
            ReloadZone(warpZone);
            break; // laisse le flux visuel se derouler des la frame suivante
        }
        // Bascule PRIORITAIRE et RÉELLE InGame : armée par EntityManager::OnEnterWorld
        // (Game/EntityManager.cpp, réception op 0x0c) via game::g_World.
        // sceneEnterWorldPending, fidèle à dword_1676180=6 écrit DIRECTEMENT par
        // Pkt_EnterWorld dans le binaire (cf. GameState.h et
        // Docs/TS2_ENTERWORLD_WIRING_TODO.md). Testé EN PREMIER, avant
        // EnterWorldFlow_Update ci-dessous (qui ne gère plus qu'un timeout de secours).
        if (game::g_World.sceneEnterWorldPending) {
            game::g_World.sceneEnterWorldPending = false;
            Change(Scene::InGame);
            break;
        }
        // Automate fidèle Scene_EnterWorldUpdate 0x52BFF0 (Game/EnterWorldFlow.h) :
        // attente(30) -> 20 ressources de zone espacées de 10 frames (~200 frames,
        // ~6,7s) -> attente(30) -> envoi requête -> attente ACK serveur (jusqu'à
        // 5000 frames). La bascule InGame réelle est déclenchée par la RÉCEPTION du
        // paquet serveur EnterWorld (op 0x0c, cf. ci-dessus) — ce flux ne sert plus
        // que de PROGRESSION VISUELLE (chargement des ressources de zone) + timeout
        // de secours si le serveur ne répond jamais.
        game::EnterWorldFlowHost host;
        host.ResetUiAndAudio = [this] {
            // Scene_EnterWorldUpdate 0x52BFF0, case 0 (WaitBeforeUnload), bloc gardé par
            // `if ((unsigned)*(this+2) >= 0x1E)` @0x52C02C (30 frames) : purge de l'UI et de
            // l'audio résiduels de CharSelect. L'ORDRE CI-DESSOUS EST CELUI DU BINAIRE
            // (dialogs -> curseur -> focus -> scratch -> son), vérifié au désassemblage
            // (0x52C033..0x52C089).

            // (1) UI_ResetAllDialogs(&dword_1821D4C) @0x52C038 — réel, câblé sur le même
            // UIManager que le reste du shell. Sûr même si windows_ n'est pas encore
            // construit (InGame pas encore atteint) : la liste de dialogues enregistrés est
            // alors vide (UIManager::ResetAll() itère dialogs_, vector par défaut vide).
            ui::UIManager::Instance().ResetAll();                          // 0x52C038

            // (2) Util_SetClampedU8Field(&dword_8E714C, 0) @0x52C044 — NON BRANCHÉ.
            // RECTIFICATION D'ÉTIQUETTE (prouvée dans l'IDB cette mission) : ce n'est PAS un
            // « reset tooltip », contrairement à ce qu'annoncent Game/EnterWorldFlow.h:91 et
            // Game/InGameTickFlow.h:128 (fichiers hors de ce front — à corriger ailleurs).
            // dword_8E714C EST mPOINTER, le jeu de curseurs souris. Chaîne de preuve :
            //   - App_Init @0x461F8B : `mov ecx, offset dword_8E714C` puis
            //     `call CursorSet_LoadResources 0x4C0FA0` @0x461F90 ; sur échec, le MessageBox
            //     empile la chaîne "[Error::mPOINTER.Init()]" (aErrorMpointerI @0x7A6BC0,
            //     push @0x461FA3) -> ce manager EST mPOINTER ;
            //   - CursorSet_LoadResources 0x4C0FA0 : `*this = 0` @0x4C0FAC (+0 = index actif),
            //     puis +1..+9 = 9 HCURSOR (LoadCursorA, ids 0x66..0x6C puis 0x75 et 0x77) ;
            //   - Cursor_AnimateTick 0x4C1140 : `SetCursor(*(this + *this + 1))` @0x4C115A
            //     -> *this EST bien l'index du curseur actif ;
            //   - Util_SetClampedU8Field 0x4C1110 : `if (a2 <= 8) *this = a2;` (borne ≤ 8 =
            //     exactement 9 slots). Le nom IDA est générique (157 appelants) ; appliqué à
            //     0x8E714C il n'a qu'un sens : l'index de curseur.
            // Donc @0x52C044 = « remettre la forme du curseur souris au slot 0 ».
            // L'équivalent fidèle EXISTE déjà : game::CursorSet::SetActiveSlot(0)
            // (Game/MiscManagers.cpp:88, miroir exact de 0x4C1110). Il n'est PAS appelable
            // ici : l'instance est App::cursors_, membre PRIVÉ de App (App/App.h:43), et
            // App.h/App.cpp ne sont pas possédés par ce front. L'appel serait de toute façon
            // un no-op prouvé aujourd'hui (seuls LoadResources/DestroyAll écrivent
            // CursorSet::state, tous deux à 0 ; SetActiveSlot n'a aucun appelant) — même
            // arbitrage que UI/LoginScene.cpp:809-813 pour ce même appel (EA 0x51A8FD).
            // TODO [ancre 0x52C044 / 0x4C1110] : exposer App::cursors_ (décision
            // orchestrateur, fichier non possédé) puis appeler CursorSet::SetActiveSlot(0) ICI.

            // (3) UI_FocusEditBox(&g_UIEditBoxMgr, 0) @0x52C050. Avec a2 = 0, l'original
            // (0x50F4A0) fait, sous `if (a2 < 0x16)` (22 slots) : `*this = 0` (index de
            // focus ; 0 = jeu, 1..21 = saisie active — cf. commentaire IDA sur
            // g_UIEditBoxMgr 0x1668FC0) PUIS, la branche `if (*this)` étant fausse,
            // `SetFocus(hWndParent)` @0x50F4CB : retirer le focus clavier de tout EDIT natif
            // et le rendre à la fenêtre de jeu.
            // ClientSource n'a aujourd'hui AUCUN EDIT natif vivant (ui::Win32EditBox existe
            // mais aucun fichier ne l'inclut) : la saisie texte in-game est le widget
            // custom-dessiné ChatWindow (flag focused_). Les deux volets sont donc rendus par :
            //   - index de focus = 0   -> Chat().Unfocus() (UI/ChatWindow.h:189) ;
            //   - SetFocus(hWndParent) -> notifyHwnd_ EST hWndParent 0x815184, PROUVÉ cette
            //     mission : App_Init @0x461C51 fait `mov ds:hWndParent, ecx` avec ecx = arg_4
            //     = le HWND créé par WinMain 0x4609C0 ; côté C++, App::Init passe ce même
            //     hwnd_ à SceneManager::Init (App/App.cpp:489) -> notifyHwnd_.
            //     Sans effet observable tant qu'aucun EDIT natif ne prend le focus, mais
            //     fidèle et immédiatement correct dès que Win32EditBox sera câblé.
            if (hud_) hud_->Chat().Unfocus();                              // 0x52C050 / 0x50F4BB
            if (notifyHwnd_) ::SetFocus(static_cast<HWND>(notifyHwnd_));   // 0x50F4CB

            // (4) scratch 150 dw @0x52C055 (`for (i=0;i<150;++i) *(this+i+3)=0;`, soit
            // +0xC..+0x260) — NON MODÉLISÉ, donc RIEN à remettre à zéro. SceneManager.h:42
            // documente bien « +12 tampon 150 dw » mais aucun membre ne le réifie, et ces
            // 150 dwords restent non identifiés dans le binaire (UI/LoginScene.cpp:811-814
            // n'en nomme que 3 — a1[3..5] = ses boutons ok/exit/opt — qui appartiennent à
            // LoginScene, PAS à la scène EnterWorld). Ne pas inventer de champ fantôme.
            // TODO [ancre 0x52C055] : identifier le tampon 150 dw de cSceneMgr +12.

            // (5) Snd_ReleaseBuffers(this + 153) @0x52C089 : `add ecx, 264h` -> slot
            // +0x264 = +612 = LE SLOT BGM DE SCÈNE, exactement celui de
            // SceneMgr_ReleaseSoundBuffers 0x517B60 (`return Snd_ReleaseBuffers(this + 153);`),
            // déjà réifié ici par bgm_ / ReleaseBgm(). Le binaire coupe le son AVANT de
            // spouler la zone (case 1).
            // RECTIFICATION du commentaire qui occupait ce bloc : il annonçait comme « reste
            // TODO » le release de la BANQUE DE SONS DE MONDE (WSndMgr_Free 0x4DB060) ; c'est
            // une confusion de slots — 0x52C089 vise +612 (bgm_), et WSndMgr_Free porte sur un
            // slot DISTINCT tenu par g_GameWorld, qui n'est pas appelé à cette EA.
            ReleaseBgm();                                                  // 0x52C089 / 0x6A80D0

            // ÉCART CONNU (hors mission, non introduit ici) : dans le binaire, la case 1
            // rechargera le BGM via World_LoadZoneResource 0x4DCB60 case 12 (~120 frames plus
            // tard). Côté C++, host.LoadZoneResource(idx=12) ne charge qu'un SoundBuffer
            // throwaway côté WorldAssets ; le vrai rechargement n'a lieu qu'à l'entrée InGame
            // (Change() -> LoadZoneBgm). Le silence dure donc un peu plus longtemps que dans
            // l'original, sans autre conséquence.
            // TODO [ancre 0x4DCB60 case 12] : faire appeler LoadZoneBgm(zoneId) par
            // host.LoadZoneResource quand idx==12 pour aligner exactement la fenêtre de silence.
        };
        host.LoadZoneResource = [this](int zoneId, int idx) {
            // World_LoadZoneResource 0x4DCB60 : idx EST directement world::ResourceKind
            // (cf. EnterWorldFlow.h, audit idx∈[1,12] réel, 0/[13,19] no-op fidèles —
            // le switch `default` d'origine ne fait rien pour ces valeurs).
            // worldMap_ est désormais construit UNE FOIS dans Init() (cf. plus haut),
            // PAS paresseusement à l'entrée InGame comme avant ce câblage, précisément
            // pour être déjà disponible ici (EnterWorld précède InGame dans le flux).
            if (!worldMap_) return; // gameDataDir_ vide à Init() — dégradation déjà journalisée
            if (idx < 1 || idx > 12) return; // no-op fidèle (switch `default` d'origine)
            const auto kind = static_cast<world::ResourceKind>(idx);
            const unsigned char ok = worldMap_->LoadZoneResource(zoneId, kind);
            if (!ok) {
                TS2_WARN("EnterWorld: World_LoadZoneResource(kind=%d, zone=%d) a echoue.", idx, zoneId);
            }
        };
        host.SendEnterWorldRequest = [this] {
            // Net_SendPacket_Op12 0x4B43C0 (opcode 12, 222 o) : bloc1 128 o (compte),
            // bloc2 13 o (nom du personnage), bloc3 72 o = record de spawn/téléport
            // confirmé (Net/CharSelectPackets.h::BuildEnterWorldTail72).
            uint8_t name13[13] = {};
            {
                const std::string& nm = game::g_World.self.localPlayerName;
                const size_t n = nm.size() < sizeof(name13) ? nm.size() : sizeof(name13);
                std::memcpy(name13, nm.data(), n);
            }
            uint8_t tail72[72] = {};
            net::BuildEnterWorldTail72(game::g_World.self.spawnX,
                                       game::g_World.self.spawnY,
                                       game::g_World.self.spawnZ,
                                       game::g_World.self.spawnRotationDeg,
                                       tail72);
            net::Net_SendPacket_Op12(net_->Client(), net::g_AccountName, name13, tail72);
            // NetSend() interne est fire-and-forget (void, cf. tous les autres builders
            // Net_Send* de ce fichier) : "true" est une fidélité partielle assumée, comme
            // host.SendKeepAlive plus bas dans le case Scene::InGame.
            return true;
        };
        host.ShowErrorNotice = [](int strId) {
            // UI_NoticeDlg_Open(byte_18225C8, 2, StrTable005_Get(g_LangId, strId), &String) :
            // strId 67 = echec emission Op12 (0x52C1A2), 68 = timeout ACK serveur (0x52C213).
            // Meme modele que host.ShowSpawnTimeoutNotice (InGame). // 0x5C0280
            game::g_Client.prompt.Open(2, game::Str(strId));
        };
        const int zoneId = game::g_World.zoneId;
        if (!game::EnterWorldFlow_Update(enterWorldState_, host, zoneId)) {
            // Etat Failed (timeout ACK 5000f @0x52C203 ou echec emission @0x52C194) : la notice
            // Str 67/68 a deja ete emise par host.ShowErrorNotice. NE PAS forcer InGame : le
            // binaire reste en scene 5 / etat 4 (default 0x52C232 = no-op). Le seul chemin
            // legitime vers InGame reste sceneEnterWorldPending (op 0x0c) teste en tete de ce case.
        }
        // Miroir du champ +4 de cSceneMgr pendant la scène EnterWorld : dans le binaire,
        // g_SceneSubState est LE MÊME champ (*(this+1)) pour Scene_EnterWorldUpdate 0x52BFF0
        // et Scene_InGameUpdate 0x52C600 — il porte donc ici l'état EnterWorld (0..4).
        // Non requis par la garde @0x50B7EC (elle court-circuite sur g_SceneMgr != 6), mais
        // maintenu pour que le miroir reste exact quelle que soit la scène. // 0x1676184
        g_SceneSubState = static_cast<int>(enterWorldState_.state);
        break;
    }
    case Scene::InGame: {
        // cSceneMgr_Update 0x517BF0 (case 6) : Scene_InGameUpdate() PUIS
        // AutoPlay_Update(g_AutoPlayBot) — dans cet ORDRE, à chaque frame InGame
        // (confirmé décompilation directe). Scene_InGameUpdate = InGameTickFlow_Update
        // (Game/InGameTickFlow.h, câblé ci-dessous) ; AutoPlay reste juste après, inchangé.
        //
        // Hooks câblés sur du code EXISTANT (réels, pas des TODO) :
        //   SendKeepAlive/SendPendingTargetPoll -> Net_SendPacket_Op13/Net_SendOp64 (Net/SendPackets.h)
        //   AppendKeepAliveFailedMessage    -> game::g_Client.msg.System(Str(70))
        //   ShowSpawnTimeoutNotice          -> game::g_Client.prompt.Open(2, Str(71)) (UI_NoticeDlg_Open)
        //   GetSelfActionState              -> g_World.players[0].body @kSelfActionStateOffset (Game/MapWarp.h)
        //   IsGm                            -> net::g_GmAuthLevel != 0 (Net/NetClient.h)
        //   IsExchangeWindowOpen            -> windows_->PlayerTrade().IsOpen() (Dialog::IsOpen)
        //   CanAutoInteractNpc/IsInventoryDirty/IsMorphInProgress
        //                                    -> windows_->AutoPlaySys().externalState.*
        // Hooks câblés sur les 4 systèmes Game/AnimationTick.h, Game/EntityLifecycleTick.h,
        // Game/CameraWarpTick.h, Game/ComboPickupTick.h (mission de câblage 2026-07-14, cf.
        // commentaires locaux à chaque hook ci-dessous pour le détail et les écarts/TODO
        // documentés) : TickWarpSuppressionTimeout, AutoUsePotion, UpdateLocalPlayerAnim,
        // UpdateEntityAnimFrame, DespawnStalePlayer, UpdateMonster/RespawnMonsterAfterKnockback,
        // TickNpcEffect/CleanupStaleNpcEffect, AutoInteractNpcForPet, UpdateQuestMarkerTimer,
        // FindComboFollowupTarget/BeginComboMorph, TickNearbyPickupSlots, RotateTipText.
        // Hooks câblés sur les 3 systèmes supplémentaires Game/GroundAuraWorldObjectTick.h,
        // Game/AutoTargetCombatGate.h, Gfx/CameraThirdPersonBridge.h (2e vague de câblage,
        // même date) : TickGroundItemEffect, GetFxAuraCount/IsFxAuraActive/
        // UpdateHomingProjectile, GetWorldObjectCount/IsWorldObjectActive/TickWorldObject,
        // ValidateAutoTarget, IsCombatAllowedOnMap ; InitCamera/UpdateCameraCollision
        // passent désormais par gfx::TickThirdPersonCamera (appelée juste après
        // InGameTickFlow_Update ci-dessous, hors host — cf. son commentaire local) grâce à
        // Update(dt, camera) qui reçoit enfin une gfx::Camera MUTABLE (SceneManager.h/
        // App.cpp étendus par cette même mission).
        // Reste TODO (aucune donnée/instance disponible dans ClientSource pour la nourrir) :
        // UpdateMapObjectAnim (aucun objet de collision de map animé modélisé).
        game::InGameTickFlowHost host;

        // --- Setup (case 0), one-shot ---------------------------------------------------
        // Scene_InGameUpdate 0x52C600 case 0 (jumptable @0x52C61F -> loc_52C626) : quadruplet
        // prouvé instruction par instruction, dans cet ordre exact.
        // ÉCART PROUVÉ vs Scene_EnterWorldUpdate 0x52BFF0 case 0 : la case 0 InGame ne contient
        // NI UI_ResetAllDialogs (@0x52C038, propre à EnterWorld) NI Snd_ReleaseBuffers(this+153)
        // (@0x52C089, idem). Ne PAS les ajouter ici : l'entrée en jeu ne coupe pas le BGM et ne
        // rabat pas les dialogues.
        host.ResetUiAndScratch = [this] {
            // (1) Gfx_ApplyOverlayBlendMode_SetState() @0x52C62B — NON BRANCHÉ.
            // L'original (0x53F630) fait exactement deux choses :
            //   Gfx_SetTextureBlendMode(g_GfxRenderer, 3, dword_7FFF78, 2) @0x53F646
            //   dword_8002C8 = 3 @0x53F64B  (état global de mode de blend d'overlay)
            // et 0x69DCA0 écrit les champs renderer +331/+332/+333 (+332 clampé sur +88) puis
            // enchaîne 4x SetTextureStageState (vtbl+276, stage 0, états 5/6/10/7).
            // NOTE (relevée cette mission) : le `mov ecx, offset unk_1685740` @0x52C626 qui
            // précède l'appel est DEAD — 0x53F630 est sans paramètre et n'utilise pas ecx (il
            // travaille sur g_GfxRenderer 0x7FFE18). Le commentaire Game/InGameTickFlow.h:128
            // (« sub_53F630(&unk_1685740) ») prête donc à cette fonction un argument qui
            // n'existe pas — fichier hors de ce front, signalé seulement.
            // Rien à appeler ici : aucun équivalent réifié dans ClientSource (ni dword_8002C8,
            // ni les champs renderer +331..+333), et Gfx/Renderer.h n'est pas possédé par ce
            // front. Ne pas confondre avec MeshRenderer::applyBlendMode (0x69DCA0 est un
            // chemin d'état d'overlay distinct, pas le blend par mesh).
            // TODO [ancre 0x52C62B / 0x53F630 / 0x69DCA0] : réifier l'état de blend d'overlay
            // (dword_8002C8 + renderer +331..+333) puis l'appeler ICI.

            // (2) Util_SetClampedU8Field(&dword_8E714C, 0) @0x52C637 — NON BRANCHÉ.
            // Strictement le même appel qu'@0x52C044 (case 0 d'EnterWorld) : voir la chaîne de
            // preuve complète plus haut dans ce fichier (dword_8E714C EST mPOINTER, le jeu de
            // curseurs ; 0x4C1110 = `if (a2 <= 8) *this = a2;` soit 9 slots) -> « remettre la
            // forme du curseur souris au slot 0 ». L'équivalent fidèle existe
            // (game::CursorSet::SetActiveSlot(0), Game/MiscManagers.cpp:88) mais l'instance est
            // App::cursors_, membre PRIVÉ de App (App/App.h:43), fichier non possédé -> même
            // arbitrage qu'@0x52C044.
            // TODO [ancre 0x52C637 / 0x4C1110] : exposer App::cursors_ (décision orchestrateur)
            // puis appeler CursorSet::SetActiveSlot(0) ICI.

            // (3) UI_FocusEditBox(&g_UIEditBoxMgr, 0) @0x52C643 — CÂBLÉ.
            // Appel STRICTEMENT identique à @0x52C050 (case 0 d'EnterWorld) : même fonction,
            // même a2 = 0 (`push 0` @0x52C63E). Re-vérifié cette mission sur 0x50F4A0 : sous
            // `if (a2 < 0x16)`, `*this = 0` @0x50F4BB puis, la branche `if (*this)` étant
            // fausse, `SetFocus(hWndParent)` @0x50F4CB. On réutilise donc le motif déjà prouvé
            // et câblé plus haut (index de focus -> Chat().Unfocus() ; SetFocus(hWndParent) ->
            // notifyHwnd_, qui EST hWndParent 0x815184).
            if (hud_) hud_->Chat().Unfocus();                              // 0x52C643 / 0x50F4BB
            if (notifyHwnd_) ::SetFocus(static_cast<HWND>(notifyHwnd_));   // 0x50F4CB

            // (4) scratch 150 dw @0x52C648 — NON MODÉLISÉ, donc RIEN à remettre à zéro.
            // `for (i=0; i<150; ++i) *(this+i+3) = 0;` (`cmp [ebp+var_8], 96h` @0x52C65A ;
            // `mov dword ptr [edx+ecx*4+0Ch], 0` @0x52C669), soit +0xC..+0x260 de cSceneMgr —
            // exactement le même tampon qu'@0x52C055 côté EnterWorld. SceneManager.h:56 le
            // documente mais aucun membre ne le réifie, et ces 150 dwords restent non
            // identifiés dans le binaire. Ne pas inventer de champ fantôme.
            // TODO [ancre 0x52C648] : identifier le tampon 150 dw de cSceneMgr +12.
        };

        // --- WaitFirstSpawn (case 1), timeout 5000 frames --------------------------------
        host.ShowSpawnTimeoutNotice = [] {
            // UI_NoticeDlg_Open(2, StrTable005_Get(g_LangId,71), "") 0x5C0280 — réel via
            // ClientRuntime::PromptState (même modèle que les autres prompts modaux).
            game::g_Client.prompt.Open(2, game::Str(71));
        };

        // --- InitCamera (case 3), one-shot -------------------------------------------------
        // Cam_SetLookAt/Camera_SetEyeTarget désormais câblés RÉELLEMENT via
        // gfx::TickThirdPersonCamera (Gfx/CameraThirdPersonBridge.h), appelée UNE FOIS par
        // frame juste après InGameTickFlow_Update ci-dessous (hors host : elle gère elle-même
        // le cadrage one-shot ET le suivi/collision chaque frame à partir du même flag
        // `justEnteredInGame`, cf. plus bas) — ce hook reste no-op pour éviter un double appel.
        host.InitCamera = [](float, float, float) {};

        // --- MainTick étape 1 : keepalive /300 frames + poll requête clan/faction ---------
        host.SendKeepAlive = [this]() -> bool {
            // Net_SendPacket_Op13(client, g_LocalElement) 0x4B4570. NetSend() intérieur est
            // best-effort (fire-and-forget) : les builders Net_Send* ne remontent PAS le
            // succès d'émission (void), donc "true" ici est une fidélité partielle assumée
            // (TODO : faire remonter le bool NetSend jusqu'ici si un jour nécessaire).
            net::Net_SendPacket_Op13(net_->Client(), static_cast<int8_t>(net::g_LocalElement));
            return true;
        };
        host.AppendKeepAliveFailedMessage = [] {
            game::g_Client.msg.System(game::Str(70)); // StrTable005 id 70
        };
        host.HasPendingTargetRequest = [] {
            const auto readReqName = [](uint32_t addr) {
                const auto& blob = game::g_Client.Blob(addr, 13);
                size_t len = 0;
                while (len < blob.size() && blob[len] != 0) ++len;
                return std::string(reinterpret_cast<const char*>(blob.data()), len);
            };
            return game::HasPendingTargetRequest(readReqName(0x167468A), readReqName(0x1674697));
        };
        host.SendPendingTargetPoll = [this] {
            net::Net_SendOp64(net_->Client()); // 0x4B9B20 — poll de requête clan/faction.
        };

        // --- MainTick étape 2 : timeout 10 s du flag "warp supprimé" ----------------------
        // Warp_TickSuppressionTimeout (Game/CameraWarpTick.h). Le hook reçoit directement
        // g_GameTimeSec depuis RunMainTick, puis synchronise ce latch avec
        // AutoPlayExternalState::warpSuppressed (MÊME global dword_1675B00, cf. tête de
        // Game/CameraWarpTick.h) : le site d'ARMEMENT réel (dword_1675B00=1) reste ailleurs
        // dans AutoPlaySystem (hors périmètre de ce câblage) — capturé ici au vol dès qu'il
        // est détecté, pour que l'auto-clear à 10 s (0x52C91F) reste fidèle.
        static game::WarpSuppressionState s_warpSuppression;
        host.TickWarpSuppressionTimeout = [this](float /*dt*/) {
            const float gameTimeSec = game::g_World.gameTimeSec;
            if (windowsReady_ && windows_) {
                auto& ext = windows_->AutoPlaySys().externalState;
                if (ext.warpSuppressed && !s_warpSuppression.suppressed) {
                    game::Warp_SetSuppressed(s_warpSuppression, gameTimeSec);
                }
                game::Warp_TickSuppressionTimeout(s_warpSuppression, gameTimeSec);
                ext.warpSuppressed = s_warpSuppression.suppressed; // repropage l'auto-clear
            } else {
                game::Warp_TickSuppressionTimeout(s_warpSuppression, gameTimeSec);
            }
        };

        // --- MainTick étapes 3-5 : anim/collision inconditionnelles chaque frame ----------
        // Game_AutoUsePotion (Game/CameraWarpTick.h). Câblage RÉEL des jauges/seuils/état
        // d'action (déjà modélisés dans GameState.h) ; la ceinture auto-play (3x14) et les
        // réglages de seuil UI n'existent encore nulle part dans ClientSource -> hooks
        // laissés nuls (dégradation propre : IsAutoPotionSystemEnabled==false par défaut,
        // donc la fonction ne consomme jamais de potion tant qu'un futur InventorySystem/
        // réglage AutoPlay ne les branche pas — cf. AutoPotionHost dans le header pour
        // chaque EA d'origine manquante).
        host.AutoUsePotion = [this](float /*dt*/) {
            game::AutoPotionHost potionHost;
            potionHost.GetHpGauge = [] { return static_cast<float>(game::g_World.self.hp); };
            potionHost.GetMpGauge = [] { return static_cast<float>(game::g_World.self.mp); };
            // ÉCART DOCUMENTÉ (cf. Game/CameraWarpTick.h::AutoPotionHost) : le binaire compare
            // bien HP/MP aux agrégats Char_CalcAttackRatingMin/Max, PAS à une capacité max
            // HP/MP — reproduit tel quel par fidélité.
            potionHost.GetHpThresholdMetric = [] { return static_cast<float>(game::g_World.self.atkRatingMin); };
            potionHost.GetMpThresholdMetric = [] { return static_cast<float>(game::g_World.self.atkRatingMax); };
            potionHost.GetSelfActionState = [] {
                if (game::g_World.players.empty()) return 0;
                const game::PlayerEntity& self0 = game::g_World.players[0];
                int32_t raw = 0;
                if (self0.body.size() >= game::kSelfActionStateOffset + sizeof(raw)) {
                    std::memcpy(&raw, self0.body.data() + game::kSelfActionStateOffset, sizeof(raw));
                }
                return static_cast<int>(raw);
            };
            if (windowsReady_ && windows_) {
                potionHost.IsMorphInProgress = [this] { return windows_->AutoPlaySys().externalState.morphInProgress; };
            }
            game::Game_AutoUsePotion(potionHost);
        };
        // MapColl_UpdateObjectAnim (Game/AnimationTick.h) : le "this" d'origine (objet de
        // collision de zone animé, MapCollisionObjectAnimState) n'a AUCUNE instance dans
        // ClientSource à ce jour — ni World/WorldMap.h ni Gfx/WorldGeometryRenderer.h
        // n'exposent de tableau de sous-objets/particules par objet de map. Câblage réel
        // impossible sans étendre ce système (hors périmètre de ce câblage) : TODO conservé.
        host.UpdateMapObjectAnim = [](float) {
            TS2_INGAME_TODO_ONCE("InGame: MapColl_UpdateObjectAnim non branche - aucune instance "
                                   "de MapCollisionObjectAnimState nulle part dans ClientSource "
                                   "(World/WorldMap.h n'expose pas d'objets de collision animes) "
                                   "(TODO EA 0x694A00).");
        };
        // Player_UpdateLocalAnim (Game/AnimationTick.h) : opère sur game::g_World (self),
        // reconstruit les ~80 timers de morph aux adresses d'origine via g_Client.Var/VarF.
        // LoadCurrentZoneModel câblé sur world::WorldMap::LoadCurrentZoneModel (déjà écrit,
        // instance possédée par SceneManager) ; IsPointOnGround/musique d'ambiance laissés
        // nuls (terrain/audio hors périmètre de ce câblage).
        host.UpdateLocalPlayerAnim = [this](float dt) {
            game::LocalAnimTickHost localHost;
            localHost.LoadCurrentZoneModel = [this](int reason) {
                if (worldMap_) worldMap_->LoadCurrentZoneModel(reason);
            };
            game::Player_UpdateLocalAnim(game::g_World, dt, nullptr, localHost);
        };
        // Char_UpdateAnimationFrame (Game/AnimationTick.h) : appelé pour l'entité 0 (soi,
        // étape 5) ET pour chaque joueur distant (étape 6, cf. Game/InGameTickFlow.cpp) via
        // le MÊME hook. isSelf/isLocalSimulation = (idx==0). GetPendingStopRequest/
        // ClearPendingStopRequest câblés sur g_PendingStopRequest (0xE0000072, MÊME variable
        // que Net/GameHandlers_Misc.cpp::kPendingStopReq) ; SendAutoPlayStopAck câblé sur
        // Net_SendOp95(pos_self, 2) (déjà déclaré, Net/SendPackets.h). Contact/interruption
        // de cast délégués à ActionFsm (déjà écrit) ; le switch terminal 55-handlers
        // (asset-driven, 0x5727BF) reste hors périmètre (stateHandler nul -> FSM gelée sur
        // son état courant au-delà de contact/interrupt/FX/rotation). Si un coup/compétence
        // instantané est validé pour SOI (contactFiredThisTick), le résultat est sérialisé et
        // envoyé via Net_SendPacket_Op18 (76o), complétant fidèlement Combat_QueueMeleeAttack/
        // Combat_QueueSkillAction (Game/CombatSystem.h, déjà écrit — réutilisé, pas dupliqué).
        host.UpdateEntityAnimFrame = [this](int idx, float dt) {
            if (idx < 0 || static_cast<size_t>(idx) >= game::g_World.players.size()) return;
            game::PlayerEntity& p = game::g_World.players[static_cast<size_t>(idx)];
            if (!p.active) return;
            const bool isSelf = (idx == 0);

            game::CombatActorState actor;
            actor.selfId = p.id;
            actor.x = p.x; actor.y = p.y; actor.z = p.z;
            actor.facing = p.anim.state; // entity+244, même offset que CharAnimState::state

            game::CharAnimTickHost animHost;
            animHost.GetPendingStopRequest = [] { return game::g_Client.Var(0xE0000072u) != 0; };
            animHost.ClearPendingStopRequest = [] { game::g_Client.Var(0xE0000072u) = 0; };
            animHost.SendAutoPlayStopAck = [this] {
                const game::PlayerEntity& self = game::g_World.Self();
                float pos[3] = { self.x, self.y, self.z };
                net::Net_SendOp95(net_->Client(), pos, 2);
            };

            game::CharAnimTickResult result;
            game::Char_UpdateAnimationFrame(p.anim, actor, game::g_World, nullptr,
                                             isSelf, isSelf,
                                             false /* pendingCastInterrupt: TODO g_AutoHuntFuelA/B non traces */,
                                             dt, nullptr, nullptr, animHost, result);

            if (isSelf && result.contactFiredThisTick) {
                uint8_t payload[76] = {};
                result.lastAction.Serialize(payload);
                net::Net_SendPacket_Op18(net_->Client(), payload);
            }
        };
        // Camera_UpdateCollision (Game/AnimationTick.h) : câblée RÉELLEMENT via
        // gfx::TickThirdPersonCamera, MÊME appel unique que host.InitCamera ci-dessus (cf. son
        // commentaire) — ce hook reste no-op pour éviter un double appel de
        // Camera_UpdateCollision sur la même frame.
        host.UpdateCameraCollision = [] {};

        // --- MainTick étape 6 : joueurs distants, péremption 7,5 s ------------------------
        // sub_55D720 (Game/EntityLifecycleTick.h) : désactive le slot périmé.
        host.DespawnStalePlayer = [](int idx, float) {
            game::DespawnStalePlayer(game::g_World, idx);
        };

        // --- MainTick étape 7 : tableau 88 o (GroundItem au sens GameState.h) -------------
        // Fx_MeleeSwingTick 0x5803A0 (Game/GroundAuraWorldObjectTick.h). GetWeaponEffectFrameCount
        // (Model_GetWeaponEffectFrameCount 0x4E5A40) laissé nul : aucune table de modèles/assets
        // d'effet d'arme câblée côté ClientSource à ce jour -> dégradation propre documentée
        // (le timer de frame avance mais ne boucle/complète jamais, cf. commentaire du header).
        host.TickGroundItemEffect = [](int index, float dt) {
            static const game::GroundAuraWorldObjectTickHost s_groundFxHost{}; // GetWeaponEffectFrameCount nul
            game::TickGroundItemEffect(game::g_World, index, dt, s_groundFxHost);
        };

        // --- MainTick étape 8 : monstres, péremption 7,5 s --------------------------------
        // Char_Update / sub_580550 (Game/EntityLifecycleTick.h). EntityLifecycleTickHost
        // partagé avec l'étape 9 ci-dessous ; sous-hooks hors périmètre (tables de fenêtre de
        // coup, envoi réseau melee/projectile, dispatch FSM, hauteur de sol, son d'impact —
        // cf. tête de Game/EntityLifecycleTick.h) laissés nuls : dégradation propre (le
        // monstre tick sans jamais frapper/tomber tant qu'un futur système combat/FX ne les
        // branche pas).
        static game::EntityLifecycleTickHost s_lifecycleHost;
        host.UpdateMonster = [](int idx, float dt) {
            game::UpdateMonster(game::g_World, idx, dt, s_lifecycleHost);
        };
        host.RespawnMonsterAfterKnockback = [](int idx) {
            game::RespawnMonsterAfterKnockback(game::g_World, idx);
        };

        // --- MainTick étape 9 : tableau 152 o (NpcEntity au sens GameState.h) ------------
        // Fx_GibUpdate / sub_583390 (Game/EntityLifecycleTick.h), même host que ci-dessus.
        host.TickNpcEffect = [](int idx, float dt) {
            game::TickNpcEffect(game::g_World, idx, dt, s_lifecycleHost);
        };
        host.CleanupStaleNpcEffect = [](int idx) {
            game::CleanupStaleNpcEntity(game::g_World, idx);
        };

        // --- MainTick étape 10 : auras/homing (PAS dans GameState.h) ----------------------
        // g_FxAuraCount/dword_17D06F4 (Game/GroundAuraWorldObjectTick.h) : pool SoA de
        // projectiles d'attaque non modélisé côté ClientSource (cf. commentaire du header) ;
        // GetFxAuraCount lit le vrai slot via g_Client.Var (0 aujourd'hui, personne ne le
        // peuple encore) -> IsFxAuraActive/UpdateHomingProjectile jamais atteints en pratique,
        // dégradation propre et sûre (pas un stub figé, câblage réel malgré tout).
        host.GetFxAuraCount = [] { return game::GetFxAuraCount(); };
        host.IsFxAuraActive = [](int index) { return game::IsFxAuraActive(index); };
        host.UpdateHomingProjectile = [](int index, float dt) { game::UpdateHomingProjectile(index, dt); };

        // --- MainTick étape 11 : objets de monde (PAS dans GameState.h) -------------------
        // dword_1687230/dword_180EEF4 (Game/GroundAuraWorldObjectTick.h) : câblés sur
        // game::g_World.zoneObjects (déjà dimensionné à 500 par GameData_InitPools). TickWorldObject
        // (sub_584170) est un stub __stdcall VIDE confirmé par décompilation -> ne fait rien,
        // reproduit fidèlement (pas une supposition).
        host.GetWorldObjectCount = [] { return game::GetWorldObjectCount(game::g_World); };
        host.IsWorldObjectActive = [](int index) { return game::IsWorldObjectActive(game::g_World, index); };
        host.TickWorldObject = [](float dt) { game::TickWorldObject(dt); };

        // --- MainTick étape 12, porte de gating -------------------------------------------
        host.GetSelfActionState = [] {
            // g_SelfActionState[0] == g_World.players[0].body @+220 (== entity+244, cf.
            // Game/MapWarp.h::kSelfActionStateOffset, dérivation vérifiée depuis g_LocalPlayerSheet).
            if (game::g_World.players.empty()) return 0;
            const game::PlayerEntity& self0 = game::g_World.players[0];
            int32_t raw = 0;
            if (self0.body.size() >= game::kSelfActionStateOffset + sizeof(raw)) {
                std::memcpy(&raw, self0.body.data() + game::kSelfActionStateOffset, sizeof(raw));
            }
            return static_cast<int>(raw);
        };
        if (windowsReady_ && windows_) {
            host.IsExchangeWindowOpen = [this] { return windows_->PlayerTrade().IsOpen(); };
            // sub_53B9E0 : AutoPlayExternalState::sceneTransitionBlocking stocke déjà la
            // valeur BRUTE de sub_53B9E0 (cf. Game/AutoPlaySystem.h) — ce hook veut "true
            // quand sub_53B9E0 renvoie vrai", donc AUCUNE inversion ici (cf. avertissement
            // explicite dans Game/InGameTickFlow.h).
            host.CanAutoInteractNpc = [this] { return windows_->AutoPlaySys().externalState.sceneTransitionBlocking; };
            host.IsInventoryDirty   = [this] { return windows_->AutoPlaySys().externalState.invDirtyEnable; };
            host.IsMorphInProgress  = [this] { return windows_->AutoPlaySys().externalState.morphInProgress; };
        } else {
            host.IsExchangeWindowOpen = [] { return false; };
            host.CanAutoInteractNpc   = [] { return false; };
            host.IsInventoryDirty     = [] { return false; };
            host.IsMorphInProgress    = [] { return false; };
        }
        // Npc_AutoInteractForPet 0x53B5F0 : DÉJÀ porté par NpcInteractionSystem::
        // AutoInteractForPet() (Game/NpcInteraction.h, réutilisé tel quel, cf. rapport de
        // mission combo_pickup_quest). `selectedItemId` (g_SelectedInvItemId 0x1673258) n'est
        // tracé nulle part dans ClientSource à ce jour -> 0 fixe (garde interne
        // `if (selectedItemId < 1) return;` -> no-op fidèle et sûr, TODO le vrai câblage
        // le jour où la sélection d'item d'inventaire existe côté UI).
        static game::NpcInteractionSystem s_npcInteract;
        host.AutoInteractNpcForPet = [this] {
            if (windowsReady_ && windows_) {
                s_npcInteract.morphInProgress = windows_->AutoPlaySys().externalState.morphInProgress;
            }
            s_npcInteract.AutoInteractForPet(0 /* TODO g_SelectedInvItemId non trace */,
                                              game::g_World.gameTimeSec);
        };

        // --- MainTick étape 12a ------------------------------------------------------------
        // ValidateAutoTarget (Game/AutoTargetCombatGate.h) : câblé sur game::g_World, oracle de
        // portée par défaut (rangedLookup nul -> AutoTarget_DefaultRangeLookup) : mode 7 =
        // g_World.zoneObjects ; mode 4 = g_World.groundItems (même tableau que
        // g_NpcRenderArray/dword_1764D14, cf. commentaire du header AutoTargetCombatGate.h) —
        // les deux modes résolvent désormais une vraie position, plus de repli systématique.
        host.ValidateAutoTarget = [] { game::ValidateAutoTarget(game::g_World); };

        // --- MainTick étape 12b ------------------------------------------------------------
        // Quest_UpdateMarkerTimer (Game/ComboPickupTick.h), réutilise game::g_QuestProgress
        // (déjà porté, Game/QuestSystem.h). isArenaZone (Map_IsArenaZone 0x54B690) non
        // modélisé -> false fixe (TODO) ; fenêtre entrepôt/son de marqueur laissés nuls (UI/
        // audio hors périmètre de ce câblage).
        static game::QuestMarkerState s_questMarker;
        host.UpdateQuestMarkerTimer = [] {
            game::Quest_UpdateMarkerTimer(s_questMarker, game::g_QuestProgress,
                                           game::g_World.gameTimeSec,
                                           false /* isArenaZone: TODO Map_IsArenaZone non modelise */,
                                           nullptr, nullptr);
        };

        // --- MainTick étape 12c -------------------------------------------------------------
        // Combo_FindNearbyFollowup (Game/ComboPickupTick.h) : la table GINFO2 (candidats de
        // suivi + Combo_CheckTransition) n'est modélisée nulle part dans ClientSource ->
        // candidates/transitionCheck nuls (résultat -1 fidèle, garde EA 0x501286). motionId
        // (g_SelfMorphNpcId) n'est pas non plus tracé dans GameState.h -> 0 fixe (TODO),
        // renvoie -1 immédiatement (hors bornes [1,350]).
        host.FindComboFollowupTarget = [] {
            const game::PlayerEntity& self = game::g_World.Self();
            const int motionId = 0; // TODO : g_SelfMorphNpcId non trace dans GameState.h
            return game::Combo_FindNearbyFollowup(motionId, self.x, self.y, self.z, nullptr, nullptr);
        };
        // host.IsMorphInProgress est DÉJÀ câblé réellement plus haut (porte de gating étape 12,
        // même champ réutilisé ici par le binaire pour la gate combo étape 12c) — pas de
        // réaffectation ici.
        // BeginComboMorph (Game/ComboPickupTick.h) : port fidèle complet (phase=4, reset des
        // 72 o, rotation aléatoire via net::DefaultRng(), Net_SendPacket_Op20). currentMotionId
        // (clé GInfo2_FindVec3ByKey) partage le même TODO que motionId ci-dessus -> 0 fixe,
        // originLookup GINFO2 nul (position d'origine résolue à {0,0,0}, fidèle). Ne
        // s'exécutera jamais tant que FindComboFollowupTarget renverra -1 ci-dessus.
        static game::ComboMorphState s_comboMorph;
        host.BeginComboMorph = [this](int followupTargetId) {
            const int currentMotionId = 0; // TODO : g_SelfMorphNpcId non trace
            game::BeginComboMorph(s_comboMorph, followupTargetId, currentMotionId,
                                   net_->Client(), nullptr);
            if (windowsReady_ && windows_) {
                windows_->AutoPlaySys().externalState.morphInProgress = s_comboMorph.inProgress;
            }
        };

        // --- MainTick étape 12d -------------------------------------------------------------
        // Combat_IsElementAllowedOnMap (Game/AutoTargetCombatGate.h::IsCombatAllowedOnMapForSelf,
        // wrapper direct, déjà porté par Game/ComboPickupTick.h) : câblé sur world.self.element
        // (g_LocalElement) + g_SelfMorphNpcId (g_Client.VarGet). ElementPairTable
        // (g_LocalPlayerSheet+455..458) reste non modélisée dans ClientSource -> repli {} (4x -1,
        // "aucune paire enregistrée", PAS un raccourci "toujours faux" : le résultat dépend
        // toujours réellement de selfMorphNpcId, cf. commentaire du header).
        host.IsCombatAllowedOnMap = [] { return game::IsCombatAllowedOnMapForSelf(game::g_World); };
        host.IsGm = [] { return net::g_GmAuthLevel != 0; };
        // TickNearbyPickupSlots (Game/ComboPickupTick.h) : les 5 emplacements flt_1676130 sont
        // déjà alimentés par le handler du paquet 0x82 (Net/GameHandlers_Misc.cpp) via
        // g_Client.VarF — ce câblage réutilise le MÊME stockage (aucune duplication). Ne
        // s'exécute que si IsCombatAllowedOnMap ci-dessus renvoie un jour true (fidèle).
        host.TickNearbyPickupSlots = [this] {
            const game::PlayerEntity& self = game::g_World.Self();
            game::TickNearbyPickupSlots(self.x, self.y, self.z, net_->Client());
        };

        // --- MainTick étape 12e -------------------------------------------------------------
        // Tips_RotateUpdate (Game/ComboPickupTick.h) : réutilise game::g_Strings.notices
        // (TipsTable déjà porté, Game/StringTables.h) — le timer/index (600 s) y est déjà
        // tenu fidèlement ; ce câblage n'ajoute que l'appel manquant à l'append chat.
        host.RotateTipText = [] {
            game::Tips_RotateUpdate(game::g_Strings.notices, game::g_World.gameTimeSec);
        };

        // Détection "entrée en InGame" pour gfx::TickThirdPersonCamera ci-dessous : capturée
        // AVANT InGameTickFlow_Update, car l'état de la machine (inGameTickState_) transite
        // DURANT cet appel (InitCamera -> MainTick, one-shot, cf. Game/InGameTickFlow.cpp) —
        // c'est exactement la même frame où le binaire d'origine exécute son case 3
        // (Cam_SetLookAt/Camera_SetEyeTarget, EA 0x52C6EF).
        const bool justEnteredInGame = (inGameTickState_.state == game::InGameTickState::InitCamera);

        game::InGameTickFlow_Update(inGameTickState_, host, static_cast<float>(dt));

        // Miroir du champ +4 de cSceneMgr (g_SceneSubState 0x1676184) pour la scène InGame :
        // dans le binaire, Scene_InGameUpdate 0x52C600 écrit ce champ LUI-MÊME au fil de ses
        // transitions (ex. sub=4 posé @0x52C7F1 en fin de case 3 InitCamera, sub=3 posé
        // directement par Pkt_SpawnCharacter @0x464901). inGameTickState_.state EN EST le
        // modèle 1:1 (game::InGameTickState, valeurs alignées sur les cases d'origine :
        // Setup=0/WaitFirstSpawn=1/Failed=2/InitCamera=3/MainTick=4) -> on le recopie juste
        // APRÈS l'appel, à l'instant où le binaire a fini d'écrire le champ pour cette frame.
        // Consommé par la garde de Camera_UpdateFromInput @0x50B7EC (App/PlayerInputController).
        // Ordonnancement : App::FrameTick appelle g_playerInput.Update (App.cpp:643) AVANT la
        // boucle scene_.Update (App.cpp:656) — exactement comme le binaire appelle
        // Camera_UpdateFromInput @0x462619 avant cSceneMgr_Update @0x46263B. Le contrôleur lit
        // donc le sous-état produit par la frame PRÉCÉDENTE : même décalage d'une frame que
        // l'original. // 0x1676184
        g_SceneSubState = static_cast<int>(inGameTickState_.state);

        // M2 — MapColl_UpdateObjectAnim 0x694A00, appelee 1x/frame depuis Scene_InGameUpdate
        // 0x52C600 @0x52c94b (xref UNIQUE confirmee IDA). Cote ClientSource, WorldGeometryRenderer::
        // TickWorldAnim est l'equivalent documente (WorldGeometryRenderer.h:271-277, kAnimFps=15.0) :
        // seul writer de wavePhase_ (matrice bump-env eau) + phase de flipbook de sway par instance
        // .WO. Jamais appele avant ce cablage -> eau/sway figes. Garde one-shot worldGeomReady_
        // (device chaud requis, meme garde que le rendu .WO). // 0x694A00
        if (worldGeomReady_ && worldGeom_)
            worldGeom_->TickWorldAnim(static_cast<float>(dt));

        // InGame_InitCamera (one-shot, si justEnteredInGame) + Camera_UpdateCollision (chaque
        // frame) : câblage RÉEL via gfx::TickThirdPersonCamera (Gfx/CameraThirdPersonBridge.h),
        // APRÈS la mise à jour de la position du joueur local pour cette frame (host.
        // UpdateEntityAnimFrame(0,...) déjà exécuté par InGameTickFlow_Update ci-dessus) —
        // remplace host.InitCamera/host.UpdateCameraCollision (laissés no-op plus haut).
        gfx::TickThirdPersonCamera(camera, game::g_World, static_cast<float>(dt), justEnteredInGame);

        // AutoPlay_Update(g_AutoPlayBot) — TOUJOURS après Scene_InGameUpdate, cf. commentaire
        // en tête de bloc et Game/InGameTickFlow.h (note d'intégration en bas de fichier).
        if (windowsReady_ && windows_) windows_->UpdateAutoPlay(static_cast<float>(dt));
        break;
    }
    default: break;
    }
}

void SceneManager::Render(IDirect3DDevice9* /*device*/, const gfx::Camera& camera) {
    switch (scene_) {
    case Scene::Intro:
        // Scene_IntroRender 0x518880 (UI/IntroRender.h) : logos défilés depuis les VRAIS
        // fichiers 001_00799..831.IMG (atlas UI), centrés sur leur taille réelle. Délégué à
        // LoginScene qui réutilise ses ressources GPU déjà créées (cf. LoginScene::RenderIntro) —
        // introState_ reste intégralement piloté ici (Update, cas Scene::Intro).
        if (login_) login_->RenderIntro(introState_);
        break;
    case Scene::ServerSelect:
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) login_->Render(scene_);
        break;
    case Scene::EnterWorld:
        // Scene_EnterWorldRender 0x52C260 (UI/EnterWorldRender.h) : écran de transition
        // CharSelect->InGame (fond de zone 008_%05d.IMG + barre de progression). Délégué à
        // LoginScene (mêmes ressources GPU que Scene::Intro ci-dessus). Sans ce case, la
        // scène EnterWorld tombait dans `default:` -> écran noir pendant tout le chargement.
        // enterWorldState_ reste piloté par SceneManager::Update() (case Scene::EnterWorld) ;
        // zoneId = game::g_World.zoneId (même valeur qu'EnterWorldFlow_Update).
        if (login_) login_->RenderEnterWorld(enterWorldState_, game::g_World.zoneId);
        break;
    case Scene::InGame:
        // ORDRE CORRIGÉ : la couche SilverLining minimale est appelée à deux moments,
        // comme le binaire d'origine :
        //   1) avant le décor/terrain (Env_UpdateFrame -> cAtmosphere_RenderFrame),
        //   2) après les entités (Env_StepTimeOfDay -> Atmosphere_DrawFrame).
        // Ici on l'applique autour du rendu monde: ciel -> décor .WO -> entités -> ciel
        // de fin -> HUD -> fenêtres.
        if (worldGeomReady_ && worldGeom_) worldGeom_->RenderSky(screenW_, screenH_);
        if (worldGeomReady_ && worldGeom_) worldGeom_->Render(camera, screenW_, screenH_);
        if (worldReady_ && world_) world_->Render(camera);
        if (worldGeomReady_ && worldGeom_) worldGeom_->RenderSky(screenW_, screenH_);
        if (hudReady_ && hud_) hud_->Render();
        if (windowsReady_ && windows_) windows_->Render();
        break;
    default:
        // None : écran effacé par Renderer::BeginFrame.
        break;
    }
}

void SceneManager::OnLButtonDown(int x, int y) {
    switch (scene_) {
    case Scene::ServerSelect:
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) { login_->OnMouseDown(scene_, x, y); ConsumePending(); }
        break;
    case Scene::InGame:
        // Les fenêtres (dialogues) interceptent le clic en premier (règle « premier
        // consommateur gagne » de UIManager) ; sinon il tombe vers le HUD.
        if (windowsReady_ && ui::UIManager::Instance().RouteMouseDown(x, y)) break;
        if (hudReady_ && hud_) hud_->OnMouseDown(x, y);
        break;
    default: break;
    }
}

void SceneManager::OnLButtonUp(int x, int y) {
    switch (scene_) {
    case Scene::ServerSelect:
    case Scene::Login:
    case Scene::CharSelect:
        if (login_) { login_->OnMouseUp(scene_, x, y); ConsumePending(); }
        break;
    case Scene::InGame:
        if (windowsReady_) ui::UIManager::Instance().RouteMouseUp(x, y);
        break;
    default: break;
    }
}

void SceneManager::OnChar(char c) {
    if (scene_ == Scene::Login && login_) { login_->OnChar(c); ConsumePending(); }
}

void SceneManager::OnKeyDown(int vk) {
    if ((scene_ == Scene::Login || scene_ == Scene::CharSelect) && login_) {
        login_->OnKeyDown(vk);
        ConsumePending();
    } else if (scene_ == Scene::InGame && windowsReady_ && windows_) {
        // Un dialogue OUVERT (Échap/Entrée...) intercepte avant les raccourcis
        // globaux d'ouverture (I/C/K/G/O/...), comme UI_RouteKeyInput d'origine.
        if (ui::UIManager::Instance().RouteKey(vk)) return;
        windows_->HandleHotkey(vk);
    }
}

void SceneManager::OnDeviceLost() {
    if (login_)    login_->OnDeviceLost();
    if (hud_)      hud_->OnDeviceLost();
    if (windows_)  windows_->OnDeviceLost();
    if (world_)    world_->OnDeviceLost();
    if (worldGeom_) worldGeom_->OnDeviceLost();
}

void SceneManager::OnDeviceReset() {
    if (login_)    login_->OnDeviceReset();
    if (hud_)      hud_->OnDeviceReset();
    if (windows_)  windows_->OnDeviceReset();
    if (world_)    world_->OnDeviceReset();
    if (worldGeom_) worldGeom_->OnDeviceReset();
}

} // namespace ts2

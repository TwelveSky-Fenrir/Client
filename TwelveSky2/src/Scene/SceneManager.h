// Scene/SceneManager.h — machine de scènes du client = cSceneMgr (objet dword_1676180).
// Dispatch cSceneMgr_Update 0x517BF0 / _Render 0x517CB0 sur l'id de scène.
// Voir Docs/TS2_CLIENT_SHELL.md §2.1.
//
// Détient les scènes shell (LoginScene = ServerSelect/Login/CharSelect) et le HUD
// en jeu (GameHud). Header léger : les scènes concrètes sont tenues par pointeur
// (unique_ptr) pour ne pas tirer winsock2/d3dx9 dans tous les incluants.
#pragma once
#include <memory>
#include <string>
#include "Game/IntroFlow.h"      // léger (STL uniquement) : IntroState détenu par valeur
#include "Game/EnterWorldFlow.h" // idem : EnterWorldFlowState détenu par valeur
#include "Game/InGameTickFlow.h" // idem : InGameTickFlowState détenu par valeur (Scene_InGameUpdate)

struct IDirect3DDevice9; // COM (global) — évite d'inclure d3d9.h ici

namespace ts2 {

// WorldRenderer (Scene/WorldRenderer.h) tire d3d9/d3dx9 (MeshRenderer/Font) : tenu
// par pointeur et forward-déclaré ici, comme login_/hud_/windows_, pour ne pas
// alourdir les incluants de ce header léger.
class WorldRenderer;

namespace gfx { class Renderer; class Camera; class WorldGeometryRenderer; }
namespace net { class NetSystem; }
namespace ui  { class LoginScene; class GameHud; class GameWindows; }
namespace world { class WorldAssets; class WorldMap; }
// Slot BGM de scène (Audio/AudioSystem.h) : forward-déclaré + tenu par pointeur pour
// ne pas tirer dsound.h dans ce header léger (comme login_/hud_/world_).
namespace audio { class BgmChannel; }

enum class Scene {
    None         = 0,
    Intro        = 1,  // Scene_IntroUpdate 0x517FE0 : splash INTRO.AVI + fondus des logos
    ServerSelect = 2,  // 0x518B30 : liste des serveurs (hardcodée ou via thread de statut)
    Login        = 3,  // Scene_LoginUpdate 0x51A8D0 : saisie identifiants + handshake
    CharSelect   = 4,  // sélection de personnage (UI_CharListWnd)
    EnterWorld   = 5,  // transition d'entrée en monde
    InGame       = 6,  // en jeu ; appelle aussi AutoPlay_Update
};

// Miroir du champ +4 de l'objet cSceneMgr 0x1676180, soit g_SceneSubState 0x1676184 =
// sous-état de la scène courante. Exposé en GLOBAL — et non en membre privé — parce que
// c'est ainsi que le binaire le consomme : Camera_UpdateFromInput 0x50B7D0 lit
// DIRECTEMENT les deux globals dans sa garde d'entrée @0x50B7EC
//   if ( g_SceneMgr != 6 || g_SceneSubState != 4 ) return;
// sans jamais recevoir le sous-état en paramètre (cf. App/PlayerInputController.cpp).
//
// ATTENTION (piège vérifié) : SceneManager::subState_ (privé, plus bas) NE porte PAS cette
// valeur en scène InGame — il n'est jamais écrit qu'à 0. Le sous-état InGame réel vit dans
// inGameTickState_ (game::InGameTickState, Game/InGameTickFlow.h) ; celui d'EnterWorld dans
// enterWorldState_. C'est CE global que SceneManager::Update tient à jour depuis ces deux
// automates (points de synchro commentés dans le .cpp). // 0x1676184
extern int g_SceneSubState;

// En-tête de l'objet cSceneMgr : [+0 id][+4 sous-état][+8 compteur de frames][+12 tampon 150 dw][+612 slot BGM].
class SceneManager {
public:
    SceneManager();
    ~SceneManager();
    SceneManager(const SceneManager&) = delete;
    SceneManager& operator=(const SceneManager&) = delete;

    // cSceneMgr_Init 0x517AF0 : crée LoginScene + HUD à partir du device/réseau.
    // renderer/net doivent être déjà initialisés (device créé, WSAStartup fait).
    // `gameDataDir` : racine GameData (App::gameDataDir_, cf. App::ResolveGameDataDir) —
    // nécessaire à world::WorldAssets pour charger Z%03d.WO (géométrie statique, cf.
    // Gfx/WorldGeometryRenderer.h).
    // `serverModeFlag` = GameConfig::buildVariant (1er jeton cmdline, g_ServerModeFlag
    // dword_166918C) : pilote le mode ServerSelect (0 = SingleServer 8088). Transmis à
    // LoginScene::Init (BuildServerList conditionnel). Défauté à 0 (lancement documenté).
    void  Init(gfx::Renderer& renderer, net::NetSystem& net, void* notifyHwnd,
               int screenW, int screenH, const std::string& gameDataDir,
               int serverModeFlag = 0);
    void  Shutdown();

    void  StartIntro();              // cSceneMgr_StartIntro 0x517B80
    // cSceneMgr_Update 0x517BF0. `camera` : caméra 3e personne de App (gfx::Camera), reçue
    // ICI en MUTABLE (contrairement à Render() qui la reçoit en const) — nécessaire au
    // câblage RÉEL de InGame_InitCamera/Camera_UpdateCollision (case Scene::InGame, cf.
    // Gfx/CameraThirdPersonBridge.h) : la MÊME instance que celle passée à Render(),
    // sinon le suivi de cible calculé ici n'aurait aucun effet visible côté rendu. Ajout
    // MINIMAL de signature (même politique que celle déjà motivée pour Render() ci-dessous).
    void  Update(double dt, gfx::Camera& camera);
    // cSceneMgr_Render 0x517CB0. `camera` : caméra 3e personne de App (gfx::Camera),
    // nécessaire au rendu monde (case InGame) pour les matrices vue/projection —
    // absente de l'original cSceneMgr_Render (App ne portait pas encore de caméra
    // séparée) ; ajout MINIMAL de signature pour le câblage WorldRenderer.
    void  Render(IDirect3DDevice9* device, const gfx::Camera& camera);
    void  OnLButtonDown(int x, int y);
    void  OnLButtonUp(int x, int y);
    void  OnChar(char c);            // WM_CHAR (saisie login/chat)
    void  OnKeyDown(int vk);         // WM_KEYDOWN
    void  Change(Scene s);
    Scene Current() const { return scene_; }

    // Perte/restauration du device D3D9 (autour d'un Reset()).
    void  OnDeviceLost();
    void  OnDeviceReset();

private:
    // Applique la transition demandée par LoginScene (PendingScene) et gère
    // l'entrée en jeu (init HUD la 1re fois).
    void  ConsumePending();

    // --- Slot BGM de scène (cSceneMgr +612 : cSceneMgr_ReinitBgm 0x517A80 /
    //     SceneMgr_ReleaseSoundBuffers 0x517B60). ---
    // Charge+joue le .BGM de la zone (World_LoadZoneResource 0x4DCB60 case 12,
    //   "G03_GDATA\D10_WORLDBGM\Z%03d.BGM") dans le slot, en boucle si l'option
    //   g_BgmEnabled (0x84DEF0) est active. Guard si fileId inconnu / fichier absent.
    void  LoadZoneBgm(int zoneId);
    // Snd_ReleaseBuffers 0x6A80D0 (SceneMgr_ReleaseSoundBuffers 0x517B60) sur le slot.
    void  ReleaseBgm();

    // Rechargement RE-ENTRANT de zone (warp / op 0x18 Pkt_GameServerConnectResult 0x469CF0
    // -> g_SceneMgr=5). Rejoue le cycle Scene_EnterWorldUpdate 0x52BFF0 case 1
    // (World_LoadZoneResource 0x4DCB60 idx 1..12) + rebuild geometrie .WO + BGM sur une
    // NOUVELLE zone, SANS re-init du renderer (leve les gardes one-shot worldGeomReady_/
    // LoadZoneBgm). Consomme sceneReloadPending/pendingWarpZoneId (GameState.h). // 0x52BFF0
    void  ReloadZone(int zoneId);

    Scene scene_      = Scene::None; // +0
    int   subState_   = 0;           // +4
    int   frameCount_ = 0;           // +8
    game::IntroState       introState_;      // automate fidèle Scene_IntroUpdate (279 frames)
    game::EnterWorldFlowState enterWorldState_; // automate fidèle Scene_EnterWorldUpdate
    game::InGameTickFlowState inGameTickState_; // automate fidèle Scene_InGameUpdate 0x52C600 (scène InGame)

    std::unique_ptr<ui::LoginScene>   login_;       // scènes 2/3/4
    std::unique_ptr<ui::GameHud>      hud_;          // HUD scène 6
    std::unique_ptr<ui::GameWindows>  windows_;      // fenêtres de jeu scène 6 (Entrepôt/Guilde/...)
    std::unique_ptr<WorldRenderer>    world_;        // rendu 3D des entités scène 6 (Scene/WorldRenderer.h)
    // Géométrie de monde statique (chunk .WO, cf. Gfx/WorldGeometryRenderer.h) — DISTINCT de
    // `world_` (WorldRenderer = entités players/monsters). worldAssets_/worldMap_ chargent
    // Z%03d.WO SEUL (pas WM/WJ/WG/atmosphère/son, hors périmètre de ce câblage) via les
    // hooks World/WorldIntegration.h ; worldGeom_ uploade+dessine son contenu GPU.
    std::unique_ptr<world::WorldAssets>       worldAssets_;
    std::unique_ptr<world::WorldMap>          worldMap_;
    std::unique_ptr<gfx::WorldGeometryRenderer> worldGeom_;
    // Slot BGM de scène = sous-objet SoundObj à cSceneMgr +612 dans l'original
    //   (cSceneMgr_ReinitBgm 0x517A80). Tenu par pointeur ici (header léger).
    std::unique_ptr<audio::BgmChannel>        bgm_;
    gfx::Renderer* renderer_ = nullptr;
    net::NetSystem* net_     = nullptr;
    void* notifyHwnd_ = nullptr;
    int   screenW_ = 1024, screenH_ = 768;
    std::string gameDataDir_;
    bool  hudReady_ = false;
    bool  windowsReady_ = false;
    bool  worldReady_ = false;
    bool  worldGeomReady_ = false;
};

} // namespace ts2

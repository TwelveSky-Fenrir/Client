// World/WorldIntegration.h — branche WorldMap (leaf, hooks) sur les VRAIS chargeurs
// Asset (WorldChunk/Texture/Sound) et Audio (SoundBank), pour que
// World_LoadZoneResource charge réellement les fichiers G03_GDATA\D07_GWORLD\Z%03d.*.
//
// WorldMap (World/WorldMap.h, décompilation IDA directe) ne possède aucune ressource :
// il expose des hooks. Ce fichier est la COLLE d'intégration (écrite à la main, pas
// par un agent) entre ce leaf module et les couches Asset/Audio déjà validées.
//
// Périmètre NON couvert ici (documenté, pas de faux-semblant) :
//   - Atmosphère/météo SilverLining COMPLÈTE (cAtmosphere_ctor 0x791b40, nuages/précipitations/
//     étoiles/soleil/lune) : SDK externe SilverLiningDirectX9-MT.dll non lié au projet ->
//     LoadMap() échoue proprement (comme si la licence était absente) plutôt que de simuler
//     un succès. EN REVANCHE (2026-07-15, mission "WAVE_06_silverlining") : le fichier
//     SilverLining.config global est chargé une fois, et le fichier .ATM PAR ZONE (case 7 de
//     LoadZoneResource, ci-dessous LoadDataFile) EST réellement parsé — cf.
//     Asset/AtmosphereFile.h — puis exposé via Atmosphere() pour Gfx/SkyRenderer.h. C'est un
//     sous-ensemble honnête (position géo + heure réelle + flags de rendu), pas le SDK.
//   - Rendu D3D des chunks chargés (upload VB/IB/texture) : hors périmètre "monde
//     de données" ; à brancher au jalon Gfx (MeshRenderer) une fois le placement
//     caméra/scène décidé.
#pragma once
#include "World/WorldMap.h"
#include "World/CollisionMesh.h" // Vague B4 : collision::GroundPlane + chaîne pick segment / plan-sol
#include "Asset/AtmosphereFile.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// COM (global) — évite d'inclure d3d9.h/d3dx9.h ici (même idiome que Scene/SceneManager.h:15).
struct IDirect3DDevice9;
struct IDirect3DTexture9;

namespace ts2::asset { class WorldChunk; struct Texture; class WSound; }
namespace ts2::audio { class SoundBank; class SoundBuffer; }
namespace ts2::gfx   { class GpuTexture; } // Gfx/GpuTexture.h — inclus par le .cpp seulement

namespace ts2::world {

// Sous-ensemble non destructif de SilverLining.config, chargé une fois par session.
// Le client d'origine exploite un grand nombre de clés côté DLL SilverLining ; ici on
// ne retient que les réglages de base nécessaires au socle de rendu et au repli.
struct SilverLiningConfig {
    double defaultLongitude = -122.064840;
    double defaultLatitude  = 30.0;
    double defaultAltitude  = 100.0;

    int defaultYear   = 2006;
    int defaultMonth  = 8;
    int defaultDay    = 15;
    int defaultHour   = 12;
    int defaultMinute = 0;
    double defaultSecond   = 0.0;
    double defaultTimezone  = -8.0;
    bool defaultDst         = true;

    double defaultTurbidity = 2.2;
    bool disableToneMapping = false;
    bool enableAtmosphereFromSpace = false;
    double atmosphereHeight = 300000.0;
    double atmosphereScaleHeightMeters = 8435.0;

    double skyBoxGamma = 2.2;
    bool skySimpleShader = false;
    double sunWidthDegrees = 1.0;
    double moonWidthDegrees = 10.0;
    bool disableLensFlare = false;
    bool disableSunGlare = true;
    bool disableMoonGlare = true;
    bool disableStarGlare = true;

    bool enablePrecipitationVisibilityEffects = true;
    int rainMaxParticles = 100000;
    int snowMaxParticles = 200000;
    int sleetMaxParticles = 100000;
};

// Ressources chargées pour la zone COURANTE (une seule zone active à la fois,
// comme le client d'origine qui recharge/écrase ses chunks au changement de zone).
class WorldAssets {
public:
    explicit WorldAssets(std::string gameDataDir);
    ~WorldAssets();
    WorldAssets(const WorldAssets&) = delete;
    WorldAssets& operator=(const WorldAssets&) = delete;

    // Construit les hooks liés à cette instance (user = this). À passer à WorldMap.
    WorldLoadHooks MakeHooks();

    // Device D3D9 servant à UPLOADER les textures de zone déjà décodées (aujourd'hui : les 3
    // minimaps, cf. MinimapTexture ci-dessous). Doit être posé AVANT le premier LoadZoneResource
    // (idem WorldMap::SetDevice). Sans device, les minimaps restent CPU-only et MinimapTexture()
    // renvoie nullptr -> la mini-carte retombe sur son aplat (dégradation propre « zone non chargée »).
    void SetDevice(IDirect3DDevice9* dev) { device_ = dev; }
    IDirect3DDevice9* Device() const { return device_; }

    // Racine "GameData" (contient G03_GDATA\D07_GWORLD, D09_WSOUND, D10_WORLDBGM, D11_ATMOSPHERE).
    const std::string& GameDataDir() const { return gameDataDir_; }

    // Accès aux chunks chargés (nullptr si absent/non chargé).
    // Collision (.WM/.WJ/.WM2) — CONFIRMED ex-VeryOldClient: WORLD_FOR_GXD::LoadWM (mRANGE*). Ancre
    // IDA : MapColl_LoadFaces 0x694510. TODO terrain WM (hauteur de sol) : buffer chargé mais JAMAIS
    // décodé typé (CollisionTri 156o + QuadNode 48o) ni requêté -> sol nul partout. Voir SPEC
    // TS2_WORLD_ROSETTA.md §3 G01 (décodage) + G02 (MapColl_GetGroundHeight 0x697130) ; consommateurs
    // G03 (Char_Update 0x581e10 ...). Requêtes raycast/sweep/slide = G04 (MapColl_RaycastNearest 0x6960c0).
    const asset::WorldChunk* Collision(CollisionSlot slot) const;
    // Terrain .WG — CONFIRMED ex-VeryOldClient: WORLD_FOR_GXD::LoadWG. Ancre IDA : MapColl_LoadMapFile
    // 0x697b30 (loader réel des faces + matériaux/textures). RÉELLEMENT rendu par Gfx/
    // WorldGeometryRenderer (chemin fixed-function FVF 530, Terrain_Render 0x698670, appelé AVANT les
    // .WO par Scene_InGameRender @0x52d9be). La CATÉGORIE/eau vient de textures[m].trailer[0]/trailer[1]
    // (prouvé Tex_LoadCompressedFromHandle 0x6a9cf0 : mat+40=cat, mat+44=subOrder). G01/G02 collision faits.
    const asset::WorldChunk* Faces()   const { return wg_.get(); }
    // Objets statiques .WO — CONFIRMED ex-VeryOldClient: LoadWO (mMObject). Ancre IDA :
    // MapColl_LoadObjectsA 0x6980d0. RÉELLEMENT rendu par Gfx/WorldGeometryRenderer (placement OK).
    const asset::WorldChunk* Objects() const { return wo_.get(); }
    // FX de zone .WP — CONFIRMED ex-VeryOldClient: LoadWP (mPSystem). Ancre IDA :
    // MapColl_LoadObjectsB 0x6983b0. RÉELLEMENT rendu par Gfx/WorldGeometryRenderer::RenderFxBillboards()
    // : le point d'entrée de rendu .WP EST Terrain_Render a5=2 @0x698c6d (Gfx_BeginUnlitPass 0x69e470 ->
    // Particle_RenderBillboards 0x6a70b0) — l'ancien « point d'entrée non identifié » était FAUX.
    const asset::WorldChunk* FxNodes() const { return wp_.get(); }

    // Lightmap .SHADOW de la zone (fichier DDS brut complet), consommée par
    // Gfx/WorldGeometryRenderer::buildTerrain() -> stage 1 (uv1, MODULATE). Vide si absente/non chargée.
    // Ancre IDA : Tex_LoadFromFile 0x6a9910 (charge) ; Terrain_Render @0x698f68 (bind stage 1).
    const std::vector<uint8_t>& ShadowBytes() const { return shadowBytes_; }
    // Objet texture .SHADOW décodé (asset::Texture, cf. LoadShadowTexture case 5) — nullptr si absent.
    const asset::Texture* Shadow() const { return shadow_.get(); }

    // État d'atmosphère RÉEL de la zone courante (fichier Z%03d.ATM, parsé byte-exact par
    // Asset/AtmosphereFile.h — case 7 de World_LoadZoneResource). Atmosphere().Valid()==false
    // tant qu'aucune zone n'a chargé son .ATM avec succès (cf. LoadDataFile ci-dessous) ;
    // consommé par Gfx/SkyRenderer.h::SetAtmosphere() pour le gradient jour/nuit dérivé de
    // l'heure réelle de la zone.
    const asset::AtmosphereFile& Atmosphere() const { return atmosphere_; }
    const SilverLiningConfig& SilverLining() const { return silverLining_; }

    // -----------------------------------------------------------------------
    // Requêtes de sol / collision terrain (Gaps G02/G03/G04), sur la couche principale (.WM,
    // CollisionSlot::Main). Fournisseurs prêts à câbler aux hooks consommateurs hors périmètre
    // (host.GetGroundHeight Game/EntityLifecycleTick.h:199 ; IsPointOnGround Game/AnimationTick.h:95 ;
    // IsGroundBlocked ICameraCollisionQueries AnimationTick.h:190). Délèguent au moteur
    // ts2::world::collision:: (World/WorldMap.h), portage byte-fidèle des MapColl_*. Build-safe :
    // renvoient false / no-op si la couche .WM n'est pas chargée/décodée. Ancres IDA sur chaque ligne.
    // -----------------------------------------------------------------------
    bool GetGroundHeight(float x, float z, float probeCeilingY, float& outGroundY) const; // 0x697130
    bool HasGroundAt(float x, float z) const;                                             // 0x697130 (plafond défaut)
    bool IsPointOnGround(float x, float y, float z) const;                                // 0x540d40
    bool PointInMeshXZ(float x, float z) const;                                           // 0x695dc0
    bool Raycast(const float start[3], const float dir[3], uint32_t& outFaceIndex,
                 float outHit[3], bool twoSide = false) const;                            // 0x6960c0
    bool SlideMoveGround(const float from[3], const float to[3], float speed, float dt,
                         float outPos[3]) const;                                          // 0x697330

    // -----------------------------------------------------------------------
    // Vague B4 — PLAN-SOL pour l'ombre planaire projetée (F_ENTITY3D). FOURNISSEUR du plan de sol
    // que Model_RenderPlanarShadow 0x40f720 lit puis passe à D3DXMatrixShadow. Deux voies :
    //   GetGroundPlaneForShadow : voie fidèle du binaire — pick SEGMENT model+hauteur -> +lightDir
    //     (Collision_SegPickA 0x420d60), filtre materialIndex==1, extraction plan + biais -d-0.1.
    //   GetGroundPlaneUnder     : commodité VERTICALE — plan directement sous (x,z) via la descente
    //     de MapColl_GetGroundHeight 0x697130 (filtre marchable planeB>0). Utile si la lumière est
    //     ~verticale ou pour un fallback simple.
    // Opèrent sur la couche .WM Main (walkable, materialIndex==1) — cf. rapport de front pour le
    // choix Main vs .WG (l'objet `a8` de Model_RenderWithShadow 0x40eee0 relève du Game-layer).
    // Build-safe : renvoient false / out.valid=false si la couche n'est pas chargée. Le vecteur
    // lumière à passer à D3DXMatrixShadow À CÔTÉ de out.shadowPlane est { -lightDir, 0.0 }.
    // -----------------------------------------------------------------------
    bool GetGroundPlaneForShadow(const float modelPos[3], float modelHeight, const float lightDir[3],
                                 float maxDist, collision::GroundPlane& out) const;       // 0x40f720
    bool GetGroundPlaneUnder(float x, float z, collision::GroundPlane& out) const;        // 0x697130

    // -----------------------------------------------------------------------
    // WG-02 — Collision CAMÉRA (Camera_UpdateCollision 0x538580). FOURNISSEURS des 4 requêtes du
    // binaire (3 slots distincts, prouvé) que l'oracle Gfx/CameraThirdPersonBridge
    // (WorldCameraCollision : game::ICameraCollisionQueries) branche par frame InGame :
    //   SweepCameraSegment -> .WG (slot 0 = g_GameWorld, @0x5387b9) ; IsPointBlocked -> Main+WJ ;
    //   HasGroundAt (déjà présent) -> Main ; LineOfSightBlockedByObjects -> objets .WO (TODO).
    // -----------------------------------------------------------------------
    // Maille de collision d'une couche quelconque (.WM/.WJ/.WM2 via AsCollision). nullptr si absente.
    const asset::CollisionMesh* CollisionMeshOf(CollisionSlot slot) const;
    // Maille du TERRAIN .WG (slot 0 = g_GameWorld lui-même) — un MapFaceChunk (AsFace), PAS un
    // MapCollisionChunk. JAMAIS requêté avant ce front. Ancre : Terrain_SweepSphereSegment
    // 0x69a1f0 opère sur this[35]=quadtree du .WG (dword_14A88C8).
    const asset::CollisionMesh* TerrainMesh() const;
    // Terrain_SweepSphereSegment 0x69a1f0 — sweep sphère caméra (rayon 2.5) contre le .WG.
    // false si la zone n'a pas de terrain chargé (dégradation fidèle « zone non chargée »).
    bool SweepCameraSegment(const float from[3], const float to[3], float radius,
                            float outHit[3]) const;                                        // 0x69a1f0
    // World_IsPointBlocked 0x540da0 — point bloqué (pas de sol Main, ou sol WJ au-dessus du Main).
    bool IsPointBlocked(const float p[3]) const;                                          // 0x540da0
    // MapColl_LineOfSightObjects 0x696fc0 — NON porté (blocage prouvé : table d'OBB par frame non
    // décodée dans asset::WorldMeshPart::geo). Renvoie toujours false -> le repli « ground-height
    // stepping » de Camera_UpdateCollision est désactivé (dégradation fidèle « objets ignorés »).
    bool LineOfSightBlockedByObjects(const float from[3], const float to[3]) const;       // 0x696fc0 (TODO)

    // -----------------------------------------------------------------------
    // WG-03 — Picking écran->terrain (Terrain_PickRayScreen 0x699a80). FOURNISSEUR : l'implémenteur
    // concret de game::ITerrainPicker (Game/SkillCombat.h:238, front skill-learn-cast / orchestrateur)
    // dérive un collision::ScreenPickCamera de gfx::Camera + viewport et appelle ceci. Le picking
    // du binaire vise les slots .WM(Main @0x536715) / .WJ(@0x540fc4), déjà décodés/requêtables.
    // -----------------------------------------------------------------------
    bool PickRayScreen(CollisionSlot slot, const collision::ScreenPickCamera& cam,
                       int sx, int sy, uint32_t& outFaceIndex, float outHit[3],
                       bool twoSide) const;                                                // 0x699a80

    // -----------------------------------------------------------------------
    // GX-ICON-01 / BEW-01 — Minimap de zone (3 textures Tex_LoadCompressedDDS 0x6A2E80). Les 3
    // minimaps SONT consommées par le binaire (accès indexé @0x681AAB, PAS mortes) ; la sélection
    // 0/1/2 vient de widget+0x268, gardée par widget+0x264==1 (@0x6818B4/@0x6818C7) — état UI.
    // ÉTAT : le consommateur EXISTE désormais (UI/MinimapWidget::DrawPanels, blit crop 145x128) et
    // reçoit texture+bornes via ui::MinimapWidget::SetSourceProvider — à poser dans
    // Scene/SceneManager.cpp (voir le rapport de front, le câblage n'est pas dans ce fichier).
    // -----------------------------------------------------------------------
    // Texture de minimap par index 0..2 (0=_MINIMAP01, 1=_MINIMAP02, 2=_MINIMAP03). nullptr sinon.
    const asset::Texture* Minimap(int index) const;                                       // 0x6a2e80

    // --- BEW-01 : ce que le consommateur UI (UI/MinimapWidget) doit réellement recevoir -------
    // La texture GPU de la minimap `index`, uploadée par LoadMinimap quand SetDevice a été appelé.
    // nullptr si zone/index non chargé ou device absent. D3DPOOL_MANAGED (gfx::GpuTexture) -> AUCUNE
    // recréation nécessaire sur OnDeviceLost/OnDeviceReset. Ancre : Tex_LoadCompressedDDS 0x6A2E80
    // @0x6A3040 (D3DXCreateTextureFromFileInMemoryEx -> objet+36 = IDirect3DTexture9*, ici Handle()).
    IDirect3DTexture9* MinimapTexture(int index) const;
    // Dimensions LOGIQUES (en-tête GXD +0/+4) de la minimap `index` = EXACTEMENT var_868/var_864 de
    // UI_GameHud_Render : @0x681560 `mov ecx, ds:dword_14A906C[eax]` et @0x68157B `mov ecx,
    // ds:dword_14A9070[eax]` avec eax = 0x28*mode — or dword_14A906C == unk_14A9068+4 et
    // dword_14A9070 == unk_14A9068+8, soit les champs +4/+8 de l'OBJET TEXTURE d'index `mode`
    // (`qmemcpy(this+1, header, 0x1C)` @0x6A2FFE). Ce ne sont donc PAS des « échelles par mode » :
    // c'est la taille logique de l'image, distincte de la surface D3D9 physique NextPow2
    // (Util_NextPow2_GXD @0x6A3040) -> asset::Texture::imgLogicalWidth/Height, JAMAIS width/height.
    // false si index invalide / non chargé.
    bool MinimapLogicalSize(int index, int& outW, int& outH) const;                        // 0x6a2e80
    // Bornes monde de la minimap = bbox de la RACINE du quadtree .WG (dword_14A88C8 = TerrainMesh()
    // ->nodes[0]). Ancre IDA : UI_GameHud_Render @0x681513/@0x681527/@0x681535/@0x681546 (noter les
    // DEUX négations sur Z). Projection self->pixel côté UI (hors périmètre) :
    //   px = (int)( logicalW * ( self.x  - minX)    / (maxX    - minX) )
    //   py = (int)( logicalH * (-self.z  - negMaxZ) / (negMinZ - negMaxZ) )
    struct MinimapBounds { float minX; float maxX; float negMaxZ; float negMinZ; bool valid; };
    MinimapBounds MinimapWorldBounds() const;

    // -----------------------------------------------------------------------
    // AUD-03 — FOURNISSEUR pour le tick d'ambiance positionnelle. `soundBank_` (chargé par
    // LoadWorldSound, case 11) est le pendant de la banque globale dword_14A90E0 ; le binaire
    // la rafraîchit CHAQUE frame en TÊTE de Player_UpdateLocalAnim 0x5321D0 :
    //   @0x5321DC  mov eax, ds:g_Opt_MusicVolume   ; 0x84DEE8, option idx10, 0..100 (PAS un booléen)
    //   @0x5321E2  push offset flt_1687330         ; = dword_1687234+0xFC = position du self
    //   @0x5321E7  mov ecx, offset dword_14A90E0   ; banque globale
    //   @0x5321EC  call WSndBank_UpdatePositional  ; 0x4DAC30  (xref UNIQUE 1/1)
    // Mapping vers la signature C++ existante (Audio/Sound3D.h:147) : a5 -> (enable = a5 != 0,
    // enableScale = a5). Le TICK lui-même vit dans Game/AnimationTick.* (hors de ce front) : cet
    // accesseur est le prérequis de son câblage. nullptr tant qu'aucune zone n'a chargé son .WSOUND.
    // -----------------------------------------------------------------------
    audio::SoundBank* SoundBank() const { return soundBank_.get(); }                       // 0x4dac30

    // -----------------------------------------------------------------------
    // AUD-05 — BGM de zone : slot PERSISTANT, pendant exact de g_GameWorld+2236 (0x14A90F8).
    // World_LoadZoneResource 0x4DCB60 case 12 @0x4DD43E :
    //   Snd_LoadOggToBuffers(this + 0x8BC, "G03_GDATA\D10_WORLDBGM\Z%03d.BGM", 1, 1, 1)
    //   -> ecx = g_GameWorld(0x14A883C) + 0x8BC = 0x14A90F8 ; a3 = kind = 1 = ONE-SHOT single
    //      (et NON 2=loop : cf. commentaire IDA de Snd_LoadOggToBuffers 0x6A8120).
    // Le play est ailleurs — Player_ResetCombatState @0x50F769 `mov ecx, offset dword_14A90F8`
    // (= le MÊME slot) puis @0x50F76E `call Snd_Play3D` avec vol=0x64=100 / pan=0, gardé par
    // @0x50F75A `cmp ds:g_BgmEnabled, 1` (0x84DEF0). PlayWorldBgm() reproduit ce site.
    //
    // ⚠️ CONFLIT DE MODÈLE à arbitrer par l'orchestrateur (voir rapport) : Scene/SceneManager.cpp
    // (LoadZoneBgm/bgm_) joue DÉJÀ ce même Z%03d.BGM via cSceneMgr+612 — qui est, lui, le slot du
    // BGM de menu (Scene_ServerSelectUpdate 0x518B30). Câbler PlayWorldBgm() SANS retirer
    // LoadZoneBgm ferait jouer la piste DEUX FOIS. Les deux slots sont distincts dans la cible.
    // -----------------------------------------------------------------------
    audio::SoundBuffer* WorldBgm() const { return worldBgm_.get(); }                       // g_GameWorld+2236
    // Player_ResetCombatState @0x50F75A/@0x50F769/@0x50F76E : if (g_BgmEnabled == 1)
    //   Snd_Play3D(&dword_14A90F8, 0, /*vol*/100, /*pan*/0). No-op si le slot n'est pas chargé.
    void PlayWorldBgm(bool bgmEnabled);                                                    // 0x50f76e
    // Snd_ReleaseBuffers 0x6A80D0 sur le slot monde (libère le SoundObj de g_GameWorld+2236).
    void ReleaseWorldBgm();                                                                // 0x6a80d0

private:
    // --- Implémentations des hooks (signatures WorldLoadHooks, `user` = this*). ---
    static void* AllocAtmosphere(void* user, unsigned size);
    static void* ConstructAtmosphere(void* user, void* mem, const char* name, const char* key);
    static void  DeviceBeginMap(void* user, void* device);
    static void  DeviceEndMap(void* user, void* device);
    static int   AtmosphereInitialize(void* user, void* atmosphere, const char* mapName, void* device);
    static bool  LoadWeatherDat(void* user, const char* path);
    static void  SetGeoLocation(void* user, double lat, double lon, double alt);
    static void  FinishLoad(void* user);

    static bool FreeZoneSound(void* user);
    static bool LoadMapFileWG(void* user, const char* path);
    static bool LoadObjectsWO(void* user, const char* path);
    static bool LoadObjectsWP(void* user, const char* path);
    static bool LoadShadowTexture(void* user, const char* path);
    static bool LoadFaces(void* user, CollisionSlot slot, const char* path);
    static void FreeFaces(void* user, CollisionSlot slot);
    static bool LoadMinimap(void* user, int index, const char* path);
    static bool LoadWorldSound(void* user, const char* path);
    static bool LoadWorldBgm(void* user, const char* path);
    static bool LoadDataFile(void* user, const char* path);
    // Hook queryCollisionMesh (WorldLoadHooks) : relie la maille décodée (Gap G01/G02) d'une
    // couche à WorldMap pour ses requêtes de sol. nullptr si la couche n'est pas chargée.
    static const asset::CollisionMesh* QueryCollisionMesh(void* user, CollisionSlot slot);

    // Maille de collision décodée de la couche principale (.WM Main), nullptr si absente.
    const asset::CollisionMesh* MainCollisionMesh() const;

    std::string gameDataDir_;
    IDirect3DDevice9* device_ = nullptr;  // posé par SetDevice (upload des minimaps, BEW-01)

    std::unique_ptr<asset::WorldChunk> wm_;             // CollisionSlot::Main
    std::unique_ptr<asset::WorldChunk> wj_;             // CollisionSlot::WJ
    std::unique_ptr<asset::WorldChunk> wmSecondary_;    // CollisionSlot::Secondary
    std::unique_ptr<asset::WorldChunk> wg_;
    std::unique_ptr<asset::WorldChunk> wo_;
    std::unique_ptr<asset::WorldChunk> wp_;
    // .SHADOW chargée (case 5) et DÉSORMAIS liée au stage 1 (lightmap sur uv1) par le chemin FF de
    // Gfx/WorldGeometryRenderer (ancre IDA : Terrain_Render @0x698f54/@0x698f68). `shadow_` = objet
    // décodé (asset::Texture) ; `shadowBytes_` = octets DDS bruts du fichier, fournis au renderer via
    // ShadowBytes() (le renderer crée la texture GPU sans dépendre d'Asset/Texture.h).
    std::unique_ptr<asset::Texture>    shadow_;
    std::vector<uint8_t>               shadowBytes_;
    // Les 3 minimaps de zone = world+2092/+2132/+2172 (unk_14A9068, stride 40 @0x681AAB).
    //   `minimaps_`   = objet CPU décodé (dims logiques + blocs DXT)  — asset::Texture.
    //   `minimapGpu_` = surface D3D9 correspondante (champ +36 de l'objet cible, @0x6A3040),
    //                   uploadée par LoadMinimap si SetDevice a été appelé. BEW-01.
    std::array<std::unique_ptr<asset::Texture>, 3> minimaps_;
    std::array<std::unique_ptr<gfx::GpuTexture>, 3> minimapGpu_; // type incomplet ici : ~WorldAssets est
                                                                 // défini dans le .cpp (GpuTexture complet)
    std::unique_ptr<asset::WSound>     wsound_;
    std::unique_ptr<audio::SoundBank>  soundBank_;
    // AUD-05 : slot BGM PERSISTANT de la zone = g_GameWorld+2236 (0x14A90F8), chargé par
    // LoadWorldBgm (case 12 @0x4DD43E). Avant ce front, un SoundBuffer AUTOMATIQUE de pile était
    // décodé puis détruit au `return` — travail pur perdu.
    std::unique_ptr<audio::SoundBuffer> worldBgm_;
    asset::AtmosphereFile              atmosphere_; // Z%03d.ATM de la zone courante (case 7)
    SilverLiningConfig                 silverLining_; // SilverLining.config (global, session)
};

} // namespace ts2::world

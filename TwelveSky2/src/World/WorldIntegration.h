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
#include "Asset/AtmosphereFile.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace ts2::asset { class WorldChunk; struct Texture; class WSound; }
namespace ts2::audio { class SoundBank; }

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
    // GX-ICON-01 — Minimap de zone (les 3 textures Tex_LoadCompressedDDS 0x6a2e80, chargées mais
    // jamais exposées). FOURNISSEUR pour UI/MinimapWidget (autre vague) : je produis/expose, je ne
    // câble PAS le consommateur UI. Les 3 minimaps SONT consommées par le binaire (accès indexé
    // @0x681aab, PAS mortes) ; la sélection 0/1/2 vient de widget+0x268, gardée par widget+0x264==1
    // (@0x6818b4/@0x6818c7) — état UI, hors périmètre monde.
    // -----------------------------------------------------------------------
    // Texture de minimap par index 0..2 (0=_MINIMAP01, 1=_MINIMAP02, 2=_MINIMAP03). nullptr sinon.
    const asset::Texture* Minimap(int index) const;                                       // 0x6a2e80
    // Bornes monde de la minimap = bbox de la RACINE du quadtree .WG (dword_14A88C8 = TerrainMesh()
    // ->nodes[0]). Ancre IDA : UI_GameHud_Render @0x681513/@0x681527/@0x681535/@0x681546 (noter les
    // DEUX négations sur Z). Projection self->pixel côté UI (hors périmètre) :
    //   px = (int)( logicalW * ( self.x  - minX)    / (maxX    - minX) )
    //   py = (int)( logicalH * (-self.z  - negMaxZ) / (negMinZ - negMaxZ) )
    struct MinimapBounds { float minX; float maxX; float negMaxZ; float negMinZ; bool valid; };
    MinimapBounds MinimapWorldBounds() const;

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
    std::array<std::unique_ptr<asset::Texture>, 3> minimaps_;
    std::unique_ptr<asset::WSound>     wsound_;
    std::unique_ptr<audio::SoundBank>  soundBank_;
    asset::AtmosphereFile              atmosphere_; // Z%03d.ATM de la zone courante (case 7)
    SilverLiningConfig                 silverLining_; // SilverLining.config (global, session)
};

} // namespace ts2::world

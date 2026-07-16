// Gfx/WorldGeometryRenderer.h — dessin RÉEL des objets statiques du chunk .WO (jalon 4->5,
// pendant "géométrie de monde" de Scene/WorldRenderer.h qui, lui, dessine les ENTITÉS).
//
// Ce fichier CÂBLE trois modules déjà écrits, jusqu'ici non reliés :
//   World/WorldIntegration.h — charge RÉELLEMENT Z%03d.WO (world::WorldAssets::Objects()).
//   Asset/WorldChunk.h       — parseur .WO (asset::ObjectChunk -> asset::Model -> WorldMeshPart,
//                              validé EOF-exact sur 455/455 fichiers D07_GWORLD).
//   Gfx/MeshRenderer.h       — pipeline GPU skinné (VB/IB 76o + palette d'os) du jalon 4,
//                              ici réutilisé TEL QUEL (aucune modification de MeshRenderer.*)
//                              via son API PUBLIQUE DrawSkinnedSubset().
//
// ===========================================================================================
//  FORMAT DE PLACEMENT — RÉSOLU par désassemblage IDA, cf. Docs/TS2_WO_PLACEMENT_FORMAT.md
// ===========================================================================================
// (Corrige le bandeau précédent, écrit sans accès IDA/x32dbg — l'hypothèse "matrice identité,
// placement non récupérable" était FAUSSE : le placement existe, il est simplement porté par
// `ObjectChunk::auxRecords`/`models[]`, pas par `placements[]`.)
//
// 1) `ObjectChunk::models[]` (Asset/WorldChunk.h) = des maillages GABARITS (templates), en
//    repère LOCAL/OBJET — PAS en coordonnées monde finales. Uploadés UNE SEULE FOIS chacun en
//    VB/IB GPU (cf. uploadPart()/Build() : géométrie inchangée par rapport à la version
//    précédente, seule l'interprétation du placement change).
//
// 2) `ObjectChunk::auxRecords[]` (28 o/instance sur disque, désormais typé `asset::AuxRecord`
//    dans WorldChunk.h) = LES INSTANCES PLACÉES : chaque entrée référence un gabarit par
//    `modelIndex` et porte SA propre position/rotation. Confirmé par désassemblage de
//    `Model_RenderParts` (0x6A3720) et `Model_RenderWithShadow_0` (0x6A4110), consommateurs de
//    ce tableau dans le rendu de scène principal (`Terrain_Render` 0x698670). 50 413 instances
//    vérifiées sur 97 fichiers réels, jusqu'à 643 réutilisations d'un même gabarit — la preuve
//    directe que `models[]` est en repère local (sinon les instances se superposeraient).
//
// 3) `ObjectChunk::placements[]` (100 o/GABARIT, pas par instance) = métadonnée : nom de
//    fichier source NUL-terminé (+ bourrage mémoire non lu par le moteur, hors le tag
//    "NO_SHADOW_" utilisé par `MapColl_RenderShadowMap`). Exposé en debug via
//    `ObjectChunk::placementNames`, IGNORÉ pour le placement (ce n'est pas un transform).
//
// 4) Matrice monde par instance (désassemblage `0x6a379c`-`0x6a3892` / `0x6a41a3`-`0x6a4299`,
//    IDENTIQUE dans les deux fonctions consommatrices) :
//      World = Rz(rot.z°) * Ry(rot.y°) * Rx(rot.x°) * T(pos)
//    Pas de matrice d'échelle nulle part (scale = 1.0 toujours pour les .WO).
//
// 5) [RÉSOLU 2026-07-14, mission « parts multi-ancre A>1 »] Le bloc "os" en tête de géométrie
//    (64 o * `A`, cf. WorldMeshPart::A) n'est PAS une palette de matrices de skinning : `A` est
//    le nombre de FRAMES d'un flipbook de positions précalculées (balancement au vent probable
//    des arbres/objets), confirmé par désassemblage :
//      - `MeshPart_Load` (0x6AD169) copie `32*A*B` octets — A blocs consécutifs de B sommets
//        32 o (MOBJ, position+normale+uv) — TELS QUELS dans un seul IDirect3DVertexBuffer9,
//        sans jamais lire le bloc "os" (64*A o, this+284) pour construire une palette d'os :
//        ce bloc est chargé/libéré (MeshPart_Free 0x6AB1F0) mais AUCUN consommateur du chemin
//        de rendu ne le lit — métadonnée d'animation probable (paramètres de sway), pas un
//        skinning au sens de MeshRenderer.h.
//      - `MeshPart_Render` (0x6AED60) sélectionne la frame via `SetStreamSource(0, vb,
//        32*frame*B, 32)` où `frame = Crt_Dbl2Uint(temps)` (tronqué, borné à `[0, A-1]` par
//        `Model_RenderParts` 0x6A3720/0x6A3756) : un SWAP BRUT de toute la frame, PAS
//        d'interpolation/blend GPU. L'index buffer (6*D o) est PARTAGÉ par toutes les frames
//        (topologie fixe, seules positions/normales varient).
//    CE RENDU DÉCODE DÉSORMAIS TOUT `A` (plus de saut) mais reste STATIQUE : seule la FRAME 0
//    du flipbook est uploadée (pose figée, cf. uploadPart()/Gfx/WorldGeometryRenderer.cpp) —
//    PAS le vrai balancement au vent (nécessiterait de rejouer les frames au temps réel via un
//    VS dédié ou un swap de stream source par frame, hors périmètre de cette mission). Compteur
//    exposé : `MultiAnchorStaticCount()` (parts A>1 effectivement dessinées en pose statique).
//
// 6) Format vertex EXPLOITÉ (inchangé, cf. Docs/TS2_ASSET_FORMATS.md "MobjVertex 32 o, FVF
//    0x112" = D3DFVF_XYZ|NORMAL|TEX1) :
//      +0  float3 position   (coordonnées LOCALES au gabarit, cf. point 1 — PAS finales)
//      +12 float3 normal     (unitaire, vérifié)
//      +24 float2 uv
//    PAS de BLENDWEIGHT/BLENDINDICES dans ce format disque (contrairement au vertex SObject
//    76 o de MeshRenderer.h) : converti à la volée vers gfx::GpuSkinVertex avec poids
//    (1,0,0,0) / index os 0 -> réutilise le pipeline skinné de MeshRenderer SANS le modifier
//    (le VS ignore tangent/binormal, jamais lus par kSkinnedVS — donc mis à zéro sans perte).
//
// CE QUI EST DONC RÉELLEMENT DESSINÉ : la géométrie (VB/IB/texture diffuse tex1) de CHAQUE
// WorldMeshPart (TOUT `A`, cf. point 5 MIS À JOUR 2026-07-14 — plus limité à A==1) d'un chunk
// .WO chargé, une fois par INSTANCE de `auxRecords[]`, à la matrice monde `Rz*Ry*Rx*T`
// construite depuis la position/rotation propre de cette instance (cf. point 2/4 ci-dessus).
// Pour les parts `A>1`, seule la FRAME 0 du flipbook de sway est dessinée (pose statique figée).
// CE QUI NE L'EST PAS (TODO documenté, pas simulé) :
//   - Vrai balancement au vent des parts multi-ancre (A>1, arbres/objets) : format décodé (cf.
//     point 5) mais rejeu temporel non câblé — pose figée à la frame 0 (compteur exposé
//     MultiAnchorStaticCount()), pas d'interpolation/shader de sway.
//   - Matériaux secondaires (tex2/materials[]) : seul tex1 est utilisé comme diffuse.
//   - Collision WM/WJ (invisible) et FX WP (nœuds particules) : hors périmètre de ce fichier.
//   - Frustum culling / LOD / batching par texture : aucun (boucle plate par instance).
//   - Tag "NO_SHADOW_" (placements[]) : pas de shadow map ici, donc sans effet visible.
//
// ===========================================================================================
//  AUDIT 2026-07-14 — CIEL / ATMOSPHÈRE / EAU / EFFETS DE ZONE : INVENTAIRE DE CE QUI MANQUE
//  (mission "audit du manque de rendu ciel/atmosphere/effets de zone" — désassemblage IDA
//  via idaTs2, adresses = imagebase 0x400000). CONSTAT : la géométrie .WO ci-dessus est
//  actuellement le SEUL contenu de décor dessiné par ClientSource en InGame — tout ce qui
//  suit est ABSENT à 100%, confirmé par recherche exhaustive (grep) dans src/ ET par xrefs
//  IDA sur les points d'entrée réels. Rien de ce qui suit n'est une supposition.
//
//  1) SKYBOX SIMPLE — Env_RenderSkyCube (0x6a8f60), appelée par Gfx_BeginFrame (0x6a2280) au
//     tout début de CHAQUE frame de CHAQUE scène (Intro/ServerSelect/Login/CharSelect/
//     EnterWorld/InGame — 8 xrefs), sous garde `a2 && *a2` (flag "ciel actif" non identifié
//     ici). Décompilée : un cube texturé à 6 faces (quad 4 sommets/face, 20 o/sommet =
//     XYZ+TEX1, PAS de vertex color), centré sur la caméra (translation = g_CameraPos, donc
//     suit le joueur — technique skybox classique), lighting désactivé, culling inversé
//     pendant le dessin (dword_7FFEA4 gate D3DRS_CULLMODE). ABSENT de ClientSource : aucun
//     cube/texture de ciel, aucun appel équivalent.
//  2) ATMOSPHÈRE COMPLÈTE (SilverLining SDK, ~150 fonctions `cAtmosphere_*`/`SL_*`/`Sky_*`/
//     `Cloud_*`/`AtmoFromSpace_*` toutes présentes et analysées dans l'IDB, ex. 0x793390-
//     0x797920) — pipeline réel confirmé par recherche dans Scene_InGameRender (0x52D0B0,
//     20 Ko de pseudocode) :
//       a) Env_UpdateFrame (0x412550, appelée à l'offset 0x25d de la fonction, donc TRÈS TÔT
//          dans la frame) = Env_UpdateSkyMatrix(0x412190) [pousse view/proj dans
//          cAtmosphere_SetModelviewMatrix/SetProjectionMatrix] + cAtmosphere_RenderFrame
//          (0x793b80, appelle en interne cAtmosphere_DrawObjects 0x792a60) + mise à jour
//          lumière directionnelle/brouillard (Env_UpdateSunLight/Env_UpdateFogState).
//       b) Env_StepTimeOfDay (0x412590, appelée à l'offset 0x2a99, APRÈS les 2 passes
//          Terrain_Render/3 passes Fx_EmitterDraw ci-dessous) = Atmosphere_DrawFrame
//          (0x794fe0, 0x813 o) : le VRAI dessin du dôme de ciel/soleil/lune/étoiles/nuages
//          volumétriques (Sky_RenderStars 0x74b030, Cloud_UpdateAndRender 0x702ff0, etc.).
//     ABSENT de ClientSource : World/WorldIntegration.h le documente déjà honnêtement dans
//     son bandeau de tête ("Atmosphère/météo SilverLining : SDK externe
//     SilverLiningDirectX9-MT.dll non lié au projet -> LoadMap() échoue proprement") — ce
//     fichier-ci CONFIRME par désassemblage que le SDK est bien massivement utilisé par le
//     client d'origine (ce n'est pas un vestige mort), donc l'écart est RÉEL et MAJEUR, pas
//     juste une prudence excessive de WorldIntegration.h.
//  3) TERRAIN / SOL VISIBLE + EAU — Terrain_Render (0x698670, commentaire IDA d'origine :
//     "render quadtree terrain tile/water/land layers with reflections"), appelée 2×/frame
//     depuis Scene_InGameRender (offsets 0x90e et 0x1a28 — donc AVANT même les objets .WO
//     dans le pipeline d'origine). Consomme le fichier .WG (Z%03d.WG, format quadtree G3W/
//     WM², chargé par MapColl_LoadMapFile 0x697b30 ; wave texture générée par
//     cWorldMesh_MakeWaterWaveTexture 0x451220 pendant le chargement). CONSTAT CRITIQUE :
//     Asset/WorldChunk.cpp PARSE ce fichier .WG (asset::WorldChunk::AsFace(), exposé par
//     World/WorldIntegration.h::WorldAssets::Faces()) mais AUCUN fichier de ClientSource/
//     ne consomme jamais Faces()/AsFace() (grep exhaustif, 0 résultat hors Asset/ et
//     WorldIntegration.h) : le SOL/TERRAIN LUI-MÊME (pas seulement le ciel) n'est dessiné
//     NULLE PART. Conséquence visible : les objets .WO de ce fichier flottent actuellement
//     sans aucune surface dessinée sous leurs pieds — seul le fond d'écran (Renderer::
//     clearColor_, désormais noir pur 0x00000000) tient lieu de "sol". La passe reflet dédiée
//     (cWorldMesh_RenderReflection 0x450f50) a, elle, 0 xref dans le binaire d'origine
//     (confirmé via xrefs_to) — code mort côté TS2 lui-même, donc PAS un écart introduit ici.
//  4) EFFETS DE ZONE / PERSONNAGE — Fx_EmitterDraw (0x585e30, cf. Docs/TS2_FX_CATALOG.md),
//     appelée 3×/frame depuis Scene_InGameRender (offsets 0xc64/0x1c3b/0x2a28 — probablement
//     passes ombre/couleur/son) : dispatch de rendu des 26 slots `Fx_Attach*` (lueur d'arme,
//     étincelle d'impact, aura de compétence, parade, dash…) + du pool de projectiles
//     (Fx_HomingProjectileUpdate 0x5862d0). ABSENT de ClientSource : Game/
//     GroundAuraWorldObjectTick.h/.cpp REPRODUIT fidèlement le TICK de données (positions,
//     timers, pool SoA de projectiles) mais il n'existe AUCUN contrepartie GPU (aucun
//     Gfx/Fx*.h, aucun appel dessinant réellement un de ces effets à l'écran) — les données
//     avancent, rien n'est dessiné.
//  5) FX AMBIANTS PLACÉS EN ZONE (fichier .WP — torches/feux/cascades probables, distincts
//     des Fx_Attach* liés aux personnages) — Z%03d.WP chargé via MapColl_LoadObjectsB
//     (0x6983b0) qui appelle Fx_NodeLoadFromHandle (0x6a69f0, format "Node" documenté au
//     §4.3 de Docs/TS2_FX_CATALOG.md). Asset/WorldChunk.cpp PARSE ce fichier
//     (asset::WorldChunk, exposé par WorldAssets::FxNodes()) mais, exactement comme le
//     terrain (point 3), AUCUN fichier ne consomme jamais FxNodes() (grep exhaustif). Le
//     point d'entrée de RENDU réel de ces nœuds dans le binaire d'origine n'a PAS été
//     retrouvé dans cette passe d'audit (pas de fonction `MapColl_RenderObjects*` ; probable
//     partage du pipeline Model_RenderParts/Fx_EmitterDraw via une table non identifiée ici)
//     — laissé comme mystère RE ouvert, PAS inventé.
//
//  RÉSUMÉ : sur 5 systèmes (skybox simple, atmosphère SilverLining, terrain+eau, FX
//  personnage, FX de zone), AUCUN n'a de contrepartie de rendu dans ClientSource — seule la
//  géométrie .WO (props statiques) et les entités (Scene/WorldRenderer.h) sont dessinées.
//  CORRECTIF D'ORIGINE (2026-07-14, ce bandeau) : un DÉGRADÉ DE CIEL EN REPLI SIMPLIFIÉ — PAS
//  SilverLining réel, PAS de skybox texturée, juste un quad plein écran 2 couleurs FIXES
//  dessiné avant le décor. Le sol/terrain/eau/FX restent des TODO non traités ici (hors
//  périmètre : nécessitent respectivement le parseur .WG déjà écrit mais jamais câblé à un
//  renderer, et un nouveau module Gfx/Fx*.h qui n'existe pas encore).
//
//  MISE À JOUR 2026-07-15 (WAVE_06_silverlining) : le quad 2 couleurs FIXES ci-dessus est
//  REMPLACÉ par Gfx/SkyRenderer.h, dont les couleurs sont désormais DÉRIVÉES du fichier .ATM
//  RÉEL de la zone active (Asset/AtmosphereFile.h, parsé byte-exact par
//  World/WorldIntegration.h::WorldAssets::Atmosphere() — case 7 de World_LoadZoneResource,
//  déjà géré par World/WorldMap.h, désormais réellement câblé côté données) et de
//  SilverLining.config global chargé au démarrage. Reste un premier pas honnête, PAS le SDK
//  SilverLining complet (aucun nuage/précipitation/étoile/soleil/lune dessiné — cf. bandeau
//  complet dans SkyRenderer.h). Build() transmet désormais assets.SilverLining() et
//  assets.Atmosphere() au ciel ; SceneManager place RenderSky() avant/après le monde.
//
//  AUDIT 2026-07-15 (relecture SkyRenderer.cpp + WorldGeometryRenderer.cpp) — BUG CORRIGÉ :
//  SkyRenderer::Render() pose SetVertexShader(nullptr)/SetPixelShader(nullptr) directement
//  sur le device D3D9 PARTAGÉ avec meshRenderer_. Or MeshRenderer::DrawSkinnedSubset()
//  court-circuite le re-bind VS/PS via un cache purement local à l'instance (`currentPass_`),
//  qui ignore ces pokes externes au device : sans correctif, le cache pourrait conserver un
//  shader skinné en mémoire après la passe ciel. Corrigé en ajoutant
//  MeshRenderer::InvalidateShaderBindingCache() (Gfx/MeshRenderer.h), appelé dans Render()
//  après tout passage ciel avant tout dessin de mesh. Le reste des deux fichiers
//  (SkyRenderer.cpp/.h et WorldGeometryRenderer.cpp/.h) a été relu intégralement : aucun autre placeholder non
//  documenté trouvé (formules d'offset géométrie A/B/C/D, conversion vertex MOBJ->GPU,
//  matrice Rz*Ry*Rx*T, tailles VB/IB, tout concorde avec Asset/WorldChunk.h et les bandeaux
//  IDA existants) ; les manques restants (sway réel, tex2/materials, terrain/eau, FX,
//  éphéméride/keyframes .ATM réelles) étaient déjà honnêtement documentés avant cet audit.
//
//  MISE À JOUR 2026-07-16 (STAGE RENDER — Gap G1 « le sol ») : le point 3 de l'audit ci-dessus
//  (« TERRAIN / SOL VISIBLE non rendu ») est RÉSOLU pour la surface diffuse. buildTerrain() +
//  renderTerrain() (cf. .cpp) consomment enfin WorldAssets::Faces() (asset::MapFaceChunk décodé
//  typé : CollisionFace 156o + TerrainVertex 40o) : les faces .WG sont groupées par materialIndex
//  et dessinées via meshRenderer_ (world=identité, texture diffuse par matériau), fidèle à
//  Terrain_Render 0x698670 (SetFVF 530 = XYZ|NORMAL|TEX2 @0x698e6d, batch/matériau, appelé AVANT
//  les .WO par Scene_InGameRender @0x52d9be). Les props .WO ne flottent plus sans sol. RESTES
//  (TODO précis, ancres dans renderTerrain()) : cull quadtree/frustum par frame (perf, dessine
//  tout pour l'instant — correct), G6 eau/bump-env (catégorie de matériau non décodée + pipeline
//  skinné sans BUMPENVMAP), G8 lightmap .SHADOW au stage 1 (uv1 ; le vertex réutilisé n'a qu'un
//  TEXCOORD0 — texture .SHADOW déjà chargée dans WorldAssets::shadow_, prête à câbler). L'eau et
//  l'ombre nécessitent un chemin fixed-function multitexture dédié (jalon ultérieur).
#pragma once
#include "Gfx/Renderer.h"
#include "Gfx/MeshRenderer.h"
#include "Gfx/Camera.h"
#include "Gfx/SkyRenderer.h" // ciel dérivé du .ATM réel (cf. MISE À JOUR 2026-07-14 ci-dessus)
#include "Asset/WorldChunk.h" // asset::AuxRecord : type complet requis (membre std::vector direct)
#include <cstddef>
#include <vector>

namespace ts2::asset { struct WorldMeshPart; struct TextureBlock; }
namespace ts2::world { class WorldAssets; }

namespace ts2::gfx {

class WorldGeometryRenderer {
public:
    ~WorldGeometryRenderer() { Shutdown(); }
    WorldGeometryRenderer() = default;
    WorldGeometryRenderer(const WorldGeometryRenderer&) = delete;
    WorldGeometryRenderer& operator=(const WorldGeometryRenderer&) = delete;

    // Construit son propre MeshRenderer (décl. vertex 76o + shaders skinnés) — instance
    // dédiée, indépendante de celle de Scene/WorldRenderer.h (pas de couplage inter-fichiers
    // pour éviter tout conflit d'édition concurrente).
    bool Init(Renderer& renderer);
    void Shutdown();

    // Convertit + uploade les WorldMeshPart (TOUT A, cf. bandeau ci-dessus point 5 — parts
    // A>1 en pose statique frame 0) du chunk .WO courant vers des
    // IDirect3DVertexBuffer9/IndexBuffer9. Remplace le contenu
    // GPU précédent (changement de zone). Renvoie false seulement si aucun chunk WO n'est
    // chargé dans `assets` (un WO présent mais vide/0-modèle renvoie true, 0 objet dessiné).
    // Transmet aussi assets.Atmosphere() (Z%03d.ATM réel de la zone) à skyRenderer_ — cf.
    // MISE À JOUR 2026-07-14 : c'est ICI que le ciel dérivé des vraies données est rafraîchi
    // à chaque changement de zone, indépendamment du succès/échec du chargement .WO lui-même
    // (appelé avant tout `return` de cette fonction, cf. .cpp).
    bool Build(const world::WorldAssets& assets);

    void OnDeviceLost();
    void OnDeviceReset();

    // Passe atmosphérique SilverLining minimale (couche ciel/atmosphère). Peut être appelée
    // avant les objets statiques ou après les entités, avec depth test actif, pour respecter
    // le placement réel des deux points d'entrée frame du moteur d'origine.
    void RenderSky(int screenW, int screenH);

    // Dessine, pour chaque instance de auxRecords[], toutes les parts uploadées du gabarit
    // qu'elle référence (models[instance.modelIndex]), à la matrice monde
    // Rz*Ry*Rx*T construite depuis SA position/rotation propre (cf. bandeau .h point 2/4 —
    // CORRIGÉ, remplace l'ancienne matrice identité globale). La couche ciel est pilotée par
    // RenderSky() pour pouvoir se placer à la fois avant le décor et après les entités.
    void Render(const Camera& camera, int screenW, int screenH);

    size_t UploadedPartCount() const { return objects_.size(); }
    // Parts A>1 réellement ignorées (échec de taille/corruption uniquement, cf. uploadPart()) —
    // depuis la résolution du format multi-ancre (bandeau .h point 5, MISE À JOUR 2026-07-14),
    // A>1 n'est plus une cause de saut : voir MultiAnchorStaticCount() pour ces parts-là.
    size_t SkippedMultiAnchorCount() const { return skippedMultiAnchor_; }
    // Parts A>1 dessinées avec succès mais en pose STATIQUE (frame 0 du flipbook de sway
    // uniquement, cf. bandeau .h point 5) — pas le vrai balancement au vent.
    size_t MultiAnchorStaticCount() const { return multiAnchorStaticCount_; }
    size_t InstanceCount() const { return instances_.size(); }
    // Nombre d'appels DrawSkinnedSubset qu'effectuera Render() (instances * parts uploadées
    // du gabarit référencé) — sert de log de sanité pour prouver que le rendu utilise bien
    // N positions distinctes (une par instance) et pas une seule matrice globale.
    size_t PlannedDrawCallCount() const;

    // --- Terrain .WG (Gap G1 « le sol », ancre IDA : Terrain_Render 0x698670) : logs de sanité. ---
    // Nombre de lots GPU terrain (chunks <=65535 sommets, groupés par matériau) réellement
    // uploadés, et total de faces terrain (3 sommets/face, 120o/face copiés @0x698e21).
    size_t TerrainBatchCount() const { return terrainBatches_.size(); }
    size_t TerrainFaceCount()  const { return terrainFaceCount_; }

private:
    struct StaticObject {
        SkinnedMesh mesh; // un seul LOD (le .WO n'a pas de niveaux de LOD multiples)
    };

    // Plage [start, start+count) dans objects_ des parts uploadées d'un gabarit models[i].
    struct ModelRange {
        size_t start = 0;
        size_t count = 0;
    };

    // Lot de dessin terrain (Gap G1) : un morceau (<=65535 sommets, contrainte INDEX16) des
    // faces d'UN matériau, prêt pour un DrawIndexedPrimitive via meshRenderer_. La texture est
    // une réf NON-possédante dans terrainTextures_ (libérée une seule fois par releaseTerrain()).
    // Ancre IDA : Terrain_Render 0x698670 (batch par matériau, DrawPrimitiveUP stride 40 @0x698ff3).
    struct TerrainBatch {
        SkinnedLod         lod;              // VB(76o)/IB(INDEX16) — possédés par ce lot
        IDirect3DTexture9* diffuse = nullptr; // réf dans terrainTextures_ (NON possédée ici)
    };

    void releaseObjects();
    bool uploadPart(const asset::WorldMeshPart& part, StaticObject& out);
    // Gap G1 : construit les lots GPU du terrain .WG (WorldAssets::Faces()) — faces groupées par
    // materialIndex, sommets TerrainVertex 40o -> GpuSkinVertex 76o (repère MONDE, world=identité).
    // Build-safe : no-op si device/.WG absent. Ancre IDA : Terrain_Render 0x698670.
    bool buildTerrain(const world::WorldAssets& assets);
    void releaseTerrain();
    // Gap G1 : dessine tous les lots terrain via meshRenderer_ (world=identité, palette identité),
    // CULLMODE=NONE encadré (sauvegarde/restaure) pour garantir la visibilité du sol. Le cull par
    // quadtree/frustum de l'original reste un TODO (cf. .cpp). Appelée par Render() AVANT les .WO.
    void renderTerrain();
    static IDirect3DTexture9* createTextureFromBlock(IDirect3DDevice9* dev,
                                                      const asset::TextureBlock& tex);
    // Construit World = Rz(rot.z)*Ry(rot.y)*Rx(rot.x)*T(pos), cf. bandeau .h point 4.
    static D3DXMATRIX BuildInstanceWorldMatrix(const asset::AuxRecord& inst);

    IDirect3DDevice9*           dev_ = nullptr;
    MeshRenderer                 meshRenderer_;
    SkyRenderer                  skyRenderer_;  // ciel dérivé du .ATM réel (cf. bandeau MISE À JOUR)
    // TODO terrain WO (tick d'anim non câblé) : ces 3 états (objects_/modelRanges_/instances_) sont
    // LA SOURCE (auxRecords/models) qu'attend l'avance de phase d'anim des sous-objets .WO, aujourd'hui
    // non reliée. Voir SPEC TS2_WORLD_ROSETTA.md §3 G08 ; ancre IDA : MapColl_UpdateObjectAnim 0x694a00
    // (site d'appel unique Scene_InGameUpdate 0x52c600 @0x52c94b, kAnimFps=15.0). À EXPOSER (hors passe).
    std::vector<StaticObject>    objects_;      // parts GPU uploadées, groupées par gabarit
    std::vector<ModelRange>      modelRanges_;  // modelRanges_[modelIndex] -> plage dans objects_
    std::vector<asset::AuxRecord> instances_;   // copie de ObjectChunk::auxRecords ; CONFIRMED ex-VeryOldClient: MOBJECTINFO
    size_t                       skippedMultiAnchor_ = 0;     // parts ignorées (échec réel, cf. pt.5)
    size_t                       multiAnchorStaticCount_ = 0; // parts A>1 rendues en pose statique

    // --- Terrain .WG (Gap G1) : le SOL. Source = WorldAssets::Faces() (asset::MapFaceChunk),
    // décodé typé (CollisionFace 156o + TerrainVertex 40o) par le stage DECODE. terrainTextures_
    // = une texture diffuse par matériau (nullptr si absente) ; terrainBatches_ = lots GPU
    // (chunks <=65535 sommets) groupés par matériau. Ancre IDA : Terrain_Render 0x698670
    // (a1+88=faces, face.materialIndex@0 -> texture a1+16[+48], vertex 40o FVF 530). ---
    std::vector<IDirect3DTexture9*> terrainTextures_; // POSSÉDÉES (une par matériau, ordre .WG)
    std::vector<TerrainBatch>       terrainBatches_;   // lots prêts à dessiner (réfs textures ci-dessus)
    size_t                          terrainFaceCount_ = 0; // total faces terrain uploadées (sanité)

    bool                         ready_ = false;
};

} // namespace ts2::gfx

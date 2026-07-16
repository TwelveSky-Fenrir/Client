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
//     WM², chargé par MapColl_LoadMapFile 0x697b30 ; TWS-01 : le bump-map d'eau est la texture de
//     FALLOFF de MapColl_CreateFalloffTexture 0x694ca0 — cWorldMesh_MakeWaterWaveTexture 0x451220
//     est du code MORT, jamais appelé par le binaire livré). CONSTAT CRITIQUE :
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
//  MISE À JOUR 2026-07-16 (FRONT W3-F3 — chemin terrain FIXED-FUNCTION dédié) : le rendu du terrain
//  passe désormais par un chemin FF NATIF (plus meshRenderer_/shaders), fidèle à Terrain_Render
//  0x698670. buildTerrain()/renderTerrain() (cf. .cpp) :
//    - VB FVF 530 (0x212 = XYZ|NORMAL|TEX2, stride 40) uploadé par memcpy depuis asset::TerrainVertex
//      (aucune conversion) — SetFVF(530) @0x698e6d ;
//    - couches groupées par matériau et TRIÉES par un RANG dérivé de (catégorie=trailer[0],
//      subOrder=trailer[1]) = textures[m].trailer[*] (prouvé Tex_LoadCompressedFromHandle 0x6a9cf0
//      ; ex-VeryOldClient TEXTURE_FOR_GXD : trailer = processMode/alphaMode) ;
//    - LIGHTMAP .SHADOW au stage 1 sur uv1 : MODULATE (=4, PAS MODULATE2X — commentaire corrigé)
//      @0x698f54 + SetTexture(1) @0x698f68 (le vertex FF possède bien uv1 -> le TODO « 1 seul
//      TEXCOORD » a DISPARU ; texture créée depuis WorldAssets::ShadowBytes()) ;
//    - EAU (catégorie 3) : passe bump-env D3DTOP_BUMPENVMAPLUMINANCE @0x699206 avec matrice bump
//      animée (wavePhase_) + falloffTex_ (MapColl_CreateFalloffTexture 0x694ca0), V8U8 256x256
//      procédurale générée au Build. TWS-01 : c'est bien la FALLOFF (*(a1+20) @0x69928f), pas une
//      texture de vagues — 0x451220 est mort et non porté.
//  FX de zone .WP : RenderFxBillboards() (passe a5=2 @0x698c6d : Gfx_BeginUnlitPass 0x69e470 ->
//  Particle_RenderBillboards 0x6a70b0) dessine 1 billboard placé par instance (sous-ensemble
//  build-safe). Sway .WO : TickWorldAnim() avance la phase de flipbook par instance (état possédé,
//  MapColl_UpdateObjectAnim 0x694A00).
//
//  MISE À JOUR Passe 4 / W5 (front terrain-motion) — LISTE EXACTE DES CATÉGORIES DESSINÉES.
//  Terrain_Render n'a que 2 passes (garde @0x698676-0x6986a2 : a5 ∈ [1,2] ; sites d'appel
//  Scene_InGameRender @0x52d9be a5=1 et @0x52ead8 a5=2). Table complète des rangs + ancres de
//  chaque boucle : cf. TerrainLayerRank() en tête de WorldGeometryRenderer.cpp. En résumé :
//      a5=1 : cat2 (tout sub) -> cat4 (tout sub) -> cat1/sub0 -> eau cat3 (gate sub0)
//             -> cat1/sub1 (alpha-test) -> eau cat3 (gate sub1)
//      a5=2 : cat1/sub2 -> eau cat3 (gate sub2)          [z-write OFF + alpha-blend ON]
//      TOUT LE RESTE (cat ∉ {1,2,3,4} ; cat1/sub>=3) n'est dessiné par AUCUNE boucle -> écarté
//      dès buildTerrain (rang -1), avant tout upload GPU : c'était la « géométrie fantôme ».
//  ⚠ Les boucles EAU testent `cat==3` SEUL (aucun test de sub ; seule la gate teste un sub) :
//    une couche cat3 n'est JAMAIS filtrée, quel que soit son subOrder.
//  Corrections de fidélité apportées par ce front : (1,2) et (3,2) passent d'« opaque z-write ON »
//  à la vraie passe a5=2 blendée ; l'alpha-test cesse de frapper (2,1)/(4,1) (il est piloté par le
//  rang, pas par subOrder) ; l'adressage sampler CLAMP/WRAP par couche est posé. Le filtre lui-même
//  n'écarte 0 face sur les 97 .WG réels (domaine mesuré : cat ∈ {1,2,3,4}, sub ∈ {0,1,2}) — c'est
//  un garde-fou de fidélité, pas un correctif visuel.
//  ⚠ « Terrain_PushRenderState 0x69cb80 » est un nom IDA TROMPEUR : ce n'est PAS un push d'états
//    de rendu mais un TIMER QueryPerformanceCounter (renvoie des secondes écoulées ; appelé aussi
//    par App_Init @0x46242e / App_FrameTick @0x4625d9). Il ne valide RIEN de `wavePhase_ * 10.0f`
//    (Passe 4/W5b) : son retour tombe dans un slot MORT (@0x6986b2 `fstp [esp+58h+var_48]`, +0x3a0,
//    1 écriture / 0 lecture), tandis que le `fmul flt_7A8D74` @0x6991ca lit var_3C (+0x3ac, slot
//    distinct) — lequel a 3 lectures / 0 écriture sur toute Terrain_Render 0x698670, donc lu NON
//    INITIALISÉ. Seul le facteur 10.0f (flt_7A8D74 = 0x41200000) est prouvé ; `wavePhase_` ≡
//    « secondes écoulées » est un choix build-safe NON PROUVÉ. Détail complet dans le .cpp.
//
//  RESTES (TODO ancres) : cull quadtree/frustum par frame ; sélection GPU de la frame de sway
//  (MeshPart_Render 0x6aed60, uploadPart n'uploade que la frame 0) ; sim complet de particules
//  (Particle_UpdateEmit 0x6a7530) ; eau des 5 zones « mixtes » dessinée une fois au lieu de deux
//  (cf. bandeau de renderTerrain dans le .cpp). Échelle du bump eau : ancre 0x699206 RÉSOLUE
//  (= a10 = distance de tirage Game_GetTierRange 0x5402f0, très probablement un bug d'origine)
//  mais volontairement NON reproduite — cf. bindWaterStates(). Skybox/atmosphère = FRONT W3-F4
//  (SkyRenderer), hors périmètre ici.
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

    // FRONT W3-F3 — passe FX de zone (.WP) : billboards unlit (Gfx_BeginUnlitPass 0x69e470 ->
    // Particle_RenderBillboards 0x6a70b0), correspond à Terrain_Render a5=2 @0x698c6d (le point
    // d'entrée de rendu .WP EST là — corrige WorldIntegration « point non identifié »). Appelée par
    // Render() APRÈS le terrain et les props .WO (blend actif, depth-write off). Camera-facing via
    // la matrice vue.
    void RenderFxBillboards(const Camera& camera);

    // FRONT W3-F3 — tick d'animation du monde (à appeler par SceneManager chaque frame, cf.
    // MapColl_UpdateObjectAnim 0x694A00, site Scene_InGameUpdate 0x52c94b, kAnimFps=15.0) :
    //   - wavePhase_ += dt (matrice bump-env eau) ;
    //   - phase de flipbook sway par instance .WO (aux+28 += dt*15, wrap par nb de frames A) ;
    //   - tick des systèmes de particules .WP (données prêtes ; sim complet = TODO ancre).
    // SceneManager (non possédé) doit l'appeler ; commentaire d'intégration seulement, pas d'édition.
    void TickWorldAnim(float dt);

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

    // --- Terrain .WG (FRONT W3-F3, ancre IDA : Terrain_Render 0x698670) : logs de sanité. ---
    // Nombre de couches GPU terrain (groupées par matériau, triées par catégorie/subOrder) et total
    // de faces terrain (3 sommets/face, 120o/face copiés @0x698e21).
    size_t TerrainBatchCount() const { return terrainLayers_.size(); }
    size_t TerrainFaceCount()  const { return terrainFaceCount_; }
    // Billboards FX de zone (.WP) prêts à dessiner (1 par instance placée à texture résolue).
    size_t FxBillboardCount()  const { return fxBillboards_.size(); }

private:
    struct StaticObject {
        SkinnedMesh mesh; // un seul LOD (le .WO n'a pas de niveaux de LOD multiples)
    };

    // Plage [start, start+count) dans objects_ des parts uploadées d'un gabarit models[i].
    struct ModelRange {
        size_t start = 0;
        size_t count = 0;
    };

    // FRONT W3-F3 — chemin terrain FIXED-FUNCTION natif (remplace le chemin skinné G1).
    // FVF 0x212 = 530 = D3DFVF_XYZ|NORMAL|TEX2 (2 jeux d'UV), stride 40. Ancre IDA :
    // Terrain_Render 0x698670 SetFVF(530) @0x698e6d. Le vertex est BIT-À-BIT asset::TerrainVertex
    // (pos12+normal12+uv0 8+uv1 8) -> uploadé par memcpy, aucune conversion (uv1 = lightmap stage 1).
    struct FfTerrainVertex { float pos[3]; float normal[3]; float uv0[2]; float uv1[2]; };
    static_assert(sizeof(FfTerrainVertex) == 40, "FfTerrainVertex doit faire 40 octets (FVF 530)");

    // Un LOD terrain FF : VB natif (stride 40) + IB16 (index séquentiels, comme le DrawPrimitiveUP
    // d'origine). Découpe <=65535 sommets conservée (INDEX16).
    struct FfLod {
        IDirect3DVertexBuffer9* vb = nullptr;
        IDirect3DIndexBuffer9*  ib = nullptr;
        UINT                    vertexCount = 0;
        UINT                    faceCount   = 0;
    };

    // Couche terrain = faces d'UN matériau, étiquetée par (catégorie, subOrder) =
    // textures[m].trailer[0]/trailer[1] (prouvé Tex_LoadCompressedFromHandle 0x6a9cf0 : mat+40=cat,
    // mat+44=subOrder). L'ordre de dessin de Terrain_Render (passes a5=1 PUIS a5=2) est reproduit en
    // triant les couches par `rank` (cf. TerrainLayerRank dans le .cpp, table complète + ancres).
    // Catégorie 3 = EAU (passe bump-env). Seules les couches de rang >= 0 existent ici : celles que
    // le binaire ne dessine jamais sont écartées dès buildTerrain (pas de VB/IB créé).
    struct TerrainLayer {
        IDirect3DTexture9* diffuse  = nullptr; // réf dans terrainTextures_ (NON possédée)
        uint32_t           category = 0;       // trailer[0]
        uint32_t           subOrder = 0;       // trailer[1]
        int                rank     = 0;       // TerrainLayerRank(category, subOrder), toujours >= 0
        std::vector<FfLod> lods;               // VB/IB possédés par la couche
    };

    // Billboard FX de zone (.WP) — SOUS-ENSEMBLE build-safe de Particle_RenderBillboards 0x6a70b0 :
    // 1 quad camera-facing par instance placée AuxFxRecord, à sa position, texture du nœud FX.
    // (Le sim complet de particules — Particle_Init/UpdateEmit + base flt_8001D4..E8 runtime — reste
    // un TODO ancre, cf. RenderFxBillboards() dans le .cpp.)
    struct FxBillboard {
        float              pos[3]  = {0, 0, 0};
        IDirect3DTexture9* texture = nullptr;  // réf dans fxTextures_ (NON possédée)
    };

    void releaseObjects();
    bool uploadPart(const asset::WorldMeshPart& part, StaticObject& out);
    // FRONT W3-F3 : construit les couches FF du terrain .WG (WorldAssets::Faces()) — faces groupées
    // par matériau, triées par (catégorie=trailer[0], subOrder=trailer[1]), sommets FfTerrainVertex
    // 40o (repère MONDE). Crée aussi wave/falloff (eau) et récupère la lightmap. Build-safe : no-op
    // si device/.WG absent. Ancre IDA : Terrain_Render 0x698670.
    bool buildTerrain(const world::WorldAssets& assets);
    void releaseTerrain();
    // Dessine les couches terrain en FIXED-FUNCTION (FVF 530), ordre fidèle à Terrain_Render(a5=1) :
    // couches opaques par (cat,sub), passe eau bump-env (cat 3), alpha-test (sub 1), lightmap stage 1
    // (uv1). CULLMODE=NONE encadré. Appelée par Render() AVANT les .WO. Ancre IDA : 0x698670.
    void renderTerrain(const D3DXMATRIX& view, const D3DXMATRIX& proj);
    // Passe eau d'UNE couche cat==3 : matrice bump-env animée (cos/sin de wavePhase_) + falloffTex_ en
    // BUMPENVMAPLUMINANCE stage 0 (TWS-01), diffuse eau stage 1. Ancre IDA : Terrain_Render @0x699206-0x6992b7.
    void bindWaterStates(IDirect3DTexture9* waterDiffuse);
    void unbindWaterStates();

    // FRONT W3-F3 : construit les billboards FX de zone (.WP) + leurs textures GPU depuis
    // WorldAssets::FxNodes(). Build-safe : no-op si device/.WP absent. Ancre IDA : MapColl_LoadObjectsB
    // 0x6983b0 (fxbRecords) + Fx_NodeLoadFromHandle 0x6a69f0 (texture du nœud).
    bool buildFx(const world::WorldAssets& assets);
    void releaseFx();

    static IDirect3DTexture9* createTextureFromBlock(IDirect3DDevice9* dev,
                                                      const asset::TextureBlock& tex);
    // Crée une IDirect3DTexture9 depuis un fichier DDS complet en mémoire (lightmap .SHADOW brute
    // exposée par WorldAssets::ShadowBytes()). nullptr si vide/échec.
    static IDirect3DTexture9* createTextureFromDds(IDirect3DDevice9* dev,
                                                   const std::vector<uint8_t>& dds);
    // Construit World = Rz(rot.z)*Ry(rot.y)*Rx(rot.x)*T(pos), cf. bandeau .h point 4.
    static D3DXMATRIX BuildInstanceWorldMatrix(const asset::AuxRecord& inst);

    IDirect3DDevice9*           dev_ = nullptr;
    MeshRenderer                 meshRenderer_;
    SkyRenderer                  skyRenderer_;  // ciel dérivé du .ATM réel (cf. bandeau MISE À JOUR)
    // FRONT W3-F3 : ces 3 états (objects_/modelRanges_/instances_) sont LA SOURCE (auxRecords/models)
    // consommée par l'avance de phase de sway (instancePhase_ ci-dessous, tickée par TickWorldAnim).
    // Ancre IDA : MapColl_UpdateObjectAnim 0x694a00 (site Scene_InGameUpdate 0x52c94b, kAnimFps=15.0).
    std::vector<StaticObject>    objects_;      // parts GPU uploadées, groupées par gabarit
    std::vector<ModelRange>      modelRanges_;  // modelRanges_[modelIndex] -> plage dans objects_
    std::vector<asset::AuxRecord> instances_;   // copie de ObjectChunk::auxRecords ; CONFIRMED ex-VeryOldClient: MOBJECTINFO
    // Phase de flipbook de sway PAR INSTANCE .WO (aux+28 runtime), état possédé ici (RÈGLE #6) :
    // avancée à dt*kAnimFps par TickWorldAnim, wrap par le nb de frames A du gabarit. Ancre IDA :
    // MapColl_UpdateObjectAnim 0x694a00 (@0x694a30 aux+28 += dt*fps ; wrap par model.part.frameCount).
    std::vector<float>           instancePhase_;
    // Nombre de frames de flipbook (A) du gabarit de chaque instance (borne de wrap du sway).
    // Ancre IDA : MapColl_UpdateObjectAnim @0x694a4a (frameCount = *(model.part+252) = part.A).
    std::vector<uint32_t>        instanceFrameCount_;
    size_t                       skippedMultiAnchor_ = 0;     // parts ignorées (échec réel, cf. pt.5)
    size_t                       multiAnchorStaticCount_ = 0; // parts A>1 rendues en pose statique

    // --- Terrain .WG (FRONT W3-F3) : le SOL. Source = WorldAssets::Faces() (asset::MapFaceChunk).
    // terrainTextures_ = une texture diffuse par matériau (POSSÉDÉE) ; terrainLayers_ = couches FF
    // triées par (catégorie, subOrder), chacune référençant une diffuse. Ancre IDA : Terrain_Render
    // 0x698670 (a1+16 matériaux stride 52 : +40 cat, +44 subOrder, +48 texture ; vertex 40o FVF 530). ---
    std::vector<IDirect3DTexture9*> terrainTextures_; // POSSÉDÉES (une par matériau, ordre .WG)
    std::vector<TerrainLayer>       terrainLayers_;    // couches triées prêtes à dessiner (FF)
    size_t                          terrainFaceCount_ = 0; // total faces terrain uploadées (sanité)

    // Eau (cat 3) : texture procédurale générée UNE FOIS au Build si une couche cat==3 existe.
    // TWS-01/TWS-02 : falloffTex_ = V8U8 256x256 radial, port de MapColl_CreateFalloffTexture 0x694ca0
    // (seul écrivain vivant de *(a1+20), le bump-map stage 0 lié @0x69928f). Il n'y a PAS de waveTex_ :
    // son générateur cWorldMesh_MakeWaterWaveTexture 0x451220 a 2 appelants à 0 xref (code mort) et le
    // binaire ne crée jamais de texture de vagues. wavePhase_ = accumulateur temps (t = wavePhase_*10).
    IDirect3DTexture9*           falloffTex_ = nullptr;
    float                        wavePhase_  = 0.0f;

    // Lightmap .SHADOW (stage 1, uv1) — texture GPU créée au Build depuis WorldAssets::ShadowBytes().
    // Ancre IDA : Terrain_Render @0x698f54 (SetTextureStageState(1,COLOROP,MODULATE=4)) / @0x698f68.
    IDirect3DTexture9*           shadowTex_ = nullptr;

    // FX de zone (.WP) : billboards placés + textures GPU des nœuds FX (POSSÉDÉES).
    std::vector<FxBillboard>        fxBillboards_;
    std::vector<IDirect3DTexture9*> fxTextures_; // POSSÉDÉES (une par nœud FX, nullptr si absente)

    bool                         ready_ = false;
};

} // namespace ts2::gfx

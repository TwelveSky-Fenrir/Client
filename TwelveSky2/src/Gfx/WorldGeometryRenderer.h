// Gfx/WorldGeometryRenderer.h — real drawing of the .WO chunk's static objects (milestone 4->5,
// the "world geometry" companion of Scene/WorldRenderer.h, which draws the ENTITIES).
//
// This file WIRES three already-written, until-now unconnected modules:
//   World/WorldIntegration.h — REALLY loads Z%03d.WO (world::WorldAssets::Objects()).
//   Asset/WorldChunk.h       — .WO parser (asset::ObjectChunk -> asset::Model -> WorldMeshPart),
//                              validated EOF-exact on 455/455 D07_GWORLD files.
//   Gfx/MeshRenderer.h       — milestone 4's skinned GPU pipeline (VB/IB 76o + bone palette),
//                              reused AS-IS here (no modification to MeshRenderer.*) via its
//                              PUBLIC API DrawSkinnedSubset().
//
// PLACEMENT FORMAT — RESOLVED by IDA disassembly, cf. Docs/TS2_WO_PLACEMENT_FORMAT.md
// (Corrects the previous banner, written without IDA/x32dbg access — the "identity matrix,
// placement unrecoverable" hypothesis was WRONG: placement exists, it is simply carried by
// `ObjectChunk::auxRecords`/`models[]`, not by `placements[]`.)
//
// 1) `ObjectChunk::models[]` (Asset/WorldChunk.h) = mesh TEMPLATES, in LOCAL/OBJECT space —
//    NOT final world coordinates. Each uploaded ONCE to GPU VB/IB (cf. uploadPart()/Build():
//    geometry unchanged from the previous version, only the placement interpretation changes).
//
// 2) `ObjectChunk::auxRecords[]` (28 bytes/instance on disk, now typed `asset::AuxRecord` in
//    WorldChunk.h) = THE PLACED INSTANCES: each entry references a template via `modelIndex`
//    and carries its OWN position/rotation. Confirmed by disassembling `Model_RenderParts`
//    (0x6A3720) and `Model_RenderWithShadow_0` (0x6A4110), the array's consumers in the main
//    scene render (`Terrain_Render` 0x698670). 50,413 instances verified across 97 real files,
//    up to 643 reuses of the same template — direct proof that `models[]` is in local space
//    (otherwise instances would overlap).
//
// 3) `ObjectChunk::placements[]` (100 bytes/TEMPLATE, not per instance) = metadata: NUL-
//    terminated source filename (+ memory padding not read by the engine, except the
//    "NO_SHADOW_" tag used by `MapColl_RenderShadowMap`). Exposed for debug via
//    `ObjectChunk::placementNames`, IGNORED for placement (it is not a transform).
//
// 4) Per-instance world matrix (disassembly `0x6a379c`-`0x6a3892` / `0x6a41a3`-`0x6a4299`,
//    IDENTICAL in both consumer functions):
//      World = Rz(rot.z°) * Ry(rot.y°) * Rx(rot.x°) * T(pos)
//    No scale matrix anywhere (scale = 1.0 always for .WO).
//
// 5) [RESOLVED 2026-07-14, "multi-anchor A>1 parts" mission] The "bone" block at the head of
//    the geometry (64 bytes * `A`, cf. WorldMeshPart::A) is NOT a skinning matrix palette: `A`
//    is the FRAME count of a flipbook of precomputed positions (likely wind sway of trees/
//    objects), confirmed by disassembly:
//      - `MeshPart_Load` (0x6AD169) copies `32*A*B` bytes — A consecutive blocks of B 32-byte
//        vertices (MOBJ, position+normal+uv) — VERBATIM into a single IDirect3DVertexBuffer9,
//        never reading the "bone" block (64*A bytes, this+284) to build a bone palette: that
//        block is loaded/freed (MeshPart_Free 0x6AB1F0) but read by NO render-path consumer —
//        likely animation metadata (sway parameters), not skinning in the MeshRenderer.h sense.
//      - `MeshPart_Render` (0x6AED60) selects the frame via `SetStreamSource(0, vb,
//        32*frame*B, 32)` where `frame = Crt_Dbl2Uint(time)` (truncated, clamped to `[0, A-1]`
//        by `Model_RenderParts` 0x6A3720/0x6A3756): a RAW SWAP of the whole frame, NOT GPU
//        interpolation/blending. The index buffer (6*D bytes) is SHARED by all frames (fixed
//        topology, only positions/normals vary).
//    THIS RENDERER NOW DECODES ALL of `A` (no more skipping) but stays STATIC: only FRAME 0 of
//    the flipbook is uploaded (pinned pose, cf. uploadPart()/Gfx/WorldGeometryRenderer.cpp) —
//    NOT real wind sway (would require replaying frames in real time via a dedicated VS or a
//    per-frame stream-source swap, out of scope for this mission). Exposed counter:
//    `MultiAnchorStaticCount()` (A>1 parts actually drawn in a static pose).
//
// 6) Vertex format USED (unchanged, cf. Docs/TS2_ASSET_FORMATS.md "MobjVertex 32 bytes, FVF
//    0x112" = D3DFVF_XYZ|NORMAL|TEX1):
//      +0  float3 position   (template-LOCAL coordinates, cf. point 1 — NOT final)
//      +12 float3 normal     (unit, verified)
//      +24 float2 uv
//    No BLENDWEIGHT/BLENDINDICES in this on-disk format (unlike MeshRenderer.h's 76-byte
//    SObject vertex): converted on the fly to gfx::GpuSkinVertex with weight (1,0,0,0) / bone
//    index 0 -> reuses MeshRenderer's skinned pipeline WITHOUT modifying it (the VS ignores
//    tangent/binormal, never read by kSkinnedVS — so zeroing them loses nothing).
//
// WHAT IS THEREFORE ACTUALLY DRAWN: the geometry (VB/IB/tex1 diffuse texture) of EVERY
// WorldMeshPart (ALL of `A`, cf. point 5 UPDATED 2026-07-14 — no longer limited to A==1) of a
// loaded .WO chunk, once per `auxRecords[]` INSTANCE, at the world matrix `Rz*Ry*Rx*T` built
// from that instance's own position/rotation (cf. point 2/4 above). For `A>1` parts, only
// FRAME 0 of the sway flipbook is drawn (pinned static pose).
// WHAT IS NOT (documented TODO, not simulated):
//   - Real wind sway of multi-anchor parts (A>1, trees/objects): format decoded (cf. point 5)
//     but temporal replay not wired — pinned at frame 0 (exposed counter
//     MultiAnchorStaticCount()), no interpolation/sway shader.
//   - Secondary materials (tex2/materials[]): only tex1 is used as diffuse.
//   - WM/WJ collision (invisible) and WP FX (particle nodes): out of scope for this file.
//   - Frustum culling / LOD / per-texture batching: none (flat per-instance loop).
//   - "NO_SHADOW_" tag (placements[]): no shadow map here, so no visible effect.
//
// AUDIT 2026-07-14 — SKY / ATMOSPHERE / WATER / ZONE FX: INVENTORY OF WHAT IS MISSING
//  (mission "audit of missing sky/atmosphere/zone-fx rendering" — IDA disassembly via
//  idaTs2, addresses = imagebase 0x400000). FINDING: the .WO geometry above is currently the
//  ONLY scenery content drawn by ClientSource in InGame — everything below is 100% ABSENT,
//  confirmed by exhaustive grep in src/ AND by IDA xrefs on the real entry points. Nothing
//  below is a guess.
//
//  1) SIMPLE SKYBOX — Env_RenderSkyCube (0x6a8f60), called by Gfx_BeginFrame (0x6a2280) at the
//     very start of every scene's frame (Intro/ServerSelect/Login/CharSelect/EnterWorld/InGame,
//     8 xrefs), guarded by `a2 && *a2` (an unidentified "sky active" flag). Decompiled: a
//     6-face textured cube (4 verts/face, 20B/vertex = XYZ+TEX1, no vertex color), centered on
//     the camera (translation = g_CameraPos, classic skybox technique), lighting off, culling
//     inverted during draw (dword_7FFEA4 gates D3DRS_CULLMODE). ABSENT from ClientSource: no
//     sky cube/texture, no equivalent call.
//  2) FULL ATMOSPHERE (SilverLining SDK, ~150 `cAtmosphere_*`/`SL_*`/`Sky_*`/`Cloud_*`/
//     `AtmoFromSpace_*` functions, all present/analyzed in the IDB, e.g. 0x793390-0x797920) —
//     real pipeline confirmed in Scene_InGameRender (0x52D0B0, 20KB of pseudocode):
//       a) Env_UpdateFrame (0x412550, called at offset 0x25d, very early in the frame) =
//          Env_UpdateSkyMatrix(0x412190) [pushes view/proj into
//          cAtmosphere_SetModelviewMatrix/SetProjectionMatrix] + cAtmosphere_RenderFrame
//          (0x793b80, internally calls cAtmosphere_DrawObjects 0x792a60) + directional
//          light/fog update (Env_UpdateSunLight/Env_UpdateFogState).
//       b) Env_StepTimeOfDay (0x412590, called at offset 0x2a99, AFTER the 2 Terrain_Render
//          passes / 3 Fx_EmitterDraw passes below) = Atmosphere_DrawFrame (0x794fe0, 0x813
//          bytes): the REAL draw of the sky dome/sun/moon/stars/volumetric clouds
//          (Sky_RenderStars 0x74b030, Cloud_UpdateAndRender 0x702ff0, etc.).
//     ABSENT from ClientSource: World/WorldIntegration.h already honestly documents this in
//     its header banner ("SilverLining atmosphere/weather: external SDK
//     SilverLiningDirectX9-MT.dll not linked -> LoadMap() fails cleanly") — this file CONFIRMS
//     by disassembly that the original client does massively use the SDK (not dead vestige),
//     so the gap is REAL and MAJOR, not just WorldIntegration.h's excess caution.
//  3) VISIBLE TERRAIN/GROUND + WATER — Terrain_Render (0x698670, original IDA comment: "render
//     quadtree terrain tile/water/land layers with reflections"), called 2x/frame from
//     Scene_InGameRender (offsets 0x90e and 0x1a28 — i.e. BEFORE the .WO objects in the
//     original pipeline). Consumes the .WG file (Z%03d.WG, G3W/WM² quadtree format, loaded by
//     MapColl_LoadMapFile 0x697b30; TWS-01: the water bump-map is the FALLOFF texture from
//     MapColl_CreateFalloffTexture 0x694ca0 — cWorldMesh_MakeWaterWaveTexture 0x451220 is DEAD
//     code, never called by the shipped binary). CRITICAL FINDING: Asset/WorldChunk.cpp PARSES
//     this .WG file (asset::WorldChunk::AsFace(), exposed by
//     World/WorldIntegration.h::WorldAssets::Faces()) but NO ClientSource/ file ever consumes
//     Faces()/AsFace() (exhaustive grep, 0 hits outside Asset/ and WorldIntegration.h): the
//     GROUND/TERRAIN ITSELF (not just the sky) is drawn NOWHERE. Visible consequence: this
//     file's .WO objects currently float with no ground surface beneath them — only the
//     background (Renderer::clearColor_, now pure black 0x00000000) stands in as "ground". The
//     dedicated reflection pass (cWorldMesh_RenderReflection 0x450f50) has 0 xrefs in the
//     original binary itself (confirmed via xrefs_to) — dead code on TS2's own side, so NOT a
//     gap introduced here.
//  4) CHARACTER/ZONE FX — Fx_EmitterDraw (0x585e30, cf. Docs/TS2_FX_CATALOG.md), called
//     3x/frame from Scene_InGameRender (offsets 0xc64/0x1c3b/0x2a28 — probably
//     shadow/color/sound passes): render dispatch of the 26 `Fx_Attach*` slots (weapon glow,
//     hit spark, skill aura, parry, dash...) + the projectile pool
//     (Fx_HomingProjectileUpdate 0x5862d0). ABSENT from ClientSource: Game/
//     GroundAuraWorldObjectTick.h/.cpp faithfully reproduces the data TICK (positions, timers,
//     SoA projectile pool) but there is NO GPU counterpart (no Gfx/Fx*.h, no call actually
//     drawing any of these effects on screen) — the data advances, nothing is drawn.
//  5) AMBIENT ZONE-PLACED FX (.WP file — likely torches/fires/waterfalls, distinct from the
//     character Fx_Attach*) — Z%03d.WP loaded via MapColl_LoadObjectsB (0x6983b0), which calls
//     Fx_NodeLoadFromHandle (0x6a69f0, "Node" format documented in Docs/TS2_FX_CATALOG.md
//     §4.3). Asset/WorldChunk.cpp PARSES this file (asset::WorldChunk, exposed by
//     WorldAssets::FxNodes()) but, just like terrain (point 3), NO file ever consumes
//     FxNodes() (exhaustive grep). The real RENDER entry point for these nodes in the
//     original binary was NOT found in this audit pass (no `MapColl_RenderObjects*` function;
//     probably shares the Model_RenderParts/Fx_EmitterDraw pipeline via a table not identified
//     here) — left as an open RE mystery, NOT invented.
//
//  SUMMARY: of 5 systems (simple skybox, SilverLining atmosphere, terrain+water, character
//  FX, zone FX), NONE has a render counterpart in ClientSource — only the .WO geometry
//  (static props) and the entities (Scene/WorldRenderer.h) are drawn.
//  ORIGINAL FIX (2026-07-14, this banner): a SIMPLIFIED SKY GRADIENT FALLBACK — NOT real
//  SilverLining, no textured skybox, just a 2-fixed-color fullscreen quad drawn before the
//  scenery. Ground/terrain/water/FX remain untreated TODOs here (out of scope: they'd need,
//  respectively, the already-written but never-wired .WG parser, and a new Gfx/Fx*.h module
//  that does not exist yet).
//
//  UPDATE 2026-07-15 (WAVE_06_silverlining): the 2-fixed-color quad above is REPLACED by
//  Gfx/SkyRenderer.h, whose colors are now DERIVED from the active zone's REAL .ATM file
//  (Asset/AtmosphereFile.h, parsed byte-exact by
//  World/WorldIntegration.h::WorldAssets::Atmosphere() — World_LoadZoneResource case 7,
//  already handled by World/WorldMap.h, now really wired on the data side) and from the
//  global SilverLining.config loaded at startup. Still an honest first step, NOT the full
//  SilverLining SDK (no cloud/precipitation/star/sun/moon drawn — cf. the full banner in
//  SkyRenderer.h). Build() now passes assets.SilverLining() and assets.Atmosphere() to the
//  sky; SceneManager places RenderSky() before/after the world.
//
//  AUDIT 2026-07-15 (re-read of SkyRenderer.cpp + WorldGeometryRenderer.cpp) — BUG FIXED:
//  SkyRenderer::Render() sets SetVertexShader(nullptr)/SetPixelShader(nullptr) directly on
//  the D3D9 device SHARED with meshRenderer_. But MeshRenderer::DrawSkinnedSubset() bypasses
//  the VS/PS re-bind via a purely instance-local cache (`currentPass_`), which ignores these
//  external device pokes: without a fix, the cache could keep a skinned shader bound after the
//  sky pass. Fixed by adding MeshRenderer::InvalidateShaderBindingCache() (Gfx/MeshRenderer.h),
//  called in Render() after every sky pass and before any mesh draw. The rest of both files
//  (SkyRenderer.cpp/.h and WorldGeometryRenderer.cpp/.h) was fully re-read: no other
//  undocumented placeholder found (A/B/C/D geometry offset formulas, MOBJ->GPU vertex
//  conversion, Rz*Ry*Rx*T matrix, VB/IB sizes all match Asset/WorldChunk.h and the existing
//  IDA banners); remaining gaps (real sway, tex2/materials, terrain/water, FX, real .ATM
//  ephemeris/keyframes) were already honestly documented before this audit.
//
//  UPDATE 2026-07-16 (FRONT W3-F3 — dedicated FIXED-FUNCTION terrain path): terrain rendering now
//  goes through a NATIVE FF path (no more meshRenderer_/shaders), faithful to Terrain_Render
//  0x698670. buildTerrain()/renderTerrain() (cf. .cpp):
//    - FVF 530 VB (0x212 = XYZ|NORMAL|TEX2, stride 40) uploaded via memcpy from asset::TerrainVertex
//      (no conversion) — SetFVF(530) @0x698e6d;
//    - layers grouped by material and SORTED by a RANK derived from (category=trailer[0],
//      subOrder=trailer[1]) = textures[m].trailer[*] (proven via Tex_LoadCompressedFromHandle
//      0x6a9cf0; ex-VeryOldClient TEXTURE_FOR_GXD: trailer = processMode/alphaMode);
//    - .SHADOW LIGHTMAP on stage 1 over uv1: MODULATE (=4, NOT MODULATE2X — comment corrected)
//      @0x698f54 + SetTexture(1) @0x698f68 (the FF vertex does have uv1 -> the "single TEXCOORD"
//      TODO is GONE; texture created from WorldAssets::ShadowBytes());
//    - WATER (category 3): bump-env pass D3DTOP_BUMPENVMAPLUMINANCE @0x699206 with an animated
//      bump matrix (wavePhase_) + falloffTex_ (MapColl_CreateFalloffTexture 0x694ca0), procedural
//      V8U8 256x256 generated at Build. TWS-01: it really is the FALLOFF (*(a1+20) @0x69928f), not
//      a wave texture — 0x451220 is dead and not ported.
//  .WP zone FX: RenderFxBillboards() (pass a5=2 @0x698c6d: Gfx_BeginUnlitPass 0x69e470 ->
//  Particle_RenderBillboards 0x6a70b0) draws 1 placed billboard per instance (build-safe
//  subset). .WO sway: TickWorldAnim() advances the per-instance flipbook phase (owned state,
//  MapColl_UpdateObjectAnim 0x694A00).
//
//  UPDATE Pass 4 / W5 (terrain-motion front) — EXACT LIST OF DRAWN CATEGORIES. Terrain_Render has
//  only 2 passes (guard @0x698676-0x6986a2: a5 in [1,2]; call sites Scene_InGameRender @0x52d9be
//  a5=1 and @0x52ead8 a5=2). Full rank table + per-loop anchors: cf. TerrainLayerRank() at the top
//  of WorldGeometryRenderer.cpp. Summary:
//      a5=1: cat2 (any sub) -> cat4 (any sub) -> cat1/sub0 -> water cat3 (sub0 gate)
//            -> cat1/sub1 (alpha-test) -> water cat3 (sub1 gate)
//      a5=2: cat1/sub2 -> water cat3 (sub2 gate)          [z-write OFF + alpha-blend ON]
//      EVERYTHING ELSE (cat not in {1,2,3,4}; cat1/sub>=3) is drawn by NO loop -> discarded in
//      buildTerrain (rank -1) before any GPU upload: this was the "ghost geometry".
//  Water loops test `cat==3` ALONE (no sub test; only the gate checks a sub): a cat3 layer is
//  NEVER filtered, whatever its subOrder.
//  Fidelity fixes from this front: (1,2) and (3,2) move from "opaque z-write ON" to the real
//  blended a5=2 pass; alpha-test stops hitting (2,1)/(4,1) (driven by rank, not subOrder); per-
//  layer CLAMP/WRAP sampler addressing is set. The filter itself discards 0 faces on the 97 real
//  .WG files (measured domain: cat in {1,2,3,4}, sub in {0,1,2}) — a fidelity safeguard, not a
//  visual fix.
//  "Terrain_PushRenderState 0x69cb80" is a MISLEADING IDA name: it is NOT a render-state push but
//  a QueryPerformanceCounter TIMER (returns elapsed seconds; also called by App_Init @0x46242e /
//  App_FrameTick @0x4625d9). It validates NOTHING about `wavePhase_ * 10.0f` (Pass 4/W5b): its
//  return lands in a DEAD slot (@0x6986b2 `fstp [esp+58h+var_48]`, +0x3a0, 1 write / 0 reads),
//  while `fmul flt_7A8D74` @0x6991ca reads var_3C (+0x3ac, a distinct slot) — which has 3 reads /
//  0 writes across all of Terrain_Render 0x698670, hence read UNINITIALIZED. Only the 10.0f factor
//  (flt_7A8D74 = 0x41200000) is proven; `wavePhase_` as "elapsed seconds" is an UNPROVEN build-safe
//  choice. Full detail in the .cpp.
//
//  UPDATE FRONT F_TERRAIN (B5 + B6, 2026-07-17) — two TODO anchors RESOLVED:
//   - B5: per-frame terrain quadtree/frustum cull (cullTerrain() in the .cpp) — descent via
//     MapColl_CollectLeafFaces 0x694b50 (per-node AABB test Cam_FrustumTestAABB 0x69f230), then
//     per face: dedup, backface dot(planeN,eye)>=planeD @0x698dd4, doubled-margin sphere
//     Cam_FrustumTestSphere2x 0x69f0e0, 120-byte batch per material @0x698e21 -> DrawPrimitiveUP
//     @0x698ff3. The .WG quadtree was ALREADY decoded (Asset/WorldChunk.h
//     CollisionMesh::nodes/triIndices): reused as-is, no WorldChunk changes. Same visible faces,
//     fewer triangles.
//   - B6: .WO SWAY replay — uploadPart NOW uploads the A frames (native 32*A*B bytes, FVF 0x112)
//     and renderObjects() selects the frame via SetStreamSource(0, vb, 32*frame*B, 32)
//     (MeshPart_Render 0x6aeea5), frame = Crt_Dbl2Uint(phase) gated [0, A-1] (Model_RenderParts
//     0x6a3756). The .WO path moves from skinned (76 bytes) to native FIXED-FUNCTION (faithful to
//     the binary, which is FF).
//
//  UPDATE FRONT F_ZONEFX (2026-07-17) — LIVE .WP PARTICLE SIM: the degenerate static billboard is
//  replaced by the real "Object A" engine (Gfx/ZoneFxEmitter.h) — a 232-byte template per node
//  (Fx_NodeLoadFromHandle 0x6a69f0), a 48-byte POBJECT pool per placed instance, Init/UpdateEmit
//  (6-shape switch) in TickWorldAnim (loop 2 of MapColl_UpdateObjectAnim 0x694a00), rendered via
//  Particle_RenderBillboards 0x6a70b0 in RenderFxBillboards with a squared-distance cull per node
//  (Terrain_Render a5=2 @0x698c81; range Game_GetTierRange 0x5402f0 = 3000 documented default).
//  Not ported: the emitter transform keyframe path (template+56, quaternion track) -> frame matrix
//  = identity; the original's Cam_FrustumTestPoint6 cull (nullptr, identical on-screen visual).
//
//  REMAINING (TODO anchors): .WP emitter keyframe (0x6a787d); the 5 "mixed" zones' water drawn
//  once instead of twice (cf. renderTerrain's banner in the .cpp); water bump scale: anchor
//  0x699206 RESOLVED (= a10 = draw-distance tier Game_GetTierRange 0x5402f0, very likely an
//  original bug) but deliberately NOT reproduced — cf. bindWaterStates(); DISTANCE cull + alpha
//  fade for far .WO props (Terrain_Render 0x69977c/0x69979f -> Model_RenderWithShadow_0 0x6a4110)
//  NOT reproduced (a9/a10/a11 call-site values unavailable statically, cf. renderObjects()).
//  Skybox/atmosphere = FRONT W3-F4 (SkyRenderer), out of scope here.
#pragma once
#include "Gfx/Renderer.h"
#include "Gfx/MeshRenderer.h"
#include "Gfx/Camera.h"
#include "Gfx/SkyRenderer.h" // sky derived from the real .ATM file (cf. 2026-07-14 update above)
#include "Gfx/ZoneFxEmitter.h" // FRONT F_ZONEFX: the .WP "Object A" particle engine (live sim)
#include "Gfx/MeshPartMaterial.h" // FRONT C2: material layer for MeshPart_RenderFull 0x6b0850 (B1) of .WO props
#include "Asset/WorldChunk.h" // asset::AuxRecord: full type required (direct std::vector member)
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

    // Builds its own MeshRenderer (76-byte vertex decl. + skinned shaders) — a dedicated
    // instance, independent from Scene/WorldRenderer.h's (no cross-file coupling, avoids
    // concurrent-edit conflicts).
    bool Init(Renderer& renderer);
    void Shutdown();

    // Converts + uploads the current .WO chunk's WorldMeshPart geometry (ALL of A, cf. banner
    // point 5 above — A>1 parts pinned to frame-0 static pose) into
    // IDirect3DVertexBuffer9/IndexBuffer9. Replaces the previous GPU content (zone change).
    // Returns false only if no WO chunk is loaded in `assets` (a present-but-empty/0-model WO
    // returns true, 0 objects drawn). Also passes assets.Atmosphere() (the zone's real
    // Z%03d.ATM) to skyRenderer_ — cf. 2026-07-14 update: this is WHERE the real-data-derived
    // sky is refreshed on every zone change, independent of whether the .WO load itself
    // succeeds (called before any `return` in this function, cf. .cpp).
    bool Build(const world::WorldAssets& assets);

    void OnDeviceLost();
    void OnDeviceReset();

    // Minimal SilverLining atmosphere pass (sky/atmosphere layer). Can be called before the
    // static objects or after the entities, with depth test active, to respect the original
    // engine's two real frame entry points.
    void RenderSky(int screenW, int screenH);

    // Draws, for each auxRecords[] instance, every uploaded part of the template it references
    // (models[instance.modelIndex]), at the world matrix Rz*Ry*Rx*T built from ITS OWN
    // position/rotation (cf. .h banner point 2/4 — FIXED, replaces the old global identity
    // matrix). The sky layer is driven by RenderSky() so it can be placed both before the
    // scenery and after the entities.
    void Render(const Camera& camera, int screenW, int screenH);

    // FRONT W3-F3 / F_ZONEFX — .WP zone FX pass: unlit billboards (Gfx_BeginUnlitPass 0x69e470
    // -> Particle_RenderBillboards 0x6a70b0), corresponds to Terrain_Render a5=2 @0x698c6d (the
    // .WP render entry point IS there — fixes WorldIntegration's "point not identified").
    // Called by Render() AFTER the terrain and the .WO props (blend active, depth-write off).
    // Camera-facing via the view matrix. FRONT F_ZONEFX: now draws each emitter's REAL particle
    // pool (ZoneFx_RenderBillboards) with a squared-distance cull per node (Terrain_Render
    // 0x698c81), no longer a single degenerate static billboard.
    void RenderFxBillboards(const Camera& camera);

    // FRONT W3-F3 / F_ZONEFX — world animation tick (to be called by SceneManager every frame,
    // cf. MapColl_UpdateObjectAnim 0x694A00, site Scene_InGameUpdate 0x52c94b):
    //   - wavePhase_ += dt (water bump-env matrix);
    //   - per-instance .WO sway flipbook phase (aux+28 += dt*15, wraps by the A frame count);
    //   - FRONT F_ZONEFX: .WP particle sim (loop 2 of MapColl_UpdateObjectAnim @0x694af0:
    //     ZoneFx_Init on the 1st pass, else ZoneFx_UpdateEmit, dt = a3 = true frame delta).
    // SceneManager (not owned here) must call it; integration note only, no edit.
    void TickWorldAnim(float dt);

    size_t UploadedPartCount() const { return objects_.size(); }
    // Parts A>1 actually skipped (size failure/corruption only, cf. uploadPart()) — since the
    // multi-anchor format was resolved (.h banner point 5, 2026-07-14 update), A>1 is no longer
    // a skip cause: see MultiAnchorStaticCount() for those parts.
    size_t SkippedMultiAnchorCount() const { return skippedMultiAnchor_; }
    // Parts A>1 (sway flipbook) drawn successfully. Since B6 (2026-07-17) the flipbook is
    // REPLAYED on the GPU (SetStreamSource(0, vb, 32*frame*B, 32), MeshPart_Render 0x6aeea5):
    // these parts are NO LONGER pinned to frame 0 — the historical "static" name is kept for the
    // API but now means "animated A>1 parts". IDA anchor: Model_RenderParts 0x6a3720.
    size_t MultiAnchorStaticCount() const { return multiAnchorStaticCount_; }
    size_t InstanceCount() const { return instances_.size(); }
    // Number of DrawSkinnedSubset calls Render() will make (instances * uploaded parts of the
    // referenced template) — a sanity log proving the render does use N distinct positions
    // (one per instance) and not a single global matrix.
    size_t PlannedDrawCallCount() const;

    // Terrain .WG (FRONT W3-F3, IDA anchor: Terrain_Render 0x698670): sanity logs.
    // Number of GPU terrain layers (grouped by material, sorted by category/subOrder) and total
    // terrain face count (3 vertices/face, 120 bytes/face copied @0x698e21).
    size_t TerrainBatchCount() const { return terrainLayers_.size(); }
    size_t TerrainFaceCount()  const { return terrainFaceCount_; }
    // Instantiated zone FX (.WP) emitters (1 "Object A" particle pool per placed instance).
    size_t FxBillboardCount()  const { return fxPools_.size(); }
    // FX nodes (232-byte templates) loaded for the zone (sanity).
    size_t FxNodeCount()       const { return fxTemplates_.size(); }

private:
    // FRONT F_TERRAIN (B6, 2026-07-17) — .WO part in native FIXED-FUNCTION (replaces the 76-byte
    // skinned path). The VB holds the CONTIGUOUS A frames of the sway flipbook (32*A*B bytes, raw
    // 32-byte MobjVertex, FVF 0x112): the frame is chosen AT DRAW TIME via SetStreamSource(0, vb,
    // 32*frame*B, 32) — no re-upload, just the offset (cf. .h banner point 5, now REPLAYED, not
    // pinned to frame 0). IDA anchor: MeshPart_Load 0x6ad169 (copies 32*A*B); MeshPart_Render
    // 0x6aed60 (SetStreamSource 0x6aeea5 / SetIndices 0x6aeedc / DrawIndexedPrimitive 0x6aef00);
    // Model_RenderParts 0x6a3720 (frame = Crt_Dbl2Uint(phase), gate [0, A-1], A = MeshPart+252).
    struct StaticObject {
        IDirect3DVertexBuffer9* vb      = nullptr; // 32*A*B bytes (A frames), a1[72]/+288
        IDirect3DIndexBuffer9*  ib      = nullptr; // 6*D bytes (shared by all frames), a1[73]/+292
        IDirect3DTexture9*      diffuse = nullptr; // tex1 = base tex0 (OWNED), a1[86]/+344
        // FRONT C2 (2026-07-17) — B1 MeshPart_RenderFull 0x6B0850 WIRING: beyond diffuse, the part
        // carries its 2nd texture (2nd blended pass, a1[99]/+396), its flipbook atlas (a1[101]/+404),
        // and its DECODED 120-byte material header (part.mat, MeshPart_Load qmemcpy 0x78 @0x6ad2d1).
        // `second` and `flipbook` are OWNED (created at upload, freed in releaseObjects). `mat` is a
        // COPY BY VALUE (the source WorldChunk may be freed on zone change -> no dangling pointer).
        IDirect3DTexture9*      second  = nullptr; // tex2 = 2nd texture (OWNED), a1[99]/+396
        std::vector<IDirect3DTexture9*> flipbook;  // materials[] atlas (OWNED), a1[101]/+404 -> [tex]
        asset::MeshPartMaterial mat;               // decoded 120-byte material header (part.mat) — COPY
        int                     baseMode   = 0;    // part.tex1.mode() = a1[85]/+340 (1=alpha-test/2=blend)
        int                     secondMode = 0;    // part.tex2.mode() = a1[98]/+392 (same)
        uint32_t                A = 1;             // flipbook frame count (swap bound), MeshPart+252
        uint32_t                B = 0;             // vertices per frame, a1[64]/+256 (offset = 32*frame*B)
        uint32_t                D = 0;             // triangle count (primCount), a1[66]/+264
    };

    // [start, start+count) range in objects_ for a models[i] template's uploaded parts.
    struct ModelRange {
        size_t start = 0;
        size_t count = 0;
    };

    // FRONT W3-F3 — native FIXED-FUNCTION terrain path (replaces the G1 skinned path).
    // FVF 0x212 = 530 = D3DFVF_XYZ|NORMAL|TEX2 (2 UV sets), stride 40. IDA anchor:
    // Terrain_Render 0x698670 SetFVF(530) @0x698e6d. The vertex is BIT-FOR-BIT asset::TerrainVertex
    // (pos12+normal12+uv0 8+uv1 8) -> uploaded via memcpy, no conversion (uv1 = lightmap stage 1).
    struct FfTerrainVertex { float pos[3]; float normal[3]; float uv0[2]; float uv1[2]; };
    static_assert(sizeof(FfTerrainVertex) == 40, "FfTerrainVertex must be 40 bytes (FVF 530)");

    // FRONT F_TERRAIN (B5, 2026-07-17) — Terrain layer = faces of ONE material, tagged by
    // (category, subOrder) = textures[m].trailer[0]/trailer[1] (proven via
    // Tex_LoadCompressedFromHandle 0x6a9cf0: mat+40=cat, mat+44=subOrder). Terrain_Render's draw
    // order (pass a5=1 THEN a5=2) is reproduced by sorting layers by `rank` (cf. TerrainLayerRank
    // in the .cpp). Category 3 = WATER (bump-env pass). Only rank >= 0 layers exist here.
    // Since B5, the layer no longer owns a static VB/IB: drawing goes through a PER-FRAME
    // quadtree/frustum CULL that fills a CPU batch (terrainBatch_) then DrawPrimitiveUP per
    // material, EXACTLY like the binary (Terrain_Render 0x698e21 batch / 0x698ff3 DrawPrimitiveUP).
    // `materialIndex` = grouping key (face.materialIndex@0): batch slot = matBase_[m], per-frame
    // cursor = matCounter_[m].
    struct TerrainLayer {
        IDirect3DTexture9* diffuse       = nullptr; // reference into terrainTextures_ (NOT owned)
        uint32_t           category      = 0;       // trailer[0]
        uint32_t           subOrder      = 0;       // trailer[1]
        int                rank          = 0;       // TerrainLayerRank(category, subOrder), >= 0
        uint32_t           materialIndex = 0;       // face.materialIndex@0 -> matBase_/matCounter_ region
    };

    // FRONT F_ZONEFX (2026-07-17) — the degenerate static billboard is REPLACED by the real
    // "Object A" particle engine (Gfx/ZoneFxEmitter.h): one ts2::gfx::FxEmitterTemplate (232 bytes)
    // per FX node, one ts2::gfx::FxParticlePool (POBJECT 48 bytes) per placed AuxFxRecord instance,
    // ticked/rendered by Particle_Init/UpdateEmit/RenderBillboards 0x6a70xx/0x6a75xx. See
    // fxTemplates_/fxPools_ below.

    void releaseObjects();
    bool uploadPart(const asset::WorldMeshPart& part, StaticObject& out);
    // FRONT W3-F3: builds the .WG terrain FF layers (WorldAssets::Faces()) — faces grouped by
    // material, sorted by (category=trailer[0], subOrder=trailer[1]), 40-byte FfTerrainVertex
    // vertices (WORLD space). Also creates the wave/falloff (water) texture and fetches the
    // lightmap. Build-safe: no-op if device/.WG is absent. IDA anchor: Terrain_Render 0x698670.
    bool buildTerrain(const world::WorldAssets& assets);
    void releaseTerrain();
    // Draws the terrain layers in FIXED-FUNCTION (FVF 530), order faithful to Terrain_Render(a5=1):
    // opaque layers by (cat,sub), water bump-env pass (cat 3), alpha-test (sub 1), stage-1 lightmap
    // (uv1). CULLMODE=NONE bracketed. Called by Render() BEFORE the .WO. IDA anchor: 0x698670.
    // FRONT F_TERRAIN (B5): first runs the PER-FRAME quadtree/frustum cull (cullTerrain), which
    // fills terrainBatch_/matCounter_, then draws each layer via DrawPrimitiveUP(count[m]).
    void renderTerrain(const D3DXMATRIX& view, const D3DXMATRIX& proj, const D3DXVECTOR3& eye);
    // FRONT F_TERRAIN (B5) — PER-FRAME terrain quadtree/frustum cull (Terrain_Render pass a5=1,
    // 0x698ce7-0x698e3c): resets flags/counters, descends via MapColl_CollectLeafFaces 0x694b50
    // (per-node AABB frustum test Cam_FrustumTestAABB 0x69f230, leaf = child[0]==-1), then per
    // collected face: dedup (faceSeen_), backface dot(planeN,eye)>=planeD, doubled-margin sphere
    // (Cam_FrustumTestSphere2x 0x69f0e0), 120-byte batch grouped by material (terrainBatch_).
    void cullTerrain(const D3DXMATRIX& view, const D3DXMATRIX& proj, const D3DXVECTOR3& eye);
    // FRONT F_TERRAIN (B6) — draws .WO props in native FIXED-FUNCTION with SWAY REPLAY: per
    // instance, frame = Crt_Dbl2Uint(instancePhase_[i]) gated [0, A-1]; per part,
    // SetStreamSource(0, vb, 32*frame*B, 32) selects the flipbook frame. IDA anchor:
    // Model_RenderParts 0x6a3720 / MeshPart_Render 0x6aed60. Called by Render() AFTER the terrain.
    // FRONT C2 (2026-07-17): for each `mat.decoded` part, the base-draw is REPLACED by the full
    // material layer MeshPartMaterialRenderer::Render (B1, MeshPart_RenderFull 0x6B0850) — real
    // path Model_RenderWithShadow_0 0x6a4110 -> 0x6b0850; `mat.decoded==false` keeps the base-draw.
    // `eye` (g_CameraPos 0x800130) feeds B1's MeshPartRuntime. PRIVATE method: free signature.
    void renderObjects(const D3DXMATRIX& view, const D3DXMATRIX& proj, const D3DXVECTOR3& eye);
    // Water pass for ONE cat==3 layer: animated bump-env matrix (cos/sin of wavePhase_) +
    // falloffTex_ in BUMPENVMAPLUMINANCE stage 0 (TWS-01), water diffuse in stage 1. IDA anchor:
    // Terrain_Render @0x699206-0x6992b7.
    void bindWaterStates(IDirect3DTexture9* waterDiffuse);
    void unbindWaterStates();

    // FRONT W3-F3: builds the .WP zone FX billboards + their GPU textures from
    // WorldAssets::FxNodes(). Build-safe: no-op if device/.WP is absent. IDA anchor:
    // MapColl_LoadObjectsB 0x6983b0 (fxbRecords) + Fx_NodeLoadFromHandle 0x6a69f0 (node texture).
    bool buildFx(const world::WorldAssets& assets);
    void releaseFx();

    static IDirect3DTexture9* createTextureFromBlock(IDirect3DDevice9* dev,
                                                      const asset::TextureBlock& tex);
    // Creates an IDirect3DTexture9 from a complete in-memory DDS file (raw .SHADOW lightmap
    // exposed by WorldAssets::ShadowBytes()). nullptr if empty/failed.
    static IDirect3DTexture9* createTextureFromDds(IDirect3DDevice9* dev,
                                                   const std::vector<uint8_t>& dds);
    // Builds World = Rz(rot.z)*Ry(rot.y)*Rx(rot.x)*T(pos), cf. .h banner point 4.
    static D3DXMATRIX BuildInstanceWorldMatrix(const asset::AuxRecord& inst);

    IDirect3DDevice9*           dev_ = nullptr;
    MeshRenderer                 meshRenderer_;
    SkyRenderer                  skyRenderer_;  // sky derived from the real .ATM file (cf. update banner)
    // FRONT W3-F3: these 3 states (objects_/modelRanges_/instances_) are THE SOURCE (auxRecords/models)
    // consumed by the sway phase advance (instancePhase_ below, ticked by TickWorldAnim).
    // IDA anchor: MapColl_UpdateObjectAnim 0x694a00 (site Scene_InGameUpdate 0x52c94b, kAnimFps=15.0).
    std::vector<StaticObject>    objects_;      // uploaded GPU parts, grouped by template
    std::vector<ModelRange>      modelRanges_;  // modelRanges_[modelIndex] -> range within objects_
    std::vector<asset::AuxRecord> instances_;   // copy of ObjectChunk::auxRecords; CONFIRMED ex-VeryOldClient: MOBJECTINFO
    // PER-INSTANCE .WO sway flipbook phase (aux+28 at runtime), state owned here (RULE #6):
    // advanced by dt*kAnimFps in TickWorldAnim, wrapped by the template's A frame count. IDA
    // anchor: MapColl_UpdateObjectAnim 0x694a00 (@0x694a30 aux+28 += dt*fps; wraps by
    // model.part.frameCount).
    std::vector<float>           instancePhase_;
    // Flipbook frame count (A) of each instance's template (sway wrap bound).
    // IDA anchor: MapColl_UpdateObjectAnim @0x694a4a (frameCount = *(model.part+252) = part.A).
    std::vector<uint32_t>        instanceFrameCount_;
    size_t                       skippedMultiAnchor_ = 0;     // skipped parts (real failure, cf. pt.5)
    size_t                       multiAnchorStaticCount_ = 0; // A>1 parts (sway flipbook REPLAYED, cf. B6)

    // Terrain .WG (FRONT W3-F3): the GROUND. Source = WorldAssets::Faces() (asset::MapFaceChunk).
    // terrainTextures_ = one diffuse texture per material (OWNED); terrainLayers_ = FF layers
    // sorted by (category, subOrder), each referencing a diffuse. IDA anchor: Terrain_Render
    // 0x698670 (a1+16 materials stride 52: +40 cat, +44 subOrder, +48 texture; 40-byte FVF 530 vertex).
    std::vector<IDirect3DTexture9*> terrainTextures_; // OWNED (one per material, .WG order)
    std::vector<TerrainLayer>       terrainLayers_;    // sorted layers ready to draw (FF)
    size_t                          terrainFaceCount_ = 0; // total drawable terrain faces (sanity)

    // FRONT F_TERRAIN (B5): PER-FRAME quadtree/frustum CULL state (Terrain_Render 0x698670).
    // CPU copy of the .WG cull data (the original keeps it resident: a1+88/a1+140/heap) since
    // Render() no longer has access to WorldAssets. The .WG quadtree is decoded by
    // Asset/WorldChunk.cpp (CollisionMesh::nodes/triIndices, anchor MapColl_LoadFaces 0x694510)
    // -> reused as-is.
    std::vector<asset::CollisionFace>     terrainFaces_;      // a1+88  : 156-byte faces (batch source)
    std::vector<asset::CollisionQuadNode> terrainNodes_;      // a1+140 : 48-byte quadtree nodes (root=0)
    std::vector<uint32_t>                 terrainTriIndices_; // faceRefIndex heap (node.trisIndex=offset)
    // CPU batch filled each frame with visible faces (a1+164, 3 vertices/face). Allocated ONCE at
    // Build (capacity = 3 * total drawable faces), reused without reallocation. Consumed by
    // DrawPrimitiveUP(TRIANGLELIST, matCounter_[m], &terrainBatch_[3*matBase_[m]], 40) @0x698ff3.
    std::vector<FfTerrainVertex>          terrainBatch_;      // a1+164 : slots of 3 FfTerrainVertex
    std::vector<uint32_t>                 matBase_;           // a1+144 : per-material starting (face) slot
    std::vector<uint32_t>                 matCounter_;        // a1+160 : PER-FRAME per-material cursor
    std::vector<uint8_t>                  faceSeen_;          // a1+156 : "face seen this frame" flag
    std::vector<int32_t>                  terrainLeafScratch_;// a1+152 : visible leaf node indices
    uint32_t                              terrainNumMaterials_ = 0; // a1+12 : material count

    // Water (cat 3): procedural texture generated ONCE at Build if a cat==3 layer exists.
    // TWS-01/TWS-02: falloffTex_ = radial V8U8 256x256, port of MapColl_CreateFalloffTexture 0x694ca0
    // (the only live writer of *(a1+20), the stage-0 bump-map bound @0x69928f). There is NO waveTex_:
    // its generator cWorldMesh_MakeWaterWaveTexture 0x451220 has 2 callers with 0 xrefs (dead code)
    // and the binary never creates a wave texture. wavePhase_ = time accumulator (t = wavePhase_*10).
    IDirect3DTexture9*           falloffTex_ = nullptr;
    float                        wavePhase_  = 0.0f;

    // .SHADOW lightmap (stage 1, uv1) — GPU texture created at Build from WorldAssets::ShadowBytes().
    // IDA anchor: Terrain_Render @0x698f54 (SetTextureStageState(1,COLOROP,MODULATE=4)) / @0x698f68.
    IDirect3DTexture9*           shadowTex_ = nullptr;

    // Zone FX (.WP) — LIVE "Object A" particle engine (FRONT F_ZONEFX). IDA anchor:
    // MapColl_LoadObjectsB 0x6983b0 (this+29 templates stride 232, this+32 record B stride 76).
    std::vector<IDirect3DTexture9*>       fxTextures_;   // OWNED (one per FX node, nullptr if absent)
    std::vector<ts2::gfx::FxEmitterTemplate> fxTemplates_; // per FX node (this+29) — 232 bytes; stable (pointed to by pools)
    std::vector<ts2::gfx::FxParticlePool>    fxPools_;      // per placed instance (FxNode+28) — POBJECT 48 bytes
    std::vector<asset::AuxFxRecord>          fxRecords_;    // per instance: nodeIndex/pos/rot (FxNode+0/+4/+16)
    std::vector<ts2::gfx::Billboard_Vertex>  fxScratch_;    // CPU render buffer (dword_800080), reused/frame

    bool                         ready_ = false;
};

} // namespace ts2::gfx

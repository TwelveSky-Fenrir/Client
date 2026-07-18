// Gfx/WorldGeometryRenderer.cpp — see WorldGeometryRenderer.h for the full banner.
// (Placement format decoded; ALL A frames are now rendered — parts with A>1 replay the sway
// flipbook instead of being pinned to frame 0, cf. .h banner point 5, 2026-07-14 update.)
//
// Split (mechanical file split): terrain build/cull/render lives in
// WorldGeometryRenderer_Terrain.cpp, .WO part upload/draw in WorldGeometryRenderer_Objects.cpp,
// sky/zone-FX in WorldGeometryRenderer_FxSky.cpp. This file keeps Init/Shutdown, the release*
// methods, OnDeviceLost/OnDeviceReset, Build, PlannedDrawCallCount and Render.
#include "Gfx/WorldGeometryRenderer.h"
#include "Gfx/WorldGeometryRenderer_Internal.h"
#include "Asset/WorldChunk.h"
#include "World/WorldIntegration.h"
#include "Core/Log.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

// Init / Shutdown

bool WorldGeometryRenderer::Init(Renderer& renderer) {
    dev_ = renderer.Device();
    if (!dev_) { TS2_ERR("WorldGeometryRenderer::Init : device nul"); return false; }
    if (!meshRenderer_.Init(renderer)) {
        TS2_ERR("WorldGeometryRenderer::Init : MeshRenderer::Init a echoue");
        return false;
    }
    if (!skyRenderer_.Init(renderer)) {
        TS2_ERR("WorldGeometryRenderer::Init : SkyRenderer::Init a echoue");
        return false;
    }
    ready_ = true;
    TS2_LOG("WorldGeometryRenderer pret.");
    return true;
}

void WorldGeometryRenderer::Shutdown() {
    releaseObjects();
    meshRenderer_.Shutdown();
    skyRenderer_.Shutdown();
    dev_ = nullptr;
    ready_ = false;
}

void WorldGeometryRenderer::releaseObjects() {
    // B6: each StaticObject owns a native VB (A frames), an IB, and its diffuse texture.
    // FRONT C2: + the 2nd texture and the flipbook atlas (materials[]), all owned (B1 wiring).
    for (StaticObject& obj : objects_) {
        SafeRelease(obj.vb); SafeRelease(obj.ib); SafeRelease(obj.diffuse);
        SafeRelease(obj.second);
        for (IDirect3DTexture9*& t : obj.flipbook) SafeRelease(t);
        obj.flipbook.clear();
    }
    objects_.clear();
    modelRanges_.clear();
    instances_.clear();
    instancePhase_.clear();
    // Hygiene (Pass 4/W5b): instancePhase_ and instanceFrameCount_ are assigned IN PARALLEL to
    // instances_.size() at load time (twin assign() calls, anchor MapColl_UpdateObjectAnim 0x694a00
    // -> frameCount = part.A), so they must be freed together. No effect today (TickWorldAnim
    // guards with `i < instanceFrameCount_.size()`), but the asymmetry was real.
    instanceFrameCount_.clear();
    releaseTerrain(); // also frees the .WG ground layers/textures + water + lightmap
    releaseFx();      // also frees the .WP zone FX billboards + their textures
}

// Frees the terrain layers' VB/IB, the owned diffuse textures, the procedural water textures,
// and the lightmap.
void WorldGeometryRenderer::releaseTerrain() {
    // B5: layers no longer own a static VB/IB (the batch is CPU-side -> DrawPrimitiveUP).
    terrainLayers_.clear();
    for (IDirect3DTexture9*& t : terrainTextures_) SafeRelease(t);
    terrainTextures_.clear();
    SafeRelease(falloffTex_); // TWS-01: no more waveTex_ (dead generator 0x451220, not ported)
    SafeRelease(shadowTex_);
    wavePhase_ = 0.0f;
    terrainFaceCount_ = 0;
    // Quadtree/frustum cull state (B5).
    terrainFaces_.clear();
    terrainNodes_.clear();
    terrainTriIndices_.clear();
    terrainBatch_.clear();
    matBase_.clear();
    matCounter_.clear();
    faceSeen_.clear();
    terrainLeafScratch_.clear();
    terrainNumMaterials_ = 0;
}

// Frees the particle pools (.WP), their heap arrays, and the FX node templates/textures.
// FRONT F_ZONEFX: each FxParticlePool owns a HeapAlloc array (Particle_Free 0x6a6ff0).
void WorldGeometryRenderer::releaseFx() {
    for (ts2::gfx::FxParticlePool& pool : fxPools_)
        ts2::gfx::ZoneFx_Free(&pool);           // HeapFree of the particle array
    fxPools_.clear();
    fxRecords_.clear();
    fxTemplates_.clear();                        // 232-byte templates (pointed to by pools, freed after)
    for (IDirect3DTexture9*& t : fxTextures_) SafeRelease(t);
    fxTextures_.clear();
    fxScratch_.clear();
}

// D3DPOOL_MANAGED: survives a Reset() without re-upload (same policy as MeshRenderer::Upload).
void WorldGeometryRenderer::OnDeviceLost() {}
void WorldGeometryRenderer::OnDeviceReset() {}

// Build — uploads each models[*].parts[*] template ONCE (GPU), then copies the auxRecords[*]
// instances (the placement) for Render().

bool WorldGeometryRenderer::Build(const world::WorldAssets& assets) {
    releaseObjects();
    skippedMultiAnchor_ = 0;
    multiAnchorStaticCount_ = 0;

    // Sky / SilverLining config: refreshed BEFORE any `return` below, so the sky background
    // reflects the current zone even if the .WO geometry load fails.
    skyRenderer_.ApplyConfig(assets.SilverLining());
    skyRenderer_.SetAtmosphere(assets.Atmosphere());

    // FRONT W3-F3 "the ground": builds the .WG terrain FF layers (+ water + lightmap) BEFORE the
    // .WO guards below, so the ground is drawn even without a .WO chunk (IDA anchor:
    // Terrain_Render 0x698670, called by Scene_InGameRender 0x52d0b0 BEFORE the .WO objects).
    buildTerrain(assets);
    // .WP zone FX: placed billboards (independent of .WO — built even without a .WO).
    buildFx(assets);

    const asset::WorldChunk* chunk = assets.Objects();
    if (!chunk) {
        TS2_WARN("WorldGeometryRenderer::Build : aucun chunk .WO charge (WorldAssets::Objects() nul).");
        return false;
    }
    const asset::ObjectChunk* wo = chunk->AsObjects();
    if (!wo) {
        TS2_WARN("WorldGeometryRenderer::Build : chunk charge mais pas de type WO.");
        return false;
    }
    if (wo->empty) {
        TS2_LOG("WorldGeometryRenderer::Build : .WO vide (0 modele) pour cette zone.");
        return true;
    }

    size_t uploaded = 0, totalParts = 0;
    modelRanges_.assign(wo->models.size(), ModelRange{});
    for (size_t modelIdx = 0; modelIdx < wo->models.size(); ++modelIdx) {
        const asset::Model& model = wo->models[modelIdx];
        ModelRange& range = modelRanges_[modelIdx];
        range.start = objects_.size();
        if (model.present) {
            for (const asset::WorldMeshPart& part : model.parts) {
                ++totalParts;
                StaticObject obj;
                if (uploadPart(part, obj)) {
                    objects_.push_back(std::move(obj));
                    ++uploaded;
                }
            }
        }
        range.count = objects_.size() - range.start;
    }

    // THE placement: typed copy of ObjectChunk::auxRecords (modelIndex + pos + rot),
    // cf. Docs/TS2_WO_PLACEMENT_FORMAT.md — Render() derives a per-instance matrix from it.
    // CONFIRMED ex-VeryOldClient: MOBJECTINFO { mIndex; mCoord[3]; mAngle[3] } (28-byte on-disk). IDA
    // anchor: Terrain_Render 0x698670 -> Model_RenderWithShadow_0 0x6a4110 @0x698bdd (array consumer).
    instances_ = wo->auxRecords;

    // FRONT W3-F3: per-instance sway state (wrap bound = template's A frame count).
    // IDA anchor: MapColl_UpdateObjectAnim 0x694a00 (@0x694a4d advances aux+28, wraps by frameCount =
    // part.A = *(...+252)). Ticked by TickWorldAnim.
    //
    // TWS-03 (Pass 4 / W11) — RANDOM SEEDING +28/+32 DELIBERATELY NOT REPRODUCED (refutes the
    // phase-1 prescription). PROVEN in IDA: MapColl_LoadObjectsA 0x6980d0 reads a 28-byte on-disk
    // record (u32 idx@+0, float3@+4, float3@+16), then in the loop @0x698340-0x69837d (runtime
    // stride 36) SEEDS the remaining 8 bytes with two draws: *(base+28) = Math_RandRangeFloat(0,100)
    // @0x69835d and *(base+32) likewise @0x698370 — so +28/+32 do NOT come from disk (corroborates
    // World/WorldIntegration.cpp:235). instancePhase_ maps to +28 (dual-use seed/anim cursor,
    // mapping correct). But the seeding is NOT applied here, for two re-proven reasons: (1)
    // Math_RandRangeFloat 0x69cb10 draws via Rng_Next 0x7603fd = the SAME shared CRT rand() stream
    // as net::DefaultRng() (network nonces, cf. Net/Login.cpp:34 / CharSelectPackets.cpp:58) —
    // injecting 2xN draws at zone load would perturb that stream without reproducing the binary's
    // global rand() timeline, worsening nonce fidelity rather than helping it; (2) no consumer of
    // the seed is ported: the GPU always draws frame 0 (static pose, cf. .h banner point 5) and +32
    // only feeds Model_RenderWithShadow_0 0x6a4110 arg 6, unported — so the seed would have NO
    // observable effect (and no instanceSeed2_ either: that would be a write-never-read state, the
    // anti-pattern this campaign hunts). -> initial phase 0 kept (documented), no RNG draw. TODO: if
    // GPU frame selection + the global rand() timeline are ever ported, seed via net::DefaultRng().
    instancePhase_.assign(instances_.size(), 0.0f);
    instanceFrameCount_.assign(instances_.size(), 1u);
    for (size_t i = 0; i < instances_.size(); ++i) {
        const uint32_t mi = instances_[i].modelIndex;
        if (mi < wo->models.size() && wo->models[mi].present && !wo->models[mi].parts.empty()) {
            const uint32_t A = wo->models[mi].parts[0].A;
            instanceFrameCount_[i] = (A > 0) ? A : 1u;
        }
    }

    // Sanity log: proof that Render() does use N distinct positions.
    size_t outOfRange = 0, emptyModelInstances = 0;
    float minP[3] = { (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(),
                       (std::numeric_limits<float>::max)() };
    float maxP[3] = { (std::numeric_limits<float>::lowest)(), (std::numeric_limits<float>::lowest)(),
                       (std::numeric_limits<float>::lowest)() };
    for (const asset::AuxRecord& inst : instances_) {
        if (inst.modelIndex >= modelRanges_.size()) { ++outOfRange; continue; }
        if (modelRanges_[inst.modelIndex].count == 0) { ++emptyModelInstances; continue; }
        for (int k = 0; k < 3; ++k) {
            if (inst.pos[k] < minP[k]) minP[k] = inst.pos[k];
            if (inst.pos[k] > maxP[k]) maxP[k] = inst.pos[k];
        }
    }
    TS2_LOG("WorldGeometryRenderer::Build : %zu/%zu parts uploadees GPU (%zu ignorees pour "
            "cause reelle, dont %zu parts multi-ancre A>1 rendues en pose statique figee "
            "frame 0 -- format decode, cf. bandeau uploadPart(), pas de balancement au vent) ; "
            "%zu instances (auxRecords) sur %zu gabarits, %zu appels de dessin prevus (%zu "
            "modelIndex hors bornes, %zu instances de gabarit vide) ; bbox positions "
            "x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f].",
            uploaded, totalParts, skippedMultiAnchor_, multiAnchorStaticCount_,
            instances_.size(), wo->models.size(),
            PlannedDrawCallCount(), outOfRange, emptyModelInstances,
            minP[0], maxP[0], minP[1], maxP[1], minP[2], maxP[2]);
    return true;
}

size_t WorldGeometryRenderer::PlannedDrawCallCount() const {
    size_t total = 0;
    for (const asset::AuxRecord& inst : instances_) {
        if (inst.modelIndex >= modelRanges_.size()) continue;
        total += modelRanges_[inst.modelIndex].count;
    }
    return total;
}

// Render — draws the .WG GROUND first (Gap G1, renderTerrain()), then a PER-INSTANCE world
// matrix (Rz*Ry*Rx*T) for the .WO props (cf. .h banner point 2/4). Order matches
// Scene_InGameRender 0x52d0b0: terrain (@0x52d9be) BEFORE objects/entities. SilverLining layers
// are invoked separately by SceneManager.
//
// Gap G1 done (2026-07-16): .WG terrain (WorldAssets::Faces()) is now drawn by renderTerrain()
// (IDA anchor Terrain_Render 0x698670, SetFVF 530, world=identity, 40-byte vertices grouped by
// materialIndex). FRONT F_TERRAIN B5 (2026-07-17): per-frame quadtree/frustum cull is done
// (cullTerrain(): MapColl_CollectLeafFaces 0x694b50 + backface @0x698dd4 + Cam_FrustumTestSphere2x
// 0x69f0e0 -> per-material CPU batch -> DrawPrimitiveUP @0x698ff3); same visible faces, fewer
// triangles. B6: .WO props replay their SWAY (renderObjects(), stream-offset frame swap) — no
// longer floating without ground. G6 water (category 3, bump-env) and G8 .SHADOW lightmap (stage
// 1, uv1) are done in this front (cf. bindWaterStates() @0x699206: bump-map = falloff
// MapColl_CreateFalloffTexture 0x694ca0; lightmap stage 1 MODULATE @0x698f54/@0x698f68). TWS-01
// (W11): the "wave texture" 0x451220 is DEAD CODE (2 callers, 0 xrefs), never created by the
// binary -> not ported. Remaining gap = exact water bump scale (runtime a10, TODO anchor
// 0x699206), purely visual.

void WorldGeometryRenderer::Render(const Camera& camera, int screenW, int screenH) {
    if (!ready_) return;
    // Since B5/B6, both terrain and .WO props go through a native FIXED-FUNCTION path (no more
    // DrawSkinnedSubset): meshRenderer_ is no longer used for drawing. This call is kept as a
    // defensive no-op to keep meshRenderer_'s VS/PS cache consistent with the
    // SetVertexShader(nullptr)/SetPixelShader(nullptr) pokes SkyRenderer::Render() issues on the
    // shared device (cf. Gfx/MeshRenderer.h::InvalidateShaderBindingCache()) — safe if a future
    // skinned path reappears.
    meshRenderer_.InvalidateShaderBindingCache();

    // Camera: shared by the ground (B5) and the .WO props. view/proj + world-space eye (terrain
    // cull backface test, = g_CameraPos in the original, anchor 0x698dd4).
    D3DXMATRIX view, proj;
    camera.BuildViewMatrix(view);
    const float aspect = (screenH > 0)
        ? static_cast<float>(screenW) / static_cast<float>(screenH)
        : 1.0f;
    camera.BuildProjMatrix(proj, aspect);
    const D3DXVECTOR3 eye = camera.Eye();

    // FRONT W3-F3: the .WG GROUND first (matches Scene_InGameRender: terrain before props). FF,
    // with the SAME view/proj as the props. Drawn even without a .WO object in the zone. B5: cull
    // per frame.
    renderTerrain(view, proj, eye);

    // FRONT F_TERRAIN (B6): .WO props with SWAY REPLAY (per-instance frame via stream offset).
    // FRONT C2: `eye` (g_CameraPos) is passed through -> feeds the B1 material layer's MeshPartRuntime.
    renderObjects(view, proj, eye);

    // FRONT W3-F3: .WP zone FX AFTER the props (pass a5=2 @0x698c6d: blend active, depth-write
    // off). This is the real .WP render entry point (fixes WorldIntegration's "not identified").
    RenderFxBillboards(camera);
}

} // namespace ts2::gfx

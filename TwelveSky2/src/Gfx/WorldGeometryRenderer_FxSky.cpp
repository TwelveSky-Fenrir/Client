// Gfx/WorldGeometryRenderer_FxSky.cpp — sky orchestration + zone FX (.WP), split out of
// WorldGeometryRenderer.cpp; see WorldGeometryRenderer.h for the full class banner.
// (Placement format decoded; ALL A frames are now rendered — parts with A>1 replay the sway
// flipbook instead of being pinned to frame 0, cf. .h banner point 5, 2026-07-14 update.)
#include "Gfx/WorldGeometryRenderer.h"
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

namespace {

// Unlit FX-billboard fixed-function FVF: 0x142 = 322 = D3DFVF_XYZ|DIFFUSE|TEX1 (stride 24).
// IDA anchor: Gfx_BeginUnlitPass 0x69e470 SetFVF(322).
constexpr DWORD kFvfBillboard = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1; // 0x142
static_assert(kFvfBillboard == 0x142, "billboard FVF must be 322 (0x142)");

} // namespace

// FRONT W3-F3 — build of .WP zone FX billboards. IDA anchor: MapColl_LoadObjectsB 0x6983b0
// (fxbRecords: nodeIndex+pos+rot) + Fx_NodeLoadFromHandle 0x6a69f0 (node texture). Build-safe
// subset: 1 billboard placed per instance, at its position, FX node texture resolved via
// AuxFxRecord::nodeIndex -> FxChunk::nodes[]. The full particle sim (Particle_Init 0x6a7020 /
// Particle_UpdateEmit 0x6a7530) remains a TODO anchor.
bool WorldGeometryRenderer::buildFx(const world::WorldAssets& assets) {
    if (!dev_) return false;
    const asset::WorldChunk* chunk = assets.FxNodes();
    if (!chunk) return false;
    const asset::FxChunk* wp = chunk->AsFx();
    if (!wp || wp->empty || wp->nodes.empty()) return true; // no FX: nothing to draw

    // 1) One GPU texture + one 232-byte TEMPLATE per FX node (this+29 in the binary, stride 232).
    //    The template is built byte-exact from asset::FxNode: enabled=1, texture at +52, and the
    //    144 on-disk bytes (FxNode.fields = runtime [+72,+216)) copied to (tmpl+72). Anchor:
    //    Fx_NodeLoadFromHandle 0x6a69f0.
    //    WARNING: fxTemplates_ is sized ONCE here and never resized again: the pools keep a
    //    pointer into its elements (pool->tmpl), which must stay stable until releaseFx().
    fxTextures_.assign(wp->nodes.size(), nullptr);
    fxTemplates_.assign(wp->nodes.size(), ts2::gfx::FxEmitterTemplate{});
    for (size_t i = 0; i < wp->nodes.size(); ++i) {
        fxTextures_[i] = createTextureFromBlock(dev_, wp->nodes[i].tex); // nullptr if absent
        const asset::FxNode& node = wp->nodes[i];
        ts2::gfx::ZoneFx_BuildTemplate(&fxTemplates_[i], node.fields.data(), node.fields.size(),
                                       fxTextures_[i]);
        if (!node.present) fxTemplates_[i].enabled = 0; // node not loaded -> emitter disabled (Init gate)
    }

    // 2) One POOL (POBJECT 48 bytes, flag=0 -> ZoneFx_Init on the 1st tick) per placed instance
    //    (record B, this+32 stride 76). The record (nodeIndex/pos/rot) is kept aligned with the
    //    pool. Anchor: MapColl_LoadObjectsB 0x6983b0 (@0x698602 reads nodeIndex+pos+rot) + loop 2
    //    0x694af0.
    fxRecords_.clear();
    size_t outOfRange = 0;
    for (const asset::AuxFxRecord& rec : wp->fxbRecords) {
        if (rec.nodeIndex >= fxTemplates_.size()) { ++outOfRange; continue; }
        fxRecords_.push_back(rec);
    }
    fxPools_.assign(fxRecords_.size(), ts2::gfx::FxParticlePool{}); // zero-init: flag=0, particles=nullptr

    TS2_LOG("WorldGeometryRenderer::buildFx (F_ZONEFX) : %zu noeuds FX (templates 232o), %zu emetteurs "
            "instancies (POBJECT 48o) (%zu nodeIndex hors bornes). Sim VIVANTE : Particle_Init 0x6a7020 "
            "/ UpdateEmit 0x6a7530 / RenderBillboards 0x6a70b0.",
            fxTemplates_.size(), fxPools_.size(), outOfRange);
    return true;
}

// This file is only the sky ORCHESTRATOR (SkyRenderer NOT owned here, boundary CONFIRMED
// ex-VeryOldClient: SKY_FOR_GXD = separate class outside WORLD_FOR_GXD). PLAUSIBLE (VeryOldClient
// n/a) — not byte-exact proven: the 2 calls' placement mirrors the 2 real entry points
// (Env_RenderSkyCube 0x6a8f60 early via Gfx_BeginFrame 0x6a2280; Atmosphere_DrawFrame 0x794fe0
// after terrain via Scene_InGameRender 0x52d0b0). TODO out-of-FRONT-4: real skybox cube = X03,
// full SilverLining (clouds/sun/moon/stars/fog) = X04 (FRONT 6), cf. TS2_WORLD_ROSETTA.md §3.Z.
void WorldGeometryRenderer::RenderSky(int screenW, int screenH) {
    if (!ready_) return;
    skyRenderer_.Render(screenW, screenH);
}

// FRONT W3-F3 — .WP zone FX pass: unlit billboards. IDA anchor: Terrain_Render a5=2 @0x698c6d ->
// Gfx_BeginUnlitPass 0x69e470 (LIGHTING off, ALPHABLEND on, stage0 ALPHAOP=MODULATE, FVF
// 322=XYZ|DIFFUSE|TEX1, texture0 matrix identity) -> Particle_RenderBillboards 0x6a70b0
// (camera-facing quads 24o/vertex, DrawPrimitiveUP(TRIANGLELIST, 2*n, ..., 24)).
// Build-safe SUBSET: 1 camera-facing quad per placed instance (right/up basis derived from the
// view matrix). Full sim (runtime right/up basis flt_8001D4..E8 + Particle_Init/UpdateEmit) = TODO anchor.
void WorldGeometryRenderer::RenderFxBillboards(const Camera& camera) {
    if (!ready_ || fxPools_.empty()) return;

    DWORD prevLighting = TRUE, prevBlend = FALSE, prevZWrite = TRUE, prevCull = D3DCULL_CCW,
          prevSrc = D3DBLEND_ONE, prevDst = D3DBLEND_ZERO;
    dev_->GetRenderState(D3DRS_LIGHTING, &prevLighting);
    dev_->GetRenderState(D3DRS_ALPHABLENDENABLE, &prevBlend);
    dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prevZWrite);
    dev_->GetRenderState(D3DRS_CULLMODE, &prevCull);
    dev_->GetRenderState(D3DRS_SRCBLEND, &prevSrc);
    dev_->GetRenderState(D3DRS_DESTBLEND, &prevDst);

    // Gfx_BeginUnlitPass 0x69e470: (137,0) [SPECULARENABLE], (14,0)=LIGHTING off, (27,1)=ALPHABLEND on.
    dev_->SetVertexShader(nullptr);
    dev_->SetPixelShader(nullptr);
    dev_->SetFVF(kFvfBillboard);                                   // 322 @0x69e4-SetFVF
    dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(137), 0); // state 137 (exact role unproven, faithful to IDA)
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);                   // (14,0)
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);           // (27,1)
    dev_->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);              // a5=2: depth-write off
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,  D3DTOP_MODULATE); // (0, ALPHAOP(4), MODULATE(4))
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    D3DXMATRIX ident; D3DXMatrixIdentity(&ident);
    dev_->SetTransform(D3DTS_WORLD, &ident);
    dev_->SetTransform(D3DTS_TEXTURE0, &ident);

    // Camera-facing basis from the view matrix (world right/up = rotation columns). Faithful to
    // the role of the binary's flt_8001D4..8001E8 globals (world camera right/up), read and
    // multiplied by particle.size in Particle_RenderBillboards 0x6a70b0.
    D3DXMATRIX view; camera.BuildViewMatrix(view);
    D3DXVECTOR3 right(view._11, view._21, view._31);
    D3DXVECTOR3 up(view._12, view._22, view._32);
    D3DXVec3Normalize(&right, &right);
    D3DXVec3Normalize(&up, &up);

    // Frame parameters common to all emitters (device, basis, and CPU scratch are shared —
    // dword_800080 in the binary). maxQuads=0 => no cap (dword_7FFEE0 is 0 statically; set at
    // runtime by the renderer -> unproven, kept at 0 = no cutoff).
    ts2::gfx::ZoneFxFrameParams params;
    params.device   = dev_;
    params.right[0] = right.x; params.right[1] = right.y; params.right[2] = right.z;
    params.up[0]    = up.x;    params.up[1]    = up.y;    params.up[2]    = up.z;
    params.maxQuads = 0;                // dword_7FFEE0 (0 statically)
    params.scratch  = &fxScratch_;      // CPU vertex buffer (reused), DrawPrimitiveUP
    params.frustum  = nullptr;          // Cam_FrustumTestPoint6 not wired here (distance cull below)

    // SQUARED DISTANCE cull per node (Terrain_Render a5=2 @0x698c81-0x698cbb: dist²(eye, FxNode.pos)
    // < a10²). a10 = Game_GetTierRange 0x5402f0 (display option 1000/2000/3000 per
    // g_Opt_DisplayRangeTier 0x84dec4, = 0 statically -> RUNTIME value not dumpable). Documented
    // build-safe threshold = FAR tier 3000.0 (the most permissive of the 3 proven values) so
    // nothing is hidden; wire it to the real option once config is ported. Cull origin = record B
    // pos (FxNode.pos).
    const D3DXVECTOR3 eye = camera.Eye();
    constexpr float kZoneFxDrawRange = 3000.0f;                 // Game_GetTierRange (tier 3, documented)
    const float range2 = kZoneFxDrawRange * kZoneFxDrawRange;
    for (size_t i = 0; i < fxPools_.size(); ++i) {
        ts2::gfx::FxParticlePool& pool = fxPools_[i];
        if (!pool.flag) continue;                              // not yet Init -> no pool to draw
        const float dx = eye.x - fxRecords_[i].pos[0];
        const float dy = eye.y - fxRecords_[i].pos[1];
        const float dz = eye.z - fxRecords_[i].pos[2];
        if (dx * dx + dy * dy + dz * dz >= range2) continue;   // out of range -> skip the node (@0x698cbb)
        ts2::gfx::ZoneFx_RenderBillboards(&pool, params);      // Particle_RenderBillboards 0x6a70b0
    }

    // Restore.
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, prevZWrite);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, prevBlend);
    dev_->SetRenderState(D3DRS_SRCBLEND, prevSrc);
    dev_->SetRenderState(D3DRS_DESTBLEND, prevDst);
    dev_->SetRenderState(D3DRS_LIGHTING, prevLighting);
    dev_->SetRenderState(D3DRS_CULLMODE, prevCull);
    meshRenderer_.InvalidateShaderBindingCache();
}

// FRONT W3-F3 — world animation tick. IDA anchor: MapColl_UpdateObjectAnim 0x694A00 (site
// Scene_InGameUpdate 0x52c94b, kAnimFps=15.0). To be called by SceneManager every frame:
//   if (worldGeom_) worldGeom_.TickWorldAnim(dtSeconds);
// (SceneManager not owned here -> integration note only, no edit.)
void WorldGeometryRenderer::TickWorldAnim(float dt) {
    // Water: time accumulator (t = wavePhase_*10 in bindWaterStates). Origin: v92 Terrain_Render.
    wavePhase_ += dt;

    // .WO sway: per-instance flipbook phase (aux+28 += dt*fps; wraps by the template's A frame
    // count). IDA anchor: MapColl_UpdateObjectAnim @0x694a30/@0x694a4a. B6 (2026-07-17): the phase
    // is now CONSUMED at draw time by renderObjects() -> frame = Crt_Dbl2Uint(instancePhase_[i]) ->
    // SetStreamSource(0, vb, 32*frame*B, 32) (MeshPart_Render 0x6aeea5). No more pinned pose.
    constexpr float kAnimFps = 15.0f;
    if (instancePhase_.size() != instances_.size())
        instancePhase_.assign(instances_.size(), 0.0f);
    for (size_t i = 0; i < instancePhase_.size(); ++i) {
        instancePhase_[i] += dt * kAnimFps;
        const uint32_t frames = (i < instanceFrameCount_.size()) ? instanceFrameCount_[i] : 1u;
        if (frames > 1) {
            const float span = static_cast<float>(frames);
            while (instancePhase_[i] >= span) instancePhase_[i] -= span; // wrap (cf. loop @0x694a5e)
        } else {
            instancePhase_[i] = 0.0f;
        }
    }

    // FRONT F_ZONEFX — .WP particle sim: EXACT mirror of MapColl_UpdateObjectAnim's loop 2
    // @0x694af0 (stride 76): if the POBJECT is already initialized (flag != 0) -> Particle_UpdateEmit
    // with (dt=a3, pos=FxNode+4, rot=FxNode+16); else -> Particle_Init(POBJECT, template =
    // this+29[nodeIndex]). dt = binary's a3 = TRUE frame delta (call site Scene_InGameUpdate
    // @0x52c94b: a2=15.0, a3=dt). Emission frustum not wired (nullptr): the Cam_FrustumTestPoint6
    // cull is a perf/lifetime optimization; omitting it keeps the on-screen visual identical
    // (off-screen emitters keep simulating — no observable effect). Anchor: MapColl_UpdateObjectAnim
    // 0x694a00.
    for (size_t i = 0; i < fxPools_.size(); ++i) {
        ts2::gfx::FxParticlePool& pool = fxPools_[i];
        const uint32_t node = fxRecords_[i].nodeIndex;
        if (pool.flag) {                                        // if(*(v11+v12+28)) @0x694af6
            ts2::gfx::ZoneFx_UpdateEmit(&pool, dt, fxRecords_[i].pos, fxRecords_[i].rot, nullptr); // @0x694b2e
        } else if (node < fxTemplates_.size()) {
            ts2::gfx::ZoneFx_Init(&pool, &fxTemplates_[node]); // template = this+29[nodeIndex] @0x694b13
        }
    }
}

} // namespace ts2::gfx

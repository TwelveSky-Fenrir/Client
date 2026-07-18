// Gfx/WorldGeometryRenderer_Objects.cpp — .WO part upload/draw, split out of
// WorldGeometryRenderer.cpp; see WorldGeometryRenderer.h for the full class banner.
// (Placement format decoded; ALL A frames are now rendered — parts with A>1 replay the sway
// flipbook instead of being pinned to frame 0, cf. .h banner point 5, 2026-07-14 update.)
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

namespace {

// .WO part fixed-function FVF (MobjVertex 32 bytes): 0x112 = 274 = D3DFVF_XYZ|NORMAL|TEX1.
// IDA anchor: Model_RenderParts 0x6a3720 SetFVF(274) @0x6a377f (stride 32, cf. MeshPart_Render 0x6aeea5).
constexpr DWORD kFvfWo = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1; // 0x112
static_assert(kFvfWo == 0x112, ".WO FVF must be 274 (0x112)");

// Fixed header size of a WorldMeshPart GXD geometry block (Asset/WorldChunk.cpp,
// ReadMeshPart: A/B/C/D read at offset 120, i.e. 136 bytes of header total).
constexpr size_t kGeoHeaderSize = 136;
// On-disk stride of a MOBJECT vertex (Docs/TS2_ASSET_FORMATS.md: FVF 0x112 = XYZ|NORMAL|TEX1).
constexpr size_t kMobjVertexStride = 32;
// On-disk stride of an index (INDEX16, 3 indices/face = 6 bytes/face).
constexpr size_t kIndexStride = 2;

} // namespace

// Diffuse texture (tex1 already decoded to DDS by Asset/WorldChunk.cpp::ReadTextureBlock — no
// zlib re-inflate needed here, unlike MeshRenderer::createDiffuse which starts from still-
// compressed SObject data).

IDirect3DTexture9* WorldGeometryRenderer::createTextureFromBlock(IDirect3DDevice9* dev,
                                                                  const asset::TextureBlock& tex) {
    if (!tex.present || tex.data.empty()) return nullptr;
    IDirect3DTexture9* out = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev, tex.data.data(), static_cast<UINT>(tex.data.size()),
        D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN,
        D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &out);
    if (FAILED(hr)) {
        TS2_WARN("WorldGeometryRenderer: creation texture WO echouee (0x%08lX)", hr);
        return nullptr;
    }
    return out;
}

// Creates a texture from a complete in-memory DDS file (raw .SHADOW lightmap, exposed by
// WorldAssets::ShadowBytes()). IDA anchor: Tex_LoadFromFile 0x6a9910 (DDS DXT1/3/5).
IDirect3DTexture9* WorldGeometryRenderer::createTextureFromDds(IDirect3DDevice9* dev,
                                                               const std::vector<uint8_t>& dds) {
    if (!dev || dds.empty()) return nullptr;
    IDirect3DTexture9* out = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev, dds.data(), static_cast<UINT>(dds.size()),
        D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN,
        D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &out);
    if (FAILED(hr)) {
        TS2_WARN("WorldGeometryRenderer: creation lightmap .SHADOW echouee (0x%08lX)", hr);
        return nullptr;
    }
    return out;
}

// Uploading a WorldMeshPart — ALL of A (cf. .h banner point 5, "multi-anchor" 2026-07-14
// update): field `A` is NOT a skinning bone count but the frame count of a FLIPBOOK of
// precomputed positions (likely wind sway), confirmed by disassembling `MeshPart_Render`
// (0x6AED60, SetStreamSource offset = `32*frame*B`, frame = `Crt_Dbl2Uint(time)`, truncated and
// clamped to `[0, A-1]`) and `MeshPart_Load` (0x6AD169, which copies `32*A*B` bytes verbatim — A
// consecutive blocks of B 32-byte vertices — into a SINGLE D3D vertex buffer, never replayed via
// a matrix palette). The index buffer (6*D bytes) is SHARED by all frames (same topology, only
// positions/normals change). The "bone" block (64*A bytes, this+284 on the original client) is
// loaded into memory by `MeshPart_Load` but read by NO identified consumer of the render path
// (`Model_RenderParts`/`MeshPart_Render`) — likely animation metadata (sway parameters) used
// elsewhere, not needed for a static pose.

bool WorldGeometryRenderer::uploadPart(const asset::WorldMeshPart& part, StaticObject& out) {
    if (!part.present || !part.geoSizeOk) return false;
    if (part.A == 0 || part.B == 0 || part.D == 0) return false; // no frame/vertex/face

    // Layout of the decoded geometry block (Asset/WorldChunk.cpp::ReadMeshPart):
    //   [136B header][64*A B bones (sway metadata, not read at render time)][32*A*B B VB = A
    //   consecutive frames of B 32-byte vertices][6*D B IB, SHARED by all frames (fixed topology)]
    const size_t boneBytes        = 64ull * part.A;
    const size_t vbOffset         = kGeoHeaderSize + boneBytes;               // start of frame 0
    const size_t vbAllFramesBytes = kMobjVertexStride * static_cast<size_t>(part.B) * part.A; // 32*A*B
    const size_t ibOffset         = vbOffset + vbAllFramesBytes;
    const size_t ibBytes          = 3ull * kIndexStride * part.D;             // 6*D
    if (part.geo.size() < ibOffset + ibBytes) {
        TS2_WARN("WorldGeometryRenderer: part incoherente (geo=%zu < attendu=%zu)",
                 part.geo.size(), ibOffset + ibBytes);
        return false;
    }

    // B6 (2026-07-17) — SWAY REPLAY: the A contiguous frames are now uploaded AS-IS in native 32B
    // format (MobjVertex, FVF 0x112), mirroring MeshPart_Load 0x6ad169 (`qmemcpy 32*A*B`). The
    // frame is selected at draw time via SetStreamSource(0, vb, 32*frame*B, 32) (MeshPart_Render
    // 0x6aeea5) -> no more 76B conversion or frame-0 pinned pose. The IB (6*D bytes) is shared by
    // all frames (same topology, only positions/normals change, cf. .h banner point 5).
    const UINT vbBytes = static_cast<UINT>(vbAllFramesBytes);
    HRESULT hr = dev_->CreateVertexBuffer(vbBytes, 0, kFvfWo, D3DPOOL_MANAGED, &out.vb, nullptr);
    if (FAILED(hr)) {
        TS2_ERR("WorldGeometryRenderer: CreateVertexBuffer (.WO) echoue (0x%08lX)", hr);
        return false;
    }
    void* p = nullptr;
    if (SUCCEEDED(out.vb->Lock(0, vbBytes, &p, 0))) {
        std::memcpy(p, part.geo.data() + vbOffset, vbBytes); // A raw frames (no conversion)
        out.vb->Unlock();
    }

    const UINT ibTotal = static_cast<UINT>(ibBytes);
    hr = dev_->CreateIndexBuffer(ibTotal, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &out.ib, nullptr);
    if (FAILED(hr)) {
        TS2_ERR("WorldGeometryRenderer: CreateIndexBuffer (.WO) echoue (0x%08lX)", hr);
        SafeRelease(out.vb);
        return false;
    }
    if (SUCCEEDED(out.ib->Lock(0, ibTotal, &p, 0))) {
        std::memcpy(p, part.geo.data() + ibOffset, ibTotal);
        out.ib->Unlock();
    }

    out.A = part.A; out.B = part.B; out.D = part.D;
    // FRONT C2 (2026-07-17) — part's MATERIAL RESOURCES for B1 MeshPart_RenderFull 0x6b0850 (the
    // "tex2/materials[] parsed but ignored" TODO is RESOLVED):
    //   base     = tex1  -> a1[86]/+344; base mode = tex1.mode() (= trailer[1] = struct+44 = a1[85]/+340)
    //   2nd tex  = tex2  -> a1[99]/+396; 2nd mode  = tex2.mode() (= a1[98]/+392)
    //   flipbook = materials[] (animated atlas) -> a1[101]/+404, one IDirect3DTexture9 per present entry
    //   mat      = decoded 120-byte material header (part.mat), COPIED BY VALUE (the WorldChunk may be freed)
    // IDA anchor: MeshPart_Load 0x6ad160 (tex1@296, tex2@348, materials@404; qmemcpy(this+132,Heap,0x78) @0x6ad2d1).
    out.diffuse    = createTextureFromBlock(dev_, part.tex1);
    out.second     = createTextureFromBlock(dev_, part.tex2);
    out.baseMode   = static_cast<int>(part.tex1.mode()); // this[85]: 1=alpha-test / 2=blend / other=additive
    out.secondMode = static_cast<int>(part.tex2.mode()); // this[98]
    out.mat        = part.mat;                            // copy of the decoded 120-byte header (mat.decoded preserved)
    out.flipbook.clear();
    out.flipbook.reserve(part.materials.size());
    for (const asset::TextureBlock& m : part.materials)
        out.flipbook.push_back(createTextureFromBlock(dev_, m)); // nullptr if the holder is absent (imageSize==0)
    if (part.A > 1) ++multiAnchorStaticCount_; // A>1 = sway flipbook (now REPLAYED, cf. B6)
    return true;
}

// Builds World = Rz(rot.z)*Ry(rot.y)*Rx(rot.x)*T(pos) — exact D3DX order confirmed by
// disassembly (Model_RenderParts 0x6a379c-0x6a3892, Model_RenderWithShadow_0
// 0x6a41a3-0x6a4299), cf. .h banner point 4 / Docs/TS2_WO_PLACEMENT_FORMAT.md.
// CONFIRMED ex-VeryOldClient: MOBJECT drawn at mCoord/mAngle (kDegToRad = pi/180, value from the binary).
D3DXMATRIX WorldGeometryRenderer::BuildInstanceWorldMatrix(const asset::AuxRecord& inst) {
    constexpr float kDegToRad = 0.017453292f; // pi/180, exact value read from the binary
    D3DXMATRIX t, rx, ry, rz, m;
    D3DXMatrixTranslation(&t, inst.pos[0], inst.pos[1], inst.pos[2]);
    D3DXMatrixRotationX(&rx, inst.rot[0] * kDegToRad);
    D3DXMatrixRotationY(&ry, inst.rot[1] * kDegToRad);
    D3DXMatrixRotationZ(&rz, inst.rot[2] * kDegToRad);
    D3DXMatrixMultiply(&m, &rz, &ry); // M = Rz * Ry
    D3DXMatrixMultiply(&m, &m, &rx);  // M = M * Rx
    D3DXMatrixMultiply(&m, &m, &t);   // M = M * T
    return m;
}

// FRONT F_TERRAIN (B6) + FRONT C2 (2026-07-17) — draws .WO props in native FIXED-FUNCTION, with
// SWAY REPLAY and a FULL MATERIAL LAYER.
// Placement/sway reproduced from Model_RenderWithShadow_0 0x6a4110 (the real material path for
// props; Model_RenderParts 0x6a3720 is the simple variant with no material):
//   - FVF 274 = 0x112 (XYZ|NORMAL|TEX1, stride 32): SetFVF(274) @0x6a4186;
//   - PER-INSTANCE world matrix = Rz*Ry*Rx*T (SetTransform(256,m) @0x6a4299);
//   - frame = Crt_Dbl2Uint(animPhase) @0x6a4148, GATED [0, A-1] with A = MeshPart[0]+252 @0x6a415e;
//   - SWAP: SetStreamSource(0, vb, 32*frame*B, 32) @0x6aeea5 (B = a1[64]/+256); SetIndices(+292)
//     @0x6aeedc; DrawIndexedPrimitive(TRIANGLELIST, 0,0, B, 0, D) @0x6aef00 (D = a1[66]/+264).
// The VB carries the A contiguous frames (uploadPart): changing frame = shifting the offset, no
// re-upload. animPhase is advanced by TickWorldAnim (already called by SceneManager, wraps [0,A)).
//
// FRONT C2 — B1 WIRING: for each `mat.decoded` part, the base-draw is REPLACED by the
// MeshPartMaterialRenderer::Render material state machine (MeshPart_RenderFull 0x6b0850), which
// re-includes the base-draw (32*frame*B). What the material layer now ADDS (UNLIT layers, active
// under LIGHTING=FALSE): texture flipbook (materials[] atlas, @0x6b0d33), tex1/tex2 UV-scroll
// (@0x6b0f59/@0x6b19bb), a 2nd texture in a 2nd blended pass (@0x6b19ad), and alpha-test/blend
// modes for both the base AND the 2nd texture (a1[85]/a1[98]). The 4 context arguments follow
// B1's "FX path" defaults: frame=morph frame, animTime=v66 (fallback wavePhase_), glowEnable=1,
// alphaFade=0, decal=nullptr (cf. body + .h banner). mat.decoded==false keeps exactly the B6
// base-draw.
//
// ACCEPTED GAPS (build-safe, documented):
//  - LIGHTING=FALSE kept (preserves the current textured render, avoids black from missing the
//    original's global lights/ambient): B1's LIT layers (emissive @0x6b08a5, specular glow
//    @0x6b0a11, proj-light) are set & restored but INERT. sunDir/sceneCenter unavailable -> (0,0,0)
//    (so glow mode 2's fresnel falls back to 0 even under lighting). PRE-EXISTING fidelity gap
//    (before C2 there was no material at all), not a regression.
//  - Camera-facing billboard (mat.billboard.Enable): B1 falls back to an indexed draw (runtime
//    axes flt_8001D4/unk_80022C unproven) — the 64*A node block is not kept (geo.frameNodes=nullptr).
//  - CULLMODE=NONE: the binary inherits the device's material cull state (MOBJ winding not
//    re-checked here) -> NONE guarantees props stay visible (no regression), at the cost of some
//    overdraw.
//  - Distance cull + alpha fade for far .WO props (Terrain_Render 0x69977c/0x69979f, via
//    Model_RenderWithShadow_0 0x6a4110 args a6/a8/a9): NOT reproduced here (the Scene_InGameRender
//    call-site values are not statically dumpable — cf. TS2_EXTRACT_TERRAIN_CULL.md §10). This is
//    also why glow/alphaFade use the "FX path" defaults rather than the real a8/a9.
void WorldGeometryRenderer::renderObjects(const D3DXMATRIX& view, const D3DXMATRIX& proj,
                                          const D3DXVECTOR3& eye) {
    if (!ready_ || objects_.empty() || instances_.empty()) return;

    // Save the states we PERTURB (device SHARED with meshRenderer_ / FX/shadow/bloom neighbors).
    // FRONT C2: B1 MeshPart_RenderFull 0x6B0850 drives a material state machine
    // (blend/z-write/alpha-test/alpharef/alphafunc/texturefactor/specular + texture matrix)
    // that it restores to a FIXED BASELINE, not the entry state -> save everything here and
    // restore it fully on exit so nothing gets polluted.
    DWORD prevLighting = TRUE, prevCull = D3DCULL_CCW, prevBlend = FALSE, prevZWrite = TRUE,
          prevAlphaTest = FALSE, prevAlphaRef = 0, prevAlphaFunc = D3DCMP_GREATEREQUAL,
          prevSrc = D3DBLEND_SRCALPHA, prevDst = D3DBLEND_INVSRCALPHA, prevTexFactor = 0xFFFFFFFF,
          prevSpecular = FALSE;
    dev_->GetRenderState(D3DRS_LIGHTING, &prevLighting);
    dev_->GetRenderState(D3DRS_CULLMODE, &prevCull);
    dev_->GetRenderState(D3DRS_ALPHABLENDENABLE, &prevBlend);
    dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prevZWrite);
    dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &prevAlphaTest);
    dev_->GetRenderState(D3DRS_ALPHAREF, &prevAlphaRef);
    dev_->GetRenderState(D3DRS_ALPHAFUNC, &prevAlphaFunc);
    dev_->GetRenderState(D3DRS_SRCBLEND, &prevSrc);
    dev_->GetRenderState(D3DRS_DESTBLEND, &prevDst);
    dev_->GetRenderState(D3DRS_TEXTUREFACTOR, &prevTexFactor);
    dev_->GetRenderState(D3DRS_SPECULARENABLE, &prevSpecular);

    // Common device pre-condition (faithful to Model_RenderWithShadow_0 0x6a4110: SetFVF(274)
    // @0x6a4186, no shader). VS/PS off -> meshRenderer_'s bind cache is invalidated on exit.
    dev_->SetVertexShader(nullptr);
    dev_->SetPixelShader(nullptr);
    dev_->SetFVF(kFvfWo);                                 // 274 (0x112) @0x6a4186
    dev_->SetTransform(D3DTS_VIEW, &view);
    dev_->SetTransform(D3DTS_PROJECTION, &proj);
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);          // FF UNLIT: cf. banner — B1's LIT layers
                                                          // (emissive/glow/proj-light) stay INERT; this
                                                          // preserves the current textured render (no black).
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);   // build-safe (cf. banner)

    // EXACT BASELINE B1 restores to (restoreBlendTriplet 0x6b1440/0x6b1ce6: blend off, z-write on,
    // alpha-test off, ALPHAREF=0, ALPHAFUNC=GREATER(5), TEXTUREFACTOR=-1, SPECULAR off). Set BEFORE
    // the loop so decoded (B1) and non-decoded (base-draw) parts interleave on the SAME starting
    // state. B1 enables blend WITHOUT resetting the factors -> SRCALPHA/INVSRCALPHA are set here
    // (device permanent state, Gfx_InitDevice @0x69c526/@0x69c535, PROVEN values).
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev_->SetRenderState(D3DRS_ALPHAREF, 0);
    dev_->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);       // 25,5 (baseline B1)
    dev_->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev_->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);       // 60,-1 (baseline B1)
    dev_->SetRenderState(D3DRS_SPECULARENABLE, FALSE);

    // Stage 0: color = texture (SELECTARG1) on uv0; alpha = texture (required for B1's
    // alpha-test/blend). ALPHAARG2=CURRENT + TTFF=DISABLE = B1's restoration baseline.
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,  D3DTOP_SELECTARG1); // 4,2 (baseline B1)
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);    // 6,1 (baseline B1)
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE); // baseline UV-scroll
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    D3DXMATRIX texIdent; D3DXMatrixIdentity(&texIdent);
    dev_->SetTransform(D3DTS_TEXTURE0, &texIdent);        // baseline stage-0 texture matrix

    // animTime = v66 (MeshPart_RenderFull @0x6b0883: Terrain_PushRenderState() + a3): clock for
    // B1's animated layers (flipbook @0x6b0d78, UV-scroll @0x6b1016, ping-pong @0x6b08bb). DOCUMENTED
    // FALLBACK: the a3 phase at the call site (Terrain_Render -> Model_RenderWithShadow_0 0x6a4110,
    // arg a6) is not statically dumpable -> only the QPC-seconds part is used, approximated by
    // wavePhase_ (TickWorldAnim's dt-seconds accumulator). GLOBAL (not per instance), like the v66 QPC.
    const float animTime = wavePhase_;

    // cameraAt (camera target +804..812): FALLBACK from the view matrix's forward (LH row-vector,
    // column 3 = world sight direction; at = eye + forward). Only feeds Gfx_SetShadowProjLight
    // (glow, an inert LIT layer under LIGHTING=FALSE) -> reasonable value, zero effect. sunDir/
    // sceneCenter unavailable here -> left at (0,0,0): they only drive the inert LIT layers
    // (glow/emissive).
    const D3DXVECTOR3 camAt(eye.x + view._13, eye.y + view._23, eye.z + view._33);

    for (size_t idx = 0; idx < instances_.size(); ++idx) {
        const asset::AuxRecord& inst = instances_[idx];
        if (inst.modelIndex >= modelRanges_.size()) continue; // corrupt modelIndex, cf. Build() log
        const ModelRange& range = modelRanges_[inst.modelIndex];
        if (range.count == 0) continue;                       // template with no uploaded part

        // frame = Crt_Dbl2Uint(animPhase), gated [0, A-1] (Model_RenderWithShadow_0 0x6a4148/0x6a415e;
        // Model_RenderParts 0x6a3739/0x6a3756). instancePhase_ is wrapped [0,A) by TickWorldAnim ->
        // Dbl2Uint (truncation toward 0) yields [0,A-1].
        const uint32_t A     = (idx < instanceFrameCount_.size() && instanceFrameCount_[idx] > 0)
                                   ? instanceFrameCount_[idx] : 1u;
        const float    phase = (idx < instancePhase_.size()) ? instancePhase_[idx] : 0.0f;
        uint32_t frame = (phase > 0.0f) ? static_cast<uint32_t>(phase) : 0u; // Crt_Dbl2Uint (truncation)
        if (frame > A - 1) frame = A - 1;                     // gate (upper bound)

        const D3DXMATRIX world = BuildInstanceWorldMatrix(inst);
        dev_->SetTransform(D3DTS_WORLD, &world);              // SetTransform(256, m) @0x6a4299 (B1 PRE-CONDITION)

        for (size_t i = 0; i < range.count; ++i) {
            const StaticObject& obj = objects_[range.start + i];
            if (!obj.vb || !obj.ib || obj.B == 0 || obj.D == 0) continue;

            if (obj.mat.decoded) {
                // ===== FRONT C2 — FULL MATERIAL LAYER via B1 (replaces the base-draw). =====
                // Real path: Model_RenderWithShadow_0 0x6a4110 -> MeshPart_RenderFull 0x6b0850.
                // MeshPartGpu: A/B/D from the WO geometry (a1[63]/[64]/[66]) + VB (A frames) / IB.
                MeshPartGpu geo;
                geo.vb            = obj.vb;
                geo.ib            = obj.ib;
                geo.vertsPerFrame = obj.B;   // a1[64]/+256 (DrawIndexedPrimitive's numVerts)
                geo.triCount      = obj.D;   // a1[66]/+264 (primCount)
                geo.frameCount    = obj.A;   // a1[63]/+252 (B1 re-clamps the frame to A)
                geo.frameNodes    = nullptr; // 64*A node block (a1[71]/+284) NOT kept: it only feeds
                                             // the glow fresnel (mode 2) and the billboard — both inert
                                             // here (glow under LIGHTING=FALSE + sunDir=(0,0,0);
                                             // billboard = indexed-draw fallback with no node read).
                                             // nullptr = safe skip.

                // MeshPartTextures: base=tex1, second=tex2, flipbook=materials[] (already uploaded).
                MeshPartTextures tex;
                tex.base          = obj.diffuse;     // a1[86]/+344
                tex.baseMode      = obj.baseMode;    // a1[85]/+340
                tex.second        = obj.second;      // a1[99]/+396
                tex.secondMode    = obj.secondMode;  // a1[98]/+392
                tex.flipbook      = obj.flipbook.empty() ? nullptr : obj.flipbook.data(); // a1[101]/+404
                tex.flipbookCount = static_cast<uint32_t>(obj.flipbook.size());           // a1[100]/+400

                // MeshPartRuntime from the REAL camera/scene (documented fallback for sunDir/sceneCenter).
                MeshPartRuntime rt;
                rt.world      = world;   // dword_800244 (already set as D3DTS_WORLD above)
                rt.worldValid = true;
                rt.cameraEye  = eye;     // g_CameraPos 0x800130
                rt.cameraAt   = camAt;   // camera target (view forward fallback, cf. above)
                // rt.sunDir / rt.sceneCenter: (0,0,0) by default (unavailable; LIT layers are inert).

                // frame = morph frame (a2); animTime = v66 (a3). glowEnable=1 / alphaFade=0 /
                // decal=nullptr = B1's DOCUMENTED "FX path" defaults: the real a8/a9/a6 at the
                // Terrain_Render call site are not statically dumpable (cf. .h renderObjects banner).
                MeshPartMaterialRenderer::Render(dev_, obj.mat, geo, tex,
                                                 static_cast<int>(frame), animTime, rt);
            } else {
                // ===== mat.decoded == false: CURRENT BASE-DRAW PRESERVED (unchanged). =====
                // IDA anchor: MeshPart_Render 0x6aed60 (SetStreamSource 0x6aeea5 / SetIndices 0x6aeedc /
                // DrawIndexedPrimitive 0x6aef00). Frame swap via stream offset = 32*frame*B.
                // Defensive bound: if A is not uniform across parts, do not overrun this part's VB.
                const uint32_t pf = (frame < obj.A) ? frame : (obj.A - 1);
                dev_->SetTexture(0, obj.diffuse);
                dev_->SetStreamSource(0, obj.vb, 32u * pf * obj.B, 32u);                 // 0x6aeea5
                dev_->SetIndices(obj.ib);                                                // 0x6aeedc
                dev_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, obj.B, 0, obj.D);   // 0x6aef00
            }
        }
    }

    // Restore: B1 baseline -> REAL entry state (do not pollute FX/shadow/bloom neighbors).
    dev_->SetTexture(0, nullptr);
    dev_->SetTexture(1, nullptr);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);            // FF default value
    dev_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev_->SetTransform(D3DTS_TEXTURE0, &texIdent);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, prevBlend);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, prevZWrite);
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, prevAlphaTest);
    dev_->SetRenderState(D3DRS_ALPHAREF, prevAlphaRef);
    dev_->SetRenderState(D3DRS_ALPHAFUNC, prevAlphaFunc);
    dev_->SetRenderState(D3DRS_SRCBLEND, prevSrc);
    dev_->SetRenderState(D3DRS_DESTBLEND, prevDst);
    dev_->SetRenderState(D3DRS_TEXTUREFACTOR, prevTexFactor);
    dev_->SetRenderState(D3DRS_SPECULARENABLE, prevSpecular);
    dev_->SetRenderState(D3DRS_LIGHTING, prevLighting);
    dev_->SetRenderState(D3DRS_CULLMODE, prevCull);
    dev_->LightEnable(1, FALSE); // B1 may enable light slot 1 (glow); guarantee OFF on exit
    meshRenderer_.InvalidateShaderBindingCache();          // the next skinned draw rebinds VS/PS
}

} // namespace ts2::gfx

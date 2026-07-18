// Gfx/ModelObjectRenderer.cpp — see ModelObjectRenderer.h for the full map (original
// chain, MiscC bank, minimal slice, assumed degradations). IDA ground truth: TwelveSky2.exe
// (idaTs2, imagebase 0x400000). Each block carries its anchor.
#include "Gfx/ModelObjectRenderer.h"
#include "Core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

template <class T>
void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Fixed-function FVF of a MOBJECT vertex (MeshPart_Load 0x6AD160: 32-byte vertex):
// 0x112 = 274 = D3DFVF_XYZ|NORMAL|TEX1. IDENTICAL to .WO (kFvfWo, Model_RenderParts 0x6a377f).
constexpr DWORD kFvfMobj = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1; // 0x112
static_assert(kFvfMobj == 0x112, "MOBJECT FVF must equal 274 (0x112)");

// On-disk strides (Docs/TS2_ASSET_FORMATS.md; MeshPart_Load): 32-byte vertex, index16 (6 bytes/face).
constexpr size_t kMobjVertexStride = 32;
constexpr size_t kFaceStride       = 6;   // 3 indices * 2 bytes

// Number of slots in the MiscC bank (unk_B60AB8): loop AssetMgr_InitAllSlots 0x4DEB50
// @0x4dee8c `for(i6 = 0; i6 < 246; ++i6)`.
constexpr int kMiscCSlotCount = 246;

// Same join convention as Gfx/ModelCache.cpp::JoinPath / Game/GameDatabase.cpp (separator '\').
std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// ---------------------------------------------------------------------------------------------
//  Frustum reconstructed from vp = view*proj (Gribb-Hartmann), INWARD-facing NORMALIZED planes.
//  The binary reads its 6 planes at g_GfxRenderer+334 (see decompilation of Cam_FrustumTestSphere
//  0x69EF90); ClientSource has no such singleton -> we RECONSTRUCT them, which yields the
//  SAME planes (world space, inside <=> a*x+b*y+c*z+d >= 0). Normalized planes are mandatory:
//  the -radius margin of the sphere test is in world units. (Mirrors WorldGeometryRenderer::
//  ExtractFrustum, but margin ×1 = Cam_FrustumTestSphere instead of the world's ×2.)
// ---------------------------------------------------------------------------------------------
void ExtractFrustumPlanes(const D3DXMATRIX& vp, float pl[6][4]) {
    // Left=col3+col0, Right=col3-col0, Bottom=col3+col1, Top=col3-col1, Near=col2, Far=col3-col2.
    pl[0][0]=vp._14+vp._11; pl[0][1]=vp._24+vp._21; pl[0][2]=vp._34+vp._31; pl[0][3]=vp._44+vp._41; // left
    pl[1][0]=vp._14-vp._11; pl[1][1]=vp._24-vp._21; pl[1][2]=vp._34-vp._31; pl[1][3]=vp._44-vp._41; // right
    pl[2][0]=vp._14+vp._12; pl[2][1]=vp._24+vp._22; pl[2][2]=vp._34+vp._32; pl[2][3]=vp._44+vp._42; // bottom
    pl[3][0]=vp._14-vp._12; pl[3][1]=vp._24-vp._22; pl[3][2]=vp._34-vp._32; pl[3][3]=vp._44-vp._42; // top
    pl[4][0]=vp._13;        pl[4][1]=vp._23;        pl[4][2]=vp._33;        pl[4][3]=vp._43;        // near
    pl[5][0]=vp._14-vp._13; pl[5][1]=vp._24-vp._23; pl[5][2]=vp._34-vp._33; pl[5][3]=vp._44-vp._43; // far
    for (int p = 0; p < 6; ++p) {
        const float len = std::sqrt(pl[p][0]*pl[p][0] + pl[p][1]*pl[p][1] + pl[p][2]*pl[p][2]);
        if (len > 1e-8f) { const float inv = 1.0f / len; for (int k = 0; k < 4; ++k) pl[p][k] *= inv; }
    }
}

// ---------------------------------------------------------------------------------------------
//  Active renderer for the hook shim (FxModelObjDrawFn is a FREE function pointer: it cannot
//  capture an instance -> we keep the active instance in a file-scope variable, as
//  FxRenderer.cpp does for s_fxDevice/s_particleRender). Single writer: Init/Shutdown.
// ---------------------------------------------------------------------------------------------
ModelObjectRenderer* s_active = nullptr;

} // namespace

// ===========================================================================
//  Init / Shutdown
// ===========================================================================

ModelObjectRenderer::~ModelObjectRenderer() { Shutdown(); }

bool ModelObjectRenderer::Init(Renderer& renderer, std::string gameDataDir) {
    dev_ = renderer.Device();
    if (!dev_) { TS2_ERR("ModelObjectRenderer::Init : device nul"); return false; }
    gameDataDir_ = std::move(gameDataDir);
    ready_       = true;
    frameValid_  = false;
    s_active     = this; // registers for the hook shim (ModelObjectRenderer_MeshDrawShim)
    TS2_LOG("ModelObjectRenderer pret (banque MiscC E*.MOBJECT, %d slots).", kMiscCSlotCount);
    return true;
}

void ModelObjectRenderer::Shutdown() {
    if (s_active == this) s_active = nullptr;
    releaseAll();
    dev_        = nullptr;
    ready_      = false;
    frameValid_ = false;
}

// D3DPOOL_MANAGED: the cache's VB/IB/textures survive a Reset() (restored by D3D9).
// No-op, like WorldGeometryRenderer. NOTE: on a full device RECREATION (dev_ changes),
// MAIN must call Shutdown()+Init() to purge the cache (resources tied to the old device).
void ModelObjectRenderer::OnDeviceLost() {}
void ModelObjectRenderer::OnDeviceReset() {}

void ModelObjectRenderer::releaseEntry(MObjEntry& e) {
    for (GpuPart& p : e.parts) {
        SafeRelease(p.vb); SafeRelease(p.ib);
        SafeRelease(p.diffuse); SafeRelease(p.second);      // tex0 + tex1 owned
        for (IDirect3DTexture9* ft : p.flipbook) SafeRelease(ft); // owned flipbook atlas
        p.flipbook.clear();
    }
    e.parts.clear();
}

void ModelObjectRenderer::releaseAll() {
    for (auto& kv : cacheMiscC_) releaseEntry(kv.second);
    cacheMiscC_.clear();
}

// ===========================================================================
//  SetFrame — frame frustum planes (for per-part culling).
// ===========================================================================
void ModelObjectRenderer::SetFrame(const D3DXMATRIX& view, const D3DXMATRIX& proj) {
    D3DXMATRIX vp;
    D3DXMatrixMultiply(&vp, &view, &proj);
    ExtractFrustumPlanes(vp, planes_);

    // World camera eye/target for MeshPartRuntime (fresnel glow @0x6b0a90 + proj-light direction
    // = target - eye @0x69d8ae). g_GfxRenderer 0x7FFE18 (eye g_CameraPos 0x800130, target +804) is
    // absent -> we DERIVE them from the view matrix (the frame's REAL source, no invention):
    //   eye = inverse(view) translation; world forward = (view._13, view._23, view._33) = zaxis
    //   (D3DXMatrixLookAtLH); target = eye + forward. Proj-light direction = target - eye = forward.
    D3DXMATRIX invView;
    if (D3DXMatrixInverse(&invView, nullptr, &view)) {
        cameraEye_ = D3DXVECTOR3(invView._41, invView._42, invView._43);
        cameraAt_  = D3DXVECTOR3(cameraEye_.x + view._13, cameraEye_.y + view._23, cameraEye_.z + view._33);
    }
    frameValid_ = true;
}

// Material animation clock (v66) — QPC timer, seconds elapsed since the 1st call (local
// origin = relative phase). Mirrors Terrain_PushRenderState 0x69CB80 ((now - start)/freq, see
// WorldGeometryRenderer.cpp) and EmitterMeshRenderer::ElapsedSeconds (QPC @0x430C16/@0x430C34).
float ModelObjectRenderer::animClockSeconds() {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!qpcInit_) {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        qpcFreq_  = (f.QuadPart != 0) ? f.QuadPart : 1;
        qpcStart_ = now.QuadPart;
        qpcInit_  = true;
    }
    return static_cast<float>(double(now.QuadPart - qpcStart_) / double(qpcFreq_));
}

// Cam_FrustumTestSphere 0x69EF90: sphere kept IFF plane·c + d >= -radius for all 6 planes
// (margin ×1: `v4 = a3 * -1.0` @0x69ef9e). Conservative (never over-culls a visible object).
bool ModelObjectRenderer::sphereInFrustum(const D3DXVECTOR3& c, float radius) const {
    const float v4 = radius * -1.0f;                         // 0x69ef9e
    for (int p = 0; p < 6; ++p)
        if (planes_[p][0]*c.x + planes_[p][1]*c.y + planes_[p][2]*c.z + planes_[p][3] < v4)
            return false;                                    // 0x69f0ca (chain of &&)
    return true;
}

// ===========================================================================
//  World matrix — Rz*Ry*Rx*T (Model_RenderWithShadow_0 0x6a41a3-0x6a4299).
// ===========================================================================
D3DXMATRIX ModelObjectRenderer::BuildWorldMatrix(const float pos[3], const float* orient) {
    constexpr float kDegToRad = 0.017453292f; // pi/180, binary literal (Model_RenderWithShadow_0 @0x6a41bc)
    const float rx = orient ? orient[0] : 0.0f;
    const float ry = orient ? orient[1] : 0.0f;
    const float rz = orient ? orient[2] : 0.0f;
    D3DXMATRIX t, mrx, mry, mrz, m;
    D3DXMatrixTranslation(&t, pos[0], pos[1], pos[2]);       // T   @0x6a41a3
    D3DXMatrixRotationX(&mrx, rx * kDegToRad);               // Rx  @0x6a41c0
    D3DXMatrixRotationY(&mry, ry * kDegToRad);               // Ry  @0x6a41da
    D3DXMatrixRotationZ(&mrz, rz * kDegToRad);               // Rz  @0x6a41f4
    D3DXMatrixMultiply(&m, &mrz, &mry);                      // M = Rz*Ry
    D3DXMatrixMultiply(&m, &m, &mrx);                        // M = M*Rx
    D3DXMatrixMultiply(&m, &m, &t);                          // M = M*T
    return m;
}

// ===========================================================================
//  Diffuse texture — from asset::MTexture (DDS image + 8-byte trailer, already decompressed by
//  asset::MObject::Load; Tex_LoadCompressedFromHandle 0x6A9CF0). We pass `imgSize` bytes to
//  D3DX (the trailing 8-byte trailer = processMode/alphaMode, outside the DDS image).
// ===========================================================================
IDirect3DTexture9* ModelObjectRenderer::createTexture(IDirect3DDevice9* dev, const asset::MTexture& tex) {
    if (!dev || !tex.present || tex.image.empty() || tex.imgSize == 0) return nullptr;
    const UINT sz = static_cast<UINT>((std::min)(static_cast<size_t>(tex.imgSize), tex.image.size()));
    if (sz == 0) return nullptr;
    IDirect3DTexture9* out = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev, tex.image.data(), sz,
        D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN,
        D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &out);
    if (FAILED(hr)) {
        TS2_WARN("ModelObjectRenderer: creation texture MOBJECT echouee (0x%08lX)", hr);
        return nullptr;
    }
    return out;
}

// ===========================================================================
//  Uploading one part — VB 32*A*B (A contiguous frames), shared IB 6*D, diffuse tex0.
//  Mirrors MeshPart_Load 0x6AD160 (CreateVertexBuffer @0x6ad3a2, CreateIndexBuffer @0x6ad64c)
//  and WorldGeometryRenderer::uploadPart (same GPU pattern, source asset::MeshPart).
//  The parser (asset::MObject::Load) has ALREADY split geo.vertices (32*M*V), geo.indices (6*I)
//  and geo.matrices (M*64): we memcpy directly, no re-slicing from a raw blob.
// ===========================================================================
bool ModelObjectRenderer::uploadPart(const asset::MeshPart& part, GpuPart& out) {
    if (!part.hasMesh) return false;                         // MeshPart+128 = 0: empty part
    const asset::MGeometry& g = part.geo;
    if (g.M == 0 || g.V == 0 || g.I == 0) return false;      // no frame/vertex/face

    const size_t vbBytes = kMobjVertexStride * static_cast<size_t>(g.M) * g.V; // 32*A*B
    const size_t ibBytes = kFaceStride * static_cast<size_t>(g.I);            // 6*D
    if (g.vertices.size() < vbBytes || g.indices.size() < ibBytes) {
        TS2_WARN("ModelObjectRenderer: part incoherente (vtx=%zu/%zu, idx=%zu/%zu)",
                 g.vertices.size(), vbBytes, g.indices.size(), ibBytes);
        return false;
    }

    HRESULT hr = dev_->CreateVertexBuffer(static_cast<UINT>(vbBytes), 0, kFvfMobj,
                                          D3DPOOL_MANAGED, &out.vb, nullptr);
    if (FAILED(hr)) { TS2_ERR("ModelObjectRenderer: CreateVertexBuffer echoue (0x%08lX)", hr); return false; }
    void* p = nullptr;
    if (SUCCEEDED(out.vb->Lock(0, static_cast<UINT>(vbBytes), &p, 0))) {
        std::memcpy(p, g.vertices.data(), vbBytes);          // A raw frames (no conversion)
        out.vb->Unlock();
    }

    hr = dev_->CreateIndexBuffer(static_cast<UINT>(ibBytes), 0, D3DFMT_INDEX16,
                                 D3DPOOL_MANAGED, &out.ib, nullptr);
    if (FAILED(hr)) {
        TS2_ERR("ModelObjectRenderer: CreateIndexBuffer echoue (0x%08lX)", hr);
        SafeRelease(out.vb);
        return false;
    }
    if (SUCCEEDED(out.ib->Lock(0, static_cast<UINT>(ibBytes), &p, 0))) {
        std::memcpy(p, g.indices.data(), ibBytes);
        out.ib->Unlock();
    }

    out.A = g.M; out.B = g.V; out.D = g.I;
    out.frameBbox = g.matrices;                              // A*64 bytes (per-part cull + fresnel nodes; empty if corrupted)
    out.diffuse   = createTexture(dev_, part.tex0);          // diffuse/base tex0 (MeshPart+296/+344)

    // FRONT C1: material layer (B1 wiring). Modes = trailer1 (alphaMode) of the texture block —
    // Tex_LoadCompressedFromHandle 0x6A9CF0 @0x6a9eab stores this[11]=data[imgSize+4] at holder+44,
    // i.e. MeshPart+340 (tex0, a1[85]) / +392 (tex1, a1[98]) read by MeshPart_RenderFull.
    out.baseMode   = static_cast<int>(part.tex0.trailer1);  // MeshPart+340 (a1[85])
    out.second     = createTexture(dev_, part.tex1);         // tex1 (MeshPart_Load @0x6ad6eb, holder +348 / ptr +396)
    out.secondMode = static_cast<int>(part.tex1.trailer1);  // MeshPart+392 (a1[98])
    out.mat        = part.mat;                               // decoded 120-byte material header (DecodeMeshPartMaterialHeader)
    // Flipbook (a1[101] MeshPart+404, count=a1[100]=matCount): loop Tex_LoadCompressedFromHandle
    // @0x6ad7a0. We upload each MTexture; we only keep the created textures (indices valid
    // for MeshPart_RenderFull @0x6b0d95 `idx % this[100]`, which tests flipbook[0] non-null @0x6b0d33).
    out.flipbook.reserve(part.mats.size());
    for (const asset::MTexture& mt : part.mats) {
        IDirect3DTexture9* ft = createTexture(dev_, mt);
        if (ft) out.flipbook.push_back(ft);
    }
    return true;
}

// ===========================================================================
//  getOrLoadMiscC — SYNCHRONOUS lazy-load (mirrors ModelObj_Load 0x4D6F80 a6=1 +
//  Model_LoadFromFile 0x6A3490). Resolves E{idxC+1}001.MOBJECT (category 4).
// ===========================================================================
ModelObjectRenderer::MObjEntry* ModelObjectRenderer::getOrLoadMiscC(int idxC) {
    if (idxC < 0 || idxC >= kMiscCSlotCount) return nullptr; // outside MiscC bank (unk_B60AB8, 246 slots)

    auto found = cacheMiscC_.find(idxC);
    if (found != cacheMiscC_.end())
        return found->second.loadFailed ? nullptr : &found->second;

    // 1st access: builds the path (ModelObj_BuildPath 0x4D6E20 category 4 @0x4d6ed5):
    //   <gameDataDir>\G03_GDATA\D02_GMOBJECT\003\E{idxC+1:03}001.MOBJECT
    MObjEntry entry;
    char name[32];
    std::snprintf(name, sizeof(name), "E%03d001.MOBJECT", idxC + 1);
    std::string path = JoinPath(JoinPath(JoinPath(gameDataDir_, "G03_GDATA\\D02_GMOBJECT"), "003"), name);

    asset::MObject mo;
    if (!mo.Load(path)) {
        TS2_WARN("ModelObjectRenderer: echec chargement '%s' (%s)", path.c_str(), mo.error().c_str());
        entry.loadFailed = true;
    } else {
        // frameCountA = parts[0].A (frame gate bound = *(parts+252), Model_RenderWithShadow_0
        // @0x6a415e) — read on the FIRST part like the binary, independent of uploaded parts.
        entry.frameCountA = mo.parts().empty() ? 0u : mo.parts()[0].geo.M;
        for (const asset::MeshPart& part : mo.parts()) {
            GpuPart gp;
            if (uploadPart(part, gp)) entry.parts.push_back(std::move(gp));
        }
        entry.loaded = true; // valid MObject (even with 0 usable parts: do not retry)
        TS2_LOG("ModelObjectRenderer: '%s' charge (%zu part(s) GPU, A=%u, resident=%zu)",
                name, entry.parts.size(), entry.frameCountA, cacheMiscC_.size() + 1);
    }

    auto [ins, inserted] = cacheMiscC_.emplace(idxC, std::move(entry));
    (void)inserted;
    return ins->second.loadFailed ? nullptr : &ins->second;
}

// Number of flipbook frames for a MiscC slot (A = parts[0].geo.M, see getOrLoadMiscC).
uint32_t ModelObjectRenderer::FrameCount(int idxC) {
    MObjEntry* e = getOrLoadMiscC(idxC);
    return e ? e->frameCountA : 0u;
}

// ===========================================================================
//  MeshDraw — ModelObj_Draw 0x4D71B0 + Model_RenderWithShadow_0 0x6A4110 (setup + per-part cull)
//  then, per-part, MeshPartMaterialRenderer::Render (= MeshPart_RenderFull 0x6B0850, FRONT C1).
// ===========================================================================
void ModelObjectRenderer::MeshDraw(FxMeshBank bank, int /*idxA*/, int /*idxB*/, int idxC,
                                   int pass, float drawParam, const float pos[3], const float* orient) {
    if (!ready_ || !dev_ || !pos) return;
    // V1: a single bank (MiscC = types 8/9/0xA of Fx_EmitterDraw). AvatarA (1/2) / NpcB (3/4)
    // -> TODO anchor (unk_A71410 @0x585EB3 / unk_B551B8 @0x585F73): same 148-byte ModelObj, but
    // distinct bank/category (cat 1 `C%03d%03d` / cat 3 `M%03d001`). Not handled here.
    if (bank != FxMeshBank::MiscC) return;
    // Model_RenderWithShadow_0: pass gate a2 in [1,2] (@0x6a412c/@0x6a4135).
    if (pass < 1 || pass > 2) return;

    MObjEntry* e = getOrLoadMiscC(idxC);
    if (!e || !e->loaded || e->parts.empty()) return;        // ModelObj_Draw: draw gated by loading

    // frame = Crt_Dbl2Uint(a3) (truncation toward 0), gate [0, parts[0].A-1] (@0x6a4148/@0x6a415e).
    const int frame = (drawParam > 0.0f) ? static_cast<int>(drawParam) : 0;
    if (frame < 0 || frame > static_cast<int>(e->frameCountA) - 1) return;

    const D3DXMATRIX world = BuildWorldMatrix(pos, orient); // SetTransform(256, ...) @0x6a4299

    // --- Snapshot of modified device states: this shim is called WITHIN passes 1/2 of
    //     Fx_EmitterDraw, in between states set for billboard pass 3 (SceneManager sets the
    //     Gfx_BeginUnlitPass bracket BEFORE the 3 passes). So we restore EVERYTHING we change
    //     so the particle pass stays correct. "Polished" mirror of ModelObj_Draw's one-shot
    //     state reset (dword_8E7178: RS 25=5, 19=5, 20=6 @0x4d7206-@0x4d723b) + base FF. ---
    IDirect3DVertexShader9* oldVS = nullptr; dev_->GetVertexShader(&oldVS);
    IDirect3DPixelShader9*  oldPS = nullptr; dev_->GetPixelShader(&oldPS);
    DWORD oldFvf = 0;                        dev_->GetFVF(&oldFvf);
    DWORD oldLighting = TRUE, oldCull = D3DCULL_CCW, oldABE = FALSE;
    DWORD oldSrc = D3DBLEND_ONE, oldDst = D3DBLEND_ZERO, oldAFunc = D3DCMP_ALWAYS;
    dev_->GetRenderState(D3DRS_LIGHTING, &oldLighting);
    dev_->GetRenderState(D3DRS_CULLMODE, &oldCull);
    dev_->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldABE);
    dev_->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
    dev_->GetRenderState(D3DRS_DESTBLEND, &oldDst);
    dev_->GetRenderState(D3DRS_ALPHAFUNC, &oldAFunc);
    // FRONT C1 extension: MeshPartMaterialRenderer::Render ALSO modifies these states (per-part,
    // then resets them to ITS "clean" state via restoreBlendTriplet — see MeshPart_RenderFull). We
    // snapshot them so MeshDraw stays SELF-CONTAINED (billboard pass 3 finds ITS exact state again).
    // (D3DMATERIAL9 material + lights 0/1: Render restores them to ITS entry snapshot = our current
    //  state, AND they are INERT under LIGHTING=FALSE -> not re-snapshotted here. See §CAVEAT in the .h.)
    DWORD oldZW=TRUE, oldATE=FALSE, oldARef=0, oldSpec=FALSE, oldTF=0xFFFFFFFF;
    dev_->GetRenderState(D3DRS_ZWRITEENABLE,    &oldZW);
    dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &oldATE);
    dev_->GetRenderState(D3DRS_ALPHAREF,        &oldARef);
    dev_->GetRenderState(D3DRS_SPECULARENABLE,  &oldSpec);
    dev_->GetRenderState(D3DRS_TEXTUREFACTOR,   &oldTF);
    DWORD s0co=0, s0c1=0, s0ao=0, s0a1=0, s0tci=0, s1co=0;
    dev_->GetTextureStageState(0, D3DTSS_COLOROP,       &s0co);
    dev_->GetTextureStageState(0, D3DTSS_COLORARG1,     &s0c1);
    dev_->GetTextureStageState(0, D3DTSS_ALPHAOP,       &s0ao);
    dev_->GetTextureStageState(0, D3DTSS_ALPHAARG1,     &s0a1);
    dev_->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &s0tci);
    dev_->GetTextureStageState(1, D3DTSS_COLOROP,       &s1co);
    DWORD s0aa2=D3DTA_CURRENT, s0ttf=D3DTTFF_DISABLE;    // Render touches ALPHAARG2 + TEXTURETRANSFORMFLAGS (stage 0)
    dev_->GetTextureStageState(0, D3DTSS_ALPHAARG2,             &s0aa2);
    dev_->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &s0ttf);
    IDirect3DBaseTexture9* oldTex0 = nullptr; dev_->GetTexture(0, &oldTex0);
    D3DXMATRIX oldWorld;  dev_->GetTransform(D3DTS_WORLD,    &oldWorld);
    D3DXMATRIX oldTex0M;  dev_->GetTransform(D3DTS_TEXTURE0, &oldTex0M); // UV-scroll sets a tex matrix (@0x6b1067)

    // --- Base-draw FF states (FX path a4=0/a6=0: neither projected texture nor alpha-fade) ---
    // Render PRE-CONDITION (faithful to Model_RenderWithShadow_0 @0x6a4299): VS/PS cleared (pure FF) +
    // FVF(274) + WORLD set BEFORE Render; Render does NOT touch VS/PS/FVF/WORLD.
    // SHADER-BIND-CACHE TRAP (SHARED D3D9 device, 2 singletons): we clear VS/PS here — BUT we
    // symmetrically RESTORE them at function end (SetVertexShader(oldVS)/SetPixelShader(oldPS), on
    // ALL paths: NO return between the clear and the restore). The device therefore stays
    // NET-UNCHANGED (identical VS/PS/FVF before/after the call) -> no MeshRenderer sees its
    // currentPass_ go stale (the bug only occurs if VS/PS stay cleared AFTERWARD, see
    // Gfx/MeshRenderer.h::InvalidateShaderBindingCache). Unlike WorldGeometryRenderer (which
    // clears VS/PS then draws skinned WITHOUT restoring -> it MUST invalidate), here the symmetric
    // restore IS the guarantee; besides, no MeshRenderer is reachable from this renderer.
    dev_->SetVertexShader(nullptr);                          // clears any skinned VS (ModelObj_Draw @0x4d7266)
    dev_->SetPixelShader(nullptr);
    dev_->SetFVF(kFvfMobj);                                  // 274 (0x112)
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);             // unlit FF (pure diffuse)
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);      // build-safe (FX often double-sided)
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);      // "standard alpha-blend" (effects)
    dev_->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);    // ModelObj_Draw reset 19=5 @0x4d7220
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA); // reset 20=6 @0x4d723b
    dev_->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);       // reset 25=5 @0x4d7206 (inert without alpha-test)
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,       D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1,     D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,       D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1,     D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP,       D3DTOP_DISABLE);
    dev_->SetTransform(D3DTS_WORLD, &world);

    // --- Shared runtime state for the B1 material layer (identical for all parts) ---
    const float animTime = animClockSeconds();  // v66 = Terrain_PushRenderState()+a3, a3=0 (ModelObj_Draw @0x4d72af)
    MeshPartRuntime rt;
    rt.world       = world;                     // dword_800244 (already set to D3DTS_WORLD above)
    rt.worldValid  = true;
    rt.cameraEye   = frameValid_ ? cameraEye_ : D3DXVECTOR3(0.0f, 0.0f, 0.0f); // documented fallback if SetFrame not called
    rt.cameraAt    = frameValid_ ? cameraAt_  : D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    rt.sunDir      = D3DXVECTOR3(0.0f, 0.0f, 0.0f); // flt_800308/30C/310 UNAVAILABLE -> fresnel dot=0 => no flash (safe)
    rt.sceneCenter = D3DXVECTOR3(0.0f, 0.0f, 0.0f); // scene AABB center (g_GfxRenderer+1204*0.5+1236) UNAVAILABLE -> fallback (inert under LIGHTING=FALSE)

    // --- Loop over parts (408-byte stride on the binary side), per-part cull + B1 material layer ---
    for (const GpuPart& p : e->parts) {
        if (!p.vb || !p.ib || p.B == 0 || p.D == 0) continue;
        // Opaque/transparent pass partition (Model_RenderWithShadow_0 0x6A4110): each part is
        // only drawn in ITS pass. Pass 1 = opaque @0x6a4560 (flipbook inactive AND
        // baseMode!=2); pass 2 = transparent @0x6a43a3 (flipbook active [this[53] && *(this[101]+48)]
        // OR baseMode this[85]==2). Without this filter, every FX part was drawn in BOTH pass 1 AND 2
        // -> additive over-brightening (double-draw). (Audit-C, verified in IDA.)
        const bool flipbookActive = p.mat.flipbook.Enable && !p.flipbook.empty();
        const bool isTransparent  = flipbookActive || (p.baseMode == 2);
        if ((pass == 1) == isTransparent) continue;
        // A can differ between parts (the binary assumes A is uniform and doesn't re-bound): we
        // defensively clamp to the part's VB to avoid reading out of buffer.
        const uint32_t pf = (frame < static_cast<int>(p.A)) ? static_cast<uint32_t>(frame) : (p.A - 1);

        // Per-part frustum cull (Cam_FrustumTestSphere 0x69EF90, margin ×1): local center of the
        // frame (frameBbox[pf].+48) transformed into world space, radius (+60). Skipped if frame not
        // provided (SetFrame not called) or bbox missing -> draw (never over-culls). @0x6a431b/@0x6a4339.
        if (frameValid_ && p.frameBbox.size() >= static_cast<size_t>(pf) * 64 + 64) {
            const uint8_t* elem = p.frameBbox.data() + static_cast<size_t>(pf) * 64;
            D3DXVECTOR3 localC; std::memcpy(&localC, elem + 48, sizeof(localC)); // center @+48
            float radius = 0.0f; std::memcpy(&radius, elem + 60, sizeof(radius)); // radius @+60
            D3DXVECTOR3 worldC; D3DXVec3TransformCoord(&worldC, &localC, &world);
            if (!sphereInFrustum(worldC, radius)) continue;
        }

        if (p.mat.decoded) {
            // === B1 wiring: COMPLETE material state machine (MeshPart_RenderFull 0x6B0850,
            //     called by Model_RenderWithShadow_0 @0x6a4362/@0x6a45f7). Replaces the base-draw. ===
            MeshPartGpu geo;
            geo.vb            = p.vb;
            geo.ib            = p.ib;
            geo.vertsPerFrame = p.B;                                            // B = this[64] (+256)
            geo.triCount      = p.D;                                            // D = this[66] (+264)
            geo.frameCount    = p.A;                                            // A = this[63] (+252) — Render re-clamps frame
            geo.frameNodes    = p.frameBbox.empty() ? nullptr : p.frameBbox.data(); // this[71] (+284), center @+48
            MeshPartTextures tx;
            tx.base          = p.diffuse;                                       // this[86] (+344)
            tx.baseMode      = p.baseMode;                                      // this[85] (+340)
            tx.second        = p.second;                                        // this[99] (+396)
            tx.secondMode    = p.secondMode;                                    // this[98] (+392)
            tx.flipbook      = p.flipbook.empty() ? nullptr : p.flipbook.data(); // this[101] (+404)
            tx.flipbookCount = static_cast<uint32_t>(p.flipbook.size());        // this[100] (+400)
            // PROVEN FX path (ModelObj_Draw 0x4D71B0 @0x4d72af -> Model_RenderWithShadow_0(…,0.0,0,1,0)):
            // animTime phase=0, decal=null, glowEnable=1 (a8), alphaFade=0 (a9). frame=v10 (Render re-clamps).
            MeshPartMaterialRenderer::Render(dev_, p.mat, geo, tx, frame, animTime, rt,
                                             /*glowEnable*/ 1, /*alphaFade*/ 0, /*decal*/ nullptr);
        } else {
            // Fallback: material header NOT decoded (degenerate part) -> current base-draw UNCHANGED.
            dev_->SetTexture(0, p.diffuse);                                     // SetTexture(0, tex0)
            dev_->SetStreamSource(0, p.vb, 32u * pf * p.B, 32u);                // 32*frame*B @0x6b1327
            dev_->SetIndices(p.ib);                                            // @0x6b133c
            dev_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, p.B, 0, p.D);  // @0x6b1360
        }
    }

    // --- Restore (self-contained: billboard pass 3 finds its state again) ---
    dev_->SetTransform(D3DTS_TEXTURE0, &oldTex0M);           // texture matrix (UV-scroll) — FRONT C1
    dev_->SetTransform(D3DTS_WORLD,    &oldWorld);
    dev_->SetTexture(0, oldTex0); if (oldTex0) oldTex0->Release();
    dev_->SetTextureStageState(1, D3DTSS_COLOROP,              s1co);
    dev_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, s0ttf);         // FRONT C1
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2,            s0aa2);          // FRONT C1
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX,        s0tci);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1,            s0a1);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,              s0ao);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1,            s0c1);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,              s0co);
    dev_->SetRenderState(D3DRS_TEXTUREFACTOR,   oldTF);                         // FRONT C1
    dev_->SetRenderState(D3DRS_SPECULARENABLE,  oldSpec);                       // FRONT C1
    dev_->SetRenderState(D3DRS_ALPHAREF,        oldARef);                       // FRONT C1
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, oldATE);                        // FRONT C1
    dev_->SetRenderState(D3DRS_ZWRITEENABLE,    oldZW);                         // FRONT C1
    dev_->SetRenderState(D3DRS_ALPHAFUNC, oldAFunc);
    dev_->SetRenderState(D3DRS_DESTBLEND, oldDst);
    dev_->SetRenderState(D3DRS_SRCBLEND, oldSrc);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, oldABE);
    dev_->SetRenderState(D3DRS_CULLMODE, oldCull);
    dev_->SetRenderState(D3DRS_LIGHTING, oldLighting);
    dev_->SetFVF(oldFvf);
    dev_->SetVertexShader(oldVS); if (oldVS) oldVS->Release();
    dev_->SetPixelShader(oldPS);  if (oldPS) oldPS->Release();
}

// ---------------------------------------------------------------------------
//  Free shim for the FxModelObjDrawFn hook — forwards to the active renderer.
void ModelObjectRenderer_MeshDrawShim(FxMeshBank bank, int idxA, int idxB, int idxC,
                                      int pass, float drawParam,
                                      const float pos[3], const float* orient) {
    if (s_active)
        s_active->MeshDraw(bank, idxA, idxB, idxC, pass, drawParam, pos, orient);
}

} // namespace ts2::gfx

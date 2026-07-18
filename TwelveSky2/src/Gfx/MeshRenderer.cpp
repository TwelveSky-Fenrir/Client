// Gfx/MeshRenderer.cpp - core setup/upload/draw-dispatch for GXD GPU-skinned rendering.
// See MeshRenderer.h for the reverse-engineering anchors. Split family:
// MeshRenderer_Skinning.cpp (per-subset skinned draw path) and
// MeshRenderer_Shadow.cpp (stencil shadow volume + planar shadow).
#include "Gfx/MeshRenderer.h"
#include "Gfx/MeshRenderer_Internal.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"

#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

template <class T>
void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// ---------------------------------------------------------------------------
//  Skinned shader program - faithful reconstruction of Shader03 (VS SkinnedLit)
//  + Shader04 (textured PS). ex-VeryOldClient: mAmbient2_VS / mAmbient2_PS (02.Ambient2.*.fx,
//  MakeShaderProgram03/04). Handles CONFIRMED (Sec 1.4); HLSL body is a coherent
//  RECONSTRUCTION, PLAUSIBLE (P-11, original encrypted .fx source absent from IDB+VeryOld).
//  The original source lives encrypted in
//  ./GXDEFFECT/GXDEffect.npk (Shader03.fx/Shader04.fx); recompiled here via
//  D3DXCompileShader exactly like the Shader_LoadVSxx loaders (entry point
//  "Main", profiles vs_2_0/ps_2_0). Uniforms IDENTICAL to the survey (Sec 2.2):
//    mKeyMatrix, mWorldViewProjMatrix, mLightDirection, mLightAmbient, mLightDiffuse.
//  4-influence skinning: BLENDINDICES (D3DCOLOR) via D3DCOLORtoUBYTE4,
//  weighted by BLENDWEIGHT. D3DX convention: constants set via SetMatrix*
//  (row-major transposed) -> mul(row_vector, matrix).
// ---------------------------------------------------------------------------
static const char kSkinnedVS[] = R"HLSL(
float4x4 mWorldViewProjMatrix;
// bone palette (== MeshRenderer::kMaxBones). The PLAYER skeleton has 76 bones (measured on disk
// from .MOTION: count_B=76; torso/legs reference bones 40..70). float4x4[76]=304 registers
// > 256 (vs_2_0) -> impossible. PACKED as float4x3 (3 registers/bone, like the real Shader03):
// 80*3=240 + WVP(4) + 3 lights(3) = 247 <= 256. The w column (0,0,0,1 of an affine
// matrix) is dropped -> result identical to the .xyz of the old float4x4.
float4x3 mKeyMatrix[80];
float3   mLightDirection;       // light direction in object space
float3   mLightAmbient;
float3   mLightDiffuse;

struct VS_IN {
    float3 Pos      : POSITION;
    float4 Weights  : BLENDWEIGHT;
    float4 Indices  : BLENDINDICES;  // D3DCOLOR: b0,b1,b2,b3
    float3 Tangent  : TANGENT;
    float3 Binormal : BINORMAL;
    float3 Normal   : NORMAL;
    float2 Tex      : TEXCOORD0;
};

struct VS_OUT {
    float4 Pos   : POSITION;
    float2 Tex   : TEXCOORD0;
    float4 Color : COLOR0;
};

VS_OUT Main(VS_IN In)
{
    // D3DCOLORtoUBYTE4 restores byte order (b0,b1,b2,b3) packed into a D3DCOLOR.
    int4  idx = D3DCOLORtoUBYTE4(In.Indices);
    float4 p  = float4(In.Pos, 1.0f);

    // Skinned position: weighted sum of the 4 bones. mul(float4, float4x3) -> float3 (no .xyz).
    float3 sp = mul(p, mKeyMatrix[idx.x]) * In.Weights.x;
    sp += mul(p, mKeyMatrix[idx.y]) * In.Weights.y;
    sp += mul(p, mKeyMatrix[idx.z]) * In.Weights.z;
    sp += mul(p, mKeyMatrix[idx.w]) * In.Weights.w;

    // Skinned normal (3x3 rotation part of the bone matrices).
    float3 nrm = mul(In.Normal, (float3x3)mKeyMatrix[idx.x]) * In.Weights.x;
    nrm += mul(In.Normal, (float3x3)mKeyMatrix[idx.y]) * In.Weights.y;
    nrm += mul(In.Normal, (float3x3)mKeyMatrix[idx.z]) * In.Weights.z;
    nrm += mul(In.Normal, (float3x3)mKeyMatrix[idx.w]) * In.Weights.w;
    nrm = normalize(nrm);

    VS_OUT o;
    o.Pos = mul(float4(sp, 1.0f), mWorldViewProjMatrix);
    o.Tex = In.Tex;

    float ndl = saturate(dot(nrm, -mLightDirection));
    o.Color   = float4(mLightAmbient + mLightDiffuse * ndl, 1.0f);
    return o;
}
)HLSL";

static const char kSkinnedPS[] = R"HLSL(
sampler2D mTexture0;

struct PS_IN {
    float2 Tex   : TEXCOORD0;
    float4 Color : COLOR0;
};

float4 Main(PS_IN In) : COLOR0
{
    return tex2D(mTexture0, In.Tex) * In.Color;
}
)HLSL";

} // namespace

BonePalette MotionPalette::FrameSlice(float animTime) const {
    BonePalette bp;
    if (!valid || bonesPerFrame <= 0 || base == nullptr || frameCount <= 0)
        return bp;

    // v59 = ftol(a5) clamped to [0, frameCount-1] (see Model_Render 0x40EBB0).
    int frame = static_cast<int>(animTime);
    if (frame < 0)           frame = 0;
    if (frame > frameCount-1) frame = frameCount - 1;

    bp.matrices = base + static_cast<size_t>(frame) * static_cast<size_t>(bonesPerFrame);
    bp.count    = static_cast<UINT>(bonesPerFrame);
    return bp;
}

void SkinnedModel::Release() {
    for (auto& mesh : meshes) {
        for (auto& lod : mesh.lods) {
            SafeRelease(lod.vb);
            SafeRelease(lod.ib);
        }
        // The mesh's 3 material slots (mat0/mat1/mat2 = mesh+712/+768/+824) each own their
        // IDirect3DTexture9 (mat+52): tex1/tex2 are uploaded by Upload() from sm.tex[1]/sm.tex[2]
        // and must be released just like diffuse (otherwise a GPU leak).
        SafeRelease(mesh.diffuse);
        SafeRelease(mesh.tex1);
        SafeRelease(mesh.tex2);
    }
    meshes.clear();
}

bool MeshRenderer::Init(Renderer& renderer) {
    dev_ = renderer.Device();
    if (!dev_) { TS2_ERR("MeshRenderer::Init: null device"); return false; }

    D3DXMatrixIdentity(&view_);
    D3DXMatrixIdentity(&proj_);
    D3DXMatrixIdentity(&viewProj_);
    for (UINT i = 0; i < kMaxBones; ++i)
        D3DXMatrixIdentity(&identityPalette_[i]);

    if (!buildVertexDeclaration()) return false;
    if (!compileSkinnedProgram())  return false;

    TS2_LOG("MeshRenderer ready: 76 B decl + skinned shaders (sampler mTexture0=%u)", sampler0_);
    return true;
}

void MeshRenderer::Shutdown() {
    SafeRelease(vs_);
    SafeRelease(ps_);
    SafeRelease(ctVs_);
    SafeRelease(ctPs_);
    SafeRelease(decl_);
    // shaderSet_ is NOT owned (loaded/released by ShaderSet on the caller side): just drop
    // the reference, no Release().
    shaderSet_ = nullptr;
    dev_ = nullptr;
}

// ----- Wiring onto the real npk shaders (Shader_LoadVS03 0x409AB0 / PS04 0x409CC0) -----------
// The swap is purely internal: DrawSkinnedSubset routes its binds/constants to these slots
// instead of the reconstructed HLSL. Also recovers the REAL bound of mKeyMatrix[] (Elements),
// since Model_DrawSkinnedSubset does not clamp client-side (SetMatrixArray count=nBones, 0x40d4e8).
void MeshRenderer::AttachShaderSet(const ShaderSet* shaders) {
    shaderSet_ = nullptr;
    boneArraySize_ = kMaxBones;
    if (!shaders) return;

    const GxdShader& vs = shaders->Get(GxdShaderId::VS03_SkinnedLit);
    const GxdShader& ps = shaders->Get(GxdShaderId::PS04_Tex);
    if (!vs.Valid() || !ps.Valid()) {
        TS2_WARN("AttachShaderSet: Shader03/04 npk invalid -> falling back to reconstructed HLSL");
        return;
    }
    shaderSet_ = shaders;

    // Real bound = D3DXCONSTANT_DESC.Elements of mKeyMatrix in the real Shader03.
    D3DXHANDLE h = vs.Handle("mKeyMatrix");
    if (h && vs.ct) {
        D3DXCONSTANT_DESC d;
        UINT n = 1;
        if (SUCCEEDED(vs.ct->GetConstantDesc(h, &d, &n)) && d.Elements > 0)
            boneArraySize_ = d.Elements;
    }
    TS2_LOG("MeshRenderer wired to real npk shaders (VS03/PS04); mKeyMatrix[%u]", boneArraySize_);
}

void MeshRenderer::SetShadowParams(bool enabled, int method, float fogNear, float fogFar,
                                   const D3DXVECTOR3& lightDir) {
    shadowsEnabled_ = enabled;
    shadowMethod_   = method;
    fogNear_        = fogNear;
    fogFar_         = fogFar;
    shadowLightDir_ = lightDir;
}

// 76 B vertex declaration - EXACT copy of g_GxdVertexDecl (0x814A58).
// ex-VeryOldClient: mVertexElementForSKIN2 -> mDECLForSKIN2 (stride 76) - BIT-EXACT, CONFIRMED Sec 1.5.
bool MeshRenderer::buildVertexDeclaration() {
    static const D3DVERTEXELEMENT9 kDecl[] = {
        { 0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0 },
        { 0, 12, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0 },
        { 0, 28, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
        { 0, 32, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      0 },
        { 0, 44, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL,     0 },
        { 0, 56, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0 },
        { 0, 68, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0 },
        D3DDECL_END()
    };
    HRESULT hr = dev_->CreateVertexDeclaration(kDecl, &decl_);
    if (FAILED(hr)) { TS2_ERR("CreateVertexDeclaration failed (0x%08lX)", hr); return false; }
    return true;
}

// Compiles the SkinnedLit VS + textured PS (like Shader_LoadVS03/PS04).
bool MeshRenderer::compileSkinnedProgram() {
    // --- Vertex shader ---
    LPD3DXBUFFER code = nullptr, errs = nullptr;
    HRESULT hr = D3DXCompileShader(kSkinnedVS, sizeof(kSkinnedVS) - 1,
                                   nullptr, nullptr, "Main", "vs_2_0", 0,
                                   &code, &errs, &ctVs_);
    if (FAILED(hr)) {
        TS2_ERR("D3DXCompileShader(VS) failed (0x%08lX): %s", hr,
                errs ? static_cast<const char*>(errs->GetBufferPointer()) : "?");
        SafeRelease(errs);
        return false;
    }
    SafeRelease(errs);
    hr = dev_->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()), &vs_);
    SafeRelease(code);
    if (FAILED(hr)) { TS2_ERR("CreateVertexShader failed (0x%08lX)", hr); return false; }

    // Retrieve uniform handles (GetConstantByName, as in the shader loaders).
    hKeyMatrix_      = ctVs_->GetConstantByName(nullptr, "mKeyMatrix");
    hWorldViewProj_  = ctVs_->GetConstantByName(nullptr, "mWorldViewProjMatrix");
    hLightDirection_ = ctVs_->GetConstantByName(nullptr, "mLightDirection");
    hLightAmbient_   = ctVs_->GetConstantByName(nullptr, "mLightAmbient");
    hLightDiffuse_   = ctVs_->GetConstantByName(nullptr, "mLightDiffuse");
    if (!hKeyMatrix_ || !hWorldViewProj_) {
        TS2_ERR("VS uniforms not found (mKeyMatrix/mWorldViewProjMatrix)");
        return false;
    }
    ctVs_->SetDefaults(dev_); // SetDefaults(device) like the original loaders.

    // --- Pixel shader ---
    hr = D3DXCompileShader(kSkinnedPS, sizeof(kSkinnedPS) - 1,
                           nullptr, nullptr, "Main", "ps_2_0", 0,
                           &code, &errs, &ctPs_);
    if (FAILED(hr)) {
        TS2_ERR("D3DXCompileShader(PS) failed (0x%08lX): %s", hr,
                errs ? static_cast<const char*>(errs->GetBufferPointer()) : "?");
        SafeRelease(errs);
        return false;
    }
    SafeRelease(errs);
    hr = dev_->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &ps_);
    SafeRelease(code);
    if (FAILED(hr)) { TS2_ERR("CreatePixelShader failed (0x%08lX)", hr); return false; }

    // mTexture0 sampler register (GetConstantDesc.RegisterIndex, see Sec 2.2).
    D3DXHANDLE hTex = ctPs_->GetConstantByName(nullptr, "mTexture0");
    sampler0_ = hTex ? ctPs_->GetSamplerIndex(hTex) : 0;
    ctPs_->SetDefaults(dev_);
    return true;
}

bool MeshRenderer::Upload(const asset::SObject& src, SkinnedModel& out) {
    if (!Ready()) { TS2_ERR("MeshRenderer::Upload: renderer not initialized"); return false; }
    out.Release();

    if (src.format() != asset::SObject::Format::SObjectA) {
        TS2_WARN("Upload: unsupported SOBJECT format (only skinned Format A is handled)");
        return false;
    }

    out.meshes.reserve(src.meshes().size());
    for (const asset::SObjectMesh& sm : src.meshes()) {
        SkinnedMesh dst;
        dst.empty = sm.empty || sm.field0 == 0;
        if (dst.empty) { out.meshes.push_back(std::move(dst)); continue; }

        // Each parser "subset" = one LOD level (arrays a2+684..696).
        dst.lods.reserve(sm.subsets.size());
        for (const asset::SObjectSubset& ss : sm.subsets) {
            SkinnedLod lod;
            lod.vertexCount = ss.vertexCount;
            lod.faceCount   = ss.faceCount;

            const UINT vbBytes = ss.vertexCount * static_cast<UINT>(asset::SObjectSubset::kVertexStride);
            const UINT ibBytes = ss.faceCount  * static_cast<UINT>(asset::SObjectSubset::kFaceStride);
            if (vbBytes == 0 || ibBytes == 0 ||
                ss.vertexBuffer.size() < vbBytes || ss.indexBuffer.size() < ibBytes) {
                // Empty/inconsistent LOD: skip it (stays null, not drawn).
                dst.lods.push_back(lod);
                continue;
            }

            // 76 B/vertex VB (D3DPOOL_MANAGED: survives a Reset without re-upload).
            HRESULT hr = dev_->CreateVertexBuffer(vbBytes, 0, 0, D3DPOOL_MANAGED, &lod.vb, nullptr);
            if (SUCCEEDED(hr)) {
                void* p = nullptr;
                if (SUCCEEDED(lod.vb->Lock(0, vbBytes, &p, 0))) {
                    std::memcpy(p, ss.vertexBuffer.data(), vbBytes);
                    lod.vb->Unlock();
                }
            } else {
                TS2_ERR("CreateVertexBuffer failed (0x%08lX)", hr);
            }

            // IB INDEX16, 6 B/face.
            hr = dev_->CreateIndexBuffer(ibBytes, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &lod.ib, nullptr);
            if (SUCCEEDED(hr)) {
                void* p = nullptr;
                if (SUCCEEDED(lod.ib->Lock(0, ibBytes, &p, 0))) {
                    std::memcpy(p, ss.indexBuffer.data(), ibBytes);
                    lod.ib->Unlock();
                }
            } else {
                TS2_ERR("CreateIndexBuffer failed (0x%08lX)", hr);
            }

            // Retain CPU data for the shadow volume (Model_BuildShadowVolume 0x40DC70): 32 B/vertex
            // skin (mesh+700), topology (mesh+704), adjacency (mesh+708). Without them,
            // DrawModelShadow cannot skin/silhouette -> the mesh's shadow volume is skipped.
            const UINT skinBytes = ss.vertexCount * static_cast<UINT>(asset::SObjectSubset::kSkinStride);
            if (skinBytes != 0 && ss.skin.size() >= skinBytes &&
                ss.indexCopy1.size() >= ibBytes && ss.indexCopy2.size() >= ibBytes) {
                lod.skinCpu.assign(ss.skin.begin(), ss.skin.begin() + skinBytes);
                lod.idxTopo.assign(ss.indexCopy1.begin(), ss.indexCopy1.begin() + ibBytes);
                lod.idxAdj.assign(ss.indexCopy2.begin(), ss.indexCopy2.begin() + ibBytes);
            }

            dst.lods.push_back(std::move(lod));
        }

        // --- Materials: mat0/mat1/mat2 (mesh+712/+768/+824, see SkinnedMesh in the .h) --------
        // mat0 = diffuse texture (tex[0] block) -> mTexture0.
        dst.diffuse = createDiffuse(dev_, sm.tex[0]);
        // SOBJ-04: mat1/mat2 used to be parsed then NEVER uploaded -> passes 3/4 impossible.
        // They DO survive the parse: g_TextureDetailLevel is frozen at 2 and the two conditional
        // Tex_Free calls in Mesh_ReadFromFile 0x40BC50 (@0x40c228 `if (!detail)` / @0x40c250
        // `if (detail != 2)`) are then NEITHER of them taken (see kTextureDetailLevel).
        dst.tex1 = createDiffuse(dev_, sm.tex[1]); // -> mTexture1 (passes 3 and 4)
        dst.tex2 = createDiffuse(dev_, sm.tex[2]); // -> mTexture2 (pass 4)
        // SOBJ-02: blendMode = material +44 = `alphaMode` trailer of the tex[0] block
        //   (Tex_ReadPacked 0x417740: a1[11] @0x4178dd; read at draw time @0x40cb1a/@0x40cb2c/@0x40d953).
        //   Stays 0 (opaque) if the trailer could not be decoded - see asset::WalkTexture.
        dst.blendMode = sm.tex[0].alphaMode;
        out.meshes.push_back(std::move(dst));
    }

    TS2_LOG("SOBJECT upload: %zu meshes -> GPU", out.meshes.size());
    return true;
}

// Decompresses the texture block's zlib envelope (Tex_ReadPacked 0x417740, format A)
// then creates the IDirect3DTexture9 from the DDS via D3DX.
IDirect3DTexture9* MeshRenderer::createDiffuse(IDirect3DDevice9* dev,
                                               const asset::SObjectTexture& tex) {
    if (!tex.present || tex.packedSize == 0 || tex.compressed.empty() ||
        tex.rawSize == 0 || tex.ddsSize == 0) {
        return nullptr;
    }
    auto& zlib = asset::Zlib::Instance();
    if (!zlib.Available()) {
        TS2_WARN("createDiffuse: GXDCompress.dll unavailable, texture skipped");
        return nullptr;
    }

    // Decompressed block = [ddsSize bytes DDS][u32 userTag][u32 alphaType] (= rawSize).
    std::vector<uint8_t> raw(tex.rawSize);
    if (!zlib.Inflate(tex.compressed.data(), tex.compressed.size(), raw.data(), tex.rawSize)) {
        TS2_WARN("createDiffuse: DDS inflate failed");
        return nullptr;
    }

    IDirect3DTexture9* out = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev, raw.data(), tex.ddsSize,
        D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN,
        D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &out);
    if (FAILED(hr)) {
        TS2_WARN("createDiffuse: D3DXCreateTextureFromFileInMemoryEx failed (0x%08lX)", hr);
        return nullptr;
    }
    return out;
}

void MeshRenderer::SetCamera(const D3DXMATRIX& view, const D3DXMATRIX& proj) {
    view_ = view;
    proj_ = proj;
    D3DXMatrixMultiply(&viewProj_, &view_, &proj_); // view * proj

    // World camera position = translation of the inverse view (== g_CameraEye dword_18C51C0).
    // DrawModelShadow uses it for the model<->camera distance (0x40ef8e).
    D3DXMATRIX invView;
    if (D3DXMatrixInverse(&invView, nullptr, &view_))
        eye_ = D3DXVECTOR3(invView._41, invView._42, invView._43);
}

void MeshRenderer::SetLight(const D3DXVECTOR3& dirWorld,
                            const D3DXVECTOR3& ambient,
                            const D3DXVECTOR3& diffuse) {
    D3DXVec3Normalize(&lightDirWorld_, &dirWorld);
    lightAmbient_ = ambient;
    lightDiffuse_ = diffuse;
}

void MeshRenderer::DrawModel(const SkinnedModel& model,
                             const D3DXVECTOR3&  position,
                             const D3DXVECTOR3&  rotationDeg,
                             const D3DXVECTOR3&  scale,
                             const BonePalette&  palette,
                             int                 lod,
                             int                 pass) {
    if (!Ready() || model.Empty()) return;

    // Composed world matrix = Scale * RotZ * RotY * RotX * Translate
    // (Model_Render 0x40EBB0; Euler angles in degrees -> radians x0.017453292).
    D3DXMATRIX mS, mRx, mRy, mRz, mT, tmp, world;
    D3DXMatrixScaling(&mS, scale.x, scale.y, scale.z);
    D3DXMatrixRotationX(&mRx, rotationDeg.x * kDeg2Rad);
    D3DXMatrixRotationY(&mRy, rotationDeg.y * kDeg2Rad);
    D3DXMatrixRotationZ(&mRz, rotationDeg.z * kDeg2Rad);
    D3DXMatrixTranslation(&mT, position.x, position.y, position.z);

    D3DXMatrixMultiply(&world, &mS,    &mRz); // S*Rz
    D3DXMatrixMultiply(&tmp,   &world, &mRy); // *Ry
    D3DXMatrixMultiply(&world, &tmp,   &mRx); // *Rx
    D3DXMatrixMultiply(&tmp,   &world, &mT);  // *T
    world = tmp;

    // SOBJ-03 - TWO-PASS structure (a6 of Model_Render 0x40EBB0, bounded {1,2} @0x40ebd5).
    // kPassBoth = assumed shim (see .h): two sweeps 1 then 2, in that order - what the
    // binary does via adjacent call pairs (@0x51d359/@0x51d3c4, @0x51d421/@0x51d478).
    if (pass == kPassBoth) {
        drawMeshSweep(model, world, palette, lod, kDrawPass_Opaque);
        drawMeshSweep(model, world, palette, lod, kDrawPass_Blend);
        return;
    }
    drawMeshSweep(model, world, palette, lod, pass);
}

} // namespace ts2::gfx

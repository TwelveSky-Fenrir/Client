// Gfx/MeshRenderer.cpp — rendu skinné GPU du moteur GXD.
// Voir MeshRenderer.h pour les ancres de rétro-ingénierie.
#include "Gfx/MeshRenderer.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"

#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

template <class T>
void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// D3DRENDERSTATETYPE / valeurs (numéros bruts relevés dans Model_DrawSkinnedSubset).
constexpr DWORD kRS_ZWriteEnable      = 14; // D3DRS_ZWRITEENABLE
constexpr DWORD kRS_AlphaTestEnable   = 15; // D3DRS_ALPHATESTENABLE
constexpr DWORD kRS_SrcBlend          = 19; // D3DRS_SRCBLEND
constexpr DWORD kRS_DestBlend         = 20; // D3DRS_DESTBLEND
constexpr DWORD kRS_AlphaRef          = 24; // D3DRS_ALPHAREF
constexpr DWORD kRS_AlphaFunc         = 25; // D3DRS_ALPHAFUNC
constexpr DWORD kRS_AlphaBlendEnable  = 27; // D3DRS_ALPHABLENDENABLE

constexpr DWORD kBLEND_Zero     = 1;  // D3DBLEND_ZERO
constexpr DWORD kBLEND_One      = 2;  // D3DBLEND_ONE
constexpr DWORD kBLEND_SrcAlpha = 5;  // D3DBLEND_SRCALPHA
constexpr DWORD kCMP_GreaterEqual = 5; // D3DCMP_GREATEREQUAL
constexpr DWORD kCMP_Always       = 8; // D3DCMP_ALWAYS

// Identifiant de passe shader interne (arbitraire, joue le rôle de g_CurrentShaderPass).
constexpr int kPass_SkinnedLit = 2;

constexpr float kDeg2Rad = 0.017453292519943295f; // 0.017453292 (×π/180)

// ---------------------------------------------------------------------------
//  Programme de shaders skinné — reconstruction fidèle de Shader03 (VS SkinnedLit)
//  + Shader04 (PS texturé). ex-VeryOldClient: mAmbient2_VS / mAmbient2_PS (02.Ambient2.*.fx,
//  MakeShaderProgram03/04). Handles CONFIRMED (§1.4) ; corps HLSL = RECONSTRUCTION cohérente
//  PLAUSIBLE (P-11, source .fx chiffrée absente IDB+VeryOld).
//  La source d'origine vit chiffrée dans
//  ./GXDEFFECT/GXDEffect.npk (Shader03.fx/Shader04.fx) ; on la recompile ici par
//  D3DXCompileShader exactement comme les loaders Shader_LoadVSxx (point d'entrée
//  "Main", profils vs_2_0/ps_2_0). Uniformes IDENTIQUES au relevé (§2.2) :
//    mKeyMatrix, mWorldViewProjMatrix, mLightDirection, mLightAmbient, mLightDiffuse.
//  Skinning à 4 influences : BLENDINDICES (D3DCOLOR) via D3DCOLORtoUBYTE4,
//  pondéré par BLENDWEIGHT. Convention D3DX : constantes posées par SetMatrix*
//  (row-major transposé) -> mul(vecteur_ligne, matrice).
// ---------------------------------------------------------------------------
static const char kSkinnedVS[] = R"HLSL(
float4x4 mWorldViewProjMatrix;
float4x4 mKeyMatrix[40];        // palette d'os (== MeshRenderer::kMaxBones)
float3   mLightDirection;       // direction lumière en espace objet
float3   mLightAmbient;
float3   mLightDiffuse;

struct VS_IN {
    float3 Pos      : POSITION;
    float4 Weights  : BLENDWEIGHT;
    float4 Indices  : BLENDINDICES;  // D3DCOLOR : b0,b1,b2,b3
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
    // D3DCOLORtoUBYTE4 rétablit l'ordre d'octets (b0,b1,b2,b3) empaquetés en D3DCOLOR.
    int4  idx = D3DCOLORtoUBYTE4(In.Indices);
    float4 p  = float4(In.Pos, 1.0f);

    // Position skinnée : somme pondérée des 4 os.
    float3 sp = mul(p, mKeyMatrix[idx.x]).xyz * In.Weights.x;
    sp += mul(p, mKeyMatrix[idx.y]).xyz * In.Weights.y;
    sp += mul(p, mKeyMatrix[idx.z]).xyz * In.Weights.z;
    sp += mul(p, mKeyMatrix[idx.w]).xyz * In.Weights.w;

    // Normale skinnée (partie rotation 3x3 des matrices d'os).
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

// ===========================================================================
//  MotionPalette / SkinnedModel
// ===========================================================================

BonePalette MotionPalette::FrameSlice(float animTime) const {
    BonePalette bp;
    if (!valid || bonesPerFrame <= 0 || base == nullptr || frameCount <= 0)
        return bp;

    // v59 = ftol(a5) borné [0, frameCount-1] (cf. Model_Render 0x40EBB0).
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
        SafeRelease(mesh.diffuse);
    }
    meshes.clear();
}

// ===========================================================================
//  Init / Shutdown
// ===========================================================================

bool MeshRenderer::Init(Renderer& renderer) {
    dev_ = renderer.Device();
    if (!dev_) { TS2_ERR("MeshRenderer::Init : device nul"); return false; }

    D3DXMatrixIdentity(&view_);
    D3DXMatrixIdentity(&proj_);
    D3DXMatrixIdentity(&viewProj_);
    for (UINT i = 0; i < kMaxBones; ++i)
        D3DXMatrixIdentity(&identityPalette_[i]);

    if (!buildVertexDeclaration()) return false;
    if (!compileSkinnedProgram())  return false;

    TS2_LOG("MeshRenderer pret : decl 76 o + shaders skinnes (sampler mTexture0=%u)", sampler0_);
    return true;
}

void MeshRenderer::Shutdown() {
    SafeRelease(vs_);
    SafeRelease(ps_);
    SafeRelease(ctVs_);
    SafeRelease(ctPs_);
    SafeRelease(decl_);
    dev_ = nullptr;
}

// Déclaration de vertex 76 o — copie EXACTE de g_GxdVertexDecl (0x814A58).
// ex-VeryOldClient: mVertexElementForSKIN2 -> mDECLForSKIN2 (stride 76) — BIT-EXACT, CONFIRMED §1.5.
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
    if (FAILED(hr)) { TS2_ERR("CreateVertexDeclaration echoue (0x%08lX)", hr); return false; }
    return true;
}

// Compile le VS SkinnedLit + PS texturé (comme Shader_LoadVS03/PS04).
bool MeshRenderer::compileSkinnedProgram() {
    // --- Vertex shader ---
    LPD3DXBUFFER code = nullptr, errs = nullptr;
    HRESULT hr = D3DXCompileShader(kSkinnedVS, sizeof(kSkinnedVS) - 1,
                                   nullptr, nullptr, "Main", "vs_2_0", 0,
                                   &code, &errs, &ctVs_);
    if (FAILED(hr)) {
        TS2_ERR("D3DXCompileShader(VS) echoue (0x%08lX) : %s", hr,
                errs ? static_cast<const char*>(errs->GetBufferPointer()) : "?");
        SafeRelease(errs);
        return false;
    }
    SafeRelease(errs);
    hr = dev_->CreateVertexShader(static_cast<const DWORD*>(code->GetBufferPointer()), &vs_);
    SafeRelease(code);
    if (FAILED(hr)) { TS2_ERR("CreateVertexShader echoue (0x%08lX)", hr); return false; }

    // Récupération des handles d'uniformes (GetConstantByName, cf. loaders shaders).
    hKeyMatrix_      = ctVs_->GetConstantByName(nullptr, "mKeyMatrix");
    hWorldViewProj_  = ctVs_->GetConstantByName(nullptr, "mWorldViewProjMatrix");
    hLightDirection_ = ctVs_->GetConstantByName(nullptr, "mLightDirection");
    hLightAmbient_   = ctVs_->GetConstantByName(nullptr, "mLightAmbient");
    hLightDiffuse_   = ctVs_->GetConstantByName(nullptr, "mLightDiffuse");
    if (!hKeyMatrix_ || !hWorldViewProj_) {
        TS2_ERR("Uniformes VS introuvables (mKeyMatrix/mWorldViewProjMatrix)");
        return false;
    }
    ctVs_->SetDefaults(dev_); // SetDefaults(device) comme les loaders d'origine.

    // --- Pixel shader ---
    hr = D3DXCompileShader(kSkinnedPS, sizeof(kSkinnedPS) - 1,
                           nullptr, nullptr, "Main", "ps_2_0", 0,
                           &code, &errs, &ctPs_);
    if (FAILED(hr)) {
        TS2_ERR("D3DXCompileShader(PS) echoue (0x%08lX) : %s", hr,
                errs ? static_cast<const char*>(errs->GetBufferPointer()) : "?");
        SafeRelease(errs);
        return false;
    }
    SafeRelease(errs);
    hr = dev_->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()), &ps_);
    SafeRelease(code);
    if (FAILED(hr)) { TS2_ERR("CreatePixelShader echoue (0x%08lX)", hr); return false; }

    // Registre de sampler de mTexture0 (GetConstantDesc.RegisterIndex, cf. §2.2).
    D3DXHANDLE hTex = ctPs_->GetConstantByName(nullptr, "mTexture0");
    sampler0_ = hTex ? ctPs_->GetSamplerIndex(hTex) : 0;
    ctPs_->SetDefaults(dev_);
    return true;
}

// ===========================================================================
//  Upload (.SOBJECT -> VB/IB/texture)
// ===========================================================================

bool MeshRenderer::Upload(const asset::SObject& src, SkinnedModel& out) {
    if (!Ready()) { TS2_ERR("MeshRenderer::Upload : renderer non initialise"); return false; }
    out.Release();

    if (src.format() != asset::SObject::Format::SObjectA) {
        TS2_WARN("Upload : format SOBJECT non supporte (seul Format A skinne est gere)");
        return false;
    }

    out.meshes.reserve(src.meshes().size());
    for (const asset::SObjectMesh& sm : src.meshes()) {
        SkinnedMesh dst;
        dst.empty = sm.empty || sm.field0 == 0;
        if (dst.empty) { out.meshes.push_back(std::move(dst)); continue; }

        // Chaque "subset" du parseur = un niveau de LOD (tableaux a2+684..696).
        dst.lods.reserve(sm.subsets.size());
        for (const asset::SObjectSubset& ss : sm.subsets) {
            SkinnedLod lod;
            lod.vertexCount = ss.vertexCount;
            lod.faceCount   = ss.faceCount;

            const UINT vbBytes = ss.vertexCount * static_cast<UINT>(asset::SObjectSubset::kVertexStride);
            const UINT ibBytes = ss.faceCount  * static_cast<UINT>(asset::SObjectSubset::kFaceStride);
            if (vbBytes == 0 || ibBytes == 0 ||
                ss.vertexBuffer.size() < vbBytes || ss.indexBuffer.size() < ibBytes) {
                // LOD vide/incohérent : on saute (reste nul, non dessiné).
                dst.lods.push_back(lod);
                continue;
            }

            // VB 76 o/sommet (D3DPOOL_MANAGED : survit à un Reset sans re-upload).
            HRESULT hr = dev_->CreateVertexBuffer(vbBytes, 0, 0, D3DPOOL_MANAGED, &lod.vb, nullptr);
            if (SUCCEEDED(hr)) {
                void* p = nullptr;
                if (SUCCEEDED(lod.vb->Lock(0, vbBytes, &p, 0))) {
                    std::memcpy(p, ss.vertexBuffer.data(), vbBytes);
                    lod.vb->Unlock();
                }
            } else {
                TS2_ERR("CreateVertexBuffer echoue (0x%08lX)", hr);
            }

            // IB INDEX16, 6 o/face.
            hr = dev_->CreateIndexBuffer(ibBytes, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &lod.ib, nullptr);
            if (SUCCEEDED(hr)) {
                void* p = nullptr;
                if (SUCCEEDED(lod.ib->Lock(0, ibBytes, &p, 0))) {
                    std::memcpy(p, ss.indexBuffer.data(), ibBytes);
                    lod.ib->Unlock();
                }
            } else {
                TS2_ERR("CreateIndexBuffer echoue (0x%08lX)", hr);
            }

            dst.lods.push_back(lod);
        }

        // Texture diffuse (matDiffuse) depuis le bloc tex[0].
        dst.diffuse   = createDiffuse(dev_, sm.tex[0]);
        dst.blendMode = 0; // GXD_Material +0x2C non exposé par le parseur SObject -> opaque par défaut.
        out.meshes.push_back(std::move(dst));
    }

    TS2_LOG("Upload SOBJECT : %zu meshes -> GPU", out.meshes.size());
    return true;
}

// Décompresse l'enveloppe zlib du bloc texture (Tex_ReadPacked 0x417740, format A)
// puis crée l'IDirect3DTexture9 depuis le DDS via D3DX.
IDirect3DTexture9* MeshRenderer::createDiffuse(IDirect3DDevice9* dev,
                                               const asset::SObjectTexture& tex) {
    if (!tex.present || tex.packedSize == 0 || tex.compressed.empty() ||
        tex.rawSize == 0 || tex.ddsSize == 0) {
        return nullptr;
    }
    auto& zlib = asset::Zlib::Instance();
    if (!zlib.Available()) {
        TS2_WARN("createDiffuse : GXDCompress.dll indisponible, texture ignoree");
        return nullptr;
    }

    // Bloc décompressé = [ddsSize octets DDS][u32 userTag][u32 alphaType] (= rawSize).
    std::vector<uint8_t> raw(tex.rawSize);
    if (!zlib.Inflate(tex.compressed.data(), tex.compressed.size(), raw.data(), tex.rawSize)) {
        TS2_WARN("createDiffuse : inflate DDS echoue");
        return nullptr;
    }

    IDirect3DTexture9* out = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev, raw.data(), tex.ddsSize,
        D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN,
        D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &out);
    if (FAILED(hr)) {
        TS2_WARN("createDiffuse : D3DXCreateTextureFromFileInMemoryEx echoue (0x%08lX)", hr);
        return nullptr;
    }
    return out;
}

// ===========================================================================
//  État caméra / lumière
// ===========================================================================

void MeshRenderer::SetCamera(const D3DXMATRIX& view, const D3DXMATRIX& proj) {
    view_ = view;
    proj_ = proj;
    D3DXMatrixMultiply(&viewProj_, &view_, &proj_); // view * proj
}

void MeshRenderer::SetLight(const D3DXVECTOR3& dirWorld,
                            const D3DXVECTOR3& ambient,
                            const D3DXVECTOR3& diffuse) {
    D3DXVec3Normalize(&lightDirWorld_, &dirWorld);
    lightAmbient_ = ambient;
    lightDiffuse_ = diffuse;
}

// ===========================================================================
//  Rendu
// ===========================================================================

void MeshRenderer::DrawModel(const SkinnedModel& model,
                             const D3DXVECTOR3&  position,
                             const D3DXVECTOR3&  rotationDeg,
                             const D3DXVECTOR3&  scale,
                             const BonePalette&  palette,
                             int                 lod) {
    if (!Ready() || model.Empty()) return;

    // Matrice monde composée = Scale * RotZ * RotY * RotX * Translate
    // (Model_Render 0x40EBB0 ; angles Euler degrés -> radians ×0.017453292).
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

    for (const SkinnedMesh& mesh : model.meshes) {
        if (mesh.empty || mesh.lods.empty()) continue;
        int useLod = lod;
        if (useLod < 0) useLod = 0;
        if (useLod >= static_cast<int>(mesh.lods.size()))
            useLod = static_cast<int>(mesh.lods.size()) - 1;
        DrawSkinnedSubset(mesh, useLod, world, palette);
    }
}

void MeshRenderer::DrawSkinnedSubset(const SkinnedMesh& mesh, int lod,
                                     const D3DXMATRIX&  world,
                                     const BonePalette& palette) {
    if (!Ready() || lod < 0 || lod >= static_cast<int>(mesh.lods.size())) return;
    const SkinnedLod& L = mesh.lods[lod];
    if (!L.vb || !L.ib || L.vertexCount == 0 || L.faceCount == 0) return;

    // 1) États de mélange du matériau (v14 = blendMode).
    applyBlendMode(mesh.blendMode);

    // 2) Bind du programme skinné (évite les re-bind redondants via currentPass_).
    if (currentPass_ != kPass_SkinnedLit) {
        dev_->SetVertexShader(vs_);
        dev_->SetPixelShader(ps_);
        currentPass_ = kPass_SkinnedLit;
    }
    dev_->SetVertexDeclaration(decl_); // g_SkinVertexDecl (method +348)

    // 3) WVP = world * view * proj (v84 dans l'original).
    D3DXMATRIX wvp;
    D3DXMatrixMultiply(&wvp, &world, &viewProj_);

    // 4) Direction de lumière ramenée en espace objet (inverse de world),
    //    cf. D3DXVec3TransformNormal(&v61,&v61,&invWorld) + normalize.
    D3DXMATRIX invWorld;
    D3DXMatrixInverse(&invWorld, nullptr, &world);
    D3DXVECTOR3 lightObj;
    D3DXVec3TransformNormal(&lightObj, &lightDirWorld_, &invWorld);
    D3DXVec3Normalize(&lightObj, &lightObj);

    // 5) Palette d'os -> mKeyMatrix (SetMatrixArray, method +88 de l'ID3DXConstantTable).
    //    g_GxdSh03_hKeyMatrix 0x1945974 — ex-VeryOldClient: mAmbient2_VS_KeyMatrix.
    //    Palette de secours (identité) si aucune tranche valide n'est fournie.
    const D3DXMATRIX* palMats = identityPalette_;
    UINT boneCount = 1;
    if (palette.Valid()) {
        palMats   = palette.matrices;
        boneCount = palette.count;
        if (boneCount > kMaxBones) boneCount = kMaxBones; // borne vs_2_0 (256 registres)
    }
    ctVs_->SetMatrixArray(dev_, hKeyMatrix_, palMats, boneCount);

    // 6) WVP + lumière (SetMatrix +84, SetFloatArray +72).
    ctVs_->SetMatrix(dev_, hWorldViewProj_, &wvp);
    if (hLightDirection_) ctVs_->SetFloatArray(dev_, hLightDirection_, &lightObj.x, 3);
    if (hLightAmbient_)   ctVs_->SetFloatArray(dev_, hLightAmbient_,   &lightAmbient_.x, 3);
    if (hLightDiffuse_)   ctVs_->SetFloatArray(dev_, hLightDiffuse_,   &lightDiffuse_.x, 3);

    // 7) Texture diffuse (method +260 SetTexture, registre mTexture0).
    if (mesh.diffuse) dev_->SetTexture(sampler0_, mesh.diffuse);

    // 8) Flux + indices + dessin (SetStreamSource +400 stride 76, SetIndices +416,
    //    DrawIndexedPrimitive +328 : TRIANGLELIST, nbVtx, nbTri).
    dev_->SetStreamSource(0, L.vb, 0, static_cast<UINT>(sizeof(GpuSkinVertex)));
    dev_->SetIndices(L.ib);
    dev_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, L.vertexCount, 0, L.faceCount);

    // 9) Restauration des états de blend (LABEL_70 de l'original).
    resetBlendMode(mesh.blendMode);
}

// blendMode 1 = alpha-test ; blendMode 2 = additif/alpha (cf. Model_DrawSkinnedSubset).
void MeshRenderer::applyBlendMode(uint32_t blendMode) {
    if (blendMode == 1) {
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaTestEnable), TRUE);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaFunc), kCMP_GreaterEqual);
    } else if (blendMode == 2) {
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_ZWriteEnable), FALSE);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaBlendEnable), TRUE);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_SrcBlend), kBLEND_SrcAlpha);
    }
}

void MeshRenderer::resetBlendMode(uint32_t blendMode) {
    if (blendMode == 1) {
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaRef), 0);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaFunc), kCMP_Always);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaTestEnable), FALSE);
    } else if (blendMode == 2) {
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_DestBlend), kBLEND_Zero);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_SrcBlend),  kBLEND_One);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaBlendEnable), FALSE);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_ZWriteEnable), TRUE);
    }
}

} // namespace ts2::gfx

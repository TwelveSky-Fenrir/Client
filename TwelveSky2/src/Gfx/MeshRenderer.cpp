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

constexpr DWORD kBLEND_Zero        = 1;  // D3DBLEND_ZERO
constexpr DWORD kBLEND_One         = 2;  // D3DBLEND_ONE
constexpr DWORD kBLEND_SrcAlpha    = 5;  // D3DBLEND_SRCALPHA
constexpr DWORD kBLEND_InvSrcAlpha = 6;  // D3DBLEND_INVSRCALPHA
constexpr DWORD kCMP_GreaterEqual = 5; // D3DCMP_GREATEREQUAL
constexpr DWORD kCMP_Always       = 8; // D3DCMP_ALWAYS
// Seuil d'alpha-test du matériau blendMode 1 : `push 80h` @0x40cbd1 (voir applyBlendMode).
constexpr DWORD kAlphaRef_Material = 128;

// États du volume d'ombre stencil (numéros bruts relevés dans Model_RenderWithShadow 0x40EEE0).
constexpr DWORD kRS_CullMode     = 22; // D3DRS_CULLMODE
constexpr DWORD kRS_StencilZFail = 54; // D3DRS_STENCILZFAIL
constexpr DWORD kCULL_CW         = 2;  // D3DCULL_CW
constexpr DWORD kCULL_CCW        = 3;  // D3DCULL_CCW
constexpr DWORD kSTENCILOP_Incr  = 7;  // D3DSTENCILOP_INCR
constexpr DWORD kSTENCILOP_Decr  = 8;  // D3DSTENCILOP_DECR
constexpr DWORD kFVF_XYZ         = 2;  // D3DFVF_XYZ (sommet d'ombre position seule, stride 12)

// Identifiant de PASSE SHADER (joue le rôle de g_CurrentShaderPass 0x194591C).
// ⚠ Numérotation DISTINCTE de la passe de DESSIN (MeshRenderer::kDrawPass_*, = a6 de Model_Render) :
//   les deux se croisent dans Model_DrawSkinnedSubset 0x40CA40 sans jamais désigner la même chose.
//   2 = skinné VS03/PS04         (g_CurrentShaderPass = 2 @0x40d3e5)
//   3 = multi-texture VS05/PS06  (g_CurrentShaderPass = 3 @0x40d9c3) — mTexture0 + mTexture1
//   4 = multi-texture VS07/PS08  (g_CurrentShaderPass = 4 @0x40d682) — mTexture0/1/2 + mCameraEye
//   8 = volume d'ombre VS15/PS NULL
constexpr int kPass_SkinnedLit   = 2;
constexpr int kPass_MultiTex2    = 3;
constexpr int kPass_MultiTex3    = 4;
constexpr int kPass_ShadowVolume = 8;

// g_TextureDetailLevel 0x18C4F04 — CONSTANTE GELÉE À 2, pas une option.
// data_refs : 1 SEUL writer, `mov ds:g_TextureDetailLevel, 2` @0x4013A2 (GXD_InitGlobalState
// 0x401320) — un IMMÉDIAT, jamais réécrit (aucune option UI ne le touche) ; 5 readers.
// Conséquences prouvées, à contre-courant de l'intuition « niveau de détail gelé = pas de
// multi-texture » : la valeur 2 CONSERVE les textures et ACTIVE les passes 3/4.
//   Au parse  (Mesh_ReadFromFile 0x40BC50) : `if (!detail) Tex_Free(a1+192)` @0x40c228 -> non pris
//     -> tex1 CONSERVÉE ; `if (detail != 2) Tex_Free(a1+206)` @0x40c250 -> non pris -> tex2 CONSERVÉE.
//   Au dessin (0x40d3a8) : branche detail==0 morte, branche detail==1 MORTE PAR VALEUR, branche
//     `else` (detail>=2) TOUJOURS prise -> les passes 3 et 4 sont VIVANTES.
// On ne porte donc que la branche vivante ; les deux autres seraient du code mort par valeur.
constexpr int kTextureDetailLevel = 2;

// Plafond du volume d'ombre : l'original garde v45 > 29976 -> abort (tableaux 30000 sommets).
constexpr UINT kShadowVolMaxVerts = 30000;
constexpr UINT kShadowVolGuard    = 29976;

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
        // Les 3 slots de matériau du mesh (mat0/mat1/mat2 = mesh+712/+768/+824) possèdent chacun
        // leur IDirect3DTexture9 (mat+52) : tex1/tex2 sont uploadées par Upload() depuis
        // sm.tex[1]/sm.tex[2] et doivent être relâchées comme diffuse (sinon fuite GPU).
        SafeRelease(mesh.diffuse);
        SafeRelease(mesh.tex1);
        SafeRelease(mesh.tex2);
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
    // shaderSet_ n'est PAS possédé (chargé/libéré par ShaderSet côté appelant) : on lâche juste
    // la référence, sans Release().
    shaderSet_ = nullptr;
    dev_ = nullptr;
}

// ----- Câblage sur les shaders réels du npk (Shader_LoadVS03 0x409AB0 / PS04 0x409CC0) --------
// Le swap est purement interne : DrawSkinnedSubset route ses binds/constantes vers ces slots au
// lieu du HLSL reconstruit. Récupère aussi la VRAIE borne de mKeyMatrix[] (Elements), car
// Model_DrawSkinnedSubset ne clampe pas côté client (SetMatrixArray count=nBones, 0x40d4e8).
void MeshRenderer::AttachShaderSet(const ShaderSet* shaders) {
    shaderSet_ = nullptr;
    boneArraySize_ = kMaxBones;
    if (!shaders) return;

    const GxdShader& vs = shaders->Get(GxdShaderId::VS03_SkinnedLit);
    const GxdShader& ps = shaders->Get(GxdShaderId::PS04_Tex);
    if (!vs.Valid() || !ps.Valid()) {
        TS2_WARN("AttachShaderSet : Shader03/04 npk invalides -> fallback HLSL reconstruit");
        return;
    }
    shaderSet_ = shaders;

    // Vraie borne = D3DXCONSTANT_DESC.Elements de mKeyMatrix dans le Shader03 réel.
    D3DXHANDLE h = vs.Handle("mKeyMatrix");
    if (h && vs.ct) {
        D3DXCONSTANT_DESC d;
        UINT n = 1;
        if (SUCCEEDED(vs.ct->GetConstantDesc(h, &d, &n)) && d.Elements > 0)
            boneArraySize_ = d.Elements;
    }
    TS2_LOG("MeshRenderer cable sur shaders npk reels (VS03/PS04) ; mKeyMatrix[%u]", boneArraySize_);
}

void MeshRenderer::SetShadowParams(bool enabled, int method, float fogNear, float fogFar,
                                   const D3DXVECTOR3& lightDir) {
    shadowsEnabled_ = enabled;
    shadowMethod_   = method;
    fogNear_        = fogNear;
    fogFar_         = fogFar;
    shadowLightDir_ = lightDir;
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

            // Rétention des données CPU pour le volume d'ombre (Model_BuildShadowVolume 0x40DC70) :
            // skin 32 o/sommet (mesh+700), topologie (mesh+704) et adjacence (mesh+708). Sans elles,
            // DrawModelShadow ne peut pas skinner/silhouetter -> l'ombre-volume du mesh est ignorée.
            const UINT skinBytes = ss.vertexCount * static_cast<UINT>(asset::SObjectSubset::kSkinStride);
            if (skinBytes != 0 && ss.skin.size() >= skinBytes &&
                ss.indexCopy1.size() >= ibBytes && ss.indexCopy2.size() >= ibBytes) {
                lod.skinCpu.assign(ss.skin.begin(), ss.skin.begin() + skinBytes);
                lod.idxTopo.assign(ss.indexCopy1.begin(), ss.indexCopy1.begin() + ibBytes);
                lod.idxAdj.assign(ss.indexCopy2.begin(), ss.indexCopy2.begin() + ibBytes);
            }

            dst.lods.push_back(std::move(lod));
        }

        // --- Matériaux : mat0/mat1/mat2 (mesh+712/+768/+824, cf. SkinnedMesh dans le .h) --------
        // mat0 = texture diffuse (bloc tex[0]) -> mTexture0.
        dst.diffuse = createDiffuse(dev_, sm.tex[0]);
        // SOBJ-04 : mat1/mat2 étaient parsées puis JAMAIS uploadées -> passes 3/4 impossibles.
        // Elles SURVIVENT bien au parse : g_TextureDetailLevel est gelé à 2 et les deux Tex_Free
        // conditionnels de Mesh_ReadFromFile 0x40BC50 (@0x40c228 `if (!detail)` / @0x40c250
        // `if (detail != 2)`) ne sont alors NI l'un NI l'autre pris (cf. kTextureDetailLevel).
        dst.tex1 = createDiffuse(dev_, sm.tex[1]); // -> mTexture1 (passes 3 et 4)
        dst.tex2 = createDiffuse(dev_, sm.tex[2]); // -> mTexture2 (passe 4)
        // SOBJ-02 : blendMode = matériau +44 = trailer `alphaMode` du bloc tex[0]
        //   (Tex_ReadPacked 0x417740 : a1[11] @0x4178dd ; lu au dessin @0x40cb1a/@0x40cb2c/@0x40d953).
        //   Reste 0 (opaque) si le trailer n'a pas pu être décodé — cf. asset::WalkTexture.
        dst.blendMode = sm.tex[0].alphaMode;
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

    // Position caméra monde = translation de l'inverse de la vue (== g_CameraEye dword_18C51C0).
    // DrawModelShadow s'en sert pour la distance modèle<->caméra (0x40ef8e).
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

// ===========================================================================
//  Rendu
// ===========================================================================

void MeshRenderer::DrawModel(const SkinnedModel& model,
                             const D3DXVECTOR3&  position,
                             const D3DXVECTOR3&  rotationDeg,
                             const D3DXVECTOR3&  scale,
                             const BonePalette&  palette,
                             int                 lod,
                             int                 pass) {
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

    // SOBJ-03 — structure à DEUX PASSES (a6 de Model_Render 0x40EBB0, borné {1,2} @0x40ebd5).
    // kPassBoth = shim assumé (cf. .h) : deux balayages 1 puis 2, dans cet ordre — ce que fait le
    // binaire par paire d'appels adjacents (@0x51d359/@0x51d3c4, @0x51d421/@0x51d478).
    if (pass == kPassBoth) {
        drawMeshSweep(model, world, palette, lod, kDrawPass_Opaque);
        drawMeshSweep(model, world, palette, lod, kDrawPass_Blend);
        return;
    }
    drawMeshSweep(model, world, palette, lod, pass);
}

// Corps de la boucle de meshes de Model_Render 0x40EBB0 (@0x40ee02..0x40eebf, stride 888 @0x40eebf).
void MeshRenderer::drawMeshSweep(const SkinnedModel& model, const D3DXMATRIX& world,
                                 const BonePalette& palette, int lod, int pass) {
    // SOBJ-05 — HÉRITAGE DE MATÉRIAU DEPUIS mesh[0]. La question posée (« héritage explicite, ou
    // état de texture rémanent du device non réinitialisé ? ») est TRANCHÉE : c'est un HÉRITAGE
    // EXPLICITE, un passage de pointeurs, rien de rémanent. Model_Render @0x40ee53 :
    //     v17 = v11[2];                  // base du tableau == &mesh[0]
    //     if ( *(v18 + 764) || !*(v17 + 764) )                       // mesh[i].mat0.tex, mesh[0].mat0.tex
    //         Model_DrawSkinnedSubset(0, v18, ..., 0, 0, ..);        // @0x40eeb1 : matériaux PROPRES
    //     else
    //         Model_DrawSkinnedSubset(v17+712, v18, ..., v17+768, v17+824, ..); // @0x40ee8e
    // -> si mesh[i] n'a PAS de tex0 ET que mesh[0] en a une, on passe le TRIPLET COMPLET de
    //    mesh[0] (mat0 +712, mat1 +768, mat2 +824) en override.
    // COROLLAIRE À NE PAS RATER : en héritage, le blendMode utilisé est celui de mesh[0] (v61+44)
    // et le choix de passe multi-textures se fait sur mat1/mat2 DE mesh[0]. C'est automatique ici :
    // DrawSkinnedSubset lit tout son matériau dans `matSrc`.
    const SkinnedMesh* mesh0 = &model.meshes[0]; // v17 = v11[2]
    for (const SkinnedMesh& mesh : model.meshes) {
        if (mesh.empty || mesh.lods.empty()) continue; // `if (*(v16 + v17))` @0x40ee0e
        int useLod = lod;
        if (useLod < 0) useLod = 0;
        if (useLod >= static_cast<int>(mesh.lods.size()))
            useLod = static_cast<int>(mesh.lods.size()) - 1;
        // `!*(v18+764) && *(v17+764)` -> override par le triplet de mesh[0].
        const SkinnedMesh* matSrc = (!mesh.diffuse && mesh0->diffuse) ? mesh0 : nullptr;
        DrawSkinnedSubset(mesh, useLod, world, palette, matSrc, pass);
    }
}

// TODO [ancre 0x40CDC9] PASSE 1 (billboard) NON IMPLÉMENTÉE — gap SH-05, Passe 4 / W9.
// BLOQUÉE PAR UNE DONNÉE NON PROUVÉE, PAS PAR LE TEMPS. État exact de l'enquête :
//
//  CE QUI EST PROUVÉ (et donc immédiatement disponible le jour du déblocage) :
//   * La garde : `mov esi, 1` @0x40CCED -> `jmp loc_40CDC6` @0x40CD22 -> `cmp [ebx+34h], esi`
//     @0x40CDC6 / `jnz loc_40D3A8` @0x40CDC9  ==>  la branche est prise ssi `mesh+52 == 1`.
//   * Les champs sont TOUS lisibles depuis asset::SObjectMesh::header (Asset/Model.h, non
//     possédé) SANS le modifier — équivalence de layout établie par Mesh_ReadFromMemory
//     0x40C380 : `qmemcpy(a1+1, v6+a2, 0x44)` @0x40C3C9 copie le disque +4..+71 vers mesh+4,
//     `qmemcpy(a1+18, v6+a2+68, 0x130)` @0x40C3EC copie +72..+375 vers mesh+72, et le
//     parseur C++ (Model.cpp:88-89) range exactement ces 372 octets dans `header`. D'où :
//       header[48] = mesh+52 (flag billboard, u32)   header[52] = mesh+56 (mode axe, u32)
//       header[56] = mesh+60 (largeur, float)        header[60] = mesh+64 (hauteur, float)
//       header[68..371] = mesh+72..+375 = les 4 RenderVertex(76 o) inline du quad
//   * Le rendu : skinning CPU des 4 sommets par la palette (@0x40CE47..0x40D0F9), centroïde
//     x0.25 (@0x40D117..0x40D129), demi-largeur = (mesh+60)*0.5 (`fld [ebx+3Ch]` @0x40CDCF),
//     demi-hauteur = (mesh+64)*0.5 (`fld [ebx+40h]` @0x40CDFF), bind passe 1 VS01+PS02
//     (@0x40D2A3..0x40D2DC), SetMatrix(mWorldViewProjMatrix) @0x40D32B,
//     SetFloatArray(mLightAmbient, 3) @0x40D34D, SetTexture @0x40D36C,
//     SetVertexDeclaration @0x40D383, DrawPrimitiveUP(TRIANGLESTRIP, 2, quad, 76) @0x40D39A.
//
//  CE QUI BLOQUE (non prouvé — NE PAS DEVINER, cf. règle projet) : l'orientation du quad.
//   Le vecteur d'axe est `&flt_18C5264` si (mesh+56)==1 (`cmp [ebx+38h], esi` @0x40CDD2),
//   sinon `&unk_18C52BC`. Or ces deux adresses SONT des champs de g_GxdRenderer :
//     0x18C5264 - 0x18C4EF8 = 876   |   0x18C52BC - 0x18C4EF8 = 964
//   `xrefs_to` sur les deux ne rend QUE DES LECTEURS (9 et 7 : Model_DrawSkinnedSubset,
//   PtclDef_RenderQuads, Mesh_DrawAnimatedFrame, cMesh_RenderAnimated,
//   cMesh_RenderBillboardOutline, cVtxAnimMesh_RenderAnimated/RenderFrame/
//   RenderBillboardShadow) — AUCUN writer absolu, l'écriture étant esi-relative
//   (esi = this = 0x18C4EF8), même schéma que flt_18C53C0 (cf. MeshRenderer.h:396-401).
//   Le producteur présumé est GXD_BeginScene 0x404640 (base/up billboard, +876/+888/+964/+976)
//   — MAIS `xrefs_to(0x404640)` = 0 : la fonction est MORTE dans le binaire. On a donc
//   9 lecteurs vivants d'une valeur qu'aucun producteur atteignable ne calcule : soit un
//   autre writer esi-relatif reste à trouver, soit ces vecteurs sont figés à l'init.
//   -> Fabriquer ici un axe « caméra droite/haut » serait une INVENTION : le quad serait
//      orienté, mais pas comme l'original. Tant que le producteur de +876/+964 n'est pas
//      identifié (piste : x32dbg, watch mémoire sur 0x18C5264 pendant une frame in-game),
//      la branche billboard reste non écrite. Ne pas « compléter » ce TODO au jugé.
void MeshRenderer::DrawSkinnedSubset(const SkinnedMesh& mesh, int lod,
                                     const D3DXMATRIX&  world,
                                     const BonePalette& palette,
                                     const SkinnedMesh* matSrc,
                                     int                pass) {
    if (!Ready() || lod < 0 || lod >= static_cast<int>(mesh.lods.size())) return;
    const SkinnedLod& L = mesh.lods[lod];
    if (!L.vb || !L.ib || L.vertexCount == 0 || L.faceCount == 0) return;

    // -1) SOURCE DES MATÉRIAUX (a1 = override) — @0x40ca96..0x40cabc :
    //       if (a1) { v61 = a1;     v62 = a7;     v56 = a8;     }  // triplet override
    //       else    { v61 = a2+712; v62 = a2+768; v56 = a2+824; }  // matériaux propres du mesh
    //     La GÉOMÉTRIE (`L`) reste TOUJOURS celle de `mesh` — seuls les matériaux basculent.
    const SkinnedMesh& M = matSrc ? *matSrc : mesh;

    // 0a) FILTRE DE PASSE (a3) — Model_DrawSkinnedSubset @0x40cb14..0x40cb32 :
    //      if (a3 == 1) { if (blendMode == 2) return; }   // passe 1 : tout SAUF l'alpha blend
    //      else         { if (blendMode != 2) return; }   // passe 2 : l'alpha blend UNIQUEMENT
    //    blendMode vient de v61+44 = du matériau EFFECTIF (donc de mesh[0] en cas d'héritage).
    //    kPassBoth (défaut) = shim hors binaire : aucun filtre (cf. .h).
    if (pass == kDrawPass_Opaque) {
        if (M.blendMode == 2) return;
    } else if (pass == kDrawPass_Blend) {
        if (M.blendMode != 2) return;
    }

    // 0b) Source de shaders : slots RÉELS du npk (Shader03 VS03_SkinnedLit 0x409AB0 + Shader04
    //    PS04_Tex 0x409CC0) si un ShaderSet est attaché, sinon HLSL reconstruit (fallback).
    //    Uniformes/handles prouvés identiques (0x409c23..0x409c8f) -> swap purement interne.
    IDirect3DVertexShader9*      useVS   = vs_;
    IDirect3DPixelShader9*       usePS   = ps_;
    ID3DXConstantTable*          useCT   = ctVs_;
    D3DXHANDLE                   hKey    = hKeyMatrix_;
    D3DXHANDLE                   hWvp    = hWorldViewProj_;
    D3DXHANDLE                   hDir    = hLightDirection_;
    D3DXHANDLE                   hAmb    = hLightAmbient_;
    D3DXHANDLE                   hDif    = hLightDiffuse_;
    UINT                         useSamp = sampler0_;
    IDirect3DVertexDeclaration9* useDecl = decl_;
    bool                         realShader = false;
    if (shaderSet_) {
        const GxdShader& gvs = shaderSet_->Get(GxdShaderId::VS03_SkinnedLit);
        const GxdShader& gps = shaderSet_->Get(GxdShaderId::PS04_Tex);
        if (gvs.Valid() && gps.Valid()) {
            realShader = true;
            useVS = gvs.vs;   usePS = gps.ps;   useCT = gvs.ct;
            hKey  = gvs.Handle("mKeyMatrix");
            hWvp  = gvs.Handle("mWorldViewProjMatrix");
            hDir  = gvs.Handle("mLightDirection");
            hAmb  = gvs.Handle("mLightAmbient");
            hDif  = gvs.Handle("mLightDiffuse");
            const int s = gps.Sampler("mTexture0");             // registre de mTexture0 (0x409e34)
            useSamp = (s >= 0) ? static_cast<UINT>(s) : 0;
            // Déclaration de vertex réelle g_GxdSkinVtxDecl (0x1945918, dérivée de 0x814A58).
            if (IDirect3DVertexDeclaration9* d = shaderSet_->SkinnedVertexDecl()) useDecl = d;
        }
    }

    // 0c) SOBJ-04 / GX-SH-01 — SÉLECTION DE LA PASSE MULTI-TEXTURES (@0x40d3a8..0x40d66a).
    //   Seule la branche `else` (g_TextureDetailLevel >= 2) est vivante (cf. kTextureDetailLevel).
    //   Logique exacte, désassemblée (v21 = index LOD ; v62 = mat1 ; v56 = mat2 ; `+52` = pTexture) :
    //     if (v21 != 0)                        -> PASSE 2   ; multi-texture SEULEMENT au LOD 0 (0x40d61f)
    //     mat1.tex && mat2.tex                 -> PASSE 4   ; 0x40d62d -> 0x40d660 vrai -> 0x40d675
    //     mat1.tex && !mat2.tex                -> PASSE 3   ; 0x40d660 faux -> chute vers 0x40d9b6
    //     !mat1.tex && mat2.tex                -> PASSE 4   ; 0x40d63e non pris -> 0x40d660 vrai
    //     !mat1.tex && !mat2.tex               -> PASSE 2   ; 0x40d642 -> LABEL_53
    //   Mapping mat->sampler prouvé PAR LES NOMS DE CONSTANTES des loaders, PAS par le nommage de
    //   pile d'IDA (infiable dans cette zone : delta esp mal suivi) :
    //     Shader_LoadPS06_MultiTex  0x40A060 -> mTexture0, mTexture1                 (2 textures)
    //     Shader_LoadPS08_MultiTex3 0x40A490 -> mTexture0, mTexture1, mTexture2      (3 textures)
    //   corroboré par le NOMBRE de SetTexture (passe 3 : 2 @0x40db25/@0x40db44 ; passe 4 : 3
    //   @0x40d849/@0x40d868/@0x40d887) -> mat0->mTexture0, mat1->mTexture1, mat2->mTexture2.
    //
    //   FALLBACK PRÉSERVÉ : le HLSL reconstruit kSkinnedPS n'a QUE mTexture0 -> on ne bascule en
    //   passe 3/4 que sur shaders RÉELS du npk. Sans ShaderSet, on reste en passe 2 : le chemin
    //   qui fonctionne aujourd'hui est intact.
    int usePass = kPass_SkinnedLit;
    const GxdShader* mtVS = nullptr;
    const GxdShader* mtPS = nullptr;
    if (realShader && lod == 0 && kTextureDetailLevel >= 2) {
        const bool t1 = (M.tex1 != nullptr); // v62 && *(v62+52)
        const bool t2 = (M.tex2 != nullptr); // v56 && *(v56+52)
        if (t1 || t2) {
            // pass 4 <=> t2 (t1&&t2 ET !t1&&t2) ; pass 3 <=> t1 && !t2.
            const bool wantPass4 = t2;
            const GxdShader& gv = shaderSet_->Get(wantPass4 ? GxdShaderId::VS07_SkinnedEye
                                                            : GxdShaderId::VS05_Skinned);
            const GxdShader& gp = shaderSet_->Get(wantPass4 ? GxdShaderId::PS08_MultiTex3
                                                            : GxdShaderId::PS06_MultiTex);
            if (gv.Valid() && gp.Valid()) {
                usePass = wantPass4 ? kPass_MultiTex3 : kPass_MultiTex2;
                mtVS = &gv;  mtPS = &gp;
                useVS = gv.vs;  usePS = gp.ps;  useCT = gv.ct;
                // VS05 (0x409E80) : mKeyMatrix, mWorldViewProjMatrix, mLightDirection.
                // VS07 (0x40A290) : idem + mCameraEye. Aucun des deux n'a mLightAmbient/mLightDiffuse
                // -> en passes 3/4 l'ambiante/diffuse vivent sur le PIXEL shader (voir plus bas).
                hKey = gv.Handle("mKeyMatrix");
                hWvp = gv.Handle("mWorldViewProjMatrix");
                hDir = gv.Handle("mLightDirection");
                hAmb = nullptr;
                hDif = nullptr;
            }
        }
    }

    // 1) États de mélange du matériau EFFECTIF (v16 = *(v61+44)).
    applyBlendMode(M.blendMode);

    // 2) Bind du programme (évite les re-bind redondants via currentPass_ == g_CurrentShaderPass).
    if (currentPass_ != usePass) {
        dev_->SetVertexShader(useVS);
        dev_->SetPixelShader(usePS);
        currentPass_ = usePass;
    }
    dev_->SetVertexDeclaration(useDecl); // g_SkinVertexDecl (method +348)

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
        // Model_DrawSkinnedSubset ne clampe PAS (0x40d4e8) : sur shader réel, on passe nBones tel
        // quel (D3DX borne lui-même à mKeyMatrix.Elements = boneArraySize_). Sur le fallback HLSL,
        // mKeyMatrix[40] impose la borne kMaxBones pour ne pas déborder le tableau.
        if (!realShader && boneCount > kMaxBones) boneCount = kMaxBones;
    }
    if (hKey) useCT->SetMatrixArray(dev_, hKey, palMats, boneCount);

    // 6) WVP + lumière (SetMatrix +84, SetFloatArray +72).
    if (hWvp) useCT->SetMatrix(dev_, hWvp, &wvp);
    if (hDir) useCT->SetFloatArray(dev_, hDir, &lightObj.x, 3);
    if (hAmb) useCT->SetFloatArray(dev_, hAmb, &lightAmbient_.x, 3);
    if (hDif) useCT->SetFloatArray(dev_, hDif, &lightDiffuse_.x, 3);

    // 7) Textures (method +260 SetTexture) — GX-SH-02 : mat1/mat2 sont désormais TRANSPORTÉES
    //    jusqu'ici (SkinnedMesh::tex1/tex2) et uploadées sur leurs samplers.
    if (usePass == kPass_SkinnedLit) {
        // Passe 2 : mTexture0 seul (@0x40d590).
        if (M.diffuse) dev_->SetTexture(useSamp, M.diffuse);
    } else {
        // Passes 3/4 : samplers résolus PAR NOM sur le PS réel (aucun registre en dur).
        const int s0 = mtPS->Sampler("mTexture0");
        if (s0 >= 0) dev_->SetTexture(static_cast<UINT>(s0), M.diffuse); // @0x40db25 / @0x40d849
        const int s1 = mtPS->Sampler("mTexture1");
        if (s1 >= 0) dev_->SetTexture(static_cast<UINT>(s1), M.tex1);    // @0x40db44 / @0x40d868
        if (usePass == kPass_MultiTex3) {
            const int s2 = mtPS->Sampler("mTexture2");
            if (s2 >= 0) dev_->SetTexture(static_cast<UINT>(s2), M.tex2); // @0x40d887 (passe 4 seule)
        }
        // mLightAmbient/mLightDiffuse sur le PIXEL shader en passes 3/4 (PS06 0x40A060 / PS08
        // 0x40A490) : @0x40db65/@0x40db86 (passe 3) et @0x40d8a9/@0x40d8cb (passe 4).
        if (mtPS->ct) {
            if (D3DXHANDLE h = mtPS->Handle("mLightAmbient"))
                mtPS->ct->SetFloatArray(dev_, h, &lightAmbient_.x, 3);
            if (D3DXHANDLE h = mtPS->Handle("mLightDiffuse"))
                mtPS->ct->SetFloatArray(dev_, h, &lightDiffuse_.x, 3);
        }
        // mCameraEye — VS07 UNIQUEMENT (passe 4, 0x40A290) : position caméra monde
        // (g_CameraEye dword_18C51C0/C4/C8 @0x40d757..0x40d783) ramenée en ESPACE OBJET par
        // Vec3_TransformCoord(v89, v89, invWorld) @0x40d78b, puis posée @0x40d82a.
        if (usePass == kPass_MultiTex3 && useCT) {
            if (D3DXHANDLE h = mtVS->Handle("mCameraEye")) {
                D3DXVECTOR3 eyeObj;
                D3DXVec3TransformCoord(&eyeObj, &eye_, &invWorld);
                useCT->SetFloatArray(dev_, h, &eyeObj.x, 3);
            }
        }
    }

    // 8) Flux + indices + dessin (SetStreamSource +400 stride 76, SetIndices +416,
    //    DrawIndexedPrimitive +328 : TRIANGLELIST, nbVtx, nbTri).
    dev_->SetStreamSource(0, L.vb, 0, static_cast<UINT>(sizeof(GpuSkinVertex)));
    dev_->SetIndices(L.ib);
    dev_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, L.vertexCount, 0, L.faceCount);

    // 9) Restauration des états de blend (LABEL_72 de l'original, @0x40d953 : `*(v61+44)`
    //    -> comme à l'aller, sur le matériau EFFECTIF).
    resetBlendMode(M.blendMode);
}

// États de blend par matériau — Model_DrawSkinnedSubset 0x40CA40 @0x40cba4..0x40cc30.
//   blendMode 1 = alpha-test ; blendMode 2 = ALPHA BLEND (SRCALPHA/INVSRCALPHA), PAS un additif.
//
// DÉFAUT CORRIGÉ (Passe 4 / W7) — cette fonction n'était PAS bit-exacte, contrairement à ce
// qu'affirmait le commentaire qui vivait ici : il MANQUAIT un état à CHACUNE des deux branches.
// Hex-Rays les avait perdus parce que les deux branches partagent leur DERNIER SetRenderState via
// une queue commune (`jmp loc_40CC22` @0x40cbd8 / chute @0x40cc20 -> `call` @0x40cc30) : le
// décompilateur n'en restituait pas les arguments. Relevé au désassemblage :
//   mode 1 : push esi(=1)/push 0Fh @0x40cbb2 -> ALPHATESTENABLE=1 @0x40cbbb
//            push 5/push 19h      @0x40cbca -> ALPHAFUNC=5 (D3DCMP_GREATEREQUAL) @0x40cbcf
//            push 80h/push 18h    @0x40cbd1 -> ALPHAREF=128            <-- MANQUAIT
//   mode 2 : push 0/push 0Eh      @0x40cbf0 -> ZWRITEENABLE=0 @0x40cbf5
//            push esi(=1)/push 1Bh@0x40cc04 -> ALPHABLENDENABLE=1 @0x40cc08
//            push 5/push 13h      @0x40cc17 -> SRCBLEND=5 (D3DBLEND_SRCALPHA) @0x40cc1c
//            push 6/push 14h      @0x40cc1e -> DESTBLEND=6 (D3DBLEND_INVSRCALPHA) <-- MANQUAIT
// POURQUOI C'ÉTAIT CRITIQUE : resetBlendMode remet ALPHAREF=0 et DESTBLEND=1 (ZERO). Sans ces
// deux états, dès le 2e mesh, mode 1 -> alpha-test `alpha >= 0` = toujours vrai = INOPÉRANT, et
// mode 2 -> blend SRCALPHA/ZERO = destination ÉCRASÉE au lieu d'être mélangée. Le défaut était
// LATENT tant que blendMode valait 0 partout ; corriger SOBJ-02 sans corriger ceci l'aurait ACTIVÉ.
// Les deux correctifs sont donc indissociables.
void MeshRenderer::applyBlendMode(uint32_t blendMode) {
    if (blendMode == 1) {
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaTestEnable), TRUE);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaFunc), kCMP_GreaterEqual);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaRef), kAlphaRef_Material);
    } else if (blendMode == 2) {
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_ZWriteEnable), FALSE);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaBlendEnable), TRUE);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_SrcBlend), kBLEND_SrcAlpha);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_DestBlend), kBLEND_InvSrcAlpha);
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

// ===========================================================================
//  Ombres — Model_RenderWithShadow 0x40EEE0 / Model_BuildShadowVolume 0x40DC70
// ===========================================================================

// FRONT FX-F4 (tache ombre) : DEFERRED -- cette fonction reste NON cablee, intentionnellement.
// PREUVE DURCIE (Passe 4 / vague W5, front shadow-wiring, 2026-07-16) : le volume d'ombre stencil
// est du CODE MORT INATTEIGNABLE **dans le binaire lui-meme**. Ce n'est PAS un blocage de ShaderSet :
// attacher VS15 ne "debloque" rien, il n'y a aucun appelant a debloquer.
//
//   1) Chaine d'appelants ENUMEREE EXHAUSTIVEMENT (xrefs_to, pas un `reaches` borne) :
//        Model_BuildShadowVolume 0x40DC70  <- 1 seul appelant : Model_RenderWithShadow 0x40EEE0
//        Model_RenderWithShadow  0x40EEE0  <- 1 seul appelant : SObject_DrawAnimated 0x4D9050 (3 sites)
//        SObject_DrawAnimated    0x4D9050  <- 3 appelants : Char_DrawShadow 0x580CE0,
//                                             Npc_DrawMeshShadow 0x5800E0,
//                                             Char_DrawWeaponEffectVariantA 0x568FE0
//        ces 3 tetes de chaine                <- 0 xref CHACUNE  => MORTES.
//   2) L'objection "appel indirect / vtable" est ELIMINEE : `find_bytes` des 6 adresses en
//      little-endian (E0 0C 58 00 / E0 8F 56 00 / E0 00 58 00 / 50 90 4D 00 / E0 EE 40 00 /
//      70 DC 40 00) rend **0 occurrence dans TOUTE l'image**. Aucune vtable, table de pointeurs
//      ni jump-table ne peut les atteindre. `reaches(Scene_InGameRender 0x52D0B0 -> 0x40DC70)` = false.
//   3) Constantes GELEES -- GXD_InitGlobalState 0x401320 est le SEUL writer (verifie par xrefs) :
//        g_ShadowsEnabled 0x18C4F14 = 1 (0x4013B2) : son UNIQUE lecteur est 0x40EEEC, dans la
//          fonction morte -> global inerte, ne pilote plus rien.
//        g_ShadowMethod   0x18C4F18 = 1 (0x4013B8) : la branche z-fail Carmack `if (!method)`
//          (0x40F671) serait donc MORTE PAR VALEUR meme si la fonction vivait.
//
// NE PAS "corriger" ce non-cablage : dessiner ce volume serait dessiner un effet que le binaire
// d'origine ne produit JAMAIS. La fonction reste en place comme trace de RE fidele.
//
// ATTENTION -- ne pas en deduire "aucune ombre n'est dessinee" (erreur de l'ancienne redaction) :
// le client dessine bel et bien des ombres, mais PLANAIRES, par une chaine JUMELLE VIVANTE que la
// Passe 3 avait manquee : Model_RenderPlanarShadow 0x40F720 (aplatissement j_D3DXMatrixShadow
// @0x40FB28, passe 5 = VS09 g_GxdSh09_VS + PS NULL), atteinte depuis Scene_InGameRender 0x52D0B0
// via SObject_DrawAnimated2 0x4D91C0 (`reaches` = true, profondeur 3). Les devs ont DUPLIQUE la
// chaine puis bascule sur le planaire, orphelinant le volume (tailles identiques deux a deux) :
//        MORT (-> volume)                        VIVANT (-> planaire)                taille
//        Char_DrawShadow 0x580CE0                Char_DrawReflection 0x581090        0x3A4
//        Npc_DrawMeshShadow 0x5800E0             Npc_DrawMeshGlow 0x5801D0           0xE2
//        Char_DrawWeaponEffectVariantA 0x568FE0  Char_DrawWeaponEffectVariantB 0x56BF90  0x2AFF
// Cf. le bandeau Scene/WorldRenderer.h §Ombre pour le bracket de scene et l'etat de ce chantier.
void MeshRenderer::DrawModelShadow(const SkinnedModel& model,
                                   const D3DXVECTOR3&  position,
                                   const D3DXVECTOR3&  rotationDeg,
                                   const D3DXVECTOR3&  scale,
                                   const BonePalette&  palette,
                                   float               boundRadius) {
    // 1) Gate g_ShadowsEnabled (0x40eef8) + entités présentes. Le volume d'ombre passe par VS15
    //    (g_GxdSh15_VS) : sans ShaderSet réel attaché, le HLSL reconstruit n'a pas ce shader -> no-op.
    if (!Ready() || !shadowsEnabled_ || model.Empty()) return;
    if (!shaderSet_) return;
    const GxdShader& sh15 = shaderSet_->Get(GxdShaderId::VS15_WorldVP);
    if (!sh15.Valid()) return;

    // 2) Matrice monde = Scale*RotZ*RotY*RotX*Translate (deg->rad ×0.017453292 ; 0x40f3b7..0x40f4c3).
    D3DXMATRIX mS, mRx, mRy, mRz, mT, tmp, world;
    D3DXMatrixScaling(&mS, scale.x, scale.y, scale.z);
    D3DXMatrixRotationX(&mRx, rotationDeg.x * kDeg2Rad);
    D3DXMatrixRotationY(&mRy, rotationDeg.y * kDeg2Rad);
    D3DXMatrixRotationZ(&mRz, rotationDeg.z * kDeg2Rad);
    D3DXMatrixTranslation(&mT, position.x, position.y, position.z);
    D3DXMatrixMultiply(&world, &mS,    &mRz);
    D3DXMatrixMultiply(&tmp,   &world, &mRy);
    D3DXMatrixMultiply(&world, &tmp,   &mRx);
    D3DXMatrixMultiply(&tmp,   &world, &mT);
    world = tmp;

    // 3) Distance modèle<->caméra (0x40ef8e : dist = |position - eye|). Le test de frustum sphère
    //    (Frustum_IntersectsSphere 0x406660, centre = position + Y·boundRadius·0.5) n'est qu'une
    //    optimisation de culling -> écarté ici sans changer le rendu.
    D3DXVECTOR3 delta = position - eye_;
    const float dist = D3DXVec3Length(&delta);

    if (dist > fogNear_) {
        // ---- LOIN (0x40efde) : ombre planaire projetée ----
        // TODO [ancre 0x40F720] Model_RenderPlanarShadow : projection planaire + masque stencil
        //   anti-double-blend (TEXTUREFACTOR60 = luminance diffuse moyenne ×64 <<24, SRCALPHA/
        //   INVSRCALPHA, DEPTHBIAS195=1e-5 anti z-fight). Non réversée/possédée ici (géométrie
        //   projetée séparée) : régime laissé en attente pour ne pas programmer un état device
        //   sans le draw associé.
        return;
    }

    // ---- PROCHE (0x40efc6) : VOLUME D'OMBRE STENCIL ----
    // WVP = world · view · proj (0x40f50f/0x40f523 ; viewProj_ = view·proj).
    D3DXMATRIX wvp;
    D3DXMatrixMultiply(&wvp, &world, &viewProj_);

    // Passe 8 = VS15 + PS NULL (cache g_CurrentShaderPass ; 0x40f4d0..0x40f4fe).
    if (currentPass_ != kPass_ShadowVolume) {
        currentPass_ = kPass_ShadowVolume;
        dev_->SetVertexShader(sh15.vs); // method +368 = g_GxdSh15_VS
        dev_->SetPixelShader(nullptr);  // method +428 (PS NULL : écritures depth/stencil seules)
    }
    if (D3DXHANDLE h = sh15.Handle("mWorldViewProjMatrix"))
        sh15.ct->SetMatrix(dev_, h, &wvp); // g_GxdSh15_hWorldViewProj (method +84 ; 0x40f55b)
    dev_->SetTexture(0, nullptr);          // method +260 (0x40f56f)

    // Direction lumière d'ombre en espace objet : négée, transformée par inverse(world), normalisée
    // (flt_18C53C0/C4/C8 ; 0x40f580..0x40f5cc).
    D3DXMATRIX invWorld;
    D3DXMatrixInverse(&invWorld, nullptr, &world);
    D3DXVECTOR3 lightObj(-shadowLightDir_.x, -shadowLightDir_.y, -shadowLightDir_.z);
    D3DXVec3TransformNormal(&lightObj, &lightObj, &invWorld);
    D3DXVec3Normalize(&lightObj, &lightObj);

    // Boucle des meshes (0x40f5e5..0x40f703). Régime proche : le fondu v30 sature à 1.0 (0x40f367)
    // -> LOD 0 (0x40dca3). Chaque mesh non-billboard skinne + silhouette + extrude sa géométrie.
    //
    // CORRECTION DE FIDÉLITÉ (Passe 4 / W5b, front shadow-fidelity) : a9 (longueur d'extrusion)
    // n'est PAS « hors périmètre » — elle est PROUVÉE, et vaut exactement a2 × 2.5.
    //   SObject_DrawAnimated 0x4D9050 est l'appelant UNIQUE de Model_RenderWithShadow 0x40EEE0
    //   (xrefs_to(0x40EEE0) = 3, et les 3 sont dans cette seule fonction) -> la relation est
    //   universelle, pas propre à une branche. Aux 3 sites, a9 est calculée puis passée :
    //     calcul  v11/v10/v9 = a5 * 2.5   @0x4D90C6 / @0x4D9129 / @0x4D9178
    //     appel   Model_RenderWithShadow(..., a2=a5, ..., a9=a5*2.5)
    //                                    @0x4D90F9 / @0x4D915C / @0x4D91B4
    //   Le param a2 de Model_RenderWithShadow reçoit a5 de DrawAnimated -> a9 = a2 × 2.5.
    //   Consommée par Model_BuildShadowVolume(v20, a6, a7, v30, &v31, a9) @0x40F61C.
    //   Ici a2 == boundRadius (diamètre englobant, cf. .h) -> extrusion = boundRadius * 2.5f.
    for (const SkinnedMesh& mesh : model.meshes) {
        if (mesh.empty || mesh.lods.empty()) continue;
        const SkinnedLod& L = mesh.lods[0];
        // a9 = a2 * 2.5 — SObject_DrawAnimated 0x4D9050 @0x4D90C6/@0x4D9129/@0x4D9178.
        if (!buildShadowVolume(L, palette, lightObj, boundRadius * 2.5f)) continue;

        const UINT triCount = shadowVolVertCount_ / 3;
        if (triCount == 0) continue;
        const UINT stride = 3 * sizeof(float); // sommet d'ombre = position seule (12 o)

        // 1ère passe : FVF XYZ + DrawPrimitiveUP (cull hérité) — 0x40f639/0x40f668.
        dev_->SetFVF(kFVF_XYZ); // method +356
        dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triCount, shadowVol_.data(), stride); // method +332

        if (shadowMethod_ == 0) {
            // z-fail (Carmack reverse) : CW+INCR redraw, puis DECR+CCW (0x40f685..0x40f6f0).
            dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_CullMode),     kCULL_CW);
            dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_StencilZFail), kSTENCILOP_Incr);
            dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triCount, shadowVol_.data(), stride);
            dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_StencilZFail), kSTENCILOP_Decr);
            dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_CullMode),     kCULL_CCW);
        }
        // g_ShadowMethod==1 (stencil two-sided) : le régime proche ne fait que la 1ère passe
        //   (les états two-sided TWOSIDEDSTENCILMODE185 sont programmés dans le régime planaire).
    }
}

// Model_BuildShadowVolume 0x40DC70 — skinning CPU des sommets + silhouette extrudée.
// Opère sur les données CPU retenues (SkinnedLod::skinCpu/idxTopo/idxAdj). Écrit shadowVol_
// (XYZ interleavé, stride 12) et shadowVolVertCount_ ; renvoie false si abandon (0x40dc87/0x40dd37/
// 0x40e1bd) ou données absentes.
bool MeshRenderer::buildShadowVolume(const SkinnedLod&  L,
                                     const BonePalette& palette,
                                     const D3DXVECTOR3& lightDirObj,
                                     float              extrude) {
    // Mesh vide / billboard / données CPU absentes -> 0 (0x40dc87).
    if (L.vertexCount == 0 || L.faceCount == 0) return false;
    if (L.skinCpu.empty() || L.idxTopo.empty() || L.idxAdj.empty()) return false;
    if (!palette.Valid()) return false;
    // Garde LOD (0x40dd37) : > 10000 sommets ou faces -> abandon.
    if (L.vertexCount > 10000 || L.faceCount > 10000) return false;

    const UINT nVtx   = L.vertexCount;
    const UINT nFace  = L.faceCount;
    const D3DXMATRIX* bones = palette.matrices; // tranche de frame MOTION (base + frame·nBones)
    const UINT nBones = palette.count;

    // Redimensionne les tampons scratch (globaux d'origine, plafonds identiques).
    if (worldPos_.size() < static_cast<size_t>(nVtx) * 3)
        worldPos_.resize(static_cast<size_t>(nVtx) * 3);
    if (faceLightFacing_.size() < nFace)
        faceLightFacing_.resize(nFace);
    if (shadowVol_.size() < static_cast<size_t>(kShadowVolMaxVerts) * 3)
        shadowVol_.resize(static_cast<size_t>(kShadowVolMaxVerts) * 3);

    // --- 1) Skinning CPU (0x40dd76..0x40e004) : SkinVertex 32 o = pos[3]@+0, poids[4]@+12/16/20/24,
    //        boneIdx u32@+28 (4× u8). out = Σ_i w_i · (M[bone_i]·pos), transform row-vector affine. ---
    const uint8_t* skin = L.skinCpu.data();
    for (UINT i = 0; i < nVtx; ++i) {
        const uint8_t* v = skin + static_cast<size_t>(i) * 32;
        float px, py, pz, w[4];
        uint32_t packed;
        std::memcpy(&px, v + 0, 4);
        std::memcpy(&py, v + 4, 4);
        std::memcpy(&pz, v + 8, 4);
        std::memcpy(&w[0], v + 12, 4);
        std::memcpy(&w[1], v + 16, 4);
        std::memcpy(&w[2], v + 20, 4);
        std::memcpy(&w[3], v + 24, 4);
        std::memcpy(&packed, v + 28, 4);

        float ox = 0.0f, oy = 0.0f, oz = 0.0f;
        for (int k = 0; k < 4; ++k) {
            UINT b = (packed >> (8 * k)) & 0xFFu;
            if (b >= nBones) b = 0; // garde (indices d'os hors palette — jamais atteint sur data valide)
            const D3DXMATRIX& m = bones[b];
            // == v105/106/107 : _11*px+_21*py+_31*pz+_41 (translation incluse).
            const float tx = px * m._11 + py * m._21 + pz * m._31 + m._41;
            const float ty = px * m._12 + py * m._22 + pz * m._32 + m._42;
            const float tz = px * m._13 + py * m._23 + pz * m._33 + m._43;
            ox += w[k] * tx; oy += w[k] * ty; oz += w[k] * tz;
        }
        worldPos_[static_cast<size_t>(i) * 3 + 0] = ox;
        worldPos_[static_cast<size_t>(i) * 3 + 1] = oy;
        worldPos_[static_cast<size_t>(i) * 3 + 2] = oz;
    }

    const uint16_t* topo = reinterpret_cast<const uint16_t*>(L.idxTopo.data());
    const uint16_t* adj  = reinterpret_cast<const uint16_t*>(L.idxAdj.data());
    auto WP = [&](UINT idx) -> const float* { return &worldPos_[static_cast<size_t>(idx) * 3]; };

    // --- 2) Face facing (0x40e02c..0x40e122) : normale = eA×eB, éclairée si N·lightDir > 0. ---
    for (UINT f = 0; f < nFace; ++f) {
        const uint16_t i0 = topo[f * 3 + 0];
        const uint16_t i1 = topo[f * 3 + 1];
        const uint16_t i2 = topo[f * 3 + 2];
        const float* p0 = WP(i0);
        const float* p1 = WP(i1);
        const float* p2 = WP(i2);
        const float ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2]; // eA = p1-p0
        const float bx = p2[0] - p1[0], by = p2[1] - p1[1], bz = p2[2] - p1[2]; // eB = p2-p1
        const float nx = bz * ay - by * az; // v96
        const float ny = az * bx - bz * ax; // v97
        const float nz = ax * by - bx * ay; // v98
        faceLightFacing_[f] =
            (nx * lightDirObj.x + lightDirObj.y * ny + lightDirObj.z * nz > 0.0f) ? 1 : 0;
    }

    // --- 3) Silhouette + extrusion (0x40e156..0x40e635). ---
    shadowVolVertCount_ = 0;
    const float ex = lightDirObj.x * extrude; // v53
    const float ey = lightDirObj.y * extrude; // v52
    const float ez = lightDirObj.z * extrude; // v50
    float* vol = shadowVol_.data();
    auto emit = [&](float x, float y, float z) {
        const size_t o = static_cast<size_t>(shadowVolVertCount_) * 3;
        vol[o + 0] = x; vol[o + 1] = y; vol[o + 2] = z;
        ++shadowVolVertCount_;
    };

    for (UINT f = 0; f < nFace; ++f) {
        if (faceLightFacing_[f] != 1) continue;
        if (shadowVolVertCount_ > kShadowVolGuard) return false; // v45 > 29976 (0x40e1bd)

        const uint16_t i0 = topo[f * 3 + 0];
        const uint16_t i1 = topo[f * 3 + 1];
        const uint16_t i2 = topo[f * 3 + 2];
        const float* p0 = WP(i0);
        const float* p1 = WP(i1);
        const float* p2 = WP(i2);

        // Front cap = la face éclairée (p0, p1, p2) — 0x40e1fb..0x40e2b6.
        emit(p0[0], p0[1], p0[2]);
        emit(p1[0], p1[1], p1[2]);
        emit(p2[0], p2[1], p2[2]);

        // Arêtes de silhouette : voisin absent (==nFace) ou non éclairé -> quad extrudé.
        //   nearA = [p1,p2,p0], nearB = [p0,p1,p2] (tables d'indices &v100/&v99 ; 0x40e30b/0x40e34b).
        const float* nearA[3] = { p1, p2, p0 };
        const float* nearB[3] = { p0, p1, p2 };
        for (int e = 0; e < 3; ++e) {
            const uint16_t nb = adj[f * 3 + e]; // adjacence : voisin par arête (0x40e2e6)
            if (nb >= nFace || !faceLightFacing_[nb]) {
                const float* a = nearA[e];
                const float* b = nearB[e];
                // quad = 2 triangles (a, b, b_far, a, b_far, a_far) — 0x40e314..0x40e4d6.
                emit(a[0],       a[1],       a[2]);
                emit(b[0],       b[1],       b[2]);
                emit(b[0] - ex,  b[1] - ey,  b[2] - ez);
                emit(a[0],       a[1],       a[2]);
                emit(b[0] - ex,  b[1] - ey,  b[2] - ez);
                emit(a[0] - ex,  a[1] - ey,  a[2] - ez);
            }
        }

        // Back cap extrudé (p0_far, p2_far, p1_far) — 0x40e507..0x40e608.
        emit(p0[0] - ex, p0[1] - ey, p0[2] - ez);
        emit(p2[0] - ex, p2[1] - ey, p2[2] - ez);
        emit(p1[0] - ex, p1[1] - ey, p1[2] - ez);
    }
    return true;
}

} // namespace ts2::gfx

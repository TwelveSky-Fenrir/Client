// Gfx/MeshRenderer.h — rendu d'un mesh/modèle skinné du moteur GXD (Direct3D9).
//
// Réécriture FIDÈLE du chemin de rendu skinné GPU de TwelveSky2 :
//   Model_DrawSkinnedSubset 0x40CA40  (cœur : palette d'os -> constantes VS, DrawIndexedPrimitive)
//   Model_Render            0x40EBB0  (compose la matrice monde, boucle les meshes/LOD)
//   Mesh_ReadFromFile       0x40BC50  (sous-ensembles : VB 76 o, IB 6 o/face, INDEX16)
//   g_GxdVertexDecl         0x814A58  (déclaration D3DVERTEXELEMENT9 du vertex 76 o)
//   Shader_LoadVS03_SkinnedLit 0x409AB0 / Shader_LoadPS04_Tex 0x409CC0
//       (skinning GPU 4 influences via mKeyMatrix, cf. Docs/TS2_GXD_ENGINE.md §2.2)
//
// Ici on utilise le DirectX SDK June 2010 (d3dx9) exactement comme le binaire d'origine :
// D3DXCompileShader + ID3DXConstantTable (SetMatrixArray/SetMatrix/SetFloatArray),
// D3DXCreateTextureFromFileInMemoryEx, D3DXMatrix*.
//
// Le device physique est celui de ts2::gfx::Renderer (== Object B +524 dword_18C5104).
#pragma once
#include "Gfx/Renderer.h"
#include "Asset/Model.h"
#include <d3dx9.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ts2::gfx {

// ---------------------------------------------------------------------------
//  Vertex skinné GPU — 76 octets, layout EXACT de la déclaration g_GxdVertexDecl
//  (relevé à 0x814A58 ; cf. Docs/TS2_GXD_ENGINE.md §4.3) :
//    [0]  POSITION     FLOAT3   @ 0
//    [1]  BLENDWEIGHT  FLOAT4   @ 12  (4 poids)
//    [2]  BLENDINDICES D3DCOLOR @ 28  (4 indices d'os empaquetés)
//    [3]  TANGENT      FLOAT3   @ 32
//    [4]  BINORMAL     FLOAT3   @ 44
//    [5]  NORMAL       FLOAT3   @ 56
//    [6]  TEXCOORD0    FLOAT2   @ 68
//  Les 32 premiers octets = SkinVertex CPU (position + 4 poids + 4 indices).
// ---------------------------------------------------------------------------
#pragma pack(push, 1)
struct GpuSkinVertex {
    float    position[3];    // +0   POSITION
    float    blendWeight[4]; // +12  BLENDWEIGHT (w0..w3)
    uint32_t blendIndices;   // +28  BLENDINDICES (D3DCOLOR : octets b0,b1,b2,b3)
    float    tangent[3];     // +32  TANGENT
    float    binormal[3];    // +44  BINORMAL
    float    normal[3];      // +56  NORMAL
    float    texcoord[2];    // +68  TEXCOORD0
};
#pragma pack(pop)
static_assert(sizeof(GpuSkinVertex) == 76, "GpuSkinVertex doit faire 76 octets");
static_assert(offsetof(GpuSkinVertex, blendWeight) == 12, "BLENDWEIGHT @ 12");
static_assert(offsetof(GpuSkinVertex, blendIndices) == 28, "BLENDINDICES @ 28");
static_assert(offsetof(GpuSkinVertex, tangent) == 32, "TANGENT @ 32");
static_assert(offsetof(GpuSkinVertex, binormal) == 44, "BINORMAL @ 44");
static_assert(offsetof(GpuSkinVertex, normal) == 56, "NORMAL @ 56");
static_assert(offsetof(GpuSkinVertex, texcoord) == 68, "TEXCOORD @ 68");

// Palette d'os déjà résolue pour UNE frame (mKeyMatrix uploadé par SetMatrixArray).
// Dans Model_DrawSkinnedSubset : base + ((frame*bonesPerFrame) << 6), count=bonesPerFrame.
struct BonePalette {
    const D3DXMATRIX* matrices = nullptr; // tranche de frame (bonesPerFrame matrices 64 o)
    UINT              count    = 0;       // nombre d'os (bonesPerFrame)
    bool Valid() const { return matrices != nullptr && count > 0; }
};

// Palette d'animation complète (MotionPalette : valid/frameCount/bonesPerFrame/base,
// cf. Docs/TS2_GXD_ENGINE.md §5.7). Une matrice = 64 o (4x4 float).
struct MotionPalette {
    int               valid         = 0;
    int               frameCount    = 0;
    int               bonesPerFrame = 0;
    const D3DXMATRIX* base          = nullptr;

    // Découpe la tranche d'os d'une frame (frame = ftol(animTime), borné 0..frameCount-1).
    // Reproduit v59 = ftol(a5) puis base + ((frame*bonesPerFrame) << 6).
    BonePalette FrameSlice(float animTime) const;
};

// Un niveau de LOD d'un mesh, résident GPU (Model_DrawSkinnedSubset : tableaux
// a2+688 = VB par LOD, a2+696 = IB par LOD, a2+684 = nbVtx, a2+692 = nbTri).
struct SkinnedLod {
    IDirect3DVertexBuffer9* vb          = nullptr; // 76 o/sommet
    IDirect3DIndexBuffer9*  ib          = nullptr; // INDEX16, 6 o/face
    UINT                    vertexCount = 0;
    UINT                    faceCount   = 0;
};

// Un mesh du modèle (SObjectMesh 888 o). Les "subsets" du parseur = niveaux de LOD.
struct SkinnedMesh {
    std::vector<SkinnedLod> lods;
    IDirect3DTexture9*      diffuse   = nullptr; // matDiffuse.texture (GXD_Material +0x34)
    uint32_t                blendMode = 0;       // GXD_Material +0x2C : 0=opaque/1=alphatest/2=additif
    bool                    empty     = true;    // valid==0 => mesh vide
};

// Modèle skinné complet (.SOBJECT) résident GPU.
class SkinnedModel {
public:
    std::vector<SkinnedMesh> meshes;

    bool Empty() const { return meshes.empty(); }
    void Release(); // libère tous les VB/IB/textures

    ~SkinnedModel() { Release(); }
};

// ---------------------------------------------------------------------------
//  MeshRenderer — possède la déclaration de vertex skinné + le programme de
//  shaders skinné (VS SkinnedLit + PS texturé) et pilote le rendu.
// ---------------------------------------------------------------------------
class MeshRenderer {
public:
    // Nombre max d'os supportés par la palette VS (vs_2_0 : 256 registres float4 ;
    // 40 matrices = 160 registres + WVP + lumière). Doit égaler la taille du
    // tableau mKeyMatrix[] dans le HLSL.
    static constexpr UINT kMaxBones = 40;

    ~MeshRenderer() { Shutdown(); }

    // Construit la déclaration de vertex (g_GxdVertexDecl 0x814A58) + compile les shaders.
    bool Init(Renderer& renderer);
    void Shutdown();
    bool Ready() const { return dev_ != nullptr && decl_ != nullptr && vs_ != nullptr; }

    // Crée les IDirect3DVertexBuffer9/IndexBuffer9 (+ textures) depuis un .SOBJECT parsé.
    // Reproduit Mesh_ReadFromFile 0x40BC50 (côté upload GPU).
    bool Upload(const asset::SObject& src, SkinnedModel& out);

    // Caméra : matrices vue/projection de l'Object B (+748 / +648).
    void SetCamera(const D3DXMATRIX& view, const D3DXMATRIX& proj);

    // Lumière directionnelle monde (light @+1120 : Direction/Ambient/Diffuse).
    void SetLight(const D3DXVECTOR3& dirWorld,
                  const D3DXVECTOR3& ambient,
                  const D3DXVECTOR3& diffuse);

    // Model_Render 0x40EBB0 : compose world = Scale*RotZ*RotY*RotX*Translate
    // (angles Euler en degrés) puis dessine tous les meshes au LOD demandé.
    void DrawModel(const SkinnedModel& model,
                   const D3DXVECTOR3&  position,
                   const D3DXVECTOR3&  rotationDeg,
                   const D3DXVECTOR3&  scale,
                   const BonePalette&  palette,
                   int                 lod = 0);

    // Model_DrawSkinnedSubset 0x40CA40 (chemin skinné principal, un mesh/LOD) :
    // états de blend selon blendMode, bind shaders, palette d'os -> mKeyMatrix,
    // WVP/lumière, texture, SetStreamSource(76)/SetIndices, DrawIndexedPrimitive.
    void DrawSkinnedSubset(const SkinnedMesh& mesh, int lod,
                           const D3DXMATRIX&  world,
                           const BonePalette& palette);

    // BUG CORRIGÉ (audit 2026-07-14, cf. Gfx/WorldGeometryRenderer.cpp::Render()) :
    // DrawSkinnedSubset() évite les re-bind VS/PS redondants via `currentPass_`, un
    // cache PUREMENT LOCAL à cette instance. Si du code EXTÉRIEUR pose directement
    // IDirect3DDevice9::SetVertexShader(nullptr)/SetPixelShader(nullptr) sur le MÊME
    // device partagé (ex. Gfx/SkyRenderer.h, qui dessine un quad plein écran en
    // fixed-function AVANT chaque frame de géométrie), ce cache devient stale dès la
    // 2e frame : `currentPass_` reste à `kPass_SkinnedLit` alors que le device réel
    // n'a plus aucun VS/PS bindé -> DrawIndexedPrimitive() suivant tourne sans
    // shaders, silencieusement. À appeler après TOUT code externe qui manipule
    // SetVertexShader/SetPixelShader/SetFVF sur le device partagé, avant le
    // prochain DrawSkinnedSubset()/DrawModel().
    void InvalidateShaderBindingCache() { currentPass_ = 0; }

private:
    bool buildVertexDeclaration();
    bool compileSkinnedProgram();
    static IDirect3DTexture9* createDiffuse(IDirect3DDevice9* dev,
                                            const asset::SObjectTexture& tex);

    // États de mélange par matériau (Model_DrawSkinnedSubset : v14 = blendMode).
    void applyBlendMode(uint32_t blendMode);
    void resetBlendMode(uint32_t blendMode);

    IDirect3DDevice9*            dev_  = nullptr;
    IDirect3DVertexDeclaration9* decl_ = nullptr; // g_SkinVertexDecl (0x1945918)

    // Slot de shader skinné (reproduit GXD_ShaderSlot : VS/PS + ID3DXConstantTable + handles).
    IDirect3DVertexShader9* vs_    = nullptr;
    IDirect3DPixelShader9*  ps_    = nullptr;
    LPD3DXCONSTANTTABLE     ctVs_  = nullptr;
    LPD3DXCONSTANTTABLE     ctPs_  = nullptr;
    D3DXHANDLE hKeyMatrix_       = nullptr; // mKeyMatrix[kMaxBones]
    D3DXHANDLE hWorldViewProj_   = nullptr; // mWorldViewProjMatrix
    D3DXHANDLE hLightDirection_  = nullptr; // mLightDirection (espace objet)
    D3DXHANDLE hLightAmbient_    = nullptr; // mLightAmbient
    D3DXHANDLE hLightDiffuse_    = nullptr; // mLightDiffuse
    UINT       sampler0_         = 0;       // registre de mTexture0

    D3DXMATRIX  view_;
    D3DXMATRIX  proj_;
    D3DXMATRIX  viewProj_;
    D3DXVECTOR3 lightDirWorld_ = D3DXVECTOR3(-1.0f, -1.0f, 1.0f);
    D3DXVECTOR3 lightAmbient_  = D3DXVECTOR3(0.3f, 0.3f, 0.3f);
    D3DXVECTOR3 lightDiffuse_  = D3DXVECTOR3(0.7f, 0.7f, 0.7f);

    // g_CurrentShaderPass (0x194591C) : évite de re-binder VS/PS entre subsets identiques.
    int currentPass_ = 0;

    // Palette de secours (identité) si aucune palette valide n'est fournie.
    D3DXMATRIX identityPalette_[kMaxBones];
};

} // namespace ts2::gfx

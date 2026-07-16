// Gfx/MeshRenderer.h — rendu d'un mesh/modèle skinné du moteur GXD (Direct3D9).
//
// Réécriture FIDÈLE du chemin de rendu skinné GPU de TwelveSky2 :
//   Model_DrawSkinnedSubset 0x40CA40  (cœur : palette d'os -> constantes VS, DrawIndexedPrimitive)
//   Model_Render            0x40EBB0  (compose la matrice monde, boucle les meshes/LOD)
//   Mesh_ReadFromFile       0x40BC50  (sous-ensembles : VB 76 o, IB 6 o/face, INDEX16)
//   g_GxdVertexDecl         0x814A58  (déclaration D3DVERTEXELEMENT9 du vertex 76 o)
//   Shader_LoadVS03_SkinnedLit 0x409AB0 / Shader_LoadPS04_Tex 0x409CC0
//       (skinning GPU 4 influences via mKeyMatrix, cf. Docs/TS2_GXD_ENGINE.md §2.2)
//       ex-VeryOldClient: mAmbient2_VS (02.Ambient2.vs.fx / MakeShaderProgram03) +
//       mAmbient2_PS (02.Ambient2.ps.fx / MakeShaderProgram04) — Pass 2 skinnée, CONFIRMED §1.4.
//
// Ici on utilise le DirectX SDK June 2010 (d3dx9) exactement comme le binaire d'origine :
// D3DXCompileShader + ID3DXConstantTable (SetMatrixArray/SetMatrix/SetFloatArray),
// D3DXCreateTextureFromFileInMemoryEx, D3DXMatrix*.
//
// Le device physique est celui de ts2::gfx::Renderer (== Object B +524 dword_18C5104).
// ex-VeryOldClient: pDevice @+524 de TW2AddIn::GXD (v2), == g_GfxRenderer+604 (device partagé).
#pragma once
#include "Gfx/Renderer.h"
#include "Gfx/ShaderSet.h" // slots shaders reels du npk (VS03/PS04/VS15) — lecture seule (W3-F2)
#include "Asset/Model.h"
#include <d3dx9.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ts2::gfx {

// ---------------------------------------------------------------------------
//  Vertex skinné GPU — 76 octets, layout EXACT de la déclaration g_GxdVertexDecl
//  (relevé à 0x814A58 ; cf. Docs/TS2_GXD_ENGINE.md §4.3) :
//  ex-VeryOldClient: mVertexElementForSKIN2 -> mDECLForSKIN2 (stride 76, BLENDINDICES D3DCOLOR) —
//  concordance BIT-EXACTE IDA=VeryOld, CONFIRMED Docs/TS2_GXD_ROSETTA.md §1.5.
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

    // Données CPU retenues pour le volume d'ombre (Model_BuildShadowVolume 0x40DC70) :
    //   skinCpu  = SObjectSubset::skin        (32 o/sommet : pos12 + poids16 + boneIdx u32 ; mesh+700 v6[175])
    //   idxTopo  = SObjectSubset::indexCopy1  (6 o/face = 3× u16 : topologie triangles ; mesh+704 v6[176])
    //   idxAdj   = SObjectSubset::indexCopy2  (6 o/face = 3× u16 : adjacence par arête ; mesh+708 v6[177])
    // Retenus tels quels (octets) car BuildShadowVolume skinne sur CPU + parcourt la silhouette.
    std::vector<uint8_t> skinCpu; // 32 * vertexCount
    std::vector<uint8_t> idxTopo; // 6  * faceCount
    std::vector<uint8_t> idxAdj;  // 6  * faceCount
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

    // ----- Câblage sur les shaders RÉELS du npk (GXDEffect.npk) — additif (W3-F2) -------
    // Bascule le chemin skinné (passe 2) du HLSL reconstruit kSkinnedVS/kSkinnedPS vers les
    // vrais Shader03 (VS03_SkinnedLit 0x409AB0) + Shader04 (PS04_Tex 0x409CC0) chargés par
    // ShaderSet.cpp depuis ./GXDEFFECT/GXDEffect.npk. VS15 (0x40ACB0) sert aussi au volume
    // d'ombre stencil (DrawModelShadow). `shaders` n'est PAS possédé (durée de vie côté appelant).
    // Additif : si jamais appelé, DrawSkinnedSubset retombe sur le HLSL reconstruit (fallback).
    void AttachShaderSet(const ShaderSet* shaders);

    // ----- Paramètres d'ombre runtime (Model_RenderWithShadow 0x40EEE0) — additif ------
    //   enabled  = g_ShadowsEnabled 0x18C4F14
    //   method   = g_ShadowMethod   0x18C4F18 (0 = z-fail Carmack ; 1 = stencil two-sided)
    //   fogNear/fogFar = flt_18C4F08 / flt_18C4F0C (seuil volume↔planaire + fondu du volume)
    //   lightDir = flt_18C53C0/C4/C8 (direction lumière d'ombre, négée au calcul)
    //
    // CORRECTION (Passe 4 / W5, front shadow-wiring) : `enabled`/`method` ne sont PAS des options
    // de menu — ce sont des CONSTANTES GELÉES. GXD_InitGlobalState 0x401320 est leur seul writer
    // (vérifié par xrefs) et pose g_ShadowsEnabled=1 (0x4013B2) / g_ShadowMethod=1 (0x4013B8) ;
    // rien d'autre ne les écrit jamais. De plus l'unique LECTEUR de g_ShadowsEnabled (0x40EEEC)
    // vit dans Model_RenderWithShadow 0x40EEE0 — fonction INATTEIGNABLE -> le global est inerte.
    //   PRÉCISION (Passe 4 / W5b) : « fonction morte » était imprécis — 0x40EEE0 a bien 3 CALL
    //   SITES (tous dans SObject_DrawAnimated 0x4D9050). Elle est morte par INATTEIGNABILITÉ :
    //   la closure d'appelants se ferme à la profondeur 2 sur 3 racines orphelines
    //   (Char_DrawWeaponEffectVariantA 0x568FE0 / Npc_DrawMeshShadow 0x5800E0 /
    //   Char_DrawShadow 0x580CE0, 0 xref chacune) et `reaches(WinMain 0x4609C0 -> 0x40EEE0)` = false.
    // Corollaire : `method==0` (z-fail Carmack, 0x40F671) est INATTEIGNABLE PAR VALEUR (le seul
    // writer pose 1). Cette fonction documente le binaire, elle ne le pilote pas : DrawModelShadow()
    // reste volontairement sans appelant côté C++.
    // Ne PAS la câbler à l'option UI g_Opt_GfxDetailShadows 0x84DEF8 : cette option existe bien
    // (UI_OptionsWnd_OnClick 0x66D140) mais elle ne bascule aucune ombre malgré son nom.
    //   PRÉCISION (Passe 4 / W5b) — le global a 16 xrefs, pas un lecteur unique :
    //     * 4 WRITERS, tous dans UI_OptionsWnd_OnClick 0x66D140 : @0x66DF5B (`sub ecx,1` -> store),
    //       @0x66DF63 (clamp = 0), @0x66DFDB (`add ecx,1` -> store), @0x66DFEA (clamp = 1)
    //       -> option clampée à [0,1] (bascule 2 états), persistée par Options_SaveBin 0x4C2280.
    //     * 12 LECTEURS : 0x51B54F, 0x51B8C3, 0x51B8CD, 0x55B351, 0x580840, 0x580E3A, 0x5811EA,
    //       0x581990, 0x66DF52, 0x66DFD2, 0x66DFE1, 0x66F43E.
    //   0x5811EA est L'UN de ces 12 lecteurs, et il vit À L'INTÉRIEUR du chemin d'ombre VIVANT
    //   (Char_DrawReflection 0x581090, appelée par Scene_InGameRender @0x52DB09, dans le bracket
    //   d'ombre 0x52D9DC..0x52DB15) : il y garde le choix de branche, mais ce qu'il sélectionne
    //   est une VARIANTE DE MODÈLE selon les PV (`unk_FC7A8C + 144*type + 36*(3 - hp%/30)`)
    //   — ce n'est pas une bascule d'ombre.
    void SetShadowParams(bool enabled, int method, float fogNear, float fogFar,
                         const D3DXVECTOR3& lightDir);

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

    // Model_RenderWithShadow 0x40EEE0 : rendu de l'ombre d'un modèle skinné (additif, W3-F2).
    //   PROCHE (dist caméra <= fogNear) -> VOLUME D'OMBRE STENCIL (passe 8 = VS15 + PS NULL) :
    //     Model_BuildShadowVolume 0x40DC70 (silhouette extrudée, FVF XYZ), z-fail si method==0.
    //   LOIN  (dist > fogNear) -> ombre planaire projetée (Model_RenderPlanarShadow 0x40F720).
    //   Le sens du test est confirmé par Hex-Rays : `if (flt_18C4F08 >= dist)` (0x40EFC6) -> branche
    //   VOLUME ; `else` (0x40EFDE) -> branche PLANAIRE appelant 0x40F720 @0x40F1A9.
    //
    // FONCTION JAMAIS APPELÉE — ET DOUBLEMENT INERTE (Passe 4 / W5, front shadow-wiring) :
    //   (a) Toute la chaîne 0x40EEE0/0x40DC70 est du CODE MORT dans le binaire (0 xref sur les
    //       3 têtes ; 0 occurrence des adresses en LE dans l'image -> pas d'appel indirect).
    //       Détail complet + preuves dans MeshRenderer.cpp au-dessus de DrawModelShadow().
    //   (b) La branche « LOIN -> planaire » est en plus MORTE PAR VALEUR : fogNear = flt_18C4F08
    //       = 999999.0 (figé par GXD_InitGlobalState @0x40137E, seul writer), donc `dist > fogNear`
    //       n'arrive jamais pour une caméra de jeu -> seule la branche VOLUME serait prise si la
    //       fonction vivait. Ne PAS confondre ce planaire-là (mort) avec la VRAIE ombre planaire
    //       vivante du jeu, qui atteint 0x40F720 par l'autre chaîne (SObject_DrawAnimated2 0x4D91C0)
    //       et utilise la PASSE 5 = VS09 (g_GxdSh09_VS), et non VS15/passe 8.
    //   Le `if (!shaderSet_) return` ci-dessous n'est donc PAS le verrou qui empêche l'ombre :
    //   attacher un ShaderSet ne « débloque » rien, il n'y a aucun appelant à débloquer.
    //   `boundRadius` = a2 (diamètre englobant).
    //   Passe 4 / W5b (front shadow-fidelity) : la longueur d'extrusion a9 en est DÉRIVÉE, et
    //   n'est pas un paramètre libre — a9 = a2 × 2.5, prouvé aux 3 (et seuls) call sites de
    //   0x40EEE0 dans SObject_DrawAnimated 0x4D9050 (@0x4D90C6/@0x4D9129/@0x4D9178 : `a5 * 2.5`).
    //   DrawModelShadow applique donc `boundRadius * 2.5f` en interne : NE PAS le pré-multiplier
    //   côté appelant si cette fonction venait un jour à être câblée.
    void DrawModelShadow(const SkinnedModel& model,
                         const D3DXVECTOR3&  position,
                         const D3DXVECTOR3&  rotationDeg,
                         const D3DXVECTOR3&  scale,
                         const BonePalette&  palette,
                         float               boundRadius);

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

    // Skinning CPU + silhouette extrudée d'un LOD (Model_BuildShadowVolume 0x40DC70).
    // Remplit shadowVol_ (XYZ interleavé, stride 12) + shadowVolVertCount_ ; renvoie false si
    // données CPU absentes, LOD > 10000 vtx/faces, ou débordement (>29976 sommets émis).
    bool buildShadowVolume(const SkinnedLod&  lod,
                           const BonePalette& palette,
                           const D3DXVECTOR3& lightDirObj,
                           float              extrude);

    IDirect3DDevice9*            dev_  = nullptr;
    IDirect3DVertexDeclaration9* decl_ = nullptr; // g_SkinVertexDecl (0x1945918) — ex-VeryOldClient: mDECLForSKIN2 (cible=global de fichier, PAS membre)

    // Slot de shader skinné (reproduit GXD_ShaderSlot : VS/PS + ID3DXConstantTable + handles).
    // ex-VeryOldClient: mAmbient2_VS_Shader / mAmbient2_PS_Shader (+ _ConstantTable), Shader03/04.
    IDirect3DVertexShader9* vs_    = nullptr; // g_GxdSh03_VS 0x1945970 — ex-VeryOldClient: mAmbient2_VS_Shader
    IDirect3DPixelShader9*  ps_    = nullptr; // g_GxdSh04_PS 0x194598C — ex-VeryOldClient: mAmbient2_PS_Shader
    LPD3DXCONSTANTTABLE     ctVs_  = nullptr; // g_GxdSh03VS_CT 0x194596C — ex-VeryOldClient: mAmbient2_VS_ConstantTable
    LPD3DXCONSTANTTABLE     ctPs_  = nullptr; // g_GxdSh04PS_CT 0x1945988 — ex-VeryOldClient: mAmbient2_PS_ConstantTable
    D3DXHANDLE hKeyMatrix_       = nullptr; // mKeyMatrix[kMaxBones] — g_GxdSh03_hKeyMatrix 0x1945974 — ex-VeryOldClient: mAmbient2_VS_KeyMatrix
    D3DXHANDLE hWorldViewProj_   = nullptr; // mWorldViewProjMatrix — ex-VeryOldClient: mAmbient2_VS_WorldViewProjMatrix
    D3DXHANDLE hLightDirection_  = nullptr; // mLightDirection (espace objet) — ex-VeryOldClient: mAmbient2_VS_LightDirection
    D3DXHANDLE hLightAmbient_    = nullptr; // mLightAmbient — ex-VeryOldClient: mAmbient2_VS_LightAmbient
    D3DXHANDLE hLightDiffuse_    = nullptr; // mLightDiffuse — ex-VeryOldClient: mAmbient2_VS_LightDiffuse
    UINT       sampler0_         = 0;       // registre de mTexture0 — ex-VeryOldClient: mAmbient2_PS_Texture0

    D3DXMATRIX  view_;
    D3DXMATRIX  proj_;
    D3DXMATRIX  viewProj_;
    // Position caméra monde (dérivée de l'inverse de view_) — g_CameraEye dword_18C51C0/C4/C8,
    // utilisée par DrawModelShadow pour choisir volume vs ombre planaire (0x40ef8e).
    D3DXVECTOR3 eye_ = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    // ex-VeryOldClient: mLight (v2 / Object B @+1120) — Amb 0.3 / Diff 0.7 / Dir (-1,-1,1).
    // PLAUSIBLE (P-9) : floats corroborés par v2 ; prouvés bit-exact IDA au chunk device (0x402711).
    // Discriminant v1/v2 : bien 0.3/0.7 (v2), PAS 0.4/0.5 (v1).
    D3DXVECTOR3 lightDirWorld_ = D3DXVECTOR3(-1.0f, -1.0f, 1.0f);
    D3DXVECTOR3 lightAmbient_  = D3DXVECTOR3(0.3f, 0.3f, 0.3f);
    D3DXVECTOR3 lightDiffuse_  = D3DXVECTOR3(0.7f, 0.7f, 0.7f);

    // g_CurrentShaderPass (0x194591C) : évite de re-binder VS/PS entre subsets identiques.
    // ex-VeryOldClient: mPresentShaderProgramNumber (PLAUSIBLE, P-12 — offset 0x194591C non nommé
    //   formellement dans gxd_findings ; à ne pas confondre avec currentPassId Object B+526884).
    int currentPass_ = 0;

    // Palette de secours (identité) si aucune palette valide n'est fournie.
    D3DXMATRIX identityPalette_[kMaxBones];

    // ----- Câblage shaders réels npk (AttachShaderSet) — additif W3-F2 ------------------
    // Slots réels chargés du npk (Shader03/04/15). Non possédé. Si nullptr -> fallback HLSL.
    const ShaderSet* shaderSet_ = nullptr;
    // Vraie borne du tableau mKeyMatrix[] déclarée par Shader03.fx (D3DXCONSTANT_DESC.Elements) ;
    // Model_DrawSkinnedSubset ne clampe PAS côté client (0x40d4e8) -> c'est le shader qui borne.
    UINT boneArraySize_ = kMaxBones;

    // ----- État d'ombre runtime (Model_RenderWithShadow 0x40EEE0) — additif -------------
    // CORRECTION DE FIDÉLITÉ (Passe 4 / W5b, front shadow-fidelity) : ces défauts
    // CONTREDISAIENT les ancres qu'ils citaient. Ils portent désormais les valeurs du binaire,
    // posées par GXD_InitGlobalState 0x401320 (seul writer, vérifié par xrefs).
    // NB : le `1` de enabled/method n'est PAS un immédiat — il transite par ebx
    //      (`mov ebx, 1` @0x401365, puis `mov ds:g_XXX, ebx`).
    bool        shadowsEnabled_ = true;      // g_ShadowsEnabled 0x18C4F14 = 1 (ebx @0x401365 ; store @0x4013B2)
    int         shadowMethod_   = 1;         // g_ShadowMethod   0x18C4F18 = 1 (ebx @0x401365 ; store @0x4013B8)
                                             //   -> cohérent avec « method==0 INATTEIGNABLE PAR VALEUR » ci-dessus.
    float       fogNear_        = 999999.0f;  // flt_18C4F08 <- flt_7EDBD8 (0x497423F0) @0x401378/0x40137E
    float       fogFar_         = 1000000.0f; // flt_18C4F0C <- flt_7EDB80 (0x49742400) @0x40138A/0x401396
    // ATTENTION — valeur DÉRIVÉE, PAS une constante lue du binaire (le commentaire précédent
    // « flt_18C53C0/C4/C8 » laissait croire l'inverse, et donnait (0,-1,0) que cette ancre ne
    // produit PAS). flt_18C53C0/C4/C8 est un CACHE recalculé à chaque frame par
    // GXD_SetupStencilShadowState 0x404F20 @0x404F26..0x404F62 depuis la lumière monde ;
    // aucun writer n'apparaît en xref absolue car l'écriture est esi-relative (esi = this =
    // g_GxdRenderer 0x18C4EF8 -> esi+4C8h = 0x18C53C0). Dérivation (cf. Scene/WorldRenderer.h) :
    //     shadowLightDir = normalize( normalize(L.x, 0, L.z) puis .y := -1 )
    // Appliquée à lightDirWorld_ = (-1,-1,1) (cf. ci-dessus) -> (-0.5, -1/sqrt(2), 0.5).
    // Propriété structurelle : la 1re normalisation rendant l'horizontale unitaire, la norme
    // avant la 2nde vaut TOUJOURS sqrt(2) -> y ≡ -1/sqrt(2) : direction à 45° vers le bas,
    // quelle que soit la position du soleil. (0,-1,0) n'en serait le résultat que si
    // L.x == L.z == 0 (D3DXVec3Normalize du vecteur nul) — cas NON prouvé.
    D3DXVECTOR3 shadowLightDir_ = D3DXVECTOR3(-0.5f, -0.70710678f, 0.5f);

    // Tampons scratch du volume d'ombre (globaux d'origine -> membres, mêmes plafonds) :
    //   worldPos_        = positions skinnées monde, stride 3 (flt_18C69D4)
    //   faceLightFacing_ = 1 octet/face : face éclairée ? (g_FaceLightFacing 0x18E3E94)
    //   shadowVol_       = sommets d'ombre XYZ interleavés, stride 3 (g_ShadowVolumeX 0x18EDAD8)
    //   shadowVolVertCount_ = nombre de sommets émis (g_ShadowVolumeVertCount 0x18EDAD4)
    std::vector<float>   worldPos_;
    std::vector<uint8_t> faceLightFacing_;
    std::vector<float>   shadowVol_;
    UINT                 shadowVolVertCount_ = 0;
};

} // namespace ts2::gfx

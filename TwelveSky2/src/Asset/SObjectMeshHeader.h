// Asset/SObjectMeshHeader.h — décodeur TYPÉ de l'en-tête d'un mesh .SOBJECT.
//
// PORTÉE : purement ADDITIF et LECTURE SEULE. `Asset/Model.cpp` garde le blob
// opaque de 372 o dans `SObjectMesh::header` (mesh+4..+376) ; ce décodeur EXPOSE,
// sans le modifier, les champs typés que le binaire décode réellement. Aucune
// régression sur le walker : on ne fait que relire `header`.
//
// ============================ ANCRES IDA (seule vérité) ======================
//   Mesh_ReadFromMemory       0x40C380  — frontières du header :
//       qmemcpy(a1+1 , src   , 0x44)  @0x40C3C9  -> 68 o  (block56 56 + block12 12) -> mesh+4
//       qmemcpy(a1+18, src+68, 0x130) @0x40C3EC  -> 304 o (block304 = 4×GpuSkinVertex) -> mesh+72
//       subsetCount = *(src+372)      @0x40C3EE  -> mesh+680 ; curseur += 376 @0x40C400
//   cMesh_SaveToFileWithLOD   0x43AC10  — WRITER = vérité de la sémantique de chaque champ :
//       block56 (0x38) écrit @0x43ADAA ; block12 (0x0C) @0x43AE19 ; 4× record 76 o @0x43AE1F..0x43AF7D
//       block12[i] = bboxMax[i]-bboxMin[i]  @0x43ADD4/0x43ADF0/0x43AE03
//   Model_DrawSkinnedSubset   0x40CA40  — usage des flags à l'exécution :
//       animColorFlag mesh+12 (==1 ⇒ interpolation couleur/échelle)
//       billboardFlag mesh+52 : cmp [ebx+34h],esi @0x40CDC6
//       billboardAxisMode mesh+56 : cmp [ebx+38h],esi @0x40CDD2
//       bboxExtents[0] mesh+60 réutilisé comme demi-largeur : fld [ebx+3Ch] @0x40CDCF
//   g_GxdSkinnedVertexDecl76  0x814A58  — déclaration D3DVERTEXELEMENT9 du GpuSkinVertex 76 o.
//
// Rappel : `header[k]` (index dans le blob 372 o) == mesh+(k+4)  (le blob démarre à mesh+4).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ts2::asset {

struct SObjectMesh; // Asset/Model.h — surcharge de commodité Decode(const SObjectMesh&)

// ---------------------------------------------------------------------------
//  GpuSkinVertex — sommet skinné GPU, 76 o. Déclaration EXACTE relevée dans
//  g_GxdSkinnedVertexDecl76 0x814A58 (tableau de D3DVERTEXELEMENT9) :
//    {0,  0, FLOAT3(2),   POSITION(0)}     -> +0
//    {0, 12, FLOAT4(3),   BLENDWEIGHT(1)}  -> +12  (w0,w1,w2,w3)
//    {0, 28, D3DCOLOR(4), BLENDINDICES(2)} -> +28  (4 os empaquetés, D3DCOLORtoUBYTE4 au VS)
//    {0, 32, FLOAT3(2),   TANGENT(6)}      -> +32
//    {0, 44, FLOAT3(2),   BINORMAL(7)}     -> +44
//    {0, 56, FLOAT3(2),   NORMAL(3)}       -> +56
//    {0, 68, FLOAT2(1),   TEXCOORD(5)}     -> +68
//    D3DDECL_END()
//  Corroboré par D3DXComputeTangentFrameEx(...,TEXCOORD=5,TANGENT=6,BINORMAL=7,NORMAL=3,...) @0x43B415.
//  NB : ce même layout décrit les sommets de VB des subsets ET les 4 points d'attache
//  (block304). Pour ces derniers, tangente/binormale/normale sont écrites à ZÉRO
//  (writer @0x43AEE3..0x43AF36 : v173..v181 = 0.0).
// ---------------------------------------------------------------------------
struct GpuSkinVertex {
    float    position[3];    // +0   POSITION    FLOAT3
    float    blendWeight[4]; // +12  BLENDWEIGHT FLOAT4 (w0,w1,w2 lus ; w3 = 1-w0-w1-w2 @0x43AEC7)
    uint32_t blendIndices;   // +28  BLENDINDICES D3DCOLOR : 4 os empaquetés (octet i = os i, @0x43AED4)
    float    tangent[3];     // +32  TANGENT     FLOAT3 (0 pour les points d'attache)
    float    binormal[3];    // +44  BINORMAL    FLOAT3 (0 pour les points d'attache)
    float    normal[3];      // +56  NORMAL      FLOAT3 (0 pour les points d'attache)
    float    texcoord[2];    // +68  TEXCOORD0   FLOAT2
};

static_assert(sizeof(GpuSkinVertex) == 76, "GpuSkinVertex doit faire 76 o (g_GxdSkinnedVertexDecl76 0x814A58)");
static_assert(offsetof(GpuSkinVertex, position)    == 0,  "POSITION @+0");
static_assert(offsetof(GpuSkinVertex, blendWeight) == 12, "BLENDWEIGHT @+12");
static_assert(offsetof(GpuSkinVertex, blendIndices)== 28, "BLENDINDICES @+28");
static_assert(offsetof(GpuSkinVertex, tangent)     == 32, "TANGENT @+32");
static_assert(offsetof(GpuSkinVertex, binormal)    == 44, "BINORMAL @+44");
static_assert(offsetof(GpuSkinVertex, normal)      == 56, "NORMAL @+56");
static_assert(offsetof(GpuSkinVertex, texcoord)    == 68, "TEXCOORD @+68");

// Octet d'os `i` (0..3) empaqueté dans blendIndices. Le WRITER empaquette
// octet0=os0 ... octet3=os3 (@0x43AED4 : LOBYTE/BYTE1/BYTE2/HIBYTE). Le VS les
// déballe via D3DCOLORtoUBYTE4. Faithful : octet i = (v >> 8*i) & 0xFF.
inline uint8_t GpuSkinBoneIndex(const GpuSkinVertex& v, size_t i) {
    return static_cast<uint8_t>((v.blendIndices >> (8u * static_cast<unsigned>(i))) & 0xFFu);
}

// ---------------------------------------------------------------------------
//  SObjectMeshHeader — vue typée du blob 372 o `SObjectMesh::header`.
//
//  Carte du blob (offset header == mesh - 4) :
//    header[0..8)    (mesh+4..+12)   block56 tête (groupe A) — RAW, semi-prouvé :
//                       mesh+4 = flag (writer @0x43AC78 : *(a2+272)&&*(a2+480)>0)
//                       mesh+8 = ftol(100.0) ou 0 (writer @0x43AC86)
//    header[8]       (mesh+12)       animColorFlag  (u32 ; ==1 ⇒ interpolation, 0x40CA40)
//    header[12..48)  (mesh+16..+52)  courbes glow/pulse — RAW (9× ftol(100.*..) writer @0x43AC9B..0x43AD3E ; TODO runtime)
//    header[48]      (mesh+52)       billboardFlag       (u32 ; ==1 ⇒ quad écran, 0x40CDC6)
//    header[52]      (mesh+56)       billboardAxisMode   (u32 ; ==1 ⇒ carré symétrique, 0x40CDD2)
//    header[56..68)  (mesh+60..+72)  bboxExtents[3] float = bboxMax-bboxMin (writer @0x43ADD4)
//    header[68..372) (mesh+72..+376) attachPoints[4] = 4× GpuSkinVertex 76 o (block304)
// ---------------------------------------------------------------------------
class SObjectMeshHeader {
public:
    static constexpr size_t kHeaderSize        = 372; // == SObjectMesh::kHeaderSize (mesh+4..+376)
    static constexpr size_t kAttachPointCount  = 4;   // writer : boucle 4× @0x43AE1F..0x43AF7D
    static constexpr size_t kGpuSkinVertexStride = 76;
    static constexpr size_t kGlowRawSize       = 36;  // header[12..48) = mesh+16..+52 (9 dwords)
    static constexpr size_t kBlockAHeadSize    = 8;   // header[0..8)   = mesh+4..+12  (2 dwords)

    SObjectMeshHeader() = default;

    // Décode depuis le blob 372 o. Renvoie false (et laisse decoded()==false) si
    // la taille n'est pas 372. Ne lève JAMAIS : décodeur défensif.
    bool Decode(const uint8_t* header, size_t size);
    bool Decode(const std::vector<uint8_t>& header) { return Decode(header.data(), header.size()); }

    // Surcharge de commodité : lit `mesh.header` (branche sans casser Model.*).
    bool Decode(const SObjectMesh& mesh);

    bool decoded() const { return decoded_; }

    // --- Flags d'animation / billboard ---
    uint32_t animColorFlag()     const { return animColorFlag_; }        // mesh+12
    uint32_t billboardFlag()     const { return billboardFlag_; }        // mesh+52
    bool     isBillboard()       const { return billboardFlag_ == 1; }   // 0x40CDC6 (cmp ..,esi ; ==1)
    uint32_t billboardAxisMode() const { return billboardAxisMode_; }    // mesh+56 (==1 ⇒ axe symétrique)

    // --- Bounding-box (extents = max - min) mesh+60/+64/+68 (writer @0x43ADD4) ---
    const float* bboxExtents() const { return bboxExtents_; }
    float bboxExtentX() const { return bboxExtents_[0]; } // mesh+60 (aussi demi-largeur billboard @0x40CDCF)
    float bboxExtentY() const { return bboxExtents_[1]; } // mesh+64 (aussi demi-hauteur billboard @0x40CDFF)
    float bboxExtentZ() const { return bboxExtents_[2]; } // mesh+68 (dz ; non lu par les chemins de dessin — TODO runtime)

    // --- 4 points d'attache (block304) ---
    const GpuSkinVertex& attachPoint(size_t i) const { return attach_[i < kAttachPointCount ? i : 0]; }
    const GpuSkinVertex* attachPoints() const { return attach_; }

    // --- Régions non prouvées, conservées BRUTES (aucune invention) ---
    const uint8_t* glowAnimRaw()  const { return glowRaw_; }    // 36 o (mesh+16..+52)
    const uint8_t* blockAHeadRaw() const { return blockAHead_; } // 8 o  (mesh+4..+12)

private:
    bool         decoded_          = false;
    uint32_t     animColorFlag_    = 0;
    uint32_t     billboardFlag_    = 0;
    uint32_t     billboardAxisMode_= 0;
    float        bboxExtents_[3]   = {0.0f, 0.0f, 0.0f};
    GpuSkinVertex attach_[kAttachPointCount] = {};
    uint8_t      glowRaw_[kGlowRawSize]      = {};
    uint8_t      blockAHead_[kBlockAHeadSize]= {};
};

} // namespace ts2::asset

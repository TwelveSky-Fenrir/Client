// Asset/SObjectMeshHeader.h — TYPED decoder for a .SOBJECT mesh header.
//
// SCOPE: purely ADDITIVE and READ-ONLY. `Asset/Model.cpp` keeps the opaque
// 372-byte blob in `SObjectMesh::header` (mesh+4..+376); this decoder EXPOSES,
// without modifying it, the typed fields the binary actually decodes. No
// regression on the walker: we only re-read `header`.
//
// ============================ IDA ANCHORS (sole source of truth) ======================
//   Mesh_ReadFromMemory       0x40C380  — header boundaries:
//       qmemcpy(a1+1 , src   , 0x44)  @0x40C3C9  -> 68 bytes (block56 56 + block12 12) -> mesh+4
//       qmemcpy(a1+18, src+68, 0x130) @0x40C3EC  -> 304 bytes (block304 = 4×GpuSkinVertex) -> mesh+72
//       subsetCount = *(src+372)      @0x40C3EE  -> mesh+680 ; cursor += 376 @0x40C400
//   cMesh_SaveToFileWithLOD   0x43AC10  — WRITER = source of truth for each field's semantics:
//       block56 (0x38) written @0x43ADAA ; block12 (0x0C) @0x43AE19 ; 4× 76-byte record @0x43AE1F..0x43AF7D
//       block12[i] = bboxMax[i]-bboxMin[i]  @0x43ADD4/0x43ADF0/0x43AE03
//   Model_DrawSkinnedSubset   0x40CA40  — runtime usage of the flags:
//       animColorFlag mesh+12 (==1 => color/scale interpolation)
//       billboardFlag mesh+52 : cmp [ebx+34h],esi @0x40CDC6
//       billboardAxisMode mesh+56 : cmp [ebx+38h],esi @0x40CDD2
//       bboxExtents[0] mesh+60 reused as half-width: fld [ebx+3Ch] @0x40CDCF
//   g_GxdSkinnedVertexDecl76  0x814A58  — D3DVERTEXELEMENT9 declaration of the 76-byte GpuSkinVertex.
//
// Reminder: `header[k]` (index into the 372-byte blob) == mesh+(k+4)  (the blob starts at mesh+4).
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace ts2::asset {

struct SObjectMesh; // Asset/Model.h — convenience overload Decode(const SObjectMesh&)

// ---------------------------------------------------------------------------
//  GpuSkinVertex — 76-byte GPU-skinned vertex. EXACT declaration read from
//  g_GxdSkinnedVertexDecl76 0x814A58 (D3DVERTEXELEMENT9 array):
//    {0,  0, FLOAT3(2),   POSITION(0)}     -> +0
//    {0, 12, FLOAT4(3),   BLENDWEIGHT(1)}  -> +12  (w0,w1,w2,w3)
//    {0, 28, D3DCOLOR(4), BLENDINDICES(2)} -> +28  (4 packed bones, D3DCOLORtoUBYTE4 in the VS)
//    {0, 32, FLOAT3(2),   TANGENT(6)}      -> +32
//    {0, 44, FLOAT3(2),   BINORMAL(7)}     -> +44
//    {0, 56, FLOAT3(2),   NORMAL(3)}       -> +56
//    {0, 68, FLOAT2(1),   TEXCOORD(5)}     -> +68
//    D3DDECL_END()
//  Corroborated by D3DXComputeTangentFrameEx(...,TEXCOORD=5,TANGENT=6,BINORMAL=7,NORMAL=3,...) @0x43B415.
//  NOTE: this same layout describes both the subset VB vertices AND the 4 attach
//  points (block304). For the latter, tangent/binormal/normal are written as ZERO
//  (writer @0x43AEE3..0x43AF36: v173..v181 = 0.0).
// ---------------------------------------------------------------------------
struct GpuSkinVertex {
    float    position[3];    // +0   POSITION    FLOAT3
    float    blendWeight[4]; // +12  BLENDWEIGHT FLOAT4 (w0,w1,w2 read ; w3 = 1-w0-w1-w2 @0x43AEC7)
    uint32_t blendIndices;   // +28  BLENDINDICES D3DCOLOR: 4 packed bones (byte i = bone i, @0x43AED4)
    float    tangent[3];     // +32  TANGENT     FLOAT3 (0 for attach points)
    float    binormal[3];    // +44  BINORMAL    FLOAT3 (0 for attach points)
    float    normal[3];      // +56  NORMAL      FLOAT3 (0 for attach points)
    float    texcoord[2];    // +68  TEXCOORD0   FLOAT2
};

static_assert(sizeof(GpuSkinVertex) == 76, "GpuSkinVertex must be 76 bytes (g_GxdSkinnedVertexDecl76 0x814A58)");
static_assert(offsetof(GpuSkinVertex, position)    == 0,  "POSITION @+0");
static_assert(offsetof(GpuSkinVertex, blendWeight) == 12, "BLENDWEIGHT @+12");
static_assert(offsetof(GpuSkinVertex, blendIndices)== 28, "BLENDINDICES @+28");
static_assert(offsetof(GpuSkinVertex, tangent)     == 32, "TANGENT @+32");
static_assert(offsetof(GpuSkinVertex, binormal)    == 44, "BINORMAL @+44");
static_assert(offsetof(GpuSkinVertex, normal)      == 56, "NORMAL @+56");
static_assert(offsetof(GpuSkinVertex, texcoord)    == 68, "TEXCOORD @+68");

// Bone byte `i` (0..3) packed into blendIndices. The WRITER packs
// byte0=bone0 ... byte3=bone3 (@0x43AED4: LOBYTE/BYTE1/BYTE2/HIBYTE). The VS
// unpacks them via D3DCOLORtoUBYTE4. Faithful: byte i = (v >> 8*i) & 0xFF.
inline uint8_t GpuSkinBoneIndex(const GpuSkinVertex& v, size_t i) {
    return static_cast<uint8_t>((v.blendIndices >> (8u * static_cast<unsigned>(i))) & 0xFFu);
}

// ---------------------------------------------------------------------------
//  SObjectMeshHeader — typed view of the 372-byte `SObjectMesh::header` blob.
//
//  Blob map (offset header == mesh - 4):
//    header[0..8)    (mesh+4..+12)   block56 head (group A) — RAW, partly proven:
//                       mesh+4 = flag (writer @0x43AC78 : *(a2+272)&&*(a2+480)>0)
//                       mesh+8 = ftol(100.0) or 0 (writer @0x43AC86)
//    header[8]       (mesh+12)       animColorFlag  (u32 ; ==1 => interpolation, 0x40CA40)
//    header[12..48)  (mesh+16..+52)  glow/pulse curves — RAW (9× ftol(100.*..) writer @0x43AC9B..0x43AD3E ; runtime TODO)
//    header[48]      (mesh+52)       billboardFlag       (u32 ; ==1 => screen quad, 0x40CDC6)
//    header[52]      (mesh+56)       billboardAxisMode   (u32 ; ==1 => symmetric square, 0x40CDD2)
//    header[56..68)  (mesh+60..+72)  bboxExtents[3] float = bboxMax-bboxMin (writer @0x43ADD4)
//    header[68..372) (mesh+72..+376) attachPoints[4] = 4× GpuSkinVertex 76 bytes (block304)
// ---------------------------------------------------------------------------
class SObjectMeshHeader {
public:
    static constexpr size_t kHeaderSize        = 372; // == SObjectMesh::kHeaderSize (mesh+4..+376)
    static constexpr size_t kAttachPointCount  = 4;   // writer: 4× loop @0x43AE1F..0x43AF7D
    static constexpr size_t kGpuSkinVertexStride = 76;
    static constexpr size_t kGlowRawSize       = 36;  // header[12..48) = mesh+16..+52 (9 dwords)
    static constexpr size_t kBlockAHeadSize    = 8;   // header[0..8)   = mesh+4..+12  (2 dwords)

    SObjectMeshHeader() = default;

    // Decodes from the 372-byte blob. Returns false (and leaves decoded()==false) if
    // the size is not 372. NEVER throws: defensive decoder.
    bool Decode(const uint8_t* header, size_t size);
    bool Decode(const std::vector<uint8_t>& header) { return Decode(header.data(), header.size()); }

    // Convenience overload: reads `mesh.header` (branch without touching Model.*).
    bool Decode(const SObjectMesh& mesh);

    bool decoded() const { return decoded_; }

    // --- Animation / billboard flags ---
    uint32_t animColorFlag()     const { return animColorFlag_; }        // mesh+12
    uint32_t billboardFlag()     const { return billboardFlag_; }        // mesh+52
    bool     isBillboard()       const { return billboardFlag_ == 1; }   // 0x40CDC6 (cmp ..,esi ; ==1)
    uint32_t billboardAxisMode() const { return billboardAxisMode_; }    // mesh+56 (==1 => symmetric axis)

    // --- Bounding-box (extents = max - min) mesh+60/+64/+68 (writer @0x43ADD4) ---
    const float* bboxExtents() const { return bboxExtents_; }
    float bboxExtentX() const { return bboxExtents_[0]; } // mesh+60 (also billboard half-width @0x40CDCF)
    float bboxExtentY() const { return bboxExtents_[1]; } // mesh+64 (also billboard half-height @0x40CDFF)
    float bboxExtentZ() const { return bboxExtents_[2]; } // mesh+68 (dz ; not read by the draw paths — runtime TODO)

    // --- 4 attach points (block304) ---
    const GpuSkinVertex& attachPoint(size_t i) const { return attach_[i < kAttachPointCount ? i : 0]; }
    const GpuSkinVertex* attachPoints() const { return attach_; }

    // --- Unproven regions, kept RAW (no invention) ---
    const uint8_t* glowAnimRaw()  const { return glowRaw_; }    // 36 bytes (mesh+16..+52)
    const uint8_t* blockAHeadRaw() const { return blockAHead_; } // 8 bytes  (mesh+4..+12)

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

// Asset/Mobject2.h — reader for the .MOBJECT2 container (array of effect meshes).
//
//   .MOBJECT2 -> struct Mobject2  (container: magic "MOBJECT2" + array of 268-byte meshes)
//
// Format DISTINCT from .MOBJECT (408-byte parts, cf. Asset/Model.h MObject): 268-byte mesh,
// FVF 258 vertex = D3DFVF_XYZ|TEX1 (20 bytes, NO normal), textures in SOBJECT format 56 bytes
// (Tex_ReadPacked 0x417740 — same struct as asset::SObjectTexture, reused as-is).
//
// Liveness (xrefs_to Mesh_LoadMOBJECT2): Model_LoadWO2_A/B (DEAD CODE, 0 xrefs) + Emitter_ReadFile
// 0x424D30 <- Effect_ReadStream 0x42A990 <- Effect_LoadFile 0x42A920: .MOBJECT2 is LIVE
// ONLY via the effect/emitter system (particle meshes). This is why this parser
// exposes Parse(buffer, offset): the container is most often EMBEDDED in an effect
// stream, not a standalone file. PARSER ONLY — no rendering, no GPU upload (cf. Docs/TS2_DEEP_MOBJECT.md §T5).
//
// Original loaders (IDA ground truth, imagebase 0x400000, READ-ONLY as of 2026-07-17):
//   Container : Mesh_LoadMOBJECT2 0x4318C0  (magic + attribute + meshCount + array)
//   Mesh      : Mesh_ReadFile     0x430470  (header 76 + N + tables 40*N/4*N + header 80 +
//                                            subsets [VB 20*N*vc FVF 258 / IB 6*fc] + tex SOBJECT 56)
//   Texture   : Tex_ReadPacked    0x417740  (imgSize/rawSize/packedSize/zlib + 8-byte trailer)
//   Reset     : Mesh_ResetFields  0x430110
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Asset/Model.h" // reuses asset::SObjectTexture (SOBJECT texture format 56 bytes, Tex_ReadPacked)

namespace ts2::asset {

// A rendering "subset" of a MOBJECT2 mesh (per-subset loop in Mesh_ReadFile 0x430470).
// Buffers are kept raw (bytes): meant for a direct D3D9 VB/IB upload (separate rendering axis).
struct Mobject2Subset {
    // FVF 258 = 0x102 = D3DFVF_XYZ | D3DFVF_TEX1 -> 20-byte vertex (pos vec3 + uv vec2, NO normal) —
    // CreateVertexBuffer(..., FVF=258, ...) @0x430897. This is the BASE vertex size (N==1, cf. Mesh::n).
    static constexpr size_t kVertexStrideBase = 20; // 20 bytes/vertex (FVF 258)
    static constexpr size_t kFaceStride       = 6;  // IB: 6 bytes/face (3x u16), D3DFMT_INDEX16

    uint32_t vertexCount = 0;           // u32 read -> a1[45][i]   @0x43079d
    uint32_t faceCount   = 0;           // u32 read -> a1[48][i]   @0x430928
    // VB: on-disk size = 20 * N * vertexCount bytes (N = Mobject2Mesh::n).
    //   @0x4307c1 (CPU path mode1: v47 = 20 * a1[21] * vertexCount)
    //   @0x430897 (GXD path       : CreateVertexBuffer(20 * a1[21] * vertexCount, FVF=258))
    // The `20*N` multiplier is byte-exact. SEMANTICS OF N RESOLVED (B4, 2026-07-17): N = number of
    //   flipbook FRAMES, proven by Mesh_DrawAnimatedFrame 0x430BE0 (VB offset of frame f =
    //   20*f*vertexCount @0x431520; bbox block = 40*f @0x431207). N==1 degenerates to a static mesh.
    //   The read below stays byte-exact regardless of N.
    std::vector<uint8_t> vertexBuffer;  // 20 * N * vertexCount bytes (N frames, 20-byte vertex stride)
    std::vector<uint8_t> indexBuffer;   // 6 * faceCount bytes    @0x430942 (CPU) / @0x430a03 (GXD)
};

// A mesh of the MOBJECT2 container (Mesh_ReadFile 0x430470), stride 268 bytes (67 dwords).
// Runtime pointer fields (a1[22/23/45..50/66]) NOT stored: these are heaps allocated at load
// time, their *on-disk contents* are captured here (boneTable/table4/subsets/extraTex).
struct Mobject2Mesh {
    static constexpr size_t kStride       = 268; // 268 bytes per mesh (alloc 268*meshCount @0x4319df)
    static constexpr size_t kHeader1Size  = 76;  // 0x4C, read into a1+2 (a1[2..20])   @0x4304dc
    static constexpr size_t kHeader2Size  = 80;  // 0x50, read into a1+24 (a1[24..43])  @0x4305c6
    static constexpr size_t kBoneStride   = 40;  // 40 bytes/entry (table a1[22], 40*N)   @0x43051d
    static constexpr size_t kTable4Stride = 4;   // 4 bytes/entry  (table a1[23], 4*N)     @0x430573

    uint32_t index = 0;
    bool     empty = false;             // type==0 => empty mesh (nothing else to read)  @0x4304a9
    uint32_t type  = 0;                 // a1[0]: type/flag                              @0x4304a7
    std::vector<uint8_t> header1;       // 76 bytes (a1[2..20]) — transform/bounds, undecoded
    // a1[21]: flipbook FRAME COUNT (VB = 20*N*vertexCount, @0x4304fd). SEMANTICS RESOLVED
    // (B4, 2026-07-17) by Mesh_DrawAnimatedFrame 0x430BE0: frame f indexes the VB at 20*f*vertexCount
    // (@0x431520) and its bbox block at 40*f (@0x431207); N==1 => static mesh. Consumed by
    // Gfx/EmitterMeshRenderer. (The earlier "texcoord sets" hypothesis is ruled out.)
    uint32_t n = 0;
    std::vector<uint8_t> boneTable;     // 40 * N bytes (a1[22]) — bone/matrix table, undecoded
    std::vector<uint8_t> table4;        // 4  * N bytes (a1[23]) — parallel table, undecoded
    std::vector<uint8_t> header2;       // 80 bytes (a1[24..43]) — 2nd header block, undecoded
    uint32_t subsetCount = 0;           // a1[44]                                        @0x4305ec
    std::vector<Mobject2Subset> subsets;
    SObjectTexture tex;                 // 1 SOBJECT texture 56 bytes (Tex_ReadPacked a1+51)  @0x430a80
    uint32_t extraTexCount = 0;         // a1[65]                                        @0x430ab1
    std::vector<SObjectTexture> extraTex; // 56 bytes each (a1[66], 56*extraTexCount)      @0x430b22
};

// .MOBJECT2 container (Mesh_LoadMOBJECT2 0x4318C0). On-disk layout:
//   [8-byte magic "MOBJECT2"][u32 attribute][u32 meshCount][mesh x meshCount]
// attribute==0 => empty container (no meshCount read, valid, return 1 @0x4319b0).
class Mobject2 {
public:
    static constexpr size_t kMagicSize  = 8;
    static constexpr size_t kMeshStride = Mobject2Mesh::kStride; // 268

    // Loads and decodes a STANDALONE .MOBJECT2 (whole file). Returns false on failure.
    bool Load(const std::string& path);

    // Decodes a MOBJECT2 container EMBEDDED at `offset` in a stream (emitter/effect use).
    // bytesConsumed() reports how many bytes were read (to resume the stream afterward).
    bool Parse(const uint8_t* data, size_t size, size_t offset = 0);

    uint32_t attribute() const { return attribute_; } // lpBuffer[0] (container flag/type)
    uint32_t meshCount() const { return meshCount_; }  // lpBuffer[1]
    const std::vector<Mobject2Mesh>& meshes() const { return meshes_; }

    bool   ok() const { return ok_; }                  // full structural parse, no exception
    size_t bytesConsumed() const { return bytesConsumed_; } // bytes read since `offset`
    size_t leftover() const { return leftover_; }      // bytes remaining after the container (>0 if embedded)
    const std::string& error() const { return error_; }

private:
    uint32_t attribute_    = 0;
    uint32_t meshCount_    = 0;
    std::vector<Mobject2Mesh> meshes_;
    bool     ok_           = false;
    size_t   bytesConsumed_ = 0;
    size_t   leftover_     = 0;
    std::string error_;
};

} // namespace ts2::asset

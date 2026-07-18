// Asset/Model.h — 3D model readers for TwelveSky2.
//   .SOBJECT -> struct SObject  (animated skeletal model, array of meshes)
//   .MOBJECT -> struct MObject  (static GMOBJECT model, array of "parts")
//
// Faithful translation of the VALIDATED Python parsers:
//   RE/asset_parsers/sobject.py  (Format A "SOBJECT" validated 4910/4910 files)
//   RE/asset_parsers/mobject.py  (Model_LoadFromFile 0x6A3490)
//
// Original loaders:
//   SOBJECT A : Model_LoadFile 0x40E700 -> Model_LoadFromPak 0x40EA30
//               Mesh_ReadFromMemory 0x40C380, Tex_ReadFromMemory 0x417D20
//   SOBJECT B : cSObject 0x43D380 -> cMesh_ReadFromStream 0x436CA0 (per-mesh compression)
//   MOBJECT   : Model_LoadFromFile 0x6A3490 -> MeshPart_Load 0x6AD160
//               Tex_LoadCompressedFromHandle 0x6A9CF0, GXD_DecompressEntity 0x6A1A30
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

// =====================================================================
//  .SOBJECT — skinned model (Format A: magic "SOBJECT" + zlib envelope
//  as a SINGLE stream covering the whole model).
// =====================================================================

// Texture block of a SOBJECT mesh (Tex_ReadFromMemory 0x417D20).
// The decompressed geometry contains these blocks WITHOUT decompressing them:
// so we keep the raw zlib stream (usable on demand via Zlib).
// ex-VeryOldClient: TEXTURE_FOR_GXD.Load (CONFIRMED) — image + 2 trailing u32 =
// processMode/alphaMode; on the target side the zlib stream stays raw (no GXCW/Load2-DXT).
//
// TRAILER 8 bytes (Pass 4 / W7, sobject-material front — gap SOBJ-02): the decompressed block equals
// `[DDS: ddsSize bytes][u32 processMode][u32 alphaMode]` (rawSize == ddsSize + 8). Tex_ReadPacked
// 0x417740 reads THESE TWO u32 from the tail of the block and stores them in the 56-byte texture struct:
//     a1[10] = *(_DWORD *)&Heap[lpMema];      // @0x4178d6 -> material +40  (processMode)
//     a1[11] = *(_DWORD *)&Heap[lpMema + 4];  // @0x4178dd -> material +44  (alphaMode)
//   (lpMema = a1[1] = ddsSize @0x41788e ; pTexture = a1[13] -> +52 @0x41795d)
// Stride 56 cross-checked: `v51 = 56 * *v34` @0x40c29c, `v37 += 56` @0x40c2d0, `ii += 56` @0x40c2e4,
// `v61 = *(a2+884) + 56*v15` @0x40cb09.
// Field +44 IS the blendMode consumed by rendering: `v16 = *(_DWORD *)(v61 + 44)`
// @0x40cb1a / @0x40cb2c (pass filter) and @0x40d953 (state restoration).
struct SObjectTexture {
    bool     present    = false; // ddsSize==0 => texture absent
    uint32_t ddsSize    = 0;     // a2[1]: DDS size
    uint32_t rawSize    = 0;     // decompressed size (DDS + 8)
    uint32_t packedSize = 0;     // zlib bytes
    std::vector<uint8_t> compressed; // raw zlib stream (packedSize bytes)

    // --- Trailer 8 bytes (Tex_ReadPacked 0x417740) ---
    // WARNING: `processMode` has NO PROVEN READER in the skinned draw path
    // (Model_DrawSkinnedSubset 0x40CA40 only reads +44). It is exposed because the binary
    // decodes and stores it — NOT because any usage is known. Do not infer anything from it.
    uint32_t processMode = 0;  // material +40 — a1[10] @0x4178d6
    uint32_t alphaMode   = 0;  // material +44 — a1[11] @0x4178dd — == blendMode (0/1/2)
    bool     trailerDecoded = false; // true if the block inflate allowed reading the 8-byte trailer
};

// A rendering "subset" of a mesh (skinned FVF). Buffers are kept
// as-is (bytes): they are uploaded directly into D3D9 VB/IB.
// Mesh_ReadFromMemory 0x40C380 (strides CONFIRMED byte-exact 4910/4910).
// ex-VeryOldClient: SKIN2_FOR_GXD (same strides VB76/IB6/skin32/idxCopy1 6/idxCopy2 6).
struct SObjectSubset {
    // VB 76 bytes/vertex — ex-VeryOldClient: SKINVERTEX2_FOR_GXD (PLAUSIBLE, GAP G2/FVF typing):
    //   pos12 (mV3) + blendWeight16 (mW4) + boneIndex4 u8 (mB[4]) + normal/tangent/binormal 36
    //   (3× mN) + uv8 (mT2). pos/weights/bones proven by IDA; N/T/B order not resolved statically.
    static constexpr size_t kVertexStride = 76; // VB: 76 bytes/vertex (skinned FVF) — Model_DrawSkinnedSubset 0x40CA40
    // skin 32 bytes/vertex — ex-VeryOldClient: SKINSHADOWVERTEX2_FOR_GXD (PLAUSIBLE, GAP G3/skinning
    //   runtime): pos12 + weight[4]16 + boneIndex[4] u8 4; Model_BuildShadowVolume 0x40DC70.
    static constexpr size_t kSkinStride   = 32; // 32 bytes/vertex (weights/bones)
    static constexpr size_t kFaceStride   = 6;  // IB: 6 bytes/face (3× u16)

    uint32_t vertexCount = 0;          // a1[171]
    uint32_t faceCount   = 0;          // a1[173]
    std::vector<uint8_t> vertexBuffer; // 76 * vertexCount
    std::vector<uint8_t> indexBuffer;  // 6 * faceCount
    std::vector<uint8_t> skin;         // 32 * vertexCount
    std::vector<uint8_t> indexCopy1;   // 6 * faceCount (copy #1: topology)
    std::vector<uint8_t> indexCopy2;   // 6 * faceCount (copy #2: adjacency, Model_BuildShadowVolume 0x40DC70)
};

// A mesh of the SOBJECT model (Mesh_ReadFromMemory 0x40C380).
// ex-VeryOldClient: SKIN_FOR_GXD / SKIN3::Load — names only; VeryOld layout DIFFERENT
// (SKINEFFECT 120 + SKINSIZE 40). IDA wins: opaque fixed header of 372 bytes (CONFIRMED).
struct SObjectMesh {
    static constexpr size_t kHeaderSize = 372; // 0x44 + 0x130 between field0 and subsetCount

    uint32_t index      = 0;
    bool     empty      = false; // field0==0 => empty mesh (no body)
    uint32_t field0     = 0;     // a1[0] : type/flag
    std::vector<uint8_t> header; // 372-byte fixed header (transform/bounds, not decoded)
    uint32_t subsetCount = 0;    // a1[93] (u32 @ +372)
    std::vector<SObjectSubset> subsets;
    SObjectTexture tex[3];       // diffuse, ~normal, ~specular/emissive
    uint32_t extraCount = 0;     // a1[220]
    std::vector<SObjectTexture> extra;
};

// "Per-mesh" envelope of Format B (no magic). The validated Python parser
// only decodes the zlib envelope of the 1st mesh (the internal format
// cMesh_ReadFromStream is not proven): we stay faithful to this limitation.
struct SObjectRawMesh {
    uint32_t flag       = 0;
    uint32_t rawSize    = 0;
    uint32_t packedSize = 0;
    bool     inflateOk  = false;
    std::vector<uint8_t> decompressed; // rawSize bytes (mesh geometry)
};

// Model_LoadFile 0x40E700 -> Model_ReadSubHeader 0x40E8E0 -> Model_LoadFromPak 0x40EA30.
// ex-VeryOldClient: SOBJECT3_FOR_GXD.cpp (struct names only). CONFLICT crypto (IDA wins):
// the GXCW of the VeryOld build is ABSENT from the target — the `01 01 00 00` header is read IN THE CLEAR
// then a single zlib stream (4910/4910 inflate plain OK). No GXCW/XXTEA to port (cf. Rosetta §4.B).
class SObject {
public:
    enum class Format {
        Unknown,
        SObjectA, // magic "SOBJECT": global zlib envelope (validated)
        RawB,     // no magic: per-mesh compression (light validation)
    };

    // Loads and decodes a .SOBJECT. Returns false on failure.
    bool Load(const std::string& path);

    Format format()  const { return format_; }
    char   version()  const { return version_; }   // '2' | '3' (Format A)
    uint8_t subType() const { return subType_; }
    uint8_t subVer()  const { return subVer_; }

    // Envelope metadata (Format A).
    uint32_t rawSize()    const { return rawSize_; }
    uint32_t packedSize() const { return packedSize_; }
    bool     envOk()      const { return envOk_; }     // header + packedSize == file size
    bool     inflateOk()  const { return inflateOk_; } // decompressed size == rawSize
    bool     walkOk()     const { return walkOk_; }    // final cursor == rawSize

    // Model meshes (Format A). Empty in Format B.
    uint32_t meshCount() const { return meshCount_; }
    const std::vector<SObjectMesh>& meshes() const { return meshes_; }

    // Format B: announced mesh count + envelope of the 1st mesh (partial).
    uint32_t rawBMeshCount() const { return rawBMeshCount_; }
    const std::vector<SObjectRawMesh>& rawBMeshes() const { return rawBMeshes_; }

    const std::string& error() const { return error_; }

private:
    bool parseFormatA(const std::vector<uint8_t>& data, const std::string& path);
    bool parseFormatB(const std::vector<uint8_t>& data, const std::string& path);

    Format   format_     = Format::Unknown;
    char     version_    = 0;
    uint8_t  subType_    = 0;
    uint8_t  subVer_     = 0;
    uint16_t pad_        = 0;
    uint32_t rawSize_    = 0;
    uint32_t packedSize_ = 0;
    bool     envOk_      = false;
    bool     inflateOk_  = false;
    bool     walkOk_     = false;
    uint32_t meshCount_  = 0;
    std::vector<SObjectMesh> meshes_;

    uint32_t rawBMeshCount_ = 0;
    std::vector<SObjectRawMesh> rawBMeshes_;

    std::string error_;
};

// =====================================================================
//  .MOBJECT — static GMOBJECT model (compression PER ENTITY:
//  [rawSize][packedSize][zlib stream] envelope decoded for geometry AND
//  textures).
// =====================================================================

// Texture block of a part (Tex_LoadCompressedFromHandle 0x6A9CF0).
// Unlike SOBJECT, the image is decompressed here.
// ex-VeryOldClient: TEXTURE_FOR_GXD / CTEXTURE (CONFIRMED) — 8-byte trailer = 2 u32
// processMode/alphaMode; rawSize == imgSize+8 verified.
struct MTexture {
    bool     present    = false; // imgSize==0 => absent
    uint32_t imgSize    = 0;     // DDS/TGA image size
    uint32_t rawSize    = 0;     // = imgSize + 8 (image + trailer)
    uint32_t packedSize = 0;     // zlib bytes
    std::vector<uint8_t> image;  // rawSize decompressed bytes (image + 8-byte trailer)
    uint32_t trailer0   = 0;     // 2 trailing u32 (dims/uv), read at data[imgSize]
    uint32_t trailer1   = 0;
    char     magic[4]   = {0,0,0,0}; // first 4 bytes of the image (FourCC / "DDS ")
};

// Decompressed geometry of a part (cf. MeshPart_Load 0x6AD160 / parse_geometry).
struct MGeometry {
    static constexpr size_t kHeaderSize   = 0x78; // 120 bytes, copied into part+132
    static constexpr size_t kMatrixStride = 64;   // 4×4 matrix (16 floats)
    // ex-VeryOldClient: MESHVERTEX_FOR_GXD (CONFIRMED) — 32 bytes = mV3 pos + mN3 normal + mT2 uv.
    // Static MOBJECT vertex, DISTINCT from the 32-byte SOBJECT SkinVertex (pos+weights+bones).
    static constexpr size_t kVertexStride = 32;   // FVF XYZ | NORMAL | TEX1
    static constexpr size_t kFaceStride   = 6;    // 3× u16

    std::vector<uint8_t> header;   // 120 bytes (Heap[..30])
    uint32_t M = 0;                // Heap[30]: number of groups/matrices
    uint32_t V = 0;                // Heap[31]: vertices per group
    uint32_t X = 0;                // Heap[32]: (unused for size)
    uint32_t I = 0;                // Heap[33]: number of triangles
    std::vector<uint8_t> matrices; // M * 64 bytes
    std::vector<uint8_t> vertices; // 32 * M * V bytes  (M*V vertices)
    std::vector<uint8_t> indices;  // 6 * I bytes
    bool     sizeOk = false;       // 0x88 + mats + vtx + idx == decompressed size
};

// 120-byte material header of the MeshPart, DECODED into named fields.
//
// Provenance: MeshPart_Load 0x6AD160 copies the first 120 bytes of the decompressed geometry
// blob into part+132 (`qmemcpy((void*)(this+132), Heap, 0x78)` @0x6ad2d1), i.e.
// Heap[0..29] = 30 dwords. This same blob is kept raw in `MGeometry::header`
// (kHeaderSize = 0x78); it is REINTERPRETED here without being modified (ADDITIVE decoding;
// geo.header stays intact for audit).
//
// Offset correspondence proven by the write (Load) <-> read agreement
// (MeshPart_RenderFull 0x6B0850): header dword `k` = Heap[k] = part dword [33+k] =
// part offset (132 + 4*k). Each field carries the anchor of the site that READS it at render time.
// A/B/X/D (Heap[30..33]) are OUTSIDE this block (already exposed as MGeometry::M/V/X/I).
//
// SHARED STRUCT: .MOBJECT (MeshPart) AND .WO (WorldMeshPart, Asset/WorldChunk.h)
// go through the SAME loader MeshPart_Load 0x6AD160 → BYTE-IDENTICAL material header.
// WorldMeshPart therefore reuses THIS type (cf. banner in Asset/WorldChunk.h) — no duplicate.
//
// ⚠ Fine semantics NOT PINNED (runtime residue, cf. TS2_DEEP_MESHPART_MATERIAL.md §10 /
// TS2_DEEP_MOBJECT.md §2): the R/G/B/A role of the `lightAnim.Pairs` channels (passed to
// Gfx_SetLight slot 2 @0x6b0988 as animated diffuse color) and the
// `glow.SpecRGBA` channels (set as D3DMATERIAL9.Specular @0x6b0a48) — only the OFFSETS are
// proven, not the component order.
struct MeshPartMaterial {
    // header[0] = Heap[0]: internal sub-count, NEVER read by MeshPart_RenderFull
    // (exposed for completeness; do not infer anything from it).
    uint32_t subCount = 0;

    // Animated emissive light (triangular ping-pong) — gate `this[34]` @0x6b087d.
    // 4-channel loop `v10=(float*)(this+144)`: channel i reads v10[0]=Pairs[i] ("from")
    // and v10[4]=Pairs[i+4] ("to") @0x6b08c5; phase = v66 * Speed @0x6b08bb.
    struct {
        uint32_t Enable  = 0;                        // header[1]  = part[34] (+136)  @0x6b087d
        float    Speed   = 0.0f;                     // header[2]  = part[35] (+140)  @0x6b08bb
        float    Pairs[8] = {0,0,0,0,0,0,0,0};       // header[3..10] = part[36..43] (+144..+175)
                                                     //   [0..3]="from", [4..7]="to"; channels not pinned
    } lightAnim;

    // Disables light 0 (Gfx_SetLight(1,0…)) — gate `this[44]` @0x6b099b.
    uint32_t noLight = 0;                            // header[11] = part[44] (+176)

    // View-dependent specular glow: Gfx_SetMaterialEmissive actually sets the
    // D3DMATERIAL9.Specular (RGBA) + Power — gate `a5 && this[45] && !this[99]` @0x6b0a11.
    struct {
        uint32_t Enable      = 0;                    // header[12] = part[45] (+180)  @0x6b0a11
        uint32_t Mode        = 0;                    // header[13] = part[46] (+184)  @0x6b0a1f (1=constant, 2=view-dependent)
        float    SpecRGBA[4] = {0,0,0,0};            // header[14..17] = part[47..50] (+188..+203) @0x6b0a48 (this+188=(_DWORD*)this+47)
        float    SpecPower   = 0.0f;                 // header[18] = part[51] (+204)  @0x6b0a34 (D3DMATERIAL9.Power)
    } glow;

    // Intensity scalar of the projection light (Gfx_SetShadowProjLight(this[52],…))
    // @0x6b0a59. Outside the requested list but within the 120 bytes: exposed for complete decoding.
    float lightOffset = 0.0f;                        // header[19] = part[52] (+208)

    // Flipbook of the base texture (animated atlas, modulo matCount) — gate `this[53]` @0x6b0d33.
    struct {
        uint32_t Enable = 0;                         // header[20] = part[53] (+212)  @0x6b0d33
        float    Fps    = 0.0f;                      // header[21] = part[54] (+216)  @0x6b0d78 (Crt_Dbl2Uint(v66*this[54]))
    } flipbook;

    // UV scroll per texture matrix. Mode (switch 1..4):
    // 1=scroll V, 2=scroll U, 3=diagonal, 4=anti-diagonal; speed = v66 * Speed.
    struct UvScroll {
        uint32_t Enable = 0;                         // tex1 header[22]=part[55](+220) @0x6b0f59 | tex2 header[27]=part[60](+240) @0x6b19bb
        uint32_t Mode   = 0;                         // tex1 header[23]=part[56](+224) @0x6b0f73 | tex2 header[28]=part[61](+244) @0x6b19d5
        float    Speed  = 0.0f;                      // tex1 header[24]=part[57](+228) @0x6b1016 | tex2 header[29]=part[62](+248) @0x6b1a78
    };
    struct {
        UvScroll tex1;                               // header[22..24] (stage 0)
        UvScroll tex2;                               // header[27..29] (2nd pass)
    } uvScroll;

    // Camera-facing billboard overlay (CPU-built quad + DrawPrimitiveUP) — gate `this[58]` @0x6b107c.
    struct {
        uint32_t Enable = 0;                         // header[25] = part[58] (+232)  @0x6b107c
        uint32_t Mode   = 0;                         // header[26] = part[59] (+236)  @0x6b11b0 (1=screen plane flt_8001D4 / other=free axis unk_80022C)
    } billboard;

    bool decoded = false;                            // true if the 120-byte header has been decoded
};

// Decodes the 120-byte material header (MGeometry::header) into named fields (ADDITIVE).
// Shared .MOBJECT / .WO (same MeshPart_Load 0x6AD160). Implementation: Model.cpp.
void DecodeMeshPartMaterialHeader(const std::vector<uint8_t>& header, MeshPartMaterial& out);

// A "part" of the MOBJECT model (MeshPart_Load 0x6AD160).
struct MeshPart {
    uint32_t index    = 0;
    bool     hasMesh  = false;  // flag 0 => empty part (nothing else to read)
    uint32_t geoRaw   = 0;      // rawSize of the geometry envelope
    uint32_t geoPacked = 0;     // packedSize of the geometry envelope
    MGeometry geo;
    MeshPartMaterial mat;       // decoded 120-byte material header (from geo.header) — ADDITIVE
    MTexture  tex0;             // main texture
    MTexture  tex1;             // 2nd texture
    uint32_t  matCount = 0;     // number of additional materials
    std::vector<MTexture> mats;
};

// Model_LoadFromFile 0x6A3490 -> MeshPart_Load 0x6AD160 ; GXD_DecompressEntity 0x6A1A30.
// ex-VeryOldClient: MOBJECT_FOR_GXD.cpp (CONFLICT crypto, IDA wins) — VeryOld encrypts `{mMeshNum}`
// with XXTEA; the target reads `nPart` IN THE CLEAR then per-entity zlib. No `TEA1` magic.
class MObject {
public:
    // Loads and decodes a .MOBJECT. Returns false on failure.
    bool Load(const std::string& path);

    uint32_t partCount() const { return partCount_; }
    const std::vector<MeshPart>& parts() const { return parts_; }

    // true if the file was fully consumed (leftover==0) and without
    // internal geometry size warning.
    bool ok() const { return ok_; }
    size_t leftover() const { return leftover_; }

    const std::string& error() const { return error_; }

private:
    uint32_t partCount_ = 0;
    std::vector<MeshPart> parts_;
    bool   ok_       = false;
    size_t leftover_ = 0;
    std::string error_;
};

} // namespace ts2::asset

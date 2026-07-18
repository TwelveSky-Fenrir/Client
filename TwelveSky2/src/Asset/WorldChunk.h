// Asset/WorldChunk.h — reader for TS2 world geometry chunks.
//   D07_GWORLD\Z%03d.{WM,WJ,WG,WO,WP}
//
// Faithful reverse of RE/asset_parsers/world_geometry.py (validated 455/455 files),
// itself modeled on TwelveSky2.exe's runtime readers:
//   .WM -> MapColl_LoadFaces      (0x694510)  primary collision    (zlib envelope)
//   .WJ -> MapColl_LoadFaces      (0x694510)  secondary collision  (same structure)
//   .WG -> MapColl_LoadMapFile    (0x697B30)  faces + materials/textures
//   .WO -> MapColl_LoadObjectsA   (0x6980D0)  static models (Model/MeshPart)
//   .WP -> MapColl_LoadObjectsB   (0x6983B0)  FX nodes (Fx_NodeLoadFromHandle)
//
// Compression primitive = GXD_DecompressEntity (0x6A1A30):
//   GXD block   = [rawSize:u32][packedSize:u32][zlib stream] -> rawSize bytes
//   image block = [imageSize:u32][rawSize=imageSize+8:u32][packedSize:u32][zlib]
//                -> DDS/BMP(imageSize) + 8-byte trailer; imageSize==0 => texture absent
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// asset::MeshPartMaterial (decoded 120-byte material header): REUSED as-is for the
// WorldMeshPart of .WO files. .WO AND .MOBJECT go through the SAME MeshPart_Load
// 0x6AD160 loader, so their material header is BYTE-IDENTICAL — hence sharing the same
// struct (no duplication: AssetSelfTest.cpp includes both headers in the same unit).
#include "Asset/Model.h"

namespace ts2::asset {

// Sub-format detected (by extension). WM and WJ share the same structure.
// SOBJECT_B = multi-part "Format B" mesh (W*.SOBJECT) — NOT routed by extension
// (the .SOBJECT extension is ambiguous with the skinned SObject): must be passed
// explicitly to LoadFromMemory. IDA anchor: cMesh_ReadFromStream 0x436CA0.
enum class WorldChunkType { Unknown, WM, WJ, WG, WO, WP, SOBJECT_B };

// Compressed texture block (Tex_LoadCompressedFromHandle).
// present==false <=> imageSize==0 ("texture absent", 4 bytes only).
struct TextureBlock {
    bool        present   = false;   // Python: not empty
    uint32_t    imageSize = 0;       // size of the decoded image (DDS/BMP)
    uint32_t    packedSize = 0;      // size of the on-disk zlib stream
    std::string imgType;             // "DDS", "BMP", or hex of the 4 magic bytes
    uint32_t    trailer[2] = {0, 0}; // 8 bytes at offset imageSize (two u32)
    std::vector<uint8_t> data;       // decoded image (imageSize bytes) — usable downstream

    // --- Decoded trailer (Tex_LoadCompressedFromHandle 0x6A9CF0) ---------------
    // The decompressed block is [image(imageSize)][u32 @imageSize][u32 @imageSize+4]. Both
    // trailing u32 are stored in the 52-byte runtime texture struct, proven by decompilation:
    //   struct+40 = trailer[0]  (this[10], writes `*(this+10)=*(lpMem+imageSize)`   @0x6a9ea1)
    //   struct+44 = trailer[1]  (this[11], writes `*(this+11)=*(lpMem+imageSize+4)` @0x6a9eab)
    // For a MeshPart's textures (part+296 = base, part+348 = 2nd), field +44 IS the blend
    // mode READ by MeshPart_RenderFull 0x6B0850: base = `this[85]` (v24), 2nd = `this[98]`
    // (v42). Values: 1 = alpha-test (RS15=1/RS24=128), 2 = alpha blend, other = additive (if fade).
    uint32_t category() const { return trailer[0]; } // struct+40 (this[10]) @0x6a9ea1
    uint32_t mode()     const { return trailer[1]; } // struct+44 (this[11]) @0x6a9eab = MeshPart blend
};

// Quaternion animation track (Anim_LoadQuatTrackFromHandle).
// GXD block: 8-byte header (frames,tracks) + 28 bytes * frames * tracks.
struct AnimTrack {
    bool     present = false;
    uint32_t frames  = 0;            // u32 @0 of the decoded block
    uint32_t tracks  = 0;            // u32 @4 of the decoded block
    std::vector<uint8_t> data;       // decoded GXD block (header + 28-byte entries)
};

// Terrain vertex (Gap G7) — FVF 530 = 0x212 = D3DFVF_XYZ|NORMAL|TEX2, 40 bytes.
// Two UV sets: uv0 = diffuse texture (stage 0), uv1 = lightmap/.SHADOW (stage 1).
// IDA ref: Terrain_Render 0x698670 — SetFVF(530) @0x698e6d; stride 40 in every
// DrawPrimitiveUP (device vtbl+332, e.g. @0x698ff3/@0x69913c/@0x69945d); 120 bytes=3*40/face
// copied via qmemcpy(dst, v48+1, 0x78) @0x698e21 (v48+1 = skips materialIndex).
// Position @+0 proven by the barycentric tests: MapColl_RayHitTriangle 0x695ae0
// (reads face+4/+8/+12 then face+44/+48/+52) and MapColl_PointInTriangleXZ 0x695c70 (face+84).
// DISTINCT from the 32-byte MobjVertex of .WO files (see Gfx). normal/uv/uv2 layout = FVF interpretation.
struct TerrainVertex {
    float position[3];   // +0x00  WORLD space (world = identity for terrain rendering)
    float normal[3];     // +0x0C  unit-length (D3DFVF_NORMAL)
    float uv0[2];        // +0x18  texcoord set 0 (diffuse, stage 0)
    float uv1[2];        // +0x20  texcoord set 1 (lightmap/shadow, stage 1 — supports G6)
};
static_assert(sizeof(TerrainVertex) == 40, "TerrainVertex must be 40 bytes (FVF 530)");

// Collision/terrain-render face (Gap G4) — 156 bytes, AUTHORITATIVE IDA order.
// CONFLICT C-02 (TS2_WORLD_ROSETTA.md §2): materialIndex comes FIRST (@0). VeryOldClient
// placed it @152 -> IGNORED (IDA wins). Total 156 bytes confirmed on both sides.
// IDA ref (proven offsets):
//   - materialIndex@0    : 120 bytes of vertices copied from face+4 (Terrain_Render qmemcpy @0x698e21)
//   - v0@+4 v1@+44 v2@+84 (stride 40): MapColl_RayHitTriangle 0x695af4/0x695afc / PointInTriangleXZ 0x695c98
//   - plane@+124/+128/+132/+136 (a,b,c,d): MapColl_GetGroundHeight plane-solve 0x6972ad
//                                            + backface Terrain_Render 0x698dd4 (v47[31..34])
//   - sphereCenter@+140 / sphereRadius@+152: Cam_FrustumTestSphere2x(v47+35, v47[38]) @0x698de9
struct CollisionFace {
    uint32_t      materialIndex;    // +0x00  (aka "mTextureIndex"); ==1 => walkable in the .WM layer
    TerrainVertex v0;               // +0x04
    TerrainVertex v1;               // +0x2C
    TerrainVertex v2;               // +0x54
    float         plane[4];         // +0x7C  planeA/B/C/D = normal.xyz + D (tris+124/+128/+132/+136)
    float         sphereCenter[3];  // +0x8C  bounding sphere center (tris+140/+144/+148)
    float         sphereRadius;     // +0x98  bounding sphere radius (tris+152)

    // planeB (= normal.y): plane-solve divisor; > 0 => face oriented upward,
    // walkable by default (MapColl_GetGroundHeight filter 0x697259, solve 0x6972ad).
    bool PlaneFacesUp() const { return plane[1] > 0.0f; }
    // Walkable tag of the .WM layer (WORLD2 variant). Rosetta §1.A / §3 G04: mTextureIndex==1.
    bool IsWalkableTag() const { return materialIndex == 1; }
};
static_assert(sizeof(CollisionFace) == 156, "CollisionFace must be 156 bytes");

// Collision quadtree node (Gap G5) — 48 bytes, RUNTIME layout.
// IDA ref: MapColl_GetGroundHeight 0x697130 — quadtree base = *(this+35);
//   bboxMin@+0 / bboxMax@+12 (XZ test @0x6971ba); ceiling = node0.bboxMax.y @+16 (@0x6971e5);
//   trisNum@+24 (@0x6971fc); trisIndex@+28 (@0x69726c); child[4]@+32 (@0x697171/@0x697159).
// Root = node index 0; leaf <=> child[0] == -1.
// The ON-DISK format is variable-size (fixed 48 bytes + 4*faceRefCount if hasFaceRefs); we
// rebuild it here as a fixed 48-byte array + aggregated index buffer (CollisionMesh::triIndices).
// `trisIndex` = OFFSET (in u32 entries) into triIndices (the runtime keeps a live pointer there).
struct CollisionQuadNode {
    float    bboxMin[3];   // +0x00
    float    bboxMax[3];   // +0x0C   (node0 +16 = bboxMax.y = default world ceiling)
    uint32_t trisNum;      // +0x18   number of face indices (leaf)
    uint32_t trisIndex;    // +0x1C   offset into CollisionMesh::triIndices (originally a live ptr)
    int32_t  child[4];     // +0x20   4 children; child[0]==-1 => leaf

    bool IsLeaf() const { return child[0] == -1; }
};
static_assert(sizeof(CollisionQuadNode) == 48, "CollisionQuadNode must be 48 bytes");

// Shared collision mesh (MapColl_LoadFaces 0x694510; 1st block of WG).
//   numTri + (156 bytes * triangles) + numNodes + field34 + quadtree.
//   Each on-disk node: min[3]f max[3]f numIdx u32 hasIdx u32 [index u32*numIdx] children[4]u32.
// Typed decoding (Gaps G4/G5/G7): `raw` is kept (byte-exact fidelity + backward compat),
// and the typed fields below expose the same data ready to consume.
struct CollisionMesh {
    uint32_t numTri       = 0;       // number of triangles (156 bytes each)
    uint32_t numNodes     = 0;       // number of quadtree nodes
    uint32_t field34      = 0;       // (this+34) global index counter (leafFaceRefTotal)
    uint32_t totalIndices = 0;       // sum of triangle indices (nodes with hasIdx)
    std::vector<uint8_t> raw;        // full decompressed buffer (triangles + quadtree)

    // --- Typed views decoded from `raw` (read order = MapColl_LoadFaces 0x694510) ---
    std::vector<CollisionFace>     tris;        // Gap G4: numTri decoded faces (156 bytes)
    std::vector<CollisionQuadNode> nodes;       // Gap G5: numNodes decoded nodes (48 bytes), root = index 0
    std::vector<uint32_t>          triIndices;  // aggregated face index buffer (leaves -> tris[])
    std::vector<TerrainVertex>     vertices;    // Gap G7: 3*numTri flat vertices (mirrors the
                                                //   dynamic VB in Terrain_Render a1+164, FVF 530 upload)
};

// Mesh part of a static model (MeshPart_Load).
//   GXD geometry block: 120-byte header + bones + VB + IB.
//   A=Heap[30] B=Heap[31] C=Heap[32] D=Heap[33] (triangle count).
//   expected size = 136 + (A<<6) + 32*A*B + 6*D.
struct WorldMeshPart {
    bool     present   = false;
    uint32_t A = 0, B = 0, C = 0, D = 0;
    bool     geoSizeOk = false;      // raw == 136 + (A<<6) + 32*A*B + 6*D
    std::vector<uint8_t> geo;        // decoded GXD geometry block (first 120 bytes = material header)
    TextureBlock tex1;               // this+296 (BASE texture; tex1.mode() = base blend this[85])
    TextureBlock tex2;               // this+348 (2nd texture ; tex2.mode() = 2nd blend  this[98])
    std::vector<TextureBlock> materials; // this+404[] (num_mat entries = flipbook atlas this[101])

    // 120-byte material header (Heap[0..29]) DECODED into named fields — SAME struct and SAME
    // decoding as the .MOBJECT (shared MeshPart_Load 0x6AD160 loader; `qmemcpy(this+132, Heap, 0x78)`
    // @0x6ad2d1). ADDITIVE: `geo` still kept raw (fidelity + audit). Each field drives a layer of
    // MeshPart_RenderFull 0x6B0850 (cross-proven write<->read mapping, see Model.h).
    MeshPartMaterial mat;            // decoded if mat.decoded (geo >= 120 bytes)

    // Billboard-quad case detected by MeshPart_Load @0x6ad413: `B==4 && C==4 && D==2` -> the
    // part is a camera-facing quad template (2 triangles / 4 vertices), AABB of the 4 verts
    // computed at load time. Complements the mat.billboard.Enable flag (this[58]).
    bool IsBillboardQuad() const { return B == 4 && C == 4 && D == 2; }
};

// Static model (Model_LoadFromHandle) = list of MeshPart.
struct Model {
    bool present = false;
    std::vector<WorldMeshPart> parts;     // num_parts
};

// .WM / .WJ file — a single GXD block containing the collision mesh.
struct MapCollisionChunk {
    CollisionMesh mesh;
    uint32_t rawSize    = 0;
    uint32_t packedSize = 0;
};

// .WG file — geometry (collision) block + materials/textures.
struct MapFaceChunk {
    CollisionMesh mesh;                     // geometry block (tri + quadtree)
    uint32_t geoRaw    = 0;
    uint32_t geoPacked = 0;
    uint32_t numMaterials = 0;              // this+3
    std::vector<TextureBlock> textures;     // num_materials (some absent)
    std::vector<uint32_t> materialIndices;  // this+36: material index table (u32*num)
};

// Placed instance of a `models[]` template in the world — THE actual placement.
// 28 bytes on disk, format confirmed by disassembly (Model_RenderParts
// 0x6A3720, Model_RenderWithShadow_0 0x6A4110): cf. Docs/TS2_WO_PLACEMENT_FORMAT.md.
// NO scale field: the engine never multiplies by a scale matrix for
// .WO objects — scale is always 1.0.
struct AuxRecord {
    uint32_t modelIndex = 0;      // +0x00 index (0-based) into ObjectChunk::models[]
    float    pos[3] = {0, 0, 0};  // +0x04 world position x,y,z
    float    rot[3] = {0, 0, 0};  // +0x10 rotation in DEGREES x,y,z
    // World = Rz(rot.z) * Ry(rot.y) * Rx(rot.x) * T(pos)  (exact D3DX order, see doc)
};

// .WO file — static models + placements + auxiliary records.
struct ObjectChunk {
    bool     empty = false;                 // numModels == 0
    std::vector<Model> models;              // this+23: num_models
    std::vector<uint8_t> placements;        // this+25: 100 bytes * num_models (PER-TEMPLATE
                                             // metadata: NUL-terminated name + padding not read
                                             // by the engine, except for the "NO_SHADOW_" tag —
                                             // NOT a transform record, cf. doc)
    std::vector<std::string> placementNames;// name extracted from placements[] for each model
                                             // (debug / future NO_SHADOW_ tag), same size as
                                             // models
    uint32_t numAux = 0;                    // this+26
    std::vector<AuxRecord> auxRecords;      // THE PLACED INSTANCES (28 bytes/instance on disk):
                                             // position + rotation per instance, resolved via
                                             // modelIndex -> models[modelIndex]
};

// FX node (Fx_NodeLoadFromHandle 0x6a69f0): texture + anim track + 144 bytes of
// emitter fields. The runtime structure is 232 bytes; ON DISK, after the texture (this+1) and
// anim track (this+14), 18 ReadFile calls sequentially read 144 bytes [runtime +72, +216):
//   +72 u32, +76 u32, +80 u32, +84 u32, +88 u32, +92 (12b), +104 u32, +108 (12b),
//   +120 (12b), +132 u32, +136 u32, +140 (16b), +156 (16b), +172 (12b), +184 (12b),
//   +196 (12b), +208 u32, +212 u32  (= exactly 144 bytes, confirmed by Fx_NodeLoadFromHandle).
// `fields` keeps the raw 144-byte blob (fidelity + validated 455/455 parser unchanged); the
// typed view below re-decodes the named fields (offset in `fields` = runtime - 72). The
// names (lifetime/rate/shape/box/…) follow the proven usage on the emission side (Particle_Init
// 0x6a7020 / Particle_UpdateEmit 0x6a7530); their float/u32 interpretation is coherent
// but the exact SEMANTICS of the tail (+132..+215) is not proven -> left in `fields`.
struct FxNode {
    bool present = false;
    TextureBlock tex;                       // this+1  (byte 4)  Tex_LoadCompressedFromHandle
    AnimTrack    anim;                      // this+14 (byte 56) Anim_LoadQuatTrackFromHandle
    std::vector<uint8_t> fields;            // 144 raw bytes [runtime +72, +216)

    // --- Decoded typed view (anchor Fx_NodeLoadFromHandle 0x6a69f0) ---
    float    lifetime     = 0.0f;           // +72
    float    kfFps        = 0.0f;           // +76  (keyframe rate)
    float    rate         = 0.0f;           // +80  (emission rate)
    uint32_t shape        = 0;              // +84  (emission shape, ∈ [1..6])
    float    speed        = 0.0f;           // +88
    float    box[3]       = {0,0,0};        // +92  (emission box xyz)
    float    particleLife = 0.0f;           // +104
    float    minRange[3]  = {0,0,0};        // +108
    float    maxRange[3]  = {0,0,0};        // +120
    float    accelMin[3]  = {0,0,0};        // +172
    float    accelMax[3]  = {0,0,0};        // +184
    // Tail +132/+136/+140(16b)/+156(16b)/+196(12b)/+208/+212: present in `fields`, role
    // not proven (read by Particle_UpdateEmit 0x6a7530) -> TODO anchor 0x6a7530 before typing.
};

// FX instance placed in a zone (.WP, "B" record). 28 bytes on disk (4+12+12),
// 76 bytes at runtime. IDA anchor: MapColl_LoadObjectsB 0x6983b0 (@0x698602 reads 4/12/12);
// tick MapColl_UpdateObjectAnim 0x694A00 (stride 76; fxb+28 = particle system state,
// fxb+0 = nodeIndex, fxb+4 = pos, fxb+16 = rot passed to Particle_Init/UpdateEmit).
struct AuxFxRecord {
    uint32_t nodeIndex = 0;                  // +0  index into FxChunk::nodes[]
    float    pos[3] = {0, 0, 0};             // +4  world position
    float    rot[3] = {0, 0, 0};             // +16 rotation (degrees; kDegToRad = pi/180)
    // +28..+75 = particle system state (runtime, not on disk — init Particle_Init 0x6a7020)
};

// .WP file — FX nodes + placements + B records (placed instances).
struct FxChunk {
    bool     empty = false;                 // numFx == 0
    std::vector<FxNode> nodes;              // this+28: num_fx
    std::vector<uint8_t> placements;        // this+30: 100 bytes * num_fx
    uint32_t numFxb = 0;                    // this+31
    std::vector<AuxFxRecord> fxbRecords;    // placed instances (28 bytes on disk: nodeIndex+pos+rot)
};

// "Format B" mesh — a single PART (W*.SOBJECT). IDA anchor: cMesh_ReadFromStream 0x436CA0.
// On disk: [present u32] (if 0 -> part absent, end of walker); then GXD block
// [rawSize u32][packedSize u32][zlib] (RULE #4: pure zlib, NEVER XTEA) decompressed into Heap;
//   Heap[0..136)   = header (numVerts@120, C@124, numFaces@132)
//   Heap[136..176) = subHeader (40 bytes)
//   Heap[176 ..)   = stream 0 (32 bytes/vertex, numVerts)      -> a1+348
//   Heap[176+32*B) = stream 1 (32 bytes/vertex, numVerts)      -> a1+352
//   then 6*numFaces bytes of indices (INDEX16)                  -> a1+356
// (compacted positions a1+360 = first 12 bytes/vertex; normals a1+364 = vertex+12: derived
//  from stream 0, not re-read from disk). Then textures via Tex_ReadPacked (framing =
// imageSize/rawSize/packedSize/zlib + 8-byte trailer, IDENTICAL to ReadTextureBlock, proven by
// Tex_ReadPacked 0x417740): tex1 (a1+368), tex2 (a1+424), [numMat u32] (a1+480), numMat
// sub-textures (56 bytes runtime each, a1+484). Special case B==4&&C==4&&D==2 = billboard quad
// (AABB of the 4 verts).
struct MeshFormatBPart {
    bool     present = false;
    uint8_t  header[136]    = {};   // Heap[0..136)   numVerts@120 / C@124 / numFaces@132
    uint8_t  subHeader[40]  = {};   // Heap[136..176)
    uint32_t numVerts = 0, C = 0, numFaces = 0;
    std::vector<uint8_t> vb0;       // 32*numVerts (stream 0)
    std::vector<uint8_t> vb1;       // 32*numVerts (stream 1)
    std::vector<uint8_t> ib;        // 6*numFaces  (INDEX16)
    TextureBlock tex1;              // a1+368
    TextureBlock tex2;              // a1+424
    std::vector<TextureBlock> materials; // a1+484 (numMat entries)
};

// Complete "Format B" mesh = multi-part walker (loop while present != 0).
struct MeshFormatBChunk {
    std::vector<MeshFormatBPart> parts;
};

// Unified loader: detects the sub-format and populates the matching member.
class WorldChunk {
public:
    // Loads a chunk from a file; the type is inferred from the extension.
    // Returns false if the extension is unknown, or reading/parsing fails.
    bool Load(const std::string& path);

    // Parses an already in-memory buffer with an explicit type.
    bool LoadFromMemory(const std::vector<uint8_t>& data, WorldChunkType type);

    WorldChunkType Type() const { return type_; }

    // Typed access: only the member matching Type() is populated (nullptr otherwise).
    const MapCollisionChunk* AsCollision() const { return collision_ ? &*collision_ : nullptr; } // WM/WJ
    const MapFaceChunk*      AsFace()      const { return face_      ? &*face_      : nullptr; } // WG
    const ObjectChunk*       AsObjects()   const { return objects_   ? &*objects_   : nullptr; } // WO
    const FxChunk*           AsFx()        const { return fx_        ? &*fx_        : nullptr; } // WP
    const MeshFormatBChunk*  AsMeshB()     const { return meshB_     ? &*meshB_     : nullptr; } // SOBJECT_B

    // Readable summary (counters), useful for comparing to the Python parser's output.
    std::string Describe() const;

private:
    void Reset();

    WorldChunkType type_ = WorldChunkType::Unknown;
    std::optional<MapCollisionChunk> collision_;
    std::optional<MapFaceChunk>      face_;
    std::optional<ObjectChunk>       objects_;
    std::optional<FxChunk>           fx_;
    std::optional<MeshFormatBChunk>  meshB_;
};

// Infers the sub-format from the path's extension (.WM/.WJ/.WG/.WO/.WP).
WorldChunkType WorldChunkTypeFromExtension(const std::string& path);
// Short type name ("WM", "WG", …) — "?" if Unknown.
const char*    WorldChunkTypeName(WorldChunkType t);

} // namespace ts2::asset

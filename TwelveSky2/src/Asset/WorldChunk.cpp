// Asset/WorldChunk.cpp — faithful to RE/asset_parsers/world_geometry.py (validated 455/455).
#include "Asset/WorldChunk.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <cstdio>
#include <cstring>

namespace ts2::asset {
namespace {

// ---- bounded helpers (modeled on Reader.skip/take from the Python parser) ---------

// Copies n consecutive bytes into a vector, validating the bound first.
std::vector<uint8_t> ReadBlob(ByteReader& r, uint64_t n) {
    if (n > r.Remaining()) throw AssetError("out-of-bounds block read");
    std::vector<uint8_t> v(static_cast<size_t>(n));
    if (n) r.Read(v.data(), static_cast<size_t>(n));
    return v;
}

// read_gxd_block: [rawSize u32][packedSize u32][zlib] -> decompressed bytes.
// Advances r by 8 + packedSize bytes. InflateTo validates len == rawSize.
// GXD_DecompressEntity 0x6A1A30. ex-VeryOldClient: ZlibScope.h (shared framing, CONFIRMED).
std::vector<uint8_t> ReadGxdBlock(ByteReader& r, uint32_t& rawOut, uint32_t& packedOut) {
    const uint32_t rawSize = r.U32();
    const uint32_t packed  = r.U32();
    if (r.Remaining() < packed)
        throw AssetError("GXD block: packedSize exceeds the stream");
    std::vector<uint8_t> out = Zlib::Instance().InflateTo(r.Ptr(), packed, rawSize);
    r.Skip(packed);
    rawOut = rawSize;
    packedOut = packed;
    return out;
}

// read_texture_block: [imageSize u32]; if 0 -> absent (present=false).
// Otherwise [rawSize u32][packedSize u32][zlib] -> image(imageSize)+trailer(8 bytes).
// Tex_LoadCompressedFromHandle 0x6A9CF0. ex-VeryOldClient: TEXTURE_FOR_GXD (8-byte trailer) —
// CONFIRMED: world texture = pure zlib; the VeryOld LoadGXCW path (SOBJECT3) does NOT
// apply to the world (IDA wins, no GXCW substitution).
TextureBlock ReadTextureBlock(ByteReader& r) {
    TextureBlock tb;
    const uint32_t imageSize = r.U32();
    if (imageSize == 0) return tb; // present stays false

    const uint32_t rawSize = r.U32();
    const uint32_t packed  = r.U32();
    if (r.Remaining() < packed)
        throw AssetError("texture: packedSize exceeds the stream");
    std::vector<uint8_t> out = Zlib::Instance().InflateTo(r.Ptr(), packed, rawSize);
    r.Skip(packed);
    if (rawSize != imageSize + 8)
        throw AssetError("texture: rawSize != imageSize+8");

    tb.present    = true;
    tb.imageSize  = imageSize;
    tb.packedSize = packed;

    // Image type from the 4 magic bytes (identical to the Python parser).
    if (out.size() >= 4 && std::memcmp(out.data(), "DDS ", 4) == 0) {
        tb.imgType = "DDS";
    } else if (out.size() >= 2 && out[0] == 'B' && out[1] == 'M') {
        tb.imgType = "BMP";
    } else {
        char hex[9] = {0};
        const size_t k = out.size() < 4 ? out.size() : 4;
        for (size_t i = 0; i < k; ++i) std::snprintf(hex + i * 2, 3, "%02x", out[i]);
        tb.imgType = hex;
    }

    // Trailer: two u32 at offset imageSize (out is exactly imageSize+8 bytes).
    std::memcpy(tb.trailer, out.data() + imageSize, 8);
    // Decoded image = the first imageSize bytes.
    tb.data.assign(out.begin(), out.begin() + imageSize);
    return tb;
}

// read_anim_track: [present u32]; if !=0 -> GXD block (8-byte header + 28 bytes*frames*tracks).
// Anim_LoadQuatTrackFromHandle 0x6AAE20. ex-VeryOldClient: MOTION_MATRIX (28-byte record =
// quaternion+translation keyframe, reused, CONFIRMED).
AnimTrack ReadAnimTrack(ByteReader& r) {
    AnimTrack at;
    const uint32_t present = r.U32();
    if (present == 0) return at;

    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> out = ReadGxdBlock(r, raw, packed);
    if (out.size() < 8)
        throw AssetError("anim: block too short for the header");
    uint32_t numFrames = 0, numTracks = 0;
    std::memcpy(&numFrames, out.data() + 0, 4);
    std::memcpy(&numTracks, out.data() + 4, 4);
    const uint64_t expect = 8ull + 28ull * numFrames * numTracks;
    if (raw != expect)
        throw AssetError("anim: rawSize != 8 + 28*frames*tracks");

    at.present = true;
    at.frames  = numFrames;
    at.tracks  = numTracks;
    at.data    = std::move(out);
    return at;
}

// Decodes a 40-byte terrain vertex (FVF 530 = XYZ|NORMAL|TEX2). Gap G7.
// Position@0 (proven barycentric) then normal@12, uv0@24, uv1@32 (FVF interpretation).
void ReadTerrainVertex(ByteReader& r, TerrainVertex& v) {
    v.position[0] = r.F32(); v.position[1] = r.F32(); v.position[2] = r.F32(); // +0
    v.normal[0]   = r.F32(); v.normal[1]   = r.F32(); v.normal[2]   = r.F32(); // +12
    v.uv0[0] = r.F32(); v.uv0[1] = r.F32();                                    // +24
    v.uv1[0] = r.F32(); v.uv1[1] = r.F32();                                    // +32
}

// parse_collision_mesh: operates on the already-decompressed buffer (buf moved into mesh.raw).
// TYPED decoding (Gaps G4/G5/G7): read order mirrors MapColl_LoadFaces 0x694510.
// Layouts proven via IDA (see WorldChunk.h: CollisionFace/CollisionQuadNode/TerrainVertex).
CollisionMesh ParseCollisionMesh(std::vector<uint8_t> buf) {
    CollisionMesh cm;
    ByteReader m(buf);

    // --- Faces (Gap G4): numTri × 156 bytes -----------------------------------
    const uint32_t numTri = m.U32();
    cm.tris.reserve(numTri);
    cm.vertices.reserve(static_cast<size_t>(numTri) * 3u);
    for (uint32_t i = 0; i < numTri; ++i) {
        CollisionFace f{};
        f.materialIndex = m.U32();               // +0
        ReadTerrainVertex(m, f.v0);              // +4
        ReadTerrainVertex(m, f.v1);              // +44
        ReadTerrainVertex(m, f.v2);              // +84
        f.plane[0] = m.F32(); f.plane[1] = m.F32();          // +124/+128 (a,b)
        f.plane[2] = m.F32(); f.plane[3] = m.F32();          // +132/+136 (c,d)
        f.sphereCenter[0] = m.F32();                          // +140
        f.sphereCenter[1] = m.F32();                          // +144
        f.sphereCenter[2] = m.F32();                          // +148
        f.sphereRadius    = m.F32();                          // +152
        // Flat vertices (mirrors the dynamic VB in Terrain_Render a1+164, 120 bytes/face upload).
        cm.vertices.push_back(f.v0);
        cm.vertices.push_back(f.v1);
        cm.vertices.push_back(f.v2);
        cm.tris.push_back(f);
    }

    // --- Quadtree (Gap G5): numNodes on-disk nodes -> 48-byte nodes + aggregated index ---
    const uint32_t numNodes = m.U32();
    const uint32_t field34  = m.U32();       // (this+34) global index counter (leafFaceRefTotal)
    cm.nodes.reserve(numNodes);
    uint32_t totalIdx = 0;
    for (uint32_t i = 0; i < numNodes; ++i) {
        CollisionQuadNode node{};
        node.bboxMin[0] = m.F32(); node.bboxMin[1] = m.F32(); node.bboxMin[2] = m.F32(); // +0
        node.bboxMax[0] = m.F32(); node.bboxMax[1] = m.F32(); node.bboxMax[2] = m.F32(); // +12
        const uint32_t numIdx = m.U32();     // +24 on disk: number of face indices in this node
        const uint32_t hasIdx = m.U32();     // +28 on disk: hasFaceRefs flag
        node.trisNum   = numIdx;
        node.trisIndex = static_cast<uint32_t>(cm.triIndices.size()); // offset into the aggregated buffer
        if (hasIdx) {
            for (uint32_t k = 0; k < numIdx; ++k)
                cm.triIndices.push_back(m.U32()); // u32 index into tris[] (leaf)
            totalIdx += numIdx;
        }
        node.child[0] = m.I32();             // +32 on disk: children[4] u32
        node.child[1] = m.I32();             //   child[0]==-1 => leaf
        node.child[2] = m.I32();
        node.child[3] = m.I32();
        cm.nodes.push_back(node);
    }
    if (!m.Eof())
        throw AssetError("collision: leftover bytes after the quadtree");

    cm.numTri       = numTri;
    cm.numNodes     = numNodes;
    cm.field34      = field34;
    cm.totalIndices = totalIdx;
    cm.raw          = std::move(buf);
    return cm;
}

// NOTE (dedup audit 2026-07-17): decoding of the 120-byte material header is
// FACTORED into asset::DecodeMeshPartMaterialHeader (declared Model.h:294, defined
// Model.cpp) — .WO and .MOBJECT go through the SAME MeshPart_Load 0x6AD160, so
// the header is BYTE-IDENTICAL: a single body avoids any future divergence. Here it's
// called with `mp.geo` (geometry block >= 136 bytes guaranteed); it only reads the first
// 120 bytes. Cross-proven write<->read mapping: see Model.cpp.

// read_meshpart: [present u32]; if !=0 -> GXD geometry block + tex1 + tex2 + materials.
WorldMeshPart ReadMeshPart(ByteReader& r) {
    WorldMeshPart mp;
    const uint32_t present = r.U32();
    if (present == 0) return mp;

    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> out = ReadGxdBlock(r, raw, packed);
    if (out.size() < 136)
        throw AssetError("meshpart: geometry block too short");
    uint32_t A = 0, B = 0, C = 0, D = 0;
    std::memcpy(&A, out.data() + 120, 4);    // Heap[30]
    std::memcpy(&B, out.data() + 124, 4);    // Heap[31]
    std::memcpy(&C, out.data() + 128, 4);    // Heap[32]
    std::memcpy(&D, out.data() + 132, 4);    // Heap[33] (triangle count)
    const uint64_t expect =
        136ull + (static_cast<uint64_t>(A) << 6) +
        32ull * A * B + 6ull * D;

    mp.present   = true;
    mp.A = A; mp.B = B; mp.C = C; mp.D = D;
    mp.geoSizeOk = (static_cast<uint64_t>(raw) == expect);
    mp.geo       = std::move(out);
    // Decodes the first 120 bytes of the geometry block (= material header Heap[0..29]) into
    // named fields (ADDITIVE: `geo` stays intact). Decoder SHARED with the .MOBJECT (MeshPart_Load 0x6AD160).
    DecodeMeshPartMaterialHeader(mp.geo, mp.mat);
    mp.tex1      = ReadTextureBlock(r);      // this+296 (base; tex1.mode() = base blend this[85])
    mp.tex2      = ReadTextureBlock(r);      // this+348 (2nd; tex2.mode() = 2nd blend  this[98])
    const uint32_t numMat = r.U32();         // this+400
    mp.materials.reserve(numMat);
    for (uint32_t i = 0; i < numMat; ++i)
        mp.materials.push_back(ReadTextureBlock(r)); // this+404[]
    return mp;
}

// read_model: [present u32]; if !=0 -> [numParts u32] + numParts MeshPart.
Model ReadModel(ByteReader& r) {
    Model md;
    const uint32_t present = r.U32();
    if (present == 0) return md;
    const uint32_t numParts = r.U32();
    md.present = true;
    md.parts.reserve(numParts);
    for (uint32_t i = 0; i < numParts; ++i)
        md.parts.push_back(ReadMeshPart(r));
    return md;
}

// read_fxnode: [present u32]; if !=0 -> texture + anim track + 144 bytes of emitter fields.
// IDA anchor: Fx_NodeLoadFromHandle 0x6a69f0 (18 ReadFile calls after texture+anim = 144 bytes on disk).
FxNode ReadFxNode(ByteReader& r) {
    FxNode fn;
    const uint32_t present = r.U32();
    if (present == 0) return fn;
    fn.tex    = ReadTextureBlock(r);         // this+1  Tex_LoadCompressedFromHandle 0x6a9cf0
    fn.anim   = ReadAnimTrack(r);            // this+14 Anim_LoadQuatTrackFromHandle 0x6aae20
    fn.fields = ReadBlob(r, 144);            // [runtime +72, +216): raw blob (parser unchanged)
    fn.present = true;

    // Typed view decoded from `fields` (offset = runtime - 72). The 144 bytes are read in the
    // exact order of Fx_NodeLoadFromHandle 0x6a69f0; only fields with a coherent semantics are
    // exposed (the tail +132.. stays in `fields`, cf. WorldChunk.h).
    const uint8_t* f = fn.fields.data();
    auto rf = [f](size_t off) { float v; std::memcpy(&v, f + off, 4); return v; };
    auto ru = [f](size_t off) { uint32_t v; std::memcpy(&v, f + off, 4); return v; };
    fn.lifetime     = rf(0);    // +72
    fn.kfFps        = rf(4);    // +76
    fn.rate         = rf(8);    // +80
    fn.shape        = ru(12);   // +84
    fn.speed        = rf(16);   // +88
    fn.box[0] = rf(20); fn.box[1] = rf(24); fn.box[2] = rf(28);          // +92
    fn.particleLife = rf(32);   // +104
    fn.minRange[0] = rf(36); fn.minRange[1] = rf(40); fn.minRange[2] = rf(44); // +108
    fn.maxRange[0] = rf(48); fn.maxRange[1] = rf(52); fn.maxRange[2] = rf(56); // +120
    fn.accelMin[0] = rf(100); fn.accelMin[1] = rf(104); fn.accelMin[2] = rf(108); // +172
    fn.accelMax[0] = rf(112); fn.accelMax[1] = rf(116); fn.accelMax[2] = rf(120); // +184
    return fn;
}

// ---- full-file parsers --------------------------------------------

// parse_WM (and .WJ): a single GXD block = collision mesh, then EOF.
MapCollisionChunk ParseWM(ByteReader& r) {
    MapCollisionChunk out;
    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> buf = ReadGxdBlock(r, raw, packed);
    out.mesh       = ParseCollisionMesh(std::move(buf));
    out.rawSize    = raw;
    out.packedSize = packed;
    if (!r.Eof())
        throw AssetError("WM/WJ: bytes remaining after the GXD block");
    return out;
}

// parse_WG: GXD geometry block + [numMat u32] + numMat textures + index table.
MapFaceChunk ParseWG(ByteReader& r) {
    MapFaceChunk out;
    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> buf = ReadGxdBlock(r, raw, packed);
    out.mesh      = ParseCollisionMesh(std::move(buf));
    out.geoRaw    = raw;
    out.geoPacked = packed;

    const uint32_t numMat = r.U32();         // this+3
    out.textures.reserve(numMat);
    for (uint32_t i = 0; i < numMat; ++i)
        out.textures.push_back(ReadTextureBlock(r));
    out.materialIndices.reserve(numMat);     // this+36: material index table
    for (uint32_t i = 0; i < numMat; ++i)
        out.materialIndices.push_back(r.U32());
    out.numMaterials = numMat;

    if (!r.Eof())
        throw AssetError("WG: bytes remaining at EOF");
    return out;
}

// Extracts the NUL-terminated ASCII/MBCS name at the head of a 100-byte placements[] record
// (the rest is padding not read by the engine, cf. Docs/TS2_WO_PLACEMENT_FORMAT.md).
std::string ExtractPlacementName(const uint8_t* rec, size_t recSize) {
    const void* nul = std::memchr(rec, '\0', recSize);
    const size_t len = nul ? (static_cast<const uint8_t*>(nul) - rec) : recSize;
    return std::string(reinterpret_cast<const char*>(rec), len);
}

// Decodes the placed-instance array (28 bytes/instance on disk, format confirmed by
// disassembly: see AuxRecord in WorldChunk.h and Docs/TS2_WO_PLACEMENT_FORMAT.md).
std::vector<AuxRecord> ReadAuxRecords(ByteReader& r, uint32_t numAux) {
    std::vector<AuxRecord> out;
    out.reserve(numAux);
    for (uint32_t i = 0; i < numAux; ++i) {
        AuxRecord rec;
        rec.modelIndex = r.U32();
        rec.pos[0] = r.F32(); rec.pos[1] = r.F32(); rec.pos[2] = r.F32();
        rec.rot[0] = r.F32(); rec.rot[1] = r.F32(); rec.rot[2] = r.F32();
        out.push_back(rec);
    }
    return out;
}

// parse_WO: [numModels u32] + models + placements + [numAux u32] + aux records.
ObjectChunk ParseWO(ByteReader& r) {
    ObjectChunk out;
    const uint32_t numModels = r.U32();      // this+23
    if (numModels == 0) {
        out.empty = true;
        if (!r.Eof()) throw AssetError("WO: data after numModels=0");
        return out;
    }
    out.models.reserve(numModels);
    for (uint32_t i = 0; i < numModels; ++i)
        out.models.push_back(ReadModel(r));
    out.placements = ReadBlob(r, 100ull * numModels); // this+25: 100 bytes/model (template metadata)
    out.placementNames.reserve(numModels);
    for (uint32_t i = 0; i < numModels; ++i)
        out.placementNames.push_back(
            ExtractPlacementName(out.placements.data() + 100ull * i, 100));
    const uint32_t numAux = r.U32();                  // this+26
    out.auxRecords = ReadAuxRecords(r, numAux);       // THE PLACEMENT: 28 bytes/instance (modelIndex+pos+rot)
    out.numAux = numAux;
    if (!r.Eof())
        throw AssetError("WO: bytes remaining at EOF");
    return out;
}

// Decodes placed FX instances (28 bytes/instance: nodeIndex u32 + pos 12b + rot 12b).
// IDA anchor: MapColl_LoadObjectsB 0x6983b0 @0x698602 (ReadFile +0/4, +4/12, +16/12).
std::vector<AuxFxRecord> ReadAuxFxRecords(ByteReader& r, uint32_t numFxb) {
    std::vector<AuxFxRecord> out;
    out.reserve(numFxb);
    for (uint32_t i = 0; i < numFxb; ++i) {
        AuxFxRecord rec;
        rec.nodeIndex = r.U32();                                        // +0
        rec.pos[0] = r.F32(); rec.pos[1] = r.F32(); rec.pos[2] = r.F32(); // +4
        rec.rot[0] = r.F32(); rec.rot[1] = r.F32(); rec.rot[2] = r.F32(); // +16
        out.push_back(rec);
    }
    return out;
}

// parse_WP: [numFx u32] + FX nodes + placements + [numFxb u32] + B records (placed instances).
FxChunk ParseWP(ByteReader& r) {
    FxChunk out;
    const uint32_t numFx = r.U32();          // this+28
    if (numFx == 0) {
        out.empty = true;
        if (!r.Eof()) throw AssetError("WP: data after numFx=0");
        return out;
    }
    out.nodes.reserve(numFx);
    for (uint32_t i = 0; i < numFx; ++i)
        out.nodes.push_back(ReadFxNode(r));
    out.placements = ReadBlob(r, 100ull * numFx);     // this+30: 100 bytes/fx
    const uint32_t numFxb = r.U32();                  // this+31
    out.fxbRecords = ReadAuxFxRecords(r, numFxb);     // 28 bytes on disk (nodeIndex+pos+rot)
    out.numFxb = numFxb;
    if (!r.Eof())
        throw AssetError("WP: bytes remaining at EOF");
    return out;
}

// read_meshpart_B: reproduces ONE part of cMesh_ReadFromStream 0x436CA0 (Format B). Returns
// present==false when the head flag is 0 (end of walker). RULE #4: the geometry block is
// PURE zlib (ReadGxdBlock), never XTEA/GXCW. Textures = ReadTextureBlock (framing identical
// to Tex_ReadPacked 0x417740, proven).
MeshFormatBPart ReadMeshFormatBPart(ByteReader& r) {
    MeshFormatBPart mp;
    const uint32_t present = r.U32();        // a1+188
    if (present == 0) return mp;             // end of walker

    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> heap = ReadGxdBlock(r, raw, packed); // [rawSize][packedSize][zlib]
    if (heap.size() < 176)
        throw AssetError("meshB: geometry block too short (<176)");
    std::memcpy(mp.header, heap.data() + 0, 136);        // qmemcpy(a1+192, Heap, 0x88)
    std::memcpy(mp.subHeader, heap.data() + 136, 40);    // qmemcpy(a1+144, Heap+136, 0x28)
    std::memcpy(&mp.numVerts, mp.header + 120, 4);       // a1+312
    std::memcpy(&mp.C,        mp.header + 124, 4);       // a1+316
    std::memcpy(&mp.numFaces, mp.header + 132, 4);       // a1+324

    const size_t vbBytes = 32ull * mp.numVerts;
    const size_t ibBytes = 6ull * mp.numFaces;
    // Heap+176 = stream 0; Heap+176+32*B = stream 1; then 6*D bytes of indices.
    if (heap.size() < 176 + 2 * vbBytes + ibBytes)
        throw AssetError("meshB: geometry block inconsistent (streams/indices out of bounds)");
    const uint8_t* p = heap.data() + 176;
    mp.vb0.assign(p, p + vbBytes);          p += vbBytes;   // a1+348 (Crt_Memcpy Heap+176)
    mp.vb1.assign(p, p + vbBytes);          p += vbBytes;   // a1+352 (Heap+176+32*B)
    mp.ib.assign(p, p + ibBytes);                            // a1+356 (6*D)

    mp.tex1 = ReadTextureBlock(r);          // Tex_ReadPacked(a1+368)
    mp.tex2 = ReadTextureBlock(r);          // Tex_ReadPacked(a1+424)
    const uint32_t numMat = r.U32();        // a1+480
    mp.materials.reserve(numMat);
    for (uint32_t i = 0; i < numMat; ++i)
        mp.materials.push_back(ReadTextureBlock(r)); // a1+484 (56 bytes runtime each)

    mp.present = true;
    return mp;
}

// parse_SOBJECT_B: multi-part walker. Loops ReadMeshFormatBPart while present != 0
// (each part is self-delimited by its own head flag). IDA anchor: cMesh_ReadFromStream
// 0x436CA0 called in a loop by the original client's caller.
MeshFormatBChunk ParseMeshFormatB(ByteReader& r) {
    MeshFormatBChunk out;
    for (;;) {
        MeshFormatBPart part = ReadMeshFormatBPart(r);
        if (!part.present) break;   // flag 0 -> no more parts
        out.parts.push_back(std::move(part));
        if (r.Eof()) break;         // stream exhausted (the final flag-0 marker can be absent at EOF)
    }
    return out;
}

} // namespace (anonymous)

// ---- public API -----------------------------------------------------------

void WorldChunk::Reset() {
    type_ = WorldChunkType::Unknown;
    collision_.reset();
    face_.reset();
    objects_.reset();
    fx_.reset();
    meshB_.reset();
}

bool WorldChunk::Load(const std::string& path) {
    const WorldChunkType t = WorldChunkTypeFromExtension(path);
    if (t == WorldChunkType::Unknown) {
        TS2_ERR("WorldChunk : extension inconnue : %s", path.c_str());
        return false;
    }
    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data)) {
        TS2_ERR("WorldChunk : ouverture impossible : %s", path.c_str());
        return false;
    }
    return LoadFromMemory(data, t);
}

bool WorldChunk::LoadFromMemory(const std::vector<uint8_t>& data, WorldChunkType type) {
    Reset();
    type_ = type;
    try {
        ByteReader r(data);
        switch (type) {
            case WorldChunkType::WM:
            case WorldChunkType::WJ:
                collision_ = ParseWM(r);
                break;
            case WorldChunkType::WG:
                face_ = ParseWG(r);
                break;
            case WorldChunkType::WO:
                objects_ = ParseWO(r);
                break;
            case WorldChunkType::WP:
                fx_ = ParseWP(r);
                break;
            case WorldChunkType::SOBJECT_B:
                meshB_ = ParseMeshFormatB(r);
                break;
            default:
                TS2_ERR("WorldChunk : type non supporté");
                Reset();
                return false;
        }
    } catch (const std::exception& ex) {
        TS2_ERR("WorldChunk : parsing échoué (%s)", ex.what());
        Reset();
        return false;
    }
    return true;
}

std::string WorldChunk::Describe() const {
    char buf[256] = {0};
    switch (type_) {
        case WorldChunkType::WM:
        case WorldChunkType::WJ:
            if (collision_)
                std::snprintf(buf, sizeof(buf),
                    "%s numTri=%u numNodes=%u field34=%u totalIndices=%u raw=%u packed=%u",
                    WorldChunkTypeName(type_), collision_->mesh.numTri,
                    collision_->mesh.numNodes, collision_->mesh.field34,
                    collision_->mesh.totalIndices, collision_->rawSize, collision_->packedSize);
            break;
        case WorldChunkType::WG:
            if (face_) {
                uint32_t texOk = 0, texEmpty = 0;
                for (const auto& t : face_->textures) { if (t.present) ++texOk; else ++texEmpty; }
                std::snprintf(buf, sizeof(buf),
                    "WG numTri=%u numNodes=%u numMaterials=%u texturesOk=%u texturesEmpty=%u",
                    face_->mesh.numTri, face_->mesh.numNodes, face_->numMaterials, texOk, texEmpty);
            }
            break;
        case WorldChunkType::WO:
            if (objects_) {
                uint32_t totalParts = 0;
                for (const auto& m : objects_->models) totalParts += static_cast<uint32_t>(m.parts.size());
                std::snprintf(buf, sizeof(buf),
                    "WO numModels=%u totalParts=%u numAux=%u%s",
                    static_cast<uint32_t>(objects_->models.size()), totalParts,
                    objects_->numAux, objects_->empty ? " (empty)" : "");
            }
            break;
        case WorldChunkType::WP:
            if (fx_) {
                uint32_t withDds = 0;
                for (const auto& n : fx_->nodes) if (n.tex.present) ++withDds;
                std::snprintf(buf, sizeof(buf),
                    "WP numFx=%u fxWithDds=%u numFxb=%u%s",
                    static_cast<uint32_t>(fx_->nodes.size()), withDds,
                    fx_->numFxb, fx_->empty ? " (empty)" : "");
            }
            break;
        case WorldChunkType::SOBJECT_B:
            if (meshB_) {
                uint32_t totalVerts = 0, totalFaces = 0;
                for (const auto& p : meshB_->parts) { totalVerts += p.numVerts; totalFaces += p.numFaces; }
                std::snprintf(buf, sizeof(buf),
                    "SOBJECT_B parts=%u totalVerts=%u totalFaces=%u",
                    static_cast<uint32_t>(meshB_->parts.size()), totalVerts, totalFaces);
            }
            break;
        default:
            std::snprintf(buf, sizeof(buf), "WorldChunk vide/inconnu");
            break;
    }
    return std::string(buf);
}

WorldChunkType WorldChunkTypeFromExtension(const std::string& path) {
    // Extracts the extension (after the last '.'), uppercased.
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return WorldChunkType::Unknown;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext)
        c = static_cast<char>((c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c);
    if (ext == "WM") return WorldChunkType::WM;
    if (ext == "WJ") return WorldChunkType::WJ;
    if (ext == "WG") return WorldChunkType::WG;
    if (ext == "WO") return WorldChunkType::WO;
    if (ext == "WP") return WorldChunkType::WP;
    return WorldChunkType::Unknown;
}

const char* WorldChunkTypeName(WorldChunkType t) {
    switch (t) {
        case WorldChunkType::WM: return "WM";
        case WorldChunkType::WJ: return "WJ";
        case WorldChunkType::WG: return "WG";
        case WorldChunkType::WO: return "WO";
        case WorldChunkType::WP: return "WP";
        case WorldChunkType::SOBJECT_B: return "SOBJECT_B";
        default:                 return "?";
    }
}

} // namespace ts2::asset

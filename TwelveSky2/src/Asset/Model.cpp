// Asset/Model.cpp — faithful translation of RE/asset_parsers/sobject.py + mobject.py.
#include "Asset/Model.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <cstring>

namespace ts2::asset {

// ---------------------------------------------------------------------
//  Envelope helpers
// ---------------------------------------------------------------------

// Reads the generic envelope [rawSize:u32][packedSize:u32][zlib stream] at the
// current position and returns the decompressed bytes (read_entity /
// GXD_DecompressEntity 0x6A1A30). Advances the cursor past the zlib stream.
static std::vector<uint8_t> ReadEntity(ByteReader& r, uint32_t& raw, uint32_t& packed) {
    raw    = r.U32();
    packed = r.U32();
    if (r.Remaining() < packed)
        throw AssetError("enveloppe zlib : packedSize depasse le fichier");
    std::vector<uint8_t> out = Zlib::Instance().InflateTo(r.Ptr(), packed, raw);
    r.Skip(packed);
    return out;
}

// =====================================================================
//  .SOBJECT
// =====================================================================

// Texture block of a mesh (Tex_ReadFromMemory 0x417D20). The raw zlib stream is KEPT as-is
// (the walker doesn't need it, and MeshRenderer::createDiffuse re-inflates it at GPU upload).
// ex-VeryOldClient: TEXTURE_FOR_GXD.Load (2 trailing u32 = processMode/alphaMode).
//
// SOBJ-02 (Pass 4 / W7, sobject-material front): the 8-byte trailer was DISCARDED -> blendMode
// stuck at 0 (opaque) for all meshes. Tex_ReadPacked 0x417740 decompresses the block THEN reads
// the 8 trailing bytes (a1[10]/a1[11] @0x4178d6/@0x4178dd, cf. SObjectTexture banner in Model.h).
// We reproduce that decoding here: this is the ONLY place where the asset sees the decompressed
// block, and it's the texture struct itself that the binary fills — filling the field elsewhere
// (at GPU upload time) would leave these Asset fields dead.
//
// ACCEPTED COST: one inflate per texture at parse time (the binary also inflates at load,
// @0x417872), plus a second one at GPU upload (createDiffuse). Models are loaded once and
// cached (Gfx/ModelCache), so this duplication stays off the per-frame path.
static void WalkTexture(ByteReader& b, SObjectTexture& t) {
    t.ddsSize = b.U32();          // a2[1]: 0 => absent
    if (t.ddsSize == 0) {
        t.present = false;
        return;
    }
    t.present    = true;
    t.rawSize    = b.U32();       // DDS + 8
    t.packedSize = b.U32();       // zlib bytes
    t.compressed.resize(t.packedSize);
    if (t.packedSize) b.Read(t.compressed.data(), t.packedSize);

    // Trailer: decompressed block = [DDS: ddsSize][u32 processMode][u32 alphaMode], rawSize==ddsSize+8
    // (Tex_ReadPacked 0x417740: lpMema = a1[1] = ddsSize @0x41788e, reads @0x4178d6/@0x4178dd).
    // Inflate into a TEMPORARY buffer (not retained: only the 8 trailing bytes matter here).
    // Non-fatal: without a readable trailer, we leave trailerDecoded=false and alphaMode=0 (opaque) —
    // i.e. exactly the behavior before this fix, never worse.
    // Bounds computed BY SUBTRACTION (never `ddsSize + 8`, which would overflow on a corrupted
    // u32 and let an out-of-buffer read slip through a few lines below).
    if (t.packedSize == 0 || t.ddsSize > t.rawSize || t.rawSize - t.ddsSize < 8u) return;
    Zlib& zlib = Zlib::Instance();
    if (!zlib.Available()) return;
    std::vector<uint8_t> block(t.rawSize);
    if (!zlib.Inflate(t.compressed.data(), t.compressed.size(), block.data(), t.rawSize)) return;
    std::memcpy(&t.processMode, block.data() + t.ddsSize,     4); // a1[10] -> material +40
    std::memcpy(&t.alphaMode,   block.data() + t.ddsSize + 4, 4); // a1[11] -> material +44
    t.trailerDecoded = true;
}

// Mesh reader (Mesh_ReadFromMemory 0x40C380).
// ex-VeryOldClient: SKIN2_FOR_GXD (subset strides 76/6/32/6/6); the VeryOld
// SKIN_FOR_GXD/SKIN3 header has a DIFFERENT layout — IDA wins (opaque 372-byte header).
static void WalkMesh(ByteReader& b, SObjectMesh& m, uint32_t index) {
    m.index  = index;
    m.field0 = b.U32();           // a1[0]: type/flag
    if (m.field0 == 0) {          // empty mesh
        m.empty = true;
        return;
    }
    m.empty = false;

    // Fixed header: 68 (0x44) + 304 (0x130) then subsetCount (u32) = 376 bytes.
    m.header.resize(SObjectMesh::kHeaderSize);        // 372 bytes
    b.Read(m.header.data(), SObjectMesh::kHeaderSize);
    m.subsetCount = b.U32();

    m.subsets.resize(m.subsetCount);
    for (uint32_t si = 0; si < m.subsetCount; ++si) {
        SObjectSubset& s = m.subsets[si];
        s.vertexCount = b.U32();                       // a1[171]
        s.vertexBuffer.resize(SObjectSubset::kVertexStride * s.vertexCount); // 76/vtx
        if (!s.vertexBuffer.empty()) b.Read(s.vertexBuffer.data(), s.vertexBuffer.size());

        s.faceCount = b.U32();                         // a1[173]
        s.indexBuffer.resize(SObjectSubset::kFaceStride * s.faceCount);      // 6/face
        if (!s.indexBuffer.empty()) b.Read(s.indexBuffer.data(), s.indexBuffer.size());

        s.skin.resize(SObjectSubset::kSkinStride * s.vertexCount);           // 32/vtx
        if (!s.skin.empty()) b.Read(s.skin.data(), s.skin.size());

        s.indexCopy1.resize(SObjectSubset::kFaceStride * s.faceCount);       // copy #1
        if (!s.indexCopy1.empty()) b.Read(s.indexCopy1.data(), s.indexCopy1.size());

        s.indexCopy2.resize(SObjectSubset::kFaceStride * s.faceCount);       // copy #2
        if (!s.indexCopy2.empty()) b.Read(s.indexCopy2.data(), s.indexCopy2.size());
    }

    // 3 fixed textures (diffuse, ~normal, ~specular/emissive).
    for (int k = 0; k < 3; ++k)
        WalkTexture(b, m.tex[k]);

    // Array of additional materials/textures (a1[220]).
    m.extraCount = b.U32();
    m.extra.resize(m.extraCount);
    for (uint32_t k = 0; k < m.extraCount; ++k)
        WalkTexture(b, m.extra[k]);
}

// Format A (Model_ReadSubHeader 0x40E8E0 -> Model_LoadFromPak 0x40EA30).
// ex-VeryOldClient: SOBJECT3_FOR_GXD.cpp — crypto CONFLICT (IDA wins): header `01 01 00 00`
// read IN THE CLEAR then ONE zlib stream; the VeryOld build's GXCW/XXTEA is ABSENT (cf. Rosetta §4.B).
bool SObject::parseFormatA(const std::vector<uint8_t>& data, const std::string& path) {
    format_ = Format::SObjectA;
    ByteReader r(data);
    r.Skip(7);                    // magic "SOBJECT"
    version_    = static_cast<char>(r.U8()); // '2' | '3'
    subType_    = r.U8();         // Model_ReadSubHeader: requires ==1
    subVer_     = r.U8();         // ==1 => compressed body
    pad_        = r.U16();
    rawSize_    = r.U32();
    packedSize_ = r.U32();
    const size_t header = 20;
    envOk_ = (header + packedSize_ == data.size());

    if (subVer_ != 1) {
        error_ = "SOBJECT subVer!=1 (corps non compresse) : non gere";
        TS2_WARN("SOBJECT : %s : %s", error_.c_str(), path.c_str());
        return false;
    }
    if (r.Remaining() < packedSize_) {
        error_ = "SOBJECT : packedSize depasse le fichier";
        TS2_ERR("SOBJECT : %s : %s", error_.c_str(), path.c_str());
        return false;
    }

    // A SINGLE zlib stream -> the whole model decompressed (rawSize bytes).
    std::vector<uint8_t> body = Zlib::Instance().InflateTo(r.Ptr(), packedSize_, rawSize_);
    inflateOk_ = true; // InflateTo throws if size != rawSize

    // Byte-by-byte walk of the body.
    ByteReader b(body);
    meshCount_ = b.U32();
    meshes_.resize(meshCount_);
    for (uint32_t mi = 0; mi < meshCount_; ++mi)
        WalkMesh(b, meshes_[mi], mi);

    walkOk_ = (b.Pos() == rawSize_);
    if (!walkOk_)
        TS2_WARN("SOBJECT : curseur final %zu != rawSize %u : %s",
                 b.Pos(), rawSize_, path.c_str());
    return true;
}

bool SObject::parseFormatB(const std::vector<uint8_t>& data, const std::string& path) {
    format_ = Format::RawB;
    ByteReader r(data);
    rawBMeshCount_ = r.U32();

    // The validated parser proves ONLY the 1st mesh's zlib envelope
    // (internal cMesh_ReadFromStream format undecoded) -> light validation.
    if (rawBMeshCount_ >= 1 && r.Remaining() >= 4) {
        SObjectRawMesh rm;
        rm.flag = r.U32();
        if (rm.flag != 0) {
            uint32_t raw = 0, packed = 0;
            try {
                rm.decompressed = ReadEntity(r, raw, packed);
                rm.rawSize   = raw;
                rm.packedSize = packed;
                rm.inflateOk = (rm.decompressed.size() == raw);
            } catch (const std::exception& ex) {
                rm.inflateOk = false;
                TS2_WARN("SOBJECT(B) : 1er mesh non decode (%s) : %s", ex.what(), path.c_str());
            }
        }
        rawBMeshes_.push_back(std::move(rm));
    }
    return true;
}

bool SObject::Load(const std::string& path) {
    *this = SObject{}; // reset to zero

    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data)) {
        error_ = "ouverture impossible";
        TS2_ERR("SOBJECT : %s : %s", error_.c_str(), path.c_str());
        return false;
    }
    try {
        if (data.size() >= 7 && std::memcmp(data.data(), "SOBJECT", 7) == 0)
            return parseFormatA(data, path);
        return parseFormatB(data, path);
    } catch (const std::exception& ex) {
        error_ = ex.what();
        TS2_ERR("SOBJECT : parse echoue (%s) : %s", ex.what(), path.c_str());
        return false;
    }
}

// =====================================================================
//  .MOBJECT
// =====================================================================

// Decodes a compressed texture block (Tex_LoadCompressedFromHandle 0x6A9CF0).
// ex-VeryOldClient: TEXTURE_FOR_GXD/CTEXTURE (8-byte trailer = processMode/alphaMode).
static MTexture ReadMTexture(ByteReader& r) {
    MTexture t;
    t.imgSize = r.U32();
    if (t.imgSize == 0) {          // texture absent
        t.present = false;
        return t;
    }
    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> data = ReadEntity(r, raw, packed); // image + 8-byte trailer
    if (raw != t.imgSize + 8)
        throw AssetError("MOBJECT texture : rawSize != imgSize+8");
    t.present    = true;
    t.rawSize    = raw;
    t.packedSize = packed;
    t.image      = std::move(data);
    if (t.image.size() >= 4)
        std::memcpy(t.magic, t.image.data(), 4);
    if (t.image.size() >= static_cast<size_t>(t.imgSize) + 8) {
        std::memcpy(&t.trailer0, t.image.data() + t.imgSize,     4);
        std::memcpy(&t.trailer1, t.image.data() + t.imgSize + 4, 4);
    }
    return t;
}

// Decodes the decompressed geometry blob of a part (MeshPart_Load 0x6AD160 / parse_geometry).
// ex-VeryOldClient: MESHVERTEX_FOR_GXD (32-byte vertex = mV3 pos + mN3 normal + mT2 uv).
static void ParseGeometry(const std::vector<uint8_t>& blob, MGeometry& g) {
    const size_t off0 = MGeometry::kHeaderSize;   // 0x78
    const size_t off1 = off0 + 16;                // 0x88 (after M,V,X,I)
    if (blob.size() < off1) {
        g.sizeOk = false;
        return;
    }
    g.header.assign(blob.begin(), blob.begin() + off0);
    std::memcpy(&g.M, blob.data() + off0 + 0,  4);  // Heap[30]
    std::memcpy(&g.V, blob.data() + off0 + 4,  4);  // Heap[31]
    std::memcpy(&g.X, blob.data() + off0 + 8,  4);  // Heap[32]
    std::memcpy(&g.I, blob.data() + off0 + 12, 4);  // Heap[33]

    const size_t mats = static_cast<size_t>(g.M) * MGeometry::kMatrixStride;         // M*64
    const size_t vtx  = MGeometry::kVertexStride * static_cast<size_t>(g.M) * g.V;   // 32*M*V
    const size_t idx  = MGeometry::kFaceStride * static_cast<size_t>(g.I);           // 6*I
    const size_t total = off1 + mats + vtx + idx;
    g.sizeOk = (total == blob.size());

    // Bounded slicing (validated files land exactly; we guard regardless).
    size_t p = off1;
    auto slice = [&](size_t n) -> std::vector<uint8_t> {
        size_t avail = (p <= blob.size()) ? (blob.size() - p) : 0;
        size_t take  = n < avail ? n : avail;
        std::vector<uint8_t> v(blob.begin() + p, blob.begin() + p + take);
        p += take;
        return v;
    };
    g.matrices = slice(mats);
    g.vertices = slice(vtx);
    g.indices  = slice(idx);
}

// Decodes the 120-byte material header (MGeometry::header = Heap[0..29]) into named fields.
// ADDITIVE: modifies neither `header` nor any existing field — pure reinterpretation.
// SHARED .MOBJECT / .WO: declared in Model.h, reused by WorldMeshPart (header is
// BYTE-IDENTICAL, same loader MeshPart_Load 0x6AD160).
//
// Cross-proven write <-> read mapping:
//   MeshPart_Load 0x6AD160: `qmemcpy((void*)(this+132), Heap, 0x78)` @0x6ad2d1
//     -> header dword `k` = Heap[k] = part dword [33+k] = part offset (132 + 4*k).
//   MeshPart_RenderFull 0x6B0850: each field carries the anchor of the site that READS it.
// Floats are read as-is via memcpy (no aliasing, no value invented).
void DecodeMeshPartMaterialHeader(const std::vector<uint8_t>& header, MeshPartMaterial& m) {
    if (header.size() < MGeometry::kHeaderSize) {   // 0x78 = 30 dwords
        m.decoded = false;
        return;
    }
    const uint8_t* h = header.data();
    auto u32 = [h](size_t dword) -> uint32_t {
        uint32_t v; std::memcpy(&v, h + 4 * dword, 4); return v;
    };
    auto f32 = [h](size_t dword) -> float {
        float v; std::memcpy(&v, h + 4 * dword, 4); return v;
    };

    m.subCount = u32(0);                       // header[0]: opaque, not read at render time

    m.lightAnim.Enable = u32(1);               // @0x6b087d
    m.lightAnim.Speed  = f32(2);               // @0x6b08bb (v66 * this[35])
    for (int i = 0; i < 8; ++i)
        m.lightAnim.Pairs[i] = f32(3 + i);     // header[3..10] @0x6b08c5 ([0..3]=from, [4..7]=to)

    m.noLight = u32(11);                       // @0x6b099b

    m.glow.Enable = u32(12);                   // @0x6b0a11
    m.glow.Mode   = u32(13);                   // @0x6b0a1f (1=constant, 2=view-dependent)
    for (int i = 0; i < 4; ++i)
        m.glow.SpecRGBA[i] = f32(14 + i);      // header[14..17] @0x6b0a48 (D3DMATERIAL9.Specular)
    m.glow.SpecPower = f32(18);                // @0x6b0a34 (D3DMATERIAL9.Power)

    m.lightOffset = f32(19);                   // @0x6b0a59 (Gfx_SetShadowProjLight this[52])

    m.flipbook.Enable = u32(20);               // @0x6b0d33
    m.flipbook.Fps    = f32(21);               // @0x6b0d78 (Crt_Dbl2Uint(v66 * this[54]))

    m.uvScroll.tex1.Enable = u32(22);          // @0x6b0f59
    m.uvScroll.tex1.Mode   = u32(23);          // @0x6b0f73 (switch 1..4)
    m.uvScroll.tex1.Speed  = f32(24);          // @0x6b1016 (v66 * this[57])

    m.billboard.Enable = u32(25);              // @0x6b107c
    m.billboard.Mode   = u32(26);              // @0x6b11b0 (1=screen-aligned / other=free axis)

    m.uvScroll.tex2.Enable = u32(27);          // @0x6b19bb
    m.uvScroll.tex2.Mode   = u32(28);          // @0x6b19d5 (switch 1..4)
    m.uvScroll.tex2.Speed  = f32(29);          // @0x6b1a78 (v66 * this[62])

    m.decoded = true;
}

bool MObject::Load(const std::string& path) {
    *this = MObject{}; // reset to zero

    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data)) {
        error_ = "ouverture impossible";
        TS2_ERR("MOBJECT : %s : %s", error_.c_str(), path.c_str());
        return false;
    }
    try {
        ByteReader r(data);
        partCount_ = r.U32();               // nPart
        parts_.reserve(partCount_);
        bool warned = false;
        for (uint32_t pi = 0; pi < partCount_; ++pi) {
            MeshPart p;
            p.index   = pi;
            p.hasMesh = (r.U32() != 0);     // flag
            if (!p.hasMesh) {
                parts_.push_back(std::move(p));
                continue;
            }
            std::vector<uint8_t> geo = ReadEntity(r, p.geoRaw, p.geoPacked);
            ParseGeometry(geo, p.geo);
            DecodeMeshPartMaterialHeader(p.geo.header, p.mat); // decodes the 120-byte header into named fields (ADDITIVE)
            if (!p.geo.sizeOk) {
                warned = true;
                TS2_WARN("MOBJECT : part %u : taille geometrie interne incoherente : %s",
                         pi, path.c_str());
            }
            p.tex0 = ReadMTexture(r);
            p.tex1 = ReadMTexture(r);
            p.matCount = r.U32();
            p.mats.reserve(p.matCount);
            for (uint32_t mi = 0; mi < p.matCount; ++mi)
                p.mats.push_back(ReadMTexture(r));
            parts_.push_back(std::move(p));
        }
        leftover_ = r.Remaining();
        ok_ = (leftover_ == 0) && !warned;
        if (leftover_ != 0)
            TS2_WARN("MOBJECT : %zu octets residuels apres parse : %s", leftover_, path.c_str());
        return true;
    } catch (const std::exception& ex) {
        error_ = ex.what();
        TS2_ERR("MOBJECT : parse echoue (%s) : %s", ex.what(), path.c_str());
        return false;
    }
}

} // namespace ts2::asset

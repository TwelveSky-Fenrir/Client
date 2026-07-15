// Asset/Model.cpp — traduction fidèle de RE/asset_parsers/sobject.py + mobject.py.
#include "Asset/Model.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <cstring>

namespace ts2::asset {

// ---------------------------------------------------------------------
//  Helpers d'enveloppe
// ---------------------------------------------------------------------

// Lit l'enveloppe générique [rawSize:u32][packedSize:u32][flux zlib] à la
// position courante et renvoie les octets décompressés (read_entity /
// GXD_DecompressEntity 0x6A1A30). Avance le curseur au-delà du flux zlib.
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

// Bloc texture d'un mesh (Tex_ReadFromMemory 0x417D20). Le flux zlib n'est
// PAS décompressé ici (fidèle au walker) : on conserve les octets bruts.
// ex-VeryOldClient: TEXTURE_FOR_GXD.Load (2 u32 finaux = processMode/alphaMode).
static void WalkTexture(ByteReader& b, SObjectTexture& t) {
    t.ddsSize = b.U32();          // a2[1] : 0 => absente
    if (t.ddsSize == 0) {
        t.present = false;
        return;
    }
    t.present    = true;
    t.rawSize    = b.U32();       // DDS + 8
    t.packedSize = b.U32();       // octets zlib
    t.compressed.resize(t.packedSize);
    if (t.packedSize) b.Read(t.compressed.data(), t.packedSize);
}

// Lecteur d'un mesh (Mesh_ReadFromMemory 0x40C380).
// ex-VeryOldClient: SKIN2_FOR_GXD (strides subset 76/6/32/6/6) ; en-tête VeryOld
// SKIN_FOR_GXD/SKIN3 a un layout DIFFÉRENT — IDA gagne (en-tête opaque 372 o).
static void WalkMesh(ByteReader& b, SObjectMesh& m, uint32_t index) {
    m.index  = index;
    m.field0 = b.U32();           // a1[0] : type/flag
    if (m.field0 == 0) {          // mesh vide
        m.empty = true;
        return;
    }
    m.empty = false;

    // En-tête fixe : 68 (0x44) + 304 (0x130) puis subsetCount (u32) = 376 o.
    m.header.resize(SObjectMesh::kHeaderSize);        // 372 o
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

        s.indexCopy1.resize(SObjectSubset::kFaceStride * s.faceCount);       // copie #1
        if (!s.indexCopy1.empty()) b.Read(s.indexCopy1.data(), s.indexCopy1.size());

        s.indexCopy2.resize(SObjectSubset::kFaceStride * s.faceCount);       // copie #2
        if (!s.indexCopy2.empty()) b.Read(s.indexCopy2.data(), s.indexCopy2.size());
    }

    // 3 textures fixes (diffuse, ~normale, ~specular/emissive).
    for (int k = 0; k < 3; ++k)
        WalkTexture(b, m.tex[k]);

    // Tableau de matériaux/textures additionnels (a1[220]).
    m.extraCount = b.U32();
    m.extra.resize(m.extraCount);
    for (uint32_t k = 0; k < m.extraCount; ++k)
        WalkTexture(b, m.extra[k]);
}

// Format A (Model_ReadSubHeader 0x40E8E0 -> Model_LoadFromPak 0x40EA30).
// ex-VeryOldClient: SOBJECT3_FOR_GXD.cpp — CONFLICT crypto (IDA gagne) : en-tête `01 01 00 00`
// lu EN CLAIR puis UN flux zlib ; le GXCW/XXTEA du build VeryOld est ABSENT (cf. Rosetta §4.B).
bool SObject::parseFormatA(const std::vector<uint8_t>& data, const std::string& path) {
    format_ = Format::SObjectA;
    ByteReader r(data);
    r.Skip(7);                    // magic "SOBJECT"
    version_    = static_cast<char>(r.U8()); // '2' | '3'
    subType_    = r.U8();         // Model_ReadSubHeader : exige ==1
    subVer_     = r.U8();         // ==1 => corps compressé
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

    // UN SEUL flux zlib -> tout le modèle décompressé (rawSize octets).
    std::vector<uint8_t> body = Zlib::Instance().InflateTo(r.Ptr(), packedSize_, rawSize_);
    inflateOk_ = true; // InflateTo lève si taille != rawSize

    // Parcours octet par octet du corps.
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

    // Le parseur validé ne prouve QUE l'enveloppe zlib du 1er mesh
    // (format interne cMesh_ReadFromStream non décodé) -> validation légère.
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
    *this = SObject{}; // remise à zéro

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

// Décode un bloc texture compressé (Tex_LoadCompressedFromHandle 0x6A9CF0).
// ex-VeryOldClient: TEXTURE_FOR_GXD/CTEXTURE (trailer 8 o = processMode/alphaMode).
static MTexture ReadMTexture(ByteReader& r) {
    MTexture t;
    t.imgSize = r.U32();
    if (t.imgSize == 0) {          // texture absente
        t.present = false;
        return t;
    }
    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> data = ReadEntity(r, raw, packed); // image + 8 o trailer
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

// Décode le blob géométrie décompressé d'un part (MeshPart_Load 0x6AD160 / parse_geometry).
// ex-VeryOldClient: MESHVERTEX_FOR_GXD (sommet 32 o = mV3 pos + mN3 normale + mT2 uv).
static void ParseGeometry(const std::vector<uint8_t>& blob, MGeometry& g) {
    const size_t off0 = MGeometry::kHeaderSize;   // 0x78
    const size_t off1 = off0 + 16;                // 0x88 (après M,V,X,I)
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

    // Découpage borné (les fichiers validés tombent pile ; on protège malgré tout).
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

bool MObject::Load(const std::string& path) {
    *this = MObject{}; // remise à zéro

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

// Asset/Mobject2.cpp — faithful translation of the .MOBJECT2 container.
//   Mesh_LoadMOBJECT2 0x4318C0 (container) + Mesh_ReadFile 0x430470 (mesh 268 bytes) +
//   Tex_ReadPacked 0x417740 (SOBJECT texture 56 bytes).
// IDA ground truth (imagebase 0x400000), decompilation verified 2026-07-17. PARSER ONLY.
#include "Asset/Mobject2.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <cstring>

namespace ts2::asset {

// ---------------------------------------------------------------------
//  SOBJECT texture 56 bytes — Tex_ReadPacked 0x417740
// ---------------------------------------------------------------------
// Byte-exact on-disk layout (identical to asset::SObjectTexture / WalkTexture of Model.cpp):
//   u32 imgSize (a1[1] @0x41777d)   — 0 => texture absent (gate `if(a1[1])` @0x417783)
//   [ if imgSize != 0 : ]
//   u32 rawSize    (Buffer @0x41779e)               — decompressed = imgSize + 8
//   u32 packedSize (nNumberOfBytesToRead @0x4177e1) — zlib bytes
//   [packedSize bytes] -> GXD_DecompressBlock @0x417872 -> [image: imgSize][u32 procMode][u32 alphaMode]
// The two trailing u32 are stored at a1[10]=procMode @0x4178d6 / a1[11]=alphaMode @0x4178dd.
// Trailer decoding is NON-FATAL (same as in Model.cpp): without a readable zlib/trailer we leave
// trailerDecoded=false and alphaMode=0. Bounds computed BY SUBTRACTION (never imgSize+8, which
// would overflow on a corrupted u32).
static void ReadPackedTexture(ByteReader& b, SObjectTexture& t) {
    t.ddsSize = b.U32();                 // a1[1]: imgSize
    if (t.ddsSize == 0) {                // texture absent (Tex_ReadPacked returns 1, no further read)
        t.present = false;
        return;
    }
    t.present    = true;
    t.rawSize    = b.U32();              // decompressed (= imgSize + 8)
    t.packedSize = b.U32();              // zlib bytes
    t.compressed.resize(t.packedSize);
    if (t.packedSize) b.Read(t.compressed.data(), t.packedSize);

    // 8-byte trailer (procMode/alphaMode) — non-fatal decoding.
    if (t.packedSize == 0 || t.ddsSize > t.rawSize || t.rawSize - t.ddsSize < 8u) return;
    Zlib& zlib = Zlib::Instance();
    if (!zlib.Available()) return;
    std::vector<uint8_t> block(t.rawSize);
    if (!zlib.Inflate(t.compressed.data(), t.compressed.size(), block.data(), t.rawSize)) return;
    std::memcpy(&t.processMode, block.data() + t.ddsSize,     4); // a1[10]  @0x4178d6
    std::memcpy(&t.alphaMode,   block.data() + t.ddsSize + 4, 4); // a1[11]  @0x4178dd
    t.trailerDecoded = true;
}

// ---------------------------------------------------------------------
//  Mesh 268 bytes — Mesh_ReadFile 0x430470
// ---------------------------------------------------------------------
// NOTE: a1[1] = compression mode is written from parameter a2 (@0x4304ce), NOT read from disk —
// it only affects the CPU/GXD choice at runtime (BOTH paths consume the SAME on-disk bytes:
// 20*N*vertexCount for the VB, 6*faceCount for the IB). A pure parser thus does not consume it.
static void ParseMesh(ByteReader& r, Mobject2Mesh& m, uint32_t index) {
    m.index = index;
    m.type  = r.U32();                   // a1[0]              @0x4304a7
    if (m.type == 0) {                   // empty mesh -> return 1 @0x4304a9
        m.empty = true;
        return;
    }
    m.empty = false;

    // (a1[1] = mode: NOT read here — see banner above.)
    m.header1.resize(Mobject2Mesh::kHeader1Size);              // 76 bytes (a1[2..20])   @0x4304dc
    r.Read(m.header1.data(), m.header1.size());

    m.n = r.U32();                       // a1[21]             @0x4304fd
    const size_t boneBytes   = static_cast<size_t>(Mobject2Mesh::kBoneStride)   * m.n; // 40*N @0x43051d
    const size_t table4Bytes = static_cast<size_t>(Mobject2Mesh::kTable4Stride) * m.n; //  4*N @0x430573
    m.boneTable.resize(boneBytes);
    if (boneBytes) r.Read(m.boneTable.data(), boneBytes);
    m.table4.resize(table4Bytes);
    if (table4Bytes) r.Read(m.table4.data(), table4Bytes);

    m.header2.resize(Mobject2Mesh::kHeader2Size);             // 80 bytes (a1[24..43])  @0x4305c6
    r.Read(m.header2.data(), m.header2.size());

    m.subsetCount = r.U32();             // a1[44]             @0x4305ec
    m.subsets.resize(m.subsetCount);
    for (uint32_t si = 0; si < m.subsetCount; ++si) {
        Mobject2Subset& s = m.subsets[si];
        s.vertexCount = r.U32();         // a1[45][si]         @0x43079d
        // VB: 20 * N * vertexCount bytes (byte-exact @0x4307c1 / @0x430897). See Mesh::n banner.
        const size_t vbBytes = static_cast<size_t>(Mobject2Subset::kVertexStrideBase)
                             * static_cast<size_t>(m.n) * s.vertexCount;
        s.vertexBuffer.resize(vbBytes);
        if (vbBytes) r.Read(s.vertexBuffer.data(), vbBytes);

        s.faceCount = r.U32();           // a1[48][si]         @0x430928
        const size_t ibBytes = static_cast<size_t>(Mobject2Subset::kFaceStride) * s.faceCount; // 6*fc @0x430942
        s.indexBuffer.resize(ibBytes);
        if (ibBytes) r.Read(s.indexBuffer.data(), ibBytes);
    }

    ReadPackedTexture(r, m.tex);         // 1 SOBJECT texture (a1+51)   @0x430a80

    m.extraTexCount = r.U32();           // a1[65]             @0x430ab1
    // 0x430ab9 test eax,eax + 0x430abb jle: SIGNED comparison (<=0 => no
    // additional texture). A counter with the high bit set (signed negative) comes
    // out here the same as the binary does, instead of being treated as a huge unsigned (OOB).
    if (static_cast<int32_t>(m.extraTexCount) <= 0) // jle @0x430abb
        return;
    m.extraTex.resize(m.extraTexCount);
    for (uint32_t k = 0; k < m.extraTexCount; ++k) // 56 bytes each (a1[66])   @0x430b22
        ReadPackedTexture(r, m.extraTex[k]);
}

// ---------------------------------------------------------------------
//  Container — Mesh_LoadMOBJECT2 0x4318C0
// ---------------------------------------------------------------------
bool Mobject2::Parse(const uint8_t* data, size_t size, size_t offset) {
    *this = Mobject2{}; // reset to zero
    if (!data || offset > size) {
        error_ = "buffer/offset invalide";
        return false;
    }
    try {
        ByteReader r(data + offset, size - offset);

        // Magic "MOBJECT2" (8 bytes: 4D 4F 42 4A 45 43 54 32)   @0x43198f
        if (!r.PeekMagic("MOBJECT2", Mobject2::kMagicSize)) {
            error_ = "magic MOBJECT2 absent";
            bytesConsumed_ = 0;
            leftover_ = size - offset;
            return false;
        }
        r.Skip(Mobject2::kMagicSize);

        attribute_ = r.U32();            // lpBuffer[0]        @0x431995
        if (attribute_ == 0) {           // empty container -> return 1 @0x4319b0
            ok_            = true;
            bytesConsumed_ = r.Pos();
            leftover_      = r.Remaining();
            return true;
        }

        meshCount_ = r.U32();            // lpBuffer[1]        @0x4319c2
        meshes_.resize(meshCount_);
        for (uint32_t mi = 0; mi < meshCount_; ++mi) // array stride 268 @0x431a1b
            ParseMesh(r, meshes_[mi], mi);

        bytesConsumed_ = r.Pos();
        leftover_      = r.Remaining();
        ok_            = true;
        return true;
    } catch (const std::exception& ex) {
        error_ = ex.what();
        ok_    = false;
        return false;
    }
}

bool Mobject2::Load(const std::string& path) {
    std::vector<uint8_t> file;
    if (!ReadWholeFile(path, file)) {
        *this = Mobject2{};
        error_ = "ouverture impossible";
        TS2_ERR("MOBJECT2 : %s : %s", error_.c_str(), path.c_str());
        return false;
    }
    if (!Parse(file.data(), file.size(), 0)) {
        TS2_ERR("MOBJECT2 : parse echoue (%s) : %s", error_.c_str(), path.c_str());
        return false;
    }
    // Standalone file: the cursor should land exactly at the end (leftover==0). We warn otherwise,
    // without invalidating (a .MOBJECT2 could theoretically be followed by padding).
    if (leftover_ != 0)
        TS2_WARN("MOBJECT2 : %zu octets residuels apres parse : %s", leftover_, path.c_str());
    return true;
}

} // namespace ts2::asset

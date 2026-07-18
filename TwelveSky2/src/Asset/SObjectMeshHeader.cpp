// Asset/SObjectMeshHeader.cpp — implementation of the typed decoder (read-only).
//
// All positions are expressed as an offset INSIDE THE 372-BYTE BLOB `header`
// (== mesh+4..+376). Reminder: header[k] == mesh+(k+4). Integers/floats
// are little-endian (x86 target); reads use std::memcpy to avoid depending
// on any alignment of the blob (same policy as Asset/ByteReader).
//
// Anchors: see SObjectMeshHeader.h (0x40C3C9/0x40C3EC boundaries, 0x43ADD4
// bbox, 0x43AE1F attach points, 0x814A58 vertex declaration, 0x40CDC6
// flag usage).
#include "SObjectMeshHeader.h"
#include "Model.h" // SObjectMesh (convenience overload)

#include <cstring>

namespace ts2::asset {

namespace {
// Bounded raw reads, with no assumed alignment.
inline uint32_t ReadU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
inline float ReadF32(const uint8_t* p) {
    float v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}

// Offsets within the 372-byte blob (mesh - 4).
constexpr size_t kOffBlockAHead = 0;   // mesh+4  : group A (2 dwords) — RAW
constexpr size_t kOffAnimColor  = 8;   // mesh+12 : animColorFlag (u32)
constexpr size_t kOffGlowRaw    = 12;  // mesh+16 : glow curves (36 bytes) — RAW
constexpr size_t kOffBillboard  = 48;  // mesh+52 : billboardFlag (u32)
constexpr size_t kOffAxisMode   = 52;  // mesh+56 : billboardAxisMode (u32)
constexpr size_t kOffBbox       = 56;  // mesh+60 : bboxExtents[3] (3 floats)
constexpr size_t kOffAttach     = 68;  // mesh+72 : 4× GpuSkinVertex (block304)
} // namespace

bool SObjectMeshHeader::Decode(const uint8_t* header, size_t size) {
    decoded_ = false;
    if (header == nullptr || size != kHeaderSize)
        return false;

    // --- block56 (mesh+4..+60) ---
    // Group A head (mesh+4/+8): semantics partly proven (writer @0x43AC78),
    // kept raw to avoid inventing anything.
    std::memcpy(blockAHead_, header + kOffBlockAHead, kBlockAHeadSize);

    // animColorFlag (mesh+12): read ==1 by Model_DrawSkinnedSubset 0x40CA40 to
    // enable color/scale interpolation. Written by the group-B writer @0x43ACAD (v151).
    animColorFlag_ = ReadU32(header + kOffAnimColor);

    // Glow/pulse curves (mesh+16..+52): 9 dwords = ftol(100.*curve) written by
    // the group-B writer @0x43AC9B..0x43AD3E. Fine semantics undetermined statically
    // -> kept RAW (runtime TODO).
    std::memcpy(glowRaw_, header + kOffGlowRaw, kGlowRawSize);

    // billboardFlag (mesh+52): ==1 => the mesh is rendered as a camera-facing
    // screen quad (Model_DrawSkinnedSubset 0x40CDC6: cmp [ebx+34h],esi ; jnz non-billboard).
    billboardFlag_ = ReadU32(header + kOffBillboard);

    // billboardAxisMode (mesh+56): ==1 => symmetric square (flt_18C5264), otherwise
    // a w×h rectangle (unk_18C52BC). Model_DrawSkinnedSubset 0x40CDD2: cmp [ebx+38h],esi.
    billboardAxisMode_ = ReadU32(header + kOffAxisMode);

    // --- block12 (mesh+60..+72): bounding-box extents = max - min ---
    // The WRITER proves the semantics: v184[i] = bboxMax[i]-bboxMin[i] @0x43ADD4/
    // 0x43ADF0/0x43AE03 (bboxMin @authoring+144.., bboxMax @+156..). On the
    // billboard branch, [0]/[1] are reused as half-width/half-height
    // (fld [ebx+3Ch] @0x40CDCF, fld [ebx+40h] @0x40CDFF).
    bboxExtents_[0] = ReadF32(header + kOffBbox + 0);
    bboxExtents_[1] = ReadF32(header + kOffBbox + 4);
    bboxExtents_[2] = ReadF32(header + kOffBbox + 8);

    // --- block304 (mesh+72..+376): 4 attach points = 4× GpuSkinVertex 76 bytes ---
    // Confirmed by the WRITER (4× loop, WriteFile(&v165,0x4C=76) @0x43AE1F..0x43AF7D):
    // each record follows the exact stride/order of g_GxdSkinnedVertexDecl76
    // 0x814A58 (pos/weight4/packed bones/useful uv; tangent/binormal/normal=0).
    for (size_t i = 0; i < kAttachPointCount; ++i) {
        std::memcpy(&attach_[i], header + kOffAttach + i * kGpuSkinVertexStride,
                    kGpuSkinVertexStride);
    }

    decoded_ = true;
    return true;
}

bool SObjectMeshHeader::Decode(const SObjectMesh& mesh) {
    return Decode(mesh.header);
}

} // namespace ts2::asset

// Asset/SObjectMeshHeader.cpp — implémentation du décodeur typé (lecture seule).
//
// Toutes les positions sont exprimées en offset DANS LE BLOB 372 o `header`
// (== mesh+4..+376). Rappel : header[k] == mesh+(k+4). Les entiers/flottants
// sont little-endian (cible x86) ; on lit par std::memcpy pour ne dépendre
// d'aucun alignement du blob (même politique que Asset/ByteReader).
//
// Ancres : voir SObjectMeshHeader.h (0x40C3C9/0x40C3EC frontières, 0x43ADD4
// bbox, 0x43AE1F points d'attache, 0x814A58 déclaration de sommet, 0x40CDC6
// usage des flags).
#include "SObjectMeshHeader.h"
#include "Model.h" // SObjectMesh (surcharge de commodité)

#include <cstring>

namespace ts2::asset {

namespace {
// Lectures brutes bornées, sans alignement présumé.
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

// Offsets dans le blob 372 o (mesh - 4).
constexpr size_t kOffBlockAHead = 0;   // mesh+4  : groupe A (2 dwords) — RAW
constexpr size_t kOffAnimColor  = 8;   // mesh+12 : animColorFlag (u32)
constexpr size_t kOffGlowRaw    = 12;  // mesh+16 : courbes glow (36 o) — RAW
constexpr size_t kOffBillboard  = 48;  // mesh+52 : billboardFlag (u32)
constexpr size_t kOffAxisMode   = 52;  // mesh+56 : billboardAxisMode (u32)
constexpr size_t kOffBbox       = 56;  // mesh+60 : bboxExtents[3] (3 float)
constexpr size_t kOffAttach     = 68;  // mesh+72 : 4× GpuSkinVertex (block304)
} // namespace

bool SObjectMeshHeader::Decode(const uint8_t* header, size_t size) {
    decoded_ = false;
    if (header == nullptr || size != kHeaderSize)
        return false;

    // --- block56 (mesh+4..+60) ---
    // Tête de groupe A (mesh+4/+8) : sémantique semi-prouvée (writer @0x43AC78),
    // conservée brute pour ne rien inventer.
    std::memcpy(blockAHead_, header + kOffBlockAHead, kBlockAHeadSize);

    // animColorFlag (mesh+12) : lu ==1 par Model_DrawSkinnedSubset 0x40CA40 pour
    // activer l'interpolation couleur/échelle. Écrit par le writer @0x43ACAD (v151).
    animColorFlag_ = ReadU32(header + kOffAnimColor);

    // Courbes glow/pulse (mesh+16..+52) : 9 dwords = ftol(100.*courbe) écrits par
    // le writer groupe B @0x43AC9B..0x43AD3E. Sémantique fine indéterminable en
    // statique -> conservée BRUTE (TODO runtime).
    std::memcpy(glowRaw_, header + kOffGlowRaw, kGlowRawSize);

    // billboardFlag (mesh+52) : ==1 ⇒ le mesh est rendu comme quad écran orienté
    // caméra (Model_DrawSkinnedSubset 0x40CDC6 : cmp [ebx+34h],esi ; jnz non-billboard).
    billboardFlag_ = ReadU32(header + kOffBillboard);

    // billboardAxisMode (mesh+56) : ==1 ⇒ carré symétrique (flt_18C5264), sinon
    // rectangle w×h (unk_18C52BC). Model_DrawSkinnedSubset 0x40CDD2 : cmp [ebx+38h],esi.
    billboardAxisMode_ = ReadU32(header + kOffAxisMode);

    // --- block12 (mesh+60..+72) : extents de bounding-box = max - min ---
    // Le WRITER prouve la sémantique : v184[i] = bboxMax[i]-bboxMin[i] @0x43ADD4/
    // 0x43ADF0/0x43AE03 (bboxMin @authoring+144.., bboxMax @+156..). En branche
    // billboard, [0]/[1] sont réutilisés comme demi-largeur/demi-hauteur
    // (fld [ebx+3Ch] @0x40CDCF, fld [ebx+40h] @0x40CDFF).
    bboxExtents_[0] = ReadF32(header + kOffBbox + 0);
    bboxExtents_[1] = ReadF32(header + kOffBbox + 4);
    bboxExtents_[2] = ReadF32(header + kOffBbox + 8);

    // --- block304 (mesh+72..+376) : 4 points d'attache = 4× GpuSkinVertex 76 o ---
    // Confirmé par le WRITER (boucle 4×, WriteFile(&v165,0x4C=76) @0x43AE1F..0x43AF7D) :
    // chaque enregistrement reprend le stride/ordre exact de g_GxdSkinnedVertexDecl76
    // 0x814A58 (pos/poids4/os empaquetés/uv utiles ; tangente/binormale/normale=0).
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

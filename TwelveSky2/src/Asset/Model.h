// Asset/Model.h — lecteurs des modèles 3D de TwelveSky2.
//   .SOBJECT -> struct SObject  (modèle squelettique animé, tableau de meshes)
//   .MOBJECT -> struct MObject  (modèle statique GMOBJECT, tableau de "parts")
//
// Traduction fidèle des parseurs Python VALIDÉS :
//   RE/asset_parsers/sobject.py  (Format A "SOBJECT" validé 4910/4910 fichiers)
//   RE/asset_parsers/mobject.py  (Model_LoadFromFile 0x6A3490)
//
// Loaders d'origine :
//   SOBJECT A : Model_LoadFile 0x40E700 -> Model_LoadFromPak 0x40EA30
//               Mesh_ReadFromMemory 0x40C380, Tex_ReadFromMemory 0x417D20
//   SOBJECT B : cSObject 0x43D380 -> cMesh_ReadFromStream 0x436CA0 (compression par mesh)
//   MOBJECT   : Model_LoadFromFile 0x6A3490 -> MeshPart_Load 0x6AD160
//               Tex_LoadCompressedFromHandle 0x6A9CF0, GXD_DecompressEntity 0x6A1A30
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ts2::asset {

// =====================================================================
//  .SOBJECT — modèle skinné (Format A : magic "SOBJECT" + enveloppe zlib
//  d'UN SEUL flux couvrant tout le modèle).
// =====================================================================

// Bloc texture d'un mesh SOBJECT (Tex_ReadFromMemory 0x417D20).
// La géométrie décompressée contient ces blocs SANS les décompresser :
// on conserve donc le flux zlib brut (exploitable à la demande via Zlib).
// ex-VeryOldClient: TEXTURE_FOR_GXD.Load (CONFIRMED) — image + 2 u32 finaux =
// processMode/alphaMode ; côté cible le flux zlib reste brut (aucun GXCW/Load2-DXT).
struct SObjectTexture {
    bool     present    = false; // ddsSize==0 => texture absente
    uint32_t ddsSize    = 0;     // a2[1] : taille DDS
    uint32_t rawSize    = 0;     // taille décompressée (DDS + 8)
    uint32_t packedSize = 0;     // octets zlib
    std::vector<uint8_t> compressed; // flux zlib brut (packedSize octets)
};

// Un "subset" de rendu d'un mesh (FVF skinné). Les tampons sont conservés
// tels quels (octets) : ils sont uploadés directement dans des VB/IB D3D9.
// Mesh_ReadFromMemory 0x40C380 (strides CONFIRMED byte-exact 4910/4910).
// ex-VeryOldClient: SKIN2_FOR_GXD (mêmes strides VB76/IB6/skin32/idxCopy1 6/idxCopy2 6).
struct SObjectSubset {
    // VB 76 o/sommet — ex-VeryOldClient: SKINVERTEX2_FOR_GXD (PLAUSIBLE, GAP G2/typage FVF) :
    //   pos12 (mV3) + blendWeight16 (mW4) + boneIndex4 u8 (mB[4]) + normal/tangent/binormal 36
    //   (3× mN) + uv8 (mT2). pos/poids/os prouvés IDA ; ordre N/T/B non résolu en statique.
    static constexpr size_t kVertexStride = 76; // VB : 76 o/sommet (FVF skinné) — Model_DrawSkinnedSubset 0x40CA40
    // skin 32 o/sommet — ex-VeryOldClient: SKINSHADOWVERTEX2_FOR_GXD (PLAUSIBLE, GAP G3/skinning
    //   runtime) : pos12 + weight[4]16 + boneIndex[4] u8 4 ; Model_BuildShadowVolume 0x40DC70.
    static constexpr size_t kSkinStride   = 32; // 32 o/sommet (poids/os)
    static constexpr size_t kFaceStride   = 6;  // IB : 6 o/face (3× u16)

    uint32_t vertexCount = 0;          // a1[171]
    uint32_t faceCount   = 0;          // a1[173]
    std::vector<uint8_t> vertexBuffer; // 76 * vertexCount
    std::vector<uint8_t> indexBuffer;  // 6 * faceCount
    std::vector<uint8_t> skin;         // 32 * vertexCount
    std::vector<uint8_t> indexCopy1;   // 6 * faceCount (copie #1 : topologie)
    std::vector<uint8_t> indexCopy2;   // 6 * faceCount (copie #2 : adjacence, Model_BuildShadowVolume 0x40DC70)
};

// Un mesh du modèle SOBJECT (Mesh_ReadFromMemory 0x40C380).
// ex-VeryOldClient: SKIN_FOR_GXD / SKIN3::Load — noms seuls ; layout VeryOld DIFFÉRENT
// (SKINEFFECT 120 + SKINSIZE 40). IDA gagne : en-tête fixe opaque de 372 o (CONFIRMED).
struct SObjectMesh {
    static constexpr size_t kHeaderSize = 372; // 0x44 + 0x130 entre field0 et subsetCount

    uint32_t index      = 0;
    bool     empty      = false; // field0==0 => mesh vide (aucun corps)
    uint32_t field0     = 0;     // a1[0] : type/flag
    std::vector<uint8_t> header; // 372 o d'en-tête fixe (transform/bornes, non décodé)
    uint32_t subsetCount = 0;    // a1[93] (u32 @ +372)
    std::vector<SObjectSubset> subsets;
    SObjectTexture tex[3];       // diffuse, ~normale, ~specular/emissive
    uint32_t extraCount = 0;     // a1[220]
    std::vector<SObjectTexture> extra;
};

// Enveloppe "par mesh" du Format B (pas de magic). Le parseur Python validé
// ne décode QUE l'enveloppe zlib du 1er mesh (le format interne
// cMesh_ReadFromStream n'est pas prouvé) : on reste fidèle à cette limite.
struct SObjectRawMesh {
    uint32_t flag       = 0;
    uint32_t rawSize    = 0;
    uint32_t packedSize = 0;
    bool     inflateOk  = false;
    std::vector<uint8_t> decompressed; // rawSize octets (géométrie du mesh)
};

// Model_LoadFile 0x40E700 -> Model_ReadSubHeader 0x40E8E0 -> Model_LoadFromPak 0x40EA30.
// ex-VeryOldClient: SOBJECT3_FOR_GXD.cpp (noms de struct seuls). CONFLICT crypto (IDA gagne) :
// le GXCW du build VeryOld est ABSENT de la cible — l'en-tête `01 01 00 00` est lu EN CLAIR
// puis un unique flux zlib (4910/4910 inflate plain OK). Aucun GXCW/XXTEA à porter (cf. Rosetta §4.B).
class SObject {
public:
    enum class Format {
        Unknown,
        SObjectA, // magic "SOBJECT" : enveloppe zlib globale (validé)
        RawB,     // pas de magic : compression par mesh (validation légère)
    };

    // Charge et décode un .SOBJECT. Renvoie false en cas d'échec.
    bool Load(const std::string& path);

    Format format()  const { return format_; }
    char   version()  const { return version_; }   // '2' | '3' (Format A)
    uint8_t subType() const { return subType_; }
    uint8_t subVer()  const { return subVer_; }

    // Métadonnées d'enveloppe (Format A).
    uint32_t rawSize()    const { return rawSize_; }
    uint32_t packedSize() const { return packedSize_; }
    bool     envOk()      const { return envOk_; }     // header + packedSize == taille fichier
    bool     inflateOk()  const { return inflateOk_; } // taille décompressée == rawSize
    bool     walkOk()     const { return walkOk_; }    // curseur final == rawSize

    // Meshes du modèle (Format A). Vide en Format B.
    uint32_t meshCount() const { return meshCount_; }
    const std::vector<SObjectMesh>& meshes() const { return meshes_; }

    // Format B : nombre de meshes annoncé + enveloppe du 1er mesh (partiel).
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
//  .MOBJECT — modèle statique GMOBJECT (compression PAR ENTITÉ :
//  enveloppe [rawSize][packedSize][flux zlib] décodée pour géométrie ET
//  textures).
// =====================================================================

// Bloc texture d'un part (Tex_LoadCompressedFromHandle 0x6A9CF0).
// Contrairement au SOBJECT, l'image est décompressée ici.
// ex-VeryOldClient: TEXTURE_FOR_GXD / CTEXTURE (CONFIRMED) — trailer 8 o = 2 u32
// processMode/alphaMode ; rawSize == imgSize+8 vérifié.
struct MTexture {
    bool     present    = false; // imgSize==0 => absente
    uint32_t imgSize    = 0;     // taille image DDS/TGA
    uint32_t rawSize    = 0;     // = imgSize + 8 (image + trailer)
    uint32_t packedSize = 0;     // octets zlib
    std::vector<uint8_t> image;  // rawSize octets décompressés (image + 8 o trailer)
    uint32_t trailer0   = 0;     // 2 u32 de queue (dims/uv), lus à data[imgSize]
    uint32_t trailer1   = 0;
    char     magic[4]   = {0,0,0,0}; // 4 premiers octets de l'image (FourCC / "DDS ")
};

// Géométrie décompressée d'un part (cf. MeshPart_Load 0x6AD160 / parse_geometry).
struct MGeometry {
    static constexpr size_t kHeaderSize   = 0x78; // 120 o, copié dans part+132
    static constexpr size_t kMatrixStride = 64;   // matrice 4×4 (16 float)
    // ex-VeryOldClient: MESHVERTEX_FOR_GXD (CONFIRMED) — 32 o = mV3 pos + mN3 normale + mT2 uv.
    // Sommet MOBJECT statique, DISTINCT du SkinVertex 32 o SOBJECT (pos+poids+os).
    static constexpr size_t kVertexStride = 32;   // FVF XYZ | NORMAL | TEX1
    static constexpr size_t kFaceStride   = 6;    // 3× u16

    std::vector<uint8_t> header;   // 120 o (Heap[..30])
    uint32_t M = 0;                // Heap[30] : nombre de groupes/matrices
    uint32_t V = 0;                // Heap[31] : sommets par groupe
    uint32_t X = 0;                // Heap[32] : (non utilisé pour la taille)
    uint32_t I = 0;                // Heap[33] : nombre de triangles
    std::vector<uint8_t> matrices; // M * 64 o
    std::vector<uint8_t> vertices; // 32 * M * V o  (M*V sommets)
    std::vector<uint8_t> indices;  // 6 * I o
    bool     sizeOk = false;       // 0x88 + mats + vtx + idx == taille décompressée
};

// Un "part" du modèle MOBJECT (MeshPart_Load 0x6AD160).
struct MeshPart {
    uint32_t index    = 0;
    bool     hasMesh  = false;  // flag 0 => part vide (rien d'autre à lire)
    uint32_t geoRaw   = 0;      // rawSize de l'enveloppe géométrie
    uint32_t geoPacked = 0;     // packedSize de l'enveloppe géométrie
    MGeometry geo;
    MTexture  tex0;             // texture principale
    MTexture  tex1;             // 2e texture
    uint32_t  matCount = 0;     // nombre de matériaux additionnels
    std::vector<MTexture> mats;
};

// Model_LoadFromFile 0x6A3490 -> MeshPart_Load 0x6AD160 ; GXD_DecompressEntity 0x6A1A30.
// ex-VeryOldClient: MOBJECT_FOR_GXD.cpp (CONFLICT crypto, IDA gagne) — VeryOld chiffre `{mMeshNum}`
// en XXTEA ; la cible lit `nPart` EN CLAIR puis zlib par entité. Aucun magic `TEA1`.
class MObject {
public:
    // Charge et décode un .MOBJECT. Renvoie false en cas d'échec.
    bool Load(const std::string& path);

    uint32_t partCount() const { return partCount_; }
    const std::vector<MeshPart>& parts() const { return parts_; }

    // true si le fichier a été entièrement consommé (leftover==0) et sans
    // avertissement de taille interne de géométrie.
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

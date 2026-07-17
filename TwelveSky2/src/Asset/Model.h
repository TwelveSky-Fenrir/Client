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
//
// TRAILER 8 o (Passe 4 / W7, front sobject-material — gap SOBJ-02) : le bloc décompressé vaut
// `[DDS: ddsSize o][u32 processMode][u32 alphaMode]` (rawSize == ddsSize + 8). Tex_ReadPacked
// 0x417740 lit CES DEUX u32 depuis la queue du bloc et les range dans la struct texture 56 o :
//     a1[10] = *(_DWORD *)&Heap[lpMema];      // @0x4178d6 -> matériau +40  (processMode)
//     a1[11] = *(_DWORD *)&Heap[lpMema + 4];  // @0x4178dd -> matériau +44  (alphaMode)
//   (lpMema = a1[1] = ddsSize @0x41788e ; pTexture = a1[13] -> +52 @0x41795d)
// Stride 56 recoupé : `v51 = 56 * *v34` @0x40c29c, `v37 += 56` @0x40c2d0, `ii += 56` @0x40c2e4,
// `v61 = *(a2+884) + 56*v15` @0x40cb09.
// Le champ +44 EST le blendMode consommé par le rendu : `v16 = *(_DWORD *)(v61 + 44)`
// @0x40cb1a / @0x40cb2c (filtre de passe) et @0x40d953 (restauration des états).
struct SObjectTexture {
    bool     present    = false; // ddsSize==0 => texture absente
    uint32_t ddsSize    = 0;     // a2[1] : taille DDS
    uint32_t rawSize    = 0;     // taille décompressée (DDS + 8)
    uint32_t packedSize = 0;     // octets zlib
    std::vector<uint8_t> compressed; // flux zlib brut (packedSize octets)

    // --- Trailer 8 o (Tex_ReadPacked 0x417740) ---
    // ATTENTION : `processMode` n'a AUCUN LECTEUR PROUVÉ dans le chemin de dessin skinné
    // (Model_DrawSkinnedSubset 0x40CA40 ne lit que +44). Il est exposé parce que le binaire le
    // décode et le stocke — surtout PAS parce qu'on lui connaîtrait un usage. Ne rien en déduire.
    uint32_t processMode = 0;  // matériau +40 — a1[10] @0x4178d6
    uint32_t alphaMode   = 0;  // matériau +44 — a1[11] @0x4178dd — == blendMode (0/1/2)
    bool     trailerDecoded = false; // true si l'inflate du bloc a permis de lire les 8 o de queue
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

// En-tête matériau 120 o du MeshPart, DÉCODÉ en champs nommés.
//
// Provenance : MeshPart_Load 0x6AD160 copie les 120 premiers octets du blob géométrie
// décompressé dans part+132 (`qmemcpy((void*)(this+132), Heap, 0x78)` @0x6ad2d1), soit
// Heap[0..29] = 30 dwords. Ce même blob est conservé brut dans `MGeometry::header`
// (kHeaderSize = 0x78) ; on le RÉINTERPRÈTE ici sans le modifier (décodage ADDITIF ;
// geo.header reste intact pour audit).
//
// Correspondance d'offset prouvée par l'accord écriture (Load) ↔ lecture
// (MeshPart_RenderFull 0x6B0850) : header dword `k` = Heap[k] = part dword [33+k] =
// part offset (132 + 4*k). Chaque champ porte l'ancre du site qui le LIT au rendu.
// A/B/X/D (Heap[30..33]) sont HORS de ce bloc (déjà exposés en MGeometry::M/V/X/I).
//
// STRUCT PARTAGÉE : les .MOBJECT (MeshPart) ET les .WO (WorldMeshPart, Asset/WorldChunk.h)
// passent par le MÊME chargeur MeshPart_Load 0x6AD160 → en-tête matériau BYTE-IDENTIQUE.
// WorldMeshPart réutilise donc CE type (cf. bandeau Asset/WorldChunk.h) — pas de doublon.
//
// ⚠ Sémantique fine NON PINNÉE (résidu runtime, cf. TS2_DEEP_MESHPART_MATERIAL.md §10 /
// TS2_DEEP_MOBJECT.md §2) : le rôle R/G/B/A des canaux de `lightAnim.Pairs` (passés à
// Gfx_SetLight slot 2 @0x6b0988 comme couleur diffuse animée) et des canaux
// `glow.SpecRGBA` (posés en D3DMATERIAL9.Specular @0x6b0a48) — seuls les OFFSETS sont
// prouvés, pas l'ordre des composantes.
struct MeshPartMaterial {
    // header[0] = Heap[0] : sous-compte interne, JAMAIS lu par MeshPart_RenderFull
    // (exposé pour complétude ; ne rien en déduire).
    uint32_t subCount = 0;

    // Lumière émissive animée (ping-pong triangulaire) — gate `this[34]` @0x6b087d.
    // Boucle 4 canaux `v10=(float*)(this+144)` : canal i lit v10[0]=Pairs[i] (« from »)
    // et v10[4]=Pairs[i+4] (« to ») @0x6b08c5 ; phase = v66 * Speed @0x6b08bb.
    struct {
        uint32_t Enable  = 0;                        // header[1]  = part[34] (+136)  @0x6b087d
        float    Speed   = 0.0f;                     // header[2]  = part[35] (+140)  @0x6b08bb
        float    Pairs[8] = {0,0,0,0,0,0,0,0};       // header[3..10] = part[36..43] (+144..+175)
                                                     //   [0..3]=« from », [4..7]=« to » ; canaux non pinnés
    } lightAnim;

    // Neutralise la lumière 0 (Gfx_SetLight(1,0…)) — gate `this[44]` @0x6b099b.
    uint32_t noLight = 0;                            // header[11] = part[44] (+176)

    // Glow spéculaire vue-dépendant : Gfx_SetMaterialEmissive pose en réalité le
    // D3DMATERIAL9.Specular (RGBA) + Power — gate `a5 && this[45] && !this[99]` @0x6b0a11.
    struct {
        uint32_t Enable      = 0;                    // header[12] = part[45] (+180)  @0x6b0a11
        uint32_t Mode        = 0;                    // header[13] = part[46] (+184)  @0x6b0a1f (1=constant, 2=vue-dépendant)
        float    SpecRGBA[4] = {0,0,0,0};            // header[14..17] = part[47..50] (+188..+203) @0x6b0a48 (this+188=(_DWORD*)this+47)
        float    SpecPower   = 0.0f;                 // header[18] = part[51] (+204)  @0x6b0a34 (D3DMATERIAL9.Power)
    } glow;

    // Scalaire d'intensité de la lumière de projection (Gfx_SetShadowProjLight(this[52],…))
    // @0x6b0a59. Hors de la liste demandée mais dans les 120 o : exposé pour un décodage complet.
    float lightOffset = 0.0f;                        // header[19] = part[52] (+208)

    // Flipbook de la texture de base (atlas animé, modulo matCount) — gate `this[53]` @0x6b0d33.
    struct {
        uint32_t Enable = 0;                         // header[20] = part[53] (+212)  @0x6b0d33
        float    Fps    = 0.0f;                      // header[21] = part[54] (+216)  @0x6b0d78 (Crt_Dbl2Uint(v66*this[54]))
    } flipbook;

    // Défilement UV par matrice de texture. Mode (switch 1..4) :
    // 1=scroll V, 2=scroll U, 3=diagonale, 4=anti-diagonale ; vitesse = v66 * Speed.
    struct UvScroll {
        uint32_t Enable = 0;                         // tex1 header[22]=part[55](+220) @0x6b0f59 | tex2 header[27]=part[60](+240) @0x6b19bb
        uint32_t Mode   = 0;                         // tex1 header[23]=part[56](+224) @0x6b0f73 | tex2 header[28]=part[61](+244) @0x6b19d5
        float    Speed  = 0.0f;                      // tex1 header[24]=part[57](+228) @0x6b1016 | tex2 header[29]=part[62](+248) @0x6b1a78
    };
    struct {
        UvScroll tex1;                               // header[22..24] (stage 0)
        UvScroll tex2;                               // header[27..29] (2e passe)
    } uvScroll;

    // Overlay billboard face-caméra (quad construit CPU + DrawPrimitiveUP) — gate `this[58]` @0x6b107c.
    struct {
        uint32_t Enable = 0;                         // header[25] = part[58] (+232)  @0x6b107c
        uint32_t Mode   = 0;                         // header[26] = part[59] (+236)  @0x6b11b0 (1=plan écran flt_8001D4 / autre=axe libre unk_80022C)
    } billboard;

    bool decoded = false;                            // true si les 120 o d'en-tête ont été décodés
};

// Décode les 120 o d'en-tête matériau (MGeometry::header) en champs nommés (ADDITIF).
// Partagé .MOBJECT / .WO (même MeshPart_Load 0x6AD160). Implémentation : Model.cpp.
void DecodeMeshPartMaterialHeader(const std::vector<uint8_t>& header, MeshPartMaterial& out);

// Un "part" du modèle MOBJECT (MeshPart_Load 0x6AD160).
struct MeshPart {
    uint32_t index    = 0;
    bool     hasMesh  = false;  // flag 0 => part vide (rien d'autre à lire)
    uint32_t geoRaw   = 0;      // rawSize de l'enveloppe géométrie
    uint32_t geoPacked = 0;     // packedSize de l'enveloppe géométrie
    MGeometry geo;
    MeshPartMaterial mat;       // en-tête matériau 120 o décodé (depuis geo.header) — ADDITIF
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

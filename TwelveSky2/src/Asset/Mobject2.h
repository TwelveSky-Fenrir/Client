// Asset/Mobject2.h — lecteur du conteneur .MOBJECT2 (array de meshes d'effet).
//
//   .MOBJECT2 -> struct Mobject2  (conteneur : magic "MOBJECT2" + tableau de meshes 268 o)
//
// Format DISTINCT du .MOBJECT (parts 408 o, cf. Asset/Model.h MObject) : mesh 268 o,
// sommet FVF 258 = D3DFVF_XYZ|TEX1 (20 o, SANS normale), textures au format SOBJECT 56 o
// (Tex_ReadPacked 0x417740 — même struct que asset::SObjectTexture, réutilisée telle quelle).
//
// Vivacité (xrefs_to Mesh_LoadMOBJECT2) : Model_LoadWO2_A/B (CODE MORT, 0 xref) + Emitter_ReadFile
// 0x424D30 <- Effect_ReadStream 0x42A990 <- Effect_LoadFile 0x42A920 : le .MOBJECT2 est VIVANT
// UNIQUEMENT via le système d'effets/emitters (meshes de particules). C'est pourquoi ce parseur
// expose Parse(buffer, offset) : le conteneur est le plus souvent EMBARQUÉ dans un flux d'effet,
// pas un fichier autonome. PARSER SEUL — aucun rendu, aucun upload GPU (cf. Docs/TS2_DEEP_MOBJECT.md §T5).
//
// Loaders d'origine (vérité IDA, imagebase 0x400000, LECTURE SEULE le 2026-07-17) :
//   Conteneur : Mesh_LoadMOBJECT2 0x4318C0  (magic + attribut + meshCount + array)
//   Mesh      : Mesh_ReadFile     0x430470  (header 76 + N + tables 40·N/4·N + header 80 +
//                                            subsets [VB 20·N·vc FVF 258 / IB 6·fc] + tex SOBJECT 56)
//   Texture   : Tex_ReadPacked    0x417740  (imgSize/rawSize/packedSize/zlib + trailer 8 o)
//   Reset     : Mesh_ResetFields  0x430110
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "Asset/Model.h" // réutilise asset::SObjectTexture (format texture SOBJECT 56 o, Tex_ReadPacked)

namespace ts2::asset {

// Un "subset" de rendu d'un mesh MOBJECT2 (boucle par subset dans Mesh_ReadFile 0x430470).
// Les tampons sont conservés bruts (octets) : destinés à un upload direct VB/IB D3D9 (axe rendu séparé).
struct Mobject2Subset {
    // FVF 258 = 0x102 = D3DFVF_XYZ | D3DFVF_TEX1 -> sommet 20 o (pos vec3 + uv vec2, SANS normale) —
    // CreateVertexBuffer(..., FVF=258, ...) @0x430897. C'est la taille de sommet de BASE (N==1, cf. Mesh::n).
    static constexpr size_t kVertexStrideBase = 20; // 20 o/sommet (FVF 258)
    static constexpr size_t kFaceStride       = 6;  // IB : 6 o/face (3× u16), D3DFMT_INDEX16

    uint32_t vertexCount = 0;           // u32 lu -> a1[45][i]   @0x43079d
    uint32_t faceCount   = 0;           // u32 lu -> a1[48][i]   @0x430928
    // VB : taille disque = 20 * N * vertexCount octets (N = Mobject2Mesh::n).
    //   @0x4307c1 (chemin CPU mode1 : v47 = 20 * a1[21] * vertexCount)
    //   @0x430897 (chemin GXD       : CreateVertexBuffer(20 * a1[21] * vertexCount, FVF=258))
    // ⚠ Le multiplicateur `20·N` est byte-exact (c'est littéralement la taille lue), mais la SÉMANTIQUE
    //   de N n'est PAS prouvée en statique (cf. Mobject2Mesh::n). La lecture ci-dessous reste correcte
    //   quel que soit N ; seule l'INTERPRÉTATION « 20 o/sommet » suppose N==1.
    std::vector<uint8_t> vertexBuffer;  // 20 * N * vertexCount o
    std::vector<uint8_t> indexBuffer;   // 6 * faceCount o        @0x430942 (CPU) / @0x430a03 (GXD)
};

// Un mesh du conteneur MOBJECT2 (Mesh_ReadFile 0x430470), stride 268 o (67 dwords).
// Champs pointeurs runtime (a1[22/23/45..50/66]) NON stockés : ce sont des heaps alloués au load,
// leurs *contenus disque* sont capturés ici (boneTable/table4/subsets/extraTex).
struct Mobject2Mesh {
    static constexpr size_t kStride       = 268; // 268 o par mesh (alloc 268*meshCount @0x4319df)
    static constexpr size_t kHeader1Size  = 76;  // 0x4C, lu dans a1+2 (a1[2..20])   @0x4304dc
    static constexpr size_t kHeader2Size  = 80;  // 0x50, lu dans a1+24 (a1[24..43])  @0x4305c6
    static constexpr size_t kBoneStride   = 40;  // 40 o/entrée (table a1[22], 40*N)   @0x43051d
    static constexpr size_t kTable4Stride = 4;   // 4 o/entrée  (table a1[23], 4*N)     @0x430573

    uint32_t index = 0;
    bool     empty = false;             // type==0 => mesh vide (rien d'autre à lire)  @0x4304a9
    uint32_t type  = 0;                 // a1[0] : type/flag                            @0x4304a7
    std::vector<uint8_t> header1;       // 76 o (a1[2..20]) — transform/bornes, non décodé
    // a1[21] : facteur multiplicatif de la taille de sommet (VB = 20·N·vertexCount, @0x4304fd).
    // Le DEEP doc l'appelle « nbFrames » (flipbook, cf. MeshPart A frames) — PLAUSIBLE mais NON PROUVÉ.
    // Autre hypothèse ouverte : N = nombre de jeux de texcoords (sommet 20·N o). Résidu à dumper en
    // runtime (Docs/TS2_DEEP_MOBJECT.md §3.3/§T5). TODO ancre @0x4307c1 : ne rien inventer en aval.
    uint32_t n = 0;
    std::vector<uint8_t> boneTable;     // 40 * N o (a1[22]) — table d'os/matrices, non décodée
    std::vector<uint8_t> table4;        // 4  * N o (a1[23]) — table parallèle, non décodée
    std::vector<uint8_t> header2;       // 80 o (a1[24..43]) — 2e bloc d'en-tête, non décodé
    uint32_t subsetCount = 0;           // a1[44]                                        @0x4305ec
    std::vector<Mobject2Subset> subsets;
    SObjectTexture tex;                 // 1 texture SOBJECT 56 o (Tex_ReadPacked a1+51)  @0x430a80
    uint32_t extraTexCount = 0;         // a1[65]                                        @0x430ab1
    std::vector<SObjectTexture> extraTex; // 56 o chacune (a1[66], 56*extraTexCount)      @0x430b22
};

// Conteneur .MOBJECT2 (Mesh_LoadMOBJECT2 0x4318C0). Layout disque :
//   [8 o magic "MOBJECT2"][u32 attribut][u32 meshCount][mesh ×meshCount]
// attribut==0 => conteneur vide (aucun meshCount lu, valide, return 1 @0x4319b0).
class Mobject2 {
public:
    static constexpr size_t kMagicSize  = 8;
    static constexpr size_t kMeshStride = Mobject2Mesh::kStride; // 268

    // Charge et décode un .MOBJECT2 AUTONOME (fichier entier). Renvoie false en cas d'échec.
    bool Load(const std::string& path);

    // Décode un conteneur MOBJECT2 EMBARQUÉ à `offset` dans un flux (usage emitter/effet).
    // bytesConsumed() indique combien d'octets ont été lus (pour reprendre le flux ensuite).
    bool Parse(const uint8_t* data, size_t size, size_t offset = 0);

    uint32_t attribute() const { return attribute_; } // lpBuffer[0] (flag/type conteneur)
    uint32_t meshCount() const { return meshCount_; }  // lpBuffer[1]
    const std::vector<Mobject2Mesh>& meshes() const { return meshes_; }

    bool   ok() const { return ok_; }                  // parse structural complet, sans exception
    size_t bytesConsumed() const { return bytesConsumed_; } // octets lus depuis `offset`
    size_t leftover() const { return leftover_; }      // octets restants après le conteneur (>0 si embarqué)
    const std::string& error() const { return error_; }

private:
    uint32_t attribute_    = 0;
    uint32_t meshCount_    = 0;
    std::vector<Mobject2Mesh> meshes_;
    bool     ok_           = false;
    size_t   bytesConsumed_ = 0;
    size_t   leftover_     = 0;
    std::string error_;
};

} // namespace ts2::asset

// Asset/WorldChunk.h — lecteur des chunks de géométrie de monde TS2.
//   D07_GWORLD\Z%03d.{WM,WJ,WG,WO,WP}
//
// Reverse fidèle de RE/asset_parsers/world_geometry.py (validé 455/455 fichiers),
// lui-même calqué sur les lecteurs runtime du client TwelveSky2.exe :
//   .WM -> MapColl_LoadFaces      (0x694510)  collision principale  (enveloppe zlib)
//   .WJ -> MapColl_LoadFaces      (0x694510)  collision secondaire  (même structure)
//   .WG -> MapColl_LoadMapFile    (0x697B30)  faces + matériaux/textures
//   .WO -> MapColl_LoadObjectsA   (0x6980D0)  modèles statiques (Model/MeshPart)
//   .WP -> MapColl_LoadObjectsB   (0x6983B0)  nœuds FX (Fx_NodeLoadFromHandle)
//
// Primitive de compression = GXD_DecompressEntity (0x6A1A30) :
//   bloc GXD   = [rawSize:u32][packedSize:u32][flux zlib] -> rawSize octets
//   bloc image = [imageSize:u32][rawSize=imageSize+8:u32][packedSize:u32][zlib]
//                -> DDS/BMP(imageSize) + trailer 8o ; imageSize==0 => texture absente
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ts2::asset {

// Sous-format détecté (par extension). WM et WJ partagent la même structure.
enum class WorldChunkType { Unknown, WM, WJ, WG, WO, WP };

// -------------------------------------------------------------------------
// Bloc texture compressé (Tex_LoadCompressedFromHandle).
// present==false <=> imageSize==0 (« texture absente », 4 octets seulement).
// -------------------------------------------------------------------------
struct TextureBlock {
    bool        present   = false;   // Python: not empty
    uint32_t    imageSize = 0;       // taille de l'image décodée (DDS/BMP)
    uint32_t    packedSize = 0;      // taille du flux zlib sur disque
    std::string imgType;             // "DDS", "BMP", ou hex des 4 octets magiques
    uint32_t    trailer[2] = {0, 0}; // 8 octets à l'offset imageSize (deux u32)
    std::vector<uint8_t> data;       // image décodée (imageSize octets) — exploitable en aval
};

// -------------------------------------------------------------------------
// Piste d'animation par quaternions (Anim_LoadQuatTrackFromHandle).
// bloc GXD : 8o d'en-tête (frames,tracks) + 28o * frames * tracks.
// -------------------------------------------------------------------------
struct AnimTrack {
    bool     present = false;
    uint32_t frames  = 0;            // u32 @0 du bloc décodé
    uint32_t tracks  = 0;            // u32 @4 du bloc décodé
    std::vector<uint8_t> data;       // bloc GXD décodé (en-tête + données 28o/entrée)
};

// -------------------------------------------------------------------------
// Vertex terrain (Gap G7) — FVF 530 = 0x212 = D3DFVF_XYZ|NORMAL|TEX2, 40 octets.
// Deux jeux d'UV : uv0 = texture diffuse (stage 0), uv1 = lightmap/.SHADOW (stage 1).
// Réf IDA : Terrain_Render 0x698670 — SetFVF(530) @0x698e6d ; stride 40 dans tous les
// DrawPrimitiveUP (device vtbl+332, ex. @0x698ff3/@0x69913c/@0x69945d) ; 120o=3*40/face
// copiés via qmemcpy(dst, v48+1, 0x78) @0x698e21 (v48+1 = saut du materialIndex).
// Position @+0 prouvée par les tests barycentriques : MapColl_RayHitTriangle 0x695ae0
// (lit face+4/+8/+12 puis face+44/+48/+52) et MapColl_PointInTriangleXZ 0x695c70 (face+84).
// DISTINCT du MobjVertex 32o des .WO (voir Gfx). Layout normal/uv/uv2 = interprétation FVF.
// -------------------------------------------------------------------------
struct TerrainVertex {
    float position[3];   // +0x00  repère MONDE (world = identité au rendu terrain)
    float normal[3];     // +0x0C  unitaire (D3DFVF_NORMAL)
    float uv0[2];        // +0x18  texcoord set 0 (diffuse, stage 0)
    float uv1[2];        // +0x20  texcoord set 1 (lightmap/shadow, stage 1 — support G6)
};
static_assert(sizeof(TerrainVertex) == 40, "TerrainVertex doit faire 40 octets (FVF 530)");

// -------------------------------------------------------------------------
// Face de collision / rendu terrain (Gap G4) — 156 octets, ordre AUTORITATIF IDA.
// CONFLICT C-02 (TS2_WORLD_ROSETTA.md §2) : materialIndex EN PREMIER (@0). VeryOldClient
// le plaçait @152 -> IGNORÉ (IDA gagne). Total 156o confirmé des deux côtés.
// Réf IDA (offsets prouvés) :
//   - materialIndex@0    : 120o de sommets copiés depuis face+4 (Terrain_Render qmemcpy @0x698e21)
//   - v0@+4 v1@+44 v2@+84 (stride 40) : MapColl_RayHitTriangle 0x695af4/0x695afc / PointInTriangleXZ 0x695c98
//   - plane@+124/+128/+132/+136 (a,b,c,d) : MapColl_GetGroundHeight plane-solve 0x6972ad
//                                            + backface Terrain_Render 0x698dd4 (v47[31..34])
//   - sphereCenter@+140 / sphereRadius@+152 : Cam_FrustumTestSphere2x(v47+35, v47[38]) @0x698de9
// -------------------------------------------------------------------------
struct CollisionFace {
    uint32_t      materialIndex;    // +0x00  (aussi « mTextureIndex ») ; ==1 => marchable couche .WM
    TerrainVertex v0;               // +0x04
    TerrainVertex v1;               // +0x2C
    TerrainVertex v2;               // +0x54
    float         plane[4];         // +0x7C  planeA/B/C/D = normal.xyz + D (tris+124/+128/+132/+136)
    float         sphereCenter[3];  // +0x8C  centre de la sphère englobante (tris+140/+144/+148)
    float         sphereRadius;     // +0x98  rayon de la sphère englobante (tris+152)

    // planeB (= normal.y) : diviseur du plane-solve ; > 0 => face orientée vers le haut,
    // marchable par défaut (MapColl_GetGroundHeight filtre 0x697259, solve 0x6972ad).
    bool PlaneFacesUp() const { return plane[1] > 0.0f; }
    // Tag marchable de la couche .WM (variante WORLD2). Rosetta §1.A / §3 G04 : mTextureIndex==1.
    bool IsWalkableTag() const { return materialIndex == 1; }
};
static_assert(sizeof(CollisionFace) == 156, "CollisionFace doit faire 156 octets");

// -------------------------------------------------------------------------
// Nœud de quadtree de collision (Gap G5) — 48 octets, layout RUNTIME.
// Réf IDA : MapColl_GetGroundHeight 0x697130 — base quadtree = *(this+35) ;
//   bboxMin@+0 / bboxMax@+12 (test XZ @0x6971ba) ; ceiling = node0.bboxMax.y @+16 (@0x6971e5) ;
//   trisNum@+24 (@0x6971fc) ; trisIndex@+28 (@0x69726c) ; child[4]@+32 (@0x697171/@0x697159).
// Racine = nœud index 0 ; feuille <=> child[0] == -1.
// Le format DISQUE est à taille variable (48o fixe + 4*faceRefCount si hasFaceRefs) ; on le
// reconstruit ici en tableau 48o fixe + buffer d'index agrégé (CollisionMesh::triIndices).
// `trisIndex` = OFFSET (en entrées u32) dans triIndices (le runtime y garde un pointeur vif).
// -------------------------------------------------------------------------
struct CollisionQuadNode {
    float    bboxMin[3];   // +0x00
    float    bboxMax[3];   // +0x0C   (node0 +16 = bboxMax.y = plafond monde par défaut)
    uint32_t trisNum;      // +0x18   nombre d'index de faces (feuille)
    uint32_t trisIndex;    // +0x1C   offset dans CollisionMesh::triIndices (ptr vif à l'origine)
    int32_t  child[4];     // +0x20   4 enfants ; child[0]==-1 => feuille

    bool IsLeaf() const { return child[0] == -1; }
};
static_assert(sizeof(CollisionQuadNode) == 48, "CollisionQuadNode doit faire 48 octets");

// -------------------------------------------------------------------------
// Maillage de collision partagé (MapColl_LoadFaces 0x694510 ; 1er bloc de WG).
//   numTri + (156o * triangles) + numNodes + field34 + quadtree.
//   Chaque nœud disque : min[3]f max[3]f numIdx u32 hasIdx u32 [index u32*numIdx] children[4]u32.
// Décodage typé (Gaps G4/G5/G7) : `raw` reste conservé (fidélité byte-exacte + rétro-compat),
// et les champs typés ci-dessous exposent la même donnée prête à consommer.
// -------------------------------------------------------------------------
struct CollisionMesh {
    uint32_t numTri       = 0;       // nombre de triangles (156 octets chacun)
    uint32_t numNodes     = 0;       // nombre de nœuds du quadtree
    uint32_t field34      = 0;       // (this+34) compteur global d'index (leafFaceRefTotal)
    uint32_t totalIndices = 0;       // somme des index de triangles (nœuds avec hasIdx)
    std::vector<uint8_t> raw;        // buffer décompressé complet (triangles + quadtree)

    // --- Vues typées décodées depuis `raw` (ordre de lecture = MapColl_LoadFaces 0x694510) ---
    std::vector<CollisionFace>     tris;        // Gap G4 : numTri faces (156o) décodées
    std::vector<CollisionQuadNode> nodes;       // Gap G5 : numNodes nœuds (48o), racine = index 0
    std::vector<uint32_t>          triIndices;  // buffer d'index de faces agrégé (feuilles -> tris[])
    std::vector<TerrainVertex>     vertices;    // Gap G7 : 3*numTri sommets à plat (miroir du VB
                                                //   dynamique Terrain_Render a1+164, upload FVF 530)
};

// -------------------------------------------------------------------------
// Partie de maillage d'un modèle statique (MeshPart_Load).
//   bloc GXD géométrie : 120o d'en-tête + os + VB + IB.
//   A=Heap[30] B=Heap[31] C=Heap[32] D=Heap[33] (nb triangles).
//   taille attendue = 136 + (A<<6) + 32*A*B + 6*D.
// -------------------------------------------------------------------------
struct WorldMeshPart {
    bool     present   = false;
    uint32_t A = 0, B = 0, C = 0, D = 0;
    bool     geoSizeOk = false;      // raw == 136 + (A<<6) + 32*A*B + 6*D
    std::vector<uint8_t> geo;        // bloc GXD géométrie décodé
    TextureBlock tex1;               // this+296
    TextureBlock tex2;               // this+348
    std::vector<TextureBlock> materials; // this+404[] (num_mat entrées)
};

// Modèle statique (Model_LoadFromHandle) = liste de MeshPart.
struct Model {
    bool present = false;
    std::vector<WorldMeshPart> parts;     // num_parts
};

// -------------------------------------------------------------------------
// Fichier .WM / .WJ — un seul bloc GXD contenant le maillage de collision.
// -------------------------------------------------------------------------
struct MapCollisionChunk {
    CollisionMesh mesh;
    uint32_t rawSize    = 0;
    uint32_t packedSize = 0;
};

// -------------------------------------------------------------------------
// Fichier .WG — bloc géométrie (collision) + matériaux/textures.
// -------------------------------------------------------------------------
struct MapFaceChunk {
    CollisionMesh mesh;                     // bloc géométrie (tri + quadtree)
    uint32_t geoRaw    = 0;
    uint32_t geoPacked = 0;
    uint32_t numMaterials = 0;              // this+3
    std::vector<TextureBlock> textures;     // num_materials (certaines absentes)
    std::vector<uint32_t> materialIndices;  // this+36 : table d'index matériaux (u32*num)
};

// -------------------------------------------------------------------------
// Instance placée d'un gabarit `models[]` dans le monde — LE PLACEMENT réel.
// 28 octets sur disque, format confirmé par désassemblage (Model_RenderParts
// 0x6A3720, Model_RenderWithShadow_0 0x6A4110) : cf. Docs/TS2_WO_PLACEMENT_FORMAT.md.
// PAS de champ d'échelle : le moteur ne multiplie jamais par une matrice de
// scale pour les objets .WO — scale = 1.0 toujours.
// -------------------------------------------------------------------------
struct AuxRecord {
    uint32_t modelIndex = 0;      // +0x00 index (0-based) dans ObjectChunk::models[]
    float    pos[3] = {0, 0, 0};  // +0x04 position monde x,y,z
    float    rot[3] = {0, 0, 0};  // +0x10 rotation en DEGRÉS x,y,z
    // World = Rz(rot.z) * Ry(rot.y) * Rx(rot.x) * T(pos)  (ordre D3DX exact, voir doc)
};

// -------------------------------------------------------------------------
// Fichier .WO — modèles statiques + placements + records auxiliaires.
// -------------------------------------------------------------------------
struct ObjectChunk {
    bool     empty = false;                 // numModels == 0
    std::vector<Model> models;              // this+23 : num_models
    std::vector<uint8_t> placements;        // this+25 : 100 octets * num_models (métadonnée
                                             // PAR GABARIT : nom NUL-terminé + bourrage non lu
                                             // par le moteur, hors tag "NO_SHADOW_" — PAS un
                                             // enregistrement de transform, cf. doc)
    std::vector<std::string> placementNames;// nom extrait de placements[] pour chaque modèle
                                             // (debug / futur tag NO_SHADOW_), même taille que
                                             // models
    uint32_t numAux = 0;                    // this+26
    std::vector<AuxRecord> auxRecords;      // LES INSTANCES PLACÉES (28 o/instance sur disque) :
                                             // position + rotation par instance, résolues via
                                             // modelIndex -> models[modelIndex]
};

// -------------------------------------------------------------------------
// Nœud FX (Fx_NodeLoadFromHandle) : texture + piste anim + 144o de champs fixes.
// -------------------------------------------------------------------------
struct FxNode {
    bool present = false;
    TextureBlock tex;                       // Tex_LoadCompressedFromHandle
    AnimTrack    anim;                      // Anim_LoadQuatTrackFromHandle
    std::vector<uint8_t> fields;            // 144 octets de champs fixes
};

// -------------------------------------------------------------------------
// Fichier .WP — nœuds FX + placements + records B.
// -------------------------------------------------------------------------
struct FxChunk {
    bool     empty = false;                 // numFx == 0
    std::vector<FxNode> nodes;              // this+28 : num_fx
    std::vector<uint8_t> placements;        // this+30 : 100 octets * num_fx
    uint32_t numFxb = 0;                    // this+31
    std::vector<uint8_t> fxbRecords;        // 28 octets disque (4+12+12) * num_fxb
};

// -------------------------------------------------------------------------
// Chargeur unifié : détecte le sous-format et peuple le membre correspondant.
// -------------------------------------------------------------------------
class WorldChunk {
public:
    // Charge un chunk depuis un fichier ; le type est déduit de l'extension.
    // Renvoie false si extension inconnue, lecture ou parsing impossible.
    bool Load(const std::string& path);

    // Parse un buffer déjà en mémoire avec un type explicite.
    bool LoadFromMemory(const std::vector<uint8_t>& data, WorldChunkType type);

    WorldChunkType Type() const { return type_; }

    // Accès typé : seul le membre correspondant à Type() est peuplé (nullptr sinon).
    const MapCollisionChunk* AsCollision() const { return collision_ ? &*collision_ : nullptr; } // WM/WJ
    const MapFaceChunk*      AsFace()      const { return face_      ? &*face_      : nullptr; } // WG
    const ObjectChunk*       AsObjects()   const { return objects_   ? &*objects_   : nullptr; } // WO
    const FxChunk*           AsFx()        const { return fx_        ? &*fx_        : nullptr; } // WP

    // Résumé lisible (compteurs), utile pour comparer à la sortie du parseur Python.
    std::string Describe() const;

private:
    void Reset();

    WorldChunkType type_ = WorldChunkType::Unknown;
    std::optional<MapCollisionChunk> collision_;
    std::optional<MapFaceChunk>      face_;
    std::optional<ObjectChunk>       objects_;
    std::optional<FxChunk>           fx_;
};

// Déduit le sous-format à partir de l'extension du chemin (.WM/.WJ/.WG/.WO/.WP).
WorldChunkType WorldChunkTypeFromExtension(const std::string& path);
// Nom court du type (« WM », « WG », …) — « ? » si Unknown.
const char*    WorldChunkTypeName(WorldChunkType t);

} // namespace ts2::asset

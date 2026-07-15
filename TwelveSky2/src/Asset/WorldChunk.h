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
// Maillage de collision partagé (MapColl_LoadFaces ; 1er bloc de WG).
//   numTri + (156o * triangles) + numNodes + field34 + quadtree.
//   Chaque nœud : min[3]f max[3]f numIdx u32 hasIdx u32 [index u32*numIdx] children[4]u32.
// -------------------------------------------------------------------------
struct CollisionMesh {
    uint32_t numTri       = 0;       // nombre de triangles (156 octets chacun)
    uint32_t numNodes     = 0;       // nombre de nœuds du quadtree
    uint32_t field34      = 0;       // (this+34) compteur global d'index
    uint32_t totalIndices = 0;       // somme des index de triangles (nœuds avec hasIdx)
    std::vector<uint8_t> raw;        // buffer décompressé complet (triangles + quadtree)
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

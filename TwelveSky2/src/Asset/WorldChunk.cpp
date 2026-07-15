// Asset/WorldChunk.cpp — fidèle à RE/asset_parsers/world_geometry.py (validé 455/455).
#include "Asset/WorldChunk.h"
#include "Asset/ByteReader.h"
#include "Asset/FileUtil.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"
#include <cstdio>
#include <cstring>

namespace ts2::asset {
namespace {

// ---- helpers bornés (calqués sur Reader.skip/take du parseur Python) ---------

// Vérifie n <= restant en 64 bits (évite le débordement de size_t en build 32-bit)
// puis saute n octets. Lève AssetError si hors limites (comme Reader.skip).
void SkipChecked(ByteReader& r, uint64_t n) {
    if (n > r.Remaining()) throw AssetError("saut hors limites");
    r.Skip(static_cast<size_t>(n));
}

// Copie n octets consécutifs dans un vecteur, en validant d'abord la borne.
std::vector<uint8_t> ReadBlob(ByteReader& r, uint64_t n) {
    if (n > r.Remaining()) throw AssetError("lecture de bloc hors limites");
    std::vector<uint8_t> v(static_cast<size_t>(n));
    if (n) r.Read(v.data(), static_cast<size_t>(n));
    return v;
}

// read_gxd_block : [rawSize u32][packedSize u32][zlib] -> octets décompressés.
// Fait avancer r de 8 + packedSize octets. InflateTo valide len == rawSize.
std::vector<uint8_t> ReadGxdBlock(ByteReader& r, uint32_t& rawOut, uint32_t& packedOut) {
    const uint32_t rawSize = r.U32();
    const uint32_t packed  = r.U32();
    if (r.Remaining() < packed)
        throw AssetError("bloc GXD : packedSize dépasse le flux");
    std::vector<uint8_t> out = Zlib::Instance().InflateTo(r.Ptr(), packed, rawSize);
    r.Skip(packed);
    rawOut = rawSize;
    packedOut = packed;
    return out;
}

// read_texture_block : [imageSize u32] ; si 0 -> absente (present=false).
// Sinon [rawSize u32][packedSize u32][zlib] -> image(imageSize)+trailer(8o).
TextureBlock ReadTextureBlock(ByteReader& r) {
    TextureBlock tb;
    const uint32_t imageSize = r.U32();
    if (imageSize == 0) return tb; // present reste false

    const uint32_t rawSize = r.U32();
    const uint32_t packed  = r.U32();
    if (r.Remaining() < packed)
        throw AssetError("texture : packedSize dépasse le flux");
    std::vector<uint8_t> out = Zlib::Instance().InflateTo(r.Ptr(), packed, rawSize);
    r.Skip(packed);
    if (rawSize != imageSize + 8)
        throw AssetError("texture : rawSize != imageSize+8");

    tb.present    = true;
    tb.imageSize  = imageSize;
    tb.packedSize = packed;

    // Type d'image d'après les 4 octets magiques (identique au parseur Python).
    if (out.size() >= 4 && std::memcmp(out.data(), "DDS ", 4) == 0) {
        tb.imgType = "DDS";
    } else if (out.size() >= 2 && out[0] == 'B' && out[1] == 'M') {
        tb.imgType = "BMP";
    } else {
        char hex[9] = {0};
        const size_t k = out.size() < 4 ? out.size() : 4;
        for (size_t i = 0; i < k; ++i) std::snprintf(hex + i * 2, 3, "%02x", out[i]);
        tb.imgType = hex;
    }

    // Trailer : deux u32 à l'offset imageSize (out fait exactement imageSize+8).
    std::memcpy(tb.trailer, out.data() + imageSize, 8);
    // Image décodée = les imageSize premiers octets.
    tb.data.assign(out.begin(), out.begin() + imageSize);
    return tb;
}

// read_anim_track : [present u32] ; si !=0 -> bloc GXD (8o en-tête + 28o*frames*tracks).
AnimTrack ReadAnimTrack(ByteReader& r) {
    AnimTrack at;
    const uint32_t present = r.U32();
    if (present == 0) return at;

    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> out = ReadGxdBlock(r, raw, packed);
    if (out.size() < 8)
        throw AssetError("anim : bloc trop court pour l'en-tête");
    uint32_t numFrames = 0, numTracks = 0;
    std::memcpy(&numFrames, out.data() + 0, 4);
    std::memcpy(&numTracks, out.data() + 4, 4);
    const uint64_t expect = 8ull + 28ull * numFrames * numTracks;
    if (raw != expect)
        throw AssetError("anim : rawSize != 8 + 28*frames*tracks");

    at.present = true;
    at.frames  = numFrames;
    at.tracks  = numTracks;
    at.data    = std::move(out);
    return at;
}

// parse_collision_mesh : opère sur le buffer déjà décompressé (buf déplacé dans mesh.raw).
CollisionMesh ParseCollisionMesh(std::vector<uint8_t> buf) {
    CollisionMesh cm;
    ByteReader m(buf);
    const uint32_t numTri = m.U32();
    SkipChecked(m, 156ull * numTri);        // triangles : 156 octets chacun
    const uint32_t numNodes = m.U32();
    const uint32_t field34  = m.U32();       // (this+34) compteur global d'index
    uint32_t totalIdx = 0;
    for (uint32_t i = 0; i < numNodes; ++i) {
        SkipChecked(m, 12);                  // min[3] float
        SkipChecked(m, 12);                  // max[3] float
        const uint32_t numIdx = m.U32();     // nb d'index de triangles dans ce nœud
        const uint32_t hasIdx = m.U32();     // flag
        if (hasIdx) {
            SkipChecked(m, 4ull * numIdx);   // index u32
            totalIdx += numIdx;
        }
        SkipChecked(m, 16);                  // children[4] u32 (quadtree)
    }
    if (!m.Eof())
        throw AssetError("collision : octets restants après le quadtree");

    cm.numTri       = numTri;
    cm.numNodes     = numNodes;
    cm.field34      = field34;
    cm.totalIndices = totalIdx;
    cm.raw          = std::move(buf);
    return cm;
}

// read_meshpart : [present u32] ; si !=0 -> bloc GXD géométrie + tex1 + tex2 + matériaux.
WorldMeshPart ReadMeshPart(ByteReader& r) {
    WorldMeshPart mp;
    const uint32_t present = r.U32();
    if (present == 0) return mp;

    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> out = ReadGxdBlock(r, raw, packed);
    if (out.size() < 136)
        throw AssetError("meshpart : bloc géométrie trop court");
    uint32_t A = 0, B = 0, C = 0, D = 0;
    std::memcpy(&A, out.data() + 120, 4);    // Heap[30]
    std::memcpy(&B, out.data() + 124, 4);    // Heap[31]
    std::memcpy(&C, out.data() + 128, 4);    // Heap[32]
    std::memcpy(&D, out.data() + 132, 4);    // Heap[33] (nb triangles)
    const uint64_t expect =
        136ull + (static_cast<uint64_t>(A) << 6) +
        32ull * A * B + 6ull * D;

    mp.present   = true;
    mp.A = A; mp.B = B; mp.C = C; mp.D = D;
    mp.geoSizeOk = (static_cast<uint64_t>(raw) == expect);
    mp.geo       = std::move(out);
    mp.tex1      = ReadTextureBlock(r);      // this+296
    mp.tex2      = ReadTextureBlock(r);      // this+348
    const uint32_t numMat = r.U32();         // this+400
    mp.materials.reserve(numMat);
    for (uint32_t i = 0; i < numMat; ++i)
        mp.materials.push_back(ReadTextureBlock(r)); // this+404[]
    return mp;
}

// read_model : [present u32] ; si !=0 -> [numParts u32] + numParts MeshPart.
Model ReadModel(ByteReader& r) {
    Model md;
    const uint32_t present = r.U32();
    if (present == 0) return md;
    const uint32_t numParts = r.U32();
    md.present = true;
    md.parts.reserve(numParts);
    for (uint32_t i = 0; i < numParts; ++i)
        md.parts.push_back(ReadMeshPart(r));
    return md;
}

// read_fxnode : [present u32] ; si !=0 -> texture + piste anim + 144o de champs.
FxNode ReadFxNode(ByteReader& r) {
    FxNode fn;
    const uint32_t present = r.U32();
    if (present == 0) return fn;
    fn.tex    = ReadTextureBlock(r);         // Tex_LoadCompressedFromHandle
    fn.anim   = ReadAnimTrack(r);            // Anim_LoadQuatTrackFromHandle
    fn.fields = ReadBlob(r, 144);            // 144 octets de champs fixes
    fn.present = true;
    return fn;
}

// ---- parseurs de fichier complet --------------------------------------------

// parse_WM (et .WJ) : un seul bloc GXD = maillage de collision, puis EOF.
MapCollisionChunk ParseWM(ByteReader& r) {
    MapCollisionChunk out;
    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> buf = ReadGxdBlock(r, raw, packed);
    out.mesh       = ParseCollisionMesh(std::move(buf));
    out.rawSize    = raw;
    out.packedSize = packed;
    if (!r.Eof())
        throw AssetError("WM/WJ : octets après le bloc GXD");
    return out;
}

// parse_WG : bloc GXD géométrie + [numMat u32] + numMat textures + table d'index.
MapFaceChunk ParseWG(ByteReader& r) {
    MapFaceChunk out;
    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> buf = ReadGxdBlock(r, raw, packed);
    out.mesh      = ParseCollisionMesh(std::move(buf));
    out.geoRaw    = raw;
    out.geoPacked = packed;

    const uint32_t numMat = r.U32();         // this+3
    out.textures.reserve(numMat);
    for (uint32_t i = 0; i < numMat; ++i)
        out.textures.push_back(ReadTextureBlock(r));
    out.materialIndices.reserve(numMat);     // this+36 : table d'index matériaux
    for (uint32_t i = 0; i < numMat; ++i)
        out.materialIndices.push_back(r.U32());
    out.numMaterials = numMat;

    if (!r.Eof())
        throw AssetError("WG : octets restants à EOF");
    return out;
}

// Extrait le nom NUL-terminé ASCII/MBCS en tête d'un enregistrement placements[] de 100 o
// (le reste est du bourrage non lu par le moteur, cf. Docs/TS2_WO_PLACEMENT_FORMAT.md).
std::string ExtractPlacementName(const uint8_t* rec, size_t recSize) {
    const void* nul = std::memchr(rec, '\0', recSize);
    const size_t len = nul ? (static_cast<const uint8_t*>(nul) - rec) : recSize;
    return std::string(reinterpret_cast<const char*>(rec), len);
}

// Décode le tableau d'instances placées (28 o/instance sur disque, format confirmé par
// désassemblage : voir AuxRecord dans WorldChunk.h et Docs/TS2_WO_PLACEMENT_FORMAT.md).
std::vector<AuxRecord> ReadAuxRecords(ByteReader& r, uint32_t numAux) {
    std::vector<AuxRecord> out;
    out.reserve(numAux);
    for (uint32_t i = 0; i < numAux; ++i) {
        AuxRecord rec;
        rec.modelIndex = r.U32();
        rec.pos[0] = r.F32(); rec.pos[1] = r.F32(); rec.pos[2] = r.F32();
        rec.rot[0] = r.F32(); rec.rot[1] = r.F32(); rec.rot[2] = r.F32();
        out.push_back(rec);
    }
    return out;
}

// parse_WO : [numModels u32] + modèles + placements + [numAux u32] + records aux.
ObjectChunk ParseWO(ByteReader& r) {
    ObjectChunk out;
    const uint32_t numModels = r.U32();      // this+23
    if (numModels == 0) {
        out.empty = true;
        if (!r.Eof()) throw AssetError("WO : données après numModels=0");
        return out;
    }
    out.models.reserve(numModels);
    for (uint32_t i = 0; i < numModels; ++i)
        out.models.push_back(ReadModel(r));
    out.placements = ReadBlob(r, 100ull * numModels); // this+25 : 100o/modèle (métadonnée gabarit)
    out.placementNames.reserve(numModels);
    for (uint32_t i = 0; i < numModels; ++i)
        out.placementNames.push_back(
            ExtractPlacementName(out.placements.data() + 100ull * i, 100));
    const uint32_t numAux = r.U32();                  // this+26
    out.auxRecords = ReadAuxRecords(r, numAux);       // LE PLACEMENT : 28o/instance (modelIndex+pos+rot)
    out.numAux = numAux;
    if (!r.Eof())
        throw AssetError("WO : octets restants à EOF");
    return out;
}

// parse_WP : [numFx u32] + nœuds FX + placements + [numFxb u32] + records B.
FxChunk ParseWP(ByteReader& r) {
    FxChunk out;
    const uint32_t numFx = r.U32();          // this+28
    if (numFx == 0) {
        out.empty = true;
        if (!r.Eof()) throw AssetError("WP : données après numFx=0");
        return out;
    }
    out.nodes.reserve(numFx);
    for (uint32_t i = 0; i < numFx; ++i)
        out.nodes.push_back(ReadFxNode(r));
    out.placements = ReadBlob(r, 100ull * numFx);     // this+30 : 100o/fx
    const uint32_t numFxb = r.U32();                  // this+31
    out.fxbRecords = ReadBlob(r, 28ull * numFxb);     // 28o disque (4+12+12)
    out.numFxb = numFxb;
    if (!r.Eof())
        throw AssetError("WP : octets restants à EOF");
    return out;
}

} // namespace (anonyme)

// ---- API publique -----------------------------------------------------------

void WorldChunk::Reset() {
    type_ = WorldChunkType::Unknown;
    collision_.reset();
    face_.reset();
    objects_.reset();
    fx_.reset();
}

bool WorldChunk::Load(const std::string& path) {
    const WorldChunkType t = WorldChunkTypeFromExtension(path);
    if (t == WorldChunkType::Unknown) {
        TS2_ERR("WorldChunk : extension inconnue : %s", path.c_str());
        return false;
    }
    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, data)) {
        TS2_ERR("WorldChunk : ouverture impossible : %s", path.c_str());
        return false;
    }
    return LoadFromMemory(data, t);
}

bool WorldChunk::LoadFromMemory(const std::vector<uint8_t>& data, WorldChunkType type) {
    Reset();
    type_ = type;
    try {
        ByteReader r(data);
        switch (type) {
            case WorldChunkType::WM:
            case WorldChunkType::WJ:
                collision_ = ParseWM(r);
                break;
            case WorldChunkType::WG:
                face_ = ParseWG(r);
                break;
            case WorldChunkType::WO:
                objects_ = ParseWO(r);
                break;
            case WorldChunkType::WP:
                fx_ = ParseWP(r);
                break;
            default:
                TS2_ERR("WorldChunk : type non supporté");
                Reset();
                return false;
        }
    } catch (const std::exception& ex) {
        TS2_ERR("WorldChunk : parsing échoué (%s)", ex.what());
        Reset();
        return false;
    }
    return true;
}

std::string WorldChunk::Describe() const {
    char buf[256] = {0};
    switch (type_) {
        case WorldChunkType::WM:
        case WorldChunkType::WJ:
            if (collision_)
                std::snprintf(buf, sizeof(buf),
                    "%s numTri=%u numNodes=%u field34=%u totalIndices=%u raw=%u packed=%u",
                    WorldChunkTypeName(type_), collision_->mesh.numTri,
                    collision_->mesh.numNodes, collision_->mesh.field34,
                    collision_->mesh.totalIndices, collision_->rawSize, collision_->packedSize);
            break;
        case WorldChunkType::WG:
            if (face_) {
                uint32_t texOk = 0, texEmpty = 0;
                for (const auto& t : face_->textures) { if (t.present) ++texOk; else ++texEmpty; }
                std::snprintf(buf, sizeof(buf),
                    "WG numTri=%u numNodes=%u numMaterials=%u texturesOk=%u texturesEmpty=%u",
                    face_->mesh.numTri, face_->mesh.numNodes, face_->numMaterials, texOk, texEmpty);
            }
            break;
        case WorldChunkType::WO:
            if (objects_) {
                uint32_t totalParts = 0;
                for (const auto& m : objects_->models) totalParts += static_cast<uint32_t>(m.parts.size());
                std::snprintf(buf, sizeof(buf),
                    "WO numModels=%u totalParts=%u numAux=%u%s",
                    static_cast<uint32_t>(objects_->models.size()), totalParts,
                    objects_->numAux, objects_->empty ? " (empty)" : "");
            }
            break;
        case WorldChunkType::WP:
            if (fx_) {
                uint32_t withDds = 0;
                for (const auto& n : fx_->nodes) if (n.tex.present) ++withDds;
                std::snprintf(buf, sizeof(buf),
                    "WP numFx=%u fxWithDds=%u numFxb=%u%s",
                    static_cast<uint32_t>(fx_->nodes.size()), withDds,
                    fx_->numFxb, fx_->empty ? " (empty)" : "");
            }
            break;
        default:
            std::snprintf(buf, sizeof(buf), "WorldChunk vide/inconnu");
            break;
    }
    return std::string(buf);
}

WorldChunkType WorldChunkTypeFromExtension(const std::string& path) {
    // Extrait l'extension (après le dernier '.'), en majuscules.
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return WorldChunkType::Unknown;
    std::string ext = path.substr(dot + 1);
    for (char& c : ext)
        c = static_cast<char>((c >= 'a' && c <= 'z') ? c - ('a' - 'A') : c);
    if (ext == "WM") return WorldChunkType::WM;
    if (ext == "WJ") return WorldChunkType::WJ;
    if (ext == "WG") return WorldChunkType::WG;
    if (ext == "WO") return WorldChunkType::WO;
    if (ext == "WP") return WorldChunkType::WP;
    return WorldChunkType::Unknown;
}

const char* WorldChunkTypeName(WorldChunkType t) {
    switch (t) {
        case WorldChunkType::WM: return "WM";
        case WorldChunkType::WJ: return "WJ";
        case WorldChunkType::WG: return "WG";
        case WorldChunkType::WO: return "WO";
        case WorldChunkType::WP: return "WP";
        default:                 return "?";
    }
}

} // namespace ts2::asset

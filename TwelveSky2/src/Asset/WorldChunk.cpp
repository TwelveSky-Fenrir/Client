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

// Copie n octets consécutifs dans un vecteur, en validant d'abord la borne.
std::vector<uint8_t> ReadBlob(ByteReader& r, uint64_t n) {
    if (n > r.Remaining()) throw AssetError("lecture de bloc hors limites");
    std::vector<uint8_t> v(static_cast<size_t>(n));
    if (n) r.Read(v.data(), static_cast<size_t>(n));
    return v;
}

// read_gxd_block : [rawSize u32][packedSize u32][zlib] -> octets décompressés.
// Fait avancer r de 8 + packedSize octets. InflateTo valide len == rawSize.
// GXD_DecompressEntity 0x6A1A30. ex-VeryOldClient: ZlibScope.h (framing partagé, CONFIRMED).
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
// Tex_LoadCompressedFromHandle 0x6A9CF0. ex-VeryOldClient: TEXTURE_FOR_GXD (trailer 8 o) —
// CONFIRMED : texture monde = zlib pur ; le LoadGXCW VeryOld (chemin SOBJECT3) ne s'applique
// PAS au monde (IDA gagne, aucune substitution GXCW).
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
// Anim_LoadQuatTrackFromHandle 0x6AAE20. ex-VeryOldClient: MOTION_MATRIX (record 28 o =
// keyframe quaternion+translation, réutilisé, CONFIRMED).
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

// Décode un sommet terrain 40o (FVF 530 = XYZ|NORMAL|TEX2). Gap G7.
// Position@0 (prouvée barycentrique) puis normal@12, uv0@24, uv1@32 (interprétation FVF).
void ReadTerrainVertex(ByteReader& r, TerrainVertex& v) {
    v.position[0] = r.F32(); v.position[1] = r.F32(); v.position[2] = r.F32(); // +0
    v.normal[0]   = r.F32(); v.normal[1]   = r.F32(); v.normal[2]   = r.F32(); // +12
    v.uv0[0] = r.F32(); v.uv0[1] = r.F32();                                    // +24
    v.uv1[0] = r.F32(); v.uv1[1] = r.F32();                                    // +32
}

// parse_collision_mesh : opère sur le buffer déjà décompressé (buf déplacé dans mesh.raw).
// Décodage TYPÉ (Gaps G4/G5/G7) : ordre de lecture = miroir de MapColl_LoadFaces 0x694510.
// Layouts prouvés via IDA (voir WorldChunk.h : CollisionFace/CollisionQuadNode/TerrainVertex).
CollisionMesh ParseCollisionMesh(std::vector<uint8_t> buf) {
    CollisionMesh cm;
    ByteReader m(buf);

    // --- Faces (Gap G4) : numTri × 156o -----------------------------------
    const uint32_t numTri = m.U32();
    cm.tris.reserve(numTri);
    cm.vertices.reserve(static_cast<size_t>(numTri) * 3u);
    for (uint32_t i = 0; i < numTri; ++i) {
        CollisionFace f{};
        f.materialIndex = m.U32();               // +0
        ReadTerrainVertex(m, f.v0);              // +4
        ReadTerrainVertex(m, f.v1);              // +44
        ReadTerrainVertex(m, f.v2);              // +84
        f.plane[0] = m.F32(); f.plane[1] = m.F32();          // +124/+128 (a,b)
        f.plane[2] = m.F32(); f.plane[3] = m.F32();          // +132/+136 (c,d)
        f.sphereCenter[0] = m.F32();                          // +140
        f.sphereCenter[1] = m.F32();                          // +144
        f.sphereCenter[2] = m.F32();                          // +148
        f.sphereRadius    = m.F32();                          // +152
        // Sommets à plat (miroir du VB dynamique Terrain_Render a1+164, upload 120o/face).
        cm.vertices.push_back(f.v0);
        cm.vertices.push_back(f.v1);
        cm.vertices.push_back(f.v2);
        cm.tris.push_back(f);
    }

    // --- Quadtree (Gap G5) : numNodes nœuds disque -> nœuds 48o + index agrégé ---
    const uint32_t numNodes = m.U32();
    const uint32_t field34  = m.U32();       // (this+34) compteur global d'index (leafFaceRefTotal)
    cm.nodes.reserve(numNodes);
    uint32_t totalIdx = 0;
    for (uint32_t i = 0; i < numNodes; ++i) {
        CollisionQuadNode node{};
        node.bboxMin[0] = m.F32(); node.bboxMin[1] = m.F32(); node.bboxMin[2] = m.F32(); // +0
        node.bboxMax[0] = m.F32(); node.bboxMax[1] = m.F32(); node.bboxMax[2] = m.F32(); // +12
        const uint32_t numIdx = m.U32();     // +24 disque : nb d'index de faces dans ce nœud
        const uint32_t hasIdx = m.U32();     // +28 disque : flag hasFaceRefs
        node.trisNum   = numIdx;
        node.trisIndex = static_cast<uint32_t>(cm.triIndices.size()); // offset dans le buffer agrégé
        if (hasIdx) {
            for (uint32_t k = 0; k < numIdx; ++k)
                cm.triIndices.push_back(m.U32()); // index u32 dans tris[] (feuille)
            totalIdx += numIdx;
        }
        node.child[0] = m.I32();             // +32 disque : children[4] u32
        node.child[1] = m.I32();             //   child[0]==-1 => feuille
        node.child[2] = m.I32();
        node.child[3] = m.I32();
        cm.nodes.push_back(node);
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

// read_fxnode : [present u32] ; si !=0 -> texture + piste anim + 144o de champs émetteur.
// Ancre IDA : Fx_NodeLoadFromHandle 0x6a69f0 (18 ReadFile après texture+anim = 144 o disque).
FxNode ReadFxNode(ByteReader& r) {
    FxNode fn;
    const uint32_t present = r.U32();
    if (present == 0) return fn;
    fn.tex    = ReadTextureBlock(r);         // this+1  Tex_LoadCompressedFromHandle 0x6a9cf0
    fn.anim   = ReadAnimTrack(r);            // this+14 Anim_LoadQuatTrackFromHandle 0x6aae20
    fn.fields = ReadBlob(r, 144);            // [runtime +72, +216) : blob brut (parseur inchangé)
    fn.present = true;

    // Vue typée décodée depuis `fields` (offset = runtime - 72). Les 144 o sont lus dans l'ordre
    // exact de Fx_NodeLoadFromHandle 0x6a69f0 ; seuls les champs à sémantique cohérente sont
    // exposés (la queue +132.. reste dans `fields`, cf. WorldChunk.h).
    const uint8_t* f = fn.fields.data();
    auto rf = [f](size_t off) { float v; std::memcpy(&v, f + off, 4); return v; };
    auto ru = [f](size_t off) { uint32_t v; std::memcpy(&v, f + off, 4); return v; };
    fn.lifetime     = rf(0);    // +72
    fn.kfFps        = rf(4);    // +76
    fn.rate         = rf(8);    // +80
    fn.shape        = ru(12);   // +84
    fn.speed        = rf(16);   // +88
    fn.box[0] = rf(20); fn.box[1] = rf(24); fn.box[2] = rf(28);          // +92
    fn.particleLife = rf(32);   // +104
    fn.minRange[0] = rf(36); fn.minRange[1] = rf(40); fn.minRange[2] = rf(44); // +108
    fn.maxRange[0] = rf(48); fn.maxRange[1] = rf(52); fn.maxRange[2] = rf(56); // +120
    fn.accelMin[0] = rf(100); fn.accelMin[1] = rf(104); fn.accelMin[2] = rf(108); // +172
    fn.accelMax[0] = rf(112); fn.accelMax[1] = rf(116); fn.accelMax[2] = rf(120); // +184
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

// Décode les instances FX placées (28 o/instance : nodeIndex u32 + pos 12o + rot 12o).
// Ancre IDA : MapColl_LoadObjectsB 0x6983b0 @0x698602 (ReadFile +0/4, +4/12, +16/12).
std::vector<AuxFxRecord> ReadAuxFxRecords(ByteReader& r, uint32_t numFxb) {
    std::vector<AuxFxRecord> out;
    out.reserve(numFxb);
    for (uint32_t i = 0; i < numFxb; ++i) {
        AuxFxRecord rec;
        rec.nodeIndex = r.U32();                                        // +0
        rec.pos[0] = r.F32(); rec.pos[1] = r.F32(); rec.pos[2] = r.F32(); // +4
        rec.rot[0] = r.F32(); rec.rot[1] = r.F32(); rec.rot[2] = r.F32(); // +16
        out.push_back(rec);
    }
    return out;
}

// parse_WP : [numFx u32] + nœuds FX + placements + [numFxb u32] + records B (instances placées).
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
    out.fxbRecords = ReadAuxFxRecords(r, numFxb);     // 28o disque (nodeIndex+pos+rot)
    out.numFxb = numFxb;
    if (!r.Eof())
        throw AssetError("WP : octets restants à EOF");
    return out;
}

// read_meshpart_B : reproduit UNE part de cMesh_ReadFromStream 0x436CA0 (Format B). Renvoie
// present==false quand le flag de tête est 0 (fin du walker). RÈGLE #4 : le bloc géométrie est
// du zlib PUR (ReadGxdBlock), jamais XTEA/GXCW. Textures = ReadTextureBlock (framing identique
// à Tex_ReadPacked 0x417740, prouvé).
MeshFormatBPart ReadMeshFormatBPart(ByteReader& r) {
    MeshFormatBPart mp;
    const uint32_t present = r.U32();        // a1+188
    if (present == 0) return mp;             // fin de walker

    uint32_t raw = 0, packed = 0;
    std::vector<uint8_t> heap = ReadGxdBlock(r, raw, packed); // [rawSize][packedSize][zlib]
    if (heap.size() < 176)
        throw AssetError("meshB : bloc géométrie trop court (<176)");
    std::memcpy(mp.header, heap.data() + 0, 136);        // qmemcpy(a1+192, Heap, 0x88)
    std::memcpy(mp.subHeader, heap.data() + 136, 40);    // qmemcpy(a1+144, Heap+136, 0x28)
    std::memcpy(&mp.numVerts, mp.header + 120, 4);       // a1+312
    std::memcpy(&mp.C,        mp.header + 124, 4);       // a1+316
    std::memcpy(&mp.numFaces, mp.header + 132, 4);       // a1+324

    const size_t vbBytes = 32ull * mp.numVerts;
    const size_t ibBytes = 6ull * mp.numFaces;
    // Heap+176 = stream 0 ; Heap+176+32*B = stream 1 ; puis 6*D indices.
    if (heap.size() < 176 + 2 * vbBytes + ibBytes)
        throw AssetError("meshB : bloc géométrie incohérent (streams/indices hors limites)");
    const uint8_t* p = heap.data() + 176;
    mp.vb0.assign(p, p + vbBytes);          p += vbBytes;   // a1+348 (Crt_Memcpy Heap+176)
    mp.vb1.assign(p, p + vbBytes);          p += vbBytes;   // a1+352 (Heap+176+32*B)
    mp.ib.assign(p, p + ibBytes);                            // a1+356 (6*D)

    mp.tex1 = ReadTextureBlock(r);          // Tex_ReadPacked(a1+368)
    mp.tex2 = ReadTextureBlock(r);          // Tex_ReadPacked(a1+424)
    const uint32_t numMat = r.U32();        // a1+480
    mp.materials.reserve(numMat);
    for (uint32_t i = 0; i < numMat; ++i)
        mp.materials.push_back(ReadTextureBlock(r)); // a1+484 (56o runtime chacune)

    mp.present = true;
    return mp;
}

// parse_SOBJECT_B : walker multi-part. Boucle ReadMeshFormatBPart tant que present != 0
// (chaque part auto-délimitée par son propre flag de tête). Ancre IDA : cMesh_ReadFromStream
// 0x436CA0 rappelé en boucle par l'appelant du client d'origine.
MeshFormatBChunk ParseMeshFormatB(ByteReader& r) {
    MeshFormatBChunk out;
    for (;;) {
        MeshFormatBPart part = ReadMeshFormatBPart(r);
        if (!part.present) break;   // flag 0 -> plus de part
        out.parts.push_back(std::move(part));
        if (r.Eof()) break;         // flux épuisé (le flag 0 final peut être absent en fin de fichier)
    }
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
    meshB_.reset();
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
            case WorldChunkType::SOBJECT_B:
                meshB_ = ParseMeshFormatB(r);
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
        case WorldChunkType::SOBJECT_B:
            if (meshB_) {
                uint32_t totalVerts = 0, totalFaces = 0;
                for (const auto& p : meshB_->parts) { totalVerts += p.numVerts; totalFaces += p.numFaces; }
                std::snprintf(buf, sizeof(buf),
                    "SOBJECT_B parts=%u totalVerts=%u totalFaces=%u",
                    static_cast<uint32_t>(meshB_->parts.size()), totalVerts, totalFaces);
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
        case WorldChunkType::SOBJECT_B: return "SOBJECT_B";
        default:                 return "?";
    }
}

} // namespace ts2::asset

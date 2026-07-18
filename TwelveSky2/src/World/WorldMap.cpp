// World/WorldMap.cpp — faithful implementation of the TwelveSky2 world/zone loaders.
// See WorldMap.h for source EAs. All branches of the four target functions are
// covered; file paths and the zoneId->fileId table are byte-exact.
// (Zone-model path resolution and zone resource dispatch live in WorldMap_ZoneModel.cpp:
//  ZoneModelPathWM/WJ, CurrentZoneModelPath, LoadZoneResource, LoadCurrentZoneModel.)
#include "WorldMap.h"
#include "Asset/WorldChunk.h" // typed terrain views (CollisionMesh/Face/QuadNode/TerrainVertex) — Gaps G4/G5/G7

#include <algorithm> // std::min/std::max — AABB bounds for sweeps (namespace collision, WG-02/WG-03)
#include <cmath>   // sqrt/fabs — collision queries (namespace collision, Gaps G02/G03/G04)
#include <cstdint>
#include <cstdio>

namespace ts2::world {

namespace {
// Empty returns when no mesh is bound (build-safe: never a null dereference).
const std::vector<asset::CollisionFace>     kEmptyFaces;
const std::vector<asset::CollisionQuadNode> kEmptyNodes;
const std::vector<asset::TerrainVertex>     kEmptyVertices;
const std::vector<uint32_t>                 kEmptyFaceIndices;
} // namespace

// ===========================================================================
// Typed terrain data accessors (Gaps G4/G5/G7). Forward to the mesh bound
// by SetCollisionMesh; empty vectors if no mesh (see WorldMap.h).
// ===========================================================================
const std::vector<asset::CollisionFace>& WorldMap::Faces() const {
    return collisionMesh_ ? collisionMesh_->tris : kEmptyFaces;
}
const std::vector<asset::CollisionQuadNode>& WorldMap::Quadtree() const {
    return collisionMesh_ ? collisionMesh_->nodes : kEmptyNodes;
}
const std::vector<asset::TerrainVertex>& WorldMap::Vertices() const {
    return collisionMesh_ ? collisionMesh_->vertices : kEmptyVertices;
}
const std::vector<uint32_t>& WorldMap::FaceIndices() const {
    return collisionMesh_ ? collisionMesh_->triIndices : kEmptyFaceIndices;
}

// ===========================================================================
// World_ZoneIdToFileId 0x4db0f0 — zoneId -> fileId table. Exact transcription
// of the switch (groups of `case`s that fall through to the same value are merged).
// ===========================================================================
int WorldMap::ZoneIdToFileId(int zoneId) {
    switch (zoneId) {
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 4;
        case 5: return 175;
        case 6: return 6;
        case 7: return 7;
        case 8: return 8;
        case 9: return 9;
        case 10: return 175;
        case 11: return 11;
        case 12: return 12;
        case 13: return 13;
        case 14: return 14;
        case 15: return 175;
        case 16: return 16;
        case 17: return 17;
        case 18: return 18;
        case 19: case 20: case 21: return 175;
        case 22: return 16;
        case 23: return 17;
        case 24: return 18;
        case 25: case 26: case 27: return 175;
        case 28: return 16;
        case 29: return 17;
        case 30: return 18;
        case 31: case 32: case 33: case 34: case 35: case 36: return 175;
        case 37: return 37;
        case 38: return 38;
        case 39: return 39;
        case 40: case 41: case 42: return 40;
        case 43: case 44: case 45: return 43;
        case 46: case 47: case 48: return 46;
        case 49: return 49;
        case 50: return 170;
        case 51: return 51;
        case 52: return 170;
        case 53: return 53;
        case 54: return 54;
        case 55: return 55;
        case 56: case 57: case 58: return 56;
        case 59: case 60: case 61: return 59;
        case 62: return 16;
        case 63: return 17;
        case 64: return 18;
        case 65: return 16;
        case 66: return 17;
        case 67: return 18;
        case 68: return 16;
        case 69: return 17;
        case 70: return 18;
        case 71: return 71;
        case 72: return 72;
        case 73: return 73;
        case 74: return 74;
        case 75: return 75;
        case 76: case 77: case 78: case 79: return 76;
        case 80: case 81: case 82: case 83: return 80;
        case 84: return 84;
        case 85: case 86: case 87: return 196;
        case 88: return 88;
        case 89: return 89;
        case 90: return 90;
        case 91: case 92: case 93: case 94: return 91;
        case 95: case 96: case 97: case 98: return 95;
        case 99: case 100: return 196;
        case 101: case 102: case 103: return 101;
        case 104: return 104;
        case 105: return 105;
        case 106: return 104;
        case 107: return 105;
        case 108: return 104;
        case 109: return 105;
        case 110: return 104;
        case 111: return 105;
        case 112: return 104;
        case 113: return 105;
        case 114: return 104;
        case 115: return 105;
        case 116: return 104;
        case 117: return 105;
        case 118: return 118;
        // NB: case 119 absent -> default (-1)
        case 120: case 121: case 122: return 154;
        case 123: return 175;
        case 124: return 124;
        case 125: return 125;
        case 126: case 127: case 128: case 129: case 130: case 131: case 132: case 133:
        case 134: case 135: case 136: case 137: return 126;
        case 138: case 139: return 138;
        case 140: return 140;
        case 141: return 141;
        case 142: return 142;
        case 143: return 143;
        case 144: case 145: return 39;
        case 146: return 49;
        case 147: return 51;
        case 148: return 53;
        case 149: return 49;
        case 150: return 51;
        case 151: case 152: case 153: return 53;
        case 154: return 154;
        case 155: return 155;
        case 156: return 156;
        case 157: return 154;
        case 158: return 155;
        case 159: return 156;
        case 160: return 154;
        case 161: return 155;
        case 162: case 163: case 164: return 156;
        case 165: case 166: return 138;
        case 167: return 101;
        case 168: return 104;
        case 169: return 105;
        case 170: return 170;
        case 171: case 172: case 173: case 174: return 126;
        case 175: case 176: case 177: case 178: case 179: case 180: case 181: case 182:
        case 183: case 184: case 185: case 186: case 187: case 188: case 189: case 190:
        case 191: case 192: case 193: return 175;
        case 194: return 194;
        case 195: return 195;
        case 196: case 197: case 198: case 199: return 196;
        case 200: return 297;
        case 201: return 201;
        case 202: case 203: case 204: case 205: return 202;
        case 206: case 207: case 208: case 209: return 206;
        case 210: case 211: case 212: case 213: case 214: case 215: case 216: case 217:
        case 218: case 219: case 220: case 221: return 126;
        case 222: case 223: case 224: case 225: case 226: case 227: case 228: case 229:
        case 230: case 231: case 232: case 233: return 222;
        case 234: return 234;
        case 235: return 235;
        case 236: return 236;
        case 237: return 237;
        case 238: return 235;
        case 239: return 236;
        case 240: return 237;
        case 241: case 242: case 243: case 244: case 245: case 246: case 247: case 248:
        case 249: return 241;
        case 250: return 267;
        case 251: return 104;
        case 252: return 105;
        case 253: return 104;
        case 254: return 105;
        case 255: return 104;
        case 256: return 105;
        case 257: return 104;
        case 258: return 105;
        case 259: return 104;
        case 260: return 105;
        case 261: return 104;
        case 262: return 105;
        case 263: return 104;
        case 264: return 105;
        case 265: return 104;
        case 266: return 105;
        case 267: case 268: case 269: return 267;
        case 270: case 271: case 272: case 273: case 274: return 270;
        case 275: case 276: case 277: case 278: return 275;
        case 279: case 280: case 281: case 282: return 279;
        case 283: case 284: case 285: case 286: return 283;
        case 287: case 288: case 289: case 290: return 287;
        case 291: return 291;
        case 292: case 293: case 294: return 241;
        case 295: case 296: return 154;
        case 297: case 298: case 299: return 297;
        case 300: case 301: return 300;
        case 302: return 302;
        case 303: return 303;
        case 304: case 305: case 306: case 307: return 304;
        case 308: return 308;
        // NB: case 309 absent -> default (-1)
        case 310: return 310;
        case 311: case 312: return 241;
        case 313: case 314: case 315: case 316: case 317: case 318: return 313;
        case 319: case 320: case 321: case 322: case 323: return 319;
        case 324: return 324;
        case 325: case 326: case 327: case 328: case 329: case 330: return 241;
        case 331: case 332: case 333: case 334: case 335: case 336: return 313;
        // NB: cases 337, 338 absent -> default (-1)
        case 339: return 118;
        case 340: return 340;
        case 341: return 341;
        case 342: return 342;
        default: return -1;
    }
}

// ===========================================================================
// World_LoadMap 0x4116b0 — DRM gate + atmosphere init + Atmosphere.DAT weather.
// ===========================================================================
bool WorldMap::LoadMap(const std::string& mapName, const std::string& drmKey) {
    // this+12 = device (already set via SetDevice; a3 in the binary).

    // --- "ALT1" DRM gate ---------------------------------------------------
    // if (Crt_OperatorNew(648)) atmo = cAtmosphere_ctor("ALT1 License 3", key) else atmo = 0
    void* atmo = nullptr;
    void* mem = hooks_.allocAtmosphere ? hooks_.allocAtmosphere(hooks_.user, 648) : nullptr;
    if (mem && hooks_.constructAtmosphere) {
        // cAtmosphere_ctor 0x791b40: validates the SilverLining license (const name + key).
        atmo = hooks_.constructAtmosphere(hooks_.user, mem, kAltLicenseName, drmKey.c_str());
    }
    atmosphere_ = atmo; // this+8

    // (*device)[+164]: device bracket before init.
    if (hooks_.deviceBeginMap) hooks_.deviceBeginMap(hooks_.user, device_);

    // cAtmosphere_Initialize(1, mapName, 0, device). Nonzero return => "failure" branch -> 0.
    int initRc = hooks_.atmosphereInitialize
                     ? hooks_.atmosphereInitialize(hooks_.user, atmosphere_, mapName.c_str(), device_)
                     : 0;

    // (*device)[+168]: device bracket after init (called on BOTH branches).
    if (hooks_.deviceEndMap) hooks_.deviceEndMap(hooks_.user, device_);

    if (initRc) {
        // Branch "if (Initialize(...)) { ...; return 0; }": failure/cancel.
        return false; // returns 0
    }

    // --- Success branch: atmosphere[+644]=1, this+4=1, default weather, Atmosphere.DAT ---
    // *(atmosphere+644)=1 (cAtmosphere internal marker) @0x411765 (eax=[ebx+8]=cAtmosphere
    // object, 0x284=644).
    // EW-02 (Pass 4/W11): the "C-03 fix" that armed the flag was a TRAP, reverted here (chain
    // re-proven in IDA, idaTs2 read-only). g_WorldEnv+4 (0x18C67C8, g_WorldEnvAtmInitFlag) is
    // reset to 0 on every zone entry -> World_LoadMap runs 1x/ZONE, not 1x/session:
    // World_LoadMap @0x41176E arms the flag (ebx=this=g_WorldEnv 0x18C67C4); step 1 of
    // Scene_EnterWorldUpdate case 1 @0x52C091 (counter incremented 0,1,...,19 @0x52C106, steps
    // run IN ORDER so step 1 always precedes step 7) = World_LoadZoneResource case 1 @0x4DCBB1
    // -> WSndMgr_Free 0x4DB060 @0x4DB09E -> World_UnloadMap 0x411A80 @0x411A9F clears the flag
    // (guarded by `if (this+8)` @0x411A89, true after the 1st load since this+8 = non-null
    // cAtmosphere) before step 7 (case 7 @0x4DD202) rereads it (=0) -> @0x4DD217 doesn't skip ->
    // World_LoadMap re-runs. The case-7 `||` short-circuit is thus a defensive guard never taken
    // in this flow. Before C-03 the C++ port never armed the flag -> LoadMap ran every zone:
    // ACCIDENTALLY CORRECT. C-03 armed the flag -> 1x/session = a fidelity REGRESSION, reverted
    // here: we do NOT arm `atmosphereLoaded_`, so step 7 (`proceed = atmosphereLoaded_`, always
    // false) replays LoadMap every zone = the binary's exact behavior. Do NOT port
    // World_UnloadMap "to compensate": that would be a harmful no-op. Its guard @0x411A89
    // `if (this+8)` -> in ClientSource `atmosphere_` is ALWAYS null (WorldAssets::AllocAtmosphere
    // returns nullptr by design, WorldIntegration.cpp:302) -> systematic early-return -> the flag
    // would NEVER be cleared -> the 1x/session bug would come back, plus 3 dangling hooks
    // (the "code nobody calls" anti-pattern). Reconsider ONLY if SilverLining ever gets linked
    // (atmosphere_ non-null), and then in WorldIntegration (EW-03), not here.
    valid_ = true;                       // this+4 = 1  @0x41176E — literal write, no reader outside WorldMap
    // (NOT `atmosphereLoaded_ = true`: see EW-02 above — flag left at 0 -> LoadMap 1x/zone.)
    // Str_Assign(mapName): stores the map name (byte_815190 in the binary) — omitted (leaf).
    // EW-05: qmemcpy(this+180, &dword_18C5358, 0x68) @0x4117A2 = global->instance SAVE of the
    // sun D3DLIGHT9 (0x68 = sizeof(D3DLIGHT9)), NOT "weather zeroed out". Not modeled here (no
    // reader of weather_): the fill(0) is an unobservable placeholder. See .h:275 and
    // Gfx/SilverLiningSky.cpp:217-222 / Env_UpdateSunLight 0x412210.
    weather_.fill(0);

    // File_IfstreamOpen("Atmosphere.DAT"): if absent/empty -> default geolocation (Seoul);
    // otherwise parse line by line (Istream_GetChar/Ostream_WritePad) then sub_4135F0 + World_FinishLoad.
    bool weatherOk = hooks_.loadWeatherDat
                         ? hooks_.loadWeatherDat(hooks_.user, kAtmosphereDatFile)
                         : false;
    if (!weatherOk) {
        if (hooks_.setGeoLocation)
            hooks_.setGeoLocation(hooks_.user, kDefaultGeoLat, kDefaultGeoLon, kDefaultGeoAlt);
    } else {
        if (hooks_.finishLoad) hooks_.finishLoad(hooks_.user); // World_FinishLoad 0x411c40
    }
    return true; // returns 1
}

// ===========================================================================
// namespace collision — terrain query engine (Gaps G02/G03/G04).
// Byte-faithful port of the MapColl_* functions (see WorldMap.h for the this[]->mesh
// correspondence). Every function carries the target's @EA anchors. All build-safe.
// ===========================================================================
namespace collision {
namespace {

// this[1]: the MapColl is active as soon as a mesh (faces + quadtree) is loaded.
inline bool MeshActive(const asset::CollisionMesh& m) {
    return !m.nodes.empty() && !m.tris.empty();
}
inline float Dot3(float ax, float ay, float az, float bx, float by, float bz) {
    return ax * bx + ay * by + az * bz;
}

// XZ point-location descent, shared by MapColl_GetGroundHeight 0x697130
// (0x697148..0x6971d9) and MapColl_PointInMeshXZ 0x695dc0 (0x695dd9..0x695e80).
// Returns the index of the LEAF containing (x,z), or -1 if (x,z) is outside the quadtree.
int LocateLeafXZ(const asset::CollisionMesh& m, float x, float z) {
    const auto& nodes = m.nodes;
    uint32_t nodeIdx = 0;                                   // root = this[35] index 0
    if (nodes[0].child[0] != -1) {                          // 0x697159: root is not a leaf
        for (;;) {
            const asset::CollisionQuadNode& n = nodes[nodeIdx];
            int c = 0;
            for (; c < 4; ++c) {                            // 0x697182: scan the 4 children
                const int32_t ci = n.child[c];
                if (ci < 0 || static_cast<size_t>(ci) >= nodes.size())
                    continue;                               // OOB guard (malformed data)
                const asset::CollisionQuadNode& cn = nodes[static_cast<size_t>(ci)];
                if (x >= cn.bboxMin[0] && x <= cn.bboxMax[0] &&   // 0x6971ba: XZ bbox test
                    z >= cn.bboxMin[2] && z <= cn.bboxMax[2])
                    break;
            }
            if (c == 4) return -1;                          // 0x6971c3: no containing child
            nodeIdx = static_cast<uint32_t>(n.child[c]);    // 0x6971c7: descend
            if (nodes[nodeIdx].child[0] == -1) break;       // 0x6971d9: leaf reached
        }
    }
    return static_cast<int>(nodeIdx);
}

} // namespace

// MapColl_RayHitTriangle 0x695ae0.
bool RayHitTriangle(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                    float px, float py, float pz) {
    if (faceIndex >= mesh.tris.size()) return false;        // guard (this[22] + 156*faceIndex)
    const asset::CollisionFace& f = mesh.tris[faceIndex];
    // edge0 = v1-v0, edge1 = v2-v0, q = point-v0 (0x695af4..0x695b47).
    const float e0x = f.v1.position[0] - f.v0.position[0];
    const float e0y = f.v1.position[1] - f.v0.position[1];
    const float e0z = f.v1.position[2] - f.v0.position[2];
    const float e1x = f.v2.position[0] - f.v0.position[0];
    const float e1y = f.v2.position[1] - f.v0.position[1];
    const float e1z = f.v2.position[2] - f.v0.position[2];
    const float qx = px - f.v0.position[0];
    const float qy = py - f.v0.position[1];
    const float qz = pz - f.v0.position[2];
    const float d00 = Dot3(e0x, e0y, e0z, e0x, e0y, e0z);   // |e0|^2
    const float d01 = Dot3(e1x, e1y, e1z, e0x, e0y, e0z);   // e1·e0
    const float dp0 = Dot3(qx, qy, qz, e0x, e0y, e0z);      // q·e0
    const float d11 = Dot3(e1x, e1y, e1z, e1x, e1y, e1z);   // |e1|^2
    const float dp1 = Dot3(qx, qy, qz, e1x, e1y, e1z);      // q·e1
    const float denom = d11 * d00 - d01 * d01;              // 0x695be8
    if (denom == 0.0f) return false;                        // 0x695bf9
    const float inv = 1.0f / denom;
    const float u = (d11 * dp0 - dp1 * d01) * inv;          // 0x695c12
    if (u < 0.0f) return false;                             // 0x695c23
    const float v = inv * (dp1 * d00 - dp0 * d01);          // 0x695c36
    if (v < 0.0f) return false;                             // 0x695c43
    return (u + v) <= 1.0f;                                 // 0x695c62
}

// MapColl_PointInTriangleXZ 0x695c70.
bool PointInTriangleXZ(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                       float px, float pz) {
    if (faceIndex >= mesh.tris.size()) return false;
    const asset::CollisionFace& f = mesh.tris[faceIndex];
    // Barycentric in the XZ plane (0x695c84..0x695d18).
    const float e0x = f.v1.position[0] - f.v0.position[0];
    const float e0z = f.v1.position[2] - f.v0.position[2];
    const float e1x = f.v2.position[0] - f.v0.position[0];
    const float e1z = f.v2.position[2] - f.v0.position[2];
    const float qx = px - f.v0.position[0];
    const float qz = pz - f.v0.position[2];
    const float d00 = e0z * e0z + e0x * e0x;                // |e0_xz|^2
    const float d01 = e1z * e0z + e1x * e0x;                // e1·e0 (xz)
    const float dp0 = qz * e0z + qx * e0x;                  // q·e0 (xz)
    const float d11 = e1z * e1z + e1x * e1x;                // |e1_xz|^2
    const float dp1 = qz * e1z + qx * e1x;                  // q·e1 (xz)
    const float denom = d11 * d00 - d01 * d01;              // 0x695d2b
    if (denom == 0.0f) return false;                        // 0x695d3c
    const float inv = 1.0f / denom;
    const float u = (d11 * dp0 - dp1 * d01) * inv;          // 0x695d55
    if (u < 0.0f) return false;                             // 0x695d66
    const float v = inv * (dp1 * d00 - dp0 * d01);          // 0x695d79
    if (v < 0.0f) return false;                             // 0x695d86
    return (u + v) <= 1.0f;                                 // 0x695da5
}

// MapColl_GetGroundHeight 0x697130 — "null ground" filled in.
bool GetGroundHeight(const asset::CollisionMesh& mesh, float x, float z,
                     float& outGroundY, bool a5CeilingGiven, float a6Ceiling,
                     bool a7TwoSide, bool a8OnlyOne) {
    if (!MeshActive(mesh)) return false;                    // 0x697135: if (!this[1]) return 0
    const int leaf = LocateLeafXZ(mesh, x, z);              // XZ descent down to the leaf
    if (leaf < 0) return false;                             // outside the quadtree (0x6971c3/0x697226)
    const asset::CollisionQuadNode& node = mesh.nodes[static_cast<size_t>(leaf)];
    const float ceiling = a5CeilingGiven ? a6Ceiling : mesh.nodes[0].bboxMax[1]; // 0x6971e5 (node0 +16)
    bool hit = false;                                       // v18
    for (uint32_t i = 0; i < node.trisNum; ++i) {           // 0x697215: leaf faces
        const size_t idx = static_cast<size_t>(node.trisIndex) + i;
        if (idx >= mesh.triIndices.size()) break;           // guard
        const uint32_t faceIdx = mesh.triIndices[idx];      // trisIndex[i] -> face
        if (faceIdx >= mesh.tris.size()) continue;          // guard
        const asset::CollisionFace& f = mesh.tris[faceIdx];
        const float b = f.plane[1];                         // this[22]+156*f+128 (= normal.y)
        if (!a7TwoSide && b <= 0.0f) continue;              // 0x697259: walkable filter
        if (b == 0.0f) continue;                            // 0x697288: division guard
        // y = (d - a*x - c*z) / b — plane-solve (0x6972ad, plane @+124/+128/+132/+136).
        const float y = (f.plane[3] - x * f.plane[0] - z * f.plane[2]) / b;
        if (y <= ceiling && RayHitTriangle(mesh, faceIdx, x, y, z)) {   // 0x6972ca
            if (!hit) {
                outGroundY = y;                             // 0x6972df
                hit = true;
                if (a8OnlyOne) return true;                 // 0x6972ec: 1st hit
            } else if (y > outGroundY) {
                outGroundY = y;                             // 0x6972fb: keep the highest
            }
        }
    }
    return hit;                                             // 0x697318
}

// World_IsPointOnGround 0x540d40.
bool IsPointOnGround(const asset::CollisionMesh& mesh, float x, float y, float z) {
    float out = 0.0f;                                       // ceiling = y+20; a5=1, a7=0, a8=1
    return GetGroundHeight(mesh, x, z, out, true, y + 20.0f, false, true); // 0x540d59/0x540d93
}

// MapColl_PointInMeshXZ 0x695dc0.
bool PointInMeshXZ(const asset::CollisionMesh& mesh, float x, float z) {
    if (!MeshActive(mesh)) return false;                    // 0x695dc3
    const int leaf = LocateLeafXZ(mesh, x, z);
    if (leaf < 0) return false;
    const asset::CollisionQuadNode& node = mesh.nodes[static_cast<size_t>(leaf)];
    for (uint32_t i = 0; i < node.trisNum; ++i) {           // 0x695e9d: no walkable filter
        const size_t idx = static_cast<size_t>(node.trisIndex) + i;
        if (idx >= mesh.triIndices.size()) break;
        if (PointInTriangleXZ(mesh, mesh.triIndices[idx], x, z)) return true;
    }
    return false;                                           // 0x695eb9/0x695ec4
}

// MapColl_RayPlaneTriHit 0x695ee0.
bool RayPlaneTriHit(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                    const float start[3], const float dir[3], float outHit[3], bool twoSide) {
    if (faceIndex >= mesh.tris.size()) return false;
    const asset::CollisionFace& f = mesh.tris[faceIndex];
    const float denom = f.plane[1] * dir[1] + f.plane[2] * dir[2] + f.plane[0] * dir[0]; // 0x695f1b: n·dir
    if (twoSide) {
        if (denom == 0.0f) return false;                    // 0x695f4a
    } else if (denom >= 0.0f) {
        return false;                                       // 0x695f30: single-sided face
    }
    // t = (d - n·start) / (n·dir) — 0x695f77.
    const float t = (f.plane[3]
                     - (f.plane[2] * start[2] + f.plane[0] * start[0] + f.plane[1] * start[1]))
                    / denom;
    if (t < 0.0f) return false;                             // 0x695f86
    outHit[0] = t * dir[0] + start[0];                      // 0x695f9d
    outHit[1] = t * dir[1] + start[1];
    outHit[2] = t * dir[2] + start[2];
    return RayHitTriangle(mesh, faceIndex, outHit[0], outHit[1], outHit[2]); // 0x695f32
}

// Collide_SegmentAABB 0x69fb20 — SAT segment(point p, vector dir) vs AABB [bmin,bmax].
// The disassembly's "*0.0" terms (AABB unit axes) are exactly 0 and are therefore
// elided without loss of fidelity.
bool SegmentAABB(const float p[3], const float dir[3],
                 const float bmin[3], const float bmax[3]) {
    if (p[0] >= bmin[0] && p[0] <= bmax[0] &&               // 0x69fb78: point inside the box
        p[1] >= bmin[1] && p[1] <= bmax[1] &&
        p[2] >= bmin[2] && p[2] <= bmax[2])
        return true;
    const float hx = (bmax[0] - bmin[0]) * 0.5f;            // half-extents
    const float hy = (bmax[1] - bmin[1]) * 0.5f;
    const float hz = (bmax[2] - bmin[2]) * 0.5f;
    const float mx = p[0] - (bmin[0] + bmax[0]) * 0.5f;     // box center -> point
    const float my = p[1] - (bmax[1] + bmin[1]) * 0.5f;
    const float mz = p[2] - (bmax[2] + bmin[2]) * 0.5f;
    // AABB face axes (0x69fc60/0x69fcbc/0x69fd06).
    if (std::fabs(mx) > hx && mx * dir[0] >= 0.0f) return false;
    if (std::fabs(my) > hy && my * dir[1] >= 0.0f) return false;
    if (std::fabs(mz) > hz && mz * dir[2] >= 0.0f) return false;
    const float adx = std::fabs(dir[0]);
    const float ady = std::fabs(dir[1]);
    const float adz = std::fabs(dir[2]);
    // Cross axes dir × AABB-axes (0x69fd86/0x69fdb9/0x69fde2).
    if (std::fabs(mz * dir[1] - my * dir[2]) > adz * hy + ady * hz) return false;
    if (std::fabs(mx * dir[2] - mz * dir[0]) > adz * hx + adx * hz) return false;
    if (ady * hx + adx * hy < std::fabs(my * dir[0] - mx * dir[1])) return false;
    return true;                                            // 0x69fc64
}

// MapColl_RaycastNearest 0x6960c0 — recursive quadtree descent, nearest impact.
bool RaycastNearest(const asset::CollisionMesh& mesh, uint32_t nodeIndex,
                    const float start[3], const float dir[3],
                    uint32_t& outFaceIndex, float outHit[3], bool twoSide) {
    if (nodeIndex >= mesh.nodes.size()) return false;       // guard (child == -1 => 0xFFFFFFFF)
    const asset::CollisionQuadNode& node = mesh.nodes[nodeIndex];
    if (node.trisNum == 0) return false;                    // 0x6960d7: subtree with no face
    if (!SegmentAABB(start, dir, node.bboxMin, node.bboxMax)) return false; // 0x6960fd
    float best = -1.0f;                                     // v34
    if (node.child[0] == -1) {                              // 0x696123: leaf
        for (uint32_t i = 0; i < node.trisNum; ++i) {
            const size_t idx = static_cast<size_t>(node.trisIndex) + i;
            if (idx >= mesh.triIndices.size()) break;       // guard
            const uint32_t faceIdx = mesh.triIndices[idx];
            float hp[3];
            if (RayPlaneTriHit(mesh, faceIdx, start, dir, hp, twoSide)) {   // 0x696282
                const float dx = hp[0] - start[0];
                const float dy = hp[1] - start[1];
                const float dz = hp[2] - start[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);  // 0x6962d1
                if (best == -1.0f || dist < best) {         // 0x6962e6 / 0x69631b
                    best = dist;
                    outFaceIndex = faceIdx;
                    outHit[0] = hp[0]; outHit[1] = hp[1]; outHit[2] = hp[2];
                }
            }
        }
    } else {                                                // 0x696129: internal node (4 children)
        for (int c = 0; c < 4; ++c) {
            uint32_t childFace = 0;
            float childHit[3];
            if (RaycastNearest(mesh, static_cast<uint32_t>(node.child[c]),
                               start, dir, childFace, childHit, twoSide)) {  // 0x69615d
                const float dx = childHit[0] - start[0];
                const float dy = childHit[1] - start[1];
                const float dz = childHit[2] - start[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);   // 0x6961a8
                if (best == -1.0f || dist < best) {         // 0x6961bd / 0x6961f0
                    best = dist;
                    outFaceIndex = childFace;
                    outHit[0] = childHit[0]; outHit[1] = childHit[1]; outHit[2] = childHit[2];
                }
            }
        }
    }
    return best != -1.0f;                                   // 0x69623f
}

// MapColl_SlideMoveGround 0x697330 — slides pinned to the walkable mesh (XZ) + ground resolve.
// The original returns the resolved y bits (or a passthrough); we expose a clean bool
// (ground found?) + outPos always filled.
bool SlideMoveGround(const asset::CollisionMesh& mesh, const float from[3],
                     const float to[3], float speed, float dt, float outPos[3]) {
    if (!MeshActive(mesh) || speed <= 0.0f || dt <= 0.0f) { // 0x697365
        outPos[0] = from[0]; outPos[1] = from[1]; outPos[2] = from[2]; // 0x6974a4
        return false;
    }
    const float maxStep = speed * dt;                       // 0x697380
    outPos[0] = from[0];                                    // 0x697384 (y resolved at the end)
    outPos[2] = from[2];                                    // 0x697392
    float dx = to[0] - outPos[0];
    float dz = to[2] - outPos[2];
    float dist = std::sqrt(dx * dx + dz * dz);              // 0x6973ae
    bool doSnap = (dist <= maxStep);                        // 0x6973b9
    if (!doSnap) {
        for (;;) {                                          // 0x6973cf: step by maxStep
            const float inv = 1.0f / dist;
            const float sx = dx * inv * maxStep + outPos[0];
            const float sz = dz * inv * maxStep + outPos[2];
            if (!PointInMeshXZ(mesh, sx, sz)) break;        // 0x6973f8: step outside mesh -> stop
            outPos[0] = sx; outPos[2] = sz;                 // 0x697405: commit the step
            dx = to[0] - outPos[0];
            dz = to[2] - outPos[2];
            dist = std::sqrt(dx * dx + dz * dz);            // 0x697426
            if (dist <= maxStep) { doSnap = true; break; }  // 0x697431: target reachable
        }
    }
    if (doSnap && PointInMeshXZ(mesh, to[0], to[2])) {      // 0x69744b: direct snap onto 'to'
        outPos[0] = to[0]; outPos[2] = to[2];               // 0x69745c
    }
    // Ground height at the final point (a5=0,a6=0,a7=0,a8=1) — 0x697476.
    const bool found = GetGroundHeight(mesh, outPos[0], outPos[2], outPos[1],
                                       false, 0.0f, false, true);
    if (!found) {                                           // 0x69747d: no ground -> stay in place
        outPos[0] = from[0]; outPos[1] = from[1]; outPos[2] = from[2];
    }
    return found;
}

// ===========================================================================
// WG-02 — CAMERA collision chain (Camera_UpdateCollision 0x538580): sphere sweep vs
// the .WG terrain quadtree. Byte-faithful port (@EA anchor on each block). The sweep operates
// on slot 0 (= g_GameWorld itself, proven @0x5387b9 `mov ecx, offset g_GameWorld`) = .WG.
// ===========================================================================

// Collide_AABBOverlap_0 0x6a0600 — boxA=[amin,amax] overlaps boxB=[bmin,bmax]?
bool AABBOverlap(const float amin[3], const float amax[3],
                 const float bmin[3], const float bmax[3]) {
    return amin[0] <= bmax[0] && amin[1] <= bmax[1] && amin[2] <= bmax[2]      // 0x6a065f
        && amax[0] >= bmin[0] && amax[1] >= bmin[1] && amax[2] >= bmin[2];
}

// Collide_ProjectTriOnAxis 0x69f9c0 — min/max of {v0·axis, v1·axis, v2·axis}. tri9 = {v0,v1,v2}.
void ProjectTriOnAxis(const float axis[3], const float tri9[9], float& outMin, float& outMax) {
    const float d0 = tri9[1] * axis[1] + tri9[2] * axis[2] + tri9[0] * axis[0]; // v0·axis 0x69f9e3
    const float d1 = tri9[4] * axis[1] + tri9[3] * axis[0] + tri9[5] * axis[2]; // v1·axis 0x69f9f9
    const float d2 = tri9[7] * axis[1] + tri9[6] * axis[0] + tri9[8] * axis[2]; // v2·axis 0x69fa14
    outMin = d0; outMax = d0;                                                   // 0x69fa1b/0x69fa20
    if (d1 >= outMin) { if (d1 > d0) outMax = d1; }                             // 0x69fa29/0x69fa37
    else outMin = d1;                                                           // 0x69fa2b
    if (d2 >= outMin) { if (d2 > outMax) outMax = d2; }                         // 0x69fa4a/0x69fa63
    else outMin = d2;                                                           // 0x69fa50
}

// Collide_ProjectBoxOnAxis 0x69fa80 — projects the OBB (center, axes9=3 rows 3x3, half) onto axis.
void ProjectBoxOnAxis(const float axis[3], const float center[3], const float axes9[9],
                      const float half[3], float& outMin, float& outMax) {
    const float c = center[1] * axis[1] + center[2] * axis[2] + center[0] * axis[0]; // 0x69faa2
    const float r = std::fabs((axes9[3] * axis[0] + axes9[4] * axis[1] + axes9[5] * axis[2]) * half[1])
                  + std::fabs((axes9[6] * axis[0] + axes9[7] * axis[1] + axes9[8] * axis[2]) * half[2])
                  + std::fabs((axes9[1] * axis[1] + axes9[2] * axis[2] + axes9[0] * axis[0]) * half[0]); // 0x69fb02
    outMin = c - r;                                                            // 0x69fb08
    outMax = c + r;                                                            // 0x69fb0c
}

// Collide_TriAABB 0x6a00e0 — SAT triangle vs AABB (1 normal + 3 AABB faces + 9 edge×axis).
// Vertices read @+4/+44/+84 (face.v0/v1/v2.position, cf. asset::CollisionFace).
bool TriAABB(const asset::CollisionFace& face, const float aabbMin[3], const float aabbMax[3]) {
    const float* v0 = face.v0.position;
    const float* v1 = face.v1.position;
    const float* v2 = face.v2.position;
    // Short-circuit: a vertex inside the AABB -> overlap (0x6a0150/0x6a01a2/0x6a01f4).
    if (v0[0] >= aabbMin[0] && v0[0] <= aabbMax[0] && v0[1] >= aabbMin[1] && v0[1] <= aabbMax[1] &&
        v0[2] >= aabbMin[2] && v0[2] <= aabbMax[2]) return true;
    if (v1[0] >= aabbMin[0] && v1[0] <= aabbMax[0] && v1[1] >= aabbMin[1] && v1[1] <= aabbMax[1] &&
        v1[2] >= aabbMin[2] && v1[2] <= aabbMax[2]) return true;
    if (v2[0] >= aabbMin[0] && v2[0] <= aabbMax[0] && v2[1] >= aabbMin[1] && v2[1] <= aabbMax[1] &&
        v2[2] >= aabbMin[2] && v2[2] <= aabbMax[2]) return true;

    // Flattened triangle + AABB center/half-extents + identity axes (0x6a01ff..0x6a02f2).
    const float tri9[9]   = { v0[0], v0[1], v0[2], v1[0], v1[1], v1[2], v2[0], v2[1], v2[2] };
    const float center[3] = { (aabbMin[0] + aabbMax[0]) * 0.5f,
                              (aabbMin[1] + aabbMax[1]) * 0.5f,
                              (aabbMin[2] + aabbMax[2]) * 0.5f };
    const float half[3]   = { (aabbMax[0] - aabbMin[0]) * 0.5f,
                              (aabbMax[1] - aabbMin[1]) * 0.5f,
                              (aabbMax[2] - aabbMin[2]) * 0.5f };
    const float axes9[9]  = { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f }; // v58

    // Edges (0x6a02fe..0x6a0374).
    const float e0[3] = { v1[0] - v0[0], v1[1] - v0[1], v1[2] - v0[2] };
    const float e1[3] = { v2[0] - v1[0], v2[1] - v1[1], v2[2] - v1[2] };
    const float e2[3] = { v0[0] - v2[0], v0[1] - v2[1], v0[2] - v2[2] };

    float triMin, triMax, boxMin, boxMax;
    // Axis 0: triangle normal = cross(e0, e1) (0x6a03a3..0x6a03e1).
    {
        const float n[3] = { e1[2] * e0[1] - e1[1] * e0[2],
                             e1[0] * e0[2] - e1[2] * e0[0],
                             e1[1] * e0[0] - e1[0] * e0[1] };
        ProjectTriOnAxis(n, tri9, triMin, triMax);                    // 0x6a03e5
        ProjectBoxOnAxis(n, center, axes9, half, boxMin, boxMax);     // 0x6a040d
        if (!(triMin <= boxMax && triMax >= boxMin)) return false;    // 0x6a0432 (else return 0)
    }
    // Axes 1-3: the 3 AABB faces (0x6a0438..0x6a04a9).
    for (int a = 0; a < 3; ++a) {
        ProjectTriOnAxis(&axes9[3 * a], tri9, triMin, triMax);
        ProjectBoxOnAxis(&axes9[3 * a], center, axes9, half, boxMin, boxMax);
        if (triMin > boxMax || triMax < boxMin) return false;         // 0x6a049c
    }
    // Axes 4-12: 9 cross products edge_i × boxAxis_j. INLINE box projection (identity
    // axes -> *0.0 terms elided, cf. 0x6a052d..0x6a0595 — same policy as SegmentAABB).
    const float* edges[3] = { e0, e1, e2 };
    for (int j = 0; j < 3; ++j) {                                     // box axis (v17)
        const float* bax = &axes9[3 * j];
        for (int i = 0; i < 3; ++i) {                                 // triangle edge (v19)
            const float* e = edges[i];
            const float ax[3] = { e[2] * bax[1] - e[1] * bax[2],      // 0x6a04e9
                                  e[0] * bax[2] - e[2] * bax[0],      // 0x6a04fa
                                  bax[0] * e[1] - e[0] * bax[1] };     // 0x6a050a
            ProjectTriOnAxis(ax, tri9, triMin, triMax);               // 0x6a050e
            const float c = ax[2] * center[2] + ax[1] * center[1] + ax[0] * center[0]; // 0x6a052d
            const float r = std::fabs(ax[0] * half[0]) + std::fabs(ax[1] * half[1])
                          + std::fabs(ax[2] * half[2]);               // 0x6a0577/0x6a0589
            if (triMin > c + r || triMax < c - r) return false;       // 0x6a05b7
        }
    }
    return true;                                                      // 0x6a05d5
}

// MapColl_SweepSphereNearest 0x696ad0 — thick-ray/sphere sweep vs quadtree, nearest impact.
bool SweepSphereNearest(const asset::CollisionMesh& mesh, uint32_t nodeIndex,
                        const float from[3], const float to[3], float radius, float outHit[3]) {
    if (nodeIndex >= mesh.nodes.size()) return false;                 // child == -1 (0xFFFFFFFF)
    const asset::CollisionQuadNode& node = mesh.nodes[nodeIndex];
    if (node.trisNum == 0) return false;                              // 0x696ae7: *(nodes+48*a2+24)==0
    // Segment [from,to] AABB inflated by ±radius (0x696b0c..0x696ba5).
    const float segMin[3] = { std::min(from[0], to[0]) - radius,
                              std::min(from[1], to[1]) - radius,
                              std::min(from[2], to[2]) - radius };
    const float segMax[3] = { std::max(from[0], to[0]) + radius,
                              std::max(from[1], to[1]) + radius,
                              std::max(from[2], to[2]) + radius };
    if (!AABBOverlap(segMin, segMax, node.bboxMin, node.bboxMax)) return false; // 0x696bcd
    float best = -1.0f;                                               // v56
    if (node.child[0] == -1) {                                        // 0x696be3: leaf
        const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
        const float len = std::sqrt(dx * dx + dy * dy + dz * dz);     // 0x696cf2: v76
        if (radius + 1.0f > len) return false;                        // 0x696d09
        const float inv = 1.0f / len;                                 // 0x696d24
        const float dir[3] = { dx * inv, dy * inv, dz * inv };        // 0x696d34..0x696d48
        const float lenSq = len * len;                               // 0x696e15: v76*v76 (walk bound)
        for (uint32_t i = 0; i < node.trisNum; ++i) {                 // 0x696f93
            const size_t idx = static_cast<size_t>(node.trisIndex) + i;
            if (idx >= mesh.triIndices.size()) break;                 // guard
            const uint32_t faceIdx = mesh.triIndices[idx];            // triIndices[node.trisIndex+i]
            if (faceIdx >= mesh.tris.size()) continue;                // guard
            const asset::CollisionFace& face = mesh.tris[faceIdx];
            if (!TriAABB(face, segMin, segMax)) continue;             // 0x696d73: coarse (tri vs segAABB)
            // Box of half-side radius centered on `from` (0x696d80..0x696dec).
            float boxMin[3] = { from[0] - radius, from[1] - radius, from[2] - radius };
            float boxMax[3] = { from[0] + radius, from[1] + radius, from[2] + radius };
            float center[3] = { from[0], from[1], from[2] };          // v57/v59/v60 (impact tracer)
            bool hit = false;
            if (TriAABB(face, boxMin, boxMax)) {                      // 0x696e08: box at `from`
                hit = true;                                           // impact = from (dist²=0)
            } else {
                for (;;) {                                            // step by `dir` (0x696e21)
                    boxMin[0] += dir[0]; boxMin[1] += dir[1]; boxMin[2] += dir[2];
                    boxMax[0] += dir[0]; boxMax[1] += dir[1]; boxMax[2] += dir[2];
                    center[0] += dir[0]; center[1] += dir[1]; center[2] += dir[2];
                    const float cdx = center[0] - from[0], cdy = center[1] - from[1],
                                cdz = center[2] - from[2];
                    if (cdx * cdx + cdy * cdy + cdz * cdz > lenSq) break; // 0x696ead: past the segment
                    if (TriAABB(face, boxMin, boxMax)) { hit = true; break; } // 0x696eea
                }
            }
            if (hit) {                                               // LABEL_38 (0x696ef7)
                const float hdx = center[0] - from[0], hdy = center[1] - from[1],
                            hdz = center[2] - from[2];
                const float dist2 = hdx * hdx + hdy * hdy + hdz * hdz;
                if (best == -1.0f || dist2 < best) {                 // 0x696f32/0x696f5e
                    best = dist2;
                    outHit[0] = center[0]; outHit[1] = center[1]; outHit[2] = center[2];
                }
            }
        }
    } else {                                                         // internal node: 4 children (0x696bf1)
        for (int c = 0; c < 4; ++c) {
            float childHit[3];
            if (SweepSphereNearest(mesh, static_cast<uint32_t>(node.child[c]),
                                   from, to, radius, childHit)) {    // 0x696c18
                const float dx2 = childHit[0] - from[0], dy2 = childHit[1] - from[1],
                            dz2 = childHit[2] - from[2];
                const float dist2 = dx2 * dx2 + dy2 * dy2 + dz2 * dz2;
                if (best == -1.0f || dist2 < best) {                 // 0x696c60/0x696c8c
                    best = dist2;
                    outHit[0] = childHit[0]; outHit[1] = childHit[1]; outHit[2] = childHit[2];
                }
            }
        }
    }
    return best != -1.0f;                                            // 0x696faa
}

// Terrain_SweepSphereSegment 0x69a1f0 — descends the root's 4 children, nearest impact.
bool SweepSphereSegment(const asset::CollisionMesh& mesh, const float from[3], const float to[3],
                        float radius, float outHit[3]) {
    if (!MeshActive(mesh)) return false;                             // 0x69a1f3: if (!this[1])
    if (from[0] == to[0] && from[1] == to[1] && from[2] == to[2]) return false; // 0x69a238
    const float segMin[3] = { std::min(from[0], to[0]) - radius,
                              std::min(from[1], to[1]) - radius,
                              std::min(from[2], to[2]) - radius };
    const float segMax[3] = { std::max(from[0], to[0]) + radius,
                              std::max(from[1], to[1]) + radius,
                              std::max(from[2], to[2]) + radius };
    // Gate on the ROOT bbox (0x69a2fe) — the root is NEVER tested as a leaf.
    if (!AABBOverlap(segMin, segMax, mesh.nodes[0].bboxMin, mesh.nodes[0].bboxMax)) return false;
    float best = -1.0f;                                              // v36
    for (int c = 0; c < 4; ++c) {                                    // 4 children of the root (0x69a319)
        float childHit[3];
        if (SweepSphereNearest(mesh, static_cast<uint32_t>(mesh.nodes[0].child[c]),
                               from, to, radius, childHit)) {        // 0x69a336
            const float dx = childHit[0] - from[0], dy = childHit[1] - from[1],
                        dz = childHit[2] - from[2];
            const float dist2 = dx * dx + dy * dy + dz * dz;
            if (best == -1.0f || dist2 < best) {                     // 0x69a37e/0x69a3a7
                best = dist2;
                outHit[0] = childHit[0]; outHit[1] = childHit[1]; outHit[2] = childHit[2];
            }
        }
    }
    return best != -1.0f;                                           // 0x69a3e8
}

// World_IsPointBlocked 0x540da0 — blocked if (a) no Main ground under ceiling p.y+20, OR (b) a
// WJ ground exists above the Main ground ("water/forbidden" layer). GetGroundHeight shaped
// (a5=1, a6=ceiling, a7=0, a8=1) — exactly the args at BOTH call sites (0x540de1/0x540e43).
bool IsPointBlocked(const asset::CollisionMesh& main, const asset::CollisionMesh* wj,
                    const float p[3]) {
    const float ceiling = p[1] + 20.0f;                             // 0x540db9
    float groundMain = 0.0f;
    if (!GetGroundHeight(main, p[0], p[2], groundMain, true, ceiling, false, true)) // 0x540de1
        return true;                                                // 0x540dea: no ground -> blocked
    if (!wj) return false;                                          // WJ layer absent -> 2nd test false
    float groundWJ = 0.0f;
    const float ceiling2 = p[1] + 20.0f;                            // 0x540e01
    return GetGroundHeight(*wj, p[0], p[2], groundWJ, true, ceiling2, false, true) // 0x540e43
        && groundMain < groundWJ;
}

// ===========================================================================
// WG-03 — Screen->terrain picking (Terrain_PickRayScreen 0x699a80).
// ===========================================================================

// Screen->world ray unprojection (0x699ae7..0x699b40). W-1/H-1 denominators (NOT W/H);
// z=1 BEFORE transform; direction NOT renormalized after TransformNormal. Divisions in double
// (v17/v18 replayed to float — faithful to the original FPU computation).
bool BuildScreenRay(const ScreenPickCamera& cam, int sx, int sy, float outOrigin[3],
                    float outDir[3]) {
    outOrigin[0] = cam.eye[0];                                      // g_CameraPos (0x699aa6)
    outOrigin[1] = cam.eye[1];                                      // flt_800134
    outOrigin[2] = cam.eye[2];                                      // flt_800138
    const double w1 = static_cast<double>(static_cast<unsigned>(cam.screenW - 1)); // dword_8000A0-1
    const double h1 = static_cast<double>(static_cast<unsigned>(cam.screenH - 1)); // dword_8000A4-1
    float d[3];
    d[0] = static_cast<float>((2.0 * static_cast<double>(sx) / w1 - 1.0)
                              / static_cast<double>(cam.proj11));    // /proj._11 (0x699ae7)
    d[1] = static_cast<float>((static_cast<double>(sy) * -2.0 / h1 + 1.0)
                              / static_cast<double>(cam.proj22));    // /proj._22 (0x699b31)
    d[2] = 1.0f;                                                     // 0x699b21 (LH view space)
    // D3DXVec3TransformNormal(d, d, invView) — 3x3 rotation (row-vector), translation ignored.
    outDir[0] = d[0] * cam.invView[0] + d[1] * cam.invView[4] + d[2] * cam.invView[8];  // 0x699b40
    outDir[1] = d[0] * cam.invView[1] + d[1] * cam.invView[5] + d[2] * cam.invView[9];
    outDir[2] = d[0] * cam.invView[2] + d[1] * cam.invView[6] + d[2] * cam.invView[10];
    return true;
}

// Terrain_PickRayScreen 0x699a80.
bool PickRayScreen(const asset::CollisionMesh& mesh, const ScreenPickCamera& cam,
                   int sx, int sy, uint32_t& outFaceIndex, float outHit[3], bool twoSide) {
    if (!MeshActive(mesh)) return false;                            // 0x699a86: if (!this[1])
    float origin[3], dir[3];
    BuildScreenRay(cam, sx, sy, origin, dir);
    if (!SegmentAABB(origin, dir, mesh.nodes[0].bboxMin, mesh.nodes[0].bboxMax)) return false; // 0x699b5f
    float best = -1.0f;                                             // v16
    for (int c = 0; c < 4; ++c) {                                   // 4 children of the root (0x699b7f)
        uint32_t faceIdx = 0;
        float hit[3];
        if (RaycastNearest(mesh, static_cast<uint32_t>(mesh.nodes[0].child[c]),
                           origin, dir, faceIdx, hit, twoSide)) {   // 0x699ba9
            const float dx = hit[0] - origin[0], dy = hit[1] - origin[1], dz = hit[2] - origin[2];
            const float d = std::sqrt(dx * dx + dy * dy + dz * dz); // 0x699bde: Euclidean distance
            if (best == -1.0f || d < best) {                        // 0x699c25
                best = d;
                outFaceIndex = faceIdx;
                outHit[0] = hit[0]; outHit[1] = hit[1]; outHit[2] = hit[2];
            }
        }
    }
    return best != -1.0f;                                          // 0x699c77
}

} // namespace collision

// ===========================================================================
// Ground/collision queries exposed by WorldMap — delegate to collision:: on the bound
// main mesh (collisionMesh_). Build-safe: false/no-op as long as no mesh is bound.
// ===========================================================================
bool WorldMap::GetGroundHeight(float x, float z, float probeCeilingY, float& outGroundY) const {
    if (!collisionMesh_) return false;                      // mesh not bound -> ground undetermined
    // Consumer shape (Char_Update 0x581e10 / World_IsPointOnGround 0x540d40):
    // a5=1 (ceiling = probeCeilingY), a7=0, a8=1 (1st hit).
    return collision::GetGroundHeight(*collisionMesh_, x, z, outGroundY, true, probeCeilingY,
                                      false, true);
}
bool WorldMap::HasGroundAt(float x, float z) const {
    if (!collisionMesh_) return false;
    float out = 0.0f;                                       // IsGroundBlocked-shape (default ceiling)
    return collision::GetGroundHeight(*collisionMesh_, x, z, out, false, 0.0f, false, true);
}
bool WorldMap::IsPointOnGround(float x, float y, float z) const {
    if (!collisionMesh_) return false;
    return collision::IsPointOnGround(*collisionMesh_, x, y, z);
}
bool WorldMap::Raycast(const float start[3], const float dir[3], uint32_t& outFaceIndex,
                       float outHit[3], bool twoSide) const {
    if (!collisionMesh_) return false;
    return collision::RaycastNearest(*collisionMesh_, 0, start, dir, outFaceIndex, outHit, twoSide);
}
bool WorldMap::SlideMoveGround(const float from[3], const float to[3], float speed, float dt,
                               float outPos[3]) const {
    if (!collisionMesh_) {                                  // no mesh: stay in place
        outPos[0] = from[0]; outPos[1] = from[1]; outPos[2] = from[2];
        return false;
    }
    return collision::SlideMoveGround(*collisionMesh_, from, to, speed, dt, outPos);
}

} // namespace ts2::world

// World/CollisionMesh.cpp — segment pick "variant A" + ground plane (Wave B4 / F_COLLISION).
// Byte-faithful port; each block cites its @EA anchor (imagebase 0x400000). See CollisionMesh.h
// for the full chain. IDA = SOLE TRUTH; IDB is read-only.
#include "World/CollisionMesh.h"
#include "World/WorldMap.h"      // collision::SegmentAABB (== 0x4070d0) + collision::RayHitTriangle (0x695ae0)
#include "Asset/WorldChunk.h"    // asset::CollisionMesh/CollisionFace/CollisionQuadNode (Gap G01)

#include <cmath>   // std::sqrt (pick distances, cf. Crt_sqrtf 0x401090)

namespace ts2::world {
namespace collision {
namespace {

// this[1] (SegNodeNearestA @0x420f51 : == 2 = mLoadSort loaded): here, mesh is active as soon as
// faces + quadtree are present (build-safe, never a null dereference).
inline bool MeshActive(const asset::CollisionMesh& m) {
    return !m.nodes.empty() && !m.tris.empty();
}

// "p on the same side as the centroid" test on an edge (a->b) projected in XZ. Unified
// transcription of the 3 edges of Collision_PointInTriangle 0x41fe30 (vertical edge @0x41fec3/0x42000c/
// 0x42013d ; slope/intercept otherwise). c = centroid (cx,cz), p = (px,pz). false = opposite side (reject).
inline bool EdgeSideOk(float ax, float az, float bx, float bz,
                       float cx, float cz, float px, float pz) {
    const float dx = bx - ax;
    if (dx == 0.0f) {                                        // vertical edge x = ax
        // reject if centroid and p are on opposite sides of x=ax, or centroid IS ON the line but not p.
        if ((cx > ax && px < ax) || (cx < ax && px > ax) || (ax == cx && ax != px))
            return false;
        return true;
    }
    const float m = (bz - az) / dx;                         // slope dz/dx (e.g. v39 @0x41ff31)
    const float b = az - m * ax;                            // intercept z = m*x + b (v40 @0x41ff3d)
    const float lineAtC = m * cx + b;                       // z of the line at cx (v16 @0x41ff51)
    if (lineAtC < cz) {                                     // centroid ABOVE (cz > line)
        if (m * px + b > pz) return false;                 // reject if p BELOW (@0x41ff75)
    } else if (lineAtC > cz) {                              // centroid BELOW
        if (m * px + b < pz) return false;                 // reject if p ABOVE (@0x41ffb1)
    } else {                                                // centroid ON the line
        if (pz != m * px + b) return false;                // p must be ON the line (@0x41ffd6)
    }
    return true;
}

// XZ leaf-location descent — identical to MapColl_GetGroundHeight 0x697130
// (0x697148..0x6971d9) / LocateLeafXZ (World/WorldMap.cpp). -1 if (x,z) is outside the quadtree.
int LocateLeafXZ(const asset::CollisionMesh& m, float x, float z) {
    const auto& nodes = m.nodes;
    uint32_t nodeIdx = 0;                                   // root = this[35] index 0 (0x697148)
    if (nodes[0].child[0] != -1) {                          // 0x697159 : non-leaf root
        for (;;) {
            const asset::CollisionQuadNode& n = nodes[nodeIdx];
            int c = 0;
            for (; c < 4; ++c) {                            // 0x697182 : scan the 4 children
                const int32_t ci = n.child[c];
                if (ci < 0 || static_cast<size_t>(ci) >= nodes.size())
                    continue;                               // OOB guard (malformed data)
                const asset::CollisionQuadNode& cn = nodes[static_cast<size_t>(ci)];
                if (x >= cn.bboxMin[0] && x <= cn.bboxMax[0] &&   // 0x6971ba : XZ bbox
                    z >= cn.bboxMin[2] && z <= cn.bboxMax[2])
                    break;
            }
            if (c == 4) return -1;                          // 0x6971c3 : no containing child
            nodeIdx = static_cast<uint32_t>(n.child[c]);    // 0x6971c7 : descend
            if (nodes[nodeIdx].child[0] == -1) break;       // 0x6971d9 : leaf reached
        }
    }
    return static_cast<int>(nodeIdx);
}

} // namespace

// ===========================================================================
// Collision_PointInTriangle 0x41fe30 — point in triangle projected in XZ.
// ===========================================================================
bool PointInTriangleProjXZ(const asset::CollisionMesh& mesh, uint32_t faceIndex, const float p[3]) {
    if (faceIndex >= mesh.tris.size()) return false;
    const asset::CollisionFace& f = mesh.tris[faceIndex];
    // XZ vertices (@0x41fe4b..0x41fe76 : face+4/+44/+84 = x, face+12/+52/+92 = z).
    const float v0x = f.v0.position[0], v0z = f.v0.position[2];
    const float v1x = f.v1.position[0], v1z = f.v1.position[2];
    const float v2x = f.v2.position[0], v2z = f.v2.position[2];
    // XZ centroid (v45/v46 @0x41fe90/0x41feae).
    const float cx = (v1x + v0x + v2x) / 3.0f;
    const float cz = (v1z + v0z + v2z) / 3.0f;
    const float px = p[0], pz = p[2];                       // *a3 / a3[2] (a3[1]=Y ignored)
    if (!EdgeSideOk(v0x, v0z, v1x, v1z, cx, cz, px, pz)) return false; // edge v0->v1 (0x41fec3)
    if (!EdgeSideOk(v1x, v1z, v2x, v2z, cx, cz, px, pz)) return false; // edge v1->v2 (0x42000c)
    if (!EdgeSideOk(v2x, v2z, v0x, v0z, cx, cz, px, pz)) return false; // edge v2->v0 (0x42013d)
    return true;                                            // 0x41ff25 : inside all 3 -> inside
}

// ===========================================================================
// Collision_RayTriangle 0x420240 — single-face plane-solve + XZ containment.
// ===========================================================================
bool RayTriangleHit(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                    const float start[3], const float dir[3], float outHit[3]) {
    if (faceIndex >= mesh.tris.size()) return false;
    const asset::CollisionFace& f = mesh.tris[faceIndex];
    const float a = f.plane[0], b = f.plane[1], c = f.plane[2], d = f.plane[3]; // +124/+128/+132/+136
    const float nDotDir = b * dir[1] + a * dir[0] + c * dir[2];   // 0x420274 : n.dir
    if (nDotDir >= 0.0f) return false;                            // 0x420285 : single-sided
    const float t = (d - (b * start[1] + a * start[0] + c * start[2])) / nDotDir; // 0x4202c0
    if (t < 0.0f) return false;                                   // 0x4202d1
    outHit[0] = start[0] + t * dir[0];                            // 0x4202db
    outHit[1] = start[1] + t * dir[1];                            // 0x4202e5
    outHit[2] = start[2] + t * dir[2];                            // 0x4202ee
    return PointInTriangleProjXZ(mesh, faceIndex, outHit);        // 0x42028e
}

// ===========================================================================
// Collision_SegNodeNearestA 0x420f40 — recursive descent, nearest walkable impact.
// ===========================================================================
bool SegNodeNearest(const asset::CollisionMesh& mesh, uint32_t nodeIndex,
                    const float start[3], const float dir[3],
                    uint32_t& outFaceIndex, float outHit[3]) {
    if (!MeshActive(mesh)) return false;                          // 0x420f51 : this[1] != 2
    if (nodeIndex >= mesh.nodes.size()) return false;             // child == -1 (0xFFFFFFFF)
    const asset::CollisionQuadNode& node = mesh.nodes[nodeIndex];
    if (node.trisNum == 0) return false;                          // 0x420f67 : node+24 == 0
    // AABB gate: Collide_AABBvsSegmentSAT(node+12, node, dir, start) @0x420f89 — SAME ALGORITHM as
    // Collide_SegmentAABB 0x69fb20 = collision::SegmentAABB(start, dir, bmin, bmax) (args permuted).
    if (!SegmentAABB(start, dir, node.bboxMin, node.bboxMax)) return false;
    float best = -1.0f;                                           // v31
    if (node.child[0] == -1) {                                    // 0x420fa4 : leaf
        for (uint32_t i = 0; i < node.trisNum; ++i) {            // 0x421105 (node+24 = trisNum)
            const size_t idx = static_cast<size_t>(node.trisIndex) + i;
            if (idx >= mesh.triIndices.size()) break;            // guard
            const uint32_t faceIdx = mesh.triIndices[idx];       // node.trisIndex[i]
            if (faceIdx >= mesh.tris.size()) continue;           // guard
            if (mesh.tris[faceIdx].materialIndex != 1) continue; // 0x421128 : walkable FILTER ==1
            float hp[3];
            if (RayTriangleHit(mesh, faceIdx, start, dir, hp)) { // 0x42113f
                const float dx = hp[0] - start[0], dy = hp[1] - start[1], dz = hp[2] - start[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz); // 0x4211b3
                if (best == -1.0f || dist < best) {              // 0x4211cd / 0x42120c
                    best = dist;
                    outFaceIndex = faceIdx;                      // *a5 = face index
                    outHit[0] = hp[0]; outHit[1] = hp[1]; outHit[2] = hp[2];
                }
            }
        }
    } else {                                                     // 0x420fae : internal node (4 children)
        for (int c = 0; c < 4; ++c) {
            uint32_t childFace = 0;
            float childHit[3];
            if (SegNodeNearest(mesh, static_cast<uint32_t>(node.child[c]),
                               start, dir, childFace, childHit)) { // 0x420fe0
                const float dx = childHit[0] - start[0], dy = childHit[1] - start[1],
                            dz = childHit[2] - start[2];
                const float dist = std::sqrt(dx * dx + dy * dy + dz * dz); // 0x421053
                if (best == -1.0f || dist < best) {              // 0x42106d / 0x4210a6
                    best = dist;
                    outFaceIndex = childFace;
                    outHit[0] = childHit[0]; outHit[1] = childHit[1]; outHit[2] = childHit[2];
                }
            }
        }
    }
    return best != -1.0f;                                        // 0x421265 / 0x4210eb
}

// ===========================================================================
// Collision_SegPickA 0x420d60 — pick of segment [near -> far], nearest impact.
// ===========================================================================
bool SegPickNearest(const asset::CollisionMesh& mesh, const float nearPt[3], const float farPt[3],
                    uint32_t& outFaceIndex, float outHit[3]) {
    // Guard: !this[0] || this[1] != 2 || near == far (0x420da7).
    if (!MeshActive(mesh)) return false;
    if (nearPt[0] == farPt[0] && nearPt[1] == farPt[1] && nearPt[2] == farPt[2]) return false;
    // dir = far - near (v31 = a1 - a2 @0x420db1) ; start = near (v25 = a2).
    const float dir[3] = { farPt[0] - nearPt[0], farPt[1] - nearPt[1], farPt[2] - nearPt[2] };
    const asset::CollisionQuadNode& root = mesh.nodes[0];        // a4[43] = quadtree, node 0
    // Root AABB gate: Collide_AABBvsSegmentSAT(root+12, root, dir, near) @0x420dee.
    if (!SegmentAABB(nearPt, dir, root.bboxMin, root.bboxMax)) return false;
    float best = -1.0f;                                          // v20
    for (int c = 0; c < 4; ++c) {                               // v11 = 32..44 : root.child[0..3] (0x420e30)
        uint32_t childFace = 0;
        float childHit[3];
        if (SegNodeNearest(mesh, static_cast<uint32_t>(root.child[c]),
                           nearPt, dir, childFace, childHit)) {
            const float dx = childHit[0] - nearPt[0], dy = childHit[1] - nearPt[1],
                        dz = childHit[2] - nearPt[2];
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz); // 0x420e8e
            if (best == -1.0f || dist < best) {                 // 0x420ea8 / 0x420ee1
                best = dist;
                outFaceIndex = childFace;
                outHit[0] = childHit[0]; outHit[1] = childHit[1]; outHit[2] = childHit[2];
            }
        }
    }
    return best != -1.0f;                                       // 0x420f36
}

// ===========================================================================
// Ground plane for planar shadow — Model_RenderPlanarShadow 0x40f720 (@0x40f97d..0x40fb00).
// ===========================================================================
bool GetGroundPlaneForShadow(const asset::CollisionMesh& mesh, const float modelPos[3],
                             float modelHeight, const float lightDir[3], float maxDist,
                             GroundPlane& out) {
    out.valid = false;
    // near = { pos.x, pos.y + modelHeight, pos.z } (v46/v47/v48 @0x40f97d/0x40f995/0x40f9a0).
    const float nearPt[3] = { modelPos[0], modelPos[1] + modelHeight, modelPos[2] };
    // far = near + lightDir (v49 @0x40f9ae/0x40f9bc/0x40f9ca).
    const float farPt[3] = { lightDir[0] + nearPt[0], lightDir[1] + nearPt[1], lightDir[2] + nearPt[2] };
    uint32_t faceIdx = 0;
    float hit[3];
    if (!SegPickNearest(mesh, nearPt, farPt, faceIdx, hit)) return false; // 0x40f9ce
    // Distance guard: proceed if maxDist >= |hit - near| (a9 >= sqrt(v35) @0x40fa39).
    const float dx = hit[0] - nearPt[0], dy = hit[1] - nearPt[1], dz = hit[2] - nearPt[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);            // 0x40fa1c
    if (dist > maxDist) return false;
    // plane = tris[faceIdx].plane (v17 = a8[40] + 156*v42 ; v17[31..34] @0x40fa52..0x40fa75).
    if (faceIdx >= mesh.tris.size()) return false;                        // guard
    const asset::CollisionFace& f = mesh.tris[faceIdx];
    const float a = f.plane[0], b = f.plane[1], c = f.plane[2], d = f.plane[3]; // +124/+128/+132/+136
    // Guard: b != 0 && (d - x*a - z*c)/b <= pos.y + 0.1 (@0x40fac2).
    if (b == 0.0f) return false;
    const float solveY = (d - modelPos[0] * a - modelPos[2] * c) / b;
    if (solveY > modelPos[1] + 0.1f) return false;
    out.plane[0] = a; out.plane[1] = b; out.plane[2] = c; out.plane[3] = d;
    // shadowPlane = { a, b, c, -d - 0.1 } (v45 @0x40face..0x40fafc).
    out.shadowPlane[0] = a; out.shadowPlane[1] = b; out.shadowPlane[2] = c;
    out.shadowPlane[3] = d * -1.0f - 0.1f;
    out.hit[0] = hit[0]; out.hit[1] = hit[1]; out.hit[2] = hit[2];
    out.faceIndex = faceIdx;
    out.valid = true;
    return true;
}

// ===========================================================================
// VERTICAL variant — ground plane under (x,z) via the descent of MapColl_GetGroundHeight 0x697130.
// ===========================================================================
bool GetGroundPlaneUnder(const asset::CollisionMesh& mesh, float x, float z, GroundPlane& out) {
    out.valid = false;
    if (!MeshActive(mesh)) return false;                          // 0x697135
    const int leaf = LocateLeafXZ(mesh, x, z);
    if (leaf < 0) return false;                                   // outside the quadtree
    const asset::CollisionQuadNode& node = mesh.nodes[static_cast<size_t>(leaf)];
    const float ceiling = mesh.nodes[0].bboxMax[1];               // a5=0 -> node0 +16 (0x6971e5)
    for (uint32_t i = 0; i < node.trisNum; ++i) {                // 0x697215
        const size_t idx = static_cast<size_t>(node.trisIndex) + i;
        if (idx >= mesh.triIndices.size()) break;                // guard
        const uint32_t faceIdx = mesh.triIndices[idx];
        if (faceIdx >= mesh.tris.size()) continue;               // guard
        const asset::CollisionFace& f = mesh.tris[faceIdx];
        const float b = f.plane[1];                              // normal.y (+128)
        if (b <= 0.0f) continue;                                 // 0x697259/0x697288 : walkable + div
        const float y = (f.plane[3] - x * f.plane[0] - z * f.plane[2]) / b; // 0x6972ad
        if (y <= ceiling && RayHitTriangle(mesh, faceIdx, x, y, z)) {       // 0x6972ca (0x695ae0)
            // a8=1 : 1st hit (0x6972ec) -> this ground + its plane.
            out.plane[0] = f.plane[0]; out.plane[1] = f.plane[1];
            out.plane[2] = f.plane[2]; out.plane[3] = f.plane[3];
            out.shadowPlane[0] = f.plane[0]; out.shadowPlane[1] = f.plane[1];
            out.shadowPlane[2] = f.plane[2];
            out.shadowPlane[3] = f.plane[3] * -1.0f - 0.1f;      // same bias as 0x40fafc
            out.hit[0] = x; out.hit[1] = y; out.hit[2] = z;
            out.faceIndex = faceIdx;
            out.valid = true;
            return true;
        }
    }
    return false;                                                // 0x697318 : no ground
}

} // namespace collision
} // namespace ts2::world

// World/CollisionMesh.h — segment PICK + GROUND-PLANE collision engine (Wave B4 / F_COLLISION).
//
// This NEW module completes the query engine already in place in World/WorldMap.cpp
// (namespace ts2::world::collision — MapColl_GetGroundHeight 0x697130, RaycastNearest 0x6960c0,
// SweepSphereNearest 0x696ad0, SlideMoveGround 0x697330, PointInTriangleXZ 0x695c70, etc., all
// ported byte-faithfully). There is NO duplication: only the "variant A" SEGMENT PICK chain and
// GROUND-PLANE extraction are added here — absent until now — needed for the projected planar
// shadow of Wave B (F_ENTITY3D). The typed G01 decoders (CollisionFace 156B,
// CollisionQuadNode 48B, TerrainVertex 40B) already live in Asset/WorldChunk.{h,cpp} and are
// consumed as-is (asset::CollisionMesh).
//
// Reversed chain (@EA anchors, imagebase 0x400000):
//   Model_RenderPlanarShadow   0x40f720  — builds a model+height segment -> +lightDir, picks,
//                                          reads tris[hit].plane (+124/+128/+132/+136), bias -d-0.1,
//                                          D3DXMatrixShadow. This IS the ground-plane consumer.
//   Collision_SegPickA         0x420d60  — root AABB gate + descends the 4 children (nearest impact).
//   Collision_SegNodeNearestA  0x420f40  — recursive; LEAF filters materialIndex==1 (walkable faces
//                                          tagged .WM), otherwise recurses into 4 children; guards this[1]==2.
//   Collision_RayTriangle      0x420240  — single-face plane-solve (n.dir<0) + t>=0 + containment.
//   Collision_PointInTriangle  0x41fe30  — point-in-triangle projected in XZ (centroid-side/3 edges).
//   Collide_AABBvsSegmentSAT   0x4070d0  — SAT segment/AABB; SAME ALGORITHM as Collide_SegmentAABB
//                                          0x69fb20 (already ported = collision::SegmentAABB) up to
//                                          argument permutation -> reused, not re-ported.
//   MapColl_GetGroundHeight    0x697130  — VERTICAL variant (GetGroundPlaneUnder): XZ quadtree
//                                          descent + plane-solve + MapColl_RayHitTriangle 0x695ae0.
//
// RULE #0: each block of the .cpp cites its IDA anchor. Pure, stateless functions, build-safe
// (empty-mesh / out-of-bounds-index guards). IDA = SOLE TRUTH; IDB is read-only.
#pragma once
#include <cstdint>

// LEAF MODULE: operates on the DECODED mesh (asset::CollisionMesh, Gap G01, Asset/WorldChunk.h).
// Forward declarations only — the .cpp includes Asset/WorldChunk.h + World/WorldMap.h.
namespace ts2::asset {
struct CollisionMesh;
struct CollisionFace;
} // namespace ts2::asset

namespace ts2::world {
namespace collision {

// GroundPlane — ground plane returned for the planar shadow (Wave B / F_ENTITY3D).
// IDA ref: Model_RenderPlanarShadow 0x40f720 (@0x40fa52..0x40fb28).
//   plane[4]       = {a,b,c,d} = tris[faceIndex].plane RAW (+124/+128/+132/+136) = normal + D.
//   shadowPlane[4] = {a,b,c, -d - 0.1f} — plane READY for D3DXMatrixShadow: D negated + anti
//                    z-fight bias (v45[3] = v41*-1.0 - 0.1 @0x40fafc). This is the vector the binary
//                    passes to j_D3DXMatrixShadow(mat, light4, shadowPlane) @0x40fb28.
//   hit[3]         = pick's impact point on the ground (Collision_SegPickA output a3).
//   faceIndex      = index of the ground face hit, in mesh.tris[].
// The light vector to pass ALONGSIDE = light4 = { -lightDir.x, -lightDir.y, -lightDir.z, 0.0 }
// (v38..v41 @0x40fb08..0x40fb24 : flt_18C53C0/C4/C8 negated, w=0 = directional). lightDir comes
// from globals flt_18C53C0/18C53C4/18C53C8 (shadow projection direction) — supplied by F_ENTITY3D.
struct GroundPlane {
    float    plane[4]       = {0.0f, 0.0f, 0.0f, 0.0f}; // raw a,b,c,d (tris[faceIndex].plane)
    float    shadowPlane[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // a,b,c,-d-0.1 (ready for D3DXMatrixShadow)
    float    hit[3]         = {0.0f, 0.0f, 0.0f};       // pick's impact point on the ground
    uint32_t faceIndex      = 0;                         // ground face hit (index into tris[])
    bool     valid          = false;                     // false = no ground under the pick
};

// "Variant A" SEGMENT PICK chain (nearest walkable-face impact).

// Collision_PointInTriangle 0x41fe30 — point-in-triangle PROJECTED in XZ (centroid-side test on
// the 3 edges ; p[1]=Y ignored). Uses the same 156B faces as MapColl_RayHitTriangle 0x695ae0
// but a DISTINCT EDGE-SIDEDNESS test (not barycentric). faceIndex out of bounds -> false.
bool PointInTriangleProjXZ(const asset::CollisionMesh& mesh, uint32_t faceIndex, const float p[3]);

// Collision_RayTriangle 0x420240 — SINGLE-FACE plane-solve : n.dir must be < 0 (0x420285),
// t = (d - n.start)/(n.dir) >= 0 (0x4202d1), hit = start + t*dir, then PointInTriangleProjXZ.
// outHit filled only if hit (returns true).
bool RayTriangleHit(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                    const float start[3], const float dir[3], float outHit[3]);

// Collision_SegNodeNearestA 0x420f40 — recursive descent of the quadtree (nodeIndex ; 0 = root).
// LEAF (child[0]==-1): for each face in the leaf, FILTER materialIndex==1 (0x421128 :
// walkable faces tagged .WM/WORLD2), test RayTriangleHit, keep the impact nearest to
// `start`. INTERNAL NODE: recurse into the 4 children. AABB gate = collision::SegmentAABB (== 0x4070d0).
// outFaceIndex/outHit = nearest impact. Returns true if an impact was found.
bool SegNodeNearest(const asset::CollisionMesh& mesh, uint32_t nodeIndex,
                    const float start[3], const float dir[3],
                    uint32_t& outFaceIndex, float outHit[3]);

// Collision_SegPickA 0x420d60 — pick of SEGMENT [near -> far] against the mesh. ROOT AABB gate
// (near, dir=far-near) then descends its 4 children (SegNodeNearest), impact nearest to
// `near`. Guard: mesh active + near != far (0x420da7). outFaceIndex/outHit filled if hit.
bool SegPickNearest(const asset::CollisionMesh& mesh, const float nearPt[3], const float farPt[3],
                    uint32_t& outFaceIndex, float outHit[3]);

// GROUND PLANE for the planar shadow (Wave B) — two providers.

// Model_RenderPlanarShadow 0x40f720 (@0x40f97d..0x40fb00) — reproduces the ground-plane extraction:
//   near = { pos.x, pos.y + modelHeight, pos.z }              (@0x40f97d/0x40f995/0x40f9a0)
//   far  = near + lightDir                                    (@0x40f9ae/0x40f9bc/0x40f9ca)
//   pick = Collision_SegPickA(near, far)                      (@0x40f9ce)
//   distance guard: |hit - near| <= maxDist                   (@0x40fa39)
//   plane = tris[hit].plane ; guard b != 0 && (d - x*a - z*c)/b <= pos.y + 0.1  (@0x40fac2)
//   out.shadowPlane = { a, b, c, -d - 0.1 }                   (@0x40fafc)
// Returns true + fills `out` if a valid shadow ground exists ; false otherwise (out.valid=false).
// `lightDir` = flt_18C53C0/C4/C8 (shadow projection direction, supplied by the caller).
bool GetGroundPlaneForShadow(const asset::CollisionMesh& mesh, const float modelPos[3],
                             float modelHeight, const float lightDir[3], float maxDist,
                             GroundPlane& out);

// VERTICAL variant (convenience) — ground plane directly UNDER (x,z), via the quadtree descent +
// plane-solve of MapColl_GetGroundHeight 0x697130 (walkable filter planeB>0, first hit, containment
// via MapColl_RayHitTriangle 0x695ae0). shadowPlane is biased -d-0.1 like the original path.
// NB: filter planeB>0 (walkable orientation) != filter materialIndex==1 of GetGroundPlaneForShadow
// (.WM tag) — two distinct criteria, both proven. Choose per the caller's need.
bool GetGroundPlaneUnder(const asset::CollisionMesh& mesh, float x, float z, GroundPlane& out);

} // namespace collision
} // namespace ts2::world

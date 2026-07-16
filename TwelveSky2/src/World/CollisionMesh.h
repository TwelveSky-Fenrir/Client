// World/CollisionMesh.h — moteur de PICK segment + PLAN-SOL de collision (Vague B4 / F_COLLISION).
//
// Ce module NOUVEAU complète le moteur de requêtes déjà en place dans World/WorldMap.cpp
// (namespace ts2::world::collision — MapColl_GetGroundHeight 0x697130, RaycastNearest 0x6960c0,
// SweepSphereNearest 0x696ad0, SlideMoveGround 0x697330, PointInTriangleXZ 0x695c70, etc., tous
// portés byte-fidèlement). Il n'y a AUCUNE duplication : on n'ajoute ici que la chaîne de PICK
// SEGMENT « variante A » et l'extraction du PLAN-SOL — absentes jusqu'ici — nécessaires à l'ombre
// planaire projetée de la Vague B (F_ENTITY3D). Les décodeurs typés G01 (CollisionFace 156o,
// CollisionQuadNode 48o, TerrainVertex 40o) vivent déjà dans Asset/WorldChunk.{h,cpp} et sont
// consommés tels quels (asset::CollisionMesh).
//
// Chaîne réversée (ancres @EA, imagebase 0x400000) :
//   Model_RenderPlanarShadow   0x40f720  — construit un segment model+hauteur -> +lightDir, pick,
//                                          lit tris[hit].plane (+124/+128/+132/+136), biais -d-0.1,
//                                          D3DXMatrixShadow. C'EST le consommateur du plan-sol.
//   Collision_SegPickA         0x420d60  — gate AABB racine + descend les 4 enfants (impact le + proche).
//   Collision_SegNodeNearestA  0x420f40  — récursif ; FEUILLE filtre materialIndex==1 (faces marchables
//                                          taggées .WM), sinon récurse 4 enfants ; garde this[1]==2.
//   Collision_RayTriangle      0x420240  — plane-solve une-face (n·dir<0) + t>=0 + containment.
//   Collision_PointInTriangle  0x41fe30  — point-dans-triangle projeté en XZ (côté-du-centroïde/3 arêtes).
//   Collide_AABBvsSegmentSAT   0x4070d0  — SAT segment/AABB ; ALGORITHME IDENTIQUE à Collide_SegmentAABB
//                                          0x69fb20 (déjà porté = collision::SegmentAABB) à permutation
//                                          d'arguments près -> réutilisé, pas re-porté.
//   MapColl_GetGroundHeight    0x697130  — variante VERTICALE (GetGroundPlaneUnder) : descente quadtree
//                                          XZ + plane-solve + MapColl_RayHitTriangle 0x695ae0.
//
// RÈGLE #0 : chaque bloc du .cpp cite son ancre IDA. Fonctions pures, sans état, build-safe
// (guards maille vide / index hors bornes). IDA = SEULE VÉRITÉ ; IDB en lecture seule.
#pragma once
#include <cstdint>

// MODULE LEAF : on opère sur la maille DÉCODÉE (asset::CollisionMesh, Gap G01, Asset/WorldChunk.h).
// Déclarations anticipées seulement — le .cpp inclut Asset/WorldChunk.h + World/WorldMap.h.
namespace ts2::asset {
struct CollisionMesh;
struct CollisionFace;
} // namespace ts2::asset

namespace ts2::world {
namespace collision {

// ---------------------------------------------------------------------------
// GroundPlane — plan-sol renvoyé pour l'ombre planaire (Vague B / F_ENTITY3D).
// Réf IDA : Model_RenderPlanarShadow 0x40f720 (@0x40fa52..0x40fb28).
//   plane[4]       = {a,b,c,d} = tris[faceIndex].plane BRUT (+124/+128/+132/+136) = normale + D.
//   shadowPlane[4] = {a,b,c, -d - 0.1f} — plan PRÊT pour D3DXMatrixShadow : D négé + biais anti
//                    z-fight (v45[3] = v41*-1.0 - 0.1 @0x40fafc). C'est ce vecteur que le binaire
//                    passe à j_D3DXMatrixShadow(mat, light4, shadowPlane) @0x40fb28.
//   hit[3]         = point d'impact du pick sur le sol (Collision_SegPickA sortie a3).
//   faceIndex      = index de la face de sol touchée dans mesh.tris[].
// Le vecteur lumière à passer À CÔTÉ = light4 = { -lightDir.x, -lightDir.y, -lightDir.z, 0.0 }
// (v38..v41 @0x40fb08..0x40fb24 : flt_18C53C0/C4/C8 négés, w=0 = directionnel). lightDir provient
// des globals flt_18C53C0/18C53C4/18C53C8 (direction de projection d'ombre) — fourni par F_ENTITY3D.
// ---------------------------------------------------------------------------
struct GroundPlane {
    float    plane[4]       = {0.0f, 0.0f, 0.0f, 0.0f}; // a,b,c,d bruts (tris[faceIndex].plane)
    float    shadowPlane[4] = {0.0f, 0.0f, 0.0f, 0.0f}; // a,b,c,-d-0.1 (prêt D3DXMatrixShadow)
    float    hit[3]         = {0.0f, 0.0f, 0.0f};       // point d'impact du pick sur le sol
    uint32_t faceIndex      = 0;                         // face de sol touchée (index dans tris[])
    bool     valid          = false;                     // false = aucun sol sous le pick
};

// ---------------------------------------------------------------------------
// Chaîne de PICK SEGMENT « variante A » (impact de face marchable le plus proche).
// ---------------------------------------------------------------------------

// Collision_PointInTriangle 0x41fe30 — point dans le triangle PROJETÉ en XZ (côté-du-centroïde sur
// les 3 arêtes ; p[1]=Y ignoré). Utilise les mêmes 156o de face que MapColl_RayHitTriangle 0x695ae0
// mais un test EDGE-SIDEDNESS distinct (pas barycentrique). faceIndex hors bornes -> false.
bool PointInTriangleProjXZ(const asset::CollisionMesh& mesh, uint32_t faceIndex, const float p[3]);

// Collision_RayTriangle 0x420240 — plane-solve UNE-FACE : n·dir doit être < 0 (0x420285),
// t = (d - n·start)/(n·dir) >= 0 (0x4202d1), hit = start + t*dir, puis PointInTriangleProjXZ.
// outHit rempli seulement si touché (renvoie true).
bool RayTriangleHit(const asset::CollisionMesh& mesh, uint32_t faceIndex,
                    const float start[3], const float dir[3], float outHit[3]);

// Collision_SegNodeNearestA 0x420f40 — descente récursive du quadtree (nodeIndex ; 0 = racine).
// FEUILLE (child[0]==-1) : pour chaque face de la feuille, FILTRE materialIndex==1 (0x421128 :
// faces marchables taggées .WM/WORLD2), test RayTriangleHit, retient l'impact le plus proche de
// `start`. NŒUD INTERNE : récurse les 4 enfants. Gate AABB = collision::SegmentAABB (== 0x4070d0).
// outFaceIndex/outHit = impact le plus proche. Renvoie true si un impact a été trouvé.
bool SegNodeNearest(const asset::CollisionMesh& mesh, uint32_t nodeIndex,
                    const float start[3], const float dir[3],
                    uint32_t& outFaceIndex, float outHit[3]);

// Collision_SegPickA 0x420d60 — pick du SEGMENT [near -> far] contre la maille. Gate AABB de la
// RACINE (near, dir=far-near) puis descend ses 4 enfants (SegNodeNearest), impact le plus proche de
// `near`. Garde : maille active + near != far (0x420da7). outFaceIndex/outHit remplis si touché.
bool SegPickNearest(const asset::CollisionMesh& mesh, const float nearPt[3], const float farPt[3],
                    uint32_t& outFaceIndex, float outHit[3]);

// ---------------------------------------------------------------------------
// PLAN-SOL pour l'ombre planaire (Vague B) — deux fournisseurs.
// ---------------------------------------------------------------------------

// Model_RenderPlanarShadow 0x40f720 (@0x40f97d..0x40fb00) — reproduit l'extraction du plan-sol :
//   near = { pos.x, pos.y + modelHeight, pos.z }              (@0x40f97d/0x40f995/0x40f9a0)
//   far  = near + lightDir                                    (@0x40f9ae/0x40f9bc/0x40f9ca)
//   pick = Collision_SegPickA(near, far)                      (@0x40f9ce)
//   garde distance : |hit - near| <= maxDist                  (@0x40fa39)
//   plane = tris[hit].plane ; garde b != 0 && (d - x*a - z*c)/b <= pos.y + 0.1  (@0x40fac2)
//   out.shadowPlane = { a, b, c, -d - 0.1 }                   (@0x40fafc)
// Renvoie true + remplit `out` si un sol d'ombre valide existe ; false sinon (out.valid=false).
// `lightDir` = flt_18C53C0/C4/C8 (direction de projection d'ombre, fournie par l'appelant).
bool GetGroundPlaneForShadow(const asset::CollisionMesh& mesh, const float modelPos[3],
                             float modelHeight, const float lightDir[3], float maxDist,
                             GroundPlane& out);

// Variante VERTICALE (commodité) — plan-sol directement SOUS (x,z), via la descente quadtree +
// plane-solve de MapColl_GetGroundHeight 0x697130 (filtre marchable planeB>0, 1er hit, containment
// MapColl_RayHitTriangle 0x695ae0). Le shadowPlane est biaisé -d-0.1 comme la voie d'origine.
// NB : filtre planeB>0 (orientation marchable) ≠ filtre materialIndex==1 de GetGroundPlaneForShadow
// (tag .WM) — deux critères distincts, tous deux prouvés. À choisir selon le besoin de l'appelant.
bool GetGroundPlaneUnder(const asset::CollisionMesh& mesh, float x, float z, GroundPlane& out);

} // namespace collision
} // namespace ts2::world

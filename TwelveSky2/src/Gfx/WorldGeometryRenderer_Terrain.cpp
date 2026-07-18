// Gfx/WorldGeometryRenderer_Terrain.cpp — terrain (.WG) build/cull/render, split out of
// WorldGeometryRenderer.cpp; see WorldGeometryRenderer.h for the full class banner.
// (Placement format decoded; ALL A frames are now rendered — parts with A>1 replay the sway
// flipbook instead of being pinned to frame 0, cf. .h banner point 5, 2026-07-14 update.)
#include "Gfx/WorldGeometryRenderer.h"
#include "Asset/WorldChunk.h"
#include "World/WorldIntegration.h"
#include "Core/Log.h"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

// Terrain fixed-function FVF: 0x212 = 530 = D3DFVF_XYZ|NORMAL|TEX2 (2 UV sets, stride 40).
// IDA anchor: Terrain_Render 0x698670 SetFVF(530) @0x698e6d.
constexpr DWORD kFvfTerrain = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2; // 0x212
static_assert(kFvfTerrain == 0x212, "terrain FVF must be 530 (0x212)");

// TERRAIN DRAW RANKS — exact list of layers that Terrain_Render 0x698670 draws.
// (Pass 4 / W5, terrain-motion front: fixes the "ghost geometry" issue and the a5=2 pass.)
//
// The binary has only 2 passes: entry guard @0x698676-0x6986a2 = `*(a1+4) && *(a1+8)==1 &&
// a5>=1 && a5<=2`; call sites Scene_InGameRender @0x52d9be (a5=1) and @0x52ead8 (a5=2).
// Each loop walks the materials (a1+16, stride 52: +40 category, +44 subOrder, +48 texture)
// and draws only those passing the test below (plus visible faces > 0):
//
//  rank 0  cat==2            @0x698f97  (a5=1) no sub test — lightmap ON, sampler CLAMP
//  rank 1  cat==4            @0x69902d  (a5=1) no sub test — lightmap ON, sampler WRAP
//  rank 2  cat==1 && sub==0  @0x6990fa  (a5=1) lightmap OFF (cut @0x6990ba), WRAP
//  rank 3  cat==3            @0x6992c4  (a5=1) water, CLAMP — gate @0x69914d: cat3/sub0 exists
//  rank 4  cat==1 && sub==1  @0x69941b  (a5=1) ALPHATEST @0x6993d4 + ALPHAREF=128 @0x6993e9
//  rank 5  cat==3            @0x6995e7  (a5=1) water, CLAMP — gate @0x699473: cat3/sub1 exists
//                                       (alpha-test STAYS on: only cut @0x699704)
//  rank 6  cat==1 && sub==2  @0x698811  (a5=2) ZWRITE OFF @0x6987f3 + ALPHABLEND ON @0x698804
//  rank 7  cat==3            @0x698a0a  (a5=2) blended water — gate @0x698890: cat3/sub2 exists
//  EVERYTHING ELSE = never drawn by any loop in the binary => rank -1, discarded in Build
//  (this was the "ghost geometry": the old catch-all `return 6` used to upload AND draw
//  opaque layers the original engine ignores entirely).
//
//  PITFALL (verified, counter-intuitive): the 3 WATER loops test `cat==3` ALONE — they have
//  no subOrder test (only the preceding GATE checks a specific sub). Any cat3 layer, whatever
//  its subOrder, is drawn once its gate passes: cat==3 must NEVER be filtered by sub (sub only
//  picks the rank/pass). Same for cat2/cat4: every subOrder is drawn (hence rank-by-category
//  alone, no sub test).
//
//  Real domain measured on the 97 .WG files of D07_GWORLD (trailer inventory + carried faces):
//  (1,0) (1,1) (1,2) (2,0) (2,1) (2,2) (3,0) (3,2) (4,0) (4,1) (4,2) — i.e. cat in {1,2,3,4},
//  sub in {0,1,2}. The -1 filter therefore discards NO real face (0 material, 0 face): it is a
//  fidelity safeguard, not a visual fix. The real visual gain is elsewhere — (1,2) and (3,2)
//  (59 + 49 materials, ~73,000 faces) move from "opaque, z-write ON" to the real a5=2 pass
//  (blend + z-write OFF), and (2,1)/(4,1) stop suffering a spurious alpha-test.
enum : int {
    kRank_Cat2     = 0, kRank_Cat4   = 1, kRank_Cat1Sub0 = 2, kRank_Water0 = 3,
    kRank_Cat1Sub1 = 4, kRank_Water1 = 5, kRank_Cat1Sub2 = 6, kRank_Water2 = 7,
    kRank_NotDrawn = -1,
    kRank_FirstPass2 = kRank_Cat1Sub2 // ranks >= this: a5=2 sub-pass (blend + z-write off)
};

// WATER material category (trailer[0]==3). IDA anchor: MapColl_LoadMapFile @0x698033
// (looks for mat+40==3 -> triggers wave/falloff).
constexpr uint32_t kTerrainCatWater = 3;

// Returns a layer's draw rank, or kRank_NotDrawn if the binary never draws it.
int TerrainLayerRank(uint32_t category, uint32_t subOrder) {
    if (category == 2) return kRank_Cat2;                    // @0x698f97, any sub
    if (category == 4) return kRank_Cat4;                    // @0x69902d, any sub
    if (category == 1) {                                     // cat1: sub picks the pass
        if (subOrder == 0) return kRank_Cat1Sub0;            // @0x6990fa
        if (subOrder == 1) return kRank_Cat1Sub1;            // @0x69941b (alpha-test)
        if (subOrder == 2) return kRank_Cat1Sub2;            // @0x698811 (a5=2, blend)
        return kRank_NotDrawn;                               // cat1/sub>=3: no loop covers it
    }
    if (category == kTerrainCatWater) {                      // water loops have NO sub test
        if (subOrder == 2) return kRank_Water2;              // @0x698a0a (a5=2), gate @0x698890
        if (subOrder == 1) return kRank_Water1;              // @0x6995e7, gate @0x699473
        return kRank_Water0;                                 // @0x6992c4, gate @0x69914d — and
        // any unproven sub falls back here: the water loops have NO sub test, so a cat3 layer
        // is never discarded (case absent from the 97 real .WG files, cf. banner above).
    }
    return kRank_NotDrawn;                                   // cat 0, 5, 6... never drawn
}

// Packs a float into the DWORD expected by SetTextureStageState/SetRenderState (bit copy).
inline DWORD F2DW(float f) { DWORD d; std::memcpy(&d, &f, 4); return d; }

// TWS-01 (Pass 4 / W11) — WAVE TEXTURE = DEAD CODE, DELIBERATELY NOT PORTED.
// Re-proven in IDA: the binary never creates a wave texture on the live terrain path. The only
// creator, cWorldMesh_MakeWaterWaveTexture 0x451220, has 2 callers (cWorldMesh_LoadG3W 0x449800,
// cWorldMesh_LoadQuadtreeWM2 0x44d440), both with 0 xrefs — unreachable from WinMain 0x4609C0.
// The perturbation map bound by Terrain_Render at stage 0 (`SetTexture(0, *(a1+20))` @0x69928f,
// under COLOROP=BUMPENVMAPLUMINANCE @0x699206) is actually the radial FALLOFF texture:
// *(a1+20) == this+5, written only by MapColl_CreateFalloffTexture 0x694ca0 (@0x694cac), called
// @0x698043 by MapColl_LoadMapFile 0x697b30 under the "material category 3" gate @0x698033;
// object identity proven via matching stride-52 writes/reads (+40==3) between the two functions.
// The earlier port both revived dead code 0x451220 AND bound the wrong texture instead of the
// falloff — both fixed here: no wave generator is ported (RULE #7: dead binary code stays dead).

// Generates the NxN radial falloff texture, V8U8. Port of MapColl_CreateFalloffTexture 0x694ca0:
// value = round(-sqrt((x/N-0.5)^2+(y/N-0.5)^2) * 1.442695040888963407), written to both du and dv.
IDirect3DTexture9* MakeFalloffTexture(IDirect3DDevice9* dev, UINT dim) {
    if (!dev || dim == 0) return nullptr;
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(D3DXCreateTexture(dev, dim, dim, 1, 0, D3DFMT_V8U8, D3DPOOL_MANAGED, &tex)) || !tex)
        return nullptr;
    D3DLOCKED_RECT lr;
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) { tex->Release(); return nullptr; }
    auto* row = static_cast<uint8_t*>(lr.pBits);
    const float N = static_cast<float>(dim);
    constexpr float kInvLn2 = 1.442695040888963407f; // 1/ln(2), exact literal from the binary @0x694ca0
    for (UINT y = 0; y < dim; ++y) {
        int8_t* texel = reinterpret_cast<int8_t*>(row);
        for (UINT x = 0; x < dim; ++x) {
            const float dx = static_cast<float>(x) / N - 0.5f;
            const float dy = static_cast<float>(y) / N - 0.5f;
            const float v = std::nearbyint(-std::sqrt(dx * dx + dy * dy) * kInvLn2); // frndint
            const int8_t b = static_cast<int8_t>(std::max(-128.0f, std::min(127.0f, v)));
            texel[0] = b; texel[1] = b;
            texel += 2;
        }
        row += lr.Pitch;
    }
    tex->UnlockRect(0);
    return tex;
}

// Dimension of the water falloff/bump-env texture. PROVEN (TWS-02) = 256, not a build-safe
// choice: MapColl_Construct 0x693080 @0x6930b1 (`mov dword ptr [esi], 100h`) and
// MapColl_ResetHeader 0x693120 @0x693129 set MapColl+0 = 256, consumed by
// MapColl_CreateFalloffTexture 0x694ca0 @0x694cd3 (D3DXCreateTexture(dev, *this, *this, ...)) for
// both width and height, and by its loop bounds @0x694d08/@0x694d1a. The value is never read from
// the .WG file: MapColl_LoadMapFile 0x697b30 writes this+1..+4,+21,+22,+33..+41 but never *this
// (full decompile checked) -> 256 is a static constructor constant, not runtime data. (The former
// "cWorldMesh+0 at runtime -> default 64" banner was wrong on both counts: wrong object —
// cWorldMesh is dead, cf. TWS-01 — and the value is actually available statically.)
constexpr UINT kWaterTexDim = 256;

// Terrain FVF -> FfTerrainVertex 40 bytes: the on-disk asset::TerrainVertex (pos/normal/uv0/uv1)
// is BIT-FOR-BIT identical -> no conversion, direct copy (memcpy in the cull batch).
static_assert(sizeof(asset::TerrainVertex) == 40, "on-disk TerrainVertex = 40 bytes (direct FF memcpy)");

// FRONT F_TERRAIN (B5) — TERRAIN QUADTREE/FRUSTUM CULL (read-only render helpers).
// Byte-exact reproduction of the binary's 3 cull primitives (checked in IDA on 2026-07-17):
//   Cam_FrustumTestAABB      0x69f230  (AABB rejected iff all 8 corners are behind ONE plane)
//   Cam_FrustumTestSphere2x  0x69f0e0  (sphere: plane*c + d >= -2*radius, DOUBLED margin)
//   MapColl_CollectLeafFaces 0x694b50  (frustum-cull quadtree descent -> visible leaves)
// The original stores the 6 frustum planes at g_GfxRenderer+334 (offsets +334..+357, cf.
// Cam_FrustumTestSphere2x decompile). ClientSource has no such singleton: the planes are
// RECONSTRUCTED from view*proj (Gribb-Hartmann), yielding the SAME planes (world space, inward
// normals, inside <=> a*x+b*y+c*z+d >= 0). Planes are NORMALIZED: the sphere test's -2*radius
// margin is in world units, so the scale factor MUST be unit.
struct FrustumPlanes { float pl[6][4]; }; // [plane][a,b,c,d]; g_GfxRenderer+(334+4*p)

// Extracts the 6 inward, normalized planes from vp = view*proj (D3DX row-vector, LH D3D z in [0,w]).
FrustumPlanes ExtractFrustum(const D3DXMATRIX& vp) {
    FrustumPlanes f;
    // col j (0-based) component i = vp._{i+1}{j+1}. Left=col3+col0, Right=col3-col0,
    // Bottom=col3+col1, Top=col3-col1, Near=col2 (z>=0 in D3D), Far=col3-col2.
    f.pl[0][0]=vp._14+vp._11; f.pl[0][1]=vp._24+vp._21; f.pl[0][2]=vp._34+vp._31; f.pl[0][3]=vp._44+vp._41; // left
    f.pl[1][0]=vp._14-vp._11; f.pl[1][1]=vp._24-vp._21; f.pl[1][2]=vp._34-vp._31; f.pl[1][3]=vp._44-vp._41; // right
    f.pl[2][0]=vp._14+vp._12; f.pl[2][1]=vp._24+vp._22; f.pl[2][2]=vp._34+vp._32; f.pl[2][3]=vp._44+vp._42; // bottom
    f.pl[3][0]=vp._14-vp._12; f.pl[3][1]=vp._24-vp._22; f.pl[3][2]=vp._34-vp._32; f.pl[3][3]=vp._44-vp._42; // top
    f.pl[4][0]=vp._13;        f.pl[4][1]=vp._23;        f.pl[4][2]=vp._33;        f.pl[4][3]=vp._43;        // near
    f.pl[5][0]=vp._14-vp._13; f.pl[5][1]=vp._24-vp._23; f.pl[5][2]=vp._34-vp._33; f.pl[5][3]=vp._44-vp._43; // far
    for (int p = 0; p < 6; ++p) {
        const float len = std::sqrt(f.pl[p][0]*f.pl[p][0] + f.pl[p][1]*f.pl[p][1] + f.pl[p][2]*f.pl[p][2]);
        if (len > 1e-8f) { const float inv = 1.0f / len; for (int k = 0; k < 4; ++k) f.pl[p][k] *= inv; }
    }
    return f;
}

// Cam_FrustumTestAABB 0x69f230: returns false (reject) IFF, for ONE plane, all 8 corners of
// [mn,mx] have plane*corner + d < 0 (box entirely behind that plane). Conservative (never a
// false reject).
bool FrustumTestAABB(const FrustumPlanes& fr, const float mn[3], const float mx[3]) {
    for (int p = 0; p < 6; ++p) {
        const float a = fr.pl[p][0], b = fr.pl[p][1], c = fr.pl[p][2], d = fr.pl[p][3];
        bool allOut = true;
        for (int ci = 0; ci < 8; ++ci) {                       // 8 corners {mn|mx}^3
            const float x = (ci & 1) ? mx[0] : mn[0];
            const float y = (ci & 2) ? mx[1] : mn[1];
            const float z = (ci & 4) ? mx[2] : mn[2];
            if (a * x + b * y + c * z + d >= 0.0f) { allOut = false; break; } // this corner is on the right side
        }
        if (allOut) return false;                              // 8 corners behind -> reject the subtree
    }
    return true;                                               // intersecting/inside -> kept
}

// Cam_FrustumTestSphere2x 0x69f0e0: sphere (center, radius) kept IFF plane*c + d >= -2*radius for
// the 6 planes (DOUBLED margin @0x69f0ee `v4 = a3 * -2.0`).
bool FrustumTestSphere2x(const FrustumPlanes& fr, const float c[3], float radius) {
    const float v4 = radius * -2.0f;                           // 0x69f0ee
    for (int p = 0; p < 6; ++p)
        if (fr.pl[p][0]*c[0] + fr.pl[p][1]*c[1] + fr.pl[p][2]*c[2] + fr.pl[p][3] < v4) return false; // 0x69f21a
    return true;
}

// MapColl_CollectLeafFaces 0x694b50: recursive quadtree descent, collects leaves whose AABB
// intersects the frustum. Byte-exact: guard trisNum>0 (node+24 @0x694b65), AABB test (@0x694b7d,
// break/return on reject -> subtree pruned), leaf = child[0]==-1 (@0x694b93 -> append @0x694bed),
// else recurse child[0..2] (@0x694b98/@0x694baa/@0x694bbc) + tail-loop child[3] (@0x694bc7).
// Root = node index 0. `out` = indices of visible leaf nodes (a1+152).
void CollectLeaves(const std::vector<asset::CollisionQuadNode>& nodes, const FrustumPlanes& fr,
                   int nodeIdx, std::vector<int32_t>& out) {
    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= nodes.size()) return;
    if (nodes[nodeIdx].trisNum == 0) return;                    // *(node+24)==0 -> empty node  0x694b65
    for (;;) {
        const asset::CollisionQuadNode& n = nodes[nodeIdx];
        if (!FrustumTestAABB(fr, n.bboxMin, n.bboxMax)) return; // out of frustum -> whole subtree pruned  0x694b7d
        if (n.child[0] == -1) { out.push_back(nodeIdx); return; } // LEAF  0x694b93 / append 0x694bed
        CollectLeaves(nodes, fr, n.child[0], out);              // 0x694b98
        CollectLeaves(nodes, fr, n.child[1], out);              // 0x694baa
        CollectLeaves(nodes, fr, n.child[2], out);              // 0x694bbc
        nodeIdx = n.child[3];                                   // tail loop  0x694bc7
        if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= nodes.size()) return;
        if (nodes[nodeIdx].trisNum == 0) return;                // while *(child3+24)!=0  0x694b7d
    }
}

} // namespace

// FRONT W3-F3 — build of the .WG terrain FIXED-FUNCTION layers ("the ground").
//
// IDA anchor: Terrain_Render 0x698670 ("render quadtree terrain tile/water/land layers with
// reflections"), called 2x/frame from Scene_InGameRender 0x52d0b0, BEFORE the .WO objects.
// Source: WorldAssets::Faces() -> asset::MapFaceChunk (mesh.tris = CollisionFace 156o,
// per-material textures[]). Original render model:
//   - a1+88  = faces (156o); face.materialIndex@0; 120o=3*40 vertices copied (qmemcpy @0x698e21);
//   - a1+16  = materials (stride 52): +40 CATEGORY (trailer[0]), +44 subOrder (trailer[1]),
//              +48 texture (filled by Tex_LoadCompressedFromHandle 0x6a9cf0);
//   - SetFVF(530) @0x698e6d, DrawPrimitiveUP(TRIANGLELIST, count, VB+120*start, stride 40).
// Here: one TerrainLayer(diffuse, category, subOrder, materialIndex) per drawn material, SORTED
// by rank to reproduce Terrain_Render(a5=1)'s draw order. FRONT F_TERRAIN (B5): the layer no
// longer owns a static VB/IB — rendering is a PER-FRAME quadtree/frustum CULL (cullTerrain())
// that fills a CPU batch (terrainBatch_, 3 vertices/face) then DrawPrimitiveUP per material,
// EXACTLY like the binary. buildTerrain therefore only handles: per-material textures, a COPY of
// the quadtree/faces/index heap (a1+88/a1+140), cumulative batch bases (matBase_), water textures
// (falloff, cat==3), and the .SHADOW lightmap. Vertices = asset::TerrainVertex bit-for-bit as
// FfTerrainVertex 40o.
bool WorldGeometryRenderer::buildTerrain(const world::WorldAssets& assets) {
    // releaseObjects() (called by Build before us) has already purged terrainLayers_/terrainTextures_.
    if (!dev_) return false; // build-safe: no device
    const asset::WorldChunk* chunk = assets.Faces();
    if (!chunk) return false;                       // no .WG loaded for this zone
    const asset::MapFaceChunk* wg = chunk->AsFace();
    if (!wg) return false;                          // chunk present but not of type WG

    const asset::CollisionMesh& mesh = wg->mesh;
    const uint32_t numMat = wg->numMaterials;
    if (mesh.tris.empty() || numMat == 0) {
        TS2_LOG("WorldGeometryRenderer::buildTerrain : .WG sans face/materiau (numTri=%zu numMat=%u).",
                mesh.tris.size(), numMat);
        return true; // empty .WG: nothing to draw, not an error
    }

    // 1) One diffuse texture per material (.WG order). textures[m].present==false => nullptr.
    terrainTextures_.assign(numMat, nullptr);
    for (uint32_t m = 0; m < numMat && m < wg->textures.size(); ++m)
        terrainTextures_[m] = createTextureFromBlock(dev_, wg->textures[m]);

    // 2) FRONT F_TERRAIN (B5) — CPU copy of the .WG cull data (Render() no longer has access to
    //    WorldAssets): 156o faces (a1+88), 48o quadtree (a1+140, root = index 0), and the face
    //    index heap (node.trisIndex = offset into this heap). The quadtree is already decoded by
    //    Asset/WorldChunk.cpp (CollisionMesh::nodes/triIndices, anchor MapColl_LoadFaces 0x694510)
    //    -> reused as-is, no re-decoding needed.
    terrainNumMaterials_ = numMat;
    terrainFaces_       = mesh.tris;         // a1+88
    terrainNodes_       = mesh.nodes;        // a1+140
    terrainTriIndices_  = mesh.triIndices;   // faceRefIndex heap

    // 3) Face count PER MATERIAL -> cumulative base matBase_ (a1+144): each material gets a
    //    contiguous region [matBase_[m], matBase_[m]+count[m]) in the batch, sized for the worst
    //    case (all its faces visible). The original precomputes these bases at .WG load time.
    std::vector<uint32_t> faceCountPerMat(numMat, 0);
    size_t outOfRange = 0;
    for (const asset::CollisionFace& f : terrainFaces_) {
        if (f.materialIndex < numMat) ++faceCountPerMat[f.materialIndex];
        else ++outOfRange;                   // materialIndex out of bounds -> never batched
    }
    matBase_.assign(numMat, 0);
    uint32_t cum = 0;
    for (uint32_t m = 0; m < numMat; ++m) { matBase_[m] = cum; cum += faceCountPerMat[m]; }
    const uint32_t drawableFaces = cum;      // faces with a valid material (batch capacity)

    // 4) Per-frame working buffers (allocated ONCE here): CPU batch (3 vertices per face, a1+164),
    //    per-material cursor (a1+160), "face seen this frame" flag (a1+156, indexed by face),
    //    visible-leaf list (a1+152). The last three are reset in cullTerrain.
    terrainBatch_.assign(static_cast<size_t>(drawableFaces) * 3, FfTerrainVertex{}); // a1+164
    matCounter_.assign(numMat, 0);                                                    // a1+160
    faceSeen_.assign(terrainFaces_.size(), 0);                                        // a1+156
    terrainLeafScratch_.clear();
    terrainLeafScratch_.reserve(terrainNodes_.size());                                // a1+152 (upper bound)

    // 5) One TerrainLayer per material that CARRIES faces AND is actually DRAWN by the binary
    //    (rank >= 0). (cat,sub) pairs Terrain_Render 0x698670 draws in no loop (rank < 0) are
    //    discarded here (ghost geometry). No more VB/IB: the layer only references its batch
    //    region via `materialIndex` (the batch is filled per frame and drawn with DrawPrimitiveUP).
    size_t ghostLayers = 0, ghostFaces = 0;
    bool hasWater = false;
    for (uint32_t m = 0; m < numMat; ++m) {
        if (faceCountPerMat[m] == 0) continue;
        TerrainLayer layer;
        layer.diffuse       = terrainTextures_[m]; // non-owning reference
        // trailer[0]=category, trailer[1]=subOrder (proven Tex_LoadCompressedFromHandle 0x6a9cf0).
        layer.category      = (m < wg->textures.size()) ? wg->textures[m].trailer[0] : 0;
        layer.subOrder      = (m < wg->textures.size()) ? wg->textures[m].trailer[1] : 0;
        layer.rank          = TerrainLayerRank(layer.category, layer.subOrder);
        layer.materialIndex = m;
        if (layer.rank == kRank_NotDrawn) {
            // The binary has no loop for this (cat,sub): counted/logged (expected 0 on the 97
            // real .WG files; if != 0, re-check TerrainLayerRank BEFORE drawing conclusions).
            ++ghostLayers;
            ghostFaces += faceCountPerMat[m];
            continue;
        }
        if (layer.category == kTerrainCatWater) hasWater = true;
        terrainLayers_.push_back(layer);
    }
    terrainFaceCount_ = drawableFaces;

    // 6) Sort layers by rank — reproduces Terrain_Render's EXACT draw order: pass a5=1
    //    (ranks 0..5) then pass a5=2 (ranks 6..7). renderTerrain() relies on this ascending
    //    order to open the blended sub-pass only once (one-way switch).
    std::stable_sort(terrainLayers_.begin(), terrainLayers_.end(),
                     [](const TerrainLayer& a, const TerrainLayer& b) { return a.rank < b.rank; });

    // 7) Water (TWS-01/TWS-02): the radial FALLOFF texture (V8U8 256x256) is created ONCE if a
    //    cat==3 layer exists, a faithful mirror of MapColl_LoadMapFile @0x698043 ->
    //    MapColl_CreateFalloffTexture 0x694ca0 (the only live writer of *(a1+20), the stage-0
    //    bump-map). NO wave texture: its generator 0x451220 is dead (cf. TWS-01 banner above).
    if (hasWater) {
        falloffTex_ = MakeFalloffTexture(dev_, kWaterTexDim);
    }

    // 8) .SHADOW lightmap (stage 1, uv1) — GPU texture from the raw DDS bytes exposed by
    //    WorldAssets (the FF vertex does have uv1: the "single TEXCOORD" G8 TODO is gone).
    shadowTex_ = createTextureFromDds(dev_, assets.ShadowBytes());

    TS2_LOG("WorldGeometryRenderer::buildTerrain (B5) : %u faces dessinables sur %u materiaux -> %zu "
            "couches FF (%zu faces hors bornes) ; quadtree=%zu noeuds / %zu index de faces (cull par "
            "frame) ; batch CPU=%zu sommets ; eau=%d (falloff=%p) lightmap=%p ; sol .WG pret (FVF 530, "
            "world=identite). Filtre categories (Terrain_Render 0x698670) : %zu couches / %zu faces "
            "ecartees car jamais dessinees par le binaire (attendu 0 sur donnees reelles -- si != 0, "
            "re-verifier la table TerrainLayerRank AVANT de conclure).",
            drawableFaces, numMat, terrainLayers_.size(), outOfRange,
            terrainNodes_.size(), terrainTriIndices_.size(), terrainBatch_.size(),
            hasWater ? 1 : 0, (void*)falloffTex_, (void*)shadowTex_,
            ghostLayers, ghostFaces);
    return true;
}

// FRONT F_TERRAIN (B5) — TERRAIN QUADTREE/FRUSTUM CULL, PER FRAME.
// Byte-exact reproduction of Terrain_Render 0x698670's collect/batch block (pass a5=1,
// 0x698ce7-0x698e3c), checked in IDA (decompile + disasm 0x698d36) on 2026-07-17:
//   reset leafCount/faceSeen/matCounter (0x698cf9/0x698cff/0x698d1b) -> quadtree descent
//   (MapColl_CollectLeafFaces 0x694b50) -> per leaf, per collected face: dedup (0x698d8c),
//   backface dot(planeN,eye)>=planeD (0x698dd4, eye=g_CameraPos), doubled-margin sphere
//   (Cam_FrustumTestSphere2x 0x69f0e0 @0x698de9), 120o batch grouped by material (0x698e21).
// The result (terrainBatch_ + matCounter_) is consumed by renderTerrain() via DrawPrimitiveUP.
// Optimization is CONFORMANT with the binary: the cull only removes NON-visible faces (out of
// frustum or backfacing) -> same visible faces on screen as a flat draw, fewer triangles.
void WorldGeometryRenderer::cullTerrain(const D3DXMATRIX& view, const D3DXMATRIX& proj,
                                        const D3DXVECTOR3& eye) {
    if (terrainNodes_.empty() || terrainFaces_.empty() || matBase_.empty()) return;

    // 6 frustum planes (world space) reconstructed from view*proj (cf. ExtractFrustum):
    // equivalent to g_GfxRenderer+334 used by Cam_FrustumTestAABB/Sphere2x.
    D3DXMATRIX vp;
    D3DXMatrixMultiply(&vp, &view, &proj);
    const FrustumPlanes fr = ExtractFrustum(vp);

    // Per-frame buffer reset (Terrain_Render 0x698cf9/0x698cff/0x698d1b).
    std::fill(matCounter_.begin(), matCounter_.end(), 0u);
    std::fill(faceSeen_.begin(), faceSeen_.end(), static_cast<uint8_t>(0));
    terrainLeafScratch_.clear();

    // Frustum-cull quadtree descent from the root (node 0) -> visible leaves.
    CollectLeaves(terrainNodes_, fr, 0, terrainLeafScratch_); // MapColl_CollectLeafFaces((a1),0) 0x698d27

    // Per leaf, per referenced face (Terrain_Render 0x698d36-0x698e3c).
    for (int leafIdx : terrainLeafScratch_) {
        if (leafIdx < 0 || static_cast<size_t>(leafIdx) >= terrainNodes_.size()) continue;
        const asset::CollisionQuadNode& node = terrainNodes_[leafIdx];
        const uint32_t refBase  = node.trisIndex; // offset into terrainTriIndices_ (originally node+28)
        const uint32_t refCount = node.trisNum;   // node+24
        if (static_cast<size_t>(refBase) + refCount > terrainTriIndices_.size()) continue; // OOB guard
        for (uint32_t k = 0; k < refCount; ++k) {
            const uint32_t faceIdx = terrainTriIndices_[refBase + k];         // node.faceRefIndex[k] 0x698d7a
            if (faceIdx >= faceSeen_.size()) continue;
            if (faceSeen_[faceIdx]) continue;                                 // dedup 0x698d8c
            faceSeen_[faceIdx] = 1;                                           // 0x698d98
            const asset::CollisionFace& face = terrainFaces_[faceIdx];

            // (2) BACKFACE: dot(planeN, eye) < planeD -> face facing away from the eye -> skip. 0x698dd4
            //     eye = g_CameraPos; planeN = face.plane[0..2]; planeD = face.plane[3].
            const float dotp = eye.x * face.plane[0] + eye.y * face.plane[1] + eye.z * face.plane[2];
            if (dotp < face.plane[3]) continue;

            // (3) FRUSTUM SPHERE (doubled margin). 0x698de9
            if (!FrustumTestSphere2x(fr, face.sphereCenter, face.sphereRadius)) continue;

            // (4) BATCH: 120o copy (3 vertices = &face.v0), grouped by material. 0x698e21
            const uint32_t m = face.materialIndex;                           // face+0  0x698dfe
            if (m >= terrainNumMaterials_) continue;                          // materialIndex out of bounds
            const uint32_t slot  = matBase_[m] + matCounter_[m];             // base[m] + cursor[m]
            const size_t   vbase = static_cast<size_t>(slot) * 3;            // 3 vertices/face
            if (vbase + 3 > terrainBatch_.size()) continue;                  // guard (capacity)
            std::memcpy(&terrainBatch_[vbase], &face.v0, sizeof(FfTerrainVertex) * 3); // 0x78 = 120 bytes
            ++matCounter_[m];                                                // 0x698e30
        }
    }
}

// FRONT W3-F3 — draws the .WG ground in FIXED-FUNCTION (FVF 530). IDA anchor: Terrain_Render
// 0x698670, draw order of pass a5=1. Called by Render() AFTER the camera and BEFORE the .WO
// objects (faithful order: Scene_InGameRender draws the terrain @0x52d9be before props).
// world=identity.
//
// Reproduced sequence: SetFVF(530) @0x698e6d; stage-0 texture matrix = identity @0x698f25;
// CULLMODE=NONE @0x698f37 (CPU backface in the original, neutered here); stage-1 lightmap
// MODULATE (=4, NOT MODULATE2X) @0x698f54 + SetTexture(1) @0x698f68 (uv1 — the FF vertex has 2
// UV sets, the G8 TODO is gone); layers sorted by RANK (cf. TerrainLayerRank table at the top of
// this file); water cat==3 in bump-env (bindWaterStates); alpha-test on ranks 4/5 only
// (ALPHAREF=128 @0x6993e9); sampler CLAMP/WRAP addressing per layer. States saved/restored so as
// not to pollute meshRenderer_ (which rebinds its shaders afterward).
//
// UPDATE Pass 4 / W5 (terrain-motion front) — this function now covers BOTH binary passes, in
// order: a5=1 (ranks 0..5, opaque + water + alpha-test) then a5=2 (ranks 6..7, z-write OFF +
// alpha-blend ON @0x6987f3/@0x698804). Rank < 0 layers (never drawn by the binary) no longer
// exist: buildTerrain discards them before any GPU upload.
//
// UPDATE FRONT F_TERRAIN (B5, 2026-07-17) — PER-FRAME QUADTREE/FRUSTUM CULL DONE: cullTerrain()
// (called at the top of renderTerrain) fills terrainBatch_/matCounter_ with only the visible
// faces via MapColl_CollectLeafFaces 0x694b50 + backface @0x698dd4 + Cam_FrustumTestSphere2x
// 0x69f0e0. Drawing per layer moved from DrawIndexedPrimitive (static VB, all faces) to
// DrawPrimitiveUP (CPU batch, visible faces) — exact mirror of Terrain_Render @0x698ff3. Same
// visible faces.
//
// KNOWN, ACCEPTED GAP (water drawn once per layer): the binary's 3 water loops test `cat==3`
// ALONE, each gated by the existence of a cat3/sub{0,1,2}. If a zone has BOTH cat3/sub0 and
// cat3/sub2, both gates pass and the binary draws EACH cat3 layer TWICE (opaque in a5=1, then
// blended in a5=2). Here, each layer is drawn only once, at its own subOrder's rank. Measured on
// the 97 real .WG files: 57 zones have water — 14 sub0-only (-> rank 3, exact), 38 sub2-only
// (-> rank 7, exact), and only 5 mixed zones (Z118/Z175/Z201/Z267/Z279) where the binary
// double-draws. Second-order gap, limited to these 5 zones; fixing it would require decoupling
// "gate" from "loop" (out of scope for this mission).
void WorldGeometryRenderer::renderTerrain(const D3DXMATRIX& view, const D3DXMATRIX& proj,
                                          const D3DXVECTOR3& eye) {
    if (!ready_ || terrainLayers_.empty()) return;

    // FRONT F_TERRAIN (B5) — QUADTREE/FRUSTUM CULL FIRST: fills terrainBatch_ + matCounter_ with
    // this frame's visible faces only (Terrain_Render collects BEFORE drawing, 0x698ce7).
    cullTerrain(view, proj, eye);

    // Save the states we modify (device shared with meshRenderer_).
    DWORD prevCull = D3DCULL_CCW, prevLighting = TRUE, prevAlphaTest = FALSE,
          prevAlphaRef = 0, prevAlphaFunc = D3DCMP_ALWAYS;
    dev_->GetRenderState(D3DRS_CULLMODE, &prevCull);
    dev_->GetRenderState(D3DRS_LIGHTING, &prevLighting);
    dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &prevAlphaTest);
    dev_->GetRenderState(D3DRS_ALPHAREF, &prevAlphaRef);
    dev_->GetRenderState(D3DRS_ALPHAFUNC, &prevAlphaFunc);
    // a5=2 sub-pass (ranks 6-7) + per-layer sampler addressing: extra states to render.
    DWORD prevBlend = FALSE, prevZWrite = TRUE, prevSrc = D3DBLEND_ONE, prevDst = D3DBLEND_ZERO,
          prevAddrU = D3DTADDRESS_WRAP, prevAddrV = D3DTADDRESS_WRAP;
    dev_->GetRenderState(D3DRS_ALPHABLENDENABLE, &prevBlend);
    dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prevZWrite);
    dev_->GetRenderState(D3DRS_SRCBLEND, &prevSrc);
    dev_->GetRenderState(D3DRS_DESTBLEND, &prevDst);
    dev_->GetSamplerState(0, D3DSAMP_ADDRESSU, &prevAddrU);
    dev_->GetSamplerState(0, D3DSAMP_ADDRESSV, &prevAddrV);

    // Fixed-function: no VS/PS, terrain FVF, world=identity transform + camera view/proj.
    dev_->SetVertexShader(nullptr);
    dev_->SetPixelShader(nullptr);
    dev_->SetFVF(kFvfTerrain);
    D3DXMATRIX ident; D3DXMatrixIdentity(&ident);
    dev_->SetTransform(D3DTS_WORLD, &ident);              // Terrain_Render: no WORLD SetTransform
    dev_->SetTransform(D3DTS_VIEW, &view);
    dev_->SetTransform(D3DTS_PROJECTION, &proj);
    dev_->SetTransform(D3DTS_TEXTURE0, &ident);           // stage-0 texture matrix identity @0x698f25

    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);          // unlit FF (pure texture)
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);   // CPU backface in the original @0x698f37
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

    // Stage 0: diffuse = texture (SELECTARG1), on uv0.
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,  D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);

    // FRONT FX-F4 (M5): the .SHADOW lightmap (stage 1 MODULATE) is NOT enabled globally.
    // Terrain_Render 0x698670 only binds it for cat==2 (enable @0x698f54, loop @0x698f97) and
    // cat==4 (loop @0x69902d), then DISABLES it (@0x6990ba) BEFORE cat1/water/alpha-test. This
    // gate is reproduced PER LAYER below (layers are sorted by rank: cat2/cat4 come first).
    // Stage 1 disabled at the start.
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    bool lightmapBound = false;
    // a5=2 sub-pass: ONE-WAY switch (layers are sorted by ascending rank, so ranks 6-7 are
    // necessarily contiguous at the end of the list). Anchors: ZWRITE off @0x6987f3 +
    // ALPHABLEND on @0x698804 on open, restored @0x698b21 / @0x698b32 on close.
    bool blendPassOpen = false;

    // Draw layers, already sorted by rank (pass a5=1 ranks 0..5, then pass a5=2 ranks 6..7).
    for (const TerrainLayer& layer : terrainLayers_) {
        // OPEN the a5=2 transparent sub-pass (cat1/sub2 @0x698811, water/sub2 @0x698a0a). The
        // binary draws these layers with z-write OFF and alpha-blend ON — the old code sent them
        // OPAQUE (catch-all rank 6), rendering solid what should be transparent ground/water.
        if (layer.rank >= kRank_FirstPass2 && !blendPassOpen) {
            dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);       // (14,0) @0x6987f3
            dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);    // (27,1) @0x698804
            // Terrain_Render does not set SRCBLEND/DESTBLEND: it inherits the device's permanent
            // state, set once by Gfx_InitDevice 0x69b9b0 -> (19=SRCBLEND, 5=SRCALPHA) @0x69c526
            // and (20=DESTBLEND, 6=INVSRCALPHA) @0x69c535. Set explicitly here (PROVEN values,
            // not a choice) since renderTerrain restores states on exit.
            dev_->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
            dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            blendPassOpen = true;
        }
        // FRONT FX-F4 (M5): lightmap stage 1 ONLY for cat==2 and cat==4 (Terrain_Render 0x698670:
        // enable @0x698f54 before the cat2 loop @0x698f97, disable @0x6990ba before cat1
        // @0x6990d4). Since layers are sorted by rank (cat2=0, cat4=1, then cat1/water/alpha-
        // test), this toggle turns the lightmap on for the first layers then cuts it at the first
        // non-cat2/cat4 layer -> exact functional equivalent of the two IDA sites.
        const bool wantLightmap = (shadowTex_ != nullptr) &&
                                  (layer.category == 2 || layer.category == 4);
        if (wantLightmap && !lightmapBound) {
            // ENABLE -- layer @0x698f54 (COLOROP=MODULATE) + @0x698f68 (SetTexture stage 1 =
            // *(a1+72) lightmap). COLORARG1/2 + TEXCOORDINDEX=1 explicit: NOT proven here, the
            // original inherits them from the device's permanent state (set neither by
            // Terrain_Render nor Gfx_InitDevice 0x69b9b0); TEXCOORDINDEX=1 is required for uv1.
            // CORRECTION (Pass 4/W5): the previous comment attributed this inheritance to
            // "Terrain_PushRenderState 0x69cb80" -- that is WRONG. Despite its name, 0x69cb80
            // pushes NO render state: it is a TIMER (QueryPerformanceCounter -> this+208, returns
            // (now - this+224) / this+216 = elapsed seconds), also called by App_Init @0x46242e
            // and App_FrameTick @0x4625d9. The IDA name is misleading, do not trust it.
            //
            // CORRECTION (Pass 4/W5b): the previous version of this block extended the above
            // correction with an INVENTED causal chain ("the timer's return feeds v92, hence
            // `v92 * 10.0` @0x6991ca -> this VALIDATES `wavePhase_ * 10.0f`"). IDA contradicts it,
            // across the ENTIRE Terrain_Render 0x698670 function (scan 0x698670-0x699800):
            //   - the timer's return goes to a DEAD SLOT: @0x6986b2 `fstp [esp+58h+var_48]`
            //     (var_48 = frame +0x3a0) is the ONLY reference to var_48 -> 1 write / 0 reads;
            //     hence the Hex-Rays pseudocode `Terrain_PushRenderState(g_GfxRenderer);` with no
            //     assignment;
            //   - the `fmul ds:flt_7A8D74` @0x6991ca operates on var_3C (frame +0x3ac), a DISTINCT
            //     slot (12 bytes apart), loaded @0x6991c1 `fld [esp+50h+var_3C]`;
            //   - var_3C has 3 READS (@0x698900 / @0x6991c1 / @0x6994e4, all "v92 * 10.0") and
            //     ZERO WRITES (lvar_usage: read=3 / write=0 / addr=0); no stack address ever
            //     escapes (the only `lea ...esp` are `lea esp,[esp+0]` alignment NOPs), so no call
            //     can write it indirectly -> var_3C is read UNINITIALIZED. What IS proven here is
            //     only the CONSTANT: flt_7A8D74 @0x7A8D74 = bytes `00 00 20 41` LE = 0x41200000 =
            //     10.0f. The OPERAND itself has no writer: identifying `wavePhase_` as "elapsed
            //     seconds" is therefore an UNPROVEN build-safe choice. The CODE is unchanged (cf.
            //     `wavePhase_ * 10.0f` in bindWaterStates): only this comment was wrong. TODO
            //     [anchor 0x6991ca]: if a runtime dump ever gives the real value of var_3C,
            //     settle the actual quantity instead of assuming it is time-based.
            dev_->SetTexture(1, shadowTex_);
            dev_->SetTextureStageState(1, D3DTSS_COLOROP,  D3DTOP_MODULATE); // = 4
            dev_->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            dev_->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
            dev_->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);          // uv1
            lightmapBound = true;
        } else if (!wantLightmap && lightmapBound) {
            // DISABLE -- layer @0x6990ba (COLOROP=DISABLE) + @0x6990cb (SetTexture stage 1 = 0).
            dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            dev_->SetTexture(1, nullptr);
            lightmapBound = false;
        }

        // ALPHA-TEST: driven by RANK, not subOrder. Terrain_Render only enables ALPHATESTENABLE
        // (@0x6993d4) + ALPHAREF=128 (@0x6993e9) AFTER the cat2/cat4/cat1sub0/water-sub0 loops,
        // and only cuts it @0x699704: so only ranks 4 (cat1/sub1 @0x69941b) and 5 (water gate
        // sub1 @0x6995e7) are alpha-tested. The old `subOrder == 1` test also alpha-tested
        // cat2/sub1 and cat4/sub1 (122 materials / ~39,800 real faces), which the binary actually
        // draws via the cat2/cat4 loops BEFORE alpha-test is ever enabled. The a5=2 pass (ranks
        // 6-7) inherits an alpha-test cut off (@0x699704 at the end of pass a5=1) -> FALSE here
        // too, correctly.
        const bool alphaTest = (layer.rank == kRank_Cat1Sub1 || layer.rank == kRank_Water1);
        dev_->SetRenderState(D3DRS_ALPHATESTENABLE, alphaTest ? TRUE : FALSE);
        if (alphaTest) {
            dev_->SetRenderState(D3DRS_ALPHAREF, 128);                    // (24,128) @0x6993e9
            // ALPHAFUNC is not set by Terrain_Render: device permanent state =
            // (25=ALPHAFUNC, 5=D3DCMP_GREATEREQUAL) set by Gfx_InitDevice @0x69c517.
            dev_->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
        }

        // Sampler 0 UV ADDRESSING: CLAMP for cat2 (@0x698f7b/@0x698f8e) and for the water passes
        // (@0x699195/@0x6991a8, @0x6994b8/@0x6994cb, @0x6988cf/@0x6988e3); WRAP everywhere else
        // (@0x699011/@0x699024 before cat4, restored @0x6993af/@0x6993c2 and @0x6996cf/@0x6996e2
        // after each water pass; WRAP is also the device default, Gfx_InitDevice @0x69c49d/
        // @0x69c4ac). Absent until now -> incorrect ground texture tiling.
        const bool water   = (layer.category == kTerrainCatWater);
        const bool clampUv = (layer.category == 2) || water;
        const DWORD addr   = clampUv ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP; // 3 : 1
        dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, addr);
        dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, addr);

        if (water) bindWaterStates(layer.diffuse);
        else       dev_->SetTexture(0, layer.diffuse);

        // B5 — draw this frame's batch for this material: DrawPrimitiveUP(TRIANGLELIST,
        // matCounter_[m] triangles, &terrainBatch_[3*matBase_[m]], stride 40). Exact mirror of
        // Terrain_Render @0x698ff3 (device vtbl+332). matCounter_[m] == 0 -> material out of
        // frustum this frame (nothing to draw). PrimitiveCount = number of visible faces for
        // this material.
        const uint32_t m     = layer.materialIndex;
        const uint32_t count = (m < matCounter_.size()) ? matCounter_[m] : 0;
        if (count > 0 && m < matBase_.size()) {
            const size_t vbase = static_cast<size_t>(matBase_[m]) * 3;
            if (vbase < terrainBatch_.size()) {
                dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, count,
                                      &terrainBatch_[vbase], sizeof(FfTerrainVertex));
            }
        }
        if (water) unbindWaterStates();
    }

    // CLOSE the a5=2 sub-pass — layer @0x698b21 (27,0 = ALPHABLEND off) then @0x698b32
    // (14,1 = ZWRITE on). The real states are then restored to the caller below.
    if (blendPassOpen) {
        dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    }

    // Restore: do not pollute meshRenderer_.
    dev_->SetTexture(1, nullptr);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);       // FF default value
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, prevAddrU);
    dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, prevAddrV);
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, prevAlphaTest);
    dev_->SetRenderState(D3DRS_ALPHAREF, prevAlphaRef);
    dev_->SetRenderState(D3DRS_ALPHAFUNC, prevAlphaFunc);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, prevBlend);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, prevZWrite);
    dev_->SetRenderState(D3DRS_SRCBLEND, prevSrc);
    dev_->SetRenderState(D3DRS_DESTBLEND, prevDst);
    dev_->SetRenderState(D3DRS_LIGHTING, prevLighting);
    dev_->SetRenderState(D3DRS_CULLMODE, prevCull);
    meshRenderer_.InvalidateShaderBindingCache();          // the next DrawSkinnedSubset rebinds VS/PS
}

// Water bump-env pass for ONE cat==3 layer. IDA anchor: Terrain_Render @0x699206-0x6992b7.
// TWS-01: the perturbation map bound to stage 0 is the FALLOFF texture (falloffTex_, live writer
// MapColl_CreateFalloffTexture 0x694ca0 -> *(a1+20), read @0x69928f), in BUMPENVMAPLUMINANCE(23);
// water diffuse in stage 1 (MODULATE + ALPHAOP=SELECTARG1). Animated bump matrix: MAT00=cos(t)*s,
// MAT01=-sin(t)*s, MAT10=sin(t)*s, MAT11=cos(t)*s (t = wavePhase_*10), BUMPENVLSCALE=1.0.
void WorldGeometryRenderer::bindWaterStates(IDirect3DTexture9* waterDiffuse) {
    if (!falloffTex_) { dev_->SetTexture(0, waterDiffuse); return; } // fallback: plain water texture
    // BUMP SCALE — TODO anchor 0x699206 RESOLVED (2026-07-16), deliberately NOT applied: the
    // binary uses `a10` RAW as the bump-env matrix scale (checked @0x6991e5 `v94 = cos(v62) *
    // a10` / @0x6991f2 `sin(v109) * a10`, same at @0x699508 and @0x698925), where a10 =
    // Game_GetTierRange 0x5402f0 = the DRAW-DISTANCE TIER (1000/2000/3000 per
    // g_Opt_DisplayRangeTier 0x84DEC4). Passing a draw distance into a bump-env matrix is very
    // likely an ORIGINAL BUG (wrong variable passed): at 1000+, the perturbation saturates and
    // the water becomes noise. FIDELITY vs PLAYABILITY tradeoff: the exact value is DOCUMENTED
    // here but not reproduced -- renderTerrain does not receive the draw distance and
    // g_Opt_DisplayRangeTier is not wired up. kWaterBumpScale remains a build-safe rendering
    // choice. Escalate to the orchestrator if reproducing the bug is desired.
    constexpr float kWaterBumpScale = 0.05f;
    const float t = wavePhase_ * 10.0f;
    const float c = std::cos(t) * kWaterBumpScale;
    const float s = std::sin(t) * kWaterBumpScale;
    dev_->SetTexture(0, falloffTex_); // TWS-01: *(a1+20) @0x69928f = falloff, NOT a wave texture
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_BUMPENVMAPLUMINANCE); // = 23
    dev_->SetTextureStageState(0, D3DTSS_BUMPENVMAT00, F2DW(c));
    dev_->SetTextureStageState(0, D3DTSS_BUMPENVMAT01, F2DW(-s));
    dev_->SetTextureStageState(0, D3DTSS_BUMPENVMAT10, F2DW(s));
    dev_->SetTextureStageState(0, D3DTSS_BUMPENVMAT11, F2DW(c));
    dev_->SetTextureStageState(0, D3DTSS_BUMPENVLSCALE, F2DW(1.0f));           // @0x6992b7
    dev_->SetTexture(1, waterDiffuse);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP,  D3DTOP_MODULATE);
    dev_->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
    dev_->SetTextureStageState(1, D3DTSS_ALPHAOP,  D3DTOP_SELECTARG1);         // ALPHAOP(4)=SELECTARG1(2)
    dev_->SetTextureStageState(1, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 0);
}

// Restores stage-0 diffuse after a water layer and DISABLES stage 1.
void WorldGeometryRenderer::unbindWaterStates() {
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    // FRONT FX-F4 (M5): do NOT re-bind the lightmap here. Terrain_Render 0x698670 disables stage
    // 1 after a water pass (@0x699377 SetTextureStageState(1,COLOROP,DISABLE); @0x69939c
    // SetTexture(1,0)) and never puts the lightmap back on stage 1 (water always comes AFTER the
    // cat4 disable @0x6990ba). renderTerrain()'s state machine keeps lightmapBound=false after
    // water anyway (water never sets wantLightmap), so no spurious re-enable happens.
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev_->SetTexture(1, nullptr);
}

} // namespace ts2::gfx

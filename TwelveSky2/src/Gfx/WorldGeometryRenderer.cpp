// Gfx/WorldGeometryRenderer.cpp — voir WorldGeometryRenderer.h pour le bandeau complet
// (format de placement déduit ; TOUT A désormais rendu, parts A>1 en pose statique frame 0
// du flipbook de sway — cf. bandeau .h point 5, MISE À JOUR 2026-07-14).
#include "Gfx/WorldGeometryRenderer.h"
#include "Asset/WorldChunk.h"
#include "World/WorldIntegration.h"
#include "Core/Log.h"

#include <algorithm>
#include <cstring>
#include <limits>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

template <class T>
void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// Taille de l'en-tête fixe du bloc géométrie GXD d'un WorldMeshPart (Asset/WorldChunk.cpp,
// ReadMeshPart : A/B/C/D lus à l'offset 120, soit 136 o d'en-tête au total).
constexpr size_t kGeoHeaderSize = 136;
// Stride disque d'un vertex MOBJECT (Docs/TS2_ASSET_FORMATS.md : FVF 0x112 = XYZ|NORMAL|TEX1).
constexpr size_t kMobjVertexStride = 32;
// Stride disque d'un index (INDEX16, 3 index/face = 6 o/face).
constexpr size_t kIndexStride = 2;

// Convertit un vertex MOBJECT disque (32 o : pos12+normal12+uv8, PAS de poids/os) vers le
// GpuSkinVertex 76 o de MeshRenderer (cf. bandeau du .h, point 4) : poids (1,0,0,0), os 0
// (repli palette identité de meshRenderer_), tangent/binormal à zéro (jamais lus par le VS).
GpuSkinVertex ConvertMobjVertex(const uint8_t* src) {
    GpuSkinVertex v{};
    std::memcpy(v.position, src + 0, 12);
    v.blendWeight[0] = 1.0f;
    v.blendWeight[1] = v.blendWeight[2] = v.blendWeight[3] = 0.0f;
    v.blendIndices = 0; // 4 index empaquetés (D3DCOLOR) = os 0 pour les 4 influences
    v.tangent[0] = v.tangent[1] = v.tangent[2] = 0.0f;
    v.binormal[0] = v.binormal[1] = v.binormal[2] = 0.0f;
    std::memcpy(v.normal, src + 12, 12);
    std::memcpy(v.texcoord, src + 24, 8);
    return v;
}

// Gap G1 (terrain .WG) : convertit un TerrainVertex 40o (FVF 530 = XYZ|NORMAL|TEX2, ancre IDA
// Terrain_Render SetFVF(530) @0x698e6d) vers le GpuSkinVertex 76o de MeshRenderer, avec poids
// (1,0,0,0) / os 0 (repli palette identité) et tangent/binormal à zéro (jamais lus par kSkinnedVS).
// La position est en repère MONDE (world=identité au rendu terrain, cf. Terrain_Render : aucune
// SetTransform WORLD, seule la matrice de texture stage 0 est posée @0x698f25). uv0 (diffuse,
// stage 0) -> texcoord0. uv1 (lightmap/.SHADOW, stage 1) est IGNORÉ ici : le pipeline skinné
// réutilisé n'a qu'un seul jeu de coordonnées de texture -> voir TODO G8 dans renderTerrain().
GpuSkinVertex ConvertTerrainVertex(const asset::TerrainVertex& src) {
    GpuSkinVertex v{};
    v.position[0] = src.position[0]; v.position[1] = src.position[1]; v.position[2] = src.position[2];
    v.blendWeight[0] = 1.0f;
    v.blendWeight[1] = v.blendWeight[2] = v.blendWeight[3] = 0.0f;
    v.blendIndices = 0;
    v.tangent[0] = v.tangent[1] = v.tangent[2] = 0.0f;
    v.binormal[0] = v.binormal[1] = v.binormal[2] = 0.0f;
    v.normal[0] = src.normal[0]; v.normal[1] = src.normal[1]; v.normal[2] = src.normal[2];
    v.texcoord[0] = src.uv0[0];  v.texcoord[1] = src.uv0[1]; // uv0 = diffuse (stage 0)
    return v;
}

// Contrainte INDEX16 : au plus 65536 sommets adressables par lot. On borne les lots terrain à
// 65535 sommets = 21845 faces (3 sommets/face, 65535 % 3 == 0) pour rester dans DrawIndexedPrimitive.
constexpr uint32_t kTerrainMaxVertsPerBatch = 65535u; // 21845 faces * 3

} // namespace

// ===========================================================================
//  Init / Shutdown
// ===========================================================================

bool WorldGeometryRenderer::Init(Renderer& renderer) {
    dev_ = renderer.Device();
    if (!dev_) { TS2_ERR("WorldGeometryRenderer::Init : device nul"); return false; }
    if (!meshRenderer_.Init(renderer)) {
        TS2_ERR("WorldGeometryRenderer::Init : MeshRenderer::Init a echoue");
        return false;
    }
    if (!skyRenderer_.Init(renderer)) {
        TS2_ERR("WorldGeometryRenderer::Init : SkyRenderer::Init a echoue");
        return false;
    }
    ready_ = true;
    TS2_LOG("WorldGeometryRenderer pret.");
    return true;
}

void WorldGeometryRenderer::Shutdown() {
    releaseObjects();
    meshRenderer_.Shutdown();
    skyRenderer_.Shutdown();
    dev_ = nullptr;
    ready_ = false;
}

void WorldGeometryRenderer::releaseObjects() {
    for (StaticObject& obj : objects_) {
        for (SkinnedLod& lod : obj.mesh.lods) { SafeRelease(lod.vb); SafeRelease(lod.ib); }
        SafeRelease(obj.mesh.diffuse);
    }
    objects_.clear();
    modelRanges_.clear();
    instances_.clear();
    releaseTerrain(); // Gap G1 : libère aussi les lots/textures du sol .WG
}

// Gap G1 : libère les VB/IB des lots terrain puis les textures diffuses (possédées une seule
// fois, réfs non-possédantes dans TerrainBatch::diffuse).
void WorldGeometryRenderer::releaseTerrain() {
    for (TerrainBatch& b : terrainBatches_) { SafeRelease(b.lod.vb); SafeRelease(b.lod.ib); }
    terrainBatches_.clear();
    for (IDirect3DTexture9*& t : terrainTextures_) SafeRelease(t);
    terrainTextures_.clear();
    terrainFaceCount_ = 0;
}

// D3DPOOL_MANAGED : survit à un Reset() sans re-upload (même politique que MeshRenderer::Upload).
void WorldGeometryRenderer::OnDeviceLost() {}
void WorldGeometryRenderer::OnDeviceReset() {}

// ===========================================================================
//  Texture diffuse (tex1 déjà décodée en DDS par Asset/WorldChunk.cpp::ReadTextureBlock —
//  pas de ré-inflate zlib nécessaire ici, contrairement à MeshRenderer::createDiffuse qui
//  part de données SObject encore compressées).
// ===========================================================================

IDirect3DTexture9* WorldGeometryRenderer::createTextureFromBlock(IDirect3DDevice9* dev,
                                                                  const asset::TextureBlock& tex) {
    if (!tex.present || tex.data.empty()) return nullptr;
    IDirect3DTexture9* out = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev, tex.data.data(), static_cast<UINT>(tex.data.size()),
        D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN,
        D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &out);
    if (FAILED(hr)) {
        TS2_WARN("WorldGeometryRenderer: creation texture WO echouee (0x%08lX)", hr);
        return nullptr;
    }
    return out;
}

// ===========================================================================
//  Upload d'un WorldMeshPart — TOUT A (cf. bandeau du .h point 5, MISE À JOUR
//  2026-07-14 « multi-ancre ») : le champ `A` n'est PAS un compteur d'os de
//  skinning mais le nombre de frames d'un FLIPBOOK de positions précalculées
//  (balancement au vent probable), confirmé par désassemblage de
//  `MeshPart_Render` (0x6AED60, offset SetStreamSource = `32*frame*B`, frame =
//  `Crt_Dbl2Uint(temps)` tronqué et borné à `[0, A-1]`) et de `MeshPart_Load`
//  (0x6AD169, qui copie tel quel `32*A*B` octets — A blocs consécutifs de B
//  sommets 32 o — dans UN SEUL vertex buffer D3D, jamais rejoué via une
//  palette de matrices). L'index buffer (6*D o) est PARTAGÉ par toutes les
//  frames (même topologie, seules les positions/normales bougent). Le bloc
//  « os » (64*A o, this+284 côté client d'origine) est chargé en mémoire par
//  `MeshPart_Load` mais n'est lu par AUCUN consommateur identifié du chemin
//  de rendu (`Model_RenderParts`/`MeshPart_Render`) — probable métadonnée
//  d'animation (paramètres de sway) exploitée ailleurs, non nécessaire pour
//  une pose statique.
// ===========================================================================

bool WorldGeometryRenderer::uploadPart(const asset::WorldMeshPart& part, StaticObject& out) {
    if (!part.present || !part.geoSizeOk) return false;
    if (part.A == 0 || part.B == 0 || part.D == 0) return false; // aucune frame/sommet/face

    // Layout du bloc géométrie décodé (Asset/WorldChunk.cpp::ReadMeshPart) :
    //   [136 o en-tête][64*A o bones (ignorés, cf. bandeau ci-dessus)][32*A*B o VB
    //   = A frames consécutives de B sommets][6*D o IB, partagé par toutes les frames]
    const size_t boneBytes       = 64ull * part.A;
    const size_t vbOffset        = kGeoHeaderSize + boneBytes;      // début de la frame 0
    const size_t vbFrameBytes    = kMobjVertexStride * static_cast<size_t>(part.B); // 1 frame
    const size_t vbAllFramesBytes = vbFrameBytes * part.A;          // A frames sur le disque
    const size_t ibOffset        = vbOffset + vbAllFramesBytes;
    const size_t ibBytes         = 3ull * kIndexStride * part.D;
    if (part.geo.size() < ibOffset + ibBytes) {
        TS2_WARN("WorldGeometryRenderer: part incoherente (geo=%zu < attendu=%zu)",
                 part.geo.size(), ibOffset + ibBytes);
        return false;
    }
    // CONFIRMED ex-VeryOldClient: MOBJECTINFO::mFrame (A copies de B sommets = flipbook de sway).
    // TODO terrain WO (sway non rejoué) : seule la frame 0 est uploadée, pas le vrai balancement au
    // vent -> voir SPEC TS2_WORLD_ROSETTA.md §3 G09 ; ancre IDA : MeshPart_Render 0x6aed60
    // (SetStreamSource(0, vb, 32*frame*B, 32), frame = Crt_Dbl2Uint(animPhase) borné [0, A-1]).
    if (part.A > 1) ++multiAnchorStaticCount_; // rendu en pose figee (frame 0), pas de sway

    // VB : on ne charge QUE la frame 0 (pose statique — pas de vrai balancement au vent, cf.
    // bandeau ci-dessus), conversion MOBJECT(32o) -> GpuSkinVertex(76o) via ConvertMobjVertex.
    std::vector<GpuSkinVertex> verts(part.B);
    const uint8_t* vbSrc = part.geo.data() + vbOffset; // la frame 0 commence exactement ici
    for (uint32_t i = 0; i < part.B; ++i)
        verts[i] = ConvertMobjVertex(vbSrc + static_cast<size_t>(i) * kMobjVertexStride);

    SkinnedLod lod;
    lod.vertexCount = part.B;
    lod.faceCount   = part.D;

    const UINT vbTotal = static_cast<UINT>(verts.size() * sizeof(GpuSkinVertex));
    HRESULT hr = dev_->CreateVertexBuffer(vbTotal, 0, 0, D3DPOOL_MANAGED, &lod.vb, nullptr);
    if (FAILED(hr)) {
        TS2_ERR("WorldGeometryRenderer: CreateVertexBuffer echoue (0x%08lX)", hr);
        return false;
    }
    void* p = nullptr;
    if (SUCCEEDED(lod.vb->Lock(0, vbTotal, &p, 0))) {
        std::memcpy(p, verts.data(), vbTotal);
        lod.vb->Unlock();
    }

    const UINT ibTotal = static_cast<UINT>(ibBytes);
    hr = dev_->CreateIndexBuffer(ibTotal, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &lod.ib, nullptr);
    if (FAILED(hr)) {
        TS2_ERR("WorldGeometryRenderer: CreateIndexBuffer echoue (0x%08lX)", hr);
        SafeRelease(lod.vb);
        return false;
    }
    if (SUCCEEDED(lod.ib->Lock(0, ibTotal, &p, 0))) {
        std::memcpy(p, part.geo.data() + ibOffset, ibTotal);
        lod.ib->Unlock();
    }

    out.mesh.lods.clear();
    out.mesh.lods.push_back(lod);
    // tex1 seul (diffuse). TODO terrain WO (matériaux secondaires) : tex2/materials[] parsés mais
    // ignorés (forcé opaque) -> voir SPEC TS2_WORLD_ROSETTA.md §3 G09 ; ancre IDA :
    // MeshPart_Load 0x6ad160 (tex1@296 / tex2@348 / materials@404).
    out.mesh.diffuse   = createTextureFromBlock(dev_, part.tex1);
    out.mesh.blendMode = 0; // opaque par defaut (blendMode reel non expose par WorldMeshPart)
    out.mesh.empty     = false;
    return true;
}

// ===========================================================================
//  Build — uploade chaque gabarit models[*].parts[*] UNE SEULE FOIS (GPU), puis
//  copie les instances auxRecords[*] (LE placement) pour Render().
// ===========================================================================

bool WorldGeometryRenderer::Build(const world::WorldAssets& assets) {
    releaseObjects();
    skippedMultiAnchor_ = 0;
    multiAnchorStaticCount_ = 0;

    // Ciel / config SilverLining : rafraîchis AVANT tout `return` ci-dessous, pour que le
    // fond de ciel reflète la zone courante même si le chargement de la géométrie .WO échoue.
    skyRenderer_.ApplyConfig(assets.SilverLining());
    skyRenderer_.SetAtmosphere(assets.Atmosphere());

    // Gap G1 « le sol » : construit les lots GPU du terrain .WG AVANT les gardes .WO ci-dessous,
    // pour que le sol soit dessiné même quand la zone n'a pas de chunk .WO (ancre IDA :
    // Terrain_Render 0x698670, appelé par Scene_InGameRender 0x52d0b0 AVANT les objets .WO).
    buildTerrain(assets);

    const asset::WorldChunk* chunk = assets.Objects();
    if (!chunk) {
        TS2_WARN("WorldGeometryRenderer::Build : aucun chunk .WO charge (WorldAssets::Objects() nul).");
        return false;
    }
    const asset::ObjectChunk* wo = chunk->AsObjects();
    if (!wo) {
        TS2_WARN("WorldGeometryRenderer::Build : chunk charge mais pas de type WO.");
        return false;
    }
    if (wo->empty) {
        TS2_LOG("WorldGeometryRenderer::Build : .WO vide (0 modele) pour cette zone.");
        return true;
    }

    size_t uploaded = 0, totalParts = 0;
    modelRanges_.assign(wo->models.size(), ModelRange{});
    for (size_t modelIdx = 0; modelIdx < wo->models.size(); ++modelIdx) {
        const asset::Model& model = wo->models[modelIdx];
        ModelRange& range = modelRanges_[modelIdx];
        range.start = objects_.size();
        if (model.present) {
            for (const asset::WorldMeshPart& part : model.parts) {
                ++totalParts;
                StaticObject obj;
                if (uploadPart(part, obj)) {
                    objects_.push_back(std::move(obj));
                    ++uploaded;
                }
            }
        }
        range.count = objects_.size() - range.start;
    }

    // LE placement : copie typée de ObjectChunk::auxRecords (modelIndex + pos + rot),
    // cf. Docs/TS2_WO_PLACEMENT_FORMAT.md — Render() en dérive une matrice par instance.
    // CONFIRMED ex-VeryOldClient: MOBJECTINFO { mIndex; mCoord[3]; mAngle[3] } (28o disque). Ancre IDA :
    // Terrain_Render 0x698670 -> Model_RenderWithShadow_0 0x6a4110 @0x698bdd (consommateur du tableau).
    instances_ = wo->auxRecords;

    // ---- log de sanité : preuve que Render() va bien utiliser N positions distinctes ----
    size_t outOfRange = 0, emptyModelInstances = 0;
    float minP[3] = { (std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(),
                       (std::numeric_limits<float>::max)() };
    float maxP[3] = { (std::numeric_limits<float>::lowest)(), (std::numeric_limits<float>::lowest)(),
                       (std::numeric_limits<float>::lowest)() };
    for (const asset::AuxRecord& inst : instances_) {
        if (inst.modelIndex >= modelRanges_.size()) { ++outOfRange; continue; }
        if (modelRanges_[inst.modelIndex].count == 0) { ++emptyModelInstances; continue; }
        for (int k = 0; k < 3; ++k) {
            if (inst.pos[k] < minP[k]) minP[k] = inst.pos[k];
            if (inst.pos[k] > maxP[k]) maxP[k] = inst.pos[k];
        }
    }
    TS2_LOG("WorldGeometryRenderer::Build : %zu/%zu parts uploadees GPU (%zu ignorees pour "
            "cause reelle, dont %zu parts multi-ancre A>1 rendues en pose statique figee "
            "frame 0 -- format decode, cf. bandeau uploadPart(), pas de balancement au vent) ; "
            "%zu instances (auxRecords) sur %zu gabarits, %zu appels de dessin prevus (%zu "
            "modelIndex hors bornes, %zu instances de gabarit vide) ; bbox positions "
            "x[%.1f..%.1f] y[%.1f..%.1f] z[%.1f..%.1f].",
            uploaded, totalParts, skippedMultiAnchor_, multiAnchorStaticCount_,
            instances_.size(), wo->models.size(),
            PlannedDrawCallCount(), outOfRange, emptyModelInstances,
            minP[0], maxP[0], minP[1], maxP[1], minP[2], maxP[2]);
    return true;
}

// ===========================================================================
//  Gap G1 — construction des lots GPU du terrain .WG (« le sol »).
//
//  Ancre IDA : Terrain_Render 0x698670 (« render quadtree terrain tile/water/land layers
//  with reflections »), appelé 2×/frame depuis Scene_InGameRender 0x52d0b0 (@0x52d9be pass 1,
//  @0x52ead8 pass 2), AVANT les objets .WO. Source de données : WorldAssets::Faces() ->
//  asset::MapFaceChunk (mesh.tris = CollisionFace 156o, textures[] par matériau, décodés
//  byte-exact par le stage DECODE). Modèle de rendu de l'original :
//    - a1+88  = tableau de faces (156o) ; face.materialIndex@0 (qmemcpy 120o depuis face+4
//               @0x698e21, saut du materialIndex) ;
//    - a1+16  = tableau matériaux (stride 52) : +40 catégorie, +44 sous-flag, +48 texture ;
//    - a1+144 = offset de départ par matériau (== MapFaceChunk::materialIndices) ;
//    - a1+160 = compteur de sommets par matériau (rempli au cull) ;
//    - SetFVF(530) @0x698e6d, DrawPrimitiveUP(TRIANGLELIST, count, VB+120*start, stride 40).
//  Ici on GROUPE les faces par materialIndex et on pré-construit des VB/IB statiques (le sol
//  est statique), dessinés via meshRenderer_ (world=identité, palette identité) — réutilisation
//  de l'infra .WO. Simplifications assumées (TODO précis, cf. renderTerrain()) : pas de cull
//  quadtree/frustum par frame, pas de multi-passe par catégorie (eau G6 / lightmap G8), pas de
//  matrice de texture animée. Le sol s'affiche, texturé par matériau — c'est le livrable G1.
// ===========================================================================
bool WorldGeometryRenderer::buildTerrain(const world::WorldAssets& assets) {
    // releaseObjects() (appelé par Build avant nous) a déjà purgé terrainBatches_/terrainTextures_.
    if (!dev_) return false; // build-safe : device absent
    const asset::WorldChunk* chunk = assets.Faces();
    if (!chunk) return false;                       // pas de .WG chargé pour cette zone
    const asset::MapFaceChunk* wg = chunk->AsFace();
    if (!wg) return false;                          // chunk présent mais pas de type WG

    const asset::CollisionMesh& mesh = wg->mesh;
    const uint32_t numMat = wg->numMaterials;
    if (mesh.tris.empty() || numMat == 0) {
        TS2_LOG("WorldGeometryRenderer::buildTerrain : .WG sans face/materiau (numTri=%zu numMat=%u).",
                mesh.tris.size(), numMat);
        return true; // .WG vide : rien à dessiner, pas une erreur
    }

    // 1) Une texture diffuse par matériau (ordre .WG). textures[m].present==false => nullptr
    //    (matériau non texturé : l'original bind mat+48 possiblement nul, cf. 0x69930b). Le DDS
    //    est déjà décodé (ReadTextureBlock), on le crée directement (pas de ré-inflate).
    terrainTextures_.assign(numMat, nullptr);
    for (uint32_t m = 0; m < numMat && m < wg->textures.size(); ++m)
        terrainTextures_[m] = createTextureFromBlock(dev_, wg->textures[m]);

    // 2) Regroupe les sommets par materialIndex (face.materialIndex@0 -> a1+144/a1+160/a1+16).
    //    Chaque face fournit 3 TerrainVertex (v0/v1/v2), convertis en GpuSkinVertex 76o.
    std::vector<std::vector<GpuSkinVertex>> perMat(numMat);
    size_t outOfRange = 0;
    for (const asset::CollisionFace& f : mesh.tris) {
        const uint32_t m = f.materialIndex;
        if (m >= numMat) { ++outOfRange; continue; } // materialIndex hors bornes -> ignoré
        std::vector<GpuSkinVertex>& dst = perMat[m];
        dst.push_back(ConvertTerrainVertex(f.v0));
        dst.push_back(ConvertTerrainVertex(f.v1));
        dst.push_back(ConvertTerrainVertex(f.v2));
    }

    // 3) Pour chaque matériau, découpe en lots <=65535 sommets (INDEX16) et crée VB/IB statiques.
    //    IB = index séquentiels 0,1,2,... (TRIANGLELIST non partagé, comme DrawPrimitiveUP d'origine).
    size_t totalFaces = 0, failed = 0;
    for (uint32_t m = 0; m < numMat; ++m) {
        const std::vector<GpuSkinVertex>& verts = perMat[m];
        for (size_t base = 0; base < verts.size(); base += kTerrainMaxVertsPerBatch) {
            const UINT vcount = static_cast<UINT>(
                (std::min)(static_cast<size_t>(kTerrainMaxVertsPerBatch), verts.size() - base));
            if (vcount < 3) break; // pas un triangle complet

            TerrainBatch batch;
            batch.diffuse           = terrainTextures_[m]; // réf non-possédante
            batch.lod.vertexCount   = vcount;
            batch.lod.faceCount     = vcount / 3u;

            const UINT vbBytes = vcount * static_cast<UINT>(sizeof(GpuSkinVertex));
            HRESULT hr = dev_->CreateVertexBuffer(vbBytes, 0, 0, D3DPOOL_MANAGED, &batch.lod.vb, nullptr);
            if (FAILED(hr)) { TS2_ERR("buildTerrain: CreateVertexBuffer echoue (0x%08lX)", hr); ++failed; continue; }
            void* p = nullptr;
            if (SUCCEEDED(batch.lod.vb->Lock(0, vbBytes, &p, 0))) {
                std::memcpy(p, verts.data() + base, vbBytes);
                batch.lod.vb->Unlock();
            }

            const UINT ibBytes = vcount * static_cast<UINT>(kIndexStride); // 1 index u16/sommet
            hr = dev_->CreateIndexBuffer(ibBytes, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &batch.lod.ib, nullptr);
            if (FAILED(hr)) { TS2_ERR("buildTerrain: CreateIndexBuffer echoue (0x%08lX)", hr); SafeRelease(batch.lod.vb); ++failed; continue; }
            if (SUCCEEDED(batch.lod.ib->Lock(0, ibBytes, &p, 0))) {
                uint16_t* idx = static_cast<uint16_t*>(p);
                for (UINT i = 0; i < vcount; ++i) idx[i] = static_cast<uint16_t>(i);
                batch.lod.ib->Unlock();
            }

            totalFaces += batch.lod.faceCount;
            terrainBatches_.push_back(batch);
        }
    }
    terrainFaceCount_ = totalFaces;

    TS2_LOG("WorldGeometryRenderer::buildTerrain (G1) : %zu faces terrain sur %u materiaux -> "
            "%zu lots GPU (%zu faces materialIndex hors bornes ignorees, %zu lots en echec) ; "
            "sol .WG pret (world=identite, FVF-equiv XYZ|NORMAL|TEX0). Cull quadtree/frustum, eau "
            "(G6) et lightmap .SHADOW (G8) = TODO, cf. renderTerrain().",
            totalFaces, numMat, terrainBatches_.size(), outOfRange, failed);
    return true;
}

size_t WorldGeometryRenderer::PlannedDrawCallCount() const {
    size_t total = 0;
    for (const asset::AuxRecord& inst : instances_) {
        if (inst.modelIndex >= modelRanges_.size()) continue;
        total += modelRanges_[inst.modelIndex].count;
    }
    return total;
}

// Construit World = Rz(rot.z)*Ry(rot.y)*Rx(rot.x)*T(pos) — ordre D3DX exact confirmé par
// désassemblage (Model_RenderParts 0x6a379c-0x6a3892, Model_RenderWithShadow_0
// 0x6a41a3-0x6a4299), cf. bandeau .h point 4 / Docs/TS2_WO_PLACEMENT_FORMAT.md.
// CONFIRMED ex-VeryOldClient: MOBJECT dessiné à mCoord/mAngle (kDegToRad = pi/180, valeur binaire).
D3DXMATRIX WorldGeometryRenderer::BuildInstanceWorldMatrix(const asset::AuxRecord& inst) {
    constexpr float kDegToRad = 0.017453292f; // pi/180, valeur exacte relevée dans le binaire
    D3DXMATRIX t, rx, ry, rz, m;
    D3DXMatrixTranslation(&t, inst.pos[0], inst.pos[1], inst.pos[2]);
    D3DXMatrixRotationX(&rx, inst.rot[0] * kDegToRad);
    D3DXMatrixRotationY(&ry, inst.rot[1] * kDegToRad);
    D3DXMatrixRotationZ(&rz, inst.rot[2] * kDegToRad);
    D3DXMatrixMultiply(&m, &rz, &ry); // M = Rz * Ry
    D3DXMatrixMultiply(&m, &m, &rx);  // M = M * Rx
    D3DXMatrixMultiply(&m, &m, &t);   // M = M * T
    return m;
}

// ===========================================================================
// Ce fichier n'est que l'ORDONNANCEUR du ciel (SkyRenderer NON possédé, frontière CONFIRMED
// ex-VeryOldClient: SKY_FOR_GXD = classe séparée hors WORLD_FOR_GXD). PLAUSIBLE (VeryOldClient n/a)
// — non prouvé byte-exact : placement des 2 appels calqué sur les 2 points d'entrée réels
// (Env_RenderSkyCube 0x6a8f60 tôt via Gfx_BeginFrame 0x6a2280 ; Atmosphere_DrawFrame 0x794fe0 après
// terrain via Scene_InGameRender 0x52d0b0). TODO hors-FRONT 4 : skybox cube réel = X03, SilverLining
// complet (nuages/soleil/lune/étoiles/fog) = X04 (FRONT 6), cf. TS2_WORLD_ROSETTA.md §3.Z.
void WorldGeometryRenderer::RenderSky(int screenW, int screenH) {
    if (!ready_) return;
    skyRenderer_.Render(screenW, screenH);
}

// ===========================================================================
//  Gap G1 — dessin du sol .WG. Ancre IDA : Terrain_Render 0x698670. Appelée par Render() APRÈS
//  la caméra et AVANT les objets .WO (ordre fidèle : Scene_InGameRender dessine le terrain
//  @0x52d9be avant les props/entités). world=identité (sommets déjà en repère MONDE) ; chaque
//  lot est dessiné via meshRenderer_ (pipeline skinné réutilisé, palette identité 1 os).
//
//  CULLMODE=NONE encadré (sauvegarde/restaure la valeur courante du device) : garantit la
//  VISIBILITÉ du sol quel que soit l'ordre d'enroulement des faces terrain (l'original gère son
//  propre état de cull dans Terrain_PushRenderState 0x69cb80 / backface par face @0x698dd4 ;
//  le sens d'enroulement du .WG vs le CULLMODE du device partagé n'étant pas prouvé ici, on
//  neutralise le backface pour ne jamais faire disparaître le sol — le z-buffer masque la face
//  arrière). Le cull par distance/frustum reste un TODO perf (cf. plus bas).
//
//  TODO (précis, non implémentés — hors « le sol s'affiche ») :
//   - Cull quadtree + frustum par frame (perf) : MapColl_CollectLeafFaces 0x694b50 (descente +
//     Cam_FrustumTestAABB 0x69f230) puis, par face, backface `dot(camPos, planeN) >= planeD`
//     @0x698dd4 + Cam_FrustumTestSphere2x 0x69f0e0 (sphere face @+140/rayon @+152). Ici on
//     dessine TOUTES les faces (correct, non optimisé) — nécessiterait un VB/IB dynamique/frame.
//   - G6 eau (matériau catégorie 3) : bump-env D3DTOP_BUMPENVMAPLUMINANCE @0x699206 + wave
//     texture cWorldMesh_MakeWaterWaveTexture 0x451220 + falloff MapColl_CreateFalloffTexture
//     0x694ca0. Non faisable via le pipeline skinné (pas de bump-env) ET la CATÉGORIE de matériau
//     (+40 dans a1+16) n'est pas décodée par le stage DECODE -> TODO.
//   - G8 lightmap .SHADOW (stage 1, uv1) : SetTextureStageState(1,COLOROP,MODULATE2X) @0x698f54 +
//     SetTexture(1, shadowTex) @0x698f68, coordonnées uv1 (TerrainVertex.uv1). Le GpuSkinVertex
//     réutilisé n'a qu'un TEXCOORD0 et kSkinnedPS échantillonne une seule texture -> TODO (la
//     texture .SHADOW EST chargée dans WorldAssets::shadow_, prête à câbler à une 2e passe).
// ===========================================================================
void WorldGeometryRenderer::renderTerrain() {
    if (!ready_ || terrainBatches_.empty()) return;

    // Sauvegarde/neutralise le backface cull le temps du terrain (cf. bandeau ci-dessus).
    DWORD prevCull = D3DCULL_CCW;
    dev_->GetRenderState(D3DRS_CULLMODE, &prevCull);
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);

    D3DXMATRIX kIdentity;
    D3DXMatrixIdentity(&kIdentity);       // world = identité (Terrain_Render : pas de SetTransform WORLD)
    const BonePalette noPalette{};        // -> repli palette identité (1 os) de meshRenderer_

    for (const TerrainBatch& b : terrainBatches_) {
        if (!b.lod.vb || !b.lod.ib) continue;
        // SkinnedMesh transitoire (réfs non-possédantes : sa destruction ne libère rien).
        SkinnedMesh sm;
        sm.lods.push_back(b.lod);
        sm.diffuse   = b.diffuse;
        sm.blendMode = 0;                 // sol opaque (catégorie/blend non décodés -> opaque, cf. G6)
        sm.empty     = false;
        meshRenderer_.DrawSkinnedSubset(sm, 0, kIdentity, noPalette);
    }

    dev_->SetRenderState(D3DRS_CULLMODE, prevCull); // restaure l'état de cull du device partagé
}

//  Render — dessine d'abord le SOL .WG (Gap G1, renderTerrain()) puis une matrice monde PAR
//  INSTANCE (Rz*Ry*Rx*T) pour les props .WO, cf. bandeau .h point 2/4. Ordre fidèle à
//  Scene_InGameRender 0x52d0b0 : terrain (@0x52d9be) AVANT les objets/entités. Les couches
//  SilverLining sont appelées séparément par SceneManager.
//
//  Gap G1 IMPLÉMENTÉ (2026-07-16) : le terrain .WG (WorldAssets::Faces()) est désormais dessiné
//  par renderTerrain() (ancre IDA : Terrain_Render 0x698670, SetFVF 530 = XYZ|NORMAL|TEX2,
//  world=identité, sommets 40o groupés par materialIndex, texture diffuse par matériau). Les
//  objets .WO ne flottent plus sans sol. Restes documentés dans renderTerrain() (TODO précis) :
//    - Cull quadtree/frustum par frame (perf) : MapColl_CollectLeafFaces 0x694b50 + backface
//      @0x698dd4 + Cam_FrustumTestSphere2x 0x69f0e0 (ici : dessine tout, correct/non optimisé).
//    - G6 eau (catégorie 3, bump-env) : cWorldMesh_MakeWaterWaveTexture 0x451220 + falloff
//      0x694ca0 (catégorie de matériau non décodée + pipeline skinné sans bump-env -> TODO).
//    - G8 lightmap .SHADOW (stage 1, uv1) : Terrain_Render 0x698670 stage 1 (1 seul TEXCOORD
//      dans le pipeline réutilisé -> TODO ; texture déjà chargée dans WorldAssets::shadow_).
// ===========================================================================

void WorldGeometryRenderer::Render(const Camera& camera, int screenW, int screenH) {
    if (!ready_) return;
    // BUG CORRIGÉ (audit 2026-07-14) : SkyRenderer::Render() pose SetVertexShader(nullptr)/
    // SetPixelShader(nullptr) directement sur le device PARTAGÉ avec meshRenderer_. Or
    // MeshRenderer::DrawSkinnedSubset() évite les re-bind VS/PS redondants via un cache
    // purement local (currentPass_) qui ignore ces pokes externes : sans cette invalidation,
    // dès la 2e frame le cache croit encore le VS/PS skinné bindé (valeur laissée par la
    // frame précédente) et NE LES REBIND PLUS après le quad ciel -- tous les objets .WO se
    // dessineraient alors sans aucun shader (silencieusement, DrawIndexedPrimitive sans VS/PS
    // liés). Cf. Gfx/MeshRenderer.h::InvalidateShaderBindingCache().
    meshRenderer_.InvalidateShaderBindingCache();

    // Caméra : partagée par le sol (G1) et les props .WO. Posée AVANT renderTerrain() pour que
    // le terrain utilise la même vue/projection (ancre IDA : Gfx_InitDevice/GXD_BeginScene).
    D3DXMATRIX view, proj;
    camera.BuildViewMatrix(view);
    const float aspect = (screenH > 0)
        ? static_cast<float>(screenW) / static_cast<float>(screenH)
        : 1.0f;
    camera.BuildProjMatrix(proj, aspect);
    meshRenderer_.SetCamera(view, proj);

    // Gap G1 : le SOL .WG d'abord (fidèle à l'ordre de Scene_InGameRender : terrain avant props).
    // Dessiné même s'il n'y a aucun objet .WO dans la zone (return anticipé ci-dessous).
    renderTerrain();

    if (objects_.empty() || instances_.empty()) return; // pas de props .WO : le sol seul suffit

    // Boucle plate (chaque instance dessinée). CONFIRMED ex-VeryOldClient: RecursionForDraw
    // (descente + cull). TODO terrain WO (perf/visuel) : l'original cull par distance + alpha-fade
    // les .WO lointains -> voir SPEC TS2_WORLD_ROSETTA.md §3 G09 ; ancres IDA : Terrain_Render
    // 0x698670 (distance-cull/fade) + Model_RenderWithShadow_0 0x6a4110 (frustum/part). Écart perf,
    // PAS de placement erroné (ne pas implémenter ici — jalon compilé dédié).
    const BonePalette noPalette{}; // invalide -> repli palette identite (1 os) de meshRenderer_.
    for (const asset::AuxRecord& inst : instances_) {
        if (inst.modelIndex >= modelRanges_.size()) continue; // modelIndex corrompu, cf. log Build()
        const ModelRange& range = modelRanges_[inst.modelIndex];
        if (range.count == 0) continue; // gabarit sans part uploadee (absent ou A>1 partout)

        const D3DXMATRIX world = BuildInstanceWorldMatrix(inst);
        for (size_t i = 0; i < range.count; ++i) {
            const StaticObject& obj = objects_[range.start + i];
            if (obj.mesh.empty || obj.mesh.lods.empty()) continue;
            meshRenderer_.DrawSkinnedSubset(obj.mesh, 0, world, noPalette);
        }
    }
}

} // namespace ts2::gfx

// Gfx/WorldGeometryRenderer.cpp — voir WorldGeometryRenderer.h pour le bandeau complet
// (format de placement déduit ; TOUT A désormais rendu, parts A>1 en pose statique frame 0
// du flipbook de sway — cf. bandeau .h point 5, MISE À JOUR 2026-07-14).
#include "Gfx/WorldGeometryRenderer.h"
#include "Asset/WorldChunk.h"
#include "World/WorldIntegration.h"
#include "Core/Log.h"

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
    out.mesh.diffuse   = createTextureFromBlock(dev_, part.tex1); // tex2/materials[] ignorés (TODO)
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
void WorldGeometryRenderer::RenderSky(int screenW, int screenH) {
    if (!ready_) return;
    skyRenderer_.Render(screenW, screenH);
}

//  Render — une matrice monde PAR INSTANCE (Rz*Ry*Rx*T), cf. bandeau .h point 2/4 :
//  CORRIGÉ, remplace l'ancienne matrice identité globale. Les couches SilverLining sont
//  appelées séparément par SceneManager pour reproduire le placement réel des deux points
//  d'entrée frame.
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
    if (objects_.empty() || instances_.empty()) return;

    D3DXMATRIX view, proj;
    camera.BuildViewMatrix(view);
    const float aspect = (screenH > 0)
        ? static_cast<float>(screenW) / static_cast<float>(screenH)
        : 1.0f;
    camera.BuildProjMatrix(proj, aspect);
    meshRenderer_.SetCamera(view, proj);

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

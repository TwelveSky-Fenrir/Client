// Gfx/WorldGeometryRenderer.cpp — voir WorldGeometryRenderer.h pour le bandeau complet
// (format de placement déduit ; TOUT A désormais rendu, parts A>1 en pose statique frame 0
// du flipbook de sway — cf. bandeau .h point 5, MISE À JOUR 2026-07-14).
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

template <class T>
void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// FVF fixed-function du terrain : 0x212 = 530 = D3DFVF_XYZ|NORMAL|TEX2 (2 jeux d'UV, stride 40).
// Ancre IDA : Terrain_Render 0x698670 SetFVF(530) @0x698e6d.
constexpr DWORD kFvfTerrain = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX2; // 0x212
static_assert(kFvfTerrain == 0x212, "FVF terrain doit valoir 530 (0x212)");
// FVF fixed-function des billboards FX unlit : 0x142 = 322 = D3DFVF_XYZ|DIFFUSE|TEX1 (stride 24).
// Ancre IDA : Gfx_BeginUnlitPass 0x69e470 SetFVF(322).
constexpr DWORD kFvfBillboard = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1; // 0x142
static_assert(kFvfBillboard == 0x142, "FVF billboard doit valoir 322 (0x142)");

// Catégorie de matériau EAU (trailer[0]==3). Ancre IDA : MapColl_LoadMapFile @0x698033
// (cherche mat+40==3 -> déclenche wave/falloff). Rang de dessin pour reproduire l'ordre exact de
// Terrain_Render(a5=1) : cat2 -> cat4 -> cat1/sub0 -> eau cat3/sub0 -> cat1/sub1(alphatest) ->
// eau cat3/sub1 -> (autres opaques). Ancres : boucles @0x698f7b..@0x69914d..@0x6990d8.
int TerrainLayerRank(uint32_t category, uint32_t subOrder) {
    if (category == 2) return 0;
    if (category == 4) return 1;
    if (category == 1 && subOrder == 0) return 2;
    if (category == 3 && subOrder == 0) return 3; // eau
    if (category == 1 && subOrder == 1) return 4; // alpha-test
    if (category == 3 && subOrder == 1) return 5; // eau alpha-test
    return 6;                                     // reste : opaque, dessiné en dernier
}

// Empaquette un float dans le DWORD attendu par SetTextureStageState/SetRenderState (bit-copie).
inline DWORD F2DW(float f) { DWORD d; std::memcpy(&d, &f, 4); return d; }

// Génère la texture de vagues V8U8 NxN (bump map du/dv signé). Port STRUCTUREL de
// cWorldMesh_MakeWaterWaveTexture 0x451220 : D3DXCreateTexture(dim,dim,1,0,D3DFMT_V8U8=60,MANAGED),
// remplissage par somme de 3 sin/cos (amplitudes -64/16/-32, angles 360*r / (x+y)*180 / (x-y)*90).
// La quantification exacte du binaire (Crt_ftol tronqué + Math_CIsqrt) est APPROXIMÉE ici ; le port
// byte-exact reste un TODO ancre 0x451220 (impact purement visuel sur l'ondulation). `dim` est lu au
// runtime depuis cWorldMesh+0 dans l'original ; non disponible en statique -> 64 par défaut (choix de
// résolution, pas une valeur du protocole/format).
IDirect3DTexture9* MakeWaterWaveTexture(IDirect3DDevice9* dev, UINT dim) {
    if (!dev || dim == 0) return nullptr;
    IDirect3DTexture9* tex = nullptr;
    // D3DFMT_V8U8 = 60 (signed du/dv), 1 mip, usage 0, pool MANAGED (survit à Reset).
    if (FAILED(D3DXCreateTexture(dev, dim, dim, 1, 0, D3DFMT_V8U8, D3DPOOL_MANAGED, &tex)) || !tex)
        return nullptr;
    D3DLOCKED_RECT lr;
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) { tex->Release(); return nullptr; }
    auto* row = static_cast<uint8_t*>(lr.pBits);
    const float N = static_cast<float>(dim);
    for (UINT y = 0; y < dim; ++y) {
        int8_t* texel = reinterpret_cast<int8_t*>(row);
        for (UINT x = 0; x < dim; ++x) {
            const float dx = static_cast<float>(x) / N - 0.5f;
            const float dy = static_cast<float>(y) / N - 0.5f;
            const float r2 = dx * dx + dy * dy;
            const float aR  = 360.0f * r2;          // v25
            const float aD1 = (dy + dx) * 180.0f;   // v22
            const float aD2 = (dx - dy) * 90.0f;    // v24
            // du (3 termes sin), dv (3 termes cos) — ftol = troncature vers 0.
            const float du = std::trunc(std::sin(aR)  * -64.0f * -r2)
                           - std::trunc(std::sin(aD2) *  16.0f)
                           - std::trunc(std::sin(aD1) * -32.0f);
            const float dv = std::trunc(std::cos(aR)  * -64.0f * -r2)
                           - std::trunc(std::cos(aD2) *  16.0f)
                           - std::trunc(std::cos(aD1) * -32.0f);
            texel[0] = static_cast<int8_t>(std::max(-128.0f, std::min(127.0f, du)));
            texel[1] = static_cast<int8_t>(std::max(-128.0f, std::min(127.0f, dv)));
            texel += 2;
        }
        row += lr.Pitch;
    }
    tex->UnlockRect(0);
    return tex;
}

// Génère la texture de falloff radial V8U8 NxN. Port de MapColl_CreateFalloffTexture 0x694ca0 :
// valeur = round(-sqrt((x/N-0.5)^2+(y/N-0.5)^2) * 1.442695040888963407) écrite en du ET dv.
IDirect3DTexture9* MakeFalloffTexture(IDirect3DDevice9* dev, UINT dim) {
    if (!dev || dim == 0) return nullptr;
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(D3DXCreateTexture(dev, dim, dim, 1, 0, D3DFMT_V8U8, D3DPOOL_MANAGED, &tex)) || !tex)
        return nullptr;
    D3DLOCKED_RECT lr;
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) { tex->Release(); return nullptr; }
    auto* row = static_cast<uint8_t*>(lr.pBits);
    const float N = static_cast<float>(dim);
    constexpr float kInvLn2 = 1.442695040888963407f; // 1/ln(2), littéral exact du binaire @0x694ca0
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

// Résolution par défaut des textures procédurales d'eau (cf. note MakeWaterWaveTexture : le binaire
// lit la dimension au runtime depuis cWorldMesh+0 ; 64 est un choix de résolution build-safe).
constexpr UINT kWaterTexDim = 64;

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

// Contrainte INDEX16 : au plus 65536 sommets adressables par lot. On borne les lots terrain à
// 65535 sommets = 21845 faces (3 sommets/face, 65535 % 3 == 0) pour rester dans DrawIndexedPrimitive.
constexpr uint32_t kTerrainMaxVertsPerBatch = 65535u; // 21845 faces * 3

// FVF terrain -> FfTerrainVertex 40o : le vertex disque asset::TerrainVertex (pos/normal/uv0/uv1)
// est BIT-À-BIT identique -> aucune conversion, copie directe (memcpy dans buildTerrain).
static_assert(sizeof(asset::TerrainVertex) == 40, "TerrainVertex disque = 40o (memcpy direct FF)");

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
    instancePhase_.clear();
    releaseTerrain(); // libère aussi les couches/textures du sol .WG + eau + lightmap
    releaseFx();      // libère les billboards FX de zone .WP + leurs textures
}

// Libère les VB/IB des couches terrain, les textures diffuses (possédées), les textures d'eau
// procédurales et la lightmap.
void WorldGeometryRenderer::releaseTerrain() {
    for (TerrainLayer& l : terrainLayers_)
        for (FfLod& lod : l.lods) { SafeRelease(lod.vb); SafeRelease(lod.ib); }
    terrainLayers_.clear();
    for (IDirect3DTexture9*& t : terrainTextures_) SafeRelease(t);
    terrainTextures_.clear();
    SafeRelease(waveTex_);
    SafeRelease(falloffTex_);
    SafeRelease(shadowTex_);
    wavePhase_ = 0.0f;
    terrainFaceCount_ = 0;
}

// Libère les textures GPU des nœuds FX de zone (.WP) et vide la liste de billboards.
void WorldGeometryRenderer::releaseFx() {
    for (IDirect3DTexture9*& t : fxTextures_) SafeRelease(t);
    fxTextures_.clear();
    fxBillboards_.clear();
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

// Crée une texture depuis un fichier DDS complet en mémoire (lightmap .SHADOW brute, exposée par
// WorldAssets::ShadowBytes()). Ancre IDA : Tex_LoadFromFile 0x6a9910 (DDS DXT1/3/5).
IDirect3DTexture9* WorldGeometryRenderer::createTextureFromDds(IDirect3DDevice9* dev,
                                                               const std::vector<uint8_t>& dds) {
    if (!dev || dds.empty()) return nullptr;
    IDirect3DTexture9* out = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev, dds.data(), static_cast<UINT>(dds.size()),
        D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN,
        D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &out);
    if (FAILED(hr)) {
        TS2_WARN("WorldGeometryRenderer: creation lightmap .SHADOW echouee (0x%08lX)", hr);
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

    // FRONT W3-F3 « le sol » : construit les couches FF du terrain .WG (+ eau + lightmap) AVANT les
    // gardes .WO ci-dessous, pour que le sol soit dessiné même sans chunk .WO (ancre IDA :
    // Terrain_Render 0x698670, appelé par Scene_InGameRender 0x52d0b0 AVANT les objets .WO).
    buildTerrain(assets);
    // FX de zone .WP : billboards placés (indépendants des .WO — construits même sans .WO).
    buildFx(assets);

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

    // FRONT W3-F3 : état de sway par instance (phase 0, borne de wrap = nb de frames A du gabarit).
    // Ancre IDA : MapColl_UpdateObjectAnim 0x694a00 (frameCount = part.A). Tické par TickWorldAnim.
    instancePhase_.assign(instances_.size(), 0.0f);
    instanceFrameCount_.assign(instances_.size(), 1u);
    for (size_t i = 0; i < instances_.size(); ++i) {
        const uint32_t mi = instances_[i].modelIndex;
        if (mi < wo->models.size() && wo->models[mi].present && !wo->models[mi].parts.empty()) {
            const uint32_t A = wo->models[mi].parts[0].A;
            instanceFrameCount_[i] = (A > 0) ? A : 1u;
        }
    }

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
//  FRONT W3-F3 — construction des couches FIXED-FUNCTION du terrain .WG (« le sol »).
//
//  Ancre IDA : Terrain_Render 0x698670 (« render quadtree terrain tile/water/land layers with
//  reflections »), appelé 2×/frame depuis Scene_InGameRender 0x52d0b0, AVANT les objets .WO.
//  Source : WorldAssets::Faces() -> asset::MapFaceChunk (mesh.tris = CollisionFace 156o,
//  textures[] par matériau). Modèle de rendu de l'original :
//    - a1+88  = faces (156o) ; face.materialIndex@0 ; 120o=3*40 sommets copiés (qmemcpy @0x698e21) ;
//    - a1+16  = matériaux (stride 52) : +40 CATÉGORIE (trailer[0]), +44 subOrder (trailer[1]),
//               +48 texture (rempli par Tex_LoadCompressedFromHandle 0x6a9cf0) ;
//    - SetFVF(530) @0x698e6d, DrawPrimitiveUP(TRIANGLELIST, count, VB+120*start, stride 40).
//  Ici : GROUPE les faces par matériau -> une TerrainLayer(diffuse, category, subOrder) par matériau,
//  TRIÉES par rang (catégorie, subOrder) pour reproduire l'ordre de dessin de Terrain_Render(a5=1).
//  Sommets FfTerrainVertex 40o (= asset::TerrainVertex bit-à-bit, uv0=diffuse stage0, uv1=lightmap
//  stage1) uploadés par memcpy. Crée aussi les textures d'eau procédurales (si une couche cat==3
//  existe, fidèle @0x698043) et la lightmap .SHADOW. Le cull quadtree/frustum par frame reste un
//  TODO perf (on dessine tout — correct, non optimisé).
// ===========================================================================
bool WorldGeometryRenderer::buildTerrain(const world::WorldAssets& assets) {
    // releaseObjects() (appelé par Build avant nous) a déjà purgé terrainLayers_/terrainTextures_.
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

    // 1) Une texture diffuse par matériau (ordre .WG). textures[m].present==false => nullptr.
    terrainTextures_.assign(numMat, nullptr);
    for (uint32_t m = 0; m < numMat && m < wg->textures.size(); ++m)
        terrainTextures_[m] = createTextureFromBlock(dev_, wg->textures[m]);

    // 2) Regroupe les sommets FF 40o par materialIndex (face.materialIndex@0). Chaque face fournit
    //    3 TerrainVertex (v0/v1/v2) copiés tels quels (memcpy — layout identique à FfTerrainVertex).
    std::vector<std::vector<FfTerrainVertex>> perMat(numMat);
    size_t outOfRange = 0;
    for (const asset::CollisionFace& f : mesh.tris) {
        const uint32_t m = f.materialIndex;
        if (m >= numMat) { ++outOfRange; continue; } // materialIndex hors bornes -> ignoré
        std::vector<FfTerrainVertex>& dst = perMat[m];
        FfTerrainVertex v;
        std::memcpy(&v, &f.v0, sizeof(FfTerrainVertex)); dst.push_back(v);
        std::memcpy(&v, &f.v1, sizeof(FfTerrainVertex)); dst.push_back(v);
        std::memcpy(&v, &f.v2, sizeof(FfTerrainVertex)); dst.push_back(v);
    }

    // 3) Une TerrainLayer par matériau (catégorie/subOrder = trailer[0]/trailer[1] de la texture),
    //    découpée en FfLod <=65535 sommets (INDEX16, index séquentiels comme DrawPrimitiveUP).
    size_t totalFaces = 0, failed = 0;
    bool hasWater = false;
    for (uint32_t m = 0; m < numMat; ++m) {
        const std::vector<FfTerrainVertex>& verts = perMat[m];
        if (verts.size() < 3) continue;

        TerrainLayer layer;
        layer.diffuse  = terrainTextures_[m]; // réf non-possédante
        // trailer[0]=catégorie, trailer[1]=subOrder (prouvé Tex_LoadCompressedFromHandle 0x6a9cf0).
        layer.category = (m < wg->textures.size()) ? wg->textures[m].trailer[0] : 0;
        layer.subOrder = (m < wg->textures.size()) ? wg->textures[m].trailer[1] : 0;
        if (layer.category == 3) hasWater = true;

        for (size_t base = 0; base < verts.size(); base += kTerrainMaxVertsPerBatch) {
            const UINT vcount = static_cast<UINT>(
                (std::min)(static_cast<size_t>(kTerrainMaxVertsPerBatch), verts.size() - base));
            if (vcount < 3) break; // pas un triangle complet

            FfLod lod;
            lod.vertexCount = vcount;
            lod.faceCount   = vcount / 3u;

            const UINT vbBytes = vcount * static_cast<UINT>(sizeof(FfTerrainVertex));
            HRESULT hr = dev_->CreateVertexBuffer(vbBytes, 0, kFvfTerrain, D3DPOOL_MANAGED, &lod.vb, nullptr);
            if (FAILED(hr)) { TS2_ERR("buildTerrain: CreateVertexBuffer echoue (0x%08lX)", hr); ++failed; continue; }
            void* p = nullptr;
            if (SUCCEEDED(lod.vb->Lock(0, vbBytes, &p, 0))) {
                std::memcpy(p, verts.data() + base, vbBytes);
                lod.vb->Unlock();
            }

            const UINT ibBytes = vcount * static_cast<UINT>(kIndexStride); // 1 index u16/sommet
            hr = dev_->CreateIndexBuffer(ibBytes, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &lod.ib, nullptr);
            if (FAILED(hr)) { TS2_ERR("buildTerrain: CreateIndexBuffer echoue (0x%08lX)", hr); SafeRelease(lod.vb); ++failed; continue; }
            if (SUCCEEDED(lod.ib->Lock(0, ibBytes, &p, 0))) {
                uint16_t* idx = static_cast<uint16_t*>(p);
                for (UINT i = 0; i < vcount; ++i) idx[i] = static_cast<uint16_t>(i);
                lod.ib->Unlock();
            }

            totalFaces += lod.faceCount;
            layer.lods.push_back(lod);
        }
        if (!layer.lods.empty()) terrainLayers_.push_back(std::move(layer));
    }
    terrainFaceCount_ = totalFaces;

    // 4) Tri des couches par rang (catégorie, subOrder) — reproduit l'ordre de Terrain_Render(a5=1).
    std::stable_sort(terrainLayers_.begin(), terrainLayers_.end(),
                     [](const TerrainLayer& a, const TerrainLayer& b) {
                         return TerrainLayerRank(a.category, a.subOrder) <
                                TerrainLayerRank(b.category, b.subOrder);
                     });

    // 5) Eau : wave + falloff procédurales créées UNE FOIS si une couche cat==3 existe (fidèle
    //    @0x698043). Ancres : cWorldMesh_MakeWaterWaveTexture 0x451220 / MapColl_CreateFalloffTexture
    //    0x694ca0. falloffTex_ chargée mais non encore utilisée dans la passe (radial, réservée).
    if (hasWater) {
        waveTex_    = MakeWaterWaveTexture(dev_, kWaterTexDim);
        falloffTex_ = MakeFalloffTexture(dev_, kWaterTexDim);
    }

    // 6) Lightmap .SHADOW (stage 1, uv1) — texture GPU depuis les octets DDS bruts exposés par
    //    WorldAssets (le vertex FF possède bien uv1 : le TODO G8 « 1 seul TEXCOORD » disparaît).
    shadowTex_ = createTextureFromDds(dev_, assets.ShadowBytes());

    TS2_LOG("WorldGeometryRenderer::buildTerrain (W3-F3) : %zu faces sur %u materiaux -> %zu couches "
            "FF (%zu hors bornes, %zu lots echec) ; eau=%d (wave=%p falloff=%p) lightmap=%p ; sol .WG "
            "pret (FVF 530, world=identite). Cull quadtree/frustum = TODO perf.",
            totalFaces, numMat, terrainLayers_.size(), outOfRange, failed,
            hasWater ? 1 : 0, (void*)waveTex_, (void*)falloffTex_, (void*)shadowTex_);
    return true;
}

// ===========================================================================
//  FRONT W3-F3 — construction des billboards FX de zone (.WP). Ancre IDA : MapColl_LoadObjectsB
//  0x6983b0 (fxbRecords : nodeIndex+pos+rot) + Fx_NodeLoadFromHandle 0x6a69f0 (texture du nœud).
//  Sous-ensemble build-safe : 1 billboard placé par instance, à sa position, texture du nœud FX
//  résolue via AuxFxRecord::nodeIndex -> FxChunk::nodes[]. Le sim complet de particules
//  (Particle_Init 0x6a7020 / Particle_UpdateEmit 0x6a7530) reste un TODO ancre.
// ===========================================================================
bool WorldGeometryRenderer::buildFx(const world::WorldAssets& assets) {
    if (!dev_) return false;
    const asset::WorldChunk* chunk = assets.FxNodes();
    if (!chunk) return false;
    const asset::FxChunk* wp = chunk->AsFx();
    if (!wp || wp->empty || wp->nodes.empty()) return true; // pas de FX : rien à dessiner

    // Texture GPU par nœud FX (nullptr si absente) — POSSÉDÉES par fxTextures_.
    fxTextures_.assign(wp->nodes.size(), nullptr);
    for (size_t i = 0; i < wp->nodes.size(); ++i)
        fxTextures_[i] = createTextureFromBlock(dev_, wp->nodes[i].tex);

    // Un billboard par instance placée (fxbRecords), texture = celle de son nœud.
    size_t outOfRange = 0;
    for (const asset::AuxFxRecord& rec : wp->fxbRecords) {
        if (rec.nodeIndex >= fxTextures_.size()) { ++outOfRange; continue; }
        FxBillboard bb;
        bb.pos[0] = rec.pos[0]; bb.pos[1] = rec.pos[1]; bb.pos[2] = rec.pos[2];
        bb.texture = fxTextures_[rec.nodeIndex]; // réf non-possédante
        fxBillboards_.push_back(bb);
    }
    TS2_LOG("WorldGeometryRenderer::buildFx (W3-F3) : %zu noeuds FX, %zu billboards places (%zu "
            "nodeIndex hors bornes). Sim particules complet = TODO ancre (Particle_UpdateEmit 0x6a7530).",
            wp->nodes.size(), fxBillboards_.size(), outOfRange);
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
//  FRONT W3-F3 — dessin du sol .WG en FIXED-FUNCTION (FVF 530). Ancre IDA : Terrain_Render 0x698670,
//  ordre de dessin de la passe a5=1. Appelée par Render() APRÈS la caméra et AVANT les .WO (ordre
//  fidèle : Scene_InGameRender dessine le terrain @0x52d9be avant les props). world=identité.
//
//  Séquence reproduite : SetFVF(530) @0x698e6d ; matrice texture stage0 = identité @0x698f25 ;
//  CULLMODE=NONE @0x698f37 (backface CPU dans l'original, neutralisé ici) ; lightmap stage 1
//  MODULATE (=4, PAS MODULATE2X) @0x698f54 + SetTexture(1) @0x698f68 (uv1 — le vertex FF a 2 jeux
//  d'UV, le TODO G8 disparaît) ; couches triées par (catégorie, subOrder) ; eau cat==3 en bump-env
//  (bindWaterStates) ; alpha-test sur subOrder==1 (ALPHAREF=128 @0x6993d4). États sauvés/restaurés
//  pour ne pas polluer meshRenderer_ (qui rebinde ses shaders ensuite).
//
//  TODO perf (non implémenté) : cull quadtree + frustum par frame — MapColl_CollectLeafFaces
//  0x694b50 + backface @0x698dd4 + Cam_FrustumTestSphere2x 0x69f0e0. Ici on dessine TOUTES les
//  couches (correct, non optimisé).
// ===========================================================================
void WorldGeometryRenderer::renderTerrain(const D3DXMATRIX& view, const D3DXMATRIX& proj) {
    if (!ready_ || terrainLayers_.empty()) return;

    // Sauvegarde des états qu'on modifie (device partagé avec meshRenderer_).
    DWORD prevCull = D3DCULL_CCW, prevLighting = TRUE, prevAlphaTest = FALSE,
          prevAlphaRef = 0, prevAlphaFunc = D3DCMP_ALWAYS;
    dev_->GetRenderState(D3DRS_CULLMODE, &prevCull);
    dev_->GetRenderState(D3DRS_LIGHTING, &prevLighting);
    dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &prevAlphaTest);
    dev_->GetRenderState(D3DRS_ALPHAREF, &prevAlphaRef);
    dev_->GetRenderState(D3DRS_ALPHAFUNC, &prevAlphaFunc);

    // Fixed-function : pas de VS/PS, FVF terrain, transforms world=identité + view/proj caméra.
    dev_->SetVertexShader(nullptr);
    dev_->SetPixelShader(nullptr);
    dev_->SetFVF(kFvfTerrain);
    D3DXMATRIX ident; D3DXMatrixIdentity(&ident);
    dev_->SetTransform(D3DTS_WORLD, &ident);              // Terrain_Render : pas de SetTransform WORLD
    dev_->SetTransform(D3DTS_VIEW, &view);
    dev_->SetTransform(D3DTS_PROJECTION, &proj);
    dev_->SetTransform(D3DTS_TEXTURE0, &ident);           // matrice texture stage 0 identité @0x698f25

    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);          // FF sans éclairage (texture pure)
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);   // backface CPU dans l'original @0x698f37
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

    // Stage 0 : diffuse = texture (SELECTARG1), sur uv0.
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,  D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);

    // FRONT FX-F4 (M5) : la lightmap .SHADOW (stage 1 MODULATE) n'est PAS activee globalement.
    // Terrain_Render 0x698670 ne la lie QUE pour cat==2 (enable @0x698f54, boucle @0x698f97) et
    // cat==4 (boucle @0x69902d), puis la DESACTIVE (@0x6990ba) AVANT cat1/eau/alpha-test. On
    // reproduit cette porte PAR COUCHE ci-dessous (les couches sont triees par rang : cat2/cat4 en
    // premier). Stage 1 desactive au depart.
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    bool lightmapBound = false;

    // Dessin des couches, déjà triées par rang (catégorie, subOrder).
    for (const TerrainLayer& layer : terrainLayers_) {
        // FRONT FX-F4 (M5) : lightmap stage 1 UNIQUEMENT pour cat==2 et cat==4 (Terrain_Render
        // 0x698670 : enable @0x698f54 avant la boucle cat2 @0x698f97, disable @0x6990ba avant
        // cat1 @0x6990d4). Comme les couches sont triees par rang (cat2=0, cat4=1, puis cat1/eau/
        // alpha-test), cette bascule active la lightmap sur les premieres couches puis la coupe des
        // la 1ere couche non-cat2/cat4 -> equivalent fonctionnel exact des deux points IDA.
        const bool wantLightmap = (shadowTex_ != nullptr) &&
                                  (layer.category == 2 || layer.category == 4);
        if (wantLightmap && !lightmapBound) {
            // ENABLE -- calque @0x698f54 (COLOROP=MODULATE) + @0x698f68 (SetTexture stage 1 =
            // *(a1+72) lightmap). COLORARG1/2 + TEXCOORDINDEX=1 explicites (l'original herite ces
            // valeurs de Terrain_PushRenderState 0x69cb80 ; TEXCOORDINDEX=1 requis pour uv1).
            dev_->SetTexture(1, shadowTex_);
            dev_->SetTextureStageState(1, D3DTSS_COLOROP,  D3DTOP_MODULATE); // = 4
            dev_->SetTextureStageState(1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
            dev_->SetTextureStageState(1, D3DTSS_COLORARG2, D3DTA_CURRENT);
            dev_->SetTextureStageState(1, D3DTSS_TEXCOORDINDEX, 1);          // uv1
            lightmapBound = true;
        } else if (!wantLightmap && lightmapBound) {
            // DISABLE -- calque @0x6990ba (COLOROP=DISABLE) + @0x6990cb (SetTexture stage 1 = 0).
            dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
            dev_->SetTexture(1, nullptr);
            lightmapBound = false;
        }

        const bool alphaTest = (layer.subOrder == 1);
        dev_->SetRenderState(D3DRS_ALPHATESTENABLE, alphaTest ? TRUE : FALSE);
        if (alphaTest) {
            dev_->SetRenderState(D3DRS_ALPHAREF, 128);                    // @0x6993d4
            dev_->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
        }
        const bool water = (layer.category == 3);
        if (water) bindWaterStates(layer.diffuse);
        else       dev_->SetTexture(0, layer.diffuse);

        for (const FfLod& lod : layer.lods) {
            if (!lod.vb || !lod.ib) continue;
            dev_->SetStreamSource(0, lod.vb, 0, sizeof(FfTerrainVertex));
            dev_->SetIndices(lod.ib);
            dev_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, lod.vertexCount, 0, lod.faceCount);
        }
        if (water) unbindWaterStates();
    }

    // Restauration : ne pas polluer meshRenderer_.
    dev_->SetTexture(1, nullptr);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);       // valeur FF par défaut
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, prevAlphaTest);
    dev_->SetRenderState(D3DRS_ALPHAREF, prevAlphaRef);
    dev_->SetRenderState(D3DRS_ALPHAFUNC, prevAlphaFunc);
    dev_->SetRenderState(D3DRS_LIGHTING, prevLighting);
    dev_->SetRenderState(D3DRS_CULLMODE, prevCull);
    meshRenderer_.InvalidateShaderBindingCache();          // le prochain DrawSkinnedSubset rebinde VS/PS
}

// Passe eau bump-env d'une couche cat==3. Ancre IDA : Terrain_Render @0x699206-0x6992b7. waveTex_
// (V8U8) en stage 0 comme carte de perturbation BUMPENVMAPLUMINANCE(23) ; eau diffuse en stage 1
// (MODULATE + ALPHAOP=SELECTARG1). Matrice bump animée : MAT00=cos(t)*s, MAT01=-sin(t)*s,
// MAT10=sin(t)*s, MAT11=cos(t)*s (t = wavePhase_*10), BUMPENVLSCALE=1.0.
void WorldGeometryRenderer::bindWaterStates(IDirect3DTexture9* waterDiffuse) {
    if (!waveTex_) { dev_->SetTexture(0, waterDiffuse); return; } // repli : eau texture simple
    // s = échelle : l'original utilise a10 (farDist runtime) — non disponible statiquement ->
    // kWaterBumpScale (petit, build-safe). TODO ancre 0x699206 pour l'échelle exacte.
    constexpr float kWaterBumpScale = 0.05f;
    const float t = wavePhase_ * 10.0f;
    const float c = std::cos(t) * kWaterBumpScale;
    const float s = std::sin(t) * kWaterBumpScale;
    dev_->SetTexture(0, waveTex_);
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

// Restaure le stage 0 diffuse après une couche eau et DESACTIVE le stage 1.
void WorldGeometryRenderer::unbindWaterStates() {
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    // FRONT FX-F4 (M5) : NE PAS reinstaller la lightmap ici. Terrain_Render 0x698670 desactive le
    // stage 1 apres une passe eau (@0x699377 SetTextureStageState(1,COLOROP,DISABLE) ; @0x69939c
    // SetTexture(1,0)) et ne remet JAMAIS la lightmap sur le stage 1 (l'eau vient toujours APRES la
    // desactivation cat4 @0x6990ba). La machine a etats de renderTerrain() garde d'ailleurs
    // lightmapBound=false apres l'eau (l'eau n'a jamais wantLightmap), donc aucune reactivation
    // parasite.
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev_->SetTexture(1, nullptr);
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
//  G6 eau (catégorie 3, bump-env) et G8 lightmap .SHADOW (stage 1, uv1) sont désormais FAITS dans
//  ce front (cf. bindWaterStates() @0x699206 + MakeWaterWaveTexture 0x451220 / falloff 0x694ca0 ;
//  lightmap stage 1 MODULATE @0x698f54/@0x698f68) : la catégorie vient de textures[m].trailer[0]
//  et le vertex FF possède bien uv1. Restes = échelle exacte du bump eau (a10 runtime, TODO ancre
//  0x699206) et quantification exacte de la wave (TODO ancre 0x451220), purement visuels.
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

    // FRONT W3-F3 : le SOL .WG d'abord (fidèle à Scene_InGameRender : terrain avant props). FF, avec
    // les MÊMES view/proj que les props. Dessiné même sans objet .WO dans la zone.
    renderTerrain(view, proj);

    if (objects_.empty() || instances_.empty()) {
        RenderFxBillboards(camera); // FX de zone même sans .WO (torches/feux/cascades)
        return;                     // pas de props .WO : le sol + FX suffisent
    }

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

    // FRONT W3-F3 : FX de zone .WP APRÈS les props (passe a5=2 @0x698c6d : blend actif, depth-write
    // off). C'est le point d'entrée de rendu .WP réel (corrige WorldIntegration « non identifié »).
    RenderFxBillboards(camera);
}

// ===========================================================================
//  FRONT W3-F3 — passe FX de zone (.WP) : billboards unlit. Ancre IDA : Terrain_Render a5=2
//  @0x698c6d -> Gfx_BeginUnlitPass 0x69e470 (LIGHTING off, ALPHABLEND on, stage0 ALPHAOP=MODULATE,
//  FVF 322=XYZ|DIFFUSE|TEX1, matrice texture0 identité) -> Particle_RenderBillboards 0x6a70b0
//  (quads camera-facing 24o/sommet, DrawPrimitiveUP(TRIANGLELIST, 2*n, ..., 24)).
//  SOUS-ENSEMBLE build-safe : 1 quad camera-facing par instance placée (base right/up dérivée de la
//  matrice vue). Le sim complet (base flt_8001D4..E8 runtime + Particle_Init/UpdateEmit) = TODO ancre.
// ===========================================================================
void WorldGeometryRenderer::RenderFxBillboards(const Camera& camera) {
    if (!ready_ || fxBillboards_.empty()) return;

    DWORD prevLighting = TRUE, prevBlend = FALSE, prevZWrite = TRUE, prevCull = D3DCULL_CCW,
          prevSrc = D3DBLEND_ONE, prevDst = D3DBLEND_ZERO;
    dev_->GetRenderState(D3DRS_LIGHTING, &prevLighting);
    dev_->GetRenderState(D3DRS_ALPHABLENDENABLE, &prevBlend);
    dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prevZWrite);
    dev_->GetRenderState(D3DRS_CULLMODE, &prevCull);
    dev_->GetRenderState(D3DRS_SRCBLEND, &prevSrc);
    dev_->GetRenderState(D3DRS_DESTBLEND, &prevDst);

    // Gfx_BeginUnlitPass 0x69e470 : (137,0) [SPECULARENABLE], (14,0)=LIGHTING off, (27,1)=ALPHABLEND on.
    dev_->SetVertexShader(nullptr);
    dev_->SetPixelShader(nullptr);
    dev_->SetFVF(kFvfBillboard);                                   // 322 @0x69e4-SetFVF
    dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(137), 0); // état 137 (rôle exact non prouvé, fidèle IDA)
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);                   // (14,0)
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);           // (27,1)
    dev_->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);              // a5=2 : depth-write off
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_MODULATE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,  D3DTOP_MODULATE); // (0, ALPHAOP(4), MODULATE(4))
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    D3DXMATRIX ident; D3DXMatrixIdentity(&ident);
    dev_->SetTransform(D3DTS_WORLD, &ident);
    dev_->SetTransform(D3DTS_TEXTURE0, &ident);

    // Base camera-facing depuis la matrice vue (right/up monde = colonnes de la rotation).
    D3DXMATRIX view; camera.BuildViewMatrix(view);
    D3DXVECTOR3 right(view._11, view._21, view._31);
    D3DXVECTOR3 up(view._12, view._22, view._32);
    D3DXVec3Normalize(&right, &right);
    D3DXVec3Normalize(&up, &up);
    // Demi-taille du quad : la vraie base flt_8001D4..E8 est calculée au runtime depuis la vue
    // (Particle_RenderBillboards) et non disponible statiquement -> défaut build-safe. TODO ancre 0x6a70b0.
    constexpr float kHalf = 8.0f;

    struct BbVert { float x, y, z; DWORD color; float u, v; };
    static_assert(sizeof(BbVert) == 24, "vertex billboard = 24 o (FVF 322)");
    const DWORD kWhite = 0xFFFFFFFF;
    for (const FxBillboard& bb : fxBillboards_) {
        if (!bb.texture) continue;
        const D3DXVECTOR3 c(bb.pos[0], bb.pos[1], bb.pos[2]);
        const D3DXVECTOR3 r = right * kHalf, u2 = up * kHalf;
        const D3DXVECTOR3 p0 = c - r + u2, p1 = c + r + u2, p2 = c + r - u2, p3 = c - r - u2;
        auto set = [&](BbVert& d, const D3DXVECTOR3& p, float u, float v) {
            d.x = p.x; d.y = p.y; d.z = p.z; d.color = kWhite; d.u = u; d.v = v;
        };
        BbVert q[6];
        set(q[0], p0, 0, 0); set(q[1], p1, 1, 0); set(q[2], p2, 1, 1); // tri 1
        set(q[3], p0, 0, 0); set(q[4], p2, 1, 1); set(q[5], p3, 0, 1); // tri 2
        dev_->SetTexture(0, bb.texture);
        dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 2, q, sizeof(BbVert));
    }

    // Restauration.
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, prevZWrite);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, prevBlend);
    dev_->SetRenderState(D3DRS_SRCBLEND, prevSrc);
    dev_->SetRenderState(D3DRS_DESTBLEND, prevDst);
    dev_->SetRenderState(D3DRS_LIGHTING, prevLighting);
    dev_->SetRenderState(D3DRS_CULLMODE, prevCull);
    meshRenderer_.InvalidateShaderBindingCache();
}

// ===========================================================================
//  FRONT W3-F3 — tick d'animation du monde. Ancre IDA : MapColl_UpdateObjectAnim 0x694A00
//  (site Scene_InGameUpdate 0x52c94b, kAnimFps=15.0). À appeler par SceneManager chaque frame :
//    if (worldGeom_) worldGeom_.TickWorldAnim(dtSeconds);
//  (SceneManager non possédé ici -> commentaire d'intégration, pas d'édition.)
// ===========================================================================
void WorldGeometryRenderer::TickWorldAnim(float dt) {
    // Eau : accumulateur de temps (t = wavePhase_*10 dans bindWaterStates). Origine : v92 Terrain_Render.
    wavePhase_ += dt;

    // Sway .WO : phase de flipbook par instance (aux+28 += dt*fps ; wrap par nb de frames A du
    // gabarit). Ancre IDA : MapColl_UpdateObjectAnim @0x694a30/@0x694a4a. Donnée d'état prête ; la
    // SÉLECTION de la frame au GPU (SetStreamSource(0, vb, 32*frame*B, stride)) reste un TODO ancre
    // MeshPart_Render 0x6aed60 (uploadPart n'uploade que la frame 0 -> pose statique).
    constexpr float kAnimFps = 15.0f;
    if (instancePhase_.size() != instances_.size())
        instancePhase_.assign(instances_.size(), 0.0f);
    for (size_t i = 0; i < instancePhase_.size(); ++i) {
        instancePhase_[i] += dt * kAnimFps;
        const uint32_t frames = (i < instanceFrameCount_.size()) ? instanceFrameCount_[i] : 1u;
        if (frames > 1) {
            const float span = static_cast<float>(frames);
            while (instancePhase_[i] >= span) instancePhase_[i] -= span; // wrap (cf. boucle @0x694a5e)
        } else {
            instancePhase_[i] = 0.0f;
        }
    }

    // Tick des systèmes de particules .WP : le sim complet (Particle_Init 0x6a7020 /
    // Particle_UpdateEmit 0x6a7530) n'est pas porté -> les billboards restent en pose placée.
    // TODO ancre 0x694a00 (branche fxbRecords : Particle_Init/UpdateEmit par fxb+28).
}

} // namespace ts2::gfx

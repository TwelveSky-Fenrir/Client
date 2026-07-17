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
// FVF fixed-function des parts .WO (MobjVertex 32o) : 0x112 = 274 = D3DFVF_XYZ|NORMAL|TEX1.
// Ancre IDA : Model_RenderParts 0x6a3720 SetFVF(274) @0x6a377f (stride 32, cf. MeshPart_Render 0x6aeea5).
constexpr DWORD kFvfWo = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1; // 0x112
static_assert(kFvfWo == 0x112, "FVF .WO doit valoir 274 (0x112)");

// ---------------------------------------------------------------------------------------------
//  RANGS DE DESSIN DU TERRAIN — liste EXACTE des couches que Terrain_Render 0x698670 dessine.
//  (Passe 4 / W5, front terrain-motion : corrige la « géométrie fantôme » + la passe a5=2.)
//
//  Le binaire n'a QUE 2 passes : garde d'entrée @0x698676-0x6986a2 = `*(a1+4) && *(a1+8)==1 &&
//  a5>=1 && a5<=2` ; sites d'appel Scene_InGameRender @0x52d9be (a5=1) et @0x52ead8 (a5=2).
//  Chaque boucle balaye les matériaux (a1+16, stride 52 : +40 catégorie, +44 subOrder, +48
//  texture) et ne dessine que ceux dont le test ci-dessous passe (+ faces visibles > 0) :
//
//   rang 0  cat==2            @0x698f97  (a5=1) AUCUN test de sub — lightmap ON, sampler CLAMP
//   rang 1  cat==4            @0x69902d  (a5=1) AUCUN test de sub — lightmap ON, sampler WRAP
//   rang 2  cat==1 && sub==0  @0x6990fa  (a5=1) lightmap OFF (coupée @0x6990ba), WRAP
//   rang 3  cat==3            @0x6992c4  (a5=1) eau, CLAMP — gate @0x69914d : ∃ cat3/sub0
//   rang 4  cat==1 && sub==1  @0x69941b  (a5=1) ALPHATEST @0x6993d4 + ALPHAREF=128 @0x6993e9
//   rang 5  cat==3            @0x6995e7  (a5=1) eau, CLAMP — gate @0x699473 : ∃ cat3/sub1
//                                        (alpha-test TOUJOURS actif : coupé seulement @0x699704)
//   rang 6  cat==1 && sub==2  @0x698811  (a5=2) ZWRITE OFF @0x6987f3 + ALPHABLEND ON @0x698804
//   rang 7  cat==3            @0x698a0a  (a5=2) eau blendée — gate @0x698890 : ∃ cat3/sub2
//   -----------------------------------------------------------------------------------------
//   TOUT LE RESTE = JAMAIS dessiné par AUCUNE boucle du binaire => rang -1, écarté dès le Build
//   (c'était la « géométrie fantôme » : l'ancien `return 6` fourre-tout uploadait ET dessinait
//   en opaque des couches que le moteur d'origine ignore totalement).
//
//  ⚠ PIÈGE (vérifié, contre-intuitif) : les 3 boucles EAU testent `cat==3` SEUL — elles n'ont
//  AUCUN test de subOrder (seule la GATE qui les précède teste un sub précis). Une couche cat3
//  de subOrder quelconque est donc dessinée dès qu'une gate passe : cat==3 ne doit JAMAIS être
//  filtré, quel que soit son sub (le sub ne sert qu'à choisir le rang/la passe). Idem cat2/cat4 :
//  tout subOrder est dessiné (d'où le rang par catégorie seule, sans test de sub).
//
//  Domaine réel mesuré sur les 97 .WG de D07_GWORLD (inventaire des trailers + faces portées) :
//  (1,0) (1,1) (1,2) (2,0) (2,1) (2,2) (3,0) (3,2) (4,0) (4,1) (4,2) — soit cat ∈ {1,2,3,4} et
//  sub ∈ {0,1,2}. Le filtre -1 n'écarte donc AUCUNE face réelle (0 matériau, 0 face) : c'est un
//  garde-fou de fidélité, pas un correctif visuel. Le vrai gain visuel est ailleurs — (1,2) et
//  (3,2) (59 + 49 matériaux, ~73 000 faces) passent de « opaque, z-write ON » à la passe a5=2
//  réelle (blend + z-write OFF), et (2,1)/(4,1) cessent de subir un alpha-test parasite.
// ---------------------------------------------------------------------------------------------
enum : int {
    kRank_Cat2     = 0, kRank_Cat4   = 1, kRank_Cat1Sub0 = 2, kRank_Water0 = 3,
    kRank_Cat1Sub1 = 4, kRank_Water1 = 5, kRank_Cat1Sub2 = 6, kRank_Water2 = 7,
    kRank_NotDrawn = -1,
    kRank_FirstPass2 = kRank_Cat1Sub2 // rangs >= : sous-passe a5=2 (blend + z-write off)
};

// Catégorie de matériau EAU (trailer[0]==3). Ancre IDA : MapColl_LoadMapFile @0x698033
// (cherche mat+40==3 -> déclenche wave/falloff).
constexpr uint32_t kTerrainCatWater = 3;

// Renvoie le rang de dessin d'une couche, ou kRank_NotDrawn si le binaire ne la dessine jamais.
int TerrainLayerRank(uint32_t category, uint32_t subOrder) {
    if (category == 2) return kRank_Cat2;                    // @0x698f97, tout sub
    if (category == 4) return kRank_Cat4;                    // @0x69902d, tout sub
    if (category == 1) {                                     // cat1 : le sub choisit la passe
        if (subOrder == 0) return kRank_Cat1Sub0;            // @0x6990fa
        if (subOrder == 1) return kRank_Cat1Sub1;            // @0x69941b (alpha-test)
        if (subOrder == 2) return kRank_Cat1Sub2;            // @0x698811 (a5=2, blend)
        return kRank_NotDrawn;                               // cat1/sub>=3 : aucune boucle
    }
    if (category == kTerrainCatWater) {                      // boucles eau SANS test de sub
        if (subOrder == 2) return kRank_Water2;              // @0x698a0a (a5=2), gate @0x698890
        if (subOrder == 1) return kRank_Water1;              // @0x6995e7, gate @0x699473
        return kRank_Water0;                                 // @0x6992c4, gate @0x69914d — et
        // tout sub non prouvé retombe ici : les boucles eau n'ont AUCUN test de sub, donc une
        // couche cat3 n'est jamais écartée (cas absent des 97 .WG réels, cf. bandeau ci-dessus).
    }
    return kRank_NotDrawn;                                   // cat 0, 5, 6... : jamais dessiné
}

// Empaquette un float dans le DWORD attendu par SetTextureStageState/SetRenderState (bit-copie).
inline DWORD F2DW(float f) { DWORD d; std::memcpy(&d, &f, 4); return d; }

// ---------------------------------------------------------------------------------------------
//  TWS-01 (Passe 4 / W11) — TEXTURE DE VAGUES = CODE MORT, VOLONTAIREMENT NON PORTÉE.
//  Re-prouvé dans IDA (idaTs2, lecture seule) : le binaire ne crée JAMAIS de texture de vagues
//  dans le chemin de terrain VIVANT. Le SEUL créateur d'une telle texture, cWorldMesh_MakeWaterWave
//  Texture 0x451220, n'a que deux appelants — cWorldMesh_LoadG3W 0x449800 et
//  cWorldMesh_LoadQuadtreeWM2 0x44d440 — et CHACUN a 0 xref (code ET data ; absents des vtables) :
//  tout le sous-arbre est injoignable depuis WinMain 0x4609C0. La carte de perturbation liée par
//  Terrain_Render au stage 0 (`SetTexture(0, *(a1+20))` @0x69928f, sous COLOROP=BUMPENVMAPLUMINANCE
//  @0x699206) est en réalité la TEXTURE DE FALLOFF radiale : *(a1+20) == this+5 == octet 20, écrit
//  UNIQUEMENT par MapColl_CreateFalloffTexture 0x694ca0 (@0x694cac `v2 = this + 5`), appelée @0x698043
//  par MapColl_LoadMapFile 0x697b30 sous la gate « matériau catégorie 3 » @0x698033. Identité d'objet
//  prouvée deux fois : 0x697b30 (__thiscall) écrit *(this+3)/*(this+4) stride 52, gate +40==3, que
//  Terrain_Render relit à l'identique -> même classe -> this+5 ≡ a1+20. Le port précédent commettait
//  DEUX fautes cumulées : (1) il réanimait le code mort 0x451220 (appel MakeWaterWaveTexture au Build)
//  et (2) il liait cette mauvaise texture au lieu du falloff. Les deux sont corrigés ici -> on ne
//  porte donc PAS de générateur de vagues (RÈGLE #7 : une fonction morte du binaire reste morte).
// ---------------------------------------------------------------------------------------------

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

// Dimension de la texture de falloff/bump-env de l'eau. PROUVÉE (TWS-02) = 256, PAS un choix
// build-safe : MapColl_Construct 0x693080 @0x6930b1 (`mov dword ptr [esi], 100h`) et
// MapColl_ResetHeader 0x693120 @0x693129 posent MapColl+0 = 256, consommé par
// MapColl_CreateFalloffTexture 0x694ca0 @0x694cd3 (D3DXCreateTexture(dev, *this, *this, ...)) en
// largeur ET hauteur, et par ses bornes de boucle @0x694d08/@0x694d1a. La valeur n'est JAMAIS lue
// du fichier .WG : MapColl_LoadMapFile 0x697b30 écrit this+1..+4,+21,+22,+33..+41 mais jamais *this
// (décompilation intégrale vérifiée) -> 256 est une constante statique du constructeur, pas une
// donnée runtime. (L'ancien bandeau « cWorldMesh+0 au runtime -> 64 par défaut » était faux sur les
// deux points : mauvais objet — cWorldMesh est mort, cf. TWS-01 — et valeur disponible en statique.)
constexpr UINT kWaterTexDim = 256;

// Taille de l'en-tête fixe du bloc géométrie GXD d'un WorldMeshPart (Asset/WorldChunk.cpp,
// ReadMeshPart : A/B/C/D lus à l'offset 120, soit 136 o d'en-tête au total).
constexpr size_t kGeoHeaderSize = 136;
// Stride disque d'un vertex MOBJECT (Docs/TS2_ASSET_FORMATS.md : FVF 0x112 = XYZ|NORMAL|TEX1).
constexpr size_t kMobjVertexStride = 32;
// Stride disque d'un index (INDEX16, 3 index/face = 6 o/face).
constexpr size_t kIndexStride = 2;

// FVF terrain -> FfTerrainVertex 40o : le vertex disque asset::TerrainVertex (pos/normal/uv0/uv1)
// est BIT-À-BIT identique -> aucune conversion, copie directe (memcpy dans le batch de cull).
static_assert(sizeof(asset::TerrainVertex) == 40, "TerrainVertex disque = 40o (memcpy direct FF)");

// =============================================================================================
//  FRONT F_TERRAIN (B5) — CULL QUADTREE/FRUSTUM DU TERRAIN (helpers de rendu, LECTURE SEULE).
//  Reproduction byte-exacte des 3 primitives de cull du binaire (vérifiées en IDA le 2026-07-17) :
//    Cam_FrustumTestAABB      0x69f230  (rejet AABB ssi les 8 coins derrière UN même plan)
//    Cam_FrustumTestSphere2x  0x69f0e0  (sphère : plane·c + d >= -2*rayon, marge DOUBLÉE)
//    MapColl_CollectLeafFaces 0x694b50  (descente quadtree frustum-cull -> feuilles visibles)
//  Les 6 plans du frustum sont stockés par l'original à g_GfxRenderer+334 (offsets +334..+357,
//  cf. décompilation Cam_FrustumTestSphere2x). ClientSource ne dispose pas de ce singleton : on
//  les RECONSTRUIT depuis view*proj (Gribb-Hartmann), ce qui donne les MÊMES plans (repère monde,
//  normales rentrantes, dedans <=> a*x+b*y+c*z+d >= 0). Plans NORMALISÉS : la marge -2*rayon du
//  test sphère est en unités monde, donc le facteur d'échelle DOIT être unitaire.
// =============================================================================================
struct FrustumPlanes { float pl[6][4]; }; // [plan][a,b,c,d] ; g_GfxRenderer+(334+4*p)

// Extrait les 6 plans (rentrants, normalisés) depuis vp = view*proj (D3DX row-vector, LH D3D z€[0,w]).
FrustumPlanes ExtractFrustum(const D3DXMATRIX& vp) {
    FrustumPlanes f;
    // col j (0-based) composante i = vp._{i+1}{j+1}. Gauche=col3+col0, Droite=col3-col0,
    // Bas=col3+col1, Haut=col3-col1, Proche=col2 (z>=0 en D3D), Loin=col3-col2.
    f.pl[0][0]=vp._14+vp._11; f.pl[0][1]=vp._24+vp._21; f.pl[0][2]=vp._34+vp._31; f.pl[0][3]=vp._44+vp._41; // gauche
    f.pl[1][0]=vp._14-vp._11; f.pl[1][1]=vp._24-vp._21; f.pl[1][2]=vp._34-vp._31; f.pl[1][3]=vp._44-vp._41; // droite
    f.pl[2][0]=vp._14+vp._12; f.pl[2][1]=vp._24+vp._22; f.pl[2][2]=vp._34+vp._32; f.pl[2][3]=vp._44+vp._42; // bas
    f.pl[3][0]=vp._14-vp._12; f.pl[3][1]=vp._24-vp._22; f.pl[3][2]=vp._34-vp._32; f.pl[3][3]=vp._44-vp._42; // haut
    f.pl[4][0]=vp._13;        f.pl[4][1]=vp._23;        f.pl[4][2]=vp._33;        f.pl[4][3]=vp._43;        // proche
    f.pl[5][0]=vp._14-vp._13; f.pl[5][1]=vp._24-vp._23; f.pl[5][2]=vp._34-vp._33; f.pl[5][3]=vp._44-vp._43; // loin
    for (int p = 0; p < 6; ++p) {
        const float len = std::sqrt(f.pl[p][0]*f.pl[p][0] + f.pl[p][1]*f.pl[p][1] + f.pl[p][2]*f.pl[p][2]);
        if (len > 1e-8f) { const float inv = 1.0f / len; for (int k = 0; k < 4; ++k) f.pl[p][k] *= inv; }
    }
    return f;
}

// Cam_FrustumTestAABB 0x69f230 : renvoie false (rejet) SSI, pour UN plan, les 8 coins de [mn,mx] ont
// tous plane·coin + d < 0 (boîte entièrement derrière ce plan). Conservateur (jamais de faux rejet).
bool FrustumTestAABB(const FrustumPlanes& fr, const float mn[3], const float mx[3]) {
    for (int p = 0; p < 6; ++p) {
        const float a = fr.pl[p][0], b = fr.pl[p][1], c = fr.pl[p][2], d = fr.pl[p][3];
        bool allOut = true;
        for (int ci = 0; ci < 8; ++ci) {                       // 8 coins {mn|mx}^3
            const float x = (ci & 1) ? mx[0] : mn[0];
            const float y = (ci & 2) ? mx[1] : mn[1];
            const float z = (ci & 4) ? mx[2] : mn[2];
            if (a * x + b * y + c * z + d >= 0.0f) { allOut = false; break; } // ce coin est du bon côté
        }
        if (allOut) return false;                              // 8 coins derrière -> rejet du sous-arbre
    }
    return true;                                               // intersection/dedans -> gardé
}

// Cam_FrustumTestSphere2x 0x69f0e0 : sphère (centre, rayon) gardée SSI plane·c + d >= -2*rayon pour
// les 6 plans (marge DOUBLÉE @0x69f0ee `v4 = a3 * -2.0`).
bool FrustumTestSphere2x(const FrustumPlanes& fr, const float c[3], float radius) {
    const float v4 = radius * -2.0f;                           // 0x69f0ee
    for (int p = 0; p < 6; ++p)
        if (fr.pl[p][0]*c[0] + fr.pl[p][1]*c[1] + fr.pl[p][2]*c[2] + fr.pl[p][3] < v4) return false; // 0x69f21a
    return true;
}

// MapColl_CollectLeafFaces 0x694b50 : descente récursive du quadtree, collecte des feuilles dont
// l'AABB intersecte le frustum. Byte-exact : garde trisNum>0 (node+24 @0x694b65), test AABB
// (@0x694b7d, break/return sur rejet -> sous-arbre pruné), feuille = child[0]==-1 (@0x694b93 -> append
// @0x694bed), sinon récursion child[0..2] (@0x694b98/@0x694baa/@0x694bbc) + boucle-tail child[3]
// (@0x694bc7). Racine = nœud d'index 0. `out` = indices des nœuds feuilles visibles (a1+152).
void CollectLeaves(const std::vector<asset::CollisionQuadNode>& nodes, const FrustumPlanes& fr,
                   int nodeIdx, std::vector<int32_t>& out) {
    if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= nodes.size()) return;
    if (nodes[nodeIdx].trisNum == 0) return;                    // *(node+24)==0 -> nœud vide  0x694b65
    for (;;) {
        const asset::CollisionQuadNode& n = nodes[nodeIdx];
        if (!FrustumTestAABB(fr, n.bboxMin, n.bboxMax)) return; // hors frustum -> tout pruné  0x694b7d
        if (n.child[0] == -1) { out.push_back(nodeIdx); return; } // FEUILLE  0x694b93 / append 0x694bed
        CollectLeaves(nodes, fr, n.child[0], out);              // 0x694b98
        CollectLeaves(nodes, fr, n.child[1], out);              // 0x694baa
        CollectLeaves(nodes, fr, n.child[2], out);              // 0x694bbc
        nodeIdx = n.child[3];                                   // boucle-tail  0x694bc7
        if (nodeIdx < 0 || static_cast<size_t>(nodeIdx) >= nodes.size()) return;
        if (nodes[nodeIdx].trisNum == 0) return;                // while *(child3+24)!=0  0x694b7d
    }
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
    // B6 : chaque StaticObject possède un VB natif (A frames), un IB et sa texture diffuse.
    // FRONT C2 : + la 2e texture et l'atlas flipbook (materials[]), tous possédés (câblage B1).
    for (StaticObject& obj : objects_) {
        SafeRelease(obj.vb); SafeRelease(obj.ib); SafeRelease(obj.diffuse);
        SafeRelease(obj.second);
        for (IDirect3DTexture9*& t : obj.flipbook) SafeRelease(t);
        obj.flipbook.clear();
    }
    objects_.clear();
    modelRanges_.clear();
    instances_.clear();
    instancePhase_.clear();
    // Hygiène (Passe 4/W5b) : instancePhase_ et instanceFrameCount_ sont posés EN PARALLÈLE sur
    // instances_.size() au chargement (cf. assign() jumeaux, ancre MapColl_UpdateObjectAnim 0x694a00
    // -> frameCount = part.A) ; ils doivent donc être libérés ensemble. Sans conséquence aujourd'hui
    // (TickWorldAnim se garde par `i < instanceFrameCount_.size()`), mais l'asymétrie était réelle.
    instanceFrameCount_.clear();
    releaseTerrain(); // libère aussi les couches/textures du sol .WG + eau + lightmap
    releaseFx();      // libère les billboards FX de zone .WP + leurs textures
}

// Libère les VB/IB des couches terrain, les textures diffuses (possédées), les textures d'eau
// procédurales et la lightmap.
void WorldGeometryRenderer::releaseTerrain() {
    // B5 : les couches ne possèdent plus de VB/IB statique (le batch est CPU -> DrawPrimitiveUP).
    terrainLayers_.clear();
    for (IDirect3DTexture9*& t : terrainTextures_) SafeRelease(t);
    terrainTextures_.clear();
    SafeRelease(falloffTex_); // TWS-01 : plus de waveTex_ (générateur mort 0x451220 non porté)
    SafeRelease(shadowTex_);
    wavePhase_ = 0.0f;
    terrainFaceCount_ = 0;
    // État du cull quadtree/frustum (B5).
    terrainFaces_.clear();
    terrainNodes_.clear();
    terrainTriIndices_.clear();
    terrainBatch_.clear();
    matBase_.clear();
    matCounter_.clear();
    faceSeen_.clear();
    terrainLeafScratch_.clear();
    terrainNumMaterials_ = 0;
}

// Libère les pools de particules (.WP), leurs tableaux heap, les templates et textures des nœuds FX.
// FRONT F_ZONEFX : chaque FxParticlePool possède un tableau HeapAlloc (Particle_Free 0x6a6ff0).
void WorldGeometryRenderer::releaseFx() {
    for (ts2::gfx::FxParticlePool& pool : fxPools_)
        ts2::gfx::ZoneFx_Free(&pool);           // HeapFree du tableau de particules
    fxPools_.clear();
    fxRecords_.clear();
    fxTemplates_.clear();                        // templates 232o (pointés par les pools, libérés après)
    for (IDirect3DTexture9*& t : fxTextures_) SafeRelease(t);
    fxTextures_.clear();
    fxScratch_.clear();
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
    //   [136 o en-tête][64*A o bones (métadonnée sway, non lue au rendu)][32*A*B o VB = A frames
    //   consécutives de B sommets 32o][6*D o IB, PARTAGÉ par toutes les frames (topologie fixe)]
    const size_t boneBytes        = 64ull * part.A;
    const size_t vbOffset         = kGeoHeaderSize + boneBytes;               // début de la frame 0
    const size_t vbAllFramesBytes = kMobjVertexStride * static_cast<size_t>(part.B) * part.A; // 32*A*B
    const size_t ibOffset         = vbOffset + vbAllFramesBytes;
    const size_t ibBytes          = 3ull * kIndexStride * part.D;             // 6*D
    if (part.geo.size() < ibOffset + ibBytes) {
        TS2_WARN("WorldGeometryRenderer: part incoherente (geo=%zu < attendu=%zu)",
                 part.geo.size(), ibOffset + ibBytes);
        return false;
    }

    // B6 (2026-07-17) — REJEU DU SWAY : on uploade DÉSORMAIS les A frames contiguës TELLES QUELLES en
    // 32o natif (MobjVertex, FVF 0x112), miroir de MeshPart_Load 0x6ad169 (`qmemcpy 32*A*B`). La frame
    // est sélectionnée au dessin par SetStreamSource(0, vb, 32*frame*B, 32) (MeshPart_Render 0x6aeea5)
    // -> plus de conversion 76o ni de pose figée frame 0. L'IB (6*D o) est partagé par toutes les
    // frames (topologie identique, seules positions/normales changent, cf. bandeau .h point 5).
    const UINT vbBytes = static_cast<UINT>(vbAllFramesBytes);
    HRESULT hr = dev_->CreateVertexBuffer(vbBytes, 0, kFvfWo, D3DPOOL_MANAGED, &out.vb, nullptr);
    if (FAILED(hr)) {
        TS2_ERR("WorldGeometryRenderer: CreateVertexBuffer (.WO) echoue (0x%08lX)", hr);
        return false;
    }
    void* p = nullptr;
    if (SUCCEEDED(out.vb->Lock(0, vbBytes, &p, 0))) {
        std::memcpy(p, part.geo.data() + vbOffset, vbBytes); // A frames brutes (aucune conversion)
        out.vb->Unlock();
    }

    const UINT ibTotal = static_cast<UINT>(ibBytes);
    hr = dev_->CreateIndexBuffer(ibTotal, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &out.ib, nullptr);
    if (FAILED(hr)) {
        TS2_ERR("WorldGeometryRenderer: CreateIndexBuffer (.WO) echoue (0x%08lX)", hr);
        SafeRelease(out.vb);
        return false;
    }
    if (SUCCEEDED(out.ib->Lock(0, ibTotal, &p, 0))) {
        std::memcpy(p, part.geo.data() + ibOffset, ibTotal);
        out.ib->Unlock();
    }

    out.A = part.A; out.B = part.B; out.D = part.D;
    // FRONT C2 (2026-07-17) — RESSOURCES MATÉRIAU du part pour B1 MeshPart_RenderFull 0x6b0850 (le TODO
    // « tex2/materials[] parsés mais ignorés » est RÉSOLU) :
    //   base     = tex1  -> a1[86]/+344 ; mode base = tex1.mode() (= trailer[1] = struct+44 = a1[85]/+340)
    //   2e tex   = tex2  -> a1[99]/+396 ; mode 2e   = tex2.mode() (= a1[98]/+392)
    //   flipbook = materials[] (atlas animé) -> a1[101]/+404, une IDirect3DTexture9 par entrée présente
    //   mat      = en-tête matériau 120o décodé (part.mat), COPIÉ PAR VALEUR (le WorldChunk peut être libéré)
    // Ancre IDA : MeshPart_Load 0x6ad160 (tex1@296, tex2@348, materials@404 ; qmemcpy(this+132,Heap,0x78) @0x6ad2d1).
    out.diffuse    = createTextureFromBlock(dev_, part.tex1);
    out.second     = createTextureFromBlock(dev_, part.tex2);
    out.baseMode   = static_cast<int>(part.tex1.mode()); // this[85] : 1=alpha-test / 2=blend / autre=additif
    out.secondMode = static_cast<int>(part.tex2.mode()); // this[98]
    out.mat        = part.mat;                            // copie du header 120o décodé (mat.decoded préservé)
    out.flipbook.clear();
    out.flipbook.reserve(part.materials.size());
    for (const asset::TextureBlock& m : part.materials)
        out.flipbook.push_back(createTextureFromBlock(dev_, m)); // nullptr si holder absent (imageSize==0)
    if (part.A > 1) ++multiAnchorStaticCount_; // A>1 = flipbook de sway (désormais REJOUÉ, cf. B6)
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

    // FRONT W3-F3 : état de sway par instance (borne de wrap = nb de frames A du gabarit).
    // Ancre IDA : MapColl_UpdateObjectAnim 0x694a00 (@0x694a4d avance aux+28, wrap par frameCount =
    // part.A = *(...+252)). Tické par TickWorldAnim.
    //
    // TWS-03 (Passe 4 / W11) — SEEDING ALÉATOIRE +28/+32 DÉLIBÉRÉMENT NON REPRODUIT (refutation de la
    // prescription phase-1). Fait PROUVÉ dans IDA : MapColl_LoadObjectsA 0x6980d0 lit un record de 28 o
    // DISQUE (u32 idx@+0, float3@+4, float3@+16) puis, en boucle @0x698340-0x69837d (stride runtime 36),
    // SÈME les 8 octets restants avec DEUX tirages : *(base+28) = Math_RandRangeFloat(0,100) @0x69835d et
    // *(base+32) idem @0x698370 — donc +28/+32 ne viennent PAS du disque (corrobore
    // World/WorldIntegration.cpp:235). instancePhase_ ≡ +28 (double emploi graine + curseur d'anim,
    // mapping correct). MAIS le seeding n'est PAS appliqué ici, pour DEUX raisons re-prouvées :
    //   1) Math_RandRangeFloat 0x69cb10 tire via Rng_Next 0x7603fd = le rand() CRT PARTAGÉ, i.e. le
    //      MÊME flux que net::DefaultRng() (nonces réseau, cf. Net/Login.cpp:34 / CharSelectPackets.cpp:58).
    //      Injecter 2×N tirages au chargement de zone PERTURBERAIT ce flux SANS reproduire la timeline
    //      rand() globale du binaire (intro/menus/… non portés dans l'ordre) -> divergence des nonces,
    //      pire que le manque. La fidélité bit-exacte des nonces (CLAUDE.md) est déjà inatteignable ; on
    //      ne l'aggrave pas pour une graine cosmétique.
    //   2) Aucun consommateur de la graine n'est porté : le GPU dessine toujours la frame 0 (pose
    //      statique, cf. bandeau .h point 5) et +32 n'alimente que Model_RenderWithShadow_0 0x6a4110
    //      arg 6, non porté. La graine n'aurait donc AUCUN effet observable. Pas d'instanceSeed2_ non
    //      plus (ce serait un état écrit-jamais-lu = l'anti-motif que la campagne traque).
    // -> phase initiale 0 conservée (documentée), pas de tirage RNG. TODO si un jour la sélection GPU
    //    de frame + la timeline rand() globale sont portées : semer via net::DefaultRng() à ce moment.
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
//  Ici : une TerrainLayer(diffuse, category, subOrder, materialIndex) par matériau dessiné, TRIÉE par
//  rang pour reproduire l'ordre de dessin de Terrain_Render(a5=1). FRONT F_TERRAIN (B5) : la couche
//  ne porte PLUS de VB/IB statique — le rendu est un CULL quadtree/frustum PAR FRAME (cullTerrain())
//  qui remplit un batch CPU (terrainBatch_, 3 sommets/face) puis DrawPrimitiveUP par matériau,
//  EXACTEMENT comme le binaire. buildTerrain se limite donc à : textures par matériau, COPIE du
//  quadtree/faces/heap d'index (a1+88/a1+140), bases de batch cumulées (matBase_), textures d'eau
//  (falloff, cat==3) et lightmap .SHADOW. Sommets = asset::TerrainVertex bit-à-bit FfTerrainVertex 40o.
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

    // 2) FRONT F_TERRAIN (B5) — copie CPU des données de cull du .WG (Render() n'a plus accès à
    //    WorldAssets) : faces 156o (a1+88), quadtree 48o (a1+140, racine = index 0) et heap d'index
    //    de faces (node.trisIndex = offset dans ce heap). Le quadtree est déjà décodé par
    //    Asset/WorldChunk.cpp (CollisionMesh::nodes/triIndices, ancre MapColl_LoadFaces 0x694510)
    //    -> réutilisé tel quel, AUCUN re-décodage nécessaire.
    terrainNumMaterials_ = numMat;
    terrainFaces_       = mesh.tris;         // a1+88
    terrainNodes_       = mesh.nodes;        // a1+140
    terrainTriIndices_  = mesh.triIndices;   // heap faceRefIndex

    // 3) Compte des faces PAR MATÉRIAU -> base cumulée matBase_ (a1+144) : chaque matériau reçoit une
    //    région contiguë [matBase_[m], matBase_[m]+count[m]) dans le batch, dimensionnée au pire cas
    //    (toutes ses faces visibles). L'original précalcule ces bases au chargement du .WG.
    std::vector<uint32_t> faceCountPerMat(numMat, 0);
    size_t outOfRange = 0;
    for (const asset::CollisionFace& f : terrainFaces_) {
        if (f.materialIndex < numMat) ++faceCountPerMat[f.materialIndex];
        else ++outOfRange;                   // materialIndex hors bornes -> jamais batché
    }
    matBase_.assign(numMat, 0);
    uint32_t cum = 0;
    for (uint32_t m = 0; m < numMat; ++m) { matBase_[m] = cum; cum += faceCountPerMat[m]; }
    const uint32_t drawableFaces = cum;      // faces à matériau valide (capacité du batch)

    // 4) Buffers de travail réutilisés chaque frame (alloués UNE FOIS ici) : batch CPU (3 sommets par
    //    face, a1+164), curseur par matériau (a1+160), flag « face vue cette frame » (a1+156, indexé
    //    par face), liste de feuilles visibles (a1+152). Reset des trois derniers dans cullTerrain.
    terrainBatch_.assign(static_cast<size_t>(drawableFaces) * 3, FfTerrainVertex{}); // a1+164
    matCounter_.assign(numMat, 0);                                                    // a1+160
    faceSeen_.assign(terrainFaces_.size(), 0);                                        // a1+156
    terrainLeafScratch_.clear();
    terrainLeafScratch_.reserve(terrainNodes_.size());                                // a1+152 (borne haute)

    // 5) Une TerrainLayer par matériau PORTANT des faces et effectivement DESSINÉ par le binaire
    //    (rang >= 0). Les (cat,sub) que Terrain_Render 0x698670 ne dessine par AUCUNE boucle (rang < 0)
    //    sont écartés ici (géométrie fantôme). Plus de VB/IB : la couche ne fait que référencer sa
    //    région de batch via `materialIndex` (le batch est rempli par frame et dessiné en DrawPrimitiveUP).
    size_t ghostLayers = 0, ghostFaces = 0;
    bool hasWater = false;
    for (uint32_t m = 0; m < numMat; ++m) {
        if (faceCountPerMat[m] == 0) continue;
        TerrainLayer layer;
        layer.diffuse       = terrainTextures_[m]; // réf non-possédante
        // trailer[0]=catégorie, trailer[1]=subOrder (prouvé Tex_LoadCompressedFromHandle 0x6a9cf0).
        layer.category      = (m < wg->textures.size()) ? wg->textures[m].trailer[0] : 0;
        layer.subOrder      = (m < wg->textures.size()) ? wg->textures[m].trailer[1] : 0;
        layer.rank          = TerrainLayerRank(layer.category, layer.subOrder);
        layer.materialIndex = m;
        if (layer.rank == kRank_NotDrawn) {
            // Le binaire n'a aucune boucle pour ce (cat,sub) : compté/loggé (attendu 0 sur les 97 .WG
            // réels ; si != 0, re-vérifier TerrainLayerRank AVANT de conclure).
            ++ghostLayers;
            ghostFaces += faceCountPerMat[m];
            continue;
        }
        if (layer.category == kTerrainCatWater) hasWater = true;
        terrainLayers_.push_back(layer);
    }
    terrainFaceCount_ = drawableFaces;

    // 6) Tri des couches par rang — reproduit l'ordre EXACT de Terrain_Render : passe a5=1
    //    (rangs 0..5) puis passe a5=2 (rangs 6..7). renderTerrain() s'appuie sur cet ordre
    //    croissant pour n'ouvrir la sous-passe blendée qu'une fois (bascule à sens unique).
    std::stable_sort(terrainLayers_.begin(), terrainLayers_.end(),
                     [](const TerrainLayer& a, const TerrainLayer& b) { return a.rank < b.rank; });

    // 7) Eau (TWS-01/TWS-02) : la texture de FALLOFF radiale (V8U8 256x256) est créée UNE FOIS si une
    //    couche cat==3 existe, miroir fidèle de MapColl_LoadMapFile @0x698043 -> MapColl_CreateFalloff
    //    Texture 0x694ca0 (seul écrivain vivant de *(a1+20), le bump-map stage 0). PAS de texture de
    //    vagues : son générateur 0x451220 est mort (cf. bandeau TWS-01 ci-dessus).
    if (hasWater) {
        falloffTex_ = MakeFalloffTexture(dev_, kWaterTexDim);
    }

    // 8) Lightmap .SHADOW (stage 1, uv1) — texture GPU depuis les octets DDS bruts exposés par
    //    WorldAssets (le vertex FF possède bien uv1 : le TODO G8 « 1 seul TEXCOORD » disparaît).
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

// ===========================================================================
//  FRONT F_TERRAIN (B5) — CULL QUADTREE/FRUSTUM DU TERRAIN, PAR FRAME.
//  Reproduit à l'octet le bloc de collecte/batch de Terrain_Render 0x698670 (passe a5=1,
//  0x698ce7-0x698e3c), vérifié en IDA (decompile + disasm 0x698d36) le 2026-07-17 :
//    reset leafCount/faceSeen/matCounter (0x698cf9/0x698cff/0x698d1b) -> descente quadtree
//    (MapColl_CollectLeafFaces 0x694b50) -> par feuille, par face collectée : anti-doublon
//    (0x698d8c), backface dot(planeN,eye)>=planeD (0x698dd4, eye=g_CameraPos), sphère marge x2
//    (Cam_FrustumTestSphere2x 0x69f0e0 @0x698de9), batch 120o groupé par matériau (0x698e21).
//  Le résultat (terrainBatch_ + matCounter_) est consommé par renderTerrain() en DrawPrimitiveUP.
//  ⚠ Optimisation CONFORME au binaire : le cull ne retire que des faces NON visibles (hors frustum
//  ou face arrière) -> mêmes faces visibles à l'écran qu'un dessin plat, en moins de triangles.
// ===========================================================================
void WorldGeometryRenderer::cullTerrain(const D3DXMATRIX& view, const D3DXMATRIX& proj,
                                        const D3DXVECTOR3& eye) {
    if (terrainNodes_.empty() || terrainFaces_.empty() || matBase_.empty()) return;

    // 6 plans du frustum (repère monde) reconstruits depuis view*proj (cf. ExtractFrustum) :
    // équivalents à g_GfxRenderer+334 utilisés par Cam_FrustumTestAABB/Sphere2x.
    D3DXMATRIX vp;
    D3DXMatrixMultiply(&vp, &view, &proj);
    const FrustumPlanes fr = ExtractFrustum(vp);

    // Reset des tampons par frame (Terrain_Render 0x698cf9/0x698cff/0x698d1b).
    std::fill(matCounter_.begin(), matCounter_.end(), 0u);
    std::fill(faceSeen_.begin(), faceSeen_.end(), static_cast<uint8_t>(0));
    terrainLeafScratch_.clear();

    // Descente quadtree frustum-cull depuis la racine (nœud 0) -> feuilles visibles.
    CollectLeaves(terrainNodes_, fr, 0, terrainLeafScratch_); // MapColl_CollectLeafFaces((a1),0) 0x698d27

    // Par feuille, par face référencée (Terrain_Render 0x698d36-0x698e3c).
    for (int leafIdx : terrainLeafScratch_) {
        if (leafIdx < 0 || static_cast<size_t>(leafIdx) >= terrainNodes_.size()) continue;
        const asset::CollisionQuadNode& node = terrainNodes_[leafIdx];
        const uint32_t refBase  = node.trisIndex; // offset dans terrainTriIndices_ (node+28 à l'origine)
        const uint32_t refCount = node.trisNum;   // node+24
        if (static_cast<size_t>(refBase) + refCount > terrainTriIndices_.size()) continue; // garde OOB
        for (uint32_t k = 0; k < refCount; ++k) {
            const uint32_t faceIdx = terrainTriIndices_[refBase + k];         // node.faceRefIndex[k] 0x698d7a
            if (faceIdx >= faceSeen_.size()) continue;
            if (faceSeen_[faceIdx]) continue;                                 // anti-doublon 0x698d8c
            faceSeen_[faceIdx] = 1;                                           // 0x698d98
            const asset::CollisionFace& face = terrainFaces_[faceIdx];

            // (2) BACKFACE : dot(planeN, eye) < planeD -> face à l'opposé de l'œil -> skip. 0x698dd4
            //     eye = g_CameraPos ; planeN = face.plane[0..2] ; planeD = face.plane[3].
            const float dotp = eye.x * face.plane[0] + eye.y * face.plane[1] + eye.z * face.plane[2];
            if (dotp < face.plane[3]) continue;

            // (3) FRUSTUM SPHÈRE (marge x2). 0x698de9
            if (!FrustumTestSphere2x(fr, face.sphereCenter, face.sphereRadius)) continue;

            // (4) BATCH : copie 120o (3 sommets = &face.v0) groupé par matériau. 0x698e21
            const uint32_t m = face.materialIndex;                           // face+0  0x698dfe
            if (m >= terrainNumMaterials_) continue;                          // materialIndex hors bornes
            const uint32_t slot  = matBase_[m] + matCounter_[m];             // base[m] + curseur[m]
            const size_t   vbase = static_cast<size_t>(slot) * 3;            // 3 sommets/face
            if (vbase + 3 > terrainBatch_.size()) continue;                  // garde (capacité)
            std::memcpy(&terrainBatch_[vbase], &face.v0, sizeof(FfTerrainVertex) * 3); // 0x78 = 120o
            ++matCounter_[m];                                                // 0x698e30
        }
    }
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

    // 1) Une texture GPU + un TEMPLATE 232o par nœud FX (this+29 du binaire, stride 232). Le template
    //    est bâti octet-exact depuis asset::FxNode : enabled=1, texture à +52, et les 144 o de disque
    //    (FxNode.fields = runtime [+72,+216)) recopiés à (tmpl+72). Ancre : Fx_NodeLoadFromHandle 0x6a69f0.
    //    ⚠ fxTemplates_ est dimensionné UNE FOIS ici et n'est plus redimensionné : les pools gardent
    //    un pointeur vers ses éléments (pool->tmpl), qui doit rester stable jusqu'à releaseFx().
    fxTextures_.assign(wp->nodes.size(), nullptr);
    fxTemplates_.assign(wp->nodes.size(), ts2::gfx::FxEmitterTemplate{});
    for (size_t i = 0; i < wp->nodes.size(); ++i) {
        fxTextures_[i] = createTextureFromBlock(dev_, wp->nodes[i].tex); // nullptr si absente
        const asset::FxNode& node = wp->nodes[i];
        ts2::gfx::ZoneFx_BuildTemplate(&fxTemplates_[i], node.fields.data(), node.fields.size(),
                                       fxTextures_[i]);
        if (!node.present) fxTemplates_[i].enabled = 0; // nœud non chargé -> émetteur désactivé (gate Init)
    }

    // 2) Un POOL (POBJECT 48o, flag=0 -> ZoneFx_Init au 1er tick) par instance placée (record B,
    //    this+32 stride 76). On garde le record (nodeIndex/pos/rot) aligné avec le pool. Ancre :
    //    MapColl_LoadObjectsB 0x6983b0 (@0x698602 lit nodeIndex+pos+rot) + boucle 2 0x694af0.
    fxRecords_.clear();
    size_t outOfRange = 0;
    for (const asset::AuxFxRecord& rec : wp->fxbRecords) {
        if (rec.nodeIndex >= fxTemplates_.size()) { ++outOfRange; continue; }
        fxRecords_.push_back(rec);
    }
    fxPools_.assign(fxRecords_.size(), ts2::gfx::FxParticlePool{}); // zéro-init : flag=0, particles=nullptr

    TS2_LOG("WorldGeometryRenderer::buildFx (F_ZONEFX) : %zu noeuds FX (templates 232o), %zu emetteurs "
            "instancies (POBJECT 48o) (%zu nodeIndex hors bornes). Sim VIVANTE : Particle_Init 0x6a7020 "
            "/ UpdateEmit 0x6a7530 / RenderBillboards 0x6a70b0.",
            fxTemplates_.size(), fxPools_.size(), outOfRange);
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
//  d'UV, le TODO G8 disparaît) ; couches triées par RANG (cf. table TerrainLayerRank en tête de
//  fichier) ; eau cat==3 en bump-env (bindWaterStates) ; alpha-test sur les rangs 4/5 uniquement
//  (ALPHAREF=128 @0x6993e9) ; adressage sampler CLAMP/WRAP par couche. États sauvés/restaurés
//  pour ne pas polluer meshRenderer_ (qui rebinde ses shaders ensuite).
//
//  MISE À JOUR Passe 4 / W5 (front terrain-motion) — cette fonction couvre désormais les DEUX
//  passes du binaire, dans l'ordre : a5=1 (rangs 0..5, opaque + eau + alpha-test) puis a5=2
//  (rangs 6..7, z-write OFF + alpha-blend ON @0x6987f3/@0x698804). Les couches de rang < 0 (que
//  le binaire ne dessine par aucune boucle) n'existent plus : buildTerrain les écarte avant tout
//  upload GPU.
//
//  MISE À JOUR FRONT F_TERRAIN (B5, 2026-07-17) — CULL QUADTREE/FRUSTUM PAR FRAME FAIT : cullTerrain()
//  (appelée en tête de renderTerrain) remplit terrainBatch_/matCounter_ avec les seules faces visibles
//  via MapColl_CollectLeafFaces 0x694b50 + backface @0x698dd4 + Cam_FrustumTestSphere2x 0x69f0e0. Le
//  dessin par couche est passé de DrawIndexedPrimitive (VB statique, toutes faces) à DrawPrimitiveUP
//  (batch CPU, faces visibles) — miroir exact de Terrain_Render @0x698ff3. Mêmes faces visibles.
//
//  ÉCART CONNU, ASSUMÉ (eau dessinée une seule fois par couche) : les 3 boucles eau du binaire
//  testent `cat==3` SEUL, chacune gatée par l'existence d'un cat3/sub{0,1,2}. Si une zone possède
//  À LA FOIS du cat3/sub0 et du cat3/sub2, les DEUX gates passent et le binaire dessine CHAQUE
//  couche cat3 DEUX FOIS (opaque en a5=1, puis blendée en a5=2). Ici, chaque couche n'est dessinée
//  qu'une fois, au rang de son propre subOrder. Mesuré sur les 97 .WG réels : 57 zones ont de
//  l'eau — 14 en sub0 seul (-> rang 3, exact), 38 en sub2 seul (-> rang 7, exact), et seulement
//  5 zones mixtes (Z118/Z175/Z201/Z267/Z279) où le binaire double le dessin. Écart de 2e ordre,
//  limité à ces 5 zones ; le corriger exigerait de dissocier « gate » et « boucle » (hors mission).
// ===========================================================================
void WorldGeometryRenderer::renderTerrain(const D3DXMATRIX& view, const D3DXMATRIX& proj,
                                          const D3DXVECTOR3& eye) {
    if (!ready_ || terrainLayers_.empty()) return;

    // FRONT F_TERRAIN (B5) — CULL QUADTREE/FRUSTUM D'ABORD : remplit terrainBatch_ + matCounter_ avec
    // les seules faces visibles de cette frame (Terrain_Render collecte AVANT de dessiner, 0x698ce7).
    cullTerrain(view, proj, eye);

    // Sauvegarde des états qu'on modifie (device partagé avec meshRenderer_).
    DWORD prevCull = D3DCULL_CCW, prevLighting = TRUE, prevAlphaTest = FALSE,
          prevAlphaRef = 0, prevAlphaFunc = D3DCMP_ALWAYS;
    dev_->GetRenderState(D3DRS_CULLMODE, &prevCull);
    dev_->GetRenderState(D3DRS_LIGHTING, &prevLighting);
    dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &prevAlphaTest);
    dev_->GetRenderState(D3DRS_ALPHAREF, &prevAlphaRef);
    dev_->GetRenderState(D3DRS_ALPHAFUNC, &prevAlphaFunc);
    // Sous-passe a5=2 (rangs 6-7) + adressage sampler par couche : états supplémentaires à rendre.
    DWORD prevBlend = FALSE, prevZWrite = TRUE, prevSrc = D3DBLEND_ONE, prevDst = D3DBLEND_ZERO,
          prevAddrU = D3DTADDRESS_WRAP, prevAddrV = D3DTADDRESS_WRAP;
    dev_->GetRenderState(D3DRS_ALPHABLENDENABLE, &prevBlend);
    dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prevZWrite);
    dev_->GetRenderState(D3DRS_SRCBLEND, &prevSrc);
    dev_->GetRenderState(D3DRS_DESTBLEND, &prevDst);
    dev_->GetSamplerState(0, D3DSAMP_ADDRESSU, &prevAddrU);
    dev_->GetSamplerState(0, D3DSAMP_ADDRESSV, &prevAddrV);

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
    // Sous-passe a5=2 : bascule à SENS UNIQUE (les couches sont triées par rang croissant, donc les
    // rangs 6-7 sont forcément contigus en fin de liste). Ancres : ZWRITE off @0x6987f3 +
    // ALPHABLEND on @0x698804 à l'ouverture, restaurés @0x698b21 / @0x698b32 à la fermeture.
    bool blendPassOpen = false;

    // Dessin des couches, déjà triées par rang (passe a5=1 rangs 0..5, puis passe a5=2 rangs 6..7).
    for (const TerrainLayer& layer : terrainLayers_) {
        // OUVERTURE de la sous-passe transparente a5=2 (cat1/sub2 @0x698811, eau/sub2 @0x698a0a).
        // Le binaire y dessine ces couches avec z-write OFF et alpha-blend ON — l'ancien code les
        // envoyait en OPAQUE (rang fourre-tout 6), d'où un sol/eau transparents rendus solides.
        if (layer.rank >= kRank_FirstPass2 && !blendPassOpen) {
            dev_->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);       // (14,0) @0x6987f3
            dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);    // (27,1) @0x698804
            // Terrain_Render ne pose PAS SRCBLEND/DESTBLEND : il hérite de l'état permanent du
            // device, posé une fois par Gfx_InitDevice 0x69b9b0 -> (19=SRCBLEND, 5=SRCALPHA)
            // @0x69c526 et (20=DESTBLEND, 6=INVSRCALPHA) @0x69c535. On les repose explicitement
            // (valeurs PROUVÉES, pas un choix) car renderTerrain restaure les états en sortie.
            dev_->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
            dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
            blendPassOpen = true;
        }
        // FRONT FX-F4 (M5) : lightmap stage 1 UNIQUEMENT pour cat==2 et cat==4 (Terrain_Render
        // 0x698670 : enable @0x698f54 avant la boucle cat2 @0x698f97, disable @0x6990ba avant
        // cat1 @0x6990d4). Comme les couches sont triees par rang (cat2=0, cat4=1, puis cat1/eau/
        // alpha-test), cette bascule active la lightmap sur les premieres couches puis la coupe des
        // la 1ere couche non-cat2/cat4 -> equivalent fonctionnel exact des deux points IDA.
        const bool wantLightmap = (shadowTex_ != nullptr) &&
                                  (layer.category == 2 || layer.category == 4);
        if (wantLightmap && !lightmapBound) {
            // ENABLE -- calque @0x698f54 (COLOROP=MODULATE) + @0x698f68 (SetTexture stage 1 =
            // *(a1+72) lightmap). COLORARG1/2 + TEXCOORDINDEX=1 explicites : NON prouvés ici,
            // l'original les hérite de l'état permanent du device (ils ne sont posés ni par
            // Terrain_Render ni par Gfx_InitDevice 0x69b9b0) ; TEXCOORDINDEX=1 est requis pour uv1.
            // CORRECTION (Passe 4/W5) : l'ancien commentaire attribuait cet héritage à
            // « Terrain_PushRenderState 0x69cb80 » -- c'est FAUX. Malgré son nom, 0x69cb80 ne pousse
            // AUCUN état de rendu : c'est un TIMER (QueryPerformanceCounter -> this+208, renvoie
            // (now - this+224) / this+216 = secondes écoulées), appelé aussi par App_Init @0x46242e
            // et App_FrameTick @0x4625d9. Le nom IDA est trompeur, ne pas s'y fier.
            //
            // CORRECTION (Passe 4/W5b) : la version précédente de ce bloc prolongeait la rectification
            // ci-dessus par une chaîne causale INVENTÉE (« le retour du timer alimente v92, d'où
            // `v92 * 10.0` @0x6991ca -> ceci VALIDE le `wavePhase_ * 10.0f` »). IDA la contredit,
            // sur la fonction Terrain_Render 0x698670 ENTIÈRE (balayage 0x698670-0x699800) :
            //   - le retour du timer part dans un SLOT MORT : @0x6986b2 `fstp [esp+58h+var_48]`
            //     (var_48 = frame +0x3a0) est l'UNIQUE référence à var_48 -> 1 écriture / 0 lecture ;
            //     d'où le pseudocode Hex-Rays `Terrain_PushRenderState(g_GfxRenderer);` sans affectation ;
            //   - le `fmul ds:flt_7A8D74` @0x6991ca porte sur var_3C (frame +0x3ac), slot DISTINCT
            //     (12 octets d'écart), chargé @0x6991c1 `fld [esp+50h+var_3C]` ;
            //   - var_3C = 3 LECTURES (@0x698900 / @0x6991c1 / @0x6994e4, toutes « v92 * 10.0 ») et
            //     ZÉRO ÉCRITURE (lvar_usage : read=3 / write=0 / addr=0) ; aucune prise d'adresse de
            //     pile n'échappe (les seuls `lea …esp` sont des NOP `lea esp,[esp+0]` d'alignement),
            //     donc aucun appel ne peut l'écrire indirectement -> var_3C est lu NON INITIALISÉ.
            // Ce qui est RÉELLEMENT prouvé ici se limite à la CONSTANTE : flt_7A8D74 @0x7A8D74 =
            // octets `00 00 20 41` LE = 0x41200000 = 10.0f. L'OPÉRANDE, lui, n'a aucun écrivain :
            // identifier `wavePhase_` à des « secondes écoulées » est donc un choix build-safe
            // NON PROUVÉ (côté binaire c'est une lecture non initialisée, probable bug d'origine).
            // Le CODE reste inchangé (cf. `wavePhase_ * 10.0f` dans bindWaterStates) : seul ce
            // commentaire mentait. TODO [ancre 0x6991ca] : si un jour un dump runtime donne la vraie
            // valeur de var_3C, trancher la grandeur réelle plutôt que de la supposer temporelle.
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

        // ALPHA-TEST : piloté par le RANG, pas par subOrder. Terrain_Render n'active ALPHATESTENABLE
        // (@0x6993d4) + ALPHAREF=128 (@0x6993e9) qu'APRÈS les boucles cat2/cat4/cat1sub0/eau-sub0,
        // et ne le coupe qu'@0x699704 : seuls les rangs 4 (cat1/sub1 @0x69941b) et 5 (eau gate sub1
        // @0x6995e7) sont donc alpha-testés. L'ancien test `subOrder == 1` alpha-testait aussi
        // cat2/sub1 et cat4/sub1 (122 matériaux / ~39 800 faces réelles), dessinés en réalité par
        // les boucles cat2/cat4 AVANT toute activation de l'alpha-test. La passe a5=2 (rangs 6-7)
        // hérite d'un alpha-test coupé (@0x699704 en fin de passe a5=1) -> FALSE, correct ici aussi.
        const bool alphaTest = (layer.rank == kRank_Cat1Sub1 || layer.rank == kRank_Water1);
        dev_->SetRenderState(D3DRS_ALPHATESTENABLE, alphaTest ? TRUE : FALSE);
        if (alphaTest) {
            dev_->SetRenderState(D3DRS_ALPHAREF, 128);                    // (24,128) @0x6993e9
            // ALPHAFUNC n'est pas posé par Terrain_Render : état permanent du device =
            // (25=ALPHAFUNC, 5=D3DCMP_GREATEREQUAL) posé par Gfx_InitDevice @0x69c517.
            dev_->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATEREQUAL);
        }

        // ADRESSAGE UV du sampler 0 : CLAMP pour cat2 (@0x698f7b/@0x698f8e) et pour les passes eau
        // (@0x699195/@0x6991a8, @0x6994b8/@0x6994cb, @0x6988cf/@0x6988e3) ; WRAP partout ailleurs
        // (@0x699011/@0x699024 avant cat4, restauré @0x6993af/@0x6993c2 et @0x6996cf/@0x6996e2
        // après chaque passe eau ; WRAP est aussi le défaut du device, Gfx_InitDevice @0x69c49d/
        // @0x69c4ac). Absent jusqu'ici -> tuilage erroné des textures de sol.
        const bool water   = (layer.category == kTerrainCatWater);
        const bool clampUv = (layer.category == 2) || water;
        const DWORD addr   = clampUv ? D3DTADDRESS_CLAMP : D3DTADDRESS_WRAP; // 3 : 1
        dev_->SetSamplerState(0, D3DSAMP_ADDRESSU, addr);
        dev_->SetSamplerState(0, D3DSAMP_ADDRESSV, addr);

        if (water) bindWaterStates(layer.diffuse);
        else       dev_->SetTexture(0, layer.diffuse);

        // B5 — dessin du batch de la frame pour ce matériau : DrawPrimitiveUP(TRIANGLELIST,
        // matCounter_[m] triangles, &terrainBatch_[3*matBase_[m]], stride 40). Miroir exact de
        // Terrain_Render @0x698ff3 (device vtbl+332). matCounter_[m] == 0 -> matériau hors frustum
        // cette frame (rien à dessiner). PrimitiveCount = nb de faces visibles du matériau.
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

    // FERMETURE de la sous-passe a5=2 — calque @0x698b21 (27,0 = ALPHABLEND off) puis @0x698b32
    // (14,1 = ZWRITE on). Les états réels sont ensuite rendus à l'appelant ci-dessous.
    if (blendPassOpen) {
        dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        dev_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    }

    // Restauration : ne pas polluer meshRenderer_.
    dev_->SetTexture(1, nullptr);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);       // valeur FF par défaut
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
    meshRenderer_.InvalidateShaderBindingCache();          // le prochain DrawSkinnedSubset rebinde VS/PS
}

// Passe eau bump-env d'une couche cat==3. Ancre IDA : Terrain_Render @0x699206-0x6992b7. TWS-01 :
// la carte de perturbation liée au stage 0 est la texture de FALLOFF (falloffTex_, écrivain vivant
// MapColl_CreateFalloffTexture 0x694ca0 -> *(a1+20), lu @0x69928f), en BUMPENVMAPLUMINANCE(23) ; eau
// diffuse en stage 1 (MODULATE + ALPHAOP=SELECTARG1). Matrice bump animée : MAT00=cos(t)*s,
// MAT01=-sin(t)*s, MAT10=sin(t)*s, MAT11=cos(t)*s (t = wavePhase_*10), BUMPENVLSCALE=1.0.
void WorldGeometryRenderer::bindWaterStates(IDirect3DTexture9* waterDiffuse) {
    if (!falloffTex_) { dev_->SetTexture(0, waterDiffuse); return; } // repli : eau texture simple
    // ÉCHELLE DU BUMP — TODO ancre 0x699206 RÉSOLU (2026-07-16), volontairement NON appliqué :
    // le binaire utilise `a10` BRUT comme échelle de la matrice bump-env (vérifié @0x6991e5
    // `v94 = cos(v62) * a10` / @0x6991f2 `sin(v109) * a10`, idem @0x699508 et @0x698925), où
    // a10 = Game_GetTierRange 0x5402f0 = la DISTANCE DE TIRAGE (1000/2000/3000 selon
    // g_Opt_DisplayRangeTier 0x84DEC4). Passer une distance de tirage dans une matrice bump-env
    // est très probablement un BUG D'ORIGINE (mauvaise variable passée) : à 1000+, la
    // perturbation sature et l'eau devient du bruit. RÈGLE DE FIDÉLITÉ vs jouabilité : la
    // valeur exacte est ici DOCUMENTÉE mais pas reproduite -- renderTerrain ne reçoit pas la
    // distance de tirage et g_Opt_DisplayRangeTier n'est pas câblé. kWaterBumpScale reste un
    // choix de rendu build-safe. Arbitrage à l'orchestrateur si la reproduction du bug est voulue.
    constexpr float kWaterBumpScale = 0.05f;
    const float t = wavePhase_ * 10.0f;
    const float c = std::cos(t) * kWaterBumpScale;
    const float s = std::sin(t) * kWaterBumpScale;
    dev_->SetTexture(0, falloffTex_); // TWS-01 : *(a1+20) @0x69928f = falloff, PAS une texture de vagues
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
//  world=identité, sommets 40o groupés par materialIndex, texture diffuse par matériau).
//  FRONT F_TERRAIN (B5, 2026-07-17) : le cull quadtree/frustum PAR FRAME est FAIT (cullTerrain() :
//  MapColl_CollectLeafFaces 0x694b50 + backface @0x698dd4 + Cam_FrustumTestSphere2x 0x69f0e0 ->
//  batch CPU par matériau -> DrawPrimitiveUP @0x698ff3) ; mêmes faces visibles, moins de triangles.
//  FRONT F_TERRAIN (B6, 2026-07-17) : les props .WO rejouent leur SWAY (renderObjects(), swap de
//  frame par offset de stream). Les objets .WO ne flottent plus sans sol.
//  G6 eau (catégorie 3, bump-env) et G8 lightmap .SHADOW (stage 1, uv1) sont désormais FAITS dans
//  ce front (cf. bindWaterStates() @0x699206 : bump-map = falloff MapColl_CreateFalloffTexture
//  0x694ca0 ; lightmap stage 1 MODULATE @0x698f54/@0x698f68) : la catégorie vient de
//  textures[m].trailer[0] et le vertex FF possède bien uv1. TWS-01 (W11) : la « texture de vagues »
//  0x451220 est du CODE MORT (2 appelants à 0 xref), le binaire ne la crée jamais -> non portée.
//  Restes = échelle exacte du bump eau (a10 runtime, TODO ancre 0x699206), purement visuel.
// ===========================================================================

void WorldGeometryRenderer::Render(const Camera& camera, int screenW, int screenH) {
    if (!ready_) return;
    // Depuis B5/B6, le terrain ET les props .WO passent par un chemin FIXED-FUNCTION natif (plus de
    // DrawSkinnedSubset) : meshRenderer_ n'est plus utilisé pour dessiner. On conserve néanmoins cet
    // appel (no-op défensif) pour garder cohérent le cache VS/PS de meshRenderer_ vis-à-vis des pokes
    // SetVertexShader(nullptr)/SetPixelShader(nullptr) posés par SkyRenderer::Render() sur le device
    // partagé (cf. Gfx/MeshRenderer.h::InvalidateShaderBindingCache()) — sûr si un futur chemin skinné
    // réapparaît.
    meshRenderer_.InvalidateShaderBindingCache();

    // Caméra : partagée par le sol (B5) et les props .WO. view/proj + œil monde (backface du cull
    // terrain, = g_CameraPos dans l'original, ancre 0x698dd4).
    D3DXMATRIX view, proj;
    camera.BuildViewMatrix(view);
    const float aspect = (screenH > 0)
        ? static_cast<float>(screenW) / static_cast<float>(screenH)
        : 1.0f;
    camera.BuildProjMatrix(proj, aspect);
    const D3DXVECTOR3 eye = camera.Eye();

    // FRONT W3-F3 : le SOL .WG d'abord (fidèle à Scene_InGameRender : terrain avant props). FF, avec
    // les MÊMES view/proj que les props. Dessiné même sans objet .WO dans la zone. B5 : cull par frame.
    renderTerrain(view, proj, eye);

    // FRONT F_TERRAIN (B6) : props .WO avec REJEU DU SWAY (frame par instance via l'offset de stream).
    // FRONT C2 : `eye` (g_CameraPos) transmis -> alimente le MeshPartRuntime de la couche matériau B1.
    renderObjects(view, proj, eye);

    // FRONT W3-F3 : FX de zone .WP APRÈS les props (passe a5=2 @0x698c6d : blend actif, depth-write
    // off). C'est le point d'entrée de rendu .WP réel (corrige WorldIntegration « non identifié »).
    RenderFxBillboards(camera);
}

// ===========================================================================
//  FRONT F_TERRAIN (B6) + FRONT C2 (2026-07-17) — dessin des props .WO en FIXED-FUNCTION natif, avec
//  REJEU DU SWAY et COUCHE MATÉRIAU COMPLÈTE.
//  Placement/sway reproduits d'après Model_RenderWithShadow_0 0x6a4110 (le vrai chemin matériau des
//  props ; Model_RenderParts 0x6a3720 est la variante simple sans matériau) :
//    - FVF 274 = 0x112 (XYZ|NORMAL|TEX1, stride 32) : SetFVF(274) @0x6a4186 ;
//    - matrice monde PAR INSTANCE = Rz*Ry*Rx*T (SetTransform(256,m) @0x6a4299) ;
//    - frame = Crt_Dbl2Uint(animPhase) @0x6a4148, GATE [0, A-1] avec A = MeshPart[0]+252 @0x6a415e ;
//    - SWAP : SetStreamSource(0, vb, 32*frame*B, 32) @0x6aeea5 (B = a1[64]/+256) ; SetIndices(+292)
//      @0x6aeedc ; DrawIndexedPrimitive(TRIANGLELIST, 0,0, B, 0, D) @0x6aef00 (D = a1[66]/+264).
//  Le VB porte les A frames contiguës (uploadPart) : changer de frame = décaler l'offset, sans
//  ré-upload. animPhase est avancé par TickWorldAnim (déjà appelé par SceneManager, wrap [0,A)).
//
//  FRONT C2 — CÂBLAGE B1 : pour chaque part `mat.decoded`, le base-draw est REMPLACÉ par la machine à
//  états matériau MeshPartMaterialRenderer::Render (MeshPart_RenderFull 0x6b0850), qui ré-inclut le
//  base-draw (32*frame*B). Ce que la couche matériau APPORTE désormais (couches NON-lit, actives sous
//  LIGHTING=FALSE) : flipbook de texture (atlas materials[], @0x6b0d33), UV-scroll tex1/tex2
//  (@0x6b0f59/@0x6b19bb), 2e texture en 2e passe blendée (@0x6b19ad), et modes alpha-test/blend de la
//  base ET de la 2e texture (a1[85]/a1[98]). Les 4 arguments de contexte suivent les défauts « chemin
//  FX » de B1 : frame=frame de morph, animTime=v66 (repli wavePhase_), glowEnable=1, alphaFade=0,
//  decal=nullptr (cf. corps + bandeau .h). mat.decoded==false conserve exactement le base-draw B6.
//
//  ÉCARTS ASSUMÉS (build-safe, documentés) :
//   - LIGHTING=FALSE conservé (préserve le rendu texturé actuel, pas de noir faute des lumières/ambient
//     globaux de l'original) : les couches LIT de B1 (émissif @0x6b08a5, glow spéculaire @0x6b0a11,
//     proj-light) sont posées & restaurées mais INERTES. sunDir/sceneCenter indisponibles -> (0,0,0)
//     (le fresnel du glow mode 2 retombe donc à 0 même sous lighting). Écart de fidélité PRÉ-EXISTANT
//     (avant C2 il n'y avait aucun matériau), pas une régression.
//   - Billboard face-caméra (mat.billboard.Enable) : B1 fait un repli draw-indexé (axes flt_8001D4/
//     unk_80022C runtime non prouvés) — bloc nœuds 64*A non retenu (geo.frameNodes=nullptr).
//   - CULLMODE=NONE : le binaire hérite du cull matériel du device (winding MOBJ non re-vérifié ici)
//     -> NONE garantit que les props restent visibles (pas de régression), au prix d'un léger overdraw.
//   - Cull distance + fondu alpha des .WO lointains (Terrain_Render 0x69977c/0x69979f, via
//     Model_RenderWithShadow_0 0x6a4110 args a6/a8/a9) : NON reproduit ici (valeurs du site d'appel
//     Scene_InGameRender non dumpables en statique — cf. TS2_EXTRACT_TERRAIN_CULL.md §10). C'est aussi
//     pourquoi glow/alphaFade prennent les défauts « chemin FX » plutôt que les vrais a8/a9.
// ===========================================================================
void WorldGeometryRenderer::renderObjects(const D3DXMATRIX& view, const D3DXMATRIX& proj,
                                          const D3DXVECTOR3& eye) {
    if (!ready_ || objects_.empty() || instances_.empty()) return;

    // --- Sauvegarde des états PERTURBÉS (device PARTAGÉ avec meshRenderer_ / voisins FX/ombre/bloom).
    //     FRONT C2 : B1 MeshPart_RenderFull 0x6B0850 pilote une machine à états matériau
    //     (blend/z-write/alpha-test/alpharef/alphafunc/texturefactor/specular + matrice de texture)
    //     qu'il restaure à un BASELINE FIXE, pas à l'état d'entrée -> on sauve l'ensemble ici et on le
    //     rend intégralement en sortie pour ne rien polluer. ---
    DWORD prevLighting = TRUE, prevCull = D3DCULL_CCW, prevBlend = FALSE, prevZWrite = TRUE,
          prevAlphaTest = FALSE, prevAlphaRef = 0, prevAlphaFunc = D3DCMP_GREATEREQUAL,
          prevSrc = D3DBLEND_SRCALPHA, prevDst = D3DBLEND_INVSRCALPHA, prevTexFactor = 0xFFFFFFFF,
          prevSpecular = FALSE;
    dev_->GetRenderState(D3DRS_LIGHTING, &prevLighting);
    dev_->GetRenderState(D3DRS_CULLMODE, &prevCull);
    dev_->GetRenderState(D3DRS_ALPHABLENDENABLE, &prevBlend);
    dev_->GetRenderState(D3DRS_ZWRITEENABLE, &prevZWrite);
    dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &prevAlphaTest);
    dev_->GetRenderState(D3DRS_ALPHAREF, &prevAlphaRef);
    dev_->GetRenderState(D3DRS_ALPHAFUNC, &prevAlphaFunc);
    dev_->GetRenderState(D3DRS_SRCBLEND, &prevSrc);
    dev_->GetRenderState(D3DRS_DESTBLEND, &prevDst);
    dev_->GetRenderState(D3DRS_TEXTUREFACTOR, &prevTexFactor);
    dev_->GetRenderState(D3DRS_SPECULARENABLE, &prevSpecular);

    // Pré-condition device commune (fidèle à Model_RenderWithShadow_0 0x6a4110 : SetFVF(274) @0x6a4186,
    // pas de shader). VS/PS coupés -> le cache de bind de meshRenderer_ est invalidé en sortie.
    dev_->SetVertexShader(nullptr);
    dev_->SetPixelShader(nullptr);
    dev_->SetFVF(kFvfWo);                                 // 274 (0x112) @0x6a4186
    dev_->SetTransform(D3DTS_VIEW, &view);
    dev_->SetTransform(D3DTS_PROJECTION, &proj);
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);          // FF UNLIT : cf. bandeau — les couches LIT de B1
                                                          // (émissif/glow/proj-light) restent INERTES ; on
                                                          // préserve le rendu texturé actuel (pas de noir).
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);   // build-safe (cf. bandeau)

    // BASELINE EXACT vers lequel B1 restaure (restoreBlendTriplet 0x6b1440/0x6b1ce6 : blend off, z-write
    // on, alpha-test off, ALPHAREF=0, ALPHAFUNC=GREATER(5), TEXTUREFACTOR=-1, SPECULAR off). Posé AVANT
    // la boucle pour que parts décodées (B1) et non décodées (base-draw) s'entrelacent sur le MÊME état
    // de départ. B1 active le blend SANS reposer les facteurs -> on pose SRCALPHA/INVSRCALPHA (état
    // permanent du device, Gfx_InitDevice @0x69c526/@0x69c535, valeurs PROUVÉES).
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    dev_->SetRenderState(D3DRS_ALPHAREF, 0);
    dev_->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);       // 25,5 (baseline B1)
    dev_->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev_->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);       // 60,-1 (baseline B1)
    dev_->SetRenderState(D3DRS_SPECULARENABLE, FALSE);

    // Stage 0 : couleur = texture (SELECTARG1) sur uv0 ; alpha = texture (requis pour alpha-test/blend
    // de B1). ALPHAARG2=CURRENT + TTFF=DISABLE = baseline de restauration de B1.
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,  D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,  D3DTOP_SELECTARG1); // 4,2 (baseline B1)
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);    // 6,1 (baseline B1)
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE); // baseline UV-scroll
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    D3DXMATRIX texIdent; D3DXMatrixIdentity(&texIdent);
    dev_->SetTransform(D3DTS_TEXTURE0, &texIdent);        // baseline matrice texture stage 0

    // animTime = v66 (MeshPart_RenderFull @0x6b0883 : Terrain_PushRenderState() + a3) : horloge des
    // couches animées de B1 (flipbook @0x6b0d78, UV-scroll @0x6b1016, ping-pong @0x6b08bb). REPLI
    // DOCUMENTÉ : la phase a3 du site d'appel (Terrain_Render -> Model_RenderWithShadow_0 0x6a4110,
    // arg a6) n'est pas dumpable en statique -> on n'utilise que la part QPC-secondes, approximée par
    // wavePhase_ (accumulateur dt secondes de TickWorldAnim). GLOBAL (pas par instance), comme le v66 QPC.
    const float animTime = wavePhase_;

    // cameraAt (cible caméra +804..812) : REPLI depuis le forward de la matrice vue (LH row-vector,
    // colonne 3 = direction de visée monde ; at = eye + forward). N'alimente que Gfx_SetShadowProjLight
    // (glow, couche LIT inerte sous LIGHTING=FALSE) -> valeur raisonnable, effet nul. sunDir/sceneCenter
    // indisponibles ici -> laissés à (0,0,0) : ils ne pilotent QUE les couches LIT (glow/émissif) inertes.
    const D3DXVECTOR3 camAt(eye.x + view._13, eye.y + view._23, eye.z + view._33);

    for (size_t idx = 0; idx < instances_.size(); ++idx) {
        const asset::AuxRecord& inst = instances_[idx];
        if (inst.modelIndex >= modelRanges_.size()) continue; // modelIndex corrompu, cf. log Build()
        const ModelRange& range = modelRanges_[inst.modelIndex];
        if (range.count == 0) continue;                       // gabarit sans part uploadée

        // frame = Crt_Dbl2Uint(animPhase), gate [0, A-1] (Model_RenderWithShadow_0 0x6a4148/0x6a415e ;
        // Model_RenderParts 0x6a3739/0x6a3756). instancePhase_ est wrap [0,A) par TickWorldAnim ->
        // Dbl2Uint (troncature vers 0) donne [0,A-1].
        const uint32_t A     = (idx < instanceFrameCount_.size() && instanceFrameCount_[idx] > 0)
                                   ? instanceFrameCount_[idx] : 1u;
        const float    phase = (idx < instancePhase_.size()) ? instancePhase_[idx] : 0.0f;
        uint32_t frame = (phase > 0.0f) ? static_cast<uint32_t>(phase) : 0u; // Crt_Dbl2Uint (troncature)
        if (frame > A - 1) frame = A - 1;                     // gate (borne haute)

        const D3DXMATRIX world = BuildInstanceWorldMatrix(inst);
        dev_->SetTransform(D3DTS_WORLD, &world);              // SetTransform(256, m) @0x6a4299 (PRÉ-COND B1)

        for (size_t i = 0; i < range.count; ++i) {
            const StaticObject& obj = objects_[range.start + i];
            if (!obj.vb || !obj.ib || obj.B == 0 || obj.D == 0) continue;

            if (obj.mat.decoded) {
                // ===== FRONT C2 — COUCHE MATÉRIAU COMPLÈTE via B1 (remplace le base-draw). =====
                // Chemin réel : Model_RenderWithShadow_0 0x6a4110 -> MeshPart_RenderFull 0x6b0850.
                // MeshPartGpu : A/B/D depuis la géométrie WO (a1[63]/[64]/[66]) + VB (A frames) / IB.
                MeshPartGpu geo;
                geo.vb            = obj.vb;
                geo.ib            = obj.ib;
                geo.vertsPerFrame = obj.B;   // a1[64]/+256 (numVerts du DrawIndexedPrimitive)
                geo.triCount      = obj.D;   // a1[66]/+264 (primCount)
                geo.frameCount    = obj.A;   // a1[63]/+252 (B1 re-borne la frame sur A)
                geo.frameNodes    = nullptr; // bloc nœuds 64*A (a1[71]/+284) NON retenu : il n'alimente
                                             // que le glow fresnel (mode 2) et le billboard — inertes ici
                                             // (glow sous LIGHTING=FALSE + sunDir=(0,0,0) ; billboard =
                                             // repli draw indexé sans lecture de nœud). nullptr = saut sûr.

                // MeshPartTextures : base=tex1, second=tex2, flipbook=materials[] (déjà uploadés).
                MeshPartTextures tex;
                tex.base          = obj.diffuse;     // a1[86]/+344
                tex.baseMode      = obj.baseMode;    // a1[85]/+340
                tex.second        = obj.second;      // a1[99]/+396
                tex.secondMode    = obj.secondMode;  // a1[98]/+392
                tex.flipbook      = obj.flipbook.empty() ? nullptr : obj.flipbook.data(); // a1[101]/+404
                tex.flipbookCount = static_cast<uint32_t>(obj.flipbook.size());           // a1[100]/+400

                // MeshPartRuntime depuis la caméra/scène RÉELLES (repli documenté pour sunDir/sceneCenter).
                MeshPartRuntime rt;
                rt.world      = world;   // dword_800244 (déjà posé en D3DTS_WORLD ci-dessus)
                rt.worldValid = true;
                rt.cameraEye  = eye;     // g_CameraPos 0x800130
                rt.cameraAt   = camAt;   // cible caméra (repli forward de la vue, cf. ci-dessus)
                // rt.sunDir / rt.sceneCenter : (0,0,0) par défaut (indisponibles ; couches LIT inertes).

                // frame = frame de morph (a2) ; animTime = v66 (a3). glowEnable=1 / alphaFade=0 /
                // decal=nullptr = défauts « chemin FX » DOCUMENTÉS par B1 : les vrais a8/a9/a6 du site
                // d'appel Terrain_Render ne sont pas dumpables en statique (cf. bandeau .h renderObjects).
                MeshPartMaterialRenderer::Render(dev_, obj.mat, geo, tex,
                                                 static_cast<int>(frame), animTime, rt);
            } else {
                // ===== mat.decoded == false : BASE-DRAW ACTUEL PRÉSERVÉ (inchangé). =====
                // Ancre IDA : MeshPart_Render 0x6aed60 (SetStreamSource 0x6aeea5 / SetIndices 0x6aeedc /
                // DrawIndexedPrimitive 0x6aef00). Swap de frame par offset de stream = 32*frame*B.
                // Borne défensive : si A n'est pas uniforme entre parts, ne pas dépasser le VB de la part.
                const uint32_t pf = (frame < obj.A) ? frame : (obj.A - 1);
                dev_->SetTexture(0, obj.diffuse);
                dev_->SetStreamSource(0, obj.vb, 32u * pf * obj.B, 32u);                 // 0x6aeea5
                dev_->SetIndices(obj.ib);                                                // 0x6aeedc
                dev_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, obj.B, 0, obj.D);   // 0x6aef00
            }
        }
    }

    // Restauration : baseline B1 -> état d'entrée RÉEL (ne pas polluer FX/ombre/bloom voisins).
    dev_->SetTexture(0, nullptr);
    dev_->SetTexture(1, nullptr);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP, D3DTOP_DISABLE);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);            // valeur FF par défaut
    dev_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev_->SetTransform(D3DTS_TEXTURE0, &texIdent);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, prevBlend);
    dev_->SetRenderState(D3DRS_ZWRITEENABLE, prevZWrite);
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, prevAlphaTest);
    dev_->SetRenderState(D3DRS_ALPHAREF, prevAlphaRef);
    dev_->SetRenderState(D3DRS_ALPHAFUNC, prevAlphaFunc);
    dev_->SetRenderState(D3DRS_SRCBLEND, prevSrc);
    dev_->SetRenderState(D3DRS_DESTBLEND, prevDst);
    dev_->SetRenderState(D3DRS_TEXTUREFACTOR, prevTexFactor);
    dev_->SetRenderState(D3DRS_SPECULARENABLE, prevSpecular);
    dev_->SetRenderState(D3DRS_LIGHTING, prevLighting);
    dev_->SetRenderState(D3DRS_CULLMODE, prevCull);
    dev_->LightEnable(1, FALSE); // B1 peut activer la lumière slot 1 (glow) ; garantir OFF en sortie
    meshRenderer_.InvalidateShaderBindingCache();          // le prochain draw skinné rebinde VS/PS
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
    if (!ready_ || fxPools_.empty()) return;

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

    // Base camera-facing depuis la matrice vue (right/up monde = colonnes de la rotation). Fidèle au
    // rôle des globales flt_8001D4..8001E8 du binaire (droite/haut caméra monde), lues et multipliées
    // par particle.size dans Particle_RenderBillboards 0x6a70b0.
    D3DXMATRIX view; camera.BuildViewMatrix(view);
    D3DXVECTOR3 right(view._11, view._21, view._31);
    D3DXVECTOR3 up(view._12, view._22, view._32);
    D3DXVec3Normalize(&right, &right);
    D3DXVec3Normalize(&up, &up);

    // Paramètres de frame communs à tous les émetteurs (le device, la base et le scratch CPU sont
    // partagés — dword_800080 dans le binaire). maxQuads=0 => pas de plafond (dword_7FFEE0 vaut 0 en
    // statique ; posé au runtime par le renderer -> non prouvé, 0 conservé = pas de coupe).
    ts2::gfx::ZoneFxFrameParams params;
    params.device   = dev_;
    params.right[0] = right.x; params.right[1] = right.y; params.right[2] = right.z;
    params.up[0]    = up.x;    params.up[1]    = up.y;    params.up[2]    = up.z;
    params.maxQuads = 0;                // dword_7FFEE0 (0 statique)
    params.scratch  = &fxScratch_;      // buffer CPU des sommets (réutilisé), DrawPrimitiveUP
    params.frustum  = nullptr;          // Cam_FrustumTestPoint6 non câblé ici (cull distance ci-dessous)

    // Cull DISTANCE CARRÉE par nœud (Terrain_Render a5=2 @0x698c81-0x698cbb : dist²(eye, FxNode.pos)
    // < a10²). a10 = Game_GetTierRange 0x5402f0 (option d'affichage 1000/2000/3000 selon
    // g_Opt_DisplayRangeTier 0x84dec4, = 0 en statique -> valeur RUNTIME non dumpable). Seuil build-safe
    // documenté = tier LOINTAIN 3000.0 (la plus permissive des 3 valeurs prouvées) pour ne rien masquer ;
    // à câbler sur l'option réelle quand la config sera portée. Origine cull = record B pos (FxNode.pos).
    const D3DXVECTOR3 eye = camera.Eye();
    constexpr float kZoneFxDrawRange = 3000.0f;                 // Game_GetTierRange (tier 3, documenté)
    const float range2 = kZoneFxDrawRange * kZoneFxDrawRange;
    for (size_t i = 0; i < fxPools_.size(); ++i) {
        ts2::gfx::FxParticlePool& pool = fxPools_[i];
        if (!pool.flag) continue;                              // pas encore Init -> aucun pool à dessiner
        const float dx = eye.x - fxRecords_[i].pos[0];
        const float dy = eye.y - fxRecords_[i].pos[1];
        const float dz = eye.z - fxRecords_[i].pos[2];
        if (dx * dx + dy * dy + dz * dz >= range2) continue;   // hors portée -> saute le nœud (@0x698cbb)
        ts2::gfx::ZoneFx_RenderBillboards(&pool, params);      // Particle_RenderBillboards 0x6a70b0
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
    // gabarit). Ancre IDA : MapColl_UpdateObjectAnim @0x694a30/@0x694a4a. B6 (2026-07-17) : la phase
    // est désormais CONSOMMÉE au dessin par renderObjects() -> frame = Crt_Dbl2Uint(instancePhase_[i])
    // -> SetStreamSource(0, vb, 32*frame*B, 32) (MeshPart_Render 0x6aeea5). Plus de pose figée.
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

    // FRONT F_ZONEFX — sim des particules .WP : miroir EXACT de la boucle 2 de MapColl_UpdateObjectAnim
    // @0x694af0 (stride 76) : si le POBJECT est déjà initialisé (flag != 0) -> Particle_UpdateEmit avec
    // (dt=a3, pos=FxNode+4, rot=FxNode+16) ; sinon -> Particle_Init(POBJECT, template = this+29[nodeIndex]).
    // dt = a3 du binaire = VRAI delta de frame (call site Scene_InGameUpdate @0x52c94b : a2=15.0, a3=dt).
    // Frustum d'émission non câblé (nullptr) : le cull Cam_FrustumTestPoint6 est une optimisation
    // perf/durée-de-vie ; l'omettre garde le visuel à l'écran identique (les émetteurs hors-champ
    // continuent de simuler — sans effet observable). Ancre : MapColl_UpdateObjectAnim 0x694a00.
    for (size_t i = 0; i < fxPools_.size(); ++i) {
        ts2::gfx::FxParticlePool& pool = fxPools_[i];
        const uint32_t node = fxRecords_[i].nodeIndex;
        if (pool.flag) {                                        // if(*(v11+v12+28)) @0x694af6
            ts2::gfx::ZoneFx_UpdateEmit(&pool, dt, fxRecords_[i].pos, fxRecords_[i].rot, nullptr); // @0x694b2e
        } else if (node < fxTemplates_.size()) {
            ts2::gfx::ZoneFx_Init(&pool, &fxTemplates_[node]); // template = this+29[nodeIndex] @0x694b13
        }
    }
}

} // namespace ts2::gfx

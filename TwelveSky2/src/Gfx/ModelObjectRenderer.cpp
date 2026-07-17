// Gfx/ModelObjectRenderer.cpp — voir ModelObjectRenderer.h pour la carte complète (chaîne
// d'origine, banque MiscC, tranche minimale, dégradations assumées). Vérité IDA : TwelveSky2.exe
// (idaTs2, imagebase 0x400000). Chaque bloc porte son ancre.
#include "Gfx/ModelObjectRenderer.h"
#include "Core/Log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

template <class T>
void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// FVF fixed-function d'un vertex MOBJECT (MeshPart_Load 0x6AD160 : sommet 32 o) :
// 0x112 = 274 = D3DFVF_XYZ|NORMAL|TEX1. IDENTIQUE au .WO (kFvfWo, Model_RenderParts 0x6a377f).
constexpr DWORD kFvfMobj = D3DFVF_XYZ | D3DFVF_NORMAL | D3DFVF_TEX1; // 0x112
static_assert(kFvfMobj == 0x112, "FVF MOBJECT doit valoir 274 (0x112)");

// Strides disque (Docs/TS2_ASSET_FORMATS.md ; MeshPart_Load) : sommet 32 o, index16 (6 o/face).
constexpr size_t kMobjVertexStride = 32;
constexpr size_t kFaceStride       = 6;   // 3 index * 2 o

// Nombre de slots de la banque MiscC (unk_B60AB8) : boucle AssetMgr_InitAllSlots 0x4DEB50
// @0x4dee8c `for(i6 = 0; i6 < 246; ++i6)`.
constexpr int kMiscCSlotCount = 246;

// Même convention de jointure que Gfx/ModelCache.cpp::JoinPath / Game/GameDatabase.cpp (sép. '\').
std::string JoinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    const char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "\\" + b;
}

// ---------------------------------------------------------------------------------------------
//  Frustum reconstruit depuis vp = view*proj (Gribb-Hartmann), plans RENTRANTS et NORMALISÉS.
//  Le binaire lit ses 6 plans à g_GfxRenderer+334 (cf. décompilation Cam_FrustumTestSphere
//  0x69EF90) ; ClientSource ne dispose pas de ce singleton -> on les RECONSTRUIT, ce qui donne
//  les MÊMES plans (repère monde, dedans <=> a*x+b*y+c*z+d >= 0). Plans normalisés obligatoires :
//  la marge -rayon du test sphère est en unités monde. (Miroir de WorldGeometryRenderer::
//  ExtractFrustum, mais marge ×1 = Cam_FrustumTestSphere au lieu de la ×2 du monde.)
// ---------------------------------------------------------------------------------------------
void ExtractFrustumPlanes(const D3DXMATRIX& vp, float pl[6][4]) {
    // Gauche=col3+col0, Droite=col3-col0, Bas=col3+col1, Haut=col3-col1, Proche=col2, Loin=col3-col2.
    pl[0][0]=vp._14+vp._11; pl[0][1]=vp._24+vp._21; pl[0][2]=vp._34+vp._31; pl[0][3]=vp._44+vp._41; // gauche
    pl[1][0]=vp._14-vp._11; pl[1][1]=vp._24-vp._21; pl[1][2]=vp._34-vp._31; pl[1][3]=vp._44-vp._41; // droite
    pl[2][0]=vp._14+vp._12; pl[2][1]=vp._24+vp._22; pl[2][2]=vp._34+vp._32; pl[2][3]=vp._44+vp._42; // bas
    pl[3][0]=vp._14-vp._12; pl[3][1]=vp._24-vp._22; pl[3][2]=vp._34-vp._32; pl[3][3]=vp._44-vp._42; // haut
    pl[4][0]=vp._13;        pl[4][1]=vp._23;        pl[4][2]=vp._33;        pl[4][3]=vp._43;        // proche
    pl[5][0]=vp._14-vp._13; pl[5][1]=vp._24-vp._23; pl[5][2]=vp._34-vp._33; pl[5][3]=vp._44-vp._43; // loin
    for (int p = 0; p < 6; ++p) {
        const float len = std::sqrt(pl[p][0]*pl[p][0] + pl[p][1]*pl[p][1] + pl[p][2]*pl[p][2]);
        if (len > 1e-8f) { const float inv = 1.0f / len; for (int k = 0; k < 4; ++k) pl[p][k] *= inv; }
    }
}

// ---------------------------------------------------------------------------------------------
//  Renderer actif du shim de hook (FxModelObjDrawFn est un pointeur de fonction LIBRE : il ne
//  peut pas capturer d'instance -> on garde l'instance active dans une variable de fichier, comme
//  FxRenderer.cpp le fait pour s_fxDevice/s_particleRender). Écrivain unique : Init/Shutdown.
// ---------------------------------------------------------------------------------------------
ModelObjectRenderer* s_active = nullptr;

} // namespace

// ===========================================================================
//  Init / Shutdown
// ===========================================================================

ModelObjectRenderer::~ModelObjectRenderer() { Shutdown(); }

bool ModelObjectRenderer::Init(Renderer& renderer, std::string gameDataDir) {
    dev_ = renderer.Device();
    if (!dev_) { TS2_ERR("ModelObjectRenderer::Init : device nul"); return false; }
    gameDataDir_ = std::move(gameDataDir);
    ready_       = true;
    frameValid_  = false;
    s_active     = this; // enregistre pour le shim de hook (ModelObjectRenderer_MeshDrawShim)
    TS2_LOG("ModelObjectRenderer pret (banque MiscC E*.MOBJECT, %d slots).", kMiscCSlotCount);
    return true;
}

void ModelObjectRenderer::Shutdown() {
    if (s_active == this) s_active = nullptr;
    releaseAll();
    dev_        = nullptr;
    ready_      = false;
    frameValid_ = false;
}

// D3DPOOL_MANAGED : les VB/IB/textures du cache survivent à un Reset() (restaurés par D3D9).
// No-op, comme WorldGeometryRenderer. NOTE : sur une RECRÉATION complète du device (dev_ change),
// MAIN doit appeler Shutdown()+Init() pour purger le cache (resources liées à l'ancien device).
void ModelObjectRenderer::OnDeviceLost() {}
void ModelObjectRenderer::OnDeviceReset() {}

void ModelObjectRenderer::releaseEntry(MObjEntry& e) {
    for (GpuPart& p : e.parts) {
        SafeRelease(p.vb); SafeRelease(p.ib);
        SafeRelease(p.diffuse); SafeRelease(p.second);      // tex0 + tex1 possédées
        for (IDirect3DTexture9* ft : p.flipbook) SafeRelease(ft); // atlas flipbook possédé
        p.flipbook.clear();
    }
    e.parts.clear();
}

void ModelObjectRenderer::releaseAll() {
    for (auto& kv : cacheMiscC_) releaseEntry(kv.second);
    cacheMiscC_.clear();
}

// ===========================================================================
//  SetFrame — plans du frustum de la frame (pour le cull par-part).
// ===========================================================================
void ModelObjectRenderer::SetFrame(const D3DXMATRIX& view, const D3DXMATRIX& proj) {
    D3DXMATRIX vp;
    D3DXMatrixMultiply(&vp, &view, &proj);
    ExtractFrustumPlanes(vp, planes_);

    // Œil/cible caméra monde pour MeshPartRuntime (glow fresnel @0x6b0a90 + direction proj-light
    // = cible - œil @0x69d8ae). g_GfxRenderer 0x7FFE18 (œil g_CameraPos 0x800130, cible +804) est
    // absent -> on les DÉRIVE de la matrice vue (source RÉELLE de la frame, aucune invention) :
    //   œil = inverse(view) translation ; forward monde = (view._13, view._23, view._33) = zaxis
    //   (D3DXMatrixLookAtLH) ; cible = œil + forward. Direction proj-light = cible - œil = forward.
    D3DXMATRIX invView;
    if (D3DXMatrixInverse(&invView, nullptr, &view)) {
        cameraEye_ = D3DXVECTOR3(invView._41, invView._42, invView._43);
        cameraAt_  = D3DXVECTOR3(cameraEye_.x + view._13, cameraEye_.y + view._23, cameraEye_.z + view._33);
    }
    frameValid_ = true;
}

// Horloge d'animation matériau (v66) — timer QPC, secondes écoulées depuis le 1er appel (origine
// locale = phase relative). Miroir Terrain_PushRenderState 0x69CB80 ((now - start)/freq, cf.
// WorldGeometryRenderer.cpp) et EmitterMeshRenderer::ElapsedSeconds (QPC @0x430C16/@0x430C34).
float ModelObjectRenderer::animClockSeconds() {
    LARGE_INTEGER now; QueryPerformanceCounter(&now);
    if (!qpcInit_) {
        LARGE_INTEGER f; QueryPerformanceFrequency(&f);
        qpcFreq_  = (f.QuadPart != 0) ? f.QuadPart : 1;
        qpcStart_ = now.QuadPart;
        qpcInit_  = true;
    }
    return static_cast<float>(double(now.QuadPart - qpcStart_) / double(qpcFreq_));
}

// Cam_FrustumTestSphere 0x69EF90 : sphère gardée SSI plane·c + d >= -rayon pour les 6 plans
// (marge ×1 : `v4 = a3 * -1.0` @0x69ef9e). Conservateur (jamais de sur-cull d'un objet visible).
bool ModelObjectRenderer::sphereInFrustum(const D3DXVECTOR3& c, float radius) const {
    const float v4 = radius * -1.0f;                         // 0x69ef9e
    for (int p = 0; p < 6; ++p)
        if (planes_[p][0]*c.x + planes_[p][1]*c.y + planes_[p][2]*c.z + planes_[p][3] < v4)
            return false;                                    // 0x69f0ca (chaîne de &&)
    return true;
}

// ===========================================================================
//  Matrice monde — Rz*Ry*Rx*T (Model_RenderWithShadow_0 0x6a41a3-0x6a4299).
// ===========================================================================
D3DXMATRIX ModelObjectRenderer::BuildWorldMatrix(const float pos[3], const float* orient) {
    constexpr float kDegToRad = 0.017453292f; // pi/180, littéral binaire (Model_RenderWithShadow_0 @0x6a41bc)
    const float rx = orient ? orient[0] : 0.0f;
    const float ry = orient ? orient[1] : 0.0f;
    const float rz = orient ? orient[2] : 0.0f;
    D3DXMATRIX t, mrx, mry, mrz, m;
    D3DXMatrixTranslation(&t, pos[0], pos[1], pos[2]);       // T   @0x6a41a3
    D3DXMatrixRotationX(&mrx, rx * kDegToRad);               // Rx  @0x6a41c0
    D3DXMatrixRotationY(&mry, ry * kDegToRad);               // Ry  @0x6a41da
    D3DXMatrixRotationZ(&mrz, rz * kDegToRad);               // Rz  @0x6a41f4
    D3DXMatrixMultiply(&m, &mrz, &mry);                      // M = Rz*Ry
    D3DXMatrixMultiply(&m, &m, &mrx);                        // M = M*Rx
    D3DXMatrixMultiply(&m, &m, &t);                          // M = M*T
    return m;
}

// ===========================================================================
//  Texture diffuse — depuis asset::MTexture (image DDS + trailer 8 o déjà décompressée par
//  asset::MObject::Load ; Tex_LoadCompressedFromHandle 0x6A9CF0). On passe `imgSize` octets à
//  D3DX (le trailer 8 o de queue = processMode/alphaMode, hors image DDS).
// ===========================================================================
IDirect3DTexture9* ModelObjectRenderer::createTexture(IDirect3DDevice9* dev, const asset::MTexture& tex) {
    if (!dev || !tex.present || tex.image.empty() || tex.imgSize == 0) return nullptr;
    const UINT sz = static_cast<UINT>((std::min)(static_cast<size_t>(tex.imgSize), tex.image.size()));
    if (sz == 0) return nullptr;
    IDirect3DTexture9* out = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        dev, tex.image.data(), sz,
        D3DX_DEFAULT, D3DX_DEFAULT, D3DX_DEFAULT, 0, D3DFMT_UNKNOWN,
        D3DPOOL_MANAGED, D3DX_DEFAULT, D3DX_DEFAULT, 0, nullptr, nullptr, &out);
    if (FAILED(hr)) {
        TS2_WARN("ModelObjectRenderer: creation texture MOBJECT echouee (0x%08lX)", hr);
        return nullptr;
    }
    return out;
}

// ===========================================================================
//  Upload d'une part — VB 32*A*B (A frames contiguës), IB 6*D partagé, tex0 diffuse.
//  Miroir de MeshPart_Load 0x6AD160 (CreateVertexBuffer @0x6ad3a2, CreateIndexBuffer @0x6ad64c)
//  et de WorldGeometryRenderer::uploadPart (même pattern GPU, source asset::MeshPart).
//  Le parseur (asset::MObject::Load) a DÉJÀ découpé geo.vertices (32*M*V), geo.indices (6*I) et
//  geo.matrices (M*64) : on memcpy directement, aucune re-slice depuis un blob brut.
// ===========================================================================
bool ModelObjectRenderer::uploadPart(const asset::MeshPart& part, GpuPart& out) {
    if (!part.hasMesh) return false;                         // MeshPart+128 = 0 : part vide
    const asset::MGeometry& g = part.geo;
    if (g.M == 0 || g.V == 0 || g.I == 0) return false;      // aucune frame/sommet/face

    const size_t vbBytes = kMobjVertexStride * static_cast<size_t>(g.M) * g.V; // 32*A*B
    const size_t ibBytes = kFaceStride * static_cast<size_t>(g.I);            // 6*D
    if (g.vertices.size() < vbBytes || g.indices.size() < ibBytes) {
        TS2_WARN("ModelObjectRenderer: part incoherente (vtx=%zu/%zu, idx=%zu/%zu)",
                 g.vertices.size(), vbBytes, g.indices.size(), ibBytes);
        return false;
    }

    HRESULT hr = dev_->CreateVertexBuffer(static_cast<UINT>(vbBytes), 0, kFvfMobj,
                                          D3DPOOL_MANAGED, &out.vb, nullptr);
    if (FAILED(hr)) { TS2_ERR("ModelObjectRenderer: CreateVertexBuffer echoue (0x%08lX)", hr); return false; }
    void* p = nullptr;
    if (SUCCEEDED(out.vb->Lock(0, static_cast<UINT>(vbBytes), &p, 0))) {
        std::memcpy(p, g.vertices.data(), vbBytes);          // A frames brutes (aucune conversion)
        out.vb->Unlock();
    }

    hr = dev_->CreateIndexBuffer(static_cast<UINT>(ibBytes), 0, D3DFMT_INDEX16,
                                 D3DPOOL_MANAGED, &out.ib, nullptr);
    if (FAILED(hr)) {
        TS2_ERR("ModelObjectRenderer: CreateIndexBuffer echoue (0x%08lX)", hr);
        SafeRelease(out.vb);
        return false;
    }
    if (SUCCEEDED(out.ib->Lock(0, static_cast<UINT>(ibBytes), &p, 0))) {
        std::memcpy(p, g.indices.data(), ibBytes);
        out.ib->Unlock();
    }

    out.A = g.M; out.B = g.V; out.D = g.I;
    out.frameBbox = g.matrices;                              // A*64 o (cull par-part + nœuds fresnel ; vide si corrompu)
    out.diffuse   = createTexture(dev_, part.tex0);          // tex0 diffuse/base (MeshPart+296/+344)

    // FRONT C1 : couche matériau (câblage B1). Modes = trailer1 (alphaMode) du bloc texture —
    // Tex_LoadCompressedFromHandle 0x6A9CF0 @0x6a9eab range this[11]=data[imgSize+4] au holder+44,
    // soit MeshPart+340 (tex0, a1[85]) / +392 (tex1, a1[98]) lus par MeshPart_RenderFull.
    out.baseMode   = static_cast<int>(part.tex0.trailer1);  // MeshPart+340 (a1[85])
    out.second     = createTexture(dev_, part.tex1);         // tex1 (MeshPart_Load @0x6ad6eb, holder +348 / ptr +396)
    out.secondMode = static_cast<int>(part.tex1.trailer1);  // MeshPart+392 (a1[98])
    out.mat        = part.mat;                               // en-tête matériau 120 o décodé (DecodeMeshPartMaterialHeader)
    // Flipbook (a1[101] MeshPart+404, count=a1[100]=matCount) : boucle Tex_LoadCompressedFromHandle
    // @0x6ad7a0. On uploade chaque MTexture ; on ne garde que les textures créées (indices valides
    // pour MeshPart_RenderFull @0x6b0d95 `idx % this[100]`, qui teste flipbook[0] non-nul @0x6b0d33).
    out.flipbook.reserve(part.mats.size());
    for (const asset::MTexture& mt : part.mats) {
        IDirect3DTexture9* ft = createTexture(dev_, mt);
        if (ft) out.flipbook.push_back(ft);
    }
    return true;
}

// ===========================================================================
//  getOrLoadMiscC — lazy-load SYNCHRONE (miroir ModelObj_Load 0x4D6F80 a6=1 +
//  Model_LoadFromFile 0x6A3490). Résout E{idxC+1}001.MOBJECT (catégorie 4).
// ===========================================================================
ModelObjectRenderer::MObjEntry* ModelObjectRenderer::getOrLoadMiscC(int idxC) {
    if (idxC < 0 || idxC >= kMiscCSlotCount) return nullptr; // hors banque MiscC (unk_B60AB8, 246 slots)

    auto found = cacheMiscC_.find(idxC);
    if (found != cacheMiscC_.end())
        return found->second.loadFailed ? nullptr : &found->second;

    // 1er accès : construit le chemin (ModelObj_BuildPath 0x4D6E20 catégorie 4 @0x4d6ed5) :
    //   <gameDataDir>\G03_GDATA\D02_GMOBJECT\003\E{idxC+1:03}001.MOBJECT
    MObjEntry entry;
    char name[32];
    std::snprintf(name, sizeof(name), "E%03d001.MOBJECT", idxC + 1);
    std::string path = JoinPath(JoinPath(JoinPath(gameDataDir_, "G03_GDATA\\D02_GMOBJECT"), "003"), name);

    asset::MObject mo;
    if (!mo.Load(path)) {
        TS2_WARN("ModelObjectRenderer: echec chargement '%s' (%s)", path.c_str(), mo.error().c_str());
        entry.loadFailed = true;
    } else {
        // frameCountA = parts[0].A (borne du gate frame = *(parts+252), Model_RenderWithShadow_0
        // @0x6a415e) — lu sur la PREMIÈRE part comme le binaire, indépendamment des parts uploadées.
        entry.frameCountA = mo.parts().empty() ? 0u : mo.parts()[0].geo.M;
        for (const asset::MeshPart& part : mo.parts()) {
            GpuPart gp;
            if (uploadPart(part, gp)) entry.parts.push_back(std::move(gp));
        }
        entry.loaded = true; // MObject valide (même à 0 part utilisable : ne pas retenter)
        TS2_LOG("ModelObjectRenderer: '%s' charge (%zu part(s) GPU, A=%u, resident=%zu)",
                name, entry.parts.size(), entry.frameCountA, cacheMiscC_.size() + 1);
    }

    auto [ins, inserted] = cacheMiscC_.emplace(idxC, std::move(entry));
    (void)inserted;
    return ins->second.loadFailed ? nullptr : &ins->second;
}

// Nombre de frames du flipbook pour un slot MiscC (A = parts[0].geo.M, cf. getOrLoadMiscC).
uint32_t ModelObjectRenderer::FrameCount(int idxC) {
    MObjEntry* e = getOrLoadMiscC(idxC);
    return e ? e->frameCountA : 0u;
}

// ===========================================================================
//  MeshDraw — ModelObj_Draw 0x4D71B0 + Model_RenderWithShadow_0 0x6A4110 (setup + cull par-part)
//  puis, par-part, MeshPartMaterialRenderer::Render (= MeshPart_RenderFull 0x6B0850, FRONT C1).
// ===========================================================================
void ModelObjectRenderer::MeshDraw(FxMeshBank bank, int /*idxA*/, int /*idxB*/, int idxC,
                                   int pass, float drawParam, const float pos[3], const float* orient) {
    if (!ready_ || !dev_ || !pos) return;
    // V1 : une seule banque (MiscC = types 8/9/0xA de Fx_EmitterDraw). AvatarA (1/2) / NpcB (3/4)
    // -> TODO ancre (unk_A71410 @0x585EB3 / unk_B551B8 @0x585F73) : mêmes ModelObj 148 o, mais
    // banque/catégorie distinctes (cat 1 `C%03d%03d` / cat 3 `M%03d001`). Non gérées ici.
    if (bank != FxMeshBank::MiscC) return;
    // Model_RenderWithShadow_0 : gate passe a2 ∈ [1,2] (@0x6a412c/@0x6a4135).
    if (pass < 1 || pass > 2) return;

    MObjEntry* e = getOrLoadMiscC(idxC);
    if (!e || !e->loaded || e->parts.empty()) return;        // ModelObj_Draw : dessin gaté par le chargement

    // frame = Crt_Dbl2Uint(a3) (troncature vers 0), gate [0, parts[0].A-1] (@0x6a4148/@0x6a415e).
    const int frame = (drawParam > 0.0f) ? static_cast<int>(drawParam) : 0;
    if (frame < 0 || frame > static_cast<int>(e->frameCountA) - 1) return;

    const D3DXMATRIX world = BuildWorldMatrix(pos, orient); // SetTransform(256, ...) @0x6a4299

    // --- Snapshot des états device modifiés : ce shim est appelé DANS les passes 1/2 de
    //     Fx_EmitterDraw, entre des états posés pour la passe billboard 3 (SceneManager pose le
    //     bracket Gfx_BeginUnlitPass AVANT les 3 passes). On restaure donc TOUT ce qu'on change
    //     pour que la passe particule reste correcte. Miroir « poli » du reset d'états one-shot de
    //     ModelObj_Draw (dword_8E7178 : RS 25=5, 19=5, 20=6 @0x4d7206-@0x4d723b) + FF de base. ---
    IDirect3DVertexShader9* oldVS = nullptr; dev_->GetVertexShader(&oldVS);
    IDirect3DPixelShader9*  oldPS = nullptr; dev_->GetPixelShader(&oldPS);
    DWORD oldFvf = 0;                        dev_->GetFVF(&oldFvf);
    DWORD oldLighting = TRUE, oldCull = D3DCULL_CCW, oldABE = FALSE;
    DWORD oldSrc = D3DBLEND_ONE, oldDst = D3DBLEND_ZERO, oldAFunc = D3DCMP_ALWAYS;
    dev_->GetRenderState(D3DRS_LIGHTING, &oldLighting);
    dev_->GetRenderState(D3DRS_CULLMODE, &oldCull);
    dev_->GetRenderState(D3DRS_ALPHABLENDENABLE, &oldABE);
    dev_->GetRenderState(D3DRS_SRCBLEND, &oldSrc);
    dev_->GetRenderState(D3DRS_DESTBLEND, &oldDst);
    dev_->GetRenderState(D3DRS_ALPHAFUNC, &oldAFunc);
    // Extension FRONT C1 : MeshPartMaterialRenderer::Render modifie AUSSI ces états (par-part, puis
    // les repose à SON état « propre » via restoreBlendTriplet — cf. MeshPart_RenderFull). On les
    // snapshotte pour que MeshDraw reste AUTO-CONTENU (la passe billboard 3 retrouve SON état exact).
    // (Matériau D3DMATERIAL9 + lumières 0/1 : Render les restaure à SON snapshot d'entrée = notre état
    //  courant, ET ils sont INERTES sous LIGHTING=FALSE -> non re-snapshottés ici. Cf. §CAVEAT du .h.)
    DWORD oldZW=TRUE, oldATE=FALSE, oldARef=0, oldSpec=FALSE, oldTF=0xFFFFFFFF;
    dev_->GetRenderState(D3DRS_ZWRITEENABLE,    &oldZW);
    dev_->GetRenderState(D3DRS_ALPHATESTENABLE, &oldATE);
    dev_->GetRenderState(D3DRS_ALPHAREF,        &oldARef);
    dev_->GetRenderState(D3DRS_SPECULARENABLE,  &oldSpec);
    dev_->GetRenderState(D3DRS_TEXTUREFACTOR,   &oldTF);
    DWORD s0co=0, s0c1=0, s0ao=0, s0a1=0, s0tci=0, s1co=0;
    dev_->GetTextureStageState(0, D3DTSS_COLOROP,       &s0co);
    dev_->GetTextureStageState(0, D3DTSS_COLORARG1,     &s0c1);
    dev_->GetTextureStageState(0, D3DTSS_ALPHAOP,       &s0ao);
    dev_->GetTextureStageState(0, D3DTSS_ALPHAARG1,     &s0a1);
    dev_->GetTextureStageState(0, D3DTSS_TEXCOORDINDEX, &s0tci);
    dev_->GetTextureStageState(1, D3DTSS_COLOROP,       &s1co);
    DWORD s0aa2=D3DTA_CURRENT, s0ttf=D3DTTFF_DISABLE;    // Render touche ALPHAARG2 + TEXTURETRANSFORMFLAGS (stage 0)
    dev_->GetTextureStageState(0, D3DTSS_ALPHAARG2,             &s0aa2);
    dev_->GetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, &s0ttf);
    IDirect3DBaseTexture9* oldTex0 = nullptr; dev_->GetTexture(0, &oldTex0);
    D3DXMATRIX oldWorld;  dev_->GetTransform(D3DTS_WORLD,    &oldWorld);
    D3DXMATRIX oldTex0M;  dev_->GetTransform(D3DTS_TEXTURE0, &oldTex0M); // UV-scroll pose une matrice tex (@0x6b1067)

    // --- États FF du dessin de base (chemin FX a4=0/a6=0 : ni texture-projetée ni alpha-fade) ---
    // PRÉ-CONDITION Render (fidèle à Model_RenderWithShadow_0 @0x6a4299) : VS/PS coupés (FF pur) +
    // FVF(274) + WORLD posés AVANT Render ; Render ne touche PAS VS/PS/FVF/WORLD.
    // PIÈGE shader-bind-cache (device D3D9 PARTAGÉ, 2 singletons) : on coupe VS/PS ici — MAIS on les
    // RESTAURE symétriquement en fin de fonction (SetVertexShader(oldVS)/SetPixelShader(oldPS), sur
    // TOUS les chemins : AUCUN return entre la coupe et la restauration). Le device reste donc
    // NET-INCHANGÉ (VS/PS/FVF identiques avant/après l'appel) -> aucun MeshRenderer ne voit son
    // currentPass_ devenir périmé (le bug ne survient QUE si VS/PS restent coupés APRÈS, cf.
    // Gfx/MeshRenderer.h::InvalidateShaderBindingCache). Contrairement à WorldGeometryRenderer (qui
    // coupe VS/PS puis dessine skinné SANS restaurer -> lui DOIT invalider), ici la restauration
    // symétrique EST la garantie ; aucun MeshRenderer n'est d'ailleurs joignable depuis ce renderer.
    dev_->SetVertexShader(nullptr);                          // coupe le VS skinné éventuel (ModelObj_Draw @0x4d7266)
    dev_->SetPixelShader(nullptr);
    dev_->SetFVF(kFvfMobj);                                  // 274 (0x112)
    dev_->SetRenderState(D3DRS_LIGHTING, FALSE);             // FF unlit (diffuse pure)
    dev_->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);      // build-safe (FX souvent double-face)
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);      // « alpha-blend standard » (effets)
    dev_->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);    // ModelObj_Draw reset 19=5 @0x4d7220
    dev_->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA); // reset 20=6 @0x4d723b
    dev_->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);       // reset 25=5 @0x4d7206 (inerte sans alpha-test)
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,       D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1,     D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,       D3DTOP_SELECTARG1);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1,     D3DTA_TEXTURE);
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX, 0);
    dev_->SetTextureStageState(1, D3DTSS_COLOROP,       D3DTOP_DISABLE);
    dev_->SetTransform(D3DTS_WORLD, &world);

    // --- État runtime partagé pour la couche matériau B1 (identique pour toutes les parts) ---
    const float animTime = animClockSeconds();  // v66 = Terrain_PushRenderState()+a3, a3=0 (ModelObj_Draw @0x4d72af)
    MeshPartRuntime rt;
    rt.world       = world;                     // dword_800244 (déjà posé en D3DTS_WORLD ci-dessus)
    rt.worldValid  = true;
    rt.cameraEye   = frameValid_ ? cameraEye_ : D3DXVECTOR3(0.0f, 0.0f, 0.0f); // repli documenté si SetFrame absent
    rt.cameraAt    = frameValid_ ? cameraAt_  : D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    rt.sunDir      = D3DXVECTOR3(0.0f, 0.0f, 0.0f); // flt_800308/30C/310 INDISPONIBLE -> dot fresnel=0 => pas de flash (sûr)
    rt.sceneCenter = D3DXVECTOR3(0.0f, 0.0f, 0.0f); // centre AABB scène (g_GfxRenderer+1204*0.5+1236) INDISPONIBLE -> repli (inerte sous LIGHTING=FALSE)

    // --- Boucle sur les parts (stride 408 côté binaire), cull par-part + couche matériau B1 ---
    for (const GpuPart& p : e->parts) {
        if (!p.vb || !p.ib || p.B == 0 || p.D == 0) continue;
        // Partition opaque/transparent des passes (Model_RenderWithShadow_0 0x6A4110) : chaque part
        // n'est dessinée que dans SA passe. Passe 1 = opaque @0x6a4560 (flipbook inactif ET
        // baseMode!=2) ; passe 2 = transparent @0x6a43a3 (flipbook actif [this[53] && *(this[101]+48)]
        // OU baseMode this[85]==2). Sans ce filtre, chaque part FX était dessinée en passe 1 ET 2
        // -> sur-brillance additive (double-draw). (Audit-C, vérifié en IDA.)
        const bool flipbookActive = p.mat.flipbook.Enable && !p.flipbook.empty();
        const bool isTransparent  = flipbookActive || (p.baseMode == 2);
        if ((pass == 1) == isTransparent) continue;
        // A peut différer entre parts (le binaire suppose A uniforme et ne re-borne pas) : on
        // borne défensivement au VB de la part pour ne pas lire hors buffer.
        const uint32_t pf = (frame < static_cast<int>(p.A)) ? static_cast<uint32_t>(frame) : (p.A - 1);

        // Frustum-cull par-part (Cam_FrustumTestSphere 0x69EF90, marge ×1) : centre local de la
        // frame (frameBbox[pf].+48) transformé en monde, rayon (+60). Sauté si frame non fournie
        // (SetFrame non appelé) ou bbox absente -> dessin (jamais de sur-cull). @0x6a431b/@0x6a4339.
        if (frameValid_ && p.frameBbox.size() >= static_cast<size_t>(pf) * 64 + 64) {
            const uint8_t* elem = p.frameBbox.data() + static_cast<size_t>(pf) * 64;
            D3DXVECTOR3 localC; std::memcpy(&localC, elem + 48, sizeof(localC)); // centre @+48
            float radius = 0.0f; std::memcpy(&radius, elem + 60, sizeof(radius)); // rayon @+60
            D3DXVECTOR3 worldC; D3DXVec3TransformCoord(&worldC, &localC, &world);
            if (!sphereInFrustum(worldC, radius)) continue;
        }

        if (p.mat.decoded) {
            // === Câblage B1 : machine à états matériau COMPLÈTE (MeshPart_RenderFull 0x6B0850,
            //     appelée par Model_RenderWithShadow_0 @0x6a4362/@0x6a45f7). Remplace le base-draw. ===
            MeshPartGpu geo;
            geo.vb            = p.vb;
            geo.ib            = p.ib;
            geo.vertsPerFrame = p.B;                                            // B = this[64] (+256)
            geo.triCount      = p.D;                                            // D = this[66] (+264)
            geo.frameCount    = p.A;                                            // A = this[63] (+252) — Render re-borne frame
            geo.frameNodes    = p.frameBbox.empty() ? nullptr : p.frameBbox.data(); // this[71] (+284), centre @+48
            MeshPartTextures tx;
            tx.base          = p.diffuse;                                       // this[86] (+344)
            tx.baseMode      = p.baseMode;                                      // this[85] (+340)
            tx.second        = p.second;                                        // this[99] (+396)
            tx.secondMode    = p.secondMode;                                    // this[98] (+392)
            tx.flipbook      = p.flipbook.empty() ? nullptr : p.flipbook.data(); // this[101] (+404)
            tx.flipbookCount = static_cast<uint32_t>(p.flipbook.size());        // this[100] (+400)
            // Chemin FX PROUVÉ (ModelObj_Draw 0x4D71B0 @0x4d72af -> Model_RenderWithShadow_0(…,0.0,0,1,0)) :
            // animTime phase=0, decal=null, glowEnable=1 (a8), alphaFade=0 (a9). frame=v10 (Render re-borne).
            MeshPartMaterialRenderer::Render(dev_, p.mat, geo, tx, frame, animTime, rt,
                                             /*glowEnable*/ 1, /*alphaFade*/ 0, /*decal*/ nullptr);
        } else {
            // Repli : en-tête matériau NON décodé (part dégénérée) -> base-draw actuel INCHANGÉ.
            dev_->SetTexture(0, p.diffuse);                                     // SetTexture(0, tex0)
            dev_->SetStreamSource(0, p.vb, 32u * pf * p.B, 32u);                // 32*frame*B @0x6b1327
            dev_->SetIndices(p.ib);                                            // @0x6b133c
            dev_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, p.B, 0, p.D);  // @0x6b1360
        }
    }

    // --- Restauration (self-contained : la passe billboard 3 retrouve son état) ---
    dev_->SetTransform(D3DTS_TEXTURE0, &oldTex0M);           // matrice de texture (UV-scroll) — FRONT C1
    dev_->SetTransform(D3DTS_WORLD,    &oldWorld);
    dev_->SetTexture(0, oldTex0); if (oldTex0) oldTex0->Release();
    dev_->SetTextureStageState(1, D3DTSS_COLOROP,              s1co);
    dev_->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, s0ttf);         // FRONT C1
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG2,            s0aa2);          // FRONT C1
    dev_->SetTextureStageState(0, D3DTSS_TEXCOORDINDEX,        s0tci);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAARG1,            s0a1);
    dev_->SetTextureStageState(0, D3DTSS_ALPHAOP,              s0ao);
    dev_->SetTextureStageState(0, D3DTSS_COLORARG1,            s0c1);
    dev_->SetTextureStageState(0, D3DTSS_COLOROP,              s0co);
    dev_->SetRenderState(D3DRS_TEXTUREFACTOR,   oldTF);                         // FRONT C1
    dev_->SetRenderState(D3DRS_SPECULARENABLE,  oldSpec);                       // FRONT C1
    dev_->SetRenderState(D3DRS_ALPHAREF,        oldARef);                       // FRONT C1
    dev_->SetRenderState(D3DRS_ALPHATESTENABLE, oldATE);                        // FRONT C1
    dev_->SetRenderState(D3DRS_ZWRITEENABLE,    oldZW);                         // FRONT C1
    dev_->SetRenderState(D3DRS_ALPHAFUNC, oldAFunc);
    dev_->SetRenderState(D3DRS_DESTBLEND, oldDst);
    dev_->SetRenderState(D3DRS_SRCBLEND, oldSrc);
    dev_->SetRenderState(D3DRS_ALPHABLENDENABLE, oldABE);
    dev_->SetRenderState(D3DRS_CULLMODE, oldCull);
    dev_->SetRenderState(D3DRS_LIGHTING, oldLighting);
    dev_->SetFVF(oldFvf);
    dev_->SetVertexShader(oldVS); if (oldVS) oldVS->Release();
    dev_->SetPixelShader(oldPS);  if (oldPS) oldPS->Release();
}

// ---------------------------------------------------------------------------
//  Shim libre du hook FxModelObjDrawFn — forwarde vers le renderer actif.
void ModelObjectRenderer_MeshDrawShim(FxMeshBank bank, int idxA, int idxB, int idxC,
                                      int pass, float drawParam,
                                      const float pos[3], const float* orient) {
    if (s_active)
        s_active->MeshDraw(bank, idxA, idxB, idxC, pass, drawParam, pos, orient);
}

} // namespace ts2::gfx

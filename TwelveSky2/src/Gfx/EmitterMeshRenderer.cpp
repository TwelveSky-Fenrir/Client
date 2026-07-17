// Gfx/EmitterMeshRenderer.cpp — upload GPU + dessin d'un mesh d'effet .MOBJECT2.
//
// Reproduction fidèle de `Mesh_DrawAnimatedFrame 0x430BE0` (décompilé + désassemblé le 2026-07-17,
// idaTs2, TwelveSky2.exe, imagebase 0x400000). Appelant d'origine : Mesh_DrawInstancesLOD 0x431A90.
// Chaque bloc porte son ancre EA. MODULE AUTONOME : device fourni par l'appelant, aucun câblage.
//
// Correspondance offsets runtime (a3 = mesh 268 o) → champs C++ (voir aussi EmitterMeshRenderer.h) :
//   a3+0   type (gate)            a3+4   mode compression (==1 ⇒ CPU ; on est toujours GPU/MANAGED)
//   a3+8   animatedTex            a3+12  animTexSpeed     a3+16 texMinFrame   a3+20 texMaxFrame
//   a3+24  glowEnable             a3+28  glowSpeed        a3+32/36/40 glowFrom  a3+48/52/56 glowTo
//   a3+64  uvEnable               a3+68  uvMode           a3+72 uvSpeed
//   a3+76  billboardEnable        a3+80  billboardAxisMode
//   a3+88  frameBbox (40·N)       a3+92  frameScale (4·N) a3+96..176 header2/gabarit billboard
//   a3+176 subsetCount            a3+180 vertexCounts     a3+184 CPU VB  a3+188 D3D VB
//   a3+192 faceCounts             a3+196 CPU IB           a3+200 D3D IB
//   a3+204 mainTex holder (+44 blendMode / +52 texture)   a3+260 extraTexCount  a3+264 extraTex array
#include "Gfx/EmitterMeshRenderer.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"

#include <cstring>
#include <utility> // std::move
#include <windows.h>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

// Lectures little-endian sûres depuis un blob (x86 natif). Renvoient 0 hors bornes.
inline int32_t RdI32(const std::vector<uint8_t>& b, size_t off) {
    if (off + 4 > b.size()) return 0;
    return int32_t(uint32_t(b[off]) | (uint32_t(b[off + 1]) << 8) |
                   (uint32_t(b[off + 2]) << 16) | (uint32_t(b[off + 3]) << 24));
}
inline uint32_t RdU32(const std::vector<uint8_t>& b, size_t off) {
    return uint32_t(RdI32(b, off));
}
inline float RdF32(const std::vector<uint8_t>& b, size_t off) {
    uint32_t u = RdU32(b, off);
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// Écrit 3 floats (xyz) à `off` dans un tampon de sommet (coins du quad billboard).
inline void WriteXYZ(uint8_t* dst, size_t off, float x, float y, float z) {
    std::memcpy(dst + off + 0, &x, 4);
    std::memcpy(dst + off + 4, &y, 4);
    std::memcpy(dst + off + 8, &z, 4);
}

// Constante 0.01 du binaire (dbl_7EDB40 = 0x3F847AE140000000 ≈ 0.009999999776482582).
constexpr double kHundredth = 0.009999999776482582;

// GXD_SetDirectionalLight 0x403980 : remplit un D3DLIGHT9 directionnel et pose light 0.
//   mode 2 (glow)  : Ambient = (r,g,b,a)                         (a5=1.0 côté draw)
//   mode 1 (reset) : Ambient = ambient de scène (a1+1124.. ; repli fourni en autonome)
// Diffuse=(0,0,0,1), Specular=(0,0,0,1), Direction=(-1,-1,1) — byte-exact (v13/v14.. @0x4039CC..0x403B2D).
void SetGxdLight(IDirect3DDevice9* dev, float r, float g, float b, float a) {
    dev->LightEnable(0, TRUE);                 // vtable+212 (LightEnable) @0x403999
    D3DLIGHT9 lt;
    std::memset(&lt, 0, sizeof(lt));           // Crt_Memset 0x68 @0x4039A4
    lt.Type        = D3DLIGHT_DIRECTIONAL;     // v13[0] = 3
    lt.Diffuse.r   = 0.f; lt.Diffuse.g = 0.f; lt.Diffuse.b = 0.f; lt.Diffuse.a = 1.f; // v13[1..4]
    lt.Specular.r  = 0.f; lt.Specular.g = 0.f; lt.Specular.b = 0.f; lt.Specular.a = 1.f; // v13[5..8]
    lt.Ambient.r   = r;   lt.Ambient.g = g;   lt.Ambient.b = b;   lt.Ambient.a = a;   // v14..v17
    lt.Position    = D3DXVECTOR3(0.f, 0.f, 0.f);   // v18..v20
    lt.Direction   = D3DXVECTOR3(-1.f, -1.f, 1.f); // v21=-1, v22=-1, v23=1
    lt.Range = 0.f; lt.Falloff = 0.f;
    lt.Attenuation0 = 0.f; lt.Attenuation1 = 0.f; lt.Attenuation2 = 0.f;
    lt.Theta = 0.f; lt.Phi = 0.f;
    dev->SetLight(0, &lt);                      // vtable+204 (SetLight) @0x403B3F
}

} // namespace

// =============================================================================================
//  Timer QPC — v50 = (compteur - start)/freq + phase  (dbl_18C4F80 freq / dbl_18C4F88 start)
// =============================================================================================
double EmitterMeshRenderer::ElapsedSeconds() const {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);             // @0x430C16
    if (!timerInit_) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq_       = (f.QuadPart != 0) ? f.QuadPart : 1;
        startCount_ = now.QuadPart;            // origine locale (phase relative — fidèle à l'intention)
        timerInit_  = true;
    }
    return double(now.QuadPart - startCount_) / double(freq_); // @0x430C34
}

// =============================================================================================
//  Upload — asset::Mobject2 -> EmitterGpuObject (VB/IB/textures, D3DPOOL_MANAGED)
// =============================================================================================
bool EmitterMeshRenderer::uploadTexture(IDirect3DDevice9* dev, const asset::SObjectTexture& src,
                                        EmitterTexHolder& out) {
    out.present   = false;
    out.blendMode = src.alphaMode;             // holder+44 (= SObjectTexture::alphaMode)
    if (!src.present || src.packedSize == 0 || src.compressed.empty() ||
        src.rawSize == 0 || src.ddsSize == 0) {
        return false;                          // texture absente (Tex_ReadPacked gate `if(a1[1])`)
    }
    auto& zlib = asset::Zlib::Instance();
    if (!zlib.Available()) return false;
    // Bloc décompressé = [ddsSize octets DDS][u32 procMode][u32 alphaMode] (= rawSize) — cf. MeshRenderer.
    std::vector<uint8_t> raw(src.rawSize);
    if (!zlib.Inflate(src.compressed.data(), src.compressed.size(), raw.data(), src.rawSize)) {
        return false;
    }
    if (!out.tex.CreateFromImageFileInMemory(dev, raw.data(), src.ddsSize)) return false;
    out.present = out.tex.Valid();
    return out.present;
}

bool EmitterMeshRenderer::uploadMesh(IDirect3DDevice9* dev, const asset::Mobject2Mesh& src,
                                     EmitterGpuMesh& out) const {
    if (src.empty || src.type == 0) {          // mesh vide (type==0) — conservé mais non dessiné
        out.valid = false;
        return true;
    }
    out.valid      = true;
    out.frameCount = (src.n == 0) ? 1u : src.n; // N = a1[21] (nb de frames ; garde 1 minimum)

    // --- Décodage de l'en-tête matériau (header1, 76 o = a1[2..20]) ---
    // Décalage header1 = (offset mesh) - 8  (a1[2] = mesh+8 = header1[0]).
    const std::vector<uint8_t>& h = src.header1;
    out.animatedTex       = RdU32(h,  0); // mesh+8
    out.animTexSpeed      = RdI32(h,  4); // mesh+12
    out.texMinFrame       = RdI32(h,  8); // mesh+16
    out.texMaxFrame       = RdI32(h, 12); // mesh+20
    out.glowEnable        = RdU32(h, 16); // mesh+24
    out.glowSpeed         = RdI32(h, 20); // mesh+28
    out.glowFrom[0]       = RdI32(h, 24); // mesh+32
    out.glowFrom[1]       = RdI32(h, 28); // mesh+36
    out.glowFrom[2]       = RdI32(h, 32); // mesh+40   (header1[9] @ byte 36 = mesh+44 : inutilisé)
    out.glowTo[0]         = RdI32(h, 40); // mesh+48
    out.glowTo[1]         = RdI32(h, 44); // mesh+52
    out.glowTo[2]         = RdI32(h, 48); // mesh+56   (header1[13] @ byte 52 = mesh+60 : inutilisé)
    out.uvEnable          = RdU32(h, 56); // mesh+64
    out.uvMode            = RdU32(h, 60); // mesh+68
    out.uvSpeed           = RdI32(h, 64); // mesh+72
    out.billboardEnable   = RdU32(h, 68); // mesh+76
    out.billboardAxisMode = RdU32(h, 72); // mesh+80

    // Tables par frame + gabarit billboard (copiées brutes, indexées au dessin).
    out.frameBbox         = src.boneTable; // 40·N (a1[22], mesh+88)
    out.frameScale        = src.table4;    //  4·N (a1[23], mesh+92)
    out.billboardTemplate = src.header2;   // 80   (a1[24..43], mesh+96..176)

    // --- Subsets : VB (20·N·vc, FVF 258, MANAGED) + IB (6·fc, INDEX16, MANAGED) ---
    out.subsets.clear();
    out.subsets.reserve(src.subsets.size());
    for (const asset::Mobject2Subset& s : src.subsets) {
        EmitterGpuSubset g;
        g.vertexCount = s.vertexCount;
        g.faceCount   = s.faceCount;

        // VB : CreateVertexBuffer(20·N·vc, USAGE_WRITEONLY, FVF=258, POOL_MANAGED) @0x430897.
        if (!s.vertexBuffer.empty()) {
            const UINT vbBytes = static_cast<UINT>(s.vertexBuffer.size()); // = 20·N·vertexCount
            if (SUCCEEDED(dev->CreateVertexBuffer(vbBytes, D3DUSAGE_WRITEONLY, /*FVF*/258,
                                                  D3DPOOL_MANAGED, &g.vb, nullptr)) && g.vb) {
                void* p = nullptr;
                if (SUCCEEDED(g.vb->Lock(0, vbBytes, &p, 0)) && p) {
                    std::memcpy(p, s.vertexBuffer.data(), vbBytes); // memcpy Lock @0x4308F0
                    g.vb->Unlock();
                } else {
                    g.vb->Release(); g.vb = nullptr;
                }
            }
        }
        // IB : CreateIndexBuffer(6·fc, USAGE_WRITEONLY, D3DFMT_INDEX16, POOL_MANAGED) @0x430A03.
        if (!s.indexBuffer.empty()) {
            const UINT ibBytes = static_cast<UINT>(s.indexBuffer.size()); // = 6·faceCount
            if (SUCCEEDED(dev->CreateIndexBuffer(ibBytes, D3DUSAGE_WRITEONLY, D3DFMT_INDEX16,
                                                 D3DPOOL_MANAGED, &g.ib, nullptr)) && g.ib) {
                void* p = nullptr;
                if (SUCCEEDED(g.ib->Lock(0, ibBytes, &p, 0)) && p) {
                    std::memcpy(p, s.indexBuffer.data(), ibBytes); // memcpy Lock @0x430A5C
                    g.ib->Unlock();
                } else {
                    g.ib->Release(); g.ib = nullptr;
                }
            }
        }
        out.subsets.push_back(std::move(g));
    }

    // --- Textures : principale (a1[51]) + extras (a1[66], 56·extraTexCount) ---
    uploadTexture(dev, src.tex, out.mainTex);      // Tex_ReadPacked a1+51 @0x430A80
    out.extraTex.clear();
    out.extraTex.reserve(src.extraTex.size());
    for (const asset::SObjectTexture& t : src.extraTex) {
        EmitterTexHolder holder;
        uploadTexture(dev, t, holder);             // 56 o chacune @0x430B22
        out.extraTex.push_back(std::move(holder));
    }
    return true;
}

bool EmitterMeshRenderer::Upload(IDirect3DDevice9* dev, const asset::Mobject2& src,
                                 EmitterGpuObject& out) const {
    out = EmitterGpuObject{};
    if (!dev) { TS2_ERR("EmitterMesh : device nul"); return false; }
    out.meshes.reserve(src.meshes().size());
    for (const asset::Mobject2Mesh& m : src.meshes()) {
        EmitterGpuMesh g;
        uploadMesh(dev, m, g);
        out.meshes.push_back(std::move(g));
    }
    out.ok = true;
    return true;
}

EmitterGpuObject* EmitterMeshRenderer::GetOrLoad(IDirect3DDevice9* dev, const std::string& path) {
    if (!dev) return nullptr;
    auto it = cache_.find(path);
    if (it != cache_.end()) return &it->second;
    if (loadFailed_.count(path)) return nullptr;

    asset::Mobject2 parsed;
    if (!parsed.Load(path)) { loadFailed_[path] = true; return nullptr; }

    EmitterGpuObject obj;
    if (!Upload(dev, parsed, obj)) { loadFailed_[path] = true; return nullptr; }
    auto res = cache_.emplace(path, std::move(obj));
    return &res.first->second;
}

void EmitterMeshRenderer::ReleaseCache() {
    cache_.clear();       // EmitterGpuObject/Mesh/Subset : destructeurs libèrent VB/IB/GpuTexture
    loadFailed_.clear();
}

EmitterMeshRenderer::~EmitterMeshRenderer() {
    // RAII : la destruction de cache_ libère chaque EmitterGpuSubset (VB/IB) et EmitterTexHolder
    // (GpuTexture). Rien à faire manuellement.
}

// =============================================================================================
//  DrawMesh — reproduction byte-exacte de Mesh_DrawAnimatedFrame 0x430BE0
// =============================================================================================
void EmitterMeshRenderer::DrawObject(IDirect3DDevice9* dev, const EmitterGpuObject& obj,
                                     const EmitterMeshDrawArgs& args) {
    for (const EmitterGpuMesh& m : obj.meshes)
        if (m.valid) DrawMesh(dev, m, args);
}

void EmitterMeshRenderer::DrawMesh(IDirect3DDevice9* dev, const EmitterGpuMesh& m,
                                   const EmitterMeshDrawArgs& args) {
    if (!dev || !m.valid) return;                       // gate *(a3)==0 @0x430BEF

    // v51 = v10 = ftol(a5) : index de frame (troncature vers zéro).                 @0x430C06
    int frameI = static_cast<int>(args.frame);
    // Timer v50 = secondes écoulées + a7.                                            @0x430C43
    const double t = ElapsedSeconds() + static_cast<double>(args.timePhase);

    // --- Sélection du holder de texture (v43) ------------------------------------------------
    const EmitterTexHolder* holder = args.overrideTex; // ecx (a2) : override si non nul
    if (m.animatedTex == 1 && !m.extraTex.empty()) {   // texture animée @0x430C66
        const int cnt = static_cast<int>(m.extraTex.size());
        int idx;
        if (m.texMinFrame < 1) {                       // index TEMPOREL @0x430C72
            // idx = ((int)(animTexSpeed · t · 0.01)) % extraTexCount
            int ti = static_cast<int>(static_cast<double>(m.animTexSpeed) * t * kHundredth);
            idx = ti % cnt;                            // idiv → reste signé
            if (idx < 0) idx += cnt;                   // borne défensive (accès tableau)
        } else {                                       // mappé sur [min,max] @0x430C92
            if (frameI < m.texMinFrame - 1) return;    // cull @0x430C97
            if (frameI > m.texMaxFrame - 1) return;    // cull @0x430CA5
            const double span = static_cast<double>(cnt) /
                                static_cast<double>(m.texMaxFrame - m.texMinFrame + 1);
            idx = static_cast<int>(span * static_cast<double>(frameI - m.texMinFrame + 1));
            if (idx < 0) idx = 0;                       // borne défensive
            if (idx >= cnt) idx = cnt - 1;
        }
        holder = &m.extraTex[idx];                     // v43 = extraTex[idx] @0x430CE2
    }
    if (!holder) holder = &m.mainTex;                  // défaut = mainTex (a3+204)

    const uint32_t blendMode = holder->blendMode;      // v40 = *(v43+44)
    uint8_t alpha = args.alpha;                        // v7

    // --- Gate de passe (a4) ------------------------------------------------------------------
    if (args.pass == 1) {                              // @0x430CE6
        if (blendMode == 2) return;                    // passe opaque : saute l'additif @0x430CFD
    } else {
        if (blendMode != 2) return;                    // passe additive : additif seul @0x430D17
        // alpha = frameScale[frame] · (255 - alpha)   (troncature) @0x430D20..0x430D53
        const float sc = RdF32(m.frameScale, static_cast<size_t>(frameI) * 4);
        const int scaled = static_cast<int>(static_cast<double>(sc) *
                                            static_cast<double>(255 - alpha)); // troncative (ftol)
        alpha = static_cast<uint8_t>(scaled);
    }

    // --- Sélection du subset (LOD par nb de faces, facteur a6) --------------------------------
    const int nSub = static_cast<int>(m.subsets.size()); // a3+176 subsetCount
    if (nSub <= 0) return;
    int result;                                          // @0x430D5D
    if (args.lodFactor >= 1.0f) {
        result = static_cast<int>(m.subsets[0].faceCount); // faceCounts[0] @0x430D9C
    } else {
        int r    = static_cast<int>(args.lodFactor *
                     static_cast<double>(static_cast<int>(m.subsets[0].faceCount) - 1)) + 1; // @0x430D7F
        int last = static_cast<int>(m.subsets[nSub - 1].faceCount);
        result   = (r < last) ? last : r;                // max(...) @0x430D96
    }
    int i = 0;                                           // boucle @0x430DAC
    for (; i < nSub; ++i)
        if (result >= static_cast<int>(m.subsets[i].faceCount)) break;
    if (i == nSub) return;                               // aucun LOD ne convient @0x430DBA
    const EmitterGpuSubset& sub = m.subsets[i];

    // Sécurité d'indexation de frame (le flipbook garantit 0<=frame<N côté appelant ; on borne
    // pour éviter tout accès VB/frameBbox hors limites en autonome). Non présent dans le binaire.
    if (frameI < 0) frameI = 0;
    if (static_cast<uint32_t>(frameI) >= m.frameCount) frameI = static_cast<int>(m.frameCount) - 1;

    IDirect3DTexture9* d3dTex = holder->present ? holder->tex.Handle() : nullptr; // *(v43+52)

    // --- États de fondu AVANT dessin (v40 / v7) ----------------------------------------------
    auto SetRS = [&](D3DRENDERSTATETYPE s, DWORD v) { dev->SetRenderState(s, v); };
    auto blendCommon = [&](DWORD texFactor) {          // @0x430EAB / @0x430F0B / @0x430DE4
        SetRS(D3DRS_ZWRITEENABLE, 0);
        SetRS(D3DRS_ALPHABLENDENABLE, 1);
        SetRS(D3DRS_SRCBLEND, 5);                       // D3DBLEND_SRCALPHA
        SetRS(D3DRS_DESTBLEND, 6);                      // D3DBLEND_INVSRCALPHA
        SetRS(D3DRS_TEXTUREFACTOR, texFactor);
    };
    auto tssTail = [&]() {                              // @0x430F76 / @0x430F8C
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE); // (0,4,4) @0x430F76
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);   // (0,6,3) @0x430F8C
    };
    if (blendMode == 1) {                              // @0x430E59
        SetRS(D3DRS_ALPHATESTENABLE, 1);               // @0x430E64
        SetRS(D3DRS_ALPHAFUNC, 5);                     // D3DCMP_GREATEREQUAL @0x430E78
        SetRS(D3DRS_ALPHAREF, 128);                    // @0x430E8F
        if (alpha != 0) { blendCommon(static_cast<DWORD>(alpha) << 24); tssTail(); } // sinon → LABEL_33
    } else if (blendMode == 2) {                       // @0x430F0B
        blendCommon(static_cast<DWORD>(alpha) << 24);
        tssTail();
    } else {                                           // blendMode 0 @0x430DC8
        if (alpha != 0) {
            blendCommon(static_cast<DWORD>(-1 - static_cast<int>(alpha)) << 24); // (-1-v7)<<24 @0x430E3E
            tssTail();
        }
        // blendMode 0 & alpha 0 : AUCUN état posé (LABEL_34 direct).
    }

    // --- Glow : lumière directionnelle animée (ping-pong from/to) -----------------------------
    if (m.glowEnable == 1) {                           // @0x430F9C
        const double phase = static_cast<double>(m.glowSpeed) * t * kHundredth; // v68
        float col[3];
        for (int j = 0; j < 3; ++j) {
            const int from = m.glowFrom[j];            // *v23 (a3+32+4j)
            const int to   = m.glowTo[j];              // v23[4] (a3+48+4j)
            const double range = static_cast<double>(to - from) * kHundredth; // v69
            if (range <= 0.0) {
                col[j] = static_cast<float>(static_cast<double>(from) * kHundredth); // @0x43103E
            } else {
                const double tt   = phase / range;     // v70
                const int    k    = static_cast<int>(tt); // v42 = ftol(tt) (troncature)
                const double frac = range * (tt - static_cast<double>(k)); // v71
                const bool even = (k & 1) == 0;        // idiome de parité 0x80000001 @0x430FFF..0x431018
                col[j] = even ? static_cast<float>(static_cast<double>(from) * kHundredth + frac)  // @0x431020
                              : static_cast<float>(static_cast<double>(to)   * kHundredth - frac); // @0x43102F
            }
        }
        SetGxdLight(dev, col[0], col[1], col[2], 1.0f); // GXD_SetDirectionalLight(...,2,...,1.0) @0x4310A9
    } else {                                           // reset : mode 1 (ambient de scène — repli) @0x4310ED
        SetGxdLight(dev, args.sceneAmbient[0], args.sceneAmbient[1], args.sceneAmbient[2], 1.0f);
    }

    // --- UV-scroll : matrice de texture animée ------------------------------------------------
    if (m.uvEnable == 1) {                             // @0x4310FA
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, 2); // D3DTTFF_COUNT2 @0x431114
        const double sp = static_cast<double>(m.uvSpeed) * t * kHundredth;
        float u = 0.f, v = 0.f;
        switch (m.uvMode) {                            // @0x431157
            case 1:  u = static_cast<float>(sp); v = 0.f;                     break; // @0x431166
            case 2:  u = 0.f;                    v = static_cast<float>(sp);  break; // @0x43117C
            case 3:  u = static_cast<float>(sp); v = static_cast<float>(sp);  break; // @0x431198
            default: u = static_cast<float>(sp); v = static_cast<float>(-sp); break; // @0x4311C2/@0x4311CE
        }
        D3DXMATRIX tm; D3DXMatrixIdentity(&tm);
        tm._31 = u; tm._32 = v;                        // translation UV (v60=_31, v61=_32)
        dev->SetTransform(D3DTS_TEXTURE0, &tm);        // 16 @0x4311E9
    }

    // --- Dessin : billboard (quad face-caméra) ou géométrie ----------------------------------
    if (m.billboardEnable == 1 &&                      // @0x4311EE
        m.frameBbox.size() >= static_cast<size_t>(frameI) * 40 + 40 &&
        m.billboardTemplate.size() >= 80) {
        const size_t fb = static_cast<size_t>(frameI) * 40;
        const float minX = RdF32(m.frameBbox, fb + 0),  maxX = RdF32(m.frameBbox, fb + 12);
        const float minY = RdF32(m.frameBbox, fb + 4),  maxY = RdF32(m.frameBbox, fb + 16);
        const D3DXVECTOR3 centerMesh(RdF32(m.frameBbox, fb + 24), RdF32(m.frameBbox, fb + 28),
                                     RdF32(m.frameBbox, fb + 32)); // v32+6 = centre (offset+24)

        const float* basis = (m.billboardAxisMode == 1) ? args.billboardBasisAxis1
                                                        : args.billboardBasisOther; // v33
        const float halfW = (maxX - minX) * 0.5f;      // v48
        const float halfH = (m.billboardAxisMode == 1) ? halfW : (maxY - minY) * 0.5f; // v34/v49

        D3DXVECTOR3 c;
        D3DXVec3TransformCoord(&c, &centerMesh, &args.world); // Vec3_TransformCoord @0x431264
        const D3DXVECTOR3 right(basis[0], basis[1], basis[2]); // v33[0..2]
        const D3DXVECTOR3 up   (basis[3], basis[4], basis[5]); // v33[3..5]

        // Gabarit = header2 (UV bakées) ; on réécrit les xyz des 4 coins (stride 20). @0x431286..0x431378
        uint8_t quad[80];
        std::memcpy(quad, m.billboardTemplate.data(), 80);
        WriteXYZ(quad,  0, c.x - right.x * halfW - up.x * halfH,  // coin 0 : c - right·W - up·H
                           c.y - right.y * halfW - up.y * halfH,
                           c.z - right.z * halfW - up.z * halfH);
        WriteXYZ(quad, 20, c.x - right.x * halfW + up.x * halfH,  // coin 1 : c - right·W + up·H
                           c.y - right.y * halfW + up.y * halfH,
                           c.z - right.z * halfW + up.z * halfH);
        WriteXYZ(quad, 40, c.x + right.x * halfW - up.x * halfH,  // coin 2 : c + right·W - up·H
                           c.y + right.y * halfW - up.y * halfH,
                           c.z + right.z * halfW - up.z * halfH);
        WriteXYZ(quad, 60, c.x + right.x * halfW + up.x * halfH,  // coin 3 : c + right·W + up·H
                           c.y + right.y * halfW + up.y * halfH,
                           c.z + right.z * halfW + up.z * halfH);

        D3DXMATRIX id; D3DXMatrixIdentity(&id);
        dev->SetTransform(D3DTS_WORLD, &id);           // 256, identité (coins déjà en monde) @0x4313D3
        dev->SetTexture(0, d3dTex);                    // *(v43+52) @0x4313ED
        dev->SetFVF(258);                              // @0x431402
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, 20); // 5,2,quad,20 @0x431419
    } else {
        // Chemin géométrie GXD (a3+4 != 1 : VB/IB device). On uploade toujours en MANAGED → ce chemin.
        // (Le chemin CPU a3+4==1 / DrawIndexedPrimitiveUP @0x4314BE n'est pas emprunté : pas de buffer
        //  système. Documenté : nos buffers sont GPU.)
        if (sub.vb && sub.ib) {
            dev->SetTransform(D3DTS_WORLD, &args.world); // 256, g_WorldMatrix @0x4314EB
            dev->SetTexture(0, d3dTex);                  // *(v43+52) @0x4314EB (SetTexture)
            const UINT off = 20u * static_cast<UINT>(frameI) * sub.vertexCount; // 20·frame·vc @0x431520
            dev->SetStreamSource(0, sub.vb, off, 20);    // stride 20 (FVF 258)
            dev->SetIndices(sub.ib);                     // @0x43153A
            dev->SetFVF(258);                            // @0x43154F
            dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, sub.vertexCount, 0, sub.faceCount); // @0x43157B
        }
    }

    // --- Restaurations d'états (teardown) ----------------------------------------------------
    if (m.uvEnable == 1) {                             // @0x431581
        D3DXMATRIX id; D3DXMatrixIdentity(&id);
        dev->SetTransform(D3DTS_TEXTURE0, &id);        // 16 @0x4315DF
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, 0); // 24,0 @0x4315F5
    }
    dev->LightEnable(0, TRUE);                         // @0x431609 (vtable+212)
    SetGxdLight(dev, args.sceneAmbient[0], args.sceneAmbient[1], args.sceneAmbient[2], 1.0f); // SetLight(0, dword_18C5358) @0x431620 — repli : lumière de scène reconstruite

    // Teardown de fondu selon blendMode (miroir @0x431626..0x4317B1).
    auto restoreOpaque = [&]() {                       // LABEL_67
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);     // (0,6,1) @0x431639
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1); // (0,4,2=SELECTARG1) @0x431663
        SetRS(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);        // @0x431677
        SetRS(D3DRS_DESTBLEND, 1);                     // D3DBLEND_ZERO @0x43168B
        SetRS(D3DRS_SRCBLEND, 2);                      // D3DBLEND_ONE @0x43169F
        SetRS(D3DRS_ALPHABLENDENABLE, 0);              // @0x4316B3
        SetRS(D3DRS_ZWRITEENABLE, 1);                  // @0x4316D3
    };
    if (blendMode == 0) {
        if (alpha == 0) return;                        // rien posé → rien à restaurer @0x431633
        restoreOpaque();
    } else if (blendMode == 2) {
        restoreOpaque();
    } else {                                           // blendMode 1
        if (alpha != 0) restoreOpaque();               // @0x4316E1
        SetRS(D3DRS_ALPHAREF, 0);                      // @0x431789
        SetRS(D3DRS_ALPHAFUNC, 8);                     // D3DCMP_ALWAYS @0x43179D
        SetRS(D3DRS_ALPHATESTENABLE, 0);               // @0x4317B1
    }
}

} // namespace ts2::gfx

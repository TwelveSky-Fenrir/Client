// Gfx/EmitterMeshRenderer.h — upload GPU + dessin d'un mesh d'effet .MOBJECT2 (mesh d'emitter).
//
// Consomme asset::Mobject2 (FLOTTE A, déjà committé : Asset/Mobject2.h) et reproduit le rendu
// d'origine `Mesh_DrawAnimatedFrame 0x430BE0` (flipbook animé, timer QPC, glow/UV/billboard/alpha).
// MODULE AUTONOME : aucune écriture dans la boucle de rendu, aucun hook. Le device D3D9 et la
// matrice monde sont fournis PAR L'APPELANT (FLOTTE C câblera). VB/IB/textures en D3DPOOL_MANAGED
// → survivent à un Reset() : OnDeviceLost/OnDeviceReset sont des no-op (comme WorldGeometryRenderer
// / ModelObjectRenderer / MeshRenderer).
//
// ============================ ANCRES IDA (seule vérité, imagebase 0x400000) ===================
//   Mesh_DrawAnimatedFrame 0x430BE0  — CŒUR du rendu (décompilé/désassemblé le 2026-07-17) :
//       __userpurge(alpha@eax, overrideTexHolder@ecx, mesh@a3, pass@a4, frame@a5, lod@a6, phase@a7)
//   Mesh_DrawInstancesLOD  0x431A90  — appelant (frustum-cull + LOD distance + matrice Rz·Ry·Rx·T·S ;
//       pose g_WorldMatrix 0x18C52D4 puis appelle Mesh_DrawAnimatedFrame par instance).
//   GXD_SetDirectionalLight 0x403980 — helper glow (D3DLIGHT9 : Ambient=couleur, Type=DIRECTIONAL).
//   Mesh_ReadFile 0x430470 / Mesh_LoadMOBJECT2 0x4318C0 — loader (VB 20·N·vc FVF 258 @0x430897,
//       IB 6·fc INDEX16 @0x430A03, tex SOBJECT 56 o @0x430A80) — déjà porté (asset::Mobject2).
//   Vec3_TransformCoord 0x6BB612 (= D3DXVec3TransformCoord) ; timer QPC dbl_18C4F80/88 (freq/start).
//
// ============================ RÉSOLUTION D'UN RÉSIDU DE LA FLOTTE A ============================
//   Docs/TS2_DEEP_MOBJECT.md §3.3/§T5 et Asset/Mobject2.h marquaient le multiplicateur `20·N`
//   (N = a1[21]) comme « sémantique NON PROUVÉE ». `Mesh_DrawAnimatedFrame` la PROUVE :
//     - décalage de flux VB par frame = `20 * frame * vertexCount[i]`  (@0x431520 GXD / @0x4314BE CPU)
//     - décalage du bloc bbox/ancre    = `40 * frame`                   (@0x431207)
//   → N EST le NOMBRE DE FRAMES du flipbook (chaque frame = un bloc contigu de `vertexCount` sommets
//     de 20 o). La VB tient N frames ; la sélection de frame = offset de SetStreamSource, JAMAIS de
//     skinning. Le repli « N==1 » n'est donc plus nécessaire — on gère N frames fidèlement (et N==1
//     dégénère naturellement en mesh statique). Sémantique inchangée pour les autres tables (boneTable
//     40·N = 1 enregistrement bbox+ancre PAR FRAME, header2 80 o = gabarit de quad billboard).
#pragma once
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "Asset/Mobject2.h" // asset::Mobject2 / Mobject2Mesh / Mobject2Subset / SObjectTexture (FLOTTE A)
#include "Gfx/GpuTexture.h"  // gfx::GpuTexture (pont DDS -> IDirect3DTexture9, D3DPOOL_MANAGED)

namespace ts2::gfx {

// ---------------------------------------------------------------------------------------------
//  Représentation GPU (cache) d'un mesh MOBJECT2. Move-only (possède VB/IB/textures).
// ---------------------------------------------------------------------------------------------

// Un holder de texture = 1 texture GPU + son mode de fondu (holder+44 = SObjectTexture::alphaMode).
// Dans le binaire, Mesh_DrawAnimatedFrame lit `*(v43+44)` (blendMode 0/1/2) et `*(v43+52)` (texture
// D3D) sur le holder sélectionné (mainTex si non animé, extraTex[idx] si header1[0]==1).
struct EmitterTexHolder {
    GpuTexture tex;              // texture GPU (vide si absente) — a1[51]/a1[66] via Tex_ReadPacked
    uint32_t   blendMode = 0;    // holder+44 : 0=opaque, 1=alpha-test+blend, 2=additif
    bool       present   = false;

    EmitterTexHolder() = default;
    EmitterTexHolder(EmitterTexHolder&&) noexcept = default;
    EmitterTexHolder& operator=(EmitterTexHolder&&) noexcept = default;
    EmitterTexHolder(const EmitterTexHolder&) = delete;
    EmitterTexHolder& operator=(const EmitterTexHolder&) = delete;
};

// Un subset de rendu (= un niveau de LOD dans Mesh_DrawAnimatedFrame : l'appelant en sélectionne
// UN SEUL par le facteur de LOD a6 — cf. boucle @0x430DAC). Chaque VB tient les N frames.
// Move-only, PROPRIÉTAIRE de ses VB/IB (libérés au destructeur → clear()/Reset propres, zéro fuite).
struct EmitterGpuSubset {
    IDirect3DVertexBuffer9* vb          = nullptr; // 20·N·vertexCount o, FVF 258, MANAGED (a1[47] @0x430897)
    IDirect3DIndexBuffer9*  ib          = nullptr; // 6·faceCount o, INDEX16, MANAGED (a1[50] @0x430A03)
    uint32_t                vertexCount = 0;        // sommets PAR FRAME (a1[45][i])
    uint32_t                faceCount   = 0;        // triangles = primCount (a1[48][i])

    EmitterGpuSubset() = default;
    ~EmitterGpuSubset() { if (vb) vb->Release(); if (ib) ib->Release(); }
    EmitterGpuSubset(const EmitterGpuSubset&)            = delete;
    EmitterGpuSubset& operator=(const EmitterGpuSubset&) = delete;
    EmitterGpuSubset(EmitterGpuSubset&& o) noexcept
        : vb(o.vb), ib(o.ib), vertexCount(o.vertexCount), faceCount(o.faceCount) {
        o.vb = nullptr; o.ib = nullptr;
    }
    EmitterGpuSubset& operator=(EmitterGpuSubset&& o) noexcept {
        if (this != &o) {
            if (vb) vb->Release();
            if (ib) ib->Release();
            vb = o.vb; ib = o.ib; vertexCount = o.vertexCount; faceCount = o.faceCount;
            o.vb = nullptr; o.ib = nullptr;
        }
        return *this;
    }
};

// Un mesh du conteneur (miroir GPU de asset::Mobject2Mesh, 268 o d'origine).
struct EmitterGpuMesh {
    bool     valid      = false; // faux si mesh vide (type==0)
    uint32_t frameCount = 1;     // N = a1[21] (RÉSOLU : nb de frames — cf. bandeau .h)

    // --- header1 décodé (76 o, a1[2..20]) — flags lus par Mesh_DrawAnimatedFrame ---
    uint32_t animatedTex       = 0; // header1[0]  (mesh+8)  : ==1 ⇒ texture prise dans extraTex[] (flipbook)
    int32_t  animTexSpeed      = 0; // header1[1]  (mesh+12) : vitesse (×0.01) de la sélection temporelle
    int32_t  texMinFrame       = 0; // header1[2]  (mesh+16) : <1 ⇒ index temporel ; >=1 ⇒ mappé sur [min,max]
    int32_t  texMaxFrame       = 0; // header1[3]  (mesh+20)
    uint32_t glowEnable        = 0; // header1[4]  (mesh+24) : ==1 ⇒ lumière glow animée (GXD_SetDirectionalLight)
    int32_t  glowSpeed         = 0; // header1[5]  (mesh+28) : phase glow (×0.01)
    int32_t  glowFrom[3]       = {};// header1[6..8]  (mesh+32/36/40) : couleur « from » (×0.01)
    int32_t  glowTo[3]         = {};// header1[10..12](mesh+48/52/56) : couleur « to »  (×0.01)   (header1[9] inutilisé)
    uint32_t uvEnable          = 0; // header1[14] (mesh+64) : ==1 ⇒ défilement de matrice de texture
    uint32_t uvMode            = 0; // header1[15] (mesh+68) : 1=U, 2=V, 3=UV, else=U/-V
    int32_t  uvSpeed           = 0; // header1[16] (mesh+72) : vitesse (×0.01)
    uint32_t billboardEnable   = 0; // header1[17] (mesh+76) : ==1 ⇒ quad face-caméra (DrawPrimitiveUP)
    uint32_t billboardAxisMode = 0; // header1[18] (mesh+80) : ==1 ⇒ carré symétrique (largeur=hauteur)

    std::vector<EmitterGpuSubset> subsets;

    // Bloc bbox/ancre PAR FRAME (40·N o, a1[22] / mesh+88). Par frame : min.xyz(12) max.xyz(12)
    // centre.xyz(12) rayon(4). Le billboard lit centre (offset+24) + extents pour dimensionner le quad.
    std::vector<uint8_t> frameBbox;
    // Table parallèle 4·N (a1[23] / mesh+92) : 1 float PAR FRAME. Lu UNIQUEMENT en passe additive
    // (a4!=1, blendMode 2) pour moduler l'alpha : v7 = frameScale[frame] * (255 - alpha)  @0x430D20.
    std::vector<uint8_t> frameScale;
    // header2 (80 o, a1[24..43] / mesh+96..176) : SERT DE GABARIT au quad billboard — les UV y sont
    // bakées ; seules les positions xyz des 4 coins sont réécrites par frame (@0x431286..0x431378).
    std::vector<uint8_t> billboardTemplate;

    EmitterTexHolder              mainTex;   // a1[51] (mesh+204) : texture par défaut (cas non animé)
    std::vector<EmitterTexHolder> extraTex;  // a1[66] (mesh+264) : 56·extraTexCount (cas animé)

    EmitterGpuMesh() = default;
    EmitterGpuMesh(EmitterGpuMesh&&) noexcept = default;
    EmitterGpuMesh& operator=(EmitterGpuMesh&&) noexcept = default;
    EmitterGpuMesh(const EmitterGpuMesh&) = delete;
    EmitterGpuMesh& operator=(const EmitterGpuMesh&) = delete;
};

// Représentation GPU d'un conteneur .MOBJECT2 entier (array de meshes).
struct EmitterGpuObject {
    std::vector<EmitterGpuMesh> meshes;
    bool ok = false;

    EmitterGpuObject() = default;
    EmitterGpuObject(EmitterGpuObject&&) noexcept = default;
    EmitterGpuObject& operator=(EmitterGpuObject&&) noexcept = default;
    EmitterGpuObject(const EmitterGpuObject&) = delete;
    EmitterGpuObject& operator=(const EmitterGpuObject&) = delete;
};

// ---------------------------------------------------------------------------------------------
//  Paramètres de dessin — miroir EXACT des arguments de Mesh_DrawAnimatedFrame 0x430BE0.
// ---------------------------------------------------------------------------------------------
struct EmitterMeshDrawArgs {
    // Matrice monde (g_WorldMatrix 0x18C52D4). L'appelant la construit (Mesh_DrawInstancesLOD :
    // Rz(z°)·Ry(y°)·Rx(x°)·T(pos)·S). Posée par SetTransform(D3DTS_WORLD) et utilisée pour l'ancre billboard.
    D3DXMATRIX world;

    float   frame     = 0.0f; // a5 : index de frame (tronqué par ftol → v51)
    int     pass      = 1;    // a4 : ==1 ⇒ blendMode 0/1 ; !=1 ⇒ blendMode 2 (2 passes : opaque puis additif)
    float   lodFactor = 1.0f; // a6 : >=1.0 ⇒ LOD max (subset 0) ; sinon sélection par nb de faces
    float   timePhase = 0.0f; // a7 : phase ajoutée au timer (v50 = tempsÉcoulé + a7)
    uint8_t alpha     = 255;  // eax : octet d'alpha (v7) — module TEXTUREFACTOR / alpha additif

    // ecx (a2) : holder de texture d'override. nullptr ⇒ utilise la texture du mesh (mainTex/extraTex).
    const EmitterTexHolder* overrideTex = nullptr;

    // Base caméra du billboard (flt_18C5264 pour axisMode==1, unk_18C52BC sinon) : right.xyz + up.xyz.
    // Autonome : l'appelant fournit ces axes depuis sa caméra. Repli identité (right=X, up=Y) =
    // quad aligné-monde (visible mais non face-caméra) — documenté, jamais inventé.
    float billboardBasisAxis1[6] = {1.f, 0.f, 0.f,  0.f, 1.f, 0.f};
    float billboardBasisOther[6] = {1.f, 0.f, 0.f,  0.f, 1.f, 0.f};

    // Ambient de scène pour le chemin glow « reset » (GXD_SetDirectionalLight mode 1, source
    // a1+1124.. du singleton GXD absent en autonome). Repli blanc. N'affecte que glowEnable!=1.
    float sceneAmbient[3] = {1.f, 1.f, 1.f};
};

// ---------------------------------------------------------------------------------------------
//  EmitterMeshRenderer — cache + dessin. Autonome (device fourni par l'appelant).
// ---------------------------------------------------------------------------------------------
class EmitterMeshRenderer {
public:
    EmitterMeshRenderer() = default;
    ~EmitterMeshRenderer();
    EmitterMeshRenderer(const EmitterMeshRenderer&) = delete;
    EmitterMeshRenderer& operator=(const EmitterMeshRenderer&) = delete;

    // Upload d'un conteneur .MOBJECT2 déjà parsé (asset::Mobject2) vers un objet GPU autonome.
    // VB/IB (MANAGED) + textures (GpuTexture, MANAGED). Renvoie false si `dev` nul (out.ok=false).
    // Un mesh vide (type==0) est conservé mais marqué invalid (non dessiné) — parité avec le binaire.
    bool Upload(IDirect3DDevice9* dev, const asset::Mobject2& src, EmitterGpuObject& out) const;

    // Cache paresseux par chemin : parse (asset::Mobject2::Load) + Upload au 1er accès.
    // Renvoie nullptr si device nul, parse échoué (mémorisé), ou upload échoué. Propriété = ce cache.
    EmitterGpuObject* GetOrLoad(IDirect3DDevice9* dev, const std::string& path);
    void ReleaseCache();
    size_t ResidentCount() const { return cache_.size(); }

    // D3DPOOL_MANAGED : rien à recréer après Reset(). No-op documenté (symétrie avec les autres renderers).
    void OnDeviceLost()  {}
    void OnDeviceReset() {}

    // Dessine UN mesh (miroir byte-exact de Mesh_DrawAnimatedFrame 0x430BE0). `dev` = device courant.
    void DrawMesh(IDirect3DDevice9* dev, const EmitterGpuMesh& mesh, const EmitterMeshDrawArgs& args);

    // Dessine tous les meshes valides d'un objet (mêmes args). Les meshes vides sont ignorés.
    void DrawObject(IDirect3DDevice9* dev, const EmitterGpuObject& obj, const EmitterMeshDrawArgs& args);

private:
    // Timer QPC (dbl_18C4F80 freq / dbl_18C4F88 start). Initialisé paresseusement au 1er dessin.
    // v50 = (compteur - start)/freq + phase  (= secondes écoulées + phase).
    double ElapsedSeconds() const;

    // Upload d'un seul mesh (helper de Upload). `dev` non nul garanti par l'appelant.
    bool uploadMesh(IDirect3DDevice9* dev, const asset::Mobject2Mesh& src, EmitterGpuMesh& out) const;
    static bool uploadTexture(IDirect3DDevice9* dev, const asset::SObjectTexture& src, EmitterTexHolder& out);

    mutable bool           timerInit_  = false;
    mutable long long      freq_       = 0; // QueryPerformanceFrequency
    mutable long long      startCount_ = 0; // QueryPerformanceCounter au 1er dessin

    std::unordered_map<std::string, EmitterGpuObject> cache_;      // clé = chemin
    std::unordered_map<std::string, bool>             loadFailed_; // parse/upload échoués (ne pas retenter)
};

} // namespace ts2::gfx

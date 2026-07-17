// Gfx/ModelObjectRenderer.h — rendu des « model-objects » d'effet (.MOBJECT) de la banque
// MiscC (unk_B60AB8), TRANCHE MINIMALE : débloque le hook `s_meshDraw` de FxRenderer.cpp et
// rend VISIBLE le FX combat mesh (block/parry/deflect, types 8/9/0xA de Fx_EmitterDraw).
//
// FRONT F_MOBJ (2026-07-17). Vérité IDA : TwelveSky2.exe (idaTs2, imagebase 0x400000). Chaque
// bloc du .cpp porte son ancre. RECON byte-exacte : Docs/TS2_EXTRACT_MOBJ_DISPATCH.md,
// TS2_EXTRACT_MESHPART_FULL.md, TS2_EXTRACT_MOBJ_BANKS.md, TS2_EXTRACT_MOBJ_VERDICT.md.
//
// ===========================================================================================
//  CHAÎNE D'ORIGINE REPRODUITE (réduite au dessin de base diffus)
// ===========================================================================================
//   Fx_EmitterDraw 0x585E30 (déjà porté, FxRenderer.cpp) — s_meshDraw câblé ICI
//     -> ModelObj_Draw 0x4D71B0        : lazy-load + reset d'états D3D + Model_RenderWithShadow_0
//        -> ModelObj_Load 0x4D6F80      : (V1 : SYNCHRONE — cf. divergence assumée ci-dessous)
//           -> Model_LoadFromFile 0x6A3490 -> MeshPart_Load 0x6AD160  (parseur DÉJÀ écrit :
//              asset::MObject, Asset/Model.h — RÉUTILISÉ tel quel, byte-exact)
//        -> Model_RenderWithShadow_0 0x6A4110 : matrice Rz*Ry*Rx*T, frame = Crt_Dbl2Uint(a3)
//           gate [0, parts[0].A-1], frustum-cull par-part (Cam_FrustumTestSphere 0x69EF90, ×1)
//           -> MeshPart_RenderFull 0x6B0850 : cœur base-draw @0x6B1327 (SetStreamSource(0, VB,
//              32*frame*B, 32) + SetIndices(IB) + SetTexture(0, tex0) + DrawIndexedPrimitive(
//              TRIANGLELIST, 0, 0, B, 0, D)) — IDENTIQUE au chemin monde déjà porté
//              (WorldGeometryRenderer::renderObjects, MeshPart_Render 0x6AED60).
//
//  RÉSOLUTION DE LA BANQUE (prouvée AssetMgr_InitAllSlots 0x4DEB50 @0x4dee8c) : la banque MiscC
//  (unk_B60AB8 = g_ModelMotionArray 0x8E8B30 + 2588552) est peuplée par la boucle
//  `for(i6<246) ModelObj_BuildPath(base + 148*i6, categorie=4, i6, 0, 0)` -> catégorie 4 =
//  `G03_GDATA\D02_GMOBJECT\003\E%03d001.MOBJECT` avec `%03d = idxC + 1` (ModelObj_BuildPath
//  0x4D6E20 @0x4d6ed5). Donc slot idxC ∈ [0,245] -> fichier E{idxC+1:03}001.MOBJECT.
//  idxC connus (déjà écrits par FxSetters.cpp) : 131 = Deflect ; 135/156 = BlockGuard/Parry ;
//  230..233 = auras de morph (Fx_DrawZoneAura, non routé par ce hook — cf. INTÉGRATION MAIN).
//
// ===========================================================================================
//  TRANCHE MINIMALE — ce qui est fait, ce qui est différé (TODO ancré, PAS inventé)
// ===========================================================================================
//  FAIT (chemin FX a4=0/a6=0 qui TUE texture-projetée + alpha-fade) : dessin de base diffus
//  frame-indexé, transform Rz*Ry*Rx*T du slot, cull par-part, passes 1/2, cache paresseux.
//  DIFFÉRÉ (branches optionnelles de MeshPart_RenderFull, gatées par des flags d'en-tête de la
//  part — voir §6 de TS2_EXTRACT_MESHPART_FULL.md) :
//    - glow émissif animé   (part+180/this[45], part+184 mode)          TODO ancre 0x6B0A11
//    - UV-scroll matrice tex (part+220/this[55], part+240/this[60])     TODO ancre 0x6B0F59
//    - sprite billboard glow (part+232/this[58])                        TODO ancre 0x6B107C
//    - 2e texture / overlay  (part+396/this[99])                        TODO ancre 0x6B19AD
//    - texture animée flipbook (part+212/this[53] -> materials[idx])    TODO ancre 0x6B0D33
//  Ces branches restent des surcouches : à champs d'en-tête nuls (cas par défaut du binaire),
//  MeshPart_RenderFull dégénère EXACTEMENT en ce dessin de base. Bank AvatarA/NpcB (types 1-4)
//  non gérées en V1 (une seule banque MiscC) — TODO ancre.
//
//  DÉGRADATION HONNÊTE (placement) : le placement exact sur l'os d'arme
//  (SObject_Draw 0x4D8F90 -> Model_GetAttachTransform 0x40FDC0) n'est PAS porté. `pos`/`orient`
//  viennent du slot (que MAIN peuple au centre de l'entité source, comme les particules) : le
//  mesh est VISIBLE mais placé au centre de l'entité, pas au bout de l'arme. Aucune transformée
//  d'os inventée. Les auras (Char_DrawAura/Fx_DrawZoneAura) passent, elles, des positions
//  d'entité RÉELLES -> pleinement fidèles si MAIN les route vers ce renderer (hors périmètre V1).
//
//  DIVERGENCE ASSUMÉE (chargement) : ModelObj_Load 0x4D6F80 avec a6=0 pousse une requête
//  ASYNCHRONE (MobjLoader_Enqueue 0x4E68E0, thread de fond) ; ici le chargement est SYNCHRONE au
//  1er dessin (asset::MObject::Load). Aucun format/protocole/gameplay altéré — le mesh apparaît
//  la même frame au lieu de la suivante. TODO ancre : porter le loader async si la fidélité du
//  threading devient un objectif.
#pragma once
#include "Gfx/Renderer.h"
#include "Gfx/FxRenderer.h"   // FxMeshBank, FxModelObjDrawFn (hook s_meshDraw)
#include "Asset/Model.h"      // asset::MObject / MeshPart / MGeometry / MTexture (parseur porté)
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ts2::gfx {

class ModelObjectRenderer {
public:
    ~ModelObjectRenderer();
    ModelObjectRenderer() = default;
    ModelObjectRenderer(const ModelObjectRenderer&) = delete;
    ModelObjectRenderer& operator=(const ModelObjectRenderer&) = delete;

    // renderer doit déjà être Init() (device D3D9 prêt) ; gameDataDir = racine "GameData"
    // (même convention que Gfx/ModelCache et Game/GameDatabase). Enregistre CETTE instance comme
    // renderer actif du shim de hook (ModelObjectRenderer_MeshDrawShim).
    bool Init(Renderer& renderer, std::string gameDataDir);
    void Shutdown();

    // D3DPOOL_MANAGED : survit à un Reset() sans re-upload (même politique que
    // WorldGeometryRenderer / MeshRenderer). No-op — cf. .cpp pour la note de recréation device.
    void OnDeviceLost();
    void OnDeviceReset();

    // À appeler UNE FOIS/frame AVANT les passes Fx_EmitterDraw : fournit les matrices vue/proj de
    // la frame, d'où sont reconstruits les 6 plans du frustum (miroir des plans g_GfxRenderer+334
    // lus par Cam_FrustumTestSphere 0x69EF90). Sans cet appel, le cull par-part est DÉSACTIVÉ
    // (toutes les parts présentes sont dessinées — dégradation sûre, jamais de sur-cull).
    void SetFrame(const D3DXMATRIX& view, const D3DXMATRIX& proj);

    // Miroir de ModelObj_Draw 0x4D71B0 (réduit) — appelé par le hook s_meshDraw depuis
    // Fx_EmitterDraw pour un slot MESH. `bank` : seule MiscC est gérée en V1. `idxC` : slot de la
    // banque (fichier E{idxC+1}001.MOBJECT). `pass` ∈ {1,2}. `drawParam` = index de frame (a3).
    // `pos` (3 floats monde) / `orient` (3 floats euler degrés) = a4/a5 du binaire.
    void MeshDraw(FxMeshBank bank, int idxA, int idxB, int idxC,
                  int pass, float drawParam, const float pos[3], const float* orient);

    // Sanité : nombre d'entrées .MOBJECT résidentes dans le cache MiscC.
    size_t ResidentCount() const { return cacheMiscC_.size(); }

private:
    // Part GPU d'un .MOBJECT (miroir MeshPart 408 o, champs GPU) — même disposition que
    // WorldGeometryRenderer::StaticObject, source = asset::MeshPart au lieu de WorldMeshPart.
    struct GpuPart {
        IDirect3DVertexBuffer9* vb      = nullptr; // 32*A*B o (A frames), MeshPart+288 (a1[72])
        IDirect3DIndexBuffer9*  ib      = nullptr; // 6*D o (partagé toutes frames), MeshPart+292 (a1[73])
        IDirect3DTexture9*      diffuse = nullptr; // tex0 (POSSÉDÉE), MeshPart+344 (a1[86])
        uint32_t                A = 1;             // frames du flipbook (MGeometry.M = MeshPart+252)
        uint32_t                B = 0;             // sommets par frame (MGeometry.V = MeshPart+256)
        uint32_t                D = 0;             // triangles/primCount (MGeometry.I = MeshPart+264)
        // Bloc bbox par frame (A*64 o, MeshPart+284 / a1[71]) : par élément 64 o, centre vec3 @+48,
        // rayon @+60 — source du frustum-cull par-part (Model_RenderWithShadow_0 @0x6a431b/@0x6a4339).
        std::vector<uint8_t>    frameBbox;
    };
    // Entrée de banque = un slot ModelObj 148 o (miroir : conteneur Model chargé + parts GPU).
    struct MObjEntry {
        bool                 loaded      = false; // ModelObj+0 : chargé avec succès
        bool                 loadFailed  = false; // échec mémorisé (ne pas retenter chaque frame)
        uint32_t             frameCountA = 0;     // parts[0].A (borne du gate frame, MeshPart+252)
        std::vector<GpuPart> parts;               // parts effectivement uploadées (VB/IB/tex)
    };

    // Miroir ModelObj_Load 0x4D6F80 (SYNCHRONE) : résout E{idxC+1}001.MOBJECT, parse (MObject::Load)
    // et uploade au 1er accès. nullptr si idxC hors [0,245] ou échec chargement (mis en cache).
    MObjEntry* getOrLoadMiscC(int idxC);
    bool uploadPart(const asset::MeshPart& part, GpuPart& out);
    void releaseEntry(MObjEntry& e);
    void releaseAll();

    // World = Rz(orient.z°)*Ry(orient.y°)*Rx(orient.x°)*T(pos) — ordre IDENTIQUE à
    // Model_RenderWithShadow_0 0x6a41a3-0x6a4299 (= WorldGeometryRenderer::BuildInstanceWorldMatrix).
    static D3DXMATRIX BuildWorldMatrix(const float pos[3], const float* orient);
    static IDirect3DTexture9* createTexture(IDirect3DDevice9* dev, const asset::MTexture& tex);
    // Cam_FrustumTestSphere 0x69EF90 (marge ×1 : plane·c + d >= -rayon) sur les 6 plans reconstruits.
    bool sphereInFrustum(const D3DXVECTOR3& c, float radius) const;

    IDirect3DDevice9*                  dev_ = nullptr;
    std::string                        gameDataDir_;
    bool                               ready_      = false;
    bool                               frameValid_ = false; // SetFrame appelé cette frame ?
    float                              planes_[6][4] = {};  // 6 plans frustum (rentrants, normalisés)
    std::unordered_map<int, MObjEntry> cacheMiscC_;         // banque MiscC (unk_B60AB8), clé = idxC
};

// ---------------------------------------------------------------------------
// Shim libre du hook FxModelObjDrawFn (s_meshDraw) : forwarde vers le renderer actif enregistré
// par ModelObjectRenderer::Init. No-op si aucun renderer actif (ordre de câblage indifférent).
// Signature STRICTEMENT identique à FxModelObjDrawFn (FxRenderer.h).
void ModelObjectRenderer_MeshDrawShim(FxMeshBank bank, int idxA, int idxB, int idxC,
                                      int pass, float drawParam,
                                      const float pos[3], const float* orient);

} // namespace ts2::gfx

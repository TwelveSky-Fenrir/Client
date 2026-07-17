// Gfx/MeshPartMaterial.h — machine à états matériau fixed-function des « parts » d'objets
// statiques GXD (.MOBJECT / .WO), port LIGNE À LIGNE de MeshPart_RenderFull 0x6B0850.
//
// FRONT B1:meshpart-material (2026-07-17). Vérité UNIQUE = IDA (idaTs2, TwelveSky2.exe,
// imagebase 0x400000). Chaque bloc du .cpp porte son ancre 0xADDR. Aucun accès IDB en écriture.
// Recon byte-exacte : Docs/TS2_DEEP_MESHPART_MATERIAL.md (§4 ordre d'exécution, §5 helpers,
// §6 tables de décodage) + Docs/TS2_DEEP_MOBJECT.md (§1 layout MeshPart 408 o, §4 sous-chemins).
//
// ===========================================================================================
//  RÔLE — ce que reproduit ce module
// ===========================================================================================
// MeshPart_RenderFull 0x6B0850 dessine UN part (sous-maillage) d'un objet statique du décor
// avec son matériau COMPLET : jusqu'à 9 couches fixed-function empilées par UNE machine à états.
// Le « base-draw » (SetStreamSource(0,VB,32*frame*B,32) + SetIndices + DrawIndexedPrimitive,
// ancre @0x6B1327) est DÉJÀ porté (ModelObjectRenderer.cpp / WorldGeometryRenderer.cpp) ; ce
// module reproduit les couches AU-DELÀ du base-draw et ré-inclut le base-draw pour être un
// remplaçant complet (drop-in : cf. Docs/TS2_DEEP_MOBJECT.md §7 T4/T5 —
// « remplacer le draw de base par MeshPartMaterial::Render(...) »).
//
//   (1) lumière émissive animée (ping-pong triangulaire)   @0x6B08AF  gate mat.lightAnim.Enable
//   (1b) neutralisation de la lumière 0                    @0x6B099B  gate mat.noLight
//   (2) glow spéculaire vue-dépendant (D3DMATERIAL9.Specular + lumière dir. vers caméra + RS29)
//                                                          @0x6B0A11 / re-glow @0x6B168D  gate mat.glow
//   (3) texture animée flipbook (atlas, sélection par temps) @0x6B0D33  gate mat.flipbook.Enable
//   (4) UV-scroll matrice de texture tex1 @0x6B0F59 / tex2 @0x6B19BB   gate mat.uvScroll.texN.Enable
//   (5) 2e texture (2e passe blendée, même géométrie)       @0x6B19AD  gate (tex.second != nullptr)
//   + décalque projeté (a4)                                 @0x6B0B9B  argument decal
//   + fondu distance alpha (a6)                             (blocs if(a6))  argument alphaFade
//   + BILLBOARD (face-caméra)                               @0x6B107C  gate mat.billboard.Enable
//        -> TODO ANCRE : les bases d'axes flt_8001D4 (0x8001D4) / unk_80022C (0x80022C) sont un
//           ÉTAT RUNTIME NON PROUVÉ (cf. TS2_DEEP_MESHPART_MATERIAL.md §10). On NE fabrique PAS
//           d'axe caméra. Repli honnête = draw indexé de la géométrie stockée (mesh VISIBLE mais
//           NON billboardé). Le reste de la machine à états (lumière, retour) reste fidèle.
//
// ===========================================================================================
//  HELPERS Gfx_* RÉIMPLÉMENTÉS EN D3D9 DIRECT (le singleton FF g_GfxRenderer 0x7FFE18 est absent)
// ===========================================================================================
//  Gfx_SetMaterialEmissive 0x69D1F0 -> D3DMATERIAL9 { Diffuse(1,1,1,1), Ambient(1,1,1,1),
//     Specular=glow.SpecRGBA, Emissive(0,0,0,1), Power=glow.SpecPower } + SetMaterial.
//  Gfx_SetShadowProjLight 0x69D7A0 -> D3DLIGHT9 DIRECTIONAL slot 1 : Specular =
//     (sceneCenter + lightOffset) * intensity ; Direction = cameraAt - cameraEye ; + LightEnable(1,TRUE)
//     + SetRenderState(SPECULARENABLE, TRUE).
//  Gfx_SetLight 0x69D5C0 (slot 0) -> D3DLIGHT9 DIRECTIONAL, Direction=(-1,-1,1) : mode 1 =>
//     Ambient = sceneCenter ; mode 2 => Ambient = (r,g,b,a) fournis.
//  Gfx_ApplyMeshMaterial 0x69D0E0 -> restaure le D3DMATERIAL9 (snapshot d'entrée du device).
//  Gfx_DisableMeshLighting 0x69D9C0 -> LightEnable(1,FALSE) + SetRenderState(SPECULARENABLE,FALSE).
//  Gfx_SkyboxEndState 0x69D780 -> restaure la lumière 0 (snapshot d'entrée du device).
//
//  L'état runtime que le singleton FF tenait en globals (matrice monde dword_800244, œil caméra
//  g_CameraPos 0x800130, cible caméra +804..812, direction soleil flt_800308.., centre AABB
//  scène +1204*0.5+1236) est fourni par l'APPELANT via MeshPartRuntime — AUCUNE valeur inventée.
//
// ===========================================================================================
//  AUTONOMIE
// ===========================================================================================
// Module STATELESS : ne possède AUCUNE ressource D3D (ni VB/IB/texture ni pool DEFAULT) — il ne
// fait que poser des états et dessiner avec des ressources FOURNIES par l'appelant. Donc RIEN à
// recréer sur device-lost (pas de OnDeviceLost/Reset). Aucun câblage dans la boucle de rendu :
// FLOTTE C/MAIN appellera Render() depuis ModelObjectRenderer/WorldGeometryRenderer.
//
// PRÉ-CONDITION D'APPEL (fidèle à Model_RenderWithShadow_0 0x6A4110), à poser AVANT Render() :
//   (a) SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1 = 0x112 = 274) @0x6a4186 — le base-draw et le
//       billboard supposent le stride 32 o ; MeshPart_RenderFull ne repose PAS la FVF elle-même.
//   (b) SetTransform(D3DTS_WORLD, matriceMonde) @0x6a4299 — MeshPart_RenderFull ne pose la matrice
//       monde QUE dans la branche billboard (identité puis restauration). rt.world sert au transform
//       des centres de nœuds (D3DXVec3TransformCoord) et à la restauration billboard.
#pragma once
#include "Asset/Model.h"   // asset::MeshPartMaterial (FLOTTE A — décodé, champs nommés)
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>

namespace ts2::gfx {

// -------------------------------------------------------------------------------------------
// Ressources GPU d'un part (miroir MeshPart 408 o, champs consommés par 0x6B0850).
// -------------------------------------------------------------------------------------------
struct MeshPartGpu {
    IDirect3DVertexBuffer9* vb = nullptr;   // this[72] (+288) — 32*A*B o (A frames contiguës)
    IDirect3DIndexBuffer9*  ib = nullptr;   // this[73] (+292) — 6*D o (INDEX16, partagé)
    uint32_t vertsPerFrame = 0;             // B = this[64] (+256) — numVerts du DrawIndexedPrimitive
    uint32_t triCount      = 0;             // D = this[66] (+264) — primCount
    uint32_t frameCount    = 1;             // A = this[63] (+252) — borne défensive de frame
    // Bloc nœuds/bbox (A*64 o, this[71]/+284) : par frame 64 o — centre vec3 @+48 (glow fresnel,
    // billboard), axes billboard @+36 (NON reproduits, cf. TODO billboard). nullptr => sauts sûrs.
    const uint8_t* frameNodes = nullptr;
};

// -------------------------------------------------------------------------------------------
// Textures résolues (pointeurs COM extraits des holders 52 o à l'offset +48).
// -------------------------------------------------------------------------------------------
struct MeshPartTextures {
    IDirect3DTexture9* base       = nullptr; // tex0 diffuse : this[86] (+344) ; mode this[85]
    int                baseMode   = 0;       // this[85] (+340) : 1=alpha-test / 2=blend / autre
    IDirect3DTexture9* second     = nullptr; // tex1 2e texture : this[99] (+396) ; mode this[98]
    int                secondMode = 0;       // this[98] (+392) : 1=alpha-test / 2=blend / autre
    // Flipbook : tableau de pointeurs texture DÉJÀ extraits (this[101]+52*i+48), taille = this[100].
    const IDirect3DTexture9* const* flipbook = nullptr; // this[101] (+404) réduit à [tex...]
    uint32_t flipbookCount = 0;                          // this[100] (+400) — modulo d'index
};

// -------------------------------------------------------------------------------------------
// État runtime que le singleton FF g_GfxRenderer 0x7FFE18 tenait en globals — FOURNI par
// l'appelant (aucune valeur inventée ; chaque champ porte son ancre de global d'origine).
// -------------------------------------------------------------------------------------------
struct MeshPartRuntime {
    // Matrice monde courante = dword_800244 (posée par l'appelant via SetTransform(256)). Sert au
    // transform des centres de nœuds (Vec3_TransformCoord @0x6b0a81/@0x6b11a4) et à la restauration
    // WORLD de la branche billboard. NE sert PAS à (re)poser D3DTS_WORLD du base-draw.
    D3DXMATRIX  world;
    D3DXVECTOR3 cameraEye   = {0.0f, 0.0f, 0.0f}; // g_CameraPos 0x800130 / flt_800134 / flt_800138
    D3DXVECTOR3 cameraAt    = {0.0f, 0.0f, 0.0f}; // cible caméra +804..812 ; dir proj-light = at - eye
    D3DXVECTOR3 sunDir      = {0.0f, 0.0f, 0.0f}; // flt_800308/30C/310 (le glow fresnel dote -sunDir)
    D3DXVECTOR3 sceneCenter = {0.0f, 0.0f, 0.0f}; // centre AABB scène = +1204*0.5 + 1236 (couleur proj-light)
    bool worldValid = false;                      // rt.world exploitable (sinon fresnel/billboard sautés)
    MeshPartRuntime() { D3DXMatrixIdentity(&world); }
};

// -------------------------------------------------------------------------------------------
// Décalque projeté (argument a4 de 0x6B0850) : struct texture 52 o — +44 mode, +48 pointeur tex.
// nullptr (ou tex==nullptr) = pas de décalque (chemin FX standard : a4=0).
// -------------------------------------------------------------------------------------------
struct MeshPartDecal {
    int                mode = 0;       // a4+44 : 1=alpha-test / 2=blend / autre
    IDirect3DTexture9* tex  = nullptr; // a4+48
};

// ===========================================================================================
//  MeshPartMaterialRenderer — machine à états STATELESS (méthodes statiques). Le nom de classe
//  diffère d'asset::MeshPartMaterial (le descripteur consommé) pour éviter toute collision.
// ===========================================================================================
class MeshPartMaterialRenderer {
public:
    // Port complet de MeshPart_RenderFull 0x6B0850. Pose les 9 couches d'états/matrices puis
    // dessine (base-draw @0x6B1327 + éventuelle 2e passe @0x6B1C17), et restaure symétriquement
    // TOUS les états modifiés (flags v73/v74/v91/v92/v38/v64 du binaire).
    //
    //  dev        : device D3D9 (g_GfxRenderer_pDevice 0x800074 dans le binaire).
    //  mat        : en-tête matériau 120 o DÉCODÉ (asset::MeshPartMaterial, FLOTTE A). Si
    //               mat.decoded == false => AUCUNE couche : retombe au base-draw pur (tex.base).
    //  geo        : VB/IB/B/D + bloc nœuds (base-draw + centres de frustum/billboard).
    //  tex        : tex0/tex1/flipbook résolus.
    //  frame      : index de frame de morph (a2) — présumé déjà borné [0,A-1] par l'appelant ;
    //               re-borné défensivement sur geo.frameCount pour l'offset de stream/nœud.
    //  animTime   : v66 = Terrain_PushRenderState() + a3 (secondes QPC + phase) — horloge maîtresse
    //               de TOUTES les couches animées (ping-pong, flipbook, UV-scroll). @0x6b0871/@0x6b0883.
    //  glowEnable : a5 (autorise le glow spéculaire). Chemin FX = 1.
    //  alphaFade  : a6 (fondu distance, 0..255) — 0 = tout le fondu MORT (chemin FX). @0x6b0bdf etc.
    //  decal      : a4 (décalque projeté) ou nullptr.
    static void Render(IDirect3DDevice9* dev,
                       const asset::MeshPartMaterial& mat,
                       const MeshPartGpu& geo,
                       const MeshPartTextures& tex,
                       int frame,
                       float animTime,
                       const MeshPartRuntime& rt,
                       int glowEnable = 1,
                       uint8_t alphaFade = 0,
                       const MeshPartDecal* decal = nullptr);

private:
    MeshPartMaterialRenderer() = delete;
};

} // namespace ts2::gfx

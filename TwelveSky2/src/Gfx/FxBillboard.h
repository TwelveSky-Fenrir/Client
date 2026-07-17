// Gfx/FxBillboard.h — LEAF billboard « Object A » des FX de COMBAT/SKILL (.PARTICLE).
//
// Réécriture FIDÈLE (bit-exacte visée) du chemin particule des effets de combat/skill de
// TwelveSky2. La vérité est l'IDB de TwelveSky2.exe (imagebase 0x400000) ; chaque bloc cite
// son ancre IDA (nom + 0xADDR). Spec octet-exacte : Docs/TS2_EXTRACT_FX_COMBAT.md.
//
// ┌─ Ce que fait cet étage (F_FXCOMBAT 1/2, fondation) ────────────────────────────────────┐
// │ (1) LOADER d'un template .PARTICLE par INDEX (0..51) :                                  │
// │       miroir de Fx_NodeLoadFromFile 0x6A6680 + FxParticle_BuildPath 0x4D9E60            │
// │       ("G03_GDATA\D05_GPARTICLE\%03d.PARTICLE", index+1 -> 001..052.PARTICLE).          │
// │       Table paresseuse de 52 templates (AssetMgr_InitAllSlots 0x4DEB50 @0x4E03F6,       │
// │       chargement 1re-utilisation SObject_EnsureLoadedK 0x4D9EB0).                       │
// │ (2) CYCLE DE VIE d'un pool POBJECT 48o (Object A) : Init si non-initialisé, UpdateEmit  │
// │       sinon ; RenderBillboards. Wrappers lazy-load :                                    │
// │       SObject_UpdateK 0x4D9F00 / Particle_EnsureLoadedThenUpdateEmit 0x4D9F40 /         │
// │       Particle_EnsureLoadedThenRender 0x4D9F90.                                         │
// └────────────────────────────────────────────────────────────────────────────────────────┘
//
// ⚠ FRONTIÈRE (Docs/TS2_EXTRACT_FX_COMBAT.md §7.2) : « Object A » = pool 48o + template 232o
//   + Particle_RenderBillboards 0x6A70B0, fichiers .PARTICLE. C'est LE MÊME MOTEUR que les
//   émetteurs de zone .WP (Gfx/ZoneFxEmitter.*, Vague C) — mêmes fonctions IDA (Particle_Init
//   0x6A7020, Particle_UpdateEmit 0x6A7530, Particle_RenderBillboards 0x6A70B0, Particle_Free
//   0x6A6FF0, Particle_ComputeGradients 0x6A6D10). On RÉUTILISE donc à l'IDENTIQUE les structs
//   (FxEmitterTemplate 232o, FxParticlePool 48o) et les primitives ZoneFx_* de ZoneFxEmitter.h.
//   Seuls le LOADER (.PARTICLE au lieu de .WP embarqué) et la TABLE de 52 templates sont
//   spécifiques au combat -> c'est ce que ce fichier ajoute.
//   NE PAS confondre avec « Object B » (Gfx/ParticleSystem.h : PtclDef 236o / PtclPool 60o /
//   PtclDef_RenderQuads 0x424430, fichiers .PTCL) — système DISTINCT (particules monde/météo).
//
// N'utilise que le SDK Windows/Direct3D9 + d3dx9 (comme le binaire).
#pragma once
#include "Gfx/ZoneFxEmitter.h"   // FxEmitterTemplate 232o, FxParticlePool 48o, ZoneFx_* (moteur Object A),
                                  // ZoneFxFrameParams, FxFrustumFn, Particle/Billboard_Vertex (partagés)
#include <windows.h>
#include <d3d9.h>
#include <cstddef>

namespace ts2::gfx {

// ---------------------------------------------------------------------------------------------
// Nombre de templates .PARTICLE = boucle `i88 < 52` d'AssetMgr_InitAllSlots 0x4DEB50 @0x4E03F6
// (stride 336o). 52 fichiers 001.PARTICLE..052.PARTICLE présents sur disque (spec §6.4).
inline constexpr int kFxParticleTemplateCount = 52;

// Taille d'un slot de table de template : {loaded@+0, path@+4, node@+104} = 336o (0x150). Prouvé
// par SObject_EnsureLoadedK 0x4D9EB0 (Fx_NodeLoadFromFile(this+104, this+4, 1, 1)). Ici on modélise
// la même donnée en membres C++ ; l'octet-exactitude de l'agencement mémoire n'est pas requise
// (on reconstruit la structure de données, pas l'adresse runtime byte_1151CBC/byte_86918C).
inline constexpr int kFxTemplateSlotStride = 336;

// =============================================================================================
//  CONFIGURATION (posée UNE FOIS par MAIN avant le premier chargement/rendu)
// =============================================================================================

// Device D3D9 partagé (g_GfxRenderer 0x7FFE18 dans le binaire). Nécessaire pour créer les textures
// GPU des templates (D3DPOOL_MANAGED -> survivent à un device-reset, aucune recréation à câbler).
// Si nul au moment d'un chargement, le template se charge quand même mais sans texture (gpuTex=null).
void FxBillboard_SetDevice(IDirect3DDevice9* device);

// Racine du répertoire GameData (là où vit G03_GDATA\D05_GPARTICLE\NNN.PARTICLE). Défaut "GameData".
// Le chemin final = <root> + "\\" + FxParticle_BuildPath(index).
void FxBillboard_SetDataRoot(const char* root);

// =============================================================================================
//  LOADER / TABLE DE TEMPLATES
// =============================================================================================

// FxParticle_BuildPath 0x4D9E60 : écrit "G03_GDATA\D05_GPARTICLE\%03d.PARTICLE" (index+1) dans `dst`
// (dstSize octets, terminé NUL). index 0 -> "001.PARTICLE", …, index 51 -> "052.PARTICLE".
void FxBillboard_BuildPath(char* dst, size_t dstSize, int index);

// Charge (paresseusement) et renvoie le TEMPLATE partagé 232o pour `index` (0..kFxParticleTemplateCount).
// Miroir de SObject_EnsureLoadedK 0x4D9EB0 : au 1er appel, ouvre le .PARTICLE et le parse via le
// loader Fx_NodeLoadFromFile 0x6A6680 (texture + piste quat + 144o de champs). Renvoie nullptr si
// `index` est hors bornes ou si le chargement échoue. Le pointeur est STABLE (table statique jamais
// redimensionnée) -> il peut être conservé dans pool->tmpl par ZoneFx_Init.
FxEmitterTemplate* FxBillboard_GetTemplate(int index);

// true si le template `index` est déjà chargé (flag SObject_EnsureLoadedK @+0). Aucun chargement.
bool FxBillboard_IsTemplateLoaded(int index);

// Libère toutes les textures GPU des templates chargés et remet la table à zéro (enabled=0,
// loaded=false). À appeler au shutdown du renderer. (D3DPOOL_MANAGED -> inutile sur device-reset.)
void FxBillboard_FreeAllTemplates();

// =============================================================================================
//  CYCLE DE VIE D'UN POOL POBJECT 48o (le pool appartient à l'APPELANT — inline dans son slot FX)
//  Les wrappers assurent que le template est chargé, puis pilotent le moteur Object A partagé.
// =============================================================================================

// SObject_UpdateK 0x4D9F00 : ensure-loaded, puis Particle_Init(pool, template) 0x6A7020.
// (Re)alloue le tableau de particules si pool->flag==0. À appeler quand le pool est neuf.
void FxBillboard_PoolInit(FxParticlePool* pool, int index);

// Particle_EnsureLoadedThenUpdateEmit 0x4D9F40 : ensure-loaded, puis Particle_UpdateEmit 0x6A7530.
// `dt` = vrai delta de frame ; `pos`/`rot` (3 floats) = origine/orientation d'émission (degrés) ;
// `frustum` (optionnel, nullptr => toujours visible) cull l'origine avant intégration/émission.
void FxBillboard_PoolUpdate(FxParticlePool* pool, int index, float dt,
                            const float pos[3], const float rot[3], FxFrustumFn frustum);

// Tick pratique par frame = câblage update du binaire (« if(flag) UpdateEmit else Init » ;
// cf. WorldGeometryRenderer::updateFx / boucle 2 MapColl_UpdateObjectAnim @0x694AF0, et le chemin
// combat SObject_UpdateK vs Particle_EnsureLoadedThenUpdateEmit). 1re frame = Init (pas d'émission).
void FxBillboard_PoolTick(FxParticlePool* pool, int index, float dt,
                          const float pos[3], const float rot[3], FxFrustumFn frustum);

// Particle_EnsureLoadedThenRender 0x4D9F90 : ensure-loaded, puis Particle_RenderBillboards 0x6A70B0.
// Rend les billboards des particules vivantes (quads camera-facing via `right`/`up`, scratch CPU
// interne, DrawPrimitiveUP TRIANGLELIST stride 24). `right`/`up` = base billboard monde (flt_8001D4..E8
// = droite/haut caméra × ... ) fournie par l'appelant depuis la matrice vue. `maxQuads` = plafond
// quads/batch (dword_7FFEE0 ; 0 => pas de plafond). `frustum` optionnel (Cam_FrustumTestPoint6
// 0x69ED30). Renvoie le nombre de quads dessinés (0 si pool non initialisé / hors frustum).
int FxBillboard_PoolRender(FxParticlePool* pool, int index, IDirect3DDevice9* device,
                           const float right[3], const float up[3], int maxQuads, FxFrustumFn frustum);

// Particle_Free 0x6A6FF0 : libère le tableau de particules du pool (HeapFree équilibré). No-op si vide.
void FxBillboard_PoolFree(FxParticlePool* pool);

} // namespace ts2::gfx

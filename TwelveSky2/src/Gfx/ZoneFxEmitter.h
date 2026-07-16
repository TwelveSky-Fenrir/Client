// Gfx/ZoneFxEmitter.h — moteur de particules « Object A » des ÉMETTEURS DE ZONE (.WP).
//
// Réécriture FIDÈLE (bit-exacte visée) du système de particules ambiantes des zones
// (torches/feux/cascades) de TwelveSky2. La vérité est l'IDB de TwelveSky2.exe ; chaque
// bloc cite son ancre IDA (nom + 0xADDR). Spec octet-exacte : Docs/TS2_EXTRACT_WP_EMITTERS.md.
//
// ┌─ Chaîne prouvée (IDA, reachability depuis Scene_InGameUpdate/Render, 0 code mort) ─┐
// │  Scene_InGameUpdate 0x52C600 -> MapColl_UpdateObjectAnim 0x694A00 (boucle 2)       │
// │     -> Particle_Init 0x6A7020 (1re frame) / Particle_UpdateEmit 0x6A7530 (suite)   │
// │  Scene_InGameRender 0x52D0B0 -> Terrain_Render 0x698670 (a5==2, 0x698c5a-0x698cd5) │
// │     -> Particle_RenderBillboards 0x6A70B0                                          │
// └───────────────────────────────────────────────────────────────────────────────────┘
//
// Fonctions cœur reversées (portées ici sous les noms ZoneFx_* = ancres IDA) :
//   Particle_ComputeGradients 0x6A6D10  borne couleurs/vie, DÉRIVE colorRate[4]
//   Particle_Init             0x6A7020  count = ftol(particleLife·rate), HeapAlloc 56·count
//   Particle_Free             0x6A6FF0  HeapFree du tableau, flag=0, tmpl=0
//   Particle_UpdateEmit       0x6A7530  intègre + émet (switch 6 FORMES, cas 1..6)
//   Particle_RenderBillboards 0x6A70B0  6 sommets/particule 24o, DrawPrimitiveUP TRIANGLELIST
//
// ⚠ FRONTIÈRE (TS2_EXTRACT_WP_EMITTERS.md §4.3) : ce moteur « Object A » (POBJECT 48o,
//   template 232o, 6 formes, renderer g_GfxRenderer 0x7FFE18) est DISTINCT et NON
//   FUSIONNABLE avec le moteur « Object B » de Gfx/ParticleSystem.h (PtclPool 60o,
//   PtclDef 236o, 10 formes, g_GxdRenderer 0x18C4EF8). On RÉUTILISE seulement les
//   primitives COMMUNES de ParticleSystem.h — struct Particle (56o IDENTIQUE),
//   Billboard_Vertex (24o), Math_RandFloatRange, Rng_Next, Crt_ftol — jamais PtclDef/PtclPool.
#pragma once
#include "Gfx/ParticleSystem.h"   // Particle 56o, Billboard_Vertex 24o, Math_RandFloatRange, Rng, Crt_ftol
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <cstddef>   // offsetof
#include <vector>

namespace ts2::gfx {

// ---------------------------------------------------------------------------------------------
// FxEmitterTemplate — TEMPLATE d'émetteur, 232 o (= MapColl this+29[nodeIndex], stride 232).
// Layout runtime PROUVÉ par Fx_NodeLoadFromHandle 0x6A69F0 (18 ReadFile séquentiels remplissant
// [+72, +216)) + Particle_ComputeGradients 0x6A6D10 (dérive colorRate[4] à [+216, +232)).
// L'en-tête [+0, +72) (enabled, bloc texture, piste keyframe) n'est pas relu par la sim : seuls
// enabled@+0 (gate), texture@+52 (SetTexture du rendu) et frameTrack@+56/frameCount@+60/
// frameMatrices@+68 (chemin keyframe, NON porté) y sont utiles ; le reste est `_gap`.
#pragma pack(push, 4)
struct FxEmitterTemplate {
    int32_t            enabled;        // +0    gate Init/render (=1 pour un nœud .WP chargé, 0x6a6cf4)
    uint8_t            _gap04[48];     // +4    bloc texture (Tex_LoadCompressedFromHandle) — GPU tex à +52
    IDirect3DTexture9* texture;        // +52   texture GPU (SetTexture(0,*(tmpl+52)) @0x6a745e)
    void*              frameTrack;     // +56   piste keyframe (gate template+56 @0x6a787d) — NON porté
    int32_t            frameCount;     // +60   nb keyframes (% frameCount @0x6a78b0) — NON porté
    uint8_t            _gap40[4];      // +64
    void*              frameMatrices;  // +68   base matrices 64o (frame<<6 @0x6a78a4) — NON porté
    float              emissionDuration;// +72  gate d'émission (template+72 @0x6a783c)
    float              kfFps;          // +76   cadence keyframes (idx = kfFps·elapsedTime @0x6a78ab)
    float              rate;           // +80   particules/s (spawnAccum += dt·rate ; dimensionne le pool)
    float              shape;          // +84   forme d'émission 1..6 (Crt_ftol @0x6a7a7f)
    float              speed;          // +88   vitesse/rayon de base (cas 1..4)
    float              box[3];         // +92   étendue boîte (cas 5/6 : Rand(box·-0.5, box·0.5))
    float              particleLife;   // +104  durée de vie particule (borné ≥0.01 ; mort si age>ce champ)
    float              velMin[3];      // +108  vitesse init aléatoire min XYZ (Rand(velMin,velMax))
    float              velMax[3];      // +120  vitesse init aléatoire max XYZ
    float              param0Start;    // +132  particle.param0 au spawn (diviseur d'accélération)
    float              sizeStart;      // +136  particle.size au spawn (demi-taille + gris du billboard)
    float              startColor[4];  // +140  couleur RGBA de spawn (copiée telle quelle, bornée 0..255)
    float              endColor[4];    // +156  couleur RGBA de fin (bornée 0..255 ; dérive colorRate)
    float              forceBase[3];   // +172  force constante (accél = force/param0·dt)
    float              forceRandMin[3];// +184  force aléatoire min (force = Rand(min,max)+base, 1×/frame)
    float              forceRandMax[3];// +196  force aléatoire max
    float              param0Rate;     // +208  dérive de param0/s (clamp ≥0)
    float              sizeRate;       // +212  dérive de size/s (clamp ≥0)
    float              colorRate[4];   // +216  dérive RGBA/s — DÉRIVÉ (ComputeGradients), PAS sur disque
};
#pragma pack(pop)
static_assert(sizeof(FxEmitterTemplate) == 232, "FxEmitterTemplate doit faire 232 o");
static_assert(offsetof(FxEmitterTemplate, enabled)          == 0,   "enabled @+0");
static_assert(offsetof(FxEmitterTemplate, texture)          == 52,  "texture @+52");
static_assert(offsetof(FxEmitterTemplate, frameTrack)       == 56,  "frameTrack @+56");
static_assert(offsetof(FxEmitterTemplate, frameMatrices)    == 68,  "frameMatrices @+68");
static_assert(offsetof(FxEmitterTemplate, emissionDuration) == 72,  "emissionDuration @+72");
static_assert(offsetof(FxEmitterTemplate, rate)             == 80,  "rate @+80");
static_assert(offsetof(FxEmitterTemplate, shape)            == 84,  "shape @+84");
static_assert(offsetof(FxEmitterTemplate, particleLife)     == 104, "particleLife @+104");
static_assert(offsetof(FxEmitterTemplate, velMin)           == 108, "velMin @+108");
static_assert(offsetof(FxEmitterTemplate, param0Start)      == 132, "param0Start @+132");
static_assert(offsetof(FxEmitterTemplate, startColor)       == 140, "startColor @+140");
static_assert(offsetof(FxEmitterTemplate, endColor)         == 156, "endColor @+156");
static_assert(offsetof(FxEmitterTemplate, forceBase)        == 172, "forceBase @+172");
static_assert(offsetof(FxEmitterTemplate, forceRandMin)     == 184, "forceRandMin @+184");
static_assert(offsetof(FxEmitterTemplate, forceRandMax)     == 196, "forceRandMax @+196");
static_assert(offsetof(FxEmitterTemplate, param0Rate)       == 208, "param0Rate @+208");
static_assert(offsetof(FxEmitterTemplate, sizeRate)         == 212, "sizeRate @+212");
static_assert(offsetof(FxEmitterTemplate, colorRate)        == 216, "colorRate @+216");
// Le blob disque de asset::FxNode.fields (144 o) recouvre EXACTEMENT [+72, +216) : on le recopie
// tel quel à (tmpl+72). Ancre : Fx_NodeLoadFromHandle 0x6A69F0 (this+18..this+53 = octets 72..215).
static constexpr size_t kFxTemplateDiskOffset = 72;   // runtime +72
static constexpr size_t kFxTemplateDiskSize   = 144;  // [+72, +216)

// ---------------------------------------------------------------------------------------------
// FxParticlePool — POBJECT 48 o (état runtime du pool, à FxNode+28). Layout PROUVÉ par
// Particle_Init 0x6A7020 / Particle_UpdateEmit 0x6A7530 / Particle_RenderBillboards 0x6A70B0.
// = PtclPool 60o de ParticleSystem.h MOINS le préfixe scale[3] (tous les offsets décalés de −12).
struct FxParticlePool {
    int32_t            flag;          // +0   initialized/gate (=1 après Init ; 0x6a70a5)
    FxEmitterTemplate* tmpl;          // +4   template source (this+1 = a2 ; 0x6a7043)
    float              elapsedTime;   // +8   += dt (this+2 ; 0x6a758f)
    float              position[3];   // +12  origine d'émission (copiée de FxNode+4 ; 0x6a7594)
    float              rotation[3];   // +24  rotation XYZ deg (copiée de FxNode+16 ; 0x6a75ae)
    float              spawnAccum;    // +36  accumulateur fractionnaire (this+9 ; émet tant que ≥1)
    int32_t            particleCount; // +40  capacité = ftol(particleLife·rate) (this+10 ; 0x6a7062)
    Particle*          particles;     // +44  tableau heap particleCount × 56 o (this+11 ; 0x6a707c)
};
static_assert(sizeof(FxParticlePool) == 48, "FxParticlePool (POBJECT) doit faire 48 o");
static_assert(offsetof(FxParticlePool, flag)          == 0,  "flag @+0");
static_assert(offsetof(FxParticlePool, tmpl)          == 4,  "tmpl @+4");
static_assert(offsetof(FxParticlePool, elapsedTime)   == 8,  "elapsedTime @+8");
static_assert(offsetof(FxParticlePool, position)      == 12, "position @+12");
static_assert(offsetof(FxParticlePool, rotation)      == 24, "rotation @+24");
static_assert(offsetof(FxParticlePool, spawnAccum)    == 36, "spawnAccum @+36");
static_assert(offsetof(FxParticlePool, particleCount) == 40, "particleCount @+40");
static_assert(offsetof(FxParticlePool, particles)     == 44, "particles @+44");

// ---------------------------------------------------------------------------------------------
// Prédicat de cull frustum de l'origine du pool (Cam_FrustumTestPoint6 0x69ED30 sur POBJECT+12).
// Le binaire teste l'origine contre le frustum de g_GfxRenderer (dernière caméra posée). Côté
// ClientSource ce singleton n'existe pas : on injecte le test (ou nullptr => toujours visible).
using FxFrustumFn = bool (*)(const float pos[3]);

// Paramètres de frame du rendu billboard (Particle_RenderBillboards 0x6A70B0). Le binaire lit la
// base billboard dans des globales du renderer (flt_8001D4..8001E8 = droite/haut caméra monde) ;
// ici l'appelant (WorldGeometryRenderer) les fournit depuis la matrice vue.
struct ZoneFxFrameParams {
    IDirect3DDevice9*             device   = nullptr; // g_GfxRenderer_pDevice 0x800074
    float                         right[3] = {1,0,0}; // flt_8001D4/D8/DC (droite caméra monde, unitaire)
    float                         up[3]    = {0,1,0}; // flt_8001E0/E4/E8 (haut caméra monde, unitaire)
    int                           maxQuads = 0;       // dword_7FFEE0 (plafond quads ; 0 => pas de plafond)
    std::vector<Billboard_Vertex>* scratch = nullptr; // dword_800080 (buffer CPU des sommets, DrawPrimitiveUP)
    FxFrustumFn                   frustum  = nullptr; // Cam_FrustumTestPoint6 (nullptr => toujours visible)
};

// ---------------------------------------------------------------------------------------------
// Gate global « moteur prêt » (miroir de dword_800080 != 0 : Init/UpdateEmit/Render no-op sinon).
// Le binaire garde sur l'existence du VB partagé ; côté ClientSource le module possède son propre
// scratch -> prêt par défaut. MAIN peut le forcer si besoin.
void ZoneFx_SetReady(bool ready);
bool ZoneFx_Ready();

// ---------------------------------------------------------------------------------------------
// API du moteur (noms = ancres IDA). Toutes __thiscall d'origine -> première arg = le pool/template.

// Particle_ComputeGradients 0x6A6D10 : borne kfFps/rate/speed/box ≥0, particleLife ≥0.01,
// start/endColor 0..255, puis DÉRIVE colorRate[i] = (endColor[i]-startColor[i]) / particleLife.
// Idempotent (rappelé à chaque Init d'un pool partageant le template).
void ZoneFx_ComputeGradients(FxEmitterTemplate* tmpl);

// Particle_Init 0x6A7020 : (re)alloue le pool si flag==0 && ready && tmpl->enabled. Appelle
// ComputeGradients, pose tmpl/elapsedTime/spawnAccum=0, particleCount = ftol(particleLife·rate),
// HeapAlloc(56·count), zéro-init du flag `alive` de chaque slot, flag=1.
void ZoneFx_Init(FxParticlePool* pool, FxEmitterTemplate* tmpl);

// Particle_Free 0x6A6FF0 : HeapFree du tableau, flag=0, tmpl=0, particles=0. (No-op si non alloué.)
void ZoneFx_Free(FxParticlePool* pool);

// Particle_UpdateEmit 0x6A7530 : une frame de sim. `pos`/`rot` = origine/orientation d'émission
// (3 floats chacun, recopiés dans le pool). Intègre âge/pos/vel/param0/size/couleur des particules
// vivantes (mort si age>particleLife), puis émet via le switch 6 formes tant que spawnAccum≥1.
// `frustum` (optionnel) cull l'origine : si absente du frustum, ne fait rien. `dt` = a3 du binaire
// (= vrai delta de frame, cf. MapColl_UpdateObjectAnim call site Scene_InGameUpdate 0x52c94b).
void ZoneFx_UpdateEmit(FxParticlePool* pool, float dt, const float pos[3], const float rot[3],
                       FxFrustumFn frustum);

// Particle_RenderBillboards 0x6A70B0 : construit 6 sommets/particule vivante (2 triangles, 24 o :
// XYZ + D3DCOLOR + UV) dans `params.scratch`, quad camera-facing via la base right/up × size, couleur
// GRISE dérivée de size, puis SetTexture(0, tmpl->texture) + DrawPrimitiveUP(TRIANGLELIST, 2·n, .., 24).
// Renvoie le nombre de quads dessinés (0 si pool vide/hors frustum). Cull frustum de l'origine avant.
int  ZoneFx_RenderBillboards(FxParticlePool* pool, const ZoneFxFrameParams& params);

// Remplit un FxEmitterTemplate 232o depuis le blob disque de asset::FxNode : pose enabled=1,
// texture GPU (à +52), et recopie les 144 o de champs à (tmpl+72). `diskFields` = asset::FxNode.fields
// (144 o = runtime [+72,+216)). NE dérive PAS colorRate (fait par Init/ComputeGradients).
// Le chemin keyframe (frameTrack/frameMatrices) reste nul -> matrice de frame = identité (else-branch
// 0x6a78c3), fidèle au cas ambiant sans keyframe (TODO ancre 0x6a787d : baker la piste quaternion).
void ZoneFx_BuildTemplate(FxEmitterTemplate* tmpl, const uint8_t* diskFields, size_t diskFieldsSize,
                          IDirect3DTexture9* texture);

} // namespace ts2::gfx

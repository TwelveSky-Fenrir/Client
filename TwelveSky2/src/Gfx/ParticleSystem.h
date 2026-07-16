// Gfx/ParticleSystem.h — moteur de particules du moteur « GXD » de TwelveSky2.
//
// Réécriture FIDÈLE (bit-exacte visée) du pool de particules GXD. La vérité est
// l'IDB de TwelveSky2.exe ; chaque bloc cite son ancre IDA (nom + 0xADDR).
//
// Trois structures emboîtées + le sommet billboard (layouts prouvés par
// PtclDef_ReadFile 0x422C50 et §2 de Docs/TS2_FX_ROSETTA.md) :
//   PtclDef        236 o  — définition/template immuable (fichier .ptcl)
//   PtclPool        60 o  — instance runtime (émetteur vivant)
//   Particle        56 o  — particule vivante (SoA-libre, tableau heap)
//   Billboard_Vertex 24 o — sommet du quad (pos + D3DCOLOR ARGB + uv)
//
// Fonctions cœur reversées :
//   PtclDef_ClampParams     0x422F60  borne couleurs/vie, DÉRIVE colorRate
//   PtclDef_AllocPool       0x423280  count = ftol(lifetime·spawnRate), heap
//   PtclDef_FreePool        0x423240  libère le tableau, reset scale=1
//   PtclDef_UpdateAndSpawn  0x423310  intègre + émet (switch 10 formes)
//   PtclDef_RenderQuads     0x424430  billboards → VB partagé, DrawPrimitiveUP
//   PtclDef_ReadFile        0x422C50  désérialise depuis un HANDLE fichier
//   PtclDef_Init/Reset      0x4221E0/0x4222D0  zéro-init (idx texture[11] = -1)
//
// ⚠ FRONTIÈRES (CONFLICTS FX Rosetta §3, IDA gagne) : pas de mega-struct
//   EFFECT_OBJECT, pas de banque [52], index d'émission 1..10 = ceux d'IDA
//   (NON transposés de VeryOld). Le champ field19@0x4C garde son nom neutre
//   IDA : son rôle (gate de durée d'émission) est REPRODUIT depuis la branche
//   décompilée 0x423597, pas inventé.
//
// N'utilise que le SDK Windows/Direct3D9 + d3dx9 (comme le binaire, via ses
// thunks j_D3DXVec*/j_D3DXMatrix* @0x6BB6xx).
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <cstddef>   // offsetof

namespace ts2::gfx {

// ---------------------------------------------------------------------------
// Sommet billboard — Billboard_Vertex, 24 o. FVF = XYZ|DIFFUSE|TEX1 (0x142).
// PtclDef_RenderQuads 0x424430 n'écrit QUE pos (+0) et color (+12) ; les uv
// (+16) sont pré-initialisées une fois dans le VB partagé (voir InitSharedQuadUVs).
struct Billboard_Vertex {
    float    x, y, z;   // +0x00  position monde
    D3DCOLOR color;     // +0x0C  ARGB = A<<24 | R<<16 | G<<8 | B
    float    u, v;      // +0x10  coords de texture (statiques dans le VB)
};
static_assert(sizeof(Billboard_Vertex) == 24, "Billboard_Vertex doit faire 24 o");

inline constexpr DWORD kBillboardFVF = D3DFVF_XYZ | D3DFVF_DIFFUSE | D3DFVF_TEX1; // 0x142

// ---------------------------------------------------------------------------
// Particle — particule vivante, 56 o (= PARTICLE_FOR_GXD). Offsets prouvés par
// PtclDef_UpdateAndSpawn 0x423310 (intégration) / PtclDef_RenderQuads 0x424430.
struct Particle {
    int   alive;        // +0x00  0 = slot libre ; 1 = vivante
    float age;          // +0x04  temps de vie écoulé (mort si age > lifetime)
    float pos[3];       // +0x08  position monde (spawn = coord transformée)
    float vel[3];       // +0x14  vitesse monde
    float param0;       // +0x20  diviseur d'accélération (vel += force/param0·dt)
    float size;         // +0x24  demi-taille billboard
    float colorR;       // +0x28  0..255
    float colorG;       // +0x2C
    float colorB;       // +0x30
    float colorA;       // +0x34
};
static_assert(sizeof(Particle) == 56, "Particle doit faire 56 o");

// ---------------------------------------------------------------------------
// PtclDef — définition immuable, 236 o (= PSYSTEM_FOR_GXD). Ordre/offsets EXACTS
// de PtclDef_ReadFile 0x422C50 (lecture séquentielle depuis le fichier).
// L'en-tête (enabled + cTexture[56] + motion[16]) est opaque ici : texture et
// motion sont peuplées par des sous-systèmes tiers (Tex_ReadPacked 0x417740,
// Anim_ReadMotionStream 0x43CDB0) — modélisés en blocs d'octets fidèles.
#pragma pack(push, 4)
struct PtclDef {
    int      enabled;            // +0x00  0 = def vide (gate alloc/update/render)
    uint8_t  texture[56];        // +0x04  cTexture 56 o ; IDirect3DTexture9* @+0x34
    uint8_t  motion[16];         // +0x3C  flux MOTION (ptr@+0/frameCount@+4/frames@+0xC)
    float    field19;            // +0x4C  gate d'émission (branche 0x423597) — nom IDA neutre
    float    texAnimFPS;         // +0x50  cadence anim de texture (idx = FPS·elapsedTime)
    float    spawnRate;          // +0x54  particules/s (dimensionne le pool)
    float    shapeType;          // +0x58  forme d'émission 1..10 (float → int)
    float    baseSpeed;          // +0x5C  vitesse/rayon de base
    float    boxSize[3];         // +0x60  étendue boîte/planaire
    float    lifetime;           // +0x6C  durée de vie particule (borné ≥ 0.01)
    float    velMin[3];          // +0x70  vitesse initiale aléatoire min XYZ
    float    velMax[3];          // +0x7C  vitesse initiale aléatoire max XYZ
    float    param0Start;        // +0x88  valeur de spawn de particle.param0
    float    sizeStart;          // +0x8C  valeur de spawn de particle.size
    float    startColor[4];      // +0x90  couleur RGBA au spawn (copiée telle quelle)
    float    endColor[4];        // +0xA0  couleur RGBA de fin (dérive colorRate)
    float    forceBase[3];       // +0xB0  force constante
    float    forceRandMin[3];    // +0xBC  force aléatoire min
    float    forceRandMax[3];    // +0xC8  force aléatoire max
    float    param0Rate;         // +0xD4  dérive de param0 / s (sur disque)
    float    sizeRate;           // +0xD8  dérive de size / s (sur disque)
    float    colorRate[4];       // +0xDC  dérive RGBA / s — DÉRIVÉ (PAS sur disque)
};
#pragma pack(pop)
static_assert(sizeof(PtclDef) == 236, "PtclDef doit faire 236 o");
static_assert(offsetof(PtclDef, field19)    == 0x4C, "field19 @0x4C");
static_assert(offsetof(PtclDef, spawnRate)  == 0x54, "spawnRate @0x54");
static_assert(offsetof(PtclDef, lifetime)   == 0x6C, "lifetime @0x6C");
static_assert(offsetof(PtclDef, startColor) == 0x90, "startColor @0x90");
static_assert(offsetof(PtclDef, colorRate)  == 0xDC, "colorRate @0xDC");

// ---------------------------------------------------------------------------
// PtclPool — instance runtime / émetteur, 60 o (= POBJECT_FOR_GXD + scale[3]).
// ⚠ scale[3]@0x00 est un champ IDA-only (build EU) qui décale +12 o tous les
// autres champs vs POBJECT VeryOld (§3-C). Suivre IDA, jamais VeryOld.
struct PtclPool {
    float     scale[3];      // +0x00  multiplie particle.pos au rendu (défaut 1.0)
    int       initialized;   // +0x0C  gate update/render (=1 après AllocPool)
    PtclDef*  def;           // +0x10  définition source
    float     elapsedTime;   // +0x14  += dt ; pilote l'index de frame de texture
    float     position[3];   // +0x18  origine monde d'émission (copiée de l'arg)
    float     rotation[3];   // +0x24  rotation XYZ en degrés → matrice monde
    float     spawnAccum;    // +0x30  accumulateur fractionnaire (émet tant que ≥1)
    int       particleCount; // +0x34  capacité = ftol(lifetime·spawnRate)
    Particle* particles;     // +0x38  tableau heap particleCount × 56 o
};
static_assert(sizeof(PtclPool) == 60, "PtclPool doit faire 60 o");
static_assert(offsetof(PtclPool, initialized)   == 0x0C, "initialized @0x0C");
static_assert(offsetof(PtclPool, def)           == 0x10, "def @0x10");
static_assert(offsetof(PtclPool, spawnAccum)    == 0x30, "spawnAccum @0x30");
static_assert(offsetof(PtclPool, particles)     == 0x38, "particles @0x38");

// ---------------------------------------------------------------------------
// Paramètres de frame pour le rendu billboard (PtclDef_RenderQuads 0x424430).
// Le binaire lit ces valeurs dans des globales du renderer GXD (Object B
// g_GxdRenderer 0x18C4EF8) ; côté ClientSource elles sont fournies par
// l'appelant (le renderer possède la caméra et le VB, pas ce module).
struct ParticleFrameParams {
    IDirect3DDevice9* device      = nullptr; // g_GxdRenderer_pDevice 0x18C5104
    void*             sharedVB    = nullptr; // g_GxdRenderer_pSpritePool 0x18C5110 (mémoire CPU verrouillée)
    int               maxQuads    = 0;       // g_MaxQuadsPerBatch 0x18C4F74 (plafond quads/batch)
    float             camRight[3] = {1,0,0}; // flt_18C5264/68/6C (droite caméra × demi-taille)
    float             camUp[3]    = {0,1,0}; // flt_18C5270/74/78 (haut caméra × demi-taille)
    float             eye[3]      = {0,0,0}; // dword_18C51C0/C4/C8 (position œil, pour fondu LOD)
    float             fadeNear    = 0.0f;    // flt_18C4F08 (début du fondu de distance)
    float             fadeFar     = 1.0f;    // flt_18C4F0C (fin du fondu de distance)

    // Frustum_ContainsPoint6 0x406430 : test de frustum sur l'origine du pool
    // (cull du pool entier). Le renderer possède les plans ; injecté ici.
    // nullptr => toujours visible (pas de cull).
    bool (*frustumContains)(const float pos[3]) = nullptr;
};

// ---------------------------------------------------------------------------
// VB partagé — g_GxdRenderer_pSpritePool 0x18C5110. Le binaire utilise UN seul
// buffer CPU verrouillé pour tous les émetteurs ; il sert aussi de gate
// (AllocPool/Update/Render ne font rien tant qu'il est nul). MAIN l'installe
// une fois le renderer GXD prêt.
void  SetParticleSharedVB(void* cpuBuffer);
void* ParticleSharedVB();

// Pré-remplit les uv statiques (+0x10) de `maxQuads` quads (6 sommets/quad) dans
// le VB partagé : TL(0,0) TR(1,0) BL(0,1) / BL(0,1) TR(1,0) BR(1,1). RenderQuads
// n'écrit jamais les uv — elles DOIVENT être posées une fois par le propriétaire
// du VB (GxdRenderer). Fidèle au fait que 0x424430 ne touche que pos+color.
void  InitSharedQuadUVs(void* cpuBuffer, int maxQuads);

// ---------------------------------------------------------------------------
// RNG — Rng_Next 0x7603FD : LCG MSVC (state = 214013·state + 2531011 ;
// renvoie (state>>16) & 0x7FFF). Le binaire utilise l'état rand par-thread du
// CRT ; on reproduit l'algorithme avec un état de module (graine réglable).
unsigned int Rng_Next();
void         SetRandSeed(unsigned int seed);

// Math_RandFloatRange 0x403330 : a + (b-a)·((Rng_Next()%10001)/10000) ; inverse
// les bornes si b <= a (fidèle à la branche 0x40336D/0x4033A4).
float Math_RandFloatRange(float a, float b);

// Crt_ftol 0x760810 : troncation float → int (vers zéro).
inline int Crt_ftol(double v) { return static_cast<int>(v); }

// ---------------------------------------------------------------------------
// API du pool de particules (noms = ancres IDA).

// PtclDef_Init 0x4221E0 / PtclDef_Reset 0x4222D0 : zéro-init d'une def ; pose le
// handle de texture (texture[0x2C]) à -1 (sentinelle « pas de texture »).
void PtclDef_Init(PtclDef* def);
void PtclDef_Reset(PtclDef* def);

// PtclDef_ClampParams 0x422F60 : borne texAnimFPS/spawnRate/baseSpeed/boxSize ≥0,
// lifetime ≥0.01, couleurs 0..255, puis DÉRIVE colorRate = (endColor-startColor)/lifetime.
void PtclDef_ClampParams(PtclDef* def);

// PtclDef_AllocPool 0x423280 : (re)dimensionne le pool si non initialisé et si le
// VB partagé + def.enabled sont prêts. particleCount = ftol(lifetime·spawnRate).
void PtclDef_AllocPool(PtclDef* def, PtclPool* pool);

// PtclDef_FreePool 0x423240 : libère le tableau, initialized=0, def=0, scale=1.
void PtclDef_FreePool(PtclPool* pool);

// PtclDef_UpdateAndSpawn 0x423310 : une frame de simulation. `position`/`rotation`
// (3 floats chacun) sont l'origine/l'orientation d'émission, recopiées dans le
// pool. Intègre âge/pos/vel/param0/size/couleur, puis émet via le switch de forme.
void PtclDef_UpdateAndSpawn(PtclPool* pool, const float position[3],
                            const float rotation[3], float dt);

// PtclDef_RenderQuads 0x424430 : billboards des particules vivantes → VB partagé
// (`params.sharedVB`), DrawPrimitiveUP(TRIANGLELIST, stride 24). Fondu LOD par
// distance (dessine moins de quads), plafond `params.maxQuads`.
void PtclDef_RenderQuads(PtclPool* pool, const ParticleFrameParams& params);

// PtclDef_ReadFile 0x422C50 : désérialise une def depuis un HANDLE fichier ouvert.
// `a3`/`a4` = paramètres opaques passés à Tex_ReadPacked (contexte d'archive).
// Renvoie true en cas de succès (def.enabled = 1). Les hooks texture/motion
// (Tex_ReadPacked/Anim_ReadMotionStream) sont injectables ; nuls => zéro-lus.
using TexReadPackedFn     = bool (*)(void* texture56, HANDLE hFile, int a3, int a4);
using MotionReadStreamFn  = bool (*)(void* motion16, HANDLE hFile);
void SetPtclIoHooks(TexReadPackedFn texHook, MotionReadStreamFn motionHook);
bool PtclDef_ReadFile(PtclDef* def, HANDLE hFile, int a3, int a4);

// Pointeur GPU de texture d'une def (SetTexture(0, *(def+0x38)) dans RenderQuads).
IDirect3DTexture9* PtclDef_GetTexture(const PtclDef* def);

} // namespace ts2::gfx

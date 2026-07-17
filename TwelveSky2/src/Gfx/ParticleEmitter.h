// Gfx/ParticleEmitter.h — conteneur cEmitter/cEffect du moteur « GXD » (Object B).
//
// Réécriture FIDÈLE (bit-exacte visée) du CONTENEUR de timeline empilé au-dessus
// du leaf PtclDef/PtclPool (voir Gfx/ParticleSystem.h). Vérité = IDB TwelveSky2.exe
// (imagebase 0x400000) ; chaque bloc cite son ancre IDA (nom + 0xADDR).
// Voir Docs/TS2_DEEP_PARTICLE.md §3.6–§3.8 et §5-T1.
//
// Deux niveaux :
//   Emitter (cEmitter)  288 o  — un PtclDef|FxObj|Mesh + nom + timeline de
//                                keyframes (stride 24, un PtclPool par keyframe)
//                                + canaux alpha (stride 8) + LUT alpha par-frame.
//                                Construct 0x424A10 / ReadFile 0x424D30.
//   Effect  (cEffect)  ~308 o  — magic id + std::vector<Emitter*>.
//                                ReadStream 0x42A990 / LoadFile 0x42A920.
//
// ⚠ FRONTIÈRE CLUSTER : les sous-types FxObj (32 o, FxObj_ReadStream 0x4327E0) et
//   Mesh (12 o, Mesh_LoadMOBJECT2 0x4318C0) appartiennent au sous-système mesh
//   (HORS cluster particule). Ils sont modélisés en BLOCS OPAQUES + hooks
//   injectables : le loader ne connaît que « alloue/lit → handle » et « handle →
//   nb de frames ». Sans hook, un émetteur de sous-type 2 échoue à charger — on
//   n'INVENTE PAS le format mesh (règle #0). Le sous-type 1 (PtclDef) est complet.
//
// ⚠ Aucun `.PTCL`/effet autonome n'est livré sur disque : le chemin fichier
//   (LoadFile) est de facto mort ; le conteneur est INSTANCIÉ à l'exécution par
//   les meshes animés. On porte la structure + le parseur pour ce câblage (T2).
//
// N'utilise que le SDK Windows + les primitives Object B de ParticleSystem.h.
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "ParticleSystem.h"   // PtclDef, PtclPool, PtclDef_ReadFile/AllocPool/FreePool

namespace ts2::gfx {

// ---------------------------------------------------------------------------
// EmitterNode — 24 o. RÉUTILISÉ pour les deux vecteurs stride-24 de l'émetteur :
//   • keyframes (cEmitter+88)  — Emitter_ReadKeyframe 0x4257F0 (+0/+4 lus disque)
//   • instances (cEmitter+240) — Emitter_RebuildInstances 0x429A90 (transform)
// Layout prouvé : keyframe {valeur:u32@+0, temps:float@+4, 3 floats zéro@+8/+12/+16,
// PtclPool*@+20}. En mode instance : {flag=0@+0, échelle=1.0@+4, 0@+8/+12/+16, pool@+20}.
struct EmitterNode {
    uint32_t  value;    // +0x00  keyframe: valeur ; instance: flag (=0)
    float     time;     // +0x04  keyframe: temps (sommé par ComputeDuration) ; instance: 1.0
    float     f8;       // +0x08  zéro-init (Emitter_ReadKeyframe @0x425839)
    float     f12;      // +0x0C  zéro-init
    float     f16;      // +0x10  zéro-init
    PtclPool* pool;     // +0x14  PtclPool 60 o (alloué si l'émetteur porte un PtclDef)
};
static_assert(sizeof(EmitterNode) == 24, "EmitterNode doit faire 24 o (stride keyframe/instance)");

// ---------------------------------------------------------------------------
// EmitterChannel — 8 o. Vecteur « alpha-kf » (cEmitter+112), lu UNIQUEMENT pour
// les sous-types FxObj/Mesh. Chaque enregistrement disque = 8 o : {temps:float@+0,
// valeur:octet@+4 (+3 o de padding lus tels quels)}. Interpolé en LUT par
// Emitter_BuildAlphaLUT 0x425C10. Le read écrit temps@+0 puis la dword@+4
// (seul l'octet de poids faible porte l'alpha — voir 0x424F73/0x424F79).
struct EmitterChannel {
    float    time;      // +0x00  abscisse temporelle (frame)
    uint8_t  value;     // +0x04  octet alpha (0..255)
    uint8_t  _pad[3];   // +0x05  padding disque (dword copié entier @0x424F79)
};
static_assert(sizeof(EmitterChannel) == 8, "EmitterChannel doit faire 8 o (stride canal)");

// ---------------------------------------------------------------------------
// Hooks des sous-types opaques FxObj/Mesh (sous-système mesh, HORS cluster).
// Injectés par le front mesh une fois porté ; nuls => le sous-type 2 ne charge pas.
//   fxObjRead   : FxObj_ReadStream 0x4327E0 — alloue+lit l'objet FxObj (32 o) →
//                 handle opaque (nullptr = échec). type==1 uniquement.
//   fxObjFrames : nb de frames pour la LUT alpha = *(*(FxObj+24)+264) @0x425C1F.
//   meshLoad    : Mesh_LoadMOBJECT2 0x4318C0 (flag=1) — alloue+lit le mesh (12 o).
//   meshFrames  : nb de frames pour la LUT alpha = *(*(Mesh+8)+84) @0x425C2D.
//   fxObjFree/meshFree : libération optionnelle des handles (destruction propre) ;
//                        nuls => le handle est fui (l'émetteur ne possède pas le format).
struct EmitterSubtypeHooks {
    void* (*fxObjRead)(HANDLE hFile, int a4, int a5) = nullptr;
    int   (*fxObjFrames)(void* fxObj)                = nullptr;
    void  (*fxObjFree)(void* fxObj)                  = nullptr;
    void* (*meshLoad)(HANDLE hFile)                  = nullptr;
    int   (*meshFrames)(void* mesh)                  = nullptr;
    void  (*meshFree)(void* mesh)                    = nullptr;
};
void SetEmitterSubtypeHooks(const EmitterSubtypeHooks& hooks);
const EmitterSubtypeHooks& EmitterSubtypeHooksGet();

// ---------------------------------------------------------------------------
// Emitter — cEmitter, 288 o (Emitter_Construct 0x424A10, vtable off_7ED548).
// Réécriture idiomatique : std::string/std::vector remplacent les membres STL du
// binaire (l'original EST du std::string/std::vector — c'est FIDÈLE). La carte
// d'offsets 288 o ci-dessous documente le layout binaire d'origine ; la taille
// C++ diffère (STL modernes) car ce n'est PAS un overlay mémoire.
//
//   +0x00  vtable off_7ED548              +0x88  keyframes  std::vector (stride 24)
//   +0x04  type (arg d'entrée)            +0x70  channels   std::vector (stride 8)
//   +0x08  vec3[3]                        +0xB4  name2 (chaîne, vide)     [runtime]
//   +0x14  flag (octet)                   +0xE8  duration (ComputeDuration)
//   +0x18  mode[2] (==1 rebuild/==2 dur.) +0xEC  tailPool  PtclPool*
//   +0x20  f32 (instanceCount si mode==1) +0xF0  instances std::vector (stride 24)
//   +0x24  f36/f40/f44 (3 floats)         +0x108 alphaLUT  std::vector<octet>
//   +0x30  ptclDef  (sous-type 1)         +0xD0  runtime timers (+208..+232)
//   +0x34  fxObj    (sous-type 2,type==1) +0x88  runtime state (+136/+140)
//   +0x38  mesh     (sous-type 2)
//   +0x3C  name (chaîne longueur-préfixée)
class Emitter {
public:
    Emitter();                          // Emitter_Construct 0x424A10
    ~Emitter();
    Emitter(const Emitter&)            = delete;   // possession heap : pas de copie
    Emitter& operator=(const Emitter&) = delete;

    // --- Champs disque (ordre EXACT d'Emitter_ReadFile 0x424D30) ---
    int          type    = 0;                 // +0x04  a1 (recopié @0x424D52)
    float        vec3[3] = {0, 0, 0};         // +0x08  12 o (@0x424D5D)
    uint8_t      flag    = 0;                 // +0x14  1 o  (@0x424D78)
    int32_t      mode[2] = {0, 0};            // +0x18  8 o  (@0x424D93) ; mode[0]=1→rebuild, =2→duration
    int32_t      f32     = 0;                 // +0x20  dword (@0x424DAE) ; nb d'instances si mode==1
    float        f36 = 0.0f, f40 = 0.0f, f44 = 0.0f; // +0x24/+0x28/+0x2C (3 floats)

    // Sous-type disque (u32 : 1=PtclDef, 2=FxObj|Mesh). Un SEUL des trois non nul.
    PtclDef*     ptclDef = nullptr;           // +0x30  sous-type 1 (@0x424E47)
    void*        fxObj   = nullptr;           // +0x34  sous-type 2 & type==1, OPAQUE (@0x424EAB)
    void*        mesh    = nullptr;           // +0x38  sous-type 2 sinon, OPAQUE (@0x4250E6)

    std::string  name;                        // +0x3C  chaîne (Emitter_AssignStringField 0x412750)
    std::vector<EmitterNode>    keyframes;    // +0x58  timeline (stride 24, un PtclPool/kf)
    std::vector<EmitterChannel> channels;     // +0x70  canaux alpha (stride 8, sous-type 2)

    // --- Runtime / timeline ---
    float        duration = 0.0f;             // +0xE8  somme des temps de keyframes (ComputeDuration)
    PtclPool*    tailPool = nullptr;          // +0xEC  pool « queue » (si PtclDef) — AllocPool'd
    std::vector<EmitterNode> instances;       // +0xF0  instances dupliquées (RebuildInstances)
    std::vector<uint8_t>     alphaLUT;        // +0x108 courbe alpha par-frame (BuildAlphaLUT)

    // Fanions/timers runtime posés par Construct/ResetRuntime (rôles au tick, T2).
    int32_t      runtime136 = 0;              // +0x88  état de lecture timeline (reset 0)
    uint8_t      runtime140 = 0;              // +0x8C  fanion runtime (reset 0)
    float        timerElapsed = 0.0f;         // +0xD0  (+208) horloge d'émission (reset 0)
    float        timer212 = 0.0f;             // +0xD4  (+212) reset 0
    float        timer216 = 0.0f;             // +0xD8  (+216) reset 0
    int32_t      runtime220 = 0;              // +0xDC  (+220) reset 0
    int32_t      field224 = 1;                // +0xE0  (+224) = 1 (Construct @0x424ABC)
    uint8_t      flag228 = 0, flag229 = 0;    // +0xE4/+0xE5 (Construct)
    uint8_t      flag230 = 1;                 // +0xE6  = 1 (Construct) ; reset 0

    // Emitter_ReadFile 0x424D30 — désérialise l'émetteur depuis un HANDLE ouvert.
    // `emitterType` = a1 (contexte de type, recopié en +0x04). Renvoie false sur
    // toute lecture partielle, sous-type inconnu, ou sous-type 2 sans hook.
    bool ReadFile(int emitterType, HANDLE hFile, int a4, int a5);   // 0x424D30

    // Emitter_ResetRuntime 0x42A220 — remet les timers/fanions à 0 et réalloue le
    // pool queue (libère ses particules puis PtclDef_AllocPool). Vide les instances.
    void ResetRuntime();                                            // 0x42A220

private:
    void BuildAlphaLUT();       // Emitter_BuildAlphaLUT   0x425C10
    void ComputeDuration();     // Emitter_ComputeDuration 0x429EB0
    bool RebuildInstances();    // Emitter_RebuildInstances 0x429A90
    void ClearInstances();      // Emitter_ClearInstances  0x429F90
    void FreeSubObjects();      // libération PtclDef/FxObj/Mesh/pools (destruction)
};

// Emitter_ReadKeyframe 0x4257F0 — lit un enregistrement keyframe (2 dwords) depuis
// le fichier et zéro-initialise le reste (+8/+12/+16 = 0, pool@+20 = nullptr).
// Exposé pour tests ; utilisé par Emitter::ReadFile.
bool Emitter_ReadKeyframe(HANDLE hFile, EmitterNode* kf);           // 0x4257F0

// ---------------------------------------------------------------------------
// Effect — cEffect, ~308 o. Conteneur d'émetteurs (Effect_ReadStream 0x42A990).
//   +0x04 type · +0x08 loaded · +0x09 filename[256] · +0x110 std::vector<Emitter*>
//   +0x128 magicId (comparé au 1er u32 fichier) · +0x12C/+0x130 contexte a4/a5.
class Effect {
public:
    Effect() = default;
    explicit Effect(uint32_t magic) : magicId(magic) {}
    ~Effect();
    Effect(const Effect&)            = delete;
    Effect& operator=(const Effect&) = delete;

    int                   type    = 0;        // +0x04  (@0x42A9C0)
    bool                  loaded  = false;    // +0x08  (=1 en fin de lecture @0x42AB7A)
    char                  filename[256] = {}; // +0x09  (Crt_StrcpyS @0x42A92D)
    std::vector<Emitter*> emitters;           // +0x110 (+272) stride 4
    uint32_t              magicId = 0;         // +0x128 (+296) magic attendu (@0x42AA18)
    int                   ctxA4   = 0;         // +0x12C (+300) contexte (@0x42A9E2)
    int                   ctxA5   = 0;         // +0x130 (+304) contexte (@0x42A9F0)

    // Effect_ClearEmitters 0x42A7F0 — détruit tous les émetteurs, vide le vecteur.
    void ClearEmitters();                                                    // 0x42A7F0

    // Effect_ResetAllEmitters 0x42B230 — reset runtime de chaque émetteur.
    void ResetAllEmitters();                                                 // 0x42B230

    // Effect_ReadStream 0x42A990 — vérifie le magic, alloue `count` émetteurs,
    // les construit + reset + lit (Emitter_ReadFile). false si magic/lecture KO.
    bool ReadStream(int effType, HANDLE hFile, int a4, int a5);              // 0x42A990

    // Effect_LoadFile 0x42A920 — ouvre `path` (CreateFileA) puis ReadStream.
    bool LoadFile(const char* path, int effType, int a4, int a5);            // 0x42A920
};

} // namespace ts2::gfx

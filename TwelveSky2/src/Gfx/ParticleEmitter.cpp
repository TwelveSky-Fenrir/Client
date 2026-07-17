// Gfx/ParticleEmitter.cpp — implémentation FIDÈLE du conteneur cEmitter/cEffect.
//
// Vérité IDA : TwelveSky2.exe (imagebase 0x400000). Chaque bloc cite son ancre
// (nom + 0xADDR). Voir ParticleEmitter.h pour la carte des structures et
// Docs/TS2_DEEP_PARTICLE.md §3.6–§3.8.
#include "ParticleEmitter.h"
#include <new>       // std::nothrow
#include <cstring>   // (utilitaires C)

namespace ts2::gfx {

// ---------------------------------------------------------------------------
// Hooks des sous-types opaques (FxObj/Mesh) — sous-système mesh HORS cluster.
static EmitterSubtypeHooks s_subHooks;

void SetEmitterSubtypeHooks(const EmitterSubtypeHooks& hooks) { s_subHooks = hooks; }
const EmitterSubtypeHooks& EmitterSubtypeHooksGet()          { return s_subHooks; }

// ---------------------------------------------------------------------------
// Lit `size` octets dans `dst` ; renvoie true si TOUT est lu. Mimique chaque
// étape gardée `NumberOfBytesRead == size` d'Emitter_ReadFile 0x424D30.
static bool Emit_ReadExact(HANDLE hFile, void* dst, DWORD size) {
    DWORD got = 0;
    return ReadFile(hFile, dst, size, &got, nullptr) && got == size;
}

// ---------------------------------------------------------------------------
// Allocation/libération d'un nœud PtclPool (60 o) de la timeline.
//
// Emit_NewPool — Crt_OperatorNew(60) + init (scale=1, initialized=0, def=0,
// particles=0) ; motif répété @0x4250B6 / 0x42517A / 0x429C5C.
static PtclPool* Emit_NewPool() {
    PtclPool* p = new (std::nothrow) PtclPool();   // value-init : tout à 0
    if (!p) return nullptr;
    p->scale[0] = p->scale[1] = p->scale[2] = 1.0f;
    p->initialized = 0;
    p->def         = nullptr;
    p->particles   = nullptr;
    return p;
}

// Emit_FreePoolNode — PtclDef_FreeNode 0x429380 : libère le tableau de particules
// (HeapFree), remet init/def à 0 et scale=1, PUIS détruit l'objet pool lui-même
// (Crt_FreeBase). Utilisé à la destruction et par RebuildInstances.
static void Emit_FreePoolNode(PtclPool* p) {
    if (!p) return;
    if (p->particles) {                                        // a1[14]=+56 @0x429380
        HeapFree(GetProcessHeap(), 0, p->particles);           // @0x42939F
        p->particles = nullptr;                                // a1[14]=0 @0x4293A5
    }
    p->initialized = 0;                                        // a1[3]=+12 @0x429383
    p->def         = nullptr;                                  // a1[4]=+16 @0x42938A
    p->scale[0] = p->scale[1] = p->scale[2] = 1.0f;            // a1[0..2]=1 @0x4293AF
    delete p;                                                  // Crt_FreeBase @0x4293B7
}

// ===========================================================================
// Emitter (cEmitter)
// ===========================================================================

// Emitter_Construct 0x424A10 — pose la vtable, initialise les membres STL, met
// les scalaires à 0 (field224=1, flag230=1) et appelle ResetRuntime. Ici les
// membres STL/scalaires sont initialisés par la liste d'init de la classe ; il
// ne reste que le reset runtime (tailPool nul à ce stade → sans effet).
Emitter::Emitter() {
    ResetRuntime();   // @0x424AE8 (net : flag230 repasse à 0)
}

Emitter::~Emitter() {
    FreeSubObjects();
}

// Libère PtclDef/FxObj/Mesh + tous les pools possédés. La destruction exacte du
// binaire passe par la vtable off_7ED548 (dtor NON tracé) ; on libère ici tout
// ce que Construct/ReadFile ont alloué, d'après la possession prouvée.
// TODO(ancre) : tracer le destructeur virtuel off_7ED548 si un écart apparaît.
void Emitter::FreeSubObjects() {
    for (auto& kf : keyframes)
        if (kf.pool) { Emit_FreePoolNode(kf.pool); kf.pool = nullptr; }
    for (auto& inst : instances)
        if (inst.pool) { Emit_FreePoolNode(inst.pool); inst.pool = nullptr; }
    if (tailPool) { Emit_FreePoolNode(tailPool); tailPool = nullptr; }
    if (ptclDef)  { delete ptclDef; ptclDef = nullptr; }        // Crt_OperatorNew(236) @0x424E32

    const EmitterSubtypeHooks& h = s_subHooks;
    if (fxObj) { if (h.fxObjFree) h.fxObjFree(fxObj); fxObj = nullptr; }
    if (mesh)  { if (h.meshFree)  h.meshFree(mesh);   mesh  = nullptr; }
}

// ---------------------------------------------------------------------------
// Emitter_ReadKeyframe 0x4257F0 — 2 dwords lus + reste zéro-initialisé.
bool Emitter_ReadKeyframe(HANDLE hFile, EmitterNode* kf) {
    if (!Emit_ReadExact(hFile, &kf->value, 4)) return false;   // a2+0 @0x42580B
    if (!Emit_ReadExact(hFile, &kf->time,  4)) return false;   // a2+4 @0x425827
    kf->pool = nullptr;                                        // a2+20 = 0 @0x425832
    kf->f8   = 0.0f;                                           // a2+8  = 0 @0x425839
    kf->f12  = 0.0f;                                           // a2+12 = 0 @0x42583E
    kf->f16  = 0.0f;                                           // a2+16 = 0 @0x425842
    return true;
}

// ---------------------------------------------------------------------------
// Emitter_ReadFile 0x424D30 — parseur binaire de l'émetteur.
bool Emitter::ReadFile(int emitterType, HANDLE hFile, int a4, int a5) {
    type = emitterType;                                         // +4 @0x424D52

    if (!Emit_ReadExact(hFile, vec3, 12)) return false;         // +8  @0x424D5D
    if (!Emit_ReadExact(hFile, &flag, 1)) return false;         // +20 @0x424D78
    if (!Emit_ReadExact(hFile, mode,  8)) return false;         // +24 @0x424D93
    if (!Emit_ReadExact(hFile, &f32,  4)) return false;         // +32 @0x424DAE
    if (!Emit_ReadExact(hFile, &f36,  4)) return false;         // +36 @0x424DC9
    if (!Emit_ReadExact(hFile, &f40,  4)) return false;         // +40 @0x424DE4
    if (!Emit_ReadExact(hFile, &f44,  4)) return false;         // +44 @0x424DFB

    uint32_t subtype = 0;
    if (!Emit_ReadExact(hFile, &subtype, 4)) return false;      // v37 @0x424E1B

    if (subtype == 1) {                                         // @0x424E2B
        // Sous-type 1 : PtclDef 236 o (leaf Object B — ParticleSystem.h).
        PtclDef* d = new PtclDef();                             // Crt_OperatorNew(236) @0x424E32
        PtclDef_Init(d);                                        // @0x424E3E
        ptclDef = d;                                            // +48 @0x424E47
        PtclDef_Reset(d);                                       // @0x424E4A
        if (!PtclDef_ReadFile(ptclDef, hFile, a4, a5))          // @0x424E5C
            return false;
    } else if (subtype == 2) {                                  // @0x424E76
        // Sous-type 2 : FxObj (type==1) ou Mesh — BLOCS OPAQUES via hook.
        const EmitterSubtypeHooks& h = s_subHooks;
        if (type == 1) {                                        // *(a2+4)==1 @0x424E7C
            if (!h.fxObjRead) return false;    // sans hook : on n'invente pas le format FxObj
            fxObj = h.fxObjRead(hFile, a4, a5);                 // FxObj_ReadStream 0x4327E0 @0x424ECE
            if (!fxObj) return false;
        } else {
            if (!h.meshLoad) return false;     // sans hook : on n'invente pas le format Mesh
            mesh = h.meshLoad(hFile);                           // Mesh_LoadMOBJECT2 0x4318C0 @0x4250F5
            if (!mesh) return false;
        }
        // Canaux alpha (stride 8) — lus UNIQUEMENT pour le sous-type 2.
        uint32_t chCount = 0;
        if (!Emit_ReadExact(hFile, &chCount, 4)) return false;  // v38 @0x424EED
        channels.assign(static_cast<size_t>(chCount), EmitterChannel{}); // StlVec_Resize8Zero @0x424F0D
        for (uint32_t i = 0; i < chCount; ++i) {                // boucle @0x424F55
            // 8 o : {temps:float@+0, octet@+4 (+3 padding)} — lecture directe.
            if (!Emit_ReadExact(hFile, &channels[i], 8)) return false;
        }
    } else {
        return false;                                           // sous-type inconnu @0x424E76
    }

    // --- Queue commune aux deux sous-types : nom + keyframes ---
    uint32_t nameLen = 0;
    if (!Emit_ReadExact(hFile, &nameLen, 4)) return false;      // @0x424FB6
    std::vector<char> nameBuf(static_cast<size_t>(nameLen) + 1, 0); // Crt_OperatorNewThunk(len+1) @0x424FD5
    if (nameLen && !Emit_ReadExact(hFile, nameBuf.data(), nameLen)) return false; // @0x424FED
    nameBuf[nameLen] = 0;                                       // @0x424FE2
    name.assign(nameBuf.data());                               // Emitter_AssignStringField (strlen) @0x425005

    uint32_t kfCount = 0;
    if (!Emit_ReadExact(hFile, &kfCount, 4)) return false;      // v39 @0x42502A
    keyframes.assign(static_cast<size_t>(kfCount), EmitterNode{}); // StlVec_Resize24 @0x425044
    for (uint32_t i = 0; i < kfCount; ++i) {                    // boucle @0x425084
        if (!Emitter_ReadKeyframe(hFile, &keyframes[i]))        // @0x42508E
            return false;
        if (ptclDef) {                                          // if (a2+48) @0x42509D
            PtclPool* pool = Emit_NewPool();                    // Crt_OperatorNew(60) @0x4250A8
            if (!pool) return false;
            keyframes[i].pool = pool;                           // elem+20 @0x42511D
        }
    }

    // Pool « queue » (si PtclDef) : alloué ET dimensionné (PtclDef_AllocPool).
    if (ptclDef) {                                              // if (a2+48) @0x425165
        tailPool = Emit_NewPool();                              // Crt_OperatorNew(60) @0x42516C
        if (!tailPool) return false;
        PtclDef_AllocPool(ptclDef, tailPool);                  // @0x4251AF
    }

    // Post-traitement timeline (dans l'ordre exact du binaire).
    if (fxObj || mesh)          BuildAlphaLUT();                // @0x4251C0
    if (mode[0] == 1)           RebuildInstances();             // @0x4251CC
    if (mode[0] == 2 && mode[1] == 0) ComputeDuration();        // @0x4251DE
    return true;                                                // @0x424E6B
}

// ---------------------------------------------------------------------------
// Emitter_BuildAlphaLUT 0x425C10 — interpole les canaux alpha (float temps, octet
// valeur) en une LUT d'octets d'UNE valeur par frame du sous-objet FxObj/Mesh.
void Emitter::BuildAlphaLUT() {
    const EmitterSubtypeHooks& h = s_subHooks;

    // Longueur de la LUT = nb de frames du sous-objet (OPAQUE → hook).
    int len;
    if (type == 1)                                             // a1[1]==1 @0x425C17
        len = (fxObj && h.fxObjFrames) ? h.fxObjFrames(fxObj) : 0; // *(*(FxObj+24)+264) @0x425C1F
    else
        len = (mesh && h.meshFrames)  ? h.meshFrames(mesh)   : 0;  // *(*(Mesh+8)+84) @0x425C2D
    if (len < 0) len = 0;

    alphaLUT.assign(static_cast<size_t>(len), 0);             // StlVec_ResizeByteImpl @0x425C3C

    // Garde : sans canal, le binaire déréférencerait un enregistrement absent
    // (la LUT reste alors à 0). On évite l'UB sans changer le résultat.
    const int chCount = static_cast<int>(channels.size());
    if (len <= 0 || chCount <= 0) return;

    int ci = 0;                                               // v2 : index de canal courant
    for (float frame = 0.0f; frame < static_cast<float>(len); frame += 1.0f) { // @0x425C5C..0x425EC0
        const int idx = static_cast<int>(frame);
        if (channels[ci].time == frame) {                     // @0x425C91
            alphaLUT[idx] = channels[ci].value;               // @0x425CF8
        } else if (ci + 1 < chCount) {
            // Interpolation linéaire canal ci → ci+1 (@0x425D75 / 0x425E59).
            const float denom = channels[ci + 1].time - channels[ci].time;
            const float w     = (frame - channels[ci].time) / denom;
            const int   a     = channels[ci].value;
            const int   b     = channels[ci + 1].value;
            // (int)((double)(b-a)·w + (double)a) puis octet de poids faible (@0x425E59).
            alphaLUT[idx] = static_cast<uint8_t>(static_cast<int>(
                static_cast<double>(b - a) * static_cast<double>(w) + static_cast<double>(a)));
        } else {
            // Au-delà du dernier canal : maintien de la dernière valeur (garde).
            alphaLUT[idx] = channels[ci].value;
        }
        // Avance l'index quand la frame suivante dépasse le temps du canal suivant.
        if (ci < chCount - 1 && channels[ci + 1].time <= frame + 1.0f) // @0x425E6F / 0x425EA1
            ++ci;
    }
}

// ---------------------------------------------------------------------------
// Emitter_ComputeDuration 0x429EB0 — durée = somme des temps de keyframes (hors
// premier). Si mode==2 (durée) et que la durée est < field19 du PtclDef, la
// clampe dans field19 et réinitialise les instances.
void Emitter::ComputeDuration() {
    duration = 0.0f;                                          // +232 @0x429EB3
    for (size_t i = 0; i < keyframes.size(); ++i) {
        if (i != 0)                                           // saute le premier (v2 != begin)
            duration += keyframes[i].time;                   // += *(float*)(v2+4) @0x429F30
    }
    if (ptclDef && mode[0] == 2 && mode[1] == 0
        && static_cast<double>(duration) < static_cast<double>(ptclDef->field19)) { // @0x429F7E
        ptclDef->field19 = duration;                         // def+76 = duration @0x429F87
        ClearInstances();                                    // @0x429F8A
    }
}

// ---------------------------------------------------------------------------
// Emitter_RebuildInstances 0x429A90 — pour un émetteur mode==1 à EXACTEMENT 2
// keyframes : (re)construit `f32` instances, chacune {flag=0, échelle=1, pool}.
bool Emitter::RebuildInstances() {
    if (mode[0] != 1 || keyframes.size() != 2 || f32 < 0)    // garde @0x429AC9
        return true;

    // 1. Libère les pools des instances existantes (FreeNode = free + delete).
    for (auto& inst : instances)
        if (inst.pool) { Emit_FreePoolNode(inst.pool); inst.pool = nullptr; }

    // 2. Vide puis redimensionne à f32 (StlVec_EraseRange24 + StlVec_Resize24).
    instances.clear();                                        // @0x429C0C
    instances.assign(static_cast<size_t>(f32), EmitterNode{}); // @0x429C16

    // 3. Initialise chaque instance.
    for (int i = 0; i < f32; ++i) {                           // @0x429C28
        EmitterNode& inst = instances[i];
        inst.value = 0;                                       // elem[0]=0 @0x429CCD
        inst.time  = 1.0f;                                    // elem[4]=1.0 (v36) @0x429CD3
        inst.f8 = inst.f12 = inst.f16 = 0.0f;                 // elem[8/12/16]=0 (v17) @0x429CDA
        PtclPool* pool = nullptr;
        if (tailPool) {                                       // if (*(a1+236)) @0x429C40 : émetteur à PtclDef
            pool = Emit_NewPool();                            // Crt_OperatorNew(60) @0x429C4C
            if (!pool) {                                      // chemin d'échec d'alloc @0x429D11
                for (int j = 0; j < i; ++j)
                    if (instances[j].pool) { Emit_FreePoolNode(instances[j].pool); instances[j].pool = nullptr; }
                return false;                                 // @0x429D01
            }
            PtclDef_AllocPool(ptclDef, pool);                // @0x429C93
        }
        inst.pool = pool;                                    // elem[20]=v18 @0x429CEF
    }
    return true;
}

// ---------------------------------------------------------------------------
// Emitter_ClearInstances 0x429F90 — pour chaque nœud (keyframe puis instance)
// portant un pool : libère ses particules + le remet à zéro puis le réalloue
// (PtclDef_FreePool suivi de PtclDef_AllocPool — l'objet pool est CONSERVÉ).
void Emitter::ClearInstances() {
    for (auto& kf : keyframes) {                              // boucle keyframes +88 @0x429FB0
        if (kf.pool) {
            PtclDef_FreePool(kf.pool);                        // free particules + reset (init=0/def=0/scale=1)
            PtclDef_AllocPool(ptclDef, kf.pool);             // @0x42A08E
        }
    }
    for (auto& inst : instances) {                           // boucle instances +240 @0x42A0F0
        if (inst.pool) {
            PtclDef_FreePool(inst.pool);
            PtclDef_AllocPool(ptclDef, inst.pool);           // @0x42A1D3
        }
    }
}

// ---------------------------------------------------------------------------
// Emitter_ResetRuntime 0x42A220 — remet timers/fanions à 0 et, si le pool queue
// existe, libère ses particules puis le réalloue et vide les instances.
void Emitter::ResetRuntime() {
    timer212     = 0.0f;                                      // +212 @0x42A225
    timer216     = 0.0f;                                      // +216 @0x42A22C
    PtclPool* tp = tailPool;                                  // +236 @0x42A232
    timerElapsed = 0.0f;                                      // +208 @0x42A238
    flag230      = 0;                                         // +230 @0x42A23E
    runtime136   = 0;                                         // +136 @0x42A244
    runtime220   = 0;                                         // +220 @0x42A24A
    if (tp) {                                                 // @0x42A252
        if (tp->particles) {                                 // v1+56 @0x42A254
            HeapFree(GetProcessHeap(), 0, tp->particles);     // @0x42A26A
            tp->particles = nullptr;                          // v1+56=0 @0x42A270
        }
        tp->initialized = 0;                                 // v1+12=0 @0x42A257
        tp->def         = nullptr;                           // v1+16=0 @0x42A25A
        tp->scale[0] = tp->scale[1] = tp->scale[2] = 1.0f;   // v1[0..2]=1 @0x42A275
        PtclDef_AllocPool(ptclDef, tp);                      // @0x42A29D
        ClearInstances();                                    // Emitter_ClearInstances @0x42A2A3
    }
}

// ===========================================================================
// Effect (cEffect)
// ===========================================================================

Effect::~Effect() {
    ClearEmitters();
}

// Effect_ClearEmitters 0x42A7F0 — détruit tous les émetteurs, vide le vecteur,
// puis remet loaded=false (*(this+8)=0 @0x42a906) avant de renvoyer 1.
void Effect::ClearEmitters() {
    for (Emitter* e : emitters)
        delete e;
    emitters.clear();
    loaded = false;                                         // *(this+8)=0 @0x42a906
}

// Effect_ResetAllEmitters 0x42B230 — reset runtime de CHAQUE émetteur : la boucle
// (0x42b251) n'appelle QUE Emitter_ResetRuntime (0x42b2a1). Contrairement à la
// boucle de lecture de ReadStream, elle NE remet NI flag230 NI runtime140 (flag230
// est déjà remis à 0 par ResetRuntime @0x42A23E ; runtime140 reste inchangé).
void Effect::ResetAllEmitters() {
    for (Emitter* e : emitters) {
        if (!e) continue;
        e->ResetRuntime();                                  // Emitter_ResetRuntime @0x42b2a1
    }
}

// Effect_ReadStream 0x42A990 — vérifie le magic id, alloue `count` émetteurs, les
// construit + reset + lit. false si le magic ou une lecture échoue.
bool Effect::ReadStream(int effType, HANDLE hFile, int a4, int a5) {
    type = effType;                                          // +4 @0x42A9C0
    ClearEmitters();                                         // @0x42A9C3
    ctxA4 = a4;                                              // +300 @0x42A9E2
    ctxA5 = a5;                                              // +304 @0x42A9F0

    uint32_t magic = 0;
    if (!Emit_ReadExact(hFile, &magic, 4)) return false;     // @0x42A9FE
    if (magic != magicId) return false;                      // *(a1+296) != v19 @0x42AA18

    uint32_t count = 0;
    if (!Emit_ReadExact(hFile, &count, 4)) return false;     // @0x42AA2C
    emitters.reserve(count);                                 // StlVec_Resize4Impl @0x42AA41

    for (uint32_t i = 0; i < count; ++i) {                   // boucle @0x42AA60
        Emitter* e = new Emitter();                          // Crt_OperatorNew(288)+Construct @0x42AA93/AAAF
        e->flag230    = 0;                                   // +230=0 @0x42AAF1
        e->ResetRuntime();                                   // @0x42AAF8
        e->runtime140 = 0;                                   // +140=0 @0x42AAFD
        emitters.push_back(e);                               // *v9 = émetteur (vecteur +272)
        if (!e->ReadFile(type, hFile, a4, a5))               // @0x42AB35
            return false;                                    // (émetteur possédé par le vecteur → libéré au dtor)
    }

    ResetAllEmitters();                                      // @0x42AB73
    loaded = true;                                           // +8=1 @0x42AB7A
    return true;
}

// Effect_LoadFile 0x42A920 — ouvre `path` puis ReadStream, referme le handle.
bool Effect::LoadFile(const char* path, int effType, int a4, int a5) {
    lstrcpynA(filename, path ? path : "", static_cast<int>(sizeof(filename))); // Crt_StrcpyS(+9,256,name) @0x42A92D
    HANDLE h = CreateFileA(path, GENERIC_READ, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);      // @0x42A957
    const bool ok = ReadStream(effType, h, a4, a5);                            // @0x42A961
    CloseHandle(h);                                                            // @0x42A96B / 0x42A977
    return ok;
}

} // namespace ts2::gfx

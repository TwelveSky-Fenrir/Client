// Gfx/ParticleEmitter.cpp — FAITHFUL implementation of the cEmitter/cEffect container.
//
// IDA ground truth: TwelveSky2.exe (imagebase 0x400000). Each block cites its anchor
// (name + 0xADDR). See ParticleEmitter.h for the structure map and
// Docs/TS2_DEEP_PARTICLE.md §3.6–§3.8.
#include "ParticleEmitter.h"
#include <new>       // std::nothrow
#include <cstring>   // (C utilities)

namespace ts2::gfx {

// ---------------------------------------------------------------------------
// Opaque subtype hooks (FxObj/Mesh) — mesh subsystem OUTSIDE this cluster.
static EmitterSubtypeHooks s_subHooks;

void SetEmitterSubtypeHooks(const EmitterSubtypeHooks& hooks) { s_subHooks = hooks; }
const EmitterSubtypeHooks& EmitterSubtypeHooksGet()          { return s_subHooks; }

// ---------------------------------------------------------------------------
// Reads `size` bytes into `dst`; returns true if ALL of it was read. Mimics each
// `NumberOfBytesRead == size` guarded step of Emitter_ReadFile 0x424D30.
static bool Emit_ReadExact(HANDLE hFile, void* dst, DWORD size) {
    DWORD got = 0;
    return ReadFile(hFile, dst, size, &got, nullptr) && got == size;
}

// ---------------------------------------------------------------------------
// Allocation/release of a 60-byte PtclPool node in the timeline.
//
// Emit_NewPool — Crt_OperatorNew(60) + init (scale=1, initialized=0, def=0,
// particles=0); repeated pattern @0x4250B6 / 0x42517A / 0x429C5C.
static PtclPool* Emit_NewPool() {
    PtclPool* p = new (std::nothrow) PtclPool();   // value-init: everything to 0
    if (!p) return nullptr;
    p->scale[0] = p->scale[1] = p->scale[2] = 1.0f;
    p->initialized = 0;
    p->def         = nullptr;
    p->particles   = nullptr;
    return p;
}

// Emit_FreePoolNode — PtclDef_FreeNode 0x429380: releases the particle array
// (HeapFree), resets init/def to 0 and scale=1, THEN destroys the pool object itself
// (Crt_FreeBase). Used at destruction and by RebuildInstances.
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

// Emitter_Construct 0x424A10 — sets the vtable, initializes STL members, sets
// scalars to 0 (field224=1, flag230=1) and calls ResetRuntime. Here the
// STL members/scalars are initialized by the class's init list; only the
// runtime reset remains (tailPool null at this stage → no effect).
Emitter::Emitter() {
    ResetRuntime();   // @0x424AE8 (net effect: flag230 goes back to 0)
}

Emitter::~Emitter() {
    FreeSubObjects();
}

// Releases PtclDef/FxObj/Mesh + all owned pools. The binary's exact destruction goes
// through the vtable off_7ED548 (dtor NOT traced); here we release everything
// Construct/ReadFile allocated, based on proven ownership.
// TODO(anchor): trace the virtual destructor off_7ED548 if a discrepancy appears.
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
// Emitter_ReadKeyframe 0x4257F0 — 2 dwords read + the rest zero-initialized.
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
// Emitter_ReadFile 0x424D30 — binary parser for the emitter.
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
        // Subtype 1: 236-byte PtclDef (leaf Object B — ParticleSystem.h).
        PtclDef* d = new PtclDef();                             // Crt_OperatorNew(236) @0x424E32
        PtclDef_Init(d);                                        // @0x424E3E
        ptclDef = d;                                            // +48 @0x424E47
        PtclDef_Reset(d);                                       // @0x424E4A
        if (!PtclDef_ReadFile(ptclDef, hFile, a4, a5))          // @0x424E5C
            return false;
    } else if (subtype == 2) {                                  // @0x424E76
        // Subtype 2: FxObj (type==1) or Mesh — OPAQUE blocks via hook.
        const EmitterSubtypeHooks& h = s_subHooks;
        if (type == 1) {                                        // *(a2+4)==1 @0x424E7C
            if (!h.fxObjRead) return false;    // no hook: we do not invent the FxObj format
            fxObj = h.fxObjRead(hFile, a4, a5);                 // FxObj_ReadStream 0x4327E0 @0x424ECE
            if (!fxObj) return false;
        } else {
            if (!h.meshLoad) return false;     // no hook: we do not invent the Mesh format
            mesh = h.meshLoad(hFile);                           // Mesh_LoadMOBJECT2 0x4318C0 @0x4250F5
            if (!mesh) return false;
        }
        // Alpha channels (stride 8) — read ONLY for subtype 2.
        uint32_t chCount = 0;
        if (!Emit_ReadExact(hFile, &chCount, 4)) return false;  // v38 @0x424EED
        channels.assign(static_cast<size_t>(chCount), EmitterChannel{}); // StlVec_Resize8Zero @0x424F0D
        for (uint32_t i = 0; i < chCount; ++i) {                // loop @0x424F55
            // 8 bytes: {time:float@+0, byte@+4 (+3 padding)} — direct read.
            if (!Emit_ReadExact(hFile, &channels[i], 8)) return false;
        }
    } else {
        return false;                                           // unknown subtype @0x424E76
    }

    // --- Tail common to both subtypes: name + keyframes ---
    uint32_t nameLen = 0;
    if (!Emit_ReadExact(hFile, &nameLen, 4)) return false;      // @0x424FB6
    std::vector<char> nameBuf(static_cast<size_t>(nameLen) + 1, 0); // Crt_OperatorNewThunk(len+1) @0x424FD5
    if (nameLen && !Emit_ReadExact(hFile, nameBuf.data(), nameLen)) return false; // @0x424FED
    nameBuf[nameLen] = 0;                                       // @0x424FE2
    name.assign(nameBuf.data());                               // Emitter_AssignStringField (strlen) @0x425005

    uint32_t kfCount = 0;
    if (!Emit_ReadExact(hFile, &kfCount, 4)) return false;      // v39 @0x42502A
    keyframes.assign(static_cast<size_t>(kfCount), EmitterNode{}); // StlVec_Resize24 @0x425044
    for (uint32_t i = 0; i < kfCount; ++i) {                    // loop @0x425084
        if (!Emitter_ReadKeyframe(hFile, &keyframes[i]))        // @0x42508E
            return false;
        if (ptclDef) {                                          // if (a2+48) @0x42509D
            PtclPool* pool = Emit_NewPool();                    // Crt_OperatorNew(60) @0x4250A8
            if (!pool) return false;
            keyframes[i].pool = pool;                           // elem+20 @0x42511D
        }
    }

    // "Tail" pool (if PtclDef): allocated AND sized (PtclDef_AllocPool).
    if (ptclDef) {                                              // if (a2+48) @0x425165
        tailPool = Emit_NewPool();                              // Crt_OperatorNew(60) @0x42516C
        if (!tailPool) return false;
        PtclDef_AllocPool(ptclDef, tailPool);                  // @0x4251AF
    }

    // Timeline post-processing (in the binary's exact order).
    if (fxObj || mesh)          BuildAlphaLUT();                // @0x4251C0
    if (mode[0] == 1)           RebuildInstances();             // @0x4251CC
    if (mode[0] == 2 && mode[1] == 0) ComputeDuration();        // @0x4251DE
    return true;                                                // @0x424E6B
}

// ---------------------------------------------------------------------------
// Emitter_BuildAlphaLUT 0x425C10 — interpolates the alpha channels (float time, byte
// value) into a byte LUT with ONE value per frame of the FxObj/Mesh sub-object.
void Emitter::BuildAlphaLUT() {
    const EmitterSubtypeHooks& h = s_subHooks;

    // LUT length = sub-object frame count (OPAQUE → hook).
    int len;
    if (type == 1)                                             // a1[1]==1 @0x425C17
        len = (fxObj && h.fxObjFrames) ? h.fxObjFrames(fxObj) : 0; // *(*(FxObj+24)+264) @0x425C1F
    else
        len = (mesh && h.meshFrames)  ? h.meshFrames(mesh)   : 0;  // *(*(Mesh+8)+84) @0x425C2D
    if (len < 0) len = 0;

    alphaLUT.assign(static_cast<size_t>(len), 0);             // StlVec_ResizeByteImpl @0x425C3C

    // Guard: without a channel, the binary would dereference a missing record
    // (the LUT then stays at 0). We avoid UB without changing the result.
    const int chCount = static_cast<int>(channels.size());
    if (len <= 0 || chCount <= 0) return;

    int ci = 0;                                               // v2: current channel index
    for (float frame = 0.0f; frame < static_cast<float>(len); frame += 1.0f) { // @0x425C5C..0x425EC0
        const int idx = static_cast<int>(frame);
        if (channels[ci].time == frame) {                     // @0x425C91
            alphaLUT[idx] = channels[ci].value;               // @0x425CF8
        } else if (ci + 1 < chCount) {
            // Linear interpolation channel ci → ci+1 (@0x425D75 / 0x425E59).
            const float denom = channels[ci + 1].time - channels[ci].time;
            const float w     = (frame - channels[ci].time) / denom;
            const int   a     = channels[ci].value;
            const int   b     = channels[ci + 1].value;
            // (int)((double)(b-a)·w + (double)a) then low byte (@0x425E59).
            alphaLUT[idx] = static_cast<uint8_t>(static_cast<int>(
                static_cast<double>(b - a) * static_cast<double>(w) + static_cast<double>(a)));
        } else {
            // Past the last channel: hold the last value (guard).
            alphaLUT[idx] = channels[ci].value;
        }
        // Advance the index once the next frame passes the next channel's time.
        if (ci < chCount - 1 && channels[ci + 1].time <= frame + 1.0f) // @0x425E6F / 0x425EA1
            ++ci;
    }
}

// ---------------------------------------------------------------------------
// Emitter_ComputeDuration 0x429EB0 — duration = sum of keyframe times (excluding
// the first). If mode==2 (duration) and the duration is < the PtclDef's field19,
// clamps it to field19 and reinitializes the instances.
void Emitter::ComputeDuration() {
    duration = 0.0f;                                          // +232 @0x429EB3
    for (size_t i = 0; i < keyframes.size(); ++i) {
        if (i != 0)                                           // skips the first (v2 != begin)
            duration += keyframes[i].time;                   // += *(float*)(v2+4) @0x429F30
    }
    if (ptclDef && mode[0] == 2 && mode[1] == 0
        && static_cast<double>(duration) < static_cast<double>(ptclDef->field19)) { // @0x429F7E
        ptclDef->field19 = duration;                         // def+76 = duration @0x429F87
        ClearInstances();                                    // @0x429F8A
    }
}

// ---------------------------------------------------------------------------
// Emitter_RebuildInstances 0x429A90 — for a mode==1 emitter with EXACTLY 2
// keyframes: (re)builds `f32` instances, each {flag=0, scale=1, pool}.
bool Emitter::RebuildInstances() {
    if (mode[0] != 1 || keyframes.size() != 2 || f32 < 0)    // guard @0x429AC9
        return true;

    // 1. Releases the pools of existing instances (FreeNode = free + delete).
    for (auto& inst : instances)
        if (inst.pool) { Emit_FreePoolNode(inst.pool); inst.pool = nullptr; }

    // 2. Clears then resizes to f32 (StlVec_EraseRange24 + StlVec_Resize24).
    instances.clear();                                        // @0x429C0C
    instances.assign(static_cast<size_t>(f32), EmitterNode{}); // @0x429C16

    // 3. Initializes each instance.
    for (int i = 0; i < f32; ++i) {                           // @0x429C28
        EmitterNode& inst = instances[i];
        inst.value = 0;                                       // elem[0]=0 @0x429CCD
        inst.time  = 1.0f;                                    // elem[4]=1.0 (v36) @0x429CD3
        inst.f8 = inst.f12 = inst.f16 = 0.0f;                 // elem[8/12/16]=0 (v17) @0x429CDA
        PtclPool* pool = nullptr;
        if (tailPool) {                                       // if (*(a1+236)) @0x429C40 : PtclDef emitter
            pool = Emit_NewPool();                            // Crt_OperatorNew(60) @0x429C4C
            if (!pool) {                                      // alloc failure path @0x429D11
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
// Emitter_ClearInstances 0x429F90 — for each node (keyframes then instances)
// carrying a pool: releases its particles + resets it then reallocates it
// (PtclDef_FreePool followed by PtclDef_AllocPool — the pool object is KEPT).
void Emitter::ClearInstances() {
    for (auto& kf : keyframes) {                              // keyframes loop +88 @0x429FB0
        if (kf.pool) {
            PtclDef_FreePool(kf.pool);                        // free particles + reset (init=0/def=0/scale=1)
            PtclDef_AllocPool(ptclDef, kf.pool);             // @0x42A08E
        }
    }
    for (auto& inst : instances) {                           // instances loop +240 @0x42A0F0
        if (inst.pool) {
            PtclDef_FreePool(inst.pool);
            PtclDef_AllocPool(ptclDef, inst.pool);           // @0x42A1D3
        }
    }
}

// ---------------------------------------------------------------------------
// Emitter_ResetRuntime 0x42A220 — resets timers/flags to 0 and, if the tail pool
// exists, releases its particles then reallocates it and clears the instances.
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

// Effect_ClearEmitters 0x42A7F0 — destroys all emitters, clears the vector,
// then resets loaded=false (*(this+8)=0 @0x42a906) before returning 1.
void Effect::ClearEmitters() {
    for (Emitter* e : emitters)
        delete e;
    emitters.clear();
    loaded = false;                                         // *(this+8)=0 @0x42a906
}

// Effect_ResetAllEmitters 0x42B230 — runtime reset of EACH emitter: the loop
// (0x42b251) calls ONLY Emitter_ResetRuntime (0x42b2a1). Unlike ReadStream's
// read loop, it resets NEITHER flag230 NOR runtime140 (flag230
// is already reset to 0 by ResetRuntime @0x42A23E; runtime140 stays unchanged).
void Effect::ResetAllEmitters() {
    for (Emitter* e : emitters) {
        if (!e) continue;
        e->ResetRuntime();                                  // Emitter_ResetRuntime @0x42b2a1
    }
}

// Effect_ReadStream 0x42A990 — checks the magic id, allocates `count` emitters,
// constructs + resets + reads them. false if the magic or a read fails.
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

    for (uint32_t i = 0; i < count; ++i) {                   // loop @0x42AA60
        Emitter* e = new Emitter();                          // Crt_OperatorNew(288)+Construct @0x42AA93/AAAF
        e->flag230    = 0;                                   // +230=0 @0x42AAF1
        e->ResetRuntime();                                   // @0x42AAF8
        e->runtime140 = 0;                                   // +140=0 @0x42AAFD
        emitters.push_back(e);                               // *v9 = emitter (vector +272)
        if (!e->ReadFile(type, hFile, a4, a5))               // @0x42AB35
            return false;                                    // (emitter owned by the vector → released at dtor)
    }

    ResetAllEmitters();                                      // @0x42AB73
    loaded = true;                                           // +8=1 @0x42AB7A
    return true;
}

// Effect_LoadFile 0x42A920 — opens `path` then ReadStream, closes the handle.
bool Effect::LoadFile(const char* path, int effType, int a4, int a5) {
    lstrcpynA(filename, path ? path : "", static_cast<int>(sizeof(filename))); // Crt_StrcpyS(+9,256,name) @0x42A92D
    HANDLE h = CreateFileA(path, GENERIC_READ, 0, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);      // @0x42A957
    const bool ok = ReadStream(effType, h, a4, a5);                            // @0x42A961
    CloseHandle(h);                                                            // @0x42A96B / 0x42A977
    return ok;
}

} // namespace ts2::gfx

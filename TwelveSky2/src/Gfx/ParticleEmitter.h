// Gfx/ParticleEmitter.h — cEmitter/cEffect container of the "GXD" engine (Object B).
//
// FAITHFUL rewrite (bit-exact target) of the timeline CONTAINER stacked on top of
// the PtclDef/PtclPool leaf (see Gfx/ParticleSystem.h). Ground truth = IDB TwelveSky2.exe
// (imagebase 0x400000); each block cites its IDA anchor (name + 0xADDR).
// See Docs/TS2_DEEP_PARTICLE.md §3.6–§3.8 and §5-T1.
//
// Two levels:
//   Emitter (cEmitter)  288 bytes  — a PtclDef|FxObj|Mesh + name + keyframe
//                                timeline (stride 24, one PtclPool per keyframe)
//                                + alpha channels (stride 8) + per-frame alpha LUT.
//                                Construct 0x424A10 / ReadFile 0x424D30.
//   Effect  (cEffect)  ~308 bytes  — magic id + std::vector<Emitter*>.
//                                ReadStream 0x42A990 / LoadFile 0x42A920.
//
// ⚠ CLUSTER BOUNDARY: the FxObj (32 bytes, FxObj_ReadStream 0x4327E0) and
//   Mesh (12 bytes, Mesh_LoadMOBJECT2 0x4318C0) subtypes belong to the mesh
//   subsystem (OUTSIDE the particle cluster). They are modeled as OPAQUE BLOCKS +
//   injectable hooks: the loader only knows "allocate/read → handle" and "handle →
//   frame count". Without a hook, a subtype-2 emitter fails to load — we do
//   NOT INVENT the mesh format (rule #0). Subtype 1 (PtclDef) is complete.
//
// ⚠ No standalone `.PTCL`/effect file ships on disk: the file path
//   (LoadFile) is effectively dead; the container is INSTANTIATED at runtime by
//   animated meshes. We port the structure + parser for this wiring (T2).
//
// Uses only the Windows SDK + the Object B primitives of ParticleSystem.h.
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include "ParticleSystem.h"   // PtclDef, PtclPool, PtclDef_ReadFile/AllocPool/FreePool

namespace ts2::gfx {

// ---------------------------------------------------------------------------
// EmitterNode — 24 bytes. REUSED for the emitter's two stride-24 vectors:
//   • keyframes (cEmitter+88)  — Emitter_ReadKeyframe 0x4257F0 (+0/+4 read from disk)
//   • instances (cEmitter+240) — Emitter_RebuildInstances 0x429A90 (transform)
// Proven layout: keyframe {value:u32@+0, time:float@+4, 3 zero floats@+8/+12/+16,
// PtclPool*@+20}. In instance mode: {flag=0@+0, scale=1.0@+4, 0@+8/+12/+16, pool@+20}.
struct EmitterNode {
    uint32_t  value;    // +0x00  keyframe: value; instance: flag (=0)
    float     time;     // +0x04  keyframe: time (summed by ComputeDuration); instance: 1.0
    float     f8;       // +0x08  zero-init (Emitter_ReadKeyframe @0x425839)
    float     f12;      // +0x0C  zero-init
    float     f16;      // +0x10  zero-init
    PtclPool* pool;     // +0x14  60-byte PtclPool (allocated if the emitter carries a PtclDef)
};
static_assert(sizeof(EmitterNode) == 24, "EmitterNode must be 24 bytes (keyframe/instance stride)");

// ---------------------------------------------------------------------------
// EmitterChannel — 8 bytes. "alpha-kf" vector (cEmitter+112), read ONLY for
// the FxObj/Mesh subtypes. Each on-disk record = 8 bytes: {time:float@+0,
// value:byte@+4 (+3 padding bytes read as-is)}. Interpolated into a LUT by
// Emitter_BuildAlphaLUT 0x425C10. The read writes time@+0 then the dword@+4
// (only the low byte carries the alpha — see 0x424F73/0x424F79).
struct EmitterChannel {
    float    time;      // +0x00  time abscissa (frame)
    uint8_t  value;     // +0x04  alpha byte (0..255)
    uint8_t  _pad[3];   // +0x05  disk padding (dword copied whole @0x424F79)
};
static_assert(sizeof(EmitterChannel) == 8, "EmitterChannel must be 8 bytes (channel stride)");

// ---------------------------------------------------------------------------
// Opaque FxObj/Mesh subtype hooks (mesh subsystem, OUTSIDE this cluster).
// Injected by the mesh front once ported; null => subtype 2 does not load.
//   fxObjRead   : FxObj_ReadStream 0x4327E0 — allocates+reads the FxObj object (32 bytes) →
//                 opaque handle (nullptr = failure). type==1 only.
//   fxObjFrames : frame count for the alpha LUT = *(*(FxObj+24)+264) @0x425C1F.
//   meshLoad    : Mesh_LoadMOBJECT2 0x4318C0 (flag=1) — allocates+reads the mesh (12 bytes).
//   meshFrames  : frame count for the alpha LUT = *(*(Mesh+8)+84) @0x425C2D.
//   fxObjFree/meshFree : optional handle release (clean destruction);
//                        null => the handle leaks (the emitter does not own the format).
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
// Emitter — cEmitter, 288 bytes (Emitter_Construct 0x424A10, vtable off_7ED548).
// Idiomatic rewrite: std::string/std::vector replace the binary's STL members
// (the original IS std::string/std::vector — this IS faithful). The 288-byte
// offset map below documents the original binary layout; the C++ size
// differs (modern STL) because this is NOT a memory overlay.
//
//   +0x00  vtable off_7ED548              +0x58  keyframes  std::vector (stride 24)
//   +0x04  type (input arg)               +0x70  channels   std::vector (stride 8)
//   +0x08  vec3[3]                        +0xB4  name2 (string, empty)     [runtime]
//   +0x14  flag (byte)                    +0xE8  duration (ComputeDuration)
//   +0x18  mode[2] (==1 rebuild/==2 dur.) +0xEC  tailPool  PtclPool*
//   +0x20  f32 (instanceCount if mode==1) +0xF0  instances std::vector (stride 24)
//   +0x24  f36/f40/f44 (3 floats)         +0x108 alphaLUT  std::vector<byte>
//   +0x30  ptclDef  (subtype 1)           +0xD0  runtime timers (+208..+232)
//   +0x34  fxObj    (subtype 2,type==1)   +0x88  runtime state (+136/+140)
//   +0x38  mesh     (subtype 2)
//   +0x3C  name (length-prefixed string)
class Emitter {
public:
    Emitter();                          // Emitter_Construct 0x424A10
    ~Emitter();
    Emitter(const Emitter&)            = delete;   // heap ownership: no copy
    Emitter& operator=(const Emitter&) = delete;

    // --- On-disk fields (EXACT order of Emitter_ReadFile 0x424D30) ---
    int          type    = 0;                 // +0x04  a1 (copied @0x424D52)
    float        vec3[3] = {0, 0, 0};         // +0x08  12 bytes (@0x424D5D)
    uint8_t      flag    = 0;                 // +0x14  1 byte  (@0x424D78)
    int32_t      mode[2] = {0, 0};            // +0x18  8 bytes  (@0x424D93); mode[0]=1→rebuild, =2→duration
    int32_t      f32     = 0;                 // +0x20  dword (@0x424DAE); instance count if mode==1
    float        f36 = 0.0f, f40 = 0.0f, f44 = 0.0f; // +0x24/+0x28/+0x2C (3 floats)

    // On-disk subtype (u32: 1=PtclDef, 2=FxObj|Mesh). Only ONE of the three is non-null.
    PtclDef*     ptclDef = nullptr;           // +0x30  subtype 1 (@0x424E47)
    void*        fxObj   = nullptr;           // +0x34  subtype 2 & type==1, OPAQUE (@0x424EAB)
    void*        mesh    = nullptr;           // +0x38  subtype 2 otherwise, OPAQUE (@0x4250E6)

    std::string  name;                        // +0x3C  string (Emitter_AssignStringField 0x412750)
    std::vector<EmitterNode>    keyframes;    // +0x58  timeline (stride 24, one PtclPool/kf)
    std::vector<EmitterChannel> channels;     // +0x70  alpha channels (stride 8, subtype 2)

    // --- Runtime / timeline ---
    float        duration = 0.0f;             // +0xE8  sum of keyframe times (ComputeDuration)
    PtclPool*    tailPool = nullptr;          // +0xEC  "tail" pool (if PtclDef) — AllocPool'd
    std::vector<EmitterNode> instances;       // +0xF0  duplicated instances (RebuildInstances)
    std::vector<uint8_t>     alphaLUT;        // +0x108 per-frame alpha curve (BuildAlphaLUT)

    // Runtime flags/timers set by Construct/ResetRuntime (roles at tick time, T2).
    int32_t      runtime136 = 0;              // +0x88  timeline read state (reset 0)
    uint8_t      runtime140 = 0;              // +0x8C  runtime flag (reset 0)
    float        timerElapsed = 0.0f;         // +0xD0  (+208) emission clock (reset 0)
    float        timer212 = 0.0f;             // +0xD4  (+212) reset 0
    float        timer216 = 0.0f;             // +0xD8  (+216) reset 0
    int32_t      runtime220 = 0;              // +0xDC  (+220) reset 0
    int32_t      field224 = 1;                // +0xE0  (+224) = 1 (Construct @0x424ABC)
    uint8_t      flag228 = 0, flag229 = 0;    // +0xE4/+0xE5 (Construct)
    uint8_t      flag230 = 1;                 // +0xE6  = 1 (Construct); reset 0

    // Emitter_ReadFile 0x424D30 — deserializes the emitter from an open HANDLE.
    // `emitterType` = a1 (type context, copied to +0x04). Returns false on
    // any partial read, unknown subtype, or subtype 2 without a hook.
    bool ReadFile(int emitterType, HANDLE hFile, int a4, int a5);   // 0x424D30

    // Emitter_ResetRuntime 0x42A220 — resets timers/flags to 0 and reallocates the
    // tail pool (releases its particles then PtclDef_AllocPool). Clears the instances.
    void ResetRuntime();                                            // 0x42A220

private:
    void BuildAlphaLUT();       // Emitter_BuildAlphaLUT   0x425C10
    void ComputeDuration();     // Emitter_ComputeDuration 0x429EB0
    bool RebuildInstances();    // Emitter_RebuildInstances 0x429A90
    void ClearInstances();      // Emitter_ClearInstances  0x429F90
    void FreeSubObjects();      // release of PtclDef/FxObj/Mesh/pools (destruction)
};

// Emitter_ReadKeyframe 0x4257F0 — reads a keyframe record (2 dwords) from
// the file and zero-initializes the rest (+8/+12/+16 = 0, pool@+20 = nullptr).
// Exposed for testing; used by Emitter::ReadFile.
bool Emitter_ReadKeyframe(HANDLE hFile, EmitterNode* kf);           // 0x4257F0

// ---------------------------------------------------------------------------
// Effect — cEffect, ~308 bytes. Emitter container (Effect_ReadStream 0x42A990).
//   +0x04 type · +0x08 loaded · +0x09 filename[256] · +0x110 std::vector<Emitter*>
//   +0x128 magicId (compared to the file's 1st u32) · +0x12C/+0x130 context a4/a5.
class Effect {
public:
    Effect() = default;
    explicit Effect(uint32_t magic) : magicId(magic) {}
    ~Effect();
    Effect(const Effect&)            = delete;
    Effect& operator=(const Effect&) = delete;

    int                   type    = 0;        // +0x04  (@0x42A9C0)
    bool                  loaded  = false;    // +0x08  (=1 at end of read @0x42AB7A)
    char                  filename[256] = {}; // +0x09  (Crt_StrcpyS @0x42A92D)
    std::vector<Emitter*> emitters;           // +0x110 (+272) stride 4
    uint32_t              magicId = 0;         // +0x128 (+296) expected magic (@0x42AA18)
    int                   ctxA4   = 0;         // +0x12C (+300) context (@0x42A9E2)
    int                   ctxA5   = 0;         // +0x130 (+304) context (@0x42A9F0)

    // Effect_ClearEmitters 0x42A7F0 — destroys all emitters, clears the vector.
    void ClearEmitters();                                                    // 0x42A7F0

    // Effect_ResetAllEmitters 0x42B230 — runtime reset of each emitter.
    void ResetAllEmitters();                                                 // 0x42B230

    // Effect_ReadStream 0x42A990 — checks the magic, allocates `count` emitters,
    // constructs + resets + reads them (Emitter_ReadFile). false if magic/read fails.
    bool ReadStream(int effType, HANDLE hFile, int a4, int a5);              // 0x42A990

    // Effect_LoadFile 0x42A920 — opens `path` (CreateFileA) then ReadStream.
    bool LoadFile(const char* path, int effType, int a4, int a5);            // 0x42A920
};

} // namespace ts2::gfx

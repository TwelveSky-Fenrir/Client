// Gfx/ZoneFxEmitter.cpp — FAITHFUL implementation of the "Object A" (.WP) particle engine.
//
// IDA ground truth: TwelveSky2.exe (imagebase 0x400000). Each block cites its anchor (name + 0xADDR).
// See ZoneFxEmitter.h for the structure map and Docs/TS2_EXTRACT_WP_EMITTERS.md for the
// byte-exact spec. Uses only the Windows/Direct3D9 + d3dx9 SDK (like the binary).
#include "Gfx/ZoneFxEmitter.h"
#include <cstring>   // std::memcpy
#include <cmath>     // sqrtf

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

// pi/180 — exact binary literal (flt_7BB26C = 0x3C8EFA35 = 0.017453292), read by UpdateEmit
// @0x6a7947/@0x6a7961/@0x6a797b (degrees -> radians of the original rotations).
static constexpr float kDeg2Rad = 0.017453292f;
// ±100 — random direction bounds (0xC2C80000 / 0x42C80000 pushed @0x6a7a9e/@0x6a7a99).
static constexpr float kRandLo = -100.0f;
static constexpr float kRandHi =  100.0f;
// ±0.5 — half-box (flt_7A939C = 0x3F000000 = 0.5 ; flt_7BB294 = 0xBF000000 = -0.5) for cases 5/6.
static constexpr float kBoxHalfPos =  0.5f;
static constexpr float kBoxHalfNeg = -0.5f;
// 1.0 — emission threshold (flt_7ED9B0 = 0x3F800000), compared against spawnAccum @0x6a7877/@0x6a8001.
static constexpr float kSpawnThreshold = 1.0f;

// Global gate (mirror of dword_800080 != 0). The module owns its scratch -> ready by default.
static bool s_zoneFxReady = true;
void ZoneFx_SetReady(bool ready) { s_zoneFxReady = ready; }
bool ZoneFx_Ready()              { return s_zoneFxReady; }

// ===========================================================================================
//  ZoneFx_BuildTemplate — fills the 232-byte template from the on-disk asset::FxNode blob.
//  Anchor: Fx_NodeLoadFromHandle 0x6A69F0 (this+0=enabled=1 ; texture at +52 ; 18 ReadFile
//  calls filling [+72,+216) = the 144 bytes of asset::FxNode.fields, copied here as-is).
// ===========================================================================================
void ZoneFx_BuildTemplate(FxEmitterTemplate* tmpl, const uint8_t* diskFields, size_t diskFieldsSize,
                          IDirect3DTexture9* texture) {
    if (!tmpl) return;
    std::memset(tmpl, 0, sizeof(*tmpl));
    tmpl->enabled = 1;              // *this = 1 for a loaded node (Fx_NodeLoadFromHandle @0x6a6cf4)
    tmpl->texture = texture;        // GPU tex at +52 (SetTexture(0,*(tmpl+52)) @0x6a745e)
    // Keyframe path NOT ported: frameTrack/frameCount/frameMatrices stay null -> UpdateEmit's
    // frame matrix takes the else-branch identity (@0x6a78c3), faithful to the ambient case
    // without emitter-transform animation. TODO anchor 0x6a787d: bake the quaternion track (this+14).
    if (diskFields && diskFieldsSize) {
        const size_t n = (diskFieldsSize < kFxTemplateDiskSize) ? diskFieldsSize : kFxTemplateDiskSize;
        std::memcpy(reinterpret_cast<uint8_t*>(tmpl) + kFxTemplateDiskOffset, diskFields, n);
    }
}

// ===========================================================================================
//  ZoneFx_ComputeGradients — Particle_ComputeGradients 0x6A6D10.
//  Clamps the fields (binary float indices = this+N -> byte 4N) then derives colorRate.
// ===========================================================================================
void ZoneFx_ComputeGradients(FxEmitterTemplate* t) {
    if (!t || !t->enabled) return;                      // if(*this) @0x6a6d10
    if (t->kfFps        < 0.0f) t->kfFps        = 0.0f; // this+19 (+76)  @0x6a6d2a
    if (t->rate         < 0.0f) t->rate         = 0.0f; // this+20 (+80)  @0x6a6d3d
    if (t->speed        < 0.0f) t->speed        = 0.0f; // this+22 (+88)  @0x6a6d50
    if (t->box[0]       < 0.0f) t->box[0]       = 0.0f; // this+23 (+92)  @0x6a6d63
    if (t->box[1]       < 0.0f) t->box[1]       = 0.0f; // this+24 (+96)  @0x6a6d76
    if (t->box[2]       < 0.0f) t->box[2]       = 0.0f; // this+25 (+100) @0x6a6d89
    if (t->particleLife < 0.0099999998f) t->particleLife = 0.0099999998f; // this+26 (+104) @0x6a6d9c
    if (t->param0Start  < 0.0f) t->param0Start  = 0.0f; // this+33 (+132) @0x6a6db6
    if (t->sizeStart    < 0.0f) t->sizeStart    = 0.0f; // this+34 (+136) @0x6a6dcf
    // start/endColor clamped [0,255] (this+35..42 = +140..+168) @0x6a6de8..@0x6a6f67
    for (int i = 0; i < 4; ++i) {
        if (t->startColor[i] < 0.0f)   t->startColor[i] = 0.0f;
        if (t->startColor[i] > 255.0f) t->startColor[i] = 255.0f;
    }
    for (int i = 0; i < 4; ++i) {
        if (t->endColor[i] < 0.0f)   t->endColor[i] = 0.0f;
        if (t->endColor[i] > 255.0f) t->endColor[i] = 255.0f;
    }
    // colorRate[i] = (endColor[i] - startColor[i]) / particleLife  (this+54..57 = +216..+228) @0x6a6f74
    const float inv = 1.0f / t->particleLife;
    for (int i = 0; i < 4; ++i)
        t->colorRate[i] = (t->endColor[i] - t->startColor[i]) * inv;
}

// ===========================================================================================
//  ZoneFx_Init — Particle_Init 0x6A7020. Allocates the pool on the first frame.
// ===========================================================================================
void ZoneFx_Init(FxParticlePool* pool, FxEmitterTemplate* t) {
    if (!pool || pool->flag) return;      // if(!*this) @0x6a7023
    if (!s_zoneFxReady) return;           // if(dword_800080) @0x6a7033
    if (!t || !t->enabled) return;        // if(*(a2)) @0x6a7039
    ZoneFx_ComputeGradients(t);           // @0x6a703e
    pool->tmpl        = t;                 // this+1 = template @0x6a7043
    pool->elapsedTime = 0.0f;              // this+2 = 0 @0x6a7046
    pool->spawnAccum  = 0.0f;              // this+9 = 0 @0x6a704d
    const int count = Crt_ftol(static_cast<double>(t->particleLife) * t->rate); // ftol(t+104*t+80) @0x6a705a
    pool->particleCount = count;           // this+10 @0x6a7062
    if (count < 1) return;                 // @0x6a7065
    const SIZE_T bytes = static_cast<SIZE_T>(56) * count;                       // 56*count @0x6a706a
    pool->particles = static_cast<Particle*>(HeapAlloc(GetProcessHeap(), 0, bytes)); // @0x6a706d
    if (!pool->particles) return;          // @0x6a707f
    // Zero-inits ONLY the `alive` field (1st DWORD) of each slot — faithful: the binary only
    // writes *(v8+particles)=0 with a stride of 56 (@0x6a7093), not the full 56 bytes. Spawn
    // fills the rest before setting alive=1, so uninitialized fields are never read while alive.
    for (int i = 0; i < count; ++i)
        pool->particles[i].alive = 0;
    pool->flag = 1;                        // *this = 1 @0x6a70a5
}

// ===========================================================================================
//  ZoneFx_Free — Particle_Free 0x6A6FF0.
// ===========================================================================================
void ZoneFx_Free(FxParticlePool* pool) {
    if (!pool) return;
    Particle* arr = pool->particles;      // this+11 @0x6a6ff3
    pool->flag = 0;                        // *this = 0 @0x6a6ff8
    pool->tmpl = nullptr;                  // this+1 = 0 @0x6a6ffe
    if (arr) {
        HeapFree(GetProcessHeap(), 0, arr); // @0x6a7011
        pool->particles = nullptr;         // this+11 = 0 @0x6a7017
    }
}

// ===========================================================================================
//  ZoneFx_UpdateEmit — Particle_UpdateEmit 0x6A7530. Integration + emission (6-shape switch).
//
//  WARNING - FIDELITY OF SHAPES 2 AND 6 (degenerate in the binary): cases 2 and 6 read a
//    PERSISTENT scratch window (wC = var_358/354/350) that is not freshly (re)initialized on
//    each spawn — the binary reuses leftovers from the previous iteration / the force vector
//    (likely original BUG: case 2 normalizes wA then transforms wC, case 6 only writes
//    wC.x via the wB.z<->wC.x overlap). We REPRODUCE this behavior by keeping the scratch
//    windows as PERSISTENT local variables across the whole spawn loop (mirroring the stack).
//    Cases 1/3/4/5 are well-defined. Anchor: disasm 0x6a7a63-0x6a8014.
//
//  WARNING - OFFSET MATRICES: the binary passes the world matrix M at BYTE offsets (M+0, M+8,
//    M+20) depending on the shape (likely stack aliasing). We reproduce this IDENTICALLY via
//    pointer arithmetic on a scratch of 2 contiguous matrices [M, frame*Rz] — exactly the
//    binary's stack layout (var_200 followed by var_1C0=frame*Rz): the offset reads stay valid
//    there AND read the same adjacent bytes. M+8 (D3DXVec3TransformNormal, cases 1/2/3/5/6) and
//    M+20 (D3DXVec3TransformCoord, cases 2/6). Anchor: 0x6a7e33/0x6a7f8a/0x6a7f3f (var_200/var_1F8/var_1EC).
// ===========================================================================================
void ZoneFx_UpdateEmit(FxParticlePool* pool, float dt, const float pos[3], const float rot[3],
                       FxFrustumFn frustum) {
    if (!pool || !pool->flag) return;     // if(*this) @0x6a7539
    if (!s_zoneFxReady) return;           // if(dword_800080) @0x6a754c
    FxEmitterTemplate* t = pool->tmpl;
    if (!t) return;

    pool->elapsedTime += dt;              // this+8 += a2 @0x6a758f
    pool->position[0] = pos[0]; pool->position[1] = pos[1]; pool->position[2] = pos[2]; // @0x6a7594
    pool->rotation[0] = rot[0]; pool->rotation[1] = rot[1]; pool->rotation[2] = rot[2]; // @0x6a75ae

    // Frustum cull of the origin (Cam_FrustumTestPoint6 @0x6a75c2): outside frustum -> nothing.
    if (frustum && !frustum(pool->position)) return;

    // Force vector computed ONCE per update: force = Rand(forceRandMin, forceRandMax) + forceBase
    // (v85/v86/v87 @0x6a75f7/@0x6a7617/@0x6a7639). These slots (s350/s34C/s348) overlap the spawn
    // scratch (wC.z / etc.) — hence kept in the persistent variables below.
    float s350 = Math_RandFloatRange(t->forceRandMin[0], t->forceRandMax[0]) + t->forceBase[0]; // force.x (var_350)
    float s34C = Math_RandFloatRange(t->forceRandMin[1], t->forceRandMax[1]) + t->forceBase[1]; // force.y (var_34C)
    float s348 = Math_RandFloatRange(t->forceRandMin[2], t->forceRandMax[2]) + t->forceBase[2]; // force.z (var_348)

    // ---- Integration of live particles (stride 56) — @0x6a7660-0x6a7814 ----
    int live = 0;
    for (int i = 0; i < pool->particleCount; ++i) {
        Particle& p = pool->particles[i];
        if (!p.alive) continue;                              // if(*(v18+particles)) @0x6a7666
        p.age += dt;                                          // +4 += a2 @0x6a7678
        if (p.age > t->particleLife) {                        // if(age <= particleLife) @0x6a7690
            p.alive = 0;                                      // else *(v20)=0 @0x6a7692
            continue;
        }
        p.pos[0] += dt * p.vel[0];                            // +8 += a2*vel.x @0x6a76a6
        p.pos[1] += dt * p.vel[1];                            // +12 @0x6a76bc
        p.pos[2] += dt * p.vel[2];                            // +16 @0x6a76d1
        if (p.param0 > 0.0f) {                                // if(param0>0) @0x6a76e8
            const float inv = 1.0f / p.param0;                // 1/param0 @0x6a76f0
            p.vel[0] += s350 * inv * dt;                      // vel.x += force.x/param0*dt @0x6a7703
            p.vel[1] += s34C * inv * dt;                      // vel.y @0x6a771c
            p.vel[2] += s348 * inv * dt;                      // vel.z @0x6a7732
        }
        p.param0 += dt * t->param0Rate;                       // +32 += a2*param0Rate @0x6a774d
        if (p.param0 < 0.0f) p.param0 = 0.0f;                 // clamp >=0 @0x6a7765
        p.size += dt * t->sizeRate;                           // +36 += a2*sizeRate @0x6a7782
        if (p.size < 0.0f) p.size = 0.0f;                     // clamp >=0 @0x6a779a
        ++live;                                               // ++v8 @0x6a77b5
        p.colorR += dt * t->colorRate[0];                     // +40 += a2*colorRate.r @0x6a77b8
        p.colorG += dt * t->colorRate[1];                     // +44 @0x6a77d3
        p.colorB += dt * t->colorRate[2];                     // +48 @0x6a77ee
        p.colorA += dt * t->colorRate[3];                     // +52 @0x6a7809
    }

    // ---- Emission — @0x6a781e-0x6a8043 ----
    // Emission-duration gate (@0x6a783c): emits if emissionDuration<=0 (continuous) OR elapsed<=duration.
    if (t->emissionDuration > 0.0f && pool->elapsedTime > t->emissionDuration) {
        if (live == 0)                                        // else if(v84==0) @0x6a7842
            ZoneFx_Free(pool);                                // Particle_Free @0x6a784a
        return;
    }
    pool->spawnAccum += dt * t->rate;                         // this+36 += a2*rate @0x6a7866
    if (pool->spawnAccum < kSpawnThreshold) return;           // if(spawnAccum>=1.0) @0x6a7877

    // Emission world matrix M = frame * Rz * Ry * Rx * T (EXACT order @0x6a793f-0x6a7a55).
    // `frame` = identity (else-branch @0x6a78c3 ; keyframe path template+56 not ported).
    D3DXMATRIX T, Rx, Ry, Rz, frame;
    D3DXMatrixTranslation(&T, pool->position[0], pool->position[1], pool->position[2]); // @0x6a793f
    D3DXMatrixRotationX(&Rx, pool->rotation[0] * kDeg2Rad);   // @0x6a7959
    D3DXMatrixRotationY(&Ry, pool->rotation[1] * kDeg2Rad);   // @0x6a7973
    D3DXMatrixRotationZ(&Rz, pool->rotation[2] * kDeg2Rad);   // @0x6a798d
    D3DXMatrixIdentity(&frame);
    // Scratch of 2 CONTIGUOUS matrices: mtx[0]=M (var_200), mtx[1]=frame*Rz (var_1C0=v95). This
    // layout reproduces the binary's stack for the offset reads M+8/M+20 (see banner).
    D3DXMATRIX mtx[2];
    D3DXMATRIX m98, m97;
    D3DXMatrixMultiply(&mtx[1], &frame, &Rz); // v95 = frame*Rz @0x6a79a7 (= var_1C0, adjacent data)
    D3DXMatrixMultiply(&m98,   &mtx[1], &Ry); // v98 = v95*Ry   @0x6a79d9
    D3DXMatrixMultiply(&m97,   &m98,   &Rx);  // v97 = v98*Rx   @0x6a7a0b
    D3DXMatrixMultiply(&mtx[0], &m97,  &T);   // M   = v97*T    @0x6a7a3a
    const float* mbase = reinterpret_cast<const float*>(&mtx[0]);
    const D3DXMATRIX* Mfull = &mtx[0];                                                    // M+0
    const D3DXMATRIX* Mp8   = reinterpret_cast<const D3DXMATRIX*>(mbase + 2);             // M+8 bytes
    const D3DXMATRIX* Mp20  = reinterpret_cast<const D3DXMATRIX*>(mbase + 5);             // M+20 bytes

    // PERSISTENT scratch windows (mirroring the stack var_XXX ; wB.z === wC.x = s358). Pre-loop
    // init from the binary: var_354=0 (@0x6a7a57), var_350/34C/348 = force vector (above).
    // var_358/364/368/36C/360/35C = uninitialized stack -> 0 (build-safe choice, not invented,
    // cf. shapes 2/6 banner; these slots are only read by the degenerate shapes).
    float s36C = 0.0f, s368 = 0.0f, s364 = 0.0f;   // window A (var_36C/368/364)
    float s360 = 0.0f, s35C = 0.0f, s358 = 0.0f;   // window B (var_360/35C/358) ; s358 = wC.x
    float s354 = 0.0f;                             // wC.y (var_354) — set to 0 @0x6a7a57
    // (s350 already declared = wC.z / force.x)

    for (int slot = 0; slot < pool->particleCount; ++slot) {  // v84 < particleCount @0x6a8014
        Particle& p = pool->particles[slot];
        if (p.alive) continue;                                // if(!*v31) : free slots only @0x6a7a66
        const int shape = Crt_ftol(t->shape);                 // switch(Crt_Dbl2Uint(t+84)) @0x6a7a7f
        switch (shape) {
            case 1: { // sphere surface, speed = speed (@0x6a7a90)
                p.alive = 1; p.age = 0.0f;
                s36C = Math_RandFloatRange(kRandLo, kRandHi);  // @0x6a7aac
                s368 = Math_RandFloatRange(kRandLo, kRandHi);  // @0x6a7ac4
                s364 = Math_RandFloatRange(kRandLo, kRandHi);  // @0x6a7adc
                { D3DXVECTOR3 d(s36C, s368, s364); D3DXVec3Normalize(&d, &d); s36C = d.x; s368 = d.y; s364 = d.z; } // @0x6a7aed
                const float sp = t->speed;                     // @0x6a7af2
                s36C *= sp; s368 *= sp; s364 *= sp;            // LABEL_31 @0x6a7af8
                { D3DXVECTOR3 lp(s36C, s368, s364);            // pos = M*(dir*speed) @0x6a7e45
                  D3DXVec3TransformCoord(reinterpret_cast<D3DXVECTOR3*>(p.pos), &lp, Mfull); }
                s358 = Math_RandFloatRange(t->velMin[0], t->velMax[0]); // wC.x @0x6a7e5a
                s354 = Math_RandFloatRange(t->velMin[1], t->velMax[1]); // wC.y @0x6a7e73
                s350 = Math_RandFloatRange(t->velMin[2], t->velMax[2]); // wC.z @0x6a7e8f
                { D3DXVECTOR3 lv(s358, s354, s350);            // vel = (M+8)*randVel @0x6a7f9c
                  D3DXVec3TransformNormal(reinterpret_cast<D3DXVECTOR3*>(p.vel), &lv, Mp8); }
                p.param0 = t->param0Start; p.size = t->sizeStart;       // @0x6a7fad/@0x6a7fbd
                p.colorR = t->startColor[0]; p.colorG = t->startColor[1];
                p.colorB = t->startColor[2]; p.colorA = t->startColor[3]; // @0x6a7fc7 (4 dwords)
                pool->spawnAccum -= 1.0f;                      // LABEL_40 @0x6a7fe7
                break;
            }
            case 3: { // sphere volume, speed = Rand(0,speed) (@0x6a7bcb)
                p.alive = 1; p.age = 0.0f;
                s36C = Math_RandFloatRange(kRandLo, kRandHi);  // @0x6a7be7
                s368 = Math_RandFloatRange(kRandLo, kRandHi);  // @0x6a7bff
                s364 = Math_RandFloatRange(kRandLo, kRandHi);  // @0x6a7c17
                { D3DXVECTOR3 d(s36C, s368, s364); D3DXVec3Normalize(&d, &d); s36C = d.x; s368 = d.y; s364 = d.z; } // @0x6a7c28
                const float sp = Math_RandFloatRange(0.0f, t->speed); // @0x6a7c3a (Rand(0,speed))
                s36C *= sp; s368 *= sp; s364 *= sp;            // LABEL_31 @0x6a7af8
                { D3DXVECTOR3 lp(s36C, s368, s364);            // pos = M*(dir*rand) @0x6a7e45
                  D3DXVec3TransformCoord(reinterpret_cast<D3DXVECTOR3*>(p.pos), &lp, Mfull); }
                s358 = Math_RandFloatRange(t->velMin[0], t->velMax[0]);
                s354 = Math_RandFloatRange(t->velMin[1], t->velMax[1]);
                s350 = Math_RandFloatRange(t->velMin[2], t->velMax[2]);
                { D3DXVECTOR3 lv(s358, s354, s350);            // vel = (M+8)*randVel @0x6a7f9c
                  D3DXVec3TransformNormal(reinterpret_cast<D3DXVECTOR3*>(p.vel), &lv, Mp8); }
                p.param0 = t->param0Start; p.size = t->sizeStart;
                p.colorR = t->startColor[0]; p.colorG = t->startColor[1];
                p.colorB = t->startColor[2]; p.colorA = t->startColor[3];
                pool->spawnAccum -= 1.0f;
                break;
            }
            case 4: { // sphere + independent speed (transforms via M, well-defined shape) (@0x6a7c44)
                p.alive = 1; p.age = 0.0f;
                s36C = Math_RandFloatRange(kRandLo, kRandHi);  // @0x6a7c60
                s368 = Math_RandFloatRange(kRandLo, kRandHi);  // @0x6a7c78
                s364 = Math_RandFloatRange(kRandLo, kRandHi);  // @0x6a7c90
                { D3DXVECTOR3 d(s36C, s368, s364); D3DXVec3Normalize(&d, &d); s36C = d.x; s368 = d.y; s364 = d.z; } // @0x6a7ca1
                s360 = s36C; s35C = s368; s358 = s364;         // wB = wA (saved dir) @0x6a7cb2/cb9/cbd
                const float v38 = Math_RandFloatRange(0.0f, t->speed); // @0x6a7ccb (Rand(0,speed))
                { D3DXVECTOR3 lp(s36C * v38, s368 * v38, s364 * v38);  // pos = M*(dir*v38) @0x6a7d01
                  D3DXVec3TransformCoord(reinterpret_cast<D3DXVECTOR3*>(p.pos), &lp, Mfull); }
                const float v40 = Math_RandFloatRange(t->velMin[0], t->velMax[0]); // @0x6a7d16
                { D3DXVECTOR3 lv(s360 * v40, s35C * v40, s358 * v40);  // vel = M*(dir*v40) @0x6a7d4c
                  D3DXVec3TransformNormal(reinterpret_cast<D3DXVECTOR3*>(p.vel), &lv, Mfull); }
                p.param0 = t->param0Start; p.size = t->sizeStart;      // @0x6a7d5d/@0x6a7d6d
                p.colorR = t->startColor[0]; p.colorG = t->startColor[1];
                p.colorB = t->startColor[2]; p.colorA = t->startColor[3]; // @0x6a7d77
                pool->spawnAccum -= 1.0f;                      // @0x6a7fe7
                break;
            }
            case 5: { // box, position uniform in [-box/2,box/2] (@0x6a7d9c)
                p.alive = 1; p.age = 0.0f;
                s36C = Math_RandFloatRange(t->box[0] * kBoxHalfNeg, t->box[0] * kBoxHalfPos); // @0x6a7dcd
                s368 = Math_RandFloatRange(t->box[1] * kBoxHalfNeg, t->box[1] * kBoxHalfPos); // @0x6a7dfa
                s364 = Math_RandFloatRange(t->box[2] * kBoxHalfNeg, t->box[2] * kBoxHalfPos); // @0x6a7e27
                { D3DXVECTOR3 lp(s36C, s368, s364);            // pos = M*boxPoint @0x6a7e45
                  D3DXVec3TransformCoord(reinterpret_cast<D3DXVECTOR3*>(p.pos), &lp, Mfull); }
                s358 = Math_RandFloatRange(t->velMin[0], t->velMax[0]);
                s354 = Math_RandFloatRange(t->velMin[1], t->velMax[1]);
                s350 = Math_RandFloatRange(t->velMin[2], t->velMax[2]);
                { D3DXVECTOR3 lv(s358, s354, s350);            // vel = (M+8)*randVel @0x6a7f9c
                  D3DXVec3TransformNormal(reinterpret_cast<D3DXVECTOR3*>(p.vel), &lv, Mp8); }
                p.param0 = t->param0Start; p.size = t->sizeStart;
                p.colorR = t->startColor[0]; p.colorG = t->startColor[1];
                p.colorB = t->startColor[2]; p.colorA = t->startColor[3];
                pool->spawnAccum -= 1.0f;
                break;
            }
            case 2: { // DEGENERATE: normalizes wA (unused) then transforms STALE wC via M+20 (@0x6a7b15)
                p.alive = 1; p.age = 0.0f;
                s36C = Math_RandFloatRange(kRandLo, kRandHi);  // wA (computed but NOT used) @0x6a7b31
                s368 = Math_RandFloatRange(kRandLo, kRandHi);  // @0x6a7b49
                s364 = Math_RandFloatRange(kRandLo, kRandHi);  // @0x6a7b61
                { D3DXVECTOR3 d(s36C, s368, s364); D3DXVec3Normalize(&d, &d); s36C = d.x; s368 = d.y; s364 = d.z; } // @0x6a7b72
                // (the binary saves wC into var_34C/348/344 @0x6a7b83 — dead values, not reproduced)
                const float sp = t->speed;                     // @0x6a7b92
                s358 *= sp; s354 *= sp; s350 *= sp;            // wC (STALE) *= speed @0x6a7baa/@0x6a7bba/@0x6a7bc2
                { D3DXVECTOR3 lp(s358, s354, s350);            // pos = (M+20)*wC @0x6a7f51
                  D3DXVec3TransformCoord(reinterpret_cast<D3DXVECTOR3*>(p.pos), &lp, Mp20); }
                const float v56 = Math_RandFloatRange(t->velMin[0], t->velMax[0]); // LABEL_38 @0x6a7f66
                s358 *= v56; s354 *= v56; s350 *= v56;         // wC *= v56 @0x6a7f71/@0x6a7f7b/@0x6a7f7f
                { D3DXVECTOR3 lv(s358, s354, s350);            // vel = (M+8)*wC @0x6a7f9c
                  D3DXVec3TransformNormal(reinterpret_cast<D3DXVECTOR3*>(p.vel), &lv, Mp8); }
                p.param0 = t->param0Start; p.size = t->sizeStart;
                p.colorR = t->startColor[0]; p.colorG = t->startColor[1];
                p.colorB = t->startColor[2]; p.colorA = t->startColor[3];
                pool->spawnAccum -= 1.0f;
                break;
            }
            case 6: { // DEGENERATE: box -> normalizes into wB (wB.z===wC.x) then transforms wC via M+20 (@0x6a7e99)
                p.alive = 1; p.age = 0.0f;
                s36C = Math_RandFloatRange(t->box[0] * kBoxHalfNeg, t->box[0] * kBoxHalfPos); // @0x6a7eca
                s368 = Math_RandFloatRange(t->box[1] * kBoxHalfNeg, t->box[1] * kBoxHalfPos); // @0x6a7ef7
                s364 = Math_RandFloatRange(t->box[2] * kBoxHalfNeg, t->box[2] * kBoxHalfPos); // @0x6a7f24
                { D3DXVECTOR3 d(s36C, s368, s364); D3DXVECTOR3 n; D3DXVec3Normalize(&n, &d);  // normalize wA -> wB @0x6a7f37
                  s360 = n.x; s35C = n.y; s358 = n.z; }        // wB = (s360,s35C,s358) ; s358 = wC.x (fresh), wC.y/z stale
                { D3DXVECTOR3 lp(s358, s354, s350);            // pos = (M+20)*wC @0x6a7f51
                  D3DXVec3TransformCoord(reinterpret_cast<D3DXVECTOR3*>(p.pos), &lp, Mp20); }
                const float v56 = Math_RandFloatRange(t->velMin[0], t->velMax[0]); // LABEL_38 @0x6a7f66
                s358 *= v56; s354 *= v56; s350 *= v56;         // wC *= v56 @0x6a7f71/@0x6a7f7b/@0x6a7f7f
                { D3DXVECTOR3 lv(s358, s354, s350);            // vel = (M+8)*wC @0x6a7f9c
                  D3DXVec3TransformNormal(reinterpret_cast<D3DXVECTOR3*>(p.vel), &lv, Mp8); }
                p.param0 = t->param0Start; p.size = t->sizeStart;
                p.colorR = t->startColor[0]; p.colorG = t->startColor[1];
                p.colorB = t->startColor[2]; p.colorA = t->startColor[3];
                pool->spawnAccum -= 1.0f;
                break;
            }
            default:                                           // invalid shape (def_6A7A89) : no spawn
                break;
        }
        if (pool->spawnAccum < kSpawnThreshold) break;         // if(spawnAccum<1.0) break @0x6a8001
    }

    // Drains the excess accumulator (no free slots left but accum>1) @0x6a8028-0x6a8043.
    if (pool->spawnAccum > kSpawnThreshold) {
        float acc = pool->spawnAccum;
        do { acc -= 1.0f; } while (acc > kSpawnThreshold);     // @0x6a8030
        pool->spawnAccum = acc;                                // @0x6a8043
    }
}

// ===========================================================================================
//  ZoneFx_RenderBillboards — Particle_RenderBillboards 0x6A70B0.
//  6 vertices/live particle (2 triangles) in the CPU scratch, camera-facing quad via the
//  right/up basis x size, GRAY COLOR = (u8)size replicated across ARGB, DrawPrimitiveUP(TRIANGLELIST, 24).
//
//  WARNING - The billboard color does NOT come from the colorR..A fields (particle+40..52,
//    integrated but NOT read by the render): the binary reads ONLY particle.size (particle+36
//    = v6 @0x6a710f) and turns it into an ARGB gray (v12 = (u8)ftol(size)*0x01010101,
//    @0x6a711d-0x6a7176) which ALSO serves as the corner multiplier. This is faithful:
//    intensity and size "blend" together via size.
//
//  WARNING - UVs are NOT written by 0x6A70B0 (pre-initialized once in the shared VB). Since our
//    scratch is rebuilt every frame, we set standard UVs in corner order.
// ===========================================================================================
int ZoneFx_RenderBillboards(FxParticlePool* pool, const ZoneFxFrameParams& params) {
    if (!pool || !pool->flag) return 0;                        // if(*this) @0x6a70b7
    if (!s_zoneFxReady) return 0;                              // if(dword_800080) @0x6a70c9
    if (!params.device || !params.scratch) return 0;
    // Frustum cull of the origin (Cam_FrustumTestPoint6(g_GfxRenderer, this+12) @0x6a70d8).
    if (params.frustum && !params.frustum(pool->position)) return 0;

    std::vector<Billboard_Vertex>& vb = *params.scratch;
    vb.clear();
    const float rx = params.right[0], ry = params.right[1], rz = params.right[2]; // R = flt_8001D4/D8/DC
    const float ux = params.up[0],    uy = params.up[1],    uz = params.up[2];    // U = flt_8001E0/E4/E8

    int quads = 0;                                             // v15
    for (int i = 0; i < pool->particleCount; ++i) {           // v22 < particleCount @0x6a7437
        const Particle& p = pool->particles[i];
        if (!p.alive) continue;                                // if(*(v5+v4)) @0x6a7103
        const float s = p.size;                                // v6 = particle+36 @0x6a710f
        // GRAY color: c = (u8)ftol(size) ; ARGB = c*0x01010101 (@0x6a711d-0x6a7176).
        const DWORD c = static_cast<DWORD>(Crt_ftol(s) & 0xFF);
        const D3DCOLOR col = c | (c << 8) | (c << 16) | (c << 24);
        // Camera-facing corners (basis x size), + particle position (particle+8/+12/+16).
        // TL=(U-R)*s, TR=(U+R)*s, BL=(-R-U)*s, BR=(R-U)*s — @0x6a7188-0x6a7405.
        const float px = p.pos[0], py = p.pos[1], pz = p.pos[2];
        Billboard_Vertex q[6];
        auto set = [&](Billboard_Vertex& d, float cx, float cy, float cz, float u, float v) {
            d.x = cx; d.y = cy; d.z = cz; d.color = col; d.u = u; d.v = v;
        };
        // TL
        const float tlx = (ux - rx) * s + px, tly = (uy - ry) * s + py, tlz = (uz - rz) * s + pz;
        // TR
        const float trx = (ux + rx) * s + px, try_ = (uy + ry) * s + py, trz = (uz + rz) * s + pz;
        // BL = -(R+U)*s
        const float blx = -(rx + ux) * s + px, bly = -(ry + uy) * s + py, blz = -(rz + uz) * s + pz;
        // BR = (R-U)*s
        const float brx = (rx - ux) * s + px, bry = (ry - uy) * s + py, brz = (rz - uz) * s + pz;
        set(q[0], tlx, tly, tlz, 0.0f, 0.0f);                  // triangle 1 : TL,TR,BL
        set(q[1], trx, try_, trz, 1.0f, 0.0f);
        set(q[2], blx, bly, blz, 0.0f, 1.0f);
        set(q[3], blx, bly, blz, 0.0f, 1.0f);                  // triangle 2 : BL,TR,BR
        set(q[4], trx, try_, trz, 1.0f, 0.0f);
        set(q[5], brx, bry, brz, 1.0f, 1.0f);
        for (int k = 0; k < 6; ++k) vb.push_back(q[k]);
        ++quads;                                               // ++v15 @0x6a741d
        if (quads == params.maxQuads) break;                   // if(v15==dword_7FFEE0) break @0x6a7424 (0 => never)
    }

    if (quads > 0) {                                           // if(v15) @0x6a7445
        params.device->SetTexture(0, pool->tmpl ? pool->tmpl->texture : nullptr); // *(tmpl+52) @0x6a745e
        params.device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, static_cast<UINT>(2 * quads),
                                       vb.data(), sizeof(Billboard_Vertex));       // (4, 2*v15, VB, 24) @0x6a7475
    }
    return quads;
}

} // namespace ts2::gfx

// Gfx/ParticleSystem.cpp — FAITHFUL implementation of the GXD particle pool.
//
// IDA ground truth: TwelveSky2.exe. Each block cites its anchor (name + 0xADDR).
// See ParticleSystem.h for the structure map and Docs/TS2_FX_ROSETTA.md §2.
#include "ParticleSystem.h"
#include <cstring>   // std::memcpy / std::memset
#include <cmath>     // sqrtf

namespace ts2::gfx {

// π/180 — binary literal constant (0.01745329238474369) used by
// PtclDef_UpdateAndSpawn 0x423688/0x4236AA/0x4236CC (degrees → radians).
static constexpr float kDeg2Rad = 0.01745329238474369f;

// ±100 — random direction bounds (flt_7EDB54 = 100.0 / flt_7EDB50 = -100.0),
// loaded at the top of the spawn loop @0x42376E/0x423776.
static constexpr float kRandHi = 100.0f;
static constexpr float kRandLo = -100.0f;

// ---------------------------------------------------------------------------
// Shared VB — g_GxdRenderer_pSpritePool 0x18C5110. Acts as a global gate:
// AllocPool/Update/Render do nothing while it is null (same tests as the
// binary @0x423291 / 0x42332F / 0x42445A).
static void* s_sharedVB = nullptr;

void  SetParticleSharedVB(void* cpuBuffer) { s_sharedVB = cpuBuffer; }
void* ParticleSharedVB()                   { return s_sharedVB; }

// ---------------------------------------------------------------------------
// RNG — Rng_Next 0x7603FD (MSVC LCG). The binary reads the CRT's per-thread
// rand state (Crt_GetPtd[5]); we reproduce the ALGORITHM with a module-level state.
static unsigned int s_rngState = 1;

void SetRandSeed(unsigned int seed) { s_rngState = seed; }

unsigned int Rng_Next() {
    // state = 214013·state + 2531011; returns (state>>16)&0x7FFF   (0x76040B..0x76041E)
    s_rngState = 214013u * s_rngState + 2531011u;
    return (s_rngState >> 16) & 0x7FFFu;
}

// Math_RandFloatRange 0x403330: a + (b-a)·frac; inverted if b <= a.
float Math_RandFloatRange(float a, float b) {
    int v3 = static_cast<int>(Rng_Next());                         // 0x403342
    double frac = static_cast<double>(v3 % 10001) / 10000.0;       // 0x40337F..
    if (b <= static_cast<double>(a))                               // 0x403340 (a3 <= a2)
        return static_cast<float>(b + (a - b) * frac);             // 0x4033A4
    return static_cast<float>(a + (b - a) * frac);                 // 0x40336D
}

// ---------------------------------------------------------------------------
// PtclDef_GetTexture — IDirect3DTexture9* stored at def+0x38 (= texture[0x34]),
// read by RenderQuads @0x4248F1 for SetTexture(0, tex).
IDirect3DTexture9* PtclDef_GetTexture(const PtclDef* def) {
    if (!def) return nullptr;
    IDirect3DTexture9* tex = nullptr;
    // def+0x38: the cTexture (56 bytes @0x04) stores its GPU pointer at its offset +0x34.
    std::memcpy(&tex, def->texture + 0x34, sizeof(tex));
    return tex;
}

// ---------------------------------------------------------------------------
// PtclDef_Init 0x4221E0 / PtclDef_Reset 0x4222D0 — zero-init. The only non-null
// field is the texture handle (dword idx 11 = def+0x2C) set to -1.
static void PtclDef_ZeroInit(PtclDef* def) {
    std::memset(def, 0, sizeof(PtclDef));
    const int32_t minusOne = -1;                       // result[11] = -1 @0x4221F9 / 0x4222DD
    std::memcpy(def->texture + 0x28, &minusOne, 4);    // def+0x2C = texture-local +0x28
}
void PtclDef_Init(PtclDef* def)  { PtclDef_ZeroInit(def); }
void PtclDef_Reset(PtclDef* def) { PtclDef_ZeroInit(def); }

// ---------------------------------------------------------------------------
// PtclDef_ClampParams 0x422F60 — bounds the scalars then DERIVES colorRate.
void PtclDef_ClampParams(PtclDef* def) {
    if (!def->enabled) return;                                     // 0x422F60

    if (def->texAnimFPS  < 0.0f) def->texAnimFPS  = 0.0f;          // this+20 @0x422F73
    if (def->spawnRate   < 0.0f) def->spawnRate   = 0.0f;          // this+21 @0x422F80
    if (def->baseSpeed   < 0.0f) def->baseSpeed   = 0.0f;          // this+23 @0x422F8D
    if (def->boxSize[0]  < 0.0f) def->boxSize[0]  = 0.0f;          // this+24 @0x422F9A
    if (def->boxSize[1]  < 0.0f) def->boxSize[1]  = 0.0f;          // this+25 @0x422FA7
    if (def->boxSize[2]  < 0.0f) def->boxSize[2]  = 0.0f;          // this+26 @0x422FB4
    if (def->lifetime    < 0.0099999998f) def->lifetime = 0.0099999998f; // this+27 @0x422FC7
    if (def->param0Start < 0.0f) def->param0Start = 0.0f;          // this+34 @0x422FDB
    if (def->sizeStart   < 0.0f) def->sizeStart   = 0.0f;          // this+35 @0x422FEE

    // startColor[4] / endColor[4]: clamp 0..255 (this+36..43 @0x423001..0x42315C)
    for (int i = 0; i < 4; ++i) {
        if (def->startColor[i] < 0.0f)   def->startColor[i] = 0.0f;
        if (def->startColor[i] > 255.0f) def->startColor[i] = 255.0f;
    }
    for (int i = 0; i < 4; ++i) {
        if (def->endColor[i] < 0.0f)   def->endColor[i] = 0.0f;
        if (def->endColor[i] > 255.0f) def->endColor[i] = 255.0f;
    }

    // colorRate = (endColor - startColor) / lifetime  (this+55..58 @0x423175..0x4231B4)
    def->colorRate[0] = (def->endColor[0] - def->startColor[0]) / def->lifetime;
    def->colorRate[1] = (def->endColor[1] - def->startColor[1]) / def->lifetime;
    def->colorRate[2] = (def->endColor[2] - def->startColor[2]) / def->lifetime;
    def->colorRate[3] = (def->endColor[3] - def->startColor[3]) / def->lifetime;
}

// ---------------------------------------------------------------------------
// PtclDef_AllocPool 0x423280 — sizes the pool (count = ftol(lifetime·spawnRate)).
void PtclDef_AllocPool(PtclDef* def, PtclPool* pool) {
    if (pool->initialized) return;              // 0x423280 : if (!pool+12)
    if (!s_sharedVB) return;                    // 0x423291 : if (g_GxdRenderer_pSpritePool)
    if (!def->enabled) return;                  // 0x423293 : if (*def)

    PtclDef_ClampParams(def);                   // 0x423298
    pool->elapsedTime = 0.0f;                   // 0x42329F : pool+20 = 0
    pool->def         = def;                    // 0x4232A2 : pool+16 = def
    pool->spawnAccum  = 0.0f;                   // 0x4232A5 : pool+48 = 0

    int count = Crt_ftol(static_cast<double>(def->lifetime) * def->spawnRate); // 0x4232AE
    pool->particleCount = count;                // 0x4232B6 : pool+52 = count
    if (count < 1) return;                      // 0x4232B9

    // 0x4232CA : v8 = 56·count ; RtlAllocateHeap(GetProcessHeap(), 0, v8)
    Particle* buf = static_cast<Particle*>(
        HeapAlloc(GetProcessHeap(), 0, static_cast<SIZE_T>(56) * count));
    pool->particles = buf;                      // 0x4232DA
    if (!buf) return;                           // 0x4232DF

    for (int i = 0; i < count; ++i)             // 0x4232E6..0x423301 : each particle.alive = 0
        buf[i].alive = 0;

    pool->initialized = 1;                      // 0x423303 : pool+12 = 1
}

// ---------------------------------------------------------------------------
// PtclDef_FreePool 0x423240 — releases the array and resets scale = 1.
void PtclDef_FreePool(PtclPool* pool) {
    Particle* p = pool->particles;              // 0x423240
    pool->initialized = 0;                      // 0x423243 : pool+12 = 0
    pool->def         = nullptr;                // 0x42324A : pool+16 = 0
    if (p) {                                    // 0x423253
        HeapFree(GetProcessHeap(), 0, p);       // 0x42325F
        pool->particles = nullptr;              // 0x423265
    }
    pool->scale[0] = 1.0f;                       // 0x42326E
    pool->scale[1] = 1.0f;                       // 0x423270
    pool->scale[2] = 1.0f;                       // 0x423273
}

// ---------------------------------------------------------------------------
// Spawn of ONE particle (body of the shape switch in PtclDef_UpdateAndSpawn).
// `W` = pool world matrix (rot XYZ · trans). Reproduces the semantics of the 10
// cases @0x4237AD; the cases where the decompile aliases stack slots (1/6/7/10)
// are reconstructed from the confirmed "scalar·normalized direction" pattern
// shared with cases 2/3/4/5/8/9 (see §3-B; IDA index, NEVER VeryOld).
static void Fx_SpawnOne(Particle& p, const PtclDef* def, const D3DXMATRIX& W) {
    p.alive = 1;                                                   // *v47 = 1
    p.age   = 0.0f;                                                // particle+4 = 0

    const int shape = Crt_ftol(def->shapeType);                   // ftol(def+0x58) @0x42379E

    D3DXVECTOR3 lp(0, 0, 0);   // local position (transformed as COORD)
    D3DXVECTOR3 lv(0, 0, 0);   // local velocity (transformed as NORMAL)

    // per-axis rand of the velocity bounds (LABEL_31/32/37 pattern: velMin/velMax XYZ)
    auto velPerAxis = [&]() {
        return D3DXVECTOR3(Math_RandFloatRange(def->velMin[0], def->velMax[0]),
                           Math_RandFloatRange(def->velMin[1], def->velMax[1]),
                           Math_RandFloatRange(def->velMin[2], def->velMax[2]));
    };

    switch (shape) {
        case 1: {   // point — loc_4237B4
            // dir3 = normalize(rand[-100,100]³); lp = baseSpeed·dir3; vel per-axis.
            // NOTE: the decompile aliases the dir slot @0x42382D (reads baseSpeed·stale
            // slot); reconstructed from sibling cases 2/3 (scalar·dir).
            D3DXVECTOR3 d(Math_RandFloatRange(kRandLo, kRandHi),
                          Math_RandFloatRange(kRandLo, kRandHi),
                          Math_RandFloatRange(kRandLo, kRandHi));
            D3DXVec3Normalize(&d, &d);                             // 0x423822
            lp = d * def->baseSpeed;                               // 0x42382A (def+0x5C)
            lv = velPerAxis();                                     // 0x42386E..0x4238BC
            break;
        }
        case 2: {   // line — loc_423939
            // dir3 = normalize(rand³); lp = baseSpeed·dir3; lv = rand(velMin0,velMax0)·dir3.
            D3DXVECTOR3 d(Math_RandFloatRange(kRandLo, kRandHi),
                          Math_RandFloatRange(kRandLo, kRandHi),
                          Math_RandFloatRange(kRandLo, kRandHi));
            D3DXVec3Normalize(&d, &d);                             // 0x4239A7
            lp = d * def->baseSpeed;                               // 0x4239C7 (def+0x5C)
            lv = d * Math_RandFloatRange(def->velMin[0], def->velMax[0]); // 0x423A1B
            break;
        }
        case 3: {   // sphere — loc_423A8A
            // lp = rand(0,baseSpeed)·dir3; vel per-axis.
            D3DXVECTOR3 d(Math_RandFloatRange(kRandLo, kRandHi),
                          Math_RandFloatRange(kRandLo, kRandHi),
                          Math_RandFloatRange(kRandLo, kRandHi));
            D3DXVec3Normalize(&d, &d);                             // 0x423AF8
            lp = d * Math_RandFloatRange(0.0f, def->baseSpeed);    // 0x423B14
            lv = velPerAxis();                                     // 0x423B65..
            break;
        }
        case 4: {   // hemisphere — loc_423BF2
            // lp = rand(0,baseSpeed)·dir3; lv = rand(velMin0,velMax0)·dir3.
            D3DXVECTOR3 d(Math_RandFloatRange(kRandLo, kRandHi),
                          Math_RandFloatRange(kRandLo, kRandHi),
                          Math_RandFloatRange(kRandLo, kRandHi));
            D3DXVec3Normalize(&d, &d);                             // 0x423C60
            lp = d * Math_RandFloatRange(0.0f, def->baseSpeed);    // 0x423C94
            lv = d * Math_RandFloatRange(def->velMin[0], def->velMax[0]); // 0x423CEA
            break;
        }
        case 5: {   // box (volume) — loc_423D54
            // lp = rand(±0.5·boxSize) per-axis; vel per-axis.
            lp = D3DXVECTOR3(Math_RandFloatRange(-0.5f * def->boxSize[0], 0.5f * def->boxSize[0]),  // 0x423D70..
                             Math_RandFloatRange(-0.5f * def->boxSize[1], 0.5f * def->boxSize[1]),  // 0x423DA2..
                             Math_RandFloatRange(-0.5f * def->boxSize[2], 0.5f * def->boxSize[2])); // 0x423DDA..
            lv = velPerAxis();                                     // LABEL_37
            break;
        }
        case 6: {   // ring — loc_423E04
            // dir3 = normalize(rand ±0.5·boxSize); lp = dir3 (unit);
            // lv = rand(velMin0,velMax0)·dir3.  (velocity slots aliased → sibling pattern.)
            D3DXVECTOR3 d(Math_RandFloatRange(-0.5f * def->boxSize[0], 0.5f * def->boxSize[0]),
                          Math_RandFloatRange(-0.5f * def->boxSize[1], 0.5f * def->boxSize[1]),
                          Math_RandFloatRange(-0.5f * def->boxSize[2], 0.5f * def->boxSize[2]));
            D3DXVec3Normalize(&d, &d);                             // 0x423EBD
            lp = d;                                                // 0x423ED7 : TransformCoord(dir)
            lv = d * Math_RandFloatRange(def->velMin[0], def->velMax[0]); // 0x423EF4
            break;
        }
        // Cases 7..10: 2D PLANAR emission. dir2 = normalize2D(rand[-100,100]²) mapped
        // onto the X-Z plane; the Y component = rand(±0.5·boxSize1) (box height).
        // Pattern confirmed by case 9 (the clearest); 7/10 alias the dir2 slot.
        case 7: {   // planar — loc_423F19
            D3DXVECTOR2 d2(Math_RandFloatRange(kRandLo, kRandHi),
                           Math_RandFloatRange(kRandLo, kRandHi));
            D3DXVec2Normalize(&d2, &d2);                           // 0x423F68
            lp = D3DXVECTOR3(def->baseSpeed * d2.x,                // 0x423F76 (def+0x5C)
                             Math_RandFloatRange(-0.5f * def->boxSize[1], 0.5f * def->boxSize[1]),
                             def->baseSpeed * d2.y);
            lv = velPerAxis();                                     // LABEL_31
            break;
        }
        case 8: {   // planar — loc_423FD3
            D3DXVECTOR2 d2(Math_RandFloatRange(kRandLo, kRandHi),
                           Math_RandFloatRange(kRandLo, kRandHi));
            D3DXVec2Normalize(&d2, &d2);                           // 0x424022
            float s = Math_RandFloatRange(0.0f, def->baseSpeed);   // 0x42403E
            lp = D3DXVECTOR3(s * d2.x,
                             Math_RandFloatRange(-0.5f * def->boxSize[1], 0.5f * def->boxSize[1]),
                             s * d2.y);
            lv = velPerAxis();                                     // LABEL_37
            break;
        }
        case 9: {   // planar — loc_4240A1
            D3DXVECTOR2 d2(Math_RandFloatRange(kRandLo, kRandHi),
                           Math_RandFloatRange(kRandLo, kRandHi));
            D3DXVec2Normalize(&d2, &d2);                           // 0x4240F0
            lp = D3DXVECTOR3(def->baseSpeed * d2.x,                // 0x42410B
                             Math_RandFloatRange(-0.5f * def->boxSize[1], 0.5f * def->boxSize[1]),
                             def->baseSpeed * d2.y);
            float vs = Math_RandFloatRange(def->velMin[0], def->velMax[0]); // 0x424197
            lv = D3DXVECTOR3(vs * d2.x,
                             Math_RandFloatRange(def->velMin[1], def->velMax[1]), // 0x4241D4
                             vs * d2.y);
            break;
        }
        case 10: {  // planar — loc_424224
            D3DXVECTOR2 d2(Math_RandFloatRange(kRandLo, kRandHi),
                           Math_RandFloatRange(kRandLo, kRandHi));
            D3DXVec2Normalize(&d2, &d2);                           // 0x424273
            float s = Math_RandFloatRange(0.0f, def->baseSpeed);   // 0x42429F
            lp = D3DXVECTOR3(s * d2.x,
                             Math_RandFloatRange(-0.5f * def->boxSize[1], 0.5f * def->boxSize[1]),
                             s * d2.y);
            float vs = Math_RandFloatRange(def->velMin[0], def->velMax[0]); // 0x424333
            lv = D3DXVECTOR3(vs * d2.x,
                             Math_RandFloatRange(def->velMin[1], def->velMax[1]), // 0x424370
                             vs * d2.y);
            break;
        }
        default:
            return;                                                // def_4237AD : unknown shape
    }

    // pos = TransformCoord(lp, W); vel = TransformNormal(lv, W)  (LABEL_31/32/34/37)
    D3DXVECTOR3 wp, wv;
    D3DXVec3TransformCoord(&wp, &lp, &W);
    D3DXVec3TransformNormal(&wv, &lv, &W);
    p.pos[0] = wp.x; p.pos[1] = wp.y; p.pos[2] = wp.z;
    p.vel[0] = wv.x; p.vel[1] = wv.y; p.vel[2] = wv.z;

    p.param0 = def->param0Start;                                   // particle+0x20 = def+0x88
    p.size   = def->sizeStart;                                     // particle+0x24 = def+0x8C
    // color = startColor (LABEL_34 @0x423906: copy of def+0x90..0x9C)
    p.colorR = def->startColor[0];
    p.colorG = def->startColor[1];
    p.colorB = def->startColor[2];
    p.colorA = def->startColor[3];
}

// ---------------------------------------------------------------------------
// PtclDef_UpdateAndSpawn 0x423310 — one simulation + emission frame.
void PtclDef_UpdateAndSpawn(PtclPool* pool, const float position[3],
                            const float rotation[3], float dt) {
    if (!pool->initialized || !s_sharedVB) return;                // 0x423327

    pool->elapsedTime += dt;                                      // 0x42333E : pool+20 += dt
    pool->position[0] = position[0];                             // 0x423343 : pool+24 = a2 (position)
    pool->position[1] = position[1];                             // 0x423349
    pool->position[2] = position[2];                             // 0x42334F
    pool->rotation[0] = rotation[0];                             // 0x423354 : pool+36 = a1 (rotation)
    pool->rotation[1] = rotation[1];                             // 0x42335A
    pool->rotation[2] = rotation[2];                             // 0x42335D

    PtclDef* def = pool->def;                                    // 0x423360 : pool+16

    // force = forceBase + rand(forceRandMin, forceRandMax), 1×/frame  (0x423379..0x4233DC)
    D3DXVECTOR3 force(
        Math_RandFloatRange(def->forceRandMin[0], def->forceRandMax[0]) + def->forceBase[0],
        Math_RandFloatRange(def->forceRandMin[1], def->forceRandMax[1]) + def->forceBase[1],
        Math_RandFloatRange(def->forceRandMin[2], def->forceRandMax[2]) + def->forceBase[2]);

    // --- Integration of live particles (loop 0x4233F2..0x423565) ---
    int aliveCount = 0;                                          // v4
    const int cap = pool->particleCount;                        // pool+52
    for (int i = 0; i < cap; ++i) {
        Particle& p = pool->particles[i];
        if (!p.alive) continue;                                  // 0x423400 : if (*v23)

        p.age += dt;                                             // 0x423412 : particle+4 += dt
        if (def->lifetime < p.age) {                            // 0x42342C : lifetime >= age?
            p.alive = 0;                                        // 0x42342E : otherwise dead
            continue;
        }

        p.pos[0] += p.vel[0] * dt;                              // 0x423441 : pos += vel·dt
        p.pos[1] += p.vel[1] * dt;                              // 0x423453
        p.pos[2] += p.vel[2] * dt;                              // 0x423464

        if (0.0f < p.param0) {                                  // 0x423476 : 1/param0 guard
            float inv = 1.0f / p.param0;                        // 0x42347F
            p.vel[0] += inv * force.x * dt;                     // 0x423490 : vel += (force/param0)·dt
            p.vel[1] += inv * force.y * dt;                     // 0x4234A2
            p.vel[2] += inv * force.z * dt;                     // 0x4234B1
        }

        p.param0 += def->param0Rate * dt;                      // 0x4234C7 : += def+0xD4
        if (0.0f > p.param0) p.param0 = 0.0f;                  // 0x4234D7 : clamp >= 0
        p.size += def->sizeRate * dt;                          // 0x4234EF : += def+0xD8
        if (0.0f > p.size) p.size = 0.0f;                      // 0x4234FF : clamp >= 0

        p.colorR += def->colorRate[0] * dt;                    // 0x423518 : += def+0xDC
        p.colorG += def->colorRate[1] * dt;                    // 0x42352E
        p.colorB += def->colorRate[2] * dt;                    // 0x423544
        p.colorA += def->colorRate[3] * dt;                    // 0x42355A
        ++aliveCount;                                          // 0x423513
    }

    // --- Emission gate (0x423597): v34 = constant 0.0, def+0x4C = field19,
    //     pool+0x14 = elapsedTime. Emits while field19 <= 0 OR field19 >= elapsedTime. ---
    if (0.0f >= def->field19 || def->field19 >= pool->elapsedTime) {
        pool->spawnAccum += def->spawnRate * dt;               // 0x4235C2 : pool+48 += spawnRate·dt
        if (pool->spawnAccum >= 1.0f) {                        // 0x4235DA
            // Pool world matrix: rot XYZ (degrees) · position translation.
            // (Construction 0x423672..0x423749: D3DXMatrixTranslation + RotationX/Y/Z
            //  + 4× D3DXMatrixMultiply. The exact product order and a possible
            //  "motion" factor @def+0x3C are not provable from the decompile —
            //  motion factor = identity here (case without MOTION). TODO anchored 0x4235E0.)
            D3DXMATRIX W, Rx, Ry, Rz, T;
            D3DXMatrixTranslation(&T, pool->position[0], pool->position[1], pool->position[2]); // 0x423672
            D3DXMatrixRotationX(&Rx, pool->rotation[0] * kDeg2Rad);   // 0x423694
            D3DXMatrixRotationY(&Ry, pool->rotation[1] * kDeg2Rad);   // 0x4236B6
            D3DXMatrixRotationZ(&Rz, pool->rotation[2] * kDeg2Rad);   // 0x4236D8
            D3DXMatrixMultiply(&W, &Rx, &Ry);                        // 0x4236F2
            D3DXMatrixMultiply(&W, &W, &Rz);                         // 0x42370F
            D3DXMatrixMultiply(&W, &W, &T);                          // 0x42372C / 0x423749

            // Spawn loop: fills dead slots until spawnAccum is drained
            // or capacity is exhausted (0x42378B..0x4243B2).
            for (int i = 0; i < cap; ++i) {
                Particle& p = pool->particles[i];
                if (p.alive) continue;                              // 0x42378B : if (!*v47)
                Fx_SpawnOne(p, def, W);
                pool->spawnAccum -= 1.0f;                           // 0x423931 : accum -= 1
                if (pool->spawnAccum < 1.0f) break;                // 0x42438F
            }
            // Drains the unemitted remainder (capacity full) — 0x4243D0..0x4243EA.
            if (pool->spawnAccum > 1.0f) {
                do { pool->spawnAccum -= 1.0f; } while (1.0f < pool->spawnAccum);
            }
        }
    } else if (aliveCount == 0) {                               // 0x42359F : window ended + pool empty
        PtclDef_FreePool(pool);                                // 0x4235A7 : auto-release
    }
}

// ---------------------------------------------------------------------------
// ARGB color packing from 0..255 floats (0x424566..0x424605):
//   D3DCOLOR = A<<24 | R<<16 | G<<8 | B  (float→int truncation then byte).
static inline D3DCOLOR Fx_PackColor(const Particle& p) {
    uint32_t a = static_cast<uint8_t>(static_cast<int>(p.colorA));
    uint32_t r = static_cast<uint8_t>(static_cast<int>(p.colorR));
    uint32_t g = static_cast<uint8_t>(static_cast<int>(p.colorG));
    uint32_t b = static_cast<uint8_t>(static_cast<int>(p.colorB));
    return (a << 24) | (r << 16) | (g << 8) | b;
}

// ---------------------------------------------------------------------------
// InitSharedQuadUVs — pre-fills the uv (+0x10) of the shared VB's vertices.
// RenderQuads writes ONLY pos+color; the order of the 6 vertices (2 triangles) is
// TL, TR, BL / BL, TR, BR (see writes 0x4246CB..0x424895).
void InitSharedQuadUVs(void* cpuBuffer, int maxQuads) {
    if (!cpuBuffer) return;
    Billboard_Vertex* v = static_cast<Billboard_Vertex*>(cpuBuffer);
    const float uv[6][2] = {
        {0, 0}, {1, 0}, {0, 1},   // triangle 1: TL, TR, BL
        {0, 1}, {1, 0}, {1, 1},   // triangle 2: BL, TR, BR
    };
    for (int q = 0; q < maxQuads; ++q) {
        for (int k = 0; k < 6; ++k) {
            v[q * 6 + k].u = uv[k][0];
            v[q * 6 + k].v = uv[k][1];
        }
    }
}

// ---------------------------------------------------------------------------
// PtclDef_RenderQuads 0x424430 — builds the billboard quads in the shared VB
// and issues the DrawPrimitiveUP.
void PtclDef_RenderQuads(PtclPool* pool, const ParticleFrameParams& params) {
    // Gates 0x42445A: pool initialized + shared VB + origin inside the frustum.
    if (!pool->initialized) return;
    void* vbMem = params.sharedVB ? params.sharedVB : s_sharedVB;
    if (!vbMem || !params.device) return;
    if (params.frustumContains && !params.frustumContains(pool->position)) return; // Frustum_ContainsPoint6 0x406430

    // Distance-based LOD fade (0x424481..0x4244E6):
    //   dist = |pos - eye|; fade = clamp01(1 - (dist - near)/(far - near)).
    float dx = pool->position[0] - params.eye[0];               // dword_18C51C0
    float dy = pool->position[1] - params.eye[1];               // dword_18C51C4
    float dz = pool->position[2] - params.eye[2];               // dword_18C51C8
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);            // Math_CIsqrt 0x75C680
    float denom = params.fadeFar - params.fadeNear;            // flt_18C4F0C - flt_18C4F08
    float fade = (denom != 0.0f) ? 1.0f - (dist - params.fadeNear) / denom : 1.0f;
    if (fade < 0.0f) fade = 0.0f;                              // 0x4244CD
    else if (fade > 1.0f) fade = 1.0f;                         // 0x4244E4

    const int cap = pool->particleCount;                       // pool+52

    // Counts the live particles (0x4244FB..0x42450E).
    int aliveCount = 0;
    for (int i = 0; i < cap; ++i)
        if (pool->particles[i].alive) ++aliveCount;

    // Number of quads actually drawn = ftol(alive · fade)  (0x42451A).
    int drawCap = Crt_ftol(static_cast<double>(aliveCount) * fade);

    Billboard_Vertex* vb = static_cast<Billboard_Vertex*>(vbMem);
    int quads = 0;                                             // v26

    for (int i = 0; i < cap; ++i) {                           // 0x42453C..0x4248D0
        Particle& p = pool->particles[i];
        if (!p.alive) continue;
        if (quads > drawCap - 1) continue;                    // 0x42454D : LOD ceiling

        float size = p.size;                                  // v29 : particle+0x24
        D3DCOLOR col = Fx_PackColor(p);                       // v16

        // Billboard corners = camRight/camUp × half-size (0x424617..0x4246BE).
        float rX = params.camRight[0] * size, rY = params.camRight[1] * size, rZ = params.camRight[2] * size;
        float uX = params.camUp[0]    * size, uY = params.camUp[1]    * size, uZ = params.camUp[2]    * size;
        // base = particle.pos · pool.scale  (0x4246CB : *(p+8)·scale.x, etc.)
        float bX = p.pos[0] * pool->scale[0];
        float bY = p.pos[1] * pool->scale[1];
        float bZ = p.pos[2] * pool->scale[2];

        Billboard_Vertex* q = &vb[quads * 6];
        // v0 = base + (up - right)  [TL]   (buf+0)
        q[0].x = bX + (uX - rX); q[0].y = bY + (uY - rY); q[0].z = bZ + (uZ - rZ); q[0].color = col;
        // v1 = base + (right + up) [TR]   (buf+24)
        q[1].x = bX + (rX + uX); q[1].y = bY + (rY + uY); q[1].z = bZ + (rZ + uZ); q[1].color = col;
        // v2 = base - (right + up) [BL]   (buf+48)
        q[2].x = bX - (rX + uX); q[2].y = bY - (rY + uY); q[2].z = bZ - (rZ + uZ); q[2].color = col;
        // v3 = base - (right + up) [BL]   (buf+72)
        q[3].x = q[2].x;         q[3].y = q[2].y;         q[3].z = q[2].z;         q[3].color = col;
        // v4 = base + (right + up) [TR]   (buf+96)
        q[4].x = q[1].x;         q[4].y = q[1].y;         q[4].z = q[1].z;         q[4].color = col;
        // v5 = base + (right - up) [BR]   (buf+120)
        q[5].x = bX + (rX - uX); q[5].y = bY + (rY - uY); q[5].z = bZ + (rZ - uZ); q[5].color = col;

        ++quads;                                              // 0x4248B7
        if (quads == params.maxQuads) break;                  // 0x4248BF : g_MaxQuadsPerBatch
    }

    if (quads) {                                              // 0x4248D8
        // SetTexture(0, def texture) then DrawPrimitiveUP(TRIANGLELIST, 2·quads, VB, 24).
        params.device->SetTexture(0, PtclDef_GetTexture(pool->def)); // vtbl+260 @0x4248F1
        params.device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,           // vtbl+332 @0x424910
                                       static_cast<UINT>(2 * quads),
                                       vb, sizeof(Billboard_Vertex));
    }
}

// ---------------------------------------------------------------------------
// I/O — injectable hooks for texture/motion (unowned third-party subsystems).
static TexReadPackedFn    s_texHook    = nullptr; // Tex_ReadPacked 0x417740
static MotionReadStreamFn s_motionHook = nullptr; // Anim_ReadMotionStream 0x43CDB0

void SetPtclIoHooks(TexReadPackedFn texHook, MotionReadStreamFn motionHook) {
    s_texHook = texHook; s_motionHook = motionHook;
}

// Reads `size` bytes into `dst` from `hFile`; returns true if everything was read
// (mimics ReadFile + count check, like every step of 0x422C50).
static bool Fx_ReadExact(HANDLE hFile, void* dst, DWORD size) {
    DWORD got = 0;
    return ReadFile(hFile, dst, size, &got, nullptr) && got == size;
}

// ---------------------------------------------------------------------------
// PtclDef_ReadFile 0x422C50 — deserializes a def from an open file HANDLE.
bool PtclDef_ReadFile(PtclDef* def, HANDLE hFile, int a3, int a4) {
    if (def->enabled) return false;                            // 0x422C54 : if (*a1) return 0

    // enabled (4 bytes) — if 0 on disk, empty def accepted as-is.
    if (!Fx_ReadExact(hFile, &def->enabled, 4)) return false;  // 0x422C7D
    if (!def->enabled) return true;                            // 0x422C8C : disabled def
    def->enabled = 0;                                          // 0x422CAB : reset to 0 while loading

    // texture[56] via Tex_ReadPacked; motion[16] via Anim_ReadMotionStream (hooks).
    if (s_texHook) {
        if (!s_texHook(def->texture, hFile, a3, a4)) { PtclDef_ZeroInit(def); return false; } // 0x422F32
    }
    if (s_motionHook) {
        if (!s_motionHook(def->motion, hFile)) { PtclDef_ZeroInit(def); return false; }
    }

    // Sequential numeric block (EXACT offsets/order of 0x422C50).
    const bool ok =
        Fx_ReadExact(hFile, &def->field19,       4)  &&        // a1+19 @0x4C
        Fx_ReadExact(hFile, &def->texAnimFPS,    4)  &&        // a1+20 @0x50
        Fx_ReadExact(hFile, &def->spawnRate,     4)  &&        // a1+21 @0x54
        Fx_ReadExact(hFile, &def->shapeType,     4)  &&        // a1+22 @0x58
        Fx_ReadExact(hFile, &def->baseSpeed,     4)  &&        // a1+23 @0x5C
        Fx_ReadExact(hFile, def->boxSize,        12) &&        // a1+24 @0x60
        Fx_ReadExact(hFile, &def->lifetime,      4)  &&        // a1+27 @0x6C
        Fx_ReadExact(hFile, def->velMin,         12) &&        // a1+28 @0x70
        Fx_ReadExact(hFile, def->velMax,         12) &&        // a1+31 @0x7C
        Fx_ReadExact(hFile, &def->param0Start,   4)  &&        // a1+34 @0x88
        Fx_ReadExact(hFile, &def->sizeStart,     4)  &&        // a1+35 @0x8C
        Fx_ReadExact(hFile, def->startColor,     16) &&        // a1+36 @0x90
        Fx_ReadExact(hFile, def->endColor,       16) &&        // a1+40 @0xA0
        Fx_ReadExact(hFile, def->forceBase,      12) &&        // a1+44 @0xB0
        Fx_ReadExact(hFile, def->forceRandMin,   12) &&        // a1+47 @0xBC
        Fx_ReadExact(hFile, def->forceRandMax,   12) &&        // a1+50 @0xC8
        Fx_ReadExact(hFile, &def->param0Rate,    4)  &&        // a1+53 @0xD4
        Fx_ReadExact(hFile, &def->sizeRate,      4);           // a1+54 @0xD8

    if (!ok) { PtclDef_ZeroInit(def); return false; }          // 0x422C7F : PtclDef_FreeBuffers + return 0
    def->enabled = 1;                                          // 0x422F44 : *a1 = 1
    return true;                                               // 0x422C5B
}

} // namespace ts2::gfx

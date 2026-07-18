// Game/GroundAuraWorldObjectTick.cpp — see GroundAuraWorldObjectTick.h for the original
// EAs, exact offsets, and documented naming discrepancies.
#include "Game/GroundAuraWorldObjectTick.h"
#include <cmath>
#include <cstring> // std::memcpy (reads the entity visibility gate dword_168724C)

namespace ts2::game {

namespace {

constexpr float kFrameRate30      = 30.0f; // Fx_MeleeSwingTick_Loop/Once @0x58043E/0x5804DE : a3*30.0
constexpr float kFarCullDistance  = 400.0f; // Fx_MeleeSwingTick_Loop @0x580483

// Resizes an extension vector to cover `index` (lazy growth — same
// idiom as Game/EntityLifecycleTick.cpp::EnsureCapacity).
template <class T>
bool EnsureCapacity(std::vector<T>& ext, int index) {
    if (index < 0) return false;
    if (static_cast<size_t>(index) >= ext.size()) ext.resize(static_cast<size_t>(index) + 1);
    return true;
}

inline float Dist3D(float ax, float ay, float az, float bx, float by, float bz) {
    const float dx = ax - bx, dy = ay - by, dz = az - bz;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Fx_MeleeSwingTick_Loop 0x580400.  // ex-VeryOldClient: EFFECT_OBJECT::Update (loop, advances
// mFrame via MoveCurve/DisplayMObject) — CONFIRMED (Docs/TS2_FX_ROSETTA.md §1).
void TickLoop(GroundItem& item, GroundItemTickExt& ext, float dt,
              const GroundAuraWorldObjectTickHost& host, const PlayerEntity& self) {
    const int frameCount = host.GetWeaponEffectFrameCount
                                ? host.GetWeaponEffectFrameCount(ext.effectDefHandle, ext.mode)
                                : -1; // null oracle -> never loops (clean degradation)
    ext.frame += dt * kFrameRate30; // @0x58043E
    if (frameCount > 0 && ext.frame >= static_cast<float>(frameCount))
        ext.frame -= static_cast<float>(frameCount); // @0x58045F

    // @0x580483 : Math_Dist3D((float*)this+5, flt_1687330) > 400.0 -> this+11 = this+20.
    // flt_1687330 == local player position (game::GameWorld::Self(), same convention as
    // Game/ItemPickupSystem.h/NpcInteraction.h/AutoPlaySystem.h).
    if (Dist3D(item.x, item.y, item.z, self.x, self.y, self.z) > kFarCullDistance)
        ext.farField44 = ext.farSrcField80; // @0x58048E (semantics undetermined)
}

// Fx_MeleeSwingTick_Once 0x5804A0.  // ex-VeryOldClient: EFFECT_OBJECT::Update ("one-shot"
// variant, auto-disables mObjType at completion) — CONFIRMED (Docs/TS2_FX_ROSETTA.md §1).
void TickOnce(GroundItemTickExt& ext, float dt, const GroundAuraWorldObjectTickHost& host) {
    const int frameCount = host.GetWeaponEffectFrameCount
                                ? host.GetWeaponEffectFrameCount(ext.effectDefHandle, ext.mode)
                                : -1;
    ext.frame += dt * kFrameRate30; // @0x5804DE
    if (frameCount > 0 && ext.frame >= static_cast<float>(frameCount)) {
        ext.mode  = 0; // @0x5804F8
        ext.frame = 0.0f; // @0x580504
    }
}

// ===========================================================================
// FX attack-projectile pool — internals (SoA pool dword_17D06F4, cf. section 2 of the .h).
// Layout/logic proven by Fx_SpawnAttackProjectile 0x582530/0x582A10 (writes) and
// Fx_HomingProjectileUpdate 0x5862D0 (reads). Each field = its original dword index.
// ===========================================================================

// Pool slot (64 dw = 256 bytes). Base dword_17D06F4. Zones not populated by projectile spawn
// (modelClass* [13/14], sobj*/particle* [30..44]) = attach states (pool #2), 0 here.
struct FxSlot {
    int32_t active;         // [0]  0x00  dword_17D06F4 — active
    int32_t state;          // [1]  0x04  dword_17D06F8 — FSM state (1..14)
    int32_t targetKind2;    // [2]  0x08  dword_17D06FC — v12 (states 5..) : 1=entity 2=monster
    int32_t ownerId1;       // [3]  0x0C  dword_17D0700 — shooter id hi
    int32_t ownerId2;       // [4]  0x10  dword_17D0704 — shooter id lo
    int32_t weaponId;       // [5]  0x14  dword_17D0708 — weapon/skill id
    int32_t targetKind1;    // [6]  0x18  dword_17D070C — v22 (state 1) : 1=entity 2=monster
    int32_t targetId1;      // [7]  0x1C  dword_17D0710 — target id hi
    int32_t targetId2;      // [8]  0x20  dword_17D0714 — target id lo
    int32_t arcMode;        // [9]  0x24  dword_17D0718 — arc branch (state 3-A), 1 if wep==113
    int32_t elemImmune;     // [10] 0x28  dword_17D071C — elemental immunity (suppresses Op18)
    float   arcTime;        // [11] 0x2C  unk_17D0720   — arc time (state 3-A)
    int32_t homingMode;     // [12] 0x30  dword_17D0724 — direct-homing branch (state 3-B), Alt=1
    int32_t modelClassA;    // [13] 0x34  — model A index (states 1/2, not populated here)
    int32_t modelClassB;    // [14] 0x38  — model B index (states 1/2, not populated here)
    int32_t motionIndex;    // [15] 0x3C  dword_17D0730 — motion (Anim_MapWeaponToMotion1/2)
    float   animFrame;      // [16] 0x40  unk_17D0734   — frame cursor (+= dt*30)
    float   posX, posY, posZ;   // [17..19] 0x44/48/4C  unk_17D0738/3C/40 — current position
    float   drawRotX;       // [20] 0x50  unk_17D0744   — render rot x (= 0)
    float   angle;          // [21] 0x54  unk_17D0748   — heading (Math_AngleBetween2D)
    float   drawRotZ;       // [22] 0x58  unk_17D074C   — render rot z (= 0)
    float   startX, startY, startZ;    // [23..25] 0x5C/60/64 unk_17D0750/54/58 — start position
    float   targetX, targetY, targetZ; // [26..28] 0x68/6C/70 unk_17D075C/60/64 — target position
    float   speed;          // [29] 0x74  unk_17D0768   — speed
    int32_t sobjPtr;        // [30] 0x78  — SObject (states 5..11 render, not populated here)
    int32_t sobjArg;        // [31] 0x7C  — render
    int32_t particleDef;    // [32] 0x80  — particle def index (states 5..14 render)
    int32_t _slotBody[12];  // [33..44] 0x84..0xB0 — emitter block (this+132, render/non-owned)
    // Op18 report payload @ this+180 = [45..56]:
    int32_t pktType;        // [45] 0xB4  dword_17D07A8 = 4
    int32_t pktOwnerId1;    // [46] 0xB8  dword_17D07AC — shooter id hi
    int32_t pktOwnerId2;    // [47] 0xBC  dword_17D07B0 — shooter id lo
    int32_t pktTargetId1;   // [48] 0xC0  dword_17D07B4 — target id hi
    int32_t pktTargetId2;   // [49] 0xC4  dword_17D07B8 — target id lo
    float   pktImpactX;     // [50] 0xC8  unk_17D07BC
    float   pktImpactY;     // [51] 0xCC  unk_17D07C0
    float   pktImpactZ;     // [52] 0xD0  unk_17D07C4
    int32_t pktFlag1;       // [53] 0xD4  dword_17D07C8 = 1
    int32_t pktFlag2;       // [54] 0xD8  dword_17D07CC = 0
    int32_t pktFlag3;       // [55] 0xDC  dword_17D07D0 = 0
    int32_t homingFlag2;    // [56] 0xE0  dword_17D07D4 — 0 (main) / 1 (Alt)
    int32_t _tail[7];       // [57..63] 0xE4..0xFC — unused
};
static_assert(sizeof(FxSlot) == 256, "FxSlot must be 64 dwords (stride 0x100)");

constexpr size_t kFxPoolCapacity = 1000;  // cGameData_InitPools @0x5575DC (*(this+1721)=1000)
FxSlot g_FxPool[kFxPoolCapacity];         // dword_17D06F4 (base) — static zero-init
int    g_FxAuraCount = static_cast<int>(kFxPoolCapacity); // g_FxAuraCount 0x168722C (capacity)

// --- Weapon/skill -> motion tables (Anim_MapWeaponToMotion1/2/3), ported inline -------------
// Anim_MapWeaponToMotion1 0x5475F0 (flight motion).
int MapWeaponToMotion1(int a1) {
    if (a1 > 268) { if (a1 == 270) return 301; if (a1 == 272) return 307; return -1; }
    if (a1 == 268) return 295;
    switch (a1) {
        case 8: return 0;   case 25: return 2;   case 29: return 18;  case 33: return 32;
        case 39: return 5;  case 49: return 9;   case 82: return 169; case 83: return 36;
        case 84: return 12; case 85: return 15;  case 86: return 141; case 88: return 99;
        case 89: return 21; case 100: return 112;case 101: return 117;case 102: return 122;
        case 103: return 127;case 104: return 132;case 105: return 172;case 113: return 41;
        case 115: return 24;case 116: return 26; case 117: return 28; case 122: return 90;
        case 131: return 219;case 132: return 221;case 169: return 153;case 175: return 180;
        case 184: return 192;case 185: return 195;case 186: return 198;case 187: return 201;
        case 188: return 205;case 189: return 210;case 190: return 215;case 196: return 227;
        case 200: return 233;case 206: return 241;case 210: return 247;case 216: return 256;
        case 223: return 263;
        default: return -1;
    }
}
// Anim_MapWeaponToMotion2 0x547970 (impact motion).
int MapWeaponToMotion2(int a1) {
    if (a1 > 266) { if (a1 == 270) return 302; if (a1 == 272) return 308; return -1; }
    if (a1 == 266) return 316;
    switch (a1) {
        case 8: return 1;   case 25: return 3;   case 29: return 19;  case 33: return 33;
        case 39: return 6;  case 49: return 10;  case 82: return 170; case 83: return 37;
        case 84: return 13; case 85: return 16;  case 86: return 142; case 88: return 100;
        case 89: return 22; case 100: return 113;case 101: return 118;case 102: return 123;
        case 103: return 128;case 104: return 133;case 105: return 173;case 113: return 97;
        case 115: return 25;case 116: return 27; case 117: return 29; case 122: return 91;
        case 131: return 220;case 132: return 222;case 169: return 154;case 175: return 181;
        case 184: return 193;case 185: return 196;case 186: return 199;case 187: return 202;
        case 188: return 206;case 189: return 211;case 190: return 216;case 196: return 228;
        case 200: return 234;case 206: return 242;case 210: return 248;case 216: return 257;
        case 223: return 264;
        default: return -1;
    }
}
// Anim_MapWeaponToMotion3 0x547CF0 (impact sound id).
int MapWeaponToMotion3(int a1) {
    if (a1 > 258) {
        switch (a1) {
            case 260: return 260; case 262: return 252; case 266: return 260; case 268: return 270;
            case 270: return 270; case 272: return 270; case 276: return 276; case 277: return 281;
            case 278: return 276; case 279: return 276; case 280: return 281; case 281: return 281;
            default: return -1;
        }
    }
    if (a1 == 258) return 260;
    switch (a1) {
        case 8: return 265;  case 25: return 266; case 29: return 271; case 33: return 276;
        case 39: return 267; case 49: return 268; case 83: return 277; case 84: return 269;
        case 85: return 270; case 88: return 291; case 89: return 272; case 100: return 318;
        case 101: return 319;case 102: return 320;case 103: return 321;case 104: return 322;
        case 105: return 342;case 113: return 278;case 115: return 273;case 116: return 274;
        case 117: return 275;case 122: return 275;case 131: return 369;case 132: return 370;
        case 169: return 340;case 175: return 346;case 184: return 357;case 185: return 358;
        case 186: return 359;case 187: return 360;case 188: return 365;case 189: return 366;
        case 190: return 367;case 196: return 383;case 200: return 384;case 206: return 385;
        case 210: return 386;case 216: return 396;case 222: return 397;case 252: return 252;
        case 254: return 252;
        default: return -1;
    }
}

// --- Ported math helpers (arbitrated by IDA) -------------------------------------------------
// Math_AngleBetween2D 0x53FB20 : heading (deg 0..360) from point (a1,a2) to (a3,a4).
float AngleBetween2D(float a1, float a2, float a3, float a4) {
    if (a3 == a1 && a4 == a2) return 0.0f;                       // @0x53FB45
    float dx = a3 - a1, dz = a4 - a2;                            // v12,v13
    float len = std::sqrt(dz * dz + dx * dx);                   // @0x53FB82
    if (len > 0.0f) { dx /= len; dz /= len; }                   // @0x53FB99
    float chordZ = dz - 1.0f;                                   // v14
    float chord = std::sqrt(chordZ * chordZ + dx * dx);         // @0x53FBDB
    float half = chord * 0.5f;                                  // @0x53FBF8 (v8/2)
    if (half > 1.0f) half = 1.0f;                               // @0x53FBF8 (else asin(1.0))
    float ang = std::asin(half) * 2.0f;                         // @0x53FC30/@0x53FC44 (rad)
    if (a3 < a1) ang = 6.283185482025146f - ang;                // @0x53FC54
    float deg = ang * 57.2957763671875f + 180.0f;               // @0x53FC71
    if (deg >= 360.0f) return deg - 360.0f;                     // @0x53FC82
    return deg;
}

// Math_MoveProjectileArc 0x588640 : integrates one step of a parabolic trajectory (XZ plane +
// Y arc height). Returns true on arrival (snaps to target). start/target/out = vec3.
bool MoveProjectileArc(float sx, float sy, float sz,
                       float tx, float ty, float tz,
                       float t, float speed,
                       float& ox, float& oy, float& oz) {
    float dx = tx - sx;                                         // v10
    float dz = tz - sz;                                         // v11
    float horiz = std::sqrt(dx * dx + dz * dz);                 // v22 @0x5886DF
    float ndx = 0.0f, ndz = 0.0f;                               // D3DXVec2Normalize(v18,{dx,dz}) @0x5886B8
    if (horiz > 0.0f) { ndx = dx / horiz; ndz = dz / horiz; }
    float tTotal = (speed != 0.0f) ? (horiz / speed) : 0.0f;    // v15 @0x5886EE
    ox = ndx * speed * t + sx;                                  // @0x588702
    oz = ndz * speed * t + sz;                                  // @0x588716
    float dY = ty - sy;                                         // v25 @0x588725
    float deg = (tTotal > 0.0f) ? (t / tTotal * 180.0f) : 180.0f; // v19 @0x588734
    if (deg < 0.00009999999747378752f) deg = 0.0f;             // @0x588745
    if (deg > 179.9998931884766f) deg = 180.0f;                // @0x58875A
    float rad = deg * 0.01745329238474369f;                    // v7 @0x58876E
    float arcH = std::cos(rad) * horiz * 0.3300000131130219f;  // @0x588788/@0x58878B
    arcH = deg * 0.5f / 90.0f * dY + arcH;                     // @0x588797/@0x58879D/@0x5887A3
    oy = sy + arcH;                                            // @0x5887B2/@0x5887B8
    if (deg <= 179.9998931884766f) return false;               // @0x5887C9
    ox = tx; oy = ty; oz = tz;                                 // snap @0x5887D3..
    return true;
}

// --- Entity access (g_EntityArray dword_1687234 = g_World.players ; index 0 = self) ----------
// Looks up the active PLAYER entity (g_EntityArray[227*i]) that is visible (dword_168724C[227*i] =
// entity+24 = body[0] != 0 ; exact gate semantics not confirmed, offset proven) whose
// id == (id1,id2). -1 if absent. @0x586D58 (loop of state 3 branch C).
int FindPlayerEntity(uint32_t id1, uint32_t id2) {
    for (size_t i = 0; i < g_World.players.size(); ++i) {
        const PlayerEntity& e = g_World.players[i];
        if (!e.active) continue;                               // g_EntityArray[227*i]
        uint32_t visGate = 0;                                  // dword_168724C[227*i] = entity+24 = body[0]
        std::memcpy(&visGate, e.body.data(), sizeof(visGate));
        if (!visGate) continue;
        if (e.id.hi == id1 && e.id.lo == id2) return static_cast<int>(i);
    }
    return -1;
}

// Local entity (index 0 = self, flt_1687330[0]/dword_1687238[0]). nullptr if the array is empty.
const PlayerEntity* SelfEntity() {
    return g_World.players.empty() ? nullptr : &g_World.players[0];
}

// --- FSM tick (states 3/4) -------------------------------------------------------------------
// Frees the slot (Fx_AttachSlotClear 0x584220: for a projectile, no particle/SObject
// handle to free -> active=0 suffices; resource release is a no-op here).
void ClearSlot(FxSlot& s) { s.active = 0; } // LABEL_258 @0x5884ED

FxImpactReport MakeReport(const FxSlot& s) {
    FxImpactReport r;
    r.type = s.pktType;                                        // dw[45]
    r.owner.hi = static_cast<uint32_t>(s.pktOwnerId1);         // dw[46]
    r.owner.lo = static_cast<uint32_t>(s.pktOwnerId2);         // dw[47]
    r.target.hi = static_cast<uint32_t>(s.pktTargetId1);       // dw[48]
    r.target.lo = static_cast<uint32_t>(s.pktTargetId2);       // dw[49]
    r.impactX = s.pktImpactX; r.impactY = s.pktImpactY; r.impactZ = s.pktImpactZ; // dw[50..52]
    r.flag1 = s.pktFlag1; r.flag2 = s.pktFlag2; r.flag3 = s.pktFlag3;             // dw[53..55]
    r.homing = s.homingFlag2;                                  // dw[56]
    return r;
}

// On impact when the target is the local player: fills in the target/self pos in the payload
// and emits the Op18 report (via host). @0x586992/@0x586C7D (pktTarget=self) + @0x5867B6 (Op18).
void SendSelfHitReport(FxSlot& s, const PlayerEntity& self) {
    s.pktTargetId1 = static_cast<int32_t>(self.id.hi);         // dword_1687238[0]
    s.pktTargetId2 = static_cast<int32_t>(self.id.lo);         // dword_168723C[0]
    s.pktImpactX = self.x; s.pktImpactY = self.y; s.pktImpactZ = self.z; // flt_1687330/34/38[0]
    if (g_FxProjectileHost.NotifyProjectileImpact)
        g_FxProjectileHost.NotifyProjectileImpact(MakeReport(s)); // Net_SendPacket_Op18 @0x4B4CF0
}

// Common post-impact transition (state 3 -> 4): impact sound + impact motion. @0x5869F8..0x586A5F
void FinalizeImpact(FxSlot& s) {
    int sndId = MapWeaponToMotion3(s.weaponId);                // v37 @0x5869F8
    if (sndId != -1 && g_FxProjectileHost.PlayImpactSound)
        g_FxProjectileHost.PlayImpactSound(sndId, s.posX, s.posY, s.posZ); // @0x586A1E
    s.state = 4;                                               // @0x586A26
    s.motionIndex = MapWeaponToMotion2(s.weaponId);            // @0x586A41
    if (s.motionIndex == -1) { ClearSlot(s); return; }         // @0x586A4B
    s.animFrame = 0.0f;                                        // @0x586A5F
}

int ProjectileFrameCount(const FxSlot& s) {
    return g_FxProjectileHost.GetProjectileFrameCount
               ? g_FxProjectileHost.GetProjectileFrameCount(s.motionIndex) // ModelObj_GetSubObjectCount(unk_B551B8+148*[15])
               : 0;
}

// State 3 (homing flight): 3 branches — parabolic arc (arcMode, wep==113) / direct homing
// (homingMode, Alt) / homing toward a live entity (default). @0x58689F
void TickState3(FxSlot& s, float dt) {
    const PlayerEntity* self = SelfEntity();

    if (s.arcMode) {                                           // this[9] — parabolic arc @0x58689F
        int fc = ProjectileFrameCount(s);                     // @0x5868C4
        if (fc > 0) { s.animFrame += dt * 30.0f; if (s.animFrame >= (float)fc) s.animFrame = 0.0f; } // @0x5868D8
        s.arcTime += dt;                                       // @0x58691D
        if (MoveProjectileArc(s.startX, s.startY, s.startZ, s.targetX, s.targetY, s.targetZ,
                              s.arcTime, s.speed, s.posX, s.posY, s.posZ)) {              // @0x58694C
            if (self && !s.elemImmune &&
                Dist3D(self->x, self->y, self->z, s.posX, s.posY, s.posZ) < 100.0f)      // @0x586987
                SendSelfHitReport(s, *self);
            FinalizeImpact(s);
        }
    } else if (s.homingMode) {                                 // this[12] — direct homing @0x586A6F
        int fc = ProjectileFrameCount(s);                     // @0x586A94
        if (fc > 0) { s.animFrame += dt * 30.0f; if (s.animFrame >= (float)fc) s.animFrame = 0.0f; }
        float dx = s.targetX - s.posX, dy = s.targetY - s.posY, dz = s.targetZ - s.posZ; // @0x586AED
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);     // @0x586B38
        if (d > 0.0f) { dx /= d; dy /= d; dz /= d; }          // @0x586B4F
        s.posX += s.speed * dt * dx; s.posY += s.speed * dt * dy; s.posZ += s.speed * dt * dz; // @0x586B81..
        s.angle = AngleBetween2D(s.posX, s.posZ, s.targetX, s.targetZ);                  // @0x586BE9
        if (Dist3D(s.targetX, s.targetY, s.targetZ, s.startX, s.startY, s.startZ)
            < Dist3D(s.posX, s.posY, s.posZ, s.startX, s.startY, s.startZ)) {            // @0x586C27
            s.posX = s.targetX; s.posY = s.targetY; s.posZ = s.targetZ;                  // @0x586C36..
            if (self && Dist3D(self->x, self->y, self->z, s.posX, s.posY, s.posZ) < 100.0f) // @0x586C72
                SendSelfHitReport(s, *self);
            FinalizeImpact(s);
        }
    } else {                                                  // homing toward a live entity @0x586D58
        int e = FindPlayerEntity(static_cast<uint32_t>(s.targetId1), static_cast<uint32_t>(s.targetId2));
        if (e < 0) { ClearSlot(s); return; }                  // @0x586DD8
        const PlayerEntity& tgt = g_World.players[static_cast<size_t>(e)];
        s.targetX = tgt.x;                                    // @0x586DF9
        s.targetY = tgt.y + 10.0f;                            // @0x586E14
        s.targetZ = tgt.z;                                    // @0x586E29
        const float arriveDist = 4.5f;                        // v29 @0x586E32
        int fc = ProjectileFrameCount(s);                     // @0x586E50
        if (fc > 0) { s.animFrame += dt * 30.0f; if (s.animFrame >= (float)fc) s.animFrame = 0.0f; }
        float dx = s.targetX - s.posX, dy = s.targetY - s.posY, dz = s.targetZ - s.posZ; // @0x586EA9
        float d = std::sqrt(dx * dx + dy * dy + dz * dz);     // @0x586EF4
        if (d > 500.0f) { ClearSlot(s); return; }             // @0x586F11 (target too far → give up)
        if (d > 0.0f) { dx /= d; dy /= d; dz /= d; }          // @0x586F2E
        s.posX += s.speed * dt * dx; s.posY += s.speed * dt * dy; s.posZ += s.speed * dt * dz; // @0x586F60..
        s.angle = AngleBetween2D(s.posX, s.posZ, s.targetX, s.targetZ);                  // @0x586FC8
        if (arriveDist > Dist3D(s.targetX, s.targetY, s.targetZ, s.posX, s.posY, s.posZ)
            || Dist3D(s.targetX, s.targetY, s.targetZ, s.startX, s.startY, s.startZ)
               < Dist3D(s.posX, s.posY, s.posZ, s.startX, s.startY, s.startZ)) {         // @0x587030
            s.posX = s.targetX; s.posY = s.targetY; s.posZ = s.targetZ;                  // @0x58703F..
            if (self && s.pktTargetId1 == static_cast<int32_t>(self->id.hi)
                     && s.pktTargetId2 == static_cast<int32_t>(self->id.lo))             // @0x587079 (target == self)
                SendSelfHitReport(s, *self);
            FinalizeImpact(s);
        }
    }
}

// State 4 (impact anim): plays the impact anim to completion then frees the slot. @0x58715C
void TickState4(FxSlot& s, float dt) {
    int fc = ProjectileFrameCount(s);
    if (fc < 1) { ClearSlot(s); return; }                     // @0x58715C (no anim → clear)
    s.animFrame += dt * 30.0f;                                // @0x58717D
    if (s.animFrame >= static_cast<float>(fc)) { ClearSlot(s); return; } // @0x5871B2 (anim end → clear)
}

// Allocation shared by Fx_SpawnAttackProjectile / …Alt. `alt` = variant 0x582A10.
int SpawnInternal(const FxProjectileSpawnParams& p, bool alt) {
    int i = 0;                                                 // @0x582539
    while (i < g_FxAuraCount && g_FxPool[i].active) ++i;
    if (i == g_FxAuraCount) return i;                         // pool full, no alloc @0x582572
    FxSlot& s = g_FxPool[i];
    s = FxSlot{};                                             // recycled slot (cGameData_FxItemPoolItemDtor)
    s.active = 1;                                             // dw[0]  @0x58257F
    s.state = 3;                                             // dw[1]  @0x58258F
    s.targetKind2 = 2;                                        // dw[2]  @0x58259F
    s.ownerId1 = static_cast<int32_t>(p.owner.hi);           // dw[3]  @0x5825B5
    s.ownerId2 = static_cast<int32_t>(p.owner.lo);           // dw[4]  @0x5825C7
    s.weaponId = static_cast<int32_t>(p.weaponId);           // dw[5]  @0x5825DF
    s.targetKind1 = 1;                                        // dw[6]  @0x5825EB
    s.targetId1 = static_cast<int32_t>(p.target.hi);         // dw[7]  @0x582601
    s.targetId2 = static_cast<int32_t>(p.target.lo);         // dw[8]  @0x582613
    s.arcMode = 0;                                           // dw[9]  @0x58261F
    s.homingMode = alt ? 1 : 0;                              // dw[12] @0x58262F (main) / @0x582B0F (alt)
    if (!alt && p.weaponId == 113) {                         // weaponId==113 branch (MAIN) @0x58264C
        s.arcMode = 1;                                       // dw[9]=1 @0x582659
        // dw[10]: elemental immunity (weaponSubtype switch via g_LocalPlayerSheet, not
        // modeled → host, default false). @0x58268F..0x5827A6
        s.elemImmune = (g_FxProjectileHost.IsLocalElementImmune
                            && g_FxProjectileHost.IsLocalElementImmune(p.weaponSubtype)) ? 1 : 0;
        s.arcTime = 0.0f;                                    // dw[11] @0x5827B0
    }
    s.motionIndex = MapWeaponToMotion1(static_cast<int>(p.weaponId)); // dw[15] @0x5827DB
    if (s.motionIndex == -1) {                               // @0x5827EE
        s.active = 0;                                        // dw[0]=0 @0x5827F6
        return i << 8;                                       // @0x5827F3
    }
    s.animFrame = 0.0f;                                     // dw[16] @0x58280D
    s.posX = p.startX;                                      // dw[17] @0x58281F
    s.posY = static_cast<float>(p.heightOffset) + p.startYRaw; // dw[18] @0x58283D
    s.posZ = p.startZ;                                      // dw[19] @0x58284F
    s.drawRotX = 0.0f;                                     // dw[20] @0x58285D
    s.angle = p.heading;                                   // dw[21] @0x58286F
    s.drawRotZ = 0.0f;                                     // dw[22] @0x58287D
    s.startX = s.posX;                                     // dw[23] @0x582895
    s.startY = s.posY;                                     // dw[24] @0x5828AD
    s.startZ = s.posZ;                                     // dw[25] @0x5828C5
    s.targetX = p.targetX;                                 // dw[26] @0x5828D7
    s.targetY = p.targetY;                                 // dw[27] @0x5828E9
    s.targetZ = p.targetZ;                                 // dw[28] @0x5828FB
    s.speed = static_cast<float>(p.speed);                 // dw[29] @0x582913
    s.pktType = 4;                                         // dw[45] @0x58291F
    s.pktOwnerId1 = static_cast<int32_t>(p.owner.hi);      // dw[46] @0x582935
    s.pktOwnerId2 = static_cast<int32_t>(p.owner.lo);      // dw[47] @0x582947
    s.pktTargetId1 = static_cast<int32_t>(p.target.hi);    // dw[48] @0x582959
    s.pktTargetId2 = static_cast<int32_t>(p.target.lo);    // dw[49] @0x58296B
    s.pktImpactX = 0.0f;                                   // dw[50] @0x582979
    s.pktImpactY = 0.0f;                                   // dw[51] @0x582987
    s.pktImpactZ = 0.0f;                                   // dw[52] @0x582995
    s.pktFlag1 = 1;                                        // dw[53] @0x5829A1
    s.pktFlag2 = 0;                                        // dw[54] @0x5829B1
    s.pktFlag3 = 0;                                        // dw[55] @0x5829C1
    s.homingFlag2 = alt ? 1 : 0;                           // dw[56] @0x5829D1 (main) / @0x582D2C (alt)
    return i << 8;                                         // @0x5829CE
}

} // namespace

// ===========================================================================
// 1. Fx_MeleeSwingTick 0x5803A0
// ex-VeryOldClient: EFFECT_OBJECT::Update (FSM mObjType 1..14) — CONFIRMED (Docs/TS2_FX_ROSETTA.md
//   §1). Note: the EU build does NOT have the mega-struct EFFECT_OBJECT[1000]: effects are split
//   across attach slots (Fx_Attach*) + projectile SoA pool + swing timers (CONFLICT 3-A — IDA
//   wins, no VeryOld layout/address transposed). Here = swing timer (Loop/Once) ported faithfully.
// ===========================================================================

void ResetGroundItemTickExt(int groundItemIndex) {
    if (!EnsureCapacity(g_GroundItemTickExt, groundItemIndex)) return;
    g_GroundItemTickExt[static_cast<size_t>(groundItemIndex)] = GroundItemTickExt{};
}

void TickGroundItemEffect(GameWorld& world, int groundItemIndex, float dt,
                           const GroundAuraWorldObjectTickHost& host) {
    if (groundItemIndex < 0 || static_cast<size_t>(groundItemIndex) >= world.npcRenderEntries.size())
        return;
    NpcRenderEntry& item = world.npcRenderEntries[static_cast<size_t>(groundItemIndex)];
    if (!item.active) return; // head guard `*(this+1)` @0x5803AC (defensive, already filtered
                               // by the caller InGameTickFlow.cpp)

    if (!EnsureCapacity(g_GroundItemTickExt, groundItemIndex)) return;
    GroundItemTickExt& ext = g_GroundItemTickExt[static_cast<size_t>(groundItemIndex)];

    switch (ext.mode) { // *(this+3) @0x5803BA
    case 0: TickLoop(item, ext, dt, host, world.Self()); break; // @0x5803D9
    case 1: TickOnce(ext, dt, host); break;                     // @0x5803EE
    default: break;                                             // faithful no-op (other value)
    }
}

// ===========================================================================
// 2. FX attack-projectile pool (g_FxAuraCount / dword_17D06F4)
// ===========================================================================
// IMPLEMENTED ("FX pool #1" mission). Slot layout + spawn + homing tick (states 3/4) ported
// FAITHFULLY (IDA anchors on every line, cf. internals above). Render/net/audio/element-leaf
// effects deferred via g_FxProjectileHost (cf. .h). ex-VeryOldClient: EFFECT_OBJECT
// types 1→2 (homing missile → impact → server notify), 3→4, 12→13 — PLAUSIBLE (taxonomy
// only; SoA stride 64 EU ≠ EFFECT_OBJECT[1000], CONFLICT 3-A, no layout/address transposed).

void Fx_InitProjectilePool() {
    g_FxAuraCount = static_cast<int>(kFxPoolCapacity);   // *(this+1721)=1000 @0x5575DC
    for (auto& s : g_FxPool) s.active = 0;               // clear (Pkt_EnterWorld @0x4642A4)
}

int Fx_SpawnAttackProjectile(const FxProjectileSpawnParams& p) {    // 0x582530
    return SpawnInternal(p, false);
}

int Fx_SpawnAttackProjectileAlt(const FxProjectileSpawnParams& p) { // 0x582A10
    return SpawnInternal(p, true);
}

int GetFxAuraCount() {
    return g_FxAuraCount; // g_FxAuraCount 0x168722C (capacity, tick loop bound)
}

bool IsFxAuraActive(int index) {
    if (index < 0 || index >= g_FxAuraCount) return false; // index guard
    return g_FxPool[index].active != 0;                    // dword_17D06F4[64*index]
}

void UpdateHomingProjectile(int index, float dt) {         // Fx_HomingProjectileUpdate 0x5862D0
    if (index < 0 || index >= g_FxAuraCount) return;       // index guard
    FxSlot& s = g_FxPool[index];
    if (!s.active) return;                                 // head active guard @0x5862D6
    switch (s.state) {                                     // switch (*((_DWORD *)this + 1)) @0x5862F0
    case 3: TickState3(s, dt); break;                      // @0x58689F
    case 4: TickState4(s, dt); break;                      // @0x58715C
    default:
        // States 1/2/12/13 (projectiles from OTHER spawns NOT OWNED: Effect_SpawnSkillProjectile
        // 0x573A90 / Effect_SpawnTargetedSkillFx 0x573F80) and 5..11/14 (attach/particles, pool
        // #2 render NOT OWNED): not produced by this module → faithful no-op (`default: return`).
        // TODO(pool #2 / skill-proj): if a future non-owned spawn populates these states in the
        // shared pool, route here to its dedicated updater (states 5..14 = SObject_Draw 0x4D8F90 +
        // Particle_EnsureLoadedThenUpdateEmit 0x4D9F40, OUT OF render SCOPE).
        break;
    }
}

// ===========================================================================
// 3. Zone objects / resource nodes (g_World.zoneObjects)
// ===========================================================================

int GetWorldObjectCount(const GameWorld& world) {
    return static_cast<int>(world.zoneObjects.size());
}

bool IsWorldObjectActive(const GameWorld& world, int index) {
    if (index < 0 || static_cast<size_t>(index) >= world.zoneObjects.size()) return false;
    return world.zoneObjects[static_cast<size_t>(index)].active;
}

void TickWorldObject(float) {
    // sub_584170 0x584170 : empty stub in the original binary. Reproduced faithfully.
}

} // namespace ts2::game

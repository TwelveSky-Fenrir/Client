// Game/EntityLifecycleTick.cpp — see EntityLifecycleTick.h for original EAs, exact
// offsets, and documented gaps (sub_580550 naming, this+24 ambiguity, etc.).
#include "Game/EntityLifecycleTick.h"

namespace ts2::game {

namespace {

constexpr float kGibGravity  = -300.0f; // Fx_GibUpdate @0x583d1e : a3 * -300.0
constexpr float kFallGravity = -300.0f; // Char_Update   @0x582197 : a3 * -300.0
constexpr float kFrameRate30 = 30.0f;   // dt(s) -> frames @30 FPS conversion (a3*30.0)
constexpr float kGroundProbeOffset = 20.0f; // Char_Update @0x58221b / Fx_GibUpdate @0x583db1 : pos.y + 20.0

// Resizes an extension vector to cover `index` (lazy growth, like
// GameState.cpp::FindOrAdd grows its arrays). Returns false if index < 0.
template <class T>
bool EnsureCapacity(std::vector<T>& ext, int index) {
    if (index < 0) return false;
    if (static_cast<size_t>(index) >= ext.size()) ext.resize(static_cast<size_t>(index) + 1);
    return true;
}

} // namespace

void ResetMonsterTickExt(int monsterIndex) {
    if (!EnsureCapacity(g_MonsterTickExt, monsterIndex)) return;
    g_MonsterTickExt[static_cast<size_t>(monsterIndex)] = MonsterTickExt{};
}

void ResetNpcTickExt(int npcIndex) {
    if (!EnsureCapacity(g_NpcTickExt, npcIndex)) return;
    g_NpcTickExt[static_cast<size_t>(npcIndex)] = NpcTickExt{};
}

// The 3 "slot deactivation" functions (sub_55D720 / sub_580550 / sub_583390) — all
// three decompile to `*(_DWORD*)this = 0; return this;`, see documented gap in
// EntityLifecycleTick.h. Common primitive:
namespace {
template <class Vec>
void DeactivateSlot(Vec& v, int index) {
    if (index < 0 || static_cast<size_t>(index) >= v.size()) return;
    v[static_cast<size_t>(index)].active = false; // *(_DWORD*)this = 0
}
} // namespace

void DespawnStalePlayer(GameWorld& world, int playerIndex) {              // sub_55D720
    DeactivateSlot(world.players, playerIndex);
}

void RespawnMonsterAfterKnockback(GameWorld& world, int monsterIndex) {   // sub_580550
    DeactivateSlot(world.monsters, monsterIndex);
}

void CleanupStaleNpcEntity(GameWorld& world, int npcIndex) {              // sub_583390
    DeactivateSlot(world.npcs, npcIndex);
}

// Char_Update 0x581E10 — monster tick.
void UpdateMonster(GameWorld& world, int monsterIndex, float dt, const EntityLifecycleTickHost& host) {
    if (monsterIndex < 0 || static_cast<size_t>(monsterIndex) >= world.monsters.size()) return;
    MonsterEntity& mon = world.monsters[static_cast<size_t>(monsterIndex)];
    if (!mon.active) return; // head guard `if (*(_DWORD*)this)` @0x581e1c

    if (!EnsureCapacity(g_MonsterTickExt, monsterIndex)) return;
    MonsterTickExt& ext = g_MonsterTickExt[static_cast<size_t>(monsterIndex)];

    // --- 3 model-swap timers (this+64/65, 66/67, 68/69) @0x581e29..0x581f24 ---
    for (int slot = 0; slot < 3; ++slot) {
        if (!ext.auraActive[slot]) continue;
        ext.auraFrame[slot] += dt * kFrameRate30;
        const float duration = host.GetAuraSwapDuration ? host.GetAuraSwapDuration(slot) : 0.0f;
        if (ext.auraFrame[slot] >= duration) ext.auraActive[slot] = false;
    }

    // --- Hit window (states 5=AttackA / 7=AttackB only) @0x581f34..0x582145 ---
    if (ext.motionState == 5 && ext.attackWindupMode == 1) {
        bool justArmed = false;
        const bool inWindow = host.IsFrameInHitListA && host.IsFrameInHitListA(ext.weaponOrSkillAnimId, ext.animFrame);
        if (ext.hitLatched) {
            if (!inWindow) ext.hitLatched = false; // @0x581fbe
        } else if (inWindow) {
            ext.hitLatched = true;                 // @0x581f8c
            justArmed = true;                       // v12=1 @0x581f93
        }
        if (justArmed) {
            const bool bypass = host.IsAttackTargetBypassActive && host.IsAttackTargetBypassActive(); // @0x581ff2
            bool targetExists = false;
            if (!bypass) {
                // Scan g_EntityArray (players) for an active entity whose id == attackTargetId.
                // @0x581ff4..0x582060
                for (const PlayerEntity& p : world.players) {
                    if (p.active && p.id == ext.attackTargetId) { targetExists = true; break; }
                }
            }
            if (bypass || targetExists) { // LABEL_32 @0x58206a
                if (ext.hitActionKind == 1) {
                    if (ext.hitActionSub == 1 && host.SendMeleeHit1) host.SendMeleeHit1(monsterIndex);      // @0x582095
                    else if (ext.hitActionSub == 2 && host.SendMeleeHit2) host.SendMeleeHit2(monsterIndex); // @0x58209f
                } else if (ext.hitActionKind == 2 && host.SpawnAttackProjectile) {
                    host.SpawnAttackProjectile(monsterIndex); // @0x5820a9
                }
            }
        }
    } else if (ext.motionState == 7 && ext.attackWindupMode == 1) {
        bool justArmed = false;
        const bool inWindow = host.IsFrameInHitListB && host.IsFrameInHitListB(ext.weaponOrSkillAnimId, ext.animFrame);
        if (ext.hitLatched) {
            if (!inWindow) ext.hitLatched = false; // @0x582125
        } else if (inWindow) {
            ext.hitLatched = true;  // @0x5820f3
            justArmed = true;        // @0x5820fa
        }
        if (justArmed && host.SpawnAttackProjectileAlt) host.SpawnAttackProjectileAlt(monsterIndex); // @0x58213d
    }

    // --- Fall/knockback (ABSOLUTE overlay, does NOT touch mon.x/y/z) @0x582145..0x5822b6 ---
    // FIXED (audit steps 5-8, 2026-07-14): raw-disassembly re-check of 0x582145..0x582289 --
    // ground probe AND landing sound use CURRENT fallOffX/fallOffZ (this+0xF0/+0xF8, copied
    // at 0x582206..0x582236), NOT mon.x/mon.z. Previous version wrongly used mon.x/mon.z
    // (never updated here) instead of the fall position tracked by fallOffX/Y/Z -- with
    // MonsterTickExt zero-init until spawn (see ResetMonsterTickExt above), the probe used to
    // target (0,0,~20) instead of the real landing point. Second gap fixed: probe origin
    // height uses fallOffY from BEFORE this tick's update (snapshot @0x582152..0x58217f, read
    // @0x582212 before the recompute @0x5821d3), captured below as oldFallOffY.
    if (ext.fallActive && ext.fallLandCounter < ext.fallLandThreshold) {
        const float oldFallOffY = ext.fallOffY; // pre-update snapshot @0x582152-0x58217f
        ext.fallVelY += dt * kFallGravity;             // @0x582197 (gravity)
        ext.fallOffX += ext.fallVelX * dt;              // @0x5821b5
        ext.fallOffY += ext.fallVelY * dt;              // @0x5821d3
        ext.fallOffZ += ext.fallVelZ * dt;              // @0x5821f1
        float groundY = 0.0f;
        const bool found = host.GetGroundHeight &&
            host.GetGroundHeight(ext.fallOffX, ext.fallOffZ, oldFallOffY + kGroundProbeOffset, groundY); // @0x58223e
        if (found && ext.fallOffY < groundY - ext.fallGroundClearance) {             // @0x582263
            ++ext.fallLandCounter;                                                   // @0x582274
            ext.fallOffY = groundY - ext.fallGroundClearance;                        // @0x582289
            if (ext.fallLandCounter == 1 && host.PlayLandingSound) {                 // @0x582299
                host.PlayLandingSound(ext.fallOffX, ext.fallOffY, ext.fallOffZ);      // @0x5822b1 (position = current fallOff)
            }
        }
    }

    // --- Final FSM dispatch (switch this+6) — OUT OF SCOPE, see header. @0x5822d3 ---
    if (host.DispatchMotionTick) host.DispatchMotionTick(monsterIndex, dt);
}

// Fx_GibUpdate 0x583CD0 — NPC/effect tick (gravity + ground collision).
void TickNpcEffect(GameWorld& world, int npcIndex, float dt, const EntityLifecycleTickHost& host) {
    if (npcIndex < 0 || static_cast<size_t>(npcIndex) >= world.npcs.size()) return;
    NpcEntity& npc = world.npcs[static_cast<size_t>(npcIndex)];
    if (!npc.active) return; // guard `if (*(_DWORD*)this)` @0x583cdc

    if (!EnsureCapacity(g_NpcTickExt, npcIndex)) return;
    NpcTickExt& ext = g_NpcTickExt[static_cast<size_t>(npcIndex)];
    if (!ext.gibActive) return; // guard `if (*((_DWORD*)this+21))` @0x583ce9

    // FIXED (audit steps 5-8, 2026-07-14): raw-disassembly re-check of 0x583ce9..0x583db1 --
    // probe origin height uses posY from BEFORE this tick's update (snapshot @0x583cf4..
    // 0x583d09, read @0x583da8 before the recompute @0x583d51), not the already-integrated
    // ext.posY (minor gap: up to velY*dt ~ a few tens of units at the end of a fast fall).
    // x/z remain correct (CURRENT posX/posZ, already faithful in the previous version).
    const float oldPosY = ext.posY; // pre-update snapshot @0x583cf4-0x583d09
    ext.velY += dt * kGibGravity;      // @0x583d1e (gravity)
    ext.posX += ext.velX * dt;         // @0x583d36
    ext.posY += ext.velY * dt;         // @0x583d51
    ext.posZ += ext.velZ * dt;         // @0x583d6c
    ext.extraValue += ext.extraRate * dt; // @0x583d87

    float groundY = 0.0f;
    const bool found = host.GetGroundHeight &&
        host.GetGroundHeight(ext.posX, ext.posZ, oldPosY + kGroundProbeOffset, groundY); // @0x583dd4
    if (found && groundY >= ext.posY) { // @0x583df2
        ext.gibActive = false;           // @0x583df9
        ext.posY = groundY;              // @0x583e06
    }
}

} // namespace ts2::game

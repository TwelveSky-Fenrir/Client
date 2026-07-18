// Game/EntityManager.cpp — implementation of the entity manager.
//
// Offsets confirmed by Hex-Rays decompilation (MCP idaTs2). All offsets below
// are RELATIVE to the start of the body copied into each slot, not to the
// start of the record. Mapping recap:
//   PLAYER   record 908 bytes (g_EntityArray 0x1687234): active@+0, idHi@+4, idLo@+8,
//            timestamp@+0xC, body@+0x18. body+216 = move-state block (72 bytes) =
//            {moveVal@+0, actionState@+4, animFrame@+8, posX@+12, posY@+16,
//             posZ@+20, ... heading@+36}. => world position = body+228/232/236
//            (flt_1687330/34/38, read by Fx_HomingProjectileUpdate 0x5862d0's rendering).
//            body+252 = heading (horizontal heading, degrees) — ROTATION/ORIENTATION
//            mission 2026-07-14, CONFIRMED by convergence of CharAnimState::facingCurrentDeg
//            (entity+276 = body+252, Char_UpdateAnimationFrame 0x571880) AND Char_Draw
//            0x5805C0 (see the monster counterpart below and GameState.h::
//            PlayerEntity::heading for the full detail); see Docs/
//            TS2_ENTITY_ARRAY_DUALITY_CHECK.md.
//            body+48 = character name (char[], NUL-terminated, NAMEPLATES mission
//            2026-07-14) — confirmed by Char_DrawNameplate 0x56EF40 (this+72 = body+48,
//            `Crt_Vsnprintf(v115, "%s", this+72)`); see GameState.h::PlayerEntity::name.
//   MONSTER  record 280 bytes (dword_1766F74 0x1766F74): active@+0, idHi@+4, idLo@+8,
//            timestamp@+0xC, body@+0x10. body+4 = move-state (72 bytes); world position
//            = body+16/20/24 (unk_1766F94/98/9C); def@record+0x60; radius@record+0x64;
//            body+40 = heading (move-state+36, degrees) — DIRECTLY CONFIRMED by
//            decompiling Char_Draw 0x5805C0: `this` = &dword_1766F74[i] (no intermediate
//            render array, see Docs/TS2_ENTITY_ARRAY_DUALITY_CHECK.md §1),
//            `*((float*)this+14)` (= record+56 = body+40) injected as the Y component
//            of the rotation vector {0,heading,0} passed to SObject_DrawEx ->
//            Model_Render 0x40EBB0 ("composes the world matrix S*Rz*Ry*Rx*T", IDB role);
//            the corresponding scale vector is HARDCODED to {1,1,1} in SObject_DrawEx —
//            so THIS field is a rotation, never a scale.
//   NPC      record 152 bytes (dword_17AB534 0x17AB534): body@+0x10 (84 bytes); def@record+0x64.
//            world position = body+16/20/24 — confirmed by Hex-Rays decompilation
//            of Char_SelectAuraEffect 0x5835B0 (called at the end of Pkt_SpawnNpc
//            0x467EC0, right after the body copy): this+8/9/10 (this = base
//            record &dword_17AB534[38*i]) == record+32/36/40 == body+16/20/24,
//            same convention as the monster (body+16/20/24). Value already present
//            in the network payload, copied as-is into NpcEntity::body; no rotation
//            confirmed for this record (not added, to avoid inventing one).
#include "Game/EntityManager.h"
#include "Game/GameDatabase.h"        // GetMonsterInfo / MonsterInfo (1-based resolution)
#include "Game/StaticNpcLoader.h"
#include "Game/EntityLifecycleTick.h" // ResetMonsterTickExt/ResetNpcTickExt (see TODO below)
#include "Game/PlayerCmdController.h" // g_PlayerCmd: Player_ResetCombatState 0x50F6A0 (@0x4648f2)
#include "Game/ClientRuntime.h"       // g_Client: self mirrors (dword_1675884/1675B00) + long tail of Var/VarF
#include "Net/SendPackets.h"          // Net_SendVaultReq_207/Op23 + GlobalNetClient (Pkt_EnterWorld @0x4643d1..)
#include "Gfx/FxSetters.h"            // FxPool_* (pool dword_17D06F4) + Fx_AttachDashTrail (Char_SetupAuraFlags 0x5814F0)

#include <cstring>
#include <cmath>

namespace ts2::game {
namespace {

// ---- bounds-checked read/write helpers on a byte buffer (LE, no aliasing UB).
inline uint32_t RdU32(const uint8_t* b, size_t o) { uint32_t v; std::memcpy(&v, b + o, 4); return v; }
inline int32_t  RdI32(const uint8_t* b, size_t o) { int32_t  v; std::memcpy(&v, b + o, 4); return v; }
inline float    RdF32(const uint8_t* b, size_t o) { float    v; std::memcpy(&v, b + o, 4); return v; }
inline void     WrU32(uint8_t* b, size_t o, uint32_t v) { std::memcpy(b + o, &v, 4); }
inline void     WrI32(uint8_t* b, size_t o, int32_t  v) { std::memcpy(b + o, &v, 4); }
inline void     WrF32(uint8_t* b, size_t o, float    v) { std::memcpy(b + o, &v, 4); }

// ---- PLAYER offsets (relative to the 600-byte body).
// NB: kPMoveState / kPMoveStateLen have MOVED to Game/EntityManager.h (they are
// now shared with Game/PlayerCmdController.*, which reads/writes the same block =
// g_SelfMoveStateBlock 0x1687324). Do not redeclare them here: the header's name
// would be shadowed and the two definitions would become ambiguous.
constexpr size_t kPActionState = 220;  // move-state+4 — ex-VeryOldClient: aType (ACTION_INFO; aType/aSort interchangeable, Rosetta §7)
constexpr size_t kPAnimFrame   = 224;  // move-state+8 — ex-VeryOldClient: aFrame
constexpr size_t kPPosX        = 228;  // move-state+12 -> flt_1687330 — ex-VeryOldClient: aLocation[0] (Y/Z follow)
constexpr size_t kPPosY        = 232;  // flt_1687334
constexpr size_t kPPosZ        = 236;  // flt_1687338
constexpr size_t kPHeading     = 252;  // move-state+36 -> horizontal heading (degrees), see
                                        // GameState.h::PlayerEntity::heading for the full
                                        // decompilation proof (ROTATION/ORIENTATION mission,
                                        // 2026-07-14). SAME field as CharAnimState::facingCurrentDeg
                                        // (entity+276 = body+252).
                                        // ex-VeryOldClient: aFront (ACTION_INFO) [CONFIRMED, Rosetta §2]
constexpr size_t kPLevelCtr    = 84;   // dword_16872A0 (per-entity level counter)
constexpr size_t kPHp          = 292;  // dword_1687370 (current combat-bar AR-min)
constexpr size_t kPMp          = 300;  // dword_1687378 (current combat-bar AR-max)
constexpr size_t kPAnimId      = 272;  // anim/stun id from spawn
constexpr size_t kPStateArr    = 304;  // dword_168737C: 36 state/status ints
constexpr size_t kPStateCount  = 36;
constexpr size_t kPPartyGridX  = 556;  // dword_1687478 (op 0x91)
constexpr size_t kPPartyGridY  = 560;  // dword_168747C
constexpr size_t kPPartyPos    = 564;  // unk_1687480: 3 floats (op 0x91)
constexpr size_t kPPartyPosPad = 576;  // unk_168748C = 0.0f
constexpr size_t kPStunDur     = 592;  // dword_168749C
constexpr size_t kPName        = 48;   // character name (char[], NUL-terminated) — see
                                        // GameState.h::PlayerEntity::name for the
                                        // decompilation proof (Char_DrawNameplate 0x56EF40,
                                        // this+72 = body+48) and the note on the length.
                                        // ex-VeryOldClient: aName [CONFLICT §7 C1 resolved, name@+72].
constexpr size_t kPNameBufLen  = 16;   // read bound (see comment above)

// ---- MONSTER offsets (relative to the 80-byte body).
constexpr size_t kMMoveState   = 4;    // move-state (72 bytes)
constexpr size_t kMMoveStateLen = 72;
constexpr size_t kMPosX        = 16;   // unk_1766F94 — ex-VeryOldClient: mAction.aLocation[0]
constexpr size_t kMPosY        = 20;   // unk_1766F98
constexpr size_t kMPosZ        = 24;   // unk_1766F9C
constexpr size_t kMHeading     = 40;   // move-state[4]+36 -> horizontal heading (degrees),
                                        // see GameState.h::MonsterEntity::heading for the
                                        // direct decompilation proof (Char_Draw
                                        // 0x5805C0, ROTATION/ORIENTATION mission,
                                        // 2026-07-14).
                                        // ex-VeryOldClient: mAction.aFront [CONFIRMED, Rosetta §4]

// ---- NPC offsets (relative to the 84-byte body) — see file header banner.
constexpr size_t kNPosX        = 16;
constexpr size_t kNPosY        = 20;
constexpr size_t kNPosZ        = 24;

// ---- offsets in a MONSTER_INFO definition record (for the collision radius).
// Now carried by MonsterInfo::collDim[0]/[2] (Game/GameDatabase.h); kept here as
// a documentary anchor (Pkt_SpawnMonster 0x467B00: def[+248]=collDim[0], def[+256]=collDim[2]).
[[maybe_unused]] constexpr size_t kDefDimA = 248;
[[maybe_unused]] constexpr size_t kDefDimB = 256;

// Lookup ONLY (no allocation) — the state handlers only act if the entity
// already exists (unlike the spawn handlers, which allocate via FindOrAdd*).
PlayerEntity* FindPlayer(EntityId id) {
    for (auto& e : g_World.players)
        if (e.active && e.id == id) return &e;
    return nullptr;
}
MonsterEntity* FindMonster(EntityId id) {
    for (auto& e : g_World.monsters)
        if (e.active && e.id == id) return &e;
    return nullptr;
}
NpcEntity* FindNpc(EntityId id) {
    for (auto& e : g_World.npcs)
        if (e.active && e.id == id) return &e;
    return nullptr;
}

// Resolves a MONSTER definition via ItemDefTbl_GetRecord (misleading IDB name:
// loads MONSTER_INFO, see Gfx/ModelCache.h) -> g_World.db.monster. Returns nullptr
// if the base is loaded but the id is out of range (=> spawn rejected, as in the
// original); silent nullptr if the base isn't loaded. RESERVED for monsters
// (OnSpawnMonster): network NPCs use ResolveNpcDef below (DIFFERENT table), see
// Pkt_SpawnNpc 0x467EC0.
const uint8_t* ResolveMobDef(uint32_t id, bool& tableLoaded) {
    const DataTable& t = g_World.db.monster;
    tableLoaded = (t.count != 0);
    if (!tableLoaded) return nullptr;
    // OFF-BY-ONE FIX: the MONSTER getter 0x4C6570 is STRICTLY 1-based
    // (base+944*(id-1), rejects id<1||id>count, guards 1st dword!=0). Pkt_SpawnMonster
    // 0x467B00 passes the RAW network id (1-based, body[0]) -> GetMonsterInfo applies the
    // -1. Table loaded + invalid id => nullptr => OnSpawnMonster rejects the spawn
    // (faithful to `Record==0` in 0x467B00).
    return reinterpret_cast<const uint8_t*>(GetMonsterInfo(id));
}

// Resolves a NETWORK NPC definition (dword_17AB534) -- EXACT transcription of
// Pkt_SpawnNpc 0x467EC0: `MobDb_GetEntry(mITEM, body[0])` -> ITEM_INFO table
// (g_World.db.item), NOT MONSTER_INFO. MobDb_GetEntry 0x4C3C00 is 1-based
// (record(id-1), rejects id<1 || id>count || empty slot itemId==0) -- SAME
// semantics as GetItemInfo. All consumers of NpcEntity::def genuinely read an
// ITEM_INFO record (Game/NpcInteraction.cpp: +184 faction, +188 kind, +232/+236
// quest, +352 aggro; Game/AutoPlaySystem.cpp: +0 name via +4, +184/+188) --
// offsets all WITHIN the 436-byte ITEM_INFO record and consistent with its
// layout (Game/GameDatabase.h::ItemInfo).
const uint8_t* ResolveNpcDef(uint32_t id, bool& tableLoaded) {
    const DataTable& t = g_World.db.item;
    tableLoaded = (t.count != 0);
    if (!tableLoaded || id < 1 || id > t.count) return nullptr;
    const uint8_t* rec = t.record(id - 1); // 1-based (see MobDb_GetEntry 0x4C3C00)
    if (rec) {
        uint32_t itemId = 0;
        std::memcpy(&itemId, rec, 4);
        if (itemId == 0) return nullptr; // empty slot, like MobDb_GetEntry
    }
    return rec;
}

void ReadPlayerPos(PlayerEntity& e) {
    const uint8_t* b = e.body.data();
    e.x = RdF32(b, kPPosX);
    e.y = RdF32(b, kPPosY);
    e.z = RdF32(b, kPPosZ);
    e.heading = RdF32(b, kPHeading); // see PlayerEntity::heading (move-state+36)
}
// Character name (body+48, see GameState.h::PlayerEntity::name): C-string read
// bounded to kPNameBufLen bytes (no guaranteed out-of-bounds byte if the server
// sends a non-NUL-terminated buffer within the kPNameBufLen bytes -> no overrun,
// just a name truncated to the max length rather than invented).
void ReadPlayerName(PlayerEntity& e) {
    const uint8_t* b = e.body.data() + kPName;
    size_t len = 0;
    while (len < kPNameBufLen && b[len] != 0) ++len;
    e.name.assign(reinterpret_cast<const char*>(b), len);
}
void ReadMonsterPos(MonsterEntity& m) {
    const uint8_t* b = m.body.data();
    m.x = RdF32(b, kMPosX);
    m.y = RdF32(b, kMPosY);
    m.z = RdF32(b, kMPosZ);
    m.heading = RdF32(b, kMHeading); // see MonsterEntity::heading (move-state+36)
}
void ReadNpcPos(NpcEntity& e) {
    const uint8_t* b = e.body.data();
    e.x = RdF32(b, kNPosX);
    e.y = RdF32(b, kNPosY);
    e.z = RdF32(b, kNPosZ);
}

// Char_SetActionAnimParams 0x570E70 (switch @0x570ED5 on a1[61] = entity+244 = anim.state):
// sets hitCheckActive/hitUsesSkillTable/actionKind/actionSubKind/hitFired (idx156-160).
// MODELED SUBSET of the original function (the rest = Fx_Attach*/muzzle idx183-184/
// UI block if(!a3) 0x571635 not modeled, see comment at the call site). Table transcribed
// bit-for-bit from the binary (verified idaTs2 decompilation). // 0x570E70
//
// FIDELITY NOTE idx157 (hitUsesSkillTable, bool): the binary writes a1[157]=1 (case 5/6/7)
// or a1[157]=2 (skill case); the SOLE reader (Char_UpdateAnimationFrame 0x571880 ->
// Game/ActionStateMachine.cpp) tests it as a boolean (`if (hitUsesSkillTable)`,
// 0x571936/57194D), so 1 and 2 both read as "true". hitUsesSkillTable=true in ALL
// non-default branches (1-vs-2 distinction lost with no observable consequence).
void Char_ApplyActionAnimParams(CharAnimState& a) {
    // a1[156]=0 (@0x570E95) — unconditionally set before the switch.
    a.hitCheckActive = false;
    switch (a.state) {
    case 5: case 6: case 7:                                     // @0x570EDF (157=1)
    case 0x26: case 0x47: case 0x48: case 0x57: case 0x58:      // @0x571196 (157=2)
    case 0x27: case 0x2D: case 0x2E: case 0x33: case 0x34:      // @0x571380 (157=2)
    case 0x2A: case 0x2B: case 0x30: case 0x31:                 // @0x571498 (157=2)
    case 0x45: case 0x46: case 0x55: case 0x56:
        // 156=1, 157=1|2, 158=1, 159=1, 160=0
        a.hitCheckActive = true; a.hitUsesSkillTable = true;
        a.actionKind = 1; a.actionSubKind = 1; a.hitFired = false; break;
    case 0x2C: case 0x32: case 0x38: case 0x52:                 // @0x5715B0
    case 0x51:                                                  // @0x57156A
    case 0x53:                                                  // @0x5715F3
        // 156=1, 157=2, 158=1, 159=2, 160=0
        a.hitCheckActive = true; a.hitUsesSkillTable = true;
        a.actionKind = 1; a.actionSubKind = 2; a.hitFired = false; break;
    case 0x36: case 0x37:                                       // @0x5713C6
    case 0x39: case 0x3A: case 0x49: case 0x4A: case 0x59: case 0x5A: // @0x571452
        // 156=1, 157=2, 158=2, 159=1, 160=0
        a.hitCheckActive = true; a.hitUsesSkillTable = true;
        a.actionKind = 2; a.actionSubKind = 1; a.hitFired = false; break;
    default:                                                    // @0x570ED5 default: 156=0, rest unchanged
        break;
    }
}

// ---- op 0x10 helpers (Pkt_CharStateUpdate 0x464c10): SELF mirrors + combo resets.
//
// The dword_16758D8 block (288 bytes) is modeled by g_World.self.zoneState. The binary
// always addresses it (288-byte BSS array); so we guarantee the size before writing
// (EnterWorld normally fills it first). dword_16758D8[2*i] = zoneState[8*i], dword_16758DC[2*i] =
// zoneState[8*i+4]. // 0x46530c / 0x465326
inline void WrZoneU32(size_t off, uint32_t v) {
    auto& zs = g_World.self.zoneState;
    if (zs.size() < off + 4) zs.resize(288, 0); // dword_16758D8 = 0x120 bytes in the binary
    std::memcpy(zs.data() + off, &v, 4);
}

// "Combo counter" slots = indices 9 and 29..34 of the dword_168737C state array
// (dword_16873A0 = 168737C+4*9; dword_16873F0..1687404 = 168737C+4*29..+4*34), see the
// cross-cutting finding: these are NOT separate globals but slots of the same array.
constexpr int kComboSlots[7] = { 9, 29, 30, 31, 32, 33, 34 };

// NON-SELF: zeroes only the state slot (dword_168737C[227*v7+j]).
inline void ZeroStateSlot(uint8_t* b, int j) {
    WrU32(b, kPStateArr + 4 * j, 0);
}
// SELF (branch v7==0): ALSO zeroes the mirror pair dword_16758D8[2*j]/DC[2*j] (zoneState
// 8*j / 8*j+4) and the timestamp flt_16759F8[j] (VarF 0x16759F8+4*j), see branch @0x465469..
inline void ZeroSelfSlot(uint8_t* b, int j) {
    WrZoneU32(8 * j, 0);                       // dword_16758D8[2*j]=0
    WrZoneU32(8 * j + 4, 0);                   // dword_16758DC[2*j]=0
    g_Client.VarF(0x16759F8 + 4 * j) = 0.0f;   // flt_16759F8[j]=0.0
    WrU32(b, kPStateArr + 4 * j, 0);           // dword_168737C[227*v7+j]=0
}
// Resets all kComboSlots slots EXCEPT skipA/skipB (cases 9/29..34). self=true
// reproduces the self branch (@0x465469..) with mirror+timestamp, otherwise state only (@0x464eb0..).
inline void ResetComboSlots(uint8_t* b, bool self, int skipA, int skipB) {
    for (int j : kComboSlots) {
        if (j == skipA || j == skipB) continue;
        if (self) ZeroSelfSlot(b, j);
        else      ZeroStateSlot(b, j);
    }
}

// ---- MONSTER dash trail — Char_SetupAuraFlags 0x5814F0 (dash-trail sub-path ONLY).
// Sole caller: Pkt_SpawnMonster 0x467B00 @0x467DA6, on the NEW slot only (after
// def + radius resolution). The binary tests the model CLASS (def+244 =
// MonsterInfo::kindIndexP1) via a switch, then, if the speed state (def+236 =
// MonsterInfo::field236) is in [2,6], attaches a PARTICLE trail (type 5) to the 1st
// free slot of the dword_17D06F4 pool (< g_FxAuraCount). Two class groups:
//   {42,44,46,59,61,64,65,67,74,75,76,85,89} -> side 1 (def 18)   @0x5815e1
//   {48,53,62,66,72,81}                       -> side 2 (def 19)   @0x581652
// PRODUCER ONLY: the tick + render of type-5 slots already exist (SceneManager / FxRenderer,
// Wave D); so neither is touched here. See Gfx/FxSetters.cpp::Fx_AttachDashTrail and
// Docs/TS2_SWEEP_ENTITY_FX.md §4.
//
// FIDELITY NOTE: Char_SetupAuraFlags also zeroes aura fields of the monster record
// (this+27/53/64/66/68 = record+108/212/256/264/272), ALL beyond the modeled 80-byte body
// (def@+96, radius@+100) and with no consumer in ClientSource -> NOT reproduced (rule
// "unproven / unread = absent").

// FxEntitySource from a monster: only idHi/idLo are read by the particle path
// (a3[1]/a3[2]); the model anchor (a3[24]+244) is NOT read (see Fx_AttachDashTrail, d[30]=0).
// Local equivalent of SourceFromMonster in Net/CombatResultApply.cpp (anonymous namespace, not shared).
gfx::FxEntitySource FxSourceFromMonster(const MonsterEntity& m) {
    gfx::FxEntitySource s;
    s.idHi = m.id.hi;   // a3[1]  record+4
    s.idLo = m.id.lo;   // a3[2]  record+8
    return s;
}

// Trail side (1/2) for a model class, or 0 if none (switch @0x581570 on def+244).
int MonsterDashTrailSide(int32_t modelClass) {
    switch (modelClass) {
        case 42: case 44: case 46: case 59: case 61: case 64: case 65:
        case 67: case 74: case 75: case 76: case 85: case 89:
            return 1;                                          // def 18 (@0x5815e1)
        case 48: case 53: case 62: case 66: case 72: case 81:
            return 2;                                          // def 19 (@0x581652)
        default:
            return 0;                                          // default: no trail (@0x581657)
    }
}

// Char_SetupAuraFlags 0x5814F0 (dash-trail sub-path): attaches the trail to the
// freshly spawned monster `m`, if its class provides for it AND the speed state is in [2,6].
void AttachMonsterDashTrail(const MonsterEntity& m) {
    if (!m.def) return;                                        // gate `*(this+24)` (def resolved)
    const auto* mi = reinterpret_cast<const MonsterInfo*>(m.def);
    const int side = MonsterDashTrailSide(mi->kindIndexP1);    // *(def+244) — class switch @0x58154a
    if (side == 0) return;
    const int32_t speed = mi->field236;                        // *(def+236) — speed state
    if (speed < 2 || speed > 6) return;                        // gate @0x581590 (grp1) / @0x581601 (grp2)
    const int j = gfx::FxPool_FindFreeSlot();                  // for i<g_FxAuraCount && dword_17D06F4[64*i]
    if (j < 0) return;                                         // pool full (i==g_FxAuraCount) -> no attach
    gfx::Fx_AttachDashTrail(&gfx::FxPool_Slots()[j], FxSourceFromMonster(m), side); // @0x5815e1 / @0x581652
}

} // namespace (anonymous)

// op 0x0c — Pkt_EnterWorld: entity array reset + block copy.
void EntityManager::OnEnterWorld(const net::EnterWorld& p) {
    // RESET of all entity arrays (the original despawns slot by slot; clearing
    // the vectors is the clean equivalent — index 0 = self will be repopulated
    // by the next Pkt_SpawnCharacter).
    g_World.players.clear();
    g_World.monsters.clear();
    g_World.npcs.clear();
    g_World.groundItems.clear();
    groundPickup_.clear();

    // Copy of the two memory blocks carried by the payload:
    //   selfCharInvBlock (10088 bytes) -> g_SelfCharInvBlock (local character + inventory)
    //   zoneStateBlock   (288 bytes)   -> dword_16758D8 (zone state)
    g_World.self.charInvBlock.assign(p.selfCharInvBlock, p.selfCharInvBlock + sizeof(p.selfCharInvBlock));
    g_World.self.zoneState.assign(p.zoneStateBlock, p.zoneStateBlock + sizeof(p.zoneStateBlock));

    // Arms the EnterWorld -> InGame scene switch: faithful to dword_1676180=6, written
    // DIRECTLY by Pkt_EnterWorld in the binary (NOT a normal transition of the
    // EnterWorldFlow state machine, which only detects a fallback timeout). Consumed
    // by SceneManager::Update() (case Scene::EnterWorld), see GameState.h::GameWorld::
    // sceneEnterWorldPending and Docs/TS2_ENTERWORLD_WIRING_TODO.md.
    g_World.sceneEnterWorldPending = true;

    // TODO(scene) anchor: Pkt_EnterWorld also sets g_SceneSubState=0 (@0x46430e) and
    // dword_1676188=0 (@0x464318). g_SceneSubState is private to SceneManager (Scene/*, not
    // owned by this front) -> left to its owner (see App/PlayerInputController.cpp:21). // 0x464160

    // Growth tier dword_1675D90 = f(g_GrowthIndex) @0x464329-0x464394. The INPUT
    // g_GrowthIndex 0x1674774 is modeled by g_World.self.growthIndex (GameState.h:396;
    // the Var key 0x1674774 is DEAD, see Net/GameHandlers_Misc.cpp:422). The derived tier
    // has real consumers (UI_WishA_Open 0x600059, Pkt_ItemActionDispatch 0x477abc). // 0x464160
    {
        const int gi = g_World.self.growthIndex;
        int32_t tier;
        if      (gi < 1)   tier = 0; // @0x46432b
        else if (gi < 100) tier = 1; // @0x464340
        else if (gi < 200) tier = 2; // @0x464358
        else if (gi < 300) tier = 3; // @0x464370
        else if (gi < 400) tier = 4; // @0x464388
        else               tier = 5; // @0x464394
        g_Client.Var(0x1675D90) = tier;
    }

    // Resets @0x46439e/0x4643a8/0x4643b8 (real consumers: Pkt_SetGameVar,
    // Char_CalcExternalAttack, UI_ShareBox_* family).
    g_Client.Var(0x16760D8) = 0;                          // dword_16760D8 @0x46439e
    g_Client.Var(0x16760DC) = 0;                          // dword_16760DC @0x4643a8
    g_Client.Var(0x16760E0) = g_Client.VarGet(0x1675800); // dword_16760E0 = dword_1675800 @0x4643b8

    // Follow-up requests emitted by Pkt_EnterWorld based on dword_1675A8C (@0x4643d1/0x4643da/
    // 0x4644ad). The binary addresses g_NetClient GLOBALLY -> emission via the singleton, same
    // pattern as MapWarp/ArmFullWarp. Builders (dead code until now): Net_SendVaultReq_207
    // 0x590480, Net_SendPacket_Op23 0x4b5490. Anti-spam lock g_GmCmdCooldownLatch 0x1675B08.
    if (auto* c = ts2::net::GlobalNetClient()) {
        const int32_t warpMode = g_Client.VarGet(0x1675A8C);        // dword_1675A8C
        if (warpMode == 5) {                                        // @0x4643d1
            const int32_t tgtZone = g_Client.VarGet(0x1675A9C);     // g_TargetZoneId
            const int32_t a0      = g_Client.VarGet(0x16692A0);     // dword_16692A0
            const int32_t b0      = g_Client.VarGet(0x167588C);     // dword_167588C
            if ((tgtZone == 84 || (a0 != 2 && b0 <= 0 && a0 != 3))
                && g_Client.VarGet(0x1675B08) == 0) {               // @0x464436
                ts2::net::Net_SendVaultReq_207(*c, g_Client.VarGet(0x1675A90)); // dword_1675A90 @0x464448
                g_Client.Var(0x1675B08) = 1;                        // @0x46444d
                g_Client.VarF(0x1675B0C) = g_World.gameTimeSec;     // flt_1675B0C @0x46445d
            }
        } else if (warpMode == 8 || warpMode == 9) {               // @0x4643da / @0x4644ad
            if (g_Client.VarGet(0x1675B08) == 0) {
                ts2::net::Net_SendPacket_Op23(*c,
                    static_cast<int8_t>(g_Client.VarGet(0x1675D88)), // dword_1675D88
                    static_cast<int8_t>(g_Client.VarGet(0x1675D8C)), // dword_1675D8C
                    static_cast<int8_t>(warpMode));                  // 8 @0x464489 / 9 @0x4644c5
                g_Client.Var(0x1675B08) = 1;
                g_Client.VarF(0x1675B0C) = g_World.gameTimeSec;
            }
        }
    }

    // TODO(state) anchor: the binary ends with Player_CheckStateDigit(&g_PlayerCmdController)
    // @0x4644ea (handler return value). g_PlayerCmdController (PlayerCmdController module)
    // is neither modeled nor owned by this front -> not ported. // 0x464160
}

// op 0x0f — Pkt_SpawnCharacter: character creation/update.
PlayerEntity* EntityManager::OnSpawnCharacter(const net::SpawnCharacter& p) {
    const EntityId id{ p.idHi, p.idLo };
    PlayerEntity* existing = FindPlayer(id);

    if (!existing) {
        // --- NEW slot: full init (the body already carries position + states).
        PlayerEntity* e = g_World.FindOrAddPlayer(id);
        e->timestamp = g_World.gameTimeSec;
        std::memcpy(e->body.data(), p.body, e->body.size());
        ReadPlayerPos(*e);
        ReadPlayerName(*e);
        // Spawn-anim sequence of Pkt_SpawnCharacter 0x4646C0 (brand-new slot):
        //   Char_RefreshStatusEffectVisuals 0x570890 (@0x4648bc) then Char_SetActionAnimParams
        //   0x570E70 (@0x4648da). MODELED subset on CharAnimState (idx156-160/221);
        //   Fx_Attach*/muzzle/UI remain TODO anchor (pool g_FxPool/dword_17D06F4 not attached,
        //   no active aura at this spawn -> Fx_Attach* loops = faithful no-op).
        e->anim.state = RdI32(e->body.data(), kPActionState); // a1[61]=entity+244=body+220 (0x570ED5)
        // Anim CLIP selector = 2*weaponClass (move-state+0 = body+216 = entity+240). Arg4 of
        // Char_TickMoveState 0x574830 @0x5748e0 -> PcModel_ResolveEquipSlot 0x4E46A0 (base+19968*animSlot).
        // Without it (weaponType=0 stuck), an ARMED player played the UNARMED clip (gap G5, DEEP IDA render).
        e->anim.animSlot = RdI32(e->body.data(), static_cast<int>(kPMoveState)); // entity+240 = body+216
        // (Wave G — weapon trail) Master gate of Char_DrawWeaponEffectVariantB 0x56BF90 (@0x56c01b:
        // weaponAnimSlot(this+55=entity+220=body+196) != 0 && !altWeaponSet(this+144=entity+576=body+552)).
        // Fed at spawn like anim.state above -> otherwise resolveWeaponTrail fails the gate =
        // no trail (clean degradation). TODO(anchor): copy from the action/skill packets that
        // change the active anim (Pkt_ItemActionDispatch/skill) to reflect in-progress casts.
        e->anim.weaponAnimSlot = RdI32(e->body.data(), 196);      // entity+220 = body+196 (idx55)
        e->anim.altWeaponSet   = RdI32(e->body.data(), 552) != 0; // entity+576 = body+552 (idx144)
        e->anim.fxAuraAttachedLatch = false; // a1[221]=0 (Char_RefreshStatusEffectVisuals 0x570936)
        Char_ApplyActionAnimParams(e->anim); // switch 0x570ED5 (sets 156=0 then idx156-160)
        // TODO anchor: Fx_Attach* (0x570890/0x570E70), Char_UpdateWeaponGlowState 0x55D740,
        //   muzzle idx183/184 (case 0xC @0x570F25), UI block if(!a3) (0x571635) — not modeled.
        // If index 0 (self): the original restarts local combat + map effects and
        // sets g_SceneSubState=3; scene effects are outside the entity scope.
        if (IsSelf(e)) {
            // Player_ResetCombatState 0x50F6A0 (@0x4648f2, self only): resets the
            // combat/action block of g_PlayerCmdController. Its VISIBLE effects remain
            // covered elsewhere (play-BGM gate g_BgmEnabled 0x50F76E -> SceneManager::LoadZoneBgm;
            // Net_SendOp64 poll 0x50F746 -> host.SendPendingTargetPoll InGameTickFlow).
            //
            // The "command in flight" latch (+51600 = dword_1675B00) NOW HAS a
            // consumer: the intent layer Game/PlayerCmdController.* sets it when
            // emitting op 0x0F. The old comment "the internal reset has no ClientSource
            // consumer" is therefore STALE. This reset covers WORLD ENTRY
            // (gate `var_2B0 == 0` @0x4648df, new-slot branch); the PER-PACKET
            // acknowledgment of the current game, meanwhile, is the `mode 3 + self`
            // case further down in this same function (@0x464BF0, line ~492) — both
            // write the SAME slot (g_Client.Var(0x1675B00)), consistent with the binary
            // having only one storage location. xrefs_to(0x50F6A0) = 1 SOLE caller,
            // exactly here (@0x4648f2).
            g_PlayerCmd.ResetCombatState();
            // EQUIVALENT of the cGameData_LoadZoneNpcInfo(g_LocalPlayerSheet) call @0x4648fc
            // (guard `if (!i)` of Pkt_SpawnCharacter 0x4646C0): repopulates the current
            // zone's static decor NPCs. See Game/StaticNpcLoader.h for detail; tracking of
            // g_World.zoneId on warp is now handled by SceneManager::ReloadZone
            // (Passe 3 W2-F1), so LoadZoneNpcs sees the correct zone on self (re)spawn.
            LoadZoneNpcs(g_World.zoneId);
        }
        return e;
    }

    // --- EXISTING slot: refresh + mode logic.
    PlayerEntity* e = existing;
    e->timestamp = g_World.gameTimeSec;
    uint8_t* b = e->body.data();

    // Save the old move-state BEFORE overwrite (kept by default).
    uint8_t oldMove[kPMoveStateLen];
    std::memcpy(oldMove, b + kPMoveState, kPMoveStateLen);
    const int oldActionState = RdI32(oldMove, 4); // action-state = move-state+4

    // Copy the full new body, then restore the old move-state.
    std::memcpy(b, p.body, e->body.size());
    std::memcpy(b + kPMoveState, oldMove, kPMoveStateLen);

    const int newActionState = RdI32(p.body, kPActionState);
    const int newAnimId      = RdI32(p.body, kPAnimId);

    if (p.mode == 1) {
        // Guard: do not interrupt certain in-progress action states.
        bool skip = false;
        if (oldActionState == 11) {
            if (newActionState != 1 && newActionState != 12) skip = true;
        } else if (oldActionState == 12 && newActionState != 0) {
            skip = true;
        }
        if (!skip) {
            // Apply the new move-state (i.e. new position + anim).
            std::memcpy(b + kPMoveState, p.body + kPMoveState, kPMoveStateLen);

            // Case action-state==2: preserve the anim frame if the movement hasn't changed.
            if (newActionState == 2) {
                if (!IsSelf(e)) {
                    const int newMove0 = RdI32(p.body, kPMoveState);
                    const int oldMove0 = RdI32(oldMove, 0);
                    const int oldMove1 = RdI32(oldMove, 4);
                    if (newMove0 == oldMove0 && newActionState == oldMove1) {
                        float oldFrame; std::memcpy(&oldFrame, oldMove + 8, 4);
                        std::memcpy(b + kPAnimFrame, &oldFrame, 4);
                    }
                } else {
                    // self: fully restores the old move-state.
                    std::memcpy(b + kPMoveState, oldMove, kPMoveStateLen);
                }
            }

            // Char_SetActionAnimParams 0x570E70 REPLAYS on the EXISTING slot (mode==1) — @0x464b15,
            // after applying the new move-state and the actionState==2 block, right before the
            // stun switch. Reads the CURRENT action-state from the body (a1[61]=body+220): for
            // self+actionState==2 the move-state was restored to the old one above, so it reads the
            // old action-state — faithful to the binary. Mirrors the new-slot sequence (lines 299-301,
            // without Char_RefreshStatusEffectVisuals, which is NOT called on this branch).
            e->anim.state = RdI32(b, kPActionState);
            Char_ApplyActionAnimParams(e->anim);

            // Stun duration based on the anim id (body+272), MIRRORED to self dword_1675884
            // (@0x464b69/0x464ba0/0x464bd7) — read by UI_GameHud_Render 0x67a3c0 (3 sites).
            if ((newAnimId >= 139 && newAnimId <= 145) || (newAnimId >= 147 && newAnimId <= 149)) {
                WrI32(b, kPStunDur, 90);
                if (IsSelf(e)) g_Client.Var(0x1675884) = 90; // dword_1675884 @0x464b69
            } else if (newAnimId == 146) {
                WrI32(b, kPStunDur, 60);
                if (IsSelf(e)) g_Client.Var(0x1675884) = 60; // @0x464ba0
            } else if (newAnimId == 150) {
                WrI32(b, kPStunDur, 30);
                if (IsSelf(e)) g_Client.Var(0x1675884) = 30; // @0x464bd7
            }
        }
    } else if (p.mode == 3 && IsSelf(e)) {
        // self, local spawn: dword_1675B00=0 @0x464bf0 (latch consumed by ~20 sites —
        // Scene_InGameUpdate/UI_GameHud_ProcNet/Player_CastSkill…). Same pattern as
        // Net/GameHandlers_Entity.cpp:48 (opcode 0x15).
        //
        // THIS IS THE PER-PACKET ACKNOWLEDGMENT OF OP 0x0F (nominal path): dword_1675B00
        // == g_PlayerCmdController+51600 (0x1669170+0xC990) == the latch set by the intent
        // builders of Game/PlayerCmdController.* — i.e. the SAME slot as g_PlayerCmd.Busy(),
        // not a mirror. This write previously had NO READER; it is what unblocks the player
        // after each attack/skill.
        g_Client.Var(0x1675B00) = 0;
    }
    // mode==2: no effect on the existing slot (faithful to the original handler).

    ReadPlayerPos(*e);  // refreshes x/y/z from the current move-state.
    ReadPlayerName(*e); // the name is part of the body copied in full above (line 175).
    return e;
}

// op 0x12 — Pkt_SpawnMonster: monster creation/update.
MonsterEntity* EntityManager::OnSpawnMonster(const net::SpawnMonster& p) {
    const EntityId id{ p.idHi, p.idLo };
    MonsterEntity* existing = FindMonster(id);

    if (existing) {
        // Refresh: keeps the old move-state unless updateFlag==1.
        MonsterEntity* m = existing;
        m->timestamp = g_World.gameTimeSec;
        uint8_t* b = m->body.data();
        uint8_t oldMove[kMMoveStateLen];
        std::memcpy(oldMove, b + kMMoveState, kMMoveStateLen);
        std::memcpy(b, p.body, m->body.size());
        std::memcpy(b + kMMoveState, oldMove, kMMoveStateLen);
        if (p.updateFlag == 1)
            std::memcpy(b + kMMoveState, p.body + kMMoveState, kMMoveStateLen);
        ReadMonsterPos(*m);
        return m;
    }

    // New: definition resolution BEFORE allocation (rejected if id invalid).
    const uint32_t defId = RdU32(p.body, 0);
    bool tableLoaded = false;
    const uint8_t* def = ResolveMobDef(defId, tableLoaded);
    if (tableLoaded && !def)
        return nullptr; // unknown model id -> no spawn (faithful to the original).

    MonsterEntity* m = g_World.FindOrAddMonster(id);
    m->timestamp = g_World.gameTimeSec;
    std::memcpy(m->body.data(), p.body, m->body.size());
    m->def = def;

    // The index slot (m - monsters.data()) may have been recycled from ANOTHER
    // monster (free slot reused by FindOrAdd, see GameState.cpp): its tick
    // extension is purged (Game/EntityLifecycleTick.h) to prevent a stale
    // fallOffset/aura state from "leaking" onto this new entity -- closes the
    // precise TODO left at the top of EntityLifecycleTick.h ("to hook into
    // EntityManager::OnSpawnMonster/OnSpawnNpc").
    ResetMonsterTickExt(static_cast<int>(m - g_World.monsters.data()));

    // Collision radius: Pkt_SpawnMonster 0x467B00 computes
    // sqrt(def[+256]^2 + def[+248]^2)*0.5 (= collDim[2], collDim[0]). `def` is now
    // guaranteed = MonsterInfo* (resolved by GetMonsterInfo) -> typed access.
    if (def) {
        const auto* mi = reinterpret_cast<const MonsterInfo*>(def);
        const double s = static_cast<double>(mi->collDim[0]) * mi->collDim[0]
                       + static_cast<double>(mi->collDim[2]) * mi->collDim[2];
        m->radius = static_cast<float>(std::sqrt(s) * 0.5);
    }

    // Char_SetupAuraFlags 0x5814F0 (@0x467da6, after the collision radius): particle dash
    // trail based on model class (def+244) + speed state (def+236 in [2,6]).
    // PRODUCER ONLY — tick/render of type-5 slots are already wired (SceneManager/FxRenderer).
    // (Char_UpdateMotionState 0x5816a0, called right after @0x467dc4, belongs to move-state:
    // not ported here — untouched by this front.)
    AttachMonsterDashTrail(*m);

    ReadMonsterPos(*m);
    return m;
}

// op 0x13 — Pkt_SpawnNpc: creation / refresh / despawn (action==3).
NpcEntity* EntityManager::OnSpawnNpc(const net::SpawnNpc& p) {
    const EntityId id{ p.idHi, p.idLo };
    NpcEntity* existing = FindNpc(id);

    if (existing) {
        if (p.action == 3) {
            // Despawn: frees the slot (sub_583390).
            *existing = NpcEntity{};
            return nullptr;
        }
        // Refresh.
        existing->timestamp = g_World.gameTimeSec;
        std::memcpy(existing->body.data(), p.body, existing->body.size());
        bool loaded = false;
        existing->def = ResolveNpcDef(RdU32(p.body, 0), loaded);
        existing->action = p.action;
        ReadNpcPos(*existing);
        return existing;
    }

    if (p.action == 3)
        return nullptr; // despawn of an unknown NPC -> nothing.

    // New: definition resolution before allocation (rejected if id invalid).
    // ITEM_INFO (ResolveNpcDef = MobDb_GetEntry(mITEM), see Pkt_SpawnNpc 0x467EC0), NOT
    // the monster table.
    bool tableLoaded = false;
    const uint8_t* def = ResolveNpcDef(RdU32(p.body, 0), tableLoaded);
    if (tableLoaded && !def)
        return nullptr;

    NpcEntity* e = g_World.FindOrAddNpc(id);
    e->timestamp = g_World.gameTimeSec;
    std::memcpy(e->body.data(), p.body, e->body.size());
    e->def = def;
    e->action = p.action;
    ReadNpcPos(*e);

    // Same purge as OnSpawnMonster above (potentially recycled slot) --
    // closes the precise TODO in EntityLifecycleTick.h for the NPC branch.
    ResetNpcTickExt(static_cast<int>(e - g_World.npcs.data()));

    return e;
}

// op 0x10 — Pkt_CharStateUpdate: sets/clears 36 state bitfields of a character.
void EntityManager::OnCharStateUpdate(const net::CharStateUpdate& p) {
    // Pkt_CharStateUpdate 0x464c10. Layout: [op][idHi@8156C1][idLo@8156C5]
    //   [stateValues 288 bytes = 36 pairs @8156C9][stateFlags 144 bytes = 36 @8157E9] = 441 bytes.
    PlayerEntity* e = FindPlayer({ p.entityIdHi, p.entityIdLo });
    if (!e) return; // v7 == -1: the binary does nothing if the entity doesn't exist.

    uint8_t* b = e->body.data();
    const bool self = IsSelf(e); // if(v7)/else branch @0x464d1a: v7==0 => SELF.

    for (int i = 0; i < static_cast<int>(kPStateCount); ++i) {
        const uint32_t flag  = p.stateFlags[i];          // v8[i]
        const uint32_t val   = p.stateValues[2 * i];     // v3[2*i]
        const uint32_t extra = p.stateValues[2 * i + 1]; // v3[2*i+1]

        if (flag == 1) {
            if (self) {
                // SELF branch @0x46529e: mirror dword_16758D8/DC + timestamp BEFORE the switch.
                WrZoneU32(8 * i, val);                                   // dword_16758D8[2*i] @0x46530c
                WrZoneU32(8 * i + 4, extra);                             // dword_16758DC[2*i] @0x465326
                g_Client.VarF(0x16759F8 + 4 * i) = g_World.gameTimeSec;  // flt_16759F8[i]     @0x465339
                WrU32(b, kPStateArr + 4 * i, val);                       // dword_168737C[i]   @0x46535f
                switch (i) {
                    // Cases 15/16/17/18: the binary plays a sound (Snd3D_PlayScaledVolume, last
                    //   arg=1 in self @0x46537a/0x4653c2/0x46540a/0x465452) and sets effect
                    //   flags/timers (dword_1687598 @0x46538b, unk_168759C @0x4653a3,
                    //   dword_16875AC @0x4653e1, unk_16875B0 @0x4653f9, dword_16875B4 @0x465429,
                    //   flt_16875B8 @0x465441). These flags are WRITE-ONLY (xrefs_to = 2 refs,
                    //   both in Pkt_CharStateUpdate; unk_168759C = 0 refs) and live at
                    //   record+868..900, OUTSIDE the modeled 600-byte body -> not portable
                    //   without observable effect; audio not wired here. TODO anchor (0x464c10).
                    case 15: case 16: case 17: case 18: break;
                    case 9:  ResetComboSlots(b, true, 9, -1);  break; // @0x465469 (skip slot 9)
                    case 29: ResetComboSlots(b, true, 29, -1); break; // @0x4655a7
                    case 30: ResetComboSlots(b, true, 30, -1); break; // @0x4656e5
                    case 31: ResetComboSlots(b, true, 31, -1); break; // @0x465823
                    case 32: ResetComboSlots(b, true, 32, -1); break; // @0x465961
                    case 33: ResetComboSlots(b, true, 33, -1); break; // @0x465a9f (self: skips only 33, 6 resets)
                    case 34: ResetComboSlots(b, true, 34, -1); break; // @0x465bdd (self: skips only 34, 6 resets)
                    case 35: ZeroSelfSlot(b, 35);              break; // @0x465d14 (self-canceling: writes then clears)
                    default: break;
                }
            } else {
                // NON-SELF branch @0x464d1a.
                WrU32(b, kPStateArr + 4 * i, val); // dword_168737C[227*v7+i] @0x464d9a
                switch (i) {
                    // Sounds @0x464db5/0x464dfd/0x464e1b/0x464e63 (arg=0) + same dead flags —
                    //   see the SELF branch above (not ported, TODO anchor 0x464c10).
                    case 15: case 16: case 17: case 18: break;
                    case 9:  ResetComboSlots(b, false, 9, -1);  break; // @0x464eb0 (skip slot 9)
                    case 29: ResetComboSlots(b, false, 29, -1); break; // @0x464f46
                    case 30: ResetComboSlots(b, false, 30, -1); break; // @0x464fdc
                    case 31: ResetComboSlots(b, false, 31, -1); break; // @0x465072
                    case 32: ResetComboSlots(b, false, 32, -1); break; // @0x465108
                    // PROVEN ASYMMETRY (original bug reproduced): in NON-SELF, cases 33 and 34
                    //   skip BOTH 33 AND 34 (5 resets @0x46519a / @0x465213), whereas the SELF
                    //   branch skips only its own slot (6 resets). Do NOT fix.
                    case 33: ResetComboSlots(b, false, 33, 34); break; // @0x46519a
                    case 34: ResetComboSlots(b, false, 33, 34); break; // @0x465213
                    default: break; // no case 35 in non-self.
                }
            }
        } else if (flag == 2) {
            if (self) {
                WrZoneU32(8 * i, 0);             // dword_16758D8[2*i]=0 @0x465d4e
                WrZoneU32(8 * i + 4, 0);         // dword_16758DC[2*i]=0 @0x465d5f
                WrU32(b, kPStateArr + 4 * i, 0); // dword_168737C[227*v7+i]=0 @0x465d7c
            } else {
                WrU32(b, kPStateArr + 4 * i, 0); // @0x465289
            }
        }
    }
}

// op 0x11 — Pkt_CharStatDelta: HP/MP/level deltas (entity subset of the 36-case).
void EntityManager::OnCharStatDelta(const net::CharStatDelta& p) {
    PlayerEntity* e = FindPlayer({ p.idHi, p.idLo });
    if (!e) return;

    uint8_t* b = e->body.data();
    const bool self = IsSelf(e);

    switch (p.subOp) {
        case 1: // level gain: entity counter + self level.
            WrI32(b, kPLevelCtr, RdI32(b, kPLevelCtr) + static_cast<int32_t>(p.valA));
            if (self) g_World.self.level += static_cast<int>(p.valA);
            break;
        case 4: { // HP damage: current AR-min -= (valA+valB), floor 0.
            int32_t hp = RdI32(b, kPHp) - static_cast<int32_t>(p.valA) - static_cast<int32_t>(p.valB);
            if (hp < 1) hp = 0;
            WrI32(b, kPHp, hp);
            break;
        }
        case 8: // HP heal.
            WrI32(b, kPHp, RdI32(b, kPHp) + static_cast<int32_t>(p.valA));
            break;
        case 9: // MP heal (current AR-max).
            WrI32(b, kPMp, RdI32(b, kPMp) + static_cast<int32_t>(p.valA));
            break;
        case 23: // MP = 0.
            WrI32(b, kPMp, 0);
            break;
        case 24: // set HP.
            WrI32(b, kPHp, static_cast<int32_t>(p.valA));
            break;
        case 25: // set MP.
            WrI32(b, kPMp, static_cast<int32_t>(p.valA));
            break;
        default:
            // Other cases (attributes, money, buffs, combo counters, mounts/skills,
            // multi-field reset of case 22...): handled by StatEngine / dedicated systems.
            break;
    }

    // Reflects the combat bars into the entity's plain fields.
    e->hp = RdI32(b, kPHp);
    e->mp = RdI32(b, kPMp);
}

// op 0x91 — Net_OnPartyMemberPosition: world position of a party member.
void EntityManager::OnPartyMemberPosition(const net::PartyMemberPosition& p) {
    PlayerEntity* e = FindPlayer({ p.idHi, p.idLo });
    if (!e) return;

    uint8_t* b = e->body.data();
    // Party snapshot (dword_1687478/47C + unk_1687480/84/88, unk_168748C=0).
    WrU32(b, kPPartyGridX, p.gridX);
    WrU32(b, kPPartyGridY, p.gridY);
    WrF32(b, kPPartyPos + 0, p.pos[0]);
    WrF32(b, kPPartyPos + 4, p.pos[1]);
    WrF32(b, kPPartyPos + 8, p.pos[2]);
    WrF32(b, kPPartyPosPad, 0.0f);

    // Reflected into the plain position fields (member's up-to-date world position).
    e->x = p.pos[0];
    e->y = p.pos[1];
    e->z = p.pos[2];
}

// op 0x19 — Pkt_GroundItemRemove: decrement/removal of a pickup stack.
void EntityManager::OnGroundItemRemove(const net::GroundItemRemove& p) {
    // Pkt_GroundItemRemove 0x46a200. The GM anti-spam lock g_GmCmdCooldownLatch 0x1675B08 is
    // released for status 0 (@0x46a25a) AND status 1 (@0x46a30c); status>=2 = total no-op.
    if (p.status == 0 || p.status == 1)
        g_Client.Var(0x1675B08) = 0;

    if (p.status != 0)
        return; // status>=1: no grid removal (the latch was already handled above).

    // status==0: the binary also plays a sound (Snd3D_PlayScaledVolume flt_14891BC @0x46a26f,
    // audio not wired here -> TODO anchor), then decrements/purges the stack.
    GroundPickupSlot* s = PickupSlot(p.containerIndex, p.slotIndex);
    if (!s) return;

    // --dword_1674400[...] @0x46a29c: the count>0 guard is MANDATORY (count unsigned on the
    // C++ side, signed int on the binary side -> same final state 0 with no underflow, via
    // the <1 clamp below).
    if (s->count > 0) --s->count;
    if (s->count < 1) { // stack exhausted -> clears the cell (@0x46a2cb/e5/ff).
        s->itemId = 0;
        s->count  = 0;
        s->aux    = 0;
    }
}

GroundPickupSlot* EntityManager::PickupSlot(uint32_t containerIndex, uint32_t slotIndex) {
    if (containerIndex >= kMaxContainers || slotIndex >= kSlotsPerContainer)
        return nullptr;
    const size_t idx = static_cast<size_t>(containerIndex) * kSlotsPerContainer + slotIndex;
    if (groundPickup_.size() <= idx)
        groundPickup_.resize(idx + 1);
    return &groundPickup_[idx];
}

bool EntityManager::IsSelf(const PlayerEntity* e) const {
    return !g_World.players.empty() && e == &g_World.players[0];
}

} // namespace ts2::game

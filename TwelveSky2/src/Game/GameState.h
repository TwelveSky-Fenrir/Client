// Game/GameState.h — client runtime data model (world state + player state).
// CLEAN C++ rewrite (not byte-exact) of the entity arrays and the "self block"
// found in the disassembly. See memories ts2-entity-model / ts2-gameplay-logic
// and Docs/TS2_GAMEPLAY_LOGIC.md. Systems (StatEngine, Combat, Item, Skill,
// EntityManager…) operate on these structures; network handlers populate them.
#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <string>

namespace ts2::game {

// Forward declaration — `NpcDefRecord` (mNPC table record, 11736 bytes) is defined in
// Game/ExtraDatabases.h, which ALREADY includes this file (for DataTable): an include in the
// reverse direction would create a cycle. Since `NpcRenderEntry::def` (below) is just a
// POINTER, the incomplete type suffices; readers that dereference `def`
// (Scene/WorldRenderer.cpp) already include Game/StaticNpcLoader.h -> Game/ExtraDatabases.h.
struct NpcDefRecord;

// Network identity of an entity: u32 pair (id_hi @payload+0, id_lo @payload+4).
struct EntityId {
    uint32_t hi = 0;  // netId1 (idHi) — ex-VeryOldClient: mServerIndex (avatar/item) / mIndex (monster) [CONFIRMED, Rosetta §1]
    uint32_t lo = 0;  // netId2 (idLo) — ex-VeryOldClient: mUniqueNumber [CONFIRMED, Rosetta §1]
    bool valid() const { return hi != 0 || lo != 0; }
    bool operator==(const EntityId& o) const { return hi == o.hi && lo == o.lo; }
};

// Active buff/debuff — MINIMAL model added to drive UI/BuffStatusPanel (buff grid UI
// mission, 2026-07-14; see Docs/TS2_UI_GAMEHUD_RENDER.md §9). NO direct equivalent
// in the original stride-908 layout of dword_1687234: the binary doesn't store buffs
// as a list on the player entity, it RECOMPUTES them every frame from ~50 global variables
// belonging to disjoint, unrelated systems (elemental combos, pair synergies, skill
// loadout, guild rank, weapon gem, elemental harmony, timed debuffs dword_16758D8,
// server bonus dword_1674AB0, morph food buff, elemental mastery — cf. doc §9 points 1-14,
// none of these systems is modeled on the GameState side yet). `ActiveBuff` is therefore a
// CONVENIENCE ABSTRACTION for the C++ rewrite, not an offset mirror: it provides a single
// anchor point where future network handlers/game systems can push buff state, without
// blocking widget rendering while these ~50 sources are reverse-engineered one by one.
struct ActiveBuff {
    int   id         = 0;    // catalog id (see UI::BuffIconId in
                              // UI/BuffStatusPanel.h — 0..33 map to a .IMG icon actually
                              // resolved via static RE; any other value (including the 36
                              // slots of the timed-debuff bank §9.10, not resolved to a
                              // file) falls back to a generic colored dot)
    float expiryTime = 0.0f; // g_GameTimeSec expiry (0 = indefinite/permanent duration)
};

// ---------------------------------------------------------------------------------------
// Runtime state of the anim/action FSM (Char_UpdateAnimationFrame 0x571880) — PRIMITIVE
// MIRROR of the fields consumed by Game/ActionStateMachine.h::ActionFsm and
// Game/AnimationTick.cpp ("ANIMATION/COLLISION SYSTEM" mission, 2026-07-14). Primitives
// rather than an ActionFsm stored BY VALUE here, to avoid an include cycle
// GameState.h <-> ActionStateMachine.h <-> CombatSystem.h (CombatSystem.h is marked
// "ALREADY WRITTEN, DO NOT EDIT" and already includes GameState.h; ActionStateMachine.h
// includes CombatSystem.h — an include GameState.h -> ActionStateMachine.h would therefore
// create a cycle). Game/AnimationTick.cpp round-trips CharAnimState <-> ActionFsm every
// tick (builds a transient ActionFsm on the stack, copies these fields into it, calls its
// already-written logic, copies the result back here). Original offsets in comments
// (entity+N, cf. Game/ActionStateMachine.h for the full table of offsets already found).
// ---------------------------------------------------------------------------------------
struct FxTimerSlot {
    bool  active = false;
    float frame  = 0.0f;
};

struct CharAnimState {
    // --- Direct mirror of ActionFsm (state/timing/hit-detection) ---
    // MOVE-STATE block, 72 bytes @entity+240 (= body+216): emitted by Net_SendPacket_Op16 0x4B49F0,
    // copied verbatim into the 600-byte body at spawn (Pkt_SpawnCharacter 0x4646C0). move-state+0 =
    // animSlot (anim CLIP selector = 2*weaponClass idle / +1 recovery). Passed as `weaponType`
    // to PcModel_ResolveEquipSlot 0x4E46A0 @0x4e578e (base + 19968*animSlot + 156*actionState) ->
    // selects the WEAPON POSE motion set. Without it (weaponType=0 stuck), an ARMED player played
    // the UNARMED clip. DISTINCT from weaponAnimSlot (entity+220 = body+196 = weapon trail). // 0x574830
    int32_t animSlot          = 0;     // entity+240 = body+216 = move-state+0 (Char_TickMoveState arg4 @0x5748e0)
    int32_t state             = 0;     // entity+244 (CharActionState, "facing" mirror) — ex-VeryOldClient: aType (ACTION_INFO ; aType/aSort names interchangeable, Rosetta §7) [CONFIRMED offset]
    float   animFrame         = 0.0f;  // entity+248 — ex-VeryOldClient: aFrame [CONFIRMED, Rosetta §2]
    bool    hitCheckActive    = false; // entity+624 (idx156) — ex-VeryOldClient: mUsingSkill (tentative name) [CONFIRMED offset, Rosetta §3.5]
    bool    hitFired          = false; // entity+640 (idx160) — ex-VeryOldClient: mIsAttack [CONFIRMED offset, Rosetta §3.5]
    bool    hitUsesSkillTable = false; // entity+628 (idx157) — PLAUSIBLE (VeryOldClient: SGAttackState) — not proven in IDA [Rosetta §3.5]
    bool    altWeaponSet      = false; // entity+576 (idx144)
    int32_t weaponAnimSlot    = 0;     // entity+220 (idx55)
    int32_t lastSkillEventId  = 0;
    int32_t actionKind        = 1;     // entity+632 (idx158) — PLAUSIBLE (VeryOldClient: BAttackState) — not proven in IDA [Rosetta §3.5]
    int32_t actionSubKind     = 1;     // entity+636 (idx159) — PLAUSIBLE (VeryOldClient: RAttackState) — not proven in IDA [Rosetta §3.5]
    int32_t guardSubstate     = 0;     // entity+552
    bool    guardKeyHeld      = false; // entity+548 (input, supplied by caller)
    int32_t modelIndex        = 0;     // entity+92  (idx23) — ex-VeryOldClient: aTribe (RACE 0..2) [CONFIRMED, Rosetta §3.2]
    int32_t modelVariant      = 0;     // entity+96  (idx24) — ex-VeryOldClient: aGender (0..1) [CONFIRMED, Rosetta §3.2]
    int32_t weaponClass       = 0;     // Weapon_ClassFromField56 resolved upstream (out of scope)

    // --- Cast UI countdown (dword_1675704/1675700 global, this field = this+16/+20) ---
    float cooldownA = 0.0f; // entity+16 (idx4) — countdown tied to global dword_1675704 — PLAUSIBLE (VeryOldClient: mUpdateTimeForRageTime) — not proven in IDA [Rosetta §3.1]
    float cooldownB = 0.0f; // entity+20 (idx5) — simple, unconditional countdown — PLAUSIBLE (VeryOldClient: mUpdateTime3) — not proven in IDA [Rosetta §3.1]

    // --- Generic 10s latch (entity+748/752) — exact semantics undetermined ---
    bool  genericLatch10s      = false;
    float genericLatch10sStamp = 0.0f;

    // --- 8 secondary FX timers (entity+820..877, cf. Char_UpdateAnimationFrame
    // 0x571DE4..0x572425): indices 0..4 = "double" tables parameterized by modelIndex/
    // modelVariant (chosen based on weaponAnimSlot!=0 && !altWeaponSet), indices 5..7 = fixed
    // "simple" tables (no branching). Order = order of appearance in the binary.
    std::array<FxTimerSlot, 8> fxTimers{};

    // --- Shared "==1" pair (entity+888/892 and +896/900, same table unk_B68954) ---
    FxTimerSlot fx222{}; // idx222/223 (test ==1)
    FxTimerSlot fx224{}; // idx224/225 (test ==1)

    // --- "Infinite loop" timer (entity+572/904): resets frame to 0.0 WITHOUT ever clearing
    // the flag (unlike the others) — reproduced as-is (0x5724CC..0x572512).
    int32_t fxLoopMode  = 0;    // entity+572 (idx143), tested ==1
    float   fxLoopFrame = 0.0f; // entity+904 (idx226)

    // --- Special aura (entity+180/884) ---
    int32_t fxAuraTriggerField  = 0;     // entity+180 (idx45), tested ==2160
    bool    fxAuraAttachedLatch = false; // entity+884 (idx221)

    // --- Smoothed facial rotation (entity+276/280, 0x572531..0x572649) ---
    float facingCurrentDeg = 0.0f; // entity+276 (idx69, MUTATED at 540°/s toward facingTargetDeg) — ex-VeryOldClient: aFront [CONFIRMED, Rosetta §2]
    float facingTargetDeg  = 0.0f; // entity+280 (idx70, read only) — ex-VeryOldClient: aTargetFront [CONFIRMED, Rosetta §2]

    // --- Pending guild mark (entity+68) ---
    bool hasPendingGuildMark = false; // entity+68 (idx17), tested ==1 — ex-VeryOldClient: aGuildMarkEffect [CONFIRMED, Rosetta §3.2]
};

// Remote character/player — dword_1687234 array (stride 908, index 0 = self).
struct PlayerEntity {
    bool     active = false;              // +0 — ex-VeryOldClient: mCheckValidState [CONFIRMED, Rosetta §1/§3.1]
    EntityId id;                          // +4/+8 — ex-VeryOldClient: mServerIndex (idHi) / mUniqueNumber (idLo) [CONFIRMED, Rosetta §3.1]
    float    timestamp = 0.0f;            // +0x0C — ex-VeryOldClient: mUpdateTime [CONFIRMED, Rosetta §3.1]
    std::array<uint8_t, 600> body{};      // +0x18 appearance/equip/stats (payload 0x0F) — ex-VeryOldClient: aEquipForView (remote equip snapshot; weapon = body+148 = equipSnapshot+56, ex mEquip[7]) [CONFIRMED, Rosetta §3.3]
                                           // Partially reverse-engineered layout: see
                                           // Docs/TS2_PLAYER_BODY_MODEL.md. Resolved with
                                           // decompilation proof: body+148 (u32 LE)
                                           // = ITEM_INFO id of the equipped weapon (any
                                           // player in the array, cf. doc §2). Convergent
                                           // hint (not a direct proof):
                                           // body+68/+72 = probable job(0..2)/faction(0..1)
                                           // (doc §3bis). Still UNRESOLVED: helmet/armor/
                                           // boots/accessories (12 of the 13 probable
                                           // equip slots, doc §3) and the real attach
                                           // point for rendering the equipped body
                                           // in-game (doc TS2_ENTITY_ARRAY_
                                           // DUALITY_CHECK.md §3 — not statically
                                           // located).
    float    x = 0.0f, y = 0.0f, z = 0.0f;// pos block @+0xF0 — ex-VeryOldClient: aLocation (ACTION_INFO, move-state+12) [CONFIRMED, Rosetta §2]
    // Orientation (horizontal heading, degrees) — ROTATION/ORIENTATION mission, 2026-07-14.
    // body+252 = move-state[216]+36 (move-state block = {moveVal@+0, actionState@+4,
    // animFrame@+8, posX@+12, posY@+16, posZ@+20, ..., heading@+36}, cf. header banner
    // of Game/EntityManager.cpp). CONFIRMED by TWO independent decompilations that
    // converge bit-for-bit on the same relative offset:
    //   1) CharAnimState::facingCurrentDeg (below, entity+276 = body+252 since the
    //      body starts at entity+0x18) — Char_UpdateAnimationFrame 0x571880 MUTATES this
    //      field at 540°/s toward facingCurrentDeg->facingTargetDeg (entity+280 = body+256),
    //      with 360° wraparound — cf. Game/AnimationTick.cpp.
    //   2) Char_Draw 0x5805C0 (direct idaTs2 decompilation, this mission): for the
    //      MONSTER (this = &dword_1766F74[i] DIRECTLY, no intermediate array —
    //      cf. Docs/TS2_ENTITY_ARRAY_DUALITY_CHECK.md §1), the field `*((float*)this+14)`
    //      (= record+56 = body+40 = move-state[4]+36, SAME relative offset as the player
    //      once accounting for move-state starting at body+4 instead of body+216) is
    //      injected as the Y component of a rotation vector {0, heading, 0} passed to
    //      SObject_DrawEx -> Model_Render 0x40EBB0 (confirmed IDB role: "composes the
    //      world matrix S*Rz*Ry*Rx*T"); the corresponding scale vector is HARDCODED to
    //      {1,1,1} in SObject_DrawEx — so THIS field is indeed a rotation, never a
    //      scale. FIDELITY GAP FIXED by this mission: the old comment
    //      "this+7=angle, this+8=pos, this+14=scale" in Scene/WorldRenderer.h/
    //      Game/EntityDrawLogic.h (written without Model_Render access, MCP server
    //      saturated at the time) is WRONG — this+7 = animFrame (move-state+8), this+14 =
    //      heading (move-state+36). Not modified in EntityDrawLogic.h (just annotated) so
    //      as not to disturb ComputeBodyMeshPlacement, which remains functionally correct:
    //      it's the CALLING LAYER (WorldRenderer.cpp) that must feed facingOrAnimTimer
    //      with THIS `heading` field, not a raw byte from a separate render object.
    // Top-level mirror (same convention as x/y/z above): populated by
    // EntityManager::ReadPlayerPos-adjacent (cf. Game/EntityManager.cpp), read directly
    // by Scene/WorldRenderer.cpp for the per-entity world matrix's Y rotation.
    // DOES NOT DUPLICATE anim.facingCurrentDeg (CharAnimState, not mutated here): this
    // mirror is the most recent RAW value received from the network (immediate snap), the
    // 540°/s smoothing in CharAnimState remaining a separate system, not wired to remote
    // entities to date (cf. Game/AnimationTick.h, Char_UpdateAnimationFrame never called
    // from EntityManager/WorldRenderer).
    float    heading = 0.0f;              // body+252 = move-state+36 — ex-VeryOldClient: aFront [CONFIRMED, Rosetta §2]
    int      hp = 0, mp = 0;              // +7208 (self.hp in cGameData)

    // Character name (NAMEPLATES mission, 2026-07-14): entity+72 raw, i.e.
    // body+48 (72-24, cf. body@+0x18 convention above) = Pkt_SpawnCharacter payload
    // +56 (8 + 48, the network body starting at payload offset 8). Confirmed by
    // Hex-Rays decompilation of Char_DrawNameplate 0x56EF40: `Crt_Vsnprintf(v115, "%s",
    // this + 72)` — direct C-string read, NO other resolution step (not
    // an external table). Extracted by EntityManager::ReadPlayerName (Game/EntityManager.cpp)
    // on every Pkt_SpawnCharacter (spawn AND update, the body being copied in full).
    // Buffer length NOT confirmed with byte-exact certainty (no named struct
    // in the IDB at this offset): the next field read by 0x56EF40 after the name is
    // `element` at entity+88 (body+64), which bounds the buffer to 16 bytes max (72..87) —
    // value reused here as the max read length (cf. kPNameBufLen). Consistent with
    // the `char[13]` buffers (12 chars + NUL) seen elsewhere in the protocol
    // (Docs/TS2_PROTOCOL_SPEC.md, e.g. friendName/target_name): 16 bytes leaves headroom
    // for alignment without truncating a legitimate name.
    std::string name;                     // entity+72 (body+48) — ex-VeryOldClient: aName [CONFLICT §7 C1 resolved: IDA struct decl said name[48]@40, decompilation of Char_DrawNameplate 0x56EF40 (this+72) proves @+72 → IDA wins]

    // Minimal state added (UI buffs mission, 2026-07-14): active buffs/debuffs of the LOCAL
    // PLAYER (array index 0 = self, cf. comment above), consumed exclusively by
    // UI/BuffStatusPanel::RenderGrid(). Empty by default → the widget draws an empty grid
    // (never blocks rendering) until some upstream system feeds it.
    std::vector<ActiveBuff> buffs;

    CharAnimState anim{}; // anim/action FSM (Char_UpdateAnimationFrame), cf. above
};

// Monster — dword_1766F74 array (stride 280).
struct MonsterEntity {
    bool     active = false;              // +0 — ex-VeryOldClient: mCheckValidState [CONFIRMED, Rosetta §1/§4]
    EntityId id;                          // +4/+8 — ex-VeryOldClient: mIndex (idHi) / mUniqueNumber (idLo) [CONFIRMED, Rosetta §4]
    float    timestamp = 0.0f;            // +0x0C — ex-VeryOldClient: mUpdateTime [CONFIRMED, Rosetta §4]
    std::array<uint8_t, 80> body{};       // body[0] = mob id -> MONSTER_INFO ; +0x10 — ex-VeryOldClient: mDATA (OBJECT_FOR_MONSTER) [CONFIRMED, Rosetta §4]
    const void* def = nullptr;            // +0x60 resolved MONSTER_INFO record — ex-VeryOldClient: mMONSTER_INFO [CONFIRMED, Rosetta §4]
    float    radius = 0.0f;               // +0x64 — ex-VeryOldClient: mRadiusForSize [CONFIRMED, Rosetta §4]
    int      hp = 0;                      // +923784 (in cGameData)
    float    x = 0.0f, y = 0.0f, z = 0.0f;// record+32/36/40 (body+16/20/24) — ex-VeryOldClient: mAction.aLocation [CONFIRMED, Rosetta §4]
    // Orientation (horizontal heading, degrees) — body+40 = move-state[4]+36 (same
    // move-state layout as PlayerEntity, shifted: starts at body+4 instead of body+216 — cf.
    // header banner of Game/EntityManager.cpp). DIRECT proof (not just analogy
    // with the player): Char_Draw 0x5805C0 (idaTs2 decompilation, ROTATION/ORIENTATION
    // mission 2026-07-14) operates with `this = &dword_1766F74[i]` — THIS
    // ARRAY, confirmed with no intermediate render array by
    // Docs/TS2_ENTITY_ARRAY_DUALITY_CHECK.md §1 (xrefs_to + disassembly of the 2 call
    // sites in Scene_InGameRender). The field `*((float*)this+14)` (= record+56 =
    // body+40) is injected as the Y component of the rotation vector {0,heading,0}
    // passed to SObject_DrawEx -> Model_Render 0x40EBB0 ("composes the world matrix
    // S*Rz*Ry*Rx*T", IDB role); the corresponding scale vector is HARDCODED to
    // {1,1,1} in SObject_DrawEx — this field is therefore a rotation, never a scale
    // (cf. the detailed comment on PlayerEntity::heading above for the fidelity gap
    // fixed relative to the old WorldRenderer.h/EntityDrawLogic.h comment
    // "this+14=scale"). Populated by EntityManager (top-level mirror,
    // same convention as x/y/z), consumed by Scene/WorldRenderer.cpp.
    float    heading = 0.0f;              // record+56 (body+40 = move-state[4]+36) — ex-VeryOldClient: mAction.aFront [CONFIRMED, Rosetta §4]
    CharAnimState anim{}; // anim/action FSM (Char_UpdateAnimationFrame), cf. above
};

// NPC — dword_17AB534 array (stride 152).
struct NpcEntity {
    bool     active = false;              // +0 — ex-VeryOldClient: mCheckValidState [CONFIRMED, Rosetta §5 ; note: VeryOld places it @+4 (divergent NPC_OBJECT layout), only the ROLE is aligned, cf. §7 C3]
    EntityId id;                          // +4/+8 (idHi/idLo) — absent from the reduced VeryOld wrapper → IDA only [CONFIRMED, Rosetta §5]
    float    timestamp = 0.0f;            // +0x0C — absent from VeryOld → IDA only [CONFIRMED, Rosetta §5]
    std::array<uint8_t, 84> body{};       // body[0] = mob id -> NPC/MobDb ; +0x10 — PLAUSIBLE (VeryOldClient: nAction) — not proven in IDA [Rosetta §5]
    const void* def = nullptr;            // +0x64 — CONFLICT §7 C2: VeryOld nInfo typed NPC_INFO* @+0, IDA proves ITEM_INFO* @+100 (MobDb_GetEntry 0x4C3C00) → IDA wins (type + offset)
    uint32_t action = 0;
    // World position @body+16/20/24 (confirmed by Hex-Rays decompilation of
    // Char_SelectAuraEffect 0x5835B0, called right after the body copy in
    // Pkt_SpawnNpc 0x467EC0: this+8/9/10 == record+32/36/40 == body+16/20/24,
    // same convention as MonsterEntity::x/y/z (body+16/20/24). Already received in
    // `body` over the network (Pkt_SpawnNpc), just never extracted before this milestone.
    float    x = 0.0f, y = 0.0f, z = 0.0f;// record+32/36/40 (body+16/20/24) — ex-VeryOldClient: nAction.aLocation [CONFIRMED, Rosetta §5]
    // NO `heading` field here (ROTATION/ORIENTATION mission, 2026-07-14):
    // unlike PlayerEntity/MonsterEntity (cf. their `heading` comments),
    // NO function in the render call graph calls Char_Draw/Char_DrawReflection
    // on `dword_17AB534` (NPCs are drawn via `Npc_DrawMesh 0x57FF00` on a
    // separate RENDER array `g_NpcRenderArray` 0x1764D14, cf.
    // Docs/TS2_NPC_MESH_DRAW.md and Docs/TS2_ENTITY_ARRAY_DUALITY_CHECK.md §2) — any
    // heading field of `Npc_DrawMesh` would live on THAT OTHER array, not
    // backed by `NpcEntity`/`g_World.npcs` (same gap already documented for the NPC
    // body, cf. Scene/WorldRenderer.h). Adding a `heading` here would be an INVENTION
    // (no confirmed offset on this specific record); `StaticNpcLoader.h::angle` is an
    // UNRELATED system (static decor NPCs loaded per-zone via
    // cGameData_LoadZoneNpcInfo, not the network NPCs of this array).
    // PLAUSIBLE (VeryOldClient: NPC_OBJECT.nFront = NPC heading) — not proven in IDA: no
    // heading offset confirmed on this 152-byte record (§8 Rosetta, not anchored → not added).
};

// ---------------------------------------------------------------------------------------
// NPC — entry in the RENDER/TARGETING pool `g_NpcRenderArray` (dword_1764D14, stride 88 bytes /
// 22 dw, FIXED capacity 100 = g_NpcCount 0x1687220).
//
// WARNING: THIS POOL NEVER HELD GROUND ITEMS. It was called `GroundItem` here until
// Pass 4 / wave W7 (the "npc-array-unify" front): that name was WRONG, and the pool was
// modeled TWICE on the C++ side — this field (`GameWorld::groundItems`, NEVER populated, so
// all its consumers were dead) and the private `std::vector` in Game/StaticNpcLoader.cpp (the
// only one populated, but ignored by click/tick/targeting). The two representations are now
// MERGED here: `GameWorld::npcRenderEntries` is the single pool, StaticNpcLoader::LoadZoneNpcs()
// writes to it directly, and `game::ZoneNpcs()` is now just an accessor for it.
//
// SINGLE WRITER — cGameData_LoadZoneNpcInfo 0x5578E0 (re-proven by decompilation, W7).
// `this` = g_LocalPlayerSheet 0x1685748 (`mov ecx, offset g_LocalPlayerSheet` @0x4648F7,
// right before the `call` @0x4648FC); `this` is typed `float*` and the immediate 228723 dw
// equals 914892 bytes = 0xDF5CC, and 0x1685748 + 0xDF5CC = 0x1764D14 EXACTLY. The pool
// therefore does NOT appear in the xrefs of 0x1764D14 (`this + immediate offset` addressing):
// proof by offset computation, not by xref.
//
// PROVEN fields (the ONLY ones read/written of the 88 bytes — the rest (+8, +32..43, +48..79,
// +84..87) is not touched by any known writer/reader and is therefore NOT modeled, cf. rule
// "unproven regions"):
//   +0  def       = SkillDefTbl_GetRecord(mNPC, kindId)          @0x557946
//   +4  active    = 1 (occupied flag)                            @0x55796B
//   +12 mode      = 0                                            @0x55797F
//   +16 frameAcc  = 0.0                                          @0x557995
//   +20/24/28 x/y/z ← mZONENPCINFO 0x14AA930 +0x194   @0x5579C1/0x5579ED/0x557A19
//   +44 angle       ← mZONENPCINFO +0x644                        @0x557A42
//   +80 angleBase = *(+44)                                       @0x557A62
//
// READERS — ALL treat this pool as NPCs, never as ground items: Npc_DrawMesh 0x57FF00,
// Npc_RenderSlotTick 0x5803A0 (+ _Loop 0x580400 / _Once 0x5804A0), Scene_RayHitNpcBox
// 0x541680, World_PickEntityAtCursor 0x538AB0 (loop j, bounded by g_NpcCount, click category
// 4 -> Npc_ApproachAndInteract @0x53723F), UI_NpcWin_Open 0x5DB530. Loot bags live
// ELSEWHERE: dword_17AB534 (stride 152, click category 6 -> Npc_Interact @0x536AB9), cf.
// `GameWorld::npcs`.
// ---------------------------------------------------------------------------------------
struct NpcRenderEntry {
    // +0 — resolved mNPC record (11736 bytes, cf. Game/ExtraDatabases.h). Written
    // UNCONDITIONALLY (@0x557946), including nullptr if resolution fails: it's
    // the guard that follows (@0x557956) that decides activation.
    const NpcDefRecord* def = nullptr;
    // +4 — "slot occupied" flag (originally a DWORD 0/1). The ONLY field reset to 0 by the
    // slot constructor maybe_cGameData_ListField1Reset 0x57FE50 (`*(this+1) = 0`, called
    // 100x by cGameData_InitPools 0x5575D0) AND by destructor 0x57FE70 (`*(this+1) = 0`,
    // looped by Pkt_EnterWorld @0x464237): deactivating a slot does NOT reset ANY other
    // field to zero — residual values (def/pos/angle) are kept as-is.
    bool active = false;
    // +12 — tick mode selector, read by Npc_RenderSlotTick @0x5803BA: 0 -> _Loop
    // (0x580400), 1 -> _Once (0x5804A0), ANY OTHER value -> no-op. The SAME field serves
    // as animId in _Loop (`Model_GetWeaponEffectFrameCount(g_ModelMotionArray, def+1324 - 1,
    // *(this+3))` @0x580429): dual role carried as-is from the binary.
    int mode = 0;
    // +16 — frame accumulator: `+= dt*30.0` then wraps on frameCount
    // (@0x58043E / @0x58045F).
    float frameAcc = 0.0f;
    // +20/24/28 — world position. Originally a CONTIGUOUS Vec3: Math_Dist3D((float*)this + 5,
    // flt_1687330) @0x580483 reads it as a vec3 starting at +20.
    float x = 0.0f, y = 0.0f, z = 0.0f;
    // +44 — current displayed angle (Y rotation of the mesh, Npc_DrawMesh 0x57FF00).
    float angle = 0.0f;
    // +80 — "baseline" angle: copied INTO +44 by _Loop as soon as the distance to the
    // local player exceeds 400.0 (`*(this+11) = *(this+20)` @0x58048E).
    float angleBase = 0.0f;
};

// FIXED pool capacity — cGameData_InitPools 0x5575D0 @0x5575E9 (`*((_DWORD*)this + 1718) =
// 100` -> 0x1685748 + 1718*4 = 0x1687220 == g_NpcCount). This is NOT an occupancy counter
// (never rewritten by the loader) but a CAPACITY, which also serves as a BOUND for readers
// (World_PickEntityAtCursor loops `j < g_NpcCount`). Actual occupancy is carried by the
// `active` flag alone (+4). Consistent with mZONENPCINFO, which structurally carries 100
// entries per zone (row = 501 dw = 4 count + 100 kindId + 300 pos + 100 angle = 2004 bytes).
inline constexpr int kNpcRenderPoolCapacity = 100;

// TRANSITIONAL alias for the old, wrong name (cf. banner above), kept ONLY until
// readers outside wave W7's scope are renamed (Game/
// GroundAuraWorldObjectTick.*, Game/ItemPickupSystem.cpp, Game/AutoTargetCombatGate.*).
// DO NOT USE IN NEW CODE: this pool does not contain ground items.
using GroundItem = NpcRenderEntry;

// Zone object / resource node (mine, portal, etc.) — dword_180EEF4 array
// (stride 76 bytes / 19 dw), counter dword_1687230, fixed capacity
// 500 (cf. cGameData_InitPools 0x5575D0, pool F). Populated by Pkt_SpawnZoneObject
// (opcode 0x86); layout confirmed by Docs/TS2_PROTOCOL_SPEC.md ([SC b08]):
// +0x00 = active, +0x04/+0x08 = object id pair, +0x0C = spawn timestamp
// (flt_815180), +0x18..+0x4C = 52-byte block not decoded further to
// date. Pool DISTINCT from the projectile pool (dword_17D06F4/g_FxAuraCount,
// cf. Docs/TS2_FX_CATALOG.md) despite adjacent counters in memory
// (0x168722C then 0x1687230, juxtaposed in the cGameData block).
struct ZoneObjectEntity {
    bool     active = false;              // +0x00
    uint32_t objId1 = 0;                  // +0x04
    uint32_t objId2 = 0;                  // +0x08
    float    spawnTimestamp = 0.0f;       // +0x0C
    std::array<uint8_t, 52> body{};       // +0x18 (52 bytes, not decoded)
};

// Inventory cell (6 dwords: {itemId, gridX, gridY, flag, color/appearance, durability}).
// NO `page` field here: the "page/row" dimension of the binary's flat array g_InvMain
// [384*row + 6*col] (Pkt_ItemUpgradeResult 0x488DE0, Pkt_ItemActionDispatch
// 0x46A320, cGameHud_InvCellAt 0x64F9F0) is carried by the INDEX (row, col) in
// game::ClientRuntime::InventoryState::cells (Game/ClientRuntime.h), not by
// data on the cell itself — a `page` stored INSIDE InvCell would be a 3rd
// representation of the same dimension, redundant with the array index that
// already carries it (cf. reconciliation of the inventory models, "inventory" mission,
// 2026-07-14). row 0 = main bag (always active), row 1 = bonus page, visible
// in the UI only if g_Client.VarGet(0x16732A8) >= 1 (g_Inv_ExtraPageCount, cf.
// UI/InventoryWindow.h).
struct InvCell {
    uint32_t itemId = 0, gridX = 0, gridY = 0, flag = 0, color = 0, durability = 0;
    bool empty() const { return itemId == 0; }
};

// An equipment slot (13 slots, originally 16-byte stride).
struct EquipSlot {
    uint32_t itemId = 0;
    uint32_t socket = 0;   // bit-packed word (grade/gem/enchant)
    uint32_t extra0 = 0, extra1 = 0;
};

// Local player state — the original's "self work block" (globals 0x16731A8..).
struct SelfState {
    int level          = 1;   // g_SelfLevel 0x16731A8 — ex-VeryOldClient: OBJECT_FOR_AVATAR.aLevel1 [CONFIRMED, Rosetta §6]
    int levelBonus     = 0;   //             0x16731AC — ex-VeryOldClient: aLevel2 [CONFIRMED, Rosetta §6]
    // 4 primary attributes (suffixes = corresponding ITEM_INFO offsets).
    int attrExtForce   = 0;   // g_SelfBaseAttr292 0x16731BC
    int attrIntForce   = 0;   //               296 0x16731C4
    int attrDefensive  = 0;   //               300 0x16731B8
    int attrOffensive  = 0;   //               304 0x16731C0
    int unspentAttr    = 0;   // 0x16731D0
    int skillPoints    = 0;   // 0x16731D4
    int growthIndex    = 0;   // g_GrowthIndex 0x1674774 (= tier*100 + tier-step 1..15)
    int currency       = 0;   // g_Currency 0x1673180
    // Number of unlocked inventory BONUS pages (g_Inv_ExtraPageCount 0x16732A8,
    // written by Pkt_SetGameVar case 88, cf. Net/GameVarDispatch.cpp): NO
    // dedicated field here (a second field not written by the same handler would be a
    // new silent duplication — exactly what this mission eliminates). Read
    // game::g_Client.VarGet(0x16732A8) directly (already done by Game/QuestSystem.cpp
    // and UI/InventoryWindow.h::SetBagPage). 0 = only page 0 (8x8=64 cells) is
    // accessible; >=1 unlocks page 1 (cGameHud_OnMouseDown case 1, button
    // unk_93F88C; otherwise message StrTable005_Get(156) "no room"). The binary
    // never exposes more than 2 pages total (cf. UI/InventoryWindow.h::kMaxBagPages).
    int element        = 0;   // g_LocalElement 0x1673194
    int elementSecondary = 0; //                0x1673198
    // Self combat/action mode (self.mode, cGameData+7136): read by the self-mode
    // network reflexes (Net/CombatResultApply.cpp, Pass 3 W2-F5; the real game triggers
    // reactions when mode is in {1,5,6,7}). 0 = normal mode. Field added by MAIN (shared
    // state of wave W2) to avoid any cross-front double write on GameState.h.
    int mode = 0;
    uint32_t weaponId  = 0;   // g_LocalPlayerWeaponId 0x16731E8 — ex-VeryOldClient: mEquip[7] (weapon; = equip[7].itemId, alias dword_1673248) [CONFIRMED, Rosetta §6]

    // Local player name (13-byte NUL-terminated on the binary side, probably byte_1673184 —
    // an address also referenced elsewhere under other disputed semantics, cf. Game/
    // SkillCombat.h; do NOT assume which of the two missions is right, this field exists
    // here independently of that specific address). Needed for the "am I the leader / am I
    // the departure target" comparisons in the alliance roster
    // (Net_OnGuildMemberLeave/Kick 0x4914D0/0x4916D0, Net_OnGuildRosterUpdate case 3 — cf.
    // Docs/TS2_ALLIANCE_PARTY_ROSTER.md §3). NO handler in this module populates it yet
    // (the local name normally arrives via the game-entry/login packet, out of scope for
    // the alliance roster mission): stays empty by default -> any
    // "== localPlayerName" comparison fails cleanly (never a false "that's me"), same
    // honest degradation policy as UI/SocialWindow.cpp for unfed fields.
    std::string localPlayerName;

    // Local spawn position captured from the chosen CharSelect slot (dword_166A8E0/
    // E4/E8[2522*slot], EA 0x51c79e-0x51c7d4 of Scene_CharSelectUpdate — same offsets as
    // Net/CharSelectPackets.h::kCharRecFieldPosX/Y/Z). Consumed ONCE by the tail72
    // payload (72 bytes) of Net_SendPacket_Op12 0x4B43C0 sent from Scene_EnterWorldUpdate
    // case 2 (cf. Docs/TS2_ENTERWORLD_WIRING_TODO.md) — then overwritten by the actual
    // position received from the server at first spawn, so no conflict with normal in-game use.
    // Mirror written by UI/LoginScene.cpp::CharSelectUpdate() at the same point as zoneId
    // above (charState_.enterWorldZoneId).
    // RE-CONFIRMED (2026-07-14, fresh decompilation): in the serialized struct72, these 3
    // floats go at offsets +0x0C/+0x10/+0x14 (flt_1675AAC/B0/B4), NOT +0x00/+0x04/+0x08 —
    // see Net/CharSelectPackets.h::kTail72Off* for the full confirmed layout.
    float spawnX = 0.0f, spawnY = 0.0f, spawnZ = 0.0f;

    // Spawn angle (degrees, 0..359), rolled by the CLIENT at the same point as spawnX/Y/Z
    // above (flt_1675AC4 = Rng_Next() % 360, EA 0x51c7ed of Scene_CharSelectUpdate,
    // duplicated as-is into flt_1675AC8 right after, EA 0x51c7f9 — NOT 2 distinct angles,
    // the same value is written twice in struct72, cf. kTail72OffRotA/RotB). The
    // server doesn't validate this roll, so only the FORMULA (Rng_Next()%360, same PRNG as
    // Net/Rng.h::Rng) matters for fidelity, not the value itself. Populated by
    // UI/LoginScene.cpp::CharSelectUpdate() at the same point as spawnX/Y/Z/zoneId above.
    float spawnRotationDeg = 0.0f;

    std::array<EquipSlot, 13> equip{};   // g_EquipMain 0x16731D8 — ex-VeryOldClient: mEquip[MAX_EQUIP_SLOT_NUM] (internal offsets NOT transposed: VeryOld stores ITEM_INFO* per slot, IDA {itemId,socket,extra0,extra1} 16 bytes) [CONFIRMED, Rosetta §6]

    // Derived stats (computed by StatEngine from everything above).
    int maxHp = 0, maxMp = 0, hp = 0, mp = 0;
    int extAtk = 0, intAtk = 0, extDef = 0, intDef = 0;
    int accuracy = 0, evasion = 0, critRate = 0;
    int atkRatingMin = 0, atkRatingMax = 0, attackSpeed = 0;

    // Inventory grid: NOT HERE. The old simplified model (vector<InvCell>,
    // free x/y coordinates) was removed during the reconciliation of the two
    // competing models ("inventory" mission, 2026-07-14): it silently diverged
    // from game::g_Client.inv (InventoryState, Game/ClientRuntime.h), the ONLY model
    // written by the already-wired network handlers (Net/GameHandlers_InvCells.cpp,
    // Net/ItemActionDispatch.cpp, etc.). Confirmed bit-exact by disassembly:
    // g_InvMain/g_InvGrid_GridX/GridY/Count/Durability/InstanceSerial, addressing
    // [384*row + 6*col] (Pkt_ItemUpgradeResult 0x488DE0) and, independently,
    // [(row%100)*0x600 + (col%100)*0x18] (Pkt_ItemActionDispatch
    // 0x46A320) — both reduce to the same InventoryState::At(row,col) (kCols=64,
    // stride 6 dwords/cell). Consumers (UI/InventoryWindow.cpp, HUD, etc.)
    // must read/write game::g_Client.inv, NOT a field here.

    // Raw blocks received by Pkt_EnterWorld (to decode progressively).
    std::vector<uint8_t> charInvBlock; // g_SelfCharInvBlock (10088 bytes, payload 0x0C) — PLAUSIBLE (VeryOldClient: MYINFO.mUseAvatar / AVATAR_INFO) — not proven in IDA (VA not anchored, §8 Rosetta)
    std::vector<uint8_t> zoneState;    // dword_16758D8 (288 bytes)
};

// View over a loaded .IMG data table (fixed-stride records).
struct DataTable {
    std::vector<uint8_t> data; // raw records (already decrypted/decompressed)
    uint32_t count  = 0;
    uint32_t stride = 0;
    const uint8_t* record(uint32_t i) const {
        return (i < count) ? data.data() + static_cast<size_t>(i) * stride : nullptr;
    }
};

// Game databases (.IMG 005_* files — cf. Docs/TS2_IMG_FORMAT.md).
struct GameDatabases {
    DataTable level;   // LEVEL_INFO   (44 bytes)
    DataTable item;    // ITEM_INFO    (436 bytes)
    DataTable skill;   // SKILL_INFO   (776 bytes)
    DataTable monster; // MONSTER_INFO (944 bytes)
    DataTable socketT; // SOCKET_INFO  (20 bytes)
};

// PARTY roster (names) — C++ mirror of g_PartyRosterNames (0x1674608, 10 slots x 13 bytes,
// player name only). Populated by Net_OnPartyMemberNameSet (SC opcode 0x3e / EA 0x4909A0,
// writes names[slotIndex]) and cleared by Net_OnPartyMemberClear (SC opcode 0x40 / EA 0x490AB0),
// cf. Net/GameHandlers_PartyGuild.cpp and Docs/TS2_ALLIANCE_PARTY_ROSTER.md §2 ("PARTY ROSTER
// WIRING" mission, 2026-07-14). `slotIndex` is a roster index ASSIGNED BY THE
// SERVER (0..9) — NOTHING in the disassembly proves it matches the entity index
// of GameWorld::players (dword_1687234, which lists EVERY player entity visible nearby,
// fed by a completely different mechanism: resolution by network identity via
// PartyMemberHpSet/PartyMemberUpdate). The two arrays are therefore INDEPENDENT; UI/
// PartyWindow.cpp cross-references them best-effort by matching index for lack of a known
// join key (see dedicated comment in this file).
struct PartyRoster {
    std::array<std::string, 10> names;
};

// GUILD/ALLIANCE roster (names) — C++ mirror of g_AllianceRosterNames (0x16749B8,
// 5 slots x 13 bytes: slot 0 = leader/founder, slots 1..4 = members) + g_LocalGuildName
// (0x168740C, a string SEPARATE from the 5-member array = name of MY active guild/alliance).
// Cf. Docs/TS2_ALLIANCE_PARTY_ROSTER.md §3 ("ALLIANCE/GUILD ROSTER WIRING" mission,
// 2026-07-14) for the full disassembly of the 6 handlers that populate it:
//   Net_OnGuildRosterReset  (SC 0x4a / EA 0x4911D0): full reset (5 names + guildName).
//   Net_OnGuildMemberJoin   (SC 0x4b / EA 0x491330): inserts into the 1st free slot 1..4.
//   Net_OnGuildMemberLeave  (SC 0x4d / EA 0x4914D0): self -> reset; else remove+compact.
//   Net_OnGuildMemberKick   (SC 0x4e / EA 0x4916D0): same as Leave.
//   Net_OnGuildRosterUpdate (SC 0x4f / EA 0x4918D0): cases 1/3 -> full reset (different
//     message if slot 0 == local name, i.e. I AM the leader dissolving it); case 2 -> no
//     mutation (informational message only).
// BEFORE this mission, `GameWorld::group` (type `GroupIdentity`, now REMOVED) mapped
// `guildName` onto unk_167468A/`groupName` onto unk_1674697 — that was WRONG: these two
// addresses are actually g_PendingReqTargetName_Sub2/Sub1 (name buffer of the TARGET of an
// in-progress confirmation request, guild/faction — invite/kick), NOT an active guild/group
// name. Verdict settled and proven by cross-referenced disassembly in
// Docs/TS2_ALLIANCE_PARTY_ROSTER.md §1: both reads (Scene_InGameUpdate AND
// Net/GameHandlers_ChatSocial.cpp) operate on THE SAME pair of request buffers, not two
// different fields. The real "name of my active guild" is `AllianceRoster::guildName`
// below (g_LocalGuildName 0x168740C).
struct AllianceRoster {
    static constexpr int kMaxSlots = 5; // slot 0 = leader/founder, 1..4 = members

    std::array<std::string, kMaxSlots> memberNames{}; // g_AllianceRosterNames + 13*i
    std::string                        guildName;      // g_LocalGuildName 0x168740C

    bool Empty(int slot) const {
        return slot < 0 || slot >= kMaxSlots || memberNames[static_cast<size_t>(slot)].empty();
    }

    // True if `name` (non-empty) == memberNames[0] — slot 0 = leader/founder (cf. §3: "slot
    // 0 is treated specially — compared against the local player name in several
    // handlers — consistent with slot 0 = alliance leader/founder").
    bool IsLeader(const std::string& name) const {
        return !name.empty() && memberNames[0] == name;
    }

    // Net_OnGuildMemberJoin (0x491330): loops i=1;i<5, first empty slot. Slot 0 (leader) is
    // NEVER reassigned via this path. Returns false if slots 1..4 are all occupied
    // (full roster — original behavior unspecified in this case, no mutation here).
    bool AddMember(const std::string& name) {
        for (int i = 1; i < kMaxSlots; ++i) {
            if (memberNames[static_cast<size_t>(i)].empty()) {
                memberNames[static_cast<size_t>(i)] = name;
                return true;
            }
        }
        return false;
    }

    // Removes `name` from the roster (search 0..4, cf. original TODO "remove nm from
    // g_AllianceRosterNames[0..5]"): if found at slot 0 (the leader itself, an edge case not
    // expected via this path — the leader's departure normally goes through the full
    // dissolution of GuildRosterUpdate case 1/3), full reset as a precaution; otherwise
    // compacts slots 1..4 (shifts everything after down by one, slot 0 never shifted).
    // Compaction algorithm NOT disassembled in fine detail in this mission (§3: "not
    // decompiled in detail") — reproduced here in a reasonable way, to be corrected if it
    // is ever precisely disassembled. Returns false if `name` is absent from the roster (no-op).
    bool RemoveMember(const std::string& name) {
        for (int i = 0; i < kMaxSlots; ++i) {
            if (memberNames[static_cast<size_t>(i)] != name) continue;
            if (i == 0) { Reset(); return true; }
            for (int j = i; j < kMaxSlots - 1; ++j)
                memberNames[static_cast<size_t>(j)] = memberNames[static_cast<size_t>(j + 1)];
            memberNames[static_cast<size_t>(kMaxSlots - 1)].clear();
            return true;
        }
        return false;
    }

    // Full reset (5 names + guildName) — Net_OnGuildRosterReset (0x4a) and
    // Net_OnGuildRosterUpdate (0x4f) cases 1/3, cf. §3.
    void Reset() {
        for (auto& n : memberNames) n.clear();
        guildName.clear();
    }
};

// Global game world state (equivalent of the original arrays/globals).
struct GameWorld {
    SelfState                  self;
    std::vector<PlayerEntity>  players;
    std::vector<MonsterEntity> monsters;
    std::vector<NpcEntity>     npcs;
    // g_NpcRenderArray dword_1764D14 — NPC render/targeting pool, 100 FIXED slots (cf.
    // NpcRenderEntry + kNpcRenderPoolCapacity). Sized by GameData_InitPools
    // (== cGameData_InitPools 0x5575D0, SOLE owner of the capacity); populated by
    // Game/StaticNpcLoader.cpp::LoadZoneNpcs (== cGameData_LoadZoneNpcInfo 0x5578E0, SOLE
    // writer), itself triggered from EntityManager::OnSpawnCharacter's self branch
    // (== `if (!i)` guard of Pkt_SpawnCharacter @0x4648E6). Ex-`groundItems` (wrong name,
    // never populated) — renamed/merged by wave W7, "npc-array-unify" front.
    // INACTIVE slots remain present (stable index, aligned with mZONENPCINFO[i]): every
    // reader MUST test `active` before use.
    std::vector<NpcRenderEntry> npcRenderEntries;
    // GROUND ITEM (loot bag) pool — dword_17AB534 in the binary (stride 152 bytes,
    // click category 6, Scene_RayHitItemModel 0x5418B0). DISTINCT CONCEPT from the NPC pool
    // above: wave W7 "npc-array-unify" proved that g_NpcRenderArray 0x1764D14 carries
    // ONLY NPCs, never loot. This real ground-item pool is NOT yet
    // modeled (152-byte structure not ported); it stays EMPTY — so ItemPickupSystem
    // (FindNearestGroundItem 0x539EC0 and neighbors) remains inert, which is FAITHFUL as
    // long as the source pool isn't wired up. Provisional type NpcRenderEntry: only
    // active/x/y/z are read by current consumers, all null on an empty pool.
    std::vector<NpcRenderEntry> groundItems;
    std::vector<ZoneObjectEntity> zoneObjects; // dword_180EEF4, cf. struct comment
    GameDatabases              db;
    PartyRoster                partyRoster;    // g_PartyRosterNames (10 names), cf. PartyRoster comment above
    AllianceRoster             allianceRoster; // g_AllianceRosterNames + g_LocalGuildName, cf. AllianceRoster comment above
    int   zoneId       = 0;
    float gameTimeSec  = 0.0f;   // g_GameTimeSec

    // Pkt_EnterWorld (op 0x0c) JUST ARRIVED: in the binary, this packet writes
    // g_SceneMgr.sceneId=6/subState=0 (InGame) DIRECTLY — cf. Docs/TS2_PROTOCOL_SPEC.md
    // section "0x0c Pkt_EnterWorld", line "writes: ... dword_1676180=6". The scene
    // switch is NOT a normal exit of the EnterWorldFlow state machine (which only
    // detects a fallback TIMEOUT, cf. Game/EnterWorldFlow.h). EntityManager::
    // OnEnterWorld() (Game/EntityManager.cpp) arms this flag — it has no access itself to
    // ts2::SceneManager (network -> scene coupling deliberately avoided at this level, cf.
    // its "out of entity scope" note). It is SceneManager::Update() (case Scene::
    // EnterWorld) that must consume it (test + reset + Change(Scene::InGame)) EVERY
    // frame, WITH PRIORITY over EnterWorldFlow_Update — cf. Docs/TS2_ENTERWORLD_WIRING_TODO.md
    // for the exact wiring on the SceneManager.cpp side (read-only file for this
    // mission, wiring to be applied manually).
    bool  sceneEnterWorldPending = false;

    // Request to RETURN to Scene::EnterWorld to RELOAD the zone (warp / op 0x18
    // Pkt_GameServerConnectResult 0x469CF0): InGame -> EnterWorld direction, inverse mirror
    // of sceneEnterWorldPending. Consumed by SceneManager::Update case Scene::EnterWorld
    // (re-entrant ReloadZone, Pass 3 W2-F1). g_TargetZoneId 0x1675A9C / Scene_EnterWorldUpdate 0x52BFF0.
    // Fields added by MAIN (shared state of wave W2) — GameState.h stays read-only
    // for all W2 fronts (no double write).
    bool  sceneReloadPending = false;
    int   pendingWarpZoneId  = -1;   // reload target zone (g_TargetZoneId 0x1675A9C), -1 = none

    // Entity slot lookup/allocation by network identity (handler behavior:
    // linear scan, else 1st free slot).
    PlayerEntity*  FindOrAddPlayer(EntityId id);
    MonsterEntity* FindOrAddMonster(EntityId id);
    NpcEntity*     FindOrAddNpc(EntityId id);
    PlayerEntity&  Self() { return players.empty() ? (players.emplace_back(), players[0]) : players[0]; }
};

// Single global instance.
inline GameWorld g_World;

} // namespace ts2::game

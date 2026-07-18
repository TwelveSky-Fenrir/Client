// Game/MiscManagers.cpp — implementation of the 5 misc managers (see MiscManagers.h
// for the EA <-> function mapping table and behavior summary).
#include "Game/MiscManagers.h"
#include "Game/GameState.h"
#include "Core/Log.h"
#include <cmath>
#include <cstddef> // std::size_t (pool sizing)
#include <limits>

namespace ts2::game {

// mPOINTER — CursorSet::LoadResources — faithful to CursorSet_LoadResources 0x4C0FA0.
// Original decompilation (this = &unk_8E714C):
//   *this = 0;
//   *(this+1) = LoadCursorA(hInstance, (LPCSTR)0x66);
//   *(this+2) = LoadCursorA(hInstance, (LPCSTR)0x67);
//   *(this+3) = LoadCursorA(hInstance, (LPCSTR)0x68);
//   *(this+4) = LoadCursorA(hInstance, (LPCSTR)0x69);
//   *(this+5) = LoadCursorA(hInstance, (LPCSTR)0x6A);
//   *(this+6) = LoadCursorA(hInstance, (LPCSTR)0x6B);
//   *(this+7) = LoadCursorA(hInstance, (LPCSTR)0x6C);
//   *(this+8) = LoadCursorA(hInstance, (LPCSTR)0x75);
//   *(this+9) = LoadCursorA(hInstance, (LPCSTR)0x77);
//   for (i = 0; i < 9; ++i) if (!*(this+i+1)) return 0;
//   return 1;
// hInstance is the module's global HINSTANCE (`hInstance` @ 0x815578, set
// by WinMain 0x4609C0). The 9 resource IDs (0x66..0x6C, 0x75, 0x77) are
// RT_GROUP_CURSOR embedded in the original .exe (not files).
bool CursorSet::LoadResources(HINSTANCE hInstance) {
    state = 0;

    slot66 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x66));
    slot67 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x67));
    slot68 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x68));
    slot69 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x69));
    slot6A = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x6A));
    slot6B = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x6B));
    slot6C = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x6C));
    slot75 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x75));
    slot77 = LoadCursorA(hInstance, MAKEINTRESOURCEA(0x77));

    const HCURSOR* const all[9] = { &slot66, &slot67, &slot68, &slot69,
                                     &slot6A, &slot6B, &slot6C, &slot75, &slot77 };
    for (const HCURSOR* h : all) {
        if (*h == nullptr) {
            TS2_ERR("CursorSet::LoadResources : une ressource RT_GROUP_CURSOR "
                     "manque dans le .rc (l'original chargeait ids 0x66..0x6C,0x75,0x77)");
            return false;
        }
    }
    return true;
}

// CursorSet_DestroyAll 0x4C10B0 (App_Shutdown, mPOINTER teardown) — see MiscManagers.h.
void CursorSet::DestroyAll() {
    state = 0;
    HCURSOR* const all[9] = { &slot66, &slot67, &slot68, &slot69,
                               &slot6A, &slot6B, &slot6C, &slot75, &slot77 };
    for (HCURSOR* h : all) {
        if (*h) {
            DestroyIcon(reinterpret_cast<HICON>(*h));
            *h = nullptr;
        }
    }
}

// CursorSet::AnimateTick — faithful to Cursor_AnimateTick 0x4C1140 (see MiscManagers.h
// for the full mechanism). Original decompilation:
//   HCURSOR __thiscall Cursor_AnimateTick(_DWORD *this) {
//       return SetCursor((HCURSOR)*(this + *this + 1));
//   }
// this[0] = state (active index), this[1..9] = the 9 HCURSOR — indexing
// `this + *this + 1` = this[state + 1], reproduced below via the same
// pointer array as LoadResources()/DestroyAll(). No clamp here in
// the original: `state` is already guaranteed to be in [0,8] by Util_SetClampedU8Field
// on the write side (SetActiveSlot()) — a defensive clamp is added to never
// read out of bounds if a future caller wrote `state` directly.
HCURSOR CursorSet::AnimateTick() const {
    const HCURSOR* const all[9] = { &slot66, &slot67, &slot68, &slot69,
                                     &slot6A, &slot6B, &slot6C, &slot75, &slot77 };
    const int32_t idx = (state >= 0 && state <= 8) ? state : 0;
    return SetCursor(*all[idx]);
}

// CursorSet::SetActiveSlot — faithful to Util_SetClampedU8Field 0x4C1110 applied to
// dword_8E714C (the `state` field): *this = a2 if a2 <= 8, no effect otherwise.
// Original decompilation (verified wave W10):
//   unsigned int *__thiscall Util_SetClampedU8Field(unsigned int *this, unsigned int a2)
//   { if (a2 <= 8) { *this = a2; return this; } return result; }  // result uninitialized
// The clamp is ALREADY complete here: do NOT add a second one.
bool CursorSet::SetActiveSlot(uint32_t idx) {
    if (idx > 8) return false;
    state = static_cast<int32_t>(idx);
    return true;
}

// Cursors() — single instance, mirror of the global dword_8E714C 0x8E714C (see the
// declaration comment in MiscManagers.h for the "sole target" proof
// by counting: 157 Util_SetClampedU8Field sites + 4 lifecycle sites
// (WinMain @0x461636, App_Init @0x461F8B, App_Shutdown @0x462587,
// CrtInit_CursorSetThunk @0x7A51F3) = 161 = xrefs_to(0x8E714C)).
// Local static: constructed on first access, destroyed at process end —
// functional equivalent of the original binary's .bss.
CursorSet& Cursors() {
    static CursorSet s_cursors;
    return s_cursors;
}

// mMYINFO — Player_ResetAnimState — faithful to Player_ResetAnimState 0x50F520.
// Original decompilation (this = float* = &g_PlayerCmdController 0x1669170):
//   *this = 0.0;                       // offset dword 0    (float)
//   *(this+1) = g_GameTimeSec;         // offset dword 1    (float, timestamp)
//   *(this+12870) = 0.0;               // offset dword 12870
//   *(this+13286) = 0.0;               // offset dword 13286
//   *(this+13287) = 0.0;               // offset dword 13287
//   *(this+13288) = 0.0;               // offset dword 13288
//   *(this+13289) = 0.0;               // offset dword 13289
//   *(this+13290) = 0.0;               // offset dword 13290
//   Crt_Memset(this+13291, 0, 20);     // offsets dword 13291..13295 (5 floats)
//   *(this+13314) = NAN;               // offset dword 13314 (sentinel)
//   return 1;
// Scattered fields of a very large block not yet modeled in ClientSource;
// reproduced here by float index to stay faithful without inventing a
// struct. `playerCmdController` must point to a buffer of AT LEAST
// 13315 floats (53260 bytes) — actual size of the original block unknown
// beyond this point (no other write observed in this function).
void Player_ResetAnimState(float* playerCmdController, float gameTimeSec) {
    float* const p = playerCmdController;

    p[0]     = 0.0f;
    p[1]     = gameTimeSec;
    p[12870] = 0.0f;
    p[13286] = 0.0f;
    p[13287] = 0.0f;
    p[13288] = 0.0f;
    p[13289] = 0.0f;
    p[13290] = 0.0f;
    p[13291] = 0.0f;
    p[13292] = 0.0f;
    p[13293] = 0.0f;
    p[13294] = 0.0f;
    p[13295] = 0.0f;
    p[13314] = std::numeric_limits<float>::quiet_NaN();
}

// mPLAY — GameData_InitPools — faithful to cGameData_InitPools 0x5575D0.
// Original decompilation (this = &g_LocalPlayerSheet 0x1685748):
//   *(this+1717) = 1000;   for (i=0;   i<1000; ++i) sub_55D6F0(this + 227*i + 1723);
//   *(this+1718) = 100;    for (j=0;   j<100;  ++j) sub_57FE50(this +  22*j + 228723);
//   *(this+1719) = 1000;   for (k=0;   k<1000; ++k) sub_580530(this +  70*k + 230923);
//   *(this+1720) = 1000;   for (m=0;   m<1000; ++m) sub_583370(this +  38*m + 300923);
//   *(this+1721) = 1000;   for (n=0;   n<1000; ++n) sub_5841F0(this +  64*n + 338923);
//   *(this+1722) = 500;    for (ii=0;  ii<dword_1687230(=this+1722); ++ii)
//                                          sub_583F50(this +  19*ii + 402923);
//   return 1;
//
// Absolute address check (base = 0x1685748) — CONFIRMS that the 6
// pools are EXACTLY the entity arrays documented in
// Game/GameState.h (computation: base + dword_index*4):
//   pool A  this+1723    -> 0x1687234  == dword_1687234 (players,      stride 908 B / 227 dw) [DataTable: g_World.players,     N=1000]
//   pool B  this+228723  -> 0x1764D14  == dword_1764D14 (NPC render,   stride  88 B /  22 dw) [DataTable: g_World.npcRenderEntries, N=100]
//   pool C  this+230923  -> 0x1766F74  == dword_1766F74 (monsters,     stride 280 B /  70 dw) [DataTable: g_World.monsters,    N=1000]
//   pool D  this+300923  -> 0x17AB534  == dword_17AB534 (NPCs,         stride 152 B /  38 dw) [DataTable: g_World.npcs,        N=1000]
//   pool E  this+338923  -> 0x17D06F4  == dword_17D06F4 (projectiles,  stride 256 B /  64 dw), N=1000 (=g_FxAuraCount 0x168722C)
//   pool F  this+402923  -> 0x180EEF4  == dword_180EEF4 (zone objects), stride  76 B /  19 dw,  N=500  (=dword_1687230)
//   (pool E counter = this+1721 -> 0x168722C == g_FxAuraCount; pool F counter = this+1722 -> 0x1687230 == dword_1687230;
//    the two counters are ADJACENT in memory but govern DISTINCT pools)
//
// Pool E/F identification (resolved — "aura/world-objects" mission, 2026-07-14):
//  - Pool E (0x17D06F4, N=1000, counter g_FxAuraCount) = SoA pool of ATTACK
//    PROJECTILES, allocated by Fx_SpawnAttackProjectile(Alt) 0x582530/0x582A10 (loop
//    `for (i=0; i<g_FxAuraCount && dword_17D06F4[64*i]; ++i)` = search for the 1st
//    free slot, bound = g_FxAuraCount), updated 1x/frame by
//    Fx_HomingProjectileUpdate 0x5862D0 (called from Scene_InGameUpdate). Pool
//    ALREADY CATALOGED in detail in Docs/TS2_FX_CATALOG.md (~30 parallel arrays
//    dword_17D06F4..dword_17D07D4: state/type/subtype, source/target ids, start/target
//    pos xyz, velocity, homing flag, weapon motion). NOT an "aura" pool
//    in the buff/debuff sense — the hook name GetFxAuraCount (InGameTickFlow.h) actually
//    refers to this homing-projectile pool; no dedicated GameState.h container
//    is added here (already documented elsewhere, per mission instruction "no new
//    code needed").
//  - Pool F (0x180EEF4, N=500, counter dword_1687230) = pool of ZONE OBJECTS /
//    resource nodes (mine, portal, etc.), populated by the network handler
//    Pkt_SpawnZoneObject (opcode 0x86, EA 0x4680F0) and read by World_PickEntityAtCursor
//    0x538AB0, World_IsPositionOccupied 0x541DD0, Scene_PickResourceNodeAtScreen
//    0x541510. Layout confirmed by Docs/TS2_PROTOCOL_SPEC.md ([SC b08]): stride 19
//    dwords = active, objId1, objId2, spawn timestamp (float), then 52 B of raw
//    data. Pool DISTINCT from pool E despite the memory adjacency of the two counters
//    (0x168722C then 0x1687230) — see also TS2_SUBSYSTEM_MAP.md ("resource nodes").
//    Modeled below via ZoneObjectEntity (Game/GameState.h, g_World.zoneObjects).
//
// The small per-slot constructors (sub_55D6F0/57FE50/580530/583370/
// 5841F0+sub_6A6FE0/583F50) ONLY zero-initialize 1 to 4 fields per
// slot (NOT a full slot memset) — the functional equivalent is
// an "empty/inactive" slot by default, which the default
// constructors of PlayerEntity/NpcRenderEntry/MonsterEntity/NpcEntity/
// ZoneObjectEntity (active=false, id={0,0}, ...) in GameState.h already provide.
// We thus reproduce the net effect (fixed capacity + empty slots) by
// resizing g_World, rather than duplicating a raw memory layout that
// GameState.h has deliberately replaced with clean types.
//
// FIX Pass 4 / wave W7 ("npc-array-unify" front): pool B (0x1764D14) was
// commented here as "ground items" and modeled by `g_World.groundItems` — that was WRONG. This pool
// is the NPC RENDER/TARGETING array (g_NpcRenderArray): its sole writer
// cGameData_LoadZoneNpcInfo 0x5578E0 copies the per-zone static NPC table mZONENPCINFO into it,
// and ALL its readers treat it as NPCs (Npc_DrawMesh 0x57FF00, Npc_RenderSlotTick 0x5803A0,
// Scene_RayHitNpcBox 0x541680, World_PickEntityAtCursor 0x538AB0 click category 4 ->
// Npc_ApproachAndInteract, UI_NpcWin_Open 0x5DB530). Loot bags live in pool D
// (dword_17AB534, click category 6). Renamed `g_World.npcRenderEntries`; its original
// slot ctor `sub_57FE50` (== maybe_cGameData_ListField1Reset 0x57FE50) ONLY sets `*(this+1)`
// (= +4, occupied flag) to 0 — exactly what `NpcRenderEntry{}` does (active=false), the rest
// of the 88 B untouched by this ctor.
//
// Pool E: still WITHOUT a container here (see identification above — the
// pool is already modeled/documented on the FX side, runtime wiring remains a
// separate mission). Pool F: container added (g_World.zoneObjects).
bool GameData_InitPools() {
    g_World.players.assign(1000, PlayerEntity{});
    // Pool B — g_NpcRenderArray 0x1764D14, NPC render/targeting (see W7 fix above).
    // `*(this+1718) = 100` @0x5575E9 -> g_NpcCount 0x1687220: FIXED capacity (never rewritten
    // by the loader; also the BOUND used by readers, see World_PickEntityAtCursor
    // `j < g_NpcCount`). This site is the SOLE OWNER of this capacity on the C++ side —
    // StaticNpcLoader::LoadZoneNpcs() never resizes the pool, it only writes in place.
    g_World.npcRenderEntries.assign(static_cast<std::size_t>(kNpcRenderPoolCapacity),
                                    NpcRenderEntry{}); // @0x5575E9
    g_World.monsters.assign(1000, MonsterEntity{});
    g_World.npcs.assign(1000, NpcEntity{});
    g_World.zoneObjects.assign(500, ZoneObjectEntity{});

    TS2_LOG("GameData_InitPools : pools joueurs=1000 pnj_rendu=100 monstres=1000 "
            "pnj=1000 objets_zone=500 (pool projectiles 17D06F4/g_FxAuraCount N=1000 "
            "deja catalogue Docs/TS2_FX_CATALOG.md, non modelise ici, cf. commentaire)");
    return true;
}

// cGameData_DestroyPools 0x557780 (App_Shutdown, mPLAY teardown) — see MiscManagers.h.
// Clears the 5 modeled pools (exact mirror of the 5 pools filled by GameData_InitPools
// above; pool E "projectiles" 0x17D06F4 not modeled, see InitPools comment).
// clear() + shrink_to_fit() to reproduce the binary's "free" INTENT (GlobalFree-
// like), rather than a plain clear() which would keep the reserved capacity.
bool GameData_DestroyPools() {
    g_World.players.clear();     g_World.players.shrink_to_fit();
    // Pool B — ex-`groundItems`, renamed by W7 (see InitPools fix above).
    g_World.npcRenderEntries.clear(); g_World.npcRenderEntries.shrink_to_fit();
    g_World.monsters.clear();    g_World.monsters.shrink_to_fit();
    g_World.npcs.clear();        g_World.npcs.shrink_to_fit();
    g_World.zoneObjects.clear(); g_World.zoneObjects.shrink_to_fit();

    TS2_LOG("GameData_DestroyPools : pools joueurs/pnj_rendu/monstres/pnj/objets_zone vides.");
    return true;
}

} // namespace ts2::game

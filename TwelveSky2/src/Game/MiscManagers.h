// Game/MiscManagers.h — groups 5 misc managers from the App_Init 0x461C20 sequence:
// mTRANSFER, mPOINTER, mUTIL, mMYINFO, mPLAY (in this call order in the binary).
//
// FAITHFUL rewrite, ground truth = TwelveSky2.exe disassembly (imagebase 0x400000).
// Original function -> function below mapping:
//   sub_4B43A0              0x4B43A0  -> Transfer_InitNoOp()         (mTRANSFER)
//   CursorSet_LoadResources 0x4C0FA0  -> CursorSet::LoadResources()  (mPOINTER)
//   sub_53F2B0              0x53F2B0  -> Util_InitNoOp()             (mUTIL)
//   Player_ResetAnimState   0x50F520  -> Player_ResetAnimState()     (mMYINFO)
//   cGameData_InitPools     0x5575D0  -> GameData_InitPools()        (mPLAY)
//
// Exact sequence in App_Init (0x461f64..0x462405), one manager after another,
// each must return true for init to continue:
//   ... Net_InitPacketHandlers -> sub_4B43A0(&unk_846C08)
//       -> CursorSet_LoadResources(&unk_8E714C)
//       -> Dict001_Load / Tips002_Load / ...
//       -> sub_53F2B0(&unk_1685740)
//       -> ... Player_ResetAnimState(&g_PlayerCmdController)
//       -> cSceneMgr_Init -> ... -> cGameData_InitPools(&g_LocalPlayerSheet)
//       -> g_GameTimeSec = Terr...
//
// WHAT EACH MANAGER ACTUALLY DOES (exact findings from decompilation)
// mTRANSFER (sub_4B43A0) and mUTIL (sub_53F2B0) are PURE NO-OPS in this
// build: Hex-Rays reduced each function to `return 1;` — the `this`
// parameter (&unk_846C08 and &unk_1685740 respectively) is never read or
// written. These are extension points present in the init sequence
// but never implemented in the shipped binary (or stripped at final
// compilation). Reproduced here as documented no-ops — NO fabricated logic.
//
// mPOINTER (CursorSet_LoadResources) loads 9 Win32 cursors EMBEDDED in
// the executable's RESOURCES (RT_GROUP_CURSOR, NOT an external .cur/.ani
// file) via LoadCursorA(hInstance, MAKEINTRESOURCE(id)), direct id in
// [0x66..0x6C] then {0x75, 0x77}. Returns false if EVEN ONE LoadCursorA fails.
//
// mMYINFO (Player_ResetAnimState) resets a handful of scattered fields
// (NOT a full memset) of a very large "player command controller" block
// (g_PlayerCmdController, 0x1669170): current timestamp, 6 floats
// set to 0, and a NaN flag — see the offset table in the .cpp.
//
// mPLAY (cGameData_InitPools) sets the FIXED CAPACITIES of 6 entity pools
// and zero-initializes a small number of fields per slot. This is
// the ORIGINAL INITIALIZER of the entity arrays already modeled (as
// dynamic std::vector) in Game/GameState.h: the absolute addresses of
// the first 5 pools EXACTLY match the globals already documented there
// (dword_1687234 players, dword_1764D14 ground items, dword_1766F74
// monsters, dword_17AB534 NPCs, dword_180EEF4 zone objects) — verified by
// address computation (see .cpp). The 6th pool (E, 0x17D06F4, counter
// g_FxAuraCount) is the ATTACK PROJECTILE pool already cataloged in
// Docs/TS2_FX_CATALOG.md (Fx_SpawnAttackProjectile/Fx_HomingProjectileUpdate);
// intentionally NOT duplicated here (see .cpp).
#pragma once

#include <windows.h>
#include <cstdint>

namespace ts2::game {

// mTRANSFER — sub_4B43A0 0x4B43A0.
// Confirmed no-op (the binary never touches &unk_846C08). Always true.
inline bool Transfer_InitNoOp() { return true; }

// mUTIL — sub_53F2B0 0x53F2B0.
// Confirmed no-op (the binary never touches &unk_1685740). Always true.
inline bool Util_InitNoOp() { return true; }

// mPOINTER — CursorSet_LoadResources 0x4C0FA0.
// Original block: global at unk_8E714C, 10 dwords = { state(=0), 9×HCURSOR }.
// EXACT layout reproduced below (this+0 .. this+9 in the disassembly).
struct CursorSet {
    // this+0: always set to 0 by the original loader ("active" cursor /
    // current index — never read back within LoadResources itself).
    int32_t state = 0;

    // this+1 .. this+9: the 9 cursors, in the EXACT order used by the binary.
    // Resource IDs = MAKEINTRESOURCE(id) on the .exe module itself
    // (RT_GROUP_CURSOR embedded in the .rsrc section — NOT a file
    // on disk).
    //
    // ROLE OF EACH SLOT — established (wave W10) by the 8-case switch in
    // Scene_InGameRender 0x52D0B0 (`cmp var_53C, 7 / ja def_530FC7` @0x530FB4,
    // jump @0x530FC7), where each case sets a cursor frame based on the
    // category returned by World_PickEntityAtCursor 0x538AB0:
    //
    //   slot 0     (0x66) default — scene-entry reset (Scene_EnterWorldUpdate
    //                    @0x52C044, Scene_InGameUpdate @0x52C637: `push 0`) AND
    //                    any successful UI hit-test (cDrawWin_Draw: Sprite2D_HitTest
    //                    != 0 -> `push 0` @0x6299D8).
    //   slots 1,2  (0x67,0x68) 2 Hz blinking "interact" pair: case 1
    //                    (player, +Char_DrawNameplate @0x531052), case 4 (NPC
    //                    g_NpcRenderArray stride 88, +Fx_MeleeSwingDrawMarker
    //                    @0x531148), case 7 (zone object, +Obj_DrawNameLabel
    //                    @0x531206).  base = +1 (@0x531022 / @0x53111B / @0x5311E2)
    //   slots 3,4  (0x69,0x6A) 2 Hz blinking "hostile" pair: cases 2 and 3
    //                    (players, +Char_DrawNameplate @0x5310A5 / @0x5310F8),
    //                    case 5 (monster dword_1766F74 stride 280,
    //                    +Char_DrawOverheadName @0x531199).
    //                    base = +3 (@0x531075 / @0x5310C8 / @0x53116B)
    //   slots 5,6  (0x6B,0x6C) 2 Hz blinking pair: case 6 (ground item;
    //                    no associated draw).  base = +5 (@0x5311B9)
    //   slot 7     (0x75) CASTABLE skill — case 0:
    //                    Skill_CanCastAtCursor(unk_1685740,…) != 0 -> `push 7`
    //                    @0x530FEA
    //   slot 8     (0x77) NOT castable skill — case 0, zero branch ->
    //                    `push 8` @0x530FFA
    //
    // NB: the ROLE above is proven by call-site context; the APPEARANCE
    // (sword/hand/hourglass…) is NOT — the RT_GROUP_CURSOR resources were not
    // inspected. Slots are therefore NOT named after an assumed icon.
    HCURSOR slot66 = nullptr; // id 0x66 (102) — this+1 — default / UI hover
    HCURSOR slot67 = nullptr; // id 0x67 (103) — this+2 — interact (phase A)
    HCURSOR slot68 = nullptr; // id 0x68 (104) — this+3 — interact (phase B)
    HCURSOR slot69 = nullptr; // id 0x69 (105) — this+4 — hostile (phase A)
    HCURSOR slot6A = nullptr; // id 0x6A (106) — this+5 — hostile (phase B)
    HCURSOR slot6B = nullptr; // id 0x6B (107) — this+6 — ground item (phase A)
    HCURSOR slot6C = nullptr; // id 0x6C (108) — this+7 — ground item (phase B)
    HCURSOR slot75 = nullptr; // id 0x75 (117) — this+8 — castable skill
    HCURSOR slot77 = nullptr; // id 0x77 (119) — this+9 — blocked skill

    // CursorSet_LoadResources 0x4C0FA0: LoadCursorA(hInstance, id) for the
    // 9 resources above, IN THIS ORDER. Returns false if a single call
    // fails (null HCURSOR) — faithful to the binary's control loop.
    // NB: will legitimately fail until ClientSource embeds the
    // same RT_GROUP_CURSOR resources (ids 0x66..0x6C,0x75,0x77) in its
    // own .rc — this is honest behavior, not a regression.
    bool LoadResources(HINSTANCE hInstance);

    // mPOINTER (teardown) — CursorSet_DestroyAll 0x4C10B0 (App_Shutdown 0x462480,
    // step 27/33). DestroyIcon() on the 9 cursors loaded by LoadResources(),
    // then reset to zero (state + the 9 slots) — faithful to the original, including
    // the fact that a cursor obtained via LoadCursorA (a SHARED resource) still
    // gets passed to DestroyIcon: binary behavior reproduced as-is, no
    // "fix" applied (DestroyIcon on a shared HCURSOR is usually a
    // silent no-op on Win32, not a crash).
    void DestroyAll();

    // mPOINTER (msg-loop tick) — Cursor_AnimateTick 0x4C1140, ONLY caller:
    // WinMain 0x4609C0 @0x46163b (`mov ecx, offset dword_8E714C ; call
    // Cursor_AnimateTick`). NOT a per-sprite/timer animation: `state` is
    // the [0..8] index of the "desired" cursor, written elsewhere (~157 sites, ALL
    // of UI/scene rendering) via Util_SetClampedU8Field 0x4C1110 on a mouse
    // hit-test (e.g. cDrawWin_Draw 0x629960: hover -> Util_SetClampedU8Field(
    // &unk_8E714C, 0)). AnimateTick just reapplies SetCursor(
    // slot[state]) on EVERY message-loop iteration: the window has
    // NO WNDCLASSEXA.hCursor registered, so Windows resets the
    // cursor to the default one on every WM_SETCURSOR/mouse move
    // over the client area — the game therefore reasserts its "desired" cursor
    // every iteration rather than just once on click/hover.
    // Original decompilation (this = &unk_8E714C, 10-dword array):
    //   return SetCursor(*(this + *this + 1));   // this[ state + 1 ]
    // SetActiveSlot() below is the client-side equivalent of
    // Util_SetClampedU8Field(&unk_8E714C, idx) 0x4C1110.
    //
    // SOLE TARGET — proven by COUNTING (wave W10), not sampling:
    // xrefs_to(0x8E714C) = 161 data refs = the 157 Util_SetClampedU8Field sites
    // + exactly 4 others (WinMain @0x461636 AnimateTick, App_Init @0x461F8B
    // LoadResources, App_Shutdown @0x462587 DestroyAll, CrtInit_CursorSetThunk
    // @0x7A51F3). 157 + 4 = 161 => EVERY setter site does target this global,
    // without exception.
    HCURSOR AnimateTick() const;

    // Equivalent of Util_SetClampedU8Field(&unk_8E714C, idx) 0x4C1110: sets the
    // active slot [0..8], no effect if out of bounds (faithful: the original does
    // not touch *this when a2 > 8). Returns true if the value was accepted.
    bool SetActiveSlot(uint32_t idx);
};

// Named cursor slots — proven constants (see role table above).
// Pass to CursorSet::SetActiveSlot(). The three "blinking pair" bases
// are used via CursorBlinkSlot() (2 Hz).
constexpr uint32_t kCursorDefault      = 0; // @0x52C044 / @0x52C637 / @0x6299D8
constexpr uint32_t kCursorInteractBase = 1; // @0x531022 / @0x53111B / @0x5311E2
constexpr uint32_t kCursorHostileBase  = 3; // @0x531075 / @0x5310C8 / @0x53116B
constexpr uint32_t kCursorPickupBase   = 5; // @0x5311B9
constexpr uint32_t kCursorCastOk       = 7; // @0x530FEA (Skill_CanCastAtCursor != 0)
constexpr uint32_t kCursorCastBlocked  = 8; // @0x530FF8

// 2 Hz blinking for pairs {1,2} / {3,4} / {5,6} — EXACT transcription of the
// pattern repeated across the 7 cases of Scene_InGameRender 0x52D0B0 (anchor: case 1
// @0x531009..0x531022):
//     fld ds:g_GameTimeSec        // game time
//     fadd st, st                 // x + x  (NOT a multiply by 2.0)
//     call Crt_ftol               // truncate toward zero -> int
//     and  eax, 80000001h         //  ┐
//     jns  short L                //  │ MSVC SIGNED MODULO % 2 idiom
//     dec  eax                    //  │ (and NOT a plain `& 1`: the sign
//     or   eax, 0FFFFFFFEh        //  │  fixup makes -3 % 2 == -1)
//     inc  eax                    //  ┘
//   L: add  eax, <base>           // 1, 3, or 5 depending on the category
//
// The result is passed as-is to Util_SetClampedU8Field (UNSIGNED
// parameter): a `base` + negative result would become a huge unsigned value, so
// rejected by the `a2 <= 8` clamp — behavior already reproduced by
// SetActiveSlot(uint32_t). We keep the signed type here to stay faithful
// (in practice g_GameTimeSec >= 0, the result is base or base+1).
inline int CursorBlinkSlot(int base, float gameTimeSec) {
    return static_cast<int>(gameTimeSec + gameTimeSec) % 2 + base;
}

// Cursors() — the SINGLE instance of the cursor set, mirror of dword_8E714C
// (0x8E714C), which is a GLOBAL in the binary, not an object member.
// The 161 references to 0x8E714C (see "SOLE TARGET" above) come from
// WinMain, App_Init, App_Shutdown AND all of UI/scene rendering: none of these
// sites owns the object, all address the same global. Exposing the instance
// here is thus the FAITHFUL transcription — not an encapsulation workaround.
//
// ⚠️ WARNING (inseparable wiring, see W10 report): as long as App keeps
// its private member `cursors_` (App/App.h:43), there would be TWO CursorSet
// instances — scenes/UI would write to this singleton while App ticks its member,
// and the cursor would stay stuck even though the code LOOKS complete.
// App.h:43 / App.cpp:320/406/741 must switch to Cursors() IN THE SAME
// change (files not owned by W10 -> wiringTodoForOrchestrator).
CursorSet& Cursors();

// mMYINFO — Player_ResetAnimState 0x50F520.
// Operates on g_PlayerCmdController (0x1669170 in the binary), a very large
// block not yet ported to ClientSource. Reproduced here as a function on a
// raw pointer (float*), faithful offset by offset; to be wired to the future
// "player command controller" struct once it's modeled.
// `gameTimeSec` = current value of g_GameTimeSec (0x815180) at call time.
void Player_ResetAnimState(float* playerCmdController, float gameTimeSec);

// mPLAY — cGameData_InitPools 0x5575D0.
// Sets the capacities of the entity pools (already modeled as std::vector in
// Game/GameState.h: g_World.players/monsters/npcs/groundItems) to their
// original fixed sizes, and pre-fills them with default slots
// (equivalent of the small constructors sub_55D6F0/57FE50/580530/583370
// called in a loop by the binary). Always true (faithful: the binary
// cannot fail here — no dynamic allocation is tested).
bool GameData_InitPools();

// mPLAY (teardown) — cGameData_DestroyPools 0x557780 (App_Shutdown 0x462480,
// step 1/33 — VERY FIRST call, mirror image of GameData_InitPools, which is
// the VERY LAST manager in App_Init).
// The original walks EVERY active pool (bounds = counters this+1717..1721 +
// g_ZoneObjectCount) and calls a small per-slot destructor
// (Fx_AttachSlotClear / maybe_cGameData_NpcListItemDtor / Char_RespawnAfterKnockback /
// maybe_cGameData_ListField1ItemDtor / PlayerArray_SlotDestruct /
// maybe_cGameData_ZoneObjListItemDtor) — same 1-4-field-per-slot pattern
// as GameData_InitPools's small constructors (not a deep teardown,
// no memory freeing: the pools are FIXED arrays in .bss in
// the original binary, never actually "freed").
// Here, the pools are dynamic std::vector (Game/GameState.h): the equivalent
// net effect (capacity reduced to zero / slots reset) is obtained by
// clearing the 5 modeled vectors (same 5 pools as GameData_InitPools;
// pool E "projectiles" 0x17D06F4/g_FxAuraCount also not modeled here, see
// GameData_InitPools comment).
bool GameData_DestroyPools();

} // namespace ts2::game

// World/TerrainPicker.h — screen -> world picking for the TwelveSky2 client (faithful rewrite).
//
// Wave W10, "terrain-picking" front. Closes THREE proven gaps (G-PICK-03/05/06). Ground truth
// = the TwelveSky2.exe disassembly (imagebase 0x400000) via MCP idaTs2.
//
// Original functions reproduced here:
//   World_PickEntityAtCursor  0x538AB0 -> World_PickEntityAtCursor  (8 click categories)
//   Scene_RayHitPlayerBox     0x5415E0 -> RayHitPlayerBox   (static, .cpp)
//   Scene_RayHitNpcBox        0x541680 -> RayHitNpcBox      (static, .cpp)
//   Scene_RayHitMonsterBox    0x541780 -> RayHitMonsterBox  (static, .cpp)
//   Scene_RayHitNodeBox       0x541920 -> RayHitNodeBox     (static, .cpp)
//   Cam_ScreenRayVsAABB       0x6A0670 -> collision::BuildScreenRay + collision::SegmentAABB
//   Terrain_PickRayScreen     0x699A80 -> TerrainPicker::PickRayScreen (via WorldAssets)
//   World_IsPointBlocked      0x540DA0 -> TerrainPicker::IsPointBlocked (via WorldAssets)
//   Scene_InGameRender @0x530FC7..0x53120B (cursor-shape table) -> CursorSlotForPickCategory
//
// §0. WHY NO NEW MATH IS WRITTEN HERE (arithmetic proof)
// All 4 entity hit-tests go through Cam_ScreenRayVsAABB 0x6A0670, which reads its
// camera parameters from the singleton g_GfxRenderer (0x7FFE18):
//   this+792 -> 0x800130 (= g_CameraPos)  this+648 -> 0x8000A0 (= screen width)
//   this+796 -> 0x800134                  this+652 -> 0x8000A4 (= screen height)
//   this+800 -> 0x800138                  this+728 -> 0x8000F0 (= proj._11)
//   this+892 -> 0x800194 (= invView)      this+748 -> 0x800104 (= proj._22)
// These are EXACTLY the same bytes as the globals read by Terrain_PickRayScreen
// 0x699A80 (@0x699AA6 g_CameraPos, @0x699AD7 dword_8000A4, @0x699AE7 flt_8000F0,
// @0x699B31 flt_800104, @0x699B40 unk_800194) — verified address by address. And the body
// of 0x6A0670 reduces to: build the screen ray (formula IDENTICAL to
// collision::BuildScreenRay, World/WorldMap.cpp:1139) then call Collide_SegmentAABB
// 0x69FB20 (== collision::SegmentAABB, World/WorldMap.cpp:795).
// => A SINGLE collision::ScreenPickCamera serves all 6 picking paths of the client.
//
// §1. THE 8 CLICK CATEGORIES (World_PickEntityAtCursor 0x538AB0)
// Original signature: int __stdcall (int sx, int sy, _DWORD* outKind, int* outIndex,
// int allowModifierTargets). Init: *outKind=0 @0x538ABC, *outIndex=-1 @0x538AC5. The `ecx`
// loaded at the call site is DEAD (stdcall convention). The return value (eax) is scratch
// never consumed by the caller (the switch @0x530FC7 dispatches on outKind) — hence the
// convenience `bool` here.
//   0 = ground/nothing (default)  4 = NPC                (loop @0x538DAC)
//   1 = neutral player            5 = monster            (loop @0x538E9C)
//   2 = trade partner             6 = ground item        (loop @0x5390D7) — NOT PORTED, cf. §4
//   3 = attackable player         7 = zone object         (loop @0x5391A7)
// The switch in Scene_InGameRender @0x530FB4 (`cmp 7 / ja default`) confirms 8 cases.
//
// PRIORITY: the retained category is the one CLOSEST TO THE CAMERA, via
// Math_Dist3D(entityPos, g_CameraPos) and a STRICT `v14 > dist` comparison -> on a
// tie, the FIRST one found wins. Categories 4/5/6 are written as
// `!*outKind || v14 > d`, and 1/2/3/7 as nested if/else: SEMANTICALLY IDENTICAL.
//
// GUARDS per loop (all re-proven against the decompile of 0x538AB0):
//  - players (i starts at 1, self excluded ; bound g_EntityCount 0x168721C ; stride 908) :
//      active(+0) && dword_168724C[227i](= body[0], "record populated")
//      && Char_IsTargetablePlayerState(g_SelfActionState[227i]) (0x558AE0 : state != 12)
//      && Scene_RayHitPlayerBox
//  - NPCs (bound g_NpcCount 0x1687220 ; stride 88) : active(+4) && range filter
//      Math_Dist3D(npcPos, flt_1687330) <= 500.0 @0x538E04 — measured against the
//      PLAYER's position, not the camera's; ONLY this loop has this filter.
//  - monsters (bound g_MonsterCount 0x1687224 ; stride 280) : active(+0)
//      && Char_IsTargetableMonsterState(dword_1766F8C[70k]) (0x558B10 : != 12 && != 19)
//      && Scene_RayHitMonsterBox && ELEMENTAL FILTER @0x53905E..0x539083 (cf. §2)
//  - zone objects (bound g_ZoneObjectCount 0x1687230 ; stride 76) : active(+0)
//      && Scene_RayHitNodeBox
//
// MODIFIER GATING — categories 1 and 7 ONLY (@0x538D07 / @0x539220) :
//   `if (allowModifierTargets) { if (byte_8013FE < 0) { ...keep... } } else { ...keep... }`
// In other words: allowModifierTargets==false -> category ALWAYS eligible ;
//                 allowModifierTargets==true  -> requires the modifier key held.
// DO NOT code `eligible = allowModifier && keyDown` (would wrongly exclude the false case).
// Categories 2/3/4/5/6 are NEVER gated.
//
// byte_8013FE IDENTIFIED (wave W10) = **DIK_LSHIFT (0x2A)**, cf. kModifierDik below.
//
// §2. MONSTER ELEMENTAL FILTER (@0x53905E..0x539083) — missing from the gap list
// For def = dword_1766FD4[70k] (== MonsterEntity::def, a MONSTER_INFO), if
// def+232 (== MonsterInfo::field232, proven domain [1,15]) is 10/11/12/13, the monster
// is only targetable if the decompile's four clauses hold. They reduce to a
// UNIFORM rule (proof: clause 10 reads `g_LocalElement && g_LocalElement !=
// Paired(0)`, and `g_LocalElement` != 0 IS `g_LocalElement != 0` -> this is the K=0 case of the
// general form `element != K && element != Paired(K)` of clauses 11/12/13):
//     K = field232 - 10  (0..3)  ->  targetable iff (element != K && element != Paired(K))
// Semantics: elemental guardians — you can't target one of your own element, nor
// one of the paired element. Char_GetPairedElement 0x557C00 == ElementPairTable::Paired (verified
// bit-for-bit: this[455..458] <-> a/b/c/d, same 4 tests, same -1 fallback).
//
// §3. CATEGORY 2 / CATEGORY 3
// Cat 2 @0x538BCC (reproduced LITERALLY — cf. the ambiguity note on kRecTradeFlag) :
//   g_TradePartnerIdLo[0]==1 && g_TradePartnerIdLo[227i]==1
//   && dword_1687420[0]==dword_1687420[227i] && dword_1687424[0]!=dword_1687424[227i]
// Cat 3 @0x538C4E : Combat_CanTargetOnMap(g_LocalPlayerSheet, dword_168728C[227i],
//   dword_1687320[227i], &byte_168725C[908i]) — NOT PORTED (PVP zone system), exposed
// as a host callback, cf. EntityPickHost::CanTargetOnMap.
//
// §4. WHAT IS DELIBERATELY NOT PORTED (and why it stays FAITHFUL)
// CATEGORY 6 (ground item, Scene_RayHitItemModel 0x5418B0) : not ported. Triple blocker,
// no observable effect:
//   (a) the source pool `game::g_World.groundItems` is EMPTY BY DESIGN — the 152-byte
//       structure of dword_17AB534 is not modeled (Game/GameState.h:605-613 states this
//       explicitly) ; a loop over an empty vector can retain NOTHING ;
//   (b) 0x5418B0 is NOT an AABB test like the other 4 : it's an OBB test via
//       ModelObj_GetBoneMatrix 0x4D7130 + Cam_ScreenRayVsOBB/Collide_SegmentOBB 0x6A0750 —
//       NEITHER of these two building blocks is ported ;
//   (c) so omitting it produces EXACTLY the same observable result as an empty pool.
// The day (a) is lifted, (b) will need porting, then the loop restored BETWEEN the
// monsters and the zone objects (order matters for the "first found wins" tie rule).
#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "Game/GameState.h"    // game::GameWorld
#include "Game/SkillCombat.h"  // game::ITerrainPicker (+ world::CollisionSlot transitively)
#include "World/WorldMap.h"    // world::CollisionSlot, world::collision::ScreenPickCamera
#include "World/WorldIntegration.h" // world::WorldAssets

// FORWARD declaration: keeps this header free of <d3dx9.h> (Gfx/Camera.h is pulled in only by
// the .cpp). `Camera` is indeed declared `class` in Gfx/Camera.h:35 -> no C4099.
namespace ts2::gfx { class Camera; }

namespace ts2::world {

// Modifier key of World_PickEntityAtCursor (byte_8013FE, categories 1 and 7).
//
// IDENTIFIED (wave W10) — the gap list only said "a keyboard modifier". Proof:
// Input/InputSystem.h:23-25 establishes that the client's DirectInput state
// array is `BYTE state[256]` at g_GfxRenderer+5564 = **0x8013D4** (filled by
// GetDeviceState). So byte_8013FE == state[0x8013FE - 0x8013D4] == state[0x2A] == DIK_LSHIFT.
// DOUBLE independent corroboration from already-ported and verified mappings:
//   byte_8013F2 == state[0x1E] == DIK_A (Input/InputSystem.h:95, App/PlayerInputController.cpp:56)
//   byte_8013E5 == state[0x11] == DIK_W (Input/InputSystem.h:92, App/PlayerInputController.cpp:75)
// Both land exactly on standard DIK scancodes -> the 0x8013D4 base is certain.
//
// The constant is placed HERE and not in Input/InputSystem.h::dik (a file NOT owned by
// this front) ; to be moved up into that namespace in a future consistency pass.
//
// Test convention: the binary does `byte_8013FE < 0` on a SIGNED byte, i.e. bit 7
// set == key held — strictly equivalent to InputSystem::IsKeyDown (`& 0x80`).
inline constexpr int kModifierDik = 0x2A; // DIK_LSHIFT — byte_8013FE (0x8013D4 + 0x2A)

// Zones forbidding category 1 (neutral player) — @0x538CFD.
// The binary tests g_SelfMorphNpcId 0x1675A98, which wave W10 established to be the CURRENT
// ZONE ID and not a "morph" (cf. the banner of Game/SkillCombat.h:33): these are therefore
// PVP zones, where a player can never be "neutral".
inline constexpr int kZonesBlockingNeutralPlayer[6] = { 270, 271, 272, 273, 274, 324 };

// TerrainPicker — IMPLEMENTER of game::ITerrainPicker (G-PICK-06: the interface had
// NONE). Owns nothing: references the already-decoded .WM/.WJ meshes from WorldAssets and
// a camera snapshot. To be built per frame (trivial object, no allocation).
class TerrainPicker final : public game::ITerrainPicker {
public:
    TerrainPicker(const WorldAssets& assets, const collision::ScreenPickCamera& cam)
        : assets_(&assets), cam_(cam) {}

    // World_IsPointBlocked 0x540DA0 (via WorldAssets, Main + WJ layers).
    bool IsPointBlocked(const float pos[3]) override;

    // Terrain_PickRayScreen 0x699A80 on the `slot` mesh. `twoSide` = original 6th arg.
    bool PickRayScreen(int screenX, int screenY, CollisionSlot slot, bool twoSide,
                        float outPos[3]) override;

private:
    const WorldAssets*          assets_;
    collision::ScreenPickCamera cam_;
};

// Builds the camera snapshot common to the 6 picking paths (cf. §0) from the engine's
// camera and the current viewport. `screenW`/`screenH` = dword_8000A0/dword_8000A4.
//   eye     <- Camera::Eye()                    (g_CameraPos 0x800130)
//   invView <- inverse(BuildViewMatrix())       (unk_800194)
//   proj11  <- BuildProjMatrix(aspect)._11      (flt_8000F0)
//   proj22  <- BuildProjMatrix(aspect)._22      (flt_800104)
// `aspect` = screenW/screenH, SAME calc as Gfx_InitDevice 0x69BFC6 and
// Scene/WorldRenderer.cpp:556-559.
collision::ScreenPickCamera BuildScreenPickCamera(const gfx::Camera& camera,
                                                   int screenW, int screenH);

// External dependencies of World_PickEntityAtCursor not ported to C++.
struct EntityPickHost {
    // Combat_CanTargetOnMap 0x558740(g_LocalPlayerSheet, element, pkLevel, affiliationName)
    // -> category 3 (attackable player). NOT PORTED: it depends on Map_GetPvpMode
    // 0x4FAB90 + a zone switch (291/138/139/165/166/324/342/270-274/54) and a
    // sub-mode dword_16760F4, a whole PVP zone system out of this front's scope.
    // SAME exact signature as game::NameplateHost::CanTargetOnMap (Game/NameplateLogic.h:268)
    // -> the host can wire THE SAME lambda to both.
    // Default (unwired): false -> no player is ever classified "attackable" and
    // falls back to category 1 (neutral). HONEST degradation, already accepted as-is by
    // NameplateLogic; to be lifted once the PVP zone system is ported.
    std::function<bool(int element, int pkLevel, const std::string& affiliationName)> CanTargetOnMap;
};

// World_PickEntityAtCursor 0x538AB0 — cf. §1..§4 of the banner.
//
// `cam`                  : camera snapshot (BuildScreenPickCamera).
// `allowModifierTargets` : original a5 = (GameOptions::ShowHitMarkers ? 1 : 0), cf. the
//                          two call sites @0x530F7E (0) / @0x530FA6 (1).
// `modifierKeyDown`      : byte_8013FE < 0, i.e. input.IsKeyDown(kModifierDik).
// `outKind` (0..7) / `outIndex` (-1 if none) : original *a3 / *a4, ALWAYS written.
// Return: convenience == (outKind != 0). The original eax is unconsumed scratch.
bool World_PickEntityAtCursor(const game::GameWorld& world,
                               const collision::ScreenPickCamera& cam,
                               int screenX, int screenY,
                               bool allowModifierTargets, bool modifierKeyDown,
                               const EntityPickHost& host,
                               int& outKind, int& outIndex);

// Cursor shape associated with a hover category — EXACT table from the switch of
// Scene_InGameRender @0x530FC7..0x53120B (arguments passed to Util_SetClampedU8Field
// 0x4C1110, single target dword_8E714C == game::CursorSet).
//
//   blink = ((int)(2.0f * g_GameTimeSec)) % 2   -- the binary's SIGNED idiom
//           (`fadd st,st` / Crt_ftol / `and eax,80000001h` / `jns` / `dec` /
//            `or eax,0FFFFFFFEh` / `inc`), 2 Hz alternation.
//
//   cat 0 : 7 if canCastAtCursor else 8         (@0x530FEA / @0x530FF8) — NO animation
//   cat 1 : blink + 1                          (@0x531022)
//   cat 2 : blink + 3                          (@0x531075)
//   cat 3 : blink + 3                          (@0x5310C8) — the gap list noted "+?"
//   cat 4 : blink + 1                          (@0x53111B)
//   cat 5 : blink + 3                          (@0x53116B)
//   cat 6 : blink + 5                          (@0x5311B9)
//   cat 7 : blink + 1                          (@0x5311E2)
//
// `canCastAtCursor` is read ONLY for category 0 (the binary calls
// Skill_CanCastAtCursor 0x540E60 @0x530FE1 only in this case — it is its ONLY caller,
// xrefs_to = 1). Returns -1 if `kind` is outside [0,7] (== `default` of the switch: no
// cursor write).
int CursorSlotForPickCategory(int kind, float gameTimeSec, bool canCastAtCursor);

} // namespace ts2::world

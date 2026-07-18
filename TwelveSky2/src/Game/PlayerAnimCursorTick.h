// Game/PlayerAnimCursorTick.h — advance of the animation CURSOR for ONE player (entity+248 =
// CharAnimState::animFrame), faithful port of the UNIVERSAL idiom of the ~81 terminal-switch
// handlers of Char_UpdateAnimationFrame 0x571880 (@0x5727BF). Front F_PLAYERANIM (2026-07-17).
//
// WHY THIS FILE. Players (self + remote) were drawn with animState=0 frozen (idle) + a
// GLOBAL clock (Gfx/MotionCache.h::SampleByGameTime): all in phase, never any clip other than
// idle. Root cause: the real entity+248 cursor was NEVER advanced on the C++ side. The
// terminal switch 0x5727BF (which contains the advance) is not dispatched
// (Scene/SceneManager.cpp:1140, 10th argument `stateHandler` = nullptr), and the PARTIAL
// router Game/AnimationTick.h §7 (Char_DispatchStateTick) only covers 6/81 cases (cast/guard)
// — NOT idle(0)/move(1), the DOMINANT states. This module provides the UNIVERSAL advance of
// the cursor (idle/move and any looped-clip state), to be called 1x/frame per player during
// the UPDATE phase (mirroring Char_UpdateAnimationFrame, called by Scene_InGameUpdate 0x52C600
// @0x52c96d self / @0x52c9fd remote).
//
// GROUND TRUTH (IDA idaTs2, re-decompiled this session) — the idiom is IDENTICAL across all
// switch handlers, proven on the TWO dominant states:
//   Char_AnimTick_5746E0 0x5746E0 (case 0 = idle) @0x5747a8 : *(float*)(this+248) = a3*30.0 + *(float*)(this+248)
//   Char_TickMoveState   0x574830 (case 1 = move) @0x5748f8 : *(float*)(this+248) = a3*30.0 + *(float*)(this+248)
//                                                @0x574911/@0x574922 : if((this+248) >= (double)v8) (this+248) -= (double)v8
//   v8 = PcModel_ResolveSlotAndApply 0x4E5A00  (= PcModel_ResolveEquipSlot 0x4E46A0 then
//        Motion_GetFrameCount 0x4D7830)  = NUMBER OF FRAMES of the current clip.
// -> advance = frame += dt*30 ; wrap = SUBTRACTION (NEVER a modulo, cf. Gfx/MotionCache.h
//    SampleByCursor : the Death state freezes the cursor at frameCount-1, a modulo would
//    resurrect it).
//
// SEPARATION FROM THE FSM (faithful). This module writes ONLY the cursor (animFrame). It does
// NOT touch the state (anim.state = entity+244), written elsewhere: network
// (Game/EntityManager.cpp:390 = body+220, Pkt_SpawnCharacter 0x4646C0 -> Char_SetActionAnimParams
// 0x570E70), input (~90 Player_Queue*/Net_Queue* -> g_SelfActionState = 1) and FSM transitions.
// The internal idle->move transition (Char_AnimTick_5746E0 @0x5747db : end of idle -> state=1,
// cursor=0) is a STATE WRITE, out of scope for this pure cursor: reproduced by the FSM, not
// here. The clip follows the state (PcModel_ResolveEquipSlot 0x4E46A0, base + 156*state) ; this
// module just spins the cursor WITHIN the current clip, and the clip changes when the state
// changes (network/input).
#pragma once
#include "Game/GameState.h" // CharAnimState (state = entity+244, animFrame = entity+248)

namespace ts2::game {

// Advances a player's animation cursor by ONE frame. `dt` = original a3 (1/30 s @30 FPS).
// `frameCount` = number of frames in the CURRENT clip (supplied by the caller via the
// player's motion oracle — ts2::WorldPlayerMotionFrameCount = Motion_GetFrameCount 0x4D7830,
// the SAME source as the drawn palette: otherwise wrap and sampling would diverge). Modifies
// ONLY anim.animFrame (entity+248) — never anim.state (cf. banner: FSM separation).
//
// DEGRADATION (no invention). frameCount <= 0 (slot not resolved / file absent) -> the cursor
// ADVANCES but never wraps (unknown duration: no bound is fabricated), SAME policy as
// game::Monster_DispatchMotionTick (Game/AnimationTick.h §5). This frameCount>0 guard also
// protects against an infinite wrap loop.
void Player_AdvanceAnimCursor(CharAnimState& anim, float dt, int frameCount);

// WIRING NON-REGRESSION GUARD (NOT a binary behavior) — exact counterpart of
// game::Monster_MotionTickIsWired() / ZoneNpc_AnimTickIsWired() / Char_StateTickIsWired().
// True as soon as a Player_AdvanceAnimCursor has actually run at least once. Lets
// Gfx/PlayerPaperdoll (via Scene/WorldRenderer, DrawableEntity::hasAnimCursor flag) consume
// the per-entity cursor (MotionCache::SampleByCursor) ONLY if it is actually fed ; otherwise
// keep the global-clock fallback (SampleByGameTime, correct clip but in phase), instead of
// FREEZING ALL PLAYERS AT FRAME 0 — this front's recon blocker. To remove once the UPDATE
// wiring (Scene/SceneManager.cpp) is locked in by a test.
bool Player_AnimCursorTickIsWired();

} // namespace ts2::game

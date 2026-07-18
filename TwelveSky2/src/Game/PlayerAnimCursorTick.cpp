// Game/PlayerAnimCursorTick.cpp — implementation. See the header for the full IDA proof
// (Char_AnimTick_5746E0 0x5746E0 / Char_TickMoveState 0x574830 : frame += dt*30, wrap by
// subtraction @0x574922 ; frameCount = PcModel_ResolveSlotAndApply 0x4E5A00). Front F_PLAYERANIM.
#include "Game/PlayerAnimCursorTick.h"

namespace ts2::game {

namespace {
// Wiring latch (cf. Player_AnimCursorTickIsWired) — becomes true on the 1st real tick. Process
// lifetime, same pattern as the statics in Game/AnimationTick.cpp (Monster/ZoneNpc IsWired).
bool s_playerAnimCursorWired = false;
} // namespace

void Player_AdvanceAnimCursor(CharAnimState& anim, float dt, int frameCount) {
    // Advance — Char_AnimTick_5746E0 0x5746E0 @0x5747a8 (idle) / Char_TickMoveState 0x574830
    //   @0x5748f8 (move) : *(float*)(this+248) = a3*30.0 + *(float*)(this+248). (a3 = dt ;
    //   this+248 = CharAnimState::animFrame). The 30.0 factor converts dt (s) to frames.
    anim.animFrame += dt * 30.0f;

    // Wrap by SUBTRACTION — Char_TickMoveState 0x574830 @0x574911/@0x574922:
    //   if (this+248 >= (double)v8) this+248 -= (double)v8.  NEVER a modulo (fidelity:
    //   Gfx/MotionCache.h::SampleByCursor — the wrap is a single subtraction; a modulo would
    //   restart from 0 the frozen-at-end-of-clip states like Death/Knockback).
    // The binary does a SINGLE subtraction (`if`), because the advance is exactly 1.0
    // frame/tick (dt=1/30 @30 FPS, dt*30=1.0): one pass always suffices (except for a
    // 1-frame clip, where one pass also suffices). The `while` loop below is a DEFENSIVE
    // superset of it (identical for dt=1/30 ; robust to a dt spike), guarded by frameCount>0.
    // frameCount <= 0 (slot not resolved -> Motion_GetFrameCount returns 0) -> advance
    // WITHOUT wrapping: duration unknown, no bound is fabricated (same policy as
    // Monster_DispatchMotionTick, Game/AnimationTick.h §5). Also guards against an infinite
    // `while` loop.
    if (frameCount > 0) {
        const float fc = static_cast<float>(frameCount);
        while (anim.animFrame >= fc)
            anim.animFrame -= fc;
    }

    s_playerAnimCursorWired = true; // non-regression guard (cf. header / Player_AnimCursorTickIsWired)
}

bool Player_AnimCursorTickIsWired() {
    return s_playerAnimCursorWired;
}

} // namespace ts2::game

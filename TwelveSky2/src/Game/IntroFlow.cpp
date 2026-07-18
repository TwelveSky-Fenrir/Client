// Game/IntroFlow.cpp — cf. IntroFlow.h for details of the discovered flow (Scene_IntroUpdate
// 0x517FE0). Direct reproduction of the decompiled switch(this[1]): each sub-state is a
// simple "increment the counter, if the threshold is reached advance to the next sub-state
// and reset the counter to 0", with three distinct thresholds (90 / 3*33 / 90). Generalized
// here into a handful of equivalent branches (the original repeats the same pattern 35
// times, one per `case`, with no logic variation — cf. the 33 identical cases 0x1..0x21 in
// the disassembly).
#include "Game/IntroFlow.h"

namespace ts2::game {

bool UpdateIntro(IntroState& state, float /*dt*/, const IntroHost& host) {
    // Sub-state 0: initial wait before the logo cycle (0x518019..0x51807a).
    if (state.subState == 0) {
        ++state.frameCounter;
        if (state.frameCounter >= kIntroWaitFrames) {
            // Crossing 0->1 in the original (0x51802c..0x518081):
            //   Util_SetClampedU8Field(&dword_8E714C, 0) + UI_FocusEditBox(&g_UIEditBoxMgr, 0)
            // -> ts2::ui side effects, out of scope; exposed via the host (cf. IntroFlow.h).
            if (host.OnLogoSequenceBegin) host.OnLogoSequenceBegin();

            // Zeroing of the 150-dword buffer (this[3..152], 0x518042..0x51806b), faithful to
            // the original — even though this buffer is never read back (Scene_IntroRender does not read it).
            state.logoFade.fill(0);

            state.subState     = 1;
            state.frameCounter = 0;
        }
        return false;
    }

    // Sub-states 1..33: sequential 3-frame micro-states each (0x518092..0x5187b0
    // in the original — 33 identical `case`s, each simply advancing subState+1).
    if (state.subState >= 1 && state.subState <= kIntroLogoStepCount) {
        ++state.frameCounter;
        if (state.frameCounter >= kIntroLogoStepFrames) {
            ++state.subState;
            state.frameCounter = 0;
        }
        return false;
    }

    // Sub-state 34 (0x22): final hold before transition (0x5187be..0x5187df).
    if (state.subState == kIntroFinalSubState) {
        ++state.frameCounter;
        if (state.frameCounter >= kIntroFinalHoldFrames) {
            // Transition to ServerSelect (*a1 = 2 in the original) + auto-reset
            // (a1[1]=0, a1[2]=0), enabling a faithful replay if the Intro scene is
            // re-selected. NB: the original does NOT re-zero logoFade here (only the
            // 0->1 crossing does) — faithfully reproduced (no fill() here).
            state.subState     = 0;
            state.frameCounter = 0;
            return true;
        }
        return false;
    }

    // Sub-state out of range (0..34): should never occur in normal use (mirrors the
    // original's `default: return result;`, which does nothing). Defense in depth
    // only — not part of the actually observed flow.
    return false;
}

} // namespace ts2::game

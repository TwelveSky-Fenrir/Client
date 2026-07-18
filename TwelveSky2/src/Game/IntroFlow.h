// Game/IntroFlow.h — State machine of the Intro scene (Scene_IntroUpdate 0x517FE0).
//
// FAITHFUL C++ rewrite of the decompiled state machine (re-verified 2026-07-15 on a FRESH
// decompilation of Scene_IntroUpdate 0x517FE0, called from cSceneMgr_Update 0x517BF0 when
// this->scene_ == 1). Sequential 35-sub-state automaton (this[1] = 0..34, this[2] =
// per-sub-state frame counter; this[3..152] = 150-dword buffer at offset +12) at a FIXED
// STEP: the float parameter (a2/a3 of the original __fastcall) is NEVER read; each call
// = one tick, incrementing this[2] once. Consistent with the engine's 30 FPS tick
// (flt_815188 = 0.033333, cf. CLAUDE.md), NOT a time-elapsed-driven automaton.
// NO keyboard/mouse input is tested: the sequence CANNOT be "skipped" from this function —
// an eventual skip would be handled elsewhere (WndProc?), out of scope.
//
// Actual verified sequence (tick = 1/30 s @ 30 FPS; EXACT thresholds of switch(this[1]),
// 35 cases 0x0..0x22):
//   sub-state 0             : wait of kIntroWaitFrames=90 frames (0x5A, 3.0 s), black
//                             screen / end of INTRO.AVI. At the threshold (0x51802a..0x518081):
//                             two ts2::ui side effects out of scope (cf.
//                             IntroHost::OnLogoSequenceBegin) + zeroing of buffer
//                             this[3..152] (150 dw, 0x518042..6b), then this[1]=1, this[2]=0.
//   sub-states 1..33         : kIntroLogoStepCount=33 SEQUENTIAL micro-states of
//                             kIntroLogoStepFrames=3 frames (0.1 s) each (cases 0x1..0x21,
//                             all identical) -> 99 frames (3.3 s). Advances this[1]
//                             by 1 at each step. This is the this[1] that Scene_IntroRender
//                             0x518880 reads to pick the logo sprite (slot clamp(this[1]+797,
//                             830), cf. UI/IntroRender.h): DISCRETE sprite cycling,
//                             NOT an alpha fade.
//   sub-state 34 (0x22)      : final hold of kIntroFinalHoldFrames=90 frames (0x5A, 3.0 s).
//                             At the threshold (0x5187be..0x5187df): transition -> ServerSelect
//                             (*a1 = 2) AND the state machine resets (this[1]=0, this[2]=0)
//                             — reproduced here by resetting IntroState to enable a
//                             faithful replay if the Intro scene is re-selected.
//
// FAITHFUL total duration: 90 + 33*3 + 90 = 279 frames = 9.3 s @ 30 FPS. SceneManager drives
// this state machine via UpdateIntro() below (the "~90 frames" foundation from an earlier
// version is corrected — no more invented values).
//
// The this[3..152] buffer (logoFade) is zeroed on the 0->1 crossing ONLY, then NEVER read
// back: the fresh decompilation of Scene_IntroRender 0x518880 does not reference it
// (it only reads this[1]). Role undetermined (vestige); kept for layout fidelity,
// NOT for a fade — the "logo fade" speculation from earlier sessions is disproven (the
// rendering has NEITHER alpha NOR a read of this buffer).
//
// Side effects on the 0->1 crossing (0x51802c..0x51803d), out of ts2::game scope,
// exposed via IntroHost (optional callback, no-op by default; cf. IntroHost below):
//   - Util_SetClampedU8Field(&dword_8E714C, 0): writes 0 into a u8-clamped global field
//     read by many UI renderers — precise business role uncertain (157 callers).
//   - UI_FocusEditBox(&g_UIEditBoxMgr, 0): releases keyboard focus from any native EDIT
//     control (index 0 = "game") — returns the keyboard to the game before the next scenes.
//
// Self-containment: this module only includes the STL — no dependency on SceneManager
// (wiring left to the caller, cf. mission), nor on GameState.h/ClientRuntime.h (not needed
// here).
#pragma once
#include <array>
#include <cstdint>
#include <functional>

namespace ts2::game {

// Constants of the actual flow (values extracted from the disassembly, NOT arbitrary).
constexpr int kIntroWaitFrames      = 90; // Scene_IntroUpdate 0x518023: 0x5A, initial wait
constexpr int kIntroLogoStepFrames  = 3;  // Scene_IntroUpdate 0x51809E..0x518798: threshold of each micro-state
constexpr int kIntroLogoStepCount   = 33; // Scene_IntroUpdate 0x518792/0x51879F: last case 0x21 -> 0x22
constexpr int kIntroFinalHoldFrames = 90; // Scene_IntroUpdate 0x5187C8: 0x5A, final hold
constexpr int kIntroFadeSlotCount   = 150; // Scene_IntroUpdate 0x518042/0x518063: loop i<150, this[3+i]=0

// Sub-state reached after the 33 logo micro-states (final hold before transition).
constexpr int kIntroFinalSubState = kIntroLogoStepCount + 1; // Scene_IntroUpdate 0x51879F: 34 (0x22)

// Total number of frames for the full sequence (reference/diagnostic).
constexpr int kIntroTotalFrames =
    kIntroWaitFrames + kIntroLogoStepCount * kIntroLogoStepFrames + kIntroFinalHoldFrames; // 279

// Intro state machine state — direct mirror of the original cSceneMgr object fields
// used by Scene_IntroUpdate (offsets +4/+8/+12, cf. SceneManager.h header comment:
// "[+0 id][+4 sub-state][+8 frame counter][+12 150-dword buffer]").
// Do NOT couple to Scene/SceneManager.h: standalone struct, to be copied/wired by the caller.
struct IntroState {
    int subState     = 0; // mirrors this[1] (0..34)
    int frameCounter = 0; // mirrors this[2] (frame counter within the current sub-state)

    // Mirrors this[3..152] (150 dwords, offset +12). Zeroed by the original on the
    // 0->1 crossing ONLY (NOT re-zeroed on the 34->0 wraparound), then NEVER read back:
    // Scene_IntroRender 0x518880 does not reference this buffer (fresh decompilation verified).
    // Role undetermined (vestige); kept for layout/branch fidelity.
    std::array<int32_t, kIntroFadeSlotCount> logoFade{};
};

// Side effects out of ts2::game scope, exposed as an optional callback for fidelity
// (cf. header comment). No default does anything observable (no-op): the scene
// advances identically whether the host is wired or not.
struct IntroHost {
    // Called ONCE, on the 0 -> 1 sub-state crossing (0x51802c..0x51803d in the
    // original). Reproduces Util_SetClampedU8Field(&dword_8E714C, 0) + UI_FocusEditBox(
    // &g_UIEditBoxMgr, 0) on the ts2::ui side — to be wired by the caller if the Intro screen
    // already shows UI.
    std::function<void()> OnLogoSequenceBegin;
};

// UpdateIntro — advances the Intro state machine by one frame (pure function, no global
// state).
//
// `dt` is kept in the signature for API purposes but is NOT USED for counting:
// faithful to the original, which completely ignores its dt/float parameter and increments
// its counters once per CALL (implicitly 1 call = 1 tick @ 30 FPS, cf. cSceneMgr_Update
// 0x517BF0 called from the fixed-step game loop). Must be called exactly once per game
// frame to reproduce the actual duration.
//
// `host`: optional side effects out of scope (cf. IntroHost). May be omitted
// (IntroHost{} by default = no observable effect).
//
// Return value: true EXACTLY ONCE, on the frame where the final sub-state (34) reaches its
// kIntroFinalHoldFrames threshold — this is the "transition to ServerSelect ready" signal
// (exact mirror of the original's *a1 = 2 instruction). `state` is then reset
// (subState=0, frameCounter=0) to enable a faithful replay if ever re-selected,
// exactly as the original does (a1[1]=0, a1[2]=0 right after writing *a1=2).
// Returns false on every other frame.
bool UpdateIntro(IntroState& state, float dt, const IntroHost& host = IntroHost{});

} // namespace ts2::game

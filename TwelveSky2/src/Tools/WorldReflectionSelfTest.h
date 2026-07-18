// Tools/WorldReflectionSelfTest.h — TEMPORARY empirical verification tool ("SHADOW/
// REFLECTION EXTENSION" mission, 2026-07-14).
//
// TO REMOVE after verification: NOT part of the shipped client. Goal: prove, via a
// REAL D3D9 device + the REAL WorldRenderer::Render() path (Scene/WorldRenderer.h, NOT
// MODIFIED in its logic, only in the reflectionEligible guard added by this same
// mission), that:
//   1. Char_DrawReflection (drawReflectionOverlay) REALLY triggers for an active
//      monster in range (<=300 u from the local player, outside camera near-cull);
//   2. does NOT trigger for an active player in the SAME distance conditions
//      (confirming the reflectionEligible=monster-only restriction, faithful to the
//      fact that Char_DrawReflection 0x581090 has only one real caller in the binary,
//      inside the monster loop of Scene_InGameRender -- cf. banner in
//      Scene/WorldRenderer.h).
// Does NOT invent ANY network entity: populates game::g_World directly (the same
// mechanism already used by EntityManager to fill g_World in production, just without
// going through Net_RecvDispatch) with a local player + a remote player + a monster,
// all active and close together, so the behavior difference is observable in A SINGLE
// capture.
#pragma once

namespace ts2::tools {

// `seconds` = real duration (seconds) the window stays displayed after the scene is
// set up, to leave time for an external screenshot.
// `width`/`height` = real resolution of the test window (default 1024x768 if <=0).
int RunWorldReflectionSelfTest(int seconds, int width = 0, int height = 0);

} // namespace ts2::tools

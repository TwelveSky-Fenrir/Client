// Tools/CharSelectSelfTest.h — CharSelect / CreateChar screen PREVIEW harness.
//
// RATIONALE (Passe 5 diagnostic): in the normal flow, CharSelect (scene 4) is
// UNREACHABLE without a login/status server that responds — the ServerSelect
// population gate (game::OnServerClicked, currentPopulation>=0 && <max) swallows the
// server click while offline, and there is no fallback/mock. Yet the entire rendering
// pipeline (2D atlas + 3D character preview via CharPreview3D/ModelCache/MotionCache/
// MeshRenderer) is COMPLETE and functional — it is simply never exercised. This harness
// forces Scene::CharSelect, injects default character records (net::g_CharRecords) to
// populate the LIST (+ 3D preview), and routes mouse/keyboard to test CREATION (CRÉER
// button -> form + live 3D preview).
//
// Analogous to Tools/UiWindowSelfTest (which forces Scene::InGame). Verification tool,
// enabled via the `-charselecttest [seconds]` command line (seconds<=0 => until closed).
#pragma once

namespace ts2::tools {

// Opens a window, initializes renderer/scene, forces CharSelect with 2 default
// characters, and pumps an interactive loop (Update+Render at 30 FPS) for `seconds`
// (<=0 = until the window is closed). Returns 0 on success. width/height <=0 =>
// kRefWidth/kRefHeight.
int RunCharSelectSelfTest(int seconds, int width, int height);

} // namespace ts2::tools

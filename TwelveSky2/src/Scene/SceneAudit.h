// Scene/SceneAudit.h — TEMPORARY AUDIT harness (mission "UIManager::Init
// -> GameWindows -> SceneManager chain", 2026-07-14). NOT a client component: it
// ONLY drives the REAL SceneManager (public API only, see
// Scene/SceneManager.h — Init/Change/OnKeyDown/Render) to force entry into
// the InGame scene WITHOUT going through the network handshake (no game server
// available in this verification environment). Unlike an isolated test like
// Asset/AssetSelfTest.cpp, this harness NEVER populates a UiContext itself:
// it only calls the same code path as main.cpp/App.cpp
// (SceneManager::Change -> GameWindows::Init -> UIManager::Instance().Init),
// so a visual proof here is proof of the REAL path, not a mock.
//
// NOTE: actual verification of this mission ended up reusing
// Tools/UiWindowSelfTest.h (equivalent tool already present, extended with the
// "options" option). This file is kept (vcxproj entries already generated) but is
// no longer the path called from main.cpp — to be removed along with Tools/UiWindowSelfTest.*
// once all temporary audits are closed.
#pragma once
#include <string>

namespace ts2 {

// Creates a real window + D3D9 device, initializes SceneManager, forces
// Change(Scene::InGame), opens the Options window ('O' key -> HandleHotkey),
// renders a few frames then leaves the window on screen for `holdSeconds` seconds
// (for an external screenshot) before closing cleanly.
int RunSceneAudit(const std::string& gameDataDir, int holdSeconds);

} // namespace ts2

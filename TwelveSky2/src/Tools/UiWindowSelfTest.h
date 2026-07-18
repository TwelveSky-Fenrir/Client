// Tools/UiWindowSelfTest.h — TEMPORARY empirical verification tool (real-device audit
// VendorShopWindow/WarehouseWindow/InventoryWindow, mission 2026-07-14).
//
// TO REMOVE after verification: NOT part of the shipped client. Goal: prove, via a
// REAL visible window + a REAL D3D9 device + the REAL production path
// (SceneManager::Init/Change -> GameWindows::Init -> UIManager::Init), that these 3
// windows display real textures and not a permanent fallback. Does NOT invent ANY
// parallel UiContext/Dialog object: reuses SceneManager as-is (Scene/SceneManager.h,
// NOT MODIFIED) with a real device/window, forcing Scene::InGame directly — EXACTLY
// the same state as the "EnterWorld: flow failed -> fall back to InGame" fallback
// already present in SceneManager::Update (Scene::EnterWorld), hence a state reachable
// in production, not an isolated test artifice.
#pragma once

namespace ts2::tools {

// `which` = "vendor" | "warehouse" | "inventory" | "options" (window to open via the
// real hotkey; "options" added to scope by the UIManager::Init -> GameWindows ->
// SceneManager chain audit, 2026-07-14, to prove the PanelSkin background of the 13
// windows registered by GameWindows::Init, not just Vendor/Warehouse/Inventory).
// `seconds` = real duration (seconds) the window stays displayed after opening, to
// leave time for an external screenshot.
// `width`/`height` = real resolution of the test window (ADDED coordinate system audit
// 2026-07-14): allows replaying EXACTLY the same production path
// (SceneManager::Init -> GameWindows::Init -> UIManager::Init) at a NON-1024x768
// resolution, to empirically verify UI window repositioning (or its absence). Default
// 1024x768 (kRefWidth/kRefHeight) if <=0.
int RunUiWindowSelfTest(const char* which, int seconds, int width = 0, int height = 0);

} // namespace ts2::tools

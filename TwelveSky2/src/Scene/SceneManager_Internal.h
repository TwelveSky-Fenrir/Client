// Scene/SceneManager_Internal.h — file-local state shared across the SceneManager.cpp split
// family (SceneManager.cpp / SceneManager_Update.cpp / SceneManager_Render.cpp).
// #pragma once, inline-only content, included only by that split family (never a public API).
#pragma once

namespace ts2 {

// SCN-01 — deferred actions armed by Notice_DispatchOkAction (SceneManager.cpp, switch
// @0x5C04C9 of UI_NoticeDlg_OnLButtonUp 0x5C03F0) and consumed one frame later by
// SceneManager::Update (SceneManager_Update.cpp, case Scene::InGame). Declared `inline` here
// (rather than file-local `static`/anonymous-namespace, as in the original single-file layout)
// so both translation units share the SAME variable across the split.

// Return to server-select requested by `case 2` (@0x5C04E4 `g_SceneMgr = 2`). Same deferral
// pattern as game::g_World.sceneEnterWorldPending / sceneReloadPending: don't change scene
// mid click-routing (the UI registry being iterated would be destroyed). The OK button is the
// SOLE writer -- no other cross-front dependency. // 0x5C04E4
inline bool g_noticeReturnToServerSelectPending = false;
// Abnormal end requested by `case 3` (@0x5C051B `g_QuitFlag = 1`). Same mechanism.
inline bool g_noticeAbnormalEndPending = false;

} // namespace ts2

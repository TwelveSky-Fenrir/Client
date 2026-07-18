// App/App_Internal.h — shared file-local state for the App.cpp split family
// (App.cpp / App_Init.cpp / App_WndProc.cpp). Only inline/constexpr content: included by
// every translation unit of the split, never a standalone compilation unit.
#pragma once

#include "App/PlayerInputController.h"

namespace ts2 {

// W1-F2: in-game keyboard controller (Camera_UpdateFromInput 0x50B7D0). In the original,
// its state lives in the global object g_CameraCtrl 0x1668F60 (distinct from the renderer
// singletons), initialized by mINPUT Camera_Init 0x50ABC0. App.h is not editable by this
// front (file not owned), so the instance lives here at file scope — the client only ever
// has one App, hence one instance, faithful to the original singleton. `inline` gives this
// a single shared definition across the split family's translation units (App::Init wires
// it up, App::FrameTick drives it per frame).
inline PlayerInputController g_playerInput;

} // namespace ts2

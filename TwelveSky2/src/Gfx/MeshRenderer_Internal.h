// Gfx/MeshRenderer_Internal.h - constants shared across the MeshRenderer.cpp split family
// (MeshRenderer.cpp / MeshRenderer_Skinning.cpp / MeshRenderer_Shadow.cpp).
// #pragma once, inline/constexpr content only; not part of the public MeshRenderer.h API.
#pragma once

namespace ts2::gfx {

namespace {

// Degrees -> radians conversion used by every world-matrix composition
// (Model_Render 0x40EBB0 / Model_RenderWithShadow 0x40EEE0 / Model_RenderPlanarShadow 0x40F720).
constexpr float kDeg2Rad = 0.017453292519943295f; // 0.017453292 (deg->rad, x pi/180)

} // namespace

} // namespace ts2::gfx

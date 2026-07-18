// Scene/WorldRenderer_Internal.h — shared file-local constants/helpers for the
// WorldRenderer.cpp split family (WorldRenderer.cpp / WorldRenderer_Shadows.cpp /
// WorldRenderer_Entities.cpp / WorldRenderer_Nameplates.cpp). Anonymous-namespace
// helpers used by more than one of these translation units, promoted to inline
// content so each TU sees exactly one definition — same values and behavior as the
// single anonymous namespace this content originally lived in inside WorldRenderer.cpp.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>

#include "Gfx/MotionCache.h"

namespace ts2 {

// CPU cache of animated bone palettes (mirror of g_ModelMotionArray 0x8E8B30). WorldRenderer.h
// cannot receive a new member (no member slot was available when this cache was introduced), so
// it is instanced as a process-lifetime singleton here — pure CPU data, no D3D device required
// (cf. MotionCache). `inline` (not an anonymous-namespace free function) is REQUIRED here: every
// translation unit of the split family must resolve to the SAME MotionCache instance, because the
// tick (Model_GetMotionFrameCount 0x4E5A70 and friends) and the draw (Char_Draw @0x580770) resolve
// the slot via the SAME accessor on the SAME g_ModelMotionArray — this is the fidelity-critical
// invariant: frameCount from the tick must equal frameCount of the palette actually drawn. Letting
// these two sources diverge (e.g. one .cpp getting its own separate cache instance) would
// desynchronize wrap and sampling. gameDataDir="." : same convention/reason as modelCache_ (the
// process CWD is already switched to gameDataDir since App::Init, well before any rendering — cf.
// WorldRenderer::Init).
inline gfx::MotionCache& Motions() {
    static gfx::MotionCache m(".");
    return m;
}

// LE u32 read from PlayerEntity::body at the given offset ; 0 if the offset (+4 bytes) exceeds the
// array size (defensive guard — should never trigger with the constant offsets used by callers,
// all < 600, but avoids UB should body's size ever change). Shared here because both
// WorldRenderer.cpp (Render()) and WorldRenderer_Nameplates.cpp (drawNameplatePass) read
// PlayerEntity::body through this accessor.
inline uint32_t ReadBodyU32LE(const std::array<uint8_t, 600>& body, size_t offset) {
    if (offset + sizeof(uint32_t) > body.size()) return 0;
    uint32_t v = 0;
    std::memcpy(&v, body.data() + offset, sizeof(v)); // x86 LE host: no reorder needed
    return v;
}

} // namespace ts2

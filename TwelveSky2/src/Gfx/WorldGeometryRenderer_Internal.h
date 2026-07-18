// Gfx/WorldGeometryRenderer_Internal.h — small helper shared by the WorldGeometryRenderer.cpp /
// WorldGeometryRenderer_Terrain.cpp / WorldGeometryRenderer_Objects.cpp / WorldGeometryRenderer_FxSky.cpp
// split family. Include-only, no .cpp of its own.
#pragma once

namespace ts2::gfx {

// Releases a D3D9 COM object and nulls the pointer. Used by releaseObjects()/releaseTerrain()/
// releaseFx() (WorldGeometryRenderer.cpp) and by uploadPart() (WorldGeometryRenderer_Objects.cpp).
template <class T>
void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

} // namespace ts2::gfx

// Gfx/SkyboxCube.h — FAITHFUL, STANDALONE reconstruction of Env_RenderSkyCube 0x6a8f60
// (the 6-face "skybox cube" of the TwelveSky2.exe GXD engine, EU build).
//
// =======================================================================================
// INVESTIGATION VERDICT: DEAD CODE in this build (the cube is NEVER drawn).
// ---------------------------------------------------------------------------------------
//   Env_RenderSkyCube 0x6a8f60 has ONE caller: Gfx_BeginFrame 0x6a2280, which only invokes
//   it under the gate `if (a2 && *a2)` (@0x6a24dc/@0x6a24de -> @0x6a24e3). But all EIGHT
//   call sites of Gfx_BeginFrame push a2 = NULL (`push 0`):
//     - Scene_IntroRender        @0x5188c1  (push 0) -> call 0x5188c8
//     - Scene_ServerSelectRender @0x5192a1  (push 0) -> call 0x5192a8
//     - Scene_LoginRender        @0x51b071  (push 0) -> call 0x51b078
//     - Scene_CharSelectRender   @0x51cf21  (push 0) -> call 0x51cf28
//     - Scene_EnterWorldRender   @0x52c2b1  (push 0) -> call 0x52c2b8
//     - Scene_InGameRender       @0x52d133  (push 0) -> call 0x52d13a  (sub-state 0/1)
//     - Scene_InGameRender       @0x52d180  (push 0) -> call 0x52d187  (sub-state 2)
//     - Scene_InGameRender       @0x52d1f5  (push 0) -> call 0x52d1fc  (sub-state >=3, full frame)
//   => a2 is always NULL => the `a2 && *a2` gate is always false => Env_RenderSkyCube is
//   never reached. The "sky" object (the `this` of 0x6a8f60) is therefore never
//   constructed, and in-game sky rendering goes entirely through SilverLining
//   (Env_UpdateFrame 0x412550 / Env_StepTimeOfDay 0x412590, cf. TS2_DEEP_FRAME_PIPELINE.md
//   §3/§6). On the ClientSource side, the FAITHFUL sky representation of this build thus
//   remains the gradient in Gfx/SkyRenderer.* (.ATM fallback) — NOT this cube.
//
// SOURCE OF THE 6 TEXTURES — NOT STATICALLY PROVABLE (honest, NOT invented):
//   Env_RenderSkyCube does not LOAD any texture: it reads an IDirect3DTexture9* already
//   present in the sky object, per face, at `this+13` (dword index 13 = byte 52), with a
//   stride of 13 dwords = 52 bytes between faces (loop @0x6a93b1: `v7 += 13`). The loader
//   that would fill these 6 slots lives in the code that constructs the sky object — code
//   that is never reached (see above). No asset path (.IMG/.DDS of the zone) is therefore
//   demonstrable; none is FABRICATED. This module thus stays INERT by default: without
//   SetFaceTextures(), it draws untextured faces, and by convention IsActive()==false as
//   long as no texture is provided.
//
// WHY THIS MODULE EXISTS ANYWAY (fidelity):
//   The BODY of Env_RenderSkyCube (geometry generation + D3D9 state sequence + 6-face draw
//   loop) is ENTIRELY proven by disassembly (0x6a8f60..0x6a944c). We provide a bit-faithful,
//   STANDALONE transcription (no dependency on the GXD renderer, no wiring into the frame
//   loop — FLEET C/MAIN will decide if/when to use it). If a future mission identified at
//   runtime (x32dbg) the sky object and its texture loader, this module would be ready to
//   feed without a rewrite.
//
// PROVEN GLOBAL ANCHORS:
//   - flt_7FFEA0 0x7FFEA0 = g_GfxRenderer+136 = FAR PLANE, written by Cam_SetProjection
//     0x69cbef via `*(float*)(this+136)=a3` @0x69cbd5 (register-relative store: hence the
//     absence of an absolute xref). Static image value = 0.0 (runtime cache). Cube side =
//     far/sqrt(3) (@0x6a8f9b: `fdivr flt_7FFEA0`).
//   - dword_7FFEA4 0x7FFEA4 = g_GfxRenderer+140 = FOG-ENABLE flag (read by Cam_SetProjection
//     @0x69cc06). Controls the cube's FOG bracket (RS 28), NOT culling (the "inverse cull"
//     hypothesis is refuted by the decompile: RS 28 = D3DRS_FOGENABLE).
//   - g_CameraPos 0x800130 (= g_GfxRenderer+792) = camera eye; the cube is translated onto
//     it (@0x6a936e/@0x6a9385) => "infinite" camera-centered skybox.
// =======================================================================================
#pragma once
#include <d3d9.h>

namespace ts2::gfx {

// Standalone reconstruction of the sky cube from Env_RenderSkyCube 0x6a8f60.
// Owns NO D3DPOOL_DEFAULT resource: geometry lives in RAM (DrawPrimitiveUP) and the face
// textures are NON-owned pointers injected by the owner (the zone sky object). Safe against
// device-lost: nothing to release/recreate; the owner simply needs to re-supply its textures
// after a reset.
class SkyboxCube {
public:
    // Cube vertex: 20 bytes, FVF D3DFVF_XYZ | D3DFVF_TEX1 (0x102) @0x6a934f.
    struct Vertex { float x, y, z, u, v; };

    // Radius = renderer far plane (flt_7FFEA0 = g_GfxRenderer+136). Cube side = radius/sqrt(3)
    // (@0x6a8f9b). Invalidates the geometry cache if the value changes (mirrors the
    // @0x6a8f88 test on this+210).
    void  SetRadius(float radius);
    float Radius() const { return radius_; }

    // Injects the 6 face textures (NOT owned, lifetime managed by the owner). Order =
    // draw order of the @0x6a93b1 loop (this+13, 52-byte stride): see the table in the
    // .cpp for each face's orientation. `faces == nullptr` resets all 6 slots to NULL.
    void               SetFaceTextures(IDirect3DTexture9* const faces[6]);
    IDirect3DTexture9* FaceTexture(int i) const;

    // Mirror of the original gate (Gfx_BeginFrame `a2 && *a2` + radius set by Cam_SetProjection):
    // true if radius > 0 AND at least one face texture is provided. Without that, the cube
    // would stay inert (the real build never reaches it — see banner). Helps the caller
    // decide whether it's worth issuing the draw.
    bool IsActive() const;

    // Draws the cube centered on camPos with the EXACT state sequence of Env_RenderSkyCube.
    // fogEnabled = mirror of dword_7FFEA4 (g_GfxRenderer+140): brackets the draw with a
    // disable/enable of D3DRS_FOGENABLE. No-op if dev == nullptr or radius <= 0.
    // NOTE: reproduces ONLY the states the binary modifies (see .cpp). Untouched states
    // (cull, texture-stage) are inherited from the calling pipeline, as in the original.
    void Render(IDirect3DDevice9* dev, const float camPos[3], bool fogEnabled);

    // Geometry access (inspection / tests). 24 vertices = 6 faces x 4.
    const Vertex* Vertices() const { return verts_; }

private:
    // Rebuilds the 24 vertices if the radius has changed. Env_RenderSkyCube @0x6a8f9b..0x6a92c6.
    void rebuild();

    float radius_      = 0.0f;   // flt_7FFEA0 (far plane); 0 = inactive (static image value = 0)
    float cacheRadius_ = -1.0f;  // this+210 (byte 0x348): last built radius (@0x6a92c6)
    IDirect3DTexture9* faceTex_[6] = { nullptr }; // this+13, 52-byte stride (1st dword of each face)
    Vertex verts_[24] = {};      // this+211 (6 faces x 4 vertices), 20 bytes/vertex
};

} // namespace ts2::gfx

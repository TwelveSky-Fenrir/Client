// Gfx/SkyboxCube.cpp — bit-faithful transcription of the BODY of Env_RenderSkyCube 0x6a8f60.
// See SkyboxCube.h for the VERDICT (dead code in this build) and texture provenance.
//
// WARNING: STANDALONE MODULE, NOT WIRED IN: no caller in this commit. The real build never
//    reaches Env_RenderSkyCube (the 8 Gfx_BeginFrame call sites pass a2=NULL, cf. .h); this
//    file exists to keep the EXACT geometry/states on hand, ready if a future mission were to
//    reactivate the layer (after identifying the sky object at runtime).
//    The FAITHFUL sky representation of this build remains Gfx/SkyRenderer.* (.ATM gradient).
#include "Gfx/SkyboxCube.h"

#include <cmath>

#pragma comment(lib, "d3d9.lib")

namespace ts2::gfx {

// Rebuilds the 24 vertices. Env_RenderSkyCube @0x6a8f9b..0x6a92c6.
// Proven .rdata constants (read from the IDB): flt_7A939C = +0.5 (0x3F000000),
// flt_7BB294 = -0.5 (0xBF000000), flt_7EDA10 = -1.0 (0xBF800000), dbl 3.0 under the sqrt.
void SkyboxCube::rebuild() {
    // Cache @0x6a8f88 / @0x6a92c6: only rebuild if the radius (flt_7FFEA0) has changed.
    if (cacheRadius_ == radius_) return;

    // @0x6a8f9b: s = flt_7FFEA0 / sqrt(3.0). s = half-edge of faces 1 through 5.
    const float s = radius_ / std::sqrt(3.0f);
    // WARNING - FAITHFUL ASYMMETRY (decompile 0x6a9031..0x6a9087): face 0 (+Z) is built at
    //    ±0.5*s (flt_7BB294 = -0.5 / flt_7A939C = +0.5), while faces 1 through 5 use
    //    ±s (factor 1.0) and -s (flt_7EDA10 = -1.0). REPRODUCE AS-IS: the binary IS
    //    that way (face +Z is half-size). Do NOT "fix" this into a symmetric cube.
    const float h = 0.5f * s;

    // Identical UVs on all 6 faces (proven: v0=(0,1) v1=(0,0) v2=(1,1) v3=(1,0), stores
    // ebx=0.0/eax=1.0 @0x6a8fa1.. and per-face). Positions = step-by-step trace of the x87 stack.
    const Vertex verts[24] = {
        // Face 0 (+Z, faceTex_[0]) — HALF SIZE h. @0x6a9039..0x6a9087
        { -h, -h,  h, 0.0f, 1.0f }, { -h,  h,  h, 0.0f, 0.0f },
        {  h, -h,  h, 1.0f, 1.0f }, {  h,  h,  h, 1.0f, 0.0f },
        // Face 1 (-Z, faceTex_[1]). @0x6a908d..0x6a90dd
        {  s, -s, -s, 0.0f, 1.0f }, {  s,  s, -s, 0.0f, 0.0f },
        { -s, -s, -s, 1.0f, 1.0f }, { -s,  s, -s, 1.0f, 0.0f },
        // Face 2 (-X, faceTex_[2]). @0x6a90e3..0x6a9135
        { -s, -s, -s, 0.0f, 1.0f }, { -s,  s, -s, 0.0f, 0.0f },
        { -s, -s,  s, 1.0f, 1.0f }, { -s,  s,  s, 1.0f, 0.0f },
        // Face 3 (+X, faceTex_[3]). @0x6a913b..0x6a919d
        {  s, -s,  s, 0.0f, 1.0f }, {  s,  s,  s, 0.0f, 0.0f },
        {  s, -s, -s, 1.0f, 1.0f }, {  s,  s, -s, 1.0f, 0.0f },
        // Face 4 (+Y top, faceTex_[4]). @0x6a91a3..0x6a91fd
        { -s,  s,  s, 0.0f, 1.0f }, { -s,  s, -s, 0.0f, 0.0f },
        {  s,  s,  s, 1.0f, 1.0f }, {  s,  s, -s, 1.0f, 0.0f },
        // Face 5 (-Y bottom, faceTex_[5]). @0x6a9205..0x6a92bb
        { -s, -s, -s, 0.0f, 1.0f }, { -s, -s,  s, 0.0f, 0.0f },
        {  s, -s, -s, 1.0f, 1.0f }, {  s, -s,  s, 1.0f, 0.0f },
    };
    for (int i = 0; i < 24; ++i) verts_[i] = verts[i];

    cacheRadius_ = radius_; // @0x6a92c6: this+210 = flt_7FFEA0 (last built radius)
}

void SkyboxCube::SetRadius(float radius) {
    if (radius != radius_) {
        radius_ = radius;
        cacheRadius_ = -1.0f; // forces rebuild on the next rebuild() call
    }
}

void SkyboxCube::SetFaceTextures(IDirect3DTexture9* const faces[6]) {
    for (int i = 0; i < 6; ++i) faceTex_[i] = faces ? faces[i] : nullptr;
}

IDirect3DTexture9* SkyboxCube::FaceTexture(int i) const {
    return (i >= 0 && i < 6) ? faceTex_[i] : nullptr;
}

bool SkyboxCube::IsActive() const {
    if (radius_ <= 0.0f) return false;
    for (int i = 0; i < 6; ++i) if (faceTex_[i]) return true;
    return false;
}

// EXACT state sequence of Env_RenderSkyCube 0x6a8f60 (starting at @0x6a92d7). We reproduce
// ONLY the states the binary modifies; cull and texture-stage states are NOT touched by the
// original (inherited from the calling pipeline) — so we do not add them.
void SkyboxCube::Render(IDirect3DDevice9* dev, const float camPos[3], bool fogEnabled) {
    if (!dev || radius_ <= 0.0f) return;
    rebuild();

    // @0x6a92d7: SetRenderState(RS 7 = D3DRS_ZENABLE, FALSE). Depth test is DISABLED
    //   while drawing the sky (the cube fills the screen; the world draws over it
    //   afterward). WARNING: RS 7 = D3DRS_ZENABLE (test), NOT D3DRS_ZWRITEENABLE (=14).
    dev->SetRenderState(D3DRS_ZENABLE, FALSE);

    // @0x6a9324: Gfx_SetLight(g_GfxRenderer, 1, ...) 0x69d5c0 configures light slot 0 from
    //   renderer material fields (Object A, +1204/+1236...). OMITTED HERE: (a) those fields
    //   belong to the GXD renderer, not owned by this standalone module; (b) the cube's FVF
    //   (XYZ|TEX1) has NEITHER normal NOR diffuse => lighting has NO visual effect on the
    //   cube. The call is therefore visually inert for this geometry.

    // @0x6a933c: if dword_7FFEA4 (fog active), SetRenderState(RS 28 = D3DRS_FOGENABLE, FALSE).
    if (fogEnabled) dev->SetRenderState(D3DRS_FOGENABLE, FALSE);

    // @0x6a934f: SetFVF(0x102 = D3DFVF_XYZ | D3DFVF_TEX1) (vtbl+356).
    dev->SetFVF(D3DFVF_XYZ | D3DFVF_TEX1);

    // @0x6a936e: D3DXMatrixTranslation(&m, g_CameraPos.x/y/z) then @0x6a9385
    //   SetTransform(D3DTS_WORLD = 256, &m) (vtbl+176) => cube centered on the camera eye
    //   (infinite skybox). Identity matrix + translation set by hand (bit-exact equivalent,
    //   no dependency on the legacy D3DX layer).
    const float cx = camPos ? camPos[0] : 0.0f;
    const float cy = camPos ? camPos[1] : 0.0f;
    const float cz = camPos ? camPos[2] : 0.0f;
    D3DMATRIX world = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        cx,   cy,   cz,   1.0f,
    };
    dev->SetTransform(D3DTS_WORLD, &world);

    // @0x6a9398 / @0x6a93ab: SetSamplerState(0, ADDRESSU/ADDRESSV, D3DTADDRESS_CLAMP = 3)
    //   (vtbl+276) — clamp to avoid edge bleeding between faces.
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    // 6-face loop @0x6a93b1..0x6a93ee: SetTexture(0, faceTex[i]) (@0x6a93cc, vtbl+260);
    //   DrawPrimitiveUP(D3DPT_TRIANGLESTRIP = 5, PrimitiveCount = 2, &face, Stride = 20)
    //   (@0x6a93e1, vtbl+332). In the original, the texture pointer advances by 52 bytes (this+13,
    //   `v7 += 13`) and the vertex pointer by 80 bytes (`v6 += 20` dwords = 4 vertices x 20 bytes).
    for (int i = 0; i < 6; ++i) {
        dev->SetTexture(0, faceTex_[i]);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, &verts_[i * 4], sizeof(Vertex));
    }

    // @0x6a93fd / @0x6a9410: restore D3DTADDRESS_WRAP (= 1) on ADDRESSU/ADDRESSV.
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);

    // @0x6a942a: if fog, SetRenderState(RS 28 = D3DRS_FOGENABLE, TRUE) — symmetric with the disable.
    if (fogEnabled) dev->SetRenderState(D3DRS_FOGENABLE, TRUE);

    // @0x6a9435: Gfx_SkyboxEndState 0x69d780 = SetLight(0, &g_GfxRenderer+1200) (vtbl+204),
    //   reapplies the renderer's scene light (Object A). OMITTED HERE (same reason as the
    //   Gfx_SetLight above: renderer state not owned, no effect on the cube's FVF).

    // @0x6a9446: SetRenderState(RS 7 = D3DRS_ZENABLE, TRUE) — restores the depth test.
    dev->SetRenderState(D3DRS_ZENABLE, TRUE);
}

} // namespace ts2::gfx

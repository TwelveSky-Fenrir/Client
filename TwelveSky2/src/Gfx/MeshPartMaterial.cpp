// Gfx/MeshPartMaterial.cpp — LINE-BY-LINE port of MeshPart_RenderFull 0x6B0850 (fixed-function
// material state machine for GXD static-object parts). Ground truth = IDA (idaTs2, TwelveSky2.exe,
// imagebase 0x400000). Each block carries its 0xADDR anchor. Cf. MeshPartMaterial.h banner and
// Docs/TS2_DEEP_MESHPART_MATERIAL.md (§4 exact order, §5 helpers, §6 D3D decode tables).
//
// Anchor convention: the binary's raw SetRenderState / SetTextureStageState numbers are
// rendered as the EQUIVALENT D3D9 enums (identical values):
//   RS 14=D3DRS_ZWRITEENABLE  15=D3DRS_ALPHATESTENABLE  24=D3DRS_ALPHAREF  25=D3DRS_ALPHAFUNC
//      27=D3DRS_ALPHABLENDENABLE  29=D3DRS_SPECULARENABLE  60=D3DRS_TEXTUREFACTOR
//   TSS 4=D3DTSS_ALPHAOP  6=D3DTSS_ALPHAARG2  24=D3DTSS_TEXTURETRANSFORMFLAGS
//   TSS values: 10=D3DTOP_SUBTRACT 3=D3DTOP_SELECTARG2/D3DTA_TFACTOR 2=D3DTOP_SELECTARG1
//                 1=D3DTA_CURRENT ; TTFF 2=D3DTTFF_COUNT2 0=D3DTTFF_DISABLE
//   SetTransform 16=D3DTS_TEXTURE0  256=D3DTS_WORLD
//   ALPHAFUNC 5=D3DCMP_GREATER 8=D3DCMP_ALWAYS
#include "Gfx/MeshPartMaterial.h"
#include <cstring>

namespace ts2::gfx {

namespace {

// Literal D3DCOLORVALUE (avoids aggregate initialization, portable across MSVC v143).
inline D3DCOLORVALUE col(float r, float g, float b, float a) {
    D3DCOLORVALUE c; c.r = r; c.g = g; c.b = b; c.a = a; return c;
}

// ------------------------------------------------------------------------------------------
// Gfx_SetMaterialEmissive 0x69D1F0 — sets the "glow" D3DMATERIAL9 (Specular, NOT Emissive).
//   Diffuse=(1,1,1,1) Ambient=(1,1,1,1) Specular=SpecRGBA Emissive=(0,0,0,1) Power=SpecPower.
//   Mirrors dwords v7[0..16] @0x69d20f-@0x69d306, SetMaterial(vtbl+196) @0x69d31e.
// ------------------------------------------------------------------------------------------
void setGxdMaterialGlow(IDirect3DDevice9* dev, const float spec[4], float power) {
    D3DMATERIAL9 m; std::memset(&m, 0, sizeof(m));
    m.Diffuse  = col(1.0f, 1.0f, 1.0f, 1.0f);          // v7[0..3]
    m.Ambient  = col(1.0f, 1.0f, 1.0f, 1.0f);          // v7[4..7]
    m.Specular = col(spec[0], spec[1], spec[2], spec[3]); // v7[8..11] = a2[0..3]
    m.Emissive = col(0.0f, 0.0f, 0.0f, 1.0f);          // v7[12..14]=0 ; v7[15]=1.0
    m.Power    = power;                                 // v7[16] = a3
    dev->SetMaterial(&m);
}

// ------------------------------------------------------------------------------------------
// Gfx_SetShadowProjLight 0x69D7A0 — DIRECTIONAL light slot 1 aimed at the camera +
//   specular ON. Specular = (sceneCenter + lightOffset) * intensity; Direction = at - eye.
//   Mirrors @0x69d7ce-@0x69d9ae (SetLight(1,·) vtbl+204; LightEnable(1,1) vtbl+212;
//   SetRenderState(29,1) vtbl+228).
// ------------------------------------------------------------------------------------------
void setGxdShadowProjLight(IDirect3DDevice9* dev, float lightOffset, float intensity,
                           const MeshPartRuntime& rt) {
    D3DLIGHT9 L; std::memset(&L, 0, sizeof(L));
    L.Type     = D3DLIGHT_DIRECTIONAL;                  // v19[0] = 3
    L.Diffuse  = col(0.0f, 0.0f, 0.0f, 1.0f);           // v19[1..3]=0 ; v19[4]=1.0
    L.Specular = col((rt.sceneCenter.x + lightOffset) * intensity,   // v13 @0x69d7f0
                     (rt.sceneCenter.y + lightOffset) * intensity,   // v15 @0x69d854
                     (rt.sceneCenter.z + lightOffset) * intensity,   // v17 @0x69d8a0
                     1.0f);                                          // v19[8]=1.0
    L.Ambient  = col(0.0f, 0.0f, 0.0f, 1.0f);           // v19[9..11]=0 ; v19[12]=1.0
    // Direction = cameraAt - cameraEye (this+804..812 − this+792..800) @0x69d8ae/@0x69d8f0/@0x69d906.
    L.Direction.x = rt.cameraAt.x - rt.cameraEye.x;
    L.Direction.y = rt.cameraAt.y - rt.cameraEye.y;
    L.Direction.z = rt.cameraAt.z - rt.cameraEye.z;
    dev->SetLight(1, &L);                               // @0x69d982
    dev->LightEnable(1, TRUE);                          // @0x69d995
    dev->SetRenderState(D3DRS_SPECULARENABLE, TRUE);    // @0x69d9ae
}

// ------------------------------------------------------------------------------------------
// Gfx_SetLight 0x69D5C0 (slot 0) — DIRECTIONAL light, Direction=(-1,-1,1). Mode 1 =>
//   Ambient = sceneCenter (color = scene AABB center); mode 2 => Ambient = (r,g,b,a) given.
//   Mirrors @0x69d5d1-@0x69d779 (SetLight(0,·) vtbl+204).
// ------------------------------------------------------------------------------------------
void setGxdLight0(IDirect3DDevice9* dev, int mode, float r, float g, float b, float a,
                  const MeshPartRuntime& rt) {
    D3DLIGHT9 L; std::memset(&L, 0, sizeof(L));
    L.Type     = D3DLIGHT_DIRECTIONAL;                  // v16[0] = 3
    L.Diffuse  = col(0.0f, 0.0f, 0.0f, 1.0f);           // v16[1..3]=0 ; v16[4]=1.0
    L.Specular = col(0.0f, 0.0f, 0.0f, 1.0f);           // v16[5..7]=0 ; v16[8]=1.0
    if (mode == 1)                                      // @0x69d63a: color = scene AABB center
        L.Ambient = col(rt.sceneCenter.x, rt.sceneCenter.y, rt.sceneCenter.z, 1.0f); // v13/v14/v15 ; v9=1.0
    else                                                // mode 2 @0x69d6a1: color given
        L.Ambient = col(r, g, b, a);                    // v16[9..12] = a3/a4/a5/a6
    L.Direction.x = -1.0f; L.Direction.y = -1.0f; L.Direction.z = 1.0f; // v16[16]=-1 v16[17]=-1 v16[18]=1
    dev->SetLight(0, &L);                               // @0x69d779
}

// ------------------------------------------------------------------------------------------
// Texture matrix for a UV-scroll: identity + translation (_31,_32) per mode 1..4.
// Mirrors the switch @0x6b1002 (tex1) / @0x6b1a64 (tex2). v83=_31, v84=_32; scroll = v66*Speed.
//   1: (_31=scroll,_32=0)  2: (_31=0,_32=scroll)  3: (scroll,scroll)  4: (scroll,-scroll)  other: identity.
// ------------------------------------------------------------------------------------------
void buildUvScrollMatrix(D3DXMATRIX& m, uint32_t mode, float scroll) {
    D3DXMatrixIdentity(&m);                             // v75/v80/v85/v90 = 1.0 (diag); rest 0
    switch (mode) {
        case 1: m._31 = scroll; m._32 = 0.0f;   break; // @0x6b1008/@0x6b1016
        case 2: m._31 = 0.0f;   m._32 = scroll; break; // @0x6b1025/@0x6b102d
        case 3: m._31 = scroll; m._32 = scroll; break; // @0x6b103e/@0x6b1044
        case 4: m._31 = scroll; m._32 = -scroll; break;// @0x6b1053/@0x6b105d
        default: break;                                // identity (zero translation)
    }
}

// ------------------------------------------------------------------------------------------
// Base-draw @0x6B1327 (frame-indexed SetStreamSource + SetIndices + DrawIndexedPrimitive). Reused
// as the honest fallback for the billboard branch (in place of the non-reproducible DrawPrimitiveUP).
// ------------------------------------------------------------------------------------------
void drawIndexed(IDirect3DDevice9* dev, const MeshPartGpu& geo, uint32_t frame) {
    if (!geo.vb || !geo.ib || geo.vertsPerFrame == 0 || geo.triCount == 0) return;
    dev->SetStreamSource(0, geo.vb, 32u * frame * geo.vertsPerFrame, 32u);          // 32*frame*B @0x6b1327
    dev->SetIndices(geo.ib);                                                         // @0x6b133c
    dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, geo.vertsPerFrame, 0, geo.triCount); // @0x6b1360
}

// View-dependent specular glow (modes 1/2). Called @0x6B0A11 (with gate !this[99]) and as re-glow
// @0x6B168D (2nd-texture branch). Returns true if a specular material was applied (=> v91=1).
bool applyGlow(IDirect3DDevice9* dev, const asset::MeshPartMaterial& mat,
               const MeshPartGpu& geo, uint32_t frame, const MeshPartRuntime& rt) {
    const uint32_t mode = mat.glow.Mode;                // this[46] @0x6b0a1f
    if (mode == 1) {                                    // CONSTANT specular @0x6b0a2c
        setGxdMaterialGlow(dev, mat.glow.SpecRGBA, mat.glow.SpecPower);  // @0x6b0a48
        setGxdShadowProjLight(dev, mat.lightOffset, 1.0f, rt);          // @0x6b0a59 (intensity 1.0)
        return true;
    }
    if (mode == 2) {                                    // view-dependent (fresnel) @0x6b0a61
        // Without a usable node/world, we do NOT fabricate a direction -> no flash (safe fallback).
        if (!rt.worldValid || !geo.frameNodes) return false;
        D3DXVECTOR3 nodeLocal;
        std::memcpy(&nodeLocal, geo.frameNodes + static_cast<size_t>(frame) * 64 + 48, sizeof(nodeLocal));
        D3DXVECTOR3 nodeWorld;
        D3DXVec3TransformCoord(&nodeWorld, &nodeLocal, &rt.world);       // Vec3_TransformCoord @0x6b0a81
        D3DXVECTOR3 v;                                                    // eye - node @0x6b0a90
        v.x = rt.cameraEye.x - nodeWorld.x;
        v.y = rt.cameraEye.y - nodeWorld.y;
        v.z = rt.cameraEye.z - nodeWorld.z;
        D3DXVec3Normalize(&v, &v);                                       // @0x6b0ad0
        // dot(-sunDir, v) @0x6b0b37 (v53/v57/v61 = flt_800308/30C/310 * -1.0)
        const float d = (-rt.sunDir.z) * v.z + (-rt.sunDir.y) * v.y + (-rt.sunDir.x) * v.x;
        if (d > 0.0f) {                                                  // @0x6b0b4b
            setGxdMaterialGlow(dev, mat.glow.SpecRGBA, mat.glow.SpecPower); // @0x6b0b67
            setGxdShadowProjLight(dev, mat.lightOffset, d, rt);            // @0x6b0b80 (intensity = cos)
            return true;
        }
    }
    return false;
}

// "Triplet" blend restore: mirrors @0x6b1440 (no-2nd-tex) AND @0x6b1ce6 (post-2nd-tex).
//   first = v92 (no-2nd) or v38 (2nd). EXACT order of the binary.
void restoreBlendTriplet(IDirect3DDevice9* dev, int first, int v74, int v73, uint8_t a6) {
    if (first) {                                        // @0x6b144e / @0x6b1cf4
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);      // 27,0
        dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);    // 25,5
        dev->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);    // 60,-1
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1); // 4,2
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);     // 6,1
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);          // 14,1
    }
    if (v74) {                                          // @0x6b14c6 / @0x6b1d6c
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);      // 27,0
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);          // 14,1
    }
    if (v73) {                                          // @0x6b14f6 / @0x6b1d9c
        dev->SetRenderState(D3DRS_ALPHAREF, 0);                 // 24,0
        dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);      // 15,0
        if (a6) {                                       // @0x6b1510 / @0x6b1db6
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);  // 27,0
            dev->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);// 60,-1
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1); // 4,2
            dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);     // 6,1
            dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);      // 14,1
        }
    }
}

} // namespace

// ==========================================================================================
//  MeshPart_RenderFull 0x6B0850 — full port.
// ==========================================================================================
void MeshPartMaterialRenderer::Render(IDirect3DDevice9* dev,
                                      const asset::MeshPartMaterial& mat,
                                      const MeshPartGpu& geo,
                                      const MeshPartTextures& tex,
                                      int frame,
                                      float animTime,
                                      const MeshPartRuntime& rt,
                                      int glowEnable,
                                      uint8_t alphaFade,
                                      const MeshPartDecal* decal) {
    if (!dev) return;

    // Defensive frame bound (the binary assumes a2 is already bounded [0,A-1] by the caller, cf.
    // Model_RenderWithShadow_0 @0x6a415e; re-bounded here for the stream/node offset).
    const uint32_t A = geo.frameCount ? geo.frameCount : 1u;
    uint32_t f = (frame < 0) ? 0u : static_cast<uint32_t>(frame);
    if (f >= A) f = A - 1u;

    // Undecoded header (degenerate part): the binary draws NOTHING. MeshPart_RenderFull has NO
    // "bare" base-draw — the gate this[32]==0 (@0x6b0866: mov eax,[esi+80h]; jz loc_6B1E43) jumps
    // straight to the return @0x6b1e43. We reproduce this "draw nothing" (no invented stray
    // geometry; the previous base-draw had no IDA basis). The caller keeps its own fallback.
    if (!mat.decoded) return;

    // Snapshots for the Gfx_ApplyMeshMaterial 0x69D0E0 (material) and
    // Gfx_SkyboxEndState 0x69D780 (light 0) restores. The FF singleton restores an internal
    // DEFAULT (this+1132 / this+1200) absent here: we restore the device's entry SNAPSHOT — same
    // net effect (undo our writes). Documented divergence, no invention.
    D3DMATERIAL9 savedMat{}; const bool haveMat = SUCCEEDED(dev->GetMaterial(&savedMat));
    D3DLIGHT9    savedL0{};   const bool haveL0  = SUCCEEDED(dev->GetLight(0, &savedL0));

    // v66 = Terrain_PushRenderState() + a3 (QPC clock seconds + phase) — provided already summed.
    const float v66 = animTime;                         // @0x6b0883

    // Restoration flags (init @0x6b0887-@0x6b089e).
    int v64 = 1;    // no custom light applied yet
    int v91 = 0;    // specular (glow) material applied?
    int v73 = 0;    // alpha-test applied?
    int v74 = 0;    // simple blend applied?
    int v92 = 0;    // "fade / other" blend applied?
    const bool hasSecond = (tex.second != nullptr);     // this[99]

    // ---- 4.1 Animated emissive light (triangular ping-pong) @0x6b08a5 — gate this[34] ----
    if (mat.lightAnim.Enable) {
        float v94[4];
        const float phase = v66 * mat.lightAnim.Speed;  // v93 @0x6b08bb
        for (int i = 0; i < 4; ++i) {                   // 4-channel loop @0x6b092b
            const float from  = mat.lightAnim.Pairs[i];      // v10[0]
            const float to    = mat.lightAnim.Pairs[i + 4];  // v10[4]
            const float delta = to - from;              // v11 @0x6b08c5
            if (delta <= 0.0f) {                        // constant @0x6b08d2
                v94[i] = from;                          // @0x6b091b
            } else {
                const int n = static_cast<int>(delta);  // Crt_Dbl2Uint(delta) @0x6b08e4
                const float t = delta * (phase / delta - static_cast<float>(n)); // v12 @0x6b08f3
                v94[i] = (n % 2) ? (to - t) : (from + t); // @0x6b0910 / @0x6b0902
            }
        }
        v64 = 0;                                        // @0x6b0980
        setGxdLight0(dev, 2, v94[0], v94[1], v94[2], v94[3], rt); // Gfx_SetLight(...,2,...) @0x6b0988
    }

    // ---- 4.1b Light neutralization @0x6b099b — gate this[44] && v64 ----
    if (mat.noLight && v64) {
        v64 = 0;                                        // @0x6b09e4
        setGxdLight0(dev, 1, 0.0f, 0.0f, 0.0f, 0.0f, rt); // Gfx_SetLight(...,1,0,0,0.0,0) @0x6b09e8
    }

    // ---- 4.2 View-dependent specular glow @0x6b0a11 — gate a5 && this[45] && !this[99] ----
    if (glowEnable && mat.glow.Enable && !hasSecond) {
        if (applyGlow(dev, mat, geo, f, rt)) v91 = 1;
    }

    // ---- 4.3/4.4/4.5 Texture cascade (decal / flipbook / base) @0x6b0b9b .. LABEL_48 ----
    if (decal && decal->tex) {                          // projected decal @0x6b0b9b
        const int m = decal->mode;                      // a4+44
        if (m == 1) {                                   // alpha-test @0x6b0bac
            v73 = 1;
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);   // 15,1
            dev->SetRenderState(D3DRS_ALPHAREF, 128);           // 24,128
            if (alphaFade) {                            // @0x6b0bdf
                dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);         // 14,0
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);      // 27,1
                dev->SetRenderState(D3DRS_TEXTUREFACTOR, static_cast<DWORD>(alphaFade) << 24); // 60
                dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SUBTRACT); // 4,10
                dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);   // 6,3
            }
            dev->SetTexture(0, decal->tex);             // @0x6b0c48
        } else if (m == 2) {                            // blend @0x6b0c50
            v74 = 1;
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);        // 14,0
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);     // 27,1
            dev->SetTexture(0, decal->tex);             // @0x6b0c82
        } else {                                        // other @0x6b0c89
            if (alphaFade) {
                v92 = 1;
                dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);        // 14,0
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);     // 27,1
                dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);   // 25,8
                dev->SetRenderState(D3DRS_TEXTUREFACTOR,
                                    static_cast<DWORD>((-1 - static_cast<int>(alphaFade)) << 24)); // 60
                dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG2); // 4,3
                dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);     // 6,3
            }
            dev->SetTexture(0, decal->tex);             // v19 = a4+48, tail @0x6b0e9e
        }
    } else if (mat.flipbook.Enable && tex.flipbook && tex.flipbookCount > 0 && tex.flipbook[0]) {
        // ---- 4.4 Flipbook @0x6b0d33 — gate this[53] && holder[0].tex ----
        v74 = 1;
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);            // 14,0
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);         // 27,1
        const int idx = static_cast<int>(v66 * mat.flipbook.Fps); // Crt_Dbl2Uint @0x6b0d78
        const int cnt = static_cast<int>(tex.flipbookCount);
        int sel = idx % cnt;                            // v23 % this[100] @0x6b0d95
        if (sel < 0) sel += cnt;                        // OOB guard (no-op if Fps>=0 & time>=0)
        dev->SetTexture(0, const_cast<IDirect3DTexture9*>(tex.flipbook[sel])); // @0x6b0d95
    } else if (!tex.base) {                             // no base texture @0x6b0daf
        dev->SetTexture(0, nullptr);                    // @0x6b0f4b
    } else {                                            // base texture, mode this[85]
        const int m = tex.baseMode;
        if (m == 1) {                                   // alpha-test @0x6b0dbe
            v73 = 1;
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);      // 15,1
            dev->SetRenderState(D3DRS_ALPHAREF, 128);              // 24,128
            if (alphaFade) {                            // @0x6b0df1
                dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);       // 14,0
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);    // 27,1
                dev->SetRenderState(D3DRS_TEXTUREFACTOR, static_cast<DWORD>(alphaFade) << 24); // 60
                dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SUBTRACT); // 4,10
                dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);   // 6,3
            }
        } else if (m != 2) {                            // other @0x6b0e5f
            if (alphaFade) {
                v92 = 1;
                dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);       // 14,0
                dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);    // 27,1
                dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);  // 25,8
                dev->SetRenderState(D3DRS_TEXTUREFACTOR,
                                    static_cast<DWORD>((-1 - static_cast<int>(alphaFade)) << 24)); // 60
                dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG2); // 4,3
                dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);     // 6,3
            }
        } else {                                        // mode 2: simple blend @0x6b0e66
            v74 = 1;
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);        // 14,0
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);     // 27,1
        }
        dev->SetTexture(0, tex.base);                   // v19 = this[86], @0x6b0e9e / @0x6b0f3d
    }

    // ---- 4.6 UV-scroll #1 (LABEL_48 @0x6b0f59) — gate this[55] ----
    if (mat.uvScroll.tex1.Enable) {
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2); // 24,2 @0x6b0f6d
        D3DXMATRIX texM;
        buildUvScrollMatrix(texM, mat.uvScroll.tex1.Mode, v66 * mat.uvScroll.tex1.Speed); // @0x6b1002
        dev->SetTransform(D3DTS_TEXTURE0, &texM);       // 16 @0x6b1067
    }

    // ---- 4.7 Billboard vs base-draw (LABEL_59 @0x6b107c) — gate this[58] ----
    if (mat.billboard.Enable) {
        if (v64) {                                      // neutralize the light @0x6b1090
            v64 = 0;                                    // @0x6b10d6
            setGxdLight0(dev, 1, 0.0f, 0.0f, 0.0f, 0.0f, rt); // @0x6b10e1
        }
        // TODO ANCHOR billboard @0x6b1184-@0x6b12ff: the FACE-CAMERA quad built CPU-side from the
        // axis bases flt_8001D4 (0x8001D4) / unk_80022C (0x80022C) is an UNPROVEN RUNTIME STATE
        // (cf. TS2_DEEP_MESHPART_MATERIAL.md §10 — runtime blocker). We do NOT fabricate a camera
        // axis: honest fallback = indexed draw of the stored geometry (mesh visible, NOT billboarded).
        // The rest of the state machine (light, blend, return) remains faithful.
        drawIndexed(dev, geo, f);                       // substitute for DrawPrimitiveUP @0x6b12e7
    } else {
        drawIndexed(dev, geo, f);                       // base-draw @0x6b1327
    }

    // ---- 4.8 UV-scroll #1 cleanup @0x6b1366 — gate this[55] ----
    if (mat.uvScroll.tex1.Enable) {
        D3DXMATRIX id; D3DXMatrixIdentity(&id);
        dev->SetTransform(D3DTS_TEXTURE0, &id);         // 16 @0x6b140f
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE); // 24,0 @0x6b1423
    }

    // ---- 4.9 2nd-texture branch @0x6b1429 — gate this[99] ----
    if (!hasSecond) {
        // No 2nd texture: restores then end (@0x6b1440).
        restoreBlendTriplet(dev, v92, v74, v73, alphaFade);
        if (v91) {                                      // @0x6b1579
            if (haveMat) dev->SetMaterial(&savedMat);   // Gfx_ApplyMeshMaterial @0x6b1580
            dev->LightEnable(1, FALSE);                 // Gfx_DisableMeshLighting @0x6b158a
            dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
        }
        // LABEL_134 @0x6b1e37: v37 = (v64 == 0) => a custom light was applied -> restore light 0.
        if (v64 == 0 && haveL0) dev->SetLight(0, &savedL0); // Gfx_SkyboxEndState @0x6b1e3e
        return;
    }

    // --- 2nd texture present: restores BEFORE the 2nd pass (@0x6b159a) ---
    int v38 = v92;
    if (v92) {                                          // @0x6b15b2
        v38 = 0;
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);      // 27,0
        dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);    // 25,5
        dev->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);    // 60,-1
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1); // 4,2
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);     // 6,1
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);          // 14,1
    }
    if (v73 && alphaFade) {                             // @0x6b162c
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);      // 27,0
        dev->SetRenderState(D3DRS_TEXTUREFACTOR, 0xFFFFFFFF);    // 60,-1
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG1); // 4,2
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_CURRENT);     // 6,1
        dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);          // 14,1
    }

    // Re-glow @0x6b168d — gate a5 && this[45] (WITHOUT the !this[99] condition).
    if (glowEnable && mat.glow.Enable) {
        if (applyGlow(dev, mat, geo, f, rt)) v91 = 1;
    }

    // 2nd-texture blend mode @0x6b17f8 (this[98]).
    const int m2 = tex.secondMode;
    if (m2 == 1) {                                      // alpha-test @0x6b1801
        if (!v73) {                                     // @0x6b180d
            v73 = 1;
            dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);   // 15,1
            dev->SetRenderState(D3DRS_ALPHAREF, 128);           // 24,128
        }
        if (alphaFade) {                                // @0x6b1840
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);  // 27,1
            dev->SetRenderState(D3DRS_TEXTUREFACTOR, static_cast<DWORD>(alphaFade) << 24); // 60
            dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SUBTRACT); // 4,10
            dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);   // 6,3
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);    // 14,0
        }
    } else if (m2 == 2) {                               // blend @0x6b18c4
        if (!v74) {                                     // @0x6b18cc
            v74 = 1;
            dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);    // 14,0
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE); // 27,1
        }
    } else if (alphaFade) {                             // other + fade @0x6b1918
        v38 = 1;
        dev->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);        // 14,0
        dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);     // 27,1
        dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_ALWAYS);   // 25,8
        dev->SetRenderState(D3DRS_TEXTUREFACTOR,
                            static_cast<DWORD>((-1 - static_cast<int>(alphaFade)) << 24)); // 60
        dev->SetTextureStageState(0, D3DTSS_ALPHAOP,   D3DTOP_SELECTARG2); // 4,3
        dev->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_TFACTOR);     // 6,3
    }

    dev->SetTexture(0, tex.second);                     // SetTexture(0, this[99]) @0x6b19ad

    // UV-scroll #2 @0x6b19bb — gate this[60].
    if (mat.uvScroll.tex2.Enable) {
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_COUNT2); // 24,2 @0x6b19cf
        D3DXMATRIX texM2;
        buildUvScrollMatrix(texM2, mat.uvScroll.tex2.Mode, v66 * mat.uvScroll.tex2.Speed); // @0x6b1a64
        dev->SetTransform(D3DTS_TEXTURE0, &texM2);      // 16 @0x6b1ac9
    }

    // 2nd draw (LABEL_117 @0x6b1ade): billboard (fallback) or indexed draw.
    int v48;
    if (mat.billboard.Enable) {                         // @0x6b1ade
        v48 = v64;
        if (v64) {                                      // @0x6b1af2
            v48 = 0;
            setGxdLight0(dev, 1, 0.0f, 0.0f, 0.0f, 0.0f, rt); // @0x6b1b21
        }
        // TODO ANCHOR billboard 2nd pass @0x6b1bc4-@0x6b1bf1: same as §4.7, axes unproven -> fallback.
        drawIndexed(dev, geo, f);                       // substitute for DrawPrimitiveUP @0x6b1bd9
    } else {
        drawIndexed(dev, geo, f);                       // @0x6b1c17
        v48 = v64;                                      // @0x6b1c1d
    }

    // UV-scroll #2 cleanup @0x6b1c21 — gate this[60].
    if (mat.uvScroll.tex2.Enable) {
        D3DXMATRIX id; D3DXMatrixIdentity(&id);
        dev->SetTransform(D3DTS_TEXTURE0, &id);         // 16 @0x6b1cca
        dev->SetTextureStageState(0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE); // 24,0 @0x6b1cde
    }

    // POST-2nd-pass restores @0x6b1ce6 (v38 / v74 / v73).
    restoreBlendTriplet(dev, v38, v74, v73, alphaFade);

    if (v91) {                                          // @0x6b1e1f
        if (haveMat) dev->SetMaterial(&savedMat);       // Gfx_ApplyMeshMaterial @0x6b1e26
        dev->LightEnable(1, FALSE);                     // Gfx_DisableMeshLighting @0x6b1e30
        dev->SetRenderState(D3DRS_SPECULARENABLE, FALSE);
    }
    // LABEL_134 @0x6b1e37: v37 = (v48 == 0) -> restore light 0 (Gfx_SkyboxEndState).
    if (v48 == 0 && haveL0) dev->SetLight(0, &savedL0); // @0x6b1e3e
}

} // namespace ts2::gfx

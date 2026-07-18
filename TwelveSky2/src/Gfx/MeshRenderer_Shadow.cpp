// Gfx/MeshRenderer_Shadow.cpp - stencil shadow volume + planar shadow rendering, split out of
// MeshRenderer.cpp. See MeshRenderer.h for the reverse-engineering anchors.
#include "Gfx/MeshRenderer.h"
#include "Gfx/MeshRenderer_Internal.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"

#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

// Stencil shadow volume states (raw numbers read from Model_RenderWithShadow 0x40EEE0).
constexpr DWORD kRS_CullMode     = 22; // D3DRS_CULLMODE
constexpr DWORD kRS_StencilZFail = 54; // D3DRS_STENCILZFAIL
constexpr DWORD kCULL_CW         = 2;  // D3DCULL_CW
constexpr DWORD kCULL_CCW        = 3;  // D3DCULL_CCW
constexpr DWORD kSTENCILOP_Incr  = 7;  // D3DSTENCILOP_INCR
constexpr DWORD kSTENCILOP_Decr  = 8;  // D3DSTENCILOP_DECR
constexpr DWORD kFVF_XYZ         = 2;  // D3DFVF_XYZ (shadow vertex, position only, stride 12)

// Shader PASS identifier (plays the role of g_CurrentShaderPass 0x194591C).
// Warning: numbering is DISTINCT from the DRAW pass (MeshRenderer::kDrawPass_*, = a6 of
// Model_Render) - see MeshRenderer_Skinning.cpp for passes 2/3/4 (skinned + multi-texture).
//   5 = planar shadow (VS09 g_GxdSh09_VS + PS NULL) - g_CurrentShaderPass=5 @0x40FB40
//   8 = stencil shadow volume (VS15 + PS NULL)
constexpr int kPass_PlanarShadow = 5;
constexpr int kPass_ShadowVolume = 8;

// Shadow volume cap: the original guards v45 > 29976 -> abort (30000-vertex arrays).
constexpr UINT kShadowVolMaxVerts = 30000;
constexpr UINT kShadowVolGuard    = 29976;

} // namespace

// ===========================================================================
//  Shadows - Model_RenderWithShadow 0x40EEE0 / Model_BuildShadowVolume 0x40DC70
// ===========================================================================

// FRONT FX-F4 (shadow blob): DEFERRED -- this function stays intentionally NOT wired up.
// HARDENED PROOF (Pass 4 / wave W5, shadow-wiring front, 2026-07-16): the stencil shadow volume
// is UNREACHABLE DEAD CODE **in the binary itself**. This is NOT a ShaderSet blocker:
// attaching VS15 unblocks nothing, there is no caller to unblock.
//
//   1) Caller chain EXHAUSTIVELY ENUMERATED (xrefs_to, not a bounded `reaches`):
//        Model_BuildShadowVolume 0x40DC70  <- 1 sole caller: Model_RenderWithShadow 0x40EEE0
//        Model_RenderWithShadow  0x40EEE0  <- 1 sole caller: SObject_DrawAnimated 0x4D9050 (3 sites)
//        SObject_DrawAnimated    0x4D9050  <- 3 callers: Char_DrawShadow 0x580CE0,
//                                             Npc_DrawMeshShadow 0x5800E0,
//                                             Char_DrawWeaponEffectVariantA 0x568FE0
//        these 3 chain heads                  <- 0 xref EACH  => DEAD.
//   2) The "indirect call / vtable" objection is RULED OUT: `find_bytes` for the 6 addresses in
//      little-endian (E0 0C 58 00 / E0 8F 56 00 / E0 00 58 00 / 50 90 4D 00 / E0 EE 40 00 /
//      70 DC 40 00) returns **0 occurrences in the WHOLE image**. No vtable, pointer table,
//      or jump table can reach them. `reaches(Scene_InGameRender 0x52D0B0 -> 0x40DC70)` = false.
//   3) FROZEN constants -- GXD_InitGlobalState 0x401320 is the SOLE writer (verified by xrefs):
//        g_ShadowsEnabled 0x18C4F14 = 1 (0x4013B2): its ONLY reader is 0x40EEEC, inside the
//          dead function -> the global is inert, drives nothing anymore.
//        g_ShadowMethod   0x18C4F18 = 1 (0x4013B8): the Carmack z-fail branch `if (!method)`
//          (0x40F671) would therefore be DEAD BY VALUE even if the function were live.
//
// DO NOT "fix" this non-wiring: drawing this volume would draw an effect the original binary
// NEVER produces. The function stays in place as a faithful RE trace.
//
// WARNING -- do not conclude "no shadow is drawn at all" (an error of the older draft):
// the client does draw shadows, but PLANAR ones, via a LIVE TWIN CHAIN that Pass 3 had missed:
// Model_RenderPlanarShadow 0x40F720 (flattening via j_D3DXMatrixShadow
// @0x40FB28, pass 5 = VS09 g_GxdSh09_VS + PS NULL), reached from Scene_InGameRender 0x52D0B0
// via SObject_DrawAnimated2 0x4D91C0 (`reaches` = true, depth 3). The devs DUPLICATED the
// chain then switched to the planar one, orphaning the volume (matching sizes pairwise):
//        DEAD (-> volume)                        LIVE (-> planar)                    size
//        Char_DrawShadow 0x580CE0                Char_DrawReflection 0x581090        0x3A4
//        Npc_DrawMeshShadow 0x5800E0             Npc_DrawMeshGlow 0x5801D0           0xE2
//        Char_DrawWeaponEffectVariantA 0x568FE0  Char_DrawWeaponEffectVariantB 0x56BF90  0x2AFF
// See the Scene/WorldRenderer.h Sec Shadow banner for the scene bracket and this effort's status.
void MeshRenderer::DrawModelShadow(const SkinnedModel& model,
                                   const D3DXVECTOR3&  position,
                                   const D3DXVECTOR3&  rotationDeg,
                                   const D3DXVECTOR3&  scale,
                                   const BonePalette&  palette,
                                   float               boundRadius) {
    // 1) Gate on g_ShadowsEnabled (0x40eef8) + present entities. The shadow volume goes through
    //    VS15 (g_GxdSh15_VS): without a real ShaderSet attached, the reconstructed HLSL doesn't
    //    have this shader -> no-op.
    if (!Ready() || !shadowsEnabled_ || model.Empty()) return;
    if (!shaderSet_) return;
    const GxdShader& sh15 = shaderSet_->Get(GxdShaderId::VS15_WorldVP);
    if (!sh15.Valid()) return;

    // 2) World matrix = Scale*RotZ*RotY*RotX*Translate (deg->rad x0.017453292; 0x40f3b7..0x40f4c3).
    D3DXMATRIX mS, mRx, mRy, mRz, mT, tmp, world;
    D3DXMatrixScaling(&mS, scale.x, scale.y, scale.z);
    D3DXMatrixRotationX(&mRx, rotationDeg.x * kDeg2Rad);
    D3DXMatrixRotationY(&mRy, rotationDeg.y * kDeg2Rad);
    D3DXMatrixRotationZ(&mRz, rotationDeg.z * kDeg2Rad);
    D3DXMatrixTranslation(&mT, position.x, position.y, position.z);
    D3DXMatrixMultiply(&world, &mS,    &mRz);
    D3DXMatrixMultiply(&tmp,   &world, &mRy);
    D3DXMatrixMultiply(&world, &tmp,   &mRx);
    D3DXMatrixMultiply(&tmp,   &world, &mT);
    world = tmp;

    // 3) Model<->camera distance (0x40ef8e: dist = |position - eye|). The frustum-sphere test
    //    (Frustum_IntersectsSphere 0x406660, center = position + Y*boundRadius*0.5) is only a
    //    culling optimization -> omitted here without changing the render.
    D3DXVECTOR3 delta = position - eye_;
    const float dist = D3DXVec3Length(&delta);

    if (dist > fogNear_) {
        // ---- FAR (0x40efde): projected planar shadow ----
        // TODO [anchor 0x40F720] Model_RenderPlanarShadow: planar projection + anti-double-blend
        //   stencil mask (TEXTUREFACTOR60 = mean diffuse luminance x64 <<24, SRCALPHA/
        //   INVSRCALPHA, DEPTHBIAS195=1e-5 anti z-fight). Not reversed/owned here (separate
        //   projected geometry): regime left pending so as not to program a device state
        //   without its matching draw.
        return;
    }

    // ---- NEAR (0x40efc6): STENCIL SHADOW VOLUME ----
    // WVP = world . view . proj (0x40f50f/0x40f523; viewProj_ = view.proj).
    D3DXMATRIX wvp;
    D3DXMatrixMultiply(&wvp, &world, &viewProj_);

    // Pass 8 = VS15 + PS NULL (g_CurrentShaderPass cache; 0x40f4d0..0x40f4fe).
    if (currentPass_ != kPass_ShadowVolume) {
        currentPass_ = kPass_ShadowVolume;
        dev_->SetVertexShader(sh15.vs); // method +368 = g_GxdSh15_VS
        dev_->SetPixelShader(nullptr);  // method +428 (PS NULL: depth/stencil writes only)
    }
    if (D3DXHANDLE h = sh15.Handle("mWorldViewProjMatrix"))
        sh15.ct->SetMatrix(dev_, h, &wvp); // g_GxdSh15_hWorldViewProj (method +84; 0x40f55b)
    dev_->SetTexture(0, nullptr);          // method +260 (0x40f56f)

    // Shadow light direction in object space: negated, transformed by inverse(world), normalized
    // (flt_18C53C0/C4/C8; 0x40f580..0x40f5cc).
    D3DXMATRIX invWorld;
    D3DXMatrixInverse(&invWorld, nullptr, &world);
    D3DXVECTOR3 lightObj(-shadowLightDir_.x, -shadowLightDir_.y, -shadowLightDir_.z);
    D3DXVec3TransformNormal(&lightObj, &lightObj, &invWorld);
    D3DXVec3Normalize(&lightObj, &lightObj);

    // Mesh loop (0x40f5e5..0x40f703). Near regime: the fade v30 saturates to 1.0 (0x40f367)
    // -> LOD 0 (0x40dca3). Each non-billboard mesh skins + silhouettes + extrudes its geometry.
    //
    // FIDELITY FIX (Pass 4 / W5b, shadow-fidelity front): a9 (extrusion length)
    // is NOT "out of scope" - it is PROVEN, and equals exactly a2 x 2.5.
    //   SObject_DrawAnimated 0x4D9050 is the SOLE caller of Model_RenderWithShadow 0x40EEE0
    //   (xrefs_to(0x40EEE0) = 3, and all 3 are in this one function) -> the relation is
    //   universal, not branch-specific. At all 3 sites, a9 is computed then passed:
    //     compute  v11/v10/v9 = a5 * 2.5   @0x4D90C6 / @0x4D9129 / @0x4D9178
    //     call     Model_RenderWithShadow(..., a2=a5, ..., a9=a5*2.5)
    //                                    @0x4D90F9 / @0x4D915C / @0x4D91B4
    //   Model_RenderWithShadow's a2 parameter receives DrawAnimated's a5 -> a9 = a2 x 2.5.
    //   Consumed by Model_BuildShadowVolume(v20, a6, a7, v30, &v31, a9) @0x40F61C.
    //   Here a2 == boundRadius (bounding diameter, see .h) -> extrusion = boundRadius * 2.5f.
    for (const SkinnedMesh& mesh : model.meshes) {
        if (mesh.empty || mesh.lods.empty()) continue;
        const SkinnedLod& L = mesh.lods[0];
        // a9 = a2 * 2.5 - SObject_DrawAnimated 0x4D9050 @0x4D90C6/@0x4D9129/@0x4D9178.
        if (!buildShadowVolume(L, palette, lightObj, boundRadius * 2.5f)) continue;

        const UINT triCount = shadowVolVertCount_ / 3;
        if (triCount == 0) continue;
        const UINT stride = 3 * sizeof(float); // shadow vertex = position only (12 B)

        // 1st pass: FVF XYZ + DrawPrimitiveUP (inherited cull) - 0x40f639/0x40f668.
        dev_->SetFVF(kFVF_XYZ); // method +356
        dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triCount, shadowVol_.data(), stride); // method +332

        if (shadowMethod_ == 0) {
            // z-fail (Carmack reverse): CW+INCR redraw, then DECR+CCW (0x40f685..0x40f6f0).
            dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_CullMode),     kCULL_CW);
            dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_StencilZFail), kSTENCILOP_Incr);
            dev_->DrawPrimitiveUP(D3DPT_TRIANGLELIST, triCount, shadowVol_.data(), stride);
            dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_StencilZFail), kSTENCILOP_Decr);
            dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_CullMode),     kCULL_CCW);
        }
        // g_ShadowMethod==1 (stencil two-sided): the near regime only does the 1st pass
        //   (the two-sided TWOSIDEDSTENCILMODE185 states are programmed in the planar regime).
    }
}

// ===========================================================================
//  PROJECTED PLANAR shadow - Model_RenderPlanarShadow 0x40F720 (LIVE chain [B])
// ===========================================================================
//
// This is the game's REAL shadow (unlike DrawModelShadow above = dead volume 0x40EEE0). It
// flattens the real skinned mesh onto the ground plane via D3DXMatrixShadow, PASS 5 = VS09 +
// PS NULL. The ground plane (a,b,c,-d-0.1) is supplied READY by the Scene layer
// (collision::GroundPlane::shadowPlane), this Gfx module not depending on World. Decompilation
// of 0x40F720 re-read byte-by-byte this session.
void MeshRenderer::DrawModelPlanarShadow(const SkinnedModel& model,
                                         const D3DXVECTOR3&  position,
                                         const D3DXVECTOR3&  rotationDeg,
                                         const D3DXVECTOR3&  scale,
                                         const BonePalette&  palette,
                                         const float         groundShadowPlane[4]) {
    if (!Ready() || model.Empty() || !groundShadowPlane) return;
    // PASS 5 binds VS09 (g_GxdSh09_VS 0x1945B18), REAL npk slot (@0x40FB54). The reconstructed
    // HLSL doesn't have VS09 -> clean no-op without a ShaderSet attached (shadow not drawn >
    // infidel render).
    if (!shaderSet_) return;
    const GxdShader& vs09 = shaderSet_->Get(GxdShaderId::VS09_Skinned);
    if (!vs09.Valid()) return;

    // 1) Entity world matrix = Scale*RotZ*RotY*RotX*Translate - SAME composition as
    //    DrawModel (Model_Render 0x40EBB0) and as g_WorldMatrix in 0x40F720 (@0x40F864..0x40F976:
    //    Translation/RotX/RotY/RotZ/Scaling + 4x Matrix_Multiply). Entities only rotate
    //    around Y -> inter-axis order has no effect; composition is aligned with DrawModel so the
    //    shadow is exactly the flattened body.
    D3DXMATRIX mS, mRx, mRy, mRz, mT, tmp, world;
    D3DXMatrixScaling(&mS, scale.x, scale.y, scale.z);
    D3DXMatrixRotationX(&mRx, rotationDeg.x * kDeg2Rad);
    D3DXMatrixRotationY(&mRy, rotationDeg.y * kDeg2Rad);
    D3DXMatrixRotationZ(&mRz, rotationDeg.z * kDeg2Rad);
    D3DXMatrixTranslation(&mT, position.x, position.y, position.z);
    D3DXMatrixMultiply(&world, &mS,    &mRz);
    D3DXMatrixMultiply(&tmp,   &world, &mRy);
    D3DXMatrixMultiply(&world, &tmp,   &mRx);
    D3DXMatrixMultiply(&tmp,   &world, &mT);
    world = tmp;

    // 2) Flattening matrix mShadow = D3DXMatrixShadow(light4, plane) (j_D3DXMatrixShadow
    //    @0x40FB28). plane = {a,b,c,-d-0.1} ALREADY READY (groundShadowPlane, v45 @0x40FACE..0x40FAFC).
    //    light4 = { -shadowDir.x, -shadowDir.y, -shadowDir.z, 0 } (v38..v41 @0x40FB08..0x40FB24:
    //    flt_18C53C0/C4/C8 negated, w=0 -> DIRECTIONAL light).
    D3DXVECTOR4 light4(-shadowLightDir_.x, -shadowLightDir_.y, -shadowLightDir_.z, 0.0f);
    D3DXPLANE   plane(groundShadowPlane[0], groundShadowPlane[1],
                      groundShadowPlane[2], groundShadowPlane[3]);
    D3DXMATRIX  mShadow;
    D3DXMatrixShadow(&mShadow, &light4, &plane);

    // 3) Flattened WVP = World . Shadow . View . Proj (3x Matrix_Multiply @0x40FB7D/@0x40FB97/@0x40FBB1;
    //    viewProj_ = View.Proj -> (World.Shadow).(View.Proj), associative).
    D3DXMATRIX worldShadow, wvp;
    D3DXMatrixMultiply(&worldShadow, &world, &mShadow); // World.Shadow          @0x40FB7D
    D3DXMatrixMultiply(&wvp, &worldShadow, &viewProj_); // .View.Proj  @0x40FB97/@0x40FBB1

    // 4) PASS 5 = VS09 + PS NULL (g_CurrentShaderPass 0x194591C cache; @0x40FB38..0x40FB66).
    if (currentPass_ != kPass_PlanarShadow) {
        currentPass_ = kPass_PlanarShadow;
        dev_->SetVertexShader(vs09.vs); // method +368 = g_GxdSh09_VS
        dev_->SetPixelShader(nullptr);  // method +428 (PS NULL: color = bracket's TFACTOR/TSS)
    }

    // 5) VS09 constants: bone palette (mKeyMatrix = dword_1945B1C, method +88 @0x40FBF9) then
    //    flattened WVP (mWorldViewProjMatrix = dword_1945B20, method +84 @0x40FC1C);
    //    SetTexture(0, NULL) (method +260 @0x40FC30).
    const D3DXMATRIX* palMats = identityPalette_;
    UINT boneCount = 1;
    if (palette.Valid()) { palMats = palette.matrices; boneCount = palette.count; }
    if (D3DXHANDLE hKey = vs09.Handle("mKeyMatrix"))
        vs09.ct->SetMatrixArray(dev_, hKey, palMats, boneCount);
    if (D3DXHANDLE hWvp = vs09.Handle("mWorldViewProjMatrix"))
        vs09.ct->SetMatrix(dev_, hWvp, &wvp);
    dev_->SetTexture(0, nullptr);

    // 6) Real skinned vertex declaration (g_GxdSkinVtxDecl 0x1945918, set @0x40FD4D).
    IDirect3DVertexDeclaration9* useDecl = decl_;
    if (IDirect3DVertexDeclaration9* d = shaderSet_->SkinnedVertexDecl()) useDecl = d;

    // 7) Per skinned sub-part draw loop, LOD 0 (@0x40FC42..0x40FD9D). Each part emits a
    //    flattened silhouette; the composite shadow = all parts. SetStreamSource(+400, stride
    //    76) / SetIndices(+416) / SetVertexDeclaration(+348) / DrawIndexedPrimitive(+328,
    //    4=TRIANGLELIST). (The masked part[52]!=0 is not modeled on the C++ side - same as DrawModel.)
    for (const SkinnedMesh& mesh : model.meshes) {
        if (mesh.empty || mesh.lods.empty()) continue;
        const SkinnedLod& L = mesh.lods[0];
        if (!L.vb || !L.ib || L.vertexCount == 0 || L.faceCount == 0) continue;
        dev_->SetStreamSource(0, L.vb, 0, static_cast<UINT>(sizeof(GpuSkinVertex)));
        dev_->SetIndices(L.ib);
        dev_->SetVertexDeclaration(useDecl);
        dev_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, L.vertexCount, 0, L.faceCount);
    }
}

// Model_BuildShadowVolume 0x40DC70 - CPU skinning of vertices + extruded silhouette.
// Operates on the retained CPU data (SkinnedLod::skinCpu/idxTopo/idxAdj). Writes shadowVol_
// (interleaved XYZ, stride 12) and shadowVolVertCount_; returns false on abort (0x40dc87/0x40dd37/
// 0x40e1bd) or missing data.
bool MeshRenderer::buildShadowVolume(const SkinnedLod&  L,
                                     const BonePalette& palette,
                                     const D3DXVECTOR3& lightDirObj,
                                     float              extrude) {
    // Empty/billboard mesh / missing CPU data -> 0 (0x40dc87).
    if (L.vertexCount == 0 || L.faceCount == 0) return false;
    if (L.skinCpu.empty() || L.idxTopo.empty() || L.idxAdj.empty()) return false;
    if (!palette.Valid()) return false;
    // LOD guard (0x40dd37): > 10000 vertices or faces -> abort.
    if (L.vertexCount > 10000 || L.faceCount > 10000) return false;

    const UINT nVtx   = L.vertexCount;
    const UINT nFace  = L.faceCount;
    const D3DXMATRIX* bones = palette.matrices; // MOTION frame slice (base + frame.nBones)
    const UINT nBones = palette.count;

    // Resize scratch buffers (original globals, same caps).
    if (worldPos_.size() < static_cast<size_t>(nVtx) * 3)
        worldPos_.resize(static_cast<size_t>(nVtx) * 3);
    if (faceLightFacing_.size() < nFace)
        faceLightFacing_.resize(nFace);
    if (shadowVol_.size() < static_cast<size_t>(kShadowVolMaxVerts) * 3)
        shadowVol_.resize(static_cast<size_t>(kShadowVolMaxVerts) * 3);

    // --- 1) CPU skinning (0x40dd76..0x40e004): 32 B SkinVertex = pos[3]@+0, weights[4]@+12/16/20/24,
    //        boneIdx u32@+28 (4x u8). out = sum_i w_i . (M[bone_i].pos), affine row-vector transform. ---
    const uint8_t* skin = L.skinCpu.data();
    for (UINT i = 0; i < nVtx; ++i) {
        const uint8_t* v = skin + static_cast<size_t>(i) * 32;
        float px, py, pz, w[4];
        uint32_t packed;
        std::memcpy(&px, v + 0, 4);
        std::memcpy(&py, v + 4, 4);
        std::memcpy(&pz, v + 8, 4);
        std::memcpy(&w[0], v + 12, 4);
        std::memcpy(&w[1], v + 16, 4);
        std::memcpy(&w[2], v + 20, 4);
        std::memcpy(&w[3], v + 24, 4);
        std::memcpy(&packed, v + 28, 4);

        float ox = 0.0f, oy = 0.0f, oz = 0.0f;
        for (int k = 0; k < 4; ++k) {
            UINT b = (packed >> (8 * k)) & 0xFFu;
            if (b >= nBones) b = 0; // guard (bone indices out of palette - never hit on valid data)
            const D3DXMATRIX& m = bones[b];
            // == v105/106/107: _11*px+_21*py+_31*pz+_41 (translation included).
            const float tx = px * m._11 + py * m._21 + pz * m._31 + m._41;
            const float ty = px * m._12 + py * m._22 + pz * m._32 + m._42;
            const float tz = px * m._13 + py * m._23 + pz * m._33 + m._43;
            ox += w[k] * tx; oy += w[k] * ty; oz += w[k] * tz;
        }
        worldPos_[static_cast<size_t>(i) * 3 + 0] = ox;
        worldPos_[static_cast<size_t>(i) * 3 + 1] = oy;
        worldPos_[static_cast<size_t>(i) * 3 + 2] = oz;
    }

    const uint16_t* topo = reinterpret_cast<const uint16_t*>(L.idxTopo.data());
    const uint16_t* adj  = reinterpret_cast<const uint16_t*>(L.idxAdj.data());
    auto WP = [&](UINT idx) -> const float* { return &worldPos_[static_cast<size_t>(idx) * 3]; };

    // --- 2) Face facing (0x40e02c..0x40e122): normal = eA x eB, lit if N.lightDir > 0. ---
    for (UINT f = 0; f < nFace; ++f) {
        const uint16_t i0 = topo[f * 3 + 0];
        const uint16_t i1 = topo[f * 3 + 1];
        const uint16_t i2 = topo[f * 3 + 2];
        const float* p0 = WP(i0);
        const float* p1 = WP(i1);
        const float* p2 = WP(i2);
        const float ax = p1[0] - p0[0], ay = p1[1] - p0[1], az = p1[2] - p0[2]; // eA = p1-p0
        const float bx = p2[0] - p1[0], by = p2[1] - p1[1], bz = p2[2] - p1[2]; // eB = p2-p1
        const float nx = bz * ay - by * az; // v96
        const float ny = az * bx - bz * ax; // v97
        const float nz = ax * by - bx * ay; // v98
        faceLightFacing_[f] =
            (nx * lightDirObj.x + lightDirObj.y * ny + lightDirObj.z * nz > 0.0f) ? 1 : 0;
    }

    // --- 3) Silhouette + extrusion (0x40e156..0x40e635). ---
    shadowVolVertCount_ = 0;
    const float ex = lightDirObj.x * extrude; // v53
    const float ey = lightDirObj.y * extrude; // v52
    const float ez = lightDirObj.z * extrude; // v50
    float* vol = shadowVol_.data();
    auto emit = [&](float x, float y, float z) {
        const size_t o = static_cast<size_t>(shadowVolVertCount_) * 3;
        vol[o + 0] = x; vol[o + 1] = y; vol[o + 2] = z;
        ++shadowVolVertCount_;
    };

    for (UINT f = 0; f < nFace; ++f) {
        if (faceLightFacing_[f] != 1) continue;
        if (shadowVolVertCount_ > kShadowVolGuard) return false; // v45 > 29976 (0x40e1bd)

        const uint16_t i0 = topo[f * 3 + 0];
        const uint16_t i1 = topo[f * 3 + 1];
        const uint16_t i2 = topo[f * 3 + 2];
        const float* p0 = WP(i0);
        const float* p1 = WP(i1);
        const float* p2 = WP(i2);

        // Front cap = the lit face (p0, p1, p2) - 0x40e1fb..0x40e2b6.
        emit(p0[0], p0[1], p0[2]);
        emit(p1[0], p1[1], p1[2]);
        emit(p2[0], p2[1], p2[2]);

        // Silhouette edges: missing neighbor (==nFace) or unlit -> extruded quad.
        //   nearA = [p1,p2,p0], nearB = [p0,p1,p2] (index tables &v100/&v99; 0x40e30b/0x40e34b).
        const float* nearA[3] = { p1, p2, p0 };
        const float* nearB[3] = { p0, p1, p2 };
        for (int e = 0; e < 3; ++e) {
            const uint16_t nb = adj[f * 3 + e]; // adjacency: neighbor per edge (0x40e2e6)
            if (nb >= nFace || !faceLightFacing_[nb]) {
                const float* a = nearA[e];
                const float* b = nearB[e];
                // quad = 2 triangles (a, b, b_far, a, b_far, a_far) - 0x40e314..0x40e4d6.
                emit(a[0],       a[1],       a[2]);
                emit(b[0],       b[1],       b[2]);
                emit(b[0] - ex,  b[1] - ey,  b[2] - ez);
                emit(a[0],       a[1],       a[2]);
                emit(b[0] - ex,  b[1] - ey,  b[2] - ez);
                emit(a[0] - ex,  a[1] - ey,  a[2] - ez);
            }
        }

        // Extruded back cap (p0_far, p2_far, p1_far) - 0x40e507..0x40e608.
        emit(p0[0] - ex, p0[1] - ey, p0[2] - ez);
        emit(p2[0] - ex, p2[1] - ey, p2[2] - ez);
        emit(p1[0] - ex, p1[1] - ey, p1[2] - ez);
    }
    return true;
}

} // namespace ts2::gfx

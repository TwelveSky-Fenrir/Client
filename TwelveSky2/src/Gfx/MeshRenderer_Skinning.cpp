// Gfx/MeshRenderer_Skinning.cpp - per-subset skinned draw path (mesh sweep + material blend
// states), split out of MeshRenderer.cpp. See MeshRenderer.h for the reverse-engineering anchors.
#include "Gfx/MeshRenderer.h"
#include "Asset/Zlib.h"
#include "Core/Log.h"

#include <cstring>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

// D3DRENDERSTATETYPE / values (raw numbers read from Model_DrawSkinnedSubset).
constexpr DWORD kRS_ZWriteEnable      = 14; // D3DRS_ZWRITEENABLE
constexpr DWORD kRS_AlphaTestEnable   = 15; // D3DRS_ALPHATESTENABLE
constexpr DWORD kRS_SrcBlend          = 19; // D3DRS_SRCBLEND
constexpr DWORD kRS_DestBlend         = 20; // D3DRS_DESTBLEND
constexpr DWORD kRS_AlphaRef          = 24; // D3DRS_ALPHAREF
constexpr DWORD kRS_AlphaFunc         = 25; // D3DRS_ALPHAFUNC
constexpr DWORD kRS_AlphaBlendEnable  = 27; // D3DRS_ALPHABLENDENABLE

constexpr DWORD kBLEND_Zero        = 1;  // D3DBLEND_ZERO
constexpr DWORD kBLEND_One         = 2;  // D3DBLEND_ONE
constexpr DWORD kBLEND_SrcAlpha    = 5;  // D3DBLEND_SRCALPHA
constexpr DWORD kBLEND_InvSrcAlpha = 6;  // D3DBLEND_INVSRCALPHA
constexpr DWORD kCMP_GreaterEqual = 5; // D3DCMP_GREATEREQUAL
constexpr DWORD kCMP_Always       = 8; // D3DCMP_ALWAYS
// Alpha-test threshold for blendMode-1 materials: `push 80h` @0x40cbd1 (see applyBlendMode).
constexpr DWORD kAlphaRef_Material = 128;

// Shader PASS identifier (plays the role of g_CurrentShaderPass 0x194591C).
// Warning: numbering is DISTINCT from the DRAW pass (MeshRenderer::kDrawPass_*, = a6 of
// Model_Render): the two cross paths in Model_DrawSkinnedSubset 0x40CA40 without ever meaning
// the same thing.
//   2 = skinned VS03/PS04         (g_CurrentShaderPass = 2 @0x40d3e5)
//   3 = multi-texture VS05/PS06   (g_CurrentShaderPass = 3 @0x40d9c3) - mTexture0 + mTexture1
//   4 = multi-texture VS07/PS08   (g_CurrentShaderPass = 4 @0x40d682) - mTexture0/1/2 + mCameraEye
//   5 = planar shadow, 8 = shadow volume - see MeshRenderer_Shadow.cpp
constexpr int kPass_SkinnedLit   = 2;
constexpr int kPass_MultiTex2    = 3;
constexpr int kPass_MultiTex3    = 4;

// g_TextureDetailLevel 0x18C4F04 - CONSTANT FROZEN AT 2, not an option.
// data_refs: ONE SINGLE writer, `mov ds:g_TextureDetailLevel, 2` @0x4013A2 (GXD_InitGlobalState
// 0x401320) - an IMMEDIATE, never rewritten (no UI option touches it); 5 readers.
// Proven consequence, counter to the intuition "frozen detail level = no multi-texture":
// value 2 KEEPS the textures and ENABLES passes 3/4.
//   At parse time (Mesh_ReadFromFile 0x40BC50): `if (!detail) Tex_Free(a1+192)` @0x40c228 -> not
//     taken -> tex1 KEPT; `if (detail != 2) Tex_Free(a1+206)` @0x40c250 -> not taken -> tex2 KEPT.
//   At draw time (0x40d3a8): detail==0 branch dead, detail==1 branch DEAD BY VALUE, `else`
//     branch (detail>=2) ALWAYS taken -> passes 3 and 4 are LIVE.
// Only the live branch is ported; the other two would be code dead by value.
constexpr int kTextureDetailLevel = 2;

} // namespace

// Body of the mesh loop from Model_Render 0x40EBB0 (@0x40ee02..0x40eebf, stride 888 @0x40eebf).
void MeshRenderer::drawMeshSweep(const SkinnedModel& model, const D3DXMATRIX& world,
                                 const BonePalette& palette, int lod, int pass) {
    // SOBJ-05 - MATERIAL INHERITANCE FROM mesh[0]. The question ("explicit inheritance, or
    // stale device texture state left un-reset?") is SETTLED: it is EXPLICIT INHERITANCE, a
    // pointer pass-through, nothing stale. Model_Render @0x40ee53:
    //     v17 = v11[2];                  // array base == &mesh[0]
    //     if ( *(v18 + 764) || !*(v17 + 764) )                       // mesh[i].mat0.tex, mesh[0].mat0.tex
    //         Model_DrawSkinnedSubset(0, v18, ..., 0, 0, ..);        // @0x40eeb1: OWN materials
    //     else
    //         Model_DrawSkinnedSubset(v17+712, v18, ..., v17+768, v17+824, ..); // @0x40ee8e
    // -> if mesh[i] has NO tex0 AND mesh[0] has one, the FULL TRIPLET of
    //    mesh[0] (mat0 +712, mat1 +768, mat2 +824) is passed as an override.
    // COROLLARY NOT TO MISS: under inheritance, the blendMode used is mesh[0]'s (v61+44)
    // and the multi-texture pass choice is made on mesh[0]'s mat1/mat2. This is automatic here:
    // DrawSkinnedSubset reads its whole material from `matSrc`.
    const SkinnedMesh* mesh0 = &model.meshes[0]; // v17 = v11[2]
    for (const SkinnedMesh& mesh : model.meshes) {
        if (mesh.empty || mesh.lods.empty()) continue; // `if (*(v16 + v17))` @0x40ee0e
        int useLod = lod;
        if (useLod < 0) useLod = 0;
        if (useLod >= static_cast<int>(mesh.lods.size()))
            useLod = static_cast<int>(mesh.lods.size()) - 1;
        // `!*(v18+764) && *(v17+764)` -> override by mesh[0]'s triplet.
        const SkinnedMesh* matSrc = (!mesh.diffuse && mesh0->diffuse) ? mesh0 : nullptr;
        DrawSkinnedSubset(mesh, useLod, world, palette, matSrc, pass);
    }
}

// TODO [anchor 0x40CDC9] PASS 1 (billboard) NOT IMPLEMENTED - gap SH-05, Pass 4 / W9.
// BLOCKED BY UNPROVEN DATA, NOT BY TIME. Exact state of the investigation:
//
//  WHAT IS PROVEN (and therefore immediately available the day this unblocks):
//   * The guard: `mov esi, 1` @0x40CCED -> `jmp loc_40CDC6` @0x40CD22 -> `cmp [ebx+34h], esi`
//     @0x40CDC6 / `jnz loc_40D3A8` @0x40CDC9  ==>  the branch is taken iff `mesh+52 == 1`.
//   * The fields are ALL readable from asset::SObjectMesh::header (Asset/Model.h, not owned
//     here) WITHOUT modifying it - layout equivalence established by Mesh_ReadFromMemory
//     0x40C380: `qmemcpy(a1+1, v6+a2, 0x44)` @0x40C3C9 copies on-disk +4..+71 into mesh+4,
//     `qmemcpy(a1+18, v6+a2+68, 0x130)` @0x40C3EC copies +72..+375 into mesh+72, and the
//     C++ parser (Model.cpp:88-89) places exactly these 372 bytes into `header`. Hence:
//       header[48] = mesh+52 (billboard flag, u32)   header[52] = mesh+56 (axis mode, u32)
//       header[56] = mesh+60 (width, float)           header[60] = mesh+64 (height, float)
//       header[68..371] = mesh+72..+375 = the quad's 4 inline RenderVertex(76 B)
//   * The rendering: CPU skinning of the 4 vertices by the palette (@0x40CE47..0x40D0F9),
//     centroid x0.25 (@0x40D117..0x40D129), half-width = (mesh+60)*0.5 (`fld [ebx+3Ch]` @0x40CDCF),
//     half-height = (mesh+64)*0.5 (`fld [ebx+40h]` @0x40CDFF), bind pass 1 VS01+PS02
//     (@0x40D2A3..0x40D2DC), SetMatrix(mWorldViewProjMatrix) @0x40D32B,
//     SetFloatArray(mLightAmbient, 3) @0x40D34D, SetTexture @0x40D36C,
//     SetVertexDeclaration @0x40D383, DrawPrimitiveUP(TRIANGLESTRIP, 2, quad, 76) @0x40D39A.
//
//  WHAT BLOCKS THIS (unproven - DO NOT GUESS, per project rule): the quad's orientation.
//   The axis vector is `&flt_18C5264` if (mesh+56)==1 (`cmp [ebx+38h], esi` @0x40CDD2),
//   else `&unk_18C52BC`. Both addresses ARE fields of g_GxdRenderer:
//     0x18C5264 - 0x18C4EF8 = 876   |   0x18C52BC - 0x18C4EF8 = 964
//   `xrefs_to` on both returns ONLY READERS (9 and 7: Model_DrawSkinnedSubset,
//   PtclDef_RenderQuads, Mesh_DrawAnimatedFrame, cMesh_RenderAnimated,
//   cMesh_RenderBillboardOutline, cVtxAnimMesh_RenderAnimated/RenderFrame/
//   RenderBillboardShadow) - NO absolute writer, the write being esi-relative
//   (esi = this = 0x18C4EF8), same pattern as flt_18C53C0 (see MeshRenderer.h:396-401).
//   The presumed producer is GXD_BeginScene 0x404640 (billboard base/up, +876/+888/+964/+976)
//   - BUT `xrefs_to(0x404640)` = 0: the function is DEAD in the binary. So there are
//   9 live readers of a value that no reachable producer computes: either another
//   esi-relative writer remains to be found, or these vectors are frozen at init.
//   -> Fabricating a "camera right/up" axis here would be an INVENTION: the quad would be
//      oriented, but not like the original. Until the producer of +876/+964 is
//      identified (lead: x32dbg, memory watch on 0x18C5264 during an in-game frame),
//      the billboard branch stays unwritten. Do not "complete" this TODO by guesswork.
void MeshRenderer::DrawSkinnedSubset(const SkinnedMesh& mesh, int lod,
                                     const D3DXMATRIX&  world,
                                     const BonePalette& palette,
                                     const SkinnedMesh* matSrc,
                                     int                pass) {
    if (!Ready() || lod < 0 || lod >= static_cast<int>(mesh.lods.size())) return;
    const SkinnedLod& L = mesh.lods[lod];
    if (!L.vb || !L.ib || L.vertexCount == 0 || L.faceCount == 0) return;

    // -1) MATERIAL SOURCE (a1 = override) - @0x40ca96..0x40cabc:
    //       if (a1) { v61 = a1;     v62 = a7;     v56 = a8;     }  // override triplet
    //       else    { v61 = a2+712; v62 = a2+768; v56 = a2+824; }  // mesh's own materials
    //     The GEOMETRY (`L`) is ALWAYS `mesh`'s own - only the materials switch.
    const SkinnedMesh& M = matSrc ? *matSrc : mesh;

    // 0a) PASS FILTER (a3) - Model_DrawSkinnedSubset @0x40cb14..0x40cb32:
    //      if (a3 == 1) { if (blendMode == 2) return; }   // pass 1: everything EXCEPT alpha blend
    //      else         { if (blendMode != 2) return; }   // pass 2: alpha blend ONLY
    //    blendMode comes from v61+44 = the EFFECTIVE material (so mesh[0]'s under inheritance).
    //    kPassBoth (default) = shim outside the binary: no filter (see .h).
    if (pass == kDrawPass_Opaque) {
        if (M.blendMode == 2) return;
    } else if (pass == kDrawPass_Blend) {
        if (M.blendMode != 2) return;
    }

    // 0b) Shader source: REAL npk slots (Shader03 VS03_SkinnedLit 0x409AB0 + Shader04
    //    PS04_Tex 0x409CC0) if a ShaderSet is attached, else reconstructed HLSL (fallback).
    //    Uniforms/handles proven identical (0x409c23..0x409c8f) -> purely internal swap.
    IDirect3DVertexShader9*      useVS   = vs_;
    IDirect3DPixelShader9*       usePS   = ps_;
    ID3DXConstantTable*          useCT   = ctVs_;
    D3DXHANDLE                   hKey    = hKeyMatrix_;
    D3DXHANDLE                   hWvp    = hWorldViewProj_;
    D3DXHANDLE                   hDir    = hLightDirection_;
    D3DXHANDLE                   hAmb    = hLightAmbient_;
    D3DXHANDLE                   hDif    = hLightDiffuse_;
    UINT                         useSamp = sampler0_;
    IDirect3DVertexDeclaration9* useDecl = decl_;
    bool                         realShader = false;
    if (shaderSet_) {
        const GxdShader& gvs = shaderSet_->Get(GxdShaderId::VS03_SkinnedLit);
        const GxdShader& gps = shaderSet_->Get(GxdShaderId::PS04_Tex);
        if (gvs.Valid() && gps.Valid()) {
            realShader = true;
            useVS = gvs.vs;   usePS = gps.ps;   useCT = gvs.ct;
            hKey  = gvs.Handle("mKeyMatrix");
            hWvp  = gvs.Handle("mWorldViewProjMatrix");
            hDir  = gvs.Handle("mLightDirection");
            hAmb  = gvs.Handle("mLightAmbient");
            hDif  = gvs.Handle("mLightDiffuse");
            const int s = gps.Sampler("mTexture0");             // mTexture0 register (0x409e34)
            useSamp = (s >= 0) ? static_cast<UINT>(s) : 0;
            // Real vertex declaration g_GxdSkinVtxDecl (0x1945918, derived from 0x814A58).
            if (IDirect3DVertexDeclaration9* d = shaderSet_->SkinnedVertexDecl()) useDecl = d;
        }
    }

    // 0c) SOBJ-04 / GX-SH-01 - MULTI-TEXTURE PASS SELECTION (@0x40d3a8..0x40d66a).
    //   Only the `else` branch (g_TextureDetailLevel >= 2) is live (see kTextureDetailLevel).
    //   Exact decompiled logic (v21 = LOD index; v62 = mat1; v56 = mat2; `+52` = pTexture):
    //     if (v21 != 0)                        -> PASS 2   ; multi-texture ONLY at LOD 0 (0x40d61f)
    //     mat1.tex && mat2.tex                 -> PASS 4   ; 0x40d62d -> 0x40d660 true -> 0x40d675
    //     mat1.tex && !mat2.tex                -> PASS 3   ; 0x40d660 false -> falls to 0x40d9b6
    //     !mat1.tex && mat2.tex                -> PASS 4   ; 0x40d63e not taken -> 0x40d660 true
    //     !mat1.tex && !mat2.tex               -> PASS 2   ; 0x40d642 -> LABEL_53
    //   mat->sampler mapping proven BY THE CONSTANT NAMES of the loaders, NOT by IDA's stack
    //   naming (unreliable in this area - esp delta poorly tracked):
    //     Shader_LoadPS06_MultiTex  0x40A060 -> mTexture0, mTexture1                 (2 textures)
    //     Shader_LoadPS08_MultiTex3 0x40A490 -> mTexture0, mTexture1, mTexture2      (3 textures)
    //   corroborated by the NUMBER of SetTexture calls (pass 3: 2 @0x40db25/@0x40db44; pass 4: 3
    //   @0x40d849/@0x40d868/@0x40d887) -> mat0->mTexture0, mat1->mTexture1, mat2->mTexture2.
    //
    //   FALLBACK PRESERVED: the reconstructed HLSL kSkinnedPS ONLY has mTexture0 -> pass 3/4 is
    //   only entered on REAL npk shaders. Without a ShaderSet, we stay on pass 2: the path
    //   that works today is untouched.
    int usePass = kPass_SkinnedLit;
    const GxdShader* mtVS = nullptr;
    const GxdShader* mtPS = nullptr;
    // Multi-texture routing (Model_DrawSkinnedSubset 0x40CA40 @0x40d62d/@0x40d660): models with
    // mat1/mat2 (detail>=2) go to passes 3/4 (VS05/VS07+PS06/PS08). WARNING FOLLOW-UP
    // GX-MULTITEX-01: porting these passes renders a multi-material armor DARK (mTexture0 x tex1 x
    // tex2 modulation + lighting MOVED VS->PS), and a SEPARATE rendering gap makes a multi-mesh
    // armor (e.g. C003035) EXPLODE in-world -- NOT RESOLVED by the pass (force-pass-2 tried, no
    // effect) NOR by bone padding (GX-BONEPAD-01, no effect -> not an out-of-palette bone) NOR by
    // the palette (the base body renders clean+animated = valid palette). Root cause needs DYNAMIC
    // instrumentation (sub-mesh positions/BLENDINDICES at the moment they explode, x32dbg on
    // 0x40d4e8/0x40d60d). Routing kept FAITHFUL (the binary does 3/4).
    if (realShader && lod == 0 && kTextureDetailLevel >= 2) {
        const bool t1 = (M.tex1 != nullptr); // v62 && *(v62+52)
        const bool t2 = (M.tex2 != nullptr); // v56 && *(v56+52)
        if (t1 || t2) {
            // pass 4 <=> t2 (t1&&t2 AND !t1&&t2) ; pass 3 <=> t1 && !t2.
            const bool wantPass4 = t2;
            const GxdShader& gv = shaderSet_->Get(wantPass4 ? GxdShaderId::VS07_SkinnedEye
                                                            : GxdShaderId::VS05_Skinned);
            const GxdShader& gp = shaderSet_->Get(wantPass4 ? GxdShaderId::PS08_MultiTex3
                                                            : GxdShaderId::PS06_MultiTex);
            if (gv.Valid() && gp.Valid()) {
                usePass = wantPass4 ? kPass_MultiTex3 : kPass_MultiTex2;
                mtVS = &gv;  mtPS = &gp;
                useVS = gv.vs;  usePS = gp.ps;  useCT = gv.ct;
                // VS05 (0x409E80): mKeyMatrix, mWorldViewProjMatrix, mLightDirection.
                // VS07 (0x40A290): same + mCameraEye. Neither has mLightAmbient/mLightDiffuse
                // -> in passes 3/4 ambient/diffuse live on the PIXEL shader (see below).
                hKey = gv.Handle("mKeyMatrix");
                hWvp = gv.Handle("mWorldViewProjMatrix");
                hDir = gv.Handle("mLightDirection");
                hAmb = nullptr;
                hDif = nullptr;
            }
        }
    }

    // 1) Blend states for the EFFECTIVE material (v16 = *(v61+44)).
    applyBlendMode(M.blendMode);

    // 2) Bind the program (avoids redundant re-binds via currentPass_ == g_CurrentShaderPass).
    if (currentPass_ != usePass) {
        dev_->SetVertexShader(useVS);
        dev_->SetPixelShader(usePS);
        currentPass_ = usePass;
    }
    dev_->SetVertexDeclaration(useDecl); // g_SkinVertexDecl (method +348)

    // 3) WVP = world * view * proj (v84 in the original).
    D3DXMATRIX wvp;
    D3DXMatrixMultiply(&wvp, &world, &viewProj_);

    // 4) Light direction brought back into object space (inverse of world),
    //    see D3DXVec3TransformNormal(&v61,&v61,&invWorld) + normalize.
    D3DXMATRIX invWorld;
    D3DXMatrixInverse(&invWorld, nullptr, &world);
    D3DXVECTOR3 lightObj;
    D3DXVec3TransformNormal(&lightObj, &lightDirWorld_, &invWorld);
    D3DXVec3Normalize(&lightObj, &lightObj);

    // 5) Bone palette -> mKeyMatrix (SetMatrixArray, method +88 of ID3DXConstantTable).
    //    g_GxdSh03_hKeyMatrix 0x1945974 - ex-VeryOldClient: mAmbient2_VS_KeyMatrix.
    //    Fallback palette (identity) if no valid slice is supplied.
    const D3DXMATRIX* palMats = identityPalette_;
    UINT boneCount = 1;
    if (palette.Valid()) {
        palMats   = palette.matrices;
        boneCount = palette.count;
        // Model_DrawSkinnedSubset does NOT clamp (0x40d4e8): on a real shader, nBones is passed
        // as-is (D3DX itself bounds it to mKeyMatrix.Elements = boneArraySize_). On the HLSL
        // fallback, mKeyMatrix[40] enforces the kMaxBones bound to avoid overflowing the array.
        if (!realShader && boneCount > kMaxBones) boneCount = kMaxBones;
    }
    // GX-BONEPAD-01: pads the palette to kMaxBones with identity. The player skeleton has 76
    // bones, but a multi-mesh armor (e.g. C003035) can reference a bone >= 76 in its BLENDINDICES;
    // without padding, mKeyMatrix[boneCount..kMaxBones) keeps STALE matrices (previous entity) ->
    // scattered vertices + degenerate (dark) normals. Filling [boneCount,kMaxBones) with identity
    // makes those vertices render in ASSEMBLED REST POSE. Base body (bones 0..75) unchanged. Also
    // covers the invalid-palette fallback (boneCount=1 -> everything identity = bind pose, rather
    // than stale registers).
    if (hKey) {
        if (boneCount < kMaxBones) {
            std::memcpy(paletteScratch_, palMats, static_cast<size_t>(boneCount) * sizeof(D3DXMATRIX));
            for (UINT b = boneCount; b < kMaxBones; ++b) D3DXMatrixIdentity(&paletteScratch_[b]);
            useCT->SetMatrixArray(dev_, hKey, paletteScratch_, kMaxBones);
        } else {
            useCT->SetMatrixArray(dev_, hKey, palMats, boneCount);
        }
    }

    // 6) WVP + light (SetMatrix +84, SetFloatArray +72).
    if (hWvp) useCT->SetMatrix(dev_, hWvp, &wvp);
    if (hDir) useCT->SetFloatArray(dev_, hDir, &lightObj.x, 3);
    if (hAmb) useCT->SetFloatArray(dev_, hAmb, &lightAmbient_.x, 3);
    if (hDif) useCT->SetFloatArray(dev_, hDif, &lightDiffuse_.x, 3);

    // 7) Textures (method +260 SetTexture) - GX-SH-02: mat1/mat2 are now CARRIED
    //    through here (SkinnedMesh::tex1/tex2) and uploaded to their samplers.
    if (usePass == kPass_SkinnedLit) {
        // Pass 2: mTexture0 only (@0x40d590).
        if (M.diffuse) dev_->SetTexture(useSamp, M.diffuse);
    } else {
        // Passes 3/4: samplers resolved BY NAME on the real PS (no hardcoded register).
        const int s0 = mtPS->Sampler("mTexture0");
        if (s0 >= 0) dev_->SetTexture(static_cast<UINT>(s0), M.diffuse); // @0x40db25 / @0x40d849
        const int s1 = mtPS->Sampler("mTexture1");
        if (s1 >= 0) dev_->SetTexture(static_cast<UINT>(s1), M.tex1);    // @0x40db44 / @0x40d868
        if (usePass == kPass_MultiTex3) {
            const int s2 = mtPS->Sampler("mTexture2");
            if (s2 >= 0) dev_->SetTexture(static_cast<UINT>(s2), M.tex2); // @0x40d887 (pass 4 only)
        }
        // mLightAmbient/mLightDiffuse on the PIXEL shader in passes 3/4 (PS06 0x40A060 / PS08
        // 0x40A490): @0x40db65/@0x40db86 (pass 3) and @0x40d8a9/@0x40d8cb (pass 4).
        if (mtPS->ct) {
            if (D3DXHANDLE h = mtPS->Handle("mLightAmbient"))
                mtPS->ct->SetFloatArray(dev_, h, &lightAmbient_.x, 3);
            if (D3DXHANDLE h = mtPS->Handle("mLightDiffuse"))
                mtPS->ct->SetFloatArray(dev_, h, &lightDiffuse_.x, 3);
        }
        // mCameraEye - VS07 ONLY (pass 4, 0x40A290): world camera position
        // (g_CameraEye dword_18C51C0/C4/C8 @0x40d757..0x40d783) brought back into OBJECT SPACE by
        // Vec3_TransformCoord(v89, v89, invWorld) @0x40d78b, then set @0x40d82a.
        if (usePass == kPass_MultiTex3 && useCT) {
            if (D3DXHANDLE h = mtVS->Handle("mCameraEye")) {
                D3DXVECTOR3 eyeObj;
                D3DXVec3TransformCoord(&eyeObj, &eye_, &invWorld);
                useCT->SetFloatArray(dev_, h, &eyeObj.x, 3);
            }
        }
    }

    // 8) Stream + indices + draw (SetStreamSource +400 stride 76, SetIndices +416,
    //    DrawIndexedPrimitive +328: TRIANGLELIST, vtxCount, triCount).
    //    NB: the skinned path does NOT set ZENABLE/CULLMODE (faithful to Model_DrawSkinnedSubset
    //    0x40CA40, which doesn't set them either): it inherits the 2D bracket (Gfx_Begin2D/End2D,
    //    which restores ZENABLE=TRUE) and the ambient CULLMODE. Clean depth is guaranteed by the
    //    GX2D-01 fix (the 2D background no longer writes to Z), see LoginScene::CharSelectRenderBg.
    dev_->SetStreamSource(0, L.vb, 0, static_cast<UINT>(sizeof(GpuSkinVertex)));
    dev_->SetIndices(L.ib);
    dev_->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, L.vertexCount, 0, L.faceCount);

    // 9) Restore blend states (LABEL_72 of the original, @0x40d953: `*(v61+44)`
    //    -> same as on the way in, on the EFFECTIVE material).
    resetBlendMode(M.blendMode);
}

// Per-material blend states - Model_DrawSkinnedSubset 0x40CA40 @0x40cba4..0x40cc30.
//   blendMode 1 = alpha-test; blendMode 2 = ALPHA BLEND (SRCALPHA/INVSRCALPHA), NOT additive.
//
// FIXED DEFAULT (Pass 4 / W7) - this function was NOT bit-exact, contrary to what the
// comment living here used to claim: EACH of the two branches was MISSING one state.
// Hex-Rays lost them because both branches share their LAST SetRenderState via a common tail
// (`jmp loc_40CC22` @0x40cbd8 / fallthrough @0x40cc20 -> `call` @0x40cc30): the
// decompiler did not restitute those arguments. Found at the disassembly level:
//   mode 1: push esi(=1)/push 0Fh @0x40cbb2 -> ALPHATESTENABLE=1 @0x40cbbb
//           push 5/push 19h      @0x40cbca -> ALPHAFUNC=5 (D3DCMP_GREATEREQUAL) @0x40cbcf
//           push 80h/push 18h    @0x40cbd1 -> ALPHAREF=128            <-- WAS MISSING
//   mode 2: push 0/push 0Eh      @0x40cbf0 -> ZWRITEENABLE=0 @0x40cbf5
//           push esi(=1)/push 1Bh@0x40cc04 -> ALPHABLENDENABLE=1 @0x40cc08
//           push 5/push 13h      @0x40cc17 -> SRCBLEND=5 (D3DBLEND_SRCALPHA) @0x40cc1c
//           push 6/push 14h      @0x40cc1e -> DESTBLEND=6 (D3DBLEND_INVSRCALPHA) <-- WAS MISSING
// WHY THIS WAS CRITICAL: resetBlendMode resets ALPHAREF=0 and DESTBLEND=1 (ZERO). Without these
// two states, from the 2nd mesh onward: mode 1 -> alpha-test `alpha >= 0` = always true = INERT,
// and mode 2 -> SRCALPHA/ZERO blend = destination OVERWRITTEN instead of blended. The bug was
// LATENT as long as blendMode was 0 everywhere; fixing SOBJ-02 without fixing this would have
// ACTIVATED it. The two fixes are therefore inseparable.
void MeshRenderer::applyBlendMode(uint32_t blendMode) {
    if (blendMode == 1) {
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaTestEnable), TRUE);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaFunc), kCMP_GreaterEqual);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaRef), kAlphaRef_Material);
    } else if (blendMode == 2) {
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_ZWriteEnable), FALSE);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaBlendEnable), TRUE);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_SrcBlend), kBLEND_SrcAlpha);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_DestBlend), kBLEND_InvSrcAlpha);
    }
}

void MeshRenderer::resetBlendMode(uint32_t blendMode) {
    if (blendMode == 1) {
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaRef), 0);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaFunc), kCMP_Always);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaTestEnable), FALSE);
    } else if (blendMode == 2) {
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_DestBlend), kBLEND_Zero);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_SrcBlend),  kBLEND_One);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_AlphaBlendEnable), FALSE);
        dev_->SetRenderState(static_cast<D3DRENDERSTATETYPE>(kRS_ZWriteEnable), TRUE);
    }
}

} // namespace ts2::gfx

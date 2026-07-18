// Gfx/ShaderSet.h — the 12 HLSL shaders of the GXD engine.
//
// Faithful reimplementation of the 12 Shader_LoadVSxx/PSxx loaders (0x409730..0x40ACB0),
// called in sequence by GXD_DeviceCreate 0x401610 / GXD_DeviceReinit 0x4023F0.
// Ref. Docs/TS2_GXD_ENGINE.md §2.2.
// ex-VeryOldClient: TW2AddIn::GXD::InitShader -> MakeShaderProgram01..16 (v2). CONFIRMED §1.4.
// NB CONFLICT (D-4, out of apply-pass scope): on the target side the shader slots are FILE-LEVEL
//   GLOBALS (block 0x1945918+), NOT members of g_GxdRenderer like VeryOldClient's mAmbientN_*/
//   mNormalN_* — NEVER transpose those VeryOld fields into struct offsets. ShaderSet models them
//   as independent structs = faithful to the NATURE (non-member), not to the frozen layout.
//
// Original scheme (identical for all 12):
//   Npk_OpenFile("./GXDEFFECT/GXDEffect.npk", key {1,4,4,1})
//   -> Npk_FindEntryByName("ShaderNN.fx") -> Npk_GetEntrySize -> operator new
//   -> Npk_ReadEntryData (XTEA-decrypts + zlib-decompresses -> plaintext HLSL source)
//   -> D3DXCompileShader(src, len, NULL, NULL, "Main", "vs_2_0"|"ps_2_0", 0,
//                        &ppShader, &ppErr, &slot.ct)
//   -> GetBufferPointer -> CreateVertexShader (vtbl+364) / CreatePixelShader (vtbl+424)
//   -> Release blob -> for each uniform  GetConstantByName (vtbl+36)
//                   -> for each texture  GetConstantDesc  (vtbl+24)
//                      (RegisterIndex = sampler index at render time)
//   -> SetDefaults(device) (vtbl+44). Failure => device not created.
//
// Unlike the original (which reopens the .npk 12 times), we open the archive
// once and read each entry: functionally identical behavior.
//
// Uses the DirectX SDK (June 2010): ID3DXConstantTable / D3DXCompileShader.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace ts2::asset { class NpkArchive; }

namespace ts2::gfx {

// IDs of the 12 shaders, in GXD_DeviceCreate's loading order.
// Numbers 10, 11, 13 are absent from the sequence (not loaded by the client).
// VeryOldClient (v2 TW2AddIn) -> IDA slot mapping: CONFIRMED Docs/TS2_GXD_ROSETTA.md §1.4.
// The 4 no-op slots (10/11/13/16) = MakeShaderProgram10/11/13/16 (return TRUE, npk=null).
enum class GxdShaderId : int {
    VS01_WorldVP = 0,   // Shader01.fx vs — 0x409730 — mWorldViewProjMatrix, mLightAmbient  // ex-VeryOldClient: mAmbient1_VS (MakeShaderProgram01)
    PS02_Tex,           // Shader02.fx ps — 0x4098F0 — mTexture0                              // ex-VeryOldClient: mAmbient1_PS (MakeShaderProgram02)
    VS03_SkinnedLit,    // Shader03.fx vs — 0x409AB0 — mKeyMatrix + WVP + dir/amb/diff        // ex-VeryOldClient: mAmbient2_VS (MakeShaderProgram03) — Pass 2 (ONLY one wired to the active render path)
    PS04_Tex,           // Shader04.fx ps — 0x409CC0 — mTexture0                              // ex-VeryOldClient: mAmbient2_PS (MakeShaderProgram04)
    VS05_Skinned,       // Shader05.fx vs — 0x409E80 — mKeyMatrix + WVP + mLightDirection     // ex-VeryOldClient: mNormal1_VS (MakeShaderProgram05) — Pass 3 (most frequent in-game default)
    PS06_MultiTex,      // Shader06.fx ps — 0x40A060 — tex0/tex1 + amb/diff                   // ex-VeryOldClient: mNormal1_PS (MakeShaderProgram06)
    VS07_SkinnedEye,    // Shader07.fx vs — 0x40A290 — mKeyMatrix + WVP + dir + mCameraEye     // ex-VeryOldClient: mNormal2_VS (MakeShaderProgram07) — Pass 4 (specular/Fresnel)
    PS08_MultiTex3,     // Shader08.fx ps — 0x40A490 — tex0/tex1/tex2 + amb/diff              // ex-VeryOldClient: mNormal2_PS (MakeShaderProgram08)
    VS09_Skinned,       // Shader09.fx vs — 0x40A700 — mKeyMatrix + WVP                        // ex-VeryOldClient: mAmbient3_VS (MakeShaderProgram09) — flat shadow (PS NULL)
    PS12_PostBlur,      // Shader12.fx ps — 0x40A8E0 — mTexture0 + mTexture0PostSize           // ex-VeryOldClient: mFilter1_PS (MakeShaderProgram12) — bloom H
    PS14_PostBlur,      // Shader14.fx ps — 0x40AAD0 — mTexture0 + mTexture0PostSize           // ex-VeryOldClient: mFilter2_PS (MakeShaderProgram14) — bloom V
    VS15_WorldVP,       // Shader15.fx vs — 0x40ACB0 — mWorldViewProjMatrix                    // ex-VeryOldClient: mShadow1_VS (MakeShaderProgram15) — stencil shadow volume
    kCount
};

// A compiled shader + its resolved constant table.
// Reproduces an Object B "slot" (base+0x80A28, see §2.2), replacing the
// frozen byte layout with equivalent named tables:
//   - `handles`  : D3DXHANDLE per uniform name (matrices / vectors).
//   - `samplers` : RegisterIndex (sampler index) per texture name.
struct GxdShader {
    bool                     isPixel = false;
    ID3DXConstantTable*      ct = nullptr;   // slot+0 (out param of D3DXCompileShader)
    IDirect3DVertexShader9*  vs = nullptr;   // slot+4 if !isPixel
    IDirect3DPixelShader9*   ps = nullptr;   // slot+4 if isPixel

    std::unordered_map<std::string, D3DXHANDLE> handles;  // matrices/vectors
    std::unordered_map<std::string, UINT>       samplers; // texture RegisterIndex

    // Named handle (nullptr if absent).
    D3DXHANDLE Handle(const char* name) const {
        auto it = handles.find(name);
        return it == handles.end() ? nullptr : it->second;
    }
    // Sampler index of a texture (-1 if absent).
    int Sampler(const char* name) const {
        auto it = samplers.find(name);
        return it == samplers.end() ? -1 : static_cast<int>(it->second);
    }
    bool Valid() const { return ct && (vs || ps); }
};

class ShaderSet {
public:
    ShaderSet() = default;
    ~ShaderSet();
    ShaderSet(const ShaderSet&) = delete;
    ShaderSet& operator=(const ShaderSet&) = delete;

    static constexpr int  kNumShaders = static_cast<int>(GxdShaderId::kCount);
    // Skinned vertex stride (declaration unk_814A58: 76 bytes).
    static constexpr UINT kSkinnedVertexStride = 76;

    // Compiles the 12 shaders from an already-open GXDEffect.npk archive,
    // then creates the skinned vertex declaration. `device` = shared D3D9 device.
    // Returns false (and frees everything) if a single shader fails — faithful to the
    // original where a compile failure prevents device creation.
    bool Load(IDirect3DDevice9* device, asset::NpkArchive& npk);

    // Variant: opens the archive itself (XTEA key {1,4,4,1}).
    bool LoadFromFile(IDirect3DDevice9* device,
                      const std::string& npkPath = "./GXDEFFECT/GXDEffect.npk");

    void Release();
    bool Ready() const { return ready_; }

    const GxdShader& Get(GxdShaderId id) const {
        return shaders_[static_cast<int>(id)];
    }

    // 76 B skinned vertex declaration (POSITION/BLENDWEIGHT/BLENDINDICES/
    // TANGENT/BINORMAL/NORMAL/TEXCOORD). Created from unk_814A58.
    // ex-VeryOldClient: mVertexElementForSKIN2 -> mDECLForSKIN2 (stride 76), CONFIRMED §1.5.
    IDirect3DVertexDeclaration9* SkinnedVertexDecl() const { return vertexDecl_; }

    // Binds the current shader (SetVertexShader / SetPixelShader).
    void Bind(GxdShaderId id) const;

    // Sets a texture on the shader's resolved sampler (SetTexture at the right stage).
    // false if the texture is not a sampler of this shader.
    bool BindTexture(GxdShaderId id, const char* name,
                     IDirect3DBaseTexture9* tex) const;

    // Constant helpers (delegate to ID3DXConstantTable via the named handle).
    bool SetMatrix(GxdShaderId id, const char* name, const D3DXMATRIX& m) const;
    bool SetMatrixArray(GxdShaderId id, const char* name,
                        const D3DXMATRIX* m, UINT count) const;  // e.g. mKeyMatrix
    bool SetVector(GxdShaderId id, const char* name, const D3DXVECTOR4& v) const;
    bool SetFloatArray(GxdShaderId id, const char* name,
                       const float* f, UINT count) const;        // e.g. mTexture0PostSize

private:
    bool CreateVertexDecl();

    IDirect3DDevice9*            device_     = nullptr;
    GxdShader                    shaders_[kNumShaders];
    IDirect3DVertexDeclaration9* vertexDecl_ = nullptr;
    bool                         ready_      = false;
};

} // namespace ts2::gfx

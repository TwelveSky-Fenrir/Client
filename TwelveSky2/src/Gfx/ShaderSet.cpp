// Gfx/ShaderSet.cpp — loading/compilation of the 12 GXD HLSL shaders.
// Faithful to loaders 0x409730..0x40ACB0 (see Docs/TS2_GXD_ENGINE.md §2.2).
#include "Gfx/ShaderSet.h"
#include "Asset/NpkArchive.h"
#include "Asset/Xtea.h"
#include "Core/Log.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

// Static description of a shader: .npk file, profile, textures (samplers)
// and constants (matrices/vectors) to resolve. Lists terminated by nullptr.
// The resolution order mirrors the disassembly (textures first for
// multi-texture PS), but since each uniform is independent, the order has
// no functional effect.
struct ShaderDef {
    GxdShaderId id;
    const char* file;      // "ShaderNN.fx"
    const char* profile;   // "vs_2_0" | "ps_2_0"
    bool        isPixel;
    const char* textures[4];   // mTexture0..2 (GetConstantByName + GetConstantDesc)
    const char* constants[8];  // matrices/vectors (GetConstantByName)
};

// The 12 GXD shaders, in GXD_DeviceCreate 0x401610's loading order.
// ex-VeryOldClient (semantic v2, CONFIRMED §1.4): 01=mAmbient1, 02=mAmbient1_PS, 03=mAmbient2,
//   04=mAmbient2_PS, 05=mNormal1, 06=mNormal1_PS, 07=mNormal2, 08=mNormal2_PS, 09=mAmbient3,
//   12=mFilter1_PS, 14=mFilter2_PS, 15=mShadow1. Per-slot detail: see enum GxdShaderId (ShaderSet.h).
const ShaderDef kDefs[] = {
    { GxdShaderId::VS01_WorldVP,    "Shader01.fx", "vs_2_0", false,
      { nullptr },
      { "mWorldViewProjMatrix", "mLightAmbient", nullptr } },

    { GxdShaderId::PS02_Tex,        "Shader02.fx", "ps_2_0", true,
      { "mTexture0", nullptr },
      { nullptr } },

    { GxdShaderId::VS03_SkinnedLit, "Shader03.fx", "vs_2_0", false,
      { nullptr },
      { "mKeyMatrix", "mWorldViewProjMatrix", "mLightDirection",
        "mLightAmbient", "mLightDiffuse", nullptr } },

    { GxdShaderId::PS04_Tex,        "Shader04.fx", "ps_2_0", true,
      { "mTexture0", nullptr },
      { nullptr } },

    { GxdShaderId::VS05_Skinned,    "Shader05.fx", "vs_2_0", false,
      { nullptr },
      { "mKeyMatrix", "mWorldViewProjMatrix", "mLightDirection", nullptr } },

    { GxdShaderId::PS06_MultiTex,   "Shader06.fx", "ps_2_0", true,
      { "mTexture0", "mTexture1", nullptr },
      { "mLightAmbient", "mLightDiffuse", nullptr } },

    { GxdShaderId::VS07_SkinnedEye, "Shader07.fx", "vs_2_0", false,
      { nullptr },
      { "mKeyMatrix", "mWorldViewProjMatrix", "mLightDirection",
        "mCameraEye", nullptr } },

    { GxdShaderId::PS08_MultiTex3,  "Shader08.fx", "ps_2_0", true,
      { "mTexture0", "mTexture1", "mTexture2", nullptr },
      { "mLightAmbient", "mLightDiffuse", nullptr } },

    { GxdShaderId::VS09_Skinned,    "Shader09.fx", "vs_2_0", false,
      { nullptr },
      { "mKeyMatrix", "mWorldViewProjMatrix", nullptr } },

    { GxdShaderId::PS12_PostBlur,   "Shader12.fx", "ps_2_0", true,
      { "mTexture0", nullptr },
      { "mTexture0PostSize", nullptr } },

    { GxdShaderId::PS14_PostBlur,   "Shader14.fx", "ps_2_0", true,
      { "mTexture0", nullptr },
      { "mTexture0PostSize", nullptr } },

    { GxdShaderId::VS15_WorldVP,    "Shader15.fx", "vs_2_0", false,
      { nullptr },
      { "mWorldViewProjMatrix", nullptr } },
};
static_assert(sizeof(kDefs) / sizeof(kDefs[0]) == ShaderSet::kNumShaders,
              "incomplete shader table");

// Compiles a shader and resolves its constants. On failure, leaves `out`
// in whatever state was reached (ct/vs/ps possibly created) so ShaderSet::Release
// can free them; returns false.
bool LoadOneShader(IDirect3DDevice9* dev, ts2::asset::NpkArchive& npk,
                   const ShaderDef& def, GxdShader& out) {
    // Read the entry (XTEA-decrypted + zlib-decompressed -> plaintext HLSL).
    std::vector<uint8_t> src = npk.Read(def.file);
    if (src.empty()) {
        TS2_ERR("ShaderSet: '%s' not found/empty in GXDEffect.npk", def.file);
        return false;
    }

    LPD3DXBUFFER        shaderBlob = nullptr;
    LPD3DXBUFFER        errorBlob  = nullptr;
    ID3DXConstantTable* ct         = nullptr;

    HRESULT hr = D3DXCompileShader(
        reinterpret_cast<LPCSTR>(src.data()), static_cast<UINT>(src.size()),
        nullptr, nullptr, "Main", def.profile, 0,
        &shaderBlob, &errorBlob, &ct);
    if (FAILED(hr)) {
        // Original: MessageBoxA(error, "GXD(ERROR)") then abandons the device.
        // [INVALID_SHADER_PROGRAM-#02]. Here we log the compiler message instead.
        const char* msg = errorBlob
            ? static_cast<const char*>(errorBlob->GetBufferPointer())
            : "(no message)";
        TS2_ERR("[INVALID_SHADER_PROGRAM] %s (0x%08lX): %s", def.file, hr, msg);
        if (errorBlob)  errorBlob->Release();
        if (shaderBlob) shaderBlob->Release();
        if (ct)         ct->Release();
        return false;
    }
    if (errorBlob) errorBlob->Release();   // any warnings

    out.isPixel = def.isPixel;
    out.ct      = ct;                       // kept until teardown (slot+0)

    // Create the shader object from the compiled bytecode (slot+4).
    const DWORD* fn = static_cast<const DWORD*>(shaderBlob->GetBufferPointer());
    if (def.isPixel)
        hr = dev->CreatePixelShader(fn, &out.ps);
    else
        hr = dev->CreateVertexShader(fn, &out.vs);
    shaderBlob->Release();
    if (FAILED(hr)) {
        TS2_ERR("ShaderSet: Create%sShader('%s') failed (0x%08lX)",
                def.isPixel ? "Pixel" : "Vertex", def.file, hr);
        return false;
    }

    // Textures: handle + RegisterIndex (sampler index) via GetConstantDesc.
    for (int i = 0; i < 4 && def.textures[i]; ++i) {
        const char* name = def.textures[i];
        D3DXHANDLE h = ct->GetConstantByName(nullptr, name);
        if (!h) {
            TS2_ERR("ShaderSet: texture '%s' missing from %s", name, def.file);
            return false;
        }
        out.handles[name] = h;
        D3DXCONSTANT_DESC desc = {};
        UINT descCount = 1;
        if (FAILED(ct->GetConstantDesc(h, &desc, &descCount)) || descCount == 0) {
            TS2_ERR("ShaderSet: GetConstantDesc('%s') failed in %s", name, def.file);
            return false;
        }
        out.samplers[name] = desc.RegisterIndex;   // desc+8 = sampler at render time
    }

    // Constants (matrices/vectors): a simple named handle.
    for (int i = 0; i < 8 && def.constants[i]; ++i) {
        const char* name = def.constants[i];
        D3DXHANDLE h = ct->GetConstantByName(nullptr, name);
        if (!h) {
            TS2_ERR("ShaderSet: constant '%s' missing from %s", name, def.file);
            return false;
        }
        out.handles[name] = h;
    }

    // Default constant values (vtbl+44, last step of the loaders).
    return SUCCEEDED(ct->SetDefaults(dev));
}

} // namespace (anonymous)

ShaderSet::~ShaderSet() { Release(); }

bool ShaderSet::CreateVertexDecl() {
    // 76-byte skinned vertex declaration — exact copy of unk_814A58 (0x814A58).
    // ex-VeryOldClient: mVertexElementForSKIN2 -> mDECLForSKIN2 (stride 76) — BIT-EXACT, CONFIRMED §1.5.
    // Bytes observed: 7 elements + D3DDECL_END(). NB: BLENDINDICES is declared as
    // D3DDECLTYPE_D3DCOLOR (type 4) as-is in the binary (the 4 bone indices
    // reach the VS via the D3DCOLOR's BGRA channel).
    static const D3DVERTEXELEMENT9 kDecl[] = {
        {  0,  0, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION,     0 },
        {  0, 12, D3DDECLTYPE_FLOAT4,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDWEIGHT,  0 },
        {  0, 28, D3DDECLTYPE_D3DCOLOR, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BLENDINDICES, 0 },
        {  0, 32, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TANGENT,      0 },
        {  0, 44, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_BINORMAL,     0 },
        {  0, 56, D3DDECLTYPE_FLOAT3,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL,       0 },
        {  0, 68, D3DDECLTYPE_FLOAT2,   D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD,     0 },
        D3DDECL_END()   // { 0xFF, 0, D3DDECLTYPE_UNUSED(17), 0, 0, 0 }
    };
    // Total stride = 68 (last offset) + 8 (FLOAT2) = 76 == kSkinnedVertexStride.
    HRESULT hr = device_->CreateVertexDeclaration(kDecl, &vertexDecl_);
    if (FAILED(hr)) {
        TS2_ERR("ShaderSet: CreateVertexDeclaration failed (0x%08lX)", hr);
        return false;
    }
    return true;
}

bool ShaderSet::Load(IDirect3DDevice9* device, asset::NpkArchive& npk) {
    if (!device) { TS2_ERR("ShaderSet::Load: null device"); return false; }
    device_ = device;

    if (!CreateVertexDecl()) { Release(); return false; }

    for (const ShaderDef& def : kDefs) {
        GxdShader& slot = shaders_[static_cast<int>(def.id)];
        if (!LoadOneShader(device_, npk, def, slot)) {
            TS2_ERR("ShaderSet: failed to load %s -> device not created", def.file);
            Release();
            return false;
        }
    }

    ready_ = true;
    TS2_LOG("ShaderSet: 12 GXD shaders compiled + 76B skinned vertex declaration");
    return true;
}

bool ShaderSet::LoadFromFile(IDirect3DDevice9* device, const std::string& npkPath) {
    asset::NpkArchive npk;
    if (!npk.Open(npkPath, asset::kNpkKey)) {
        TS2_ERR("ShaderSet: unable to open '%s'", npkPath.c_str());
        return false;
    }
    return Load(device, npk);
}

void ShaderSet::Release() {
    for (GxdShader& s : shaders_) {
        if (s.vs) { s.vs->Release(); s.vs = nullptr; }
        if (s.ps) { s.ps->Release(); s.ps = nullptr; }
        if (s.ct) { s.ct->Release(); s.ct = nullptr; }
        s.handles.clear();
        s.samplers.clear();
        s.isPixel = false;
    }
    if (vertexDecl_) { vertexDecl_->Release(); vertexDecl_ = nullptr; }
    ready_ = false;
}

void ShaderSet::Bind(GxdShaderId id) const {
    if (!device_) return;
    const GxdShader& s = shaders_[static_cast<int>(id)];
    if (s.isPixel) device_->SetPixelShader(s.ps);
    else           device_->SetVertexShader(s.vs);
}

bool ShaderSet::BindTexture(GxdShaderId id, const char* name,
                            IDirect3DBaseTexture9* tex) const {
    if (!device_) return false;
    int reg = shaders_[static_cast<int>(id)].Sampler(name);
    if (reg < 0) return false;
    return SUCCEEDED(device_->SetTexture(static_cast<DWORD>(reg), tex));
}

bool ShaderSet::SetMatrix(GxdShaderId id, const char* name, const D3DXMATRIX& m) const {
    const GxdShader& s = shaders_[static_cast<int>(id)];
    D3DXHANDLE h = s.Handle(name);
    if (!s.ct || !h) return false;
    return SUCCEEDED(s.ct->SetMatrix(device_, h, &m));
}

bool ShaderSet::SetMatrixArray(GxdShaderId id, const char* name,
                               const D3DXMATRIX* m, UINT count) const {
    const GxdShader& s = shaders_[static_cast<int>(id)];
    D3DXHANDLE h = s.Handle(name);
    if (!s.ct || !h) return false;
    return SUCCEEDED(s.ct->SetMatrixArray(device_, h, m, count));
}

bool ShaderSet::SetVector(GxdShaderId id, const char* name, const D3DXVECTOR4& v) const {
    const GxdShader& s = shaders_[static_cast<int>(id)];
    D3DXHANDLE h = s.Handle(name);
    if (!s.ct || !h) return false;
    return SUCCEEDED(s.ct->SetVector(device_, h, &v));
}

bool ShaderSet::SetFloatArray(GxdShaderId id, const char* name,
                              const float* f, UINT count) const {
    const GxdShader& s = shaders_[static_cast<int>(id)];
    D3DXHANDLE h = s.Handle(name);
    if (!s.ct || !h) return false;
    return SUCCEEDED(s.ct->SetFloatArray(device_, h, f, count));
}

} // namespace ts2::gfx

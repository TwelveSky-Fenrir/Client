// Gfx/ShaderSet.cpp — chargement/compilation des 12 shaders HLSL GXD.
// Fidèle aux loaders 0x409730..0x40ACB0 (voir Docs/TS2_GXD_ENGINE.md §2.2).
#include "Gfx/ShaderSet.h"
#include "Asset/NpkArchive.h"
#include "Asset/Xtea.h"
#include "Core/Log.h"

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

namespace {

// Description statique d'un shader : fichier .npk, profil, textures (samplers)
// et constantes (matrices/vecteurs) à résoudre. Listes terminées par nullptr.
// L'ordre de résolution reproduit celui du désassemblage (textures d'abord pour
// les PS multi-textures), mais chaque uniforme étant indépendant, l'ordre est
// sans incidence fonctionnelle.
struct ShaderDef {
    GxdShaderId id;
    const char* file;      // "ShaderNN.fx"
    const char* profile;   // "vs_2_0" | "ps_2_0"
    bool        isPixel;
    const char* textures[4];   // mTexture0..2 (GetConstantByName + GetConstantDesc)
    const char* constants[8];  // matrices/vecteurs (GetConstantByName)
};

// Les 12 shaders GXD, dans l'ordre de GXD_DeviceCreate 0x401610.
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
              "table de shaders incomplete");

// Compile un shader et résout ses constantes. En cas d'échec, laisse `out`
// dans l'état atteint (ct/vs/ps éventuellement créés) pour que ShaderSet::Release
// les libère ; renvoie false.
bool LoadOneShader(IDirect3DDevice9* dev, ts2::asset::NpkArchive& npk,
                   const ShaderDef& def, GxdShader& out) {
    // Lecture de l'entrée (déchiffrée XTEA + décompressée zlib -> HLSL en clair).
    std::vector<uint8_t> src = npk.Read(def.file);
    if (src.empty()) {
        TS2_ERR("ShaderSet: '%s' introuvable/vide dans GXDEffect.npk", def.file);
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
        // Original : MessageBoxA(erreur, "GXD(ERROR)") puis abandon du device.
        // [INVALID_SHADER_PROGRAM-#02]. Ici on journalise le message du compilateur.
        const char* msg = errorBlob
            ? static_cast<const char*>(errorBlob->GetBufferPointer())
            : "(pas de message)";
        TS2_ERR("[INVALID_SHADER_PROGRAM] %s (0x%08lX) : %s", def.file, hr, msg);
        if (errorBlob)  errorBlob->Release();
        if (shaderBlob) shaderBlob->Release();
        if (ct)         ct->Release();
        return false;
    }
    if (errorBlob) errorBlob->Release();   // avertissements éventuels

    out.isPixel = def.isPixel;
    out.ct      = ct;                       // conservée jusqu'au teardown (slot+0)

    // Création de l'objet shader depuis le bytecode compilé (slot+4).
    const DWORD* fn = static_cast<const DWORD*>(shaderBlob->GetBufferPointer());
    if (def.isPixel)
        hr = dev->CreatePixelShader(fn, &out.ps);
    else
        hr = dev->CreateVertexShader(fn, &out.vs);
    shaderBlob->Release();
    if (FAILED(hr)) {
        TS2_ERR("ShaderSet: Create%sShader('%s') echoue (0x%08lX)",
                def.isPixel ? "Pixel" : "Vertex", def.file, hr);
        return false;
    }

    // Textures : handle + RegisterIndex (index de sampler) via GetConstantDesc.
    for (int i = 0; i < 4 && def.textures[i]; ++i) {
        const char* name = def.textures[i];
        D3DXHANDLE h = ct->GetConstantByName(nullptr, name);
        if (!h) {
            TS2_ERR("ShaderSet: texture '%s' absente de %s", name, def.file);
            return false;
        }
        out.handles[name] = h;
        D3DXCONSTANT_DESC desc = {};
        UINT descCount = 1;
        if (FAILED(ct->GetConstantDesc(h, &desc, &descCount)) || descCount == 0) {
            TS2_ERR("ShaderSet: GetConstantDesc('%s') echoue dans %s", name, def.file);
            return false;
        }
        out.samplers[name] = desc.RegisterIndex;   // desc+8 = sampler au rendu
    }

    // Constantes (matrices/vecteurs) : simple handle nommé.
    for (int i = 0; i < 8 && def.constants[i]; ++i) {
        const char* name = def.constants[i];
        D3DXHANDLE h = ct->GetConstantByName(nullptr, name);
        if (!h) {
            TS2_ERR("ShaderSet: constante '%s' absente de %s", name, def.file);
            return false;
        }
        out.handles[name] = h;
    }

    // Valeurs par défaut des constantes (vtbl+44, dernière étape des loaders).
    return SUCCEEDED(ct->SetDefaults(dev));
}

} // namespace (anonyme)

ShaderSet::~ShaderSet() { Release(); }

bool ShaderSet::CreateVertexDecl() {
    // Déclaration de vertex skinné 76 octets — copie exacte de unk_814A58 (0x814A58).
    // Octets relevés : 7 éléments + D3DDECL_END(). NB : BLENDINDICES est déclaré en
    // D3DDECLTYPE_D3DCOLOR (type 4) tel quel dans le binaire (les 4 indices d'os
    // arrivent au VS via le canal BGRA du D3DCOLOR).
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
    // Stride total = 68 (dernier offset) + 8 (FLOAT2) = 76 == kSkinnedVertexStride.
    HRESULT hr = device_->CreateVertexDeclaration(kDecl, &vertexDecl_);
    if (FAILED(hr)) {
        TS2_ERR("ShaderSet: CreateVertexDeclaration echoue (0x%08lX)", hr);
        return false;
    }
    return true;
}

bool ShaderSet::Load(IDirect3DDevice9* device, asset::NpkArchive& npk) {
    if (!device) { TS2_ERR("ShaderSet::Load : device nul"); return false; }
    device_ = device;

    if (!CreateVertexDecl()) { Release(); return false; }

    for (const ShaderDef& def : kDefs) {
        GxdShader& slot = shaders_[static_cast<int>(def.id)];
        if (!LoadOneShader(device_, npk, def, slot)) {
            TS2_ERR("ShaderSet: echec de chargement de %s -> device non cree", def.file);
            Release();
            return false;
        }
    }

    ready_ = true;
    TS2_LOG("ShaderSet: 12 shaders GXD compiles + declaration vertex skinne 76o");
    return true;
}

bool ShaderSet::LoadFromFile(IDirect3DDevice9* device, const std::string& npkPath) {
    asset::NpkArchive npk;
    if (!npk.Open(npkPath, asset::kNpkKey)) {
        TS2_ERR("ShaderSet: ouverture de '%s' impossible", npkPath.c_str());
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

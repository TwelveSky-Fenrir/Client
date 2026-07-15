// Gfx/ShaderSet.h — les 12 shaders HLSL du moteur GXD.
//
// Réimplémentation fidèle des 12 loaders Shader_LoadVSxx/PSxx (0x409730..0x40ACB0),
// appelés en séquence par GXD_DeviceCreate 0x401610 / GXD_DeviceReinit 0x4023F0.
// Réf. Docs/TS2_GXD_ENGINE.md §2.2.
//
// Schéma d'origine (identique pour les 12) :
//   Npk_OpenFile("./GXDEFFECT/GXDEffect.npk", clé {1,4,4,1})
//   -> Npk_FindEntryByName("ShaderNN.fx") -> Npk_GetEntrySize -> operator new
//   -> Npk_ReadEntryData (déchiffre XTEA + décompresse zlib -> source HLSL en clair)
//   -> D3DXCompileShader(src, len, NULL, NULL, "Main", "vs_2_0"|"ps_2_0", 0,
//                        &ppShader, &ppErr, &slot.ct)
//   -> GetBufferPointer -> CreateVertexShader (vtbl+364) / CreatePixelShader (vtbl+424)
//   -> Release blob -> pour chaque uniforme  GetConstantByName (vtbl+36)
//                   -> pour chaque texture   GetConstantDesc  (vtbl+24)
//                      (RegisterIndex = index de sampler au rendu)
//   -> SetDefaults(device) (vtbl+44). Échec => device non créé.
//
// Contrairement à l'original (qui rouvre le .npk 12 fois), on ouvre l'archive
// une seule fois et on lit chaque entrée : comportement fonctionnel identique.
//
// Utilise le DirectX SDK (June 2010) : ID3DXConstantTable / D3DXCompileShader.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace ts2::asset { class NpkArchive; }

namespace ts2::gfx {

// Identifiants des 12 shaders, dans l'ordre de chargement de GXD_DeviceCreate.
// Les numéros 10, 11, 13 sont absents de la séquence (non chargés par le client).
enum class GxdShaderId : int {
    VS01_WorldVP = 0,   // Shader01.fx vs — 0x409730 — mWorldViewProjMatrix, mLightAmbient
    PS02_Tex,           // Shader02.fx ps — 0x4098F0 — mTexture0
    VS03_SkinnedLit,    // Shader03.fx vs — 0x409AB0 — mKeyMatrix + WVP + dir/amb/diff
    PS04_Tex,           // Shader04.fx ps — 0x409CC0 — mTexture0
    VS05_Skinned,       // Shader05.fx vs — 0x409E80 — mKeyMatrix + WVP + mLightDirection
    PS06_MultiTex,      // Shader06.fx ps — 0x40A060 — tex0/tex1 + amb/diff
    VS07_SkinnedEye,    // Shader07.fx vs — 0x40A290 — mKeyMatrix + WVP + dir + mCameraEye
    PS08_MultiTex3,     // Shader08.fx ps — 0x40A490 — tex0/tex1/tex2 + amb/diff
    VS09_Skinned,       // Shader09.fx vs — 0x40A700 — mKeyMatrix + WVP
    PS12_PostBlur,      // Shader12.fx ps — 0x40A8E0 — mTexture0 + mTexture0PostSize
    PS14_PostBlur,      // Shader14.fx ps — 0x40AAD0 — mTexture0 + mTexture0PostSize
    VS15_WorldVP,       // Shader15.fx vs — 0x40ACB0 — mWorldViewProjMatrix
    kCount
};

// Un shader compilé + sa table de constantes résolue.
// Reproduit un « slot » de shader de l'Object B (base+0x80A28, cf. §2.2), en
// remplaçant le layout d'octets figé par des tables nommées équivalentes :
//   - `handles`  : D3DXHANDLE par nom d'uniforme (matrices / vecteurs).
//   - `samplers` : RegisterIndex (index de sampler) par nom de texture.
struct GxdShader {
    bool                     isPixel = false;
    ID3DXConstantTable*      ct = nullptr;   // slot+0 (out de D3DXCompileShader)
    IDirect3DVertexShader9*  vs = nullptr;   // slot+4 si !isPixel
    IDirect3DPixelShader9*   ps = nullptr;   // slot+4 si isPixel

    std::unordered_map<std::string, D3DXHANDLE> handles;  // matrices/vecteurs
    std::unordered_map<std::string, UINT>       samplers; // RegisterIndex textures

    // Handle nommé (nullptr si absent).
    D3DXHANDLE Handle(const char* name) const {
        auto it = handles.find(name);
        return it == handles.end() ? nullptr : it->second;
    }
    // Index de sampler d'une texture (-1 si absent).
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
    // Stride du vertex skinné (déclaration unk_814A58 : 76 octets).
    static constexpr UINT kSkinnedVertexStride = 76;

    // Compile les 12 shaders depuis une archive GXDEffect.npk déjà ouverte,
    // puis crée la déclaration de vertex skinné. `device` = device D3D9 partagé.
    // Renvoie false (et libère tout) si un seul shader échoue — fidèle à l'original
    // où un échec de compilation empêche la création du device.
    bool Load(IDirect3DDevice9* device, asset::NpkArchive& npk);

    // Variante : ouvre elle-même l'archive (clé XTEA {1,4,4,1}).
    bool LoadFromFile(IDirect3DDevice9* device,
                      const std::string& npkPath = "./GXDEFFECT/GXDEffect.npk");

    void Release();
    bool Ready() const { return ready_; }

    const GxdShader& Get(GxdShaderId id) const {
        return shaders_[static_cast<int>(id)];
    }

    // Déclaration de vertex skinné 76 o (POSITION/BLENDWEIGHT/BLENDINDICES/
    // TANGENT/BINORMAL/NORMAL/TEXCOORD). Créée depuis unk_814A58.
    IDirect3DVertexDeclaration9* SkinnedVertexDecl() const { return vertexDecl_; }

    // Lie le shader courant (SetVertexShader / SetPixelShader).
    void Bind(GxdShaderId id) const;

    // Fixe une texture sur le sampler résolu du shader (SetTexture au bon stage).
    // false si la texture n'est pas un sampler de ce shader.
    bool BindTexture(GxdShaderId id, const char* name,
                     IDirect3DBaseTexture9* tex) const;

    // Helpers de constantes (délèguent à ID3DXConstantTable via le handle nommé).
    bool SetMatrix(GxdShaderId id, const char* name, const D3DXMATRIX& m) const;
    bool SetMatrixArray(GxdShaderId id, const char* name,
                        const D3DXMATRIX* m, UINT count) const;  // ex. mKeyMatrix
    bool SetVector(GxdShaderId id, const char* name, const D3DXVECTOR4& v) const;
    bool SetFloatArray(GxdShaderId id, const char* name,
                       const float* f, UINT count) const;        // ex. mTexture0PostSize

private:
    bool CreateVertexDecl();

    IDirect3DDevice9*            device_     = nullptr;
    GxdShader                    shaders_[kNumShaders];
    IDirect3DVertexDeclaration9* vertexDecl_ = nullptr;
    bool                         ready_      = false;
};

} // namespace ts2::gfx

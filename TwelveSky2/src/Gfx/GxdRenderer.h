// Gfx/GxdRenderer.h — singleton renderer du moteur GXD (g_GxdRenderer @ 0x18C4EF8).
// Empile, au-dessus de ts2::gfx::Renderer, les états « haut niveau » du device :
// matrices projection/vue/monde, matériau, lumière directionnelle, viewport et états
// d'échantillonnage de textures.
//
// Fidèle au désassemblage (voir Docs/TS2_GXD_ENGINE.md) :
//   GXD_DeviceReinit        0x4023F0  -> DeviceReinit
//   GXD_BeginScene          0x404640  -> SetupFrame
//   GXD_ConfigSamplerStates 0x403B50  -> ConfigSamplerStates
//   GXD_SetDirectionalLight 0x403980  -> SetDirectionalLight
//   GXD_WorldToScreen       0x405C00  -> WorldToScreen
//
// La struct d'origine démarre à 0x18C4EF8 ; les commentaires « (+N) » donnent l'offset
// du champ correspondant dans cette struct.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>

namespace ts2::gfx {

class GxdRenderer {
public:
    // Modes d'ambiance de SetDirectionalLight (param edi de GXD_SetDirectionalLight).
    enum LightMode : int {
        kLightKeepAmbient = 0, // ambiant laissé à 0
        kLightAutoAmbient = 1, // ambiant = diffuse*0.5 + ambient de la lumière de base
        kLightUserAmbient = 2, // ambiant fourni par l'appelant
    };

    // Accès au singleton (l'original est le global g_GxdRenderer @ 0x18C4EF8).
    static GxdRenderer& Instance();

    // (GXD_DeviceReinit) Rebranche le singleton sur le device D3D9 déjà créé par
    // g_GfxRenderer, récupère les caps, valide vs_2_0/ps_2_0 et (re)construit les
    // matrices proj/vue/monde, le viewport, le matériau et la lumière par défaut.
    // Renvoie false + code d'erreur (1..4) si les caps manquent.
    // NB : le chargement des 12 shaders + GXDCompress.dll (codes 5..12 d'origine) relève
    //      du composant shaders et n'est pas traité ici.
    bool DeviceReinit(IDirect3D9* d3d, IDirect3DDevice9* device,
                      int width, int height, float nearZ, float farZ,
                      int* outError = nullptr);

    void Shutdown();

    // (GXD_BeginScene) Prépare la frame : Clear(cible+z+stencil), recalcul de la vue,
    // viewport, SetTransform proj/vue/monde, SetMaterial, LightEnable+SetLight, états
    // d'échantillonnage, dithering, BeginScene, puis remise du pipeline à fonction fixe.
    bool SetupFrame();

    // (GXD_ConfigSamplerStates) Filtres min/mag/mip (anisotrope par défaut, linéaire si
    // m_useLinearFilter) + adressage WRAP (U,V) sur les étages 0..2.
    void ConfigSamplerStates();

    // (GXD_SetDirectionalLight) Remplit un D3DLIGHT9 directionnel local (diffuse/specular
    // nuls, ambiant selon le mode, direction (-1,-1,1)) puis LightEnable(0)+SetLight(0).
    void SetDirectionalLight(int mode, float ar = 0.0f, float ag = 0.0f,
                             float ab = 0.0f, float aa = 1.0f);

    // (GXD_WorldToScreen) Projette un point monde en pixel écran arrondi (round half-up).
    // Renvoie false si le point est hors frustum.
    bool WorldToScreen(const D3DXVECTOR3& world, int& sx, int& sy) const;

    // Position caméra (oeil/cible) — la vue est reconstruite chaque frame dans SetupFrame.
    void SetCamera(const D3DXVECTOR3& eye, const D3DXVECTOR3& at) { m_eye = eye; m_at = at; }

    IDirect3DDevice9*   Device()   const { return m_device; }
    const D3DXMATRIX&   Proj()     const { return m_matProj; }
    const D3DXMATRIX&   View()     const { return m_matView; }
    const D3DXMATRIX&   World()    const { return m_matWorld; }
    const D3DMATERIAL9& Material() const { return m_material; }
    const D3DLIGHT9&    Light()    const { return m_light; }

    void SetUseLinearFilter(bool v) { m_useLinearFilter = v; } // (+8, dword_18C4F00)
    bool AnisotropyCapable() const { return m_anisoCapable; }
    bool TwoSidedStencil()   const { return m_twoSidedStencil; }

private:
    void BuildMatrices();                // proj / vue / monde / demi-viewport
    void BuildDefaultMaterialAndLight(); // matériau + lumière directionnelle
    void BuildFrustumPlanes();           // Frustum_BuildPlanes 0x406090
    bool FrustumContains(const D3DXVECTOR3& world) const; // Frustum_ContainsPoint5 0x406560

    IDirect3D9*       m_d3d      = nullptr;
    IDirect3DDevice9* m_device   = nullptr;

    int   m_width  = 0;      // (+48 ; viewport +568)
    int   m_height = 0;      // (+52 ; viewport +572)
    float m_fovDeg = 45.0f;  // (+56) informatif ; la projection utilise PI/4 rad
    float m_nearZ  = 0.0f;   // (+60)
    float m_farZ   = 0.0f;   // (+64)

    D3DVIEWPORT9 m_viewport{};        // (+560) X,Y,W,H,MinZ=0,MaxZ=1
    D3DXMATRIX   m_matHalfViewport;   // (+584) NDC [-1,1] -> pixels
    D3DXMATRIX   m_matProj;           // (+648) D3DXMatrixPerspectiveFovLH
    D3DXVECTOR3  m_eye{0.0f, 0.0f, -10.0f}; // (+712)
    D3DXVECTOR3  m_at{0.0f, 0.0f, 0.0f};     // (+724)
    D3DXVECTOR3  m_viewDir{0.0f, 0.0f, 0.0f};// (+736) normalize(at-eye)
    D3DXMATRIX   m_matView;           // (+748) D3DXMatrixLookAtLH
    D3DXMATRIX   m_matWorld;          // (+988) identité
    D3DMATERIAL9 m_material{};        // (+1052)
    D3DLIGHT9    m_light{};           // (+1120)

    D3DCAPS9 m_caps{};                // (+164)
    DWORD    m_maxAnisotropy = 1;     // caps.MaxAnisotropy (g_GxdMaxAnisotropy @ +272)
    int      m_currentShaderId = 0;   // (+526884) programme courant, remis à 0 par frame

    bool m_useLinearFilter = false;  // (+8) 0 => anisotrope (défaut), !=0 => linéaire
    bool m_anisoCapable    = false;  // (+28) D3DPRASTERCAPS_ANISOTROPY
    bool m_twoSidedStencil = false;  // (+32) D3DSTENCILCAPS_TWOSIDED

    // Plans de frustum (Frustum_BuildPlanes 0x406090, this+309..332 dans l'original,
    // soit 6 plans de 4 floats extraits de la matrice combinée vue*projection par la
    // méthode Gribb/Hartmann, convention D3D profondeur [0,1]) :
    //   [0]=gauche(row4+row1) [1]=droite(row4-row1) [2]=bas(row4+row2) [3]=haut(row4-row2)
    //   [4]=proche(row3 seule, sans row4) [5]=lointain(row4-row3)
    // Chaque plan est stocké (a,b,c,d) tel que a*x+b*y+c*z+d >= 0 signifie "à l'intérieur".
    // Frustum_ContainsPoint5 (0x406560, utilisé par WorldToScreen) ne teste que les 5
    // premiers plans (gauche/droite/bas/haut/proche) ; le plan lointain [5] est calculé
    // mais non testé par cette fonction d'origine (fidèle au désassemblage).
    D3DXPLANE m_frustumPlanes[6]{};
};

} // namespace ts2::gfx

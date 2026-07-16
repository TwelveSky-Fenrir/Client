// Gfx/GxdRenderer.cpp — implémentation du singleton renderer GXD.
// Reconstruit fidèlement les états D3D9 posés par le renderer d'origine (0x18C4EF8).
#include "Gfx/GxdRenderer.h"
#include <cmath>

#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

namespace ts2::gfx {

GxdRenderer& GxdRenderer::Instance() {
    static GxdRenderer s_instance; // équivalent du global g_GxdRenderer @ 0x18C4EF8
                                   // ex-VeryOldClient: TW2AddIn::GXD (v2 / Object B)
    return s_instance;
}

void GxdRenderer::Shutdown() {
    m_d3d = nullptr;
    m_device = nullptr;
    m_width = 0;
    m_height = 0;
    m_fovDeg = 45.0f;
    m_nearZ = 0.0f;
    m_farZ = 0.0f;
    m_currentShaderId = 0;
    m_useLinearFilter = false;
    m_depthBiasCapable = false;
    m_twoSidedStencil = false;
}

// ---------------------------------------------------------------------------
// Construction des matrices (partie de GXD_DeviceReinit 0x4023F0).
// ---------------------------------------------------------------------------
void GxdRenderer::BuildMatrices() {
    // Viewport plein écran, profondeur 0..1. (+560)
    m_viewport.X      = 0;
    m_viewport.Y      = 0;
    m_viewport.Width  = static_cast<DWORD>(m_width);
    m_viewport.Height = static_cast<DWORD>(m_height);
    m_viewport.MinZ   = 0.0f;
    m_viewport.MaxZ   = 1.0f;

    // Matrice « demi-viewport » : NDC [-1,1] -> pixels [0,w]x[0,h], Y inversé. (+584)
    //   [ w/2    0    0   0 ]
    //   [  0  -h/2    0   0 ]
    //   [  0    0    1   0 ]
    //   [ w/2  h/2    0   1 ]
    const float hw = static_cast<float>(m_width)  * 0.5f;
    const float hh = static_cast<float>(m_height) * 0.5f;
    D3DXMatrixIdentity(&m_matHalfViewport);
    m_matHalfViewport._11 = hw;
    m_matHalfViewport._22 = -hh;
    m_matHalfViewport._33 = 1.0f;
    m_matHalfViewport._41 = hw;
    m_matHalfViewport._42 = hh;

    // Projection perspective LH : FOV 45° (PI/4 rad, cf. 0.78539819), aspect = w/h. (+648)
    D3DXMatrixPerspectiveFovLH(&m_matProj, D3DX_PI / 4.0f,
                               static_cast<float>(m_width) / static_cast<float>(m_height),
                               m_nearZ, m_farZ);

    // Vue LookAtLH : oeil (0,0,-10), cible (0,0,0), up (0,1,0). (+712/+724 -> +748)
    m_eye = D3DXVECTOR3(0.0f, 0.0f, -10.0f);
    m_at  = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    D3DXVECTOR3 up(0.0f, 1.0f, 0.0f);
    D3DXMatrixLookAtLH(&m_matView, &m_eye, &m_at, &up);

    // Monde = identité. (+988)
    D3DXMatrixIdentity(&m_matWorld);
}

// ---------------------------------------------------------------------------
// Matériau + lumière directionnelle par défaut (partie de GXD_DeviceReinit).
// ---------------------------------------------------------------------------
void GxdRenderer::BuildDefaultMaterialAndLight() {
    // ex-VeryOldClient: mMaterial (+1052) + mLight (+1120). Garde-fou v1/v2 : la lumière est
    // Amb 0.3 / Diff 0.7 (v2 / Object B, PROUVÉ BIT-EXACT à 0x402711), PAS 0.4/0.5 (v1 / Object A).
    // Matériau : diffuse/ambient blancs opaques, specular/emissive noirs, power 0. (+1052)
    ZeroMemory(&m_material, sizeof(m_material));
    m_material.Diffuse  = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
    m_material.Ambient  = D3DXCOLOR(1.0f, 1.0f, 1.0f, 1.0f);
    m_material.Specular = D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f);
    m_material.Emissive = D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f);
    m_material.Power    = 0.0f;

    // Lumière directionnelle par défaut. (+1120)
    ZeroMemory(&m_light, sizeof(m_light));
    m_light.Type      = D3DLIGHT_DIRECTIONAL;                // 3
    m_light.Diffuse   = D3DXCOLOR(0.7f, 0.7f, 0.7f, 1.0f);
    m_light.Specular  = D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f);
    m_light.Ambient   = D3DXCOLOR(0.3f, 0.3f, 0.3f, 1.0f);
    m_light.Position  = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    m_light.Direction = D3DXVECTOR3(-1.0f, -1.0f, 1.0f);
    // La lumière de base est normalisée (sub_6BB60C / Vec3_Normalize).
    D3DXVECTOR3 dir(m_light.Direction.x, m_light.Direction.y, m_light.Direction.z);
    D3DXVec3Normalize(&dir, &dir);
    m_light.Direction = dir;
    // Range/Falloff/Attenuation/Theta/Phi restent à 0.
}

// ---------------------------------------------------------------------------
// GXD_DeviceReinit 0x4023F0
// ex-VeryOldClient: TW2AddIn::GXD::InitForAddIn(sx,sy,near,far,d3d,dev,res) (GXDCore.cpp:523)
//   — reuses_device=true : reçoit un IDirect3D9*+IDirect3DDevice9* DÉJÀ créés par Object A.
// ---------------------------------------------------------------------------
bool GxdRenderer::DeviceReinit(IDirect3D9* d3d, IDirect3DDevice9* device,
                               int width, int height, float nearZ, float farZ,
                               int* outError) {
    if (outError) *outError = 0;
    m_d3d      = d3d;
    m_device   = device;
    m_width    = width;
    m_height   = height;
    m_fovDeg   = 45.0f;
    m_nearZ    = nearZ;
    m_farZ     = farZ;

    if (!m_d3d || !m_device) { if (outError) *outError = 1; return false; }

    // GetDeviceCaps -> remplit le D3DCAPS9 embarqué (+164) ; MaxAnisotropy = g_GxdMaxAnisotropy (+272).
    if (FAILED(m_device->GetDeviceCaps(&m_caps))) { if (outError) *outError = 2; return false; }

    // Exige vs_2_0 (>= 0xFFFE0200) et ps_2_0 (>= 0xFFFF0200).
    if (m_caps.VertexShaderVersion < 0xFFFE0200u) { if (outError) *outError = 3; return false; }
    if (m_caps.PixelShaderVersion  < 0xFFFF0200u) { if (outError) *outError = 4; return false; }

    // GXD_DeviceReinit 0x4023F0 @0x402928 : this+28 = (RasterCaps & 0x4000000) != 0.
    // ex-VeryOldClient: mCheckDepthBias. 0x4000000 = D3DPRASTERCAPS_DEPTHBIAS (PAS ANISOTROPY 0x20000).
    m_depthBiasCapable = (m_caps.RasterCaps & D3DPRASTERCAPS_DEPTHBIAS) != 0; // (+28)
    m_twoSidedStencil = (m_caps.StencilCaps & D3DSTENCILCAPS_TWOSIDED)   != 0; // (+32)
    m_maxAnisotropy   = m_caps.MaxAnisotropy;

    BuildMatrices();
    BuildDefaultMaterialAndLight();
    return true;
}

// ---------------------------------------------------------------------------
// GXD_BeginScene 0x404640
// ---------------------------------------------------------------------------
bool GxdRenderer::SetupFrame() {
    if (!m_device) return false;
    IDirect3DDevice9* dev = m_device;

    // 1) Efface cible + z-buffer + stencil (Flags = 7), couleur 0, Z = 1.0.
    dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);

    // 2) Direction de vue puis matrice vue LookAtLH (recalculées chaque frame). (+736/+748)
    m_viewDir = m_at - m_eye;
    D3DXVec3Normalize(&m_viewDir, &m_viewDir);
    D3DXVECTOR3 up(0.0f, 1.0f, 0.0f);
    D3DXMatrixLookAtLH(&m_matView, &m_eye, &m_at, &up);

    // 3) Monde ré-initialisé à l'identité. (+988)
    D3DXMatrixIdentity(&m_matWorld);

    // 4) Viewport + transforms + matériau + lumière.
    dev->SetViewport(&m_viewport);
    dev->SetTransform(D3DTS_PROJECTION, &m_matProj);   // état 3
    dev->SetTransform(D3DTS_VIEW,       &m_matView);   // état 2
    dev->SetTransform(D3DTS_WORLD,      &m_matWorld);  // état 256
    dev->SetMaterial(&m_material);
    dev->LightEnable(0, TRUE);
    dev->SetLight(0, &m_light);

    // 5) États d'échantillonnage + dithering.
    ConfigSamplerStates();
    dev->SetRenderState(D3DRS_DITHERENABLE, TRUE);     // état 26

    // 6) Frustum_BuildPlanes 0x406090 (culling). Les matrices inverses de vue calculées
    //    juste après dans GXD_BeginScene (+876/+888/+964/+976, base/up vecteurs billboard)
    //    ne sont pas reprises ici : elles alimentent le rendu de sprites/particules
    //    billboardés, hors périmètre de GxdRenderer (cf. SpriteBatch/PtclDef côté GXD).
    BuildFrustumPlanes();

    // 7) Ouverture de scène puis remise du pipeline à fonction fixe.
    HRESULT hr = dev->BeginScene();
    m_currentShaderId = 0;            // (+526884)
    dev->SetVertexShader(nullptr);    // vtable 92
    dev->SetPixelShader(nullptr);     // vtable 107
    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------------
// GXD_ConfigSamplerStates 0x403B50
// ex-VeryOldClient: TW2AddIn::GXD::SetDefaultTextureSamplerState
//   (ANISOTROPIC vs LINEAR selon mSamplerOptionValue = champ +8 / m_useLinearFilter).
// ---------------------------------------------------------------------------
void GxdRenderer::ConfigSamplerStates() {
    if (!m_device) return;
    IDirect3DDevice9* dev = m_device;

    if (m_useLinearFilter) {
        // Branche « this[2] != 0 » : filtrage bilinéaire (LINEAR) sur les étages 0..2.
        for (DWORD s = 0; s <= 2; ++s) {
            dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        }
    } else {
        // Branche « this[2] == 0 » (défaut) : filtrage anisotrope + facteur max.
        for (DWORD s = 0; s <= 2; ++s) {
            dev->SetSamplerState(s, D3DSAMP_MINFILTER,     D3DTEXF_ANISOTROPIC);
            dev->SetSamplerState(s, D3DSAMP_MAGFILTER,     D3DTEXF_ANISOTROPIC);
            dev->SetSamplerState(s, D3DSAMP_MIPFILTER,     D3DTEXF_ANISOTROPIC);
            dev->SetSamplerState(s, D3DSAMP_MAXANISOTROPY, m_maxAnisotropy);
        }
    }

    // Adressage WRAP (U,V) sur les 3 étages (appliqué dans les deux branches).
    for (DWORD s = 0; s <= 2; ++s) {
        dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }
}

// ---------------------------------------------------------------------------
// GXD_SetDirectionalLight 0x403980
// ---------------------------------------------------------------------------
void GxdRenderer::SetDirectionalLight(int mode, float ar, float ag, float ab, float aa) {
    if (!m_device) return;
    IDirect3DDevice9* dev = m_device;

    dev->LightEnable(0, TRUE); // activé en tête de fonction

    D3DLIGHT9 light;
    ZeroMemory(&light, sizeof(light));
    light.Type     = D3DLIGHT_DIRECTIONAL;              // 3
    light.Diffuse  = D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f);// n'apporte pas de diffus
    light.Specular = D3DXCOLOR(0.0f, 0.0f, 0.0f, 1.0f);

    if (mode == kLightAutoAmbient) {
        // Ambiant = diffuse*0.5 + ambient de la lumière directionnelle de base.
        light.Ambient.r = m_light.Diffuse.r * 0.5f + m_light.Ambient.r;
        light.Ambient.g = m_light.Diffuse.g * 0.5f + m_light.Ambient.g;
        light.Ambient.b = m_light.Diffuse.b * 0.5f + m_light.Ambient.b;
        light.Ambient.a = 1.0f;
    } else if (mode == kLightUserAmbient) {
        light.Ambient = D3DXCOLOR(ar, ag, ab, aa);
    }
    // kLightKeepAmbient : ambiant reste (0,0,0,0).

    light.Position  = D3DXVECTOR3(0.0f, 0.0f, 0.0f);
    light.Direction = D3DXVECTOR3(-1.0f, -1.0f, 1.0f); // non normalisée (fidèle à l'original)

    dev->SetLight(0, &light);
}

// ---------------------------------------------------------------------------
// GXD_WorldToScreen 0x405C00
// ---------------------------------------------------------------------------
bool GxdRenderer::WorldToScreen(const D3DXVECTOR3& world, int& sx, int& sy) const {
    // Rejette les points hors frustum (Frustum_ContainsPoint5 0x406560).
    if (!FrustumContains(world)) return false;

    // Combine vue * projection * demi-viewport, puis projette le point.
    D3DXMATRIX vp, vph;
    D3DXMatrixMultiply(&vp,  &m_matView, &m_matProj);          // sub_6BB618
    D3DXMatrixMultiply(&vph, &vp,        &m_matHalfViewport);  // sub_6BB618
    D3DXVECTOR3 p;
    D3DXVec3TransformCoord(&p, &world, &vph);                  // sub_6BB612

    // Arrondi au pixel le plus proche : troncature vers 0 (Crt_ftol) + demi-unité.
    // NB : le désassemblage est symétrique en X et Y (l'artefact « ftol(0.5) » du
    //      décompilateur vient d'un 0.5 résiduel sur la pile x87 hérité du calcul X).
    int ix = static_cast<int>(p.x);
    if (p.x - static_cast<float>(ix) >= 0.5f) ++ix;
    int iy = static_cast<int>(p.y);
    if (p.y - static_cast<float>(iy) >= 0.5f) ++iy;
    sx = ix;
    sy = iy;
    return true;
}

// ---------------------------------------------------------------------------
// Frustum_BuildPlanes 0x406090 : extrait 6 plans (Gribb/Hartmann) de la matrice
// combinée vue*projection (m_matView * m_matProj, même produit que WorldToScreen).
// Chaque plan est normalisé (division par la norme du vecteur (a,b,c)), fidèle à
// l'original qui appelle Math_CIsqrt puis 1/x plutôt qu'un rsqrt direct.
// ---------------------------------------------------------------------------
void GxdRenderer::BuildFrustumPlanes() {
    D3DXMATRIX vp;
    D3DXMatrixMultiply(&vp, &m_matView, &m_matProj); // sub_6BB618, Out = M1 * M2

    auto normalize = [](float a, float b, float c, float d, D3DXPLANE& out) {
        float invLen = 1.0f / sqrtf(a * a + b * b + c * c);
        out.a = a * invLen;
        out.b = b * invLen;
        out.c = c * invLen;
        out.d = d * invLen;
    };

    // Lignes de la matrice (row-major D3DX, v' = v * M) : row1=(_11,_21,_31,_41) ?
    // Non : ici "row N" désigne la Nᵉ RANGÉE de la matrice, _N1.._N4.
    const float m11 = vp._11, m12 = vp._12, m13 = vp._13, m14 = vp._14;
    const float m21 = vp._21, m22 = vp._22, m23 = vp._23, m24 = vp._24;
    const float m31 = vp._31, m32 = vp._32, m33 = vp._33, m34 = vp._34;
    const float m41 = vp._41, m42 = vp._42, m43 = vp._43, m44 = vp._44;

    // [0] Gauche  = row4 + row1
    normalize(m14 + m11, m24 + m21, m34 + m31, m44 + m41, m_frustumPlanes[0]);
    // [1] Droite  = row4 - row1
    normalize(m14 - m11, m24 - m21, m34 - m31, m44 - m41, m_frustumPlanes[1]);
    // [2] Bas     = row4 + row2
    normalize(m14 + m12, m24 + m22, m34 + m32, m44 + m42, m_frustumPlanes[2]);
    // [3] Haut    = row4 - row2
    normalize(m14 - m12, m24 - m22, m34 - m32, m44 - m42, m_frustumPlanes[3]);
    // [4] Proche  = row3 seule (convention D3D, profondeur [0,1])
    normalize(m13, m23, m33, m43, m_frustumPlanes[4]);
    // [5] Lointain = row4 - row3
    normalize(m14 - m13, m24 - m23, m34 - m33, m44 - m43, m_frustumPlanes[5]);
}

// Frustum_ContainsPoint5 0x406560 : teste les 5 premiers plans (gauche/droite/bas/
// haut/proche) ; le plan lointain n'est volontairement pas testé (fidèle à l'original,
// seule fonction appelée par GXD_WorldToScreen 0x405C00).
bool GxdRenderer::FrustumContains(const D3DXVECTOR3& world) const {
    for (int i = 0; i < 5; ++i) {
        const D3DXPLANE& p = m_frustumPlanes[i];
        if (p.a * world.x + p.b * world.y + p.c * world.z + p.d < 0.0f) return false;
    }
    return true;
}

} // namespace ts2::gfx

// Gfx/GxdRenderer.cpp — implémentation du singleton renderer GXD.
// Reconstruit fidèlement les états D3D9 posés par le renderer d'origine (0x18C4EF8).
#include "Gfx/GxdRenderer.h"
#include "Gfx/ShaderSet.h" // slots PS12/PS14 du npk pour RenderPostBlur — lecture seule (W9)
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
    // Libère les 4 cibles de rendu du bloom (D3DPOOL_DEFAULT) : sans ça, Shutdown fuirait
    // 4 objets COM. Même ordre que GXD_OnDeviceLost (+548, +540, +552, +544).
    ReleaseBlurTargets();

    m_d3d = nullptr;
    m_device = nullptr;
    m_width = 0;
    m_height = 0;
    m_fovDeg = 45.0f;
    m_nearZ = 0.0f;
    m_farZ = 0.0f;
    m_currentShaderId = 0;
    m_useLinearFilter = false;
    // m_depthBiasCapable / m_twoSidedStencil : PAS de remise à false (gap G3, W9).
    // GXD_FreeGlobalState 0x401530 (décompilée intégralement) ne touche NI +28 NI +32 ; ces
    // champs sont figés à 1 par GXD_InitGlobalState @0x4013B2/@0x4013B8 et seulement
    // VERROUILLÉS (jamais effacés) par GXD_DeviceReinit @0x402928/@0x402939.
    // Cf. le pavé de GxdRenderer.h au-dessus de leur déclaration.
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

    // GXD_DeviceReinit 0x4023F0 @0x402928/@0x402939 — VERROUILLAGE À 1, PAS AFFECTATION
    // (gap G3, corrigé Passe 4 / W9). Désassemblage relu, edi = 1 :
    //     test dword ptr [esi+0C8h], 4000000h    ; caps+36  = RasterCaps
    //     jz   short loc_40292D                  ; @0x402928  <- AUCUN else
    //     mov  [esi+1Ch], edi                    ; @0x40292A  this+28 = 1
    //     test dword ptr [esi+12Ch], 100h        ; caps+136 = StencilCaps
    //     jz   short loc_40293C                  ; @0x402937  <- AUCUN else
    //     mov  [esi+20h], edi                    ; @0x402939  this+32 = 1
    // Combiné à l'init à 1 de GXD_InitGlobalState (@0x4013B2/@0x4013B8), ces champs valent
    // TOUJOURS 1 quelles que soient les caps GPU. L'ancien code (`= (caps & bit) != 0`)
    // produisait `false` là où le binaire garantit 1 sur un GPU dépourvu de la cap.
    // Cf. le pavé de GxdRenderer.h (double identité +28 = g_ShadowsEnabled / +32 = g_ShadowMethod).
    if ((m_caps.RasterCaps  & D3DPRASTERCAPS_DEPTHBIAS) != 0) m_depthBiasCapable = true; // (+28)
    if ((m_caps.StencilCaps & D3DSTENCILCAPS_TWOSIDED)  != 0) m_twoSidedStencil  = true; // (+32)
    m_maxAnisotropy   = m_caps.MaxAnisotropy;

    // (+527548) = 0 @0x402451 : un device fraîchement (re)branché n'est plus « perdu ».
    m_deviceLost = false;

    BuildMatrices();
    BuildDefaultMaterialAndLight();
    return true;
}

// ---------------------------------------------------------------------------
// GXD_BeginScene 0x404640
//
// SANS APPELANT — ET C'EST FIDÈLE, PAS UN GAP (statué Passe 4 / W9, gap G4 réfuté).
//
// `xrefs_to(0x404640)` = 0 : GXD_BeginScene est MORTE DANS LE BINAIRE AUSSI. Une SetupFrame()
// sans appelant est donc la transposition EXACTE, et non une lacune de câblage à combler.
// Le chemin runtime réel est App.cpp (boucle de frame) -> Renderer::BeginFrame() ->
// SceneManager::Render(), qui reproduit Gfx_BeginFrame 0x6A2280 (Object A : Clear +
// BeginScene) et appelle GXD_ConfigSamplerStates 0x403B50 à la bonne position — exactement
// comme les 6 sites d'appel des Scene_*Render (le 7e, 0x4047C7, est ici, dans cette fonction
// morte). Câbler SetupFrame() produirait en prime un DOUBLON de Clear/BeginScene avec
// Renderer::BeginFrame (BeginScene imbriqué => D3DERR_INVALIDCALL).
//
// NE PAS « corriger » le recalcul de vue ci-dessous : le gap G4 réclamait sa suppression au
// motif que « l'original ne refait pas de LookAt par frame ». C'est FAUX — GXD_BeginScene le
// fait, ligne pour ligne : v7 = *(a1+724) - *(a1+712) @0x404692 ; Vec3_Normalize(a1+736)
// @0x4046C0 ; up = (0,1,0) ; j_D3DXMatrixLookAtLH(a1+748, a1+712, a1+724, &up) @0x4046E3.
// Ce que le gap avait vu (les qmemcpy Object A -> Object B des Scene_*Render, @0x5188DC/ED/F5)
// est un chemin DIFFÉRENT et PARALLÈLE, qui écrit des champs DISJOINTS (+748/+988/+736..744
// et non +712/+724) : les deux coexistent. Il est modélisé par SetViewMatrix/SetWorldMatrix/
// SetViewDir (cf. GxdRenderer.h), qui s'ajoutent à SetCamera sans le remplacer.
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
//
// CÂBLAGE (gap GX-DEV-02) : appelée PAR FRAME depuis ts2::gfx::Renderer::BeginFrame(),
// juste après Clear+BeginScene — position fidèle aux 6 sites d'appel des Scene_*Render
// (0x518916, 0x5192F6, 0x51B0C6, 0x51CF76, 0x52C306, 0x52D24A), qui appellent tous
// GXD_ConfigSamplerStates(&g_GxdRenderer) juste après Gfx_BeginFrame(g_GfxRenderer).
// Elle est ATTEINTE par le chemin runtime réel App.cpp -> Renderer::BeginFrame().
// (Auparavant son seul appelant C++ était SetupFrame(), elle-même sans appelant : le
//  filtrage anisotrope n'était donc JAMAIS actif et le device restait sur le LINEAR posé
//  à l'init par Object A / Gfx_InitDevice 0x69C470.)
//
// La branche exécutée est l'ANISOTROPE : le champ +8 (dword_18C4F00) n'a qu'un seul
// writer absolu dans le binaire, GXD_InitGlobalState 0x40139C, qui le met à 0 — la branche
// linéaire (+8 != 0) est morte en pratique mais reproduite ici par fidélité.
//
// ÉCART ASSUMÉ (bénin, état final identique) : le binaire ordonne les SetSamplerState par
// étage (avec une queue commune stage 2 ADDRESSU/V @0x403E1B/0x403E34), ici on fait deux
// boucles s=0..2. Chaque couple (étage, type) est écrit UNE SEULE FOIS avec la MÊME valeur
// dans les deux formes : l'état résultant du device est strictement identique.
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
// Camera_SetEyeTarget 0x403420 — pousse oeil (+712) / cible (+724) SOUS CONDITIONS.
// Décompilation relue intégralement ; (a2,a3,a4) = oeil, (a5,a6,a7) = cible :
//     if ( a5 == a2 && a6 == a3 && a7 == a4 ) return 0;          /*0x403465*/
//     v17 = a2-a5 ; v18 = a3-a6 ; v19 = a4-a7 ;                  /*0x403483..0x403491*/
//     v14 = fabs(v18) / Crt_sqrtf(v17*v17 + v18*v18 + v19*v19) ; /*0x4034a7..0x4034d6*/
//     Math_AsinFpu(v14) ; v15 = v14 * 57.2957763671875 ;         /*0x4034de..0x4034f1*/
//     if ( fabs(v15) > 89.989998 ) return 0;                     /*0x40350e*/
//     *(this+178..183) = a2..a7 ; return 1;                      /*0x403522..0x403574*/
// (this+178 = octet 712 = m_eye ; this+181 = octet 724 = m_at.)
// Le rejet est un REFUS SEC : les champs gardent leur valeur précédente.
// ---------------------------------------------------------------------------
bool GxdRenderer::SetCamera(const D3DXVECTOR3& eye, const D3DXVECTOR3& at) {
    // 1) Oeil confondu avec la cible -> direction de vue indéfinie. (@0x403465)
    if (at.x == eye.x && at.y == eye.y && at.z == eye.z) return false;

    const float dx = eye.x - at.x;
    const float dy = eye.y - at.y;
    const float dz = eye.z - at.z;

    // 2) Élévation trop proche du pôle -> LookAtLH dégénère (up figé (0,1,0)). (@0x40350E)
    //    Littéraux EXACTS du binaire : 57.2957763671875 (0x42652EE1) et 89.989998.
    const float len = sqrtf(dx * dx + dy * dy + dz * dz);
    const float elevDeg = asinf(fabsf(dy) / len) * 57.2957763671875f;
    if (fabsf(elevDeg) > 89.989998f) return false;

    m_eye = eye; // (+712)
    m_at  = at;  // (+724)
    return true;
}

// ---------------------------------------------------------------------------
// Cibles de rendu du bloom — partie « création paresseuse » de GXD_RenderPostBlur
// 0x4053E0 (@0x40547A..0x405563).
//
// Fidélité des détails qui comptent :
//   - division ENTIÈRE SIGNÉE w/2 et h/2 (`cdq/sub/sar 1` @0x405464-0x40546A) : sur une
//     largeur impaire l'original perd le pixel de reste, on fait pareil.
//   - j_D3DXCreateTexture(dev, halfW, halfH, MipLevels=1, Usage=1 D3DUSAGE_RENDERTARGET,
//     Format=22 D3DFMT_X8R8G8B8, Pool=0 D3DPOOL_DEFAULT, &tex)  @0x40548F / @0x4054F8
//   - GetSurfaceLevel(0, &surf) = vtbl+72                        @0x4054AC / @0x405536
//     (`lea edx, [esi+228h]` @0x405526 : 0x228 = 552 = surfB ; 0x224 = 548 = surfA)
//   - chaque test est INDÉPENDANT (`if (!texA) {...}` puis `if (texB) goto ...`) : les deux
//     paires sont créées séparément, pas en bloc.
// ---------------------------------------------------------------------------
bool GxdRenderer::EnsureBlurTargets() {
    if (!m_device) return false;

    const int halfW = m_width  / 2; // (+48)/2, division entière signée
    const int halfH = m_height / 2; // (+52)/2
    if (halfW <= 0 || halfH <= 0) return false;

    // --- Paire A (texA @+540 -> surfA @+548) ---
    if (!m_blurTexA) {
        if (FAILED(D3DXCreateTexture(m_device, static_cast<UINT>(halfW), static_cast<UINT>(halfH),
                                     1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8,
                                     D3DPOOL_DEFAULT, &m_blurTexA))) {
            m_blurTexA = nullptr;
            return false; // `return result` @0x405496 : rien n'a encore été pris
        }
        if (FAILED(m_blurTexA->GetSurfaceLevel(0, &m_blurSurfA))) {
            // @0x4054BA : Release(texA) + texA = 0, puis retour.
            m_blurSurfA = nullptr;
            m_blurTexA->Release();
            m_blurTexA = nullptr;
            return false;
        }
    }

    // --- Paire B (texB @+544 -> surfB @+552) ---
    if (!m_blurTexB) {
        if (FAILED(D3DXCreateTexture(m_device, static_cast<UINT>(halfW), static_cast<UINT>(halfH),
                                     1, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8,
                                     D3DPOOL_DEFAULT, &m_blurTexB))) {
            // @0x405506 : Release(surfA) + surfA = 0, Release(texA) + texA = 0.
            m_blurTexB = nullptr;
            ReleaseBlurTargets();
            return false;
        }
        if (FAILED(m_blurTexB->GetSurfaceLevel(0, &m_blurSurfB))) {
            // @0x405544 : Release(surfA), surfA=0 ; Release(texA), texA=0 ; Release(texB), texB=0.
            m_blurSurfB = nullptr;
            ReleaseBlurTargets();
            return false;
        }
    }
    return true;
}

// GXD_OnDeviceLost 0x4042E0 @0x4042E3..0x404347 : ordre de libération EXACT
// +548 (surfA), +540 (texA), +552 (surfB), +544 (texB), chacun remis à 0 après Release().
void GxdRenderer::ReleaseBlurTargets() {
    if (m_blurSurfA) { m_blurSurfA->Release(); m_blurSurfA = nullptr; } // (+548) @0x4042FD
    if (m_blurTexA)  { m_blurTexA->Release();  m_blurTexA  = nullptr; } // (+540) @0x404315
    if (m_blurSurfB) { m_blurSurfB->Release(); m_blurSurfB = nullptr; } // (+552) @0x40432D
    if (m_blurTexB)  { m_blurTexB->Release();  m_blurTexB  = nullptr; } // (+544) @0x404345
}

// ---------------------------------------------------------------------------
// GXD_OnDeviceLost 0x4042E0
//
// L'original libère 24 objets D3DPOOL_DEFAULT : les 4 RT du bloom (+540/+544/+548/+552),
// la déclaration de vertex skinné (+526880) et 10 paires de slots shader
// (+526888..+527536), puis `if (*(this+6352) == 1) World_Shutdown(this+6348)` @0x40454C.
// Côté ClientSource, GxdRenderer ne possède QUE les 4 RT (cf. le pavé de RestoreAfterReset
// dans GxdRenderer.h pour la répartition des trois autres familles).
// ---------------------------------------------------------------------------
void GxdRenderer::OnDeviceLost() {
    m_deviceLost = true; // (+527548) = 1 @0x4042EC — posé AVANT toute libération
    ReleaseBlurTargets();
}

// ---------------------------------------------------------------------------
// GXD_RestoreAfterReset 0x404570
//   if ( *(this+527548) == 1 ) {           /*0x40457a*/
//       ... World_ReloadMap / CreateVertexDeclaration / 12 shaders ...
//       if ( un échec ) return 0;          /*0x4045b8*/
//       *(this+527548) = 0;                /*0x404629*/
//   }
//   return 1;                              /*0x4045b7*/
// -> renvoie true SANS RIEN FAIRE si le drapeau n'était pas posé (cas nominal).
// Les 4 RT n'ont pas à être recréées ici : RenderPostBlur les recrée paresseusement
// (`if (!*(a1+540))` @0x40547A), exactement comme l'original.
// ---------------------------------------------------------------------------
bool GxdRenderer::RestoreAfterReset() {
    if (!m_deviceLost) return true; // `return 1` @0x4045B7
    if (!m_device) return false;    // pas de device : rien à restaurer, drapeau conservé
    m_deviceLost = false;           // (+527548) = 0 @0x404629 (succès total)
    return true;
}

// ---------------------------------------------------------------------------
// GXD_RenderPostBlur 0x4053E0 — bloom en 3 temps (décompilation + désassemblage relus).
//
// CÂBLAGE — ⚠ POINT D'APPEL HORS DE CE FRONT, À POSER PAR L'ORCHESTRATEUR.
//   Site d'origine : UNIQUE et INCONDITIONNEL, en ligne droite (aucun saut entre les deux
//   bornes) dans Scene_InGameRender :
//       @0x52FB49  call Env_StepTimeOfDay
//       @0x52FB53  mov ecx, offset g_GxdRenderer ; call GXD_RenderPostBlur
//       @0x52FB89  call Gfx_Begin2D
//   Miroir C++ attendu : Scene/WorldRenderer.cpp (ou Scene/InGameScene), APRÈS tout le rendu
//   3D et AVANT le passage en 2D, sans aucune condition :
//       ts2::gfx::GxdRenderer::Instance().RenderPostBlur(shaderSet_);
//   Tant que ce n'est pas posé, cette fonction reste du code mort.
//
// La garde d'origine `cmp [ecx+18h], 1` @0x4053ED n'est PAS reproduite comme une option :
// son unique writer (@0x4013AC, `= 1`) la rend toujours vraie -> pas de paramètre d'activation.
// La branche `else` (Release des 4 RT quand la garde est fausse) est donc morte : elle est
// tout de même réalisée, ailleurs et pour une vraie raison, par OnDeviceLost().
//
// Quad : 4 sommets (x, y, z, rhw, u, v), stride 24, D3DFVF_XYZRHW|D3DFVF_TEX1 = 260,
// dessiné en TRIANGLESTRIP (2 primitives) :
//     v0 = (0, H, 0, 1, 0, 1)   v1 = (0, 0, 0, 1, 0, 0)
//     v2 = (W, H, 0, 1, 1, 1)   v3 = (W, 0, 0, 1, 1, 0)
// avec (W,H) = (halfW, halfH) pour les passes 6/7 (@0x4055F0..0x405675) et la PLEINE
// résolution pour le composite (@0x405912..0x405995).
// ---------------------------------------------------------------------------
namespace {

// Un sommet du quad plein écran : 24 octets, FVF 260.
struct PostBlurVertex {
    float x, y, z, rhw;
    float u, v;
};
static_assert(sizeof(PostBlurVertex) == 24, "Le quad de GXD_RenderPostBlur a un stride de 24");

// Remplit le quad TRIANGLESTRIP dans l'ordre EXACT du binaire.
void BuildPostBlurQuad(PostBlurVertex q[4], float w, float h) {
    q[0] = { 0.0f, h,    0.0f, 1.0f, 0.0f, 1.0f };
    q[1] = { 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
    q[2] = { w,    h,    0.0f, 1.0f, 1.0f, 1.0f };
    q[3] = { w,    0.0f, 0.0f, 1.0f, 1.0f, 0.0f };
}

} // namespace

void GxdRenderer::RenderPostBlur(const ShaderSet& shaders) {
    if (!m_device) return;

    const GxdShader& sh12 = shaders.Get(GxdShaderId::PS12_PostBlur); // flou horizontal
    const GxdShader& sh14 = shaders.Get(GxdShaderId::PS14_PostBlur); // flou vertical
    if (!sh12.Valid() || !sh14.Valid()) return; // shaders non chargés : rien à faire

    // Registres de sampler + handles résolus à la compilation (l'original les a figés dans
    // this+527424 / this+527464 (PS12) et this+527488 / this+527528 (PS14)).
    const int  samp12 = sh12.Sampler("mTexture0");
    const int  samp14 = sh14.Sampler("mTexture0");
    const D3DXHANDLE h12 = sh12.Handle("mTexture0PostSize");
    const D3DXHANDLE h14 = sh14.Handle("mTexture0PostSize");
    if (samp12 < 0 || samp14 < 0 || !h12 || !h14) return;

    if (!EnsureBlurTargets()) return;

    IDirect3DDevice9* dev = m_device;
    const int halfW = m_width  / 2;
    const int halfH = m_height / 2;

    // --- Récupère le back-buffer (vtbl+72 @0x405580) ---
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return;

    // --- Downsample back-buffer -> surfA (StretchRect vtbl+136, Filter=2 LINEAR @0x4055AD) ---
    if (FAILED(dev->StretchRect(bb, nullptr, m_blurSurfA, nullptr, D3DTEXF_LINEAR))) {
        bb->Release(); // @0x4055B9 : l'original sort par le Release de la surface
        return;
    }

    // --- États communs aux 3 passes (@0x4055D5 / @0x4055EC ; ebp = 0 confirmé au disasm) ---
    dev->SetRenderState(D3DRS_ZENABLE,  FALSE); // état 7   = 0
    dev->SetRenderState(D3DRS_LIGHTING, FALSE); // état 137 = 0

    PostBlurVertex quad[4];
    BuildPostBlurQuad(quad, static_cast<float>(halfW), static_cast<float>(halfH));

    // =====================================================================================
    // PASSE 6 — flou horizontal : texA -> surfB (PS12)
    // L'original ferme la scène, bascule la cible, puis rouvre : EndScene/SetRenderTarget/
    // BeginScene (@0x405681 / @0x40569A / @0x4056AB).
    // =====================================================================================
    dev->EndScene();
    dev->SetRenderTarget(0, m_blurSurfB);
    dev->BeginScene();

    m_currentShaderId = 6;                                        // (+526884) = 6 @0x4056B3
    dev->SetVertexShader(nullptr);                                // @0x4056C7
    dev->SetPixelShader(sh12.ps);                                 // @0x4056DF (+527408)
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);                     // 260 @0x4056F4
    dev->SetTexture(static_cast<DWORD>(samp12), m_blurTexA);      // @0x40570F
    dev->SetSamplerState(static_cast<DWORD>(samp12), D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); // @0x40572B
    dev->SetSamplerState(static_cast<DWORD>(samp12), D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP); // @0x405747
    // ID3DXConstantTable::SetFloat = vtbl+68 @0x40576B — SetFloat, PAS SetFloatArray.
    // Valeur = (float)halfW (COERCE_FLOAT(LODWORD(v23)), v23 converti @0x40563D).
    sh12.ct->SetFloat(dev, h12, static_cast<float>(halfW));
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(PostBlurVertex)); // @0x405787
    dev->SetSamplerState(static_cast<DWORD>(samp12), D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP); // @0x4057A3
    dev->SetSamplerState(static_cast<DWORD>(samp12), D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP); // @0x4057BF

    // =====================================================================================
    // PASSE 7 — flou vertical : texB -> surfA (PS14)  (@0x4057D0 / @0x4057E9 / @0x4057FA)
    // =====================================================================================
    dev->EndScene();
    dev->SetRenderTarget(0, m_blurSurfA);
    dev->BeginScene();

    m_currentShaderId = 7;                                        // (+526884) = 7 @0x405802
    dev->SetVertexShader(nullptr);                                // @0x405816
    dev->SetPixelShader(sh14.ps);                                 // @0x40582E (+527472)
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);                     // 260 @0x405843
    dev->SetTexture(static_cast<DWORD>(samp14), m_blurTexB);      // @0x40585E
    dev->SetSamplerState(static_cast<DWORD>(samp14), D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); // @0x40587A
    dev->SetSamplerState(static_cast<DWORD>(samp14), D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP); // @0x405896
    // Valeur = (float)halfH (COERCE_FLOAT(LODWORD(v22))) @0x4058BA.
    sh14.ct->SetFloat(dev, h14, static_cast<float>(halfH));
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(PostBlurVertex)); // @0x4058D6
    dev->SetSamplerState(static_cast<DWORD>(samp14), D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP); // @0x4058F2
    dev->SetSamplerState(static_cast<DWORD>(samp14), D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP); // @0x40590E

    // =====================================================================================
    // COMPOSITE — texA (flouté) additionné au back-buffer, en PLEINE résolution.
    // Quad reconstruit avec W = (+48) et H = (+52)  (@0x405912..0x405995).
    // =====================================================================================
    BuildPostBlurQuad(quad, static_cast<float>(m_width), static_cast<float>(m_height));

    dev->EndScene();                 // @0x4059A1
    dev->SetRenderTarget(0, bb);     // @0x4059B8
    bb->Release();                   // @0x4059C4 — relâché AVANT le BeginScene, comme l'original
    bb = nullptr;
    dev->BeginScene();               // @0x4059D5

    m_currentShaderId = 0;                                        // (+526884) = 0 @0x4059DE
    dev->SetVertexShader(nullptr);                                // @0x4059ED
    dev->SetPixelShader(nullptr);                                 // @0x4059FF — retour fixed-function
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);                     // 260 @0x405A14
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);            // état 27 = 1 @0x405A29
    dev->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCCOLOR);      // état 19 = 3 @0x405A3E
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ONE);           // état 20 = 2 @0x405A53
    dev->SetTexture(0, m_blurTexA);                               // étage 0 @0x405A68
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP); // @0x405A7E
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP); // @0x405A94
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quad, sizeof(PostBlurVertex)); // @0x405AB0
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);  // @0x405AC6
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);  // @0x405ADC

    // --- Restauration des états (ordre EXACT du binaire) ---
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_ZERO);          // état 20 = 1 @0x405AF1
    dev->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_ONE);           // état 19 = 2 @0x405B06
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);           // état 27 = 0 @0x405B1A
    dev->SetRenderState(D3DRS_LIGHTING, TRUE);                    // état 137 = 1 @0x405B32
    dev->SetRenderState(D3DRS_ZENABLE,  TRUE);                    // état 7 = 1 @0x405B47
    // NB : la restauration rend SRCBLEND=ONE / DESTBLEND=ZERO — ce ne sont PAS les états
    // initiaux d'Object A (SRCALPHA/INVSRCALPHA, Gfx_InitDevice @0x69C526/@0x69C535).
    // C'est bien ce que fait l'original ; on ne « corrige » pas.
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

// Gfx/GxdRenderer.h — singleton renderer du moteur GXD (g_GxdRenderer @ 0x18C4EF8).
// ex-VeryOldClient: TW2AddIn::GXD (v2 / Object B, Core/TW2AddIn/GXDHeader.h) — la BONNE
//   classe des deux GXD homonymes (celle qui porte les 12 shaders skinnés), CONFIRMED
//   Docs/TS2_GXD_ROSETTA.md §1.1/§3 ; NE PAS confondre avec v1 Core/GXD = Object A
//   (g_GfxRenderer 0x7FFE18, cf. Renderer.h). Discriminant prouvé : lumière 0.3/0.7 (v2),
//   PAS 0.4/0.5 (v1).
// Empile, au-dessus de ts2::gfx::Renderer, les états « haut niveau » du device :
// matrices projection/vue/monde, matériau, lumière directionnelle, viewport et états
// d'échantillonnage de textures.
//
// Fidèle au désassemblage (voir Docs/TS2_GXD_ENGINE.md) :
//   GXD_DeviceReinit        0x4023F0  -> DeviceReinit        // ex-VeryOldClient: TW2AddIn::GXD::InitForAddIn (GXDCore.cpp:523, reuses_device=true)
//   GXD_BeginScene          0x404640  -> SetupFrame          // ex-VeryOldClient: BeginForDrawing (v2)
//   GXD_ConfigSamplerStates 0x403B50  -> ConfigSamplerStates // ex-VeryOldClient: SetDefaultTextureSamplerState
//   GXD_SetDirectionalLight 0x403980  -> SetDirectionalLight
//   GXD_WorldToScreen       0x405C00  -> WorldToScreen
//
// La struct d'origine démarre à 0x18C4EF8 ; les commentaires « (+N) » donnent l'offset
// du champ correspondant dans cette struct. Champs COM device B (CONFIRMED §1.1) :
//   pD3D9 @+160 (0x18C4F98) · pDevice @+524 (0x18C5104) · pSprite @+528 (0x18C5108) ·
//   pFont @+532 (0x18C510C) · pDInput8 @+5440 (0x18C6438). PIÈGE : dword_18C5104 = le
//   CHAMP pDevice (base+524), PAS la base du renderer.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <d3dx9.h>
#include <cstdint>

namespace ts2::gfx {

// Les 12 shaders du npk (Gfx/ShaderSet.h). Déclaration avancée SEULEMENT : RenderPostBlur
// reçoit le jeu de shaders en PARAMÈTRE (il ne le possède pas) pour ne pas créer de
// dépendance d'en-tête GxdRenderer.h -> ShaderSet.h. Dans le binaire les slots shader sont
// des globals de fichier (bloc 0x1945918+) que GXD_RenderPostBlur 0x4053E0 lit via
// this+527404..+527528 — ici ils transitent par la référence.
class ShaderSet;

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
    // TODO [ancre 0x404640] : SANS APPELANT à ce jour — le début de frame passe par
    // ts2::gfx::Renderer::BeginFrame (Gfx_BeginFrame 0x6A2280, Object A). Voir le pavé
    // détaillé au-dessus de la définition dans GxdRenderer.cpp.
    bool SetupFrame();

    // (GXD_ConfigSamplerStates) Filtres min/mag/mip (anisotrope par défaut, linéaire si
    // m_useLinearFilter) + adressage WRAP (U,V) sur les étages 0..2.
    // Appelée PAR FRAME par ts2::gfx::Renderer::BeginFrame(), juste après Clear+BeginScene :
    // même position que les 6 sites d'appel des Scene_*Render (0x518916, 0x51B0C6, ...).
    // Écrase délibérément, chaque frame, les filtres LINEAR posés à l'init par Object A.
    void ConfigSamplerStates();

    // (GXD_SetDirectionalLight) Remplit un D3DLIGHT9 directionnel local (diffuse/specular
    // nuls, ambiant selon le mode, direction (-1,-1,1)) puis LightEnable(0)+SetLight(0).
    void SetDirectionalLight(int mode, float ar = 0.0f, float ag = 0.0f,
                             float ab = 0.0f, float aa = 1.0f);

    // (GXD_WorldToScreen) Projette un point monde en pixel écran arrondi (round half-up).
    // Renvoie false si le point est hors frustum.
    bool WorldToScreen(const D3DXVECTOR3& world, int& sx, int& sy) const;

    // -----------------------------------------------------------------------------------
    // (GXD_RenderPostBlur 0x4053E0) Bloom/post-blur : downsample du back-buffer en demi-
    // résolution, flou horizontal (passe 6 = PS12) puis vertical (passe 7 = PS14) en
    // ping-pong, enfin composite additif SRCCOLOR/ONE sur le back-buffer.
    //
    // AUCUNE OPTION D'ACTIVATION — fidèle : la garde d'origine `cmp [ecx+18h], 1` @0x4053ED
    // porte sur dword_18C4F10 (= base+24) dont xrefs_to ne donne QU'UN writer,
    // GXD_InitGlobalState @0x4013AC (`= 1`) : la garde est TOUJOURS vraie et la branche
    // `else` (Release des 4 RT) est morte. Le bloom n'est pas désactivable dans le client.
    //
    // CONTRAT DE SCÈNE (impératif) : la fonction d'origine fait EndScene/BeginScene autour
    // de CHAQUE bascule de render target — elle SUPPOSE donc une scène OUVERTE en entrée et
    // en laisse une OUVERTE en sortie. À appeler entre BeginFrame() et EndFrame().
    //
    // ⚠ CÂBLAGE À POSER HORS DE CE FICHIER (aucun fichier de ce front n'est le miroir de
    //   Scene_InGameRender 0x52D0B0) : l'appel d'origine est UNIQUE et INCONDITIONNEL, en
    //   ligne droite @0x52FB53 (`mov ecx, offset g_GxdRenderer ; call GXD_RenderPostBlur`),
    //   entre Env_StepTimeOfDay @0x52FB49 et Gfx_Begin2D @0x52FB89 — donc APRÈS tout le
    //   rendu 3D et AVANT le passage en 2D. Cf. le pavé de câblage dans GxdRenderer.cpp.
    void RenderPostBlur(const ShaderSet& shaders);

    // -----------------------------------------------------------------------------------
    // (GXD_OnDeviceLost 0x4042E0) Pose le drapeau de device perdu (+527548 @0x4042EC) puis
    // libère les ressources D3DPOOL_DEFAULT possédées. Ordre de libération d'origine relu au
    // décompilateur : +548 (surfA), +540 (texA), +552 (surfB), +544 (texB) — chacune remise à
    // 0 après Release(). Ce sont EXACTEMENT les 4 cibles de rendu du bloom ci-dessus.
    // À appeler AVANT IDirect3DDevice9::Reset (homologue @0x5188A8).
    void OnDeviceLost();

    // (GXD_RestoreAfterReset 0x404570) Gardée par le drapeau `+527548 == 1` @0x40457A ;
    // renvoie true sans rien faire si le drapeau n'est pas posé (`return 1` @0x4045B7), et
    // ne remet le drapeau à 0 (@0x404629) qu'en cas de succès total.
    // À appeler APRÈS un Reset() RÉUSSI (homologue @0x5188BC).
    //
    // PORTÉE RÉDUITE ASSUMÉE (et non un oubli) : l'original recrée aussi, sous ce même
    // drapeau, la déclaration de vertex skinné (CreateVertexDeclaration vtbl+344 @0x40461F,
    // g_GxdSkinnedVertexDecl76 0x814A58 -> +526880) et RECOMPILE les 12 shaders dans l'ordre
    // VS01,PS02,VS03,PS04,VS05,PS06,VS07,PS08,VS09,PS12,PS14,VS15 (`return 0` @0x4045B8 si
    // l'un échoue), et appelle World_ReloadMap @0x40458F si une carte est chargée (+6352).
    // Côté ClientSource, AUCUN de ces trois objets n'appartient à GxdRenderer : la
    // déclaration + les 12 shaders sont à ts2::gfx::ShaderSet (Gfx/ShaderSet.h, non possédé
    // par ce front) et la carte à World/. Techniquement ils survivent d'ailleurs à Reset()
    // (IDirect3DVertexShader9/PixelShader9/VertexDeclaration9 ne sont pas D3DPOOL_DEFAULT) :
    // ce rechargement est défensif chez l'original. Leurs propriétaires doivent passer par
    // l'observateur Renderer::SetDeviceCallbacks (cf. Gfx/Renderer.h).
    // Les 4 RT du bloom, elles, sont bien D3DPOOL_DEFAULT et sont recréées PARESSEUSEMENT
    // par RenderPostBlur (`if (!*(a1+540))` @0x40547A) — exactement comme l'original.
    bool RestoreAfterReset();

    // -----------------------------------------------------------------------------------
    // (Camera_SetEyeTarget 0x403420) Pousse le couple oeil (+712..+720) / cible (+724..+732).
    // RENVOIE UN BOOLÉEN et REJETTE deux cas — sémantique relue au décompilateur :
    //   1. `if (a5==a2 && a6==a3 && a7==a4) return 0;` @0x403465  -> oeil == cible.
    //   2. angle d'élévation |asin(|eye.y-at.y| / dist) * 57.2957763671875| > 89.989998
    //      @0x40350E -> `return 0` : trop près du pôle pour LookAtLH (up figé (0,1,0)).
    // Les champs ne sont écrits QUE si les deux tests passent (@0x403522..0x403574).
    // NB : cette borne 89.99 est DISTINCTE du clamp 89.9 de Cam_OrbitPitch 0x69CF90
    // (cf. Camera.h::kPitchLimitDeg) et des bornes 30/80 du drag souris — les trois
    // coexistent, à trois niveaux différents.
    //
    // ⚠ SANS APPELANT à ce jour (grep : les seuls hits `SetCamera` sont
    //   MeshRenderer::SetCamera(view, proj), une AUTRE classe). L'original, lui, a 33
    //   appelants vivants (Camera_MouseDragRotate, Camera_MouseWheelZoom,
    //   Camera_UpdateFromInput, Scene_InGameUpdate, Camera_UpdateCollision) : le câblage
    //   est à poser hors de ce front (App/App.cpp ou Scene/).
    bool SetCamera(const D3DXVECTOR3& eye, const D3DXVECTOR3& at);

    // Setters de COPIE (Scene_*Render : Object A -> Object B, une fois par frame).
    // Frontière prouvée par Scene_IntroRender 0x518880 (identique dans les 5 autres scènes) :
    //   qmemcpy(&unk_18C51E4, &unk_800154, 0x40) @0x5188DC : m_matView (+748) <- GfxRenderer+828
    //   qmemcpy(&g_WorldMatrix, &dword_800244, 0x40) @0x5188ED : m_matWorld (+988) <- +1068
    //   dword_18C51D8/DC/E0 (+736/+740/+744) <- g_CameraDir 0x800148/4C/50 (+816/+820/+824)
    //     @0x5188F5 / @0x518901 / @0x51890C
    // Ces champs sont DISJOINTS de +712/+724 (oeil/cible ci-dessus) : les deux chemins
    // coexistent dans le binaire, ils ne se remplacent pas.
    // ⚠ SANS APPELANT à ce jour — câblage à poser hors de ce front (App/App.cpp, garde
    //   `if (renderer_.BeginFrame())`, ou Scene/).
    void SetViewMatrix(const D3DXMATRIX& view) { m_matView = view; }   // (+748)
    void SetWorldMatrix(const D3DXMATRIX& world) { m_matWorld = world; } // (+988)
    void SetViewDir(const D3DXVECTOR3& dir) { m_viewDir = dir; }       // (+736)

    IDirect3DDevice9*   Device()   const { return m_device; }
    const D3DXMATRIX&   Proj()     const { return m_matProj; }
    const D3DXMATRIX&   View()     const { return m_matView; }
    const D3DXMATRIX&   World()    const { return m_matWorld; }
    const D3DMATERIAL9& Material() const { return m_material; }
    const D3DLIGHT9&    Light()    const { return m_light; }

    void SetUseLinearFilter(bool v) { m_useLinearFilter = v; } // (+8, dword_18C4F00)
    bool DepthBiasCapable() const { return m_depthBiasCapable; }
    bool TwoSidedStencil()   const { return m_twoSidedStencil; }

private:
    void BuildMatrices();                // proj / vue / monde / demi-viewport
    void BuildDefaultMaterialAndLight(); // matériau + lumière directionnelle
    // Crée paresseusement les 4 cibles de rendu demi-résolution du bloom (@0x40547A..0x405563).
    // false si une création échoue (l'original libère alors ce qu'il avait déjà pris et sort).
    bool EnsureBlurTargets();
    // Release() + remise à nullptr des 4 RT, ordre d'origine +548, +540, +552, +544.
    void ReleaseBlurTargets();
    void BuildFrustumPlanes();           // Frustum_BuildPlanes 0x406090
    bool FrustumContains(const D3DXVECTOR3& world) const; // Frustum_ContainsPoint5 0x406560

    IDirect3D9*       m_d3d      = nullptr; // pD3D9 @+160  — ex-VeryOldClient: (device COM B)
    IDirect3DDevice9* m_device   = nullptr; // pDevice @+524 (0x18C5104) — ex-VeryOldClient: (device COM B, == g_GfxRenderer+604)

    int   m_width  = 0;      // (+48 ; viewport +568)  ex-VeryOldClient: mScreenXSize
    int   m_height = 0;      // (+52 ; viewport +572)  ex-VeryOldClient: mScreenYSize
    float m_fovDeg = 45.0f;  // (+56) informatif ; la projection utilise PI/4 rad — ex-VeryOldClient: mFovY (45.0)
    float m_nearZ  = 0.0f;   // (+60)  ex-VeryOldClient: mNearPlane
    float m_farZ   = 0.0f;   // (+64)  ex-VeryOldClient: mFarPlane

    D3DVIEWPORT9 m_viewport{};        // (+560) X,Y,W,H,MinZ=0,MaxZ=1  ex-VeryOldClient: mViewport
    D3DXMATRIX   m_matHalfViewport;   // (+584) NDC [-1,1] -> pixels    ex-VeryOldClient: mViewportMatrix
    D3DXMATRIX   m_matProj;           // (+648) D3DXMatrixPerspectiveFovLH  ex-VeryOldClient: mPerspectiveMatrix
    D3DXVECTOR3  m_eye{0.0f, 0.0f, -10.0f}; // (+712)  ex-VeryOldClient: mCameraEye
    D3DXVECTOR3  m_at{0.0f, 0.0f, 0.0f};     // (+724)  ex-VeryOldClient: mCameraLook
    D3DXVECTOR3  m_viewDir{0.0f, 0.0f, 0.0f};// (+736) normalize(at-eye)  ex-VeryOldClient: mCameraForward
    D3DXMATRIX   m_matView;           // (+748) D3DXMatrixLookAtLH  ex-VeryOldClient: mViewMatrix
    D3DXMATRIX   m_matWorld;          // (+988) identité           ex-VeryOldClient: mWorldMatrix
    D3DMATERIAL9 m_material{};        // (+1052)  ex-VeryOldClient: mMaterial
    D3DLIGHT9    m_light{};           // (+1120)  ex-VeryOldClient: mLight (v2: Amb 0.3/Diff 0.7 — PAS v1 0.4/0.5)

    D3DCAPS9 m_caps{};                // (+164)  ex-VeryOldClient: mGraphicSupportInfo
    DWORD    m_maxAnisotropy = 1;     // caps.MaxAnisotropy (g_GxdMaxAnisotropy @ +272)
    int      m_currentShaderId = 0;   // (+526884) programme courant, remis à 0 par frame — ex-VeryOldClient: mPresentShaderProgramNumber (0=fixed,6=Filter1,7=Filter2)

    bool m_useLinearFilter = false;  // (+8) 0 => anisotrope (défaut), !=0 => linéaire — ex-VeryOldClient: mSamplerOptionValue (ctor=0)

    // -----------------------------------------------------------------------------------
    // CHAMPS +28 / +32 — DOUBLE IDENTITÉ (gap G3, corrigé Passe 4 / W9).
    //
    // Ces deux dwords portent DEUX noms selon le bout par lequel on les regarde :
    //   +28 = 0x18C4F14 = « m_depthBiasCapable » (producteur : caps GPU) == g_ShadowsEnabled
    //         (consommateur : porte de TOUT le rendu d'ombre, lue @0x40EEF8)
    //   +32 = 0x18C4F18 = « m_twoSidedStencil »  (producteur) == g_ShadowMethod
    //         (consommateur : 0 = z-fail Carmack 2 passes, 1 = stencil two-sided ;
    //          lue @0x40EFCC / @0x40F27B / @0x40F66A)
    //
    // ILS VALENT TOUJOURS 1 DANS LE BINAIRE — deux preuves qui se cumulent :
    //   1. GXD_InitGlobalState 0x401320 les pose à 1 (`mov ebx, 1` @0x401365 puis
    //      `mov ds:g_ShadowsEnabled, ebx` @0x4013B2 / `mov ds:g_ShadowMethod, ebx` @0x4013B8).
    //   2. GXD_DeviceReinit ne fait que les VERROUILLER à 1, jamais les effacer — relu au
    //      désassemblage : `jz short loc_40292D ; mov [esi+1Ch], edi` @0x402928/@0x40292A et
    //      `test dword ptr [esi+12Ch], 100h ; jz ; mov [esi+20h], edi` @0x40292D..@0x402939
    //      (edi = 1, 0x1C = 28, 0x20 = 32). AUCUNE branche `else` : un GPU dépourvu de la cap
    //      laisse simplement le 1 de l'init en place.
    //   GXD_FreeGlobalState 0x401530 (décompilée intégralement) ne touche NI +28 NI +32 —
    //   d'où l'absence de remise à false dans Shutdown() (cf. GxdRenderer.cpp).
    // => on initialise à `true` et on n'écrit JAMAIS `false` (cf. DeviceReinit).
    //
    // ⚠ MODÈLE CONSOMMATEUR FAISANT AUTORITÉ : Gfx/MeshRenderer.h:391-392
    //   (shadowsEnabled_ = true / shadowMethod_ = 1, mêmes ancres 0x4013B2/0x4013B8). Le même
    //   champ binaire est donc modélisé DEUX FOIS dans ClientSource ; en cas de divergence
    //   future, MeshRenderer.h gagne — c'est lui qui pilote réellement une branche de rendu.
    //   DepthBiasCapable()/TwoSidedStencil() n'ont, eux, aucun appelant (grep).
    // NB producteur : le bit 0x4000000 = D3DPRASTERCAPS_DEPTHBIAS (depth-bias hérité), PAS
    // l'anisotropie (0x20000). La Rosetta §2 étiquette ce bit « SLOPESCALEDEPTHBIAS », mais
    // 0x4000000 EST DEPTHBIAS (SLOPESCALEDEPTHBIAS = 0x2000000) — IDA gagne. L'anisotropie ne
    // dépend PAS de ce champ (cf. m_useLinearFilter / m_maxAnisotropy, ConfigSamplerStates).
    // ex-VeryOldClient: mCheckDepthBias / mCheckTwoSideStencilFunction.
    bool m_depthBiasCapable = true; // (+28) == g_ShadowsEnabled 0x18C4F14, figé à 1
    bool m_twoSidedStencil  = true; // (+32) == g_ShadowMethod   0x18C4F18, figé à 1

    // ----- Post-process bloom (GXD_RenderPostBlur 0x4053E0) — D3DPOOL_DEFAULT -------------
    // Créées PARESSEUSEMENT au 1er RenderPostBlur (`if (!*(a1+540))` @0x40547A) et libérées
    // par OnDeviceLost(). surfX = GetSurfaceLevel(0) de texX (vtbl+72 @0x4054AC / @0x405536).
    // Ping-pong : texA = downsample(backbuffer) -> texB = flouH(texA) -> texA = flouV(texB)
    //             -> backbuffer += texA (SRCCOLOR/ONE).
    IDirect3DTexture9* m_blurTexA  = nullptr; // (+540)
    IDirect3DTexture9* m_blurTexB  = nullptr; // (+544)
    IDirect3DSurface9* m_blurSurfA = nullptr; // (+548) surface 0 de m_blurTexA
    IDirect3DSurface9* m_blurSurfB = nullptr; // (+552) surface 0 de m_blurTexB

    // (+527548) Drapeau « device perdu » : posé par OnDeviceLost (@0x4042EC), consommé et
    // effacé par RestoreAfterReset (@0x40457A / @0x404629), remis à 0 par DeviceReinit
    // (@0x402451).
    bool m_deviceLost = false;

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

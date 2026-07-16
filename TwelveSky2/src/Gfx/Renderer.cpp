// Gfx/Renderer.cpp — device Direct3D9.
#include "Gfx/Renderer.h"
// GXD_ConfigSamplerStates 0x403B50 opère sur Object B (g_GxdRenderer 0x18C4EF8, via son champ
// pDevice @+524) mais est appelée PAR FRAME depuis les Scene_*Render, juste après Gfx_BeginFrame
// 0x6A2280 (Object A) — d'où cette dépendance de BeginFrame vers le singleton GxdRenderer.
// Pas de cycle d'inclusion : Gfx/GxdRenderer.h n'inclut pas Gfx/Renderer.h.
#include "Gfx/GxdRenderer.h"
#include "Core/Log.h"

#pragma comment(lib, "d3d9.lib")

namespace ts2::gfx {

Renderer::~Renderer() { Shutdown(); }

bool Renderer::Init(HWND hwnd, int width, int height, bool windowed) {
    // Gfx_InitDevice 0x69B9B0 — createur du device physique (Object A = g_GfxRenderer 0x7FFE18).
    // L'original NE consulte JAMAIS GetAdapterDisplayMode : le back-buffer est fige a X8R8G8B8
    // et le mode video plein-ecran est impose par ChangeDisplaySettingsA (device reste Windowed).

    // Plein-ecran : ChangeDisplaySettingsA(CDS_FULLSCREEN) AVANT CreateDevice.
    // L'original teste `if (plein-ecran demande)` ; ici `windowed` = inverse de ce flag (a2, this+27).
    if (!windowed) {                             // a2 = flag plein-ecran
        DEVMODEA dm;                             // Gfx_InitDevice 0x69bb20
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize        = 156;                  // sizeof(DEVMODEA) fige a 156
        dm.dmBitsPerPel  = 32;
        dm.dmPelsWidth   = (DWORD)width;         // a5
        dm.dmPelsHeight  = (DWORD)height;        // a6
        dm.dmFields      = 0x1C0000;             // DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT
        // Gfx_InitDevice 0x69bb20 : ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN=4).
        if (ChangeDisplaySettingsA(&dm, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL) {
            TS2_ERR("ChangeDisplaySettingsA a echoue (code fidelite 3)"); // *a11=3 @0x69bb2e
            return false;
        }
    }

    // Direct3DCreate9(0x20) — Gfx_InitDevice 0x69bb3b (echec => code 4).
    d3d_ = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d_) { TS2_ERR("Direct3DCreate9 a echoue (code fidelite 4)"); return false; }

    // GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps) — Gfx_InitDevice 0x69bb6f (echec => code 5).
    // Reproduit l'ordonnancement d'origine (caps interrogees avant CheckDeviceFormat).
    D3DCAPS9 caps{};                             // Gfx_InitDevice 0x69bb6f
    if (FAILED(d3d_->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps))) {
        TS2_ERR("GetDeviceCaps HAL a echoue (code fidelite 5)");         // *a11=5 @0x69bb75
        Shutdown(); return false;
    }

    // CheckDeviceFormat(0, HAL, X8R8G8B8, 0, D3DRTYPE_TEXTURE, DXTn) x3 AVANT CreateDevice.
    // FourCC prouves : 0x31545844="DXT1" (0x69bb88), 0x33545844="DXT3" (0x69bba8),
    // 0x35545844="DXT5" (0x69bbc8). Un echec branche loc_69C966 => code 6.
    if (FAILED(d3d_->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                  D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT1)) ||   // 0x69bb95
        FAILED(d3d_->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                  D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT3)) ||   // 0x69bbb5
        FAILED(d3d_->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                  D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT5))) {   // 0x69bbd5
        TS2_ERR("CheckDeviceFormat DXTn non supporte (code fidelite 6)"); // *a11=6 @0x69c96a
        Shutdown(); return false;
    }

    // Present params — Gfx_InitDevice memset(this+137,0,0x38) 0x69bbef puis champs figes.
    ZeroMemory(&pp_, sizeof(pp_));                          // 0x69bbef
    pp_.BackBufferWidth        = width;                     // 0x69bc28 (*(this+137))
    pp_.BackBufferHeight       = height;                    // 0x69bc31 (*(this+138))
    pp_.BackBufferFormat       = D3DFMT_X8R8G8B8;           // 0x69bc37 (=22, INCONDITIONNEL)
    pp_.BackBufferCount        = 1;                         // 0x69bc03 (esi=1)
    pp_.MultiSampleType        = D3DMULTISAMPLE_NONE;       // 0x69bc41 (ebx=0)
    pp_.MultiSampleQuality     = 0;                         // 0x69bc47 (ebx=0)
    pp_.SwapEffect             = D3DSWAPEFFECT_DISCARD;     // 0x69bc09 (esi=1)
    pp_.hDeviceWindow          = hwnd;                      // 0x69bc4d (edi=hwnd)
    pp_.Windowed               = TRUE;                      // 0x69bc0f (esi=1, INCONDITIONNEL)
    pp_.EnableAutoDepthStencil = TRUE;                      // 0x69bc15 (esi=1)
    pp_.AutoDepthStencilFormat = D3DFMT_D24S8;              // 0x69bc53 (=0x4B=75)
    pp_.Flags                  = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL; // 0x69bc5d (=2)
    pp_.FullScreen_RefreshRateInHz = 0;                     // 0x69bc67 (ebx=0)
    pp_.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE;       // 0x69bc6d (=0x80000000)

    // CreateDevice — Gfx_InitDevice 0x69bc9d. BehaviorFlags HW=68 = HARDWARE_VERTEXPROCESSING(0x40)
    // | MULTITHREADED(0x4). L'original passe hwnd en hFocusWindow ET en pp_.hDeviceWindow.
    DWORD hwFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;
    HRESULT hr = d3d_->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, hwFlags, &pp_, &device_);
    if (FAILED(hr)) {
        // Fallback SOFTWARE_VERTEXPROCESSING(0x20) | MULTITHREADED (= 36) comme le client.
        TS2_WARN("CreateDevice HW echoue (0x%08lX), fallback SOFTWARE", hr);
        DWORD swFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;
        hr = d3d_->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, swFlags, &pp_, &device_);
    }
    if (FAILED(hr)) { TS2_ERR("CreateDevice a echoue (0x%08lX, code fidelite 7)", hr); Shutdown(); return false; }

    // Etats sampler/render initiaux EXACTS d'Object A (Gfx_InitDevice 0x69c470..0x69c543).
    ApplyInitialDeviceStates();

    TS2_LOG("Device D3D9 cree : %dx%d (%s)", width, height, windowed ? "fenetre" : "plein-ecran");
    return true;
}

// Gfx_InitDevice 0x69c470..0x69c543 : etats sampler (stages 0/1 SEULEMENT) + render states
// initiaux d'Object A. Filtres LINEAR FIXES — PAS d'anisotropie, PAS de dependance aux caps.
// (L'ancien code anisotrope base sur GXD_ConfigSamplerStates 0x403B50 appartenait a Object B /
//  GxdRenderer : ancre erronee pour Object A, remplacee ici.)
// SetSamplerState = vtbl+0x114 (276), SetRenderState = vtbl+0xE4 (228). ebx=0, edi=1 confirmes
// au desassemblage. Sampler ET render states ne survivent PAS a Reset() : rappelee apres chaque
// Reset() reussi (HandleDeviceLost).
void Renderer::ApplyInitialDeviceStates() {
    if (!device_) return;
    IDirect3DDevice9* d = device_;

    // --- Stage 0 : MAG/MIN=LINEAR, MIP=POINT, ADDRESS U/V=WRAP ---
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);   // 0x69c470 (type5, val 2)
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);   // 0x69c480 (type6, val 2)
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);    // 0x69c48f (type7, val edi=1)
    d->SetSamplerState(0, D3DSAMP_ADDRESSU,  D3DTADDRESS_WRAP); // 0x69c49d (type edi=1, val 1)
    d->SetSamplerState(0, D3DSAMP_ADDRESSV,  D3DTADDRESS_WRAP); // 0x69c4ac (type2, val edi=1)
    // --- Stage 1 : MAG/MIN=LINEAR, MIP=NONE, ADDRESS U/V=CLAMP ---
    d->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);   // 0x69c4bc (type5, val 2)
    d->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);   // 0x69c4cc (type6, val 2)
    d->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);     // 0x69c4db (type7, val ebx=0)
    d->SetSamplerState(1, D3DSAMP_ADDRESSU,  D3DTADDRESS_CLAMP);// 0x69c4ea (type edi=1, val 3)
    d->SetSamplerState(1, D3DSAMP_ADDRESSV,  D3DTADDRESS_CLAMP);// 0x69c4fa (type2, val 3)

    // --- Render states initiaux (Gfx_InitDevice 0x69c508..0x69c543) ---
    d->SetRenderState(D3DRS_ALPHAREF,     0);                    // 0x69c508 (state 24, val 0)
    d->SetRenderState(D3DRS_ALPHAFUNC,    D3DCMP_GREATER);       // 0x69c517 (state 25, val 5=GREATER, PAS GREATEREQUAL)
    d->SetRenderState(D3DRS_SRCBLEND,     D3DBLEND_SRCALPHA);    // 0x69c526 (state 19, val 5)
    d->SetRenderState(D3DRS_DESTBLEND,    D3DBLEND_INVSRCALPHA); // 0x69c535 (state 20, val 6)
    d->SetRenderState(D3DRS_DITHERENABLE, TRUE);                 // 0x69c543 (state 26, val edi=1)

    // Ne PAS toucher au stage 2 ni a MAXANISOTROPY : Object A ne les configure pas.
    // Les 3 ecritures *(this+331..333)={2,0,1} @0x69c457/61/67 sont des champs cache internes
    // du renderer (pas des appels device) ; non modelises ici (pas de consommateur).
    // Le bloc fog @0x69c3e4 (FOGENABLE/FOGCOLOR/FOGTABLEMODE=3/FOGSTART/FOGEND/RANGEFOGENABLE) est
    // conditionne par a9 (this+35, param fog absent de la signature Init) — hors perimetre ici.
}

void Renderer::Shutdown() {
    if (device_) { device_->Release(); device_ = nullptr; }
    if (d3d_)    { d3d_->Release();    d3d_    = nullptr; }
}

void Renderer::SetDeviceCallbacks(DeviceNotifyFn onLost, DeviceNotifyFn onReset, void* user) {
    // Enregistre les observateurs tenant lieu des OnLostDevice/OnResetDevice que
    // Gfx_HandleDeviceLostReset 0x69DD40 émet directement sur ses objets D3DX possédés
    // (Effect +620 / Font +612 / Sprite +608 de g_GfxRenderer 0x7FFE18).
    onLost_     = onLost;
    onReset_    = onReset;
    notifyUser_ = user;
}

// GXD_OnDeviceLost 0x4042E0 (relu intégralement) : dans le binaire d'origine, cette
// fonction libère ~24 ressources D3DPOOL_DEFAULT nommées par offset (Release() via
// vtable+8) réparties sur l'objet renderer géant (VB/IB de shadow volumes, surfaces
// offscreen de post-process/blur @+526880..+527536, etc.), puis appelle
// World_Shutdown(this+6348) si une carte est chargée (flag +6352) et pose le drapeau
// +527548 que GXD_RestoreAfterReset 0x404570 consomme. Elle est appelée par les
// Scene_*Render quand Gfx_HandleDeviceLostReset a posé *a5=1 (ex. 0x51B058).
// Non applicable telle quelle ici : dans ClientSource, TOUTES les ressources D3D9 créées
// par le renderer (VB/IB de Gfx/MeshRenderer.cpp, Gfx/WorldGeometryRenderer.cpp, textures
// de Gfx/GpuTexture.cpp) sont allouées en D3DPOOL_MANAGED (cf. leurs CreateVertexBuffer/
// CreateIndexBuffer/CreateTexture), donc gérées automatiquement par Direct3D9 à travers
// Reset() -- rien à libérer explicitement ici tant qu'aucun composant D3DPOOL_DEFAULT
// (render target offscreen, vertex buffer dynamique) n'existe côté ClientSource. Si un tel
// composant est ajouté plus tard (ex. post-process blur, shadow volumes), il devra
// s'enregistrer ici (Release avant Reset) pour rester fidèle à ce que fait l'original.
bool Renderer::HandleDeviceLost() {
    // --- Gfx_HandleDeviceLostReset 0x69DD40, séquence relue instruction par instruction ---
    // TestCooperativeLevel = vtbl+12 @0x69DD4C (device @+604 = 0x800074).
    HRESULT hr = device_->TestCooperativeLevel();

    // 0x88760868 D3DERR_DEVICELOST @0x69DD54 : *a5=0, `return 0` -> pas encore restaurable.
    if (hr == D3DERR_DEVICELOST) return false;

    // 0x88760869 D3DERR_DEVICENOTRESET @0x69DD5F : le device peut être réinitialisé.
    if (hr == D3DERR_DEVICENOTRESET) {
        // Le binaire Release() d'abord ses 4 textures/surfaces de glow D3DPOOL_DEFAULT
        // (+632/+624/+636/+628, Release = vtbl+8 @0x69DD95..0x69DDF4, chacune remise à 0).
        // Rien à faire ici : toutes les ressources D3D9 de ClientSource sont créées en
        // D3DPOOL_MANAGED (cf. Gfx/MeshRenderer.cpp, Gfx/WorldGeometryRenderer.cpp,
        // Gfx/GpuTexture.cpp, UIManager::CreateWhiteTexture) et survivent donc à Reset().
        // Le post-process de glow (+1432 et son effet "FILTER") n'est pas porté.

        // OnLostDevice AVANT Reset — ordre PROUVÉ @0x69DE3E, court-circuité par `&&` :
        //   Effect(+620) vtbl+276 -> Font(+612) vtbl+64 -> Sprite(+608) vtbl+48.
        // Un échec (<0) saute le Reset et pose *a5=1 @0x69DE14. Transposition : l'observateur
        // (SceneManager::OnDeviceLost -> Font::OnDeviceLost -> ID3DXFont/ID3DXSprite::
        // OnLostDevice) renvoie void et ne peut pas échouer ; l'ordre intra-objets est fixé
        // par le propriétaire des objets D3DX, pas ici.
        if (onLost_) onLost_(notifyUser_);

        // Reset(pp) = vtbl+64 @0x69DE55, D3DPRESENT_PARAMETERS @+548. Échec -> *a5=1, `return 0`.
        hr = device_->Reset(&pp_);
        if (FAILED(hr)) return false;

        // OnResetDevice APRÈS Reset — ordre INVERSE, PROUVÉ @0x69DE9B :
        //   Sprite(+608) vtbl+52 -> Font(+612) vtbl+68 -> Effect(+620) vtbl+280.
        if (onReset_) onReset_(notifyUser_);

        // Sampler ET render states ne survivent PAS à Reset() en D3D9 : le binaire les repose
        // intégralement dans la foulée (SetViewport/SetTransform/SetMaterial/SetLight/
        // SetRenderState/SetSamplerState, 0x69DF3A..0x69E1AC). On repose ici le sous-ensemble
        // d'Object A modélisé (Gfx_InitDevice 0x69C470..0x69C543) ; les sampler states d'Object B
        // sont de toute façon reposés à chaque frame par ConfigSamplerStates (cf. BeginFrame).
        ApplyInitialDeviceStates();

        // LABEL_42 @0x69E251 : *a5=0 puis `return 0`. Après un Reset RÉUSSI le binaire renvoie
        // 0 -> l'appelant NE REND PAS cette frame (pas de Gfx_BeginFrame, pas de Gfx_Present).
        // Le rendu reprend à la frame suivante, quand TestCooperativeLevel renverra D3D_OK.
        return false;

        // GXD_RestoreAfterReset 0x404570 : le binaire ne l'appelle PAS ici mais dans les
        // Scene_*Render, juste après un Gfx_HandleDeviceLostReset ayant renvoyé 1 (ex.
        // 0x51B06C dans Scene_LoginRender) — et son corps est gardé par le drapeau +527548
        // que GXD_OnDeviceLost 0x4042E0 vient de poser. Elle recompile les 12 shaders,
        // recrée la déclaration de vertex skinné 76 o (g_GxdSkinnedVertexDecl76 0x814A58) et,
        // si une carte est chargée, appelle World_ReloadMap 0x411B60. Non porté ici : Renderer
        // est le wrapper D3D9 bas niveau et n'a aucune référence à ts2::gfx::ShaderSet ni à
        // World/WorldMap. Techniquement IDirect3DVertexShader9/IDirect3DPixelShader9/
        // IDirect3DVertexDeclaration9 ne sont PAS D3DPOOL_DEFAULT et survivent à Reset() : ce
        // rechargement est défensif plutôt que nécessaire. Les propriétaires de ces objets
        // doivent s'y prendre via l'observateur onReset_ ci-dessus, pas depuis Renderer.
    }

    // Tout autre HRESULT NON NUL @0x69DD69 (ex. D3DERR_DRIVERINTERNALERROR) : *a5=1, `return 0`.
    // D3D_OK @0x69DD79 : *a5=0, `return 1` — SEUL chemin qui autorise le rendu de la frame.
    // Test sur `!= 0` et NON sur FAILED() : le binaire fait `if (v6)` @0x69DD63 (test/jnz sur la
    // valeur brute), donc un HRESULT de succès non nul serait lui aussi refusé.
    return hr == D3D_OK;
}

bool Renderer::BeginFrame() {
    if (!device_) return false;

    // Gfx_HandleDeviceLostReset 0x69DD40 — INCONDITIONNEL à chaque frame, comme en tête de
    // chaque Scene_*Render (0x518894, 0x51B044, ...). L'ancienne garde `deviceLost_ &&` était
    // une invention locale : le binaire n'a aucun drapeau et interroge TestCooperativeLevel
    // à chaque frame (cf. Renderer.h). false -> frame sautée (device perdu OU tout juste reset).
    if (!HandleDeviceLost()) return false;

    // Gfx_BeginFrame 0x6A2280 : Clear(vtbl+172) puis BeginScene (vtbl+164 @0x6A24CC).
    device_->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                   clearColor_, 1.0f, 0);
    if (FAILED(device_->BeginScene())) return false;

    // GXD_ConfigSamplerStates 0x403B50 (Object B) — appelée PAR FRAME, APRÈS Gfx_BeginFrame
    // (donc après Clear+BeginScene) et AVANT toute passe de dessin. Position prouvée par les
    // 6 sites d'appel dans les Scene_*Render : 0x518916, 0x5192F6, 0x51B0C6, 0x51CF76,
    // 0x52C306, 0x52D24A (le 7e site est GXD_BeginScene 0x404640 @0x4047C7, non porté).
    // C'est cet appel PAR FRAME qui rend le filtrage anisotrope effectif : il ÉCRASE
    // délibérément les filtres LINEAR posés une seule fois à l'init par Object A
    // (Gfx_InitDevice 0x69C470, cf. ApplyInitialDeviceStates) — les deux coexistent dans le
    // binaire, et le net runtime est ANISOTROPIC. No-op sûr tant que GxdRenderer::DeviceReinit
    // n'a pas tourné (garde `if (!m_device) return;`), p. ex. dans les self-tests.
    GxdRenderer::Instance().ConfigSamplerStates();
    return true;
}

void Renderer::EndFrame() {
    // Gfx_Present 0x69E270 : EndScene (vtbl+168 @0x69E27C) puis
    // Present(0,0,0,0) (vtbl+68 @0x69E296). Le HRESULT de Present n'est JAMAIS inspecté par
    // le binaire (il est simplement renvoyé) : la détection de perte de device passe
    // exclusivement par le TestCooperativeLevel de début de frame. On ne pose donc aucun
    // drapeau ici — ce que faisait l'ancien code (`deviceLost_ = true`) était une invention.
    if (!device_) return;
    device_->EndScene();
    device_->Present(nullptr, nullptr, nullptr, nullptr);
}

} // namespace ts2::gfx

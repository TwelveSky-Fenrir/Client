// Gfx/Renderer.cpp — device Direct3D9.
#include "Gfx/Renderer.h"
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

// GXD_OnDeviceLost 0x4042E0 (relu intégralement) : dans le binaire d'origine, cette
// fonction libère ~24 ressources D3DPOOL_DEFAULT nommées par offset (Release() via
// vtable+8) réparties sur l'objet renderer géant (VB/IB de shadow volumes, surfaces
// offscreen de post-process/blur @+526880..+527536, etc.), puis appelle
// World_Shutdown(this+6348) si une carte est chargée (flag +6352).
// Non applicable telle quelle ici : dans ClientSource, TOUTES les ressources D3D9 créées
// par le renderer (VB/IB de Gfx/MeshRenderer.cpp, Gfx/WorldGeometryRenderer.cpp, textures
// de Gfx/GpuTexture.cpp) sont allouées en D3DPOOL_MANAGED (cf. leurs CreateVertexBuffer/
// CreateIndexBuffer/CreateTexture), donc gérées automatiquement par Direct3D9 à travers
// Reset() -- rien à libérer explicitement ici tant qu'aucun composant D3DPOOL_DEFAULT
// (render target offscreen, vertex buffer dynamique) n'existe côté ClientSource. Si un tel
// composant est ajouté plus tard (ex. post-process blur, shadow volumes), il devra
// s'enregistrer ici (Release avant Reset) pour rester fidèle à ce que fait l'original.
bool Renderer::HandleDeviceLost() {
    HRESULT hr = device_->TestCooperativeLevel();
    if (hr == D3DERR_DEVICELOST) return false; // pas encore restaurable
    if (hr == D3DERR_DEVICENOTRESET) {
        hr = device_->Reset(&pp_);
        if (FAILED(hr)) return false;
        deviceLost_ = false;
        // Sampler ET render states (SetSamplerState/SetRenderState) ne survivent PAS à Reset()
        // en D3D9 : sans ceci le device retomberait sur ses états par défaut après toute perte
        // de device (Alt-Tab, changement de résolution) même si Init() les avait bien posés.
        // États repris de Gfx_InitDevice 0x69c470/0x69c508 (l'original les restaure via
        // GXD_RestoreAfterReset 0x404570 côté moteur ; ici on les repose localement).
        ApplyInitialDeviceStates();
        // GXD_RestoreAfterReset 0x404570 (relu intégralement) : recompile les 12 shaders
        // (Shader_LoadVS01..VS15/PS02..PS14), recrée la déclaration de vertex skinné 76 o
        // (g_GxdSkinnedVertexDecl76 0x814A58, vtbl+344 = CreateVertexDeclaration) et, si une
        // carte est chargée, appelle World_ReloadMap 0x411b60.
        // Non porté ici : Renderer est le wrapper D3D9 bas niveau (device/swapchain) et n'a
        // aucune référence à ts2::gfx::ShaderSet ni à World/WorldMap -- ces objets sont
        // possédés à un niveau supérieur (App/scène, cf. ClientSource/README.md) qui n'est
        // pas encore câblé à ce jour (jalon 1 = socle). Techniquement, IDirect3DVertexShader9/
        // IDirect3DPixelShader9/IDirect3DVertexDeclaration9 ne sont PAS des ressources
        // D3DPOOL_DEFAULT et survivent à Reset() en D3D9 (contrairement aux VB/IB/textures
        // D3DPOOL_DEFAULT) : le rechargement effectué par l'original est donc défensif plutôt
        // que strictement nécessaire. Quand ShaderSet/WorldMap seront possédés par l'App, leur
        // propriétaire devra relancer ShaderSet::Load()/LoadFromFile() et l'équivalent de
        // World_ReloadMap depuis son propre callback de device-lost (pas depuis Renderer).
    }
    return true;
}

bool Renderer::BeginFrame() {
    if (!device_) return false;
    if (deviceLost_ && !HandleDeviceLost()) return false;

    device_->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER,
                   clearColor_, 1.0f, 0);
    return SUCCEEDED(device_->BeginScene());
}

void Renderer::EndFrame() {
    if (!device_) return;
    device_->EndScene();
    HRESULT hr = device_->Present(nullptr, nullptr, nullptr, nullptr);
    if (hr == D3DERR_DEVICELOST) deviceLost_ = true;
}

} // namespace ts2::gfx

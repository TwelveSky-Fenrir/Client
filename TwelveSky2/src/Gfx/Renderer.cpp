// Gfx/Renderer.cpp — device Direct3D9.
#include "Gfx/Renderer.h"
#include "Core/Log.h"

#pragma comment(lib, "d3d9.lib")

namespace ts2::gfx {

Renderer::~Renderer() { Shutdown(); }

bool Renderer::Init(HWND hwnd, int width, int height, bool windowed) {
    d3d_ = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d_) { TS2_ERR("Direct3DCreate9 a echoue"); return false; }

    D3DDISPLAYMODE dm = {};
    d3d_->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &dm);

    ZeroMemory(&pp_, sizeof(pp_));
    pp_.Windowed               = windowed ? TRUE : FALSE;
    pp_.SwapEffect             = D3DSWAPEFFECT_DISCARD;
    pp_.BackBufferWidth        = width;
    pp_.BackBufferHeight       = height;
    pp_.BackBufferFormat       = windowed ? dm.Format : D3DFMT_X8R8G8B8;
    pp_.BackBufferCount        = 1;
    pp_.EnableAutoDepthStencil = TRUE;
    pp_.AutoDepthStencilFormat = D3DFMT_D24S8;   // fidèle : depth-stencil 24/8
    pp_.hDeviceWindow          = hwnd;
    pp_.PresentationInterval   = D3DPRESENT_INTERVAL_IMMEDIATE;

    // BehaviorFlags 68 = HARDWARE_VERTEXPROCESSING(0x40) | MULTITHREADED(0x4).
    DWORD hwFlags = D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;
    HRESULT hr = d3d_->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, hwFlags, &pp_, &device_);
    if (FAILED(hr)) {
        // Fallback SOFTWARE_VERTEXPROCESSING(0x20) | MULTITHREADED (= 36) comme le client.
        TS2_WARN("CreateDevice HW echoue (0x%08lX), fallback SOFTWARE", hr);
        DWORD swFlags = D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED;
        hr = d3d_->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, swFlags, &pp_, &device_);
    }
    if (FAILED(hr)) { TS2_ERR("CreateDevice a echoue (0x%08lX)", hr); Shutdown(); return false; }

    // GXD_ConfigSamplerStates 0x403B50 (branche par défaut, cf. Gfx/GxdRenderer.cpp) :
    // sans cet appel le device reste sur les états D3D9 par défaut (MIN/MAGFILTER=POINT,
    // MIPFILTER=NONE) -> textures aliasées "en blocs" et mips jamais échantillonnés même
    // quand ils sont uploadés (GpuTexture::UploadDxtBlocks). Corrige l'écart de qualité
    // visuelle vs le binaire d'origine (constaté : filtre POINT au lieu d'ANISOTROPIC/LINEAR).
    ConfigureSamplerStates();

    TS2_LOG("Device D3D9 cree : %dx%d (%s)", width, height, windowed ? "fenetre" : "plein-ecran");
    return true;
}

// GXD_ConfigSamplerStates 0x403B50 — branche « this[2] == 0 » (défaut d'origine, dword_18C4F00
// jamais mis à 1 par un chemin câblé côté ClientSource) : filtrage ANISOTROPIC sur les étages
// 0..2 avec MAXANISOTROPY = caps.MaxAnisotropy, adressage WRAP (U,V). Repli LINEAR si le device
// ne supporte pas l'anisotrope (D3DPRASTERCAPS_ANISOTROPY absent ou MaxAnisotropy <= 1) : le
// binaire d'origine ne fait pas ce repli explicitement (il dépend du pilote pour clamper), mais
// un device logiciel/REF peut renvoyer MaxAnisotropy=1 -> ANISOTROPIC dégénère alors en POINT
// silencieusement ; on force LINEAR dans ce cas précis pour ne jamais retomber sur POINT.
void Renderer::ConfigureSamplerStates() {
    if (!device_) return;

    D3DCAPS9 caps{};
    const bool haveCaps = SUCCEEDED(device_->GetDeviceCaps(&caps));
    const bool anisoOk  = haveCaps
        && (caps.RasterCaps & D3DPRASTERCAPS_ANISOTROPY)
        && caps.MaxAnisotropy > 1;

    for (DWORD s = 0; s <= 2; ++s) {
        if (anisoOk) {
            device_->SetSamplerState(s, D3DSAMP_MINFILTER,     D3DTEXF_ANISOTROPIC);
            device_->SetSamplerState(s, D3DSAMP_MAGFILTER,     D3DTEXF_ANISOTROPIC);
            device_->SetSamplerState(s, D3DSAMP_MIPFILTER,     D3DTEXF_ANISOTROPIC);
            device_->SetSamplerState(s, D3DSAMP_MAXANISOTROPY, caps.MaxAnisotropy);
        } else {
            device_->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            device_->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            device_->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_LINEAR);
        }
        device_->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_WRAP);
        device_->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_WRAP);
    }
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
        // Les états d'échantillonnage (SetSamplerState) ne survivent PAS à Reset() en D3D9 :
        // sans ceci le device retomberait sur POINT/NONE après toute perte de device
        // (Alt-Tab, changement de résolution) même si Init() les avait bien posés.
        ConfigureSamplerStates();
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

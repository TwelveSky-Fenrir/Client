// Gfx/Renderer.h — device Direct3D9 du moteur GXD.
// Fidèle à Gfx_InitDevice 0x69B9B0 / GXD_DeviceReinit 0x4023F0 / GXD_BeginScene 0x404640
// / Gfx_Present 0x69E270 (voir Docs/TS2_GXD_ENGINE.md).
// ex-VeryOldClient: Core/GXD (v1 / Object A = g_GfxRenderer 0x7FFE18) — le CRÉATEUR du device
//   physique (Direct3DCreate9 + CreateDevice), l'une des deux classes GXD homonymes ; l'autre
//   (v2 TW2AddIn::GXD = Object B 0x18C4EF8) RÉUTILISE ce device, cf. Gfx/GxdRenderer.h.
//   CONFIRMED Docs/TS2_GXD_ROSETTA.md §1.1/§3.
// N'utilise QUE le Direct3D9 du Windows SDK (pas de D3DX legacy) : la math passe par
// DirectXMath, les shaders par d3dcompiler, sprite/police seront réimplémentés.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>

namespace ts2::gfx {

class Renderer {
public:
    ~Renderer();

    // Crée le device (D3DDEVTYPE_HAL, HW vertex processing + fallback SOFTWARE).
    bool Init(HWND hwnd, int width, int height, bool windowed);
    void Shutdown();

    bool BeginFrame(); // Clear(TARGET|ZBUFFER) + BeginScene (Gfx_BeginFrame)
    void EndFrame();   // EndScene + Present (Gfx_Present)

    // --- Observateurs de perte/restauration de device (Gfx_HandleDeviceLostReset 0x69DD40) ---
    // Signature d'un observateur ; `user` est le pointeur opaque fourni à SetDeviceCallbacks.
    using DeviceNotifyFn = void (*)(void* user);

    // Enregistre les observateurs appelés AUTOUR de IDirect3DDevice9::Reset.
    // Ancre : Gfx_HandleDeviceLostReset 0x69DD40. Dans le binaire, le renderer (Object A =
    // g_GfxRenderer 0x7FFE18) POSSÈDE directement l'ID3DXEffect (+620), l'ID3DXFont (+612)
    // et l'ID3DXSprite (+608), et appelle lui-même leurs OnLostDevice (@0x69DE3E) puis, après
    // le Reset (@0x69DE55), leurs OnResetDevice (@0x69DE9B). Côté ClientSource ces objets D3DX
    // appartiennent aux couches supérieures (SceneManager -> LoginScene/GameHud/GameWindows ->
    // Font/SpriteBatch) : la transposition passe donc par un observateur.
    // ⚠ SANS enregistrement, Reset() est appelé sans notifier les ID3DXSprite/ID3DXFont et la
    // restauration après perte de device (Alt+Tab, changement de résolution) est IMPOSSIBLE.
    //
    // CÂBLAGE REQUIS (hors de ce front — à poser par l'orchestrateur dans App.cpp) : ce hook
    // est INERTE tant que personne ne l'enregistre (onLost_/onReset_ restent nullptr => la
    // chaîne OnLostDevice/OnResetDevice ne tourne jamais = code mort). Le point d'attache est
    // ts2::App::Init, JUSTE APRÈS le bloc GxdRenderer::DeviceReinit qui réussit (App.cpp:379,
    // après l'accolade fermante ligne 379), sur la chaîne ts2::SceneManager::OnDeviceLost/
    // OnDeviceReset (SceneManager.h:98-99, publiques) :
    //     renderer_.SetDeviceCallbacks(
    //         [](void* u){ static_cast<SceneManager*>(u)->OnDeviceLost();  },
    //         [](void* u){ static_cast<SceneManager*>(u)->OnDeviceReset(); },
    //         &scene_);
    // (lambdas sans capture -> conversion en DeviceNotifyFn ; `scene_` = membre App.h:42.)
    void SetDeviceCallbacks(DeviceNotifyFn onLost, DeviceNotifyFn onReset, void* user);

    IDirect3D9*      D3D() const { return d3d_; }
    IDirect3DDevice9* Device() const { return device_; }
    bool Ready() const { return device_ != nullptr; }
    void SetClearColor(uint32_t argb) { clearColor_ = argb; }

private:
    // Gfx_HandleDeviceLostReset 0x69DD40 — appelée INCONDITIONNELLEMENT en tête de frame
    // (le binaire l'appelle en tête de CHAQUE Scene_*Render : 0x518894, 0x519274, 0x51B044,
    // 0x51CEF4, 0x52C284, 0x52D10D/0x52D15A/0x52D1C5 — 8 xrefs).
    // CONTRAT DE RETOUR (contre-intuitif, fidèle au binaire) :
    //   true  <=> TestCooperativeLevel a renvoyé D3D_OK (@0x69DD79, seul `return 1`) -> on rend.
    //   false dans TOUS les autres cas, Y COMPRIS après un Reset() RÉUSSI (LABEL_42 @0x69E251 :
    //         *a5=0 puis `return 0`) : la frame du reset est SAUTÉE, on rend à la suivante.
    bool HandleDeviceLost();

    // Pose les sampler states (stages 0/1) + render states initiaux EXACTS d'Object A,
    // tirés directement de Gfx_InitDevice 0x69c470..0x69c543 (LINEAR fixe, PAS anisotrope ;
    // l'ancienne ancre GXD_ConfigSamplerStates 0x403B50 appartenait à Object B / GxdRenderer).
    // Sampler/render states ne survivent pas à Reset() : rappelée après CreateDevice ET après
    // chaque Reset() réussi (HandleDeviceLost).
    void ApplyInitialDeviceStates();

    IDirect3D9*           d3d_        = nullptr; // pD3D9 @+240 (0x7FFF08)   ex-VeryOldClient: mDirect3D
    IDirect3DDevice9*     device_     = nullptr; // pDevice @+604 (0x800074) ex-VeryOldClient: mGraphicDevice
    D3DPRESENT_PARAMETERS pp_         = {};      // ex-VeryOldClient: mGraphicPresentParameters (PLAUSIBLE)
    uint32_t              clearColor_ = 0x00000000; // noir pur (ARGB), fidèle au clear d'origine

    // Observateurs de perte/restauration (cf. SetDeviceCallbacks). Non possédés.
    // Tiennent lieu des appels directs OnLostDevice/OnResetDevice sur Effect(+620)/
    // Font(+612)/Sprite(+608) de Gfx_HandleDeviceLostReset 0x69DD40.
    DeviceNotifyFn        onLost_     = nullptr;
    DeviceNotifyFn        onReset_    = nullptr;
    void*                 notifyUser_ = nullptr;

    // NB : il n'existe PLUS de drapeau `deviceLost_`. Le binaire ne mémorise AUCUN état de
    // perte : Gfx_Present 0x69E270 appelle EndScene (vtbl+168) puis Present (vtbl+68) et
    // n'inspecte JAMAIS le HRESULT de Present ; la détection passe exclusivement par le
    // TestCooperativeLevel (vtbl+12 @0x69DD4C) fait en tête de chaque frame. L'ancien
    // drapeau (posé par EndFrame sur D3DERR_DEVICELOST, lu par BeginFrame) était une
    // invention locale qui, en prime, empêchait toute reprise si la perte survenait
    // ailleurs qu'au Present.
};

} // namespace ts2::gfx

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

    IDirect3D9*      D3D() const { return d3d_; }
    IDirect3DDevice9* Device() const { return device_; }
    bool Ready() const { return device_ != nullptr; }
    void SetClearColor(uint32_t argb) { clearColor_ = argb; }

private:
    bool HandleDeviceLost(); // GXD_OnDeviceLost/GXD_RestoreAfterReset (partiel)

    // Réplique GXD_ConfigSamplerStates 0x403B50 (branche par défaut this[2]==0 =>
    // anisotrope, cf. Gfx/GxdRenderer.cpp) directement sur le device du Renderer.
    // Les états d'échantillonnage NE SURVIVENT PAS à un Reset() D3D9 : appelée après
    // CreateDevice ET après chaque Reset() réussi (HandleDeviceLost).
    void ConfigureSamplerStates();

    IDirect3D9*           d3d_        = nullptr; // pD3D9 @+240 (0x7FFF08)   ex-VeryOldClient: mDirect3D
    IDirect3DDevice9*     device_     = nullptr; // pDevice @+604 (0x800074) ex-VeryOldClient: mGraphicDevice
    D3DPRESENT_PARAMETERS pp_         = {};      // ex-VeryOldClient: mGraphicPresentParameters (PLAUSIBLE)
    uint32_t              clearColor_ = 0x00000000; // noir pur (ARGB), fidèle au clear d'origine
    bool                  deviceLost_ = false;
};

} // namespace ts2::gfx

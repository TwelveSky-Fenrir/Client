// UI/EnterWorldRender.h — VISUAL RENDERING of the EnterWorld transition screen (ts2::ui).
//
// AUDIT 2026-07-14 (re-verified): Scene::EnterWorld had NO case in SceneManager::Render()
// (Scene/SceneManager.cpp, write-forbidden for this mission) — fell to `default` ("screen
// cleared by Renderer::BeginFrame"). During the whole CharSelect->InGame transition (200+
// zone-load frames, up to 5000 waiting on server ACK, cf. Game/EnterWorldFlow.h) the
// rewritten client showed an EMPTY screen (device clear color, not even guaranteed black),
// vs. a REAL loading screen in the original binary.
//
// This module (already written) is now INSTANTIATED and wired via LoginScene (allowed
// file, SAME pattern as IntroRender — cf. UI/LoginScene.h::enterWorldRender_ +
// UI/LoginScene.cpp::LoginScene::RenderEnterWorld): device_ set in LoginScene::Init(),
// render invoked by LoginScene::RenderEnterWorld(state, zoneId). ONLY missing link, STILL
// to apply manually in the write-forbidden Scene/SceneManager.cpp (2026-07-14 audit
// report, EnterWorldRender mission, for the exact patch): add a `case Scene::EnterWorld:`
// in SceneManager::Render() calling
// `login_->RenderEnterWorld(enterWorldState_, game::g_World.zoneId);`.
//
// Faithful rewrite of the real GEOMETRY of Scene_EnterWorldRender 0x52C260 (~930 bytes),
// decompiled via idaTs2 (JSON-RPC HTTP server http://127.0.0.1:13337/mcp, "decompile"
// method — the `idaTs2` MCP tool was unreachable directly during this audit, HTTP JSON-RPC
// fallback used, SAME IDB, no invented data).
//
// === REAL GEOMETRY EXTRACTED (0x52C260) ===
//
//   if (*(this + 1) == 0)   // EnterWorldState::WaitBeforeUnload (state 0, ~30 frames)
//       // NOTHING drawn (just Gfx_Begin2D/End2D/Present) — bare screen, SAME pattern as
//       // IntroRender::Render() for subState==0 (cf. UI/IntroRender.h).
//   else {
//       // Full-screen background = loading image SPECIFIC TO THE TARGET ZONE, centered on
//       // ITS real size (Sprite2D_GetWidth/Height), SEPARATE atlas (NOT unk_8E8B50/001):
//       //   unk_A649B8 + 148 * previousZoneId  (this+15727, = zoneId-1)
//       // Resolved via AssetMgr_InitAllSlots 0x4DEB50 audit (build category 7,
//       // kk=0..349) + Sprite2D_BuildPath 0x4D68E0 (case 7 -> folder "008", file
//       // "008_%05d.IMG", index = a3+1): the real file loaded for this previousZoneId
//       // slot is thus "008_%05d.IMG" with index = previousZoneId+1 = zoneId.
//       //   -> CONFIRMED REAL PATH: G03_GDATA/D01_GIMAGE2D/008/008_%05d.IMG, index=zoneId.
//       baseX = nWidth/2  - bgW/2;  baseY = nHeight/2 - bgH/2;
//       Sprite2D_Draw(bg, baseX, baseY);
//
//       // Text: StrTable003_Get(dword_84A6A8, zoneId) + StrTable005_Get(g_LangId, 69),
//       // 2 UI_DrawNumberValue batches (dedicated bitmap numeric font, NOT the normal UI
//       // font) centered around (baseX+363, baseY+475). Reproduced here by a SINGLE line
//       // of text via ctx.Text (normal font, not the exact bitmap numeric rendering) —
//       // PARTIAL fidelity assumed (TODO: reproduce UI_DrawNumberValue/
//       // UI_MeasureNumberText 0x53FCC0/0x53FCA0 if pixel-exactness is ever needed).
//
//       // Progress bar: 21 sprites (atlas unk_8E8B50/001, SAME atlas as
//       // Intro/ServerSelect), slot = 1140 + clamp(zoneResourceIndex, 0, 20), drawn at
//       // (baseX+123, baseY+504). zoneResourceIndex = EnterWorldFlowState::
//       // zoneResourceIndex (this+15726), advances 0 to 20 during LoadZoneResources
//       // (10 frames per increment, cf. Game/EnterWorldFlow.h): this is a REAL PROGRESS
//       // ANIMATION (21 distinct frames), not a simple static spinner.
//
//       UI_RenderAllDialogs();  // error notices (Failed) rendered ON TOP, as
//                                // everywhere else (cf. GameHud/GameWindows) — delegated
//                                // to ClientRuntime::PromptState on the ClientSource side,
//                                // NOT duplicated here (same hooks SceneManager.cpp already
//                                // wires for host.ShowErrorNotice).
//   }
//
// SCOPE: drawing only, from game::EnterWorldFlowState (Game/EnterWorldFlow.h, already
// written) READ ONLY + zoneId (supplied by the caller, like EnterWorldFlow_Update). No
// mouse/keyboard interaction (faithful: neither the original Update nor Render test input
// during this transition).
#pragma once
#include "UI/UIManager.h"        // ts2::ui::UiContext
#include "Game/EnterWorldFlow.h" // ts2::game::EnterWorldFlowState/EnterWorldState
#include "Gfx/GpuTexture.h"      // gfx::GpuTexture (zone background + bar, real atlases)
#include <unordered_map>

namespace ts2::ui {

namespace enterworld_layout {

// NOMINAL dimensions of the fallback background (Sprite2D_GetWidth/Height of the real
// background once loaded; these nominals are used ONLY if 008_%05d.IMG is unavailable).
constexpr int kBgW = 1024;
constexpr int kBgH = 768;

// REAL offsets recorded in Scene_EnterWorldRender (relative to baseX/baseY = top-left
// corner of the background, cf. header comment above).
constexpr int kTextOffsetX = 363; // EA 0x52c3fd/0x52c426 (center of the "number/label" block)
constexpr int kTextOffsetY = 475;
constexpr int kBarOffsetX  = 123; // EA 0x52c5c8 (Sprite2D_Draw of the progress bar)
constexpr int kBarOffsetY  = 504;

// Progress bar: 21 frames (atlas unk_8E8B50/001, slots 1140..1160), driven by
// EnterWorldFlowState::zoneResourceIndex (0..20). EA 0x52c593/0x52c59d.
constexpr int kBarFrameBase = 1140;
constexpr int kBarFrameCap  = 1160;

inline int BarFrameSlot(int zoneResourceIndex) {
    const int v = kBarFrameBase + zoneResourceIndex;
    return v > kBarFrameCap ? kBarFrameCap : v;
}

} // namespace enterworld_layout

// EnterWorldRender — draws the EnterWorld transition screen from a read-only
// game::EnterWorldFlowState + the target zoneId (same parameter as
// EnterWorldFlow_Update). No mouse/keyboard interaction.
class EnterWorldRender {
public:
    // Must be called ONCE after D3D9 device creation (same pattern as
    // IntroRender::SetDevice / ServerSelectRender::SetDevice): without a device,
    // GetBackground/GetBarFrame cannot load any real texture and Render() will
    // systematically fall back to flat colors.
    void SetDevice(IDirect3DDevice9* device) { device_ = device; }

    // Called twice per frame by the scene driver (once per UiPhase, like
    // IntroRender::Render / ServerSelectRender::Render); ctx.FillRect/Text already
    // filters internally on ctx.phase. `zoneId` = same value passed to
    // EnterWorldFlow_Update (originally dword_1675A9C, NOT re-read from a global here).
    void Render(const UiContext& ctx, const game::EnterWorldFlowState& state, int zoneId);

private:
    // Zone background (folder "008", cf. header comment): lazy cache keyed by
    // zoneId (not previousZoneId — the file-index and slot-index calculations
    // cancel out, cf. audit above: slot=zoneId-1, file=slot+1=zoneId).
    gfx::GpuTexture* GetBackground(int zoneId);

    // Progress bar (atlas unk_8E8B50/"001", SAME folder as IntroRender/
    // ServerSelectRender, IDENTICAL +1 slot->file offset — cf. their comments).
    gfx::GpuTexture* GetBarFrame(int slotIndex);

    IDirect3DDevice9* device_ = nullptr;
    std::unordered_map<int, gfx::GpuTexture> bgCache_;  // zoneId -> background texture (folder 008)
    std::unordered_map<int, gfx::GpuTexture> barCache_; // slot -> bar texture (folder 001)
};

} // namespace ts2::ui

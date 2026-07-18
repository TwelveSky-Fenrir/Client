// UI/IntroRender.h — VISUAL RENDERING of the Intro/logos screen (ts2::ui).
//
// Faithful rewrite of the actual GEOMETRY of Scene_IntroRender 0x518880 (~560 bytes),
// decompiled via idaTs2 (Scene_IntroRender 0x518880 / Scene_IntroUpdate 0x517FE0;
// IDB TwelveSky2.exe, read-only, no invented data).
//
// === KEY FINDING (corrects a speculation in Game/IntroFlow.h) ===
// Scene_IntroRender NEVER reads the logoFade buffer (this[3..152], 150 dwords) — it
// appears in NO instruction of the decompiled body. The IntroFlow.h speculation
// ("likely drives a logo fade/crossfade") is therefore FALSE in the alpha-fade sense:
// there is NEITHER a fade NOR a logoFade read in the rendering. The actual mechanism is a
// DISCRETE SPRITE SEQUENCE (one full-opacity sprite per micro-state, NO alpha-blend/
// cross-fade):
//   if (this[1] == 0)                    -> nothing drawn (bare screen, Init/wait sub-state).
//   else {
//       v8 = this[1] + 797;              // EA 0x518A0B
//       if (v8 > 830) v8 = 830;          // EA 0x518A15-0x518A17 (cap)
//       centered (nWidth/2 - w/2, nHeight/2 - h/2) on unk_8E8B50 + 148*v8 (EA 0x518A8F)
//   }
// this[1] = subState (game::IntroState::subState, 0..34). For subState=1..33
// (kIntroLogoStepCount micro-states, IntroFlow.h), v8 sweeps EXACTLY 798..830 (33
// distinct values — numerically confirms kIntroLogoStepCount=33; each 3-frame/0.1s
// micro-state shows a DIFFERENT atlas sprite, with no visual transition between them).
// For subState=34 (final hold, kIntroFinalHoldFrames=90 frames), v8=831 clamped to 830
// -> the LAST logo (830, identical to subState=33) stays on screen through the entire
// final hold. subState=0 (initial wait, kIntroWaitFrames=90 frames) -> BLACK screen,
// NOTHING drawn by this function: the black background comes from the device clear
// (Gfx_BeginFrame -> g_GfxRenderer+1308 = 0, see IntroRender.cpp) and from INTRO.AVI
// which precedes it, outside the scope of Scene_IntroRender itself.
//
// SCOPE: drawing only, from game::IntroState (Game/IntroFlow.h, already written),
// READ-ONLY.
//
// === ACTUAL WIRING ("REAL INTRO LOGO" mission, 2026-07-14, Docs/TS2_INTRO_LOGO_ASSETS.md) ===
// The REAL .IMG sprite atlas is loaded: `path = "G03_GDATA/D01_GIMAGE2D/001/001_%05d.IMG"
// % (slotIndex+1)` via asset::ImgFile::Load + gfx::GpuTexture::CreateFromImgFile (same
// pattern as UI/PanelSkin.h / UI/InventoryWindow.cpp), lazily cached by slotIndex in
// `logoCache_` (at most 33 entries, one per sub-state 1..33 — subState 34 reuses the
// already-cached slot 830). Drawn via SpriteBatch::DrawSprite with a logical source RECT,
// not the whole rounded D3D9 surface: Sprite2D_Draw 0x4D6B72 calls UI_DrawSprite 0x6A3080
// on `this+104`, and UI_DrawSprite 0x6A3093..0x6A30C5 builds the RECT [0,width]×[0,height].
// Centered on ITS actual size: 668×229 for files 001_00799..001_00831.IMG, giving
// (178,270) at 1024×768 via the integer divisions in Scene_IntroRender 0x518A28/0x518A48
// and 0x518A55/0x518A75. ZERO fallback: if the file is genuinely missing/unreadable at
// runtime, NOTHING is drawn (matches Sprite2D_Draw failing silently) — no colored fallback
// fill or diagnostic label whatsoever.
#pragma once
#include "UI/UIManager.h"    // ts2::ui::UiContext
#include "Game/IntroFlow.h"  // ts2::game::IntroState
#include "Gfx/GpuTexture.h"  // gfx::GpuTexture (real logo, atlas unk_8E8B50)
#include <unordered_map>

namespace ts2::ui {

namespace intro_layout {

// (kLogoW/kLogoH removed 2026-07-15: dead constants from the old fallback fill, never
//  referenced. The real sprite is loaded from 001_%05d.IMG (668×229, DXT1) and centered on
//  ITS actual size — see IntroRender.cpp.)

// v8 = this[1] + 797, capped at 830 (EA 0x518A0B/0x518A15/0x518A17). Faithful even for
// subState==0 (though Render never calls it in that case — see Render below).
constexpr int kLogoSpriteBase = 798; // Scene_IntroRender 0x518A0B: subState==1 -> 1+797
constexpr int kLogoSpriteCap  = 830; // Scene_IntroRender 0x518A15/0x518A17: cap

inline int LogoSpriteIndex(int subState) {
    const int v = subState + 797; // Scene_IntroRender 0x518A0B
    return v > kLogoSpriteCap ? kLogoSpriteCap : v;
}

} // namespace intro_layout

// ---------------------------------------------------------------------------
// IntroRender — draws the Intro screen from a read-only game::IntroState. No
// mouse/keyboard interaction (faithful: Scene_IntroRender/Update test no input)
// -> no visual latch, just a lazy texture cache (33 logos max, negligible memory).
// ---------------------------------------------------------------------------
class IntroRender {
public:
    // Called twice per frame by the scene driver (once per UiPhase, like
    // Dialog::Render / MsgBoxDialog::Render); ctx.FillRect/Text already filter
    // internally on ctx.phase.
    void Render(const UiContext& ctx, const game::IntroState& state);

    // FIDELITY BUG FIXED (2026-07-14 runtime check, screenshot): `ctx.renderer`
    // (UI/UIManager.h) was NEVER populated by LoginScene::RenderIntro() (only caller),
    // so GetLogoSprite() always fell back to a colored fill+label ("Logo #NNN") — the
    // real logo never rendered despite loading fine in isolation
    // (Asset/AssetSelfTest.cpp, which DOES populate ctx.renderer). Same pattern as the
    // fix already applied to ServerSelectRender::SetDevice(): device stored internally,
    // independent of ctx.renderer. Caller MUST call SetDevice() once the device is
    // created (LoginScene::Init(), same place as serverSelectRender_.SetDevice()).
    void SetDevice(IDirect3DDevice9* device) { device_ = device; }

private:
    // Resolves a slot in the unk_8E8B50 atlas (AssetMgr_InitAllSlots 0x4deb50, category 1 ->
    // "G03_GDATA\D01_GIMAGE2D\001\001_%05d.IMG") to its GPU texture, lazy cache.
    // +1 OFFSET CONFIRMED by direct decompile (Sprite2D_BuildPath 0x4d68e0 formats
    // the file with `slot+1`) AND by the actual content: the logo sequence (slots
    // 798..830) matches EXACTLY the 33 files 001_00799..001_00831.IMG (668x229
    // DXT1, uniform), not 001_00798..001_00830.IMG.
    gfx::GpuTexture* GetLogoSprite(const UiContext& ctx, int slotIndex);

    IDirect3DDevice9* device_ = nullptr; // see SetDevice() — required to load real .IMG files
    std::unordered_map<int, gfx::GpuTexture> logoCache_; // slot -> texture (lazy, <=33 entries)
};

} // namespace ts2::ui

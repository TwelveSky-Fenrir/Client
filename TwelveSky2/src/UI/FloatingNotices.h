// UI/FloatingNotices.h — HUD in-game floating notices (13 typed slots, 10 s).
//
// FAITHFUL rewrite of the TwelveSky2 `dword_1821D58` singleton object, whose
// only TWO methods are:
//   HUD_ShowFloatingMessage    0x5AEEC0  arms a slot (type 0..12), timestamps, turns
//                                        off competing slots, plays a sound;
//   HUD_RenderFloatingMessages 0x5AF4C0  draws the active slots (10 s duration, NO
//                                        fade: hard cutoff).
// Called from UI_RenderAllDialogs 0x5AE2D0: HUD_RenderFloatingMessages @0x5AE5A7
// (this = dword_1821D58) RIGHT BEFORE UI_SysMsgList_Render 0x5AEC80 @0x5AE5B9
// (this = dword_1822350) — ChatWindow::Render reproduces this exact ORDER.
//
// PROVEN LAYOUT of dword_1821D58 (offsets recorded in both functions):
//   +0     int   scratchX      output of UI_ProjectSpriteToScreen 0x50F5D0 (@0x5AF59A)
//   +4     int   scratchY      same — per-frame scratch, NOT modeled here (local variable)
//   +8     int   active[13]    slot i -> +8+4*i        (@0x5AEEE0 writes 1 / @0x5AF52B reads)
//   +60    char  text[14][101] slot i -> +60+101*i     (@0x5AEF0B / @0x5AF5E3)
//                              the 14th (+1373) = type 12's 2nd line (@0x5AEF1F / @0x5AFCFF)
//   +1476  float ts[13]        slot i -> +1476+4*i     (@0x5AEF3A write / @0x5AF552 read)
// Arithmetic consistency verified: 60 + 101*13 = 1373 ; 1476 + 4*12 = 1524.
//
// WARNING (fixed pitfall): `this+1524` is NOT a distinct field — it IS ts[12]
// itself (1476 + 4*12). HUD_ShowFloatingMessage writes ts[type] = g_GameTimeSec
// (@0x5AEF3A) then, for type 12 ONLY, adds dbl_7A7368 = 20.0 (@0x5AEF60, bytes
// verified `00 00 00 00 00 00 34 40`). Type 12's timestamp is thus POSTDATED by
// +20 s; since the render tests `now - ts <= 10.0`, type 12's real lifetime is
// 30 s, not 10 s.
//
// SCOPE / ASSUMED DEVIATIONS:
//   - SOUND: HUD_ShowFloatingMessage picks a sound via `subType` (sub-switch
//     @0x5AEFF5/0x5AF06D/0x5AF117/0x5AF18B/0x5AF2CB/0x5AF351 -> Snd3D_PlayScaledVolume
//     0x4DA380). NOT reproduced here (none of the 34 `flt_14xxxxx` sound-bank
//     addresses are resolved to a file); `subType` is accepted and kept in the
//     signature to stay faithful to the call contract. NB: in the binary, the
//     `default: return;` of these sub-switches cancels NOTHING of the slot state
//     (the active/ts/text/extinction writes all precede it) — omitting the sound
//     therefore has no effect on rendering.
//   - The binary has NO graphical fallback: if the sprite isn't loaded, nothing is
//     drawn. Kept faithful here (no substitute colored rect): text only.
//
// INCLUSION NOTE: header DELIBERATELY LIGHT (no <d3d9.h>/<d3dx9.h>/<winsock2.h>)
// — it is included by UI/ChatWindow.h, whose banner guarantees this property. GPU
// resources therefore sit behind an opaque PIMPL (`struct Gpu`), same convention
// as Scene/SceneManager.h (concrete scenes held via unique_ptr so as not to pull
// d3dx9 into includers).
#pragma once
#include <array>
#include <memory>
#include <string>

namespace ts2::gfx { class SpriteBatch; class Font; class GpuTexture; }

namespace ts2::ui {

class FloatingNotices {
public:
    // 13 typed slots (guard `type < 0 || type > 12` @0x5AEECD/@0x5AEED3 ; loop
    // `for i in [0,13)` @0x5AF509).
    static constexpr int kSlotCount = 13;
    // Per-slot text buffer: 101 bytes NUL included (Crt_Memset ..., 0x65 @0x5AEEF6).
    static constexpr int kTextLen = 101;
    // Lifetime: `g_GameTimeSec - ts <= 10.0` @0x5AF552, else slot = 0 @0x5AF55A.
    static constexpr float kLifetimeSec = 10.0f;
    // Type 12 postdating: ts[12] += 20.0 @0x5AEF60 (dbl_7A7368) -> 30 s useful life.
    static constexpr float kType12TimeBonus = 20.0f;
    // Scene sub-state required by the guard @0x5AF4DA (g_SceneSubState 0x1676184 == 4
    // = MainTick, cf. Scene/SceneManager.h).
    static constexpr int kSubStateMainTick = 4;

    FloatingNotices();
    ~FloatingNotices();
    FloatingNotices(const FloatingNotices&)            = delete;
    FloatingNotices& operator=(const FloatingNotices&) = delete;

    // HUD_ShowFloatingMessage 0x5AEEC0. `type` 0..12 (out of range -> silently
    // ignored, @0x5AEED5). `subType` = SOUND selector only
    // (not reproduced, cf. banner); kept for call-contract fidelity.
    // `text2` = 2nd line, used by type 12 only (@0x5AFCFF); UNCONDITIONALLY
    // reset on every call (Crt_Memset @0x5AEEF6 precedes the type test).
    void Show(int type, int subType, const std::string& text,
              const std::string& text2 = std::string());

    // HUD_RenderFloatingMessages 0x5AF4C0. `nowSec` = g_GameTimeSec (flt_815180) ;
    // `screenW/screenH` = screen dimensions (fields +20/+24 of g_PlayerCmdController
    // 0x1669170 read by UI_ProjectSpriteToScreen 0x50F5D0).
    void Render(gfx::SpriteBatch& sprites, gfx::Font& font, float nowSec,
                int screenW, int screenH);

private:
    struct Gpu; // opaque PIMPL (gfx::GpuTexture) — cf. "INCLUSION NOTE" above

    // Clears the 13 slots (`else` branch of the scene guard, @0x5AF4FA).
    void ClearAll();

    // UI_ProjectSpriteToScreen 0x50F5D0: anchors the sprite's CENTER at the same
    // screen fraction as its design position (the sprite itself is NOT scaled).
    static void Project(int designX, int designY, int spriteW, int spriteH,
                        int screenW, int screenH, int& outX, int& outY);

    // Lazily loads `001_%05d.IMG` (745 / 1028); nullptr if unavailable
    // (-> no sprite drawn, faithful: the binary has no fallback).
    // `which`: 0 = sprite idx 744 (types 0..11), 1 = sprite idx 1027 (type 12).
    gfx::GpuTexture* EnsureTexture(gfx::SpriteBatch& sprites, int which);

    std::array<int, kSlotCount>          active_{};  // +8+4*i
    std::array<float, kSlotCount>        ts_{};      // +1476+4*i
    std::array<std::string, kSlotCount>  text_{};    // +60+101*i
    std::string                          text2_;     // +1373 (type 12's 2nd line)

    std::unique_ptr<Gpu> gpu_;
};

} // namespace ts2::ui

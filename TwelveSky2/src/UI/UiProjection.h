// UI/UiProjection.h — design->screen projection (UI_ProjectSpriteToScreen 0x50F5D0).
//
// Faithful port of UI_ProjectSpriteToScreen 0x50F5D0 (this = g_PlayerCmdController 0x1669170):
// projects a DESIGN-space point (reference resolution 1024x768) to the real screen, anchored on
// the sprite half-size. Used by the 2 ROTATION buttons of the CharSelect creation preview
// (render Scene_CharSelectRender EA 0x51EC2F/0x51EC88; hit-test
// Scene_CharSelectOnMouseDown EA 0x522DB1/0x522E09).
//
// Formula (decompiled) — TRUNCATION toward zero (Crt_ftol 0x760810), NOT a round; W/2 = integer
// floor (sar) added to the numerator as int32:
//   outX = trunc( (worldX + floor(W/2)) * nWidth  / refW ) - floor(W/2)  // W = Sprite2D_GetWidth(atlas+148*slot)
//   outY = trunc( (worldY + floor(H/2)) * nHeight / refH ) - floor(H/2)  // H = Sprite2D_GetHeight(atlas+148*slot)
// where nWidth/nHeight = ctrl[+20]/ctrl[+24] (= g_ScreenW 0x1669184 / g_ScreenH 0x1669188) and
// refW/refH = ctrl[+8]/ctrl[+12] (= flt_1669178 / flt_166917C, design REFERENCE dims).
// Both floats are written AT RUNTIME by WinMain (0 statically) but PROVEN = 1024/768
// (same constants as the fullscreen background scaling, DrawFullscreenBg: nW/1024,
// nH/768). The module stays decoupled: nWidth/nHeight (and refW/refH) are passed by the caller.
#pragma once
#include <windows.h>  // POINT

namespace ts2::ui {

// Design REFERENCE dims (flt_1669178 = 1024.0 / flt_166917C = 768.0, cf. header banner).
inline constexpr int kUiDesignWidth  = 1024;
inline constexpr int kUiDesignHeight = 768;

// UI_ProjectSpriteToScreen 0x50F5D0: `world` = design-space point (anchors the sprite's
// top-left corner); the return value is the top-left corner on screen. refW/refH <= 0 ->
// falls back to 1024/768.
inline POINT ProjectDesignAnchor(int spriteW, int spriteH, int worldX, int worldY,
                                 int screenW, int screenH,
                                 int refW = kUiDesignWidth, int refH = kUiDesignHeight) {
    if (refW <= 0) refW = kUiDesignWidth;
    if (refH <= 0) refH = kUiDesignHeight;
    // INTEGER half-size: `cdq ; sub eax,edx ; sar eax,1` @0x50F5F1-0x50F5F4 = floor(W/2)
    // (W >= 0, Width() unsigned), added TO THE NUMERATOR as int32 (`add eax` @0x50F5F6) — NOT a
    // float half. The `static_cast<LONG>(double)` cast TRUNCATES toward zero (= Crt_ftol 0x760810:
    // cvttsd2si / (__int64)), it is NOT a round (std::lround was the fidelity gap).
    const int halfW = spriteW / 2; // sar eax,1 @0x50F5F4
    const int halfH = spriteH / 2;
    POINT p{0, 0};
    p.x = static_cast<LONG>(
              static_cast<double>((worldX + halfW) * screenW) / static_cast<float>(refW)) - halfW;
    p.y = static_cast<LONG>(
              static_cast<double>((worldY + halfH) * screenH) / static_cast<float>(refH)) - halfH;
    return p;
}

} // namespace ts2::ui

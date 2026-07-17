// UI/UiProjection.h — projection design->écran (UI_ProjectSpriteToScreen 0x50F5D0).
//
// Port fidèle de UI_ProjectSpriteToScreen 0x50F5D0 (this = g_PlayerCmdController 0x1669170) :
// projette un point de DESIGN (résolution de référence 1024x768) vers l'écran réel, ancré sur
// la demi-taille du sprite. Utilisé par les 2 boutons de ROTATION de l'aperçu de création
// CharSelect (rendu Scene_CharSelectRender EA 0x51EC2F/0x51EC88 ; hit-test
// Scene_CharSelectOnMouseDown EA 0x522DB1/0x522E09).
//
// Formule (décompilée) — TRONCATURE vers zéro (Crt_ftol 0x760810), PAS un arrondi ; W/2 = floor
// entier (sar) ajouté au numérateur en int32 :
//   outX = trunc( (worldX + floor(W/2)) * nWidth  / refW ) - floor(W/2)  // W = Sprite2D_GetWidth(atlas+148*slot)
//   outY = trunc( (worldY + floor(H/2)) * nHeight / refH ) - floor(H/2)  // H = Sprite2D_GetHeight(atlas+148*slot)
// où nWidth/nHeight = ctrl[+20]/ctrl[+24] (= g_ScreenW 0x1669184 / g_ScreenH 0x1669188) et
// refW/refH = ctrl[+8]/ctrl[+12] (= flt_1669178 / flt_166917C, dims de RÉFÉRENCE design).
// Ces deux floats sont écrits AU RUNTIME par WinMain (0 en statique) mais PROUVÉS = 1024/768
// (mêmes constantes que la mise à l'échelle du fond plein écran, DrawFullscreenBg : nW/1024,
// nH/768). Le module reste découplé : nWidth/nHeight (et refW/refH) sont passés par l'appelant.
#pragma once
#include <windows.h>  // POINT

namespace ts2::ui {

// Dimensions de RÉFÉRENCE design (flt_1669178 = 1024.0 / flt_166917C = 768.0, cf. bandeau).
inline constexpr int kUiDesignWidth  = 1024;
inline constexpr int kUiDesignHeight = 768;

// UI_ProjectSpriteToScreen 0x50F5D0 : `world` = point de design (ancre coin haut-gauche du
// sprite) ; le retour est le coin haut-gauche à l'écran. refW/refH <= 0 -> repli 1024/768.
inline POINT ProjectDesignAnchor(int spriteW, int spriteH, int worldX, int worldY,
                                 int screenW, int screenH,
                                 int refW = kUiDesignWidth, int refH = kUiDesignHeight) {
    if (refW <= 0) refW = kUiDesignWidth;
    if (refH <= 0) refH = kUiDesignHeight;
    // Demi-taille ENTIÈRE : `cdq ; sub eax,edx ; sar eax,1` @0x50F5F1-0x50F5F4 = floor(W/2)
    // (W >= 0, Width() non signé), ajoutée AU NUMÉRATEUR en int32 (`add eax` @0x50F5F6) — PAS un
    // demi-flottant. Le cast `static_cast<LONG>(double)` TRONQUE vers zéro (= Crt_ftol 0x760810 :
    // cvttsd2si / (__int64)), ce n'est PAS un arrondi (std::lround était l'écart de fidélité).
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

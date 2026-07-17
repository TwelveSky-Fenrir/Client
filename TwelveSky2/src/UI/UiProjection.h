// UI/UiProjection.h — projection design->écran (UI_ProjectSpriteToScreen 0x50F5D0).
//
// Port fidèle de UI_ProjectSpriteToScreen 0x50F5D0 (this = g_PlayerCmdController 0x1669170) :
// projette un point de DESIGN (résolution de référence 1024x768) vers l'écran réel, ancré sur
// la demi-taille du sprite. Utilisé par les 2 boutons de ROTATION de l'aperçu de création
// CharSelect (rendu Scene_CharSelectRender EA 0x51EC2F/0x51EC88 ; hit-test
// Scene_CharSelectOnMouseDown EA 0x522DB1/0x522E09).
//
// Formule (décompilée) :
//   outX = round( nWidth  * (worldX + W/2) / refW ) - W/2      // W = Sprite2D_GetWidth(atlas+148*slot)
//   outY = round( nHeight * (worldY + H/2) / refH ) - H/2      // H = Sprite2D_GetHeight(atlas+148*slot)
// où nWidth/nHeight = ctrl[+20]/ctrl[+24] (= g_ScreenW 0x1669184 / g_ScreenH 0x1669188) et
// refW/refH = ctrl[+8]/ctrl[+12] (= flt_1669178 / flt_166917C, dims de RÉFÉRENCE design).
// Ces deux floats sont écrits AU RUNTIME par WinMain (0 en statique) mais PROUVÉS = 1024/768
// (mêmes constantes que la mise à l'échelle du fond plein écran, DrawFullscreenBg : nW/1024,
// nH/768). Le module reste découplé : nWidth/nHeight (et refW/refH) sont passés par l'appelant.
#pragma once
#include <cmath>
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
    POINT p{0, 0};
    p.x = static_cast<LONG>(std::lround(
              static_cast<double>(screenW) * (worldX + spriteW / 2.0) / refW)) - spriteW / 2;
    p.y = static_cast<LONG>(std::lround(
              static_cast<double>(screenH) * (worldY + spriteH / 2.0) / refH)) - spriteH / 2;
    return p;
}

} // namespace ts2::ui

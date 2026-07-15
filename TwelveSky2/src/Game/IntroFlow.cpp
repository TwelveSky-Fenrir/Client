// Game/IntroFlow.cpp — cf. IntroFlow.h pour le détail du flux découvert (Scene_IntroUpdate
// 0x517FE0). Reproduction directe du switch(this[1]) désassemblé : chaque sous-état est un
// simple "incrémente le compteur, si seuil atteint alors avance au sous-état suivant et
// remets le compteur à 0", à trois seuils distincts (90 / 3*33 / 90). Généralisé ici en une
// poignée de branches équivalentes (l'original répète le même motif 35 fois, un par
// `case`, sans aucune variation de logique — cf. les 33 cases identiques 0x1..0x21 dans le
// désassemblage).
#include "Game/IntroFlow.h"

namespace ts2::game {

bool UpdateIntro(IntroState& state, float /*dt*/, const IntroHost& host) {
    // Sous-état 0 : attente initiale avant le cycle de logos (0x518019..0x51807a).
    if (state.subState == 0) {
        ++state.frameCounter;
        if (state.frameCounter >= kIntroWaitFrames) {
            // Franchissement 0->1 dans l'original (0x51802c..0x518081) :
            //   Util_SetClampedU8Field(&dword_8E714C, 0) + UI_FocusEditBox(&g_UIEditBoxMgr, 0)
            // -> effets de bord ts2::ui, hors périmètre ; exposés via le host (cf. IntroFlow.h).
            if (host.OnLogoSequenceBegin) host.OnLogoSequenceBegin();

            // Mise à zéro du tampon 150 dw (this[3..152], 0x518042..0x51806b), fidèle à
            // l'original — bien que ce tampon ne soit jamais relu (Scene_IntroRender ne le lit pas).
            state.logoFade.fill(0);

            state.subState     = 1;
            state.frameCounter = 0;
        }
        return false;
    }

    // Sous-états 1..33 : micro-états séquentiels de 3 frames chacun (0x518092..0x5187b0
    // dans l'original — 33 `case` identiques, chacun avance simplement subState+1).
    if (state.subState >= 1 && state.subState <= kIntroLogoStepCount) {
        ++state.frameCounter;
        if (state.frameCounter >= kIntroLogoStepFrames) {
            ++state.subState;
            state.frameCounter = 0;
        }
        return false;
    }

    // Sous-état 34 (0x22) : maintien final avant transition (0x5187be..0x5187df).
    if (state.subState == kIntroFinalSubState) {
        ++state.frameCounter;
        if (state.frameCounter >= kIntroFinalHoldFrames) {
            // Transition vers ServerSelect (*a1 = 2 dans l'original) + auto-réinitialisation
            // (a1[1]=0, a1[2]=0) permettant un replay fidèle si la scène Intro est
            // re-sélectionnée. NB : l'original NE re-zéro PAS logoFade ici (seul le
            // franchissement 0->1 le fait) — fidèlement reproduit (pas de fill() ici).
            state.subState     = 0;
            state.frameCounter = 0;
            return true;
        }
        return false;
    }

    // Sous-état hors plage (0..34) : ne devrait jamais survenir en usage normal (mirroir du
    // `default: return result;` de l'original, qui ne fait rien). Défense en profondeur
    // uniquement — ne fait pas partie du flux réel observé.
    return false;
}

} // namespace ts2::game

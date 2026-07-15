// Game/IntroFlow.h — Machine d'état de la scène Intro (Scene_IntroUpdate 0x517FE0).
//
// Réécriture C++ FIDÈLE de l'automate décompilé (re-vérifié 2026-07-15 sur décompilation
// FRAÎCHE de Scene_IntroUpdate 0x517FE0, appelée depuis cSceneMgr_Update 0x517BF0 quand
// this->scene_ == 1). Automate séquentiel à 35 sous-états (this[1] = 0..34, this[2] =
// compteur de frames du sous-état ; this[3..152] = tampon 150 dw à l'offset +12) à PAS
// FIXE : le paramètre float (a2/a3 du __fastcall d'origine) n'est JAMAIS lu ; chaque appel
// = un tick, incrémente this[2] une fois. Cohérent avec le tick 30 FPS du moteur
// (flt_815188 = 0.033333, cf. CLAUDE.md), PAS un automate piloté par le temps écoulé.
// AUCUNE entrée clavier/souris n'est testée : la séquence ne peut PAS être « skippée »
// depuis cette fonction — un éventuel skip serait géré ailleurs (WndProc ?), hors périmètre.
//
// Séquence réelle vérifiée (tick = 1/30 s @ 30 FPS ; seuils EXACTS du switch(this[1]),
// 35 cases 0x0..0x22) :
//   sous-état 0            : attente de kIntroWaitFrames=90 frames (0x5A, 3,0 s), écran
//                             noir / fin d'INTRO.AVI. Au seuil (0x51802a..0x518081) : deux
//                             effets de bord ts2::ui hors périmètre (cf.
//                             IntroHost::OnLogoSequenceBegin) + mise à zéro du tampon
//                             this[3..152] (150 dw, 0x518042..6b), puis this[1]=1, this[2]=0.
//   sous-états 1..33        : kIntroLogoStepCount=33 micro-états SÉQUENTIELS de
//                             kIntroLogoStepFrames=3 frames (0,1 s) chacun (cases 0x1..0x21,
//                             toutes identiques) -> 99 frames (3,3 s). Fait avancer this[1]
//                             de 1 à chaque palier. C'est CE this[1] que lit Scene_IntroRender
//                             0x518880 pour choisir le sprite logo (slot clamp(this[1]+797,
//                             830), cf. UI/IntroRender.h) : défilement DISCRET de sprites,
//                             PAS de fondu alpha.
//   sous-état 34 (0x22)     : maintien final de kIntroFinalHoldFrames=90 frames (0x5A, 3,0 s).
//                             Au seuil (0x5187be..0x5187df) : transition -> ServerSelect
//                             (*a1 = 2) ET l'automate se réinitialise (this[1]=0, this[2]=0)
//                             — reproduit ici en réinitialisant IntroState pour permettre un
//                             replay fidèle si la scène Intro est re-sélectionnée.
//
// Durée totale FIDÈLE : 90 + 33*3 + 90 = 279 frames = 9,3 s @ 30 FPS. SceneManager pilote
// cet automate via UpdateIntro() ci-dessous (le socle « ~90 frames » d'une version antérieure
// est corrigé — plus aucune valeur inventée).
//
// Le tampon this[3..152] (logoFade) est mis à zéro au franchissement 0->1 UNIQUEMENT, puis
// JAMAIS relu : la décompilation fraîche de Scene_IntroRender 0x518880 ne le référence pas
// (elle ne lit que this[1]). Rôle indéterminé (vestige) ; conservé pour la fidélité de
// layout, PAS pour un fondu — la spéculation « fondu de logos » d'anciennes sessions est
// infirmée (le rendu n'a NI alpha NI lecture de ce tampon).
//
// Effets de bord au franchissement 0->1 (0x51802c..0x51803d), hors périmètre ts2::game,
// exposés via IntroHost (callback optionnel, no-op par défaut ; cf. IntroHost plus bas) :
//   - Util_SetClampedU8Field(&dword_8E714C, 0) : écrit 0 dans un champ u8-borné global lu
//     par de nombreux renderers UI — rôle métier précis incertain (157 appelants).
//   - UI_FocusEditBox(&g_UIEditBoxMgr, 0) : relâche le focus clavier de tout EDIT natif
//     (index 0 = "jeu") — remet le clavier au jeu avant les scènes suivantes.
//
// Autonomie : ce module n'inclut que la STL — aucune dépendance à SceneManager (câblage
// laissé à l'appelant, cf. mission), ni à GameState.h/ClientRuntime.h (non nécessaires ici).
#pragma once
#include <array>
#include <cstdint>
#include <functional>

namespace ts2::game {

// ---------------------------------------------------------------------------
// Constantes du flux réel (valeurs extraites du désassemblage, PAS arbitraires).
// ---------------------------------------------------------------------------
constexpr int kIntroWaitFrames      = 90; // 0x5A — sous-état 0 : attente initiale (3,0 s @ 30 FPS)
constexpr int kIntroLogoStepFrames  = 3;  // durée de chaque micro-état de logo (0,1 s @ 30 FPS)
constexpr int kIntroLogoStepCount   = 33; // 0x21 — nombre de micro-états séquentiels (sous-états 1..33)
constexpr int kIntroFinalHoldFrames = 90; // 0x5A — sous-état 34 (0x22) : maintien final (3,0 s @ 30 FPS)
constexpr int kIntroFadeSlotCount   = 150; // 0x96 — taille du tampon this[3..152] (offset +12)

// Sous-état atteint après les 33 micro-états de logo (dernier maintien avant transition).
constexpr int kIntroFinalSubState = kIntroLogoStepCount + 1; // 34 (0x22)

// Nombre total de frames de la séquence complète (référence/diagnostic).
constexpr int kIntroTotalFrames =
    kIntroWaitFrames + kIntroLogoStepCount * kIntroLogoStepFrames + kIntroFinalHoldFrames; // 279

// ---------------------------------------------------------------------------
// État de l'automate Intro — mirroir direct des champs de l'objet cSceneMgr d'origine
// utilisés par Scene_IntroUpdate (offsets +4/+8/+12, cf. commentaire d'en-tête de
// SceneManager.h : "[+0 id][+4 sous-état][+8 compteur de frames][+12 tampon 150 dw]").
// Ne PAS coupler à Scene/SceneManager.h : struct autonome, à copier/brancher par l'appelant.
// ---------------------------------------------------------------------------
struct IntroState {
    int subState     = 0; // mirroir this[1] (0..34)
    int frameCounter = 0; // mirroir this[2] (compteur de frames dans le sous-état courant)

    // Mirroir this[3..152] (150 dwords, offset +12). Mis à zéro par l'original au
    // franchissement 0->1 UNIQUEMENT (PAS re-zéroé au bouclage 34->0), puis JAMAIS relu :
    // Scene_IntroRender 0x518880 ne référence pas ce tampon (décompilation fraîche vérifiée).
    // Rôle indéterminé (vestige) ; conservé pour la fidélité de layout/branchement.
    std::array<int32_t, kIntroFadeSlotCount> logoFade{};
};

// ---------------------------------------------------------------------------
// Effets de bord hors périmètre ts2::game, exposés en callback optionnel pour fidélité
// (cf. commentaire d'en-tête). Aucun défaut ne fait rien d'observable (no-op) : la scène
// avance identiquement que le host soit branché ou non.
// ---------------------------------------------------------------------------
struct IntroHost {
    // Appelé UNE fois, au franchissement du sous-état 0 -> 1 (0x51802c..0x51803d dans
    // l'original). Reproduit Util_SetClampedU8Field(&dword_8E714C, 0) + UI_FocusEditBox(
    // &g_UIEditBoxMgr, 0) côté ts2::ui — à brancher par l'appelant si l'écran Intro affiche
    // déjà de l'UI.
    std::function<void()> OnLogoSequenceBegin;
};

// ---------------------------------------------------------------------------
// UpdateIntro — avance l'automate Intro d'une frame (fonction pure, pas d'état global).
//
// `dt` est conservé dans la signature pour l'API mais N'EST PAS UTILISÉ pour compter :
// fidèle à l'original, qui ignore totalement son paramètre dt/float et incrémente ses
// compteurs une fois par APPEL (implicitement 1 appel = 1 tick 30 FPS, cf. cSceneMgr_Update
// 0x517BF0 appelé depuis la boucle de jeu à pas fixe). À appeler exactement une fois par
// frame de jeu pour reproduire la durée réelle.
//
// `host` : effets de bord optionnels hors périmètre (cf. IntroHost). Peut être omis
// (IntroHost{} par défaut = aucun effet observable).
//
// Retour : true UNE SEULE FOIS, sur la frame où le sous-état final (34) atteint son seuil
// de kIntroFinalHoldFrames — c'est le signal "transition vers ServerSelect prête" (mirroir
// exact de l'instruction *a1 = 2 de l'original). `state` est alors réinitialisé
// (subState=0, frameCounter=0) pour permettre un replay fidèle si jamais re-sélectionné,
// exactement comme le fait l'original (a1[1]=0, a1[2]=0 juste après avoir écrit *a1=2).
// Retourne false sur toutes les autres frames.
bool UpdateIntro(IntroState& state, float dt, const IntroHost& host = IntroHost{});

} // namespace ts2::game

// UI/MinimapWidget.h — mini-carte du HUD en jeu (§12 de Docs/TS2_UI_GAMEHUD_RENDER.md).
//
// Ancre binaire : bloc mini-carte de UI_GameHud_Render 0x67A3C0, EA 0x681230-0x683934
// (bouton bascule taille 0x6815BD-0x6816E6, fenêtre visible/clamp 0x6818CD-0x681D70,
// marqueurs 0x681D70-0x683876). Pas un ts2::ui::Dialog (toujours affichée, hors
// chaîne de routage UIManager) — même statut que la barre de quickslots de GameHud.
//
// Widget SANS ressources GPU propres : il dessine à travers le SpriteBatch/Font/
// texture blanche déjà possédés par l'appelant (GameHud), exactement comme
// GameHud::DrawVitalsFrame()/DrawQuickSlotFrames() dessinent dans le même lot —
// c'est le pattern demandé par la mission (« Layout struct + DrawFilledRect/
// DrawBarFill/GpuTexture », ici transposé sans doublon de device/texture).
//
// Fidélité vs simplifications assumées (RE statique disponible mais dimensions/
// tables runtime non lisibles statiquement, cf. bandeau GameHud.cpp) :
//   - §12a Deux tailles (this+612) : petite/grande -> bigMode_ ci-dessous.
//   - §12b Trois modes de fenêtre visible (this+616, switch 0/1/2) : modélisés par
//     MinimapWindowMode {Full, ClampedCenter, Free}. Bornes monde réelles
//     (dword_14A88C8, bbox 6 floats par map) et échelles par mode
//     (dword_14A906C/dword_14A9070) NON chargées dans ce modèle (aucune table de
//     bbox de map dans Game/GameState.h) -> remplacées par un rayon de vue monde
//     fixe (kWorldViewRadius, TODO ci-dessous), assumption documentée au .cpp.
//   - §12c Marqueurs : joueurs (game::g_World.players, hors self) et monstres
//     (game::g_World.monsters) projetés et clampés/omis selon le mode courant.
//     PNJ (§12c « NPC ») : la doc confirme que l'original lit `dword_1764D18`/
//     `g_NpcRenderArray` — PAS le tableau gameplay réseau `dword_17AB534`
//     (`game::NpcEntity`/`g_World.npcs`, désormais positionné via body+16/20/24
//     mais utilisé UNIQUEMENT pour l'interaction/ciblage, cf. commentaire
//     NpcEntity dans Game/GameState.h et Docs/TS2_NPC_MESH_DRAW.md). L'équivalent
//     client-source de `g_NpcRenderArray` est `game::ZoneNpcs()` (Game/
//     StaticNpcLoader.h, déjà peuplé avec x/y/z réelles et câblé sur
//     OnSpawnCharacter(self)) -> désormais projeté ci-dessous. Icônes par type
//     (§12c : 5 variantes selon +1312) non modélisées, faute de sémantique
//     confirmée pour `NpcDefRecord::fieldB` -> un point de couleur unique
//     (kNpcDotColor), même simplification que les monstres/joueurs distants.
//     Groupe/alliance : aucun roster d'alliance côté GameState
//     (g_AllianceRosterNames de l'original non repris) -> non projeté (TODO,
//     repli propre : le reste du HUD continue de fonctionner).
//   - Clignotement de surbrillance de quête (§9/§12c, formule d'origine
//     `Crt_ftol(g_GameTimeSec*2)%2==1`) : mécanisme câblé fidèlement
//     (SetQuestHighlightMonster/ClearQuestHighlight) mais INERTE par défaut,
//     faute de système de quête alimentant Game/GameState.h dans cette passe.
#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>

#include "Gfx/SpriteBatch.h"
#include "Gfx/Font.h"
#include "Game/GameState.h"
#include "Core/Types.h"

namespace ts2::ui {

// Rectangle écran simple (même forme que GameHud::HudRect, dupliqué ici pour ne
// pas coupler MinimapWidget.h à GameHud.h — voir bandeau ci-dessus).
struct MmRect {
    int x = 0, y = 0, w = 0, h = 0;
    bool Contains(int px, int py) const {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
};

// Mode de fenêtre visible de la mini-carte (this+616 dans le binaire, §12b) :
//   Full          (0) : « plein » — clamp de fenêtre 145x128 (formule v614).
//   ClampedCenter (1) : « centré-clampé » — même clamp, formule source différente
//                       (v615) mais résultat visuel équivalent au binaire (doc
//                       §12b : « redondant/vestige de refactor »).
//   Free          (2) : « libre » — pas de clamp, blit direct plein cadre.
enum class MinimapWindowMode : uint8_t { Full = 0, ClampedCenter = 1, Free = 2 };

class MinimapWidget {
public:
    MinimapWidget() = default;

    // Calcule le layout pour ces dimensions écran (cGameHud n'ayant pas de hook
    // de resize dynamique, appelé une fois depuis GameHud::Init — même
    // limitation que le reste du HUD).
    void Init(int screenW, int screenH);
    void OnScreenResize(int screenW, int screenH) { Init(screenW, screenH); }

    // Bascule petit/grand (bouton §12a). SetBigMode permet un câblage externe
    // (ex. touche raccourcie) en plus du hit-test interne d'OnMouseDown.
    void ToggleSize();
    void SetBigMode(bool big);
    bool BigMode() const { return bigMode_; }

    // Mode de fenêtre visible (§12b). Défaut : ClampedCenter (comportement de
    // jeu normal ; Free est réservé aux cartes spéciales dans l'original).
    void SetWindowMode(MinimapWindowMode mode) { windowMode_ = mode; }
    MinimapWindowMode WindowMode() const { return windowMode_; }

    // Point d'accroche pour un futur système de quête (§9/§12c) : le monstre
    // désigné clignote sur la mini-carte selon la formule d'origine. Aucun
    // appelant ne branche ceci pour l'instant (TODO, cf. bandeau de tête).
    void SetQuestHighlightMonster(game::EntityId id) { questHighlightMonster_ = id; }
    void ClearQuestHighlight() { questHighlightMonster_ = game::EntityId{}; }

    // Hit-test + bascule taille (« premier consommateur gagne », appelé par
    // GameHud::OnMouseDown avant le clic générique HUD). Renvoie true si
    // l'événement est consommé (bouton bascule OU clic dans le panneau).
    bool OnMouseDown(int x, int y);

    // Passe « panneaux » : fond + viewport + points (self/joueurs/monstres).
    // À appeler DANS un sprite.Begin()/End() déjà ouvert par l'appelant (voir
    // GameHud::Render — même sprite_/white_ que le reste du HUD).
    void DrawPanels(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex);

    // Passe texte : libellé du bouton bascule + (mode grand) nom de zone et
    // coordonnées joueur. À appeler DANS un font.BeginBatch()/EndBatch().
    void DrawText(gfx::Font& font);

private:
    struct Layout {
        MmRect frame;     // panneau complet (fond §12a)
        MmRect viewport;  // zone de projection des marqueurs (« carte »)
        MmRect toggleBtn; // bouton bascule taille (§12a)
    };

    void RecomputeLayout();

    static void DrawFilledRect(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                               const MmRect& r, D3DCOLOR color);
    static void DrawBorderRect(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                               const MmRect& r, int thickness, D3DCOLOR color);
    static void DrawDot(gfx::SpriteBatch& sprite, IDirect3DTexture9* whiteTex,
                        int cx, int cy, int size, D3DCOLOR color);

    // Projette une position monde (wx,wz) relative au joueur local (selfX,selfZ)
    // dans le viewport courant. Renvoie false si le point doit être omis (mode
    // Free hors cadre) ; sinon écrit la position écran (clampée aux bords en
    // mode Full/ClampedCenter, comme un indicateur radar). Plan horizontal
    // assumé = (x,z) — cf. Game/EntityDrawLogic.cpp qui utilise déjà (x,y,z)
    // avec y = hauteur (offset +2.5f de l'œil), x/z = sol.
    bool ProjectToViewport(float wx, float wz, float selfX, float selfZ,
                           int& outX, int& outY) const;

    int  screenW_ = ts2::kRefWidth;
    int  screenH_ = ts2::kRefHeight;
    bool bigMode_ = false;                                              // this+612
    MinimapWindowMode windowMode_ = MinimapWindowMode::ClampedCenter;   // this+616

    game::EntityId questHighlightMonster_{}; // mécanisme de clignotement (inerte)

    Layout layout_{};
};

} // namespace ts2::ui
